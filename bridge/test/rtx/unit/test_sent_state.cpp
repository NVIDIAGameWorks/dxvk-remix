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
#include <iostream>
#include <memory>
#include <string>

#include <d3d9.h>

#include "d3d9_sent_state.h"

namespace {
  void check(const bool condition, const std::string& message) {
    if (!condition) {
      throw message;
    }
  }

  void testRenderStates() {
    auto pState = std::make_unique<SentState>();
    check(pState->renderState(D3DRS_ZENABLE, TRUE), "The first render state must be sent.");
    check(!pState->renderState(D3DRS_ZENABLE, TRUE), "An unchanged render state must be dropped.");
    check(pState->renderState(D3DRS_ZENABLE, FALSE), "A changed render state must be sent.");

    // The driver hack channels are always sent.
    check(pState->renderState(D3DRS_POINTSIZE, 1), "D3DRS_POINTSIZE must be sent.");
    check(pState->renderState(D3DRS_POINTSIZE, 1), "An unchanged D3DRS_POINTSIZE must be sent.");
    check(pState->renderState(D3DRS_ADAPTIVETESS_X, 1), "D3DRS_ADAPTIVETESS_X must be sent.");
    check(pState->renderState(D3DRS_ADAPTIVETESS_X, 1), "An unchanged D3DRS_ADAPTIVETESS_X must be sent.");

    // Out-of-range states are always sent.
    check(pState->renderState(SentState::kRenderStates, 1), "An out-of-range render state must be sent.");
    check(pState->renderState(SentState::kRenderStates, 1), "An unchanged out-of-range render state must be sent.");
  }

  void testInvalidation() {
    auto pState = std::make_unique<SentState>();
    check(pState->renderState(D3DRS_LIGHTING, FALSE), "The first render state must be sent.");
    pState->invalidateAll();
    check(pState->renderState(D3DRS_LIGHTING, FALSE), "A render state must be sent after invalidateAll().");
    check(!pState->renderState(D3DRS_LIGHTING, FALSE), "A render state sent after invalidateAll() must be dropped.");

    D3DVIEWPORT9 viewport = { 0, 0, 640, 480, 0.0f, 1.0f };
    RECT scissor = { 0, 0, 640, 480 };
    check(pState->viewport(viewport), "The first viewport must be sent.");
    check(pState->scissor(scissor), "The first scissor rect must be sent.");
    check(!pState->viewport(viewport), "An unchanged viewport must be dropped.");
    pState->invalidateViewportAndScissor();
    check(pState->viewport(viewport), "The viewport must be sent after invalidateViewportAndScissor().");
    check(pState->scissor(scissor), "The scissor rect must be sent after invalidateViewportAndScissor().");
    check(!pState->renderState(D3DRS_LIGHTING, FALSE), "invalidateViewportAndScissor() must keep the render states.");
  }

  void testVertexDeclarationAndFvf() {
    auto pState = std::make_unique<SentState>();
    check(pState->vertexDeclaration(7), "The first vertex declaration must be sent.");
    check(!pState->vertexDeclaration(7), "An unchanged vertex declaration must be dropped.");
    check(pState->fvf(D3DFVF_XYZ), "The first FVF must be sent.");
    check(pState->vertexDeclaration(7), "The vertex declaration must be sent after an FVF replaced it.");
    check(pState->fvf(D3DFVF_XYZ), "The FVF must be sent after a vertex declaration replaced it.");
  }

  void testSamplers() {
    auto pState = std::make_unique<SentState>();
    check(pState->samplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR), "The first sampler state must be sent.");
    check(!pState->samplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR), "An unchanged sampler state must be dropped.");
    check(pState->samplerState(D3DVERTEXTEXTURESAMPLER0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR),
          "A vertex texture sampler has its own entry.");
    check(!pState->samplerState(D3DVERTEXTEXTURESAMPLER0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR),
          "An unchanged vertex texture sampler state must be dropped.");
    check(pState->samplerState(100, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR), "An unknown sampler must be sent.");
    check(pState->samplerState(100, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR), "An unchanged unknown sampler must be sent.");

    check(pState->texture(0, 5), "The first texture must be sent.");
    check(!pState->texture(0, 5), "An unchanged texture must be dropped.");
    check(pState->texture(0, 0), "Unbinding a texture must be sent.");
  }

  void testShaderConstants() {
    auto pState = std::make_unique<SentState>();
    float constants[4 * 4] = {};
    for (int i = 0; i < 16; ++i) {
      constants[i] = float(i);
    }

    uint32_t start = 10;
    uint32_t count = 4;
    const float* pData = constants;
    check(pState->vsConstantsF(start, pData, count), "The first constants must be sent.");
    check(start == 10 && count == 4 && pData == constants, "The first constants must be sent as a whole.");

    start = 10;
    count = 4;
    pData = constants;
    check(!pState->vsConstantsF(start, pData, count), "Unchanged constants must be dropped.");

    // Only registers 11 and 12 change, so the range narrows to them.
    constants[4] = 100.0f;
    constants[8] = 200.0f;
    start = 10;
    count = 4;
    pData = constants;
    check(pState->vsConstantsF(start, pData, count), "Changed constants must be sent.");
    check(start == 11, "The range must start at the first changed register.");
    check(count == 2, "The range must end at the last changed register.");
    check(pData == constants + 4, "The data must start at the first changed register.");

    // Pixel shader constants are separate from vertex shader constants.
    start = 10;
    count = 4;
    pData = constants;
    check(pState->psConstantsF(start, pData, count), "Pixel shader constants have their own registers.");

    // An out-of-range call is sent unchanged.
    start = caps::MaxFloatConstantsPS - 1;
    count = 2;
    pData = constants;
    check(pState->psConstantsF(start, pData, count), "An out-of-range call must be sent.");
    check(start == caps::MaxFloatConstantsPS - 1 && count == 2, "An out-of-range call must not narrow.");

    // A constant range is forgotten by invalidateAll() as well.
    pState->invalidateAll();
    start = 10;
    count = 4;
    pData = constants;
    check(pState->vsConstantsF(start, pData, count), "Constants must be sent after invalidateAll().");
    check(start == 10 && count == 4, "Constants must be sent as a whole after invalidateAll().");

    const BOOL bools[2] = { TRUE, FALSE };
    start = 0;
    count = 2;
    const BOOL* pBools = bools;
    check(pState->vsConstantsB(start, pBools, count), "The first bool constants must be sent.");
    start = 0;
    count = 2;
    pBools = bools;
    check(!pState->vsConstantsB(start, pBools, count), "Unchanged bool constants must be dropped.");
  }

  void testStreams() {
    auto pState = std::make_unique<SentState>();
    check(pState->streamSource(0, 3, 0, 32), "The first stream source must be sent.");
    check(!pState->streamSource(0, 3, 0, 32), "An unchanged stream source must be dropped.");
    check(pState->streamSource(0, 3, 64, 32), "A changed offset must be sent.");
    check(pState->streamSource(0, 3, 64, 16), "A changed stride must be sent.");
    check(pState->streamSource(SentState::kStreams, 3, 0, 32), "An out-of-range stream must be sent.");
    check(pState->streamSourceFreq(0, 1), "The first stream frequency must be sent.");
    check(!pState->streamSourceFreq(0, 1), "An unchanged stream frequency must be dropped.");
  }
}

int main() {
  try {
    testRenderStates();
    testInvalidation();
    testVertexDeclarationAndFvf();
    testSamplers();
    testShaderConstants();
    testStreams();
  } catch (const std::string& errorMessage) {
    std::cerr << "SentState test failed: " << errorMessage << std::endl;
    return -1;
  }
  std::cout << "SentState tests passed." << std::endl;
  return 0;
}
