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

#include "rtx/utility/shader_types.h"

static const uint viewDistanceModeNone = 0;
static const uint viewDistanceModeHardCutoff = 1;
static const uint viewDistanceModeCoherentNoise = 2;

static const uint viewDistanceFunctionEuclidean = 0;
static const uint viewDistanceFunctionPlanarEuclidean = 1;

// Note: Ensure 16B alignment
struct ViewDistanceArgs {
  uint16_t distanceMode;
  uint16_t distanceFunction;
  float distanceThresholdOrFadeMin;
  float distanceFadeSpan;
  // Note: Could likely be a float16 if needed for more compactness. Note that this value is per game unit so the
  // scene scale has been accounted for in its calculation.
  float noiseScale;
};

#ifdef __cplusplus
// We're packing these into a constant buffer (see: raytrace_args.h), so need to remain aligned
static_assert((sizeof(ViewDistanceArgs) & 15) == 0);

#include <cassert>

#include "rtx_option.h"

namespace dxvk {

enum class ViewDistanceMode : int {
  None = 0,
  HardCutoff,
  CoherentNoise
};

enum class ViewDistanceFunction : int {
  Euclidean = 0,
  PlanarEuclidean
};

struct ViewDistanceOptions {
  friend class ImGUI;
  friend class RtxOptions;

  RTX_OPTION("rtx.viewDistance", ViewDistanceMode, distanceMode, ViewDistanceMode::None, "The view distance mode, None disables view distance, Hard Cutoff will cut off geometry past a point, and Coherent Noise will feather geometry out using a stable worldspace noise pattern (experimental).");
  RTX_OPTION("rtx.viewDistance", ViewDistanceFunction, distanceFunction, ViewDistanceFunction::Euclidean, "The view distance function, Euclidean is a simple distance from the camera, whereas Planar Euclidean will ignore distance across the world's \"up\" direction.");
  RTX_OPTION_ARGS("rtx.viewDistance", float, distanceThreshold, 500.0f, "The view distance to draw out to based on the result of the view distance function, only used for the Hard Cutoff view distance mode.",
                  args.minValue = 0.0f);
  public: static void distanceFadeMinOnChange(DxvkDevice* device);
  RTX_OPTION_ARGS("rtx.viewDistance", float, distanceFadeMin, 400.0f, "The view distance based on the result of the view distance function to start view distance noise fading at, only used for the Coherent Noise view distance mode.",
                  args.minValue = 0.0f, args.onChangeCallback = &distanceFadeMinOnChange);
  public: static void distanceFadeMaxOnChange(DxvkDevice* device);
  RTX_OPTION_ARGS("rtx.viewDistance", float, distanceFadeMax, 500.0f, "The view distance based on the result of the view distance function to end view distance noise fading at (and effectively draw nothing past this point), only used for the Coherent Noise view distance mode.",
                  args.minValue = 0.0f, args.onChangeCallback = &distanceFadeMaxOnChange);
  RTX_OPTION("rtx.viewDistance", float, noiseScale, 3.0f, "The scale per meter value applied to ther world space position fed into the noise generation function for generating the fade in Coherent Noise view distance mode.");

public:
  static void fillShaderParams(ViewDistanceArgs& args, float meterToWorldUnitScale) {
    const auto cachedDistanceMode = distanceMode();
    const auto cachedDistanceFadeMax = distanceFadeMax();
    const auto cachedDistanceFadeMin = distanceFadeMin();

    args.distanceMode = static_cast<uint16_t>(cachedDistanceMode);
    args.distanceFunction = static_cast<uint16_t>(distanceFunction());

    if (cachedDistanceMode == ViewDistanceMode::HardCutoff) {
      args.distanceThresholdOrFadeMin = distanceThreshold();
    } else if (cachedDistanceMode == ViewDistanceMode::CoherentNoise) {
      // Note: Required for the span to be calculated properly.
      assert(cachedDistanceFadeMax >= cachedDistanceFadeMin);

      args.distanceThresholdOrFadeMin = cachedDistanceFadeMin;
      args.distanceFadeSpan = cachedDistanceFadeMax - cachedDistanceFadeMin;
      // Note: Scale express per game units per meter. This normalizes the size of the noise around the world's scale as well as allows for scaling of it on top of that
      // for adjusting the desired size of the noise.
      args.noiseScale = noiseScale() / meterToWorldUnitScale;
    }
  }
};

} // namespace dxvk

#endif
