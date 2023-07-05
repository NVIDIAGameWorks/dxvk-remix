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
#pragma once

#include "usd_include_begin.h"
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/attribute.h>
#include "usd_include_end.h"

#include "game_exporter_common.h"
#include "game_exporter_types.h"
#include "game_exporter_paths.h"

#include <mutex>

namespace lss {

class GameExporter
{
public:
  static void setMultiThreadSafety(const bool enable) {
    s_bMultiThreadSafety = enable;
  }
  static std::string buildInstanceStageName(const std::string& baseDir, const std::string& name) {
    std::stringstream stagePathSS;
    stagePathSS << baseDir << "/";
    if(name.empty()) {
      stagePathSS << "export" << lss::ext::usd;
    } else {
      stagePathSS << name;
    }
    return stagePathSS.str();
  }
  static bool loadUsdPlugins(const std::string& path);
  static void exportUsd(const Export& exportData);
private:
  struct Reference {
    std::string  stagePath;
    pxr::SdfPath ogSdfPath;
    pxr::SdfPath instanceSdfPath;
  };
  struct ExportContext {
    pxr::UsdStageRefPtr instanceStage;
    IdMap<Reference> matReferences;
    IdMap<Reference> meshReferences;
    IdMap<Skeleton> skeletons;
    
  };
  static void exportUsdInternal(const Export& exportData);
  static pxr::UsdStageRefPtr createInstanceStage(const Export& exportData);
  static void setCommonStageMetaData(pxr::UsdStageRefPtr stage, const Export& exportData);
  static void createApertureMdls(const std::string& baseExportPath);
  static void exportMaterials(const Export& exportData, ExportContext& ctx);
  static void exportMeshes(const Export& exportData, ExportContext& ctx);
  static void exportSkeletons(const Export& exportData, ExportContext& ctx);
  struct ReducedIdxBufSet {
    // Per-timecode reduced bufset
    std::map<float,IndexBuffer> bufSet;
    // Per-timecode offset from original index buffer, where offset == min(original)
    //   e.g. original={1,2,4,5}, reduced={{0,1,3,4}, offset=1}
    std::map<float,int> idxOffsets;
  };
  static ReducedIdxBufSet reduceIdxBufferSet(const std::map<float,IndexBuffer>& idxBufSet);
  template<typename T>
  static std::map<float,pxr::VtArray<T>> reduceBufferSet(const std::map<float,pxr::VtArray<T>>& bufSet,
                                                         const ReducedIdxBufSet& reducedIdxBufSet,
                                                         size_t elemsPerIdx = 1);
  template<typename T>
  static void exportBufferSet(const std::map<float,pxr::VtArray<T>>& bufSet,
                              pxr::UsdAttribute attr);
  static void exportInstances(const Export& exportData, ExportContext& ctx);
  static void exportCamera(const Export& exportData, ExportContext& ctx);
  static void exportSphereLights(const Export& exportData, ExportContext& ctx);
  static void exportDistantLights(const Export& exportData, ExportContext& ctx);
  static void exportSky(const Export& exportData, ExportContext& ctx);
  template<bool IsInstance>
  static void setTimeSampledXforms(const pxr::UsdStageRefPtr stage,
                                   const pxr::SdfPath sdfPath,
                                   const float firstTime,
                                   const float finalTime,
                                   const SampledXforms& xforms,
                                   const Export::Meta& meta,
                                   const bool teleportAway = false);
  static void setVisibilityTimeSpan(const pxr::UsdStageRefPtr stage,
                                    const pxr::SdfPath sdfPath,
                                    const double firstTime,
                                    const double finalTime,
                                    const size_t numFramesCaptured);
  static void setLightIntensityOnTimeSpan(const pxr::UsdStageRefPtr stage,
                                          const pxr::SdfPath sdfPath,
                                          const float defaultLightIntensity,
                                          const double firstTime,
                                          const double finalTime,
                                          const size_t numFramesCaptured);
  
  static pxr::UsdStageRefPtr findOpenOrCreateStage(const std::string path, const bool bClearIfExists = false);

  static bool s_bMultiThreadSafety;
  static std::mutex s_mutex;
};

}