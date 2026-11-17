/*
* Copyright (c) 2026, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

// Exercises the GPU-resident scene, see documentation/GpuResidentScene.md.
// Run it with rtx.gpuResidentScene = True, for example from an rtx.conf next to the executable.
// meson.build builds one executable per scenario from this file, and GPU_RESIDENT_SCENE_SCENARIO picks it:
// - Basic (the default): a grid of retained instances, retained instances that move every frame,
//   frame-local instances drawn every frame, a camera medium for the first half of the run,
//   and one material whose color UpdateMaterial changes halfway through.
// - Medium: the basic scene with the camera medium on for the whole run.
// - Textures: textured materials, alpha-tested and alpha-blended instances, a translucent material,
//   and a texture shared by two materials, one of which is destroyed and re-created with another texture halfway through.
// - Lifecycle: instances, meshes and materials that are destroyed and re-created,
//   and calls with destroyed or invalid handles.
// The app checks the results that the API returns right away, and exits with a failure when one differs.

#include <remix/remix.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef GPU_RESIDENT_SCENE_SCENARIO
#define GPU_RESIDENT_SCENE_SCENARIO Basic
#endif

namespace {

  struct Vec3 {
    float x;
    float y;
    float z;
  };

  Vec3 operator+(const Vec3& a, const Vec3& b) {
    return { a.x + b.x, a.y + b.y, a.z + b.z };
  }

  Vec3 operator-(const Vec3& a, const Vec3& b) {
    return { a.x - b.x, a.y - b.y, a.z - b.z };
  }

  Vec3 operator*(const Vec3& a, float s) {
    return { a.x * s, a.y * s, a.z * s };
  }

  Vec3 cross(const Vec3& a, const Vec3& b) {
    return {
      a.y * b.z - a.z * b.y,
      a.z * b.x - a.x * b.z,
      a.x * b.y - a.y * b.x,
    };
  }

  Vec3 normalize(const Vec3& value) {
    const float length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
    return { value.x / length, value.y / length, value.z / length };
  }

  remixapi_Float3D toFloat3D(const Vec3& value) {
    return { value.x, value.y, value.z };
  }

  enum class Scenario {
    Basic,
    Medium,
    Textures,
    Lifecycle,
  };

  constexpr Scenario kScenario = Scenario::GPU_RESIDENT_SCENE_SCENARIO;

  // 64 x 64 retained instances of a two-surface mesh use 8,192 instance surfaces.
  constexpr int kGridSize = 64;
  constexpr int kMovingInstanceCount = 8;
  constexpr int kFrameLocalInstanceCount = 128;
  constexpr float kTwoPi = 6.2831853f;

  // Frees finish after the GPU has finished the frame that freed them, so a slot is reused only after a few frames.
  constexpr int kSlotReuseFrames = 12;

  std::unique_ptr<remix::Interface> g_remix;
  std::filesystem::path g_textureDir;
  std::vector<remixapi_MaterialHandle> g_materials;
  std::vector<remixapi_MeshHandle> g_meshes;
  std::vector<remixapi_SceneInstanceHandle> g_retainedInstances;
  std::vector<remixapi_SceneInstanceHandle> g_movingInstances;

  remixapi_MaterialHandle g_cameraMediumMaterial = nullptr;
  remixapi_MaterialHandle g_updatedMaterial = nullptr;
  remixapi_MeshHandle g_floorMesh = nullptr;
  remixapi_MeshHandle g_blockMesh = nullptr;
  remixapi_MeshHandle g_markerMesh = nullptr;

  template<typename T>
  T check(remix::Result< T >&& result, const char* what) {
    if (!result) {
      throw std::runtime_error(std::string(what) + " failed " + std::to_string(result.status()));
    }
    return result.value();
  }

  void check(remix::Result< void >&& result, const char* what) {
    if (!result) {
      throw std::runtime_error(std::string(what) + " failed " + std::to_string(result.status()));
    }
  }

  void expectStatus(remixapi_ErrorCode status, remixapi_ErrorCode expected, const char* what) {
    if (status != expected) {
      throw std::runtime_error(std::string(what) + " returned " + std::to_string(status)
                               + " instead of " + std::to_string(expected));
    }
  }

  void expect(bool condition, const char* what) {
    if (!condition) {
      throw std::runtime_error(what);
    }
  }

  // API handles pack a slot's index and generation, see Implementation details in documentation/GpuResidentScene.md.
  template<typename Handle>
  uint32_t slotOf(Handle handle) {
    return static_cast<uint32_t>(reinterpret_cast<uint64_t>(handle));
  }

  remixapi_HardcodedVertex makeVertex(const Vec3& position, const Vec3& normal, float u, float v) {
    return {
      .position = { position.x, position.y, position.z },
      .normal = { normal.x, normal.y, normal.z },
      .texcoord = { u, v },
      .color = 0xFFFFFFFF,
    };
  }


  enum class TextureFormat : uint32_t {
    Rgba8Srgb = 29,
    Rgba8Unorm = 28,
    Rg8Unorm = 49,
    R8Unorm = 61,
  };

  // Writes a square texture with all its mips as a DDS file, as the API takes textures as files.
  // The texel function returns 8-bit channel values, and each mip keeps the average of the texels it covers.
  std::filesystem::path writeTexture(const char* name, uint32_t size, TextureFormat format,
                                     const std::function<std::array<float, 4>(uint32_t, uint32_t)>& texel) {
    const uint32_t channels = format == TextureFormat::R8Unorm ? 1 : format == TextureFormat::Rg8Unorm ? 2 : 4;
    uint32_t mips = 0;
    for (uint32_t mipSize = size; mipSize > 0; mipSize /= 2) {
      mips++;
    }
    const std::filesystem::path path = g_textureDir / name;
    std::ofstream file(path, std::ios::binary);
    const auto write = [&file](uint32_t value, uint32_t count = 1) {
      while (count--) {
        file.write(reinterpret_cast<const char*>(&value), sizeof(value));
      }
    };
    // The magic number, DDS_HEADER and DDS_HEADER_DXT10 of a 2D texture.
    write(0x20534444);            // "DDS "
    write(124);                   // Header size
    write(0x2100F);               // Flags: caps, height, width, pitch, pixel format, mip count
    write(size);                  // Height
    write(size);                  // Width
    write(size * channels);       // Pitch
    write(0);                     // Depth
    write(mips);                  // Mip count
    write(0, 11);                 // Reserved
    write(32);                    // Pixel format size
    write(0x4);                   // Pixel format flags: four character code
    write(0x30315844);            // "DX10"
    write(0, 5);                  // Bit count and masks
    write(0x401008);              // Caps: texture, mipmap, complex
    write(0, 4);                  // Caps 2 to 4 and reserved
    write(static_cast<uint32_t>(format));
    write(3);                     // Texture 2D
    write(0);                     // Misc flags
    write(1);                     // Array size
    write(0);                     // Misc flags 2
    for (uint32_t mipSize = size; mipSize > 0; mipSize /= 2) {
      const uint32_t block = size / mipSize;
      for (uint32_t i = 0; i < mipSize * mipSize; i++) {
        std::array<float, 4> sum = {};
        for (uint32_t j = 0; j < block * block; j++) {
          const std::array<float, 4> value = texel(i % mipSize * block + j % block, i / mipSize * block + j / block);
          for (uint32_t c = 0; c < 4; c++) {
            sum[c] += value[c];
          }
        }
        uint8_t bytes[4];
        for (uint32_t c = 0; c < channels; c++) {
          bytes[c] = static_cast<uint8_t>(std::clamp(sum[c] / float(block * block) + 0.5f, 0.0f, 255.0f));
        }
        file.write(reinterpret_cast<const char*>(bytes), channels);
      }
    }
    return path;
  }


  remix::MaterialInfo makeOpaqueMaterialInfo(
      remix::MaterialInfoOpaqueEXT& opaque,
      remixapi_Float3D albedo,
      remixapi_Float3D emissiveColor,
      float emissiveIntensity) {
    opaque.albedoConstant = albedo;
    opaque.opacityConstant = 1.0f;
    opaque.roughnessConstant = 0.6f;
    opaque.metallicConstant = 0.0f;

    remix::MaterialInfo info;
    info.pNext = &opaque;
    info.emissiveColorConstant = emissiveColor;
    info.emissiveIntensity = emissiveIntensity;
    return info;
  }

  remixapi_MaterialHandle createMaterial(const remixapi_MaterialInfo& info, const char* what) {
    const remixapi_MaterialHandle material = check(g_remix->CreateMaterial(info), what);
    g_materials.push_back(material);
    return material;
  }

  remixapi_MaterialHandle createOpaqueMaterial(
      remixapi_Float3D albedo,
      remixapi_Float3D emissiveColor = { 0.0f, 0.0f, 0.0f },
      float emissiveIntensity = 0.0f) {
    remix::MaterialInfoOpaqueEXT opaque;
    const remix::MaterialInfo info = makeOpaqueMaterialInfo(opaque, albedo, emissiveColor, emissiveIntensity);
    return createMaterial(info, "CreateMaterial(opaque)");
  }

  remixapi_MaterialHandle createTranslucentMaterial(float refractiveIndex, remixapi_Float3D transmittanceColor, float measurementDistance) {
    remix::MaterialInfoTranslucentEXT translucent;
    translucent.refractiveIndex = refractiveIndex;
    translucent.transmittanceColor = transmittanceColor;
    translucent.transmittanceMeasurementDistance = measurementDistance;
    translucent.useDiffuseLayer = false;
    translucent.set_thinWallThickness(std::nullopt);

    remix::MaterialInfo info;
    info.pNext = &translucent;
    return createMaterial(info, "CreateMaterial(translucent)");
  }

  void destroyMaterial(remixapi_MaterialHandle material) {
    check(g_remix->DestroyMaterial(material), "DestroyMaterial");
    std::erase(g_materials, material);
  }

  struct Quad {
    std::array<remixapi_HardcodedVertex, 4> vertices;
  };

  Quad makeQuad(const Vec3& center, const Vec3& horizontal, const Vec3& vertical) {
    const Vec3 normal = normalize(cross(horizontal, vertical));
    return { {
      makeVertex(center - horizontal - vertical, normal, 0.0f, 1.0f),
      makeVertex(center + horizontal - vertical, normal, 1.0f, 1.0f),
      makeVertex(center + horizontal + vertical, normal, 1.0f, 0.0f),
      makeVertex(center - horizontal + vertical, normal, 0.0f, 0.0f),
    } };
  }

  // A quad standing on the floor around the origin, facing the camera.
  Quad makePanel(float halfWidth, float halfHeight) {
    return makeQuad({ 0.0f, halfHeight, 0.0f }, { -halfWidth, 0.0f, 0.0f }, { 0.0f, halfHeight, 0.0f });
  }

  // Returns the mesh's surfaces, one per quad.
  std::vector<remixapi_MeshInfoSurfaceTriangles> makeSurfaces(const std::vector<Quad>& quads,
                                                              const std::vector<remixapi_MaterialHandle>& materials) {
    static constexpr std::array<uint32_t, 6> kQuadIndices = { 0, 1, 2, 0, 2, 3 };
    std::vector<remixapi_MeshInfoSurfaceTriangles> surfaces;
    for (size_t i = 0; i < quads.size(); i++) {
      surfaces.push_back({
        .vertices_values = quads[i].vertices.data(),
        .vertices_count = quads[i].vertices.size(),
        .indices_values = kQuadIndices.data(),
        .indices_count = kQuadIndices.size(),
        .skinning_hasvalue = false,
        .skinning_value = {},
        .material = materials[i],
      });
    }
    return surfaces;
  }

  remixapi_MeshInfo makeMeshInfo(const std::vector<remixapi_MeshInfoSurfaceTriangles>& surfaces) {
    return {
      .sType = REMIXAPI_STRUCT_TYPE_MESH_INFO,
      .pNext = nullptr,
      .hash = 0,
      .surfaces_values = surfaces.data(),
      .surfaces_count = static_cast<uint32_t>(surfaces.size()),
    };
  }

  // Creates a mesh with one surface per quad.
  remixapi_MeshHandle createMesh(const std::vector<Quad>& quads, const std::vector<remixapi_MaterialHandle>& materials) {
    const std::vector<remixapi_MeshInfoSurfaceTriangles> surfaces = makeSurfaces(quads, materials);
    const remixapi_MeshHandle mesh = check(g_remix->CreateMesh(makeMeshInfo(surfaces)), "CreateMesh");
    g_meshes.push_back(mesh);
    return mesh;
  }

  void destroyMesh(remixapi_MeshHandle mesh) {
    check(g_remix->DestroyMesh(mesh), "DestroyMesh");
    std::erase(g_meshes, mesh);
  }

  remixapi_InstanceInfo makeInstanceInfo(remixapi_MeshHandle mesh, const Vec3& position, float scale) {
    return {
      .sType = REMIXAPI_STRUCT_TYPE_INSTANCE_INFO,
      .pNext = nullptr,
      .categoryFlags = 0,
      .mesh = mesh,
      .transform = { {
        { scale, 0.0f, 0.0f, position.x },
        { 0.0f, scale, 0.0f, position.y },
        { 0.0f, 0.0f, scale, position.z },
      } },
      .doubleSided = true,
    };
  }

  remixapi_SceneInstanceHandle addInstance(const remixapi_InstanceInfo& info, const char* what) {
    const remixapi_SceneInstanceHandle instance = check(g_remix->SceneInstanceAdd(info), what);
    g_retainedInstances.push_back(instance);
    return instance;
  }

  void removeInstance(remixapi_SceneInstanceHandle instance) {
    check(g_remix->SceneInstanceRemove(instance), "SceneInstanceRemove");
    std::erase(g_retainedInstances, instance);
  }

  remixapi_MeshHandle createFloor(remixapi_MaterialHandle material) {
    return createMesh({ makeQuad({ 0.0f, 0.0f, 0.0f }, { 50.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -50.0f }) }, { material });
  }

  // A two-surface block: a top and a side facing the camera.
  remixapi_MeshHandle createBlock(remixapi_MaterialHandle top, remixapi_MaterialHandle side) {
    return createMesh(
      {
        makeQuad({ 0.0f, 1.0f, 0.0f }, { 0.5f, 0.0f, 0.0f }, { 0.0f, 0.0f, -0.5f }),
        makeQuad({ 0.0f, 0.5f, -0.5f }, { 0.5f, 0.0f, 0.0f }, { 0.0f, 0.5f, 0.0f }),
      },
      { top, side });
  }

  // The emissive ceiling light of the basic scene.
  remixapi_MeshHandle createCeilingLight() {
    const remixapi_MaterialHandle lightMaterial = createOpaqueMaterial({ 1.0f, 1.0f, 1.0f }, { 1.0f, 0.95f, 0.85f }, 15.0f);
    return createMesh({ makeQuad({ 0.0f, 12.0f, 20.0f }, { 6.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 6.0f }) }, { lightMaterial });
  }

  void setupCamera(uint32_t width, uint32_t height) {
    remixapi_CameraInfoParameterizedEXT parameters = {
      .sType = REMIXAPI_STRUCT_TYPE_CAMERA_INFO_PARAMETERIZED_EXT,
      .pNext = nullptr,
      .position = { 0.0f, 6.0f, -6.0f },
      .forward = toFloat3D(normalize({ 0.0f, -0.35f, 1.0f })),
      .up = toFloat3D(normalize({ 0.0f, 1.0f, 0.35f })),
      .right = { 1.0f, 0.0f, 0.0f },
      .fovYInDegrees = 70.0f,
      .aspect = static_cast<float>(width) / static_cast<float>(std::max(height, 1u)),
      .nearPlane = 0.1f,
      .farPlane = 200.0f,
    };
    const remixapi_CameraInfo cameraInfo = {
      .sType = REMIXAPI_STRUCT_TYPE_CAMERA_INFO,
      .pNext = &parameters,
    };
    check(g_remix->SetupCamera(cameraInfo), "SetupCamera");
  }


  // The basic and medium scenarios.
  namespace basic {

    Vec3 movingInstancePosition(int index, int frameIndex) {
      const float angle = 0.05f * static_cast<float>(frameIndex) + kTwoPi * static_cast<float>(index) / kMovingInstanceCount;
      return { 4.0f * std::cos(angle), 1.5f, 14.0f + 4.0f * std::sin(angle) };
    }

    void create() {
      g_cameraMediumMaterial = createTranslucentMaterial(1.33f, { 0.2f, 0.6f, 0.95f }, 30.0f);
      const remixapi_MaterialHandle floorMaterial = createOpaqueMaterial({ 0.6f, 0.6f, 0.6f });
      const remixapi_MaterialHandle topMaterial = createOpaqueMaterial({ 0.9f, 0.4f, 0.1f });
      const remixapi_MaterialHandle sideMaterial = createOpaqueMaterial({ 0.1f, 0.4f, 0.9f });
      const remixapi_MaterialHandle lightMaterial = createOpaqueMaterial({ 1.0f, 1.0f, 1.0f }, { 1.0f, 0.95f, 0.85f }, 15.0f);
      g_updatedMaterial = createOpaqueMaterial({ 0.1f, 0.9f, 0.1f });

      g_floorMesh = createFloor(floorMaterial);
      g_blockMesh = createBlock(topMaterial, sideMaterial);
      g_markerMesh = createMesh(
        { makeQuad({ 0.0f, 0.0f, 0.0f }, { 0.5f, 0.0f, 0.0f }, { 0.0f, 0.5f, 0.0f }) },
        { g_updatedMaterial });
      const remixapi_MeshHandle lightMesh = createMesh(
        { makeQuad({ 0.0f, 12.0f, 20.0f }, { 6.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 6.0f }) },
        { lightMaterial });

      addInstance(makeInstanceInfo(g_floorMesh, { 0.0f, 0.0f, 0.0f }, 1.0f), "SceneInstanceAdd(floor)");
      addInstance(makeInstanceInfo(lightMesh, { 0.0f, 0.0f, 0.0f }, 1.0f), "SceneInstanceAdd(light)");
      for (int z = 0; z < kGridSize; z++) {
        for (int x = 0; x < kGridSize; x++) {
          const Vec3 position = { (x - kGridSize / 2) * 0.6f, 0.0f, 8.0f + z * 0.6f };
          addInstance(makeInstanceInfo(g_blockMesh, position, 0.4f), "SceneInstanceAdd(grid)");
        }
      }
      for (int i = 0; i < kMovingInstanceCount; i++) {
        g_movingInstances.push_back(check(g_remix->SceneInstanceAdd(makeInstanceInfo(g_blockMesh, movingInstancePosition(i, 0), 1.0f)), "SceneInstanceAdd(moving)"));
      }
    }

    void render(int frameIndex, int halfRunFrame, bool mediumForWholeRun) {
      const bool firstHalf = frameIndex < halfRunFrame;
      check(g_remix->SetCameraMediumMaterial(firstHalf || mediumForWholeRun ? g_cameraMediumMaterial : nullptr), "SetCameraMediumMaterial");

      if (frameIndex == halfRunFrame) {
        remix::MaterialInfoOpaqueEXT opaque;
        const remix::MaterialInfo info = makeOpaqueMaterialInfo(opaque, { 0.9f, 0.1f, 0.6f }, { 0.0f, 0.0f, 0.0f }, 0.0f);
        check(g_remix->UpdateMaterial(g_updatedMaterial, info), "UpdateMaterial");
      }

      for (int i = 0; i < kMovingInstanceCount; i++) {
        const remixapi_InstanceInfo info = makeInstanceInfo(g_blockMesh, movingInstancePosition(i, frameIndex), 1.0f);
        check(g_remix->SceneInstanceUpdate(g_movingInstances[i], info), "SceneInstanceUpdate");
      }

      // Frame-local instances get new surface slots every frame.
      for (int i = 0; i < kFrameLocalInstanceCount; i++) {
        const float angle = kTwoPi * static_cast<float>(i) / kFrameLocalInstanceCount;
        const Vec3 position = Vec3 { std::cos(angle), 0.0f, std::sin(angle) } * 7.0f + Vec3 { 0.0f, 3.0f, 14.0f };
        check(g_remix->DrawInstance(makeInstanceInfo(g_markerMesh, position, 0.5f)), "DrawInstance");
      }
    }

  }


  // A row of panels standing on the floor, one for each kind of textured material.
  namespace textures {

    constexpr uint32_t kTextureSize = 64;
    constexpr float kPanelZ = 5.0f;
    constexpr float kPanelSpacing = 2.5f;

    remixapi_MaterialHandle g_checkerMaterial = nullptr;
    remixapi_MeshHandle g_checkerPanel = nullptr;
    remixapi_SceneInstanceHandle g_checkerInstance = nullptr;
    std::filesystem::path g_stripesPath;

    Vec3 panelPosition(int index) {
      return { (static_cast<float>(index) - 3.0f) * kPanelSpacing, 0.0f, kPanelZ };
    }

    bool checker(uint32_t x, uint32_t y, uint32_t cell) {
      return ((x / cell) + (y / cell)) % 2 == 0;
    }

    // Encodes a tangent-space normal as unsigned hemisphere octahedral, which is how the runtime decodes normal textures.
    std::array<float, 4> encodeNormal(Vec3 normal) {
      normal = normalize(normal);
      const float sum = std::abs(normal.x) + std::abs(normal.y) + normal.z;
      const float px = normal.x / sum;
      const float py = normal.y / sum;
      return { ((px + py) * 0.5f + 0.5f) * 255.0f, ((px - py) * 0.5f + 0.5f) * 255.0f, 0.0f, 0.0f };
    }

    remixapi_MaterialHandle createTexturedMaterial(const std::filesystem::path& albedo, float roughness) {
      remix::MaterialInfoOpaqueEXT opaque;
      opaque.roughnessConstant = roughness;
      remix::MaterialInfo info;
      info.pNext = &opaque;
      info.set_albedoTexture(albedo);
      return createMaterial(info, "CreateMaterial(albedo texture)");
    }

    remixapi_SceneInstanceHandle addPanel(remixapi_MeshHandle mesh, int index, const remixapi_InstanceInfoBlendEXT* blend = nullptr) {
      remixapi_InstanceInfo info = makeInstanceInfo(mesh, panelPosition(index), 1.0f);
      info.pNext = const_cast<remixapi_InstanceInfoBlendEXT*>(blend);
      return addInstance(info, "SceneInstanceAdd(panel)");
    }

    void create() {
      const std::filesystem::path checkerPath = writeTexture("gpu_resident_checker.dds", kTextureSize, TextureFormat::Rgba8Srgb,
        [](uint32_t x, uint32_t y) -> std::array<float, 4> {
          return checker(x, y, 8) ? std::array<float, 4> { 230, 200, 40, 255 } : std::array<float, 4> { 40, 60, 160, 255 };
        });
      g_stripesPath = writeTexture("gpu_resident_stripes.dds", kTextureSize, TextureFormat::Rgba8Srgb,
        [](uint32_t x, uint32_t) -> std::array<float, 4> {
          return (x / 8) % 2 == 0 ? std::array<float, 4> { 220, 40, 40, 255 } : std::array<float, 4> { 240, 240, 240, 255 };
        });
      const std::filesystem::path normalPath = writeTexture("gpu_resident_normal.dds", kTextureSize, TextureFormat::Rg8Unorm,
        [](uint32_t x, uint32_t y) -> std::array<float, 4> {
          // Rows of ridges along x.
          const float slope = std::sin(kTwoPi * static_cast<float>(y) / 16.0f);
          return encodeNormal({ 0.0f, slope, 1.0f });
        });
      const std::filesystem::path roughnessPath = writeTexture("gpu_resident_roughness.dds", kTextureSize, TextureFormat::R8Unorm,
        [](uint32_t x, uint32_t) -> std::array<float, 4> {
          return { static_cast<float>(x) * 4.0f, 0.0f, 0.0f, 0.0f };
        });
      const std::filesystem::path metallicPath = writeTexture("gpu_resident_metallic.dds", kTextureSize, TextureFormat::R8Unorm,
        [](uint32_t, uint32_t y) -> std::array<float, 4> {
          return { (y / 16) % 2 == 0 ? 255.0f : 0.0f, 0.0f, 0.0f, 0.0f };
        });
      const std::filesystem::path emissivePath = writeTexture("gpu_resident_emissive.dds", kTextureSize, TextureFormat::Rgba8Srgb,
        [](uint32_t x, uint32_t y) -> std::array<float, 4> {
          const float dx = static_cast<float>(x) - 31.5f;
          const float dy = static_cast<float>(y) - 31.5f;
          const bool ring = std::abs(std::sqrt(dx * dx + dy * dy) - 20.0f) < 4.0f;
          return ring ? std::array<float, 4> { 40, 255, 120, 255 } : std::array<float, 4> { 0, 0, 0, 255 };
        });
      const std::filesystem::path cutoutPath = writeTexture("gpu_resident_cutout.dds", kTextureSize, TextureFormat::Rgba8Srgb,
        [](uint32_t x, uint32_t y) -> std::array<float, 4> {
          const float dx = static_cast<float>(x % 16) - 7.5f;
          const float dy = static_cast<float>(y % 16) - 7.5f;
          const bool hole = dx * dx + dy * dy < 25.0f;
          return { 200, 90, 220, hole ? 0.0f : 255.0f };
        });
      const std::filesystem::path blendPath = writeTexture("gpu_resident_blend.dds", kTextureSize, TextureFormat::Rgba8Srgb,
        [](uint32_t, uint32_t y) -> std::array<float, 4> {
          return { 255, 140, 0, (y / 16) % 2 == 0 ? 230.0f : 60.0f };
        });
      const std::filesystem::path transmittancePath = writeTexture("gpu_resident_transmittance.dds", kTextureSize, TextureFormat::Rgba8Srgb,
        [](uint32_t x, uint32_t y) -> std::array<float, 4> {
          return checker(x, y, 16) ? std::array<float, 4> { 80, 230, 230, 255 } : std::array<float, 4> { 230, 80, 230, 255 };
        });

      const remixapi_MaterialHandle floorMaterial = createOpaqueMaterial({ 0.6f, 0.6f, 0.6f });
      addInstance(makeInstanceInfo(createFloor(floorMaterial), { 0.0f, 0.0f, 0.0f }, 1.0f), "SceneInstanceAdd(floor)");
      addInstance(makeInstanceInfo(createCeilingLight(), { 0.0f, 0.0f, 0.0f }, 1.0f), "SceneInstanceAdd(light)");

      const Quad panel = makePanel(1.0f, 1.0f);

      // The checker texture is shared with a tile on the floor, whose material outlives the panel's.
      g_checkerMaterial = createTexturedMaterial(checkerPath, 0.7f);
      g_checkerPanel = createMesh({ panel }, { g_checkerMaterial });
      g_checkerInstance = addPanel(g_checkerPanel, 0);
      const remixapi_MaterialHandle tileMaterial = createTexturedMaterial(checkerPath, 0.3f);
      const remixapi_MeshHandle tile = createMesh(
        { makeQuad({ 0.0f, 0.01f, 0.0f }, { 1.5f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.5f }) }, { tileMaterial });
      addInstance(makeInstanceInfo(tile, { panelPosition(0).x, 0.0f, kPanelZ - 2.5f }, 1.0f), "SceneInstanceAdd(tile)");

      {
        remix::MaterialInfoOpaqueEXT opaque;
        opaque.albedoConstant = { 0.8f, 0.8f, 0.8f };
        opaque.roughnessConstant = 0.35f;
        remix::MaterialInfo info;
        info.pNext = &opaque;
        info.set_normalTexture(normalPath);
        addPanel(createMesh({ panel }, { createMaterial(info, "CreateMaterial(normal texture)") }), 1);
      }
      {
        remix::MaterialInfoOpaqueEXT opaque;
        opaque.albedoConstant = { 0.95f, 0.75f, 0.3f };
        opaque.set_roughnessTexture(roughnessPath);
        opaque.set_metallicTexture(metallicPath);
        remix::MaterialInfo info;
        info.pNext = &opaque;
        addPanel(createMesh({ panel }, { createMaterial(info, "CreateMaterial(roughness and metallic textures)") }), 2);
      }
      {
        remix::MaterialInfoOpaqueEXT opaque;
        opaque.albedoConstant = { 0.1f, 0.1f, 0.1f };
        remix::MaterialInfo info;
        info.pNext = &opaque;
        info.set_emissiveTexture(emissivePath);
        info.emissiveColorConstant = { 1.0f, 1.0f, 1.0f };
        info.emissiveIntensity = 4.0f;
        addPanel(createMesh({ panel }, { createMaterial(info, "CreateMaterial(emissive texture)") }), 3);
      }
      {
        // Alpha test from the instance's blend state, which selects the any-hit path.
        remix::InstanceInfoBlendEXT blend;
        blend.alphaTestEnabled = true;
        blend.alphaTestCompareOp = 4; // VK_COMPARE_OP_GREATER
        blend.alphaTestReferenceValue = 128;
        addPanel(createMesh({ panel }, { createTexturedMaterial(cutoutPath, 0.5f) }), 4, &blend);
      }
      {
        // Alpha blending from the instance's blend state, which puts the instance in the unordered TLAS.
        remix::InstanceInfoBlendEXT blend;
        blend.alphaBlendEnabled = true;
        blend.srcColorBlendFactor = 6; // VK_BLEND_FACTOR_SRC_ALPHA
        blend.dstColorBlendFactor = 7; // VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA
        addPanel(createMesh({ panel }, { createTexturedMaterial(blendPath, 0.5f) }), 5, &blend);
      }
      {
        remix::MaterialInfoTranslucentEXT translucent;
        translucent.refractiveIndex = 1.5f;
        translucent.transmittanceColor = { 1.0f, 1.0f, 1.0f };
        translucent.transmittanceMeasurementDistance = 0.5f;
        translucent.set_thinWallThickness(0.02f);
        translucent.set_transmittanceTexture(transmittancePath);
        remix::MaterialInfo info;
        info.pNext = &translucent;
        addPanel(createMesh({ panel }, { createMaterial(info, "CreateMaterial(transmittance texture)") }), 6);
      }
    }

    // Halfway through, the checker panel is rebuilt with a material that uses another texture.
    void render(int frameIndex, int halfRunFrame) {
      if (frameIndex != halfRunFrame) {
        return;
      }
      removeInstance(g_checkerInstance);
      destroyMesh(g_checkerPanel);
      destroyMaterial(g_checkerMaterial);
      g_checkerMaterial = createTexturedMaterial(g_stripesPath, 0.7f);
      g_checkerPanel = createMesh({ makePanel(1.0f, 1.0f) }, { g_checkerMaterial });
      g_checkerInstance = addPanel(g_checkerPanel, 0);
    }

  }


  // Rows of blocks that are removed and added again, meshes and materials that are destroyed and re-created,
  // and calls with destroyed or invalid handles.
  // Each row is a mesh of its own, so that the rows can change independently.
  namespace lifecycle {

    constexpr int kRowLength = 12;

    struct Row {
      remixapi_MeshHandle mesh = nullptr;
      std::vector<remixapi_SceneInstanceHandle> instances;
    };

    Row g_removedRow;
    Row g_rebuiltMeshRow;
    Row g_deadMaterialRow;
    Row g_rebuiltMaterialRow;
    remixapi_MaterialHandle g_doomedMaterial = nullptr;
    remixapi_MaterialHandle g_rejectedUpdateMaterial = nullptr;
    remixapi_MaterialHandle g_markerMaterial = nullptr;

    // The stale handles keep the slot of an object that the test destroys.
    remixapi_SceneInstanceHandle g_staleInstance = nullptr;
    remixapi_MaterialHandle g_staleMaterial = nullptr;
    remixapi_MeshHandle g_staleMesh = nullptr;
    remixapi_SceneInstanceHandle g_reusedInstance = nullptr;
    remixapi_MeshHandle g_reusedMesh = nullptr;

    Vec3 rowPosition(int row, int column) {
      return { (static_cast<float>(column) - (kRowLength - 1) * 0.5f) * 1.2f, 0.0f, 8.0f + static_cast<float>(row) * 2.0f };
    }

    void addRow(Row& row, int rowIndex) {
      for (int column = 0; column < kRowLength; column++) {
        row.instances.push_back(addInstance(makeInstanceInfo(row.mesh, rowPosition(rowIndex, column), 0.8f), "SceneInstanceAdd(row)"));
      }
    }

    void removeRow(Row& row) {
      for (const remixapi_SceneInstanceHandle instance : row.instances) {
        removeInstance(instance);
      }
      row.instances.clear();
    }

    remixapi_MeshHandle createTower(remixapi_MaterialHandle material, float height) {
      return createMesh(
        {
          makeQuad({ 0.0f, height, 0.0f }, { 0.5f, 0.0f, 0.0f }, { 0.0f, 0.0f, -0.5f }),
          makeQuad({ 0.0f, height * 0.5f, -0.5f }, { 0.5f, 0.0f, 0.0f }, { 0.0f, height * 0.5f, 0.0f }),
        },
        { material, material });
    }

    remixapi_MeshHandle createMarker(remixapi_MaterialHandle material) {
      return createMesh({ makePanel(0.6f, 0.6f) }, { material });
    }

    void checkInvalidArguments() {
      const std::vector<remixapi_MeshInfoSurfaceTriangles> noSurfaces;
      expectStatus(g_remix->CreateMesh(makeMeshInfo(noSurfaces)).status(), REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS,
                   "CreateMesh without surfaces");
      expectStatus(g_remix->SceneInstanceAdd(makeInstanceInfo(nullptr, { 0.0f, 0.0f, 0.0f }, 1.0f)).status(),
                   REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS, "SceneInstanceAdd without a mesh");
      expectStatus(g_remix->SceneInstanceUpdate(nullptr, makeInstanceInfo(g_rebuiltMeshRow.mesh, { 0.0f, 0.0f, 0.0f }, 1.0f)).status(),
                   REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS, "SceneInstanceUpdate with a null handle");
      expectStatus(g_remix->SceneInstanceRemove(nullptr).status(), REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS,
                   "SceneInstanceRemove with a null handle");
    }

    void create() {
      const remixapi_MaterialHandle floorMaterial = createOpaqueMaterial({ 0.6f, 0.6f, 0.6f });
      addInstance(makeInstanceInfo(createFloor(floorMaterial), { 0.0f, 0.0f, 0.0f }, 1.0f), "SceneInstanceAdd(floor)");
      addInstance(makeInstanceInfo(createCeilingLight(), { 0.0f, 0.0f, 0.0f }, 1.0f), "SceneInstanceAdd(light)");

      const remixapi_MaterialHandle orange = createOpaqueMaterial({ 0.9f, 0.4f, 0.1f });
      const remixapi_MaterialHandle blue = createOpaqueMaterial({ 0.1f, 0.4f, 0.9f });
      g_doomedMaterial = createOpaqueMaterial({ 0.9f, 0.9f, 0.1f });
      g_rejectedUpdateMaterial = createOpaqueMaterial({ 0.2f, 0.8f, 0.3f });
      g_markerMaterial = createOpaqueMaterial({ 0.9f, 0.1f, 0.6f });

      // Row 0 is removed and added again, row 1's mesh is destroyed and re-created taller,
      // and rows 2 and 3 use a material that is destroyed.
      g_removedRow.mesh = createBlock(orange, blue);
      addRow(g_removedRow, 0);
      g_rebuiltMeshRow.mesh = createTower(blue, 0.5f);
      addRow(g_rebuiltMeshRow, 1);
      g_deadMaterialRow.mesh = createBlock(g_doomedMaterial, g_doomedMaterial);
      addRow(g_deadMaterialRow, 2);
      g_rebuiltMaterialRow.mesh = createBlock(g_doomedMaterial, g_doomedMaterial);
      addRow(g_rebuiltMaterialRow, 3);

      // Row 4 uses a material whose type change UpdateMaterial must reject.
      Row rejectedUpdateRow;
      rejectedUpdateRow.mesh = createBlock(g_rejectedUpdateMaterial, g_rejectedUpdateMaterial);
      addRow(rejectedUpdateRow, 4);

      checkInvalidArguments();
    }

    void removeAndDestroy() {
      const remixapi_SceneInstanceHandle removed = g_removedRow.instances.front();
      removeRow(g_removedRow);
      expectStatus(g_remix->SceneInstanceRemove(removed).status(), REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS,
                   "SceneInstanceRemove twice");

      removeRow(g_rebuiltMeshRow);
      const remixapi_MeshHandle tower = g_rebuiltMeshRow.mesh;
      destroyMesh(tower);
      expectStatus(g_remix->DestroyMesh(tower).status(), REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS, "DestroyMesh twice");

      // The rows that use the destroyed material stay in the scene, and their surfaces must not render.
      destroyMaterial(g_doomedMaterial);
      expectStatus(g_remix->DestroyMaterial(g_doomedMaterial).status(), REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS,
                   "DestroyMaterial twice");
    }

    void recreate() {
      addRow(g_removedRow, 0);

      g_rebuiltMeshRow.mesh = createTower(createOpaqueMaterial({ 0.3f, 0.9f, 0.9f }), 1.5f);
      addRow(g_rebuiltMeshRow, 1);

      removeRow(g_rebuiltMaterialRow);
      destroyMesh(g_rebuiltMaterialRow.mesh);
      const remixapi_MaterialHandle replacement = createOpaqueMaterial({ 0.6f, 0.2f, 0.9f });
      g_rebuiltMaterialRow.mesh = createBlock(replacement, replacement);
      addRow(g_rebuiltMaterialRow, 3);

      // UpdateMaterial rejects a change of the material type, so row 4 stays green and opaque.
      // The rejection may happen after the call returns, so its status is not checked.
      remix::MaterialInfoTranslucentEXT translucent;
      translucent.transmittanceColor = { 1.0f, 0.1f, 0.1f };
      remix::MaterialInfo info;
      info.pNext = &translucent;
      g_remix->UpdateMaterial(g_rejectedUpdateMaterial, info);
    }

    // Each destroyed object below is the only one freed in its frame, so its slot is the next one to be reused.
    void destroyForStaleHandles() {
      g_staleMaterial = createOpaqueMaterial({ 0.5f, 0.5f, 0.5f });
      g_staleMesh = createMarker(g_markerMaterial);
      g_staleInstance = addInstance(makeInstanceInfo(g_staleMesh, rowPosition(6, 2), 1.0f), "SceneInstanceAdd(stale)");
    }

    void freeForStaleHandles() {
      destroyMaterial(g_staleMaterial);
      removeInstance(g_staleInstance);
      destroyMesh(g_staleMesh);
    }

    void reuseStaleSlots() {
      const remixapi_MaterialHandle material = createOpaqueMaterial({ 0.95f, 0.95f, 0.2f });
      expect(slotOf(material) == slotOf(g_staleMaterial) && material != g_staleMaterial,
             "CreateMaterial did not reuse the destroyed material's slot with a new generation");
      g_reusedMesh = createMarker(material);
      expect(slotOf(g_reusedMesh) == slotOf(g_staleMesh) && g_reusedMesh != g_staleMesh,
             "CreateMesh did not reuse the destroyed mesh's slot with a new generation");
      g_reusedInstance = addInstance(makeInstanceInfo(g_reusedMesh, rowPosition(6, 9), 1.0f), "SceneInstanceAdd(reused)");
      expect(slotOf(g_reusedInstance) == slotOf(g_staleInstance) && g_reusedInstance != g_staleInstance,
             "SceneInstanceAdd did not reuse the removed instance's slot with a new generation");

      expectStatus(g_remix->SceneInstanceRemove(g_staleInstance).status(), REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS,
                   "SceneInstanceRemove with a destroyed handle");
      expectStatus(g_remix->DestroyMesh(g_staleMesh).status(), REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS,
                   "DestroyMesh with a destroyed handle");
      expectStatus(g_remix->DestroyMaterial(g_staleMaterial).status(), REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS,
                   "DestroyMaterial with a destroyed handle");

      // An instance of the destroyed mesh must not render, even though the mesh's slot is in use again.
      // Calls with destroyed handles may be ignored without an error, so the statuses below are not checked,
      // and an instance that the API accepts is removed with the others.
      auto staleMeshInstance = g_remix->SceneInstanceAdd(makeInstanceInfo(g_staleMesh, rowPosition(6, 5), 1.0f));
      if (staleMeshInstance) {
        g_retainedInstances.push_back(staleMeshInstance.value());
      }
    }

    // Calls with the destroyed handles must not change the objects that reuse their slots.
    void useStaleHandles(int frameIndex) {
      g_remix->SceneInstanceUpdate(g_staleInstance, makeInstanceInfo(g_reusedMesh, rowPosition(5, frameIndex % kRowLength), 1.0f));
      remix::MaterialInfoOpaqueEXT opaque;
      const remix::MaterialInfo info = makeOpaqueMaterialInfo(opaque, { 1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, 10.0f);
      g_remix->UpdateMaterial(g_staleMaterial, info);
    }

    // Frame numbers are fractions of the run, so that the capture frame sees every change.
    void render(int frameIndex, int runFrames) {
      const int removeFrame = runFrames / 5;
      const int recreateFrame = 2 * runFrames / 5;
      const int staleCreateFrame = recreateFrame + kSlotReuseFrames;
      const int staleFreeFrame = staleCreateFrame + kSlotReuseFrames;
      const int staleReuseFrame = staleFreeFrame + kSlotReuseFrames;
      if (frameIndex == removeFrame) {
        removeAndDestroy();
      } else if (frameIndex == recreateFrame) {
        recreate();
      } else if (frameIndex == staleCreateFrame) {
        destroyForStaleHandles();
      } else if (frameIndex == staleFreeFrame) {
        freeForStaleHandles();
      } else if (frameIndex == staleReuseFrame) {
        reuseStaleSlots();
      }
      if (frameIndex >= staleReuseFrame) {
        useStaleHandles(frameIndex);
      }
    }

  }


  void createScene() {
    switch (kScenario) {
    case Scenario::Basic:
    case Scenario::Medium:
      basic::create();
      break;
    case Scenario::Textures:
      textures::create();
      break;
    case Scenario::Lifecycle:
      lifecycle::create();
      break;
    }
  }

  void destroyScene() {
    for (const remixapi_SceneInstanceHandle instance : g_movingInstances) {
      check(g_remix->SceneInstanceRemove(instance), "SceneInstanceRemove");
    }
    for (const remixapi_SceneInstanceHandle instance : g_retainedInstances) {
      check(g_remix->SceneInstanceRemove(instance), "SceneInstanceRemove");
    }
    for (const remixapi_MeshHandle mesh : g_meshes) {
      check(g_remix->DestroyMesh(mesh), "DestroyMesh");
    }
    for (const remixapi_MaterialHandle material : g_materials) {
      check(g_remix->DestroyMaterial(material), "DestroyMaterial");
    }
    g_movingInstances.clear();
    g_retainedInstances.clear();
    g_meshes.clear();
    g_materials.clear();
  }

  void initialize(HWND hwnd) {
    const wchar_t* path = L"d3d9.dll";
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
      path = L"bin\\d3d9.dll";
    }

    auto interfaceResult = remix::lib::loadRemixDllAndInitialize(path);
    if (!interfaceResult) {
      throw std::runtime_error("loadRemixDllAndInitialize() failed " + std::to_string(interfaceResult.status()));
    }
    g_remix = std::make_unique<remix::Interface>(*interfaceResult);

    const remixapi_StartupInfo startupInfo = {
      .sType = REMIXAPI_STRUCT_TYPE_STARTUP_INFO,
      .pNext = nullptr,
      .hwnd = hwnd,
      .disableSrgbConversionForOutput = false,
      .forceNoVkSwapchain = false,
    };
    check(g_remix->Startup(startupInfo), "Startup");

    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    g_textureDir = std::filesystem::path(exePath).parent_path();

    createScene();
  }

  void render(uint32_t width, uint32_t height, int frameIndex, int halfRunFrame) {
    setupCamera(width, height);

    switch (kScenario) {
    case Scenario::Basic:
      basic::render(frameIndex, halfRunFrame, false);
      break;
    case Scenario::Medium:
      basic::render(frameIndex, halfRunFrame, true);
      break;
    case Scenario::Textures:
      textures::render(frameIndex, halfRunFrame);
      break;
    case Scenario::Lifecycle:
      lifecycle::render(frameIndex, 2 * halfRunFrame);
      break;
    }

    check(g_remix->Present(), "Present");
  }

  LRESULT WINAPI windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_DESTROY) {
      PostQuitMessage(0);
      return 0;
    }
    return DefWindowProc(hwnd, message, wParam, lParam);
  }

}

int main(int argc, char* argv[]) {
  // A frame count of zero or less runs the test until the window is closed.
  // The test runner passes -1 for --doNotAutoTerminate.
  const int captureFrames = argc >= 2 ? std::atoi(argv[1]) : 0;
  const bool runUntilClosed = captureFrames <= 0;
  constexpr int kDrainFrames = 8;
  constexpr int kInteractiveHalfRunFrame = 600;
  const int numFrames = runUntilClosed ? 0 : captureFrames + kDrainFrames;
  const int halfRunFrame = runUntilClosed ? kInteractiveHalfRunFrame : captureFrames / 2;
  const WNDCLASSEX windowClass = {
    .cbSize = sizeof(WNDCLASSEX),
    .style = CS_CLASSDC,
    .lpfnWndProc = windowProc,
    .cbClsExtra = 0L,
    .cbWndExtra = 0L,
    .hInstance = GetModuleHandle(nullptr),
    .hIcon = nullptr,
    .hCursor = nullptr,
    .hbrBackground = nullptr,
    .lpszMenuName = nullptr,
    .lpszClassName = "Remix API GPU-Resident Scene Test",
    .hIconSm = nullptr,
  };
  RegisterClassEx(&windowClass);

  constexpr DWORD kWindowStyle = WS_OVERLAPPEDWINDOW;
  RECT clientRect = { 0, 0, 900, 600 };
  AdjustWindowRect(&clientRect, kWindowStyle, false);
  HWND hwnd = CreateWindow(
    windowClass.lpszClassName,
    "Remix API GPU-Resident Scene Test",
    kWindowStyle,
    CW_USEDEFAULT,
    CW_USEDEFAULT,
    clientRect.right - clientRect.left,
    clientRect.bottom - clientRect.top,
    GetDesktopWindow(),
    nullptr,
    windowClass.hInstance,
    nullptr);

  if (hwnd == nullptr) {
    UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);
    return EXIT_FAILURE;
  }

  int exitCode = EXIT_SUCCESS;
  try {
    initialize(hwnd);
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    MSG message = {};
    int frameIndex = 0;
    while (message.message != WM_QUIT && (numFrames == 0 || frameIndex < numFrames)) {
      if (PeekMessage(&message, nullptr, 0U, 0U, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessage(&message);
      } else {
        RECT rect = {};
        GetClientRect(hwnd, &rect);
        const uint32_t width = static_cast<uint32_t>(std::max(0L, rect.right - rect.left));
        const uint32_t height = static_cast<uint32_t>(std::max(0L, rect.bottom - rect.top));
        render(width, height, frameIndex, halfRunFrame);
        frameIndex++;
      }
    }
    destroyScene();
  } catch (const std::exception& error) {
    std::printf("FAILED: %s\n", error.what());
    exitCode = EXIT_FAILURE;
  }

  if (g_remix) {
    const auto shutdownResult = remix::lib::shutdownAndUnloadRemixDll(*g_remix);
    if (!shutdownResult) {
      std::printf("FAILED: shutdownAndUnloadRemixDll() failed %d\n",
      static_cast<int>(shutdownResult.status()));
      exitCode = EXIT_FAILURE;
    }
    g_remix.reset();
  }

  UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);

  return exitCode;
}
