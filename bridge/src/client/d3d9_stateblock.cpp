/*
 * Copyright (c) 2022-2023, NVIDIA CORPORATION. All rights reserved.
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
#include "pch.h"
#include "d3d9_lss.h"


/*
 * Direct3DStateBlock9_LSS Interface Implementation
 */

HRESULT Direct3DStateBlock9_LSS::QueryInterface(REFIID riid, LPVOID* ppvObj) {
  LogFunctionCall();
  if (ppvObj == nullptr)
    return E_POINTER;

  *ppvObj = nullptr;

  if (riid == __uuidof(IUnknown)
    || riid == __uuidof(IDirect3DStateBlock9)) {
    *ppvObj = bridge_cast<IDirect3DStateBlock9*>(this);
    AddRef();
    return S_OK;
  }

  return E_NOINTERFACE;
}

ULONG Direct3DStateBlock9_LSS::AddRef() {
  LogFunctionCall();
  // No push since we only care about the last Release call
  return D3DBase::AddRef();
}

ULONG Direct3DStateBlock9_LSS::Release() {
  LogFunctionCall();
  return D3DBase::Release();
}

void Direct3DStateBlock9_LSS::onDestroy() {
  ClientMessage { Commands::IDirect3DStateBlock9_Destroy, getId() };
}

HRESULT Direct3DStateBlock9_LSS::GetDevice(IDirect3DDevice9** ppDevice) {
  LogFunctionCall();
  if (ppDevice == nullptr) {
    return D3DERR_INVALIDCALL;
  }
  m_pDevice->AddRef();
  (*ppDevice) = m_pDevice;
  return S_OK;
}

void Direct3DStateBlock9_LSS::StateTransfer(const BaseDirect3DDevice9Ex_LSS::StateCaptureDirtyFlags& flags, BaseDirect3DDevice9Ex_LSS::State& src, BaseDirect3DDevice9Ex_LSS::State& dst) {
  for (size_t i = 0; i < dst.renderStates.size(); i++) {
    if (flags.renderStates[i]) {
      dst.renderStates[i] = src.renderStates[i];
    }
  }
  if (flags.vertexDecl) {
    dst.vertexDecl = src.vertexDecl;
  }
  if (flags.indices) {
    dst.indices = src.indices;
  }
  for (int i = 0; i < flags.samplerStates.size(); i++) {
    for (int j = 0; j < flags.samplerStates[i].size(); j++) {
      if (flags.samplerStates[i][j]) {
        dst.samplerStates[i][j] = src.samplerStates[i][j];
      }
    }
  }
  for (int i = 0; i < flags.streams.size(); i++) {
    if (flags.streams[i]) {
      dst.streams[i] = src.streams[i];
    }
  }
  for (int i = 0; i < flags.streamOffsetsAndStrides.size(); i++) {
    if (flags.streamOffsetsAndStrides[i]) {
      dst.streamOffsets[i] = src.streamOffsets[i];
      dst.streamStrides[i] = src.streamStrides[i];
    }
  }
  for (int i = 0; i < flags.streamFreqs.size(); i++) {
    if (flags.streamFreqs[i]) {
      dst.streamFreqs[i] = src.streamFreqs[i];
    }
  }
  for (int i = 0; i < flags.textures.size(); i++) {
    if (flags.textures[i]) {
      dst.textures[i] = src.textures[i];
      dst.textureTypes[i] = src.textureTypes[i];
    }
  }
  if (flags.vertexShader) {
    dst.vertexShader = src.vertexShader;
  }
  if (flags.pixelShader) {
    dst.pixelShader = src.pixelShader;
  }
  if (flags.material) {
    dst.material = src.material;
  }
  for (const auto& [key, value] : flags.lights) {
    dst.lights[key] = src.lights[key];
  }
  for (const auto& [key, value] : flags.bLightEnables) {
    dst.bLightEnables[key] = src.bLightEnables[key];
  }
  for (int i = 0; i < flags.transforms.size(); i++) {
    if (flags.transforms[i]) {
      dst.transforms[i] = src.transforms[i];
    }
  }
  for (int i = 0; i < flags.textureStageStates.size(); i++) {
    for (int j = 0; j < flags.textureStageStates[i].size(); j++) {
      if (flags.textureStageStates[i][j]) {
        dst.textureStageStates[i][j] = src.textureStageStates[i][j];
      }
    }
  }
  if (flags.viewport) {
    dst.viewport = src.viewport;
  }
  if (flags.scissorRect) {
    dst.scissorRect = src.scissorRect;
  }
  for (int i = 0; i < flags.clipPlanes.size(); i++) {
    if (flags.clipPlanes[i]) {
      for (int j = 0; j < 4; j++) {
        dst.clipPlanes[i][j] = src.clipPlanes[i][j];
      }
    }
  }
  for (int i = 0; i < flags.vertexConstants.fConsts.size(); i++) {
    if (flags.vertexConstants.fConsts[i]) {
      dst.vertexConstants.fConsts[i] = src.vertexConstants.fConsts[i];
    }
  }
  for (int i = 0; i < flags.vertexConstants.iConsts.size(); i++) {
    if (flags.vertexConstants.iConsts[i]) {
      dst.vertexConstants.iConsts[i] = src.vertexConstants.iConsts[i];
    }
  }
  for (int i = 0; i < flags.vertexConstants.bConsts.size(); i++) {
    if (flags.vertexConstants.bConsts[i]) {
      size_t dwordIndex = i / 32;
      size_t dwordOffset = i % 32;
      uint32_t bitMask = 1 << dwordOffset;
      dst.vertexConstants.bConsts[dwordIndex]
        = (src.vertexConstants.bConsts[dwordIndex] & bitMask) ? dst.vertexConstants.bConsts[dwordIndex] | bitMask : dst.vertexConstants.bConsts[dwordIndex] & ~bitMask;
    }
  }
  for (int i = 0; i < flags.pixelConstants.fConsts.size(); i++) {
    if (flags.pixelConstants.fConsts[i]) {
      dst.pixelConstants.fConsts[i] = src.pixelConstants.fConsts[i];
    }
  }
  for (int i = 0; i < flags.pixelConstants.iConsts.size(); i++) {
    if (flags.pixelConstants.iConsts[i]) {
      dst.pixelConstants.iConsts[i] = src.pixelConstants.iConsts[i];
    }
  }
  for (int i = 0; i < flags.pixelConstants.bConsts.size(); i++) {
    if (flags.pixelConstants.bConsts[i]) {
      size_t dwordIndex = i / 32;
      size_t dwordOffset = i % 32;
      uint32_t bitMask = 1 << dwordOffset;
      dst.pixelConstants.bConsts[dwordIndex]
        = (src.pixelConstants.bConsts[dwordIndex] & bitMask) ? dst.pixelConstants.bConsts[dwordIndex] | bitMask : dst.pixelConstants.bConsts[dwordIndex] & ~bitMask;
    }
  }
}

void Direct3DStateBlock9_LSS::LocalCapture() {
  StateTransfer(m_dirtyFlags, m_pDevice->m_state, m_captureState);
}

HRESULT Direct3DStateBlock9_LSS::Capture() {
  LogFunctionCall();
  if (m_pDevice->m_stateRecording) {
    return D3DERR_INVALIDCALL;
  }
  LocalCapture();
  {
    ClientMessage { Commands::IDirect3DStateBlock9_Capture, getId() };
  }
  return S_OK;
}

HRESULT Direct3DStateBlock9_LSS::Apply() {
  LogFunctionCall();
  StateTransfer(m_dirtyFlags, m_captureState, m_pDevice->m_state);
  {
    ClientMessage { Commands::IDirect3DStateBlock9_Apply, getId() };
  }
  return S_OK;
}
