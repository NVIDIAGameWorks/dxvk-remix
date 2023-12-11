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
#include <mutex>
#include <vector>
#include <assert.h>

#include "rtx_ray_portal_manager.h"

#include "rtx_context.h"
#include "../d3d9/d3d9_state.h"
#include "../util/util_vector.h"
#include "../util/util_matrix.h"
#include "rtx_intersection_test_helpers.h"
#include "rtx_options.h"
#include "rtx/concept/ray/ray_utilities.h"

namespace dxvk {

  static void prepareRayPortalHitInfo(RayPortalHitInfo& result, const RayPortalInfo& info, const Matrix4& transform)
  {
    result.encodedPortalToOpposingPortalDirection.set(transform);

    result.centroid = info.centroid;
    result.materialIndex = info.materialIndex;

    result.normal = info.planeNormal;
    result.sampleThreshold = 1.0f;

    result.xAxis = info.planeBasis[0];
    result.inverseHalfWidth = (info.planeHalfExtents.x == 0.0f) ?
      0.0f :
      1.0f / info.planeHalfExtents.x;

    result.yAxis = info.planeBasis[1];
    result.inverseHalfHeight = (info.planeHalfExtents.y == 0.0f) ?
      0.0f :
      1.0f / info.planeHalfExtents.y;

    result.textureTransform.x = glm::packHalf2x16(glm::vec2(info.textureTransform[0][0], info.textureTransform[0][1]));
    result.textureTransform.y = glm::packHalf2x16(glm::vec2(info.textureTransform[1][0], info.textureTransform[1][1]));
    result.textureTransform.z = glm::packHalf2x16(glm::vec2(info.textureTransform[2][0], info.textureTransform[2][1]));

    result.spriteSheetRows = info.spriteSheetRows;
    result.spriteSheetCols = info.spriteSheetCols;
    result.spriteSheetFPS = info.spriteSheetFPS;
  }

  RayPortalManager::RayPortalManager(DxvkDevice* device, ResourceCache* pResourceCache)
    : CommonDeviceObject(device)
    , kCameraDepthPenetrationThreshold(RtxOptions::Get()->rayPortalCameraInBetweenPortalsCorrectionThreshold() * RtxOptions::Get()->getMeterToWorldUnitScale())
    , m_pResourceCache(pResourceCache) {
  }

  RayPortalManager::~RayPortalManager() {
  }

  // Calculates an offset that needs to be applied to ray origin to avoid it aliasing with exit portal plane after its teleported
  Vector3 RayPortalManager::calculateRayOriginOffset(const Vector3& centroid, const Vector3& planeNormal) {
    return rayOffsetSurfaceHelper(centroid, planeNormal) - centroid;
  };

  void RayPortalManager::processRayPortalData(RtInstance& instance, const RtSurfaceMaterial& material) {
    if (material.getType() != RtSurfaceMaterialType::RayPortal) {
      return;
    }

    const auto& drawCall = instance.getBlas()->input;
    Matrix4 objectToWorld = instance.getTransform();

    // Set Ray Portal Information

    auto&& originalGeometryData = drawCall.getGeometryData();
    auto&& rayPortalSurfaceMaterial = material.getRayPortalSurfaceMaterial();
    auto& rayPortalInfo = m_rayPortalInfos[rayPortalSurfaceMaterial.getRayPortalIndex()];

    // Note: Ignore duplicate Ray Portals if the index has already been set
    if (rayPortalInfo.has_value()) {
      // Hide the duplicate instance to avoid artifacts when one instance is offset and another is not
      instance.setHidden(true);
      return;
    }

    // Portals must be simple plane like objects, and so have 6 or less indices (two triangles)
    if (originalGeometryData.indexCount > 6)
      return;

    const GeometryBufferData bufferData(originalGeometryData);

    // Todo: Currently we do not have a great way of accessing the position and index 
    // information on the CPU side here (though it is available further up when it is passed to D3D9). The functions
    // to map these buffers I think may return nullptr if the buffer is not first copied to CPU memory, 
    // but these buffers may be host visible to begin with which is why it currently works without that.
    // In the future this should be improved though to avoid potential issues in other games though that
    // may wish to use Ray Portals.
    if (!bufferData.indexData || !bufferData.positionData)
      return;

    // Make sure that the geometry matches our expected pattern, which is 1 quad as a triangle strip
    // Note: Portal (at least our modified version of it) has 4 vertices for the Portal object, each of which represents a corner.
    constexpr uint32_t indicesPerQuad = 4;

    // Calculate world space vertices of the Ray Portal

    Vector3 worldVertices[indicesPerQuad];
    Vector3 centroid{ 0.0f, 0.0f, 0.0f };

    Vector3 maxAbsVertexWorldCoords = Vector3(0.f);

    const bool indices16bit = (originalGeometryData.indexBuffer.indexType() == VK_INDEX_TYPE_UINT16);

    std::unordered_set<uint32_t> uniqueIndices;
    for (size_t idx = 0; idx < originalGeometryData.indexCount; ++idx) {
      const uint32_t currentIndex = indices16bit ? bufferData.getIndex(idx) : bufferData.getIndex32(idx);
      if (uniqueIndices.find(currentIndex) != uniqueIndices.end()) {
        continue;
      }

      // Note: This may not be "model" coordinates as many games like to pre-transform the positions into worldspace 
      // to perhaps avoid needing a world matrix in legacy
      // API implementations where it may have had a more significant cost to apply.
      const Vector4 currentPosition(bufferData.getPosition(currentIndex), 1.0f);

      const Vector3 currentWorldPosition((objectToWorld * currentPosition).xyz());

      centroid += currentWorldPosition;
      worldVertices[uniqueIndices.size()] = currentWorldPosition;

      for (uint32_t i = 0; i < 3; i++)
        maxAbsVertexWorldCoords[i] = std::max(abs(currentWorldPosition[i]), maxAbsVertexWorldCoords[i]);

      uniqueIndices.insert(currentIndex);
    }

    // Not enough unique vertices to extract a Portal
    if (uniqueIndices.size() < 3)
      return;

    centroid /= static_cast<float>(indicesPerQuad);

    // Todo: Calculate relevant projection axes in the future from the world space coordinates 
    // via something more generic like PCA as unfortunately model space is
    // unavailable in some games (like Portal). Right now though we just do a specialized approach 
    // based on assumptions about the mesh layout.

    // Compute the plane from the Ray Portal (Specialized version for Portal)
    
    const Vector3 xVector{ worldVertices[2] - worldVertices[0] };
    const Vector3 yVector{ worldVertices[1] - worldVertices[0] };
    const Vector2 planeHalfExtents{ length(xVector) / 2, length(yVector) / 2 };
    const Vector3 xAxis{ normalize(xVector) };
    const Vector3 yAxis{ normalize(yVector) };
    const Vector3 zAxis{ normalize(cross(xAxis, yAxis)) };

    // Note: Scale not accounted for currently
    const Matrix4 worldToModelRotation{
      xAxis.x, yAxis.x, zAxis.x, 0.0f,
      xAxis.y, yAxis.y, zAxis.y, 0.0f,
      xAxis.z, yAxis.z, zAxis.z, 0.0f,
      0.0f, 0.0f, 0.0f, 1.0f
    };
    
    const Matrix4 worldToModelTranslation {
      1.0f, 0.0f, 0.0f, 0.0f,
      0.0f, 1.0f, 0.0f, 0.0f,
      0.0f, 0.0f, 1.0f, 0.0f,
      -centroid.x, -centroid.y, -centroid.z, 1.0f
    };

    // Calculate ray origin offset to avoid hitting exit portal.
    // ToDo: we should disable culling for portals instead, if portals are guaranteed
    //  to be offset enough from the objects they're placed upon (which in Portals they do).
    //  However doing that causes test failures on particles around the portal, so probably will 
    //  have to instantiate the shared material for portal quad only
    Vector3 rayOffset = calculateRayOriginOffset(maxAbsVertexWorldCoords, zAxis);
    
    uint32_t materialIndex;
    m_pResourceCache->find(material, materialIndex);

    RayPortalInfo newRayPortalInfo;
    newRayPortalInfo.worldToModelTransform = worldToModelRotation * worldToModelTranslation;
    newRayPortalInfo.centroid = centroid;
    newRayPortalInfo.planeBasis[0] = xAxis;
    newRayPortalInfo.planeBasis[1] = yAxis;
    newRayPortalInfo.planeNormal = zAxis;
    newRayPortalInfo.planeHalfExtents = planeHalfExtents;
    newRayPortalInfo.rayOffset = rayOffset;
    newRayPortalInfo.portalIndex = rayPortalSurfaceMaterial.getRayPortalIndex();
    newRayPortalInfo.isCreatedThisFrame = instance.isCreatedThisFrame(m_device->getCurrentFrameId());
    newRayPortalInfo.materialIndex = materialIndex;
    newRayPortalInfo.textureTransform = instance.surface.textureTransform;
    newRayPortalInfo.spriteSheetRows = instance.surface.spriteSheetRows;
    newRayPortalInfo.spriteSheetCols = instance.surface.spriteSheetCols;
    newRayPortalInfo.spriteSheetFPS = instance.surface.spriteSheetFPS;

    rayPortalInfo.emplace(newRayPortalInfo);
  }

  void RayPortalManager::clear() {
    // Note: Ensures the Ray Portal Infos are always reset after this scope ends to avoid stale data sticking in them if an early exit occurs
    // Resets the Ray Portal info (usually after the data is used by drawing) so they can be set by a new frame
    for (auto& rayPortalInfo : m_rayPortalInfos) {
      rayPortalInfo.reset();
    }
  }

  void RayPortalManager::garbageCollection() {
  }

  // Prepare scene data is copying constants to a structure - which is then consumed by raytracing CB
  void RayPortalManager::prepareSceneData(Rc<DxvkContext> /*ctx*/, const float /*frameTimeSecs*/) {
    ScopedCpuProfileZone();
    // Save the previous frame data
    memcpy(m_sceneData.previousRayPortalHitInfos, m_sceneData.rayPortalHitInfos, sizeof(m_sceneData.previousRayPortalHitInfos));

    uint8_t activeRayPortalCount = 0;

    // Invalidate the pointer to ray portal info that is about to reset
    m_cameraTeleportationRayPortalDirectionInfo = nullptr;

    // First clear the previous ray portal pair infos. The clear is delayed to this point so that 
    // the previous frame ray portal pair infos can be used for virtual instance matching during frame recording
    for (auto& rayPortalPairInfo : m_rayPortalPairInfos)
      rayPortalPairInfo.reset();

    static_assert(maxRayPortalCount == 2);
    // TODO: Fix Portal iteration (currently iterates in a pattern of 0,1, 1,2, 2,3 instead of 0,1, 2,3, 4,5 like it should)
    for (std::size_t i = 0; i < m_rayPortalInfos.size() / 2; ++i) {
      const uint8_t currentRayPortalIndex = i;
      // Note: The Opposing Ray Portal is always the next one in sequence, allowing for traversal in pairs.
      const uint8_t opposingRayPortalIndex = static_cast<uint8_t>(i) + 1;
      auto&& rayPortalInfo = m_rayPortalInfos[currentRayPortalIndex];
      auto&& opposingRayPortalInfo = m_rayPortalInfos[opposingRayPortalIndex];

      if (!rayPortalInfo.has_value() || !opposingRayPortalInfo.has_value()) {
        // Set the Ray Portal Hit Information for the pair to inactive
        m_sceneData.rayPortalHitInfos[currentRayPortalIndex].encodedPortalToOpposingPortalDirection.setInactive();
        m_sceneData.rayPortalHitInfos[opposingRayPortalIndex].encodedPortalToOpposingPortalDirection.setInactive();

        continue;
      }

      // Set Ray Portal Hit Information for the pair

      // Note: Flip directions across the width and depth axes (height should not be flipped for mirroring).
      const Matrix4 directionFlipMatrix{
        -1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, -1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
      };


      // Add an offset to the teleport matrix so that the teleported rays start further out and avoid self-intersecting the exit portal
      auto adjustForOriginOffset = [=](const Matrix4& worldTransformToOpposingPortal, RayPortalInfo& entryPortalInfo, RayPortalInfo& exitPortalInfo) {
        // Need to account for offset errors both on entry and exit of ray portals
        Matrix4 entryOffsetTransform = Matrix4();
        // Need to push the point into the entry portal
        entryOffsetTransform.data[3] = Vector4(-entryPortalInfo.rayOffset.x,
                                               -entryPortalInfo.rayOffset.y,
                                               -entryPortalInfo.rayOffset.z,
                                               1.f);

        Matrix4 exitOffsetTransform = Matrix4();
        exitOffsetTransform.data[3] = Vector4(exitPortalInfo.rayOffset.x,
                                              exitPortalInfo.rayOffset.y,
                                              exitPortalInfo.rayOffset.z,
                                              1.f);

        return exitOffsetTransform * worldTransformToOpposingPortal * entryOffsetTransform;
      };

      RayPortalPairInfo newRayPortalPairInfo;

      newRayPortalPairInfo.pairInfos[0].entryPortalInfo = *rayPortalInfo;
      newRayPortalPairInfo.pairInfos[0].portalToOpposingPortalDirectionWithoutRayOffset =
        inverse(opposingRayPortalInfo->worldToModelTransform) * directionFlipMatrix * rayPortalInfo->worldToModelTransform;
      newRayPortalPairInfo.pairInfos[0].portalToOpposingPortalDirection =
        adjustForOriginOffset(newRayPortalPairInfo.pairInfos[0].portalToOpposingPortalDirectionWithoutRayOffset,
                              *rayPortalInfo,
                              *opposingRayPortalInfo);

      newRayPortalPairInfo.pairInfos[1].entryPortalInfo = *opposingRayPortalInfo;
      newRayPortalPairInfo.pairInfos[1].portalToOpposingPortalDirectionWithoutRayOffset =
        inverse(rayPortalInfo->worldToModelTransform) * directionFlipMatrix * opposingRayPortalInfo->worldToModelTransform;
      newRayPortalPairInfo.pairInfos[1].portalToOpposingPortalDirection =
        adjustForOriginOffset(newRayPortalPairInfo.pairInfos[1].portalToOpposingPortalDirectionWithoutRayOffset,
                              *opposingRayPortalInfo,
                              *rayPortalInfo);

      m_rayPortalPairInfos[currentRayPortalIndex].emplace(newRayPortalPairInfo);

      // Set Ray Portal Light Information for the pair
      
       prepareRayPortalHitInfo(m_sceneData.rayPortalHitInfos[currentRayPortalIndex],
        rayPortalInfo.value(),
        m_rayPortalPairInfos[currentRayPortalIndex]->pairInfos[0].portalToOpposingPortalDirection);

       prepareRayPortalHitInfo(m_sceneData.rayPortalHitInfos[opposingRayPortalIndex], 
        opposingRayPortalInfo.value(),
        m_rayPortalPairInfos[currentRayPortalIndex]->pairInfos[1].portalToOpposingPortalDirection);

      activeRayPortalCount += 2;
    }

    for (std::size_t i = 0, activePortalIndex = 0; i < m_rayPortalInfos.size(); ++i) {
      m_sceneData.rayPortalHitInfos[i].sampleThreshold = activeRayPortalCount == 0 ? 1.0f : float(activePortalIndex) / activeRayPortalCount;
      if (m_sceneData.rayPortalHitInfos[i].encodedPortalToOpposingPortalDirection.isActive()) {
        activePortalIndex++;
      }
    }
    m_sceneData.numActiveRayPortals = activeRayPortalCount;
  }

  void RayPortalManager::fixCameraInBetweenPortals(RtCamera& camera) const {
    ScopedCpuProfileZone();

    if (!RtxOptions::Get()->getRayPortalCameraInBetweenPortalsCorrection())
      return;

    // Don't fix free camera
    if (camera.isFreeCameraEnabled())
      return;

    const Vector3 camPos = camera.getPosition();
    const Vector3 camDir = camera.getDirection();

    // Process all portal pairs
    for (uint32_t iPair = 0; iPair < m_rayPortalPairInfos.size(); iPair++) {

      auto& portalPair = m_rayPortalPairInfos[iPair];

      if (!(portalPair.has_value()))
        continue;

      uint32_t pairPortalBaseIdx = getRayPortalPairPortalBaseIndex(iPair);

      // Consider both sides
      for (uint32_t iPortalId = pairPortalBaseIdx; iPortalId < pairPortalBaseIdx + 2; iPortalId++) {

        auto& portalInfo = *m_rayPortalInfos[iPortalId];

        float orthoDistanceToPortal = calculateDistanceAlongNormalToPortal(camPos, portalInfo);
        
        if (orthoDistanceToPortal < -kCameraDepthPenetrationThreshold || orthoDistanceToPortal > 0)
          continue;

        if (projectedPointLiesInsideQuad(camPos,
          portalInfo.planeNormal, portalInfo.centroid, 
          portalInfo.planeBasis, portalInfo.planeHalfExtents)) {

          // Camera is in-between portals, and behind the current one
          // Push it out along the portal normal - it works well
          Vector3 offset = (-orthoDistanceToPortal) * portalInfo.planeNormal + 
            2.f * portalInfo.rayOffset;   // Ensure the offset camera doesn't end up on the portal plane, 
                                          // 1* rayOffset wasn't enough, 2 * works well
                                          // for teleportation detection when the offseting happens
          camera.applyArtificialWorldOffset(offset);

          Logger::info("[RTX] Camera was detected in-between portals. Pushed camera out.");

          // kCameraDepthPenetrationThreshold is small, so we don't need to really search for 
          // the closest portal. That could only be needed when the camera is is at the very corner
          // of two corner portals touching each other and it would rather inconclusive which way to
          // push it.
          return;
        }
      }
    }
  }

  // Detects portal teleportation since last frame and updates the previous camera transform 
  // to that of a virtual camera behind the exiting portal. The virtual camera is that of the
  // previous frame and includes the portal teleportation, placing it behind the exiting portal
  // and looking through it the same way the previous camera looked through it via entering portal. 
  // This is required for correct temporal reprojection lookup of data that was seen
  // through the portal in the previous frame.
  bool RayPortalManager::detectTeleportationAndCorrectCameraHistory(RtCamera& camera, RtCamera* viewmodelCamera) {
    m_cameraTeleportationRayPortalDirectionInfo = nullptr;

    if (!RtxOptions::Get()->getRayPortalCameraHistoryCorrection())
      return false;
    
    // Safe guard: let temporal camera fix its t1, t0 frames history
    // in-case of false teleportation detection. This is to prevent
    // t0 & t1 states being invalid due to a failure case 
    // as t1->t2 motion is estimated from t0->t1 states.
    // Teleportation shouldn't occur every 2 frames or less any way
    if (m_numFramesSinceTeleportationWasDetected++ < 2)
      return false;

    // There's no teleporation on free camera
    if (camera.isFreeCameraEnabled())
      return false;

    if (m_sceneData.numActiveRayPortals < 2)
      return false;

    // Camera matrices for time steps t2 (current), t1 (current - 1), t0 (current - 2)
    const Matrix4& viewToWorldT2 = camera.getViewToWorld();
    const Matrix4& viewToWorldT1 = camera.getPreviousViewToWorld();
    const Matrix4& viewToWorldT0 = camera.getPreviousPreviousViewToWorld();

    // Camera positions
    const Vector3& camPosT2 = viewToWorldT2[3].xyz();
    const Vector3& camPosT1 = viewToWorldT1[3].xyz();
    const Vector3& camPosT0 = viewToWorldT0[3].xyz();

    // Camera directions
    Vector3 camDirT2 = -viewToWorldT2[2].xyz();
    Vector3 camDirT1 = -viewToWorldT1[2].xyz();

    // Weight constants
    // Mostly gut-check set constants to handle tricky cases
    const float sigmaDir = 0.5; // ~ penalize less at start
    const float sigmaPos = 0.5;
    const float eps = 0.001f;
    const float minDirWeight = 0.25f;// Camera turning a lot of degrees per frame is valid,  
                                     // so don't disregard large angular changes completely
    const float minPosWeight = 0.15f;// We don't have a good normalization constant, and
                                     // normalizing using the sum of reciprocal distances
                                     // can make a position too close to the predicted pos 
                                     // completely disregard the other comparison target's weight, 
                                     // so we just clamp the weight

    // No teleportation case weights

    // Most basic, no camera rotation prediction, just compare direction at t1 against that of t2
    const float wDir = (dot(camDirT1, camDirT2) + 1) / 2;               // [-1, 1] => [0, 1]

    const Vector3 movementCamPosT0CamPosT1 = 
      camPosT1 - camPosT0       // Note t0 camera was already corrected for any teleportation at the time
      + camera.getArtificialWorldOffset();    // Take artificial world offset applied this frame into account

    Vector3 predictedPosT2 = camPosT1 + movementCamPosT0CamPosT1;
    const float wPos = 1 / (length(predictedPosT2 - camPosT2) + eps);   // Weights are inversely proportional to their distances from camPosT2


    // Selected teleportation candidate
    float maxCandidateWeight = 0.f;

    // Process all portal pairs
    for (uint32_t iPair = 0; iPair < m_rayPortalPairInfos.size(); iPair++) {

      auto& portalPair = m_rayPortalPairInfos[iPair];

      if (!(portalPair.has_value()))
        continue;

      uint32_t pairPortalBaseIdx = getRayPortalPairPortalBaseIndex(iPair);

      // Consider both combinations of a pair's entry | exit portals
      for (uint32_t iEntry = pairPortalBaseIdx; iEntry < pairPortalBaseIdx + 2; iEntry++) {

        uint32_t iExit = getOpposingRayPortalIndex(iEntry);

        // Adjust the actual cam position by the ray offsets to give it a little tolerance in case
        // the cameras are very close to a portal plane (such as in case if it was pushed out
        // after being detected in-between previously). Otherwise a line-segment portal intersection test
        // below may reject the intersection
        // 1 * rayOffset wasn't enough, 2 * worked well
        Vector3 _camPosT1 = camPosT1 + 2.f * portalPair->pairInfos[iEntry].entryPortalInfo.rayOffset;
        Vector3 _camPosT2 = camPosT2 + 2.f * portalPair->pairInfos[iExit].entryPortalInfo.rayOffset;

        auto lineSegmentIntersectsPortal = [&](
          const Vector3& l0, // line segment start
          const Vector3& l1, // line segment end
          const RayPortalInfo& portal) {
            // Increase the plane size a bit to account for cases where the camera is slightly outside of the geometry during transit
            const float portalSizeScale = 1.5f;

            // Scale up the line segment to avoid cases when l0 or l1 are too close to the portal and failing the camera teleportation check
            const float lineSegmentScale = 1.5f;
            Vector3 lineSegment_l0_to_l1 = l1 - l0;
            Vector3 adjustedL0 = l1 - lineSegmentScale * lineSegment_l0_to_l1;
            Vector3 adjustedL1 = l0 + lineSegmentScale * lineSegment_l0_to_l1;
            
            return lineSegmentIntersectsQuad(
              adjustedL0,
              adjustedL1,
              portal.planeNormal,
              portal.centroid,
              portal.planeBasis,
              portal.planeHalfExtents * portalSizeScale);
        };

        // Check 1: camPosT1 and camPosT2 must be on the active side of entry and exit portal planes
        if (!(
          isInFrontOfPortal(_camPosT1, *m_rayPortalInfos[iEntry]) &&
          isInFrontOfPortal(_camPosT2, *m_rayPortalInfos[iExit]))) {
          continue;
        }

        // Check 2: virtual camera to camera line segments must intersect entry and exit portals
        // Virtual camera is the camera that's transformed from Portal X to Portal Y coordinate system
        Vector3 virtualCamPosT1inExitCoordSystem = getVirtualPosition(_camPosT1, portalPair->pairInfos[iEntry].portalToOpposingPortalDirection);
        Vector3 virtualCamPosT2inEntryCoordSystem = getVirtualPosition(_camPosT2, portalPair->pairInfos[iExit].portalToOpposingPortalDirection);

        if (!(
          lineSegmentIntersectsPortal(virtualCamPosT1inExitCoordSystem, _camPosT2, *m_rayPortalInfos[iExit]) &&
          lineSegmentIntersectsPortal(virtualCamPosT2inEntryCoordSystem, _camPosT1, *m_rayPortalInfos[iEntry]))) {
          continue;
        }

        // Weight teleportation portal pair candidate against the no teleportation case
        {
          auto normalizeWeights = [&](float& w1, float& w2, float sigma, float minWeight) {
            w1 = pow(w1, sigma);
            w2 = pow(w2, sigma);
            float rcpSum = 1 / (w1 + w2);
            w1 = std::max(w1 * rcpSum, minWeight);
            w2 = std::max(w2 * rcpSum, minWeight);
          };

          // Directional weights
          Vector3 virtualCamDirT1inExitCoordSystem = Matrix3(portalPair->pairInfos[iEntry].portalToOpposingPortalDirection) * camDirT1;
          float wVirtualDir = (dot(virtualCamDirT1inExitCoordSystem, camDirT2) + 1) / 2;  // [-1, 1] => [0, 1]
          float _wDir = wDir;
          normalizeWeights(_wDir, wVirtualDir, sigmaDir, minDirWeight);

          // Positional weights
          Vector3 predictedVirtualPosT2inExitCoordSystem = getVirtualPosition(predictedPosT2, portalPair->pairInfos[iEntry].portalToOpposingPortalDirection);
          float wVirtualPos = 1 / (length(predictedVirtualPosT2inExitCoordSystem - _camPosT2) + eps);
          float _wPos = wPos;
          normalizeWeights(_wPos, wVirtualPos, sigmaPos, minPosWeight);

          float candidateWeight = wVirtualDir * wVirtualPos;

          if (candidateWeight > _wDir * _wPos && candidateWeight > maxCandidateWeight) {
            maxCandidateWeight = candidateWeight;
            m_cameraTeleportationRayPortalDirectionInfo = &portalPair->pairInfos[iEntry];
          }
        }
      }
    }

    if (m_cameraTeleportationRayPortalDirectionInfo) {
      auto applyCorrection = [](RtCamera& c, const Matrix4& portalToOpposingPortalDirection) {
        Matrix4 virtualViewToWorldT1inExitCoordSystem = portalToOpposingPortalDirection * c.getPreviousViewToWorld();
        c.setPreviousViewToWorld(virtualViewToWorldT1inExitCoordSystem);
      };

      applyCorrection(camera, m_cameraTeleportationRayPortalDirectionInfo->portalToOpposingPortalDirection);
      if (viewmodelCamera) {
        applyCorrection(*viewmodelCamera, m_cameraTeleportationRayPortalDirectionInfo->portalToOpposingPortalDirection);
      }

      m_numFramesSinceTeleportationWasDetected = 0;
      Logger::info("[RTX] Portal teleportation was detected");

      return true;
    }

    return false;
  }

  // Checks if an input camera matrix correlates with any of the registered portals
  // Returns true if a match is found
  // portalIndex: output portal index for the corresponding camera if the call succeeded
  bool RayPortalManager::tryMatchCameraToPortal(const CameraManager& cameraManager, const Matrix4& worldToView, uint8_t& portalIndex) const {

    portalIndex = maxRayPortalCount;

    if (!cameraManager.isCameraValid(CameraType::Main)) {
      Logger::err("[RTX] RayPortalManger::registerPortalCamera - tried to register a portal camera, but the main camera has not been set prior.");
      return false;
    }

    // Find a matching portal to the input camera matrix
    {
      const Matrix4 viewToWorld = inverse(worldToView);
      Vector3 camDir = -viewToWorld[2].xyz();
      Vector3 camPos = viewToWorld[3].xyz();

      const auto& mainCam = cameraManager.getMainCamera();
      const Vector3& mainCamPos = mainCam.getViewToWorld()[3].xyz();
      Vector3 mainCamDir = -mainCam.getViewToWorld()[2].xyz();

      // Rough tolerance accounting for any floating point error
      const float kCamPosDistanceTolerance = 0.001f * (lengthSqr(camPos) + lengthSqr(mainCamPos));

      static_assert(maxRayPortalCount == 2);

      // Process all portal pairs
      for (auto& portalPair : m_rayPortalPairInfos) {
        if (portalPair.has_value()) {
          for (uint i = 0; i < 2; i++) {

            const auto& portalInfo = portalPair->pairInfos[i].entryPortalInfo;

            auto isInFrontOfPortal = [&](const Vector3& p, const RayPortalInfo& portal) {
              const Vector3 portalToP = p - portal.centroid;
              return dot(portalToP, portal.planeNormal) >= 0;
            };

            // Check 1: main camera must be in front of the entry portal
            if (!isInFrontOfPortal(mainCamPos, portalInfo))
              continue;

            // Calculate main camera's view position and direction in exiting portal coord system
            Vector3 virtualMainCamDir = Matrix3(portalPair->pairInfos[i].portalToOpposingPortalDirectionWithoutRayOffset) * mainCamDir;
            Vector3 virtualMainCamPos = getVirtualPosition(mainCamPos, portalPair->pairInfos[i].portalToOpposingPortalDirectionWithoutRayOffset);

            // Check 2: check if current camera matches virtual main camera through a given entry portal
            const float kCamViewDotTolerance = 0.001f;
            float virtualMainCamDirDotCamDir = dot(virtualMainCamDir, camDir);
            
            float virtualCamPosToCamPosDistanceSq = lengthSqr(virtualMainCamPos - camPos);
            
            bool cameraMatchesPortal =
              (virtualMainCamDirDotCamDir >= (1 - kCamViewDotTolerance)) &&
              virtualCamPosToCamPosDistanceSq <= kCamPosDistanceTolerance;

            if (cameraMatchesPortal) {
              // Portal corresponding to the input camera found
              portalIndex = portalInfo.portalIndex;
              return true;
            }
          }
        }
      }

      return false;
    }
  }

  void RayPortalManager::createVirtualCameras(CameraManager& cameraManager) const {
    ScopedCpuProfileZone();
    if (!cameraManager.isCameraValid(CameraType::Main))
      return;

    const RtCamera& mainCamera = cameraManager.getMainCamera();

    // Note: we only support one portal pair here. Adding more pairs would require adding more CameraType members
    // and adjusting volume integration and sampling code (see volume_lighting.slangh)
    static_assert(maxRayPortalCount == 2);

    for (auto& portalPair : m_rayPortalPairInfos) {
      if (portalPair.has_value()) {
        // Iterate over portals in the pair.
        for (int portalIndex = 0; portalIndex < 2; ++portalIndex) {
          const Matrix4 portalViewMatrix = mainCamera.getWorldToView() * portalPair->pairInfos[!portalIndex].portalToOpposingPortalDirection;
          
          RtCamera& portalCamera = cameraManager.getCamera((CameraType::Enum) (CameraType::Portal0 + portalIndex));
          portalCamera.update(m_device->getCurrentFrameId(), portalViewMatrix, mainCamera.getViewToProjection(),
                              mainCamera.getFov(), mainCamera.getAspectRatio(), mainCamera.getNearPlane(), mainCamera.getFarPlane(), mainCamera.isLHS());
        }

        return;
      }
    }
  }
  
  bool RayPortalManager::areAnyRayPortalPairsActive() const {
    for (auto& portalPair : m_rayPortalPairInfos)
      if (portalPair.has_value())
        return true;

      return false;
  }

  float RayPortalManager::calculateDistanceAlongNormalToPortal(const Vector3& p, const RayPortalInfo& portal) {
    const Vector3 portalToP = p - portal.centroid;
    return dot(portalToP, portal.planeNormal);
  };

  bool RayPortalManager::isInFrontOfPortal(const Vector3& p, const RayPortalInfo& portal, const float distanceThreshold) {
    const Vector3 portalToP = p - portal.centroid;
    return dot(portalToP, portal.planeNormal) >= distanceThreshold;
  };

  Vector3 RayPortalManager::getVirtualPosition(const Vector3& p, const Matrix4& portalToOpposingPortal) {
    return (portalToOpposingPortal * Vector4(p.x, p.y, p.z, 1.f)).xyz();
  };

}  // namespace dxvk
