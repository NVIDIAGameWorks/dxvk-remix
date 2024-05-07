/*
* Copyright (c) 2023, NVIDIA CORPORATION. All rights reserved.
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

#include "rtx_game_capturer.h"
#include "rtx_game_capturer_paths.h"

#include "rtx_context.h"
#include "rtx_types.h"
#include "rtx_camera.h"
#include "rtx_options.h"
#include "rtx_utils.h"
#include "rtx_instance_manager.h"
#include "rtx_scene_manager.h"
#include "rtx_materials.h"

#include "../dxvk_device.h"

#include "../../util/log/log.h"
#include "../../util/config/config.h"
#include "../../util/util_vector.h"
#include "../../util/util_window.h"

#include "../../lssusd/game_exporter.h"
#include "../../lssusd/game_exporter_paths.h"
#include "../../lssusd/game_exporter_types.h"
#include "../../lssusd/usd_common.h"
#include "../../lssusd/usd_include_begin.h"
#include <pxr/base/gf/rotation.h>
#include "../../lssusd/usd_include_end.h"

#include "rtx_matrix_helpers.h"
#include "rtx_lights.h"

#include <filesystem>

#define BASE_DIR (std::string(GameCapturer::s_baseDir))

namespace dxvk {
  const std::string GameCapturer::s_baseDir = []() {
    std::string pathStr = env::getEnvVar("DXVK_CAPTURE_PATH");
    if (!pathStr.empty()) {
      if(*pathStr.rbegin() != '/') {
        pathStr += '/';
      }
    } else {
      pathStr = relPath::remixCaptureDir;
    }
    {
      using namespace std::filesystem;
      const path wholePath = path(pathStr);
      path ctorPath(".");
      for (const auto& part : wholePath) {
        ctorPath /= part;
        ctorPath = absolute(ctorPath);
        env::createDirectory(ctorPath.string());
      }
    }
    return pathStr;
  }();

  namespace {
    static inline pxr::GfMatrix4d matrix4ToGfMatrix4d(const Matrix4& mat4) {
      const auto& float4x4 = reinterpret_cast<const float(&)[4][4]>(mat4);
      return pxr::GfMatrix4d{pxr::GfMatrix4f(float4x4)};
    }
    static inline pxr::VtMatrix4dArray matrix4VecToGfMatrix4dVec(const std::vector<Matrix4>& mat4s) {
      pxr::VtMatrix4dArray result(mat4s.size());
      for (int i = 0; i < mat4s.size(); ++i) {
        const auto& float4x4 = reinterpret_cast<const float(&)[4][4]>(mat4s[i]);
        result[i] = pxr::GfMatrix4d { pxr::GfMatrix4f(float4x4) };
      }
      return result;
    }
    static inline std::string buildStagePath(const std::string& stageName) {
      std::stringstream stagePathSS;
      stagePathSS << BASE_DIR << "/" << stageName;
      const auto stagePath =
        std::filesystem::absolute(std::filesystem::path(stagePathSS.str()));
      return stagePath.string();
    }
    static lss::RenderingMetaData createDrawCallMetadata(const RtInstance& rtInstance) {
      lss::RenderingMetaData meta;
      meta.alphaTestEnabled = rtInstance.surface.alphaState.alphaTestType != AlphaTestType::kAlways;
      meta.alphaTestReferenceValue = rtInstance.surface.alphaState.alphaTestReferenceValue;
      meta.alphaTestCompareOp = (uint32_t) rtInstance.surface.alphaState.alphaTestType;
      meta.alphaBlendEnabled = !rtInstance.surface.alphaState.isBlendingDisabled;
      meta.srcColorBlendFactor = (uint32_t) rtInstance.surface.srcColorBlendFactor;
      meta.dstColorBlendFactor = (uint32_t) rtInstance.surface.dstColorBlendFactor;
      meta.colorBlendOp = (uint32_t) rtInstance.surface.colorBlendOp;
      meta.textureColorArg1Source = (uint32_t) rtInstance.surface.textureColorArg1Source;
      meta.textureColorArg2Source = (uint32_t) rtInstance.surface.textureColorArg2Source;
      meta.textureColorOperation = (uint32_t) rtInstance.surface.textureColorOperation;
      meta.textureAlphaArg1Source = (uint32_t) rtInstance.surface.textureAlphaArg1Source;
      meta.textureAlphaArg2Source = (uint32_t) rtInstance.surface.textureAlphaArg2Source;
      meta.textureAlphaOperation = (uint32_t) rtInstance.surface.textureAlphaOperation;
      meta.tFactor = rtInstance.surface.tFactor;
      meta.isTextureFactorBlend = rtInstance.surface.isTextureFactorBlend;
      return meta;
    }

    static std::string getBakedSkyProbeName(const std::string& captureName) {
      return captureName + commonFileName::bakedSkyProbeSuffix;
    }
  }

  // For capture tests, we cannot include the config data because it may contain paths/settings which are respective to the users PC.
  static const bool s_captureRemixConfigs = env::getEnvVar("RTX_CAPTURE_REMIX_CONFIGS", true);

  size_t GameCapturer::Capture::nextId = 0;

  GameCapturer::GameCapturer(DxvkDevice* const pDevice,
                             SceneManager& sceneManager,
                             AssetExporter& exporter)
    : m_pDevice(pDevice)
    , m_sceneManager(sceneManager)
    , m_exporter(exporter)
    , m_options{ getOptions() } {
    Logger::info(str::format("[GameCapturer] DXVK_RTX_CAPTURE_ENABLE_ON_FRAME: ", env::getEnvVar("DXVK_RTX_CAPTURE_ENABLE_ON_FRAME")));
    env::createDirectory(BASE_DIR);
    env::createDirectory(BASE_DIR + lss::commonDirName::texDir);
    env::createDirectory(BASE_DIR + lss::commonDirName::matDir);
    lss::GameExporter::setMultiThreadSafety(true);
  }

  GameCapturer::~GameCapturer() {
  }

  void GameCapturer::step(const Rc<DxvkContext> ctx, const float frameTimeSecs, const HWND hwnd) {
    trigger(ctx);
    if(m_state.has<State::Initializing>()) {
      initCapture(ctx, hwnd);
    }
    if (m_state.has<State::Capturing>()) {
      capture(ctx, frameTimeSecs);
    }
    if (m_state.has<State::BeginExport>()) {
      exportUsd(ctx);
    }
  }

  void GameCapturer::setInstanceUpdateFlag(const RtInstance& rtInstance, const InstFlag flag) {
    if (!m_state.has<State::Capturing>()) {
      return;
    }
    m_pCap->instanceFlags[rtInstance.getId()] |= (1 << uint8_t(flag));
  }

  void GameCapturer::trigger(const Rc<DxvkContext> ctx) {
    if (m_bTriggerCapture) {
      m_bTriggerCapture = false;
      if(isIdle()) {
        if (RtxOptions::Get()->getEnableAnyReplacements() && m_sceneManager.areReplacementsLoaded()) {
          Logger::warn("[GameCapturer] Cannot begin capture when replacement assets are enabled/loaded.");
        } else if (m_state.has<State::Capturing>()) {
          Logger::warn("[GameCapturer] Cannot begin new capture, one currently in progress.");
        } else {
          m_state.set<State::Initializing, true>();
        }
      }
    }
  }

  void GameCapturer::initCapture(const Rc<DxvkContext> ctx, const HWND hwnd) {
    assert(m_state.has<State::Initializing>());
    assert(!m_state.has<State::Capturing>());
    
    m_options = getOptions();
    
    m_pCap = std::make_unique<Capture>();
    m_pCap->idStr = hashToString(Capture::nextId++).substr(4, 4);
    m_pCap->bCaptureInstances = m_options.bCaptureInstances;
    m_pCap->bSkyProbeBaked = false;
    if (m_pCap->bCaptureInstances) {
      prepareInstanceStage(ctx);
    }
    Logger::info("[GameCapturer][" + m_pCap->idStr + "] New capture");
    m_pCap->instanceFlags.clear();

    m_pCap->hwnd = hwnd;

    m_state.set<State::Capturing, true>();
    m_state.set<State::Initializing, false>();
    m_state.set<State::Complete, false>();
  }
  
  void GameCapturer::prepareInstanceStage(const Rc<DxvkContext> ctx) {
    const auto stagePathStr = buildStagePath(m_options.instanceStageName);
    m_pCap->instance.stageName = m_options.instanceStageName;
    m_pCap->instance.stagePath = stagePathStr;
    m_exporter.generateSceneThumbnail(ctx, BASE_DIR + lss::commonDirName::thumbDir, m_options.instanceStageName);
  }

  void GameCapturer::capture(const Rc<DxvkContext> ctx, const float dt) {
    assert(m_state.has<State::Capturing>());

    m_pCap->currentFrameNum += dt * static_cast<float>(m_options.fps);
    captureFrame(ctx);

    if (m_pCap->numFramesCaptured >= m_options.numFrames) {
      m_state.set<State::BeginExport, true>();
      m_state.set<State::Capturing, false>();
    }
  }

  void GameCapturer::captureFrame(const Rc<DxvkContext> ctx) {
    Logger::debug("[GameCapturer][" + m_pCap->idStr + "] Begin frame capture");
    if (m_pCap->bCaptureInstances) {
      captureCamera();
      captureLights();
    }
    captureInstances(ctx);
    ++m_pCap->numFramesCaptured;
    Logger::debug("[GameCapturer][" + m_pCap->idStr + "] End frame capture");
  }

  void GameCapturer::captureCamera() {
    auto& sceneCamera = m_sceneManager.getCamera();
    auto& capCamera = m_pCap->camera; // Convenience for captured camera
    if (isnan(capCamera.firstTime)) {
      Logger::debug("[GameCapturer][" + m_pCap->idStr + "][Camera] New");

      // Step 1: Extract easy-to-determine properties from helper function
      float shearX, shearY;
      const auto& viewToProj = sceneCamera.getViewToProjection();
      bool bIsProjLhs = false;
      decomposeProjection(viewToProj,
                          capCamera.aspectRatio,
                          capCamera.fov,
                          capCamera.nearPlane,
                          capCamera.farPlane,
                          shearX,
                          shearY,
                          bIsProjLhs,
                          capCamera.isReverseZ, true);
      // Infinite projection is legit, but USD doesnt take kindly to it
      constexpr float kMaxFarPlane = 100000000;
      capCamera.farPlane = clampHigh(capCamera.farPlane, kMaxFarPlane);

      // Step 2: Handedness, Orientation, Inversion correction
      capCamera.proj.coord = bIsProjLhs ? lss::LHS : lss::RHS;
      capCamera.proj.bInv = (viewToProj[0][0] * viewToProj[1][1] < 0.0f);
      // Use viewToWorld here instead of view matrix b/c it's easier to extract the camera vectors and matches isMirrorTransform
      const auto viewToWorld = sceneCamera.getViewToWorld();
      if (capCamera.proj.bInv) {
        // Check if the up vector in view matrix is upside down
        const auto& up = viewToWorld[1].xyz();
        if ((!RtxOptions::Get()->isZUp() && dot(up, Vector3d(0.0, 1.0, 0.0)) < 0.0) ||
            (RtxOptions::Get()->isZUp() && dot(up, Vector3d(0.0, 0.0, 1.0)) < 0.0)) {
          capCamera.view.bInv = true;
        }
      }
      capCamera.view.coord = isMirrorTransform(viewToWorld) ? lss::LHS : lss::RHS;
      Logger::debug(str::format("Is Camera Left Handed: ", capCamera.isLHS()));
      
      // Set first frame time (should be 0.f)
      capCamera.firstTime = m_pCap->currentFrameNum;
    }
    assert(!isnan(capCamera.fov));
    assert(!isnan(capCamera.aspectRatio));
    assert(!isnan(capCamera.nearPlane));
    assert(!isnan(capCamera.farPlane));
    capCamera.finalTime = m_pCap->currentFrameNum;

    // Step 3: Construct capture Camera
    pxr::GfVec3d position = pxr::GfVec3d(sceneCamera.getPosition().x, sceneCamera.getPosition().y, sceneCamera.getPosition().z);
    pxr::GfVec3d direction = pxr::GfVec3d(sceneCamera.getDirection().x, sceneCamera.getDirection().y, sceneCamera.getDirection().z);
    pxr::GfVec3d up = pxr::GfVec3d(sceneCamera.getUp().x, sceneCamera.getUp().y, sceneCamera.getUp().z);

    // Flip the up vector of camera if the game flips this vector in the view matrix
    up = capCamera.view.bInv ? -up : up;

    // Transform camera to match the world space corrections (flipping or mirror) in GameExporter::exportInstances
    if (capCamera.proj.bInv && !capCamera.view.bInv) {
      position[0] *= -1.0;
      position[1] *= -1.0;
      direction[0] *= -1.0;
      direction[1] *= -1.0;
      up[0] *= -1.0;
    } else {
      if (capCamera.isLHS() && !capCamera.view.bInv) {
        position[0] *= -1.0;
        direction[0] *= -1.0;
      }
    }

    pxr::GfMatrix4d worldToView;
    worldToView.SetLookAt(position, position + direction, up);
    worldToView.Orthonormalize();
    capCamera.xforms.push_back({ m_pCap->currentFrameNum, worldToView.GetInverse() });
  }

  void GameCapturer::captureLights() {
    for (auto&& pair : m_sceneManager.getLightManager().getLightTable()) {
      const RtLight& rtLight = pair.second;
      assert(rtLight.getInitialHash() != 0);
      switch (rtLight.getType()) {
      default:
      case RtLightType::Sphere:
        captureSphereLight(rtLight.getSphereLight());
        break;
      case RtLightType::Rect:
        // Todo: Handle Rect lights
        Logger::err("[GameCapturer][" + m_pCap->idStr + "] RectLight not implemented");
        assert(false);
        break;
      case RtLightType::Disk:
        // Todo: Handle Disk lights
        Logger::err("[GameCapturer][" + m_pCap->idStr + "] DiskLight not implemented");
        assert(false);
        break;
      case RtLightType::Cylinder:
        // Todo: Handle Cylinder lights
        Logger::err("[GameCapturer][" + m_pCap->idStr + "] CylinderLight not implemented");
        assert(false);
        break;
      case RtLightType::Distant:
        captureDistantLight(rtLight.getDistantLight());
        break;
      }
    }
  }

  void GameCapturer::captureSphereLight(const dxvk::RtSphereLight& rtLight) {
    const auto hash = rtLight.getHash();
    pxr::GfRotation  rotation;
    rotation.SetIdentity();
    if (m_pCap->sphereLights.count(hash) == 0) {
      const std::string name = dxvk::hashToString(hash);
      lss::SphereLight& sphereLight = m_pCap->sphereLights[hash];
      sphereLight.lightName = name;
      const auto colorAndIntensity = rtLight.getColorAndIntensity();
      sphereLight.color[0] = colorAndIntensity.r;
      sphereLight.color[1] = colorAndIntensity.g;
      sphereLight.color[2] = colorAndIntensity.b;
      sphereLight.intensity = colorAndIntensity.w;
      sphereLight.radius = rtLight.getRadius();
      sphereLight.xforms.reserve(m_options.numFrames - m_pCap->numFramesCaptured);
      sphereLight.firstTime = m_pCap->currentFrameNum;
      const dxvk::RtLightShaping& shaping = rtLight.getShaping();
      if (shaping.getEnabled()) {
        sphereLight.shapingEnabled = true;
        sphereLight.coneAngleDegrees = std::acos(shaping.getCosConeAngle()) * kRadiansToDegrees;
        sphereLight.coneSoftness = shaping.getConeSoftness();
        sphereLight.focusExponent = shaping.getFocusExponent();
        rotation = pxr::GfRotation(-pxr::GfVec3d::ZAxis(), pxr::GfVec3f(&shaping.getPrimaryAxis()[0]));
      }
      Logger::debug("[GameCapturer][" + m_pCap->idStr + "][SphereLight:" + name + "] New");
    }

    lss::SphereLight& sphereLight = m_pCap->sphereLights[hash];
    const auto position = rtLight.getPosition();
    pxr::GfMatrix4d usdXform(rotation, pxr::GfVec3f(&position[0]));
    sphereLight.xforms.push_back({ m_pCap->currentFrameNum, usdXform });
    sphereLight.finalTime = m_pCap->currentFrameNum;
  }

  void GameCapturer::captureDistantLight(const RtDistantLight& rtLight) {
    const auto hash = rtLight.getHash();
    if (m_pCap->sphereLights.count(hash) == 0) {
      const std::string name = dxvk::hashToString(hash);
      lss::DistantLight& distantLight = m_pCap->distantLights[hash];
      distantLight.lightName = name;
      const auto colorAndIntensity = rtLight.getColorAndIntensity();
      distantLight.color[0] = colorAndIntensity.r;
      distantLight.color[1] = colorAndIntensity.g;
      distantLight.color[2] = colorAndIntensity.b;
      distantLight.intensity = colorAndIntensity.w;
      distantLight.angleDegrees = rtLight.getHalfAngle() * 2.0 * kRadiansToDegrees;
      distantLight.direction = -pxr::GfVec3f(rtLight.getDirection().data);
      distantLight.firstTime = m_pCap->currentFrameNum;
      Logger::debug("[GameCapturer][" + m_pCap->idStr + "][DistantLight:" + name + "] New");
    }
    lss::DistantLight& distantLight = m_pCap->distantLights[hash];
    distantLight.finalTime = m_pCap->currentFrameNum;
  }

  void GameCapturer::captureInstances(const Rc<DxvkContext> ctx) {
    for (const RtInstance* pRtInstance : m_sceneManager.getInstanceTable()) {
      assert(pRtInstance->getBlas() != nullptr);

      const XXH64_hash_t instanceId = pRtInstance->getId();
      if (instanceId == UINT64_MAX) {
        // Ignore "virtual" instances, as they are used primarily for special render
        // passes, rather than representing real entities that we want captured
        continue;
      }
      assert(instanceId != UINT64_MAX);

      if (pRtInstance->getBlas()->input.cameraType == CameraType::Sky) {
        if (!m_pCap->bSkyProbeBaked) {
          const std::string skyProbeFilename = getBakedSkyProbeName(m_pCap->instance.stageName);
          m_exporter.bakeSkyProbe(ctx, BASE_DIR + lss::commonDirName::texDir, skyProbeFilename);
          m_pCap->bSkyProbeBaked = true;
          Logger::debug("[GameCapturer][" + m_pCap->idStr + "][SkyProbe] Bake scheduled to " + skyProbeFilename);
        }
      }

      const uint8_t instanceFlags = m_pCap->instanceFlags[instanceId];
      const bool bIsNew = m_pCap->instances.count(instanceId) == 0;
      const bool bPointsUpdate = checkInstanceUpdateFlag(instanceFlags, InstFlag::PositionsUpdate);
      const bool bNormalsUpdate = checkInstanceUpdateFlag(instanceFlags, InstFlag::NormalsUpdate);
      const bool bIndexUpdate = checkInstanceUpdateFlag(instanceFlags, InstFlag::IndexUpdate);
      const bool bXformUpdate = checkInstanceUpdateFlag(instanceFlags, InstFlag::XformUpdate);
      Instance& instance = m_pCap->instances[instanceId];
      if (bIsNew) {
        newInstance(ctx, *pRtInstance);
      }
      if (m_pCap->bCaptureInstances && !bIsNew && (bPointsUpdate || bNormalsUpdate || bIndexUpdate)) {
        const BlasEntry* pBlas = pRtInstance->getBlas();
        assert(pBlas != nullptr);

        captureMesh(ctx, instance.meshHash, *pBlas, pRtInstance->getCategoryFlags(), false, bPointsUpdate, bNormalsUpdate, bIndexUpdate, pRtInstance->isFrontFaceFlipped);
      }
      if (m_pCap->bCaptureInstances && (bIsNew || bXformUpdate)) {
        pxr::GfMatrix4d xform { 1.0 };
        if (m_pCap->camera.proj.bInv && !m_pCap->camera.view.bInv) {
          // Add flip transformation for games that render world upside down
          xform[0][0] = xform[1][1] = -1.0;
        } else {
          // Add mirror transformation for games that need additional basis changing because view and projection transformation have different handedness.
          // Note1: Don't do this transformation if the view and projection have same handedness, they will automatically cancel out.
          // Note2: Don't do this transformation if the game also flip the up vector in view matrix.
          //        Because it will automatically cancel out the basis changing and we only need to send an identity matrix here.
          if (m_pCap->camera.isLHS() && !m_pCap->camera.view.bInv) {
            xform[0][0] = -1.0;
          }
        }
        instance.lssData.xforms.push_back({ m_pCap->currentFrameNum, matrix4ToGfMatrix4d(pRtInstance->getTransform()) * xform });
        const SkinningData& skinData = pRtInstance->getBlas()->input.getSkinningState();
        if (skinData.numBones > 0) {
          instance.lssData.boneXForms.push_back({ m_pCap->currentFrameNum, matrix4VecToGfMatrix4dVec(skinData.pBoneMatrices) });
        }
      }
      instance.lssData.finalTime = m_pCap->currentFrameNum;
      instance.lssData.isSky = (pRtInstance->getBlas()->input.cameraType == CameraType::Sky);
      instance.lssData.metadata = createDrawCallMetadata(*pRtInstance);
    }
  }

  void GameCapturer::newInstance(const Rc<DxvkContext> ctx, const RtInstance& rtInstance) {
    const BlasEntry* pBlas = rtInstance.getBlas();
    assert(pBlas != nullptr);
    const XXH64_hash_t matHash = rtInstance.getMaterialDataHash();
    const XXH64_hash_t meshHash = pBlas->input.getHash(RtxOptions::Get()->GeometryAssetHashRule);
    assert(meshHash != 0);

    const LegacyMaterialData& material = pBlas->getMaterialData(matHash);

    const bool bIsNewMat = (matHash != 0x0) && (m_pCap->materials.count(matHash) == 0);
    if (bIsNewMat) {
      captureMaterial(ctx, material, !rtInstance.surface.alphaState.isFullyOpaque);
    }

    bool bIsNewMesh = false;
    size_t instanceNum = 0;
    {
      std::lock_guard lock(m_meshMutex);
      bIsNewMesh = m_pCap->meshes.count(meshHash) == 0;
      if (bIsNewMesh) {
        m_pCap->meshes[meshHash] = std::make_shared<Mesh>();
        m_pCap->meshes[meshHash]->instanceCount = 0;
        m_pCap->meshes[meshHash]->matHash = matHash;
      }
      instanceNum = m_pCap->meshes[meshHash]->instanceCount++;
    }
    if (bIsNewMesh) {
      captureMesh(ctx, meshHash, *pBlas, rtInstance.getCategoryFlags(), true, true, true, true, rtInstance.isFrontFaceFlipped);
    }

    const XXH64_hash_t instanceId = rtInstance.getId();
    Instance& instance = m_pCap->instances[instanceId];
    instance.meshHash = meshHash;
    instance.matHash = matHash;
    instance.meshInstNum = instanceNum;
    instance.lssData.firstTime = m_pCap->currentFrameNum;

    Logger::debug("[GameCapturer][" + m_pCap->idStr + "][Inst:" + hashToString(instanceId) + "] New");
  }

  void GameCapturer::captureMaterial(const Rc<DxvkContext> ctx, const LegacyMaterialData& materialData, const bool bEnableOpacity) {
    lss::Material lssMat; // to be populated

    // Resolve material name
    const std::string matName = dxvk::hashToString(materialData.getHash());
    lssMat.matName = matName;
    // Export Textures
    const std::string albedoTexFilename(matName + lss::ext::dds);
    m_exporter.dumpImageToFile(ctx, BASE_DIR + lss::commonDirName::texDir,
                               albedoTexFilename,
                               materialData.getColorTexture().getImageView()->image());
    const std::string albedoTexPath = str::format(BASE_DIR + lss::commonDirName::texDir, albedoTexFilename);
    lssMat.albedoTexPath = albedoTexPath;
    // Opacity
    lssMat.enableOpacity = bEnableOpacity;
    // Collect sampler info
    const auto& samplerCreateInfo = materialData.getSampler()->info();
    lssMat.sampler.addrModeU = samplerCreateInfo.addressModeU;
    lssMat.sampler.addrModeV = samplerCreateInfo.addressModeV;
    lssMat.sampler.filter = samplerCreateInfo.magFilter;
    lssMat.sampler.borderColor = samplerCreateInfo.borderColor;

    // Set populated LSS Material in our cache
    m_pCap->materials[materialData.getHash()].lssData = lssMat;
    Logger::debug("[GameCapturer][" + m_pCap->idStr + "][Mat:" + matName + "] New");
  }

  void GameCapturer::captureMesh(const Rc<DxvkContext> ctx,
                                 const XXH64_hash_t currentMeshHash,
                                 const BlasEntry& blas,
                                 const CategoryFlags& flags,
                                 const bool bIsNewMesh,
                                 const bool bCapturePositions,
                                 const bool bCaptureNormals,
                                 const bool bCaptureIndices,
                                 const bool isLhs) {
    assert((bIsNewMesh && bCapturePositions && bCaptureNormals && bCaptureIndices) || !bIsNewMesh);
    const RaytraceGeometry& geomData = blas.modifiedGeometryData;
    const SkinningData& skinData = blas.input.getSkinningState();
    const RasterGeometry& rasterGeomData = blas.input.getGeometryData();

    // Safely get a handle to the mesh
    std::shared_ptr<Mesh> pMesh;
    {
      std::lock_guard lock(m_meshMutex);
      pMesh = m_pCap->meshes[currentMeshHash];
    }
          
    // Note: Ensures that reading a Vec3 from the position buffer will result in the proper values. This can be extended if
    // games use odd formats like R32G32B32A32 in the future, but cannot be less than 3 components unless the code is modified
    // to accomodate other strange formats.
    assert((geomData.positionBuffer.vertexFormat() == VK_FORMAT_R32G32B32_SFLOAT) ||
           (geomData.positionBuffer.vertexFormat() == VK_FORMAT_R32G32B32A32_SFLOAT));
    const size_t numVertices = geomData.vertexCount;
    assert(numVertices > 0);
    const size_t numIndices = geomData.indexCount;
    const bool isDoubleSided = geomData.cullMode == VK_CULL_MODE_NONE;
    if (bIsNewMesh) {
      assert(pMesh->lssData.buffers.positionBufs.size() == 0);
      assert(pMesh->lssData.buffers.normalBufs.size() == 0);
      assert(pMesh->lssData.buffers.idxBufs.size() == 0);
      assert(pMesh->lssData.buffers.texcoordBufs.size() == 0);
      assert(pMesh->lssData.buffers.colorBufs.size() == 0);
      pMesh->lssData.meshName = dxvk::hashToString(currentMeshHash);
      for (uint32_t i = 0; i < (uint32_t) HashComponents::Count; i++) {
        const HashComponents component = (HashComponents) i;
        pMesh->lssData.componentHashes[getHashComponentName(component)] = geomData.hashes[component];
      }
      for (uint32_t i = 0; i < (uint32_t) InstanceCategories::Count; i++) {
        const InstanceCategories flag = (InstanceCategories) i;
        pMesh->lssData.categoryFlags[getInstanceCategorySubKey(flag)] = flags.test(flag);
      }
      pMesh->lssData.numVertices = numVertices;
      pMesh->lssData.numIndices = numIndices;
      pMesh->lssData.isDoubleSided = isDoubleSided;
      pMesh->lssData.numBones = skinData.numBones;
      pMesh->lssData.bonesPerVertex = skinData.numBonesPerVertex;
      pMesh->lssData.isLhs = isLhs;
      Logger::debug("[GameCapturer][" + m_pCap->idStr + "][Mesh:" + pMesh->lssData.meshName + "] New");
    }

    if (bCapturePositions && geomData.positionBuffer.defined()) {
      if (skinData.numBones > 0) {
        captureMeshPositions(ctx, rasterGeomData.vertexCount, rasterGeomData.positionBuffer, m_pCap->currentFrameNum, pMesh);
      } else {
        captureMeshPositions(ctx, geomData.vertexCount, geomData.positionBuffer, m_pCap->currentFrameNum, pMesh);
      }
    }
    
    if (bCaptureNormals && geomData.normalBuffer.defined()) {
      if (skinData.numBones > 0) {
        captureMeshNormals(ctx, rasterGeomData.vertexCount, rasterGeomData.normalBuffer, m_pCap->currentFrameNum, pMesh);
      } else {
        captureMeshNormals(ctx, geomData.vertexCount, geomData.normalBuffer, m_pCap->currentFrameNum, pMesh);
      }
    }
    
    if (bCaptureIndices && geomData.indexBuffer.defined()) {
      captureMeshIndices(ctx, geomData, m_pCap->currentFrameNum, m_pCap->camera, pMesh);
    }

    if (bIsNewMesh && geomData.texcoordBuffer.defined()) {
      captureMeshTexCoords(ctx, geomData, m_pCap->currentFrameNum, pMesh);
    }

    if (bIsNewMesh && geomData.color0Buffer.defined()) {
      captureMeshColor(ctx, geomData, m_pCap->currentFrameNum, pMesh);
    }

    if (bIsNewMesh && skinData.numBones > 0) {
      captureMeshBlending(ctx, rasterGeomData, m_pCap->currentFrameNum, pMesh);
      pMesh->lssData.boneXForms = matrix4VecToGfMatrix4dVec(skinData.pBoneMatrices);
    }
  }

  template <typename T>
  void GameCapturer::captureMeshPositions(const Rc<DxvkContext> ctx,
                                          const size_t numVertices,
                                          const T& inputPositionBuffer,
                                          const float currentFrameNum,
                                          std::shared_ptr<Mesh> pMesh) {
                                            
    AssetExporter::BufferCallback captureMeshPositionsAsync = [this, ctx, numVertices, inputPositionBuffer, currentFrameNum, pMesh](Rc<DxvkBuffer> posBuf) {
      // Prep helper vars
      constexpr size_t positionSubElementSize = sizeof(float);
      const size_t positionStride = inputPositionBuffer.stride() / positionSubElementSize;
      const DxvkBufferSlice positionBuffer(posBuf, 0, posBuf->info().size);
      // Ensure no reads are out of bounds
      assert(((size_t) (numVertices - 1) * (size_t)inputPositionBuffer.stride() + sizeof(pxr::GfVec3f)) <=
            (positionBuffer.length() - inputPositionBuffer.offsetFromSlice()));
      // Get copied-to-CPU GPU buffer
      const float* pVkPosBuf = (float*) positionBuffer.mapPtr((size_t)inputPositionBuffer.offsetFromSlice());
      assert(pVkPosBuf);
      // Copy GPU buffer to local VtArray
      pxr::VtArray<pxr::GfVec3f> positions;
      positions.reserve(numVertices);
      OriginCalc originCalc;
      for (size_t idx = 0; idx < numVertices; ++idx) {
        pxr::GfVec3f pos = *reinterpret_cast<const pxr::GfVec3f*>(&pVkPosBuf[idx * positionStride]);
        if(m_correctBakedTransforms) {
          originCalc.compareAndSwap(pos);
        }
        positions.push_back(pos);
      }
      if(m_correctBakedTransforms) {
        pMesh->originCalc.compareAndSwap(originCalc);
      }
      assert(positions.size() > 0);
      // Create comparison function that returns float
      static auto positionsDifferentEnough = [](const pxr::GfVec3f& a, const pxr::GfVec3f& b) {
        const static float captureMeshPositionDelta = RtxOptions::Get()->getCaptureMeshPositionDelta();
        const static float captureMeshPositionDeltaSq = captureMeshPositionDelta * captureMeshPositionDelta;
        return (a - b).GetLengthSq() > captureMeshPositionDeltaSq;
      };
      // Cache buffer iff new buffer differs from previous buffer
      evalNewBufferAndCache(pMesh, pMesh->lssData.buffers.positionBufs, positions, currentFrameNum, positionsDifferentEnough);
    };
    pMesh->meshSync.numOutstandingInc();
    m_exporter.copyBufferFromGPU(ctx, inputPositionBuffer, captureMeshPositionsAsync);
  }

  template <typename T>
  void GameCapturer::captureMeshNormals(const Rc<DxvkContext> ctx,
                                        const size_t numVertices,
                                        const T& inputNormalBuffer,
                                        const float currentFrameNum,
                                        std::shared_ptr<Mesh> pMesh) {
                                          
    AssetExporter::BufferCallback captureMeshNormalsAsync = [ctx, numVertices, inputNormalBuffer, currentFrameNum, pMesh](Rc<DxvkBuffer> norBuf) {
      assert(inputNormalBuffer.vertexFormat() == VK_FORMAT_R32G32B32_SFLOAT);
      // Prep helper vars
      constexpr size_t normalSubElementSize = sizeof(float);
      const size_t normalStride = inputNormalBuffer.stride() / normalSubElementSize;
      const DxvkBufferSlice normalBuffer(norBuf, 0, norBuf->info().size );
      // Ensure no reads are out of bounds
      assert(((size_t) (numVertices - 1) * (size_t)inputNormalBuffer.stride() + sizeof(pxr::GfVec3f)) <=
            (normalBuffer.length() - inputNormalBuffer.offsetFromSlice()));
      // Get copied-to-CPU GPU buffer
      const float* pVkNormalBuf = (float*) normalBuffer.mapPtr((size_t)inputNormalBuffer.offsetFromSlice());
      assert(pVkNormalBuf);
      // Copy GPU buffer to local VtArray
      pxr::VtArray<pxr::GfVec3f> normals;
      normals.reserve(numVertices);
      for (size_t idx = 0; idx < numVertices; ++idx) {
        normals.push_back(pxr::GfVec3f(&pVkNormalBuf[idx * normalStride]));
      }
      assert(normals.size() > 0);
      // Create comparison function that returns float
      static auto normalsDifferentEnough = [](const pxr::GfVec3f& a, const pxr::GfVec3f& b) {
        const static float captureMeshNormalDelta = RtxOptions::Get()->getCaptureMeshNormalDelta();
        const static float captureMeshNormalDeltaSq = captureMeshNormalDelta * captureMeshNormalDelta;
        return (a - b).GetLengthSq() > captureMeshNormalDeltaSq;
      };
      // Cache buffer iff new buffer differs from previous buffer
      evalNewBufferAndCache(pMesh, pMesh->lssData.buffers.normalBufs, normals, currentFrameNum, normalsDifferentEnough);
    };
    pMesh->meshSync.numOutstandingInc();
    m_exporter.copyBufferFromGPU(ctx, inputNormalBuffer, captureMeshNormalsAsync);
  }

  template<typename T>
  static void getIndicesFromVK(const size_t numIndices, const DxvkBufferSlice& indexBuffer, pxr::VtArray<int>& indices) {
    // Ensure no reads are out of bounds
    assert((size_t) numIndices * sizeof(T) <= indexBuffer.length());

    // Get copied-to-CPU GPU buffer
    const T* pVkIndexBuf = (T*) indexBuffer.mapPtr(0);
    assert(pVkIndexBuf);
    for (size_t idx = 0; idx < numIndices; ++idx) {
      indices.push_back(pVkIndexBuf[idx]);
    }
  }

  void GameCapturer::captureMeshIndices(const Rc<DxvkContext> ctx,
                                        const RaytraceGeometry& geomData,
                                        const float currentFrameNum,
                                        const lss::Camera& capCam,
                                        std::shared_ptr<Mesh> pMesh) {

    AssetExporter::BufferCallback captureMeshIndicesAsync = [ctx, geomData, currentFrameNum, pMesh, capCam, this](Rc<DxvkBuffer> idxBuf) {
      const size_t numIndices = geomData.indexCount;
      const DxvkBufferSlice indexBuffer(idxBuf, 0, idxBuf->info().size);
      // Copy GPU buffer to local VtArray
      pxr::VtArray<int> indices;
      indices.reserve(numIndices);

      switch (geomData.indexBuffer.indexType()) {
      case VK_INDEX_TYPE_UINT16:
        getIndicesFromVK<uint16_t>(numIndices, indexBuffer, indices);
        break;
      case VK_INDEX_TYPE_UINT32:
        getIndicesFromVK<uint32_t>(numIndices, indexBuffer, indices);
        break;
      default:
        assert(0);
      }
      assert(indices.size() > 0);

      // Need to change winding order in 3 situations:
      // 1. The mesh is LHS
      // 2. View and Projection transformation have different handedness (because we need to add a mirror transformation in this case)
      // 3. We need to flip meshes in world space to correct upside down
      // However, swap twice is the same as original order. So, we do XOR here make sure we only swap the winding order when 1 or 3 (odd number) of these 3 situations happen.
      const bool isChangingHandedness = (capCam.proj.bInv ^ (pMesh->lssData.isLhs ^ capCam.isLHS()));
      if (isChangingHandedness){
        for (uint32_t i = 0; i < numIndices; i+=3) {
          std::swap(indices[i], indices[i + 2]);
        }
      }

      // Create comparison function that returns float
      static auto differentIndices = [](const int a, const int b) {
        return a != b;
      };
      // Cache buffer iff new buffer differs from previous buffer
      evalNewBufferAndCache(pMesh, pMesh->lssData.buffers.idxBufs, indices, currentFrameNum, differentIndices);
    };
    pMesh->meshSync.numOutstandingInc();
    m_exporter.copyBufferFromGPU(ctx, geomData.indexBuffer, captureMeshIndicesAsync);
  }

  void GameCapturer::captureMeshTexCoords(const Rc<DxvkContext> ctx,
                                          const RaytraceGeometry& geomData,
                                          const float currentFrameNum,
                                          std::shared_ptr<Mesh> pMesh) {

    AssetExporter::BufferCallback captureMeshTexCoordsAsync = [ctx, geomData, currentFrameNum, pMesh](Rc<DxvkBuffer> texBuf) {
      assert(geomData.texcoordBuffer.vertexFormat() == VK_FORMAT_R32G32_SFLOAT ||
             geomData.texcoordBuffer.vertexFormat() == VK_FORMAT_R32G32B32_SFLOAT);
      // Prep helper vars
      const size_t numVertices = geomData.vertexCount;
      constexpr size_t texcoordSubElementSize = sizeof(float);
      const size_t texcoordStride = geomData.texcoordBuffer.stride() / texcoordSubElementSize;
      const DxvkBufferSlice texcoordBuffer(texBuf, 0, texBuf->info().size);
      // Ensure no reads are out of bounds
      assert(((size_t) (numVertices - 1) * (size_t) geomData.texcoordBuffer.stride() + sizeof(pxr::GfVec2f)) <=
             (texcoordBuffer.length() - geomData.texcoordBuffer.offsetFromSlice()));
      // Get copied-to-CPU GPU buffer
      const float* pVkTexcoordsBuf = (float*) texcoordBuffer.mapPtr((size_t) geomData.texcoordBuffer.offsetFromSlice());
      assert(pVkTexcoordsBuf);
      // Copy GPU buffer to local VtArray
      pxr::VtArray<pxr::GfVec2f> texcoords;
      texcoords.reserve(numVertices);
      for (size_t idx = 0; idx < numVertices; ++idx) {
        texcoords.push_back(pxr::GfVec2f(pVkTexcoordsBuf[idx * texcoordStride],
                                         1.0f - pVkTexcoordsBuf[idx * texcoordStride + 1]));
      }
      assert(texcoords.size() > 0);
      // Create comparison function that returns float
      static auto differentIndices = [](const pxr::GfVec2f& a, const pxr::GfVec2f& b) {
        const static float captureMeshTexcoordDelta = RtxOptions::Get()->getCaptureMeshTexcoordDelta();
        const static float captureMeshTexcoordDeltaSq = captureMeshTexcoordDelta * captureMeshTexcoordDelta;
        return (a - b).GetLengthSq() > captureMeshTexcoordDeltaSq;
      };
      // Cache buffer iff new buffer differs from previous buffer
      evalNewBufferAndCache(pMesh, pMesh->lssData.buffers.texcoordBufs, texcoords, currentFrameNum, differentIndices);
    };
    pMesh->meshSync.numOutstandingInc();
    m_exporter.copyBufferFromGPU(ctx, geomData.texcoordBuffer, captureMeshTexCoordsAsync);
  }

  void GameCapturer::captureMeshColor(const Rc<DxvkContext> ctx,
                                      const RaytraceGeometry& geomData,
                                      const float currentFrameNum,
                                      std::shared_ptr<Mesh> pMesh) {

    AssetExporter::BufferCallback captureMeshColorAsync = [ctx, geomData, currentFrameNum, pMesh](Rc<DxvkBuffer> colBuf) {
      assert(geomData.color0Buffer.vertexFormat() == VK_FORMAT_B8G8R8A8_UNORM);
      // Prep helper vars
      const size_t numVertices = geomData.vertexCount;
      constexpr size_t colorSubElementSize = sizeof(uint8_t);
      const size_t colorStride = geomData.color0Buffer.stride() / colorSubElementSize;
      const DxvkBufferSlice colorBuffer(colBuf, 0, colBuf->info().size);
      // Ensure no reads are out of bounds
      assert(((size_t) (numVertices - 1) * (size_t) geomData.color0Buffer.stride() + sizeof(uint8_t) * 3) <=
             (colorBuffer.length() - geomData.color0Buffer.offsetFromSlice()));
      // Get copied-to-CPU GPU buffer
      const uint8_t* pVkColorBuf = (uint8_t*) colorBuffer.mapPtr((size_t) geomData.color0Buffer.offsetFromSlice());
      assert(pVkColorBuf);
      // Copy GPU buffer to local VtArray
      pxr::VtArray<pxr::GfVec4f> colors;
      colors.reserve(numVertices);
      for (size_t idx = 0; idx < numVertices; ++idx) {
        colors.push_back(pxr::GfVec4f((float) pVkColorBuf[idx * colorStride + 2] / 255.f,
                                      (float) pVkColorBuf[idx * colorStride + 1] / 255.f,
                                      (float) pVkColorBuf[idx * colorStride + 0] / 255.f,
                                      (float) pVkColorBuf[idx * colorStride + 3] / 255.f));
      }
      assert(colors.size() > 0);
      // Create comparison function that returns float
      static auto colorsDifferentEnough = [](const pxr::GfVec4f& a, const pxr::GfVec4f& b) {
        const static float captureMeshColorDelta = RtxOptions::Get()->getCaptureMeshColorDelta();
        const static float captureMeshColorDeltaSq = captureMeshColorDelta * captureMeshColorDelta;
        return (a - b).GetLengthSq() > captureMeshColorDeltaSq;
      };
      // Cache buffer iff new buffer differs from previous buffer
      evalNewBufferAndCache(pMesh, pMesh->lssData.buffers.colorBufs, colors, currentFrameNum, colorsDifferentEnough);
    };
    pMesh->meshSync.numOutstandingInc();
    m_exporter.copyBufferFromGPU(ctx, geomData.color0Buffer, captureMeshColorAsync);
  }

  void GameCapturer::captureMeshBlending(const Rc<DxvkContext> ctx,
                                         const RasterGeometry& geomData,
                                         const float currentFrameNum,
                                         std::shared_ptr<Mesh> pMesh) {
    AssetExporter::BufferCallback captureMeshBlendWeightsAsync = [ctx, geomData, currentFrameNum, pMesh](Rc<DxvkBuffer> inBuf) {
      // Prep helper vars
      const size_t numVertices = geomData.vertexCount;
      const size_t bonesPerVertex = pMesh->lssData.bonesPerVertex;
      const size_t stride = geomData.blendWeightBuffer.stride() / sizeof(float);
      const DxvkBufferSlice bufferSlice(inBuf, 0, inBuf->info().size);
      const VkFormat format = geomData.blendWeightBuffer.vertexFormat();
      if (bonesPerVertex <= 2) {
        assert(format == VK_FORMAT_R32_SFLOAT || format == VK_FORMAT_R32G32_SFLOAT || format == VK_FORMAT_R32G32B32_SFLOAT);
      } else if (bonesPerVertex == 3) {
        assert(format == VK_FORMAT_R32G32_SFLOAT || format == VK_FORMAT_R32G32B32_SFLOAT);
      } else if (bonesPerVertex == 4) {
        assert(format == VK_FORMAT_R32G32B32_SFLOAT);
      }
      // Ensure no reads are out of bounds
      assert(((size_t) (numVertices - 1) * (size_t) geomData.blendWeightBuffer.stride() + sizeof(float) * bonesPerVertex) <=
             (bufferSlice.length() - geomData.blendWeightBuffer.offsetFromSlice()));
      // Get copied-to-CPU GPU buffer
      const float* pVkBwBuf = (float*) bufferSlice.mapPtr((size_t) geomData.blendWeightBuffer.offsetFromSlice());
      assert(pVkBwBuf);
      // Copy GPU buffer to local VtArray
      pxr::VtArray<float> targetBuffer;
      targetBuffer.reserve(numVertices * bonesPerVertex);
      for (size_t idx = 0; idx < numVertices; ++idx) {
        float lastWeight = 1.0;
        for (size_t bone_idx = 0; bone_idx < bonesPerVertex - 1; ++bone_idx) {
          float thisWeight = pVkBwBuf[idx * stride + bone_idx];
          lastWeight -= thisWeight;
          targetBuffer.push_back(thisWeight);
        }
        // D3D9 only stores bonesPerVertex - 1 weights. The last weight is 1 minus the other weights.
        targetBuffer.push_back(lastWeight);
      }
      assert(targetBuffer.size() > 0);
      // Create comparison function that returns float
      static auto weightsDifferentEnough = [](const float& a, const float& b) {
        const static float delta = RtxOptions::Get()->getCaptureMeshBlendWeightDelta();
        return std::abs(a - b) > delta;
      };
      // Cache buffer iff new buffer differs from previous buffer
      evalNewBufferAndCache(pMesh, pMesh->lssData.buffers.blendWeightBufs, targetBuffer, currentFrameNum, weightsDifferentEnough);
    };
    AssetExporter::BufferCallback captureMeshBlendIndicesAsync = [ctx, geomData, currentFrameNum, pMesh](Rc<DxvkBuffer> inBuf) {
      assert(geomData.blendIndicesBuffer.vertexFormat() == VK_FORMAT_R8G8B8A8_USCALED);
      // Prep helper vars
      const size_t numVertices = geomData.vertexCount;
      const size_t bonesPerVertex = pMesh->lssData.bonesPerVertex;
      const size_t stride = geomData.blendIndicesBuffer.stride() / sizeof(uint8_t);
      const DxvkBufferSlice bufferSlice(inBuf, 0, inBuf->info().size);
      // Ensure no reads are out of bounds
      assert(((size_t) (numVertices - 1) * (size_t) geomData.blendIndicesBuffer.stride() + sizeof(uint8_t) * bonesPerVertex) <=
             (bufferSlice.length() - geomData.blendIndicesBuffer.offsetFromSlice()));
      // Get copied-to-CPU GPU buffer
      const uint8_t* VkBuf = (uint8_t*) bufferSlice.mapPtr((size_t) geomData.blendIndicesBuffer.offsetFromSlice());
      assert(VkBuf);
      // Copy GPU buffer to local VtArray
      pxr::VtArray<int> targetBuffer;
      targetBuffer.reserve(numVertices * bonesPerVertex);
      for (size_t idx = 0; idx < numVertices; ++idx) {
        for (size_t bone_idx = 0; bone_idx < bonesPerVertex; ++bone_idx) {
          targetBuffer.push_back(VkBuf[idx * stride + bone_idx]);
        }
      }
      assert(targetBuffer.size() > 0);
      // Create comparison function that returns float
      static auto weightsDifferentEnough = [](const int& a, const int& b) {
        return a != b;
      };
      // Cache buffer iff new buffer differs from previous buffer
      evalNewBufferAndCache(pMesh, pMesh->lssData.buffers.blendIndicesBufs, targetBuffer, currentFrameNum, weightsDifferentEnough);
    };
    pMesh->meshSync.numOutstandingInc();
    m_exporter.copyBufferFromGPU(ctx, geomData.blendWeightBuffer, captureMeshBlendWeightsAsync);
    if (geomData.blendIndicesBuffer.defined()) {
      pMesh->meshSync.numOutstandingInc();
      m_exporter.copyBufferFromGPU(ctx, geomData.blendIndicesBuffer, captureMeshBlendIndicesAsync);
    }
  }

  template <typename T, typename CompareTReturnBool>
  static void GameCapturer::evalNewBufferAndCache(std::shared_ptr<Mesh> pMesh,
                                                  std::map<float, pxr::VtArray<T>>& bufferCache,
                                                  pxr::VtArray<T>& newBuffer,
                                                  const float currentFrameNum,
                                                  CompareTReturnBool compareT) {
    std::lock_guard lock(pMesh->meshSync.mutex);
    // Discover whether the new buffer is worth cacheing
    bool bSufficientlyDifferent = false;
    if (bufferCache.size() > 0) {
      const auto& prevBuf = (--bufferCache.cend())->second;
      assert(newBuffer.size() == prevBuf.size());
      for (size_t idx = 0; idx < newBuffer.size(); ++idx) {
        const T& newVal = newBuffer[idx];
        const T& prevVal = prevBuf[idx];
        bSufficientlyDifferent = compareT(newVal, prevVal);
        if (bSufficientlyDifferent) {
          // Early out as soon as we've found enough of a difference
          break;
        }
      }
    } else {
      bSufficientlyDifferent = true;
    }
    // Cache VtArray if there is a large enough delta
    if (bSufficientlyDifferent) {
      bufferCache[currentFrameNum] = std::move(newBuffer);
    }
    pMesh->meshSync.numOutstanding--;
    pMesh->meshSync.cond.notify_all();
  }

  void GameCapturer::exportUsd(const Rc<DxvkContext> ctx) {
    assert(m_state.has<State::BeginExport>());
    assert(!m_state.has<State::PreppingExport>());
    assert(!m_state.has<State::Exporting>());
    static auto exportThreadTask = [this](const Rc<DxvkContext> ctx,
                                          std::unique_ptr<Capture> pCap,
                                          State* pState,
                                          CompletedCapture* complete,
                                          const float framesPerSecond) {
      Capture& cap = *pCap;
      const auto numTexExportsInProgress = m_exporter.getNumExportsInFlights();
      constexpr float kTimePerTexExport = 0.0050f; // Liberally decided by inspection, derived from timed out tests
      const float texExportTimeout = numTexExportsInProgress * kTimePerTexExport;
      m_exporter.waitForAllExportsToComplete(texExportTimeout);
      assert(pState->has<State::PreppingExport>());
      const auto exportPrep = prepExport(cap, framesPerSecond);
      pState->set<State::PreppingExport, false>();
      pState->set<State::Exporting, true>();

      Logger::info("[GameCapturer][" + cap.idStr + "] Begin USD export");
      lss::GameExporter::exportUsd(exportPrep);
      Logger::info("[GameCapturer][" + cap.idStr + "] End USD export");

      // Necessary step for being able to properly diff and check for regressions
      const auto flattenCaptureEnvStr = env::getEnvVar("DXVK_CAPTURE_FLATTEN");
      if (!flattenCaptureEnvStr.empty()) {
        flattenExport(exportPrep);
      }

      complete->stageName = cap.instance.stageName;
      complete->stagePath = cap.instance.stagePath;
      pState->set<State::Exporting, false>();
      pState->set<State::Complete, true>();
    };

    m_state.set<State::PreppingExport, true>();
    m_state.set<State::BeginExport, false>();
    std::thread(exportThreadTask,
                ctx,
                std::move(m_pCap),
                &m_state,
                &m_completeCapture,
                static_cast<float>(m_options.fps)).detach();
  }

  lss::Export GameCapturer::prepExport(const Capture& cap,
                                             const float framesPerSecond) {
    lss::Export exportPrep;
    prepExportMetaData(cap, framesPerSecond, exportPrep);
    prepExportMaterials(cap, exportPrep);
    prepExportMeshes(cap, exportPrep);
    if (exportPrep.bExportInstanceStage) {
      prepExportInstances(cap, exportPrep);
      prepExportLights(cap, exportPrep);
      exportPrep.camera = cap.camera;
    }
    // Prep global transform
    exportPrep.globalXform = pxr::GfMatrix4d{1.0};
    const bool bInvX = (!exportPrep.camera.view.bInv) && (exportPrep.camera.proj.bInv || exportPrep.camera.isLHS());
    const bool bInvY = (!exportPrep.camera.view.bInv) && exportPrep.camera.proj.bInv;
    const pxr::GfVec3d scale{ bInvX ? -1.0 : 1.0, bInvY ? -1.0 : 1.0, 1.0 };
    exportPrep.globalXform.SetScale(scale);
    if(exportPrep.meta.bCorrectBakedTransforms) {
      exportPrep.globalXform.SetTranslateOnly(-exportPrep.stageOrigin);
    }
    return exportPrep;
  }

  void GameCapturer::prepExportMetaData(const Capture& cap,
                                        const float framesPerSecond,
                                        lss::Export& exportPrep) {
    // Prep meta data
    exportPrep.meta.windowTitle = window::getWindowTitle(cap.hwnd);
    exportPrep.meta.exeName = env::getExeName();
    exportPrep.meta.iconPath = BASE_DIR + exportPrep.meta.exeName + "_icon.bmp";
    exportPrep.meta.geometryHashRule = RtxOptions::Get()->geometryAssetHashRuleString();
    exportPrep.meta.metersPerUnit = RtxOptions::Get()->getSceneScale();
    exportPrep.meta.timeCodesPerSecond = framesPerSecond;
    exportPrep.meta.startTimeCode = 0.0;
    exportPrep.meta.endTimeCode = floor(static_cast<double>(cap.currentFrameNum));
    exportPrep.meta.numFramesCaptured = cap.numFramesCaptured;
    window::saveWindowIconToFile(exportPrep.meta.iconPath, cap.hwnd);
    exportPrep.meta.bReduceMeshBuffers = true;
    exportPrep.meta.isZUp = RtxOptions::Get()->isZUp();
    if (s_captureRemixConfigs) {
      for (auto& pair : RtxOptionImpl::getGlobalRtxOptionMap()) {
        exportPrep.meta.renderingSettingsDict[pair.first] = pair.second->genericValueToString(RtxOptionImpl::ValueType::Value);
      }
    }
    exportPrep.meta.bCorrectBakedTransforms = false;

    exportPrep.debugId = cap.idStr;
    exportPrep.baseExportPath = BASE_DIR;
    exportPrep.bExportInstanceStage = cap.bCaptureInstances;
    exportPrep.instanceStagePath = cap.instance.stagePath;
    exportPrep.bakedSkyProbePath = cap.bSkyProbeBaked ? (BASE_DIR + lss::commonDirName::texDir + getBakedSkyProbeName(cap.instance.stageName)) : "";
  }

  void GameCapturer::prepExportMaterials(const Capture& cap,
                                         lss::Export& exportPrep) {
    for (auto& [hash, material] : cap.materials) {
      exportPrep.materials[hash] = material.lssData;
    }
  }

  void GameCapturer::prepExportMeshes(const Capture& cap, lss::Export& exportPrep) {
    OriginCalc stageOriginCalc;
    for (auto& [hash, pMesh] : cap.meshes) {
      std::unique_lock lock(pMesh->meshSync.mutex);
      pMesh->meshSync.cond.wait(lock,
        [pNumOutstanding = &pMesh->meshSync.numOutstanding] { return *pNumOutstanding == 0; });
      if (pMesh->lssData.numIndices == 0 && pMesh->lssData.numVertices == 0) {
        continue;
      }
      if (cap.materials.count(pMesh->matHash) > 0) {
        pMesh->lssData.matId = pMesh->matHash;
      }
      if(m_correctBakedTransforms) {
        pMesh->lssData.origin = pMesh->originCalc.calc();
        stageOriginCalc.compareAndSwap(pMesh->lssData.origin);
      }
      exportPrep.meshes[hash] = pMesh->lssData;
    }
    if(m_correctBakedTransforms) {
      exportPrep.stageOrigin = stageOriginCalc.calc();
    }
  }

  void GameCapturer::prepExportInstances(const Capture& cap, lss::Export& exportPrep) {
    for (auto& [hash, instance] : cap.instances) {
      if (instance.meshHash == 0) {
        continue;
      }
      auto& exportInstance = exportPrep.instances[hash];
      exportInstance = instance.lssData;

      assert(cap.meshes.count(instance.meshHash) > 0);
      exportInstance.meshId = instance.meshHash;

      const auto pMesh = cap.meshes.at(instance.meshHash);
      if (cap.materials.count(pMesh->matHash) > 0) {
        exportInstance.matId = instance.matHash;
      }
      exportInstance.instanceName = pMesh->lssData.meshName + "_" + std::to_string(instance.meshInstNum);
    }
  }

  void GameCapturer::prepExportLights(const Capture& cap,
                                      lss::Export& exportPrep) {
    // sphere lights
    for (auto& [hash, sphereLight] : cap.sphereLights) {
      exportPrep.sphereLights.emplace(hash, sphereLight);
    }
    // distant lights
    for (auto& [hash, distantLight] : cap.distantLights) {
      exportPrep.distantLights.emplace(hash, distantLight);
    }
  }

  void GameCapturer::flattenExport(const lss::Export& exportPrep) {
    Logger::info("[GameCapturer][" + exportPrep.debugId + "] Flattening USD capture.");
    const auto pStage = pxr::UsdStage::Open(exportPrep.instanceStagePath);
    assert(pStage);
    const auto flattenedStagePath = buildStagePath("flattened.usd");
    const auto pFlattenedStage = pStage->Export(flattenedStagePath, true);
    assert(pFlattenedStage);
    Logger::info("[GameCapturer][" + exportPrep.debugId + "] USD capture flattened.");
  }
}
