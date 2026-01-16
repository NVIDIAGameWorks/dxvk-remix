/*
* Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx_accumulation.h"

#include "rtx_context.h"
#include "rtx_imgui.h"
#include "rtx_scene_manager.h"

namespace dxvk {

  RemixGui::ComboWithKey<AccumulationBlendMode> accumulationBlendModeCombo = RemixGui::ComboWithKey<AccumulationBlendMode>(
    "Accumulation Blend Mode",
    RemixGui::ComboWithKey<AccumulationBlendMode>::ComboEntries { {
        {AccumulationBlendMode::Average, "Average"},
        {AccumulationBlendMode::Min, "Min"},
        {AccumulationBlendMode::Max, "Max"},
    } });

  bool RtxAccumulation::isActive() const {
    return m_enableAccumulation;
  }

  void RtxAccumulation::onFrameBegin(
    RtxContext& ctx,
    bool enableAccumulation,
    uint32_t numFramesToAccumulate,
    bool resetOnCameraTransformChange) {

    m_numFramesToAccumulate = std::max(numFramesToAccumulate, 1u);

    // Reset the count if accumulation is being enabled this frame
    if (enableAccumulation && !m_enableAccumulation) {
      resetNumAccumulatedFrames();
    }

    m_enableAccumulation = enableAccumulation;

    if (!isActive()) {
      return;
    }

    // Check if accumulation needs to be reset
    if (resetOnCameraTransformChange && m_numAccumulatedFrames > 0) {
      const RtCamera& camera = ctx.getSceneManager().getCamera();
      const Matrix4d prevWorldToProjection = camera.getPreviousViewToProjection() * camera.getPreviousWorldToView();
      const Matrix4d worldToProjection = camera.getViewToProjection() * camera.getWorldToView();
      const bool hasCameraChanged = memcmp(&prevWorldToProjection, &worldToProjection, sizeof(Matrix4d)) != 0;

      if (hasCameraChanged) {
        resetNumAccumulatedFrames();
      }
    }

    // Reset the count if the cap was lowered below the current count in the midst
    if (m_numFramesToAccumulate < m_numAccumulatedFrames) {
      resetNumAccumulatedFrames();
    }
  }

  // This is to be called at the end of a frame / after all caller's initAccumulationArgs() call(S) to increment the number of accumulated frames
  void RtxAccumulation::onFrameEnd() {
    m_numAccumulatedFrames = std::min(m_numAccumulatedFrames + 1, m_numFramesToAccumulate);
  }

  void RtxAccumulation::initAccumulationArgs(
    AccumulationBlendMode accumulationBlendMode,
    AccumulationArgs& args) {

    args.enableAccumulation = m_enableAccumulation;

    // If accumulation is disabled, return early
    if (!isActive()) {
      return;
    }

    // Determine accumulation mode
    if (m_numAccumulatedFrames == 0) {
      args.accumulationMode = AccumulationMode::WriteNewOutput;
    } else if (m_numAccumulatedFrames < m_numFramesToAccumulate || m_enableContinuousAccumulation) {
      args.accumulationMode = AccumulationMode::BlendNewAndPreviousOutputs;
    } else { // m_numAccumulatedFrames >= m_numFramesToAccumulate
      args.accumulationMode = AccumulationMode::CarryOverPreviousOutput;
    }

    args.accumulationBlendMode = accumulationBlendMode;

    args.accumulationWeight = 1.f / (m_numAccumulatedFrames + 1);
    args.enableFp16Accumulation = m_enableFp16Accumulation;
  }

  void RtxAccumulation::showImguiSettings(
    // Passing a reference to rtx options owned by the caller, since multiple accumulation instances can exist.
    // Number of frames to accumulate and blend mode make most sense to expose as an RTX option so it can be customizable via config/env_var.
    // Other accumulation option defaults should work for most cases.
    RtxOption<uint32_t>& numFramesToAccumulate,
    RtxOption<AccumulationBlendMode>& accumulationBlendMode,
    RtxOption<bool>& resetOnCameraTransformChange) {

    // Note: Additional ID appended to this header to not conflict with the button itself.
    if (RemixGui::CollapsingHeader("Accumulation##Header")) {
      ImGui::Indent();

      if (ImGui::Button("Reset History")) {
        resetNumAccumulatedFrames();
      }

      RemixGui::InputInt("Number of Frames To Accumulate", &numFramesToAccumulate);

      // Reset accumulation if the cap gets lowered and below the current count
      if (m_prevNumFramesToAccumulate > numFramesToAccumulate() &&
          m_numAccumulatedFrames >= numFramesToAccumulate()) {
        resetNumAccumulatedFrames();
      }
      m_prevNumFramesToAccumulate = numFramesToAccumulate();

      // ImGUI runs async with frame execution, so always report at least 1 frame was generated to avoid showing 0
      // since renderer will always show a generated image
      const uint32_t numFramesAccumulated = std::max(1u, m_numAccumulatedFrames);

      const float accumulatedPercentage = numFramesAccumulated / (0.01f * numFramesToAccumulate());
      ImGui::Text("   Accumulated: %u (%.2f%%)", numFramesAccumulated, accumulatedPercentage);

      accumulationBlendModeCombo.getKey(&accumulationBlendMode);

      RemixGui::Checkbox("Reset on Camera Transform Change", &resetOnCameraTransformChange);

      IMGUI_ADD_TOOLTIP(
        RemixGui::Checkbox("Continuous Accumulation", &m_enableContinuousAccumulation),
        "Enables continuous accumulation even after numFramesToAccumulate frame count is reached.\n"
        "Frame to frame accumulation weight remains limitted by numFramesToAccumulate count.\n"
        "This, however, skews the result as values contribute to the end result longer than numFramesToAccumulate allows.\n");

      IMGUI_ADD_TOOLTIP(
        RemixGui::Checkbox("Fp16 Accumulation", &m_enableFp16Accumulation),
        "Accumulate using fp16 precision. Default is fp32.\n"
        "Much of the renderer is limitted to fp16 formats so on one hand fp16 better emulates renderer's formats.\n"
        "On the other hand, renderer also clamps and filters the signal in many places and thus is less prone\n"
        "to very high values causing precision issues preventing very low values have any impact.\n"
        "Therefore, to minimize precision issues the default accumulation mode is set to fp32.");

      ImGui::Unindent();
    }
  }

  void RtxAccumulation::resetNumAccumulatedFrames() {
    m_numAccumulatedFrames = 0;
  }
} // namespace dxvk
