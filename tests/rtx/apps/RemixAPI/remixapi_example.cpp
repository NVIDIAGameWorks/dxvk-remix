#include <remix/remix.h>

remix::Interface* g_remix = nullptr;

remixapi_LightHandle g_scene_light = nullptr;
remixapi_MeshHandle g_scene_mesh = nullptr;


bool init(HWND hwnd) {
  const wchar_t* path = L"d3d9.dll";
  if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
    path = L"bin\\d3d9.dll";
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
      printf("d3d9.dll not found.\nPlease, place it in the same folder as this .exe");
    }
  }

  if (auto interf = remix::lib::loadRemixDllAndInitialize(path)) {
    g_remix = new remix::Interface { *interf };
  } else {
    throw std::runtime_error { "remix::loadRemixDllAndInitialize() failed" + std::to_string( interf.status() ) };
  }

  {
    auto startInfo = remixapi_StartupInfo {
      .sType = REMIXAPI_STRUCT_TYPE_STARTUP_INFO,
      .pNext = nullptr,
      .hwnd = hwnd,
      .disableSrgbConversionForOutput = false,
      .forceNoVkSwapchain = false,
    };
    auto success = g_remix->Startup(startInfo);
    if (!success) {
      throw std::runtime_error { "remix::Startup() failed " + std::to_string(success.status()) };
    }
  }

  {
    auto sphereLight = remixapi_LightInfoSphereEXT {
      .sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_SPHERE_EXT,
      .pNext = nullptr,
      .position = {0,-1,0},
      .radius = 0.1f,
      .shaping_hasvalue = false ,
      .shaping_value = {},
    };
    auto lightInfo = remixapi_LightInfo {
      .sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO,
      .pNext = &sphereLight,
      .hash = 0x3,
      .radiance = { 100, 200, 100 },
    };

    auto lightHandle = g_remix->CreateLight(lightInfo);
    if (!lightHandle) {
      throw std::runtime_error { "remix::CreateLight() failed " + std::to_string(lightHandle.status()) };
    }
    g_scene_light = lightHandle.value();
  }
  {
    auto makeVertex = [](float x, float y, float z)->remixapi_HardcodedVertex {
      return remixapi_HardcodedVertex {
        .position = {x,y,z},
        .normal = {0,0,-1},
        .texcoord = {0,0},
        .color = 0xFFFFFFFF,
      };
    };

    remixapi_HardcodedVertex verts[] = {
      makeVertex( 5, -5, 10),
      makeVertex( 0, 5, 10),
      makeVertex(-5, -5, 10),
    };

    auto triangles = remixapi_MeshInfoSurfaceTriangles {
      .vertices_values = verts,
      .vertices_count = std::size(verts),
      .indices_values = nullptr ,
      .indices_count = 0,
      .skinning_hasvalue = false,
      .skinning_value = {},
      .material = nullptr,
    };

    auto meshInfo = remixapi_MeshInfo {
      .sType = REMIXAPI_STRUCT_TYPE_MESH_INFO,
      .pNext = nullptr,
      .hash = 0x1,
      .surfaces_values = &triangles,
      .surfaces_count = 1,
    };

    auto meshHandle = g_remix->CreateMesh(meshInfo);
    if (!meshHandle) {
      throw std::runtime_error { "remix::CreateMesh() failed " + std::to_string(meshHandle.status()) };
    }
    g_scene_mesh = meshHandle.value();
  }
  return true;
}

void render(uint32_t windowWidth, uint32_t windowHeight) {
  {
    auto parametersForCamera = remixapi_CameraInfoParameterizedEXT {
      .sType = REMIXAPI_STRUCT_TYPE_CAMERA_INFO_PARAMETERIZED_EXT,
      .position = { 0,0,0 },
      .forward = { 0,0,1 },
      .up = { 0,1,0 },
      .right = { 1,0,0 },
      .fovYInDegrees = 70,
      .aspect = float(windowWidth) / float(windowHeight),
      .nearPlane = 0.1f,
      .farPlane = 1000.0f,
    };
    auto cameraInfo = remixapi_CameraInfo {
      .sType = REMIXAPI_STRUCT_TYPE_CAMERA_INFO,
      .pNext = &parametersForCamera,
    };
    g_remix->SetupCamera(cameraInfo);
  }
  {
    auto meshInstanceInfo = remixapi_InstanceInfo {
      .sType = REMIXAPI_STRUCT_TYPE_INSTANCE_INFO,
      .categoryFlags = 0,
      .mesh = g_scene_mesh,
      .transform = { {
        {1,0,0,0},
        {0,1,0,0},
        {0,0,1,0},
      } },
      .doubleSided = true,
    };
    g_remix->DrawInstance(meshInstanceInfo);
  }
  {
    g_remix->DrawLightInstance(g_scene_light);
  }
  g_remix->Present();
}

void destroy() {
  if (g_remix) {
    remix::lib::shutdownAndUnloadRemixDll(*g_remix);
    delete g_remix;
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

  auto wc = WNDCLASSEX {
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
  try {
    if (init(hwnd)) {
      ShowWindow(hwnd, SW_SHOWDEFAULT);
      UpdateWindow(hwnd);

      MSG msg = {};
      while (msg.message != WM_QUIT && (numFrames == 0 || frameIdx < numFrames)) {
        if (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
          TranslateMessage(&msg);
          DispatchMessage(&msg);
        } else {
          auto hwndRect = RECT {};
          GetClientRect(hwnd, &hwndRect);
          const auto w = static_cast<uint32_t>(std::max(0l, hwndRect.right - hwndRect.left));
          const auto h = static_cast<uint32_t>(std::max(0l, hwndRect.bottom - hwndRect.top));

          render(w, h);
          ++frameIdx;
        }
      }
    }
  }
  catch (const std::exception& error) {
    printf("FAILED: %s", error.what());
  }

  destroy();

  UnregisterClass(wc.lpszClassName, wc.hInstance);
  return 0;
}

#pragma endregion
