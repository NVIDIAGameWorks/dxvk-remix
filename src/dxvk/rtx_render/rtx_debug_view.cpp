/*
* Copyright (c) 2022-2025, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx_debug_view.h"
#include "dxvk_device.h"
#include "dxvk_context.h"
#include "dxvk_objects.h"
#include "rtx/utility/shader_types.h"
#include "rtx/external/turbo_colormap.h"
#include "rtx/utility/debug_view_indices.h"
#include "rtx/pass/debug_view/debug_view_binding_indices.h"
#include "rtx/pass/debug_view/debug_view_waveform_render_binding_indices.h"
#include "rtx/pass/debug_view/debug_view_args.h"
#include "rtx_render/rtx_shader_manager.h"
#include "rtx_imgui.h"
#include "dxvk_scoped_annotation.h"
#include "rtx_context.h"
#include "rtx_terrain_baker.h"
#include "rtx_neural_radiance_cache.h"
#include "rtx_nrd_context.h"

#include <rtx_shaders/debug_view.h>
#include <rtx_shaders/debug_view_using_optional_extensions.h>
#include <rtx_shaders/debug_view_waveform_render.h>

#include "rtx_options.h"

namespace dxvk {
  static const bool s_disableAnimation = (env::getEnvVar("DXVK_DEBUG_VIEW_DISABLE_ANIMATION") == "1");

  static const auto colormap0 = turboColormap(0.0f);
  static const auto colormap25= turboColormap(0.25f);
  static const auto colormap50 = turboColormap(0.5f);
  static const auto colormap75 = turboColormap(0.75f);
  static const auto colormap100 = turboColormap(1.0f);

  ImGui::ComboWithKey<uint32_t>::ComboEntries debugViewEntries = { {
        {DEBUG_VIEW_PRIMITIVE_INDEX, "Primitive Index"},
        {DEBUG_VIEW_GEOMETRY_HASH, "Geometry Hash"},
        {DEBUG_VIEW_CUSTOM_INDEX, "Custom Index"},
        {DEBUG_VIEW_BARYCENTRICS, "Barycentric Coordinates"},
        {DEBUG_VIEW_IS_FRONT_HIT, "Is Front Hit"},
        {DEBUG_VIEW_IS_STATIC, "Is Static"},
        {DEBUG_VIEW_IS_OPAQUE, "Is Opaque"},
        {DEBUG_VIEW_IS_THIN_OPAQUE, "Is Thin Opaque"},
        {DEBUG_VIEW_IS_SUBSURFACE_SCATTERING, "Is Subsurface Scattering (SSS)"},
        {DEBUG_VIEW_IS_DIRECTION_ALTERED, "Is Direction Altered"},
        {DEBUG_VIEW_IS_EMISSIVE_BLEND, "Is Emissive Blend"},
        {DEBUG_VIEW_IS_EMISSIVE, "Is Emissive"},
        {DEBUG_VIEW_IS_PARTICLE, "Is Particle"},
        {DEBUG_VIEW_VIEW_DIRECTION, "View Direction"},
        {DEBUG_VIEW_CONE_RADIUS, "Cone Radius"},

        {DEBUG_VIEW_POSITION, "Position"},
        {DEBUG_VIEW_TEXCOORDS, "Texture Coordinates"},
        {DEBUG_VIEW_TEXCOORDS_GRADIENT_X, "Texture Coordinates Gradient X"},
        {DEBUG_VIEW_TEXCOORDS_GRADIENT_Y, "Texture Coordinates Gradient Y"},
        {DEBUG_VIEW_TEXCOORD_GENERATION_MODE, "Texture Coordinates Generation Mode"},
        {DEBUG_VIEW_VIRTUAL_MOTION_VECTOR, "Virtual Motion Vector"},
        {DEBUG_VIEW_SCREEN_SPACE_MOTION_VECTOR, "Screen-Space Motion Vector"},
        {DEBUG_VIEW_TRIANGLE_NORMAL, "Triangle Normal"},
        {DEBUG_VIEW_TRIANGLE_TANGENT, "Triangle Tangent"},
        {DEBUG_VIEW_TRIANGLE_BITANGENT, "Triangle Bitangent"},
        {DEBUG_VIEW_INTERPOLATED_NORMAL, "Interpolated Normal"},
        {DEBUG_VIEW_INTERPOLATED_TANGENT, "Interpolated Tangent"},
        {DEBUG_VIEW_INTERPOLATED_BITANGENT, "Interpolated Bitangent"},
        {DEBUG_VIEW_SHADING_NORMAL, "Shading Normal"},
        {DEBUG_VIEW_VIRTUAL_SHADING_NORMAL, "Virtual Shading Normal"},
        {DEBUG_VIEW_VERTEX_COLOR, "Vertex Color"},
        {DEBUG_VIEW_PORTAL_SPACE, "Portal Space"},

        {DEBUG_VIEW_MATERIAL_TYPE, "Material Type"},
        {DEBUG_VIEW_ALBEDO, "Diffuse Albedo"},
        {DEBUG_VIEW_RAW_ALBEDO, "Diffuse Raw Albedo"},
        {DEBUG_VIEW_OPAQUE_RAW_ALBEDO_RESOLUTION_CHECKERS, "Opaque Material Raw Albedo + Texture Resolution Checkers",
                                                    "Parameterize via:\n"
                                                    "Debug Knob [0]: num texels per checker box [Default: 64]\n"
                                                    "Debug Knob [1]: checkers overlay strength [Default: 0.5]"},
        {DEBUG_VIEW_OPAQUE_NORMAL_RESOLUTION_CHECKERS, "Opaque Material Normal + Texture Resolution Checkers",
                                                    "Parameterize via:\n"
                                                    "Debug Knob [0]: num texels per checker box [Default: 64]\n"
                                                    "Debug Knob [1]: checkers overlay strength [Default: 0.5]"},
        {DEBUG_VIEW_OPAQUE_ROUGHNESS_RESOLUTION_CHECKERS, "Opaque Material Roughness + Texture Resolution Checkers",
                                                    "Parameterize via:\n"
                                                    "Debug Knob [0]: num texels per checker box [Default: 64]\n"
                                                    "Debug Knob [1]: checkers overlay strength [Default: 0.5]"},
        {DEBUG_VIEW_BASE_REFLECTIVITY, "Base Reflectivity"},
        {DEBUG_VIEW_ROUGHNESS, "Isotropic Roughness"},
        {DEBUG_VIEW_PERCEPTUAL_ROUGHNESS, "Perceptual Roughness"},
        {DEBUG_VIEW_ANISOTROPY, "Anisotropy"},
        {DEBUG_VIEW_ANISOTROPIC_ROUGHNESS, "Anisotropic Roughness"},
        {DEBUG_VIEW_OPACITY, "Opacity"},
        {DEBUG_VIEW_EMISSIVE_RADIANCE, "Emissive Radiance"},
        {DEBUG_VIEW_EMISSIVE_TRIANGLE_INTENSITY, "Emissive Triangle Intensity"},
        {DEBUG_VIEW_SURFACE_AREA, "Surface Area"},
        {DEBUG_VIEW_THIN_FILM_THICKNESS, "Thin Film Thickness"},
        {DEBUG_VIEW_EMISSIVE_PARTICLE, "Emissive Particle"},

        {DEBUG_VIEW_IS_BAKED_TERRAIN, "Terrain: Is Baked Terrain"},
        {DEBUG_VIEW_TERRAIN_MAP, "Terrain: Cascade Map",
                                "Parameterize via:\n"
                                "Debug Knob [0]: (rounded down) which texture type to show: \n"
                                "0: AlbedoOpacity, 1: Normal, 2: Tangent, 3: Height,\n"
                                "4: Roughness, 5: Metallic, 6: Emissive"},
        {DEBUG_VIEW_TERRAIN_MAP_OPACITY, "Terrain: Cascade Map Opacity",
                                "Parameterize via:\n"
                                "Debug Knob [0]: (rounded down) which texture type to show: \n"
                                "0: AlbedoOpacity, 1: Normal, 2: Tangent, 3: Height,\n"
                                "4: Roughness, 5: Metallic, 6: Emissive"},
        {DEBUG_VIEW_CASCADE_LEVEL, "Terrain: Cascade Level"},

        {DEBUG_VIEW_VIRTUAL_HIT_DISTANCE, "Virtual Hit Distance"},
        {DEBUG_VIEW_PRIMARY_DEPTH, "Primary Depth"},

        {DEBUG_VIEW_SHARED_BIAS_CURRENT_COLOR_MASK, "DLSS Bias Color Mask"},

        {DEBUG_VIEW_IS_INSIDE_FRUSTUM, "Is Inside Frustum"},
        {DEBUG_VIEW_IS_OUTSIDE_AABB, "Is Outside Axis Aligned Bounding Box",
                                    "Legend: Black - inside, Combination of Red|Green|Blue - outside x|y|z axis\n"
                                    "The Bounding Box is centered around the camera.\n"
                                    "Parameterize via:\n"
                                    "\tDebug Knob [0].xyz: {width, depth, height} of the Bounding Box"},
    
        {DEBUG_VIEW_BLUE_NOISE, "Blue Noise"},
        {DEBUG_VIEW_PIXEL_CHECKERBOARD, "Pixel Checkerboard"},
        {DEBUG_VIEW_VOLUME_RADIANCE_DEPTH_LAYERS, "Volume Radiance Depth Layers",
                                                    "Parameterize via:\n"
                                                    "Debug Knob [0]: Value selects mode: 1) average irradiance, 2) luminance DC component, 3) luminance linear coefficients, 4) chrominance values"},
        {DEBUG_VIEW_SURFACE_VOLUME_RADIANCE, "Surface Volume Radiance"},
        {DEBUG_VIEW_VOLUME_RESERVOIR_DEPTH_LAYERS, "Volume Reservoir Depth Layers"},
        {DEBUG_VIEW_VOLUME_PREINTEGRATION, "Volume Preintegration Result"},

        {DEBUG_VIEW_COMPOSITE_OUTPUT, "Composite Output"},

        {DEBUG_VIEW_LOCAL_TONEMAPPER_LUMINANCE_OUTPUT, "Local Tonemapper Luminance Output"},
        {DEBUG_VIEW_LOCAL_TONEMAPPER_EXPOSURE_OUTPUT, "Local Tonemapper Blend Weight"},
        {DEBUG_VIEW_LOCAL_TONEMAPPER_BLEND_OUTPUT, "Local Tonemapper Assembled Exposure"},
        {DEBUG_VIEW_LOCAL_TONEMAPPER_FINAL_COMBINE_OUTPUT, "Local Tonemapper Final Multiplier"},

        {DEBUG_VIEW_POST_TONEMAP_OUTPUT, "Final Output"},
        {DEBUG_VIEW_PRE_TONEMAP_OUTPUT, "Final Output (Pre Tonemap)"},
        {DEBUG_VIEW_EXPOSURE_HISTOGRAM, "Exposure Histogram"},

        {DEBUG_VIEW_VIEW_MODEL, "View Model: Final Output"},
        {DEBUG_VIEW_RESTIR_GI_INITIAL_SAMPLE, "ReSTIR GI Initial Sample"},
        {DEBUG_VIEW_RESTIR_GI_TEMPORAL_REUSE, "ReSTIR GI Temporal Reprojection"},
        {DEBUG_VIEW_RESTIR_GI_SPATIAL_REUSE, "ReSTIR GI Spatial Reuse"},
        {DEBUG_VIEW_RESTIR_GI_FINAL_SHADING, "ReSTIR GI Final Shading MIS Weight"},
        {DEBUG_VIEW_RESTIR_GI_VIRTUAL_HIT_T, "ReSTIR GI Virtual Hit Distance"},
        {DEBUG_VIEW_RESTIR_GI_VISIBILITY_INVALID_SAMPLES, "ReSTIR GI Visibility Invalid Samples"},

        {DEBUG_VIEW_NEE_CACHE_LIGHT_HISTOGRAM, "NEE Cache Light Histogram"},
        {DEBUG_VIEW_NEE_CACHE_HISTOGRAM, "NEE Cache Triangle Histogram"},
        {DEBUG_VIEW_NEE_CACHE_HASH_MAP, "NEE Cache Hash Map"},
        {DEBUG_VIEW_NEE_CACHE_ACCUMULATE_MAP, "NEE Cache Accumulate Map"},
        {DEBUG_VIEW_NEE_CACHE_SAMPLE_RADIANCE, "NEE Cache Sample Radiance"},
        {DEBUG_VIEW_NEE_CACHE_TASK, "NEE Cache Task"},
        {DEBUG_VIEW_NEE_CACHE_TRIANGLE_CANDIDATE, "NEE Cache Triangle Candidate"},

        {DEBUG_VIEW_RTXDI_GRADIENTS, "RTXDI Gradients"},
        {DEBUG_VIEW_RTXDI_CONFIDENCE, "RTXDI Confidence"},

        {DEBUG_VIEW_STOCHASTIC_ALPHA_BLEND_COLOR, "Stochastic Alpha Blend Color"},
        {DEBUG_VIEW_STOCHASTIC_ALPHA_BLEND_NORMAL, "Stochastic Alpha Blend Normal"},
        {DEBUG_VIEW_STOCHASTIC_ALPHA_BLEND_GEOMETRY_HASH, "Stochastic Alpha Blend Geometry Hash"},
        {DEBUG_VIEW_STOCHASTIC_ALPHA_BLEND_BACKGROUND_TRANSPARENCY, "Stochastic Alpha Blend Background Transparency"},

        {DEBUG_VIEW_RAY_RECONSTRUCTION_PARTICLE_LAYER, "DLSS-RR Particle Layer"},
        {DEBUG_VIEW_RAY_RECONSTRUCTION_PARTICLE_LAYER_ALPHA, "DLSS-RR Particle Layer Alpha"},
        {DEBUG_VIEW_RAY_RECONSTRUCTION_DIFFUSE_ALBEDO, "DLSS-RR Diffuse Albedo"},
        {DEBUG_VIEW_RAY_RECONSTRUCTION_SPECULAR_ALBEDO, "DLSS-RR Specular Albedo"},
        {DEBUG_VIEW_RAY_RECONSTRUCTION_HIT_DISTANCE, "DLSS-RR Hit Distance"},
        {DEBUG_VIEW_RAY_RECONSTRUCTION_PRIMARY_DEPTH, "DLSS-RR Depth"},
        {DEBUG_VIEW_RAY_RECONSTRUCTION_PRIMARY_WORLD_SHADING_NORMAL, "DLSS-RR Normal"},
        {DEBUG_VIEW_RAY_RECONSTRUCTION_PRIMARY_SCREEN_SPACE_MOTION_VECTOR, "DLSS-RR Motion Vector"},
        {DEBUG_VIEW_RAY_RECONSTRUCTION_PRIMARY_DISOCCLUSION_THRESHOLD_MIX, "DLSS-RR Disocclusion Mask"},

        {DEBUG_VIEW_GEOMETRY_FLAGS_FIRST_SAMPLED_LOBE_IS_SPECULAR, "Geometry Flags: First Sampled Lobe Is Specular"},
        {DEBUG_VIEW_INTEGRATE_INDIRECT_FIRST_RAY_THROUGHPUT, "Indirect First Ray Throughput"},
        {DEBUG_VIEW_INTEGRATE_INDIRECT_FIRST_SAMPLED_LOBE_PDF, "Indirect First Sampled Lobe Pdf"},
        {DEBUG_VIEW_INTEGRATE_INDIRECT_FIRST_SAMPLED_SOLID_ANGLE_PDF, "Indirect First Sampled Solid Angle Pdf"},

        {DEBUG_VIEW_PRIMARY_RAY_INTERACTIONS, "Primary Ray Interactions"},
        {DEBUG_VIEW_SECONDARY_RAY_INTERACTIONS, "Secondary Ray Interactions"},
        {DEBUG_VIEW_PRIMARY_RAY_BOUNCES, "Primary Ray Bounces"},
        {DEBUG_VIEW_SECONDARY_RAY_BOUNCES, "Secondary Ray Bounces"},
        {DEBUG_VIEW_PRIMARY_UNORDERED_INTERACTIONS, "Primary Unordered Ray Interactions"},
        {DEBUG_VIEW_SECONDARY_UNORDERED_INTERACTIONS, "Secondary Unordered Ray Interactions"},

        {DEBUG_VIEW_PRIMARY_RAY_AND_UNORDERED_INTERACTIONS, "Primary Ray Interactions (+Unordered)"},
        {DEBUG_VIEW_SECONDARY_RAY_AND_UNORDERED_INTERACTIONS, "Secondary Ray Interactions (+Unordered)"},

        {DEBUG_VIEW_PSR_PRIMARY_SECONDARY_SURFACE_MASK, "PSR Primary Secondary Surface Mask"},
        {DEBUG_VIEW_PSR_SELECTED_INTEGRATION_SURFACE_PDF, "PSR Selected Integration Surface PDF"},

        {DEBUG_VIEW_PRIMARY_USE_ALTERNATE_DISOCCLUSION_THRESHOLD, "Primary Use Alternate Disocclusion Threshold"},

        {DEBUG_VIEW_PRIMARY_DECAL_ALBEDO,                  "Primary Decal Albedo" },

        {DEBUG_VIEW_PRIMARY_SPECULAR_ALBEDO,               "Primary Specular Albedo"},
        {DEBUG_VIEW_SECONDARY_SPECULAR_ALBEDO,               "Secondary Specular Albedo"},

        {DEBUG_VIEW_NOISY_PRIMARY_DIRECT_DIFFUSE_RADIANCE,               "Primary Direct Diffuse: Noisy Color"},
        {DEBUG_VIEW_NOISY_PRIMARY_DIRECT_DIFFUSE_HIT_T,                  "Primary Direct Diffuse: Noisy HitT"},
        {DEBUG_VIEW_DEMODULATED_NOISY_PRIMARY_DIRECT_DIFFUSE_RADIANCE,   "Primary Direct Diffuse: Demodulated Noisy Color"},
        {DEBUG_VIEW_DENOISED_PRIMARY_DIRECT_DIFFUSE_RADIANCE,            "Primary Direct Diffuse: Denoised Color"},
        {DEBUG_VIEW_DENOISED_PRIMARY_DIRECT_DIFFUSE_HIT_T,               "Primary Direct Diffuse: Denoised HitT (ReBLUR) | Variance (ReLAX)"},
        {DEBUG_VIEW_NRD_NORMALIZED_PRIMARY_DIRECT_DIFFUSE_HIT_T,         "Primary Direct Diffuse: NRD Normalized  HitT"},

        {DEBUG_VIEW_NOISY_PRIMARY_DIRECT_SPECULAR_RADIANCE,              "Primary Direct Specular: Noisy Color"},
        {DEBUG_VIEW_NOISY_PRIMARY_DIRECT_SPECULAR_HIT_T,                 "Primary Direct Specular: Noisy HitT"},
        {DEBUG_VIEW_DEMODULATED_NOISY_PRIMARY_DIRECT_SPECULAR_RADIANCE,  "Primary Direct Specular: Demodulated Noisy Color"},
        {DEBUG_VIEW_DENOISED_PRIMARY_DIRECT_SPECULAR_RADIANCE,           "Primary Direct Specular: Denoised Color"},
        {DEBUG_VIEW_DENOISED_PRIMARY_DIRECT_SPECULAR_HIT_T,              "Primary Direct Specular: Denoised HitT (ReBLUR) | Variance (ReLAX)"},
        {DEBUG_VIEW_NRD_NORMALIZED_PRIMARY_DIRECT_SPECULAR_HIT_T,        "Primary Direct Specular: NRD Normalized  HitT"},

        {DEBUG_VIEW_NOISY_PRIMARY_INDIRECT_DIFFUSE_RADIANCE,               "Primary Indirect Diffuse: Noisy Color"},
        {DEBUG_VIEW_NOISY_PRIMARY_INDIRECT_DIFFUSE_HIT_T,                  "Primary Indirect Diffuse: Noisy HitT"},
        {DEBUG_VIEW_DEMODULATED_NOISY_PRIMARY_INDIRECT_DIFFUSE_RADIANCE,   "Primary Indirect Diffuse: Demodulated Noisy Color"},
        {DEBUG_VIEW_DENOISED_PRIMARY_INDIRECT_DIFFUSE_RADIANCE,            "Primary Indirect Diffuse: Denoised Color"},
        {DEBUG_VIEW_DENOISED_PRIMARY_INDIRECT_DIFFUSE_HIT_T,               "Primary Indirect Diffuse: Denoised HitT (ReBLUR) | Variance (ReLAX)"},
        {DEBUG_VIEW_NRD_NORMALIZED_PRIMARY_INDIRECT_DIFFUSE_HIT_T,         "Primary Indirect Diffuse: NRD Normalized  HitT"},

        {DEBUG_VIEW_NOISY_PRIMARY_INDIRECT_SPECULAR_RADIANCE,              "Primary Indirect Specular: Noisy Color"},
        {DEBUG_VIEW_NOISY_PRIMARY_INDIRECT_SPECULAR_HIT_T,                 "Primary Indirect Specular: Noisy HitT"},
        {DEBUG_VIEW_DEMODULATED_NOISY_PRIMARY_INDIRECT_SPECULAR_RADIANCE,  "Primary Indirect Specular: Demodulated Noisy Color"},
        {DEBUG_VIEW_DENOISED_PRIMARY_INDIRECT_SPECULAR_RADIANCE,           "Primary Indirect Specular: Denoised Color"},
        {DEBUG_VIEW_DENOISED_PRIMARY_INDIRECT_SPECULAR_HIT_T,              "Primary Indirect Specular: Denoised HitT (ReBLUR) | Variance (ReLAX)"},
        {DEBUG_VIEW_NRD_NORMALIZED_PRIMARY_INDIRECT_SPECULAR_HIT_T,        "Primary Indirect Specular: NRD Normalized  HitT"},

        {DEBUG_VIEW_NOISY_SECONDARY_DIRECT_DIFFUSE_RADIANCE,               "Secondary Direct Diffuse: Noisy Color"},
        {DEBUG_VIEW_NOISY_SECONDARY_INDIRECT_DIFFUSE_RADIANCE,             "Secondary Indirect Diffuse: Noisy Color"},
        {DEBUG_VIEW_NOISY_SECONDARY_COMBINED_DIFFUSE_RADIANCE,             "Secondary Combined Diffuse: Noisy Color"},
        {DEBUG_VIEW_NOISY_DEMODULATED_SECONDARY_COMBINED_DIFFUSE_RADIANCE, "Secondary Combined Diffuse: Demodulated Noisy Color"},
        {DEBUG_VIEW_DENOISED_SECONDARY_COMBINED_DIFFUSE_RADIANCE,          "Secondary Combined Diffuse: Denoised Color"},
        {DEBUG_VIEW_NOISY_SECONDARY_DIRECT_SPECULAR_RADIANCE,              "Secondary Direct Specular: Noisy Color"},
        {DEBUG_VIEW_NOISY_SECONDARY_INDIRECT_SPECULAR_RADIANCE,            "Secondary Indirect Specular: Noisy Color"},
        {DEBUG_VIEW_NOISY_SECONDARY_COMBINED_SPECULAR_RADIANCE,            "Secondary Combined Specular: Noisy Color"},
        {DEBUG_VIEW_NOISY_DEMODULATED_SECONDARY_COMBINED_SPECULAR_RADIANCE,"Secondary Combined Specular: Demodulated Noisy Color"},
        {DEBUG_VIEW_DENOISED_SECONDARY_COMBINED_SPECULAR_RADIANCE,         "Secondary Combined Specular: Denoised Color"},

        {DEBUG_VIEW_NOISY_PATHRACED_RAW_INDIRECT_RADIANCE,                 "Pathtraced Indirect (raw): Noisy Color" },
        {DEBUG_VIEW_NOISY_PRIMARY_DIRECT_RADIANCE,                         "Primary Direct: Noisy Color" },
        {DEBUG_VIEW_NOISY_PRIMARY_INDIRECT_RADIANCE,                       "Primary Indirect: Noisy Color" },
        {DEBUG_VIEW_NOISY_PRIMARY_RADIANCE,                                "Primary: Noisy Color" },
        {DEBUG_VIEW_NOISY_SECONDARY_DIRECT_RADIANCE,                       "Secondary Direct: Noisy Color" },
        {DEBUG_VIEW_NOISY_SECONDARY_INDIRECT_RADIANCE,                     "Secondary Indirect: Noisy Color" },
        {DEBUG_VIEW_NOISY_SECONDARY_RADIANCE,                              "Secondary: Noisy Color" },
        {DEBUG_VIEW_NOISY_RADIANCE,                                        "Primary + Secondary: Noisy Color" },

        {DEBUG_VIEW_INSTRUMENTATION_THREAD_DIVERGENCE,                     "Thread Divergence(Debug Knob.x)"},
        {DEBUG_VIEW_NAN,                                                   "Inf/NaN Check"},
        {DEBUG_SURFACE_LOBE_CONSISTENCY,                                   "Surface/Lobe Consistency Check"},
        {DEBUG_VIEW_SCROLLING_LINE,                                        "Scrolling Line"},
        {DEBUG_VIEW_POM_ITERATIONS,                                        "POM Iterations"},
        {DEBUG_VIEW_POM_DIRECT_HIT_POS,                                    "POM Direct Hit Position (Tangent Space)"},
        {DEBUG_VIEW_VALUE_NOISE,                                           "4D Value Noise"},
        {DEBUG_VIEW_HEIGHT_MAP,                                            "Height Map Value",
                                                                            "Valid values will be greyscale."
                                                                            "\nColored pixels indicate errors happened"
                                                                            "\nwhen passing POM state between passes."},
        {DEBUG_VIEW_RAYTRACED_RENDER_TARGET_GEOMETRY,                      "Raytraced Render Target Geometry" },
        {DEBUG_VIEW_RAYTRACED_RENDER_TARGET_DIRECT,                        "Hit Raytraced Render Target in Direct"},
        {DEBUG_VIEW_RAYTRACED_RENDER_TARGET_INDIRECT,                      "Hit Raytraced Render Target in Indirect"},
        {DEBUG_VIEW_NRC_UPDATE_PIXEL,                               "NRC Update Pixel"},
        {DEBUG_VIEW_NRC_RESOLVED_RADIANCE,                          "NRC Resolved Radiance"},
        {DEBUG_VIEW_NRC_UPDATE_IS_UNBIASED,                         "NRC Update: Is Unbiased" },
        {DEBUG_VIEW_NRC_UPDATE_NUMBER_OF_BOUNCES,                   "NRC Update: Number of Bounces"},
        {DEBUG_VIEW_NRC_UPDATE_NUMBER_OF_PATH_SEGMENTS,             "NRC Update: Number of Path Segments (i.e. Primary + Indirect, Bounces + Misses)"},
        {DEBUG_VIEW_NRC_UPDATE_NUMBER_OF_INDIRECT_PATH_SEGMENTS,    "NRC Update: Number of Indirect Path Segments (i.e. Bounces + Misses)" },
        {DEBUG_VIEW_NRC_QUERY_NUMBER_OF_BOUNCES,                    "NRC Query: Number of Bounces"},
        {DEBUG_VIEW_NRC_QUERY_NUMBER_OF_PATH_SEGMENTS,              "NRC Query: Number of Path Segments (i.e. Primary + Indirect, Bounces + Misses)" },
        {DEBUG_VIEW_NRC_QUERY_NUMBER_OF_INDIRECT_PATH_SEGMENTS,     "NRC Query: Number of Indirect Path Segments (i.e. Bounces + Misses)"},
        {DEBUG_VIEW_NRC_UPDATE_RADIANCE,        "NRC Training Radiance along a Path",
                                                "Parameterize via ROUND(Debug Knob [0]):\n"
                                                "  -2: Max: Take max incomming radiance seen at a bounce\n"
                                                "  -1: Sum: Sum all incomming radiance seen across all bounces\n"
                                                "  [0, N): Bounce: Show incomming radiance seen at a given bounce" },
        {DEBUG_VIEW_NRC_UPDATE_THROUGHPUT,      "NRC Training Throughput along a Path",
                                                "Parameterize via ROUND(Debug Knob [0]):\n"
                                                "  -2: Max: Take max incomming throughput seen at a bounce\n"
                                                "  -1: Sum: Sum all incomming throughput seen across all bounces\n"
                                                "  [0, N): Bounce: Show incomming throughput seen at a given bounce" },
        {DEBUG_VIEW_NRC_UPDATE_RADIANCE_MULTIPLIED_BY_THROUGHPUT, "NRC Training Radiance multiplied by Throughput along a Path",
                                                "Parameterize via ROUND(Debug Knob [0]):\n"
                                                "  -2: Max: Take max incomming radiance * throughput seen at a bounce\n"
                                                "  -1: Sum: Sum all incomming radiance * throughput seen across all bounces\n"
                                                "  [0, N): Bounce: Show incomming radiance * throughput seen at a given bounce" },
        {DEBUG_VIEW_NRC_IS_OUTSIDE_SCENE_AABB, "Is Primary or Secondary Hit Outside NRC's Axis Aligned Bounding Box",
                                               "Legend: Black - inside, Red - outside"},

        {DEBUG_VIEW_SSS_DIFFUSION_PROFILE_SAMPLING,       "SSS Diffusion Profile Sampling" },
        {DEBUG_VIEW_NRD_INSTANCE_0_VALIDATION_LAYER,      "NRD Instance 0 Validation Layer", "Requires NRD and \"NRD/Common Settings/Validation Layer\" enabled" },
        {DEBUG_VIEW_NRD_INSTANCE_1_VALIDATION_LAYER,      "NRD Instance 1 Validation Layer", "Requires NRD and \"NRD/Common Settings/Validation Layer\" enabled" },
        {DEBUG_VIEW_NRD_INSTANCE_2_VALIDATION_LAYER,      "NRD Instance 2 Validation Layer", "Requires NRD and \"NRD/Common Settings/Validation Layer\" enabled" },
    } };

  ImGui::ComboWithKey<CompositeDebugView> compositeDebugViewCombo = ImGui::ComboWithKey<CompositeDebugView>(
    "Composite Debug View",
    ImGui::ComboWithKey<CompositeDebugView>::ComboEntries { {
        {CompositeDebugView::FinalRenderWithMaterialProperties, "Final Render + Material Properties"},
        {CompositeDebugView::OpaqueMaterialTextureResolutionCheckers, "Opaque Material Texture Resolution Checkers", "Textures: Raw Albedo, Normal, Roughness" },
    } });

  ImGui::ComboWithKey<DebugViewDisplayType> displayTypeCombo = ImGui::ComboWithKey<DebugViewDisplayType>(
  "Display Type",
  ImGui::ComboWithKey<DebugViewDisplayType>::ComboEntries{ {
      {DebugViewDisplayType::Standard, "Standard"},
      {DebugViewDisplayType::BGRExclusiveColor, "BGR Exclusive Color"},
      {DebugViewDisplayType::EV100, "Exposure Value (EV100)"},
      {DebugViewDisplayType::HDRWaveform, "HDR Waveform"},
  } });

  ImGui::ComboWithKey<DebugViewSamplerType> samplerTypeCombo = ImGui::ComboWithKey<DebugViewSamplerType>(
  "Sampler Type",
  ImGui::ComboWithKey<DebugViewSamplerType>::ComboEntries { {
      {DebugViewSamplerType::Nearest, "Nearest"},
      {DebugViewSamplerType::NormalizedNearest, "Normalized Nearest"},
      {DebugViewSamplerType::NormalizedLinear, "Normalized Linear"},
  } });

  ImGui::ComboWithKey<PseudoColorMode> pseudoColorModeCombo = ImGui::ComboWithKey<PseudoColorMode>(
  "Pseudo Color Mode",
  ImGui::ComboWithKey<PseudoColorMode>::ComboEntries{ {
      {PseudoColorMode::Disabled, "Disabled"},
      {PseudoColorMode::Luminance, "RGB Luminance"},
      {PseudoColorMode::Red, "Red"},
      {PseudoColorMode::Green, "Green"},
      {PseudoColorMode::Blue, "Blue"},
      {PseudoColorMode::Alpha, "Alpha"},
  } });

  ImGui::ComboWithKey<DebugViewOutputStatisticsMode> outputStatisticsCombo = ImGui::ComboWithKey<DebugViewOutputStatisticsMode>(
    "Output Statistics Mode",
    ImGui::ComboWithKey<DebugViewOutputStatisticsMode>::ComboEntries { {
        {DebugViewOutputStatisticsMode::Mean, "Mean"},
        {DebugViewOutputStatisticsMode::Sum, "Sum"},
    } });

  // Defined within an unnamed namespace to ensure unique definition across binary
  namespace {
    class DebugViewShader : public ManagedShader {
      BEGIN_PARAMETER()
        TEXTURE2D(DEBUG_VIEW_BINDING_DENOISED_PRIMARY_DIRECT_DIFFUSE_RADIANCE_HIT_T_INPUT)
        TEXTURE2D(DEBUG_VIEW_BINDING_DENOISED_PRIMARY_DIRECT_SPECULAR_RADIANCE_HIT_T_INPUT)
        TEXTURE2D(DEBUG_VIEW_BINDING_DENOISED_SECONDARY_COMBINED_DIFFUSE_RADIANCE_HIT_T_INPUT)
        TEXTURE2D(DEBUG_VIEW_BINDING_DENOISED_SECONDARY_COMBINED_SPECULAR_RADIANCE_HIT_T_INPUT)
        TEXTURE2D(DEBUG_VIEW_BINDING_SHARED_FLAGS_INPUT)
        TEXTURE2D(DEBUG_VIEW_BINDING_PRIMARY_LINEAR_VIEW_Z_INPUT)
        TEXTURE2D(DEBUG_VIEW_BINDING_PRIMARY_VIRTUAL_WORLD_SHADING_NORMAL_PERCEPTUAL_ROUGHNESS_INPUT)
        TEXTURE2D(DEBUG_VIEW_BINDING_PRIMARY_VIRTUAL_MOTION_VECTOR_INPUT)
        TEXTURE2D(DEBUG_VIEW_BINDING_PRIMARY_SCREEN_SPACE_MOTION_VECTOR_INPUT)
        TEXTURE2D(DEBUG_VIEW_BINDING_RTXDI_CONFIDENCE_INPUT)
        TEXTURE2D(DEBUG_VIEW_BINDING_FINAL_SHADING_INPUT)
        TEXTURE2D(DEBUG_VIEW_BINDING_INSTRUMENTATION_INPUT)
        TEXTURE2D(DEBUG_VIEW_BINDING_TERRAIN_INPUT)
        TEXTURE3D(DEBUG_VIEW_BINDING_VOLUME_RESERVOIRS)
        SAMPLER3D(DEBUG_VIEW_BINDING_VOLUME_AGE)
        SAMPLER3D(DEBUG_VIEW_BINDING_VOLUME_RADIANCE_Y)
        SAMPLER3D(DEBUG_VIEW_BINDING_VOLUME_RADIANCE_COCG)
        SAMPLER2D(DEBUG_VIEW_BINDING_VALUE_NOISE_SAMPLER)
        TEXTURE2DARRAY(DEBUG_VIEW_BINDING_BLUE_NOISE_TEXTURE)
        TEXTURE2D(DEBUG_VIEW_BINDING_INPUT)
        TEXTURE2D(DEBUG_VIEW_NRD_VALIDATION_LAYER_INPUT)
        RW_TEXTURE2D(DEBUG_VIEW_BINDING_HDR_WAVEFORM_RED_INPUT_OUTPUT)
        RW_TEXTURE2D(DEBUG_VIEW_BINDING_HDR_WAVEFORM_GREEN_INPUT_OUTPUT)
        RW_TEXTURE2D(DEBUG_VIEW_BINDING_HDR_WAVEFORM_BLUE_INPUT_OUTPUT)

        RW_TEXTURE2D(DEBUG_VIEW_BINDING_COMPOSITE_OUTPUT_INPUT_OUTPUT)
        RW_TEXTURE2D(DEBUG_VIEW_BINDING_PREVIOUS_FRAME_INPUT_OUTPUT)

        SAMPLER(DEBUG_VIEW_BINDING_NEAREST_SAMPLER)
        SAMPLER(DEBUG_VIEW_BINDING_LINEAR_SAMPLER)

        CONSTANT_BUFFER(DEBUG_VIEW_BINDING_CONSTANTS_INPUT)

        RW_STRUCTURED_BUFFER(DEBUG_VIEW_BINDING_STATISTICS_BUFFER_OUTPUT)
        RW_TEXTURE2D(DEBUG_VIEW_BINDING_OUTPUT)
      END_PARAMETER()
    };

    class DebugViewWaveformRenderShader : public ManagedShader {
      SHADER_SOURCE(DebugViewWaveformRenderShader, VK_SHADER_STAGE_COMPUTE_BIT, debug_view_waveform_render)

      BEGIN_PARAMETER()
        TEXTURE2D(DEBUG_VIEW_WAVEFORM_RENDER_BINDING_HDR_WAVEFORM_RED_INPUT)
        TEXTURE2D(DEBUG_VIEW_WAVEFORM_RENDER_BINDING_HDR_WAVEFORM_GREEN_INPUT)
        TEXTURE2D(DEBUG_VIEW_WAVEFORM_RENDER_BINDING_HDR_WAVEFORM_BLUE_INPUT)
        RW_TEXTURE2D(DEBUG_VIEW_WAVEFORM_RENDER_BINDING_INPUT_OUTPUT)
        CONSTANT_BUFFER(DEBUG_VIEW_WAVEFORM_RENDER_BINDING_CONSTANTS_INPUT)
      END_PARAMETER()
    };

    PREWARM_SHADER_PIPELINE(DebugViewWaveformRenderShader);
  }

  DebugView::DebugView(dxvk::DxvkDevice* device)
    : RtxPass(device)
    , m_vkd(device->vkd())
    , m_device(device)
    , m_lastDebugViewIdx(DEBUG_VIEW_PRIMITIVE_INDEX)
    , m_startTime(std::chrono::system_clock::now()){
    initSettings(device->instance()->config());
  }

  void DebugView::prewarmShaders(DxvkPipelineManager& pipelineManager) const {
    getDebugViewShader();
  }

  bool DebugView::areDebugViewStatisticsSupported() const {
    return m_device->features().extShaderAtomicFloat.shaderBufferFloat32AtomicAdd;
  }

  void DebugView::initSettings(const dxvk::Config& config) {
    // Note: Set the last debug view index only if the debug view index was specified to be enabled to something (not disabled).
    if (debugViewIdx() != DEBUG_VIEW_DISABLED) {
      m_lastDebugViewIdx = debugViewIdx();
    }

    // Note: Set the last composite debug view index only if the debug view index was specified to be enabled to something (not disabled).
    if (static_cast<CompositeDebugView>(Composite::compositeViewIdx()) != CompositeDebugView::Disabled) {
      m_composite.lastCompositeViewIdx = static_cast<CompositeDebugView>(Composite::compositeViewIdx());
    }

    displayTypeRef() = static_cast<DebugViewDisplayType>(std::min(static_cast<uint32_t>(displayType()), static_cast<uint32_t>(DebugViewDisplayType::Count) - 1));
  
    const uint32_t bufferLength = kMaxFramesInFlight;

    DxvkBufferCreateInfo statisticsBufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    statisticsBufferInfo.usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    statisticsBufferInfo.stages = VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
    statisticsBufferInfo.access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_HOST_READ_BIT | VK_ACCESS_HOST_WRITE_BIT;
    statisticsBufferInfo.size = bufferLength * sizeof(m_outputStatistics);
    m_statisticsBuffer = m_device->createBuffer(statisticsBufferInfo, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, DxvkMemoryStats::Category::RTXBuffer, "Debug View Statistics");

    if (areDebugViewStatisticsSupported()) {
      // Zero init the whole buffer
      vec4* gpuMappedVec4 = reinterpret_cast<vec4*>(m_statisticsBuffer->mapPtr(0));
      for (uint32_t i = 0; i < kMaxFramesInFlight; i++) {
        gpuMappedVec4[i] = vec4(0.f, 0.f, 0.f, 0.f);
      }
    }
  }

  void getDebugViewCombo(std::string searchWord, uint32_t& lastView) {
    // turn search word into lower case
    auto toLowerCase = [](std::string& word) {
      std::transform(word.begin(), word.end(), word.begin(),
                     [](unsigned char c) { return std::tolower(c); });
    };
    toLowerCase(searchWord);
    bool filterWords = searchWord.length() > 0;

    // Hide unmatched options
    std::vector<std::pair<const char* /*name*/, const char* /*tooltip*/>> items;
    items.reserve(debugViewEntries.size());
    int itemIndex = -1;
    for (int i = 0; i < debugViewEntries.size(); i++) {
      if (debugViewEntries[i].key == lastView) {
        itemIndex = items.size();
      }

      if (filterWords) {
        std::string name(debugViewEntries[i].name);
        toLowerCase(name);

        if (debugViewEntries[i].key == lastView || name.find(searchWord) != std::string::npos) {
          items.emplace_back(debugViewEntries[i].name, debugViewEntries[i].tooltip);
        }
      } else {
        items.emplace_back(debugViewEntries[i].name, debugViewEntries[i].tooltip);
      }
    }

    ImGui::Text("Debug Views");

    const int indent = 50;
    ImGui::PushItemWidth(ImGui::GetWindowWidth() - indent);
    ImGui::PushID("Debug Views");
    ImGui::ListBox("", &itemIndex, items.data(), items.size(), 4);
    ImGui::PopID();
    ImGui::PopItemWidth();

    for (int i = 0; i < debugViewEntries.size(); i++) {
      if (itemIndex != -1 && debugViewEntries[i].name == items[itemIndex].first) {
        lastView = debugViewEntries[i].key;
      }
    }
  }

  void DebugView::showAccumulationImguiSettings(const char* tabName) {
    const ImGuiTreeNodeFlags collapsingHeaderFlags = ImGuiTreeNodeFlags_CollapsingHeader;
    
    if (ImGui::CollapsingHeader(tabName, collapsingHeaderFlags)) {
      ImGui::Indent();

      if (ImGui::Button("Reset History")) {
        resetNumAccumulatedFrames();
      }

      ImGui::InputInt("Number of Frames To Accumulate", &numberOfFramesToAccumulateObject());

      uint32_t val = numberOfFramesToAccumulate();

      // Reset accumulation if the cap gets lowered and below the current count
      if (m_prevNumberOfFramesToAccumulate > numberOfFramesToAccumulate() &&
          m_numFramesAccumulated >= numberOfFramesToAccumulate()) {
        resetNumAccumulatedFrames();
      }
      m_prevNumberOfFramesToAccumulate = numberOfFramesToAccumulate();

      if (numberOfFramesToAccumulate() > 1) {

        // ImGUI runs async with frame execution, so always report at least 1 frame was generated to avoid showing 0
        // since renderer will always show a generated image
        const uint32_t numFramesAccumulated = std::max(1u, m_numFramesAccumulated);

        const uint32_t maxNumFramesToAccumulate = std::max(1u, numberOfFramesToAccumulate());
        const float accumulatedPercentage = numFramesAccumulated / (0.01f * maxNumFramesToAccumulate);
        ImGui::Text("   Accumulated: %u (%.2f%%)", numFramesAccumulated, accumulatedPercentage);
      }

      ImGui::Checkbox("Continuous Accumulation", &enableContinuousAccumulationObject());
      ImGui::Checkbox("Fp16 Accumulation", &enableFp16AccumulationObject());

      ImGui::Unindent();
    }
  }

  void DebugView::processOutputStatistics(
    Rc<RtxContext>& ctx,
    const Resources::RaytracingOutput& rtOutput) {
    
    if (m_showOutputStatistics) {
      const uint32_t frameIdx = ctx->getDevice()->getCurrentFrameId();

      // Read from the oldest element as it is guaranteed to be written out to by the GPU by now
      VkDeviceSize offset = ((frameIdx + 1) % kMaxFramesInFlight) * sizeof(m_outputStatistics);
      vec4* gpuMappedVec4 = reinterpret_cast<vec4*>(m_statisticsBuffer->mapPtr(offset));

      m_outputStatistics = *gpuMappedVec4;

      // Zero out the backing memory
      *gpuMappedVec4 = vec4(0.f, 0.f, 0.f, 0.f);
    }

    // Normalize the retrieved values in case the input was supersampled due to resolution mismatch
    // between debug and the resolution of underlying data embedded in it

    Vector4& outputStatistics = reinterpret_cast<Vector4&>(m_outputStatistics);
    NeuralRadianceCache& nrc = ctx->getCommonObjects()->metaNeuralRadianceCache();

    switch (debugViewIdx()) {
      default:
        break;
      case DEBUG_VIEW_NRC_RESOLVE:
        if (m_outputStatisticsMode == DebugViewOutputStatisticsMode::Sum
            && nrc.isUpdateResolveModeActive()) {
          // NRC resolve loads training pixels numerous times for all query pixels,
          // so we need to divide by number of query pixels per training pixel to get the sum
          outputStatistics /=
            static_cast<float>(nrc.getNumQueryPixelsPerTrainingPixel().x * nrc.getNumQueryPixelsPerTrainingPixel().y);
        }
        break;
      case DEBUG_VIEW_NRC_UPDATE_RADIANCE:
      case DEBUG_VIEW_NRC_UPDATE_THROUGHPUT:
      case DEBUG_VIEW_NRC_UPDATE_RADIANCE_MULTIPLIED_BY_THROUGHPUT:
      case DEBUG_VIEW_NRC_UPDATE_NUMBER_OF_BOUNCES:
      case DEBUG_VIEW_NRC_UPDATE_NUMBER_OF_PATH_SEGMENTS:
        [[fallthrough]];
      case DEBUG_VIEW_NRC_UPDATE_NUMBER_OF_INDIRECT_PATH_SEGMENTS:
        outputStatistics *=
          static_cast<float>(nrc.getNumQueryPixelsPerTrainingPixel().x * nrc.getNumQueryPixelsPerTrainingPixel().y);
        break;
    }
  }

  void DebugView::showOutputStatistics() {

    if (areDebugViewStatisticsSupported()) {
      ImGui::Checkbox("Show Output Statistics", &m_showOutputStatistics);
    }

    if (m_showOutputStatistics) {
      ImGui::Indent();

      ImGui::Checkbox("Print Output Statistics", &m_printOutputStatistics);
      
      outputStatisticsCombo.getKey(&m_outputStatisticsMode);

      const std::string statisticsString = str::format(
        "RGBA ",
        m_outputStatistics.x, ", ",
        m_outputStatistics.y, ", ",
        m_outputStatistics.z, ", ",
        m_outputStatistics.w);
      
      ImGui::Text(statisticsString.c_str());

      if (m_printOutputStatistics) {
        Logger::info("Debug View Statistics: " + statisticsString);
      }

      ImGui::Unindent();
    }
  }

  void DebugView::showImguiSettings()
  {
    // Dealias same widget names from the rest of RTX
    ImGui::PushID("Debug View");

    const ImGuiSliderFlags sliderFlags = ImGuiSliderFlags_AlwaysClamp;
    const ImGuiTreeNodeFlags collapsingHeaderFlags = ImGuiTreeNodeFlags_CollapsingHeader;

    // Note: Ensure the enable checkbox state matches what the debug index was set to externally (for example when loaded from settings).
    bool enableCompositeDebugView = static_cast<CompositeDebugView>(Composite::compositeViewIdx()) != CompositeDebugView::Disabled;
    bool enableDebugView = debugViewIdx() != DEBUG_VIEW_DISABLED || enableCompositeDebugView;

    // Note: Ensure the last debug view index wasn't incorrectly set to the disabled index somehow.
    assert(m_lastDebugViewIdx != DEBUG_VIEW_DISABLED);
    assert(m_composite.lastCompositeViewIdx != CompositeDebugView::Disabled);

    if (ImGui::Button("Cache Current Image"))
      m_cacheCurrentImage = true;

    ImGui::Checkbox("Show Cached Image", &m_showCachedImage);

    ImGui::Checkbox("Enable Debug View", &enableDebugView);

    if (enableDebugView) {
      // Debug view is required for composite debug views, so put the enablement behind it
      ImGui::Checkbox("Enable Composite Debug View", &enableCompositeDebugView);

      if (!enableCompositeDebugView) {
        static char codewordBuf[32] = "";
        ImGui::InputText("Search Debug View", codewordBuf, IM_ARRAYSIZE(codewordBuf)-1, ImGuiInputTextFlags_EnterReturnsTrue);
        codewordBuf[31] = '\0';
        std::string searchWord(codewordBuf);
        // Note: Write to the last debug view index to prevent from being overridden when disabled and re-enabled.
        getDebugViewCombo(searchWord, m_lastDebugViewIdx);

        debugViewIdxRef() = m_lastDebugViewIdx;
      }
    } else {
      debugViewIdxRef() = DEBUG_VIEW_DISABLED;
      Composite::compositeViewIdxRef() = static_cast<uint32_t>(CompositeDebugView::Disabled);
      enableCompositeDebugView = false;
    }

    if (enableCompositeDebugView) {
      // Note: Write to the last composite debug view index to prevent it from being overridden when disabled and re-enabled.
      compositeDebugViewCombo.getKey(&m_composite.lastCompositeViewIdx);

      Composite::compositeViewIdxRef() = static_cast<uint32_t>(m_composite.lastCompositeViewIdx);
    } else {
      Composite::compositeViewIdxRef() = static_cast<uint32_t>(CompositeDebugView::Disabled);
    }

    ImGui::Checkbox("Accumulation", &enableAccumulationObject());

    showOutputStatistics();

    if (enableAccumulation()) {
      showAccumulationImguiSettings("Accumulation (Aliased with Reference Denoiser's Settings)");
    }

    ImGui::DragFloat4("Debug Knob", (float*)&m_debugKnob, 0.1f, -1000.f, 1000.f, "%.3f", sliderFlags);

    displayTypeCombo.getKey(&displayTypeObject());
    samplerTypeCombo.getKey(&samplerTypeObject());

    ImGui::Checkbox("Replace Composite Output", &replaceCompositeOutputObject());

    if (ImGui::CollapsingHeader("Display Settings", collapsingHeaderFlags)) {
      ImGui::Indent();

      ImGui::Text("Common:");

      ImGui::Checkbox("Show First Hit Surface", &showFirstGBufferHitObject());

      // NaN/Inf Colorization

      ImGui::Checkbox("Color NaN Red/Inf Blue", &m_enableInfNanView);

      if (m_enableInfNanView) {
        ImGui::InputInt("Color Code Pixel Radius", &m_colorCodeRadius);
      }

      // Input Quantization

      ImGui::Checkbox("Quantize Input", &enableInputQuantizationObject());

      if (enableInputQuantization()) {
        ImGui::InputFloat("Inverse Quantization Step Size", &inverseQuantizationStepSizeObject(), 0.1f, 1.0f);
        ImGui::Text("Effective Quantized Step Size: 1.0 / %f", inverseQuantizationStepSizeObject());
      }

      if (displayType() == DebugViewDisplayType::Standard) {
        ImGui::Text("Standard:");

        ImGui::Checkbox("Alpha Channel", &m_enableAlphaChannel);
        ImGui::Checkbox("Gamma Correction", &enableGammaCorrectionObject());
        pseudoColorModeCombo.getKey(&pseudoColorModeObject());

        ImGui::DragFloat("Scale", &m_scale, 0.01f, 0.0f, FLT_MAX, "%.3f", sliderFlags);
        ImGui::InputFloat("Min Value", &minValueObject(), std::max(0.01f, 0.02f * std::abs(minValue())), std::max(0.1f, 0.1f * std::abs(minValue())));
        ImGui::InputFloat("Max Value", &maxValueObject(), std::max(0.01f, 0.02f * std::abs(maxValue())), std::max(0.1f, 0.1f * std::abs(maxValue())));
        maxValueRef() = std::max(1.00001f * minValue(), maxValue());

        // Color legend
        if (pseudoColorModeObject() != PseudoColorMode::Disabled) {
          ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4 { 0.25f, 0.25f, 0.25f, 1.0f });
          ImGui::BeginChildFrame(ImGui::GetID("Pseudo Color Legend"), ImVec2(500, 20), ImGuiWindowFlags_NoScrollbar);
          ImGui::TextColored(ImVec4 { colormap0.x, colormap0.y, colormap0.z, 1.0f }, "%g", minValue());
          ImGui::SameLine();
          ImGui::TextColored(ImVec4 { colormap25.x, colormap25.y, colormap25.z, 1.0f }, "%g", lerp(static_cast<float>(minValue()), static_cast<float>(maxValue()), 0.25));
          ImGui::SameLine();
          ImGui::TextColored(ImVec4 { colormap50.x, colormap50.y, colormap50.z, 1.0f }, "%g", lerp(static_cast<float>(minValue()), static_cast<float>(maxValue()), 0.5));
          ImGui::SameLine();
          ImGui::TextColored(ImVec4 { colormap75.x, colormap75.y, colormap75.z, 1.0f }, "%g", lerp(static_cast<float>(minValue()), static_cast<float>(maxValue()), 0.75));
          ImGui::SameLine();
          ImGui::TextColored(ImVec4 { colormap100.x, colormap100.y, colormap100.z, 1.0f }, "%g", static_cast<float>(maxValue()));
          ImGui::EndChildFrame();
          ImGui::PopStyleColor();
        }
      } else if (displayType() == DebugViewDisplayType::BGRExclusiveColor) {
        ImGui::Text("BGR Exclusive Color:");

        ImGui::InputFloat("Max Value", &maxValueObject(), std::max(0.01f, 0.02f * std::abs(maxValue())), std::max(0.1f, 0.1f * std::abs(maxValue())));
      } else if (displayType() == DebugViewDisplayType::EV100) {
        ImGui::Text("Exposure Value (EV100):");

        ImGui::InputInt("Min Value (EV100)", &evMinValueObject());
        ImGui::InputInt("Max Value (EV100)", &evMaxValueObject());

        evMaxValueRef() = std::max(evMaxValue(), evMinValue());

        // Color legend
        {
          ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4 { 0.25f, 0.25f, 0.25f, 1.0f });
          ImGui::BeginChildFrame(ImGui::GetID("Exposure value legend"), ImVec2(500, 45), ImGuiWindowFlags_NoScrollbar);

          // Note: Somewhat lazy visual indicator to show which colors represent which EV values. A proper labeled gradient would be better,
          // or an actual spot meter that reads back from the GPU, but for now this is fine (should match the colors the GPU produces).
          ImGui::TextColored(ImVec4 { colormap0.x, colormap0.y, colormap0.z, 1.0f }, "%+d.0 EV", evMinValue());
          ImGui::SameLine();
          ImGui::TextColored(ImVec4 { colormap25.x, colormap25.y, colormap25.z, 1.0f }, "%+.2f EV", lerp(static_cast<float>(evMinValue()), static_cast<float>(evMaxValue()), 0.25));
          ImGui::SameLine();
          ImGui::TextColored(ImVec4 { colormap50.x, colormap50.y, colormap50.z, 1.0f }, "%+.1f EV", lerp(static_cast<float>(evMinValue()), static_cast<float>(evMaxValue()), 0.5));
          ImGui::SameLine();
          ImGui::TextColored(ImVec4 { colormap75.x, colormap75.y, colormap75.z, 1.0f }, "%+.2f EV", lerp(static_cast<float>(evMinValue()), static_cast<float>(evMaxValue()), 0.75));
          ImGui::SameLine();
          ImGui::TextColored(ImVec4 { colormap100.x, colormap100.y, colormap100.z, 1.0f }, "%+d.0 EV", evMaxValue());

          auto evToLuminanceValue = [&](float evValue) {
            // Given evValue = log2(luminance * type(100.0f / 12.5f))
            return static_cast<float>(pow(2, evValue) / (100 / 12.5));
          };
          ImGui::TextColored(ImVec4 { colormap0.x, colormap0.y, colormap0.z, 1.0f }, "%g", evToLuminanceValue(static_cast<float>(evMinValue())));
          ImGui::SameLine();
          ImGui::TextColored(ImVec4 { colormap25.x, colormap25.y, colormap25.z, 1.0f }, "%g", evToLuminanceValue(lerp(static_cast<float>(evMinValue()), static_cast<float>(evMaxValue()), 0.25)));
          ImGui::SameLine();
          ImGui::TextColored(ImVec4 { colormap50.x, colormap50.y, colormap50.z, 1.0f }, "%g", evToLuminanceValue(lerp(static_cast<float>(evMinValue()), static_cast<float>(evMaxValue()), 0.5)));
          ImGui::SameLine();
          ImGui::TextColored(ImVec4 { colormap75.x, colormap75.y, colormap75.z, 1.0f }, "%g", evToLuminanceValue(lerp(static_cast<float>(evMinValue()), static_cast<float>(evMaxValue()), 0.75)));
          ImGui::SameLine();
          ImGui::TextColored(ImVec4 { colormap100.x, colormap100.y, colormap100.z, 1.0f }, "%g", evToLuminanceValue(static_cast<float>(evMaxValue())));

          ImGui::EndChildFrame();
          ImGui::PopStyleColor();
        }
      } else if (displayType() == DebugViewDisplayType::HDRWaveform) {
        ImGui::Text("HDR Waveform:");

        ImGui::Checkbox("Luminance Mode", &m_enableLuminanceMode);

        ImGui::InputInt("Min Value (Log10)", &m_log10MinValue);
        ImGui::InputInt("Max Value (Log10)", &m_log10MaxValue);

        m_log10MaxValue = std::max(m_log10MaxValue, m_log10MinValue);

        ImGui::InputFloat("Histogram Normalization Scale", &m_hdrWaveformHistogramNormalizationScale, 0.02f, 0.1f);

        int hdrWaveformScaleFactor = static_cast<int>(m_hdrWaveformResolutionScaleFactor);
        ImGui::InputInt("Display Resolution Scale", &hdrWaveformScaleFactor);

        // Note: Clamped to 2 due to maximum texture allocation supporting up to this much.
        m_hdrWaveformResolutionScaleFactor = static_cast<uint32_t>(std::max(hdrWaveformScaleFactor, 2));

        int hdrWaveformPosition[2] = { static_cast<int>(m_hdrWaveformPosition.x),  static_cast<int>(m_hdrWaveformPosition.y) };
        ImGui::InputInt2("Display Position", hdrWaveformPosition);

        m_hdrWaveformPosition.x = static_cast<uint32_t>(std::max(hdrWaveformPosition[0], 0));
        m_hdrWaveformPosition.y = static_cast<uint32_t>(std::max(hdrWaveformPosition[1], 0));
      }

      ImGui::Unindent();
    }

    ImGui::Checkbox("Enable GPU Printing On Press CTRL", &gpuPrint.enableObject());

    if (ImGui::CollapsingHeader("GPU Print", collapsingHeaderFlags)) {
      ImGui::Checkbox("Use Mouse Position", &gpuPrint.useMousePositionObject());
      if (!gpuPrint.useMousePosition()) {
        ImGui::DragInt2("Pixel Position", &gpuPrint.pixelIndexObject(), 0.1f, 0, INT32_MAX, "%d", sliderFlags);
      }
    }

    ImGui::PopID();
  }

  void DebugView::createConstantsBuffer() {
    DxvkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    info.stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
    info.access = VK_ACCESS_TRANSFER_WRITE_BIT;
    info.size = sizeof(DebugViewArgs);
    m_debugViewConstants = m_device->createBuffer(info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, DxvkMemoryStats::Category::RTXBuffer, "Debug View Constants Buffer");
  }

  uint32_t DebugView::getDebugViewIndex() const {
    return debugViewIdx();
  }

  void DebugView::setDebugViewIndex(uint32_t debugViewIndex) {
    debugViewIdxRef() = debugViewIndex;
    if (debugViewIndex != DEBUG_VIEW_DISABLED) {
      m_lastDebugViewIdx = debugViewIndex;
    }
  }

  void DebugView::resetNumAccumulatedFrames() {
    m_numFramesAccumulated = 0;
  }

  uint32_t DebugView::getActiveNumFramesToAccumulate() const {
    return shouldEnableAccumulation()
      ? numberOfFramesToAccumulate()
      : 1;
  }

  Rc<DxvkBuffer> DebugView::getDebugViewConstantsBuffer() {
    if (m_debugViewConstants == nullptr) {
      createConstantsBuffer();
    }
    assert(m_debugViewConstants != nullptr);
    return m_debugViewConstants;
  }

  void DebugView::onFrameBegin(
    Rc<DxvkContext>& ctx,
    const FrameBeginContext& frameBeginCtx) {
    ScopedCpuProfileZone();

    RtxPass::onFrameBegin(ctx, frameBeginCtx);

    // Initialize composite view
    if (static_cast<CompositeDebugView>(Composite::compositeViewIdx()) != CompositeDebugView::Disabled) {
      switch (static_cast<CompositeDebugView>(Composite::compositeViewIdx())) {
      case CompositeDebugView::FinalRenderWithMaterialProperties:
        m_composite.debugViewIndices = std::vector<uint32_t> { DEBUG_VIEW_POST_TONEMAP_OUTPUT, DEBUG_VIEW_ALBEDO, DEBUG_VIEW_SHADING_NORMAL, DEBUG_VIEW_PERCEPTUAL_ROUGHNESS, DEBUG_VIEW_EMISSIVE_RADIANCE, DEBUG_VIEW_HEIGHT_MAP };
        break;
      case CompositeDebugView::OpaqueMaterialTextureResolutionCheckers:
        m_composite.debugViewIndices = std::vector<uint32_t> { DEBUG_VIEW_OPAQUE_RAW_ALBEDO_RESOLUTION_CHECKERS, DEBUG_VIEW_OPAQUE_NORMAL_RESOLUTION_CHECKERS, DEBUG_VIEW_OPAQUE_ROUGHNESS_RESOLUTION_CHECKERS };

      default:
        break;
      }

      // Set active debug view index when composite view is active
      if (static_cast<CompositeDebugView>(Composite::compositeViewIdx()) != CompositeDebugView::Disabled) {
        if (!m_composite.debugViewIndices.empty()) {
          uint32_t frameIndex = ctx->getDevice()->getCurrentFrameId();
          debugViewIdxRef() = m_composite.debugViewIndices[frameIndex % m_composite.debugViewIndices.size()];
        } else {
          debugViewIdxRef() = DEBUG_VIEW_DISABLED;
        }
      }
    }

    if (!isActive()) {
      return;
    }

    // Handle accumulation settings
    {
      // Check if accumulation needs to be reset
      if (m_numFramesAccumulated > 0) {
        const RtCamera& camera = dynamic_cast<RtxContext*>(ctx.ptr())->getSceneManager().getCamera();
        const Matrix4d prevWorldToProjection = camera.getPreviousViewToProjection() * camera.getPreviousWorldToView();
        const Matrix4d worldToProjection = camera.getViewToProjection() * camera.getWorldToView();
        const bool hasCameraChanged = memcmp(&prevWorldToProjection, &worldToProjection, sizeof(Matrix4d)) != 0;

        if (hasCameraChanged) {
          resetNumAccumulatedFrames();
        }
      }

      // Ensure num frames stays within limits. 
      // This is called here again since the other place is called conditionally
      m_numFramesAccumulated = std::min(m_numFramesAccumulated, getActiveNumFramesToAccumulate());
    }

    // Clear debug view resources
    {
      VkClearColorValue clearColor;

      if (debugViewIdx() == DEBUG_VIEW_NAN) {
        clearColor = { 1.f, 0.f, 0.f, 0.f };
      } else {
        clearColor = { 0.f, 0.f, 0.f, 0.f };
      }

      VkImageSubresourceRange subRange = {};
      subRange.layerCount = 1;
      subRange.levelCount = 1;
      subRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

      ctx->clearColorImage(m_debugView.image, clearColor, subRange);

      const bool clearPreviousFramedDebugView = m_numFramesAccumulated == 0;

      if (clearPreviousFramedDebugView) {
        ctx->clearColorImage(m_previousFrameDebugView.image, clearColor, subRange);
      }

      if (debugViewIdx() == DEBUG_VIEW_INSTRUMENTATION_THREAD_DIVERGENCE) {
        ctx->clearColorImage(m_instrumentation.image, clearColor, subRange);
      }
    }
  }

  DebugViewArgs DebugView::getCommonDebugViewArgs(
    RtxContext& ctx,
    const Resources::RaytracingOutput& rtOutput,
    DxvkObjects& common) {
    const VkExtent3D debugViewResolution = shouldRunDispatchPostCompositePass()
      ? rtOutput.m_compositeOutputExtent
      : m_debugView.view->imageInfo().extent;    

    auto currTime = std::chrono::system_clock::now();
    std::chrono::duration<float> elapsedSec = currTime - m_startTime;
    const uint32_t frameIdx = ctx.getDevice()->getCurrentFrameId();

    DebugViewArgs debugViewArgs = {};

    debugViewArgs.debugViewIdx = debugViewIdx();
    debugViewArgs.colorCodeRadius = std::clamp(m_colorCodeRadius, 0, 8);

    debugViewArgs.enableInputQuantization = enableInputQuantization();
    debugViewArgs.quantizationStepSize = 1.0f / inverseQuantizationStepSize();
    debugViewArgs.quantizationInverseStepSize = inverseQuantizationStepSize();
    
    if (s_disableAnimation) {
      debugViewArgs.animationTimeSec = 0;
    } else {
      debugViewArgs.animationTimeSec = elapsedSec.count();
    }

    debugViewArgs.frameIdx = ctx.getDevice()->getCurrentFrameId();

    debugViewArgs.displayType = displayType();
    debugViewArgs.enableInfNanViewFlag = m_enableInfNanView;
    debugViewArgs.debugViewResolution.x = debugViewResolution.width;
    debugViewArgs.debugViewResolution.y = debugViewResolution.height;

    debugViewArgs.debugKnob = m_debugKnob;
    debugViewArgs.camera = rtOutput.m_raytraceArgs.camera;
    debugViewArgs.volumeArgs = rtOutput.m_raytraceArgs.volumeArgs;

    if (displayType() == DebugViewDisplayType::Standard) {
      debugViewArgs.pseudoColorMode = pseudoColorModeObject();
      debugViewArgs.enableAlphaChannelFlag = m_enableAlphaChannel;
      debugViewArgs.enableGammaCorrectionFlag = enableGammaCorrection();

      debugViewArgs.minValue = minValue();
      debugViewArgs.maxValue = maxValue();
      debugViewArgs.scale = m_scale;
    } else if (displayType() == DebugViewDisplayType::EV100) {
      assert(evMaxValue() >= evMinValue());

      debugViewArgs.evMinValue = evMinValue();
      debugViewArgs.evRange = evMaxValue() - evMinValue();
    } else if (displayType() == DebugViewDisplayType::HDRWaveform) {
      debugViewArgs.enableLuminanceModeFlag = m_enableLuminanceMode;

      assert(m_log10MaxValue >= m_log10MinValue);

      debugViewArgs.log10MinValue = m_log10MinValue;
      debugViewArgs.log10Range = m_log10MaxValue - m_log10MinValue;
      debugViewArgs.hdrWaveformResolution.x = debugViewResolution.width / m_hdrWaveformResolutionScaleFactor;
      debugViewArgs.hdrWaveformResolution.y = debugViewResolution.height / m_hdrWaveformResolutionScaleFactor;
      debugViewArgs.hdrWaveformPosition.x = m_hdrWaveformPosition.x;
      debugViewArgs.hdrWaveformPosition.y = m_hdrWaveformPosition.y;
      debugViewArgs.hdrWaveformResolutionScaleFactor = m_hdrWaveformResolutionScaleFactor;
      debugViewArgs.hdrWaveformHistogramNormalizationScale = m_hdrWaveformHistogramNormalizationScale;
    }

    debugViewArgs.samplerType = samplerType();

    debugViewArgs.isRTXDIConfidenceValid = rtOutput.getCurrentRtxdiConfidence().matchesWriteFrameIdx(frameIdx);

    RayPortalManager::SceneData portalData = common.getSceneManager().getRayPortalManager().getRayPortalInfoSceneData();
    debugViewArgs.numActiveRayPortals = portalData.numActiveRayPortals;
    memcpy(&debugViewArgs.rayPortalHitInfos[0], &portalData.rayPortalHitInfos, sizeof(portalData.rayPortalHitInfos));
    memcpy(&debugViewArgs .rayPortalHitInfos[maxRayPortalCount], &portalData.previousRayPortalHitInfos, sizeof(portalData.previousRayPortalHitInfos));


    // Todo: Add cases for secondary denoiser.
    if (RtxOptions::Get()->isSeparatedDenoiserEnabled()) {
      switch (debugViewIdx()) {
      case DEBUG_VIEW_DENOISED_PRIMARY_DIRECT_DIFFUSE_RADIANCE:
      case DEBUG_VIEW_DENOISED_PRIMARY_DIRECT_SPECULAR_RADIANCE:
      case DEBUG_VIEW_DENOISED_PRIMARY_DIRECT_DIFFUSE_HIT_T:
      case DEBUG_VIEW_DENOISED_PRIMARY_DIRECT_SPECULAR_HIT_T:
        debugViewArgs.nrd = common.metaPrimaryDirectLightDenoiser().getNrdArgs();
        break;
      case DEBUG_VIEW_DENOISED_PRIMARY_INDIRECT_DIFFUSE_RADIANCE:
      case DEBUG_VIEW_DENOISED_PRIMARY_INDIRECT_SPECULAR_RADIANCE:
      case DEBUG_VIEW_DENOISED_PRIMARY_INDIRECT_DIFFUSE_HIT_T:
      case DEBUG_VIEW_DENOISED_PRIMARY_INDIRECT_SPECULAR_HIT_T:
        debugViewArgs.nrd = common.metaPrimaryIndirectLightDenoiser().getNrdArgs();
        break;
      default: break;
      }
    } else {
      debugViewArgs.nrd = common.metaPrimaryCombinedLightDenoiser().getNrdArgs();
    }

    // Determine accumulation mode
    if (m_numFramesAccumulated == 0 || !shouldEnableAccumulation()) {
      debugViewArgs.accumulationMode = DebugViewAccumulationMode::WriteNewOutput;
    } else if (m_numFramesAccumulated < getActiveNumFramesToAccumulate()
      || enableContinuousAccumulation()) {
      debugViewArgs.accumulationMode = DebugViewAccumulationMode::BlendNewAndPreviousOutputs;
    } else { // m_numFramesAccumulated >= getActiveNumFramesToAccumulate()
      debugViewArgs.accumulationMode = DebugViewAccumulationMode::CarryOverPreviousOutput;
    }

    debugViewArgs.accumulationWeight = 1.f / (m_numFramesAccumulated + 1);
    debugViewArgs.enableFp16Accumulation = enableFp16Accumulation();
    debugViewArgs.copyOutputToCompositeOutput = shouldRunDispatchPostCompositePass() || replaceCompositeOutput();

    // Statistics params
    debugViewArgs.calculateStatistics = m_showOutputStatistics;
    debugViewArgs.statisticsMode = m_outputStatisticsMode;
    debugViewArgs.rcpNumOutputPixels = 1.f /
      (debugViewArgs.debugViewResolution.x * debugViewArgs.debugViewResolution.y);

    debugViewArgs.nrcArgs = rtOutput.m_raytraceArgs.nrcArgs;

    return debugViewArgs;
  }

  bool DebugView::shouldRunDispatchPostCompositePass() const {
    return replaceCompositeOutput() || (debugViewIdx() == DEBUG_VIEW_DISABLED && RtxOptions::useDenoiserReferenceMode());
  }

  bool DebugView::shouldEnableAccumulation() const {
    return debugViewIdx() != DEBUG_VIEW_DISABLED
      ? enableAccumulation()
      : RtxOptions::useDenoiserReferenceMode();
  }

  Rc<DxvkShader> DebugView::getDebugViewShader() const {
    const bool areOptionalExtensionsSupported = m_device->extensions().extShaderAtomicFloat;

    if (areOptionalExtensionsSupported) {
      return GET_SHADER_VARIANT(VK_SHADER_STAGE_COMPUTE_BIT, DebugViewShader, debug_view_using_optional_extensions);
    } else {
      return GET_SHADER_VARIANT(VK_SHADER_STAGE_COMPUTE_BIT, DebugViewShader, debug_view);
    }
  }

  void DebugView::dispatchDebugViewInternal(
    Rc<RtxContext> ctx,
    Rc<DxvkSampler> nearestSampler,
    Rc<DxvkSampler> linearSampler,
    DebugViewArgs& debugViewArgs,
    Rc<DxvkBuffer>& debugViewConstantBuffer,
    const Resources::RaytracingOutput& rtOutput,
    DxvkObjects& common) {
    ScopedGpuProfileZone(ctx, "Debug View");

    // Process output statistics now before we schedule a debug view dispatch that updates them.
    // We also do it here since it needs access to the rtOutput
    processOutputStatistics(ctx, rtOutput);

    // Inputs 

    const RtxGlobalVolumetrics& globalVolumetrics = ctx->getCommonObjects()->metaGlobalVolumetrics();
    ctx->bindResourceView(DEBUG_VIEW_BINDING_DENOISED_PRIMARY_DIRECT_DIFFUSE_RADIANCE_HIT_T_INPUT, rtOutput.m_primaryDirectDiffuseRadiance.view(Resources::AccessType::Read), nullptr);
    ctx->bindResourceView(DEBUG_VIEW_BINDING_DENOISED_PRIMARY_DIRECT_SPECULAR_RADIANCE_HIT_T_INPUT, rtOutput.m_primaryDirectSpecularRadiance.view(Resources::AccessType::Read), nullptr);
    ctx->bindResourceView(DEBUG_VIEW_BINDING_DENOISED_SECONDARY_COMBINED_DIFFUSE_RADIANCE_HIT_T_INPUT, rtOutput.m_secondaryCombinedDiffuseRadiance.view(Resources::AccessType::Read), nullptr);
    ctx->bindResourceView(DEBUG_VIEW_BINDING_DENOISED_SECONDARY_COMBINED_SPECULAR_RADIANCE_HIT_T_INPUT, rtOutput.m_secondaryCombinedSpecularRadiance.view(Resources::AccessType::Read), nullptr);
    ctx->bindResourceView(DEBUG_VIEW_BINDING_SHARED_FLAGS_INPUT, rtOutput.m_sharedFlags.view, nullptr);
    ctx->bindResourceView(DEBUG_VIEW_BINDING_PRIMARY_LINEAR_VIEW_Z_INPUT, rtOutput.m_primaryLinearViewZ.view, nullptr);
    ctx->bindResourceView(DEBUG_VIEW_BINDING_PRIMARY_VIRTUAL_WORLD_SHADING_NORMAL_PERCEPTUAL_ROUGHNESS_INPUT, rtOutput.m_primaryVirtualWorldShadingNormalPerceptualRoughness.view, nullptr);
    ctx->bindResourceView(DEBUG_VIEW_BINDING_PRIMARY_VIRTUAL_MOTION_VECTOR_INPUT, rtOutput.m_primaryVirtualMotionVector.view, nullptr);
    ctx->bindResourceView(DEBUG_VIEW_BINDING_PRIMARY_SCREEN_SPACE_MOTION_VECTOR_INPUT, rtOutput.m_primaryScreenSpaceMotionVector.view, nullptr);
    ctx->bindResourceView(DEBUG_VIEW_BINDING_RTXDI_CONFIDENCE_INPUT, rtOutput.getCurrentRtxdiConfidence().view(Resources::AccessType::Read, debugViewArgs.isRTXDIConfidenceValid), nullptr);
    const bool isFinalOutputReadInShader = !shouldRunDispatchPostCompositePass();
    ctx->bindResourceView(DEBUG_VIEW_BINDING_FINAL_SHADING_INPUT, rtOutput.m_finalOutput.view(Resources::AccessType::Read, isFinalOutputReadInShader), nullptr);
    ctx->bindResourceView(DEBUG_VIEW_BINDING_INSTRUMENTATION_INPUT, m_instrumentation.view, nullptr);
    ctx->bindResourceView(DEBUG_VIEW_BINDING_VOLUME_RESERVOIRS, globalVolumetrics.getPreviousVolumeReservoirs().view, nullptr);
    ctx->bindResourceView(DEBUG_VIEW_BINDING_VOLUME_AGE, globalVolumetrics.getCurrentVolumeAccumulatedRadianceAge().view, nullptr);
    ctx->bindResourceSampler(DEBUG_VIEW_BINDING_VOLUME_AGE, linearSampler);
    ctx->bindResourceView(DEBUG_VIEW_BINDING_VOLUME_RADIANCE_Y, globalVolumetrics.getCurrentVolumeAccumulatedRadianceY().view, nullptr);
    ctx->bindResourceSampler(DEBUG_VIEW_BINDING_VOLUME_RADIANCE_Y, linearSampler);
    ctx->bindResourceView(DEBUG_VIEW_BINDING_VOLUME_RADIANCE_COCG, globalVolumetrics.getCurrentVolumeAccumulatedRadianceCoCg().view, nullptr);
    ctx->bindResourceSampler(DEBUG_VIEW_BINDING_VOLUME_RADIANCE_COCG, linearSampler);
    ctx->bindResourceView(DEBUG_VIEW_BINDING_VALUE_NOISE_SAMPLER, common.getResources().getValueNoiseLut(ctx), nullptr);
    Rc<DxvkSampler> valueNoiseSampler = common.getResources().getSampler(VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_REPEAT);
    ctx->bindResourceSampler(DEBUG_VIEW_BINDING_VALUE_NOISE_SAMPLER, valueNoiseSampler);
    ctx->bindResourceView(DEBUG_VIEW_BINDING_BLUE_NOISE_TEXTURE, common.getResources().getBlueNoiseTexture(ctx), nullptr);
    
    const ReplacementMaterialTextureType::Enum terrainTextureType = static_cast<ReplacementMaterialTextureType::Enum>(
      clamp<uint32_t>(static_cast<uint32_t>(m_debugKnob.x),
                      ReplacementMaterialTextureType::AlbedoOpacity,
                      ReplacementMaterialTextureType::Count - 1));
    Resources::Resource terrain = common.getSceneManager().getTerrainBaker().getTerrainTexture(terrainTextureType);
    ctx->bindResourceView(DEBUG_VIEW_BINDING_TERRAIN_INPUT, terrain.view, nullptr);
    ctx->bindResourceView(DEBUG_VIEW_BINDING_INPUT, m_debugView.view, nullptr);

    // NRD Validation Layer bindings
    {
      DxvkDenoise& denoiser0 = RtxOptions::Get()->isSeparatedDenoiserEnabled() 
        ? ctx->getCommonObjects()->metaPrimaryDirectLightDenoiser() 
        : ctx->getCommonObjects()->metaPrimaryCombinedLightDenoiser();
      DxvkDenoise& denoiser1 = ctx->getCommonObjects()->metaPrimaryIndirectLightDenoiser();
      DxvkDenoise& denoiser2 = ctx->getCommonObjects()->metaSecondaryCombinedLightDenoiser();

      switch (debugViewIdx()) {
      case DEBUG_VIEW_NRD_INSTANCE_0_VALIDATION_LAYER:
        ctx->bindResourceView(DEBUG_VIEW_NRD_VALIDATION_LAYER_INPUT, denoiser0.getNrdContext().getValidationTexture().view, nullptr);
        break;
      case DEBUG_VIEW_NRD_INSTANCE_1_VALIDATION_LAYER:
        ctx->bindResourceView(DEBUG_VIEW_NRD_VALIDATION_LAYER_INPUT, denoiser1.getNrdContext().getValidationTexture().view, nullptr);
        break;
      case DEBUG_VIEW_NRD_INSTANCE_2_VALIDATION_LAYER:
        ctx->bindResourceView(DEBUG_VIEW_NRD_VALIDATION_LAYER_INPUT, denoiser2.getNrdContext().getValidationTexture().view, nullptr);
        break;
      default:
        ctx->bindResourceView(DEBUG_VIEW_NRD_VALIDATION_LAYER_INPUT, m_debugView.view, nullptr);
        break;
      }
    }

    // Inputs / Outputs

    ctx->bindResourceView(DEBUG_VIEW_BINDING_HDR_WAVEFORM_RED_INPUT_OUTPUT, m_hdrWaveformRed.view, nullptr);
    ctx->bindResourceView(DEBUG_VIEW_BINDING_HDR_WAVEFORM_GREEN_INPUT_OUTPUT, m_hdrWaveformGreen.view, nullptr);
    ctx->bindResourceView(DEBUG_VIEW_BINDING_HDR_WAVEFORM_BLUE_INPUT_OUTPUT, m_hdrWaveformBlue.view, nullptr);

    assert(rtOutput.m_compositeOutput.ownsResource() && "Composite output is expected to be valid at this point by default");
    ctx->bindResourceView(DEBUG_VIEW_BINDING_COMPOSITE_OUTPUT_INPUT_OUTPUT, rtOutput.m_compositeOutput.view(Resources::AccessType::ReadWrite), nullptr);

    ctx->bindResourceView(DEBUG_VIEW_BINDING_PREVIOUS_FRAME_INPUT_OUTPUT, m_previousFrameDebugView.view, nullptr);
    
    ctx->bindResourceBuffer(DEBUG_VIEW_BINDING_CONSTANTS_INPUT, DxvkBufferSlice(debugViewConstantBuffer, 0, debugViewConstantBuffer->info().size));
    ctx->bindResourceSampler(DEBUG_VIEW_BINDING_NEAREST_SAMPLER, nearestSampler);
    ctx->bindResourceSampler(DEBUG_VIEW_BINDING_LINEAR_SAMPLER, linearSampler);

    // Outputs

    const uint32_t frameIdx = ctx->getDevice()->getCurrentFrameId();
    VkDeviceSize statisticsBufferOffset = (frameIdx % kMaxFramesInFlight) * sizeof(m_outputStatistics);
    ctx->bindResourceBuffer(DEBUG_VIEW_BINDING_STATISTICS_BUFFER_OUTPUT, DxvkBufferSlice(m_statisticsBuffer, statisticsBufferOffset, m_statisticsBuffer->info().size));
    ctx->bindResourceView(DEBUG_VIEW_BINDING_OUTPUT, m_postprocessedDebugView.view, nullptr);

    ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, getDebugViewShader());

    const VkExtent3D outputExtent = VkExtent3D { debugViewArgs.debugViewResolution.x, debugViewArgs.debugViewResolution.y, 1 };

    const VkExtent3D workgroups = util::computeBlockCount(outputExtent, VkExtent3D { 16, 8, 1 });
    ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);

    // Reset the count if the cap was lowered below current count in the midst
    if (getActiveNumFramesToAccumulate() < m_numFramesAccumulated) {
      resetNumAccumulatedFrames();
    }

    // Clamp the increase since dispatch is run every frame regardless of the cap being hit
    m_numFramesAccumulated = std::min(m_numFramesAccumulated + 1, getActiveNumFramesToAccumulate());
  }

  void DebugView::dispatch(
    Rc<RtxContext> ctx,
    Rc<DxvkSampler> nearestSampler,
    Rc<DxvkSampler> linearSampler,
    Rc<DxvkImage>& outputImage,
    const Resources::RaytracingOutput& rtOutput,
    DxvkObjects& common) {

    if (m_showCachedImage) {
      if (m_cachedImage.image.ptr()) {
        outputImage = m_cachedImage.image;
      }
    } else if (debugViewIdx() != DEBUG_VIEW_DISABLED &&
               !shouldRunDispatchPostCompositePass()) {
      // Dispatch a debug view pass

      DebugViewArgs&& debugViewArgs = getCommonDebugViewArgs(*ctx.ptr(), rtOutput, common);

      Rc<DxvkBuffer> cb = getDebugViewConstantsBuffer();
      ctx->writeToBuffer(cb, 0, sizeof(DebugViewArgs), &debugViewArgs);
      ctx->getCommandList()->trackResource<DxvkAccess::Read>(cb);

      if (displayType() == DebugViewDisplayType::HDRWaveform) {
        // Clear HDR Waveform textures when in use before accumulated into

        VkClearColorValue clearColor;
        clearColor.uint32[0] = clearColor.uint32[1] = clearColor.uint32[2] = clearColor.uint32[3] = 0;

        VkImageSubresourceRange subRange = {};
        subRange.layerCount = 1;
        subRange.levelCount = 1;
        subRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

        ctx->clearColorImage(m_hdrWaveformRed.image, clearColor, subRange);
        ctx->clearColorImage(m_hdrWaveformGreen.image, clearColor, subRange);
        ctx->clearColorImage(m_hdrWaveformBlue.image, clearColor, subRange);
      }

      // Dispatch Debug View 
      dispatchDebugViewInternal(ctx, nearestSampler, linearSampler, debugViewArgs, cb, rtOutput, common);
      
      // Display HDR Waveform
      if (displayType() == DebugViewDisplayType::HDRWaveform) {
        ScopedGpuProfileZone(ctx, "HDR Waveform Render");

        ctx->bindResourceView(DEBUG_VIEW_WAVEFORM_RENDER_BINDING_HDR_WAVEFORM_RED_INPUT, m_hdrWaveformRed.view, nullptr);
        ctx->bindResourceView(DEBUG_VIEW_WAVEFORM_RENDER_BINDING_HDR_WAVEFORM_GREEN_INPUT, m_hdrWaveformGreen.view, nullptr);
        ctx->bindResourceView(DEBUG_VIEW_WAVEFORM_RENDER_BINDING_HDR_WAVEFORM_BLUE_INPUT, m_hdrWaveformBlue.view, nullptr);
        ctx->bindResourceView(DEBUG_VIEW_WAVEFORM_RENDER_BINDING_INPUT_OUTPUT, m_postprocessedDebugView.view, nullptr);
        ctx->bindResourceBuffer(DEBUG_VIEW_WAVEFORM_RENDER_BINDING_CONSTANTS_INPUT, DxvkBufferSlice(cb, 0, cb->info().size));

        ctx->bindShader(VK_SHADER_STAGE_COMPUTE_BIT, DebugViewWaveformRenderShader::getShader());

        VkExtent3D waveformResolution = m_postprocessedDebugView.view->imageInfo().extent;

        waveformResolution.width /= m_hdrWaveformResolutionScaleFactor;
        waveformResolution.height /= m_hdrWaveformResolutionScaleFactor;

        VkExtent3D workgroups = util::computeBlockCount(waveformResolution, VkExtent3D { 16, 8, 1 });
        ctx->dispatch(workgroups.width, workgroups.height, workgroups.depth);
      }

      // Replace RT 
      outputImage = m_postprocessedDebugView.image;

      // Generate a composite image
      generateCompositeImage(ctx, outputImage);
    }

    // Cache current output image
    if (m_cacheCurrentImage) {
      if (!m_cachedImage.image.ptr() ||
          m_cachedImage.image->info().extent.width != outputImage->info().extent.width ||
          m_cachedImage.image->info().extent.height != outputImage->info().extent.height ||
          m_cachedImage.image->info().format != outputImage->info().format) {
        Rc<DxvkContext> dxvkContext = ctx;
        m_cachedImage = Resources::createImageResource(dxvkContext, "debug view cache", outputImage->info().extent, outputImage->info().format);
      }

      const VkImageSubresourceLayers srcSubresourceLayers = { outputImage->formatInfo()->aspectMask, 0, 0, 1 };
      const VkImageSubresourceLayers dstSubresourceLayers = { m_cachedImage.image->formatInfo()->aspectMask, 0, 0, 1 };

      ctx->copyImage(
        m_cachedImage.image, dstSubresourceLayers, VkOffset3D { 0, 0, 0 },
        outputImage, srcSubresourceLayers, VkOffset3D { 0, 0, 0 },
        outputImage->info().extent);

      m_cacheCurrentImage = false;
    }
  }

  void DebugView::dispatchAfterCompositionPass(
    Rc<RtxContext> ctx,
    Rc<DxvkSampler> nearestSampler,
    Rc<DxvkSampler> linearSampler,
    const Resources::RaytracingOutput& rtOutput,
    DxvkObjects& common) {

    if (!shouldRunDispatchPostCompositePass()) {
      return;
    }

    DebugViewArgs&& debugViewArgs = getCommonDebugViewArgs(*ctx.ptr(), rtOutput, common);

    if (RtxOptions::useDenoiserReferenceMode() && debugViewArgs.debugViewIdx == DEBUG_VIEW_DISABLED) {
      debugViewArgs.debugViewIdx = DEBUG_VIEW_COMPOSITE_OUTPUT;
    }

    Rc<DxvkBuffer> cb = getDebugViewConstantsBuffer();
    ctx->writeToBuffer(cb, 0, sizeof(DebugViewArgs), &debugViewArgs);
    ctx->getCommandList()->trackResource<DxvkAccess::Read>(cb);

    // Dispatch Debug View 
    dispatchDebugViewInternal(ctx, nearestSampler, linearSampler, debugViewArgs, cb, rtOutput, common);
  }

  void DebugView::generateCompositeImage(Rc<DxvkContext> ctx,
                                         Rc<DxvkImage>& outputImage) {
    static CompositeDebugView sCompositeIdxUsedPreviousFrame = CompositeDebugView::Disabled;

    // Blit the debug view image into the composite image 
    if (static_cast<CompositeDebugView>(Composite::compositeViewIdx()) != CompositeDebugView::Disabled &&
        !m_composite.debugViewIndices.empty()) {

      // Ensure composite resource is valid
      if (!m_composite.compositeView.image.ptr() ||
            m_composite.compositeView.image->info().extent.width != outputImage->info().extent.width ||
            m_composite.compositeView.image->info().extent.height != outputImage->info().extent.height ||
            m_composite.compositeView.image->info().format != outputImage->info().format) {
        m_composite.compositeView = Resources::createImageResource(ctx, "composite debug view", outputImage->info().extent, outputImage->info().format);
      }

      // Lookup src & dest image properties
      DxvkImageCreateInfo srcDesc = m_postprocessedDebugView.image->info();
      DxvkImageCreateInfo dstDesc = m_composite.compositeView.image->info();
      const VkExtent3D srcExtent = srcDesc.extent;
      const VkExtent3D dstExtent = srcDesc.extent;
      const VkImageSubresourceLayers srcSubresourceLayers = { imageFormatInfo(srcDesc.format)->aspectMask, 0, 0, 1 };
      const VkImageSubresourceLayers dstSubresourceLayers = { imageFormatInfo(dstDesc.format)->aspectMask, 0, 0, 1 };

      // Calculate composite grid dimensions & current grid index 
      const uint32_t numImages = m_composite.debugViewIndices.size();
      uvec2 compositeGridDims;
      compositeGridDims.x = static_cast<uint32_t>(ceilf(sqrtf(static_cast<float>(numImages))));
      compositeGridDims.y = static_cast<uint32_t>(ceilf(static_cast<float>(numImages) / compositeGridDims.x));

      uint32_t frameIndex = ctx->getDevice()->getCurrentFrameId();
      uint32_t compositeIndex = frameIndex % m_composite.debugViewIndices.size();
      uvec2 compositeGridIndex;
      compositeGridIndex.y = compositeIndex / compositeGridDims.x;
      compositeGridIndex.x = compositeIndex - compositeGridIndex.y * compositeGridDims.x;

      VkExtent2D gridICellImageDims = {
        dstExtent.width / compositeGridDims.x,
        dstExtent.height / compositeGridDims.y
      };

      // Blit region extents
      VkImageBlit region = {};
      region.srcSubresource = srcSubresourceLayers;
      region.srcOffsets[0] = VkOffset3D { 0,0,0 };
      region.srcOffsets[1] = VkOffset3D { int32_t(srcExtent.width), int32_t(srcExtent.height), int32_t(srcExtent.depth) };
      region.dstSubresource = dstSubresourceLayers;
      region.dstOffsets[0] = {
        int32_t(compositeGridIndex.x * gridICellImageDims.width),
        int32_t(compositeGridIndex.y * gridICellImageDims.height),
        0 };
      region.dstOffsets[1] = {
        int32_t(region.dstOffsets[0].x + gridICellImageDims.width),
        int32_t(region.dstOffsets[0].y + gridICellImageDims.height),
        int32_t(dstExtent.depth) };

      // Clear the composite on first use for a given composite view type
      if (static_cast<CompositeDebugView>(Composite::compositeViewIdx()) != sCompositeIdxUsedPreviousFrame) {
        VkClearColorValue clearColor = { 0.f, 0.f, 0.f, 0.f };

        VkImageSubresourceRange subRange = {};
        subRange.layerCount = 1;
        subRange.levelCount = 1;
        subRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

        ctx->clearColorImage(m_composite.compositeView.image, clearColor, subRange);
      }

      VkComponentMapping identityMap = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };

      // Blit debug view image to the composite image
      // Using nearest filter as linear interpolation may produce invalid values for some debug view data (i.e. geometry hash)
      ctx->blitImage(m_composite.compositeView.image, identityMap, m_postprocessedDebugView.image, identityMap, region, VK_FILTER_NEAREST);

      outputImage = m_composite.compositeView.image;

    } else if (m_composite.compositeView.image.ptr()) {
      // Composite view is not used, release the resource
      m_composite.compositeView.reset();
    }

    sCompositeIdxUsedPreviousFrame = static_cast<CompositeDebugView>(Composite::compositeViewIdx());
  }

  void DebugView::createDownscaledResource(Rc<DxvkContext>& ctx, const VkExtent3D& downscaledExtent) {
    // Debug
    m_debugView = Resources::createImageResource(ctx, "debug view", downscaledExtent, VK_FORMAT_R32G32B32A32_SFLOAT);
    m_postprocessedDebugView = Resources::createImageResource(ctx, "postprocessed debug view", downscaledExtent, VK_FORMAT_R32G32B32A32_SFLOAT);
    m_previousFrameDebugView = Resources::createImageResource(ctx, "previous frame debug view", downscaledExtent, VK_FORMAT_R32G32B32A32_SFLOAT);
    
    // Note: Only allocate half resolution for HDR waveform buffers, this is the default view size
    // and while it is wasteful if the resolution scale is higher, this is probably fine.
    m_hdrWaveformRed = Resources::createImageResource(ctx, "debug hdr waveform red", { (downscaledExtent.width + 2) / 2, (downscaledExtent.height + 2) / 2, 1 }, VK_FORMAT_R32_UINT);
    m_hdrWaveformBlue = Resources::createImageResource(ctx, "debug hdr waveform green", { (downscaledExtent.width + 2) / 2, (downscaledExtent.height + 2) / 2, 1 }, VK_FORMAT_R32_UINT);
    m_hdrWaveformGreen = Resources::createImageResource(ctx, "debug hdr waveform blue", { (downscaledExtent.width + 2) / 2, (downscaledExtent.height + 2) / 2, 1 }, VK_FORMAT_R32_UINT);

    // Instrumentation
    m_instrumentation = Resources::createImageResource(ctx, "debug instrumentation", downscaledExtent, VK_FORMAT_R32_UINT);

    resetNumAccumulatedFrames();
  }

  void DebugView::releaseDownscaledResource() {
    m_debugView.reset();
    m_postprocessedDebugView.reset();
    m_previousFrameDebugView.reset();
    m_hdrWaveformRed.reset();
    m_hdrWaveformBlue.reset();
    m_hdrWaveformGreen.reset();
    m_instrumentation.reset();
  }

  bool DebugView::isEnabled() const {
    return debugViewIdx() != DEBUG_VIEW_DISABLED || 
      static_cast<CompositeDebugView>(m_composite.compositeViewIdx()) != CompositeDebugView::Disabled ||
      m_showCachedImage || m_cacheCurrentImage ||
      RtxOptions::useDenoiserReferenceMode();
  }
} // namespace dxvk
