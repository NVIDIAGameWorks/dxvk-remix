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

#include "rtx_light_manager.h"
#include "rtx_context.h"
#include "rtx_options.h"
#include "rtx_utils.h"

#include "../d3d9/d3d9_state.h"
#include "rtx/pass/common_binding_indices.h"
#include "rtx/pass/raytrace_args.h"
#include "math.h"
#include "rtx_lights.h"
#include "rtx_imgui.h"
#include "../util/util_vector.h"
#include "rtx_intersection_test_helpers.h"


namespace dxvk {
  struct LightManagerGuiSettings {
    RW_RTX_OPTION_FLAG("rtx.lights", bool, enableDebugMode, false, RtxOptionFlags::NoSave, "Enables light debug visualization.");
  };

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
      ImGui::Separator();
      ImGui::Checkbox("Enable Debug Visualization", &LightManagerGuiSettings::enableDebugModeObject());
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

  bool transformToScreen(const Matrix4& worldToProj, const Vector2 screen, const Vector3 worldPos, ImVec2& outScreenPos) {
    Vector4 positionPS = worldToProj * Vector4(worldPos.x, worldPos.y, worldPos.z, 1.f);
    positionPS.xyz() /= abs(positionPS.w);

    // Projection -> screen transform
    outScreenPos = { (positionPS.x * 0.5f + 0.5f) * screen.x, (-positionPS.y * 0.5f + 0.5f) * screen.y };

    // Coarse culling
    return !(positionPS.x < -1.f || positionPS.y < -1.f || positionPS.x > 1.f || positionPS.y > 1.f || positionPS.w < 0.f);
  }

  void drawSphereLightDebug(const RtSphereLight& sphereLight, const Matrix4 worldToProj, const ImU32 colHex, cFrustum& frustum, ImDrawList* drawList) {
    if (!sphereIntersectsFrustum(frustum, sphereLight.getPosition(), sphereLight.getRadius())) {
      return;
    }
    ImVec2 screenPos;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    transformToScreen(worldToProj, { viewport->Size.x, viewport->Size.y }, sphereLight.getPosition(), screenPos);
    drawList->AddCircleFilled(screenPos, sphereLight.getRadius(), colHex);
  }

  void drawRectLightDebug(const Vector3& position, const Vector3& xAxis, const Vector3& yAxis, const Vector2& dimensions, const Matrix4 worldToProj, const ImU32 colHex, cFrustum& frustum, ImDrawList* drawList) {
    if (!rectIntersectsFrustum(frustum, position, dimensions, xAxis, yAxis)) {
      return;
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    Vector3 rectBounds[4];
    rectBounds[0] = position - dimensions.x * 0.5f * xAxis - dimensions.y * yAxis * 0.5f;
    rectBounds[1] = position - dimensions.x * 0.5f * xAxis + dimensions.y * yAxis * 0.5f;
    rectBounds[2] = position + dimensions.x * 0.5f * xAxis + dimensions.y * yAxis * 0.5f;
    rectBounds[3] = position + dimensions.x * 0.5f * xAxis - dimensions.y * yAxis * 0.5f;
    ImVec2 screenPos[4];
    for (uint32_t i = 0; i < 4; i++) {
      transformToScreen(worldToProj, { viewport->Size.x, viewport->Size.y }, rectBounds[i], screenPos[i]);
    }
    drawList->AddQuadFilled(screenPos[0], screenPos[1], screenPos[2], screenPos[3], colHex);
  }

  void drawDiskLightDebug(const Vector3& position, const Vector3& xAxis, const Vector3& yAxis, const Vector2& radius, const Matrix4 worldToProj, const ImU32 colHex, cFrustum& frustum, ImDrawList* drawList) {
    if (!rectIntersectsFrustum(frustum, position, radius * 2, xAxis, yAxis)) {
      return;
    }
    const uint32_t numPoints = 16;
    ImVec2 screenPos[numPoints];
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    for (uint32_t i = 0; i < numPoints; i++) {
      float theta = (float) i * kPi * 2 / numPoints;
      Vector3 worldPos = position + radius.x * cos(theta) * xAxis + radius.y * yAxis * sin(theta);
      transformToScreen(worldToProj, { viewport->Size.x, viewport->Size.y }, worldPos, screenPos[i]);
    }
    drawList->AddConvexPolyFilled(&screenPos[0], numPoints, colHex);
  }

  void drawCylinderLightDebug(const RtCylinderLight& cylinderLight, const Matrix4 worldToProj, const ImU32 colHex, cFrustum& frustum, ImDrawList* drawList) {
    const Vector3 pos = cylinderLight.getPosition();
    const Vector3 axis = cylinderLight.getAxis();
    const float radius = cylinderLight.getRadius();
    const float sign = axis.z < 0 ? -1.f : 1.f;

    const float a = 1.0f / (sign + axis.z);
    const float b = axis.x * axis.y * a;

    const Vector3 tangent = Vector3(1.0f + sign * axis.x * axis.x * a, sign * b, -sign * axis.x);
    const Vector3 bitangent = Vector3(b, sign + axis.y * axis.y * a, -axis.y);

    drawDiskLightDebug(pos + axis * cylinderLight.getAxisLength() * 0.5, tangent, bitangent, Vector2(radius, radius), worldToProj, colHex, frustum, drawList);
    drawDiskLightDebug(pos - axis * cylinderLight.getAxisLength() * 0.5, tangent, bitangent, Vector2(radius, radius), worldToProj, colHex, frustum, drawList);

    const uint32_t numPoints = 16;
    for (uint32_t i = 0; i < numPoints; i++) {
      float theta = (float) i * kPi * 2 / numPoints;
      float theta1 = (float) (i + 1) * kPi * 2 / numPoints;
      const Vector3 position = pos + radius * cos(theta) * tangent + radius * bitangent * sin(theta);
      const Vector3 position1 = pos + radius * cos(theta1) * tangent + radius * bitangent * sin(theta1);
      drawRectLightDebug((position + position1) * 0.5f, normalize(position1 - position), axis, Vector2(length(position1 - position), cylinderLight.getAxisLength()), worldToProj, colHex, frustum, drawList);
    }
  }

  void LightManager::showImguiDebugVisualization() const {
    if (!LightManagerGuiSettings::enableDebugMode())
      return;
    
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.f, 0.f, 0.f, 0.0f));
    if (ImGui::Begin("Light Debug View", nullptr, ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs)) {
      ImDrawList* drawList = ImGui::GetWindowDrawList();
      drawList->PushClipRectFullScreen();
      const RtCamera& camera = device()->getCommon()->getSceneManager().getCamera();
      const Matrix4 worldToProj = camera.getViewToProjection() * camera.getWorldToView(true);

      cFrustum frustum;
      frustum.Setup(NDC_D3D, *reinterpret_cast<const float4x4*>(&worldToProj));

      for (const std::pair<XXH64_hash_t, RtLight>& pair : m_lights) {
        const RtLight* light = &pair.second;
        if (light->getType() == RtLightType::Distant) {
          continue;
        }

        Vector4 color = light->getColorAndIntensity();
        const ImU32 colHex = ImColor(color.x, color.y, color.z);

        switch (light->getType()) {
        case RtLightType::Sphere:
        {
          drawSphereLightDebug(light->getSphereLight(), worldToProj, colHex, frustum, drawList);
          break;
        }
        case RtLightType::Rect:
        {
          const RtRectLight& rectLight = light->getRectLight();
          drawRectLightDebug(rectLight.getPosition(), rectLight.getXAxis(), rectLight.getYAxis(), rectLight.getDimensions(), worldToProj, colHex, frustum, drawList);
          break;
        }
        case RtLightType::Disk:
        {
          const RtDiskLight& diskLight = light->getDiskLight();
          drawDiskLightDebug(diskLight.getPosition(), diskLight.getXAxis(), diskLight.getYAxis(), diskLight.getHalfDimensions(), worldToProj, colHex, frustum, drawList);
          break;
        }
        case RtLightType::Cylinder:
        {
          drawCylinderLightDebug(light->getCylinderLight(), worldToProj, colHex, frustum, drawList);
          break;
        }
        default:
          break;

        }
      }

      drawList->PopClipRect();
      ImGui::End();
    }
    ImGui::PopStyleColor();
  }

}  // namespace dxvk
