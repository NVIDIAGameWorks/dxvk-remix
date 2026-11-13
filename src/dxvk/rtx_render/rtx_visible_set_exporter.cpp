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
#include "rtx_visible_set_exporter.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <unordered_map>

#include <windows.h>

#include "rtx_camera.h"
#include "rtx_camera_manager.h"
#include "rtx_instance_manager.h"
#include "rtx_asset_replacer.h"
#include "rtx_intersection_test.h"
#include "rtx_options.h"
#include "dxvk_scoped_annotation.h"
#include "rtx/pass/common_binding_indices.h"
#include "../../util/util_filesys.h"
#include "../../util/util_string.h"
#include "../../util/log/log.h"

namespace dxvk {

  namespace {
    // Both names are part of the external contract in documentation/AgenticRemix.md.
    const wchar_t* const kTriggerWindowClass = L"RemixVisibleSetDump";
    const wchar_t* const kTriggerMessageName = L"UWM_REMIX_VISIBLE_SET_DUMP";

    UINT triggerMessageId() {
      static const UINT id = ::RegisterWindowMessageW(kTriggerMessageName);
      return id;
    }

    // The class uses DefWindowProc rather than a procedure in this DLL, because the class can outlive the DLL.
    // Requests are read off the queue by consumeTriggerRequest and are never dispatched.
    bool ensureTriggerWindowClass() {
      static const bool registered = []() {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = ::DefWindowProcW;
        wc.hInstance = ::GetModuleHandleW(nullptr);
        wc.lpszClassName = kTriggerWindowClass;
        // A registration by another device instance in this process also counts as success.
        return ::RegisterClassExW(&wc) != 0 || ::GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
      }();
      return registered;
    }

    // Serializes the readback callbacks that save the surface-index image, because they all write one file
    // and each runs on a thread of its own.
    dxvk::mutex s_surfaceImageFileMutex;
  }

  VisibleSetExporter::~VisibleSetExporter() {
    abandonPending();
    if (m_triggerWindow != nullptr) {
      ::DestroyWindow(m_triggerWindow);
      m_triggerWindow = nullptr;
    }
  }

  bool VisibleSetExporter::ensureTriggerWindow() {
    if (m_triggerWindow != nullptr) {
      return true;
    }
    if (m_triggerWindowFailed) {
      return false;
    }
    if (ensureTriggerWindowClass() && triggerMessageId() != 0) {
      // A message-only window is never shown.
      // Other processes can still find it with FindWindowEx(HWND_MESSAGE).
      m_triggerWindow = ::CreateWindowExW(0, kTriggerWindowClass, kTriggerWindowClass, 0,
                                          0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                          ::GetModuleHandleW(nullptr), nullptr);
      if (m_triggerWindow != nullptr) {
        // Registered messages are not on the default UIPI allow-list.
        // Without this call, a consumer at lower integrity than the game has its PostMessage silently dropped.
        // A failure here is not fatal.
        ::ChangeWindowMessageFilterEx(m_triggerWindow, triggerMessageId(), MSGFLT_ALLOW, nullptr);
      }
    }
    if (m_triggerWindow == nullptr) {
      // Without a window no request can arrive, so the failure is latched instead of retried every frame.
      m_triggerWindowFailed = true;
      Logger::warn(str::format("VisibleSetExporter: could not create the request window (error ",
                               ::GetLastError(), "); the visible-set emitter is inert."));
      return false;
    }
    return true;
  }

  bool VisibleSetExporter::consumeTriggerRequest(VisibleSetRequest& outRequest) {
    const UINT msgId = triggerMessageId();
    MSG msg;
    bool found = false;
    // The filter on the message id leaves every other message in the queue.
    // Only the latest request is kept, because each request asks what is on screen now.
    while (::PeekMessageW(&msg, m_triggerWindow, msgId, msgId, PM_REMOVE)) {
      outRequest = decodeVisibleSetRequest(static_cast<uint64_t>(msg.wParam));
      found = true;
    }
    return found;
  }

  VisibleSetSnapshot VisibleSetExporter::gather(CameraManager& cameras, const std::vector<RtInstance*>& instances,
                                                const std::vector<RtInstance*>& surfaceInstances, AssetReplacer& replacer,
                                                const VisibleSetRequest& request,
                                                std::vector<int32_t>* pSurfaceToRecord) const {
    ScopedCpuProfileZone();

    RtCamera& camera = cameras.getMainCamera();
    VisibleSetSnapshot snap;
    snap.frameId = camera.getLastUpdateFrame();
    snap.requestId = request.id;
    snap.inViewOnly = (request.flags & kVisibleSetInViewOnly) != 0;
    for (uint32_t type = 0; type < CameraType::Unknown; ++type) {
      const CameraType::Enum cameraType = static_cast<CameraType::Enum>(type);
      if (cameraType == CameraType::Main || cameras.isCameraValid(cameraType)) {
        snap.cameras.push_back({ kCameraTypeNames[type], cameras.getCamera(cameraType).getPosition(true) });
      }
    }

    const Vector3 camPos = camera.getPosition(true);
    const Matrix4 worldToView = camera.getWorldToViewf(true);
    const Matrix4 worldToProj = camera.getViewToProjectionf() * worldToView;

    // The frustum is built here because camera.getFrustum() is widened for anti-culling,
    // and it is only updated while rtx.antiCulling.object.enable is on, which is off by default.
    const float fovY = camera.getFov();
    // getAspectRatio() is negative for Y-flipped projections (rtx_matrix_helpers.h), which would invert the frustum.
    const float aspect = std::abs(camera.getAspectRatio());
    // getNearPlane() and getFarPlane() describe the projection passed into update(),
    // which differs from the cached viewToProjection when enableNearPlaneOverride is on.
    const auto [nearPlane, farPlane] = camera.calculateNearFarPlanes();
    const bool isLHS = camera.isLHS();
    float4x4 frustumMatrix;
    frustumMatrix.SetupByHalfFovy(fovY * 0.5f, aspect, nearPlane, farPlane, isLHS ? PROJ_LEFT_HANDED : 0);
    RtFrustum viewFrustum;
    viewFrustum.Setup(NDC_OGL, frustumMatrix);
    viewFrustum.calculateFrustumGeometry(nearPlane, farPlane, fovY, aspect, isLHS);

    // Maps each instance to its entry of the snapshot's instances.
    std::unordered_map<const RtInstance*, int32_t> recordOfInstance;

    snap.detailLevel = visibleSetDetailLevel(request.detailLevel, detailLevel());
    const InstanceDetailLevel detail = static_cast<InstanceDetailLevel>(snap.detailLevel);
    snap.instances.reserve(instances.size());
    for (const RtInstance* inst : instances) {
      // A GC-pending instance's BLAS may already be freed, and asJson() reads it.
      if (inst == nullptr || inst->isMarkedForGC()) {
        continue;
      }
      VisibleSetRecord r;
      r.instanceJson = inst->asJson(detail, &replacer, str::kJsonCompact);
      r.drawCallId = inst->surface.objectPickingValue;
      r.traced = inst->isTraced();
      const Matrix4 objectToWorld = inst->getTransform();

      // A GPU-skinned draw's box is its bind pose, which the bones move, so it is widened to cover every bone.
      // The conditions match the ones under which SceneManager dispatches skinning.
      // The bone range is the one boneHash covers, because matrices below minBoneIndex are not the draw's bones.
      AxisAlignedBoundingBox objBox;
      if (const BlasEntry* blas = inst->getBlas()) {
        const DrawCallState& dc = blas->input;
        const SkinningData& skinning = dc.getSkinningState();
        const bool isGpuSkinned = skinning.numBones > skinning.minBoneIndex &&
                                  skinning.numBones <= skinning.pBoneMatrices.size() &&
                                  dc.getGeometryData().numBonesPerVertex > 0 &&
                                  dc.getGeometryData().blendWeightBuffer.defined();
        objBox = isGpuSkinned
          ? skinBoundingBox(dc.getGeometryData().boundingBox, skinning.pBoneMatrices.data() + skinning.minBoneIndex,
                            skinning.numBones - skinning.minBoneIndex)
          : dc.getGeometryData().boundingBox;
      }
      const bool hasObjectAabb = objBox.isValid();

      // Distance is measured from the box center, not from the transform translation.
      // The translation is the authoring pivot, which is the world origin for identity-transformed world geometry.
      Vector3 refPoint;
      if (hasObjectAabb) {
        const Vector3 center = (objBox.minPos + objBox.maxPos) * 0.5f;
        const Vector4 worldCenter = objectToWorld * Vector4(center.x, center.y, center.z, 1.f);
        refPoint = Vector3(worldCenter.x, worldCenter.y, worldCenter.z);
      } else {
        refPoint = inst->getWorldPosition();
      }
      r.distance = length(refPoint - camPos);

      const Matrix4 objectToProj = worldToProj * objectToWorld;
      r.hasScreenRect = hasObjectAabb && projectAabbToNdcRect(objectToProj, objBox.minPos, objBox.maxPos, r.screenRectNdc);

      // The SAT test is used instead of the fast helper, which mixes the components of two transformed corners
      // and so reports some rotated boxes as visible when they are not.
      // A draw without a bounding box may be anywhere, so it counts as in the frustum.
      const Matrix4 objectToView = worldToView * objectToWorld;
      r.inFrustum = !hasObjectAabb ||
        boundingBoxIntersectsFrustumSAT(viewFrustum, nearPlane, isLHS, objBox.minPos,
                                        objBox.maxPos, objectToView, /* isInfFrustum */ false);

      if (pSurfaceToRecord != nullptr) {
        recordOfInstance.emplace(inst, static_cast<int32_t>(snap.instances.size()));
      }
      snap.instances.push_back(std::move(r));
    }

    if (pSurfaceToRecord != nullptr) {
      // A point instancer spans several consecutive surfaces of one instance, so they all map to its record.
      pSurfaceToRecord->assign(surfaceInstances.size(), -1);
      for (size_t i = 0; i < surfaceInstances.size(); ++i) {
        const auto it = recordOfInstance.find(surfaceInstances[i]);
        if (it != recordOfInstance.end()) {
          (*pSurfaceToRecord)[i] = it->second;
          snap.instances[it->second].surfaces.push_back(static_cast<uint32_t>(i));
        }
      }
    }

    return snap;
  }

  void VisibleSetExporter::resolveReadback(PendingAnswer& pending) {
    SurfaceIndexReadback& readback = *pending.readback;
    {
      std::lock_guard lock { readback.mutex };
      if (!readback.hasImage) {
        return;
      }
    }
    // The worker thread is done with the readback once done is set, so the fields below are read without the lock.
    VisibleSetSnapshot& snap = *pending.snapshot;

    snap.hasReadback = true;
    snap.drawnSize[0] = readback.width;
    snap.drawnSize[1] = readback.height;
    snap.surfaceImagePath = readback.imagePath;
    snap.surfaceImageNoHit = SURFACE_INDEX_INVALID;

    // Returns the record of the instance a surface belongs to, or -1 when it has none.
    // A pixel showing no geometry holds SURFACE_INDEX_INVALID, which is past the end of the map.
    const auto recordOfSurface = [&](uint32_t surfaceIndex) {
      return surfaceIndex < pending.surfaceToRecord.size() ? pending.surfaceToRecord[surfaceIndex] : -1;
    };

    // An instance can span several surfaces, so their pixels are added up.
    std::vector<DrawnCoverage> coverageOfRecord(snap.instances.size());
    for (const auto& [surfaceIndex, coverage] : readback.coverage) {
      const int32_t record = recordOfSurface(surfaceIndex);
      if (record >= 0) {
        mergeDrawnCoverage(coverageOfRecord[record], coverage);
      }
    }
    for (size_t i = 0; i < snap.instances.size(); ++i) {
      VisibleSetRecord& r = snap.instances[i];
      const DrawnCoverage& coverage = coverageOfRecord[i];
      r.drawnPixels = coverage.pixels;
      r.drawnJson = drawnJson(coverage, readback.width, readback.height);
    }

    const int32_t centerRecord = recordOfSurface(readback.centerSurfaceIndex);
    if (centerRecord >= 0) {
      snap.hasCenterHit = true;
      snap.centerHit = snap.instances[centerRecord];
    }
  }

  void VisibleSetExporter::emit(const VisibleSetRequest& request, VisibleSetSnapshot snapshot) const {
    snapshot.totalInstances = selectVisibleSetRecords(snapshot.instances, request.flags, maxInstances());

    // The snapshot is written next to the log, to one file that each request replaces.
    // The file is complete before the line that names it is logged.
    const std::string filePath = (util::RtxFileSys::path(util::RtxFileSys::Logs) / "remix-visible-set.json").string();
    std::ofstream file(filePath, std::ios::binary | std::ios::trunc);
    file << serializeVisibleSet(snapshot);
    file.close();
    Logger::info(formatVisibleSetLogLine(request.id, filePath, file.fail() ? "The snapshot file could not be written." : ""));
  }

  void VisibleSetExporter::start(CameraManager& cameras, const std::vector<RtInstance*>& instances,
                                 const std::vector<RtInstance*>& surfaceInstances, AssetReplacer& replacer,
                                 ObjectPicking& picking, const VisibleSetRequest& request, uint32_t deviceFrameId) {
    const uint32_t* finalResolution = cameras.getMainCamera().getFinalResolution();
    if (finalResolution[0] == 0 || finalResolution[1] == 0) {
      // Before the camera has an output size there is no image to read back, so the snapshot is answered at once.
      emit(request, gather(cameras, instances, surfaceInstances, replacer, request, nullptr));
      return;
    }
    PendingAnswer pending;
    pending.request = request;
    pending.startDeviceFrame = deviceFrameId;
    pending.readback = std::make_shared<SurfaceIndexReadback>();
    // The image is saved next to the log, to one file that each request replaces.
    const std::string imagePath = (request.flags & kVisibleSetSaveImage) != 0
      ? (util::RtxFileSys::path(util::RtxFileSys::Logs) / "remix-visible-set-surface-index.bin").string()
      : std::string();
    // The callbacks hold the readback state, because they can run after the exporter is gone.
    picking.requestSurfaceIndexImage(
      [readback = pending.readback](uint32_t copiedFrame) {
        std::lock_guard lock { readback->mutex };
        readback->copiedFrame = copiedFrame;
      },
      [readback = pending.readback, imagePath](
          std::optional<ObjectPicking::SurfaceIndexImage> image) {
        // The image is reduced here, on the worker thread, so the render thread only looks up records.
        const bool hasImage = image.has_value() && image->width != 0 && image->height != 0;
        std::unordered_map<uint32_t, DrawnCoverage> coverage;
        uint32_t centerSurfaceIndex = SURFACE_INDEX_INVALID;
        if (hasImage) {
          coverage = reduceDrawnCoverage(image->values, image->width, image->height, SURFACE_INDEX_INVALID);
          centerSurfaceIndex = image->values[size_t(image->height / 2) * image->width + image->width / 2];
        }
        // The file is complete before the answer that names it is written.
        // A later request starts only after this one is abandoned, so checking under the lock
        // keeps an abandoned readback from replacing or interleaving with a later request's image.
        bool imageSaved = false;
        std::unique_lock fileLock { s_surfaceImageFileMutex };
        if (hasImage && !imagePath.empty() && !readback->abandoned) {
          std::ofstream file(imagePath, std::ios::binary | std::ios::trunc);
          file.write(reinterpret_cast<const char*>(&image->width), sizeof(uint32_t));
          file.write(reinterpret_cast<const char*>(&image->height), sizeof(uint32_t));
          file.write(reinterpret_cast<const char*>(image->values), std::streamsize(sizeof(uint32_t)) * image->width * image->height);
          file.close();
          imageSaved = !file.fail();
        }
        fileLock.unlock();
        std::lock_guard lock { readback->mutex };
        readback->hasImage = hasImage;
        if (hasImage) {
          readback->width = image->width;
          readback->height = image->height;
          readback->centerSurfaceIndex = centerSurfaceIndex;
          readback->imagePath = imageSaved ? imagePath : std::string();
          readback->coverage = std::move(coverage);
        }
        readback->done = true;
      });
    m_pending = std::move(pending);
  }

  void VisibleSetExporter::abandonPending() {
    if (m_pending.has_value()) {
      m_pending->readback->abandoned = true;
      m_pending.reset();
    }
  }

  void VisibleSetExporter::onFrameEnd(CameraManager& cameras, const std::vector<RtInstance*>& instances,
                                      const std::vector<RtInstance*>& surfaceInstances, AssetReplacer& replacer,
                                      ObjectPicking& picking, uint32_t deviceFrameId) {
    // The window is only created while enabled, but once it exists it is drained whether enabled or not.
    // A findable window that nobody drains fills its thread's message queue,
    // and then PostMessage fails for every other component on that thread.
    // Non-ray-traced frames never reach this function, so their requests wait for the next one.
    if (m_triggerWindow == nullptr && (!enable() || !ensureTriggerWindow())) {
      return;
    }
    VisibleSetRequest request;
    if (consumeTriggerRequest(request)) {
      // A request that arrives while another is answered waits for it, and only the latest one waits.
      m_queued = request;
    }
    if (!enable()) {
      // A request that arrives while disabled, or one still waiting when the option is turned off, gets no answer.
      abandonPending();
      m_queued.reset();
      return;
    }

    // This runs on the render thread, which owns the cameras and the instance table that gather() reads.
    if (!m_pending.has_value() && m_queued.has_value()) {
      const VisibleSetRequest next = *m_queued;
      m_queued.reset();
      start(cameras, instances, surfaceInstances, replacer, picking, next, deviceFrameId);
    }
    if (!m_pending.has_value()) {
      return;
    }

    PendingAnswer& pending = *m_pending;
    std::optional<uint32_t> copiedFrame;
    bool done = false;
    {
      std::lock_guard lock { pending.readback->mutex };
      copiedFrame = pending.readback->copiedFrame;
      done = pending.readback->done;
    }
    // The readback normally lands within five frames.
    // A request that takes longer is answered without the center and drawn fields, so a consumer is not left waiting.
    constexpr uint32_t kMaxReadbackFrames = 20;
    const bool timedOut = deviceFrameId - pending.startDeviceFrame > kMaxReadbackFrames;

    if (!pending.snapshot.has_value()) {
      if (copiedFrame == deviceFrameId) {
        // Surface indices are per frame, so the records are built on the frame the image was copied from.
        pending.snapshot = gather(cameras, instances, surfaceInstances, replacer, pending.request, &pending.surfaceToRecord);
      } else if (copiedFrame.has_value() || done || timedOut) {
        // The image was copied on a frame this function did not see, could not be read, or never arrived.
        // The snapshot is answered without it rather than with data resolved against another frame.
        emit(pending.request, gather(cameras, instances, surfaceInstances, replacer, pending.request, nullptr));
        abandonPending();
        return;
      } else {
        return;
      }
    }

    if (done) {
      resolveReadback(pending);
    } else if (!timedOut) {
      return;
    }
    emit(pending.request, std::move(*pending.snapshot));
    abandonPending();
  }

}
