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

namespace dxvk {
  // NV-DXVK start: Associate Remix categories with their schema and RTX option metadata
  struct RemixCategoryEntry {
    const char* attr;
    const char* displayName;
    const char* optionName;
  };

  inline constexpr RemixCategoryEntry kRemixCategoryEntries[] = {
    { "remix_category:world_ui",                  "World UI",                  "rtx.worldSpaceUiTextures" },
    { "remix_category:world_matte",               "World Matte",               "rtx.worldSpaceUiBackgroundTextures" },
    { "remix_category:sky",                       "Sky",                       "rtx.skyBoxTextures" },
    { "remix_category:ignore",                    "Ignore",                    "rtx.ignoreTextures" },
    { "remix_category:ignore_lights",             "Ignore Lights",             "rtx.ignoreLights" },
    { "remix_category:ignore_anti_culling",       "Ignore Anti Culling",       "rtx.antiCulling.antiCullingTextures" },
    { "remix_category:ignore_motion_blur",        "Ignore Motion Blur",        "rtx.postfx.motionBlurMaskOutTextures" },
    { "remix_category:ignore_opacity_micromap",   "Ignore Opacity Micromap",   "rtx.opacityMicromapIgnoreTextures" },
    { "remix_category:ignore_alpha_channel",      "Ignore Alpha Channel",      "rtx.ignoreAlphaOnTextures" },
    { "remix_category:hidden",                    "Hidden",                    "rtx.hideInstanceTextures" },
    { "remix_category:particle",                  "Particle",                  "rtx.particleTextures" },
    { "remix_category:beam",                      "Beam",                      "rtx.beamTextures" },
    { "remix_category:decal_Static",              "Decal Static",              "rtx.decalTextures" },
    { "remix_category:decal_dynamic",             "Decal Dynamic",             "rtx.dynamicDecalTextures" },
    { "remix_category:decal_single_offset",       "Decal Single Offset",       "rtx.singleOffsetDecalTextures" },
    { "remix_category:decal_no_offset",           "Decal No Offset",           "rtx.nonOffsetDecalTextures" },
    { "remix_category:alpha_blend_to_cutout",     "Alpha Blend To Cutout",     "rtx.forceCutoutAlpha" },
    { "remix_category:terrain",                   "Terrain",                   "rtx.terrainTextures" },
    { "remix_category:animated_water",            "Animated Water",            "rtx.animatedWaterTextures" },
    { "remix_category:third_person_player_model", "Third Person Player Model", "rtx.playerModelTextures" },
    { "remix_category:third_person_player_body",  "Third Person Player Body",  "rtx.playerModelBodyTextures" },
    { "remix_category:ignore_baked_lighting",     "Ignore Baked Lighting",     "rtx.ignoreBakedLightingTextures" },
    { "remix_category:particle_emitter",          "Particle Emitter",          "rtx.particleEmitterTextures" },
    { "remix_category:smooth_normals",            "Smooth Normals",            "rtx.smoothNormalsTextures" },
    { "remix_category:hair_cards",                "Hair Cards",                "rtx.hairCardTextures" },
  };
  // NV-DXVK end
}
