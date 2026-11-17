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

// Drives the GPU-resident scene through the 32-bit bridge and the Remix runtime,
// see documentation/GpuResidentScene.md.
// test_remix_api_runtime_app.py deploys the runtime with rtx.gpuResidentScene = True,
// and checks the colors of the panels below in a screenshot of a late frame.
// Bridged calls return before the server runs them, so the panels show whether each call took effect.
// The panels form a 3 x 3 grid in front of the camera. Each one checks one kind of call:
//   Row 0: a retained instance (green), an instance moved into place by SceneInstanceUpdate (blue),
//          and a removed instance (dark).
//   Row 1: a material that UpdateMaterial turns from red to green, a frame-local instance (blue),
//          and an emissive texture with a red left half and a blue right half.
//   Row 2: a panel whose mesh and material are destroyed and created again in green, in the right column.
// The camera comes from a D3D9 draw, as the bridge does not forward SetupCamera.
// While the GPU-resident scene is enabled, that draw only sets the camera and is not ray traced.

#include <array>
#include <cmath>
#include <fstream>
#include <string>
#include <vector>

#include "test_remix_api_bridge_app.h"

using namespace bridge_app_test;

namespace {
  // Keep these in sync with test_remix_api_runtime_app.py.
  constexpr UINT kWidth = 640;
  constexpr UINT kHeight = 480;
  constexpr float kFovYDegrees = 60.0f;
  constexpr float kPanelDistance = 10.0f;
  constexpr float kPanelSpacingX = 5.0f;
  constexpr float kPanelSpacingY = 3.5f;
  constexpr float kPanelHalfSize = 1.2f;

  constexpr int kFrameCount = 70;
  constexpr int kChangeFrame = 10;

  struct Vec3 {
    float x;
    float y;
    float z;
  };

  remixapi_Interface g_remix = {};
  bool g_passed = true;

  void checkStatus(const remixapi_ErrorCode status, const char* what) {
    if (status != REMIXAPI_ERROR_CODE_SUCCESS) {
      std::printf("FAILED: %s returned %u.\n", what, static_cast<unsigned>(status));
      std::fflush(stdout);
      g_passed = false;
    }
  }

  Vec3 panelCenter(const int column, const int row) {
    return { (static_cast<float>(column) - 1.0f) * kPanelSpacingX,
             (1.0f - static_cast<float>(row)) * kPanelSpacingY,
             kPanelDistance };
  }

  HWND createWindow() {
    WNDCLASSW windowClass = {};
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"RemixApiGpuResidentSceneTest";
    RegisterClassW(&windowClass);
    RECT rect = { 0, 0, static_cast<LONG>(kWidth), static_cast<LONG>(kHeight) };
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    const HWND hWnd = CreateWindowExW(0, windowClass.lpszClassName, L"Remix API GPU-resident scene test",
                                      WS_OVERLAPPEDWINDOW, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                                      nullptr, nullptr, windowClass.hInstance, nullptr);
    if (hWnd != nullptr) {
      ShowWindow(hWnd, SW_SHOWDEFAULT);
    }
    return hWnd;
  }

  std::wstring exeDirectory() {
    wchar_t exePath[MAX_PATH] = {};
    const DWORD len = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    const std::wstring path(exePath, len);
    return path.substr(0, path.find_last_of(L"\\/") + 1);
  }

  // Writes a 2 x 2 RGBA8 sRGB texture with a red left half and a blue right half as a DDS file.
  std::wstring writeHalvesTexture() {
    const std::wstring path = exeDirectory() + L"gpu_resident_halves.dds";
    std::ofstream file(path.c_str(), std::ios::binary);
    const auto write = [&file](const uint32_t value, uint32_t count = 1) {
      while (count--) {
        file.write(reinterpret_cast<const char*>(&value), sizeof(value));
      }
    };
    write(0x20534444);  // "DDS "
    write(124);         // Header size
    write(0x100F);      // Flags: caps, height, width, pitch, pixel format
    write(2);           // Height
    write(2);           // Width
    write(8);           // Pitch
    write(0, 2);        // Depth and mip count
    write(0, 11);       // Reserved
    write(32);          // Pixel format size
    write(0x4);         // Pixel format flags: four character code
    write(0x30315844);  // "DX10"
    write(0, 5);        // Bit count and masks
    write(0x1000);      // Caps: texture
    write(0, 4);        // Caps 2 to 4 and reserved
    write(29);          // DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
    write(3);           // Texture 2D
    write(0);           // Misc flags
    write(1);           // Array size
    write(0);           // Misc flags 2
    const uint8_t texels[] = { 255, 0, 0, 255,  0, 0, 255, 255,
                               255, 0, 0, 255,  0, 0, 255, 255 };
    file.write(reinterpret_cast<const char*>(texels), sizeof(texels));
    return path;
  }

  remixapi_MaterialInfoOpaqueEXT makeOpaque(const remixapi_Float3D albedo) {
    remixapi_MaterialInfoOpaqueEXT opaque = {};
    opaque.sType = REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_OPAQUE_EXT;
    opaque.albedoConstant = albedo;
    opaque.opacityConstant = 1.0f;
    opaque.roughnessConstant = 0.8f;
    opaque.thinFilmThickness_value = 200.0f;
    opaque.useDrawCallAlphaState = true;
    opaque.alphaTestType = 7;  // Always
    opaque.enableDlssControlMask = true;
    opaque.dlssControlMaskIntensity = 1.0f;
    opaque.dlssControlMaskToneStrength = 0.3f;
    opaque.dlssControlMaskStructuralStrength = 0.7f;
    opaque.textureScale_value = { 1.0f, 1.0f };
    return opaque;
  }

  remixapi_MaterialInfo makeMaterialInfo(remixapi_MaterialInfoOpaqueEXT& opaque) {
    remixapi_MaterialInfo info = {};
    info.sType = REMIXAPI_STRUCT_TYPE_MATERIAL_INFO;
    info.pNext = &opaque;
    info.spriteSheetRow = 1;
    info.spriteSheetCol = 1;
    info.filterMode = 1;
    info.wrapModeU = 1;
    info.wrapModeV = 1;
    return info;
  }

  remixapi_MaterialHandle createEmissiveMaterial(const remixapi_Float3D color, const wchar_t* emissiveTexture = nullptr) {
    remixapi_MaterialInfoOpaqueEXT opaque = makeOpaque({ 0.0f, 0.0f, 0.0f });
    remixapi_MaterialInfo info = makeMaterialInfo(opaque);
    info.emissiveColorConstant = color;
    info.emissiveIntensity = 3.0f;
    info.emissiveTexture = emissiveTexture;
    remixapi_MaterialHandle material = nullptr;
    checkStatus(g_remix.CreateMaterial(&info, &material), "CreateMaterial(emissive)");
    return material;
  }

  // A square panel around the origin, facing the camera.
  remixapi_MeshHandle createPanel(const remixapi_MaterialHandle material) {
    const float s = kPanelHalfSize;
    const remixapi_HardcodedVertex vertices[4] = {
      { { -s, -s, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 1.0f }, 0xFFFFFFFF },
      { {  s, -s, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 1.0f, 1.0f }, 0xFFFFFFFF },
      { {  s,  s, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 1.0f, 0.0f }, 0xFFFFFFFF },
      { { -s,  s, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 0.0f }, 0xFFFFFFFF },
    };
    const uint32_t indices[6] = { 0, 2, 1, 0, 3, 2 };
    remixapi_MeshInfoSurfaceTriangles surface = {};
    surface.vertices_values = vertices;
    surface.vertices_count = 4;
    surface.indices_values = indices;
    surface.indices_count = 6;
    surface.material = material;
    remixapi_MeshInfo info = {};
    info.sType = REMIXAPI_STRUCT_TYPE_MESH_INFO;
    info.surfaces_values = &surface;
    info.surfaces_count = 1;
    remixapi_MeshHandle mesh = nullptr;
    checkStatus(g_remix.CreateMesh(&info, &mesh), "CreateMesh");
    return mesh;
  }

  remixapi_InstanceInfo makeInstanceInfo(const remixapi_MeshHandle mesh, const Vec3& position) {
    remixapi_InstanceInfo info = {};
    info.sType = REMIXAPI_STRUCT_TYPE_INSTANCE_INFO;
    info.mesh = mesh;
    info.transform = { {
      { 1.0f, 0.0f, 0.0f, position.x },
      { 0.0f, 1.0f, 0.0f, position.y },
      { 0.0f, 0.0f, 1.0f, position.z },
    } };
    info.doubleSided = true;
    return info;
  }

  remixapi_SceneInstanceHandle addInstance(const remixapi_MeshHandle mesh, const Vec3& position) {
    const remixapi_InstanceInfo info = makeInstanceInfo(mesh, position);
    remixapi_SceneInstanceHandle instance = nullptr;
    checkStatus(g_remix.SceneInstanceAdd(&info, &instance), "SceneInstanceAdd");
    return instance;
  }

  D3DMATRIX identity() {
    D3DMATRIX m = {};
    m._11 = m._22 = m._33 = m._44 = 1.0f;
    return m;
  }

  // A left-handed perspective projection, as D3DXMatrixPerspectiveFovLH builds it.
  D3DMATRIX perspective() {
    constexpr float kNear = 0.1f;
    constexpr float kFar = 100.0f;
    const float yScale = 1.0f / std::tan(kFovYDegrees * 3.14159265f / 360.0f);
    D3DMATRIX m = {};
    m._11 = yScale * static_cast<float>(kHeight) / static_cast<float>(kWidth);
    m._22 = yScale;
    m._33 = kFar / (kFar - kNear);
    m._34 = 1.0f;
    m._43 = -kNear * kFar / (kFar - kNear);
    return m;
  }

  struct Scene {
    remixapi_MeshHandle retained = nullptr;
    remixapi_MeshHandle moved = nullptr;
    remixapi_MeshHandle removed = nullptr;
    remixapi_MeshHandle updatedMaterialPanel = nullptr;
    remixapi_MeshHandle frameLocal = nullptr;
    remixapi_MeshHandle textured = nullptr;
    remixapi_MeshHandle recreated = nullptr;
    remixapi_MaterialHandle updatedMaterial = nullptr;
    remixapi_MaterialHandle recreatedMaterial = nullptr;
    std::vector<remixapi_MaterialHandle> materials;
    std::vector<remixapi_SceneInstanceHandle> instances;
    remixapi_SceneInstanceHandle movedInstance = nullptr;
    remixapi_SceneInstanceHandle removedInstance = nullptr;
    remixapi_SceneInstanceHandle recreatedInstance = nullptr;
  };

  void createScene(Scene& scene, const std::wstring& texturePath) {
    const remixapi_MaterialHandle green = createEmissiveMaterial({ 0.0f, 1.0f, 0.0f });
    const remixapi_MaterialHandle blue = createEmissiveMaterial({ 0.0f, 0.0f, 1.0f });
    const remixapi_MaterialHandle red = createEmissiveMaterial({ 1.0f, 0.0f, 0.0f });
    const remixapi_MaterialHandle textured = createEmissiveMaterial({ 1.0f, 1.0f, 1.0f }, texturePath.c_str());
    scene.updatedMaterial = createEmissiveMaterial({ 1.0f, 0.0f, 0.0f });
    scene.recreatedMaterial = createEmissiveMaterial({ 1.0f, 0.0f, 0.0f });
    scene.materials = { green, blue, red, textured, scene.updatedMaterial };

    scene.retained = createPanel(green);
    scene.moved = createPanel(blue);
    scene.removed = createPanel(red);
    scene.updatedMaterialPanel = createPanel(scene.updatedMaterial);
    scene.frameLocal = createPanel(blue);
    scene.textured = createPanel(textured);
    scene.recreated = createPanel(scene.recreatedMaterial);

    scene.instances.push_back(addInstance(scene.retained, panelCenter(0, 0)));
    // The moved instance starts out of view, and SceneInstanceUpdate moves it into place.
    scene.movedInstance = addInstance(scene.moved, { 100.0f, 0.0f, kPanelDistance });
    scene.instances.push_back(scene.movedInstance);
    scene.removedInstance = addInstance(scene.removed, panelCenter(2, 0));
    scene.instances.push_back(addInstance(scene.updatedMaterialPanel, panelCenter(0, 1)));
    scene.instances.push_back(addInstance(scene.textured, panelCenter(2, 1)));
    scene.recreatedInstance = addInstance(scene.recreated, panelCenter(2, 2));
  }

  void changeScene(Scene& scene) {
    const remixapi_InstanceInfo movedInfo = makeInstanceInfo(scene.moved, panelCenter(1, 0));
    checkStatus(g_remix.SceneInstanceUpdate(scene.movedInstance, &movedInfo), "SceneInstanceUpdate");

    checkStatus(g_remix.SceneInstanceRemove(scene.removedInstance), "SceneInstanceRemove");
    scene.removedInstance = nullptr;

    remixapi_MaterialInfoOpaqueEXT opaque = makeOpaque({ 0.0f, 0.0f, 0.0f });
    remixapi_MaterialInfo info = makeMaterialInfo(opaque);
    info.emissiveColorConstant = { 0.0f, 1.0f, 0.0f };
    info.emissiveIntensity = 3.0f;
    checkStatus(g_remix.UpdateMaterial(scene.updatedMaterial, &info), "UpdateMaterial");

    checkStatus(g_remix.SceneInstanceRemove(scene.recreatedInstance), "SceneInstanceRemove(recreated)");
    checkStatus(g_remix.DestroyMesh(scene.recreated), "DestroyMesh(recreated)");
    checkStatus(g_remix.DestroyMaterial(scene.recreatedMaterial), "DestroyMaterial(recreated)");
    scene.recreatedMaterial = createEmissiveMaterial({ 0.0f, 1.0f, 0.0f });
    scene.recreated = createPanel(scene.recreatedMaterial);
    scene.recreatedInstance = addInstance(scene.recreated, panelCenter(2, 2));
  }

  void drawApiFrame(Scene& scene) {
    const remixapi_InstanceInfo frameLocal = makeInstanceInfo(scene.frameLocal, panelCenter(1, 1));
    checkStatus(g_remix.DrawInstance(&frameLocal), "DrawInstance");
  }

  void destroyScene(Scene& scene) {
    scene.instances.push_back(scene.recreatedInstance);
    for (const remixapi_SceneInstanceHandle instance : scene.instances) {
      checkStatus(g_remix.SceneInstanceRemove(instance), "SceneInstanceRemove");
    }
    for (const remixapi_MeshHandle mesh : { scene.retained, scene.moved, scene.removed, scene.updatedMaterialPanel,
                                            scene.frameLocal, scene.textured, scene.recreated }) {
      checkStatus(g_remix.DestroyMesh(mesh), "DestroyMesh");
    }
    scene.materials.push_back(scene.recreatedMaterial);
    for (const remixapi_MaterialHandle material : scene.materials) {
      checkStatus(g_remix.DestroyMaterial(material), "DestroyMaterial");
    }
  }

  // Draws one small triangle far away, which gives Remix the camera.
  void drawCameraTriangle(IDirect3DDevice9* pDevice) {
    const float vertices[] = { -40.0f, 30.0f, 90.0f,  -39.0f, 30.0f, 90.0f,  -40.0f, 29.0f, 90.0f };
    const D3DMATRIX world = identity();
    const D3DMATRIX view = identity();
    const D3DMATRIX projection = perspective();
    pDevice->SetTransform(D3DTS_WORLD, &world);
    pDevice->SetTransform(D3DTS_VIEW, &view);
    pDevice->SetTransform(D3DTS_PROJECTION, &projection);
    pDevice->SetFVF(D3DFVF_XYZ);
    pDevice->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, vertices, 3 * sizeof(float));
  }
}

int main() {
  const HWND hWnd = createWindow();
  if (hWnd == nullptr) {
    std::printf("FAILED: Could not create a window (error %lu).\n", GetLastError());
    return 1;
  }

  const HMODULE hClient = loadBridgeClient();
  if (hClient == nullptr) {
    return 1;
  }
  const PFN_remixapi_InitializeLibrary pfnInitializeLibrary = getInitializeLibrary(hClient);
  const auto pfnDirect3DCreate9 = reinterpret_cast<PFN_Direct3DCreate9>(GetProcAddress(hClient, "Direct3DCreate9"));
  if (pfnInitializeLibrary == nullptr || pfnDirect3DCreate9 == nullptr) {
    logLine("FAILED: The bridge client does not export remixapi_InitializeLibrary and Direct3DCreate9.");
    return 1;
  }

  IDirect3D9* pD3D = pfnDirect3DCreate9(D3D_SDK_VERSION);
  if (pD3D == nullptr) {
    logLine("FAILED: Direct3DCreate9 returned null.");
    return 1;
  }

  remixapi_InitializeLibraryInfo initInfo = {};
  initInfo.sType = REMIXAPI_STRUCT_TYPE_INITIALIZE_LIBRARY_INFO;
  initInfo.version = REMIXAPI_VERSION_MAKE(REMIXAPI_VERSION_MAJOR, REMIXAPI_VERSION_MINOR, REMIXAPI_VERSION_PATCH);
  const remixapi_ErrorCode initStatus = pfnInitializeLibrary(&initInfo, &g_remix);
  if (initStatus != REMIXAPI_ERROR_CODE_SUCCESS) {
    std::printf("FAILED: remixapi_InitializeLibrary returned %u.\n", static_cast<unsigned>(initStatus));
    return 1;
  }
  if (g_remix.SceneInstanceAdd == nullptr || g_remix.SceneInstanceUpdate == nullptr
      || g_remix.SceneInstanceRemove == nullptr || g_remix.UpdateMaterial == nullptr) {
    logLine("FAILED: The bridge client does not expose the retained-instance functions.");
    return 1;
  }

  D3DPRESENT_PARAMETERS presentParams = {};
  presentParams.Windowed = TRUE;
  presentParams.SwapEffect = D3DSWAPEFFECT_DISCARD;
  presentParams.BackBufferFormat = D3DFMT_UNKNOWN;
  presentParams.BackBufferWidth = kWidth;
  presentParams.BackBufferHeight = kHeight;
  presentParams.hDeviceWindow = hWnd;
  presentParams.EnableAutoDepthStencil = TRUE;
  presentParams.AutoDepthStencilFormat = D3DFMT_D24S8;
  IDirect3DDevice9* pDevice = nullptr;
  const HRESULT createResult = pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd,
                                                  D3DCREATE_HARDWARE_VERTEXPROCESSING, &presentParams, &pDevice);
  if (FAILED(createResult)) {
    std::printf("FAILED: CreateDevice returned 0x%08lX.\n", static_cast<unsigned long>(createResult));
    return 1;
  }

  Scene scene;
  createScene(scene, writeHalvesTexture());

  for (int frame = 0; frame < kFrameCount; frame++) {
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    if (frame == kChangeFrame) {
      changeScene(scene);
    }
    pDevice->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
    pDevice->BeginScene();
    drawCameraTriangle(pDevice);
    drawApiFrame(scene);
    pDevice->EndScene();
    pDevice->Present(nullptr, nullptr, nullptr, nullptr);
  }

  destroyScene(scene);
  pDevice->Release();
  pD3D->Release();
  DestroyWindow(hWnd);

  logLine(g_passed ? "PASSED" : "FAILED");
  return g_passed ? 0 : 1;
}
