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
#include <remix/remix_c.h>

#include "rtx_water_sim.h"
#include "dxvk_device.h"
#include "dxvk_scoped_annotation.h"
#include "rtx_asset_data_manager.h"
#include "rtx_context.h"
#include "rtx_imgui.h"
#include "rtx_options.h"
#include "rtx_shader_manager.h"
#include "rtx_texture_manager.h"
#include "rtx_utils.h"

#include "rtx/pass/water_sim/water_sim_binding_indices.h"
#include "../util/util_env.h"
#include "../util/util_global_time.h"

#include <rtx_shaders/water_sim.h>
#include <rtx_shaders/water_sim_waves.h>

#include <cmath>
#include <complex>
#include <filesystem>
#include <random>

namespace dxvk {

  // Defined within an unnamed namespace to ensure unique definition across binary
  namespace {
    class WaterSimShader : public ManagedShader {
      SHADER_SOURCE(WaterSimShader, VK_SHADER_STAGE_COMPUTE_BIT, water_sim)

      PUSH_CONSTANTS(WaterSimStepArgs)

      BEGIN_PARAMETER()
        TEXTURE2D(WATER_SIM_BINDING_STATE_INPUT)
        RW_TEXTURE2D(WATER_SIM_BINDING_STATE_OUTPUT)
        RW_TEXTURE2D(WATER_SIM_BINDING_DISPLAY_OUTPUT)
        STRUCTURED_BUFFER(WATER_SIM_BINDING_DISTURBANCES)
        SAMPLER2D(WATER_SIM_BINDING_WAVES)
        TEXTURE2D(WATER_SIM_BINDING_LAYER_INPUT)
        RW_TEXTURE2D(WATER_SIM_BINDING_LAYER_OUTPUT)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(WaterSimShader);

    class WaterSimWavesShader : public ManagedShader {
      SHADER_SOURCE(WaterSimWavesShader, VK_SHADER_STAGE_COMPUTE_BIT, water_sim_waves)

      PUSH_CONSTANTS(WaterSimWaveArgs)

      BEGIN_PARAMETER()
        STRUCTURED_BUFFER(WATER_SIM_WAVES_BINDING_H0)
        RW_TEXTURE2D(WATER_SIM_WAVES_BINDING_SPECTRUM)
        RW_TEXTURE2D(WATER_SIM_WAVES_BINDING_TEMP)
        RW_TEXTURE2D(WATER_SIM_WAVES_BINDING_OUTPUT)
        RW_TEXTURE2D(WATER_SIM_WAVES_BINDING_MIP_INPUT)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(WaterSimWavesShader);
  }

  namespace {
    // The simulation advances in fixed steps, so that it behaves the same at any frame rate.
    constexpr float kStepSeconds = 1.f / 60.f;
    constexpr uint32_t kMaxStepsPerFrame = 4;
    // Largest time that one frame can add, which skips the time spent in loading screens.
    constexpr float kMaxFrameSeconds = 0.25f;
    // The explicit scheme is stable below 0.5.
    constexpr float kMaxWaveCoefficient = 0.3f;
    constexpr uint32_t kThreadGroupSize = 16;

    // The shader applies this many disturbances in a frame, the others wait in the queue.
    constexpr uint32_t kMaxDisturbancesPerFrame = 32;
    constexpr size_t kMaxPendingDisturbances = 256;
    // A disturbance takes two vectors in the buffer, see water_sim.comp.slang.
    constexpr uint32_t kVectorsPerDisturbance = 2;

    // Wind waves: two cascades, tiles of sizes and angles that never line up. The long cascade holds the wavelengths from
    // kWaveSplitLengthMeters up, the short one the rest.
    constexpr float kWaveTileSizeMeters[2] = { 10.09f, 1.57f };
    constexpr float kWaveTileAngle[2] = { 0.f, 37.f };
    constexpr float kWaveSplitLengthMeters = 0.48f;
    // The wave frequencies are rounded so that the waves repeat after this many seconds, without a seam.
    constexpr double kWaveLoopSeconds = 600.0;
    // Seed of the random phases of the wind waves.
    constexpr uint32_t kWaveSeed = 1;
    // The wind waves fade out below this fraction of the peak wavelength.
    constexpr float kShortestWaveFraction = 1.f / 6.f;
    // Shortest wave lengths that the options can ask for.
    constexpr float kMinWaveLengthMeters = 0.01f;

    // Gust patterns per second, and the strength of the wind waves in the calmest patches.
    constexpr float kGustRate = 0.05f;
    constexpr float kGustMin = 0.5f;

    // Rings per second of a drop.
    constexpr float kRingRate = 1.5f;

    // A disturbance deeper than this fraction of its radius throws spray, fully at twice it.
    constexpr float kSpraySteepness = 0.3f;
    // Spread of the spray around a disturbance, in its radii: the standard deviation of a Gaussian.
    constexpr float kSpraySpread = 2.f;
    // Time for spray to land, and for landed spray to fade to about a third.
    constexpr float kSprayFallSeconds = 0.3f;
    constexpr float kSprayLifetimeSeconds = 1.5f;

    // A disturbance this deep clears the sludge fully, shallower ones partly.
    constexpr float kSludgeClearDepthMeters = 0.04f;
    // How much the edges of the holes in the sludge vary, from 0 (round) to 1 (ragged).
    constexpr float kSludgeEdgeNoise = 0.6f;

    // Margin of the parallax walk over the highest wave there can be.
    constexpr float kWalkMarginMeters = 0.005f;

    // See 'WaterSimArgs' struct for explanation.
    constexpr float kSilhouetteMinOffsetMeters = 0.0025f;
    constexpr float kSilhouettePomMarginMeters = 0.04f;
    constexpr float kShoreRefractionGapMeters = 0.0025f;
    constexpr float kShoreStepBackMeters = 0.01f;
  }

  namespace {
    // Returns a length or speed of the options in world units.
    float worldUnits(float meters) {
      return meters * RtxOptions::getMeterToWorldUnitScale();
    }

    float waveTileSize(uint32_t cascade) {
      return worldUnits(kWaveTileSizeMeters[cascade]);
    }

    // Returns the peak of landed spray, as a fraction of the spray thrown, when it lands with the time constant fallTime and
    // fades with lifetime: the first drops fade while the last still land.
    float sprayPeakFraction(float fallTime, float lifetime) {
      if (fallTime <= 0.001f) {
        return 1.f;
      }
      if (std::abs(lifetime - fallTime) < 0.001f) {
        return std::exp(-1.f);
      }
      const float peakTime = fallTime * lifetime / (lifetime - fallTime) * std::log(lifetime / fallTime);
      return lifetime / (lifetime - fallTime) * (std::exp(-peakTime / lifetime) - std::exp(-peakTime / fallTime));
    }

    Vector2 waveRotation(uint32_t cascade) {
      const float radians = kWaveTileAngle[cascade] * (kPi / 180.f);
      return Vector2(std::cos(radians), std::sin(radians));
    }

    // Returns the variance density of wind waves over the wave vector, not normalized: a Pierson-Moskowitz like shape around the
    // peak, with short waves cut off and a spread around the direction.
    float waveSpectrum(float kx, float ky, float peakK, float cutK, float direction, float spread) {
      const float k = std::sqrt(kx * kx + ky * ky);
      if (k <= 0.f) {
        return 0.f;
      }
      const float ratio = peakK / k;
      const float cut = k / cutK;
      const float along = std::max(std::cos(std::atan2(ky, kx) - direction), 0.f);
      const float spreading = spread + (1.f - spread) * 2.f * along * along;
      return std::exp(-1.25f * ratio * ratio - cut * cut) / (k * k * k * k) * spreading;
    }
  }

  void RtxWaterSim::setState(const remixapi_WaterSimInfo& info) {
    if (info.clearAllDisturbances) {
      // The waiting disturbances are dropped here, and the next update flattens the waves of the applied ones.
      m_inputDisturbances.clear();
      m_inputClear = true;
    }
  }

  void RtxWaterSim::addDisturbance(const remixapi_WaterDisturbanceInfo& info) {
    if (info.strength != 0.f && m_inputDisturbances.size() < kMaxPendingDisturbances) {
      const Vector3 position(info.position.x, info.position.y, info.position.z);
      m_inputDisturbances.push_back({ position, std::max(info.radius, 0.f), info.strength });
    }
  }

  void RtxWaterSim::createResources(RtxContext* ctx, uint32_t gridSize) {
    Rc<DxvkContext> ctxRc(ctx);
    const VkExtent3D extent = { gridSize, gridSize, 1 };

    m_state[0] = Resources::createImageResource(ctxRc, "Water Sim State 0", extent, VK_FORMAT_R32G32B32A32_SFLOAT);
    m_state[1] = Resources::createImageResource(ctxRc, "Water Sim State 1", extent, VK_FORMAT_R32G32B32A32_SFLOAT);
    m_layer[0] = Resources::createImageResource(ctxRc, "Water Sim Sludge 0", extent, VK_FORMAT_R16_SFLOAT);
    m_layer[1] = Resources::createImageResource(ctxRc, "Water Sim Sludge 1", extent, VK_FORMAT_R16_SFLOAT);
    m_display = Resources::createImageResource(ctxRc, "Water Sim Height", extent, VK_FORMAT_R16G16B16A16_SFLOAT);

    if (m_disturbanceBuffer == nullptr) {
      DxvkBufferCreateInfo info;
      info.size = kMaxDisturbancesPerFrame * kVectorsPerDisturbance * sizeof(Vector4);
      info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
      info.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
      info.access = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
      m_disturbanceBuffer = ctx->getDevice()->createBuffer(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXBuffer, "Water Sim Disturbances");
    }

    m_gridSize = gridSize;
    m_latestState = 0;
    m_hasOrigin = false;
    m_stepAccumulator = 0.f;
    m_walkBound = 0.f;
    Logger::info(str::format("[RTX] Water sim: ", gridSize, "x", gridSize, " cells"));
  }

  bool RtxWaterSim::updateRingTexture(RtxContext* ctx) {
    const std::string& path = Rings::texture();
    if (path != m_ringTexturePath) {
      m_ringTexturePath = path;
      m_ringTexture = TextureRef();
      if (!path.empty()) {
        std::filesystem::path fullPath(path);
        if (fullPath.is_relative()) {
          fullPath = std::filesystem::path(env::getExePath()).parent_path() / fullPath;
        }
        auto assetData = AssetDataManager::get().findAsset(fullPath.string());
        if (assetData == nullptr) {
          Logger::warn(str::format("[RTX] Water sim: ring texture not found: ", fullPath.string()));
        } else {
          m_ringTexture = TextureRef(ctx->getCommonObjects()->getTextureManager().preloadTextureAsset(assetData, ColorSpace::AUTO, true));
        }
      }
    }
    return m_ringTexture.isValid() && !m_ringTexture.isImageEmpty();
  }

  void RtxWaterSim::createWaveResources(RtxContext* ctx) {
    Rc<DxvkContext> ctxRc(ctx);
    const VkExtent3D extent = { WATER_SIM_WAVES_SIZE, WATER_SIM_WAVES_SIZE, 1 };
    for (WaveCascade& cascade : m_waves) {
      DxvkBufferCreateInfo info;
      info.size = WATER_SIM_WAVES_SIZE * WATER_SIM_WAVES_SIZE * sizeof(Vector4);
      info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
      info.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
      info.access = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
      cascade.initialSpectrum = ctx->getDevice()->createBuffer(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXBuffer, "Water Sim Wave Spectrum");
      cascade.spectrum = Resources::createImageResource(ctxRc, "Water Sim Wave Spectrum", extent, VK_FORMAT_R32G32B32A32_SFLOAT);
      cascade.temp = Resources::createImageResource(ctxRc, "Water Sim Wave Temp", extent, VK_FORMAT_R32G32B32A32_SFLOAT);
      cascade.output = Resources::createImageResource(ctxRc, "Water Sim Waves", extent, VK_FORMAT_R16G16B16A16_SFLOAT, 1, VK_IMAGE_TYPE_2D,
                                                      VK_IMAGE_VIEW_TYPE_2D, 0, VK_IMAGE_USAGE_STORAGE_BIT, VkClearColorValue {}, WATER_SIM_WAVES_MIPS);

      // A storage view covers a single mip, so every mip gets one for the pass that writes it.
      DxvkImageViewCreateInfo viewInfo;
      viewInfo.type = VK_IMAGE_VIEW_TYPE_2D;
      viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
      viewInfo.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
      viewInfo.minLayer = 0;
      viewInfo.numLayers = 1;
      viewInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
      viewInfo.minLevel = 0;
      viewInfo.numLevels = WATER_SIM_WAVES_MIPS;
      cascade.sampledView = ctx->getDevice()->createImageView(cascade.output.image, viewInfo);
      viewInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT;
      viewInfo.numLevels = 1;
      for (uint32_t mip = 0; mip < WATER_SIM_WAVES_MIPS; ++mip) {
        viewInfo.minLevel = mip;
        cascade.mipViews[mip] = ctx->getDevice()->createImageView(cascade.output.image, viewInfo);
      }
    }
    m_hasWaveSpectra = false;
  }

  void RtxWaterSim::buildWaveSpectra(RtxContext* ctx, const WaveParams& params) {
    constexpr uint32_t n = WATER_SIM_WAVES_SIZE;
    const float direction = params.direction * (kPi / 180.f);
    const float spread = std::clamp(params.spread, 0.f, 1.f);
    const float minLength = worldUnits(kMinWaveLengthMeters);
    const float windPeakK = 2.f * kPi / std::max(params.peakLength, minLength);
    const float swellPeakK = 2.f * kPi / std::max(params.swellLength, minLength);
    const float cutK = 2.f * kPi / std::max(params.shortestLength, 0.1f * minLength);
    const float splitLength = worldUnits(kWaveSplitLengthMeters);

    // The variance of every texel for the wind waves and for the swell, and the slope variance each gives over both cascades, so
    // that each can be scaled to its slope. E|h(k, t)|^2 = V(k) + V(-k), so the slope variance is twice the sum of k^2 V.
    std::vector<float> wind[2];
    std::vector<float> swell[2];
    double windSlopeVariance = 0.0;
    double swellSlopeVariance = 0.0;
    for (uint32_t cascade = 0; cascade < 2; ++cascade) {
      wind[cascade].assign(n * n, 0.f);
      swell[cascade].assign(n * n, 0.f);
      const float dk = 2.f * kPi / waveTileSize(cascade);
      for (uint32_t y = 0; y < n; ++y) {
        for (uint32_t x = 0; x < n; ++x) {
          const int fx = x < n / 2 ? int(x) : int(x) - int(n);
          const int fy = y < n / 2 ? int(y) : int(y) - int(n);
          // The Nyquist frequency has no partner of the opposite sign, so it stays out, like the mean.
          if (fx == -int(n / 2) || fy == -int(n / 2) || (fx == 0 && fy == 0)) {
            continue;
          }
          const float kx = fx * dk;
          const float ky = fy * dk;
          const float k = std::sqrt(kx * kx + ky * ky);
          if ((2.f * kPi / k >= splitLength) != (cascade == 0)) {
            continue;
          }
          const uint32_t i = y * n + x;
          wind[cascade][i] = waveSpectrum(kx, ky, windPeakK, cutK, direction, spread) * dk * dk;
          swell[cascade][i] = waveSpectrum(kx, ky, swellPeakK, cutK, direction, 0.1f) * dk * dk;
          windSlopeVariance += 2.0 * k * k * wind[cascade][i];
          swellSlopeVariance += 2.0 * k * k * swell[cascade][i];
        }
      }
    }
    const double windScale = windSlopeVariance > 0.0 ? double(params.slope) * params.slope / windSlopeVariance : 0.0;
    const double swellScale = swellSlopeVariance > 0.0 ? double(params.swellSlope) * params.swellSlope / swellSlopeVariance : 0.0;

    std::mt19937 random(kWaveSeed);
    std::normal_distribution<float> gaussian;
    double longHeightVariance = 0.0;
    std::vector<std::complex<float>> h0(n * n);
    std::vector<Vector4> data(n * n);
    for (uint32_t cascade = 0; cascade < 2; ++cascade) {
      // h0(k) = (a + ib) sqrt(V(k) / 2), with standard normal a and b.
      for (uint32_t i = 0; i < n * n; ++i) {
        const double variance = windScale * wind[cascade][i] + swellScale * swell[cascade][i];
        const float a = gaussian(random);
        const float b = gaussian(random);
        h0[i] = std::complex<float>(a, b) * float(std::sqrt(variance * 0.5));
        if (cascade == 0) {
          longHeightVariance += 2.0 * variance;
        }
      }
      for (uint32_t y = 0; y < n; ++y) {
        for (uint32_t x = 0; x < n; ++x) {
          const std::complex<float> mirrored = std::conj(h0[((n - y) % n) * n + (n - x) % n]);
          const std::complex<float> value = h0[y * n + x];
          data[y * n + x] = Vector4(value.real(), value.imag(), mirrored.real(), mirrored.imag());
        }
      }
      ctx->uploadBuffer(m_waves[cascade].initialSpectrum, data.data(), static_cast<uint32_t>(data.size() * sizeof(Vector4)));
    }

    // Three standard deviations of the long waves' height.
    m_waveHeightBound = 3.f * float(std::sqrt(longHeightVariance));
    m_waveParams = params;
    m_hasWaveSpectra = true;
  }

  void RtxWaterSim::dispatchWaves(RtxContext* ctx) {
    ScopedGpuProfileZone(ctx, "Water Sim Waves");

    WaterSimWaveArgs args = {};
    args.time = static_cast<float>(m_waveTime);
    // Deep water waves of the peak wavelength pass in the period of the option, which gives the gravity for all of them.
    const float period = std::max(wavePeriodSeconds(), 0.01f);
    args.gravity = std::max(2.f * kPi * m_waveParams.peakLength / (period * period), worldUnits(0.01f));
    args.omega0 = static_cast<float>(2.0 * kPi / kWaveLoopSeconds);

    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, WaterSimWavesShader::getShader());
    ctx->setPushConstantBank(DxvkPushConstantBank::RTX);
    // The cascades take turns pass by pass, so that each barrier between dependent passes serves both.
    auto dispatch = [&](uint32_t cascade, uint32_t outputMip, uint32_t groups) {
      const WaveCascade& waves = m_waves[cascade];
      args.tileSize = waveTileSize(cascade);
      ctx->bindResourceBuffer(WATER_SIM_WAVES_BINDING_H0, DxvkBufferSlice(waves.initialSpectrum));
      ctx->bindResourceView(WATER_SIM_WAVES_BINDING_SPECTRUM, waves.spectrum.view, nullptr);
      ctx->bindResourceView(WATER_SIM_WAVES_BINDING_TEMP, waves.temp.view, nullptr);
      ctx->bindResourceView(WATER_SIM_WAVES_BINDING_OUTPUT, waves.mipViews[outputMip], nullptr);
      ctx->bindResourceView(WATER_SIM_WAVES_BINDING_MIP_INPUT, waves.mipViews[outputMip > 0 ? outputMip - 1 : 1], nullptr);
      ctx->pushConstants(0, sizeof(args), &args);
      ctx->dispatch(groups, 1, 1);
    };
    for (uint32_t pass : { WATER_SIM_WAVES_PASS_SPECTRUM, WATER_SIM_WAVES_PASS_ROWS, WATER_SIM_WAVES_PASS_COLUMNS }) {
      args.pass = pass;
      for (uint32_t cascade = 0; cascade < 2; ++cascade) {
        dispatch(cascade, 0, WATER_SIM_WAVES_SIZE);
      }
    }
    args.pass = WATER_SIM_WAVES_PASS_MIP;
    for (uint32_t mip = 1; mip < WATER_SIM_WAVES_MIPS; ++mip) {
      args.mipSize = WATER_SIM_WAVES_SIZE >> mip;
      constexpr uint32_t threads = WATER_SIM_WAVES_SIZE / 2;
      for (uint32_t cascade = 0; cascade < 2; ++cascade) {
        dispatch(cascade, mip, (args.mipSize * args.mipSize + threads - 1) / threads);
      }
    }

    // The material reads the waves through the bindless tables, which the context does not track for barriers.
    ctx->emitMemoryBarrier(0, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                           VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
  }

  Rc<DxvkSampler> RtxWaterSim::getDetailSampler(RtxContext* ctx) {
    if (m_detailSampler == nullptr) {
      m_detailSampler = ctx->getSceneManager().patchSampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT,
                                                            VK_SAMPLER_ADDRESS_MODE_REPEAT, VkClearColorValue {});
    }
    return m_detailSampler;
  }

  void RtxWaterSim::updateDetail(RtxContext* ctx, float deltaSeconds) {
    if (!detail()) {
      m_detailActive = false;
      m_ringsActive = false;
      return;
    }

    if (m_waves[0].initialSpectrum == nullptr) {
      createWaveResources(ctx);
    }
    WaveParams params;
    params.slope = std::max(waveSlope(), 0.f);
    params.peakLength = worldUnits(std::max(wavePeakLengthMeters(), 0.f));
    params.shortestLength = params.peakLength * kShortestWaveFraction;
    params.direction = waveDirectionDegrees();
    params.spread = waveSpread();
    params.swellSlope = std::max(swellSlope(), 0.f);
    params.swellLength = worldUnits(std::max(swellLengthMeters(), 0.f));
    if (!m_hasWaveSpectra || !(params == m_waveParams)) {
      buildWaveSpectra(ctx, params);
    }

    SceneManager& sceneManager = ctx->getSceneManager();
    for (WaveCascade& waves : m_waves) {
      sceneManager.trackTexture(TextureRef(waves.sampledView), waves.textureIndex, true, false);
    }
    m_ringsActive = updateRingTexture(ctx);
    if (m_ringsActive) {
      sceneManager.trackTexture(m_ringTexture, m_ringTextureIndex, true, false);
    }
    m_detailSamplerIndex = sceneManager.trackSampler(getDetailSampler(ctx));
    m_detailActive = true;

    m_waveTime = std::fmod(m_waveTime + deltaSeconds, kWaveLoopSeconds);
    m_gustTime = std::fmod(m_gustTime + kGustRate * deltaSeconds, 4096.f);
    m_ringPhase += kRingRate * deltaSeconds;
    m_ringPhase -= std::floor(m_ringPhase);
  }

  void RtxWaterSim::prepareSceneData(RtxContext* ctx) {
    // The simulation decides whether it runs later in the frame, in update(), so the detail starts a frame after it.
    if (!m_active) {
      m_detailActive = false;
      return;
    }
    updateDetail(ctx, std::max(GlobalTime::get().deltaTime(), 0.f));
  }

  Rc<DxvkImageView> RtxWaterSim::getHeightView(RtxContext* ctx) {
    if (m_display.isValid()) {
      return m_display.view;
    }
    return getDummyView(ctx);
  }

  Rc<DxvkImageView> RtxWaterSim::getDummyView(RtxContext* ctx) {
    if (!m_dummy.isValid()) {
      Rc<DxvkContext> ctxRc(ctx);
      m_dummy = Resources::createImageResource(ctxRc, "Water Sim Dummy", { 1, 1, 1 }, VK_FORMAT_R16G16B16A16_SFLOAT);
    }
    return m_dummy.view;
  }

  void RtxWaterSim::update(RtxContext* ctx) {
    // Take what the application sent since the previous frame.
    const bool clearRequested = m_inputClear;
    m_inputClear = false;

    const auto first = m_inputDisturbances.begin();
    const auto last = first + std::min<size_t>(m_inputDisturbances.size(), kMaxDisturbancesPerFrame);
    const std::vector<WaterDisturbance> disturbances(first, last);
    m_inputDisturbances.erase(first, last);

    m_active = enable();
    if (!m_active) {
      // Release the images, so that the waves start flat again when the simulation is switched back on.
      if (m_display.isValid()) {
        m_state[0].reset();
        m_state[1].reset();
        m_layer[0].reset();
        m_layer[1].reset();
        m_display.reset();
      }
      for (WaveCascade& waves : m_waves) {
        waves = WaveCascade();
      }
      m_hasWaveSpectra = false;
      // Disturbances that come in while it is off would be stale when it is switched on.
      m_inputDisturbances.clear();
      return;
    }

    ScopedGpuProfileZone(ctx, "Water Sim");

    const uint32_t gridSize = static_cast<uint32_t>(std::clamp(resolution(), 64, 2048));
    if (!m_display.isValid() || gridSize != m_gridSize || clearRequested) {
      // New images start flat, which is also how the earlier waves are removed.
      createResources(ctx, gridSize);
    }

    if (m_detailActive) {
      dispatchWaves(ctx);
    }

    const bool isZUp = RtxOptions::zUp();
    const float cell = worldUnits(std::max(cellSizeMeters(), 0.0001f));
    const float waveSpeed = std::max(worldUnits(waveSpeedMetersPerSecond()), 0.f);
    const float maxHeight = std::max(worldUnits(maxHeightMeters()), 0.f);
    // The waves fade and the surface settles back at the same rate.
    const float decayRate = 1.f / std::max(rippleLifetimeSeconds(), 0.01f);
    auto toPlane = [isZUp](const Vector3& p) { return isZUp ? Vector2(p.x, p.y) : Vector2(p.x, p.z); };

    // The window follows the camera, and the waves run on the renderer's clock.
    const Vector3 center = ctx->getSceneManager().getCamera().getPosition();
    const float deltaSeconds = std::max(GlobalTime::get().deltaTime(), 0.f);

    // The highest wave there can be now bounds the parallax walk: the deepest recent dent, worn down at the rate the waves lose
    // height. Dents that overlap or spring back go deeper than one dent, hence the margin.
    m_walkBound *= std::exp(-decayRate * deltaSeconds);
    for (const WaterDisturbance& d : disturbances) {
      m_walkBound = std::max(m_walkBound, 1.5f * std::abs(d.strength));
    }
    m_walkBound = std::min(m_walkBound, maxHeight);

    // The window is anchored to the world grid, and follows the center in whole cells.
    m_centerCell = toPlane(center) / cell;
    const int half = static_cast<int>(gridSize / 2);
    const int originX = static_cast<int>(std::floor(m_centerCell.x)) - half;
    const int originY = static_cast<int>(std::floor(m_centerCell.y)) - half;
    const bool moved = !m_hasOrigin || originX != m_originX || originY != m_originY;
    const int oldOriginX = m_hasOrigin ? m_originX : originX;
    const int oldOriginY = m_hasOrigin ? m_originY : originY;
    m_originX = originX;
    m_originY = originY;
    m_hasOrigin = true;

    // Fixed steps with a cap, so that a long frame does not cause a burst of steps.
    m_stepAccumulator += std::min(deltaSeconds, kMaxFrameSeconds);
    uint32_t stepCount = static_cast<uint32_t>(m_stepAccumulator / kStepSeconds);
    if (stepCount >= kMaxStepsPerFrame) {
      stepCount = kMaxStepsPerFrame;
      m_stepAccumulator = 0.f;
    } else {
      m_stepAccumulator -= stepCount * kStepSeconds;
    }

    // A frame without a step still applies disturbances and clears the cells that the window moved over. With the wind waves,
    // the display changes every frame.
    const bool hasDisturbances = !disturbances.empty();
    const bool isFrozenPass = stepCount == 0;
    if (isFrozenPass && !hasDisturbances && !moved && !m_detailActive) {
      return;
    }
    const uint32_t passCount = std::max(stepCount, 1u);

    const uint32_t disturbanceCount = static_cast<uint32_t>(disturbances.size());
    if (hasDisturbances) {
      // A disturbance gives the water a velocity, and the dent forms over the next steps. The push is scaled so that the dent sinks
      // about as deep as its strength. Wide dents get more, since the fading and settling wear them down on the way.
      const float speed = std::max(waveSpeed, worldUnits(0.01f));
      const float decay = 2.f * decayRate;
      // The spray is scaled up by its peak fraction, so that the landed spray peaks at the amount thrown.
      const float sprayScale = 1.f / sprayPeakFraction(kSprayFallSeconds, kSprayLifetimeSeconds);
      const bool sludge = Sludge::enable();
      const float clearDepth = worldUnits(kSludgeClearDepthMeters);
      const float clearSpread = std::max(Sludge::holeSize(), 0.01f);
      std::vector<Vector4> packed(disturbanceCount * kVectorsPerDisturbance);
      for (uint32_t i = 0; i < disturbanceCount; ++i) {
        const WaterDisturbance& d = disturbances[i];
        const Vector2 position = toPlane(d.position) / cell;
        const float radius = std::max(d.radius, 1.5f * cell);
        const float push = 3.f * std::exp(0.275f * decay * radius / speed) * (speed * kStepSeconds / radius);
        const float spray = std::clamp(std::abs(d.strength) / (radius * kSpraySteepness) - 1.f, 0.f, 1.f);
        packed[i * kVectorsPerDisturbance] = Vector4(position.x, position.y, d.radius / cell, d.strength * push);
        const float clear = sludge ? std::min(std::abs(d.strength) / clearDepth, 1.f) : 0.f;
        packed[i * kVectorsPerDisturbance + 1] = Vector4(spray * sprayScale, kSpraySpread * radius / cell, clear, clearSpread * radius / cell);
      }
      ctx->updateBuffer(m_disturbanceBuffer, 0, packed.size() * sizeof(Vector4), packed.data());
    }

    const float cellsPerStep = waveSpeed * kStepSeconds / cell;

    WaterSimStepArgs args = {};
    args.originX = originX;
    args.originY = originY;
    args.gridSize = gridSize;
    args.advance = isFrozenPass ? 0.f : 1.f;
    args.waveCoefficient = isFrozenPass ? 0.f : std::min(cellsPerStep * cellsPerStep, kMaxWaveCoefficient);
    args.velocityKeep = isFrozenPass ? 1.f : std::exp(-decayRate * kStepSeconds);
    args.heightKeep = args.velocityKeep;
    args.maxHeight = maxHeight;
    args.cellSize = cell;
    args.sprayKeep = isFrozenPass ? 1.f : std::exp(-kStepSeconds / kSprayLifetimeSeconds);
    args.sprayLanding = isFrozenPass ? 0.f : 1.f - std::exp(-kStepSeconds / kSprayFallSeconds);
    args.clearKeep = !Sludge::enable() ? 0.f : isFrozenPass ? 1.f : std::exp(-kStepSeconds / std::max(Sludge::holeLifetimeSeconds(), 0.01f));
    args.waveHeightScale = m_detailActive ? 1.f : 0.f;
    args.wave0Rotation = waveRotation(0);
    args.wave0InvTileSize = 1.f / waveTileSize(0);
    args.gustInvSize = 1.f / worldUnits(std::max(gustSizeMeters(), 0.01f));
    args.gustTime = m_gustTime;
    args.gustMin = kGustMin;

    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, WaterSimShader::getShader());
    ctx->setPushConstantBank(DxvkPushConstantBank::RTX);
    ctx->bindResourceBuffer(WATER_SIM_BINDING_DISTURBANCES, DxvkBufferSlice(m_disturbanceBuffer));
    ctx->bindResourceView(WATER_SIM_BINDING_DISPLAY_OUTPUT, m_display.view, nullptr);
    // Without the wind waves the shader does not read this binding, but it has to hold a valid view.
    ctx->bindResourceView(WATER_SIM_BINDING_WAVES, m_detailActive ? m_waves[0].sampledView : getDummyView(ctx), nullptr);
    ctx->bindResourceSampler(WATER_SIM_BINDING_WAVES, getDetailSampler(ctx));

    const uint32_t groups = (gridSize + kThreadGroupSize - 1) / kThreadGroupSize;
    for (uint32_t pass = 0; pass < passCount; ++pass) {
      // Only the first pass sees the window move and the disturbances.
      args.oldOriginX = pass == 0 ? oldOriginX : originX;
      args.oldOriginY = pass == 0 ? oldOriginY : originY;
      args.disturbanceCount = pass == 0 ? disturbanceCount : 0;
      args.writeDisplay = pass + 1 == passCount;

      ctx->bindResourceView(WATER_SIM_BINDING_STATE_INPUT, m_state[m_latestState].view, nullptr);
      ctx->bindResourceView(WATER_SIM_BINDING_STATE_OUTPUT, m_state[m_latestState ^ 1].view, nullptr);
      ctx->bindResourceView(WATER_SIM_BINDING_LAYER_INPUT, m_layer[m_latestState].view, nullptr);
      ctx->bindResourceView(WATER_SIM_BINDING_LAYER_OUTPUT, m_layer[m_latestState ^ 1].view, nullptr);
      ctx->pushConstants(0, sizeof(args), &args);
      ctx->dispatch(groups, groups, 1);
      m_latestState ^= 1;
    }
  }

  void RtxWaterSim::fillShaderParams(WaterSimArgs& args) const {
    args.enable = m_active && m_display.isValid();
    args.cellSize = worldUnits(std::max(cellSizeMeters(), 0.0001f));
    args.gridSize = static_cast<float>(m_gridSize);
    args.maxHeight = std::max(worldUnits(maxHeightMeters()), 0.f);
    args.centerCell = m_centerCell;
    args.normalStrength = 1.f;
    // A view ray looking straight down takes a quarter of the samples.
    args.pomMaxSteps = static_cast<uint32_t>(std::clamp(parallaxMaxSteps(), 0, 128));
    args.pomSteps = args.pomMaxSteps > 0 ? std::max(args.pomMaxSteps / 4, 1u) : 0;
    args.silhouetteFlags =
      (silhouettes() ? WATER_SIM_SILHOUETTE_FLAG_WAVES : 0) |
      (RtxOptions::Displacement::silhouettesBehindTranslucency() ? WATER_SIM_SILHOUETTE_FLAG_POM_BEHIND_TRANSLUCENCY : 0);
    // Without silhouettes nothing would show the shore above a lowered surface, so the surface stays at the mesh.
    args.restDepth = silhouettes() ? std::max(worldUnits(restDepthMeters()), 0.f) : 0.f;
    args.silhouetteMinOffset = worldUnits(kSilhouetteMinOffsetMeters);
    args.silhouettePomMargin = worldUnits(kSilhouettePomMarginMeters);
    args.shoreRefractionGap = worldUnits(kShoreRefractionGapMeters);
    args.shoreStepBack = worldUnits(kShoreStepBackMeters);

    const bool detail = m_active && m_detailActive;
    args.detailFlags = detail ? WATER_SIM_DETAIL_FLAG_ACTIVE | (m_ringsActive ? WATER_SIM_DETAIL_FLAG_RINGS : 0) : 0;
    // The long waves are part of the displayed height, so the walk covers them too.
    const float waveHeight = detail ? m_waveHeightBound : 0.f;
    args.walkBound = std::min(m_walkBound + waveHeight + worldUnits(kWalkMarginMeters), args.maxHeight);
    // The scene manager sets the index of the sludge water material once it has added it to the surface materials.
    args.sludgeWaterMaterialIndex = SURFACE_INDEX_INVALID;
    args.wave0TextureIndex = m_waves[0].textureIndex;
    args.wave1TextureIndex = m_waves[1].textureIndex;
    args.ringTextureIndex = m_ringTextureIndex;
    args.detailSamplerIndex = m_detailSamplerIndex;
    args.wave0Rotation = waveRotation(0);
    args.wave1Rotation = waveRotation(1);
    args.wave0InvTileSize = 1.f / waveTileSize(0);
    args.wave1InvTileSize = 1.f / waveTileSize(1);
    args.gustInvSize = 1.f / worldUnits(std::max(gustSizeMeters(), 0.01f));
    args.gustTime = m_gustTime;
    args.gustMin = kGustMin;
    args.ringInvTileSize = 1.f / worldUnits(std::max(Rings::tileSizeMeters(), 0.01f));
    args.ringSlope = std::max(Rings::slope(), 0.f);
    args.ringPhase = m_ringPhase;
    args.ringRain = std::clamp(Rings::rain(), 0.f, 1.f);
    args.sludgeEdgeNoise = kSludgeEdgeNoise;
  }

  void RtxWaterSim::showImguiSettings() {
    RemixGui::Checkbox("Enable", &enableObject());
    ImGui::TextWrapped(m_active ? "Running: the application switched it on with remixapi_SetWaterSim." : "Not running: the application switches it on with remixapi_SetWaterSim.");

    RemixGui::Separator();
    RemixGui::DragInt("Resolution", &resolutionObject(), 1.f, 64, 2048, "%d", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::DragFloat("Cell Size (m)", &cellSizeMetersObject(), 0.001f, 0.001f, 2.56f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::DragFloat("Max Height (m)", &maxHeightMetersObject(), 0.001f, 0.f, 2.56f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::DragFloat("Wave Speed (m/s)", &waveSpeedMetersPerSecondObject(), 0.01f, 0.f, 10.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::DragFloat("Ripple Lifetime (s)", &rippleLifetimeSecondsObject(), 0.05f, 0.01f, 60.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::DragInt("Parallax Max Steps", &parallaxMaxStepsObject(), 0.1f, 0, 128, "%d", ImGuiSliderFlags_AlwaysClamp);

    RemixGui::Separator();
    RemixGui::Checkbox("Silhouettes", &silhouettesObject());
    ImGui::BeginDisabled(!silhouettes());
    RemixGui::DragFloat("Rest Depth (m)", &restDepthMetersObject(), 0.001f, 0.f, 0.64f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::EndDisabled();

    RemixGui::Separator();
    RemixGui::Checkbox("Detail", &detailObject());
    RemixGui::DragFloat("Wave Slope", &waveSlopeObject(), 0.001f, 0.f, 0.5f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::DragFloat("Wave Peak Length (m)", &wavePeakLengthMetersObject(), 0.005f, 0.02f, 10.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::DragFloat("Wave Period (s)", &wavePeriodSecondsObject(), 0.01f, 0.05f, 10.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::DragFloat("Wave Direction (degrees)", &waveDirectionDegreesObject(), 1.f, -360.f, 360.f, "%.0f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::DragFloat("Wave Spread", &waveSpreadObject(), 0.01f, 0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::DragFloat("Swell Slope", &swellSlopeObject(), 0.001f, 0.f, 0.2f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::DragFloat("Swell Length (m)", &swellLengthMetersObject(), 0.01f, 0.1f, 20.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::DragFloat("Gust Size (m)", &gustSizeMetersObject(), 0.1f, 0.01f, 164.f, "%.1f", ImGuiSliderFlags_AlwaysClamp);

    RemixGui::Separator();
    ImGui::TextWrapped(m_ringsActive ? "Droplet rings." : "No droplet rings: rtx.waterSim.rings.texture is not loaded.");
    RemixGui::DragFloat("Ring Slope", &Rings::slopeObject(), 0.01f, 0.f, 2.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::DragFloat("Ring Tile Size (m)", &Rings::tileSizeMetersObject(), 0.01f, 0.01f, 10.24f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::DragFloat("Rain", &Rings::rainObject(), 0.01f, 0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

    RemixGui::Separator();
    RemixGui::Checkbox("Sludge", &Sludge::enableObject());
    ImGui::BeginDisabled(!Sludge::enable());
    RemixGui::DragFloat("Sludge Hole Size", &Sludge::holeSizeObject(), 0.01f, 0.01f, 10.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::DragFloat("Sludge Hole Lifetime (s)", &Sludge::holeLifetimeSecondsObject(), 0.05f, 0.01f, 60.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    RemixGui::ColorEdit3("Sludge Water Transmittance", &Sludge::waterTransmittanceObject());
    RemixGui::DragFloat("Sludge Water Measurement Distance (m)", &Sludge::waterMeasurementDistanceMetersObject(), 0.001f, 0.001f, 10.f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::EndDisabled();
  }
}
