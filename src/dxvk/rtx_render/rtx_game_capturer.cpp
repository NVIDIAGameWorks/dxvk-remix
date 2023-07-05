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
#include "../../lssusd/game_exporter.h"
#include "../../lssusd/game_exporter_paths.h"
#include "../../util/log/log.h"
#include "../../util/config/config.h"
#include "../../util/util_window.h"

#include "../../lssusd/usd_include_begin.h"
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/gf/rotation.h>
#include "../../lssusd/usd_include_end.h"

#include "rtx_matrix_helpers.h"
#include "rtx_lights.h"

#define BASE_DIR (std::string(s_baseDir))

namespace dxvk {
  const std::string GameCapturer::s_baseDir = []() {
    std::string path = env::getEnvVar("DXVK_CAPTURE_PATH");
    if (!path.empty() && *path.rbegin() != '/') {
      path += '/';
    }
    return path;
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
  }

  size_t GameCapturer::Capture::nextId = 0;

  GameCapturer::GameCapturer(SceneManager& sceneManager, AssetExporter& exporter)
    : m_sceneManager(sceneManager)
    , m_exporter(exporter)
    , m_maxFramesCapturable(RtxOptions::Get()->getCaptureMaxFrames())
    , m_framesPerSecond(RtxOptions::Get()->getCaptureFramesPerSecond())
    , m_bUseLssUsdPlugins(lss::GameExporter::loadUsdPlugins("./lss/usd_plugins/"))
    , m_keyBindStartSingle { VirtualKey{VK_CONTROL},VirtualKey{VK_SHIFT},VirtualKey{'Q'} }
    , m_keyBindToggleMulti { VirtualKey{VK_CONTROL},VirtualKey{VK_SHIFT},VirtualKey{'M'} } {
    Logger::info(str::format("[GameCapturer] DXVK_RTX_CAPTURE_ENABLE_ON_FRAME: ", env::getEnvVar("DXVK_RTX_CAPTURE_ENABLE_ON_FRAME")));
    env::createDirectory(BASE_DIR + relPath::rtxRemixDir);
    env::createDirectory(BASE_DIR + relPath::remixCaptureDir);
    lss::GameExporter::setMultiThreadSafety(true);
    if (m_bUseLssUsdPlugins) {
      Logger::debug("[GameCapturer] LSS USD Plugins successfully found and loaded.");
    } else {
      Logger::warn("[GameCapturer] LSS USD Plugins failed to load.");
    }
  }

  GameCapturer::~GameCapturer() {
  }

  void GameCapturer::step(const Rc<DxvkContext> ctx, const float frameTimeSecs) {
    hotkeyStep(ctx);
    if (getState(StateFlag::InitCapture)) {
      initCaptureStep(ctx);
    }
    if (getState(StateFlag::Capturing)) {
      captureStep(ctx, frameTimeSecs);
    }
    exportStep();
  }

  void GameCapturer::setInstanceUpdateFlag(const RtInstance& rtInstance, const InstFlag flag) {
    if (isIdle()) {
      return;
    }
    m_cap.instanceFlags[rtInstance.getId()] |= (1 << uint8_t(flag));
  }

  void GameCapturer::hotkeyStep(const Rc<DxvkContext> ctx) {
    const bool bStartSingle = ImGUI::checkHotkeyState(m_keyBindStartSingle);
    const bool bToggleMulti = ImGUI::checkHotkeyState(m_keyBindToggleMulti);
    if (bStartSingle || bToggleMulti) {
      if (RtxOptions::Get()->getEnableAnyReplacements() && m_sceneManager.areReplacementsLoaded()) {
        Logger::warn("[GameCapturer] Cannot begin capture when replacement assets are enabled/loaded.");
      } else if (getState(StateFlag::Capturing)) {
        Logger::warn("[GameCapturer] Cannot begin new capture, one currently in progress.");
      } else {
        if (bStartSingle) {
          setState(StateFlag::InitCaptureSingle, true);
        } else if (bToggleMulti) {
          if (getState(StateFlag::CapturingMulti)) {
            setState(StateFlag::BeginExport, true);
            setState(StateFlag::CapturingMulti, false);
          } else {
            setState(StateFlag::InitCaptureMulti, true);
          }
        }
        // TODO: Remove "else" from `else if` above if we want single frame concurrent with ongoing multiframe
      }
    }
  }

  void GameCapturer::initCaptureStep(const Rc<DxvkContext> ctx) {
    assert(getState(StateFlag::InitCapture));
    assert(!getState(StateFlag::Capturing));

    m_cap.idStr = hashToString(Capture::nextId++).substr(4, 4);
    m_cap.bExportInstances = !RtxOptions::Get()->getCaptureNoInstance();
    m_cap.bSkyProbeBaked = false;
    if (m_cap.bExportInstances) {
      std::stringstream stagePathSS;
      if (RtxOptions::Get()->getCaptureInstanceStageName().compare("") != 0) {
        stagePathSS << RtxOptions::Get()->getCaptureInstanceStageName() << lss::ext::usd;
      } else {
        const std::time_t curTime = std::time(nullptr);
        std::tm locTime;
        // The vanilla versions of localtime are not thread safe, see:
        // https://en.cppreference.com/w/cpp/chrono/c/localtime
        localtime_s(&locTime, &curTime);
        static const std::string kDefaultExportFilePrefix("capture_");
        stagePathSS << kDefaultExportFilePrefix << std::put_time(&locTime, "%Y-%m-%d_%H-%M-%S") << lss::ext::usd;
      }
      m_cap.instanceStageName = stagePathSS.str();
      m_exporter.generateSceneThumbnail(ctx, BASE_DIR + relPath::remixCaptureThumbnailsDir, m_cap.instanceStageName);
    }
    Logger::info("[GameCapturer][" + m_cap.idStr + "] New capture");
    m_cap.instanceFlags.clear();

    if (getState(StateFlag::InitCaptureSingle)) {
      setState(StateFlag::CapturingSingle, true);
    }
    if (getState(StateFlag::InitCaptureMulti)) {
      setState(StateFlag::CapturingMulti, true);
    }
    setState(StateFlag::InitCapture, false);
  }

  void GameCapturer::captureStep(const Rc<DxvkContext> ctx, const float dt) {
    assert(getState(StateFlag::Capturing));

    m_cap.currentFrameNum += dt * static_cast<float>(m_framesPerSecond);
    captureFrame(ctx);

    if (getState(StateFlag::CapturingSingle) || m_cap.numFramesCaptured >= m_maxFramesCapturable) {
      setState(StateFlag::BeginExport, true);
      setState(StateFlag::Capturing, false);
    }
  }

  void GameCapturer::captureFrame(const Rc<DxvkContext> ctx) {
    Logger::debug("[GameCapturer][" + m_cap.idStr + "] Begin frame capture");
    if (m_cap.bExportInstances) {
      captureCamera();
      captureLights();
    }
    captureInstances(ctx);
    ++m_cap.numFramesCaptured;
    Logger::debug("[GameCapturer][" + m_cap.idStr + "] End frame capture");
  }

  void GameCapturer::captureCamera() {
    if (isnan(m_cap.camera.fov) ||
       isnan(m_cap.camera.aspectRatio) ||
       isnan(m_cap.camera.nearPlane) ||
       isnan(m_cap.camera.farPlane)) {
      Logger::debug("[GameCapturer][" + m_cap.idStr + "][Camera] New");
      float shearX, shearY;
      decomposeProjection(m_sceneManager.getCamera().getViewToProjection(),
                          m_cap.camera.aspectRatio,
                          m_cap.camera.fov,
                          m_cap.camera.nearPlane,
                          m_cap.camera.farPlane,
                          shearX,
                          shearY,
                          m_cap.camera.isLHS,
                          m_cap.camera.isReverseZ);
      // Infinite projection is legit, but USD doesnt take kindly to it
      if (isinf(m_cap.camera.farPlane)) {
        m_cap.camera.farPlane = 100000000;
      }
      if(m_cap.camera.aspectRatio < 0) {
        m_cap.camera.aspectRatio = abs(m_cap.camera.aspectRatio);
        m_cap.camera.bFlipVertAperture = true;
      }
      m_cap.camera.firstTime = m_cap.currentFrameNum;
    }
    assert(!isnan(m_cap.camera.fov));
    assert(!isnan(m_cap.camera.aspectRatio));
    assert(!isnan(m_cap.camera.nearPlane));
    assert(!isnan(m_cap.camera.farPlane));
    const Matrix4 xform = m_sceneManager.getCamera().getViewToWorld();
    m_cap.camera.finalTime = m_cap.currentFrameNum;
    m_cap.camera.xforms.push_back({ m_cap.currentFrameNum, matrix4ToGfMatrix4d(xform) });
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
        Logger::err("[GameCapturer][" + m_cap.idStr + "] RectLight not implemented");
        assert(false);
        break;
      case RtLightType::Disk:
        // Todo: Handle Disk lights
        Logger::err("[GameCapturer][" + m_cap.idStr + "] DiskLight not implemented");
        assert(false);
        break;
      case RtLightType::Cylinder:
        // Todo: Handle Cylinder lights
        Logger::err("[GameCapturer][" + m_cap.idStr + "] CylinderLight not implemented");
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
    if (m_cap.sphereLights.count(hash) == 0) {
      const std::string name = dxvk::hashToString(hash);
      lss::SphereLight& sphereLight = m_cap.sphereLights[hash];
      sphereLight.lightName = name;
      const auto colorAndIntensity = rtLight.getColorAndIntensity();
      sphereLight.color[0] = colorAndIntensity.r;
      sphereLight.color[1] = colorAndIntensity.g;
      sphereLight.color[2] = colorAndIntensity.b;
      sphereLight.intensity = colorAndIntensity.w;
      sphereLight.radius = rtLight.getRadius();
      sphereLight.xforms.reserve(m_maxFramesCapturable - m_cap.numFramesCaptured);
      sphereLight.firstTime = m_cap.currentFrameNum;
      const dxvk::RtLightShaping& shaping = rtLight.getShaping();
      if (shaping.enabled) {
        sphereLight.shapingEnabled = true;
        sphereLight.coneAngle = acos(shaping.cosConeAngle) * kRadiansToDegrees;
        sphereLight.coneSoftness = shaping.coneSoftness;
        sphereLight.focusExponent = shaping.focusExponent;
        rotation = pxr::GfRotation(-pxr::GfVec3d::ZAxis(), pxr::GfVec3f(&shaping.primaryAxis[0]));
      }
      Logger::debug("[GameCapturer][" + m_cap.idStr + "][SphereLight:" + name + "] New");
    }

    lss::SphereLight& sphereLight = m_cap.sphereLights[hash];
    const auto position = rtLight.getPosition();
    pxr::GfMatrix4d usdXform(rotation, pxr::GfVec3f(&position[0]));
    sphereLight.xforms.push_back({ m_cap.currentFrameNum, usdXform });
    sphereLight.finalTime = m_cap.currentFrameNum;
  }

  void GameCapturer::captureDistantLight(const RtDistantLight& rtLight) {
    const auto hash = rtLight.getHash();
    if (m_cap.sphereLights.count(hash) == 0) {
      const std::string name = dxvk::hashToString(hash);
      lss::DistantLight& distantLight = m_cap.distantLights[hash];
      distantLight.lightName = name;
      const auto colorAndIntensity = rtLight.getColorAndIntensity();
      distantLight.color[0] = colorAndIntensity.r;
      distantLight.color[1] = colorAndIntensity.g;
      distantLight.color[2] = colorAndIntensity.b;
      distantLight.intensity = colorAndIntensity.w;
      distantLight.angle = rtLight.getHalfAngle() * 2.0;
      distantLight.direction = pxr::GfVec3f(rtLight.getDirection().data);
      distantLight.firstTime = m_cap.currentFrameNum;
      Logger::debug("[GameCapturer][" + m_cap.idStr + "][DistantLight:" + name + "] New");
    }
    lss::DistantLight& distantLight = m_cap.distantLights[hash];
    distantLight.finalTime = m_cap.currentFrameNum;
  }

  void GameCapturer::captureInstances(const Rc<DxvkContext> ctx) {
    for (const RtInstance* rtInstancePtr : m_sceneManager.getInstanceTable()) {
      assert(rtInstancePtr->getBlas() != nullptr);

      if (rtInstancePtr->getBlas()->input.isSky) {
        if (!m_cap.bSkyProbeBaked) {
          m_exporter.bakeSkyProbe(ctx, BASE_DIR + relPath::remixCaptureTexturesDir, commonFileName::bakedSkyProbe);
          m_cap.bSkyProbeBaked = true;
          Logger::debug("[GameCapturer][" + m_cap.idStr + "][SkyProbe] Bake scheduled to " +
                        commonFileName::bakedSkyProbe);
        }
      }

      const XXH64_hash_t instanceId = rtInstancePtr->getId();
      const uint8_t instanceFlags = m_cap.instanceFlags[instanceId];
      const bool bIsNew = m_cap.instances.count(instanceId) == 0;
      const bool bPointsUpdate = checkInstanceUpdateFlag(instanceFlags, InstFlag::PositionsUpdate);
      const bool bNormalsUpdate = checkInstanceUpdateFlag(instanceFlags, InstFlag::NormalsUpdate);
      const bool bIndexUpdate = checkInstanceUpdateFlag(instanceFlags, InstFlag::IndexUpdate);
      const bool bXformUpdate = checkInstanceUpdateFlag(instanceFlags, InstFlag::XformUpdate);
      Instance& instance = m_cap.instances[instanceId];
      if (bIsNew) {
        newInstance(ctx, *rtInstancePtr);
      }
      if (m_cap.bExportInstances && !bIsNew && (bPointsUpdate || bNormalsUpdate || bIndexUpdate)) {
        const BlasEntry* pBlas = rtInstancePtr->getBlas();
        assert(pBlas != nullptr);
        captureMesh(ctx, instance.meshHash, *pBlas, false, bPointsUpdate, bNormalsUpdate, bIndexUpdate);
      }
      if (m_cap.bExportInstances && (bIsNew || bXformUpdate)) {
        instance.lssData.xforms.push_back({ m_cap.currentFrameNum, matrix4ToGfMatrix4d(rtInstancePtr->getTransform()) });
        const SkinningData& skinData = rtInstancePtr->getBlas()->input.getSkinningState();
        if (skinData.numBones > 0) {
          instance.lssData.boneXForms.push_back({ m_cap.currentFrameNum, matrix4VecToGfMatrix4dVec(skinData.pBoneMatrices) });
        }
      }
      instance.lssData.finalTime = m_cap.currentFrameNum;
      instance.lssData.isSky = rtInstancePtr->getBlas()->input.isSky;
    }
  }

  void GameCapturer::newInstance(const Rc<DxvkContext> ctx, const RtInstance& rtInstance) {
    const BlasEntry* pBlas = rtInstance.getBlas();
    assert(pBlas != nullptr);
    const XXH64_hash_t matHash = rtInstance.getMaterialDataHash();
    const XXH64_hash_t meshHash = pBlas->input.getHash(RtxOptions::Get()->GeometryAssetHashRule);
    assert(meshHash != 0);

    const bool bIsNewMat = (matHash != 0x0) && (m_cap.materials.count(matHash) == 0);
    if (bIsNewMat) {
      captureMaterial(ctx, pBlas->getMaterialData(matHash), !rtInstance.surface.alphaState.isFullyOpaque);
    }

    bool bIsNewMesh = false;
    size_t instanceNum = 0;
    {
      std::lock_guard lock(m_meshMutex);
      bIsNewMesh = m_cap.meshes.count(meshHash) == 0;
      if (bIsNewMesh) {
        m_cap.meshes[meshHash] = std::make_shared<Mesh>();
        m_cap.meshes[meshHash]->instanceCount = 0;
        m_cap.meshes[meshHash]->matHash = matHash;
      }
      instanceNum = m_cap.meshes[meshHash]->instanceCount++;
    }
    if (bIsNewMesh) {
      captureMesh(ctx, meshHash, *pBlas, true, true, true, true);
    }

    const XXH64_hash_t instanceId = rtInstance.getId();
    Instance& instance = m_cap.instances[instanceId];
    instance.meshHash = meshHash;
    instance.matHash = matHash;
    instance.meshInstNum = instanceNum;
    instance.lssData.firstTime = m_cap.currentFrameNum;
    instance.lssData.xforms.reserve(m_maxFramesCapturable - m_cap.numFramesCaptured);
    Logger::debug("[GameCapturer][" + m_cap.idStr + "][Inst:" + hashToString(instanceId) + "] New");
  }

  void GameCapturer::captureMaterial(const Rc<DxvkContext> ctx, const LegacyMaterialData& materialData, const bool bEnableOpacity) {
    const std::string matName = dxvk::hashToString(materialData.getHash());

    //Export Textures
    const std::string albedoTexFilename(matName + lss::ext::dds);
    m_exporter.dumpImageToFile(ctx, BASE_DIR + relPath::remixCaptureTexturesDir,
                         albedoTexFilename,
                         materialData.getColorTexture().getImageView()->image());

    const std::string albedoTexPath = str::format(BASE_DIR + relPath::remixCaptureTexturesDir, albedoTexFilename);

    // Export Material
    lss::Material lssMat;
    lssMat.matName = matName;
    lssMat.albedoTexPath = albedoTexPath;
    lssMat.enableOpacity = bEnableOpacity;
    m_cap.materials[materialData.getHash()].lssData = lssMat;
    Logger::debug("[GameCapturer][" + m_cap.idStr + "][Mat:" + matName + "] New");
  }

  void GameCapturer::captureMesh(const Rc<DxvkContext> ctx,
                                 const XXH64_hash_t currentMeshHash,
                                 const BlasEntry& blas,
                                 const bool bIsNewMesh,
                                 const bool bCapturePositions,
                                 const bool bCaptureNormals,
                                 const bool bCaptureIndices) {
    assert((bIsNewMesh && bCapturePositions && bCaptureNormals && bCaptureIndices) || !bIsNewMesh);
    const RaytraceGeometry& geomData = blas.modifiedGeometryData;
    const SkinningData& skinData = blas.input.getSkinningState();
    const RasterGeometry& rasterGeomData = blas.input.getGeometryData();

    // Safely get a handle to the mesh
    std::shared_ptr<Mesh> pMesh;
    {
      std::lock_guard lock(m_meshMutex);
      pMesh = m_cap.meshes[currentMeshHash];
    }
          
    // Note: Ensures that reading a Vec3 from the position buffer will result in the proper values. This can be extended if
    // games use odd formats like R32G32B32A32 in the future, but cannot be less than 3 components unless the code is modified
    // to accomodate other strange formats.
    assert((geomData.positionBuffer.vertexFormat() == VK_FORMAT_R32G32B32_SFLOAT) ||
           (geomData.positionBuffer.vertexFormat() == VK_FORMAT_R32G32B32A32_SFLOAT));
    const size_t numVertices = geomData.vertexCount;
    assert(numVertices > 0);
    const size_t numIndices = geomData.indexCount;
    const bool isDoubleSided = geomData.cullMode == VK_CULL_MODE_FRONT_AND_BACK;
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
      pMesh->lssData.numVertices = numVertices;
      pMesh->lssData.numIndices = numIndices;
      pMesh->lssData.isDoubleSided = isDoubleSided;
      pMesh->lssData.numBones = skinData.numBones;
      pMesh->lssData.bonesPerVertex = skinData.numBonesPerVertex;
      Logger::debug("[GameCapturer][" + m_cap.idStr + "][Mesh:" + pMesh->lssData.meshName + "] New");
    }

    if (bCapturePositions && geomData.positionBuffer.defined()) {
      if (skinData.numBones > 0) {
        captureMeshPositions(ctx, rasterGeomData.vertexCount, rasterGeomData.positionBuffer, m_cap.currentFrameNum, pMesh);
      } else {
        captureMeshPositions(ctx, geomData.vertexCount, geomData.positionBuffer, m_cap.currentFrameNum, pMesh);
      }
    }
    
    if (bCaptureNormals && geomData.normalBuffer.defined()) {
      if (skinData.numBones > 0) {
        captureMeshNormals(ctx, rasterGeomData.vertexCount, rasterGeomData.normalBuffer, m_cap.currentFrameNum, pMesh);
      } else {
        captureMeshNormals(ctx, geomData.vertexCount, geomData.normalBuffer, m_cap.currentFrameNum, pMesh);
      }
    }
    
    if (bCaptureIndices && geomData.indexBuffer.defined()) {
      captureMeshIndices(ctx, geomData, m_cap.currentFrameNum, pMesh);
    }

    if (bIsNewMesh && geomData.texcoordBuffer.defined()) {
      captureMeshTexCoords(ctx, geomData, m_cap.currentFrameNum, pMesh);
    }

    if (bIsNewMesh && geomData.color0Buffer.defined()) {
      captureMeshColor(ctx, geomData, m_cap.currentFrameNum, pMesh);
    }

    if (bIsNewMesh && skinData.numBones > 0) {
      captureMeshBlending(ctx, rasterGeomData, m_cap.currentFrameNum, pMesh);
      pMesh->lssData.boneXForms = matrix4VecToGfMatrix4dVec(skinData.pBoneMatrices);
    }
  }

  template <typename T>
  void GameCapturer::captureMeshPositions(const Rc<DxvkContext> ctx,
                                          const size_t numVertices,
                                          const T& inputPositionBuffer,
                                          const float currentFrameNum,
                                          std::shared_ptr<Mesh> pMesh) {
                                            
    AssetExporter::BufferCallback captureMeshPositionsAsync = [ctx, numVertices, inputPositionBuffer, currentFrameNum, pMesh](Rc<DxvkBuffer> posBuf) {
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
      for (size_t idx = 0; idx < numVertices; ++idx) {
        positions.push_back(pxr::GfVec3f(&pVkPosBuf[idx * positionStride]));
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
                                        std::shared_ptr<Mesh> pMesh) {

    AssetExporter::BufferCallback captureMeshIndicesAsync = [ctx, geomData, currentFrameNum, pMesh](Rc<DxvkBuffer> idxBuf) {
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
      pxr::VtArray<pxr::GfVec3f> colors;
      colors.reserve(numVertices);
      for (size_t idx = 0; idx < numVertices; ++idx) {
        colors.push_back(pxr::GfVec3f((float) pVkColorBuf[idx * colorStride + 2] / 256.f,
                                      (float) pVkColorBuf[idx * colorStride + 1] / 256.f,
                                      (float) pVkColorBuf[idx * colorStride + 0] / 256.f));
      }
      assert(colors.size() > 0);
      // Create comparison function that returns float
      static auto colorsDifferentEnough = [](const pxr::GfVec3f& a, const pxr::GfVec3f& b) {
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
        return abs(a - b) > delta;
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

  void GameCapturer::exportStep() {
    if (getState(StateFlag::BeginExport)) {
      static auto exportThreadTask = [](Capture cap,
                                        std::atomic<size_t>* pNumOutstandingExportThreads,
                                        const float framesPerSecond,
                                        const bool bUseLssUsdPlugins) {
        const auto exportPrep = prepExport(cap, framesPerSecond, bUseLssUsdPlugins);

        Logger::info("[GameCapturer][" + cap.idStr + "] Begin USD export");
        lss::GameExporter::exportUsd(exportPrep);
        Logger::info("[GameCapturer][" + cap.idStr + "] End USD export");

        // Necessary step for being able to properly diff and check for regressions
        const auto flattenCaptureEnvStr = env::getEnvVar("DXVK_CAPTURE_FLATTEN");
        if (!flattenCaptureEnvStr.empty()) {
          flattenExport(exportPrep);
        }

        (*pNumOutstandingExportThreads)--;
      };
      std::thread(exportThreadTask,
                  std::move(m_cap),
                  &m_numOutstandingExportThreads,
                  m_framesPerSecond,
                  m_bUseLssUsdPlugins).detach();
      m_numOutstandingExportThreads++;

      m_cap = Capture(); // reset to default
      setState(StateFlag::Exporting, true);
      setState(StateFlag::BeginExport, false);
    }

    if (m_numOutstandingExportThreads.load() == 0) {
      setState(StateFlag::Exporting, false);
    }
  }

  lss::Export GameCapturer::prepExport(const Capture& cap,
                                             const float framesPerSecond,
                                             const bool bUseLssUsdPlugins) {
    lss::Export exportPrep;
    prepExportMetaData(cap, framesPerSecond, bUseLssUsdPlugins, exportPrep);
    prepExportMaterials(cap, exportPrep);
    prepExportMeshes(cap, exportPrep);
    if (exportPrep.bExportInstanceStage) {
      prepExportInstances(cap, exportPrep);
      prepExportLights(cap, exportPrep);
      exportPrep.camera = cap.camera;
    }
    return exportPrep;
  }

  void GameCapturer::prepExportMetaData(const Capture& cap,
                                        const float framesPerSecond,
                                        const bool bUseLssUsdPlugins,
                                        lss::Export& exportPrep) {
    // Prep meta data
    exportPrep.meta.windowTitle = window::getWindowTitle();
    exportPrep.meta.exeName = env::getExeName();
    exportPrep.meta.iconPath = BASE_DIR + relPath::remixCaptureDir + exportPrep.meta.exeName + "_icon.bmp";
    exportPrep.meta.geometryHashRule = RtxOptions::Get()->geometryAssetHashRuleString();
    exportPrep.meta.metersPerUnit = RtxOptions::Get()->getSceneScale();
    exportPrep.meta.timeCodesPerSecond = framesPerSecond;
    exportPrep.meta.startTimeCode = 0.0;
    exportPrep.meta.endTimeCode = floor(static_cast<double>(cap.currentFrameNum));
    exportPrep.meta.numFramesCaptured = cap.numFramesCaptured;
    window::saveWindowIconToFile(exportPrep.meta.iconPath);
    exportPrep.meta.bUseLssUsdPlugins = bUseLssUsdPlugins;
    exportPrep.meta.bReduceMeshBuffers = true;
    exportPrep.meta.isZUp = RtxOptions::Get()->isZUp();
    exportPrep.meta.isLHS = RtxOptions::Get()->isLHS();
    exportPrep.debugId = cap.idStr;
    exportPrep.baseExportPath = BASE_DIR + relPath::remixCaptureDir;
    exportPrep.bExportInstanceStage = cap.bExportInstances;
    exportPrep.instanceExportName = cap.instanceStageName;
    exportPrep.bakedSkyProbePath = cap.bSkyProbeBaked ? BASE_DIR + relPath::remixCaptureBakedSkyProbePath : "";
  }

  void GameCapturer::prepExportMaterials(const Capture& cap,
                                         lss::Export& exportPrep) {
    for (auto& [hash, material] : cap.materials) {
      exportPrep.materials[hash] = material.lssData;
    }
  }

  void GameCapturer::prepExportMeshes(const Capture& cap,
                                      lss::Export& exportPrep) {
    for (auto& [hash, pMesh] : cap.meshes) {
      std::unique_lock lock(pMesh->meshSync.mutex);
      pMesh->meshSync.cond.wait(lock,
        [pNumOutstanding = &pMesh->meshSync.numOutstanding] { return *pNumOutstanding == 0; });
      auto& exportMesh = exportPrep.meshes[hash];
      if (pMesh->lssData.numIndices == 0 && pMesh->lssData.numVertices == 0) {
        continue;
      }
      if (cap.materials.count(pMesh->matHash) > 0) {
        pMesh->lssData.matId = pMesh->matHash;
      }
      exportMesh = pMesh->lssData;
    }
  }

  void GameCapturer::prepExportInstances(const Capture& cap,
                                         lss::Export& exportPrep) {
    for (auto& [hash, instance] : cap.instances) {
      if (instance.meshHash == 0) {
        continue;
      }
      auto& exportInstance =
        exportPrep.instances.emplace(hash, instance.lssData).first->second;
      assert(cap.meshes.count(instance.meshHash) > 0);
      const auto pMesh = cap.meshes.at(instance.meshHash);
      exportInstance.instanceName = pMesh->lssData.meshName + "_" + std::to_string(instance.meshInstNum);
      if (cap.materials.count(pMesh->matHash) > 0) {
        exportInstance.matId = instance.matHash;
      }
      exportInstance.meshId = instance.meshHash;
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
    const auto instanceStageName = lss::GameExporter::buildInstanceStageName(
      exportPrep.baseExportPath, exportPrep.instanceExportName);
    const auto pStage = pxr::UsdStage::Open(instanceStageName);
    assert(pStage);
    const auto flattenedStageName = lss::GameExporter::buildInstanceStageName(
      BASE_DIR + relPath::remixCaptureDir, "flattened.usd");
    const auto pFlattenedStage = pStage->Export(flattenedStageName, true);
    assert(pFlattenedStage);
    Logger::info("[GameCapturer][" + exportPrep.debugId + "] USD capture flattened.");
  }

}
