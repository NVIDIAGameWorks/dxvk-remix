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

// List all components in alphabetical order
#include "components/add.h"
#include "components/angle_to_mesh.h"
#include "components/between.h"
#include "components/bool_and.h"
#include "components/bool_not.h"
#include "components/bool_or.h"
#include "components/camera.h"
#include "components/ceil.h"
#include "components/clamp.h"
#include "components/compose_vector2.h"
#include "components/compose_vector3.h"
#include "components/compose_vector4.h"
#include "components/conditionally_store.h"
#include "components/const_asset_path.h"
#include "components/const_bool.h"
#include "components/const_color3.h"
#include "components/const_color4.h"
#include "components/const_float.h"
#include "components/const_float2.h"
#include "components/const_float3.h"
#include "components/const_float4.h"
#include "components/const_hash.h"
#include "components/const_prim.h"
#include "components/const_string.h"
#include "components/count_toggles.h"
#include "components/counter.h"
#include "components/decompose_vector2.h"
#include "components/decompose_vector3.h"
#include "components/decompose_vector4.h"
#include "components/divide.h"
#include "components/equal_to.h"
#include "components/floor.h"
#include "components/fog_hash_checker.h"
#include "components/greater_than.h"
#include "components/invert.h"
#include "components/keyboard_input.h"
#include "components/less_than.h"
#include "components/light_hash_checker.h"
#include "components/loop.h"
#include "components/max.h"
#include "components/mesh_hash_checker.h"
#include "components/mesh_proximity.h"
#include "components/min.h"
#include "components/multiply.h"
#include "components/normalize.h"
#include "components/previous_frame_value.h"
#include "components/ray_mesh_intersection.h"
#include "components/read_bone_transform.h"
#include "components/read_transform.h"
#include "components/remap.h"
#include "components/round.h"
#include "components/rtx_option_layer_action.h"
#include "components/rtx_option_layer_sensor.h"
#include "components/rtx_option_read_bool.h"
#include "components/rtx_option_read_color3.h"
#include "components/rtx_option_read_color4.h"
#include "components/rtx_option_read_number.h"
#include "components/rtx_option_read_vector2.h"
#include "components/rtx_option_read_vector3.h"
#include "components/select.h"
#include "components/smooth.h"
#include "components/sphere_light_override.h"
#include "components/subtract.h"
#include "components/texture_hash_checker.h"
#include "components/time.h"
#include "components/toggle.h"
#include "components/vector_length.h"
#include "components/velocity.h"

namespace dxvk {
namespace components {

// Forward declaration for function in rtx_component_list.cpp
void forceComponentListLinking();

// Call all type variant creation functions to ensure static initializers run
inline void forceAllFlexibleComponentInstantiations() {
  // Force linking of rtx_component_list.cpp which contains explicit template instantiations
  // for components with one shared flexible type.
  forceComponentListLinking();

  // Components that have multiple flexible types that can be different
  // (i.e. Vector2 * float) each get their own cpp file that needs to be linked.
  // Binary operations
  createTypeVariantsForAdd();
  createTypeVariantsForDivide();
  createTypeVariantsForMultiply();
  createTypeVariantsForSubtract();
  
  // Comparison operations
  createTypeVariantsForEqualTo();
  
}
}  // namespace components
}  // namespace dxvk

// TODO figure out how to include components without needing to include a header file and manual instantiation in a list like this.

