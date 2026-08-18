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
  inline constexpr const char* kRemixCategoryNames[] = {
    "remix_category:world_ui",
    "remix_category:world_matte",
    "remix_category:sky",
    "remix_category:ignore",
    "remix_category:ignore_lights",
    "remix_category:ignore_anti_culling",
    "remix_category:ignore_motion_blur",
    "remix_category:ignore_opacity_micromap",
    "remix_category:ignore_alpha_channel",
    "remix_category:hidden",
    "remix_category:particle",
    "remix_category:beam",
    "remix_category:decal_Static",
    "remix_category:decal_dynamic",
    "remix_category:decal_single_offset",
    "remix_category:decal_no_offset",
    "remix_category:alpha_blend_to_cutout",
    "remix_category:terrain",
    "remix_category:animated_water",
    "remix_category:third_person_player_model",
    "remix_category:third_person_player_body",
    "remix_category:ignore_baked_lighting",
    "remix_category:ignore_transparency_layer",
    "remix_category:particle_emitter",
    "remix_category:smooth_normals",
    "remix_category:hair_cards",
  };
}
