#include <remix/remix.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

  // Every vertex picks its bones from this pool, so bone indices differ between neighboring vertices.
  constexpr uint32_t kBoneCount = 16;

  // Bands with up to this many vertices take the CPU skinning path, and larger bands take the GPU path.
  // It must match kNumVerticesToProcessOnCPU in RtxGeometryUtils::dispatchSkinning.
  constexpr uint32_t kCpuSkinningVertexLimit = 256;

  struct Vec2 {
    float x;
    float y;
  };

  struct BandLayout {
    uint32_t bonesPerVertex;
    uint32_t columns;
    uint32_t rows;
    Vec2 center;
  };

  std::unique_ptr<remix::Interface> g_remix;
  std::vector<remixapi_MaterialHandle> g_materials;
  std::vector<remixapi_MeshHandle> g_meshes;
  std::array<remixapi_Transform, kBoneCount> g_boneTransforms;

  // Returns a deterministic pseudo-random value in [0, 1).
  float nextRandom(uint32_t& state) {
    state = state * 1664525u + 1013904223u;
    return static_cast<float>(state >> 8) / static_cast<float>(1u << 24);
  }

  // Bones are pure translations, so skinned normals stay unchanged.
  void createBones() {
    uint32_t state = 0x5EED;
    for (remixapi_Transform& bone : g_boneTransforms) {
      const float x = nextRandom(state) * 0.9f - 0.45f;
      const float y = nextRandom(state) * 0.9f - 0.45f;
      bone = { {
        { 1.0f, 0.0f, 0.0f, x },
        { 0.0f, 1.0f, 0.0f, y },
        { 0.0f, 0.0f, 1.0f, 0.0f },
      } };
    }
  }

  remixapi_MaterialHandle createEmissiveMaterial(uint64_t hash, remixapi_Float3D color) {
    remix::MaterialInfoOpaqueEXT opaque;
    opaque.albedoConstant = color;
    opaque.opacityConstant = 1.0f;
    opaque.roughnessConstant = 1.0f;
    opaque.metallicConstant = 0.0f;

    remix::MaterialInfo info;
    info.hash = hash;
    info.pNext = &opaque;
    info.emissiveColorConstant = color;
    info.emissiveIntensity = 1.0f;

    auto result = g_remix->CreateMaterial(info);
    if (!result) {
      throw std::runtime_error("CreateMaterial() failed " + std::to_string(result.status()));
    }

    g_materials.push_back(result.value());
    return result.value();
  }

  // Creates a flat grid whose rest pose is scrambled by the inverse of its blended bone offsets.
  // Only correct skinning moves every vertex back onto the band's rectangle.
  // A wrong blend weight or bone index stride reads another vertex's data and leaves the band torn apart.
  remixapi_MeshHandle createSkinnedBand(uint64_t hash, const BandLayout& layout, remixapi_MaterialHandle material) {
    constexpr float kHalfWidth = 1.7f;
    constexpr float kHalfHeight = 0.6f;
    constexpr float kDepth = 5.0f;

    const uint32_t bonesPerVertex = layout.bonesPerVertex;
    const uint32_t vertexCount = layout.columns * layout.rows;

    std::vector<remixapi_HardcodedVertex> vertices;
    std::vector<float> blendWeights;
    std::vector<uint32_t> blendIndices;
    vertices.reserve(vertexCount);
    blendWeights.reserve(vertexCount * bonesPerVertex);
    blendIndices.reserve(vertexCount * bonesPerVertex);

    uint32_t state = static_cast<uint32_t>(hash);
    for (uint32_t row = 0; row < layout.rows; row++) {
      for (uint32_t column = 0; column < layout.columns; column++) {
        const float u = static_cast<float>(column) / static_cast<float>(layout.columns - 1);
        const float v = static_cast<float>(row) / static_cast<float>(layout.rows - 1);

        // Picks distinct bones with random weights that sum to one.
        std::array<uint32_t, kBoneCount> bones;
        for (uint32_t i = 0; i < kBoneCount; i++) {
          bones[i] = i;
        }
        std::array<float, kBoneCount> weights;
        float weightSum = 0.0f;
        for (uint32_t i = 0; i < bonesPerVertex; i++) {
          const uint32_t pick = i + static_cast<uint32_t>(nextRandom(state) * static_cast<float>(kBoneCount - i));
          std::swap(bones[i], bones[pick]);
          weights[i] = 0.2f + nextRandom(state);
          weightSum += weights[i];
        }

        float offsetX = 0.0f;
        float offsetY = 0.0f;
        for (uint32_t i = 0; i < bonesPerVertex; i++) {
          weights[i] /= weightSum;
          offsetX += weights[i] * g_boneTransforms[bones[i]].matrix[0][3];
          offsetY += weights[i] * g_boneTransforms[bones[i]].matrix[1][3];
          blendWeights.push_back(weights[i]);
          blendIndices.push_back(bones[i]);
        }

        const float x = layout.center.x + (u * 2.0f - 1.0f) * kHalfWidth;
        const float y = layout.center.y + (1.0f - v * 2.0f) * kHalfHeight;
        vertices.push_back({
          .position = { x - offsetX, y - offsetY, kDepth },
          .normal = { 0.0f, 0.0f, -1.0f },
          .texcoord = { u, v },
          .color = 0xFFFFFFFF,
        });
      }
    }

    std::vector<uint32_t> indices;
    indices.reserve((layout.columns - 1) * (layout.rows - 1) * 6);
    for (uint32_t row = 0; row + 1 < layout.rows; row++) {
      for (uint32_t column = 0; column + 1 < layout.columns; column++) {
        const uint32_t topLeft = row * layout.columns + column;
        const uint32_t bottomLeft = topLeft + layout.columns;
        indices.insert(indices.end(), { topLeft, topLeft + 1, bottomLeft + 1, topLeft, bottomLeft + 1, bottomLeft });
      }
    }

    const remixapi_MeshInfoSurfaceTriangles surface = {
      .vertices_values = vertices.data(),
      .vertices_count = vertices.size(),
      .indices_values = indices.data(),
      .indices_count = indices.size(),
      .skinning_hasvalue = true,
      .skinning_value = {
        .bonesPerVertex = bonesPerVertex,
        .blendWeights_values = blendWeights.data(),
        .blendWeights_count = static_cast<uint32_t>(blendWeights.size()),
        .blendIndices_values = blendIndices.data(),
        .blendIndices_count = static_cast<uint32_t>(blendIndices.size()),
      },
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

    g_meshes.push_back(result.value());
    return result.value();
  }

  // Rows are 1, 2 and 5 bones per vertex.
  // One bone per vertex reads no weights and one packed index word, so it skins correctly even with a one-element stride.
  // Two bones per vertex exercise the blend weight stride, and five also exercise the blend index stride.
  // The left column takes the CPU skinning path and the right column takes the GPU path.
  void createScene() {
    createBones();

    constexpr std::array<uint32_t, 3> kBonesPerVertex = { 1, 2, 5 };
    constexpr std::array<float, 3> kRowCenters = { 1.9f, 0.0f, -1.9f };
    constexpr std::array<remixapi_Float3D, 3> kRowColors = { {
      { 1.0f, 1.0f, 1.0f },
      { 1.0f, 0.35f, 0.05f },
      { 0.1f, 0.6f, 1.0f },
    } };

    for (uint32_t row = 0; row < kBonesPerVertex.size(); row++) {
      const remixapi_MaterialHandle material = createEmissiveMaterial(0x1000 + row, kRowColors[row]);

      const BandLayout cpuBand = { kBonesPerVertex[row], 8, 6, { -2.0f, kRowCenters[row] } };
      const BandLayout gpuBand = { kBonesPerVertex[row], 20, 16, { 2.0f, kRowCenters[row] } };
      if (cpuBand.columns * cpuBand.rows > kCpuSkinningVertexLimit || gpuBand.columns * gpuBand.rows <= kCpuSkinningVertexLimit) {
        throw std::logic_error("Band vertex counts do not select the intended skinning paths");
      }

      createSkinnedBand(0x2000 + row, cpuBand, material);
      createSkinnedBand(0x3000 + row, gpuBand, material);
    }
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

    createScene();
  }

  void drawMesh(remixapi_MeshHandle mesh) {
    remixapi_InstanceInfoBoneTransformsEXT bones = {
      .sType = REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_BONE_TRANSFORMS_EXT,
      .pNext = nullptr,
      .boneTransforms_values = g_boneTransforms.data(),
      .boneTransforms_count = static_cast<uint32_t>(g_boneTransforms.size()),
    };
    const remixapi_InstanceInfo info = {
      .sType = REMIXAPI_STRUCT_TYPE_INSTANCE_INFO,
      .pNext = &bones,
      .categoryFlags = 0,
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

  void render(uint32_t width, uint32_t height) {
    remixapi_CameraInfoParameterizedEXT parameters = {
      .sType = REMIXAPI_STRUCT_TYPE_CAMERA_INFO_PARAMETERIZED_EXT,
      .pNext = nullptr,
      .position = { 0.0f, 0.0f, 0.0f },
      .forward = { 0.0f, 0.0f, 1.0f },
      .up = { 0.0f, 1.0f, 0.0f },
      .right = { 1.0f, 0.0f, 0.0f },
      .fovYInDegrees = 62.0f,
      .aspect = static_cast<float>(width) / static_cast<float>(height),
      .nearPlane = 0.1f,
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

    for (const remixapi_MeshHandle mesh : g_meshes) {
      drawMesh(mesh);
    }

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
  // Zero or negative runs until closed. The test runner passes -1 for --doNotAutoTerminate.
  const int captureFrames = argc >= 2 ? std::atoi(argv[1]) : 0;
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
    .lpszClassName = "Remix API Skinning Test",
    .hIconSm = nullptr,
  };
  RegisterClassEx(&windowClass);

  constexpr DWORD kWindowStyle = WS_OVERLAPPEDWINDOW;
  RECT clientRect = { 0, 0, 900, 600 };
  AdjustWindowRect(&clientRect, kWindowStyle, false);
  HWND hwnd = CreateWindow(
    windowClass.lpszClassName,
    "Remix API Skinning Test",
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
        render(width, height);
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
