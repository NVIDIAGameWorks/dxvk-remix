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
    RTX_OPTION_FLAG("rtx.lights", bool, enableDebugMode, false, RtxOptionFlags::NoSave, "Enables light debug visualization.");
    RTX_OPTION_FLAG("rtx.lights", bool, debugDrawLightHashes, false, RtxOptionFlags::NoSave, "Draw light hashes of all visible ob screen lights, when enableDebugMode=true.");
  };

  RemixGui::ComboWithKey<LightManager::FallbackLightMode> fallbackLightModeCombo {
    "Fallback Light Mode",
    RemixGui::ComboWithKey<LightManager::FallbackLightMode>::ComboEntries { {
        {LightManager::FallbackLightMode::Never, "Never"},
        {LightManager::FallbackLightMode::NoLightsPresent, "No Lights Present"},
        {LightManager::FallbackLightMode::Always, "Always"},
    } }
  };

  RemixGui::ComboWithKey<LightManager::FallbackLightType> fallbackLightTypeCombo {
    "Fallback Light Type",
    RemixGui::ComboWithKey<LightManager::FallbackLightType>::ComboEntries { {
        {LightManager::FallbackLightType::Distant, "Distant"},
        {LightManager::FallbackLightType::Sphere, "Sphere"},
    } }
  };

  void LightManager::showImguiLightOverview() {
    if (RemixGui::CollapsingHeader("Light Statistics")) {
      ImGui::Indent();
      ImGui::Text("Sphere Lights: %d", getLightCount(lightTypeSphere));
      ImGui::Text("Rectangle Lights: %d", getLightCount(lightTypeRect));
      ImGui::Text("Disk Lights: %d", getLightCount(lightTypeDisk));
      ImGui::Text("Cylinder Lights: %d", getLightCount(lightTypeCylinder));
      ImGui::Text("Distant Lights: %d", getLightCount(lightTypeDistant));
      ImGui::Text("Total Lights: %d", getActiveCount());
      RemixGui::Separator();
      RemixGui::Checkbox("Enable Debug Visualization", &LightManagerGuiSettings::enableDebugModeObject());
      {
        ImGui::BeginDisabled(!LightManagerGuiSettings::enableDebugMode());
        RemixGui::Checkbox("Draw Light Hashes", &LightManagerGuiSettings::debugDrawLightHashesObject());
        ImGui::EndDisabled();
      }
      ImGui::Dummy({ 0,2 });
      ImGui::Unindent();
    }
  }


  void LightManager::showImguiSettings() {
    bool lightSettingsDirty = false;

    const auto separator = []() {
      ImGui::Dummy({ 0,2 });
      RemixGui::Separator();
      ImGui::Dummy({ 0,2 });
    };

    if (RemixGui::CollapsingHeader("Light Translation")) {
      ImGui::Dummy({ 0,2 });
      ImGui::Indent();

      lightSettingsDirty |= RemixGui::Checkbox("Suppress Light Keeping", &suppressLightKeepingObject());

      separator();

      const bool disableDirectional = ignoreGameDirectionalLights();
      const bool disablePointSpot = ignoreGamePointLights() && ignoreGameSpotLights();

      // TODO(REMIX-3124) remove this warning
      ImGui::TextColored(ImVec4{ 0.87f, 0.75f, 0.20f, 1.0f }, "Warning: changing Light Conversion values can cause crashes.\nManually entering values is safer than dragging.");
      ImGui::BeginDisabled(disablePointSpot);
      ImGui::Text("Sphere / Spot Light settings");
      lightSettingsDirty |= RemixGui::Checkbox("Use Least Squares Intensity", &calculateLightIntensityUsingLeastSquaresObject());
      lightSettingsDirty |= RemixGui::DragFloat("Light Radius", &lightConversionSphereLightFixedRadiusObject(), 0.01f, 0.0f, FLT_MAX, "%.3f", ImGuiSliderFlags_AlwaysClamp);
      lightSettingsDirty |= RemixGui::DragFloat("Intensity Factor", &lightConversionIntensityFactorObject(), 0.01f, 0.0f, 2.f, "%.3f");
      lightSettingsDirty |= RemixGui::OptionalDragFloat("Max Intensity", &lightConversionMaxIntensityObject(), 1000000.f, 1.f, 0.0f, FLT_MAX, "%.1f", ImGuiSliderFlags_AlwaysClamp);
      ImGui::EndDisabled();

      separator();

      ImGui::BeginDisabled(disableDirectional);
      ImGui::Text("Distant Light settings");
      lightSettingsDirty |= RemixGui::DragFloat("Fixed Intensity", &lightConversionDistantLightFixedIntensityObject(), 0.01f, 0.0f, FLT_MAX, "%.3f", ImGuiSliderFlags_AlwaysClamp);
      lightSettingsDirty |= RemixGui::DragFloat("Fixed Angle", &lightConversionDistantLightFixedAngleObject(), 0.01f, 0.0f, kPi, "%.4f rad", ImGuiSliderFlags_AlwaysClamp);
      ImGui::EndDisabled();

      separator();

      ImGui::Text("Ignore Game Lights:");
      ImGui::Indent();
      lightSettingsDirty |= RemixGui::Checkbox("Directional", &ignoreGameDirectionalLightsObject());
      lightSettingsDirty |= RemixGui::Checkbox("Point", &ignoreGamePointLightsObject());
      lightSettingsDirty |= RemixGui::Checkbox("Spot", &ignoreGameSpotLightsObject());
      ImGui::Unindent();

      ImGui::Unindent();
    }

    if (RemixGui::CollapsingHeader("Fallback Light")) {
      ImGui::Dummy({ 0,2 });
      ImGui::Indent();

      lightSettingsDirty |= fallbackLightModeCombo.getKey(&fallbackLightModeObject());

      ImGui::BeginDisabled(fallbackLightMode() == FallbackLightMode::Never);
      {
        lightSettingsDirty |= fallbackLightTypeCombo.getKey(&fallbackLightTypeObject());

        lightSettingsDirty |= RemixGui::DragFloat3("Fallback Light Radiance", &fallbackLightRadianceObject(), 0.1f, 0.0f, FLT_MAX, "%.3f", ImGuiSliderFlags_AlwaysClamp);

        if (fallbackLightType() == FallbackLightType::Distant) {
          lightSettingsDirty |= RemixGui::DragFloat3("Fallback Light Direction", &fallbackLightDirectionObject(), 0.1f, 0.0f, 0.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
          lightSettingsDirty |= RemixGui::DragFloat("Fallback Light Angle", &fallbackLightAngleObject(), 0.01f, 0.0f, FLT_MAX, "%.3f deg", ImGuiSliderFlags_AlwaysClamp);
        } else if (fallbackLightType() == FallbackLightType::Sphere) {
          lightSettingsDirty |= RemixGui::DragFloat("Fallback Light Radius", &fallbackLightRadiusObject(), 0.01f, 0.0f, FLT_MAX, "%.3f", ImGuiSliderFlags_AlwaysClamp);
          lightSettingsDirty |= RemixGui::DragFloat3("Fallback Light Position Offset", &fallbackLightPositionOffsetObject(), 0.1f, 0.0f, 0.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);

          lightSettingsDirty |= RemixGui::Checkbox("Enable Fallback Light Shaping", &enableFallbackLightShapingObject());

          if (enableFallbackLightShaping()) {
            ImGui::Indent();

            lightSettingsDirty |= RemixGui::Checkbox("Fallback Light Match View Axis", &enableFallbackLightViewPrimaryAxisObject());

            if (!enableFallbackLightViewPrimaryAxis()) {
              lightSettingsDirty |= RemixGui::DragFloat3("Fallback Light Primary Axis", &fallbackLightPrimaryAxisObject(), 0.1f, 0.0f, 0.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            }

            lightSettingsDirty |= RemixGui::DragFloat("Fallback Light Cone Angle", &fallbackLightConeAngleObject(), 0.01f, 0.0f, FLT_MAX, "%.3f deg", ImGuiSliderFlags_AlwaysClamp);
            lightSettingsDirty |= RemixGui::DragFloat("Fallback Light Cone Softness", &fallbackLightConeSoftnessObject(), 0.01f, 0.0f, FLT_MAX, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            lightSettingsDirty |= RemixGui::DragFloat("Fallback Light Focus Exponent", &fallbackLightFocusExponentObject(), 0.01f, 0.0f, FLT_MAX, "%.3f", ImGuiSliderFlags_AlwaysClamp);

            ImGui::Unindent();
          }
        }
      }
      ImGui::EndDisabled();

      ImGui::Unindent();
    }

    // Clear the lights and fallback light if the settings are dirty to recreate the lights on the next frame.
    if (lightSettingsDirty) {
      clearFromUIThread();
    }
  }

  bool transformToScreen(const Matrix4& worldToProj, const Vector2 screen, const Vector3 worldPos, ImVec2& outScreenPos) {
    Vector4 positionPS = worldToProj * Vector4(worldPos.x, worldPos.y, worldPos.z, 1.f);
    positionPS.xyz() /= std::abs(positionPS.w);

    // Projection -> screen transform
    outScreenPos = { (positionPS.x * 0.5f + 0.5f) * screen.x, (-positionPS.y * 0.5f + 0.5f) * screen.y };

    // Coarse culling
    return !(positionPS.x < -1.f || positionPS.y < -1.f || positionPS.x > 1.f || positionPS.y > 1.f || positionPS.w < 0.f);
  }

  void drawLightHash(XXH64_hash_t h,
                     const Vector3& position,
                     const Matrix4& worldToProj,
                     ImDrawList* drawList,
                     bool isFromInstance = false) {
    constexpr auto safe = 2.0f;

    const ImDrawListSharedData* data = ImGui::GetDrawListSharedData();
    assert(data && data->Font && data->FontSize > 0);

    ImVec2 screenPos;
    {
      const ImGuiViewport* viewport = ImGui::GetMainViewport();
      transformToScreen(worldToProj, Vector2{ viewport->Size.x, viewport->Size.y }, position, screenPos);
    }
    std::string str = hashToString(h);
    ImU32 backColor = IM_COL32(0, 0, 0, 200);

    screenPos.y += 24; // offset to not obstruct the original point
    if (isFromInstance) {
      screenPos.y += data->FontSize + safe * 2;
      str = "Instance: " + str;
      backColor = IM_COL32(0, 0, 70, 200);
    }

    const ImVec2 extent = data->Font->CalcTextSizeA(data->FontSize, FLT_MAX, 0, str.c_str());

    const auto offsetText = ImVec2 { screenPos.x - (extent.x / 2), screenPos.y - (extent.y / 2) };
    const auto offsetMin = ImVec2 { screenPos.x - (extent.x / 2 + safe), screenPos.y - (extent.y / 2 + safe) };
    const auto offsetMax = ImVec2 { screenPos.x + (extent.x / 2 + safe), screenPos.y + (extent.y / 2 + safe) };

    drawList->AddRectFilled(offsetMin, offsetMax, backColor);
    drawList->AddText(data->Font, data->FontSize, offsetText, IM_COL32_WHITE, str.c_str());
  }

  void drawLightHashes(const RtLight& light, const Matrix4& worldToProj, ImDrawList* drawList) {
    if (!LightManagerGuiSettings::debugDrawLightHashes()) {
      return;
    }
    drawLightHash(light.getInitialHash(), light.getPosition(), worldToProj, drawList);
  }

  void drawToolTip(const RtLight& light) {
    const ImVec2 windowSize = ImVec2(100, 200); 
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_Once);
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetMousePos().x + windowSize.x / 2, ImGui::GetMousePos().y + windowSize.y / 2), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.f, 0.f, 0.f, 0.6f));
    if (ImGui::Begin("Light Info", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
      constexpr static std::pair<RtLightType, const char*> lightTypes[] = {
            { RtLightType::Sphere,   "Sphere" },
            { RtLightType::Rect,     "Rect" },
            { RtLightType::Disk,     "Disk" },
            { RtLightType::Cylinder, "Cylinder" },
            { RtLightType::Distant,  "Distant" },
          };
      ImGui::Text("Position: %.2f %.2f %.2f", light.getPosition().x, light.getPosition().y, light.getPosition().z);
      ImGui::Text("Direction: %.2f %.2f %.2f", light.getDirection().x, light.getDirection().y, light.getDirection().z);
      ImGui::Text("Radiance: %.2f %.2f %.2f", light.getRadiance().x, light.getRadiance().y, light.getRadiance().z);
      
      ImGui::Text("Type: %s", lightTypes[(uint32_t) light.getType()].second);
      const RtLightShaping* pShaping = nullptr;
      switch (light.getType()) {
      case RtLightType::Sphere:
        pShaping = &light.getSphereLight().getShaping();
        ImGui::Text("\tRadius: %.2f", light.getSphereLight().getRadius());
        break;
      case RtLightType::Rect:
        pShaping = &light.getRectLight().getShaping();
        ImGui::Text("\tDimensions: %.2f %.2f", light.getRectLight().getDimensions().x, light.getRectLight().getDimensions().y);
        break;
      case RtLightType::Disk:
        pShaping = &light.getDiskLight().getShaping();
        ImGui::Text("\tHalf-Dimensions: %.2f %.2f", light.getDiskLight().getHalfDimensions().x, light.getDiskLight().getHalfDimensions().y);
        break;
      case RtLightType::Cylinder:
        ImGui::Text("\tRadius: %.2f", light.getCylinderLight().getRadius());
        ImGui::Text("\tLength: %.2f", light.getCylinderLight().getAxisLength());
        ImGui::Text("\tAxis: %.2f %.2f %.2f", light.getCylinderLight().getAxis().x, light.getCylinderLight().getAxis().y, light.getCylinderLight().getAxis().z);
        break;
      case RtLightType::Distant:
        break;
      }

      if (pShaping) {
        if (pShaping->getEnabled()) {
          ImGui::Text("Light Shaping: Enabled");
          ImGui::Text("\tDirection: %.2f %.2f %.2f", pShaping->getDirection().x, pShaping->getDirection().y, pShaping->getDirection().z);
          ImGui::Text("\tCone Angle: %.2f deg", std::acos(pShaping->getCosConeAngle()) * kRadiansToDegrees);
          ImGui::Text("\tCone Softness: %.2f", pShaping->getConeSoftness());
          ImGui::Text("\tFocus Exponent: %.2f", pShaping->getFocusExponent());
        } else {
          ImGui::Text("Light Shaping: Disabled");
        }
      } else {
        ImGui::Text("Light Shaping: Not Supported");
      }

      ImGui::Text("Volumetric Radiance Scale: %.2f", light.getVolumetricRadianceScale());
      ImGui::Text("Initial Hash: 0x%" PRIx64, light.getInitialHash());
      ImGui::Text("Transformed Hash: 0x%" PRIx64, light.getTransformedHash());
      if (light.getPrimInstanceOwner().getReplacementInstance() != nullptr) {
        ImGui::Text("Replacement Index: %d", light.getPrimInstanceOwner().getReplacementIndex());
        ImGui::Text("Is Root: %s", light.getPrimInstanceOwner().isRoot(&light) ? "Yes" : "No");
        switch (light.getPrimInstanceOwner().getReplacementInstance()->root.getType()) {
          case PrimInstance::Type::Instance:
            ImGui::Text("Replacement Root is a Mesh");
            break;
          case PrimInstance::Type::Light:
            ImGui::Text("Replacement Root is a Light");
            break;
          case PrimInstance::Type::Graph:
            ImGui::Text("Replacement Root is a Graph");
            break;
          case PrimInstance::Type::None:
            ImGui::Text("Replacement Root is Unknown");
            break;
        }
      }
      ImGui::Text("Frame last touched: %d", light.getFrameLastTouched());
      RemixGui::Separator();

      if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
        ImGui::SetClipboardText(hashToString(light.getInitialHash()).c_str());
      }

    }
    // Note: End must always be called even if Begin returns false (unlike other ImGui patterns).
    ImGui::End();
    ImGui::PopStyleColor();
  }

  struct DrawResult {
    bool mouseHover { false };
    bool isVisible { false };

    DrawResult& operator |=(const DrawResult& other) {
      this->mouseHover |= other.mouseHover;
      this->isVisible |= other.isVisible;
      return *this;
    }
  };

  DrawResult drawSphereLightDebug(const RtSphereLight& sphereLight, const Matrix4& worldToProj, const Vector3& cameraRight, const ImU32 colHex, cFrustum& frustum, ImDrawList* drawList) {
    if (!sphereIntersectsFrustum(frustum, sphereLight.getPosition(), sphereLight.getRadius())) {
      return {};
    }
    ImVec2 screenPos[2];
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    transformToScreen(worldToProj, Vector2{ viewport->Size.x, viewport->Size.y }, sphereLight.getPosition(), screenPos[0]);
    transformToScreen(worldToProj, Vector2{ viewport->Size.x, viewport->Size.y }, sphereLight.getPosition() + cameraRight * sphereLight.getRadius(), screenPos[1]);
    const float radius = std::max(1.f, sqrtf(ImLengthSqr(ImVec2(screenPos[0].x - screenPos[1].x, screenPos[0].y - screenPos[1].y))));
    drawList->AddCircleFilled(screenPos[0], radius, colHex);

    return DrawResult {
      ImLengthSqr(ImVec2(screenPos[0].x - ImGui::GetMousePos().x, screenPos[0].y - ImGui::GetMousePos().y)) <= radius * radius,
      true,
    };
  }

  DrawResult drawRectLightDebug(const Vector3& position, const Vector3& xAxis, const Vector3& yAxis, const Vector2& dimensions, const Matrix4& worldToProj, const ImU32 colHex, cFrustum& frustum, ImDrawList* drawList) {
    if (!rectIntersectsFrustum(frustum, position, dimensions, xAxis, yAxis)) {
      return {};
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    Vector3 rectBounds[4];
    rectBounds[0] = position - dimensions.x * 0.5f * xAxis - dimensions.y * yAxis * 0.5f;
    rectBounds[1] = position - dimensions.x * 0.5f * xAxis + dimensions.y * yAxis * 0.5f;
    rectBounds[2] = position + dimensions.x * 0.5f * xAxis + dimensions.y * yAxis * 0.5f;
    rectBounds[3] = position + dimensions.x * 0.5f * xAxis - dimensions.y * yAxis * 0.5f;
    ImVec2 screenPos[4];
    for (uint32_t i = 0; i < 4; i++) {
      transformToScreen(worldToProj, Vector2{ viewport->Size.x, viewport->Size.y }, rectBounds[i], screenPos[i]);
    }
    drawList->AddQuadFilled(screenPos[0], screenPos[1], screenPos[2], screenPos[3], colHex);
    return DrawResult {
      ImTriangleContainsPoint(screenPos[0], screenPos[1], screenPos[2], ImGui::GetMousePos()) || ImTriangleContainsPoint(screenPos[1], screenPos[2], screenPos[3], ImGui::GetMousePos()),
      true,
    };
  }

  DrawResult drawDiskLightDebug(const Vector3& position, const Vector3& xAxis, const Vector3& yAxis, const Vector2& radius, const Matrix4& worldToProj, const ImU32 colHex, cFrustum& frustum, ImDrawList* drawList) {
    if (!rectIntersectsFrustum(frustum, position, radius * 2, xAxis, yAxis)) {
      return {};
    }
    const uint32_t numPoints = 16;
    ImVec2 screenPos[numPoints];
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    for (uint32_t i = 0; i < numPoints; i++) {
      float theta = (float) i * kPi * 2 / numPoints;
      Vector3 worldPos = position + radius.x * cos(theta) * xAxis + radius.y * yAxis * sin(theta);
      transformToScreen(worldToProj, Vector2{ viewport->Size.x, viewport->Size.y }, worldPos, screenPos[i]);
    }
    drawList->AddConvexPolyFilled(&screenPos[0], numPoints, colHex);

    for (uint32_t i = 1; i < numPoints-1; i++) {
      if (ImTriangleContainsPoint(screenPos[0], screenPos[i], screenPos[i+1], ImGui::GetMousePos())) {
        return DrawResult { true, true };
      }
    }
    return DrawResult { false, true };
  }

  DrawResult drawCylinderLightDebug(const RtCylinderLight& cylinderLight, const Matrix4& worldToProj, const ImU32 colHex, cFrustum& frustum, ImDrawList* drawList) {
    const Vector3 pos = cylinderLight.getPosition();
    const Vector3 axis = cylinderLight.getAxis();
    const float radius = cylinderLight.getRadius();
    const float sign = axis.z < 0 ? -1.f : 1.f;

    const float a = 1.0f / (sign + axis.z);
    const float b = axis.x * axis.y * a;

    const Vector3 tangent = Vector3(1.0f + sign * axis.x * axis.x * a, sign * b, -sign * axis.x);
    const Vector3 bitangent = Vector3(b, sign + axis.y * axis.y * a, -axis.y);

    auto result = DrawResult{};
    result |= drawDiskLightDebug(pos + axis * cylinderLight.getAxisLength() * 0.5, tangent, bitangent, Vector2(radius, radius), worldToProj, colHex, frustum, drawList);
    result |= drawDiskLightDebug(pos - axis * cylinderLight.getAxisLength() * 0.5, tangent, bitangent, Vector2(radius, radius), worldToProj, colHex, frustum, drawList);

    const uint32_t numPoints = 16;
    for (uint32_t i = 0; i < numPoints; i++) {
      float theta = (float) i * kPi * 2 / numPoints;
      float theta1 = (float) (i + 1) * kPi * 2 / numPoints;
      const Vector3 position = pos + radius * cos(theta) * tangent + radius * bitangent * sin(theta);
      const Vector3 position1 = pos + radius * cos(theta1) * tangent + radius * bitangent * sin(theta1);
      result |= drawRectLightDebug((position + position1) * 0.5f, normalize(position1 - position), axis, Vector2(length(position1 - position), cylinderLight.getAxisLength()), worldToProj, colHex, frustum, drawList);
    }
    return result;
  }

  void LightManager::showImguiDebugVisualization() const {
    if (!LightManagerGuiSettings::enableDebugMode()) {
      return;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.f, 0.f, 0.f, 0.0f));
    if (ImGui::Begin("Light Debug View", nullptr, ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings)) {
      std::lock_guard<std::mutex> lock(m_lightUIMutex);
      ImDrawList* drawList = ImGui::GetWindowDrawList();
      drawList->PushClipRectFullScreen();
      const RtCamera& camera = device()->getCommon()->getSceneManager().getCamera();
      const Matrix4 worldToProj = camera.getViewToProjection() * camera.getWorldToView(true);

      cFrustum frustum;
      frustum.Setup(NDC_D3D, *reinterpret_cast<const float4x4*>(&worldToProj));

      for (auto&& linearizedLight : m_linearizedLights) {
        const RtLight* light = linearizedLight;
        if (light->getType() == RtLightType::Distant) {
          continue;
        }

        if (light->getType() > RtLightType::Distant) {
          // This happens because the linearizedLights stored pointers to the actual lights.
          // the actual lights can be garbage collected after linearizedLights is made, but before this function runs.
          Logger::err("tried to use a deleted light in showImguiDebugVisualization.");
          continue;
        }

        Vector4 color = light->getColorAndIntensity();
        const ImU32 colHex = ImColor(color.x, color.y, color.z);

        auto result = DrawResult{};
        switch (light->getType()) {
        case RtLightType::Sphere:
        {
          result = drawSphereLightDebug(light->getSphereLight(), worldToProj, Vector3{ camera.getViewToWorld(true)[0].xyz() }, colHex, frustum, drawList);
          break;
        }
        case RtLightType::Rect:
        {
          const RtRectLight& rectLight = light->getRectLight();
          result = drawRectLightDebug(rectLight.getPosition(), rectLight.getXAxis(), rectLight.getYAxis(), rectLight.getDimensions(), worldToProj, colHex, frustum, drawList);
          break;
        }
        case RtLightType::Disk:
        {
          const RtDiskLight& diskLight = light->getDiskLight();
          result = drawDiskLightDebug(diskLight.getPosition(), diskLight.getXAxis(), diskLight.getYAxis(), diskLight.getHalfDimensions(), worldToProj, colHex, frustum, drawList);
          break;
        }
        case RtLightType::Cylinder:
        {
          result = drawCylinderLightDebug(light->getCylinderLight(), worldToProj, colHex, frustum, drawList);
          break;
        }
        default:
          break;

        }

        if (result.mouseHover) {
          drawToolTip(*light);
        }
        else if (result.isVisible) {
          drawLightHashes(*light, worldToProj, drawList);
        }
      }

      drawList->PopClipRect();
    }
    ImGui::End();
    ImGui::PopStyleColor();
  }

}  // namespace dxvk
