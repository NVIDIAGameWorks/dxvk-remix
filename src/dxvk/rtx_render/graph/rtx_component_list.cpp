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

// Explicit template instantiations for flexible components.
// This file exists to avoid exceeding object file section limits when compiling rtx_graph_batch.cpp.

#include "components/clamp.h"
#include "components/conditionally_store.h"
#include "components/invert.h"
#include "components/loop.h"
#include "components/max.h"
#include "components/min.h"
#include "components/normalize.h"
#include "components/previous_frame_value.h"
#include "components/remap.h"
#include "components/select.h"
#include "components/smooth.h"
#include "components/velocity.h"
#include "components/vector_length.h"

namespace dxvk {
namespace components {

// Macro to instantiate all types supported by "Any" property type
#define INSTANTIATE_ANY_TYPES(ComponentClass) \
  template class ComponentClass<RtComponentPropertyType::Float>; \
  template class ComponentClass<RtComponentPropertyType::Float2>; \
  template class ComponentClass<RtComponentPropertyType::Float3>; \
  template class ComponentClass<RtComponentPropertyType::Float4>; \
  template class ComponentClass<RtComponentPropertyType::Bool>; \
  template class ComponentClass<RtComponentPropertyType::Enum>; \
  template class ComponentClass<RtComponentPropertyType::Hash>; \
  template class ComponentClass<RtComponentPropertyType::Prim>; \
  template class ComponentClass<RtComponentPropertyType::String>;

// Macro to instantiate all types supported by "NumberOrVector" property type
#define INSTANTIATE_NUMBER_OR_VECTOR_TYPES(ComponentClass) \
  template class ComponentClass<RtComponentPropertyType::Float>; \
  template class ComponentClass<RtComponentPropertyType::Float2>; \
  template class ComponentClass<RtComponentPropertyType::Float3>; \
  template class ComponentClass<RtComponentPropertyType::Float4>;

// Macro to instantiate vector types only (no Float)
#define INSTANTIATE_VECTOR_TYPES(ComponentClass) \
  template class ComponentClass<RtComponentPropertyType::Float2>; \
  template class ComponentClass<RtComponentPropertyType::Float3>; \
  template class ComponentClass<RtComponentPropertyType::Float4>;

// Components supporting "Any" type
INSTANTIATE_ANY_TYPES(ConditionallyStore)
INSTANTIATE_ANY_TYPES(PreviousFrameValue)
INSTANTIATE_ANY_TYPES(Select)

// Components supporting "NumberOrVector" type
INSTANTIATE_NUMBER_OR_VECTOR_TYPES(Clamp)
INSTANTIATE_NUMBER_OR_VECTOR_TYPES(Invert)
INSTANTIATE_NUMBER_OR_VECTOR_TYPES(Loop)
INSTANTIATE_NUMBER_OR_VECTOR_TYPES(Max)
INSTANTIATE_NUMBER_OR_VECTOR_TYPES(Min)
INSTANTIATE_NUMBER_OR_VECTOR_TYPES(Remap)
INSTANTIATE_NUMBER_OR_VECTOR_TYPES(Smooth)
INSTANTIATE_NUMBER_OR_VECTOR_TYPES(Velocity)

// Components supporting vector types only
INSTANTIATE_VECTOR_TYPES(Normalize)
INSTANTIATE_VECTOR_TYPES(VectorLength)

#undef INSTANTIATE_ANY_TYPES
#undef INSTANTIATE_NUMBER_OR_VECTOR_TYPES
#undef INSTANTIATE_VECTOR_TYPES

// Force the linker to include this object file by providing a function that gets called externally
void forceComponentListLinking() {
  // This function exists solely to ensure this object file is linked.
  // It's called from forceAllFlexibleComponentInstantiations() in rtx_component_list.h
}

}  // namespace components
}  // namespace dxvk

