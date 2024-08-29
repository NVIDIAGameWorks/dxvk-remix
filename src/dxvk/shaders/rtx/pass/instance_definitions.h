/*
* Copyright (c) 2021-2023, NVIDIA CORPORATION. All rights reserved.
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

/******************************  Instance Mask - Ordered TLAS ************************************************/

// The view model and player model instance masks represent special geometry that has
// custom visibility rules spread through the resolver code functions. 
// Definitions:
// - Real view model is the gun that follows the view
// - Virtual view model is a copy of that gun at the other end of the portal the player is crossing
// - Real player model is the player model at the camera location
// - Virtual player model is a copy of that model at the other end of the portal the player is crossing
//
// For primary rays before entering any portals:
// - Real view model is visible
// - Virtual view model is hidden
// - Real player model is only visible if raytraceArgs.enablePlayerModelInPrimarySpace is true
// - Virtual player model is visible
//
// For primary rays after entering the portal specified in raytraceArgs.viewModelVirtualPortalIndex:
// - Real view model is hidden
// - Virtual view model is visible
// - Real player model is visible
// - Virtual player model is only visible if raytraceArgs.enablePlayerModelInPrimarySpace is true
//
// For primary rays after any other portal:
// - Real and virtual view models are hidden
// - Real and virtual player models are visible
//
// Secondary rays, including PSR, follow mostly the same portal transition rules, but they start tracing
// with a ray mask that is derived from the ray mask that found the originating surface, with some
// rules around view models. Also, the PSR reflection rays will include the primary player model,
// while PSR transmission rays will not.
// 
// - Real or virtual view model is visible only if the originating surface is a view model
// - Real or virtual player model is visible only if the originating surface was found by a ray
//   that included that player model version. This lets us control whether we see shadows or reflections
//   of the primary player model independently from its visibility on screen and independently from
//   other copies of the player model visible through portals.
//
// To facilitate the implementation of this originating surface rule, the corresponding rayMask is 
// stored as GeometryFlags.objectMask. Only the upper 4 bits of it are actually used though,
// the rest is determined by the specific pass or ray type.
//
// For the portal transition rules, see updateStateOnPortalCrossing(...) in resolve.slangh


// Note: if there's an absolute need for additional instance mask bits in the future
// ViewModel instances could be built into their own TLAS and their two bits can be repurposed

#define OBJECT_MASK_TRANSLUCENT       (1 << 0)
#define OBJECT_MASK_PORTAL            (1 << 1)
#define OBJECT_MASK_OPAQUE            (1 << 3)

// Instances to be drawn and visible in ViewModel pass only
#define OBJECT_MASK_VIEWMODEL         (1 << 4)
#define OBJECT_MASK_VIEWMODEL_VIRTUAL (1 << 5) // ViewModel virtual instances visible in immediate portal X space,
                                               // where X is value of a portal for which the instances were generated this frame
                                               // and passed in via a constant buffer
#define OBJECT_MASK_ALL_VIEWMODEL (OBJECT_MASK_VIEWMODEL | OBJECT_MASK_VIEWMODEL_VIRTUAL)

#define OBJECT_MASK_PLAYER_MODEL         (1 << 6) 
#define OBJECT_MASK_PLAYER_MODEL_VIRTUAL (1 << 7) 

#define OBJECT_MASK_ALL_PLAYER_MODEL  (OBJECT_MASK_PLAYER_MODEL | OBJECT_MASK_PLAYER_MODEL_VIRTUAL)

// All objects with custom visibility rules
#define OBJECT_MASK_ALL_DYNAMIC       (OBJECT_MASK_ALL_VIEWMODEL | OBJECT_MASK_ALL_PLAYER_MODEL)
#define OBJECT_MASK_ALL_DYNAMIC_FIRST_ACTIVE_BIT_OFFSET 4
#define OBJECT_MASK_ALL_DYNAMIC_NUMBER_OF_ACTIVE_BITS   4

// Note: Sky excluded as often it should not be traced against when calculating visibility.
//       ViewModel is excluded
#define OBJECT_MASK_ALL_STANDARD    (OBJECT_MASK_TRANSLUCENT | OBJECT_MASK_PORTAL | OBJECT_MASK_OPAQUE)
#define OBJECT_MASK_ALL             (OBJECT_MASK_ALL_STANDARD)

/****************************** ~Instance Mask - Ordered TLAS ************************************************/


/******************************  Instance Mask - Unordered TLAS **********************************************/

// Unordered TLAS has a separate set of the lower 4 bits from the regular, ordered TLAS.
// The upper 4 bits are the same - view model and player model.
// The lower 4 bits have un-obvious meanings to get the necessary behavior with the number of bits that we have.
// 
// Each translucent instance starts with 2 of these bits set.
// For emissive geometry, the EMISSIVE_GEOMETRY and EMISSIVE_INTERSECTION_PRIMITIVE bits are set.
// For non-emissive geometry, the BLENDED_GEOMETRY and BLENDED_INTERSECTION_PRIMITIVE bits are set.
// Later, intersection billboards or beams can be generated from *some* of the instances. 
// In case intersection primitives are generated, the original instance loses the _INTERSECTION_PRIMITIVE bits,
// and the intersection primitive inherits the original instance mask without the _GEOMETRY bits.
// - Primary rays with the original direction use the ALL_GEOMETRY mask, thereby ignoring intersection primitives.
// - Primary rays with an altered direction use the ALL_INTERSECTION_PRIMITIVE mask, thereby ignoring geometry
//   that generated the intersection primitives, but keeping the geometry that did not generate any such primitives
//   because that geometry still has both _GEOMETRY and _INTERSECTION_PRIMITIVE bits set.
// - Secondary rays use the BLENDED_INTERSECTION_PRIMITIVE mask optionally combined with EMISSIVE_INTERSECTION_PRIMITIVE,
//   depending on cb.enableUnorderedEmissiveParticlesInIndirectRays
//
// View model and player model have no duality in their translucent geometry:
// - View model has only geometric translucency that uses OBJECT_MASK_VIEWMODEL[_VIRTUAL]
// - Player model has only intersection primitive translucency that uses OBJECT_MASK_PLAYER_MODEL[_VIRTUAL]
//   The intersection primitive translucency on the player model is included in all primary rays, which is why we cannot just
//   ignore the intersection primitives in primary rays anymore.

#define OBJECT_MASK_UNORDERED_EMISSIVE_GEOMETRY                 (1 << 0)
#define OBJECT_MASK_UNORDERED_BLENDED_GEOMETRY                  (1 << 1)
#define OBJECT_MASK_UNORDERED_EMISSIVE_INTERSECTION_PRIMITIVE   (1 << 2)
#define OBJECT_MASK_UNORDERED_BLENDED_INTERSECTION_PRIMITIVE    (1 << 3)
#define OBJECT_MASK_UNORDERED_ALL_EMISSIVE                      (OBJECT_MASK_UNORDERED_EMISSIVE_GEOMETRY | OBJECT_MASK_UNORDERED_EMISSIVE_INTERSECTION_PRIMITIVE)
#define OBJECT_MASK_UNORDERED_ALL_BLENDED                       (OBJECT_MASK_UNORDERED_BLENDED_GEOMETRY | OBJECT_MASK_UNORDERED_BLENDED_INTERSECTION_PRIMITIVE)
#define OBJECT_MASK_UNORDERED_ALL_GEOMETRY                      (OBJECT_MASK_UNORDERED_EMISSIVE_GEOMETRY | OBJECT_MASK_UNORDERED_BLENDED_GEOMETRY)
#define OBJECT_MASK_UNORDERED_ALL_INTERSECTION_PRIMITIVE        (OBJECT_MASK_UNORDERED_EMISSIVE_INTERSECTION_PRIMITIVE | OBJECT_MASK_UNORDERED_BLENDED_INTERSECTION_PRIMITIVE)
#define OBJECT_MASK_ALL_UNORDERED                               (OBJECT_MASK_UNORDERED_ALL_EMISSIVE | OBJECT_MASK_UNORDERED_ALL_BLENDED)

/****************************** ~Instance Mask - Unordered TLAS **********************************************/


// Custom Index encoding
#define CUSTOM_INDEX_IS_VIEW_MODEL     (1 << 23)
#define CUSTOM_INDEX_MATERIAL_TYPE_BIT (21)
#define CUSTOM_INDEX_SURFACE_MASK      ((1 << CUSTOM_INDEX_MATERIAL_TYPE_BIT) - 1)

