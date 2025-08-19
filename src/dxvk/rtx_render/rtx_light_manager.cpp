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
#include "rtx_intersection_test.h"

/*  Light Manager (blurb)
* 
* There are 3 main types of lights we are heuristically detecting:
* 
*   A) Long-lived, static lights
*     - Typically used for scene/level lighting
*     - Many used to light up a level
*     - Many are "fill" lighting, to show/guide the player
* 
*   B) Short-lived, static lights
*     - Typically, FX lights
*     - e.g. gun muzzle flash
* 
*   C) Dynamic lights
*     - Lights that are moving with some object
*     - e.g. car lights, flash light
* 
*   For all lights, hardware limitations in the fixed-function era meant only a  handful of 
*   these lights can be enabled at any given time (max was 8 lights total back in the day).
*   
*   Many games will try to optimize and prioritize for these 8 available lights (and in many
*   cases use less for performance reasons).  That means, lights can be observed turning on
*   and off as the player moves through a level, to balance the more important (which in raster
*   means, closest) lights.
* 
*   How do we classify each light?
* 
*     A) Long-lived lights are those which haven't moved for 'getNumFramesToPutLightsToSleep' frames.
* 
*     B) A short-lived light is one which has not been seen for 'numFramesToKeepLights' frames, and before it can be put to sleep.
* 
*     C) Any light which moves, is defined as a dynamic light.
*/

namespace dxvk {

  // Note: This must be done as currently every other light index is valid, so this invalid index is the only one that can be used
  // to represent something such as a new light index.
  static_assert(LIGHT_INDEX_INVALID == kNewLightIdx, "New light index must match invalid light sentinel value");

  LightManager::LightManager(DxvkDevice* device)
    : CommonDeviceObject(device) {
    // Legacy light translation Options
    fallbackLightRadiance.setDeferred(Vector3(
      std::max(fallbackLightRadiance().x, 0.0f),
      std::max(fallbackLightRadiance().y, 0.0f),
      std::max(fallbackLightRadiance().z, 0.0f)));
    RTX_OPTION_CLAMP_MIN(fallbackLightAngle, 0.0f);
    RTX_OPTION_CLAMP_MIN(fallbackLightRadius, 0.0f);
    RTX_OPTION_CLAMP_MIN(fallbackLightConeAngle, 0.0f);
    RTX_OPTION_CLAMP_MIN(fallbackLightConeSoftness, 0.0f);
    RTX_OPTION_CLAMP_MIN(fallbackLightFocusExponent, 0.0f);
    RTX_OPTION_CLAMP_MIN(lightConversionSphereLightFixedRadius, 0.0f);
    RTX_OPTION_CLAMP_MIN(lightConversionDistantLightFixedIntensity, 0.0f);
    RTX_OPTION_CLAMP(lightConversionDistantLightFixedAngle, 0.0f, kPi);
  }

  LightManager::~LightManager() {
  }

  void LightManager::clear() {
    if (!m_lightDebugUILock.owns_lock()) {
      m_lightDebugUILock.lock();
    }
    m_lights.clear();
    m_linearizedLights.clear();
    m_lightDebugUILock.unlock();
  }

  void LightManager::clearFromUIThread() {
    // This needs to wait for `m_lightDebugUILock` to be unlocked, so it doesn't
    // cause crashes.
    std::lock_guard<std::mutex> lock(m_lightUIMutex);
    m_lights.clear();
    m_linearizedLights.clear();

    // Note: Fallback light reset here so that changes to its settings will take effect, does not need to be part
    // of usual light clearing logic though.
    m_fallbackLight.reset();
  }

  void LightManager::garbageCollectionInternal() {
    const uint32_t currentFrame = m_device->getCurrentFrameId();
    const uint32_t framesToKeep = RtxOptions::numFramesToKeepLights();
    const uint32_t framesToSleep = RtxOptions::getNumFramesToPutLightsToSleep();

    const bool forceGarbageCollection = (m_lights.size() >= RtxOptions::AntiCulling::Light::numLightsToKeep());
    for (auto it = m_lights.begin(); it != m_lights.end();) {
      const RtLight& light = it->second;
      if (light.isMarkedForGarbageCollection()) {
        it = m_lights.erase(it);
        continue;
      }
      const uint32_t frameLastTouched = light.getFrameLastTouched();
      if (!RtxOptions::AntiCulling::isLightAntiCullingEnabled() || // It's always True if anti-culling is disabled
          (light.getIsInsideFrustum() ||
           frameLastTouched + RtxOptions::AntiCulling::Light::numFramesToExtendLightLifetime() <= currentFrame)) {
        if (light.isDynamic || suppressLightKeeping()) {
          if (light.getFrameLastTouched() < currentFrame) {
            it = m_lights.erase(it);
            continue;
          }
        } else if ((light.isStaticCount < framesToSleep) && (frameLastTouched + framesToKeep) <= currentFrame) {
          it = m_lights.erase(it);
          continue;
        }
      }
      ++it;
    }

    for (auto it = m_externallyTrackedLights.begin(); it != m_externallyTrackedLights.end();) {
      RtLight& light = it->second;
      if (light.isMarkedForGarbageCollection()) {
        it = m_externallyTrackedLights.erase(it);
      } else {
        ++it;
      }
    }
  }

  void LightManager::garbageCollection(RtCamera& camera) {
    if (!m_lightDebugUILock.owns_lock()) {
      m_lightDebugUILock.lock();
    }
    if (RtxOptions::AntiCulling::isLightAntiCullingEnabled()) {
      cFrustum& cameraLightAntiCullingFrustum = camera.getLightAntiCullingFrustum();
      for (auto& [lightHash, rtLight] : getLightTable()) {
        bool isLightInsideFrustum = true;

        // We have 3 situations for a light Anti-Culling:
        // 1. Game Light: We only need to check sphere light (directional light will not be culled)
        // 2. Light replaces original light: Same as 1, we only need to care about light that the ORIGINAL type is sphere
        // 3. Light replaces mesh: Do the same as Object Anti-Culling for the original mesh
        switch (rtLight.getLightAntiCullingType()) {
        case RtLightAntiCullingType::GameLight:
        case RtLightAntiCullingType::LightReplacement:
          isLightInsideFrustum = sphereIntersectsFrustum(
            cameraLightAntiCullingFrustum, rtLight.getSphereLightReplacementOriginalPosition(), rtLight.getSphereLightReplacementOriginalRadius());
          break;
        case RtLightAntiCullingType::MeshReplacement:
          // Do Object-Anti-Culling if current light replaces original mesh
          if (RtxOptions::needsMeshBoundingBox()) {
            const AxisAlignedBoundingBox& boundingBox = rtLight.getMeshReplacementBoundingBox();
            const Matrix4 objectToView = camera.getWorldToView(false) * rtLight.getMeshReplacementTransform();
            isLightInsideFrustum = boundingBoxIntersectsFrustumSAT((RtCamera&)camera, boundingBox.minPos, boundingBox.maxPos, objectToView, false);
          }
          break;
        case RtLightAntiCullingType::Ignore:
          break;
        }

        if (isLightInsideFrustum) {
          rtLight.markAsInsideFrustum();
        } else {
          rtLight.markAsOutsideFrustum();
        }
      }
    }

    garbageCollectionInternal();
    m_lightDebugUILock.unlock();
  }

  void LightManager::dynamicLightMatching() {
    ScopedCpuProfileZone();
    // Try match up any stragglers now we have the full light list this frame.
    for (auto it = m_lights.cbegin(); it != m_lights.cend(); ) {
      const RtLight& light = it->second;
      // Only looking for instances of dynamic lights that have been updated on the previous frame
      if (light.getFrameLastTouched() + 1 != m_device->getCurrentFrameId()) {
        ++it;
        continue;
      }
      // Only interested in updating lights that have been around a while, this implicitly avoids searching for new lights that have been updated.
      if (light.getBufferIdx() == kNewLightIdx) {
        ++it;
        continue;
      }

      float currentSimilarity = -1.f;
      // Note: Using an iterator for the found similar light is safe here because the m_lights map will not change between where
      // it is found and where it is accessed.
      std::optional<decltype(m_lights)::iterator> similarLight;
      for (auto similarLightIterator = m_lights.begin(); similarLightIterator != m_lights.end(); ++similarLightIterator) {
        const RtLight& newLight = similarLightIterator->second;
        // Skip comparing to old lights, this check implicitly avoids comparing the exact same light.
        if (newLight.getBufferIdx() != kNewLightIdx)
          continue;

        const float similarity = isSimilar(light, newLight, RtxOptions::uniqueObjectDistance());
        // Update the cached light if it's similar.
        if (similarity > currentSimilarity) {
          similarLight = similarLightIterator;
          currentSimilarity = similarity;
        }
      }

      if (currentSimilarity >= 0 && similarLight.has_value()) {
        // This is a dynamic light!
        RtLight& dynamicLight = (*similarLight)->second;
        dynamicLight.isDynamic = true;

        // This is the same light, so update our new light
        updateLight(light, dynamicLight);

        // Remove the previous frames version
        it = m_lights.erase(it);
      } else {
        ++it;
      }
    }
  }

  void LightManager::prepareSceneData(Rc<DxvkContext> ctx, CameraManager const& cameraManager) {
    ScopedCpuProfileZone();
    // Note: Early outing in this function (via returns) should be done carefully (or not at all ideally) as it may skip important
    // logic such as swapping the current/previous frame light buffer, updating light count information or allocating/updating the
    // light buffer which may cause issues in some cases (or rather already has, which is why this warning exists).

    // Create or remove a fallback light depending on if any lights are present in the game and the fallback light mode

    const auto mode = fallbackLightMode();

    const bool noLightsPresent = m_lights.empty() && m_externallyTrackedLights.empty() && m_externalActiveLightList.empty();

    if (
      mode == FallbackLightMode::Always ||
      (mode == FallbackLightMode::NoLightsPresent && noLightsPresent)
    ) {
      auto const& mainCamera = cameraManager.getMainCamera();
      const auto oldFallbackLightPresent = m_fallbackLight.has_value();

      const auto type = fallbackLightType();

      if (type == FallbackLightType::Distant) {
        // Note: Distant light does not need to be dynamic, do not recreate every frame.
        if (!oldFallbackLightPresent) {
          // Create the Distant Fallback Light

          m_fallbackLight.emplace(RtDistantLight(
            // Note: Distant light direction must be normalized, but a non-normalized direction is provided as an option.
            normalize(fallbackLightDirection()),
            fallbackLightAngle() * kDegreesToRadians / 2.0f,
            fallbackLightRadiance()
          ));
        }
      } else if (type == FallbackLightType::Sphere) {
        // Create the Sphere Fallback Light

        const auto oldSphereLightBufferIndex = oldFallbackLightPresent ? m_fallbackLight->getBufferIdx() : 0;

        const auto enableFallback = enableFallbackLightShaping();

        const bool shapingEnabled = enableFallback;
        Vector3 primaryAxis = Vector3(0.0f, 0.0f, 1.0f);
        float cosConeAngle = 0.0f;
        float coneSoftness = 0.0f;
        float focusExponent = 0.0f;

        if (enableFallback) {
          if (enableFallbackLightViewPrimaryAxis()) {
            primaryAxis = mainCamera.getDirection();
          } else {
            // Note: Must normalize the fallback light's primary axis as it is specified by options or ImGui and has
            // no hard requirement to be normalized.
            primaryAxis = safeNormalize(fallbackLightPrimaryAxis(), Vector3(0.0f, 0.0f, 1.0f));
          }

          cosConeAngle = std::cos(fallbackLightConeAngle() * kDegreesToRadians);
          coneSoftness = fallbackLightConeSoftness();
          focusExponent = fallbackLightFocusExponent();
        }

        // Note: Will be recreated every frame due to the need to be dynamic. Not super effecient but this is only
        // a one-off use case for debugging so performance is not super important here.
        m_fallbackLight.emplace(RtSphereLight(
          mainCamera.getPosition() + fallbackLightPositionOffset(),
          fallbackLightRadiance(),
          fallbackLightRadius(),
          RtLightShaping(shapingEnabled, primaryAxis, cosConeAngle, coneSoftness, focusExponent)
        ));

        // Update light dynamic properties

        // Note: Sphere fallback lights are dynamic due to following the camera position.
        m_fallbackLight->isDynamic = true;

        if (oldFallbackLightPresent) {
          // Note: Carry buffer index over from previous frame if the fallback light was present on the last frame.
          m_fallbackLight->setBufferIdx(oldSphereLightBufferIndex);
        }
      }
    } else if (
      (mode == FallbackLightMode::Never) ||
      (mode == FallbackLightMode::NoLightsPresent && !noLightsPresent)
    ) {
      if (m_fallbackLight.has_value()) {
        m_fallbackLight.reset();
      }
    }

    // Light buffer
    const uint32_t previousLightActiveCount = m_currentActiveLightCount;
    m_currentActiveLightCount = 0;

    std::swap(m_lightBuffer, m_previousLightBuffer);

    // Linearize the light list
    // Note: This is done rather than just iterating over the light list twice mostly so that the fallback light
    // can be processed like all other lights without complex logic at the cost of potentially more computational
    // cost, but it might actually work out in favor of performance since unordered map traversal done redundantly
    // may be more expensive than simple vector traversal on the linearized list.
    
    m_linearizedLights.clear();

    if (m_fallbackLight) {
      m_linearizedLights.emplace_back(&*m_fallbackLight);
    }

    for (auto&& pair : m_lights) {
      RtLight& light = pair.second;

      m_linearizedLights.emplace_back(&light);
    }

    for (auto&& pair : m_externallyTrackedLights) {
      m_linearizedLights.emplace_back(&pair.second);
    }

    for (auto& handle : m_externalActiveLightList) {
      auto found = m_externalLights.find(handle);
      if (found != m_externalLights.end()) {
        m_linearizedLights.emplace_back(&found->second);
     }
    }

    // Count the active light of each type

    m_lightTypeRanges.fill(LightRange {});
    for (auto&& linearizedLight : m_linearizedLights) {
      const RtLight& light = *linearizedLight;

      if (light.getColorAndIntensity().w <= 0) {
        continue;
      }

      ++m_lightTypeRanges[static_cast<uint32_t>(light.getType())].count;
      ++m_currentActiveLightCount;

      // Note: Highest light index reserved for the invalid index sentinel.
      if (m_currentActiveLightCount == LIGHT_INDEX_INVALID) {
        ONCE(Logger::info(str::format("[RTX-Compatibility-Info] Raytracing support more than 65535 lights currently, skipping some lights for now.")));
        break;
      }
    }

    // Arrange the ligth ranges of each types sequentially in the buffer, reset the counts

    uint offset = 0;
    for (uint lightType = 0; lightType < lightTypeCount; ++lightType) {
      LightRange& range = m_lightTypeRanges[lightType];
      range.offset = offset;
      offset += range.count;
      range.count = 0;
    }

    assert(offset == m_currentActiveLightCount);
    uint32_t lightsWritten = 0;

    const size_t lightsGPUSize = m_currentActiveLightCount * kLightGPUSize;
    const uint32_t lightMappingBufferEntries = m_currentActiveLightCount + previousLightActiveCount;

    // Resize persistent data buffers
    // Note: std::vector::shrink_to_fit may potentially be useful to call on these buffers in the future if the new desired size is much smaller
    // than the reserved capacity if support for many more lights than 2^16 is desired to allow reclaiming of some memory. For now though this
    // is not an issue and the buffers are allowed to keep whatever capacity they have allocated between calls for the sake of performance.

    m_lightsGPUData.resize(lightsGPUSize);
    memset(m_lightsGPUData.data(), 0xff, sizeof(char)* m_lightsGPUData.size());
    m_lightMappingData.resize(lightMappingBufferEntries);

    // Clear all slots to new light
    memset(m_lightMappingData.data(), kNewLightIdx, sizeof(uint16_t) * m_lightMappingData.size());

    // Write the light data into the previously allocated ranges
    for (auto&& linearizedLight : m_linearizedLights) {
      RtLight& light = *linearizedLight;

      if (light.getColorAndIntensity().w > 0 && lightsWritten < m_currentActiveLightCount) {
        // Find the buffer location for this light
        LightRange& range = m_lightTypeRanges[static_cast<uint32_t>(light.getType())];
        uint32_t newBufferIdx = range.offset + range.count;
        ++range.count;

        // RTXDI needs a mapping from previous light idx to current (to deal with light list reordering)
        if (light.getBufferIdx() != kNewLightIdx)
          m_lightMappingData[m_currentActiveLightCount + light.getBufferIdx()] = (uint16_t)newBufferIdx;

        // Also a mapping from current light idx to previous (for unbiased resampling)
        m_lightMappingData[newBufferIdx] = light.getBufferIdx();

        // Prepare data for GPU
        size_t dataOffset = newBufferIdx * kLightGPUSize;
        assert(dataOffset < lightsGPUSize);
        light.writeGPUData(m_lightsGPUData.data(), dataOffset);

        // Update the position in buffer for next frame
        light.setBufferIdx(newBufferIdx);

        // Guard against overflowing the light buffer, in case the light counting loop above terminated early
        ++lightsWritten;
      } else {
        // This light is either disabled or didn't fit into the buffer, so set its buffer index to invalid.
        light.setBufferIdx(kNewLightIdx);
      }
    }

    // Allocate the light buffer and copy its contents from host to device memory
    DxvkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    info.usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    info.stages = VK_PIPELINE_STAGE_TRANSFER_BIT;
    info.access = VK_ACCESS_TRANSFER_WRITE_BIT;
    info.size = align(lightsGPUSize, kBufferAlignment);

    // Note: Only allocating the light buffer here, not the previous light buffer as on the first frame it is fine for it to be null as
    // no previous frame light indices can possibly exist (and thus nothing in the shader should be trying to access it). On the next frame
    // after the light buffer and previous light buffer are swapped, this code will allocate another buffer and the process will continue
    // fine swapping back and forth from that point onwards.
    if (info.size > 0 && (m_lightBuffer == nullptr || info.size > m_lightBuffer->info().size)) {
      m_lightBuffer = m_device->createBuffer(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXBuffer, "Light Buffer");
    }

    info.size = align(lightMappingBufferEntries * sizeof(uint16_t), kBufferAlignment);
    if (info.size > 0 && (m_lightMappingBuffer == nullptr || info.size > m_lightMappingBuffer->info().size)) {
      m_lightMappingBuffer = m_device->createBuffer(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXBuffer, "Light Mapping Buffer");
    }
    
    if (!m_lightsGPUData.empty()) {
      ctx->writeToBuffer(m_lightBuffer, 0, m_lightsGPUData.size(), m_lightsGPUData.data());
    }

    if (!m_lightMappingData.empty()) {
      ctx->writeToBuffer(m_lightMappingBuffer, 0, m_lightMappingData.size() * sizeof(uint16_t), m_lightMappingData.data());
    }

    // If there are no lights with >0 intensity, then clear the list...
    if (m_currentActiveLightCount == 0)
      clear();

    // Generate a GPU dome light if necessary
    DomeLight activeDomeLight;
    if (getActiveDomeLight(activeDomeLight)) {
      // Ensures a texture stays in VidMem
      SceneManager& sceneManager = device()->getCommon()->getSceneManager();
      sceneManager.trackTexture(activeDomeLight.texture, m_gpuDomeLightArgs.textureIndex, true, false);

      m_gpuDomeLightArgs.active = true;
      m_gpuDomeLightArgs.radiance = activeDomeLight.radiance;
      m_gpuDomeLightArgs.worldToLightTransform = activeDomeLight.worldToLight;
    } else {
      m_gpuDomeLightArgs.active = false;
      m_gpuDomeLightArgs.radiance = Vector3(0.0f);
      m_gpuDomeLightArgs.textureIndex = BINDING_INDEX_INVALID;
    }

    // Reset external active light list.
    m_externalActiveDomeLight = nullptr;
    m_externalActiveLightList.clear();
  }

  static const float kNotSimilar = -1.f;
  float LightManager::isSimilar(const RtLight& a, const RtLight& b, float distanceThreshold) {
    static const float kCosAngleSimilarityThreshold = cos(5.f * kPi / 180.f);

    // Basic similarity check.
    const bool sameType = a.getType() == b.getType();

    if (!sameType)
      return kNotSimilar;

    if (a.getType() == RtLightType::Distant) {
      // Distant lights should be compared against their direction
      const float cosAngle = dot(a.getDirection(), b.getDirection());
      const bool similarDirection = cosAngle >= kCosAngleSimilarityThreshold;

      return similarDirection ? cosAngle : kNotSimilar;
    } else {
      // This is just an epsilon, at which distance should we collapse similar lights into a single light.
      const float distNormalized = length(a.getPosition() - b.getPosition()) / distanceThreshold;
      const bool similarPosition = distNormalized <= 1.f;

      if (a.getType() == RtLightType::Sphere) {
        const RtLightShaping& aShaping = a.getSphereLight().getShaping();
        const RtLightShaping& bShaping = b.getSphereLight().getShaping();
        if (aShaping.getEnabled() != bShaping.getEnabled()) {
          return kNotSimilar;
        }

        if (aShaping.getEnabled() && bShaping.getEnabled()) {
          const float cosAxis = dot(aShaping.getDirection(), bShaping.getDirection());
          if (cosAxis < kCosAngleSimilarityThreshold) {
            return kNotSimilar;
          }
          const float coneAngleDelta = std::abs(aShaping.getCosConeAngle() - bShaping.getCosConeAngle());
          if (coneAngleDelta > 0.01f) {
            return kNotSimilar;
          }
          float coneSoftnessDelta = std::abs(aShaping.getConeSoftness() - bShaping.getConeSoftness());
          if (coneSoftnessDelta > 0.01f) {
            return kNotSimilar;
          }
        }
      }

      return similarPosition ? (1.f - distNormalized) : kNotSimilar;
    }
  }

  void LightManager::updateLight(const RtLight& in, RtLight& out) {
    // This is somewhat of a blank slate currently to allow for future improvement.
    out.isStaticCount = 0; // This light is not static anymore.
    out.setBufferIdx(in.getBufferIdx());  // We remapped this light.
  }

  RtLight* LightManager::addLight(const RtLight& rtLight, const DrawCallState& drawCallState, const RtLightAntiCullingType antiCullingType) {
    if (drawCallState.getCategoryFlags().test(InstanceCategories::IgnoreLights))
      return nullptr;

    // Mesh->Lights Replacement
    if (antiCullingType == RtLightAntiCullingType::MeshReplacement) {
      rtLight.cacheMeshReplacementAntiCullingProperties(
        drawCallState.getTransformData().objectToWorld, drawCallState.getGeometryData().boundingBox);
    }

    return addLight(rtLight, antiCullingType);
  }

  void LightManager::addGameLight(const D3DLIGHTTYPE type, const RtLight& rtLight) {
    switch (type) {
    case D3DLIGHT_DIRECTIONAL:
      if (ignoreGameDirectionalLights())
        return;
      break;

    case D3DLIGHT_POINT:
      if (ignoreGamePointLights())
        return;
      break;

    case D3DLIGHT_SPOT:
      if (ignoreGameSpotLights())
        return;
      break;
    default:
      assert(false && "Invalid option passed to addGameLight");
      break;
    }

    if (RtxOptions::AntiCulling::isLightAntiCullingEnabled() && type == D3DLIGHT_POINT) {
      // Cache the sphere light data into replacement properties so we can unify the game light and light replacement into a single case in LightManager::garbageCollection
      rtLight.cacheLightReplacementAntiCullingProperties(rtLight.getSphereLight());

      addLight(rtLight, RtLightAntiCullingType::GameLight);
    } else {
      addLight(rtLight, RtLightAntiCullingType::Ignore);
    }
  }

  RtLight* LightManager::addLight(const RtLight& rtLight, const RtLightAntiCullingType antiCullingType) {
    if (!m_lightDebugUILock.owns_lock()) {
      // As addLight can actually erase old lights, we need to lock the mutex starting from the first call each frame.
      m_lightDebugUILock.lock();
    }

    // This light is "off". This includes negative valued lights which in D3D games originally would act as subtractive lighting.
    const Vector3 originalRadiance = rtLight.getRadiance();
    if (originalRadiance.x < 0 || originalRadiance.y < 0 || originalRadiance.z < 0
        || (originalRadiance.x <= 0 && originalRadiance.y <= 0 && originalRadiance.z <= 0))
      return nullptr;

    RtLight* result = nullptr;
    rtLight.setLightAntiCullingType(antiCullingType);

    const auto& foundLightIt = m_lights.find(rtLight.getTransformedHash());
    if (foundLightIt != m_lights.end()) {
      // Ignore changes in the same frame
      if (foundLightIt->second.getFrameLastTouched() != m_device->getCurrentFrameId()) {
        if (!rtLight.isDynamic && !suppressLightKeeping()) {
          // Update the light - its an exact hash match (meaning it's static)
          const uint32_t isStaticCount = foundLightIt->second.isStaticCount;

          // If this light hasnt moved for N frames, put it to sleep.  This is a defeat device to stop games aggressively ramping up/down intensity as lights 
          if (isStaticCount < RtxOptions::getNumFramesToPutLightsToSleep()) {
            uint32_t bufferIdx = foundLightIt->second.getBufferIdx();
            foundLightIt->second = rtLight;
            foundLightIt->second.setBufferIdx(bufferIdx);
          }

          // Still static, so increment our counter.
          foundLightIt->second.isStaticCount = isStaticCount + 1;
        } else {
          uint32_t bufferIdx = foundLightIt->second.getBufferIdx();
          foundLightIt->second = rtLight;
          foundLightIt->second.setBufferIdx(bufferIdx);
        }

        // We saw this light so bump its frame counter.
        foundLightIt->second.setFrameLastTouched(m_device->getCurrentFrameId());
      }
      result = &foundLightIt->second;
    } else {
      //  Try find a similar light
      std::optional<decltype(m_lights)::iterator> similarLight;
      float bestSimilarity = kNotSimilar;
      for (auto similarLightIterator = m_lights.begin(); similarLightIterator != m_lights.end(); ++similarLightIterator) {
        const RtLight& light = similarLightIterator->second;
        if (light.getPrimInstanceOwner().getReplacementInstance() != nullptr) {
          // lights that are part of a replacement should not be considered.
          continue;
        }

        // Update the cached light if it's similar.  This should catch minor perturbations in static lights (e.g. due to precision loss)
        const float kDistanceThresholdMeters = 0.02f;
        const float kDistanceThresholdWorldUnits = kDistanceThresholdMeters * RtxOptions::getMeterToWorldUnitScale();
        const float thisLightsSimilarity = isSimilar(light, rtLight, kDistanceThresholdWorldUnits);

        if (thisLightsSimilarity >= 0.f && thisLightsSimilarity > bestSimilarity) {
          // Copy off light state.
          similarLight = similarLightIterator;
          bestSimilarity = thisLightsSimilarity;
        } 
      }

      // Add as a new light (with/out updated data depending on if a similar light was found)
      const auto& [localLightIterator, addedSuccessfully] = m_lights.try_emplace(rtLight.getTransformedHash(), rtLight);
      RtLight& localLight = localLightIterator->second;

      // Note: Ensure that the new light was added successfully (meaning that no existing light existed in the light map at the
      // given Light instance hash). This should always be the case as this code is in the "else" branch of a check to see if the
      // light exists in the map already, meaning a light with this hash should not be present in this case.
      // If this fact ever changes, use insert_or_assign instead of emplace to insert or overwrite the light in the map if that is
      // the desired behavior.
      assert(addedSuccessfully);

      if (similarLight.has_value()) {
        // Copy/interpolate any state we like from the similar light.
        updateLight(similarLight.value()->second, localLight);

        // Remove the similar light from the map
        m_lights.erase(similarLight.value());
      }

      // Record we saw this light
      localLight.setFrameLastTouched(m_device->getCurrentFrameId());
      result = &localLight;
    }
    return result;
  }

  // Creates a new externally tracked light. These lights have their lifecycle managed by external systems
  // rather than LightManager's frame-to-frame tracking and anti-culling systems.
  RtLight* LightManager::createExternallyTrackedLight(const RtLight& light) {
    if (!m_lightDebugUILock.owns_lock()) {
      m_lightDebugUILock.lock();
    }
    
    auto [newLightIt, addedSuccessfully] = m_externallyTrackedLights.try_emplace(m_nextExternallyTrackedLightId, light);
    assert(addedSuccessfully);
    RtLight* newLight = &newLightIt->second;
    newLight->setExternallyTrackedLightId(m_nextExternallyTrackedLightId);
    m_nextExternallyTrackedLightId++;
    return newLight;
  }

  // Updates an existing externally tracked light with new data. The light's lifecycle is managed by external systems
  // rather than LightManager's frame-to-frame tracking and anti-culling systems.
  void LightManager::updateExternallyTrackedLight(RtLight* light, const RtLight& newLight) {
    if (!m_lightDebugUILock.owns_lock()) {
      m_lightDebugUILock.lock();
    }
    assert(light->getExternallyTrackedLightId() != kInvalidExternallyTrackedLightId && " light passed to updateExternallyTrackedLight is not actually externally tracked.");
    uint16_t bufferIdx = light->getBufferIdx();
    *light = newLight;
    light->setFrameLastTouched(m_device->getCurrentFrameId());
    light->setBufferIdx(bufferIdx);
  }

  // Marks an externally tracked light for garbage collection. The light's lifecycle is managed by external systems
  // rather than LightManager's frame-to-frame tracking and anti-culling systems.
  void LightManager::removeExternallyTrackedLight(RtLight* light) {
    light->markForGarbageCollection();
  }

  void LightManager::addExternalLight(remixapi_LightHandle handle, const RtLight& rtlight) {
    auto found = m_externalLights.find(handle);
    if (found != m_externalLights.end()) {
      // TODO: warn the user about id collision,
      //       or just overwriting existing one is fine?
      found->second = rtlight;
    } else {
      m_externalLights.emplace(handle, rtlight);
    }
  }

  void LightManager::removeExternalLight(remixapi_LightHandle handle) {
    m_externalLights.erase(handle);
    m_externalDomeLights.erase(handle);
  }

  bool LightManager::getActiveDomeLight(DomeLight& domeLightOut) {
    if (m_externalDomeLights.size() == 0 || m_externalActiveDomeLight == nullptr) {
      return false;
    }

    auto found = m_externalDomeLights.find(m_externalActiveDomeLight);
    if (found == m_externalDomeLights.end()) {
      // Invalid active dome light, reset it
      m_externalActiveDomeLight = nullptr;
      return false;
    }

    domeLightOut = found->second;

    return true;
  }

  void LightManager::addExternalDomeLight(remixapi_LightHandle handle, const DomeLight& domeLight) {
    auto found = m_externalDomeLights.find(handle);
    if (found != m_externalDomeLights.end()) {
      // TODO: warn the user about id collision,
      //       or just overwriting existing one is fine?
      found->second = domeLight;
    } else {
      m_externalDomeLights.emplace(handle, domeLight);
    }
  }

  void LightManager::addExternalLightInstance(remixapi_LightHandle enabledLight) {
    if (m_externalLights.find(enabledLight) != m_externalLights.end()) {
      m_externalActiveLightList.insert(enabledLight);
    } else if (m_externalDomeLights.find(enabledLight) != m_externalDomeLights.end() && m_externalActiveDomeLight == nullptr) {
      m_externalActiveDomeLight = enabledLight;
    }
  }

  void LightManager::setRaytraceArgs(RaytraceArgs& raytraceArgs, uint32_t rtxdiInitialLightSamples, uint32_t volumeRISInitialLightSamples, uint32_t risLightSamples) const
  {
    // The algorithm below performs two tasks:
    // 1. Fills raytraceArgs.lightRanges[] with light range offsets and counts;
    // 2. Distributes the RTXDI and RIS samples statically among the light types, proportional to the light counts.
    //
    // The distribution code makes sure that there is at least one sample for each non-empty type, and the rest is
    // approximate, i.e. the sample counts set by the user are just guidelines and the actual total count can be
    // slightly different.

    raytraceArgs.rtxdiTotalSampleCount = 0;
    raytraceArgs.volumeRISTotalSampleCount = 0;
    raytraceArgs.risTotalSampleCount = 0;

    // Calculate the requested amount of samples per active light, but no more than 1
    const float rtxdiSamplesPerLight = std::min(1.f, (float)rtxdiInitialLightSamples / std::max(m_currentActiveLightCount, 1u));
    const float volumeRISSamplesPerLight = std::min(1.f, (float)volumeRISInitialLightSamples / std::max(m_currentActiveLightCount, 1u));
    const float risSamplesPerLight = std::min(1.f, (float)risLightSamples / std::max(m_currentActiveLightCount, 1u));

    // Go over all light types and ranges
    for (uint32_t lightType = 0; lightType < lightTypeCount; ++lightType) {
      const LightRange& srcRange = m_lightTypeRanges[lightType];
      LightRangeInfo& dstRange = raytraceArgs.lightRanges[lightType];

      // Copy the range info
      dstRange.offset = srcRange.offset;
      dstRange.count = srcRange.count;

      // Calculate the actual sample counts for this light type
      // Note: uint16 safe to use here as the total number of samples to take is a uint16 to begin with and thus these values
      // for per-light type sample counts should not be greater.
      dstRange.rtxdiSampleCount = (srcRange.count > 0 && rtxdiSamplesPerLight > 0.f)
        ? std::max((uint16_t)1u, (uint16_t) roundf(rtxdiSamplesPerLight * (float) srcRange.count)) : 0u;
      dstRange.volumeRISSampleCount = (srcRange.count > 0 && volumeRISSamplesPerLight > 0.f)
        ? std::max((uint16_t)1u, (uint16_t) roundf(volumeRISSamplesPerLight * (float) srcRange.count)) : 0u;
      dstRange.risSampleCount = (srcRange.count > 0 && risSamplesPerLight > 0.f)
        ? std::max((uint16_t)1u, (uint16_t) roundf(risSamplesPerLight * (float) srcRange.count)) : 0u;

      // Count the total samples - necessary to compute the correct PDF during sampling (not currently used by RTXDI anymore
      // at least due to changing how it does its sampling, still in use in other cases though).
      raytraceArgs.rtxdiTotalSampleCount += dstRange.rtxdiSampleCount;
      raytraceArgs.volumeRISTotalSampleCount += dstRange.volumeRISSampleCount;
      raytraceArgs.risTotalSampleCount += dstRange.risSampleCount;
    }
  }

  uint LightManager::getLightCount(uint type) {
    if (type >= lightTypeCount) {
      return 0;
    }
    return m_lightTypeRanges[type].count;
  }

}  // namespace dxvk
