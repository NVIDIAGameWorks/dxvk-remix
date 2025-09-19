/*
Copyright (c) 2024, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/
#pragma once

// Use 16bit packing since NRC data is stored in the payload. 
// It makes 0 perceivable quality difference in practice
#define NRC_PACK_PATH_16BITS 1

#ifndef NRC_USE_CUSTOM_BUFFER_ACCESSORS
#define NRC_USE_CUSTOM_BUFFER_ACCESSORS 0
#endif

// ToDo remove
#if !defined(NRC_RW_STRUCTURED_BUFFER)
#define NRC_RW_STRUCTURED_BUFFER(T) RWStructuredBuffer<T>
#endif

// Set to 8K as a decent upper ray tracing resolution cap that should never be hit in practical scenarios
#define NRC_MAX_RAYTRACING_RESOLUTION_X 7680
#define NRC_MAX_RAYTRACING_RESOLUTION_Y 4320
#define NRC_MAX_SAMPLES_PER_PIXEL 1

// Calculated using formula in NrcPathState's preamble
#define NRC_NUM_PATH_STATE_QUERY_BUFFER_INDEX_VALUES \
  NRC_MAX_RAYTRACING_RESOLUTION_X * NRC_MAX_RAYTRACING_RESOLUTION_Y * NRC_MAX_SAMPLES_PER_PIXEL + \
  NRC_MAX_RAYTRACING_RESOLUTION_X * NRC_MAX_RAYTRACING_RESOLUTION_Y

// Limit number of allowed bits to encode a few custom bits into the encoded 32bit queryBufferIndex variable
#define NRC_MAX_REQUIRED_BITS_IN_PATH_STATE_QUERY_BUFFER_INDEX 26
#define NRC_QUERY_BUFFER_INDEX_MASK ((1 << NRC_MAX_REQUIRED_BITS_IN_PATH_STATE_QUERY_BUFFER_INDEX) - 1)

#if (NRC_NUM_PATH_STATE_QUERY_BUFFER_INDEX_VALUES >= (1 << NRC_MAX_REQUIRED_BITS_IN_PATH_STATE_QUERY_BUFFER_INDEX))
 Error. NrcPathState::queryBufferIndex limits reached. 
#endif