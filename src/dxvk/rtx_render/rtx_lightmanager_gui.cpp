/*
* Copyright (c) 2021-2023, NVIDIA CORPORATION. All rights reserved.
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
#include <vector>
#include <cmath>
#include <cassert>

#include "rtx_lightmanager.h"
#include "rtx_context.h"
#include "rtx_options.h"
#include "rtx_utils.h"

#include "../d3d9/d3d9_state.h"
#include "rtx/pass/common_binding_indices.h"
#include "rtx/pass/raytrace_args.h"
#include "math.h"
#include "rtx_lights.h"
#include "rtx_imgui.h"


namespace dxvk {

  ImGui::ComboWithKey<LightManager::FallbackLightMode> fallbackLightModeCombo {
    "Fallback Light Mode",
    ImGui::ComboWithKey<LightManager::FallbackLightMode>::ComboEntries { {
        {LightManager::FallbackLightMode::Never, "Never"},
        {LightManager::FallbackLightMode::NoLightsPresent, "No Lights Present"},
        {LightManager::FallbackLightMode::Always, "Always"},
    } }
  };

  ImGui::ComboWithKey<LightManager::FallbackLightType> fallbackLightTypeCombo {
    "Fallback Light Type",
    ImGui::ComboWithKey<LightManager::FallbackLightType>::ComboEntries { {
        {LightManager::FallbackLightType::Distant, "Distant"},
        {LightManager::FallbackLightType::Sphere, "Sphere"},
    } }
  };

  void LightManager::showImguiLightOverview() {
    if (ImGui::CollapsingHeader("Light Statistics", ImGuiTreeNodeFlags_CollapsingHeader)) {
      ImGui::Indent();
      ImGui::Text("Sphere Lights: %d", getLightCount(lightTypeSphere));
      ImGui::Text("Rectangle Lights: %d", getLightCount(lightTypeRect));
      ImGui::Text("Disk Lights: %d", getLightCount(lightTypeDisk));
      ImGui::Text("Cylinder Lights: %d", getLightCount(lightTypeCylinder));
      ImGui::Text("Distant Lights: %d", getLightCount(lightTypeDistant));
      ImGui::Text("Total Lights: %d", getActiveCount());
      ImGui::Unindent();
    }
  }

  void LightManager::showImguiSettings() {
    if (ImGui::CollapsingHeader("Light Translation", ImGuiTreeNodeFlags_CollapsingHeader)) {
      ImGui::Indent();

      bool lightSettingsDirty = false;

      ImGui::Text("Ignore Game Lights:");
      ImGui::Indent();
      lightSettingsDirty |= ImGui::Checkbox("Directional", &ignoreGameDirectionalLightsObject());
      ImGui::SameLine();
      lightSettingsDirty |= ImGui::Checkbox("Point", &ignoreGamePointLightsObject());
      ImGui::SameLine();
      lightSettingsDirty |= ImGui::Checkbox("Spot", &ignoreGameSpotLightsObject());
      ImGui::Unindent();

      lightSettingsDirty |= fallbackLightModeCombo.getKey(&fallbackLightModeObject());

      if (fallbackLightMode() != FallbackLightMode::Never) {
        lightSettingsDirty |= fallbackLightTypeCombo.getKey(&fallbackLightTypeObject());

        lightSettingsDirty |= ImGui::DragFloat3("Fallback Light Radiance", &fallbackLightRadianceObject(), 0.1f, 0.0f, FLT_MAX, "%.3f", ImGuiSliderFlags_AlwaysClamp);

        if (fallbackLightType() == FallbackLightType::Distant) {
          lightSettingsDirty |= ImGui::DragFloat3("Fallback Light Direction", &fallbackLightDirectionObject(), 0.1f, 0.0f, 0.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
          lightSettingsDirty |= ImGui::DragFloat("Fallback Light Angle", &fallbackLightAngleObject(), 0.01f, 0.0f, FLT_MAX, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        } else if (fallbackLightType() == FallbackLightType::Sphere) {
          lightSettingsDirty |= ImGui::DragFloat("Fallback Light Radius", &fallbackLightRadiusObject(), 0.01f, 0.0f, FLT_MAX, "%.3f", ImGuiSliderFlags_AlwaysClamp);
          lightSettingsDirty |= ImGui::DragFloat3("Fallback Light Position Offset", &fallbackLightPositionOffsetObject(), 0.1f, 0.0f, 0.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
          ImGui::SetTooltipToLastWidgetOnHover(fallbackLightPositionOffsetDescription());

          lightSettingsDirty |= ImGui::Checkbox("Enable Fallback Light Shaping", &enableFallbackLightShapingObject());

          if (enableFallbackLightShaping()) {
            ImGui::Indent();

            lightSettingsDirty |= ImGui::Checkbox("Fallback Light Match View Axis", &enableFallbackLightViewPrimaryAxisObject());
            ImGui::SetTooltipToLastWidgetOnHover(enableFallbackLightViewPrimaryAxisDescription());

            if (!enableFallbackLightViewPrimaryAxis()) {
              lightSettingsDirty |= ImGui::DragFloat3("Fallback Light Primary Axis", &fallbackLightPrimaryAxisObject(), 0.1f, 0.0f, 0.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            }

            lightSettingsDirty |= ImGui::DragFloat("Fallback Light Cone Angle", &fallbackLightConeAngleObject(), 0.01f, 0.0f, FLT_MAX, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            lightSettingsDirty |= ImGui::DragFloat("Fallback Light Cone Softness", &fallbackLightConeSoftnessObject(), 0.01f, 0.0f, FLT_MAX, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            lightSettingsDirty |= ImGui::DragFloat("Fallback Light Focus Exponent", &fallbackLightFocusExponentObject(), 0.01f, 0.0f, FLT_MAX, "%.3f", ImGuiSliderFlags_AlwaysClamp);

            ImGui::Unindent();
          }
        }
      }

      ImGui::Separator();

      lightSettingsDirty |= ImGui::Checkbox("Least Squares Intensity Calculation", &calculateLightIntensityUsingLeastSquaresObject());
      lightSettingsDirty |= ImGui::DragFloat("Sphere Light Fixed Radius", &lightConversionSphereLightFixedRadiusObject(), 0.01f, 0.0f, FLT_MAX, "%.3f", ImGuiSliderFlags_AlwaysClamp);
      lightSettingsDirty |= ImGui::DragFloat("Distant Light Fixed Intensity", &lightConversionDistantLightFixedIntensityObject(), 0.01f, 0.0f, FLT_MAX, "%.3f", ImGuiSliderFlags_AlwaysClamp);
      lightSettingsDirty |= ImGui::DragFloat("Distant Light Fixed Angle", &lightConversionDistantLightFixedAngleObject(), 0.01f, 0.0f, kPi, "%.4f", ImGuiSliderFlags_AlwaysClamp);
      lightSettingsDirty |= ImGui::DragFloat("Equality Distance Threshold", &lightConversionEqualityDistanceThresholdObject(), 0.01f, 0.0f, FLT_MAX, "%.3f", ImGuiSliderFlags_AlwaysClamp);
      lightSettingsDirty |= ImGui::DragFloat("Equality Direction Threshold", &lightConversionEqualityDirectionThresholdObject(), 0.01f, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);

      ImGui::Unindent();

      // Clear the lights and fallback light if the settings are dirty to recreate the lights on the next frame.
      if (lightSettingsDirty) {
        clear();

        // Note: Fallback light reset here so that changes to its settings will take effect, does not need to be part
        // of usual light clearing logic though.
        m_fallbackLight.reset();
      }
    }
  }

}  // namespace dxvk
