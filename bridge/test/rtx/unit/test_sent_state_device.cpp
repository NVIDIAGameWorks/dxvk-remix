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
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <thread>

#include <wrl/client.h>

#include "test_remix_api_bridge_app.h"

using Microsoft::WRL::ComPtr;
using namespace bridge_app_test;

namespace {
  constexpr UINT kSize = 32;
  constexpr DWORD kRed = 0xffff0000;
  constexpr DWORD kBlue = 0xff0000ff;
  constexpr DWORD kBlack = 0xff000000;

  struct Vertex {
    float x;
    float y;
    float z;
    DWORD color;
  };

  void check(HRESULT result, const char* pCall) {
    if (FAILED(result)) {
      std::printf("FAILED: %s returned 0x%08lX.\n", pCall, static_cast<unsigned long>(result));
      throw std::runtime_error(pCall);
    }
  }

  HWND createWindow() {
    WNDCLASSW windowClass = {};
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"BridgeSentStateTest";
    RegisterClassW(&windowClass);
    RECT windowRect = { 0, 0, kSize, kSize };
    if (!AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE)) {
      return nullptr;
    }
    return CreateWindowExW(0, windowClass.lpszClassName, L"", WS_OVERLAPPEDWINDOW,
                           0, 0, windowRect.right - windowRect.left, windowRect.bottom - windowRect.top,
                           nullptr, nullptr, windowClass.hInstance, nullptr);
  }

  void fillTriangle(Vertex (&vertices)[3], DWORD color) {
    vertices[0] = { -1.0f, 1.0f, 0.5f, color };
    vertices[1] = { 3.0f, 1.0f, 0.5f, color };
    vertices[2] = { -1.0f, -3.0f, 0.5f, color };
  }

  bool checkPixel(IDirect3DDevice9* pDevice, IDirect3DSurface9* pTarget, UINT x, UINT y, DWORD expected) {
    ComPtr<IDirect3DSurface9> pReadback;
    check(pDevice->CreateOffscreenPlainSurface(kSize, kSize, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM,
                                               &pReadback, nullptr), "CreateOffscreenPlainSurface");
    check(pDevice->GetRenderTargetData(pTarget, pReadback.Get()), "GetRenderTargetData");
    D3DLOCKED_RECT locked = {};
    check(pReadback->LockRect(&locked, nullptr, D3DLOCK_READONLY), "LockRect");
    const auto* pRow = reinterpret_cast<const DWORD*>(static_cast<const BYTE*>(locked.pBits) + y * locked.Pitch);
    const DWORD actual = pRow[x];
    check(pReadback->UnlockRect(), "UnlockRect");
    if ((actual & 0xffffff) != (expected & 0xffffff)) {
      std::printf("FAILED: Pixel (%u, %u) is 0x%08lX, expected 0x%08lX.\n", x, y, actual, expected);
      return false;
    }
    return true;
  }

  bool testUpDraw(IDirect3DDevice9* pDevice, IDirect3DSurface9* pTarget, bool indexed) {
    std::printf("Testing %s followed by identical buffer rebinds...\n",
                indexed ? "DrawIndexedPrimitiveUP" : "DrawPrimitiveUP");
    Vertex vertices[3];
    fillTriangle(vertices, kRed);
    const WORD indices[3] = { 0, 1, 2 };
    ComPtr<IDirect3DVertexBuffer9> pVertices;
    ComPtr<IDirect3DIndexBuffer9> pIndices;
    check(pDevice->CreateVertexBuffer(sizeof(vertices), 0, D3DFVF_XYZ | D3DFVF_DIFFUSE,
                                      D3DPOOL_MANAGED, &pVertices, nullptr), "CreateVertexBuffer");
    check(pDevice->CreateIndexBuffer(sizeof(indices), 0, D3DFMT_INDEX16,
                                     D3DPOOL_MANAGED, &pIndices, nullptr), "CreateIndexBuffer");
    void* pData = nullptr;
    check(pVertices->Lock(0, sizeof(vertices), &pData, 0), "VertexBuffer::Lock");
    std::memcpy(pData, vertices, sizeof(vertices));
    check(pVertices->Unlock(), "VertexBuffer::Unlock");
    check(pIndices->Lock(0, sizeof(indices), &pData, 0), "IndexBuffer::Lock");
    std::memcpy(pData, indices, sizeof(indices));
    check(pIndices->Unlock(), "IndexBuffer::Unlock");
    check(pDevice->SetStreamSource(0, pVertices.Get(), 0, sizeof(Vertex)), "SetStreamSource");
    check(pDevice->SetIndices(pIndices.Get()), "SetIndices");
    fillTriangle(vertices, kBlue);
    check(pDevice->Clear(0, nullptr, D3DCLEAR_TARGET, kBlack, 1.0f, 0), "Clear");
    check(pDevice->BeginScene(), "BeginScene");
    if (indexed) {
      check(pDevice->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 3, 1, indices, D3DFMT_INDEX16,
                                             vertices, sizeof(Vertex)), "DrawIndexedPrimitiveUP");
    } else {
      check(pDevice->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, vertices, sizeof(Vertex)), "DrawPrimitiveUP");
    }
    check(pDevice->EndScene(), "EndScene");
    bool passed = checkPixel(pDevice, pTarget, 16, 16, kBlue);

    // These are the same bindings cached before the UP draw, which clears server bindings.
    check(pDevice->SetStreamSource(0, pVertices.Get(), 0, sizeof(Vertex)), "SetStreamSource after UP");
    check(pDevice->SetIndices(pIndices.Get()), "SetIndices after UP");
    check(pDevice->Clear(0, nullptr, D3DCLEAR_TARGET, kBlack, 1.0f, 0), "Clear");
    check(pDevice->BeginScene(), "BeginScene");
    check(pDevice->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 3, 0, 1), "DrawIndexedPrimitive after UP");
    check(pDevice->EndScene(), "EndScene");
    passed = checkPixel(pDevice, pTarget, 16, 16, kRed) && passed;
    std::printf("%s: %s\n", indexed ? "DrawIndexedPrimitiveUP" : "DrawPrimitiveUP", passed ? "PASSED" : "FAILED");
    return passed;
  }

  bool testRenderTargets(IDirect3DDevice9* pDevice, IDirect3DSurface9* pTarget) {
    logLine("Testing viewport/scissor reassertion across render-target changes on another thread...");
    const D3DVIEWPORT9 fullViewport = { 0, 0, kSize, kSize, 0.0f, 1.0f };
    const D3DVIEWPORT9 smallViewport = { 8, 8, 8, 8, 0.0f, 1.0f };
    const RECT fullScissor = { 0, 0, kSize, kSize };
    const RECT smallScissor = { 8, 8, 16, 16 };
    Vertex vertices[3];
    fillTriangle(vertices, kRed);
    bool passed = true;
    for (unsigned iteration = 0; iteration < 8; ++iteration) {
      const auto& viewport = iteration & 1 ? fullViewport : smallViewport;
      const auto& scissor = iteration & 1 ? smallScissor : fullScissor;
      check(pDevice->SetViewport(&viewport), "SetViewport before render-target change");
      check(pDevice->SetScissorRect(&scissor), "SetScissorRect before render-target change");
      HRESULT targetResult = E_FAIL;
      std::thread targetThread([&]() { targetResult = pDevice->SetRenderTarget(0, pTarget); });
      targetThread.join();
      check(targetResult, "SetRenderTarget on another thread");
      check(pDevice->SetViewport(&viewport), "SetViewport after render-target change");
      check(pDevice->SetScissorRect(&scissor), "SetScissorRect after render-target change");
      check(pDevice->ColorFill(pTarget, nullptr, kBlack), "ColorFill");
      check(pDevice->BeginScene(), "BeginScene");
      check(pDevice->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, vertices, sizeof(Vertex)), "DrawPrimitiveUP clipped");
      check(pDevice->EndScene(), "EndScene");
      passed = checkPixel(pDevice, pTarget, 12, 12, kRed) && passed;
      passed = checkPixel(pDevice, pTarget, 4, 4, kBlack) && passed;
    }
    return passed;
  }

  bool checkDeviceLost(HRESULT result, const char* pCall) {
    if (result != D3DERR_DEVICELOST) {
      std::printf("FAILED: %s returned 0x%08lX, expected D3DERR_DEVICELOST.\n",
                  pCall, static_cast<unsigned long>(result));
      return false;
    }
    return true;
  }

  bool testClosingWindow(IDirect3DDevice9* pDevice, HWND& hWnd) {
    logLine("Testing queued and concurrent Presents while destroying the window with the device alive...");
    ComPtr<IDirect3DSwapChain9> pSwapChain;
    check(pDevice->GetSwapChain(0, &pSwapChain), "GetSwapChain");
    ComPtr<IDirect3DSurface9> pBackBuffer;
    check(pSwapChain->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer), "GetBackBuffer");
    check(pDevice->SetRenderTarget(0, pBackBuffer.Get()), "SetRenderTarget backbuffer");
    for (unsigned i = 0; i < 16; ++i) {
      check(pDevice->ColorFill(pBackBuffer.Get(), nullptr, i & 1 ? kRed : kBlue), "ColorFill before Present");
      check(pDevice->Present(nullptr, nullptr, nullptr, nullptr), "Present before window destruction");
    }

    std::atomic<bool> renderStarted = false;
    HRESULT renderResult = S_OK;
    unsigned acceptedPresents = 0;
    std::thread renderThread([&]() {
      renderStarted = true;
      for (unsigned i = 0; i < 64; ++i) {
        renderResult = i & 1
          ? pSwapChain->Present(nullptr, nullptr, nullptr, nullptr, 0)
          : pDevice->Present(nullptr, nullptr, nullptr, nullptr);
        if (renderResult != S_OK) {
          break;
        }
        ++acceptedPresents;
      }
    });
    const ULONGLONG deadline = GetTickCount64() + 5000;
    while (!renderStarted && GetTickCount64() < deadline) {
      Sleep(1);
    }
    const HWND hClosingWindow = hWnd;
    const BOOL destroyed = DestroyWindow(hWnd);
    renderThread.join();
    if (!destroyed) {
      throw std::runtime_error("DestroyWindow while the device is alive");
    }
    hWnd = nullptr;
    std::printf("  Concurrent render thread accepted %u Presents before closing.\n", acceptedPresents);
    bool passed = renderResult == S_OK || renderResult == D3DERR_DEVICELOST;
    if (!passed) {
      std::printf("FAILED: Concurrent Present returned 0x%08lX.\n", static_cast<unsigned long>(renderResult));
    }
    passed = checkDeviceLost(pDevice->Present(nullptr, nullptr, nullptr, nullptr), "Device::Present after destruction") && passed;
    passed = checkDeviceLost(pDevice->Present(nullptr, nullptr, hClosingWindow, nullptr), "Device::Present to destroyed HWND") && passed;
    passed = checkDeviceLost(pSwapChain->Present(nullptr, nullptr, nullptr, nullptr, 0), "SwapChain::Present after destruction") && passed;
    passed = checkDeviceLost(pSwapChain->Present(nullptr, nullptr, hClosingWindow, nullptr, 0), "SwapChain::Present to destroyed HWND") && passed;
    return passed;
  }
}

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  HWND hWnd = createWindow();
  if (hWnd == nullptr) {
    return 1;
  }
  bool passed = false;
  try {
    std::ofstream config(".trex/bridge.conf", std::ios::app);
    config << "client.dropRedundantState = True\nclient.batchState = True\nuseSharedHeap = False\npresentSemaphoreMaxFrames = 8\n";
    config.close();
    const HMODULE hClient = loadBridgeClient();
    if (hClient == nullptr) {
      throw std::runtime_error("loadBridgeClient");
    }
    const auto pfnCreate = reinterpret_cast<PFN_Direct3DCreate9>(GetProcAddress(hClient, "Direct3DCreate9"));
    ComPtr<IDirect3D9> pD3D;
    pD3D.Attach(pfnCreate != nullptr ? pfnCreate(D3D_SDK_VERSION) : nullptr);
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
    ComPtr<IDirect3DDevice9> pDevice;
    check(pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd,
                              D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED,
                              &present, &pDevice), "CreateDevice");
    ComPtr<IDirect3DSurface9> pTarget;
    check(pDevice->CreateRenderTarget(kSize, kSize, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE,
                                      0, FALSE, &pTarget, nullptr), "CreateRenderTarget");
    check(pDevice->SetRenderTarget(0, pTarget.Get()), "SetRenderTarget");
    check(pDevice->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE), "SetFVF");
    check(pDevice->SetRenderState(D3DRS_LIGHTING, FALSE), "SetRenderState lighting");
    check(pDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE), "SetRenderState culling");
    check(pDevice->SetRenderState(D3DRS_ZENABLE, FALSE), "SetRenderState depth");
    check(pDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1), "SetTextureStageState color");
    check(pDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE), "SetTextureStageState diffuse");
    D3DMATRIX identity = {};
    identity._11 = identity._22 = identity._33 = identity._44 = 1.0f;
    check(pDevice->SetTransform(D3DTS_WORLD, &identity), "SetTransform world");
    check(pDevice->SetTransform(D3DTS_VIEW, &identity), "SetTransform view");
    check(pDevice->SetTransform(D3DTS_PROJECTION, &identity), "SetTransform projection");
    char caseName[32] = {};
    const char* pCase = GetEnvironmentVariableA("REMIX_SENT_STATE_TEST_CASE", caseName, sizeof(caseName)) ? caseName : nullptr;
    passed = true;
    if (pCase == nullptr || std::strcmp(pCase, "primitive") == 0) {
      passed = testUpDraw(pDevice.Get(), pTarget.Get(), false) && passed;
    }
    if (pCase == nullptr || std::strcmp(pCase, "indexed") == 0) {
      passed = testUpDraw(pDevice.Get(), pTarget.Get(), true) && passed;
    }
    if (pCase == nullptr || std::strcmp(pCase, "render_targets") == 0) {
      check(pDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE), "SetRenderState scissor");
      passed = testRenderTargets(pDevice.Get(), pTarget.Get()) && passed;
    }
    if (pCase == nullptr || std::strcmp(pCase, "closing_window") == 0) {
      passed = testClosingWindow(pDevice.Get(), hWnd) && passed;
    }
  } catch (const std::exception& error) {
    std::printf("FAILED: %s.\n", error.what());
    passed = false;
  }
  if (hWnd != nullptr) {
    DestroyWindow(hWnd);
  }
  logLine(passed ? "PASSED" : "FAILED");
  return passed ? 0 : 1;
}
