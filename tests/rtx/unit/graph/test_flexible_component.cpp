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

#include "test_flexible_component.h"
#include "rtx_render/graph/rtx_graph_flexible_types.h"

namespace dxvk {
namespace components {

// Use the comparison macro for TestFlexAny (returns bool)
DEFINE_COMPARISON_OP_COMPONENT_CPP(TestFlexAny, (std::declval<A>() == std::declval<B>()), RtComponentPropertyAny)

// Use the binary operation macro for TestFlexNumberOrVector (returns computed type)
DEFINE_BINARY_OP_COMPONENT_CPP(TestFlexNumberOrVector, (std::declval<A>() + std::declval<B>()), RtComponentPropertyNumberOrVector)

}  // namespace components
}  // namespace dxvk

