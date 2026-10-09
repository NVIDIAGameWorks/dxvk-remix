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

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace dxvk {

  enum class GraphicsPreset : int;
  enum class DLSSProfile : uint32_t;

  // Per-GPU defaults keyed by Vulkan vendor and device ID, stored in dynamic option namespaces.
  // See documentation/RemixConfig.md, "Per-GPU Overrides".
  class GpuOverrides {
  public:
    static constexpr const char* kGraphicsPresetPrefix = "rtx.gpuOverride.graphicsPreset.";
    static constexpr const char* kDlssModePrefix = "rtx.gpuOverride.dlssMode.";
    static constexpr const char* kRayReconstructionPrefix = "rtx.gpuOverride.rayReconstruction.";

    static void registerNamespaces();

    // Uppercase hexadecimal "VVVV_DDDD", at least four digits per ID.
    static std::string formatGpuId(uint32_t vendorId, uint32_t deviceId);
    static bool isCanonicalGpuId(std::string_view gpuId);

    static std::optional<GraphicsPreset> parseGraphicsPreset(std::string_view name);
    static std::optional<DLSSProfile> parseDlssMode(std::string_view name);
    static const char* graphicsPresetName(GraphicsPreset preset);
    static const char* dlssModeName(DLSSProfile profile);

    // Set once during device initialization, before rendering starts.
    static void setCurrentGpu(uint32_t vendorId, uint32_t deviceId);
    static const std::string& getCurrentGpuId();

    // Text of the strongest active value set for key outside the default layer ("True"/"False" for bools).
    // Layers below their blend threshold are ignored, matching how bool and string options resolve.
    static std::optional<std::string> findActiveValue(const std::string& key);

    // Each lookup ignores keys without an active value and logs unrecognized values.
    static std::optional<GraphicsPreset> findGraphicsPreset(const std::string& gpuId);
    static std::optional<DLSSProfile> findDlssMode(const std::string& gpuId);
    static std::optional<bool> findRayReconstruction(const std::string& gpuId);

    // Applies the Ray Reconstruction default and refreshes the cached DLSS mode for the current GPU.
    static void apply();

    // Writes the current GPU's Ray Reconstruction override to the Derived layer, or withdraws a previous one.
    // Values set through environment variables take precedence.
    static void applyRayReconstructionDefault();

    // Resolves the current GPU's DLSS mode override; getDlssMode returns the result until the next update.
    static void updateDlssMode();
    static std::optional<DLSSProfile> getDlssMode();

    // Warns about non-canonical GPU IDs and unrecognized values in any layer.
    static void logInvalidEntries();
  };

}
