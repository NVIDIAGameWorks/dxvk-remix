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

#include <vector>

#include "../dxvk_include.h"
#include "rtx_option.h"
#include "rtx_resources.h"
#include "rtx_texture.h"
#include "rtx/pass/water_sim/water_sim_args.h"

struct remixapi_WaterSimInfo;
struct remixapi_WaterDisturbanceInfo;

namespace dxvk {

  class DxvkDevice;
  class RtxContext;

  struct WaterDisturbance {
    Vector3 position;
    float radius;
    float strength;
  };

  // GPU wave simulation of a height field around the camera that moves animated water, driven through the Remix API.
  // See documentation/WaterSimulation.md.
  class RtxWaterSim {
  public:
    RtxWaterSim(DxvkDevice* device) { }

    // The Remix API calls both on the CS thread, see rtx_remix_api.cpp.
    void setState(const remixapi_WaterSimInfo& info);
    void addDisturbance(const remixapi_WaterDisturbanceInfo& info);

    // Adds the detail textures to the scene's bindless tables. Call it once per frame before the tables are built.
    void prepareSceneData(RtxContext* ctx);

    // Advances the simulation. Call it once per frame before ray tracing.
    void update(RtxContext* ctx);

    // Returns a valid view at all times, a dummy one while the simulation is not running.
    Rc<DxvkImageView> getHeightView(RtxContext* ctx);

    void fillShaderParams(WaterSimArgs& args) const;

    void showImguiSettings();

    // Sludge on the water (rtx.sludgeTextures) that opens into water where the water is disturbed, see water_sim_sludge.slangh.
    struct Sludge {
      RTX_OPTION("rtx.waterSim.sludge", bool, enable, false, "Disturbances open sludge (rtx.sludgeTextures) into water.");
      RTX_OPTION("rtx.waterSim.sludge", float, holeSize, 1.f, "Size of the hole a disturbance opens, in disturbance radii.");
      RTX_OPTION("rtx.waterSim.sludge", float, holeLifetimeSeconds, 8.f, "Time for a hole to close to about a third, from its edges.");
      RTX_OPTION("rtx.waterSim.sludge", Vector3, waterTransmittance, Vector3(0.55f, 0.62f, 0.38f), "Color of the light through the water under the sludge after waterMeasurementDistanceMeters.");
      RTX_OPTION("rtx.waterSim.sludge", float, waterMeasurementDistanceMeters, 0.12f, "Distance at which light through the water under the sludge takes the waterTransmittance color.");

      static constexpr float kWaterRefractiveIndex = 1.33f;
    };

  private:
    // Lengths and speeds in meters are scaled by rtx.sceneScale. The defaults are for 1 meter = 100 game units.
    RTX_OPTION("rtx.waterSim", bool, enable, false, "Runs the water simulation on animated water.");
    RTX_OPTION("rtx.waterSim", int, resolution, 512, "Cells along each side of the simulated area, which follows the camera.");
    RTX_OPTION("rtx.waterSim", float, cellSizeMeters, 0.04f, "Size of one cell, so that the simulated area is resolution times this wide.");
    RTX_OPTION("rtx.waterSim", float, waveSpeedMetersPerSecond, 1.f, "How fast ripples travel across the water.");
    RTX_OPTION("rtx.waterSim", float, rippleLifetimeSeconds, 2.f, "Time for ripples to fade to about a third while the water settles back to rest.");
    RTX_OPTION("rtx.waterSim", float, maxHeightMeters, 0.08f, "The highest a wave rises or the deepest a splash digs, which also bounds the parallax search.");
    RTX_OPTION("rtx.waterSim", int, parallaxMaxSteps, 32, "Height samples a view ray takes at grazing angles to find the waves, a quarter of them looking straight down, and 0 for no parallax.");
    RTX_OPTION("rtx.waterSim", bool, silhouettes, true, "Banks and objects hide the wave troughs behind them, at the cost of a short ray for each water pixel that looks into a trough.");
    RTX_OPTION("rtx.waterSim", float, restDepthMeters, 0.04f, "How far below the water mesh the water rests, so that the water line follows the shore (needs silhouettes).");

    // Detail: wind waves from a wave generator and droplet rings, instead of the material's normal maps.
    RTX_OPTION("rtx.waterSim", bool, detail, true, "Wind waves and droplet rings replace the material's normal maps.");
    RTX_OPTION("rtx.waterSim", float, waveSlope, 0.035f, "Steepness of the wind waves: 0.01 to 0.02 calm, about 0.05 breezy, 0.08 and more choppy.");
    RTX_OPTION("rtx.waterSim", float, wavePeakLengthMeters, 0.36f, "Length of the largest wind waves, with shorter ones down to a sixth of it.");
    RTX_OPTION("rtx.waterSim", float, wavePeriodSeconds, 0.77f, "Time for the largest wind wave to pass a point, which sets how fast all wind waves move.");
    RTX_OPTION("rtx.waterSim", float, waveDirectionDegrees, 30.f, "Direction the wind waves travel, measured from the first axis of the water plane.");
    RTX_OPTION("rtx.waterSim", float, waveSpread, 0.4f, "How widely the wind waves spread around their direction, from 0 (all along it) to 1 (every way).");
    RTX_OPTION("rtx.waterSim", float, swellSlope, 0.012f, "Steepness of the long, gentle swell that keeps calm water alive.");
    RTX_OPTION("rtx.waterSim", float, swellLengthMeters, 1.6f, "Length of the swell waves.");
    RTX_OPTION("rtx.waterSim", float, gustSizeMeters, 7.f, "Size of the calmer and rougher patches that gusts make on the water.");

    // Droplet rings: a tiling texture of drops that ring where the spray of splashes lands, and everywhere with rain.
    struct Rings {
      RTX_OPTION("rtx.waterSim.rings", std::string, texture, "", "Droplet texture (distance from the drop center over its radius, direction, phase), with relative paths starting at the game folder.");
      RTX_OPTION("rtx.waterSim.rings", float, tileSizeMeters, 1.28f, "Size of one tile of the droplet texture on the water.");
      RTX_OPTION("rtx.waterSim.rings", float, slope, 0.35f, "Steepness of the rings.");
      RTX_OPTION("rtx.waterSim.rings", float, rain, 0.f, "Rings everywhere, from 0 (only where splashes land) to 1 (rain).");
    };

    void createResources(RtxContext* ctx, uint32_t gridSize);
    Rc<DxvkImageView> getDummyView(RtxContext* ctx);

    // A tile of wind waves, remade by the wave generator every frame, see water_sim_waves.comp.slang.
    struct WaveCascade {
      Rc<DxvkBuffer> initialSpectrum;
      Resources::Resource spectrum;
      Resources::Resource temp;
      // Height and slopes with mips, and a view of each mip for the passes that write it.
      Resources::Resource output;
      Rc<DxvkImageView> sampledView;
      Rc<DxvkImageView> mipViews[WATER_SIM_WAVES_MIPS];
      uint32_t textureIndex = 0;
    };

    // The wave options that the initial spectra were made with.
    struct WaveParams {
      float slope = 0.f;
      float peakLength = 0.f;
      float shortestLength = 0.f;
      float direction = 0.f;
      float spread = 0.f;
      float swellSlope = 0.f;
      float swellLength = 0.f;

      bool operator==(const WaveParams& o) const {
        return slope == o.slope && peakLength == o.peakLength && shortestLength == o.shortestLength && direction == o.direction &&
               spread == o.spread && swellSlope == o.swellSlope && swellLength == o.swellLength;
      }
    };

    void createWaveResources(RtxContext* ctx);
    // Makes the initial spectra of both cascades from the wave options and uploads them.
    void buildWaveSpectra(RtxContext* ctx, const WaveParams& params);
    void dispatchWaves(RtxContext* ctx);
    Rc<DxvkSampler> getDetailSampler(RtxContext* ctx);
    void updateDetail(RtxContext* ctx, float deltaSeconds);

    // Loads the ring texture when its path changed.
    // Returns whether it is loaded.
    bool updateRingTexture(RtxContext* ctx);

    Resources::Resource m_state[2];
    // How much the sludge is cleared, alternating like the state.
    Resources::Resource m_layer[2];
    Resources::Resource m_display;
    Resources::Resource m_dummy;
    Rc<DxvkBuffer> m_disturbanceBuffer;
    uint32_t m_gridSize = 0;
    uint32_t m_latestState = 0;

    bool m_active = false;
    bool m_hasOrigin = false;
    int m_originX = 0;
    int m_originY = 0;
    Vector2 m_centerCell = Vector2(0.f);
    float m_stepAccumulator = 0.f;
    // The highest wave there can be now, see update().
    float m_walkBound = 0.f;

    WaveCascade m_waves[2];
    WaveParams m_waveParams;
    bool m_hasWaveSpectra = false;
    // Highest crest of the long waves in the simulated surface, for the walk.
    float m_waveHeightBound = 0.f;
    // Seconds, wrapped to the loop period of the waves.
    double m_waveTime = 0.0;
    float m_gustTime = 0.f;
    Rc<DxvkSampler> m_detailSampler;
    bool m_detailActive = false;
    uint32_t m_detailSamplerIndex = 0;

    std::string m_ringTexturePath;
    TextureRef m_ringTexture;
    bool m_ringsActive = false;
    uint32_t m_ringTextureIndex = 0;
    float m_ringPhase = 0.f;

    // Is set by a state call that removes all disturbances, and taken by the next update.
    bool m_inputClear = false;
    // Disturbances that wait to be applied, in the order they came in.
    std::vector<WaterDisturbance> m_inputDisturbances;
  };
}
