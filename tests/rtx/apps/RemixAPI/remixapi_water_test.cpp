#include <remix/remix.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

// A sandy shore under a low evening sun, where the water simulation meets parallax occlusion mapping.
// The simulation and its sludge are switched on in rtx.conf, see documentation/WaterSimulation.md.
// The sand is two planes that meet at the water level, and the water mesh ends exactly at that shore line.
// Dry sand rises to the left, and sand under the water falls gently to the right.
// The simulated surface rests 4 cm below the water mesh, so the water line follows the ripples of the lower plane.
// Between the shore line and the water line, the sand lies dry under the water mesh.
// The water mesh is water ahead of the camera and sludge behind it.
// Each half has a red glass tube in the strip of dry sand and a ripple (remixapi_AddWaterDisturbance) at the water line.
// Another ripple drops far out on the water.
// The image test remixapi_water_test looks at the water.
// The image test remixapi_water_test_sludge turns its free camera to the sludge.
// Sizes are in meters: rtx.conf sets rtx.sceneScale = 0.01, so that the shore distances of the simulation are tested in
// scene-scaled units.

namespace {

  using Vec3 = remixapi_Float3D;

  // The sand: a tile of 32 x 32 texels with 4 ripples, repeated every 40 cm, with parallax 3 cm deep.
  constexpr uint32_t kSandTileSize       = 32;
  constexpr float    kSandRipples        = 4.0f;
  constexpr float    kSandWorldSize      = 0.4f;
  constexpr float    kSandDisplaceIn     = 0.03f / kSandWorldSize;
  // The sludge: a tile of 64 x 64 texels, repeated every 32 cm.
  constexpr uint32_t kSludgeTileSize     = 64;
  constexpr float    kSludgeWorldSize    = 0.32f;
  // The free camera of remixapi_water_test_sludge (CAMERA_* in batch_apics.yml), relative to the app's camera:
  // it turns around, pitches down, and moves by the offset.
  constexpr float    kSludgeCameraYaw    = 3.14159f;
  constexpr float    kSludgeCameraPitch  = 0.782f;
  constexpr Vec3     kSludgeCameraOffset = { -0.2074f, -0.0797f, -0.2548f };


  std::unique_ptr<remix::Interface>    g_remix{};
  std::vector<remixapi_MaterialHandle> g_materials{};
  std::vector<remixapi_MeshHandle>     g_meshes{};
  remixapi_MeshHandle                  g_waterMesh{};
  remixapi_MeshHandle                  g_sludgeMesh{};
  remixapi_LightHandle                 g_sun{};
  bool                                 g_sludgeCamera{};


  // The height of the sand: ripples across y that meander along x, seamless at the tile's edges.
  float sandHeight(uint32_t x, uint32_t y) {
    constexpr float pi = std::numbers::pi_v<float>;
    return 0.5f + 0.5f * std::sin(2.0f * pi * kSandRipples * (y + 2.0f * std::sin(2.0f * pi * x / kSandTileSize)) / kSandTileSize);
  }


  std::array<float, 3> sandHeightTexel(uint32_t x, uint32_t y) {
    return { 255.0f * sandHeight(x, y), 0.0f, 0.0f };
  }


  // Darker in the troughs, so that the ripples show.
  std::array<float, 3> sandColorTexel(uint32_t x, uint32_t y) {
    const float height = sandHeight(x, y);
    return { 125.0f + 65.0f * height, 117.0f + 63.0f * height, 98.0f + 57.0f * height };
  }


  // A random value from 0 to 1 at a point of a lattice that repeats every cells points.
  float sludgeRandom(uint32_t i, uint32_t j, uint32_t cells, uint32_t seed) {
    uint32_t h = i % cells + j % cells * cells + seed * cells * cells;
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return float(h & 0xffffu) / 65535.0f;
  }


  // Smooth noise from 0 to 1 at a texel, on a lattice with a point every cell texels, seamless at the tile's edges.
  float sludgeNoise(uint32_t x, uint32_t y, uint32_t cell, uint32_t seed) {
    const uint32_t cells = kSludgeTileSize / cell;
    const auto smooth = [](float t) { return t * t * (3.0f - 2.0f * t); };
    const uint32_t i = x / cell;
    const uint32_t j = y / cell;
    const float sx = smooth(float(x % cell) / float(cell));
    const float sy = smooth(float(y % cell) / float(cell));
    const float top = std::lerp(sludgeRandom(i, j, cells, seed), sludgeRandom(i + 1, j, cells, seed), sx);
    const float bottom = std::lerp(sludgeRandom(i, j + 1, cells, seed), sludgeRandom(i + 1, j + 1, cells, seed), sx);
    return std::lerp(top, bottom, sy);
  }


  // How deep a texel lies in round blobs, one per cell at a random spot with a random radius: 1 at a center, 0 at an edge
  // and outside. Seamless at the tile's edges.
  float sludgeBlobs(uint32_t x, uint32_t y, uint32_t cell, uint32_t seed) {
    const int cells = int(kSludgeTileSize / cell);
    float inside = 0.0f;
    for (int j = -1; j <= 1; j++) {
      for (int i = -1; i <= 1; i++) {
        const int ci = int(x / cell) + i;
        const int cj = int(y / cell) + j;
        const uint32_t li = uint32_t((ci + cells) % cells);
        const uint32_t lj = uint32_t((cj + cells) % cells);
        const float px = (float(ci) + sludgeRandom(li, lj, uint32_t(cells), seed)) * float(cell);
        const float py = (float(cj) + sludgeRandom(li, lj, uint32_t(cells), seed + 1)) * float(cell);
        const float radius = float(cell) * (0.25f + 0.45f * sludgeRandom(li, lj, uint32_t(cells), seed + 2));
        inside = std::max(inside, 1.0f - std::hypot(px - (float(x) + 0.5f), py - (float(y) + 0.5f)) / radius);
      }
    }
    return inside;
  }


  // Crumbly patches of tan scum on mostly green water, with flecks of foam on the water and pits in the scum.
  // Greyer and bluer than in daylight, so that they keep their look under the warm evening light.
  std::array<float, 3> sludgeColorTexel(uint32_t x, uint32_t y) {
    // A broad mask, round clumps and fine noise, cut sharply, so that the patches get lumpy, crumbly edges.
    const float clumps = sludgeBlobs(x, y, 8, 10) > 0.0f ? 1.0f : 0.0f;
    const float field = 0.5f * sludgeNoise(x, y, 32, 1) + 0.25f * sludgeNoise(x, y, 16, 2) + 0.15f * clumps +
                        0.1f * sludgeNoise(x, y, 2, 3);
    const float scum = std::clamp((field - 0.6f) * 14.0f, 0.0f, 1.0f);
    const float tint = sludgeNoise(x, y, 16, 5) * sludgeNoise(x, y, 16, 5);
    const float grain = sludgeNoise(x, y, 2, 6);
    const bool bubble = sludgeBlobs(x, y, 8, 20) > 0.55f;
    const std::array<float, 3> water = { std::lerp(30.0f, 70.0f, tint), std::lerp(60.0f, 50.0f, tint), std::lerp(40.0f, 36.0f, tint) };
    const std::array<float, 3> foam = { std::lerp(140.0f, 206.0f, grain), std::lerp(128.0f, 198.0f, grain), std::lerp(88.0f, 156.0f, grain) };
    std::array<float, 3> texel;
    for (int c = 0; c < 3; c++) {
      const float surface = std::lerp(water[c], foam[c], scum);
      texel[c] = bubble ? std::lerp(0.8f * foam[c], 0.55f * surface, scum) : surface;
    }
    return texel;
  }


  // Writes a square texture with all its mips as a DDS file next to the executable, as the API takes textures as files.
  // A height map (R8, from the first value of each texel) keeps in each mip the highest of the texels it covers, as the
  // quadtree parallax walk needs. A color texture (RGBA8 sRGB) keeps their average.
  void writeTexture(const std::filesystem::path& path, uint32_t size, bool color,
                    std::array<float, 3> (*texel)(uint32_t, uint32_t)) {
    const uint32_t texelSize = color ? 4 : 1;
    uint32_t mips = 0;
    for (uint32_t mipSize = size; mipSize > 0; mipSize /= 2) {
      mips++;
    }
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
    write(size * texelSize);      // Pitch
    write(0);                     // Depth
    write(mips);                  // Mip count
    write(0, 11);                 // Reserved
    write(32);                    // Pixel format size
    write(0x4);                   // Pixel format flags: four character code
    write(0x30315844);            // "DX10"
    write(0, 5);                  // Bit count and masks
    write(0x401008);              // Caps: texture, mipmap, complex
    write(0, 4);                  // Caps 2 to 4 and reserved
    write(color ? 29 : 61);       // DXGI_FORMAT_R8G8B8A8_UNORM_SRGB or DXGI_FORMAT_R8_UNORM
    write(3);                     // Texture 2D
    write(0);                     // Misc flags
    write(1);                     // Array size
    write(0);                     // Misc flags 2
    for (uint32_t mipSize = size; mipSize > 0; mipSize /= 2) {
      const uint32_t block = size / mipSize;
      for (uint32_t i = 0; i < mipSize * mipSize; i++) {
        float highest = 0.0f;
        std::array<float, 3> sum = {};
        for (uint32_t j = 0; j < block * block; j++) {
          const std::array<float, 3> value = texel(i % mipSize * block + j % block, i / mipSize * block + j / block);
          highest = std::max(highest, value[0]);
          for (int c = 0; c < 3; c++) {
            sum[c] += value[c];
          }
        }
        if (color) {
          const float count = float(block * block);
          const uint8_t rgba[4] = { uint8_t(sum[0] / count), uint8_t(sum[1] / count), uint8_t(sum[2] / count), 255 };
          file.write(reinterpret_cast<const char*>(rgba), sizeof(rgba));
        } else {
          const uint8_t height = uint8_t(highest);
          file.write(reinterpret_cast<const char*>(&height), sizeof(height));
        }
      }
    }
  }


  Vec3 operator+(Vec3 a, Vec3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
  Vec3 operator-(Vec3 a, Vec3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
  Vec3 operator*(Vec3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
  Vec3 cross(Vec3 a, Vec3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
  Vec3 normalize(Vec3 v) { const float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); return { v.x / l, v.y / l, v.z / l }; }


  remixapi_HardcodedVertex makeVertex(const Vec3& position, const Vec3& normal, float u, float v) {
    return {
      .position = { position.x, position.y, position.z },
      .normal = { normal.x, normal.y, normal.z },
      .texcoord = { u, v },
      .color = 0xFFFFFFFF,
    };
  }


  remixapi_MaterialHandle createMaterial(const remix::MaterialInfo& info) {
    auto result = g_remix->CreateMaterial(info);
    if (!result) {
      throw std::runtime_error("CreateMaterial() failed " + std::to_string(result.status()));
    }
    g_materials.push_back(result.value());
    return result.value();
  }


  remixapi_MaterialHandle createOpaqueMaterial(
      uint64_t hash,
      remixapi_Float3D albedo,
      float roughness,
      remixapi_Float3D emissiveColor = { 0.0f, 0.0f, 0.0f },
      float emissiveIntensity = 0.0f) {
    remix::MaterialInfoOpaqueEXT opaque;
    opaque.albedoConstant = albedo;
    opaque.roughnessConstant = roughness;

    remix::MaterialInfo info;
    info.hash = hash;
    info.pNext = &opaque;
    info.emissiveColorConstant = emissiveColor;
    info.emissiveIntensity = emissiveIntensity;
    return createMaterial(info);
  }


  // Opaque with an albedo texture, and with parallax when there is a height map.
  remixapi_MaterialHandle createTexturedMaterial(
      uint64_t hash,
      const std::filesystem::path& albedoPath,
      float roughness,
      const std::filesystem::path& heightPath = {},
      float displaceIn = 0.0f) {
    remix::MaterialInfoOpaqueEXT opaque;
    opaque.roughnessConstant = roughness;
    if (!heightPath.empty()) {
      opaque.set_heightTexture(heightPath);
      opaque.displaceIn = displaceIn;
    }

    remix::MaterialInfo info;
    info.hash = hash;
    info.pNext = &opaque;
    info.set_albedoTexture(albedoPath);
    return createMaterial(info);
  }


  // Thick unless it has a wall thickness.
  remixapi_MaterialHandle createTranslucentMaterial(
      uint64_t hash,
      float refractiveIndex,
      remixapi_Float3D transmittanceColor,
      float transmittanceMeasurementDistance,
      std::optional<float> thinWallThickness) {
    remix::MaterialInfoTranslucentEXT translucent;
    translucent.refractiveIndex = refractiveIndex;
    translucent.transmittanceColor = transmittanceColor;
    translucent.transmittanceMeasurementDistance = transmittanceMeasurementDistance;
    translucent.useDiffuseLayer = false;
    translucent.set_thinWallThickness(thinWallThickness);

    remix::MaterialInfo info;
    info.hash = hash;
    info.pNext = &translucent;
    return createMaterial(info);
  }


  remixapi_MeshHandle createMesh(
      uint64_t hash,
      const std::vector<remixapi_HardcodedVertex>& vertices,
      const std::vector<uint32_t>& indices,
      remixapi_MaterialHandle material) {
    const remixapi_MeshInfoSurfaceTriangles surface = {
      .vertices_values = vertices.data(),
      .vertices_count = vertices.size(),
      .indices_values = indices.data(),
      .indices_count = indices.size(),
      .skinning_hasvalue = false,
      .skinning_value = {},
      .material = material,
    };
    const remixapi_MeshInfo info = {
      .sType = REMIXAPI_STRUCT_TYPE_MESH_INFO,
      .pNext = nullptr,
      .hash = hash,
      .surfaces_values = &surface,
      .surfaces_count = 1,
    };

    auto result = g_remix->CreateMesh(info);
    if (!result) {
      throw std::runtime_error("CreateMesh() failed " + std::to_string(result.status()));
    }
    return result.value();
  }


  // A quad around center, with its normal along cross(horizontal, vertical) and its texture repeated across it.
  remixapi_MeshHandle createQuad(
      uint64_t hash,
      const Vec3& center,
      const Vec3& horizontal,
      const Vec3& vertical,
      remixapi_MaterialHandle material,
      float repeatU = 1.0f,
      float repeatV = 1.0f) {
    const Vec3 normal = normalize(cross(horizontal, vertical));
    return createMesh(hash, {
      makeVertex(center - horizontal - vertical, normal, 0.0f, repeatV),
      makeVertex(center + horizontal - vertical, normal, repeatU, repeatV),
      makeVertex(center + horizontal + vertical, normal, repeatU, 0.0f),
      makeVertex(center - horizontal + vertical, normal, 0.0f, 0.0f),
    }, { 0, 1, 2, 0, 2, 3 }, material);
  }


  // An upright tube without caps, from its bottom center up.
  remixapi_MeshHandle createTube(uint64_t hash, const Vec3& bottom, float radius, float height,
                                 remixapi_MaterialHandle material) {
    constexpr uint32_t kSegments = 32;
    std::vector<remixapi_HardcodedVertex> vertices;
    std::vector<uint32_t> indices;
    for (uint32_t i = 0; i <= kSegments; i++) {
      const float angle = 2.0f * std::numbers::pi_v<float> * float(i) / float(kSegments);
      const Vec3 normal = { std::cos(angle), 0.0f, std::sin(angle) };
      const Vec3 foot = bottom + normal * radius;
      vertices.push_back(makeVertex(foot, normal, float(i) / float(kSegments), 1.0f));
      vertices.push_back(makeVertex(foot + Vec3 { 0.0f, height, 0.0f }, normal, float(i) / float(kSegments), 0.0f));
    }
    for (uint32_t i = 0; i < 2 * kSegments; i += 2) {
      indices.insert(indices.end(), { i, i + 2, i + 1, i + 1, i + 2, i + 3 });
    }
    return createMesh(hash, vertices, indices, material);
  }


  void createScene(const std::filesystem::path& folder) {
    // The shore line runs along z at x = 0 and height 0. The scene is a box that reaches this far from it.
    constexpr float kExtent = 30.0f;
    constexpr float kLandSlope = 1.0f / 8.0f;
    constexpr float kSeaSlope = 1.0f / 16.0f;

    const std::filesystem::path sandHeightPath = folder / "remixapi_water_test_sand_height.dds";
    const std::filesystem::path sandAlbedoPath = folder / "remixapi_water_test_sand_albedo.dds";
    const std::filesystem::path sludgeAlbedoPath = folder / "remixapi_water_test_sludge_albedo.dds";
    writeTexture(sandHeightPath, kSandTileSize, false, sandHeightTexel);
    writeTexture(sandAlbedoPath, kSandTileSize, true, sandColorTexel);
    writeTexture(sludgeAlbedoPath, kSludgeTileSize, true, sludgeColorTexel);

    const remixapi_MaterialHandle sandMaterial = createTexturedMaterial(0x1000, sandAlbedoPath, 0.7f, sandHeightPath, kSandDisplaceIn);
    const remixapi_MaterialHandle skyMaterial = createOpaqueMaterial(0x1001, { 0.0f, 0.0f, 0.0f }, 1.0f, { 1.0f, 0.8f, 0.5f }, 1.3f);
    // Water absorbs red first, so that it darkens and turns green with depth.
    const remixapi_MaterialHandle waterMaterial = createTranslucentMaterial(0x1002, 1.33f, { 0.5f, 0.65f, 0.55f }, 0.24f, std::nullopt);
    const remixapi_MaterialHandle sludgeMaterial = createTexturedMaterial(0x1003, sludgeAlbedoPath, 1.0f);
    // Light red glass: each pass through the thin wall keeps all of the red and about half of the rest.
    // A thin wall's transmittance is measured over 1 world unit whatever the scale, so the wall is a quarter of a unit thick.
    const remixapi_MaterialHandle glassMaterial = createTranslucentMaterial(0x1004, 1.5f, { 1.0f, 0.55f, 0.55f }, 0.25f, 0.25f);

    // The sand: a plane on each side of the shore line, facing up, with the ripples along the shore.
    // Whole repeats down the slopes keep the ripples continuous across the shore line.
    const Vec3 along = { 0.0f, 0.0f, kExtent };
    const auto createSand = [&](uint64_t hash, float fromX, float toX, float slope) {
      const Vec3 center = { (fromX + toX) / 2.0f, -(fromX + toX) / 2.0f * slope, 0.0f };
      const Vec3 across = { (toX - fromX) / 2.0f, -(toX - fromX) / 2.0f * slope, 0.0f };
      const float repeatAcross = std::round(2.0f * std::sqrt(across.x * across.x + across.y * across.y) / kSandWorldSize);
      g_meshes.push_back(createQuad(hash, center, along, across, sandMaterial, 2.0f * kExtent / kSandWorldSize, repeatAcross));
    };
    createSand(0x2000, -kExtent, 0.0f, kLandSlope);
    createSand(0x2001, 0.0f, kExtent, kSeaSlope);

    // An evenly bright warm sky: the walls and the top of the box, facing in.
    constexpr float kSkyBottom = -kExtent / 4.0f;
    constexpr float kSkyTop = 2.0f * kExtent;
    const float wallCenter = (kSkyBottom + kSkyTop) / 2.0f;
    const Vec3 wallHeight = { 0.0f, (kSkyTop - kSkyBottom) / 2.0f, 0.0f };
    g_meshes.push_back(createQuad(0x2002, { 0.0f, wallCenter, kExtent }, { -kExtent, 0.0f, 0.0f }, wallHeight, skyMaterial));
    g_meshes.push_back(createQuad(0x2003, { 0.0f, wallCenter, -kExtent }, { kExtent, 0.0f, 0.0f }, wallHeight, skyMaterial));
    g_meshes.push_back(createQuad(0x2004, { -kExtent, wallCenter, 0.0f }, { 0.0f, 0.0f, -kExtent }, wallHeight, skyMaterial));
    g_meshes.push_back(createQuad(0x2005, { kExtent, wallCenter, 0.0f }, along, wallHeight, skyMaterial));
    g_meshes.push_back(createQuad(0x2006, { 0.0f, kSkyTop, 0.0f }, { kExtent, 0.0f, 0.0f }, along, skyMaterial));

    // The water mesh at height 0, from the shore line out to the box: water ahead of the camera, sludge behind it.
    const Vec3 halfAlong = { 0.0f, 0.0f, kExtent / 2.0f };
    const Vec3 halfAcross = { kExtent / 2.0f, 0.0f, 0.0f };
    const float sludgeRepeat = kExtent / kSludgeWorldSize;
    g_waterMesh = createQuad(0x2007, { kExtent / 2.0f, 0.0f, kExtent / 2.0f }, halfAlong, halfAcross, waterMaterial);
    g_sludgeMesh = createQuad(0x2008, { kExtent / 2.0f, 0.0f, -kExtent / 2.0f }, halfAlong, halfAcross, sludgeMaterial,
                              sludgeRepeat, sludgeRepeat);

    // A red glass tube on each half, in the strip of dry sand under the water mesh, sunk below the sand's troughs.
    g_meshes.push_back(createTube(0x2009, { 0.15f, -0.06f, 1.03f }, 0.05f, 0.2f, glassMaterial));
    g_meshes.push_back(createTube(0x200A, { 0.15f, -0.06f, -1.03f }, 0.05f, 0.2f, glassMaterial));

    // A low sun, 12 degrees above the horizon, ahead and to the right.
    remix::LightInfoDistantEXT distant;
    distant.direction = normalize({ -0.56f, -0.21f, -0.8f });
    distant.angularDiameterDegrees = 1.0f;
    remix::LightInfo lightInfo;
    lightInfo.pNext = &distant;
    lightInfo.hash = 0x3000;
    lightInfo.radiance = { 24.0f, 18.0f, 11.0f };
    auto light = g_remix->CreateLight(lightInfo);
    if (!light) {
      throw std::runtime_error("CreateLight() failed " + std::to_string(light.status()));
    }
    g_sun = light.value();
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
    auto startupResult = g_remix->Startup(startupInfo);
    if (!startupResult) {
      throw std::runtime_error("Startup() failed " + std::to_string(startupResult.status()));
    }

    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    createScene(std::filesystem::path(exePath).parent_path());
  }


  void drawMesh(remixapi_MeshHandle mesh, remixapi_InstanceCategoryFlags categoryFlags) {
    const remixapi_InstanceInfo info = {
      .sType = REMIXAPI_STRUCT_TYPE_INSTANCE_INFO,
      .pNext = nullptr,
      .categoryFlags = categoryFlags,
      .mesh = mesh,
      .transform = { {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
      } },
      .doubleSided = true,
    };
    auto result = g_remix->DrawInstance(info);
    if (!result) {
      throw std::runtime_error("DrawInstance() failed " + std::to_string(result.status()));
    }
  }


  void render(int frameIndex) {
    // Standing in the shallow water at eye height, looking at the water line ahead.
    Vec3 position = { 0.48f, 0.56f, 0.0f };
    Vec3 forward = normalize(Vec3 { 0.56f, 0.0f, 2.12f } - position);
    Vec3 right = normalize(cross({ 0.0f, 1.0f, 0.0f }, forward));
    Vec3 up = cross(forward, right);
    if (g_sludgeCamera) {
      // Turned as the free camera turns the view (freeCameraViewRelative): yaw about up, then pitch about right.
      const float sinYaw = std::sin(kSludgeCameraYaw);
      const float cosYaw = std::cos(kSludgeCameraYaw);
      const float sinPitch = std::sin(kSludgeCameraPitch);
      const float cosPitch = std::cos(kSludgeCameraPitch);
      const Vec3 turnedForward = right * (sinYaw * cosPitch) - up * sinPitch + forward * (cosYaw * cosPitch);
      const Vec3 turnedUp = right * (sinYaw * sinPitch) + up * cosPitch + forward * (cosYaw * sinPitch);
      right = right * cosYaw - forward * sinYaw;
      forward = turnedForward;
      up = turnedUp;
      position = position + kSludgeCameraOffset;
    }
    remixapi_CameraInfoParameterizedEXT parameters = {
      .sType = REMIXAPI_STRUCT_TYPE_CAMERA_INFO_PARAMETERIZED_EXT,
      .pNext = nullptr,
      .position = position,
      .forward = forward,
      .up = up,
      .right = right,
      .fovYInDegrees = 38.0f,
      .aspect = 2560.0f / 1440.0f,
      .nearPlane = 0.01f,
      .farPlane = 100.0f,
    };
    const remixapi_CameraInfo cameraInfo = {
      .sType = REMIXAPI_STRUCT_TYPE_CAMERA_INFO,
      .pNext = &parameters,
    };
    auto cameraResult = g_remix->SetupCamera(cameraInfo);
    if (!cameraResult) {
      throw std::runtime_error("SetupCamera() failed " + std::to_string(cameraResult.status()));
    }

    // The ripples drop on frame 30, after the simulation has started; the image test takes its picture on frame 50.
    // The sand under the water crosses the resting surface between x = 0.16 (its troughs) and x = 0.64 (its crests).
    // One ripple drops there on each half, so that its rings run up the sand, and one far out on the water.
    if (frameIndex == 30) {
      for (const Vec3& ripple : { Vec3 { 0.54f, 0.0f, 1.62f }, Vec3 { 0.35f, 0.0f, -1.2f },
                                  Vec3 { 1.8f, 0.0f, 5.0f } }) {
        remix::WaterDisturbanceInfo disturbance {};
        disturbance.position = ripple;
        disturbance.radius = 0.16f;
        disturbance.strength = 0.24f;
        auto disturbanceResult = g_remix->AddWaterDisturbance(disturbance);
        if (!disturbanceResult) {
          throw std::runtime_error("AddWaterDisturbance() failed " + std::to_string(disturbanceResult.status()));
        }
      }
    }

    for (const remixapi_MeshHandle mesh : g_meshes) {
      drawMesh(mesh, 0);
    }
    drawMesh(g_waterMesh, REMIXAPI_INSTANCE_CATEGORY_BIT_ANIMATED_WATER);
    drawMesh(g_sludgeMesh, REMIXAPI_INSTANCE_CATEGORY_BIT_SLUDGE);
    g_remix->DrawLightInstance(g_sun);

    auto presentResult = g_remix->Present();
    if (!presentResult) {
      throw std::runtime_error("Present() failed " + std::to_string(presentResult.status()));
    }
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
  // Sizes in physical pixels, so that a scaled desktop does not stretch the window over the whole screen.
  SetProcessDPIAware();

  // Zero or negative runs until closed; the test runner passes -1 for --doNotAutoTerminate.
  const int captureFrames = argc >= 2 ? std::atoi(argv[1]) : 0;
  // The argument after the frame count, sludge, gives the app's camera the view of remixapi_water_test_sludge.
  g_sludgeCamera = argc >= 3 && std::string(argv[2]) == "sludge";
  const bool runUntilClosed = captureFrames <= 0;
  constexpr int kDrainFrames = 8;
  const int numFrames = runUntilClosed ? 0 : captureFrames + kDrainFrames;
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
    .lpszClassName = "Remix API Water Test",
    .hIconSm = nullptr,
  };
  RegisterClassEx(&windowClass);

  // The image test renders at 2560 x 1440 (batch_apics.yml).
  // In the upper left corner and without borders, so that the client area is the full size even on a screen of that size.
  constexpr DWORD kWindowStyle = WS_POPUP;
  RECT clientRect = { 0, 0, 2560, 1440 };
  AdjustWindowRect(&clientRect, kWindowStyle, false);
  HWND hwnd = CreateWindow(
    windowClass.lpszClassName,
    "Remix API Water Test",
    kWindowStyle,
    0,
    0,
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
        render(frameIndex);
        frameIndex++;
      }
    }
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
