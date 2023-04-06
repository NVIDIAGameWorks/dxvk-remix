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
#pragma once

#include "../../lssusd/game_exporter_paths.h"

#include <string>
#include <vector>

namespace dxvk {
    
namespace commonFileName {
  static const std::string mod("mod");
  static const std::string bakedSkyProbe("T_SkyProbe_NonReplaceable" + lss::ext::dds);
}
    
namespace relPath {
  static const std::string rtxRemixDir("./rtx-remix/");
  static const std::string modsDir("./mods/");
  static const std::string captureDir("./captures/");

  static const std::string remixCaptureDir(rtxRemixDir + captureDir);
  static const std::string remixCaptureThumbnailsDir(remixCaptureDir + lss::commonDirName::thumbDir);
  static const std::string remixCaptureTexturesDir(remixCaptureDir + lss::commonDirName::texDir);
  static const std::string remixCaptureMaterialsDir(remixCaptureDir + lss::commonDirName::matDir);
  static const std::string remixCaptureBakedSkyProbePath(remixCaptureTexturesDir + commonFileName::bakedSkyProbe);
}
}