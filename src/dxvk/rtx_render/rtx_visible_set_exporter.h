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
#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "rtx_objectpicking.h"
#include "rtx_option.h"
#include "rtx_visible_set_serialize.h"

struct HWND__;

namespace dxvk {

  class CameraManager;
  class RtInstance;
  struct AssetReplacer;

  // Emits every live instance, or those a request filters for, as one log line when an out-of-process consumer asks.
  // Nothing is gathered until a request arrives. See documentation/AgenticRemix.md.
  class VisibleSetExporter {
  public:
    VisibleSetExporter() = default;
    ~VisibleSetExporter();

    // SceneManager::onFrameEnd calls this on the render thread at the end of each ray-traced frame.
    // Drains pending requests, and emits a snapshot a few frames after one arrived, once the surface-index image is read back.
    // surfaceInstances maps this frame's surface indices to instances, as AccelManager::getOrderedInstances does.
    // deviceFrameId is the frame that the readback reports.
    void onFrameEnd(CameraManager& cameras, const std::vector<RtInstance*>& instances,
                    const std::vector<RtInstance*>& surfaceInstances, AssetReplacer& replacer,
                    ObjectPicking& picking, uint32_t deviceFrameId);

  private:
    RTX_OPTION_ENV("rtx.visibleSetDump", bool, enable, false, "RTX_VISIBLE_SET_DUMP_ENABLE",
                   "Enables the visible-set dump for Agentic Remix. On request from an out-of-process consumer, such as the Remix Toolkit, every live instance is described, with whether it is in the camera's view, where it is drawn and which instance is under the screen center. A request can ask for only what the camera can see, and for the image that says which instance each pixel shows. The answer is written to a file next to the log, and one log line names it. Requests arrive as a Windows message, and nothing is gathered or read back until one arrives. While enabled, a bounding box is computed for every draw so that culling has bounds. See documentation/AgenticRemix.md.");

    RTX_OPTION_ARGS("rtx.visibleSetDump", uint32_t, detailLevel, 1,
                    "How much of each instance the visible-set dump writes, for requests that do not set their own, and how much rtx.dumpAllInstancesOnFrame prints. 0: Summary, the ids, original hashes, replacement, and whether the instance is hidden or traced. 1: Standard, which adds transforms, material and geometry facts, flags, categories and cameras. 2: Full, which adds the renderer's own bookkeeping, such as TLAS instance fields, bindless slots, surface indices and billboards, and makes the answer almost twice as large.",
                    args.minValue = 0,
                    args.maxValue = kMaxVisibleSetDetailLevel);

    RTX_OPTION("rtx.visibleSetDump", uint32_t, maxInstances, 512,
               "Maximum number of instances in one visible-set snapshot. The cap keeps drawn instances first, then those in the camera's frustum, then the rest, each nearest first, and totalInstances reports how many were listed before it. 0 means no limit.");

    // The RTX_OPTION macros re-open public access, so private access is restored for what follows.
  private:
    // Shares one surface-index image readback with the readback callbacks, which can outlive the exporter.
    struct SurfaceIndexReadback {
      dxvk::mutex mutex;
      // Holds the device frame the image was copied from, once it is copied.
      std::optional<uint32_t> copiedFrame;
      bool done = false;
      // Is set when the exporter stops waiting for the readback, so a late callback does not save the image.
      std::atomic<bool> abandoned = false;
      // The fields below are set only when the image was read back.
      bool hasImage = false;
      uint32_t width = 0;
      uint32_t height = 0;
      uint32_t centerSurfaceIndex = 0;
      // Holds the coverage of each surface index, reduced on the worker thread.
      std::unordered_map<uint32_t, DrawnCoverage> coverage;
      // Holds the path of the file the image was saved to, when the request asked for it and it was written.
      std::string imagePath;
    };

    // Holds a request that waits for its surface-index image readback.
    struct PendingAnswer {
      VisibleSetRequest request;
      uint32_t startDeviceFrame = 0;
      std::shared_ptr<SurfaceIndexReadback> readback;
      // Is gathered on the frame the image was copied from, because the readback arrives after those instances may be gone.
      std::optional<VisibleSetSnapshot> snapshot;
      // Maps each surface index of that frame to an entry of the snapshot's instances.
      // Holds -1 when the surface's instance has no record.
      std::vector<int32_t> surfaceToRecord;
    };

    // Describes every live instance with its frame data, in no particular order. emit() filters and sorts them.
    // Fills pSurfaceToRecord when it is not null.
    VisibleSetSnapshot gather(CameraManager& cameras, const std::vector<RtInstance*>& instances,
                              const std::vector<RtInstance*>& surfaceInstances, AssetReplacer& replacer,
                              const VisibleSetRequest& request, std::vector<int32_t>* pSurfaceToRecord) const;
    // Fills the snapshot's center hit and drawn fields from the readback and the records of the frame it was copied from.
    static void resolveReadback(PendingAnswer& pending);
    // Applies the request's filters and the cap, sorts the instances nearest first, writes the snapshot to its file
    // and logs the line that names the file.
    void emit(const VisibleSetRequest& request, VisibleSetSnapshot snapshot) const;
    // Asks for the surface-index image a request needs, or answers it at once when there is nothing to read back.
    void start(CameraManager& cameras, const std::vector<RtInstance*>& instances,
               const std::vector<RtInstance*>& surfaceInstances, AssetReplacer& replacer,
               ObjectPicking& picking, const VisibleSetRequest& request, uint32_t deviceFrameId);
    // Drops the pending request, and keeps its readback from saving an image if it is still in flight.
    void abandonPending();

    // Creates the trigger window on first use.
    // Logs a warning once and stays inert when the window cannot be created.
    bool ensureTriggerWindow();
    // Drains queued requests and stores the most recent one in outRequest.
    // Returns false when no request was queued.
    bool consumeTriggerRequest(VisibleSetRequest& outRequest);

    // Is created on the render thread, so requests are delivered to the thread that owns the scene.
    // This holds whether the renderer runs in the game or in NvRemixBridge.exe.
    HWND__* m_triggerWindow = nullptr;
    bool m_triggerWindowFailed = false;
    std::optional<PendingAnswer> m_pending;
    // Holds the latest request that arrived while another was answered.
    std::optional<VisibleSetRequest> m_queued;
  };

}
