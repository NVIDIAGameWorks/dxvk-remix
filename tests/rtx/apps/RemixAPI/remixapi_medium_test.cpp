#include <remix/remix.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

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

  std::unique_ptr<remix::Interface> g_remix;
  remixapi_MaterialHandle g_cameraMediumMaterial = nullptr;
  std::vector<remixapi_MaterialHandle> g_materials;
  std::vector<remixapi_MeshHandle> g_meshes;

  remixapi_HardcodedVertex makeVertex(const Vec3& position, const Vec3& normal, float u, float v) {
    return {
      .position = { position.x, position.y, position.z },
      .normal = { normal.x, normal.y, normal.z },
      .texcoord = { u, v },
      .color = 0xFFFFFFFF,
    };
  }

  remixapi_MaterialHandle createOpaqueMaterial(
      uint64_t hash,
      remixapi_Float3D albedo,
      float roughness,
      float metallic,
      remixapi_Float3D emissiveColor = { 0.0f, 0.0f, 0.0f },
      float emissiveIntensity = 0.0f) {
    remix::MaterialInfoOpaqueEXT opaque;
    opaque.albedoConstant = albedo;
    opaque.opacityConstant = 1.0f;
    opaque.roughnessConstant = roughness;
    opaque.metallicConstant = metallic;

    remix::MaterialInfo info;
    info.hash = hash;
    info.pNext = &opaque;
    info.emissiveColorConstant = emissiveColor;
    info.emissiveIntensity = emissiveIntensity;

    auto result = g_remix->CreateMaterial(info);
    if (!result) {
      throw std::runtime_error("CreateMaterial(opaque) failed " + std::to_string(result.status()));
    }

    g_materials.push_back(result.value());
    return result.value();
  }

  remixapi_MaterialHandle createTranslucentMaterial(
      uint64_t hash,
      float refractiveIndex,
      remixapi_Float3D transmittanceColor,
      float measurementDistance) {
    remix::MaterialInfoTranslucentEXT translucent;
    translucent.refractiveIndex = refractiveIndex;
    translucent.transmittanceColor = transmittanceColor;
    translucent.transmittanceMeasurementDistance = measurementDistance;
    translucent.useDiffuseLayer = false;
    translucent.set_thinWallThickness(std::nullopt);

    remix::MaterialInfo info;
    info.hash = hash;
    info.pNext = &translucent;

    auto result = g_remix->CreateMaterial(info);
    if (!result) {
      throw std::runtime_error("CreateMaterial(translucent) failed " + std::to_string(result.status()));
    }

    g_materials.push_back(result.value());
    return result.value();
  }

  remixapi_MeshHandle createQuad(
      uint64_t hash,
      const Vec3& center,
      const Vec3& horizontal,
      const Vec3& vertical,
      remixapi_MaterialHandle material) {
    const Vec3 normal = normalize(cross(horizontal, vertical));
    const std::array<remixapi_HardcodedVertex, 4> vertices = {
      makeVertex(center - horizontal - vertical, normal, 0.0f, 1.0f),
      makeVertex(center + horizontal - vertical, normal, 1.0f, 1.0f),
      makeVertex(center + horizontal + vertical, normal, 1.0f, 0.0f),
      makeVertex(center - horizontal + vertical, normal, 0.0f, 0.0f),
    };
    constexpr std::array<uint32_t, 6> indices = { 0, 1, 2, 0, 2, 3 };

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

    g_meshes.push_back(result.value());
    return result.value();
  }

  void createScene() {
    g_cameraMediumMaterial = createTranslucentMaterial(
      0x1000, 1.33f, { 0.08f, 0.45f, 0.95f }, 14.0f);
    const remixapi_MaterialHandle amberMediumMaterial = createTranslucentMaterial(
      0x1001, 1.50f, { 0.94f, 0.52f, 0.18f }, 2.0f);
    const remixapi_MaterialHandle mirrorMaterial = createOpaqueMaterial(
      0x1002, { 0.98f, 0.98f, 0.98f }, 0.0f, 1.0f);
    const remixapi_MaterialHandle whiteEmissiveMaterial = createOpaqueMaterial(
      0x1003, { 1.0f, 1.0f, 1.0f }, 1.0f, 0.0f, { 1.0f, 1.0f, 1.0f }, 20.0f);
    const remixapi_MaterialHandle redEmissiveMaterial = createOpaqueMaterial(
      0x1004, { 1.0f, 0.05f, 0.03f }, 1.0f, 0.0f, { 1.0f, 0.05f, 0.03f }, 20.0f);
    const remixapi_MaterialHandle reflectionTargetMaterial = createOpaqueMaterial(
      0x1005, { 1.0f, 1.0f, 1.0f }, 1.0f, 0.0f, { 1.0f, 1.0f, 1.0f }, 0.1f);

    // Direct targets at different depths expose starting-medium attenuation.
    createQuad(0x2000, { -3.3f, 1.5f, 6.0f }, { 1.15f, 0.0f, 0.0f }, { 0.0f, 1.15f, 0.0f }, whiteEmissiveMaterial);
    createQuad(0x2001, { -7.0f, -2.0f, 13.0f }, { 2.35f, 0.0f, 0.0f }, { 0.0f, 2.35f, 0.0f }, whiteEmissiveMaterial);

    // The short camera-to-mirror path reflects along a much longer path through the camera medium.
    constexpr float kSqrtHalf = 0.70710678f;
    createQuad(
      0x2002,
      { 0.0f, 0.0f, 1.0f },
      { 0.25f, 0.0f, 0.0f },
      { 0.0f, -0.23f * kSqrtHalf, -0.23f * kSqrtHalf },
      mirrorMaterial);

    // This is the quad that will be reflected by the mirror. It is white, but if the reflection ray medium was set properly it should be tinted by the camera medium
    createQuad(
      0x2003,
      { 0.0f, 20.0f, 1.0f },
      { 8.0f, 0.0f, 0.0f },
      { 0.0f, 0.0f, -8.0f },
      reflectionTargetMaterial);

    // The right lane exits the camera medium, crosses an amber slab, then resolves a striped target.
    createQuad(0x2004, { 2.7f, 0.0f, 4.0f }, { 1.6f, 0.0f, 0.0f }, { 0.0f, 2.8f, 0.0f }, g_cameraMediumMaterial);
    createQuad(0x2005, { 4.2f, 0.0f, 7.0f }, { 2.4f, 0.0f, 0.0f }, { 0.0f, -3.1f, 0.0f }, amberMediumMaterial);
    createQuad(0x2006, { 5.4f, 0.0f, 9.0f }, { 2.9f, 0.0f, 0.0f }, { 0.0f, 3.4f, 0.0f }, amberMediumMaterial);

    constexpr std::array<float, 5> stripeCenters = { 4.0f, 5.5f, 7.0f, 8.5f, 10.0f };
    for (size_t i = 0; i < stripeCenters.size(); i++) {
      const remixapi_MaterialHandle material = i % 2 == 0 ? redEmissiveMaterial : whiteEmissiveMaterial;
      createQuad(
        0x2100 + i,
        { stripeCenters[i], 0.0f, 14.0f },
        { 0.7f, 0.0f, 0.0f },
        { 0.0f, 3.7f, 0.0f },
        material);
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
    const remixapi_InstanceInfo info = {
      .sType = REMIXAPI_STRUCT_TYPE_INSTANCE_INFO,
      .pNext = nullptr,
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

  void render(uint32_t width, uint32_t height, bool useCameraMedium) {
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
    auto mediumResult = g_remix->SetCameraMediumMaterial(useCameraMedium ? g_cameraMediumMaterial : nullptr);
    if (!mediumResult) {
      throw std::runtime_error("SetCameraMediumMaterial() failed " + std::to_string(mediumResult.status()));
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
  const int captureFrames = argc >= 2 ? std::atoi(argv[1]) : 0;
  constexpr int kDrainFrames = 8;
  const int numFrames = captureFrames == 0 ? 0 : captureFrames + kDrainFrames;
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
    .lpszClassName = "Remix API Medium Test",
    .hIconSm = nullptr,
  };
  RegisterClassEx(&windowClass);

  constexpr DWORD kWindowStyle = WS_OVERLAPPEDWINDOW;
  RECT clientRect = { 0, 0, 900, 600 };
  AdjustWindowRect(&clientRect, kWindowStyle, false);
  HWND hwnd = CreateWindow(
    windowClass.lpszClassName,
    "Remix API Medium Test",
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
        render(width, height, captureFrames == 0 || frameIndex < captureFrames);
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
