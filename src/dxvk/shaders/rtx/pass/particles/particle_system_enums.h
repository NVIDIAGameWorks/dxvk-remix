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

enum ParticleBillboardType : uint8_t {
  FaceCamera_Spherical = 0,   // classic billboard
  FaceCamera_UpAxisLocked,    // cylindrical billboard (fix up axis)
  FaceCamera_Position,        // camera->particle vector
  FaceWorldUp,                // horizontal plane (face up axis)
};

enum ParticleSpriteSheetMode : uint8_t {
  UseMaterialSpriteSheet = 0, // use the regular sprite sheet params from material
  OverrideMaterial_Lifetime,  // frame 0 at birth, last frame at death.
  OverrideMaterial_Random,    // pick one frame and keep it for the particle's life.
};

enum ParticleCollisionMode : uint8_t {
  Bounce = 0, // particle should bounce following collision
  Stop,       // stop all motion for the particle on collision
  Kill,       // kill the particle immediately on collision
};

enum ParticleRandomFlipAxis : uint8_t {
  None = 0,
  Horizontal,
  Vertical,
  Both
};

enum ParticleSpawnDistribution : uint8_t {
  ByTriangle = 0,
  BySurfaceArea = 1,
};
