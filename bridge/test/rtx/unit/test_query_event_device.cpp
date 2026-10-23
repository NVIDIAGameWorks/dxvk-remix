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

#include <chrono>
#include <cstdio>
#include <fstream>
#include <future>
#include <mutex>
#include <stdexcept>
#include <vector>

#include <wrl/client.h>

#include "test_remix_api_bridge_app.h"
#include "../../../src/util/util_queryresults.h"
#include "../../../src/util/util_scopedlock.h"

using Microsoft::WRL::ComPtr;
using namespace bridge_app_test;

namespace {
  constexpr UINT kSize = 128;

  void check(HRESULT result, const char* pCall) {
    if (FAILED(result)) {
      std::printf("FAILED: %s returned 0x%08lX.\n", pCall, static_cast<unsigned long>(result));
      throw std::runtime_error(pCall);
    }
  }

  bool optionEnabled(const char* pName) {
    char value[8] = {};
    return GetEnvironmentVariableA(pName, value, sizeof(value)) && value[0] == '1';
  }

  HWND createWindow() {
    WNDCLASSW windowClass = {};
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"BridgeQueryEventTest";
    RegisterClassW(&windowClass);
    return CreateWindowExW(0, windowClass.lpszClassName, L"", WS_OVERLAPPEDWINDOW,
                           0, 0, kSize, kSize, nullptr, nullptr, windowClass.hInstance, nullptr);
  }

  void queueGpuWork(IDirect3DDevice9* pDevice, IDirect3DSurface9* pTarget) {
    for (unsigned i = 0; i < 16; ++i) {
      check(pDevice->ColorFill(pTarget, nullptr, 0xff000000 | i * 0x00111111), "ColorFill");
    }
  }

  void waitForEvent(IDirect3DQuery9* pQuery, bool statusOnly, DWORD flags = D3DGETDATA_FLUSH) {
    const ULONGLONG deadline = GetTickCount64() + 10000;
    unsigned polls = 0;
    for (;;) {
      BOOL complete = FALSE;
      const HRESULT result = pQuery->GetData(statusOnly ? nullptr : &complete,
                                              statusOnly ? 0 : sizeof(complete), flags);
      ++polls;
      check(result, "Event::GetData");
      if (result == S_OK) {
        if (!statusOnly && complete != TRUE) {
          throw std::runtime_error("A completed GPU event did not return TRUE");
        }
        std::printf("  %s event completed after %u polls.\n", statusOnly ? "Status-only" : "BOOL", polls);
        return;
      }
      if (result != S_FALSE || GetTickCount64() >= deadline) {
        throw std::runtime_error("The GPU event did not complete within ten seconds");
      }
      Sleep(1);
    }
  }

  void checkCompletedResult(IDirect3DQuery9* pQuery) {
    for (unsigned i = 0; i < 3; ++i) {
      BOOL complete = FALSE;
      const HRESULT result = pQuery->GetData(&complete, sizeof(complete), 0);
      check(result, "Completed event::GetData");
      if (result != S_OK || complete != TRUE) {
        throw std::runtime_error("A completed event result was not retained");
      }
    }
  }

  void testEvent(IDirect3DDevice9* pDevice, IDirect3DSurface9* pTarget) {
    ComPtr<IDirect3DQuery9> pQuery;
    check(pDevice->CreateQuery(D3DQUERYTYPE_EVENT, &pQuery), "CreateQuery");
    if (pQuery->GetType() != D3DQUERYTYPE_EVENT || pQuery->GetDataSize() != sizeof(BOOL)) {
      throw std::runtime_error("The event query reports invalid metadata");
    }
    queueGpuWork(pDevice, pTarget);
    check(pQuery->Issue(D3DISSUE_END), "Event::Issue");
    waitForEvent(pQuery.Get(), true);
    checkCompletedResult(pQuery.Get());

    logLine("Reissuing the completed GPU event...");
    queueGpuWork(pDevice, pTarget);
    check(pQuery->Issue(D3DISSUE_END), "Event::Issue again");
    waitForEvent(pQuery.Get(), false);
    checkCompletedResult(pQuery.Get());
  }

  void testSlotExhaustion(IDirect3DDevice9* pDevice, IDirect3DSurface9* pTarget) {
    logLine("Testing the blocking fallback with all asynchronous slots occupied...");
    std::vector<ComPtr<IDirect3DQuery9>> queries(bridge_util::QuerySharedBlock::kMaxSlots);
    for (auto& pQuery : queries) {
      check(pDevice->CreateQuery(D3DQUERYTYPE_EVENT, &pQuery), "CreateQuery for occupied slot");
    }
    testEvent(pDevice, pTarget);
  }

  void testConcurrentEvent(IDirect3DDevice9* pDevice, IDirect3DSurface9* pTarget) {
    logLine("Testing parent-device exclusion for concurrent production query calls...");
    auto* const pSync = dynamic_cast<bridge_util::Syncable*>(pDevice);
    if (pSync == nullptr) {
      throw std::runtime_error("The bridge device does not expose multithreaded synchronization");
    }
    ComPtr<IDirect3DQuery9> pQuery;
    check(pDevice->CreateQuery(D3DQUERYTYPE_EVENT, &pQuery), "CreateQuery for concurrent callers");
    queueGpuWork(pDevice, pTarget);
    check(pQuery->Issue(D3DISSUE_END), "Issue before concurrent polling");

    std::promise<void> firstStarted;
    std::promise<void> secondStarted;
    auto firstStart = firstStarted.get_future();
    auto secondStart = secondStarted.get_future();
    std::future<HRESULT> firstPoll;
    std::future<HRESULT> secondPoll;
    // The futures outlive this guard, so exceptions release the lock before waiting on callers.
    std::unique_lock<bridge_util::Syncable> deviceLock(*pSync);
    firstPoll = std::async(std::launch::async, [&]() {
      firstStarted.set_value();
      return pQuery->GetData(nullptr, 0, 0);
    });
    secondPoll = std::async(std::launch::async, [&]() {
      secondStarted.set_value();
      return pQuery->GetData(nullptr, 0, D3DGETDATA_FLUSH);
    });
    const bool callersStarted = firstStart.wait_for(std::chrono::seconds(2)) == std::future_status::ready &&
                                secondStart.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
    const bool firstBlocked = firstPoll.wait_for(std::chrono::milliseconds(100)) == std::future_status::timeout;
    const bool secondBlocked = secondPoll.wait_for(std::chrono::milliseconds(100)) == std::future_status::timeout;
    deviceLock.unlock();
    check(firstPoll.get(), "First concurrent GetData");
    check(secondPoll.get(), "Second concurrent GetData");
    if (!callersStarted || !firstBlocked || !secondBlocked) {
      throw std::runtime_error("Concurrent GetData entered while the parent device was locked");
    }
    waitForEvent(pQuery.Get(), true, 0);
    checkCompletedResult(pQuery.Get());

    std::promise<void> issueStarted;
    std::promise<void> pollStarted;
    auto issueStart = issueStarted.get_future();
    auto pollStart = pollStarted.get_future();
    std::future<HRESULT> issue;
    std::future<HRESULT> poll;
    std::unique_lock<bridge_util::Syncable> reissueLock(*pSync);
    issue = std::async(std::launch::async, [&]() {
      issueStarted.set_value();
      return pQuery->Issue(D3DISSUE_END);
    });
    poll = std::async(std::launch::async, [&]() {
      pollStarted.set_value();
      return pQuery->GetData(nullptr, 0, D3DGETDATA_FLUSH);
    });
    const bool reissueCallersStarted = issueStart.wait_for(std::chrono::seconds(2)) == std::future_status::ready &&
                                      pollStart.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
    const bool issueBlocked = issue.wait_for(std::chrono::milliseconds(100)) == std::future_status::timeout;
    const bool pollBlocked = poll.wait_for(std::chrono::milliseconds(100)) == std::future_status::timeout;
    reissueLock.unlock();
    check(issue.get(), "Concurrent Issue");
    check(poll.get(), "GetData concurrent with Issue");
    if (!reissueCallersStarted || !issueBlocked || !pollBlocked) {
      throw std::runtime_error("Issue or cached GetData entered while the parent device was locked");
    }
    waitForEvent(pQuery.Get(), false);
    checkCompletedResult(pQuery.Get());
  }
}

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  const HWND hWnd = createWindow();
  if (hWnd == nullptr) {
    logLine("FAILED: Could not create a test window.");
    return 1;
  }
  bool passed = false;
  try {
    const bool blocking = optionEnabled("REMIX_QUERY_TEST_BLOCKING");
    std::ofstream config(".trex/bridge.conf", std::ios::app);
    config << "client.asyncQueries = " << (blocking ? "False" : "True") << "\n"
           << "sendCreateFunctionServerResponses = True\n";
    config.close();
    logLine(blocking ? "Testing event queries with asynchronous queries disabled..."
                     : "Testing asynchronous event queries against the server's system D3D9 runtime...");
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
    testEvent(pDevice.Get(), pTarget.Get());
    if (!blocking) {
      testConcurrentEvent(pDevice.Get(), pTarget.Get());
    }
    if (!blocking && optionEnabled("REMIX_QUERY_TEST_EXHAUST_SLOTS")) {
      testSlotExhaustion(pDevice.Get(), pTarget.Get());
    }
    passed = true;
  } catch (const std::exception& error) {
    std::printf("FAILED: %s.\n", error.what());
  }
  DestroyWindow(hWnd);
  logLine(passed ? "PASSED" : "FAILED");
  return passed ? 0 : 1;
}
