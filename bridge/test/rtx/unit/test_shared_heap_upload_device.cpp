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

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>
#include <tlhelp32.h>
#include <wrl/client.h>

#include "test_remix_api_bridge_app.h"

using Microsoft::WRL::ComPtr;
using namespace bridge_app_test;

namespace {
  constexpr UINT kSize = 32;
  constexpr UINT kTextureSize = 4;
  constexpr DWORD kRed = 0xffff0000;
  constexpr DWORD kBlue = 0xff0000ff;
  constexpr DWORD kBlack = 0xff000000;

  struct CloseHandleDeleter {
    void operator()(HANDLE handle) const {
      if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
        CloseHandle(handle);
      }
    }
  };
  using OwnedHandle = std::unique_ptr<void, CloseHandleDeleter>;

  void check(HRESULT result, const char* pCall) {
    if (FAILED(result)) {
      std::printf("FAILED: %s returned 0x%08lX.\n", pCall, static_cast<unsigned long>(result));
      throw std::runtime_error(pCall);
    }
  }

  void expectResult(HRESULT result, HRESULT expected, const char* pCall) {
    if (result != expected) {
      std::printf("FAILED: %s returned 0x%08lX, expected 0x%08lX.\n", pCall,
                  static_cast<unsigned long>(result), static_cast<unsigned long>(expected));
      throw std::runtime_error(pCall);
    }
  }

  void checkSystem(bool result, const char* pCall) {
    if (!result) {
      std::printf("FAILED: %s returned Windows error %lu.\n", pCall, GetLastError());
      throw std::runtime_error(pCall);
    }
  }

  bool lockstepTest() {
    char value[8] = {};
    return GetEnvironmentVariableA("REMIX_SHARED_UPLOAD_TEST_LOCKSTEP", value, sizeof(value)) && value[0] == '1';
  }

  HWND createWindow() {
    WNDCLASSW windowClass = {};
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"BridgeSharedHeapUploadTest";
    RegisterClassW(&windowClass);
    RECT rect = { 0, 0, kSize, kSize };
    checkSystem(AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE), "AdjustWindowRect");
    return CreateWindowExW(0, windowClass.lpszClassName, L"", WS_OVERLAPPEDWINDOW,
                           0, 0, rect.right - rect.left, rect.bottom - rect.top,
                           nullptr, nullptr, windowClass.hInstance, nullptr);
  }

  DWORD findServer() {
    wchar_t exePath[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    checkSystem(length && length < MAX_PATH, "GetModuleFileNameW");
    std::wstring expectedPath(exePath, length);
    expectedPath.resize(expectedPath.find_last_of(L"\\/") + 1);
    expectedPath += L".trex\\NvRemixBridge.exe";
    OwnedHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    checkSystem(snapshot.get() != INVALID_HANDLE_VALUE, "CreateToolhelp32Snapshot processes");
    PROCESSENTRY32W process = {};
    process.dwSize = sizeof(process);
    checkSystem(Process32FirstW(snapshot.get(), &process), "Process32FirstW");
    DWORD serverPid = 0;
    do {
      if (process.th32ParentProcessID != GetCurrentProcessId() || _wcsicmp(process.szExeFile, L"NvRemixBridge.exe")) {
        continue;
      }
      OwnedHandle server(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process.th32ProcessID));
      checkSystem(server != nullptr, "OpenProcess own bridge server");
      wchar_t serverPath[MAX_PATH] = {};
      DWORD pathLength = MAX_PATH;
      checkSystem(QueryFullProcessImageNameW(server.get(), 0, serverPath, &pathLength), "QueryFullProcessImageNameW");
      if (CompareStringOrdinal(serverPath, -1, expectedPath.c_str(), -1, TRUE) == CSTR_EQUAL) {
        if (serverPid) {
          throw std::runtime_error("Multiple bridge servers belong to this test app");
        }
        serverPid = process.th32ProcessID;
      }
    } while (Process32NextW(snapshot.get(), &process));
    if (!serverPid) {
      throw std::runtime_error("The test app's bridge server was not found at the expected path");
    }
    return serverPid;
  }

  class ServerPause {
    struct Thread {
      OwnedHandle handle;
      bool suspended = false;
    };
    const DWORD m_serverPid;
    std::vector<Thread> m_threads;
  public:
    explicit ServerPause(DWORD serverPid) : m_serverPid(serverPid) { }
    ~ServerPause() { resume(); }

    void suspend() {
      OwnedHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0));
      checkSystem(snapshot.get() != INVALID_HANDLE_VALUE, "CreateToolhelp32Snapshot threads");
      THREADENTRY32 entry = {};
      entry.dwSize = sizeof(entry);
      checkSystem(Thread32First(snapshot.get(), &entry), "Thread32First");
      do {
        if (entry.th32OwnerProcessID != m_serverPid) {
          continue;
        }
        OwnedHandle thread(OpenThread(THREAD_SUSPEND_RESUME | THREAD_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ThreadID));
        if (thread == nullptr && GetLastError() == ERROR_INVALID_PARAMETER) {
          continue;
        }
        checkSystem(thread != nullptr, "OpenThread own bridge server");
        if (GetProcessIdOfThread(thread.get()) != m_serverPid) {
          continue;
        }
        m_threads.push_back({ std::move(thread), false });
        checkSystem(SuspendThread(m_threads.back().handle.get()) != DWORD(-1), "SuspendThread own bridge server");
        m_threads.back().suspended = true;
      } while (Thread32Next(snapshot.get(), &entry));
      if (m_threads.empty()) {
        throw std::runtime_error("The test app's bridge server has no suspendable threads");
      }
    }

    bool resume() {
      bool success = true;
      for (auto& thread : m_threads) {
        if (thread.suspended) {
          if (ResumeThread(thread.handle.get()) == DWORD(-1)) {
            std::printf("FAILED: ResumeThread returned Windows error %lu.\n", GetLastError());
            success = false;
          }
          thread.suspended = false;
        }
      }
      m_threads.clear();
      return success;
    }
  };

  struct ColorVertex {
    float x, y, z, rhw;
    DWORD color;
  };

  void fillTriangle(ColorVertex* pVertices, DWORD color) {
    const float farEdge = 2.0f * kSize - 0.5f;
    pVertices[0] = { -0.5f, -0.5f, 0.5f, 1.0f, color };
    pVertices[1] = { farEdge, -0.5f, 0.5f, 1.0f, color };
    pVertices[2] = { -0.5f, farEdge, 0.5f, 1.0f, color };
  }

  struct Device {
    ComPtr<IDirect3D9> pD3D;
    ComPtr<IDirect3DDevice9> pDevice;
    ComPtr<IDirect3DSurface9> pTargets[2];
    ComPtr<IDirect3DSurface9> pReadbacks[2];
    ComPtr<IDirect3DQuery9> pFence;
    DWORD serverPid = 0;
    bool sameFrame = false;

    Device(HWND hWnd, bool lockstep) : sameFrame(lockstep) {
      const HMODULE client = loadBridgeClient();
      if (client == nullptr) {
        throw std::runtime_error("loadBridgeClient");
      }
      const auto create = reinterpret_cast<PFN_Direct3DCreate9>(GetProcAddress(client, "Direct3DCreate9"));
      pD3D.Attach(create != nullptr ? create(D3D_SDK_VERSION) : nullptr);
      if (pD3D == nullptr) {
        throw std::runtime_error("Direct3DCreate9");
      }
      D3DPRESENT_PARAMETERS present = {};
      present.Windowed = TRUE;
      present.SwapEffect = D3DSWAPEFFECT_DISCARD;
      present.BackBufferFormat = D3DFMT_UNKNOWN;
      present.BackBufferWidth = kSize;
      present.BackBufferHeight = kSize;
      present.hDeviceWindow = hWnd;
      check(pD3D->CreateDevice(0, D3DDEVTYPE_HAL, hWnd,
                               D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED,
                               &present, &pDevice), "CreateDevice");
      for (unsigned i = 0; i < 2; ++i) {
        check(pDevice->CreateRenderTarget(kSize, kSize, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE,
                                          0, FALSE, &pTargets[i], nullptr), "CreateRenderTarget");
        check(pDevice->CreateOffscreenPlainSurface(kSize, kSize, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM,
                                                   &pReadbacks[i], nullptr), "CreateOffscreenPlainSurface");
      }
      check(pDevice->CreateQuery(D3DQUERYTYPE_EVENT, &pFence), "CreateQuery fence");
      check(pDevice->SetRenderState(D3DRS_LIGHTING, FALSE), "SetRenderState lighting");
      check(pDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE), "SetRenderState culling");
      check(pDevice->SetRenderState(D3DRS_ZENABLE, FALSE), "SetRenderState depth");
      check(pDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1), "SetTextureStageState color");
      check(pDevice->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT), "SetSamplerState min");
      check(pDevice->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT), "SetSamplerState mag");
      check(pDevice->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP), "SetSamplerState U");
      check(pDevice->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP), "SetSamplerState V");
      serverPid = findServer();
    }

    void fence() {
      check(pFence->Issue(D3DISSUE_END), "Fence::Issue");
      const ULONGLONG deadline = GetTickCount64() + 10000;
      for (;;) {
        BOOL complete = FALSE;
        const HRESULT result = pFence->GetData(&complete, sizeof(complete), D3DGETDATA_FLUSH);
        check(result, "Fence::GetData");
        if (result == S_OK && complete) {
          return;
        }
        if (GetTickCount64() >= deadline) {
          throw std::runtime_error("The preallocation fence did not complete within ten seconds");
        }
        Sleep(1);
      }
    }

    bool checkPixels(unsigned frame, DWORD left, DWORD right) {
      check(pDevice->GetRenderTargetData(pTargets[frame].Get(), pReadbacks[frame].Get()), "GetRenderTargetData");
      D3DLOCKED_RECT locked = {};
      check(pReadbacks[frame]->LockRect(&locked, nullptr, D3DLOCK_READONLY), "Readback::LockRect");
      const auto* row = reinterpret_cast<const DWORD*>(static_cast<const BYTE*>(locked.pBits) + 16 * locked.Pitch);
      const DWORD actualLeft = row[8];
      const DWORD actualMiddle = row[20];
      const DWORD actualRight = row[24];
      check(pReadbacks[frame]->UnlockRect(), "Readback::UnlockRect");
      const bool passed = (actualLeft & 0xffffff) == (left & 0xffffff)
                       && (actualMiddle & 0xffffff) == (right & 0xffffff)
                       && (actualRight & 0xffffff) == (right & 0xffffff);
      std::printf("  Frame %c: left=0x%08lX middle=0x%08lX right=0x%08lX, expected 0x%08lX/0x%08lX/0x%08lX: %s\n",
                  frame ? 'B' : 'A', actualLeft, actualMiddle, actualRight, left, right, right, passed ? "PASSED" : "FAILED");
      return passed;
    }
  };

  bool testUploads(Device& device, const char* pName, const std::function<void(bool)>& upload,
                   const std::function<void()>& draw, bool partialTexture = false) {
    std::printf("Testing %s with non-discard partial updates and read-only preservation...\n", pName);
    check(device.pDevice->ColorFill(device.pTargets[0].Get(), nullptr, kBlack), "Clear target A");
    check(device.pDevice->ColorFill(device.pTargets[1].Get(), nullptr, kBlack), "Clear target B");
    device.fence();
    OwnedHandle written(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    OwnedHandle finished(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    checkSystem(written != nullptr && finished != nullptr, "CreateEventW");
    ServerPause pause(device.serverPid);
    pause.suspend();
    std::atomic<unsigned> phase = 0;
    std::exception_ptr failure;
    std::thread client([&]() {
      try {
        phase = 1;
        upload(false);
        check(device.pDevice->SetRenderTarget(0, device.pTargets[0].Get()), "SetRenderTarget A");
        check(device.pDevice->BeginScene(), "BeginScene A");
        draw();
        check(device.pDevice->EndScene(), "EndScene A");
        phase = 2;
        if (!device.sameFrame) {
          check(device.pDevice->Present(nullptr, nullptr, nullptr, nullptr), "Present A");
        }
        phase = 3;
        upload(true);
        phase = 4;
        SetEvent(written.get());
        check(device.pDevice->SetRenderTarget(0, device.pTargets[1].Get()), "SetRenderTarget B");
        check(device.pDevice->BeginScene(), "BeginScene B");
        draw();
        check(device.pDevice->EndScene(), "EndScene B");
        check(device.pDevice->Present(nullptr, nullptr, nullptr, nullptr), "Present B");
      } catch (...) {
        failure = std::current_exception();
      }
      SetEvent(finished.get());
    });
    const HANDLE events[] = { written.get(), finished.get() };
    const DWORD observed = WaitForMultipleObjects(2, events, FALSE, 2000);
    const unsigned pausedPhase = phase;
    const bool resumed = pause.resume();
    client.join();
    checkSystem(observed != WAIT_FAILED, "WaitForMultipleObjects");
    if (!resumed) {
      throw std::runtime_error("The owned bridge server could not be resumed");
    }
    if (failure) {
      std::rethrow_exception(failure);
    }
    std::printf("  Server resumed at client phase %u (%s).\n", pausedPhase,
                observed == WAIT_OBJECT_0 ? "B bytes written while paused" : "bounded wait or worker completion");
    if (observed != WAIT_OBJECT_0) {
      throw std::runtime_error("Frame B did not overwrite the upload storage while server dispatch was paused");
    }
    const bool first = device.checkPixels(0, kRed, kRed);
    const bool second = device.checkPixels(1, kBlue, partialTexture ? kRed : kBlue);
    return first && second;
  }

  bool testTexture(Device& device) {
    ComPtr<IDirect3DTexture9> texture;
    check(device.pDevice->CreateTexture(kTextureSize, kTextureSize, 1, 0, D3DFMT_A8R8G8B8,
                                        D3DPOOL_MANAGED, &texture, nullptr), "CreateTexture");
    const auto upload = [&](bool second) {
      const RECT updateRect = { 1, 1, 2, 3 };
      D3DLOCKED_RECT locked = {};
      check(texture->LockRect(0, &locked, second ? &updateRect : nullptr, 0), "Texture::LockRect");
      const UINT width = second ? updateRect.right - updateRect.left : kTextureSize;
      const UINT height = second ? updateRect.bottom - updateRect.top : kTextureSize;
      for (UINT y = 0; y < height; ++y) {
        auto* row = reinterpret_cast<DWORD*>(static_cast<BYTE*>(locked.pBits) + y * locked.Pitch);
        for (UINT x = 0; x < width; ++x) {
          row[x] = second ? kBlue : kRed;
        }
      }
      check(texture->UnlockRect(0), "Texture::UnlockRect");
      if (second) {
        check(texture->LockRect(0, &locked, nullptr, D3DLOCK_READONLY), "Texture::LockRect read-only");
        bool preserved = true;
        for (UINT y = 0; y < kTextureSize; ++y) {
          const auto* row = reinterpret_cast<const DWORD*>(static_cast<const BYTE*>(locked.pBits) + y * locked.Pitch);
          for (UINT x = 0; x < kTextureSize; ++x) {
            const LONG pixelX = static_cast<LONG>(x);
            const LONG pixelY = static_cast<LONG>(y);
            const bool updated = pixelX >= updateRect.left && pixelX < updateRect.right && pixelY >= updateRect.top && pixelY < updateRect.bottom;
            preserved = preserved && row[x] == (updated ? kBlue : kRed);
          }
        }
        check(texture->UnlockRect(0), "Texture::UnlockRect read-only");
        if (!preserved) {
          throw std::runtime_error("Texture partial update lost canonical bytes");
        }
      }
    };
    upload(false);
    check(device.pDevice->SetTexture(0, texture.Get()), "SetTexture");
    check(device.pDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE), "SetTextureStageState texture");
    check(device.pDevice->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1), "SetFVF texture");
    struct Vertex { float x, y, z, rhw, u, v; };
    const float farEdge = 2.0f * kSize - 0.5f;
    const Vertex vertices[] = {
      { -0.5f, -0.5f, 0.5f, 1.0f, 0.0f, 0.0f },
      { farEdge, -0.5f, 0.5f, 1.0f, 2.0f, 0.0f },
      { -0.5f, farEdge, 0.5f, 1.0f, 0.0f, 2.0f },
    };
    return testUploads(device, "texture", upload, [&]() {
      check(device.pDevice->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, vertices, sizeof(Vertex)), "DrawPrimitiveUP texture");
    }, true);
  }

  void setColorDrawing(Device& device, IDirect3DVertexBuffer9* pVertices) {
    check(device.pDevice->SetTexture(0, nullptr), "SetTexture null");
    check(device.pDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE), "SetTextureStageState diffuse");
    check(device.pDevice->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE), "SetFVF color");
    check(device.pDevice->SetStreamSource(0, pVertices, 0, sizeof(ColorVertex)), "SetStreamSource");
  }

  bool testVertexBuffer(Device& device, bool dynamic) {
    ColorVertex expected[3];
    fillTriangle(expected, kRed);
    ComPtr<IDirect3DVertexBuffer9> vertices;
    check(device.pDevice->CreateVertexBuffer(sizeof(expected), dynamic ? D3DUSAGE_DYNAMIC : 0,
                                             D3DFVF_XYZRHW | D3DFVF_DIFFUSE,
                                             dynamic ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED,
                                             &vertices, nullptr), "CreateVertexBuffer");
    const auto upload = [&](bool second) {
      void* data = nullptr;
      fillTriangle(expected, second ? kBlue : kRed);
      if (!second) {
        check(vertices->Lock(0, sizeof(expected), &data, 0), "VertexBuffer::Lock A");
        std::memcpy(data, expected, sizeof(expected));
        check(vertices->Unlock(), "VertexBuffer::Unlock A");
      } else {
        for (UINT i = 0; i < 3; ++i) {
          check(vertices->Lock(i * sizeof(ColorVertex) + offsetof(ColorVertex, color), sizeof(DWORD), &data, 0),
                "VertexBuffer::Lock partial B");
          std::memcpy(data, &kBlue, sizeof(kBlue));
          check(vertices->Unlock(), "VertexBuffer::Unlock partial B");
        }
        check(vertices->Lock(0, sizeof(expected), &data, D3DLOCK_READONLY), "VertexBuffer::Lock read-only");
        const bool preserved = std::memcmp(data, expected, sizeof(expected)) == 0;
        check(vertices->Unlock(), "VertexBuffer::Unlock read-only");
        if (!preserved) {
          throw std::runtime_error("Vertex buffer partial update lost canonical bytes");
        }
      }
    };
    upload(false);
    setColorDrawing(device, vertices.Get());
    return testUploads(device, dynamic ? "dynamic vertex buffer" : "static vertex buffer", upload, [&]() {
      check(device.pDevice->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 1), "DrawPrimitive buffer");
    });
  }

  bool testIndexBuffer(Device& device) {
    ColorVertex vertexData[6];
    fillTriangle(vertexData, kRed);
    fillTriangle(vertexData + 3, kBlue);
    ComPtr<IDirect3DVertexBuffer9> vertices;
    check(device.pDevice->CreateVertexBuffer(sizeof(vertexData), 0, D3DFVF_XYZRHW | D3DFVF_DIFFUSE,
                                             D3DPOOL_MANAGED, &vertices, nullptr), "CreateVertexBuffer indexed");
    void* data = nullptr;
    check(vertices->Lock(0, sizeof(vertexData), &data, 0), "VertexBuffer::Lock indexed");
    std::memcpy(data, vertexData, sizeof(vertexData));
    check(vertices->Unlock(), "VertexBuffer::Unlock indexed");
    ComPtr<IDirect3DIndexBuffer9> indices;
    WORD expected[] = { 0, 1, 2, 5, 4, 3 };
    check(device.pDevice->CreateIndexBuffer(sizeof(expected), 0, D3DFMT_INDEX16,
                                            D3DPOOL_MANAGED, &indices, nullptr), "CreateIndexBuffer");
    check(indices->Lock(0, sizeof(expected), &data, 0), "IndexBuffer::Lock preallocate");
    std::memcpy(data, expected, sizeof(expected));
    check(indices->Unlock(), "IndexBuffer::Unlock preallocate");
    const auto upload = [&](bool second) {
      expected[0] = static_cast<WORD>(second ? 3 : 0);
      expected[1] = static_cast<WORD>(second ? 4 : 1);
      expected[2] = static_cast<WORD>(second ? 5 : 2);
      void* locked = nullptr;
      check(indices->Lock(0, 3 * sizeof(WORD), &locked, 0), "IndexBuffer::Lock partial");
      std::memcpy(locked, expected, 3 * sizeof(WORD));
      check(indices->Unlock(), "IndexBuffer::Unlock partial");
      if (second) {
        check(indices->Lock(0, sizeof(expected), &locked, D3DLOCK_READONLY), "IndexBuffer::Lock read-only");
        const bool preserved = std::memcmp(locked, expected, sizeof(expected)) == 0;
        check(indices->Unlock(), "IndexBuffer::Unlock read-only");
        if (!preserved) {
          throw std::runtime_error("Index buffer partial update lost canonical bytes");
        }
      }
    };
    setColorDrawing(device, vertices.Get());
    check(device.pDevice->SetIndices(indices.Get()), "SetIndices");
    return testUploads(device, "index buffer", upload, [&]() {
      check(device.pDevice->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 6, 0, 1), "DrawIndexedPrimitive buffer");
    });
  }

  bool testSurfaceUnlockFailure(Device& device) {
    ComPtr<IDirect3DSurface9> surface;
    check(device.pDevice->CreateOffscreenPlainSurface(kTextureSize, kTextureSize, D3DFMT_A8R8G8B8,
                                                     D3DPOOL_SYSTEMMEM, &surface, nullptr), "CreateOffscreenPlainSurface OOM");
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
      const DWORD expected = attempt ? kBlue : kRed;
      D3DLOCKED_RECT locked = {};
      check(surface->LockRect(&locked, nullptr, 0), "Surface::LockRect before OOM");
      for (UINT y = 0; y < kTextureSize; ++y) {
        auto* pRow = reinterpret_cast<DWORD*>(static_cast<BYTE*>(locked.pBits) + y * locked.Pitch);
        for (UINT x = 0; x < kTextureSize; ++x) {
          pRow[x] = expected;
        }
      }
      expectResult(surface->UnlockRect(), E_OUTOFMEMORY, "Surface::UnlockRect snapshot OOM");
      expectResult(surface->UnlockRect(), S_OK, "Surface::UnlockRect empty after OOM");
      check(surface->LockRect(&locked, nullptr, D3DLOCK_READONLY), "Surface::LockRect read-only after OOM");
      bool preserved = true;
      for (UINT y = 0; y < kTextureSize; ++y) {
        const auto* pRow = reinterpret_cast<const DWORD*>(static_cast<const BYTE*>(locked.pBits) + y * locked.Pitch);
        for (UINT x = 0; x < kTextureSize; ++x) {
          preserved = preserved && pRow[x] == expected;
        }
      }
      expectResult(surface->UnlockRect(), S_OK, "Surface::UnlockRect read-only after OOM");
      expectResult(surface->UnlockRect(), S_OK, "Surface::UnlockRect empty after read-only");
      if (!preserved) {
        throw std::runtime_error("Surface snapshot OOM lost canonical bytes");
      }
      std::printf("  Surface snapshot OOM attempt %u consumed its lock and preserved canonical bytes.\n", attempt + 1);
    }
    return true;
  }

  template<typename Buffer>
  bool testBufferUnlockFailure(Buffer* pBuffer, const char* pName) {
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
      const DWORD color = attempt ? kBlue : kRed;
      const DWORD expected[] = { color, ~color, color, ~color };
      void* pData = nullptr;
      check(pBuffer->Lock(0, sizeof(expected), &pData, 0), "Buffer::Lock before OOM");
      std::memcpy(pData, expected, sizeof(expected));
      expectResult(pBuffer->Unlock(), E_OUTOFMEMORY, "Buffer::Unlock snapshot OOM");
      expectResult(pBuffer->Unlock(), S_OK, "Buffer::Unlock empty after OOM");
      check(pBuffer->Lock(0, sizeof(expected), &pData, D3DLOCK_READONLY), "Buffer::Lock read-only after OOM");
      const bool preserved = std::memcmp(pData, expected, sizeof(expected)) == 0;
      expectResult(pBuffer->Unlock(), S_OK, "Buffer::Unlock read-only after OOM");
      expectResult(pBuffer->Unlock(), S_OK, "Buffer::Unlock empty after read-only");
      if (!preserved) {
        throw std::runtime_error("Buffer snapshot OOM lost canonical bytes");
      }
      std::printf("  %s snapshot OOM attempt %u consumed its lock and preserved canonical bytes.\n", pName, attempt + 1);
    }
    return true;
  }

  bool testVertexBufferUnlockFailure(Device& device) {
    ComPtr<IDirect3DVertexBuffer9> vertices;
    check(device.pDevice->CreateVertexBuffer(4 * sizeof(DWORD), 0, 0, D3DPOOL_MANAGED,
                                             &vertices, nullptr), "CreateVertexBuffer OOM");
    return testBufferUnlockFailure(vertices.Get(), "Vertex buffer");
  }

  bool testIndexBufferUnlockFailure(Device& device) {
    ComPtr<IDirect3DIndexBuffer9> indices;
    check(device.pDevice->CreateIndexBuffer(4 * sizeof(DWORD), 0, D3DFMT_INDEX16, D3DPOOL_MANAGED,
                                            &indices, nullptr), "CreateIndexBuffer OOM");
    return testBufferUnlockFailure(indices.Get(), "Index buffer");
  }
}

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  HWND window = nullptr;
  bool passed = false;
  try {
    const bool lockstep = lockstepTest();
    char caseName[32] = {};
    const bool selected = GetEnvironmentVariableA("REMIX_SHARED_UPLOAD_TEST_CASE", caseName, sizeof(caseName)) != 0;
    const auto runCase = [&](const char* pName) { return !selected || std::strcmp(caseName, pName) == 0; };
    const bool oom = selected && (runCase("oom_surface") || runCase("oom_vb") || runCase("oom_ib"));
    if (selected && !oom && !runCase("texture") && !runCase("static_vb") && !runCase("dynamic_vb") && !runCase("indices")) {
      throw std::runtime_error("Unknown REMIX_SHARED_UPLOAD_TEST_CASE");
    }
    std::ofstream config(".trex/bridge.conf", std::ios::app);
    config << "useSharedHeap = True\nsharedHeapPolicy = Textures,DynamicBuffers,StaticBuffers\n"
           << "useShadowMemoryForDynamicBuffers = False\nclient.asyncQueries = False\n"
           << "sendAllServerResponses = False\nsendCreateFunctionServerResponses = True\n"
           << "presentSemaphoreEnabled = True\npresentSemaphoreMaxFrames = " << (lockstep ? 0 : 1) << "\n";
    if (oom) {
      // The canonical allocation fills the heap, and a zero wait expires before heap growth.
      config << "sharedHeapDefaultSegmentSize = 4096\nsharedHeapChunkSize = 4096\nsharedHeapFreeChunkWaitTimeout = 0\n";
    }
    config.close();
    window = createWindow();
    checkSystem(window != nullptr, "CreateWindowExW");
    Device device(window, lockstep);
    if (oom) {
      logLine("Testing shared upload allocation failure with a one-chunk heap...");
    } else {
      logLine(lockstep ? "Testing same-frame shared uploads with N0..." : "Testing cross-frame shared uploads with N1...");
    }
    passed = true;
    if (!oom && runCase("texture")) {
      passed = testTexture(device) && passed;
    }
    if (!oom && runCase("static_vb")) {
      passed = testVertexBuffer(device, false) && passed;
    }
    if (!oom && runCase("dynamic_vb")) {
      passed = testVertexBuffer(device, true) && passed;
    }
    if (!oom && runCase("indices")) {
      passed = testIndexBuffer(device) && passed;
    }
    if (oom && runCase("oom_surface")) {
      passed = testSurfaceUnlockFailure(device) && passed;
    }
    if (oom && runCase("oom_vb")) {
      passed = testVertexBufferUnlockFailure(device) && passed;
    }
    if (oom && runCase("oom_ib")) {
      passed = testIndexBufferUnlockFailure(device) && passed;
    }
  } catch (const std::exception& error) {
    std::printf("FAILED: %s.\n", error.what());
    passed = false;
  }
  if (window != nullptr) {
    DestroyWindow(window);
  }
  logLine(passed ? "PASSED" : "FAILED");
  return passed ? 0 : 1;
}
