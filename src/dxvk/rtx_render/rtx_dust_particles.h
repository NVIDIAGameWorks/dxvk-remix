/*
* Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
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

#include "../dxvk_format.h"
#include "../dxvk_include.h"

#include "rtx_resources.h"
#include "rtx/pass/particles/dust_particles_binding_indices.h"

namespace dxvk {

  class RtxContext;

  class RtxDustParticles {
    Rc<DxvkBuffer> m_particles;

    RTX_OPTION("rtx.dust", bool, enable, false, "Enables dust particle simulation and rendering.");
    RTX_OPTION("rtx.dust", int, numberOfParticles, 1000000, "Maximum number of particles to simulate simultaneously.");
    RTX_OPTION("rtx.dust", float, timeScale, 1.f, "Time modifier, can be used to slow/speed up time.");
    RTX_OPTION("rtx.dust", float, minSpawnDistance, 20.f, "Minimum distance in world space from camera to spawn particles.");
    RTX_OPTION("rtx.dust", float, maxSpawnDistance, 400.f, "Maximum distance in world space from camera to spawn particles.");
    RTX_OPTION("rtx.dust", float, minParticleLife, 3.f, "Minimum lifetime (in seconds) to give to a particle when spawned.");
    RTX_OPTION("rtx.dust", float, maxParticleLife, 6.f, "Maximum lifetime (in seconds) to give to a particle when spawned.");
    RTX_OPTION("rtx.dust", float, minParticleSize, 1.f, "Minimum size (in pixels) to give to a particle when spawned.");
    RTX_OPTION("rtx.dust", float, maxParticleSize, 3.f, "Maximum size (in pixels) to give to a particle when spawned.");
    RTX_OPTION("rtx.dust", float, opacity, .5f, "Opacity of the particles.");
    RTX_OPTION("rtx.dust", float, anisotropy, .5f, "Anisotropy of the particles for lighting purposes.");
    RTX_OPTION("rtx.dust", float, gravityForce, -.5f, "Net influence of gravity acting on each particle (meters per second squared).");
    RTX_OPTION("rtx.dust", float, maxSpeed, 3.f, "Maximum speed of a particle in world space.");
    RTX_OPTION("rtx.dust", bool, useTurbulence, true, "Enable turbulence simulation.");
    RTX_OPTION("rtx.dust", float, turbulenceAmplitude, 5.f, "How much turbulence influences the force of a particle.");
    RTX_OPTION("rtx.dust", float, turbulenceFrequency, .05f, "The rate of change of turbulence forces.");
    RTX_OPTION("rtx.dust", float, rotationSpeed, 5.f, "How quickly the particle is rotating (this primarily only affects light interaction).");

    void setupConstants(RtxContext* ctx, const float frameTimeSecs, Resources& resourceManager, DustParticleSystemConstants& constants);

  public:
    RtxDustParticles(DxvkDevice* device);
    ~RtxDustParticles() = default;
    
    void showImguiSettings();

    void simulateAndDraw(RtxContext* ctx, DxvkContextState& dxvkCtxState, const Resources::RaytracingOutput& rtOutput);
  };
}
