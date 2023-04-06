/*
* Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.
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

// The main reason for per material hitGroups is because rayPortal material shader path
// strains the resolve for other materials. Therefore rayPortal is split away into its own hitGroup for
// more optimized code for each path 
#define HIT_GROUP_MATERIAL_OPAQUE_TRANSLUCENT 0
#define HIT_GROUP_MATERIAL_RAYPORTAL 1

#define HIT_GROUP_MATERIAL_COUNT 2

#define ADD_HIT_GROUPS(ShaderClass, variant_prefix) \
  shaders.addHitGroup(GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, ShaderClass, variant_prefix ## _material_opaque_translucent_closestHit), nullptr, nullptr); \
  shaders.addHitGroup(GET_SHADER_VARIANT(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, ShaderClass, variant_prefix ## _material_rayportal_closestHit), nullptr, nullptr); \

// Material resolves to be included in shader code paths set by
// #define SURFACE_MATERIAL_RESOLVE_TYPE_ACTIVE_MASK 
// to some or a combination of below
#define SURFACE_MATERIAL_RESOLVE_TYPE_OPAQUE        (1 << 0)
#define SURFACE_MATERIAL_RESOLVE_TYPE_TRANSLUCENT   (1 << 1)
#define SURFACE_MATERIAL_RESOLVE_TYPE_RAY_PORTAL    (1 << 2)

#define SURFACE_MATERIAL_RESOLVE_TYPE_ALL           (SURFACE_MATERIAL_RESOLVE_TYPE_OPAQUE | SURFACE_MATERIAL_RESOLVE_TYPE_TRANSLUCENT | SURFACE_MATERIAL_RESOLVE_TYPE_RAY_PORTAL)
#define SURFACE_MATERIAL_RESOLVE_TYPE_OPAQUE_TRANSLUCENT (SURFACE_MATERIAL_RESOLVE_TYPE_OPAQUE | SURFACE_MATERIAL_RESOLVE_TYPE_TRANSLUCENT)