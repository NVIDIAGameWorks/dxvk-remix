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
#ifndef SHARED_CONSTANTS_H
#define SHARED_CONSTANTS_H

// contains constants shared between shader and host code

static const uint8_t surfaceMaterialTypeOpaque = uint8_t(0u);
static const uint8_t surfaceMaterialTypeTranslucent = uint8_t(1u);
static const uint8_t surfaceMaterialTypeRayPortal = uint8_t(2u);
static const uint8_t surfaceMaterialTypeMask = uint8_t(0x3u);

#define COMMON_MATERIAL_FLAG_TYPE_MASK surfaceMaterialTypeMask
#define COMMON_MATERIAL_FLAG_TYPE_OFFSET(X) (2 + X)

// NOTE: Each material memory structure contains a set of flags.  The first 2 bits in that flag identify the material type (opaque, etc).
//       We must ensure all other material flags are written to byte addresses after these first two bits.  Use the COMMON_MATERIAL_FLAG_TYPE_OFFSET(x) 
//       macro, and ensure there is enough storage in the flags to represent desired bits accordingly.

// maximum value for thin film thickness in nanometers
#define OPAQUE_SURFACE_MATERIAL_THIN_FILM_MAX_THICKNESS (1500.0f)
// bits for flags field in OpaqueSurfaceMaterial
#define OPAQUE_SURFACE_MATERIAL_FLAG_USE_THIN_FILM_LAYER (1 << COMMON_MATERIAL_FLAG_TYPE_OFFSET(0))
#define OPAQUE_SURFACE_MATERIAL_FLAG_ALPHA_IS_THIN_FILM_THICKNESS (1 << COMMON_MATERIAL_FLAG_TYPE_OFFSET(1))
#define OPAQUE_SURFACE_MATERIAL_FLAG_IGNORE_ALPHA_CHANNEL (1 << COMMON_MATERIAL_FLAG_TYPE_OFFSET(2))
#define OPAQUE_SURFACE_MATERIAL_FLAG_IS_RAYTRACED_RENDER_TARGET COMMON_MATERIAL_FLAG_TYPE_OFFSET(3)


#define OPAQUE_SURFACE_MATERIAL_INTERACTION_FLAG_HAS_HEIGHT_TEXTURE (1 << 0)
#define OPAQUE_SURFACE_MATERIAL_INTERACTION_FLAG_USE_THIN_FILM_LAYER (1 << 1)
// flags overlap with type field when in gbuffer, which occupies last 2 bits.
#define OPAQUE_SURFACE_MATERIAL_INTERACTION_FLAG_MASK 0x3F


// Note: Bits for flags field in TranslucentSurfaceMaterial and TranslucentSurfaceMaterialInteraction
// If set, then the texture bound to transmittanceOrDiffuseTextureIndex is an albedo map for the diffuse layer
#define TRANSLUCENT_SURFACE_MATERIAL_FLAG_USE_DIFFUSE_LAYER (1 << COMMON_MATERIAL_FLAG_TYPE_OFFSET(0))

// bits for flags field in SubsurfaceMaterial
#define SUBSURFACE_MATERIAL_FLAG_HAS_TRANSMITTANCE_TEXTURE            (1 << COMMON_MATERIAL_FLAG_TYPE_OFFSET(0))
#define SUBSURFACE_MATERIAL_FLAG_HAS_THICKNESS_TEXTURE                (1 << COMMON_MATERIAL_FLAG_TYPE_OFFSET(1))
#define SUBSURFACE_MATERIAL_FLAG_HAS_SINGLE_SCATTERING_ALBEDO_TEXTURE (1 << COMMON_MATERIAL_FLAG_TYPE_OFFSET(2))


#endif // ifndef SHARED_CONSTANTS_H
