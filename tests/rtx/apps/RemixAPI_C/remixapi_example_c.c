#include <remix/remix_c.h>

#include <assert.h>
#include <stdio.h>

remixapi_Interface g_remix = { 0 };
HMODULE g_remix_dll = NULL;

remixapi_LightHandle g_scene_light = NULL;
remixapi_MeshHandle g_scene_mesh = NULL;

remixapi_HardcodedVertex makeVertex(float x, float y, float z) {
  remixapi_HardcodedVertex v = {
    .position = {x,y,z},
    .normal = {0,0,-1},
    .texcoord = {0,0},
    .color = 0xFFFFFFFF,
  };
  return v;
}

remixapi_ErrorCode init(HWND hwnd) {
  const wchar_t* path = L"d3d9.dll";
  if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
    path = L"bin\\d3d9.dll";
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
      printf("d3d9.dll not found.\nPlease, place it in the same folder as this .exe");
    }
  }

  {
    remixapi_ErrorCode status = remixapi_lib_loadRemixDllAndInitialize(path, &g_remix, &g_remix_dll);
    if (status != REMIXAPI_ERROR_CODE_SUCCESS) {
      printf("remixapi_lib_loadRemixDllAndInitialize failed: %d", status);
      return status;
    }
  }

  {
    remixapi_StartupInfo startInfo = {
      .sType = REMIXAPI_STRUCT_TYPE_STARTUP_INFO,
      .pNext = NULL,
      .hwnd = hwnd,
      .disableSrgbConversionForOutput = FALSE,
      .forceNoVkSwapchain = FALSE,
    };
    remixapi_ErrorCode r = g_remix.Startup(&startInfo);
    if (r != REMIXAPI_ERROR_CODE_SUCCESS) {
      printf("remix::Startup() failed: %d", r);
      return r;
    }
  }

  {
    remixapi_LightInfoSphereEXT sphereLight = {
      .sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_SPHERE_EXT,
      .pNext = NULL,
      .position = {0,-1,0},
      .radius = 0.1f,
      .shaping_hasvalue = FALSE,
      .shaping_value = { 0 },
    };
    remixapi_LightInfo lightInfo = {
      .sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO,
      .pNext = &sphereLight,
      .hash = 0x3,
      .radiance = { 100, 200, 100 },
    };

    remixapi_ErrorCode r = g_remix.CreateLight(&lightInfo, &g_scene_light);
    if (r != REMIXAPI_ERROR_CODE_SUCCESS) {
      printf("remix::CreateLight() failed: %d", r);
      return r;
    }
  }
  {
    remixapi_HardcodedVertex verts[] = {
      makeVertex( 5, -5, 10),
      makeVertex( 0, 5, 10),
      makeVertex(-5, -5, 10),
    };

    remixapi_MeshInfoSurfaceTriangles triangles = {
      .vertices_values = verts,
      .vertices_count = ARRAYSIZE(verts),
      .indices_values = NULL,
      .indices_count = 0,
      .skinning_hasvalue = FALSE,
      .skinning_value = { 0 },
      .material = NULL,
    };

    remixapi_MeshInfo meshInfo = {
      .sType = REMIXAPI_STRUCT_TYPE_MESH_INFO,
      .pNext = NULL,
      .hash = 0x1,
      .surfaces_values = &triangles,
      .surfaces_count = 1,
    };

    remixapi_ErrorCode r = g_remix.CreateMesh(&meshInfo, &g_scene_mesh);
    if (r != REMIXAPI_ERROR_CODE_SUCCESS) {
      printf("remix::CreateMesh() failed: %d", r);
      return r;
    }
  }
  return REMIXAPI_ERROR_CODE_SUCCESS;
}

void render(uint32_t windowWidth, uint32_t windowHeight) {
  {
    remixapi_CameraInfoParameterizedEXT parametersForCamera = {
      .sType = REMIXAPI_STRUCT_TYPE_CAMERA_INFO_PARAMETERIZED_EXT,
      .position = { 0,0,0 },
      .forward = { 0,0,1 },
      .up = { 0,1,0 },
      .right = { 1,0,0 },
      .fovYInDegrees = 70,
      .aspect = (float)windowWidth / (float)windowHeight,
      .nearPlane = 0.1f,
      .farPlane = 1000.0f,
    };
    remixapi_CameraInfo cameraInfo = {
      .sType = REMIXAPI_STRUCT_TYPE_CAMERA_INFO,
      .pNext = &parametersForCamera,
    };
    g_remix.SetupCamera(&cameraInfo);
  }
  {
    remixapi_InstanceInfo meshInstanceInfo = {
      .sType = REMIXAPI_STRUCT_TYPE_INSTANCE_INFO,
      .categoryFlags = 0,
      .mesh = g_scene_mesh,
      .transform = { {
        {1,0,0,0},
        {0,1,0,0},
        {0,0,1,0},
      } },
      .doubleSided = 1,
    };
    g_remix.DrawInstance(&meshInstanceInfo);

    remixapi_InstanceInfoParticleSystemEXT particleInfo = {
      .sType = REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_PARTICLE_SYSTEM_EXT,
      .maxNumParticles = 1000,
      .spawnRatePerSecond = 10.f,
      .hideEmitter = 0,
      .gravityForce = 1.f,
      .maxSpeed = 1.f,
      .minSpawnSize = 1.f,
      .maxSpawnSize = 2.f,
      .minTimeToLive = 1.f,
      .maxTimeToLive = 10.f,
      .minSpawnColor = { 1.f, 1.f, 1.f, 1.f },
      .maxSpawnColor = { 1.f, 1.f, 1.f, 1.f },
    };
    meshInstanceInfo.pNext = &particleInfo;
    g_remix.DrawInstance(&meshInstanceInfo);
  }
  {
    g_remix.DrawLightInstance(g_scene_light);
  }
  g_remix.Present(NULL);
}

void destroy(void) {
  if (g_remix.Shutdown) {
    remixapi_lib_shutdownAndUnloadRemixDll(&g_remix, g_remix_dll);
  }
}



#pragma region HWND boilerplate

LRESULT WINAPI MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  default:
    break;
  }
  return DefWindowProc(hwnd, msg, wParam, lParam);
}

int main(int argc, char* argv[]) {
  // Parse command-line arguments for the number of frames
  int numFrames = 0;
  if (argc >= 2) {
    numFrames = atoi(argv[1]);
  }

  WNDCLASSEX wc = {
    .cbSize = sizeof(WNDCLASSEX),
    .style = CS_CLASSDC,
    .lpfnWndProc = MsgProc,
    .cbClsExtra = 0L,
    .cbWndExtra = 0L,
    .hInstance = GetModuleHandle(NULL),
    .hIcon = NULL,
    .hCursor = NULL,
    .hbrBackground = NULL,
    .lpszMenuName = NULL,
    .lpszClassName = "Remix API Example",
    .hIconSm = NULL,
  };
  RegisterClassEx(&wc);

  DWORD dwStyle = WS_OVERLAPPEDWINDOW;
  // readjust, so the client area as specified, not the window size
  RECT clientRect = { 0, 0, 1600, 900 };
  AdjustWindowRect(&clientRect, dwStyle, FALSE);

  HWND hwnd = CreateWindow(wc.lpszClassName, "Remix API Example",
                            dwStyle,
                            CW_USEDEFAULT, CW_USEDEFAULT, 
                            clientRect.right - clientRect.left,
                            clientRect.bottom - clientRect.top,
                            GetDesktopWindow(), NULL, wc.hInstance, NULL);

  int frameIdx = 0;

  if (init(hwnd) == REMIXAPI_ERROR_CODE_SUCCESS) {
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    MSG msg = { 0 };
    while (msg.message != WM_QUIT && (numFrames == 0 || frameIdx < numFrames)) {
      if (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
      } else {
        RECT hwndRect = { 0 };
        GetClientRect(hwnd, &hwndRect);
        LONG w = hwndRect.right - hwndRect.left;
        LONG h = hwndRect.bottom - hwndRect.top;

        render(w > 0 ? (uint32_t)w : 0, h > 0 ? (uint32_t)h : 0);
        ++frameIdx;
      }
    }
  }

  destroy();

  UnregisterClass(wc.lpszClassName, wc.hInstance);
  return 0;
}

#pragma endregion
