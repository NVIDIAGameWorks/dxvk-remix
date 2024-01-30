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
#include <pxr/usd/usdLux/lightapi.h>
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
  static bool loadUsdPlugins(const std::string& path);
  static void exportUsd(const Export& exportData);
private:
  struct Reference {
    std::string  stagePath;
    pxr::SdfPath ogSdfPath;
    pxr::SdfPath instanceSdfPath;
  };
  struct ExportContext {
    std::string extension;
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
  struct ReducedIdxBufSet {
    BufSet<Index> bufSet;
    // Per-timecode idx mapping
    using IdxMap = std::unordered_map<int,int>;
    std::map<float,IdxMap> redToOgSet;
  };
  static ReducedIdxBufSet reduceIdxBufferSet(const BufSet<Index>& idxBufSet);
  template<typename T>
  static BufSet<T> reduceBufferSet(const BufSet<T>& bufSet,
                                   const ReducedIdxBufSet& reducedIdxBufSet,
                                   size_t elemsPerIdx = 1);
  template<typename BufferT>
  static void exportBufferSet(const BufSet<BufferT>& bufSet, pxr::UsdAttribute attr);
  static void exportColorOpacityBufferSet(const BufSet<Color>& bufSet,
                                          pxr::UsdAttribute color,
                                          pxr::UsdAttribute opacity);
  static void exportSkeletons(const Export& exportData, ExportContext& ctx);
  static void exportInstances(const Export& exportData, ExportContext& ctx);
  static void exportCamera(const Export& exportData, ExportContext& ctx);
  static void exportSphereLights(const Export& exportData, ExportContext& ctx);
  static void exportDistantLights(const Export& exportData, ExportContext& ctx);
  static void exportSky(const Export& exportData, ExportContext& ctx);
  static void setTimeSampledXforms(const pxr::UsdStageRefPtr stage,
                                   const pxr::SdfPath sdfPath,
                                   const float firstTime,
                                   const float finalTime,
                                   const SampledXforms& xforms,
                                   const Export::Meta& meta,
                                   const bool changeBasis,
                                   const pxr::GfMatrix4d& commonXform = pxr::GfMatrix4d { 1.0 });
  static void setVisibilityTimeSpan(const pxr::UsdStageRefPtr stage,
                                    const pxr::SdfPath sdfPath,
                                    const double firstTime,
                                    const double finalTime,
                                    const size_t numFramesCaptured);
  static void setLightIntensityOnTimeSpan(const pxr::UsdLuxLightAPI& luxLight,
                                          const float defaultLightIntensity,
                                          const double firstTime,
                                          const double finalTime,
                                          const size_t numFramesCaptured);
  static void setCameraTimeSampledXforms(const pxr::UsdStageRefPtr stage,
                                         const pxr::SdfPath sdfPath,
                                         const float firstTime,
                                         const float finalTime,
                                         const SampledXform& cameraXform,
                                         const Export::Meta& meta,
                                         const pxr::GfMatrix4d& commonXform);
  
  static pxr::UsdStageRefPtr findOpenOrCreateStage(const std::string path, const bool bClearIfExists = false);

  static bool s_bMultiThreadSafety;
  static std::mutex s_mutex;
};

}