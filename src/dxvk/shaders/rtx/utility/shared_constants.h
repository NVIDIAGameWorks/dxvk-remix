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

// maximum value for thin film thickness in nanometers
#define OPAQUE_SURFACE_MATERIAL_THIN_FILM_MAX_THICKNESS (1500.0f)
// bits for flags field in OpaqueSurfaceMaterial
#define OPAQUE_SURFACE_MATERIAL_FLAG_USE_THIN_FILM_LAYER (1 << 0)
#define OPAQUE_SURFACE_MATERIAL_FLAG_ALPHA_IS_THIN_FILM_THICKNESS (1 << 1)
#define OPAQUE_SURFACE_MATERIAL_FLAG_HAS_ALBEDO_TEXTURE (1 << 2)
#define OPAQUE_SURFACE_MATERIAL_FLAG_HAS_ROUGHNESS_TEXTURE (1 << 3)
#define OPAQUE_SURFACE_MATERIAL_FLAG_HAS_METALLIC_TEXTURE (1 << 4)
#define OPAQUE_SURFACE_MATERIAL_FLAG_HAS_EMISSIVE_TEXTURE (1 << 5)
#define OPAQUE_SURFACE_MATERIAL_FLAG_HAS_SUBSURFACE_MATERIAL (1 << 6)

#define OPAQUE_SURFACE_MATERIAL_INTERACTION_FLAG_HAS_HEIGHT_TEXTURE (1 << 0)
#define OPAQUE_SURFACE_MATERIAL_INTERACTION_FLAG_USE_THIN_FILM_LAYER (1 << 1)
// flags overlap with type field when in gbuffer, which occupies last 2 bits.
#define OPAQUE_SURFACE_MATERIAL_INTERACTION_FLAG_MASK 0x3F


// Note: Bits for flags field in TranslucentSurfaceMaterial and TranslucentSurfaceMaterialInteraction
// If set, then the texture bound to transmittanceOrDiffuseTextureIndex is an albedo map for the diffuse layer
#define TRANSLUCENT_SURFACE_MATERIAL_FLAG_USE_DIFFUSE_LAYER (1 << 0)

// bits for flags field in SubsurfaceMaterial
#define SUBSURFACE_MATERIAL_FLAG_HAS_TRANSMITTANCE_TEXTURE            (1 << 0)
#define SUBSURFACE_MATERIAL_FLAG_HAS_THICKNESS_TEXTURE                (1 << 1)
#define SUBSURFACE_MATERIAL_FLAG_HAS_SINGLE_SCATTERING_ALBEDO_TEXTURE (1 << 2)

#endif // ifndef SHARED_CONSTANTS_H
