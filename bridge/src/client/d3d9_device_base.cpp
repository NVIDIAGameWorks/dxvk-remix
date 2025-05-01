/*
 * Copyright (c) 2023-2024, NVIDIA CORPORATION. All rights reserved.
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
#include "d3d9_device_base.h"

#include "d3d9_lss.h"
#include "window.h"

#include "util_modulecommand.h"

#include <d3d9.h>

BaseDirect3DDevice9Ex_LSS::BaseDirect3DDevice9Ex_LSS(const bool bExtended,
                                                     Direct3D9Ex_LSS* const pDirect3D,
                                                     const D3DDEVICE_CREATION_PARAMETERS& createParams,
                                                     const D3DPRESENT_PARAMETERS& presParams,
                                                     const D3DDISPLAYMODEEX* const pFullscreenDisplayMode,
                                                     HRESULT& hresultOut)
  : D3DBase<IDirect3DDevice9Ex>(nullptr, pDirect3D)
  , m_ex(bExtended)
  , m_pDirect3D(pDirect3D)
  , m_createParams(createParams)
  , m_presParams(presParams) {
  Logger::debug("Creating Device...");
  
  // Initialize WndProc logic
  WndProc::set(getWinProcHwnd());

  //Setting default value
  m_maxFrameLatency = 3;

  // D3D9 seems to inialize its state to this
  memset(&m_state.renderStates[0], 0xBAADCAFE, sizeof(m_state.renderStates));

  // Initialize the transforms
  memset(&m_state.transforms[0], 0, sizeof(m_state.transforms));
  for (uint32_t i = 0; i < 256; i++) {
    m_state.transforms[i].m[0][0] = 1.f;
    m_state.transforms[i].m[1][1] = 1.f;
    m_state.transforms[i].m[2][2] = 1.f;
    m_state.transforms[i].m[3][3] = 1.f;
  }

  // Initialize the implicit viewport
  memset(&m_state.viewport, 0, sizeof(m_state.viewport));
  m_state.viewport.Width = m_presParams.BackBufferWidth;
  m_state.viewport.Height = m_presParams.BackBufferHeight;
  memset(&m_state.scissorRect, 0, sizeof(m_state.scissorRect));
  m_state.scissorRect.right = m_presParams.BackBufferWidth;
  m_state.scissorRect.bottom = m_presParams.BackBufferHeight;
  m_state.viewport.MaxZ = 1.f;

  // Games may override client's exception handler when it was setup early.
  // Attempt to restore the exeption handler.
  SetupExceptionHandler();

  assert(m_createParams.hFocusWindow || m_presParams.hDeviceWindow);

  m_previousPresentParams = presParams;
  m_bSoftwareVtxProcessing = (createParams.BehaviorFlags & D3DCREATE_SOFTWARE_VERTEXPROCESSING) ? true : false;
  DWORD customBehaviorFlags = createParams.BehaviorFlags | D3DCREATE_NOWINDOWCHANGES;
  InitRamp();
  
  UID currentUID = 0;
  {
    ClientMessage c(m_ex ? Commands::IDirect3D9Ex_CreateDeviceEx : Commands::IDirect3D9Ex_CreateDevice, getId());
    currentUID = c.get_uid();
    c.send_many(           createParams.AdapterOrdinal,
                           createParams.DeviceType,
                (uint32_t) createParams.hFocusWindow,
                           customBehaviorFlags);
    if (m_ex) {
      if (!pFullscreenDisplayMode) {
        Logger::err("A null pFullscreenDisplayMode was passed to IDirect3D9Ex::CreateDeviceEx().");
      }
      c.send_data(sizeof(D3DDISPLAYMODEEX), pFullscreenDisplayMode);
    }
    c.send_data(sizeof(D3DPRESENT_PARAMETERS), &m_presParams);
  }
  Logger::debug("...server-side D3D9 device creation command sent...");

  Logger::debug("...waiting for create device ack response from server...");
  if (Result::Success != DeviceBridge::waitForCommand(Commands::Bridge_Response, 0, nullptr, true, currentUID)) {
    Logger::err("...server-side D3D9 device creation failed with: no response from server.");
    WndProc::unset();
    hresultOut = D3DERR_DEVICELOST;
    return;
  }
  Logger::debug("...create device response received from server...");
  const auto header = DeviceBridge::pop_front();

  // Grab hresult from server
  hresultOut = (HRESULT) DeviceBridge::get_data();
  assert(DeviceBridge::get_data_pos() == header.dataOffset);

  if (FAILED(hresultOut)) {
    Logger::err(format_string("...server-side D3D9 device creation failed with %x.", hresultOut));
    // Release client device and report server error to the app
    WndProc::unset();
    return;
  }
  Logger::debug("...server-side D3D9 device successfully created...");
  Logger::debug("...Device successfully created!");
}

void BaseDirect3DDevice9Ex_LSS::InitRamp() {
  for (uint32_t i = 0; i < NumControlPoints; i++) {
    DWORD identity = DWORD(MapGammaControlPoint(float(i) / float(NumControlPoints - 1)));

    m_gammaRamp.red[i] = identity;
    m_gammaRamp.green[i] = identity;
    m_gammaRamp.blue[i] = identity;
  }
}