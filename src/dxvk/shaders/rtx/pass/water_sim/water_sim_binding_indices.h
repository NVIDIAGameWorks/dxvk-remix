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

#define WATER_SIM_BINDING_STATE_INPUT      0
#define WATER_SIM_BINDING_STATE_OUTPUT     1
#define WATER_SIM_BINDING_DISPLAY_OUTPUT   2
#define WATER_SIM_BINDING_DISTURBANCES     3
#define WATER_SIM_BINDING_WAVES            4
#define WATER_SIM_BINDING_LAYER_INPUT      5
#define WATER_SIM_BINDING_LAYER_OUTPUT     6

// Wave generator, see water_sim_waves.comp.slang.
#define WATER_SIM_WAVES_BINDING_H0         0
#define WATER_SIM_WAVES_BINDING_SPECTRUM   1
#define WATER_SIM_WAVES_BINDING_TEMP       2
#define WATER_SIM_WAVES_BINDING_OUTPUT     3
#define WATER_SIM_WAVES_BINDING_MIP_INPUT  4
