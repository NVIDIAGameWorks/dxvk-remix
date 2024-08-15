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

#include <algorithm>
#include <unordered_set>
#include <cassert>
#include <limits>

#include "../util/util_keybind.h"
#include "../util/config/config.h"
#include "../util/xxHash/xxhash.h"
#include "../util/util_math.h"
#include "../util/util_env.h"
#include "rtx_utils.h"
#include "rtx/concept/ray_portal/ray_portal.h"
#include "rtx_volume_integrate.h"
#include "rtx_pathtracer_gbuffer.h"
#include "rtx_pathtracer_integrate_direct.h"
#include "rtx_pathtracer_integrate_indirect.h"
#include "rtx_dlss.h"
#include "rtx_ray_reconstruction.h"
#include "rtx_materials.h"
#include "rtx/pass/material_args.h"
#include "rtx_option.h"
#include "rtx_hashing.h"

enum _NV_GPU_ARCHITECTURE_ID;
typedef enum _NV_GPU_ARCHITECTURE_ID NV_GPU_ARCHITECTURE_ID;
enum _NV_GPU_ARCH_IMPLEMENTATION_ID;
typedef enum _NV_GPU_ARCH_IMPLEMENTATION_ID NV_GPU_ARCH_IMPLEMENTATION_ID;

// RTX specific options

namespace dxvk {
  class DxvkDevice;

  using RenderPassVolumeIntegrateRaytraceMode = DxvkVolumeIntegrate::RaytraceMode;
  using RenderPassGBufferRaytraceMode = DxvkPathtracerGbuffer::RaytraceMode;
  using RenderPassIntegrateDirectRaytraceMode = DxvkPathtracerIntegrateDirect::RaytraceMode;
  using RenderPassIntegrateIndirectRaytraceMode = DxvkPathtracerIntegrateIndirect::RaytraceMode;

  // DLSS-RR is not listed here, because it's considered as a special mode of DLSS
  enum class UpscalerType : int {
    None = 0,
    DLSS,
    NIS,
    TAAU
  };

  enum class GraphicsPreset : int {
    Ultra = 0,
    High,
    Medium,
    Low,
    Custom,
    // Note: Used to automatically have the graphics preset set on initialization, not used beyond this case
    // as it should be overridden by one of the other values by the time any other code uses it.
    Auto
  };

  enum class RaytraceModePreset {
    Custom = 0,
    Auto = 1
  };

  enum class DlssPreset : int {
    Off = 0,
    On,
    Custom
  };

  enum class NisPreset : int {
    Performance = 0,
    Balanced,
    Quality,
    Fullscreen
  };

  enum class TaauPreset : int {
    Performance = 0,
    Balanced,
    Quality,
    Fullscreen
  };

  enum class CameraAnimationMode : int {
    CameraShake_LeftRight = 0,
    CameraShake_FrontBack,
    CameraShake_Yaw,
    CameraShake_Pitch,
    YawRotation
  };

  enum class TonemappingMode : int {
    Global = 0,
    Local
  };

  enum class UIType : int {
    None = 0,
    Basic,
    Advanced,
    Count
  };

  enum class ReflexMode : int {
    None = 0,
    LowLatency,
    LowLatencyBoost
  };

  enum class FusedWorldViewMode : int {
    None = 0,
    View,
    World
  };
  
  enum class SkyAutoDetectMode : int {
    None = 0,
    CameraPosition,
    CameraPositionAndDepthFlags
  };

  enum class EnableVsync : int {
    Off = 0,
    On = 1,
    WaitingForImplicitSwapchain = 2   // waiting for the app to create the device + implicit swapchain, we latch the vsync setting from there
  };

  class RtxOptions {
    friend class ImGUI; // <-- we want to modify these values directly.
    friend class ImGuiSplash; // <-- we want to modify these values directly.
    friend class ImGuiCapture; // <-- we want to modify these values directly.
    friend class RtxContext; // <-- we want to modify these values directly.
    friend class RtxInitializer; // <-- we want to modify these values directly.

    RW_RTX_OPTION("rtx", fast_unordered_set, lightmapTextures, {},
                  "Textures used for lightmapping (baked static lighting on surfaces) in older games.\n"
                  "These textures will be ignored when attempting to determine the desired textures from a draw to use for ray tracing.");
    RW_RTX_OPTION("rtx", fast_unordered_set, skyBoxTextures, {},
                  "Textures on draw calls used for the sky or are otherwise intended to be very far away from the camera at all times (no parallax).\n"
                  "Any draw calls using a texture in this list will be treated as sky and rendered as such in a manner different from typical geometry.");    
    RW_RTX_OPTION("rtx", fast_unordered_set, skyBoxGeometries, {},
                  "Geometries from draw calls used for the sky or are otherwise intended to be very far away from the camera at all times (no parallax).\n"
                  "Any draw calls using a geometry hash in this list will be treated as sky and rendered as such in a manner different from typical geometry.\n"
                  "The geometry hash being used for sky detection is based off of the asset hash rule, see: \"rtx.geometryAssetHashRuleString\".");
    RW_RTX_OPTION("rtx", fast_unordered_set, ignoreTextures, {},
                  "Textures on draw calls that should be ignored.\n"
                  "Any draw call using an ignore texture will be skipped and not ray traced, useful for removing undesirable rasterized effects or geometry not suitable for ray tracing.");
    RW_RTX_OPTION("rtx", fast_unordered_set, ignoreLights, {},
                  "Lights that should be ignored.\nAny matching light will be skipped and not added to be ray traced.");
    RW_RTX_OPTION("rtx", fast_unordered_set, uiTextures, {},
                  "Textures on draw calls that should be treated as screenspace UI elements.\n"
                  "All exclusively UI-related textures should be classified this way and doing so allows the UI to be rasterized on top of the ray traced scene like usual.\n"
                  "Note that currently the first UI texture encountered triggers RTX injection (though this may change in the future as this does cause issues with games that draw UI mid-frame).");
    RW_RTX_OPTION("rtx", fast_unordered_set, worldSpaceUiTextures, {},
                  "Textures on draw calls that should be treated as worldspace UI elements.\n"
                  "Unlike typical UI textures this option is useful for improved rendering of UI elements which appear as part of the scene (moving around in 3D space rather than as a screenspace element).");
    RW_RTX_OPTION("rtx", fast_unordered_set, worldSpaceUiBackgroundTextures, {}, 
                  "Hack/workaround option for dynamic world space UI textures with a coplanar background.\n"
                  "Apply to backgrounds if the foreground material is a dynamic world texture rendered in UI that is unpredictable and rapidly changing.\n"
                  "This offsets the background texture backwards.");
    RW_RTX_OPTION("rtx", fast_unordered_set, hideInstanceTextures, {},
                  "Textures on draw calls that should be hidden from rendering, but not totally ignored.\n"
                  "This is similar to rtx.ignoreTextures but instead of completely ignoring such draw calls they are only hidden from rendering, allowing for the hidden objects to still appear in captures.\n"
                  "As such, this is mostly only a development tool to hide objects during development until they are properly replaced, otherwise the objects should be ignored with rtx.ignoreTextures instead for better performance.");
    RW_RTX_OPTION("rtx", fast_unordered_set, playerModelTextures, {}, "");
    RW_RTX_OPTION("rtx", fast_unordered_set, playerModelBodyTextures, {}, "");
    RW_RTX_OPTION("rtx", fast_unordered_set, lightConverter, {}, "");
    RW_RTX_OPTION("rtx", fast_unordered_set, particleTextures, {},
                  "Textures on draw calls that should be treated as particles.\n"
                  "When objects are marked as particles more approximate rendering methods are leveraged allowing for more effecient and typically better looking particle rendering.\n"
                  "Generally any billboard-like blended particle objects in the original application should be classified this way.");
    RW_RTX_OPTION("rtx", fast_unordered_set, beamTextures, {},
                  "Textures on draw calls that are already particles or emissively blended and have beam-like geometry.\n"
                  "Typically objects marked as particles or objects using emissive blending will be rendered with a special method which allows re-orientation of the billboard geometry assumed to make up the draw call in indirect rays (reflections for example).\n"
                  "This method works fine for typical particles, but some (e.g. a laser beam) may not be well-represented with the typical billboard assumption of simply needing to rotate around its centroid to face the view direction.\n"
                  "To handle such cases a different beam mode is used to treat objects as more of a cylindrical beam and re-orient around its main spanning axis, allowing for better rendering of these beam-like effect objects.");
    RW_RTX_OPTION("rtx", fast_unordered_set, decalTextures, {},
                  "Textures on draw calls used for static geometric decals or decals with complex topology.\n"
                  "These materials will be blended over the materials underneath them when decal material blending is enabled.\n"
                  "A small configurable offset is applied to each flat/co-planar part of these decals to prevent coplanar geometric cases (which poses problems for ray tracing).");
    // Todo: Deprecation/aliasing macro here for dynamicDecalTextures/singleOffsetDecalTextures/nonOffsetDecalTextures to not have to manually handle their
    // aliasing to decalTextures, or the inclusion of the deprecation notice in their documentation.
    RW_RTX_OPTION("rtx", fast_unordered_set, dynamicDecalTextures, {},
                  "Warning: This option is deprecated, please use rtx.decalTextures instead.\n"
                  "Textures on draw calls used for dynamically spawned geometric decals, such as bullet holes.\n"
                  "These materials will be blended over the materials underneath them when decal material blending is enabled.\n"
                  "A small configurable offset is applied to each quad part of these decals to prevent coplanar geometric cases (which poses problems for ray tracing).");
    RW_RTX_OPTION("rtx", fast_unordered_set, singleOffsetDecalTextures, {},
                  "Warning: This option is deprecated, please use rtx.decalTextures instead.\n"
                  "Textures on draw calls used for geometric decals that don't inter-overlap for a given texture hash. Textures must be tagged as \"Decal Texture\" or \"Dynamic Decal Texture\" to apply.\n"
                  "Applies a single shared offset to all the batched decal geometry rendered in a given draw call, rather than increasing offset per decal within the batch (i.e. a quad in case of \"Dynamic Decal Texture\").\n"
                  "Note, the offset adds to the global offset among all decals drawn with different draw calls.\n"
                  "The decal textures tagged this way must not inter-overlap within a batch / single draw call since the same offset is applied to all of them.\n"
                  "Applying a single offset is useful for stabilizing decal offsets when a game dynamically batches decals together.\n"
                  "In addition, it makes the global decal offset index grow slower and thus it minimizes a chance of hitting the \"rtx.decals.maxOffsetIndex limit\".");
    RW_RTX_OPTION("rtx", fast_unordered_set, nonOffsetDecalTextures, {},
                  "Warning: This option is deprecated, please use rtx.decalTextures instead.\n"
                  "Textures on draw calls used for geometric decals with arbitrary topology that are already offset from the base geometry.\n"
                  "These materials will be blended over the materials underneath them when decal material blending is enabled.\n"
                  "Unlike typical decals however these decals have no offset applied to them due assuming the offset is already being done by whatever is passing data to Remix.");
    RW_RTX_OPTION("rtx", fast_unordered_set, terrainTextures, {}, "Albedo textures that are baked blended together to form a unified terrain texture used during ray tracing.\n"
                                                                  "Put albedo textures into this category if the game renders terrain as a blend of multiple textures.");
    RW_RTX_OPTION("rtx", fast_unordered_set, opacityMicromapIgnoreTextures, {}, "Textures to ignore when generating Opacity Micromaps. This generally does not have to be set and is only useful for black listing problematic cases for Opacity Micromap usage.");
    RW_RTX_OPTION("rtx", fast_unordered_set, animatedWaterTextures, {},
                  "Textures on draw calls to be treated as \"animated water\".\n"
                  "Objects with this flag applied will animate their normals to fake a basic water effect based on the layered water material parameters, and only when rtx.opaqueMaterial.layeredWaterNormalEnable is set to true.\n"
                  "Should typically be used on static water planes that the original application may have relied on shaders to animate water on.");
    RW_RTX_OPTION("rtx", fast_unordered_set, ignoreBakedLightingTextures, {},
                  "Textures for which to ignore two types of baked lighting, Texture Factors and Vertex Color.\n\n"
                  "Texture Factor disablement:\n"
                  "Using this feature on selected textures will eliminate the texture factors.\n"
                  "For instance, if a game bakes lighting information into the Texture Factor for particular textures, applying this option will remove them.\n"
                  "This becomes useful when unexpected results occur due to the Texture Factor.\n"
                  "Consider an example where the original texture contains red tints baked into the Texture Factor. If a user replaces the texture, it will blend with the red tints, resulting in an undesirable reddish outcome.\n"
                  "In such cases, users can employ this option to eliminate the unwanted tints from their replacement textures.\n"
                  "Similarly, users can tag textures if shadows are baked into the Texture Factor, causing the replacing texture to appear darker than anticipated.\n\n"
                  "Vertex Color disablement:\n"
                  "Using this feature on selected textures will eliminate the vertex colors.\n\n"
                  "Note, enabling this setting will automatically disable multiple-stage texture factor blendings for the selected textures.\n"
                  "Only use this option when necessary, as the Texture Factor and Vertex Color can be used for simulating various texture effects, tagging a texture with this option will unexpectedly eliminate these effects.");
    RW_RTX_OPTION("rtx", fast_unordered_set, ignoreAlphaOnTextures, {}, 
                  "Textures for which to ignore the alpha channel of the legacy colormap. Textures will be rendered fully opaque as a result.");
    RW_RTX_OPTION("rtx.antiCulling", fast_unordered_set, antiCullingTextures, {},
                  "Textures that are forced to extend life length when anti-culling is enabled.\n"
                  "Some games use different culling methods we can't fully match, use this option to manually add textures to force extend their life when anti-culling fails.");
    RW_RTX_OPTION("rtx.postfx", fast_unordered_set, motionBlurMaskOutTextures, {}, "Disable motion blur for meshes with specific texture.");

    RW_RTX_OPTION("rtx", std::string, geometryGenerationHashRuleString, "positions,indices,texcoords,geometrydescriptor,vertexlayout,vertexshader",
                  "Defines which asset hashes we need to generate via the geometry processing engine.");
    RW_RTX_OPTION("rtx", std::string, geometryAssetHashRuleString, "positions,indices,geometrydescriptor",
                  "Defines which hashes we need to include when sampling from replacements and doing USD capture.");
    
  public:
    RTX_OPTION("rtx", bool, showRaytracingOption, true, "Enables or disables the option to toggle ray tracing in the UI. When set to false the ray tracing checkbox will not appear in the Remix UI.");

    RTX_OPTION_ENV("rtx", bool, enableRaytracing, true, "DXVK_ENABLE_RAYTRACING",
                   "Globally enables or disables ray tracing. When set to false the original game should render mostly as it would in DXVK typically.\n"
                   "Some artifacts may still appear however compared to the original game either due to issues with the underlying DXVK translation or issues in Remix itself.");

    // Note: This time is in milliseconds, should be named something like millisecondDeltaBetweenFrames ideally, but keeping it as it is for now.
    RTX_OPTION_ENV("rtx", float, timeDeltaBetweenFrames, 0.f, "RTX_FRAME_TIME_DELTA_MS",
                   "Frame time delta in milliseconds to use for rendering.\n"
                   "Setting this to 0 will use actual frame time delta for a given frame. Non-zero value allows the actual time delta to be overridden and is primarily used for automation to ensure determinism run to run without variance due to frame time fluctuations.");

    RTX_OPTION_FLAG("rtx", bool, keepTexturesForTagging, false, RtxOptionFlags::NoSave, "A flag to keep all textures in video memory, which can drastically increase VRAM consumption. Intended to assist with tagging textures that are only used for a short period of time (such as loading screens). Use only when necessary!");
    RTX_OPTION("rtx.gui", float, textureGridThumbnailScale, 1.f, 
               "A float to set the scale of thumbnails while selecting textures.\n"
               "This will be scaled by the default value of 120 pixels.\n"
               "This value must always be greater than zero.");
    RTX_OPTION("rtx", bool, skipDrawCallsPostRTXInjection, false, "Ignores all draw calls recorded after RTX Injection, the location of which varies but is currently based on when tagged UI textures begin to draw.");
    RTX_OPTION_ENV("rtx", DlssPreset, dlssPreset, DlssPreset::On, "RTX_DLSS_PRESET", "Combined DLSS Preset for quickly controlling Upscaling, Frame Interpolation and Latency Reduction.");
    RTX_OPTION("rtx", NisPreset, nisPreset, NisPreset::Balanced, "Adjusts NIS scaling factor, trades quality for performance.");
    RTX_OPTION("rtx", TaauPreset, taauPreset, TaauPreset::Balanced,  "Adjusts TAA-U scaling factor, trades quality for performance.");
    RTX_OPTION_ENV("rtx", GraphicsPreset, graphicsPreset, GraphicsPreset::Auto, "DXVK_GRAPHICS_PRESET_TYPE", "Overall rendering preset, higher presets result in higher image quality, lower presets result in better performance.");
    RTX_OPTION_ENV("rtx", RaytraceModePreset, raytraceModePreset, RaytraceModePreset::Auto, "DXVK_RAYTRACE_MODE_PRESET_TYPE", "");
    RTX_OPTION_ENV("rtx", std::string, sourceRootPath, "", "RTX_SOURCE_ROOT", "A path pointing at the root folder of the project, used to override the path to the root of the project generated at build-time (as this path is only valid for the machine the project was originally compiled on). Used primarily for locating shader source files for runtime shader recompilation.");
    RTX_OPTION("rtx", bool,  recompileShadersOnLaunch, false, "When set to true runtime shader recompilation will execute on the first frame after launch.");
    RTX_OPTION("rtx", bool, useLiveShaderEditMode, false, "When set to true shaders will be automatically recompiled when any shader file is updated (saved for instance) in addition to the usual manual recompilation trigger.");
    RTX_OPTION("rtx", float, emissiveIntensity, 1.0f, "A general scale factor on all emissive intensity values globally. Generally per-material emissive intensities should be used, but this option may be useful for debugging without needing to author materials.");
    RTX_OPTION("rtx", float, fireflyFilteringLuminanceThreshold, 1000.0f, "Maximum luminance threshold for the firefly filtering to clamp to.");
    RTX_OPTION("rtx", float, secondarySpecularFireflyFilteringThreshold, 1000.0f, "Firefly luminance clamping threshold for secondary specular signal.");
    RTX_OPTION("rtx", float, vertexColorStrength, 0.6f,
               "A scalar to apply to how strong vertex color influence should be on materials.\n"
               "A value of 1 indicates that it should be fully considered (though do note the texture operation and relevant parameters still control how much it should be blended with the actual albedo color), a value of 0 indicates that it should be fully ignored.");
    RTX_OPTION("rtx", bool, allowFSE, false,
               "A flag indicating if the application should be able to utilize exclusive full screen mode when set to true, otherwise force it to be disabled when set to false.\n"
               "Exclusive full screen may see performance benefits over other fullscreen modes at the cost of stability in some cases.\n"
               "Do note that on modern Windows full screen optimizations will likely be used regardless which in most cases results in performance similar to exclusive full screen even when it is not in use.");
    RTX_OPTION("rtx", std::string, baseGameModRegex, "", "Regex used to determine if the base game is running a mod, like a sourcemod.");
    RTX_OPTION("rtx", std::string, baseGameModPathRegex, "", "Regex used to redirect RTX Remix Runtime to another path for replacements and rtx.conf.");

  public:
    struct ViewModel {
      friend class ImGUI;
      RTX_OPTION("rtx.viewModel", bool, enable, false, "If true, try to resolve view models (e.g. first-person weapons). World geometry doesn't have shadows / reflections / etc from the view models.");
      RTX_OPTION("rtx.viewModel", float, rangeMeters, 1.0f, "[meters] Max distance at which to find a portal for view model virtual instances. If rtx.viewModel.separateRays is true, this is also max length of view model rays.");
      RTX_OPTION("rtx.viewModel", float, scale, 1.0f, "Scale for view models. Minimize to prevent clipping.");
      RTX_OPTION("rtx.viewModel", bool, enableVirtualInstances, true, "If true, virtual instances are created to render the view models behind a portal.");
      RTX_OPTION("rtx.viewModel", bool, perspectiveCorrection, true, "If true, apply correction to view models (e.g. different FOV is used for view models).");
      RTX_OPTION("rtx.viewModel", float, maxZThreshold, 0.0f, "If a draw call's viewport has max depth less than or equal to this threshold, then assume that it's a view model.");
    } viewModel;

  public:
    struct PlayerModel {
      friend class ImGUI;
      RTX_OPTION("rtx.playerModel", bool, enableVirtualInstances, true, "");
      RTX_OPTION("rtx.playerModel", bool, enableInPrimarySpace, false, "");
      RTX_OPTION("rtx.playerModel", bool, enablePrimaryShadows, true, "");
      RTX_OPTION("rtx.playerModel", float, backwardOffset, 0.f, "");
      RTX_OPTION("rtx.playerModel", float, horizontalDetectionDistance, 34.f, "");
      RTX_OPTION("rtx.playerModel", float, verticalDetectionDistance, 64.f, "");
      RTX_OPTION("rtx.playerModel", float, eyeHeight, 64.f, "");
      RTX_OPTION("rtx.playerModel", float, intersectionCapsuleRadius, 24.f, "");
      RTX_OPTION("rtx.playerModel", float, intersectionCapsuleHeight, 68.f, "");
    } playerModel;

    struct Displacement {
      friend class ImGUI;
      RTX_OPTION("rtx.displacement", DisplacementMode, mode, DisplacementMode::QuadtreePOM, "What algorithm the displacement uses.\n"
        "RaymarchPOM: advances the ray in linear steps until the ray is below the heightfield.\n"
        "QuadtreePOM: Relies on special mipmaps with maximum values instead of average values.  Uses the mipmap as a quadtree.");
      RTX_OPTION("rtx.displacement", bool, enableDirectLighting, true, "Whether direct lighting accounts for displacement mapping");
      RTX_OPTION("rtx.displacement", bool, enableIndirectLighting, true, "Whether indirect lighting accounts for displacement mapping");
      RTX_OPTION("rtx.displacement", bool, enableNEECache, true, "Whether the NEE cache accounts for displacement mapping");
      RTX_OPTION("rtx.displacement", bool, enableReSTIRGI, true, "Whether ReSTIR GI accounts for displacement mapping");
      RTX_OPTION("rtx.displacement", bool, enableIndirectHit, false, "Whether indirect ray hits account for displacement mapping (Enabling this is expensive.  Without it, non-perfect reflections of displaced objects will not show displacement.)");
      RTX_OPTION("rtx.displacement", bool, enablePSR, false, "Enable PSR (perfect reflections) for materials with displacement.  Rays that have been perfectly reflected off a POM surface will not collide correctly with other parts of that same surface.");
      RTX_OPTION("rtx.displacement", float, displacementFactor, 1.0f, "Scaling factor for all displacement maps");
      RTX_OPTION("rtx.displacement", uint, maxIterations, 64, "The max number of times the POM raymarch will iterate.");
    } displacement;

    RTX_OPTION("rtx", bool, resolvePreCombinedMatrices, true, "");

    RTX_OPTION("rtx", uint32_t, minPrimsInStaticBLAS, 1000, "");
    RTX_OPTION("rtx", uint32_t, maxPrimsInMergedBLAS, 50000, "");

    RTX_OPTION_ENV("rtx", bool, enableAlwaysCalculateAABB, false, "RTX_ALWAYS_CALCULATE_AABB", "Calculate an Axis Aligned Bounding Box for every draw call.\n This may improve instance tracking across frames for skinned and vertex shaded calls.");

    // Camera
    struct FreeCam{
      RTX_OPTION("rtx.freeCam", VirtualKeys, keyMoveFaster,  {VirtualKey{VK_LSHIFT}}, "Move faster in free camera mode.\nExample override: 'rtx.rtx.freeCam.keyMoveForward = RSHIFT'");
      RTX_OPTION("rtx.freeCam", VirtualKeys, keyMoveForward, {VirtualKey{'W'}}, "Move forward in free camera mode.\nExample override: 'rtx.rtx.freeCam.keyMoveForward = P'");
      RTX_OPTION("rtx.freeCam", VirtualKeys, keyMoveLeft,    {VirtualKey{'A'}}, "Move left in free camera mode.\nExample override: 'rtx.rtx.freeCam.keyMoveLeft = P'");
      RTX_OPTION("rtx.freeCam", VirtualKeys, keyMoveBack,    {VirtualKey{'S'}}, "Move back in free camera mode.\nExample override: 'rtx.rtx.freeCam.keyMoveBack = P'");
      RTX_OPTION("rtx.freeCam", VirtualKeys, keyMoveRight,   {VirtualKey{'D'}}, "Move right in free camera mode.\nExample override: 'rtx.rtx.freeCam.keyMoveRight = P'");
      RTX_OPTION("rtx.freeCam", VirtualKeys, keyMoveUp,      {VirtualKey{'E'}}, "Move up in free camera mode.\nExample override: 'rtx.rtx.freeCam.keyMoveUp = P'");
      RTX_OPTION("rtx.freeCam", VirtualKeys, keyMoveDown,    {VirtualKey{'Q'}}, "Move down in free camera mode.\nExample override: 'rtx.rtx.freeCam.keyMoveDown = P'");
      RTX_OPTION("rtx.freeCam", VirtualKeys, keyPitchDown,   {VirtualKey{'I'}}, "Pitch down in free camera mode.\nExample override: 'rtx.rtx.freeCam.keyPitchDown = P'");
      RTX_OPTION("rtx.freeCam", VirtualKeys, keyPitchUp,     {VirtualKey{'K'}}, "Pitch up in free camera mode.\nExample override: 'rtx.rtx.freeCam.keyPitchUp = P'");
      RTX_OPTION("rtx.freeCam", VirtualKeys, keyYawLeft,     {VirtualKey{'J'}}, "Yaw left in free camera mode.\nExample override: 'rtx.rtx.freeCam.keyYawLeft = P'");
      RTX_OPTION("rtx.freeCam", VirtualKeys, keyYawRight,    {VirtualKey{'L'}}, "Yaw right in free camera mode.\nExample override: 'rtx.rtx.freeCam.keyYawRight = P'");
    } freeCam;
    RW_RTX_OPTION_ENV("rtx", bool, shakeCamera, false, "RTX_FREE_CAMERA_ENABLE_ANIMATION", "Enables animation of the free camera.");
    RTX_OPTION_ENV("rtx", CameraAnimationMode, cameraAnimationMode, CameraAnimationMode::CameraShake_Pitch, "RTX_FREE_CAMERA_ANIMATION_MODE", "Free camera's animation mode.");
    RTX_OPTION_ENV("rtx", int, cameraShakePeriod, 20, "RTX_FREE_CAMERA_ANIMATION_PERIOD", "Period of the free camera's animation.");
    RTX_OPTION_ENV("rtx", float, cameraAnimationAmplitude, 2.0f, "RTX_FREE_CAMERA_ANIMATION_AMPLITUDE", "Amplitude of the free camera's animation.");
    RTX_OPTION("rtx", bool, skipObjectsWithUnknownCamera, false, "");
    RTX_OPTION("rtx", bool, enableNearPlaneOverride, false,
               "A flag to enable or disable the Camera's near plane override feature.\n"
               "Since the camera is not used directly for ray tracing the near plane the application uses typically does not matter, but for certain matrix-based operations (such as temporal reprojection or voxel grid projection) it is still relevant.\n"
               "The issue arises when geometry is ray traced that is behind where the chosen Camera's near plane is located, typically common on viewmodels especially with how they are ray traced, causing graphical artifacts and other issues.\n"
               "This option helps correct this issue by overriding the near plane value to else (usually smaller) to sit behind the objects in question (such as the view model). As such this option should usually be enabled on games with viewmodels.\n"
               "Do note that when adjusting the near plane the larger the relative magnitude gap between the near and far plane the worse the precision of matrix operations will be, so the near plane should be set as high as possible even when overriding.");
    RTX_OPTION("rtx", float, nearPlaneOverride, 0.1f,
               "The near plane value to use for the Camera when the near plane override is enabled.\n"
               "Only takes effect when rtx.enableNearPlaneOverride is enabled, see that option for more information about why this is useful.");

    RTX_OPTION("rtx", bool, useRayPortalVirtualInstanceMatching, true, "");
    RTX_OPTION("rtx", bool, enablePortalFadeInEffect, false, "");

    RTX_OPTION_ENV("rtx", bool, useRTXDI, true, "DXVK_USE_RTXDI",
                   "A flag indicating if RTXDI should be used, true enables RTXDI, false disables it and falls back on simpler light sampling methods.\n"
                   "RTXDI provides improved direct light sampling quality over traditional methods and should generally be enabled for improved direct lighting quality at the cost of some performance.");
    RTX_OPTION_ENV("rtx", bool, useReSTIRGI, true, "DXVK_USE_RESTIR_GI",
                   "A flag indicating if ReSTIR GI should be used, true enables ReSTIR GI, false disables it and relies on typical GI sampling.\n"
                   "ReSTIR GI provides improved indirect path sampling over typical importance sampling and should usually be enabled for better indirect diffuse and specular GI quality at the cost of some performance.");
    RTX_OPTION_ENV("rtx", UpscalerType, upscalerType, UpscalerType::DLSS, "DXVK_UPSCALER_TYPE", "Upscaling boosts performance with varying degrees of image quality tradeoff depending on the type of upscaler and the quality mode/preset.");
    RTX_OPTION_ENV("rtx", bool, enableRayReconstruction, true, "DXVK_RAY_RECONSTRUCTION", "Enable ray reconstruction.");

    RTX_OPTION("rtx", float, resolutionScale, 0.75f, "");
    RTX_OPTION("rtx", bool, forceCameraJitter, false, "");
    RTX_OPTION("rtx", bool, enableDirectLighting, true, "Enables direct lighting (lighting directly from lights on to a surface) on surfaces when set to true, otherwise disables it.");
    RTX_OPTION("rtx", bool, enableSecondaryBounces, true, "Enables indirect lighting (lighting from diffuse/specular bounces to one or more other surfaces) on surfaces when set to true, otherwise disables it.");
    RTX_OPTION("rtx", bool, zUp, false, "Indicates that the Z axis is the \"upward\" axis in the world when true, otherwise the Y axis when false.");
    RTX_OPTION("rtx", bool, leftHandedCoordinateSystem, false, "Indicates that the world space coordinate system is left-handed when true, otherwise right-handed when false.");
    RTX_OPTION("rtx", float, uniqueObjectDistance, 300.f, "The distance (in game units) that an object can move in a single frame before it is no longer considered the same object.\n"
                    "If this is too low, fast moving objects may flicker and have bad lighting.  If it's too high, repeated objects may flicker.\n"
                    "This does not account for sceneScale.");
    RTX_OPTION_FLAG_ENV("rtx", UIType, showUI, UIType::None, RtxOptionFlags::NoSave | RtxOptionFlags::NoReset, "RTX_GUI_DISPLAY_UI", "0 = Don't Show, 1 = Show Simple, 2 = Show Advanced.");
    RTX_OPTION_FLAG("rtx", bool, defaultToAdvancedUI, false, RtxOptionFlags::NoReset, "");
    RTX_OPTION_FLAG("rtx", bool, showRayReconstructionUI, true, RtxOptionFlags::NoReset, "Show ray reconstruction UI.");
    RTX_OPTION("rtx", bool, showUICursor, true, "");
    RTX_OPTION_FLAG("rtx", bool, blockInputToGameInUI, true, RtxOptionFlags::NoSave, "");

  private:
    VirtualKeys m_remixMenuKeyBinds;
  public:
    const VirtualKeys& remixMenuKeyBinds() const { return m_remixMenuKeyBinds; }

    RTX_OPTION_ENV("rtx", DLSSProfile, qualityDLSS, DLSSProfile::Auto, "RTX_QUALITY_DLSS", "Adjusts internal DLSS scaling factor, trades quality for performance.");
    // Note: All ray tracing modes depend on the rtx.raytraceModePreset option as they may be overridden by automatic defaults for a specific vendor if the preset is set to Auto. Set
    // to Custom to ensure these settings are not overridden.
    //RenderPassVolumeIntegrateRaytraceMode renderPassVolumeIntegrateRaytraceMode = RenderPassVolumeIntegrateRaytraceMode::RayQuery;
    RTX_OPTION_ENV("rtx", RenderPassGBufferRaytraceMode, renderPassGBufferRaytraceMode, RenderPassGBufferRaytraceMode::TraceRay, "DXVK_RENDER_PASS_GBUFFER_RAYTRACE_MODE",
                   "The ray tracing mode to use for the G-Buffer pass which resolves the initial primary and secondary surfaces to apply lighting to.");
    RTX_OPTION_ENV("rtx", RenderPassIntegrateDirectRaytraceMode, renderPassIntegrateDirectRaytraceMode, RenderPassIntegrateDirectRaytraceMode::RayQuery, "DXVK_RENDER_PASS_INTEGRATE_DIRECT_RAYTRACE_MODE",
                   "The ray tracing mode to use for the Direct Lighting pass which applies lighting to the primary/secondary surfaces.");
    RTX_OPTION_ENV("rtx", RenderPassIntegrateIndirectRaytraceMode, renderPassIntegrateIndirectRaytraceMode, RenderPassIntegrateIndirectRaytraceMode::TraceRay, "DXVK_RENDER_PASS_INTEGRATE_INDIRECT_RAYTRACE_MODE",
                   "The ray tracing mode to use for the Indirect Lighting pass which applies lighting to the primary/secondary surfaces.");
    RTX_OPTION("rtx", bool, captureDebugImage, false, "");

    // Denoiser Options
    RTX_OPTION_ENV("rtx", bool, useDenoiser, true, "DXVK_USE_DENOISER",
                   "Enables usage of denoiser(s) when set to true, otherwise disables denoising when set to false.\n"
                   "Denoising is important for filtering the raw noisy ray traced signal into a smoother and more stable result at the cost of some potential spatial/temporal artifacts (ghosting, boiling, blurring, etc).\n"
                   "Generally should remain enabled except when debugging behavior which requires investigating the output directly, or diagnosing denoising-related issues.");
    RTX_OPTION_ENV("rtx", bool, useDenoiserReferenceMode, false, "DXVK_USE_DENOISER_REFERENCE_MODE",
                   "Enables reference \"denoiser\" (~ accumulation mode) when set to true, otherwise uses a standard denoiser.\n"
                   "The reference denoiser accumulates frames over time to generate a reference multi-sample per pixel contribution\n"
                   "which should converge slowly to the ideal result the renderer is working towards.\n"
                   "It is useful for analyzing quality differences in various denoising methods, post-processing filters,\n"
                   "or for more accurately comparing subtle effects of potentially biased rendering techniques\n"
                   "which may be hard to see through noise and filtering.\n"
                   "It is also useful for higher quality artistic renders of a scene beyond what is possible in real-time.");
    RTX_OPTION_ENV("rtx", bool, denoiseDirectAndIndirectLightingSeparately, true, "DXVK_DENOISE_DIRECT_AND_INDIRECT_LIGHTING_SEPARATELY", "Denoising quality, high uses separate denoising of direct and indirect lighting for higher quality at the cost of performance.");
    RTX_OPTION("rtx", bool, replaceDirectSpecularHitTWithIndirectSpecularHitT, true, "");
    RTX_OPTION("rtx", bool, adaptiveResolutionDenoising, true, "");
    RTX_OPTION_ENV("rtx", bool, adaptiveAccumulation, true, "DXVK_USE_ADAPTIVE_ACCUMULATION", "");

    RTX_OPTION("rtx", uint32_t, numFramesToKeepInstances, 1, "");
    RTX_OPTION("rtx", uint32_t, numFramesToKeepBLAS, 4, "");
    RTX_OPTION("rtx", uint32_t, numFramesToKeepLights, 100, ""); // NOTE: This was the default we've had for a while, can probably be reduced...
    RTX_OPTION("rtx", uint32_t, numFramesToKeepGeometryData, 5, "");
    RTX_OPTION("rtx", uint32_t, numFramesToKeepMaterialTextures, 5, "");
    RTX_OPTION("rtx", bool, enablePreviousTLAS, true, "");
    RTX_OPTION("rtx", float, sceneScale, 1, "Defines the ratio of rendering unit (1cm) to game unit, i.e. sceneScale = 1cm / GameUnit.");

    struct AntiCulling {
      struct Object {
        friend class ImGUI;
        friend class RtxOptions;
        // Anti-Culling Options
        RTX_OPTION_ENV("rtx.antiCulling.object", bool, enable, false, "RTX_ANTI_CULLING_OBJECTS", "Extends lifetime of objects that go outside the camera frustum (anti-culling frustum).");
        RTX_OPTION("rtx.antiCulling.object", bool, enableHighPrecisionAntiCulling, true, "Use robust intersection check with Separate Axis Theorem.\n"
                   "This method is slightly expensive but it effectively addresses object flickering issues that arise from corner cases in the fast intersection check method.\n"
                   "Typically, it's advisable to enable this option unless it results in a notable performance drop; otherwise, the presence of flickering artifacts could significantly diminish the overall image quality.");
        RTX_OPTION("rtx.antiCulling.object", bool, enableInfinityFarFrustum, false, "Enable infinity far plane frustum for anti-culling.");
        RTX_OPTION("rtx.antiCulling.object", bool, hashInstanceWithBoundingBoxHash, true, "Hash instances with bounding box hash for object duplication check.\n Disable this when the game using primitive culling which may cause flickering.");
        // TODO: This should be a threshold of memory size
        RTX_OPTION("rtx.antiCulling.object", uint32_t, numObjectsToKeep, 10000, "The maximum number of RayTracing instances to keep when Anti-Culling is enabled.");
        RTX_OPTION("rtx.antiCulling.object", float, fovScale, 1.0f, "Scale applied to the FOV of Anti-Culling Frustum for matching the culling frustum in the original game.");
        RTX_OPTION("rtx.antiCulling.object", float, farPlaneScale, 10.0f, "Scale applied to the far plane for Anti-Culling Frustum for matching the culling frustum in the original game.");
      };
      struct Light {
        friend class ImGUI;
        friend class RtxOptions;
        RTX_OPTION_ENV("rtx.antiCulling.light", bool, enable, false, "RTX_ANTI_CULLING_LIGHTS", "Enable Anti-Culling for lights.");
        RTX_OPTION("rtx.antiCulling.light", uint32_t, numLightsToKeep, 1000, "Maximum number of lights to keep when Anti-Culling is enabled.");
        RTX_OPTION("rtx.antiCulling.light", uint32_t, numFramesToExtendLightLifetime, 1000, "Maximum number of frames to keep  when Anti-Culling is enabled. Make sure not to set this too low (then the anti-culling won't work), nor too high (which will hurt the performance).");
        RTX_OPTION("rtx.antiCulling.light", float, fovScale, 1.0f, "Scalar of the FOV of lights Anti-Culling Frustum.");
      };
    };
    // Resolve Options
    // Todo: Potentially document that after a number of resolver interactions is exhausted the next interaction will be treated as a hit regardless.
    RTX_OPTION("rtx", uint8_t, primaryRayMaxInteractions, 32,
               "The maximum number of resolver interactions to use for primary (initial G-Buffer) rays.\n"
               "This affects how many Decals, Ray Portals and potentially particles (if unordered approximations are not enabled) may be interacted with along a ray at the cost of performance for higher amounts of interactions.");
    RTX_OPTION("rtx", uint8_t, psrRayMaxInteractions, 32,
               "The maximum number of resolver interactions to use for PSR (primary surface replacement G-Buffer) rays.\n"
               "This affects how many Decals, Ray Portals and potentially particles (if unordered approximations are not enabled) may be interacted with along a ray at the cost of performance for higher amounts of interactions.");
    RTX_OPTION("rtx", uint8_t, secondaryRayMaxInteractions, 8,
               "The maximum number of resolver interactions to use for secondary (indirect) rays.\n"
               "This affects how many Decals, Ray Portals and potentially particles (if unordered approximations are not enabled) may be interacted with along a ray at the cost of performance for higher amounts of interactions.\n"
               "This value is recommended to be set lower than the primary/PSR max ray interactions as secondary ray interactions are less visually relevant relative to the performance cost of resolving them.");
    RTX_OPTION("rtx", bool, enableSeparateUnorderedApproximations, true,
               "Use a separate loop during resolving for surfaces which can have lighting evaluated in an approximate unordered way on each path segment (such as particles).\n"
               "This improves performance typically in how particles or decals are rendered and should usually always be enabled.\n"
               "Do note however the unordered nature of this resolving method may result in visual artifacts with large numbers of stacked particles due to difficulty in determining the intended order.\n"
               "Additionally, unordered approximations will only be done on the first indirect ray bounce (as particles matter less in higher bounces), and only if enabled by its corresponding setting.");
    RTX_OPTION("rtx", bool, trackParticleObjects, true, "Track last frame's corresponding particle object.");
    RTX_OPTION("rtx", bool, enableDirectTranslucentShadows, false, "Include OBJECT_MASK_TRANSLUCENT into primary visibility rays.");
    RTX_OPTION("rtx", bool, enableIndirectTranslucentShadows, false, "Include OBJECT_MASK_TRANSLUCENT into secondary visibility rays.");

    RTX_OPTION("rtx", float, resolveTransparencyThreshold, 1.0f / 255.0f, "A threshold for which any opacity value below is considered totally transparent and may be safely skipped without as significant of a performance cost.");
    RTX_OPTION("rtx", float, resolveOpaquenessThreshold, 254.0f / 255.0f, "A threshold for which any opacity value above is considered totally opaque.");

    // PSR Options
    RTX_OPTION("rtx", bool, enablePSRR, true,
               "A flag to enable or disable reflection PSR (Primary Surface Replacement).\n"
               "When enabled this feature allows higher quality mirror-like reflections in special cases by replacing the G-Buffer's surface with the reflected surface.\n"
               "Should usually be enabled for the sake of quality as almost all applications will utilize it in the form of glass or mirrors.");
    RTX_OPTION("rtx", bool, enablePSTR, true,
               "A flag to enable or disable transmission PSR (Primary Surface Replacement).\n"
               "When enabled this feature allows higher quality glass-like refraction in special cases by replacing the G-Buffer's surface with the refracted surface.\n"
               "Should usually be enabled for the sake of quality as almost all applications will utilize it in the form of glass.");
    RTX_OPTION("rtx", uint8_t, psrrMaxBounces, 10,
               "The maximum number of Reflection PSR bounces to traverse. Must be 15 or less due to payload encoding.\n"
               "Should be set higher when many mirror-like reflection bounces may be needed, though more bounces may come at a higher performance cost.");
    RTX_OPTION("rtx", uint8_t, pstrMaxBounces, 10,
               "The maximum number of Transmission PSR bounces to traverse. Must be 15 or less due to payload encoding.\n"
               "Should be set higher when refraction through many layers of glass may be needed, though more bounces may come at a higher performance cost.");
    RTX_OPTION("rtx", bool, enablePSTROutgoingSplitApproximation, true,
               "Enable transmission PSR on outgoing transmission events such as leaving translucent materials (rather than respecting no-split path PSR rule).\n"
               "Typically this results in better looking glass when enabled (at the cost of accuracy due to ignoring non-TIR inter-reflections within the glass itself).");
    RTX_OPTION("rtx", bool, enablePSTRSecondaryIncidentSplitApproximation, true,
               "Enable transmission PSR on secondary incident transmission events such as entering a translucent material on an already-transmitted path (rather than respecting no-split path PSR rule).\n"
               "Typically this results in better looking glass when enabled (at the cost accuracy due to ignoring reflections off of glass seen through glass for example).");
    
    // Note: In a more technical sense, any PSR reflection or transmission from a surface with "normal detail" greater than the specified value will generate a 1.0 in the
    // disocclusionThresholdMix mask, indicating that the alternate disocclusion threshold in the denoiser should be used.
    // A value of 0 is a valid setting as it means that any detail at all, no matter how small, will set that mask bit (e.g. any usage of a normal map deviating from from the
    // underlying normal).
    RTX_OPTION("rtx", float, psrrNormalDetailThreshold, 0.0f,
               "A threshold value to indicate that the denoiser's alternate disocclusion threshold should be used when normal map \"detail\" on a reflection PSR surface exceeds a desired amount.\n"
               "Normal detail is defined as 1-dot(tangent_normal, vec3(0, 0, 1)), or in other words it is 0 when no normal mapping is used, and 1 when the normal mapped normal is perpendicular to the underlying normal.\n"
               "This is typically used to reduce flickering artifacts resulting from reflection on surfaces like glass leveraging normal maps as often the denoiser is too aggressive with disocclusion checks frame to frame when DLSS or other camera jittering is in use.");
    RTX_OPTION("rtx", float, pstrNormalDetailThreshold, 0.0f,
               "A threshold value to indicate that the denoiser's alternate disocclusion threshold should be used when normal map \"detail\" on a transmission PSR surface exceeds a desired amount.\n"
               "Normal detail is defined as 1-dot(tangent_normal, vec3(0, 0, 1)), or in other words it is 0 when no normal mapping is used, and 1 when the normal mapped normal is perpendicular to the underlying normal.\n"
               "This is typically used to reduce flickering artifacts resulting from refraction on surfaces like glass leveraging normal maps as often the denoiser is too aggressive with disocclusion checks frame to frame when DLSS or other camera jittering is in use.");

    // Shader Execution Reordering Options
    RTX_OPTION_ENV("rtx", bool, isShaderExecutionReorderingSupported, true, "DXVK_IS_SHADER_EXECUTION_REORDERING_SUPPORTED", "Enables support of Shader Execution Reordering (SER) if it is supported by the target HW and SW."); 
    RTX_OPTION("rtx", bool, enableShaderExecutionReorderingInPathtracerGbuffer, false, "(Note: Hard disabled in shader code) Enables Shader Execution Reordering (SER) in GBuffer Raytrace pass if SER is supported.");
    RTX_OPTION("rtx", bool, enableShaderExecutionReorderingInPathtracerIntegrateIndirect, true, "Enables Shader Execution Reordering (SER) in Integrate Indirect pass if SER is supported.");

    // Path Options
    RTX_OPTION("rtx", bool, enableRussianRoulette, true,
               "A flag to enable or disable Russian Roulette, a rendering technique to give paths a chance of terminating randomly with each bounce based on their importance.\n"
               "This is usually useful to have enabled as it will ensure useless paths are terminated earlier while more important paths are allowed to accumulate more bounces.\n"
               "Furthermore this allows for the renderer to remain unbiased whereas a hard clamp on the number of bounces will introduce bias (though this is also done in Remix for the sake of performance).\n"
               "On the other hand, randomly terminating paths too aggressively may leave threads in GPU warps without work which may hurt thread occupancy when not used with a thread-reordering technique like SER.");
    RTX_OPTION_ENV("rtx", RussianRouletteMode, russianRouletteMode, RussianRouletteMode::ThroughputBased, "DXVK_PATH_TRACING_RR_MODE","Russian Roulette Mode. Throughput Based: paths with higher throughput become longer; Specular Based: specular paths become longer.\n");
    RTX_OPTION("rtx", float, russianRouletteDiffuseContinueProbability, 0.1f, "The probability of continuing a diffuse path when Russian Roulette is being used. Only apply to specular based mode.\n");
    RTX_OPTION("rtx", float, russianRouletteSpecularContinueProbability, 0.98f, "The probability of continuing a specular path when Russian Roulette is being used. Only apply to specular based mode.\n");
    RTX_OPTION("rtx", float, russianRouletteDistanceFactor, 0.1f, "Path segments whose distance proportion are under this threshold are more likely to continue. Only apply to specular based mode.\n");
    RTX_OPTION("rtx", float, russianRouletteMaxContinueProbability, 0.9f,
               "The maximum probability of continuing a path when Russian Roulette is being used.\n"
               "This ensures all rays have a small probability of terminating each bounce, mostly to prevent infinite paths in perfectly reflective mirror rooms (though the maximum path bounce count will also ensure this).");
    RTX_OPTION("rtx", float, russianRoulette1stBounceMinContinueProbability, 0.6f,
               "The minimum probability of continuing a path when Russian Roulette is being used on the first bounce.\n"
               "This ensures that on the first bounce rays are not terminated too aggressively as it may be useful for some denoisers to have a contribution even if it is a relatively unimportant one rather than a missing indirect sample.");
    RTX_OPTION("rtx", float, russianRoulette1stBounceMaxContinueProbability, 1.0f,
               "The maximum probability of continuing a path when Russian Roulette is being used on the first bounce.\n"
               "This is similar to the usual max continuation probability for Russian Roulette, but specifically only for the first bounce.");
    RTX_OPTION_ENV("rtx", uint8_t, pathMinBounces, 1, "DXVK_PATH_TRACING_MIN_BOUNCES",
                   "The minimum number of indirect bounces the path must complete before Russian Roulette can be used. Must be < 16.\n"
                   "This value is recommended to stay fairly low (1 for example) as forcing longer paths when they carry little contribution quickly becomes detrimental to performance.");
    RTX_OPTION_ENV("rtx", uint8_t, pathMaxBounces, 4, "DXVK_PATH_TRACING_MAX_BOUNCES",
                   "The maximum number of indirect bounces the path will be allowed to complete. Must be < 16.\n"
                   "Higher values result in better indirect lighting quality due to biasing the signal less, lower values result in better performance.\n"
                   "Very high values are not recommended however as while long paths may be technically needed for unbiased rendering, in practice the contributions from higher bounces have diminishing returns.");
    // Note: Use caution when adjusting any zero thresholds as values too high may cause entire lobes of contribution to be missing in material edge cases. For example
    // with translucency, a zero threshold on the specular lobe of 0.05 removes the entire contribution when viewing straight on for any glass with an IoR below 1.58 or so
    // which can be paticularly noticable in some scenes. To bias sampling more in the favor of one lobe the min probability should be used instead, but be aware this will
    // end up wasting more samples in some cases versus pure importance sampling (but may help denoising if it cannot deal with super sparse signals).
    RTX_OPTION("rtx", float, opaqueDiffuseLobeSamplingProbabilityZeroThreshold, 0.01f, "The threshold for which to zero opaque diffuse probability weight values.");
    RTX_OPTION("rtx", float, minOpaqueDiffuseLobeSamplingProbability, 0.25f, "The minimum allowed non-zero value for opaque diffuse probability weights.");
    RTX_OPTION("rtx", float, opaqueSpecularLobeSamplingProbabilityZeroThreshold, 0.01f, "The threshold for which to zero opaque specular probability weight values.");
    RTX_OPTION("rtx", float, minOpaqueSpecularLobeSamplingProbability, 0.25f, "The minimum allowed non-zero value for opaque specular probability weights.");
    RTX_OPTION("rtx", float, opaqueOpacityTransmissionLobeSamplingProbabilityZeroThreshold, 0.01f, "The threshold for which to zero opaque opacity probability weight values.");
    RTX_OPTION("rtx", float, minOpaqueOpacityTransmissionLobeSamplingProbability, 0.25f, "The minimum allowed non-zero value for opaque opacity probability weights.");
    RTX_OPTION("rtx", float, opaqueDiffuseTransmissionLobeSamplingProbabilityZeroThreshold, 0.01f, "The threshold for which to zero thin opaque diffuse transmission probability weight values.");
    RTX_OPTION("rtx", float, minOpaqueDiffuseTransmissionLobeSamplingProbability, 0.25f, "The minimum allowed non-zero value for thin opaque diffuse transmission probability weights.");
    // Note: 0.01 chosen as mentioned before to avoid cutting off reflection lobe on most common types of glass when looking straight on (a base reflectivity
    // of 0.01 corresponds to an IoR of 1.22 or so). Avoid changing this default without good reason to prevent glass from losing its reflection contribution.
    RTX_OPTION("rtx", float, translucentSpecularLobeSamplingProbabilityZeroThreshold, 0.01f, "The threshold for which to zero translucent specular probability weight values.");
    RTX_OPTION("rtx", float, minTranslucentSpecularLobeSamplingProbability, 0.3f, "The minimum allowed non-zero value for translucent specular probability weights.");
    RTX_OPTION("rtx", float, translucentTransmissionLobeSamplingProbabilityZeroThreshold, 0.01f, "The threshold for which to zero translucent transmission probability weight values.");
    RTX_OPTION("rtx", float, minTranslucentTransmissionLobeSamplingProbability, 0.25f, "The minimum allowed non-zero value for translucent transmission probability weights.");

    RTX_OPTION("rtx", float, indirectRaySpreadAngleFactor, 0.05f,
               "A tuning factor applied to the spread angle calculated from the sampled lobe solid angle PDF. Should be 0-1.\n"
               "This scaled spread angle is used to widen a ray's cone angle after indirect lighting BRDF samples to essentially prefilter the effects of the BRDF lobe's spread which potentially may reduce noise from indirect rays (e.g. reflections).\n"
               "Prefiltering will overblur detail however compared to the ground truth of casting multiple samples especially given this calculated spread angle is a basic approximation and ray cones to begin with are a simple approximation for ray pixel footprint.\n"
               "As such rather than using the spread angle fully this spread angle factor allows it to be scaled down to something more narrow so that overblurring can be minimized. Similarly, setting this factor to 0 disables this cone angle widening feature.");
    RTX_OPTION("rtx", bool, rngSeedWithFrameIndex, true,
               "Indicates that pseudo-random number generator should be seeded with the frame number of the application every frame, otherwise seed with 0.\n"
               "This should generally always be enabled as without the frame index each frame will typically be identical in the random values that are produced which will result in incorrect rendering. Only meant as a debugging tool.");
    RTX_OPTION("rtx", bool, enableFirstBounceLobeProbabilityDithering, true,
               "A flag to enable or disable screen-space probability dithering on the first indirect lobe sampled.\n"
               "Generally sampling a diffuse, specular or other lobe relies on a random number generated against the probability of sampling each lobe, effectively focusing more rays/paths on lobes which matter more.\n"
               "This can cause issues however with denoisers which do not handle sparse stochastic signals (like those from path tracing) well as they may be expecting a more \"complete\" signal like those used in simpler branching ray tracing setups.\n"
               "To help solve this issue this option uses a temporal screenspace dithering based on the probability rather than a purely random choice to determine which lobe to sample from on the first indirect bounce.\n"
               "This as a result helps ensure there will always be a diffuse or specular sample within the dithering pattern's area and should help the denoising resolve a more stable result.");
    RTX_OPTION("rtx", bool, enableUnorderedResolveInIndirectRays, true,
               "A flag to enable or disable unordered resolve approximations in indirect rays.\n"
               "This allows for the presence of unordered approximations in resolving to be overridden in indirect rays and as such requires separate unordered approximations to be enabled to have any effect.\n"
               "This option should be enabled if objects which can be resolvered in an unordered way in indirect rays are expected for higher quality in reflections, but may come at a performance cost.\n"
               "Note that even with this option enabled, unordered resolve approximations are only done on the first indirect bounce for the sake of performance overall.");
    RTX_OPTION("rtx", bool, enableProbabilisticUnorderedResolveInIndirectRays, true,
               "A flag to enable or disable probabilistic unordered resolve approximations in indirect rays.\n"
               "This flag speeds up the unordered resolve for indirect rays by probabilistically deciding when to perform unordered resolve or not.  Must have both unordered resolve and unordered resolve in indirect rays enabled for this to take effect.\n"
               "This option should be enabled by default as it can significantly improve performance on some hardware.  In rare cases it may come at the cost of some quality for particles and decals in reflections.\n"
               "Note that even with this option enabled, unordered resolve approximations are only done on the first indirect bounce for the sake of performance overall.");
    RTX_OPTION_ENV("rtx", bool, enableUnorderedEmissiveParticlesInIndirectRays, false, "DXVK_EMISSIVE_INDIRECT_PARTICLES",
                   "A flag to enable or disable unordered resolve emissive particles specifically in indirect rays.\n"
                   "Should be enabled in higher quality rendering modes as emissive particles are fairly important in reflections, but may be disabled to skip such interactions which can improve performance on lower end hardware.\n"
                   "Note that rtx.enableUnorderedResolveInIndirectRays must first be enabled for this option to take any effect (as it will control if unordered resolve is used to begin with in indirect rays).");
    RTX_OPTION("rtx", bool, enableTransmissionApproximationInIndirectRays, false,
               "A flag to enable transmission approximations in indirect rays.\n"
               "Translucent objects hit by indirect rays will not alter ray direction, just change the ray throughput.");
    RTX_OPTION("rtx", bool, enableDecalMaterialBlending, true,
               "A flag to enable or disable material blending on decals.\n"
               "This should generally always be enabled when decals are in use as this allows decals to be blended down on to the surface they sit slightly above which results in more convincing decals rendering.");

    RTX_OPTION("rtx", bool, enableBillboardOrientationCorrection, true, "");
    RTX_OPTION("rtx", bool, useIntersectionBillboardsOnPrimaryRays, false, "");
    RTX_OPTION("rtx", float, translucentDecalAlbedoFactor, 10.0f,
               "A global scale factor applied to the albedo of decals that are applied to a translucent base material, to make the decals more visible.\n"
               "This is generally needed as albedo values for decals may be fairly low when dealing with opaque surfaces, but the translucent diffuse layer requires a fairly high albedo value to result in an expected look.\n"
               "The need for this option could be avoided by simply authoring decals applied to translucent materials with a higher albedo to begin with, but sometimes applications may share decals between different material types.");

    RTX_OPTION("rtx", float, worldSpaceUiBackgroundOffset, -0.01f, "Distance along normal to offset objects rendered as worldspace UI, specifically for the background of screens.");

    // Light Selection/Sampling Options
    RTX_OPTION("rtx", uint16_t, risLightSampleCount, 7,
               "The number of lights randomly selected from the global pool to consider when selecting a light with RIS.\n"
               "Higher values generally increases the quality of RIS light sampling, but also has diminishing returns and higher performance cost past a point.\n"
               "Note that RIS is only used when RTXDI is disabled for direct lighting, or for light sampling in indirect rays, so the impact of this effect will vary.");

    // Froxel Radiance Cache/Volumetric Lighting ptions
    // Note: The effective froxel grid resolution (based on the resolution scale) and froxelDepthSlices when multiplied together give the number of froxel cells, and this should be greater than the maximum number of
    // "concurrent" threads the GPU can execute at once to saturate execution and ensure maximal occupancy. This can be calculated by looking at how many warps per multiprocessor the GPU can have at once (This can
    // be found in CUDA Tuning guides such as https://docs.nvidia.com/cuda/ampere-tuning-guide/index.html) and then multiplying it by the number of multiprocessors (SMs) on the GPU in question, and finally turning
    // this into a thread count by mulitplying by how many threads per warp there are (typically 32).
    // Example for a RTX 3090: 82 SMs * 64 warps per SM * 32 threads per warp = 167,936 froxels to saturate the GPU. It is fine to be a bit below this though as most gpus will have fewer SMs than this, and higher resolutions
    // will also use more froxels due to how the grid is allocated with respect to the (downscaled when DLSS is in use) resolution, and we don't want the froxel passes to be too expensive (unless higher quality results are desired).
    RTX_OPTION("rtx", uint32_t, froxelGridResolutionScale, 16, "The scale factor to divide the x and y render resolution by to determine the x and y dimensions of the froxel grid.");
    RTX_OPTION("rtx", uint16_t, froxelDepthSlices, 48, "The z dimension of the froxel grid. Must be constant after initialization.");
    RTX_OPTION("rtx", uint8_t, maxAccumulationFrames, 254,
               "The number of frames to accumulate volume lighting samples over, maximum of 254.\n"
               "Large values result in greater image stability at the cost of potentially more temporal lag."
               "Should generally be set to as large a value as is viable as the froxel radiance cache is assumed to be fairly noise-free and stable which temporal accumulation helps with.");
    RTX_OPTION("rtx", float, froxelDepthSliceDistributionExponent, 2.0f, "The exponent to use on depth values to nonlinearly distribute froxels away from the camera. Higher values bias more froxels closer to the camera with 1 being linear.");
    RTX_OPTION("rtx", float, froxelMaxDistance, 2000.0f, "The maximum distance in world units to allocate the froxel grid out to. Should be less than the distance between the camera's near and far plane, as the froxel grid will clip to the far plane otherwise.");
    RTX_OPTION("rtx", float, froxelFireflyFilteringLuminanceThreshold, 1000.0f, "Sets the maximum luminance threshold for the volumetric firefly filtering to clamp to.");
    RTX_OPTION("rtx", float, froxelFilterGaussianSigma, 1.2f, "The sigma value of the gaussian function used to filter volumetric radiance values. Larger values cause a smoother filter to be used.");
    RTX_OPTION("rtx", uint32_t, volumetricInitialRISSampleCount, 8,
               "The number of RIS samples to select from the global pool of lights when constructing a Reservoir sample.\n"
               "Higher values generally increases the quality of the selected light sample, though similar to the general RIS light sample count has diminishing returns.");
    RTX_OPTION("rtx", bool, volumetricEnableInitialVisibility, true,
               "Determines whether to trace a visibility ray for Reservoir samples.\n"
               "Results in slightly higher quality froxel grid light samples at the cost of a ray per froxel cell each frame and should generally be enabled.");
    RTX_OPTION("rtx", bool, volumetricEnableTemporalResampling, true,
               "Indicates if temporal resampling should be used for volume integration.\n"
               "Temporal resampling allows for reuse of temporal information when picking froxel grid light samples similar to how ReSTIR works, providing higher quality light samples.\n"
               "This should generally be enabled but currently due to the lack of temporal bias correction this option will slightly bias the lighting result.");
    RTX_OPTION("rtx", uint16_t, volumetricTemporalReuseMaxSampleCount, 200, "The number of samples to clamp temporal reservoirs to, should usually be around the value: desired_max_history_frames * average_reservoir_samples.");
    RTX_OPTION("rtx", float, volumetricClampedReprojectionConfidencePenalty, 0.5f, "The penalty from [0, 1] to apply to the sample count of temporally reprojected reservoirs when reprojection is clamped to the fustrum (indicating lower quality reprojection).");
    RTX_OPTION("rtx", uint8_t, froxelMinReservoirSamples, 1, "The minimum number of Reservoir samples to do for each froxel cell when stability is at its maximum, should be at least 1.");
    RTX_OPTION("rtx", uint8_t, froxelMaxReservoirSamples, 6, "The maximum number of Reservoir samples to do for each froxel cell when stability is at its minimum, should be at least 1 and greater than or equal to the minimum.");
    RTX_OPTION("rtx", uint8_t, froxelMinKernelRadius, 2, "The minimum filtering kernel radius to use when stability is at its maximum, should be at least 1.");
    RTX_OPTION("rtx", uint8_t, froxelMaxKernelRadius, 4, "The maximum filtering kernel radius to use when stability is at its minimum, should be at least 1 and greater than or equal to the minimum.");
    RTX_OPTION("rtx", uint8_t, froxelMinReservoirSamplesStabilityHistory, 1, "The minimum history to consider history at minimum stability for Reservoir samples.");
    RTX_OPTION("rtx", uint8_t, froxelMaxReservoirSamplesStabilityHistory, 64, "The maximum history to consider history at maximum stability for Reservoir samples.");
    RTX_OPTION("rtx", uint8_t, froxelMinKernelRadiusStabilityHistory, 1, "The minimum history to consider history at minimum stability for filtering.");
    RTX_OPTION("rtx", uint8_t, froxelMaxKernelRadiusStabilityHistory, 64, "The maximum history to consider history at maximum stability for filtering.");
    RTX_OPTION("rtx", float, froxelReservoirSamplesStabilityHistoryPower, 2.0f, "The power to apply to the Reservoir sample stability history weight.");
    RTX_OPTION("rtx", float, froxelKernelRadiusStabilityHistoryPower, 2.0f, "The power to apply to the kernel radius stability history weight.");
    RTX_OPTION("rtx", bool, enableVolumetricLighting, false,
               "Enabling volumetric lighting provides higher quality ray traced physical volumetrics, disabling falls back to cheaper depth based fog.\n"
               "Note that disabling this option does not disable the froxel radiance cache as a whole as it is still needed for other non-volumetric lighting approximations.");
    RTX_OPTION("rtx", Vector3, volumetricTransmittanceColor, Vector3(0.953237f, 0.928790f, 0.903545f),
               "The color to use for calculating transmittance measured at a specific distance.\n"
               "Note that this color is assumed to be in sRGB space and gamma encoded as it will be converted to linear for use in volumetrics.");
    RTX_OPTION("rtx", float, volumetricTransmittanceMeasurementDistance, 10000.0f, "The distance the specified transmittance color was measured at. Lower distances indicate a denser medium.");
    RTX_OPTION("rtx", Vector3, volumetricSingleScatteringAlbedo, Vector3(0.9f, 0.9f, 0.9f),
               "The single scattering albedo (otherwise known as the particle albedo) representing the ratio of scattering to absorption.\n"
               "While color-like in many ways this value is assumed to be more of a mathematical albedo (unlike material albedo which is treated more as a color), and is therefore treated as linearly encoded data (not gamma).");
    RTX_OPTION("rtx", float, volumetricAnisotropy, 0.0f, "The anisotropy of the scattering phase function (-1 being backscattering, 0 being isotropic, 1 being forward scattering).");
    RTX_OPTION("rtx", bool, enableVolumetricsInPortals, true,
               "Enables using extra frustum-aligned volumes for lighting in portals.\n"
               "Note that enabling this option will require 3x the memory of the typical froxel grid as well as degrade performance in some cases.\n"
               "This option should be enabled always in games using ray portals for proper looking volumetrics through them, but should be disabled on any game not using ray portals.\n"
               "Additionally, this setting must be set at startup and changing it will not take effect at runtime.");

    // Subsurface Scattering
    struct SubsurfaceScattering {
      friend class RtxOptions;
      friend class ImGUI;

      RTX_OPTION("rtx.subsurface", bool, enableThinOpaque, true, "Enable thin opaque material. The materials withthin opaque properties will fallback to normal opaque material.");
      RTX_OPTION("rtx.subsurface", bool, enableTextureMaps, true, "Enable texture maps such as thickness map or scattering albedo map. The corresponding subsurface properties will fallback to per-material constants if this is disabled.");
      RTX_OPTION("rtx.subsurface", float, surfaceThicknessScale, 1.0f, "Scalar of the subsurface thickness.");
    };

    // Note: Options for remapping legacy D3D9 fixed function fog parameters to volumetric lighting parameters and overwriting the global volumetric parameters when fixed function fog is enabled.
    // Useful for cases where dynamic fog parameters are used throughout a game (or very per-level) that cannot be captrued merely in a global set of volumetric parameters. To see remapped results
    // volumetric lighting in general must be enabled otherwise these settings will have no effect.
    RTX_OPTION("rtx", bool, enableFogRemap, false,
               "A flag to enable or disable fixed function fog remapping. Only takes effect when volumetrics are enabled.\n"
               "Typically many old games used fixed function fog for various effects and while sometimes this fog can be replaced with proper volumetrics globally, other times require some amount of dynamic behavior controlled by the game.\n"
               "When enabled this option allows for remapping of fixed function fog parameters from the game to volumetric parameters to accomodate this dynamic need.");
    RTX_OPTION("rtx", bool, enableFogColorRemap, false,
               "A flag to enable or disable remapping fixed function fox's color. Only takes effect when fog remapping in general is enabled.\n"
               "Enables or disables remapping functionality relating to the color parameter of fixed function fog with the exception of the multiscattering scale (as this scale can be set to 0 to disable it).\n"
               "This allows dynamic changes to the game's fog color to be reflected somewhat in the volumetrics system. Overrides the specified volumetric transmittance color.");
    RTX_OPTION("rtx", bool, enableFogMaxDistanceRemap, true,
               "A flag to enable or disable remapping fixed function fox's max distance. Only takes effect when fog remapping in general is enabled.\n"
               "Enables or disables remapping functionality relating to the max distance parameter of fixed function fog.\n"
               "This allows dynamic changes to the game's fog max distance to be reflected somewhat in the volumetrics system. Overrides the specified volumetric transmittance measurement distance.");
    RTX_OPTION("rtx", float, fogRemapMaxDistanceMin, 100.0f,
               "A value controlling the \"max distance\" fixed function fog parameter's minimum remapping bound.\n"
               "Note that fog remapping and fog max distance remapping must be enabled for this setting to have any effect.");
    RTX_OPTION("rtx", float, fogRemapMaxDistanceMax, 4000.0f,
               "A value controlling the \"max distance\" fixed function fog parameter's maximum remapping bound.\n"
               "Note that fog remapping and fog max distance remapping must be enabled for this setting to have any effect.");
    RTX_OPTION("rtx", float, fogRemapTransmittanceMeasurementDistanceMin, 2000.0f,
               "A value representing the transmittance measurement distance's minimum remapping bound.\n"
               "When the fixed function fog's \"max distance\" parameter is at or below its specified minimum the volumetric system's transmittance measurement distance will be set to this value and interpolated upwards.\n"
               "Note that fog remapping and fog max distance remapping must be enabled for this setting to have any effect.");
    RTX_OPTION("rtx", float, fogRemapTransmittanceMeasurementDistanceMax, 12000.0f,
               "A value representing the transmittance measurement distance's maximum remapping bound.\n"
               "When the fixed function fog's \"max distance\" parameter is at or above its specified maximum the volumetric system's transmittance measurement distance will be set to this value and interpolated upwards.\n"
               "Note that fog remapping and fog max distance remapping must be enabled for this setting to have any effect.");
    RTX_OPTION("rtx", float, fogRemapColorMultiscatteringScale, 1.0f,
               "A value representing the scale of the fixed function fog's color in the multiscattering approximation.\n"
               "This scaling factor is applied to the fixed function fog's color and becomes a multiscattering approximation in the volumetrics system.\n"
               "Sometimes useful but this multiscattering approximation is very basic (just a simple ambient term for now essentially) and may not look very good depending on various conditions.");
    RTX_OPTION("rtx", bool, fogIgnoreSky, false, "If true, sky draw calls will be skipped when searching for the D3D9 fog values.")

    // Note: Cached values used to precompute quantities for options fetching to not have to needlessly recompute them.
    uint8_t cachedFroxelReservoirSamplesStabilityHistoryRange;
    uint8_t cachedFroxelKernelRadiusStabilityHistoryRange;

    // Alpha Test/Blend Options
    RTX_OPTION("rtx", bool, enableAlphaBlend, true, "Enable rendering alpha blended geometry, used for partial opacity and other blending effects on various surfaces in many games.");
    RTX_OPTION("rtx", bool, enableAlphaTest, true, "Enable rendering alpha tested geometry, used for cutout style opacity in some games.");
    RTX_OPTION("rtx", bool, enableCulling, true, "Enable front/backface culling for opaque objects. Objects with alpha blend or alpha test are not culled.");
    RTX_OPTION("rtx", bool, enableCullingInSecondaryRays, false, "Enable front/backface culling for opaque objects. Objects with alpha blend or alpha test are not culled.  Only applies in secondary rays, defaults to off.  Generally helps with light bleeding from objects that aren't watertight.");
    RTX_OPTION("rtx", bool, enableEmissiveBlendModeTranslation, true, "Treat incoming semi/additive D3D blend modes as emissive.");
    RTX_OPTION("rtx", bool, enableEmissiveBlendEmissiveOverride, true, "Override typical material emissive information on draw calls with any emissive blending modes to emulate their original look more accurately.");
    RTX_OPTION("rtx", float, emissiveBlendOverrideEmissiveIntensity, 0.2f, "The emissive intensity to use when the emissive blend override is enabled. Adjust this if particles for example look overly bright globally.");
    RTX_OPTION("rtx", float, particleSoftnessFactor, 0.05f, "Multiplier for the view distance that is used to calculate the particle blending range.");
    RTX_OPTION("rtx", float, forceCutoutAlpha, 0.5f,
               "When an object is added to the cutout textures list it will have a cutout alpha mode forced on it, using this value for the alpha test.\n"
               "This is meant to improve the look of some legacy mode materials using low-resolution textures and alpha blending instead of alpha cutout as this can cause blurry halos around edges due to the difficulty of handling this sort of blending in Remix.\n"
               "Such objects are generally better handled with actual replacement assets using fully opaque geometry replacements or alpha cutout with higher resolution textures, so this should only be relied on until proper replacements can be authored.");

    // Ray Portal Options
    // Note: Not a set as the ordering of the hashes is important. Keep this list small to avoid expensive O(n) searching (should only have 2 or 4 elements usually).
    // Also must always be a multiple of 2 for proper functionality as each pair of hashes defines a portal connection.
    RTX_OPTION("rtx", std::vector<XXH64_hash_t>, rayPortalModelTextureHashes, {}, "Texture hashes identifying ray portals. Allowed number of hashes: {0, 2}.");
    // Todo: Add option for if a model to world transform matrix should be used or if PCA should be used instead to attempt to guess what the matrix should be (for games with
    // pretransformed Ray Portal vertices).
    // Note: Axes used for orienting the portal when PCA is used.
    RTX_OPTION("rtx", Vector3, rayPortalModelNormalAxis, Vector3(0.0f, 0.0f, 1.0f), "The axis in object space to map the ray portal geometry's normal axis to. Currently unused (as PCA is not implemented).");
    RTX_OPTION("rtx", Vector3, rayPortalModelWidthAxis, Vector3(1.0f, 0.0f, 0.0f), "The axis in object space to map the ray portal geometry's width axis to. Currently unused (as PCA is not implemented).");
    RTX_OPTION("rtx", Vector3, rayPortalModelHeightAxis, Vector3(0.0f, 1.0f, 0.0f), "The axis in object space to map the ray portal geometry's height axis to. Currently unused (as PCA is not implemented).");
    RTX_OPTION("rtx", float, rayPortalSamplingWeightMinDistance, 10.0f,
               "The minimum distance from a portal which the interpolation of the probability of light sampling through portals will begin (and is at its maximum value).\n"
               "Currently unimplemented, kept here for future use.");
    RTX_OPTION("rtx", float, rayPortalSamplingWeightMaxDistance, 1000.0f,
               "The maximum distance from a portal which the interpolation of the probability of light sampling through portals will end (and is at its minimum value such that no portal light sampling will happen beyond this point).\n"
               "Currently unimplemented, kept here for future use.");
    RTX_OPTION("rtx", bool, rayPortalCameraHistoryCorrection, false,
               "A flag to control if history correction on ray portal camera teleportation events is enabled or disabled.\n"
               "This allows for the previous camera matrix to be set to a virtual matrix to correct the large discontunity in position and view direction which happens when a camera teleports from moving through a ray portal (in games like Portal).\n"
               "As such this option should always be enabled in games utilizing ray portals the camera can pass through as it should fix artifacts from incorrectly calculated motion vectors or other deltas that rely on the current and previous camera matrix.");
    RTX_OPTION("rtx", bool, rayPortalCameraInBetweenPortalsCorrection, false,
               "A flag to contol correction when the camera is \"in-between\" a pair of ray portals.\n"
               "This is mostly relevant in applications which allow the camera to move through a ray portal (games like Portal) as often the ray portals are placed slightly off of a surface, allowing the camera to sometimes end up in this tiny gap for a frame.\n"
               "To correct this artifact (as it can mess up denoising and other temporal surface consistency checks due to the sudden frame of geometry in front of the camera) this option pushes the camera slightly backwards if this occurs when entering a ray portal.\n"
               "Similar to ray portal camera history correction this option should always be enabled in games utilizing ray portals the camera can pass through.");
    RTX_OPTION("rtx", float, rayPortalCameraInBetweenPortalsCorrectionThreshold, 0.1f,
               "The threshold to use for camera \"in-between\" ray portal detection in meters.\n"
               "When the camera is less than this distance behind the surface of a ray portal it will be pushed backwards to stay behind the ray portal.\n"
               "This value should stay small but be large enough to cover the gap between ray portals and the geometry behind them (if such a gap exists in the underlying application).\n"
               "Additionally, this setting must be set at startup and changing it will not take effect at runtime.");

    RTX_OPTION_ENV("rtx", bool, useWhiteMaterialMode, false, "RTX_USE_WHITE_MATERIAL_MODE", "Override all objects' materials by white material");
    RTX_OPTION("rtx", bool, useHighlightLegacyMode, false, "");
    RTX_OPTION("rtx", bool, useHighlightUnsafeAnchorMode, false, "");
    RTX_OPTION("rtx", bool, useHighlightUnsafeReplacementMode, false, "");
    RTX_OPTION("rtx", float, nativeMipBias, 0.0f,
               "Specifies a mipmapping level bias to add to all material texture filtering. Stacks with the upscaling mip bias.\n"
               "Mipmaps are determined based on how far away a texture is, using this can bias the desired level in a lower quality direction (positive bias), or a higher quality direction with potentially more aliasing (negative bias).\n"
               "Note that mipmaps are also important for good spatial caching of textures, so too far negative of a mip bias may start to significantly affect performance, therefore changing this value is not recommended");
    RTX_OPTION("rtx", float, upscalingMipBias, 0.0f,
               "Specifies a mipmapping level bias to add to all material texture filtering when upscaling (such as DLSS) is used.\n"
               "Mipmaps are determined based on how far away a texture is, using this can bias the desired level in a lower quality direction (positive bias), or a higher quality direction with potentially more aliasing (negative bias).\n"
               "Note that mipmaps are also important for good spatial caching of textures, so too far negative of a mip bias may start to significantly affect performance, therefore changing this value is not recommended");
    RTX_OPTION("rtx", bool, useAnisotropicFiltering, true,
               "A flag to indicate if anisotropic filtering should be used on material textures, otherwise typical trilinear filtering will be used.\n"
               "This should generally be enabled as anisotropic filtering allows for less blurring on textures at grazing angles than typical trilinear filtering with only usually minor performance impact (depending on the max anisotropy samples).");
    RTX_OPTION("rtx", float, maxAnisotropySamples, 8.0f,
               "The maximum number of samples to use when anisotropic filtering is enabled.\n"
               "The actual max anisotropy used will be the minimum between this value and the hardware's maximum. Higher values increase quality but will likely reduce performance.");
    RTX_OPTION_ENV("rtx", bool, enableMultiStageTextureFactorBlending, true, "RTX_ENABLE_MULTI_STAGE_TEXTURE_FACTOR_BLENDING", "Support texture factor blending in stage 1~7. Currently only support 1 additional blending stage, more than 1 additional blending stages will be ignored.");

    // Developer Options
    RTX_OPTION_FLAG("rtx", bool, enableInstanceDebuggingTools, false, RtxOptionFlags::NoSave, "NOTE: This will disable temporal correllation for instances, but allow the use of instance developer debug tools");
    RTX_OPTION("rtx", Vector2i, drawCallRange, Vector2i(0, INT32_MAX), "");
    RTX_OPTION("rtx", Vector3, instanceOverrideWorldOffset, Vector3(0.f, 0.f, 0.f), "");
    RTX_OPTION("rtx", uint, instanceOverrideInstanceIdx, UINT32_MAX, "");
    RTX_OPTION("rtx", uint, instanceOverrideInstanceIdxRange, 15, "");
    RTX_OPTION("rtx", bool, instanceOverrideSelectedInstancePrintMaterialHash, false, "");
    RTX_OPTION("rtx", bool, enablePresentThrottle, false,
               "A flag to enable or disable present throttling, when set to true a sleep for a time specified by the throttle delay will be inserted into the DXVK presentation thread.\n"
               "Useful to manually reduce the framerate if the application is running too fast or to reduce GPU power usage during development to keep temperatures down.\n"
               "Should not be enabled in anything other than development situations.");
    RTX_OPTION("rtx", int32_t, presentThrottleDelay, 16,
               "A time in milliseconds that the DXVK presentation thread should sleep for. Requires present throttling to be enabled to take effect.\n"
               "Note that the application may sleep for longer than the specified time as is expected with sleep functions in general.");
    RTX_OPTION_ENV("rtx", bool, validateCPUIndexData, false, "DXVK_VALIDATE_CPU_INDEX_DATA", "");

    struct OpacityMicromap
    {
      friend class RtxOptions;
      friend class ImGUI;
      bool isSupported = false;
      RTX_OPTION_ENV("rtx.opacityMicromap", bool, enable, true, "DXVK_ENABLE_OPACITY_MICROMAP", 
                     "Enables Opacity Micromaps for geometries with textures that have alpha cutouts.\n"
                     "This is generally the case for geometries such as fences, foliage, particles, etc. .\n"
                     "Opacity Micromaps greatly speed up raytracing of partially opaque triangles.\n"
                     "Examples of scenes that benefit a lot: multiple trees with a lot of foliage,\n"
                     "a ground densely covered with grass blades or steam consisting of many particles.");
    } opacityMicromap;

    RTX_OPTION("rtx", ReflexMode, reflexMode, ReflexMode::LowLatency,
               "Reflex mode selection, enabling it helps minimize input latency, boost mode may further reduce latency by boosting GPU clocks in CPU-bound cases.\n"
               "Supported enum values are 0 = None (Disabled), 1 = LowLatency (Enabled), 2 = LowLatencyBoost (Enabled + Boost).\n"
               "Note that even when using the \"None\" Reflex mode Reflex will attempt to be initialized. Use rtx.isReflexEnabled to fully disable to skip this initialization if needed.");
    RTX_OPTION_FLAG("rtx", bool, isReflexEnabled, true, RtxOptionFlags::NoSave,
                    "Enables or disables Reflex globally.\n"
                    "Note that this option when set to false will prevent Reflex from even attempting to initialize, unlike setting the Reflex mode to \"None\" which simply tells an initialized Reflex not to take effect.\n"
                    "Additionally, this setting must be set at startup and changing it will not take effect at runtime.");

    RW_RTX_OPTION_FLAG("rtx", EnableVsync, enableVsync, EnableVsync::WaitingForImplicitSwapchain, RtxOptionFlags::NoSave, "Controls the game's V-Sync setting. Native game's V-Sync settings are ignored.");

    // Replacement options
    RTX_OPTION("rtx", bool, enableReplacementAssets, true, "Globally enables or disables all enhanced asset replacement (materials, meshes, lights) functionality.");
    RTX_OPTION("rtx", bool, enableReplacementLights, true,
               "Enables or disables enhanced light replacements.\n"
               "Requires replacement assets in general to be enabled to have any effect.");
    RTX_OPTION("rtx", bool, enableReplacementMeshes, true,
               "Enables or disables enhanced mesh replacements.\n"
               "Requires replacement assets in general to be enabled to have any effect.");
    RTX_OPTION("rtx", bool, enableReplacementMaterials, true,
               "Enables or disables enhanced material replacements.\n"
               "Requires replacement assets in general to be enabled to have any effect.");
    RTX_OPTION("rtx", bool, forceHighResolutionReplacementTextures, false,
               "A flag to enable or disable forcing high resolution replacement textures.\n"
               "When enabled this mode overrides all other methods of mip calculation (adaptive resolution and the minimum mipmap level) and forces it to be 0 to always load in the highest quality of textures.\n"
               "This generally should not be used other than for various forms of debugging or visual comparisons as this mode will ignore any constraints on CPU or GPU memory which may starve the system or Remix of memory.\n"
               "Additionally, this setting must be set at startup and changing it will not take effect at runtime.");
    RTX_OPTION("rtx", bool, enableAdaptiveResolutionReplacementTextures, true,
               "A flag to enable or disable adaptive resolution replacement textures.\n"
               "When enabled, this mode allows replacement textures to load in only up to an adaptive minimum mip level to cut down on memory usage, but only when force high resolution replacement textures is disabled.\n"
               "This should generally always be enabled to ensure Remix does not starve the system of CPU or GPU memory while loading textures.\n"
               "Additionally, this setting must be set at startup and changing it will not take effect at runtime.");
    RTX_OPTION("rtx", uint, minReplacementTextureMipMapLevel, 0,
               "A parameter controlling the minimum replacement texture mipmap level to use, higher values will lower texture quality, 0 for default behavior of effectively not enforcing a minimum.\n"
               "This minimum will always be considered as long as force high resolution replacement textures is not enabled, meaning that with or without adaptive resolution replacement textures enabled this setting will always enforce a minimum mipmap restriction.\n"
               "Generally this should be changed to reduce the texture quality globally if desired to reduce CPU and GPU memory usage and typically should be controlled by some sort of texture quality setting.\n"
               "Additionally, this setting must be set at startup and changing it will not take effect at runtime.");
    RTX_OPTION("rtx", uint, adaptiveResolutionReservedGPUMemoryGiB, 2,
               "The amount of GPU memory in gibibytes to reserve away from consideration for adaptive resolution replacement textures.\n"
               "This value should only be changed to reflect the estimated amount of memory Remix itself consumes on the GPU (aside from texture loading, mostly from rendering-related buffers) and should not be changed otherwise.\n"
               "Only relevant when force high resolution replacement textures is disabled and adaptive resolution replacement textures is enabled. See asset estimated size parameter for more information.\n");
    RTX_OPTION("rtx", bool, reloadTextureWhenResolutionChanged, false, "Reload texture when resolution changed.");
    RTX_OPTION_ENV("rtx", bool, enableAsyncTextureUpload, true, "DXVK_ASYNC_TEXTURE_UPLOAD", "");
    RTX_OPTION_ENV("rtx", bool, alwaysWaitForAsyncTextures, false, "DXVK_WAIT_ASYNC_TEXTURES", "");
    RTX_OPTION("rtx", int,  asyncTextureUploadPreloadMips, 8, "");
    RTX_OPTION("rtx", bool, usePartialDdsLoader, true,
               "A flag controlling if the partial DDS loader should be used, true to enable, false to disable and use GLI instead.\n"
               "Generally this should be always enabled as it allows for simple parsing of DDS header information without loading the entire texture into memory like GLI does to retrieve similar information.\n"
               "Should only be set to false for debugging purposes if the partial DDS loader's logic is suspected to be incorrect to compare against GLI's implementation.");

    RTX_OPTION("rtx", TonemappingMode, tonemappingMode, TonemappingMode::Local,
               "The tonemapping type to use, 0 for Global, 1 for Local (Default).\n"
               "Global tonemapping tonemaps the image with respect to global parameters, usually based on statistics about the rendered image as a whole.\n"
               "Local tonemapping on the other hand uses more spatially-local parameters determined by regions of the rendered image rather than the whole image.\n"
               "Local tonemapping can result in better preservation of highlights and shadows in scenes with high amounts of dynamic range whereas global tonemapping may have to comprimise between over or underexposure.");

    // Capture Options
    //   General
    RTX_OPTION("rtx", bool, captureShowMenuOnHotkey, true,
               "If true, then the capture menu will appear whenever one of the capture hotkeys are pressed. A capture MUST be started by using a button in the menu, in that case.\n"
               "If false, the hotkeys behave as expected. The user must manually open the menu in order to change any values.");
    inline static const VirtualKeys kDefaultCaptureMenuKeyBinds{VirtualKey{VK_CONTROL},VirtualKey{VK_SHIFT},VirtualKey{'Q'}};
    RTX_OPTION("rtx", VirtualKeys, captureHotKey, kDefaultCaptureMenuKeyBinds,
               "Hotkey to trigger a capture without bringing up the menu.\n"
               "example override: 'rtx.captureHotKey = CTRL, SHIFT, P'\n"
               "Full list of key names available in src/util/util_keybind.h");
    RTX_OPTION("rtx", bool, captureInstances, true,
               "If true, an instanced snapshot of the game scene will be captured and exported to a USD stage, in addition to all meshes, textures, materials, etc.\n"
               "If false, only meshes, etc will be captured.");
    RTX_OPTION("rtx", bool, captureNoInstance, false, "Same as \'rtx.captureInstances\' except inverse. This is the original/old variant, and will be deprecated, however is still functional.");
    RTX_OPTION("rtx", std::string, captureTimestampReplacement, "{timestamp}",
               "String that can be used for auto-replacing current time stamp in instance stage name");
    RTX_OPTION("rtx", std::string, captureInstanceStageName,
                (std::string("capture_") + m_captureTimestampReplacement.getValue() + std::string(".usd")),
               "Name of the \'instance\' stage (see: \'rtx.captureInstances\')");
    RTX_OPTION("rtx", bool, captureEnableMultiframe, false, "Enables multi-frame capturing. THIS HAS NOT BEEN MAINTAINED AND SHOULD BE USED WITH EXTREME CAUTION.");
    RTX_OPTION("rtx", uint32_t, captureMaxFrames, 1, "Max frames capturable when running a multi-frame capture. The capture can be toggled to completion manually.");
    RTX_OPTION("rtx", uint32_t, captureFramesPerSecond, 24,
               "Playback rate marked in the USD stage.\n"
               "Will eventually determine frequency with which game state is captured and written. Currently every frame -- even those at higher frame rates -- are recorded.");
    //   Mesh
    RTX_OPTION("rtx", float, captureMeshPositionDelta, 0.3f, "Inter-frame position min delta warrants new time sample.");
    RTX_OPTION("rtx", float, captureMeshNormalDelta, 0.3f, "Inter-frame normal min delta warrants new time sample.");
    RTX_OPTION("rtx", float, captureMeshTexcoordDelta, 0.3f, "Inter-frame texcoord min delta warrants new time sample.");
    RTX_OPTION("rtx", float, captureMeshColorDelta, 0.3f, "Inter-frame color min delta warrants new time sample.");
    RTX_OPTION("rtx", float, captureMeshBlendWeightDelta, 0.01f, "Inter-frame blend weight min delta warrants new time sample.");

    RTX_OPTION("rtx", bool, useVirtualShadingNormalsForDenoising, true,
               "A flag to enable or disable the usage of virtual shading normals for denoising passes.\n"
               "This is primairly important for anything that modifies the direction of a primary ray, so mainly PSR and ray portals as both of these will view a surface from an angle different from the \"virtual\" viewing direction perceived by the camera.\n"
               "This can cause some issues with denoising due to the normals not matching the expected perception of what the normals should be, for example normals facing away from the camera direction due to being viewed from a different angle via refraction or portal teleportation.\n"
               "To correct this, virtual normals are calculcated such that they always are oriented relative to the primary camera ray as if its direction was never altered, matching the virtual perception of the surface from the camera's point of view.\n"
               "As an aside, virtual normals themselves can cause issues with denoising due to the normals suddenly changing from virtual to \"real\" normals upon traveling through a portal, causing surface consistency failures in the denoiser, but this is accounted for via a special transform given to the denoiser on camera ray portal teleportation events.\n"
               "As such, this option should generally always be enabled when rendering with ray portals in the scene to have good denoising quality.");
    RTX_OPTION("rtx", bool, resetDenoiserHistoryOnSettingsChange, false, "");

    RTX_OPTION("rtx", float, skyBrightness, 1.f, "");
    RTX_OPTION("rtx", bool, skyForceHDR, false, "By default sky will be rasterized in the color format used by the game. Set the checkbox to force sky to be rasterized in HDR intermediate format. This may be important when sky textures replaced with HDR textures.");
    RTX_OPTION("rtx", uint32_t, skyProbeSide, 1024, "Resolution of the skybox for indirect illumination (rough reflections, global illumination etc).");
    RTX_OPTION_FLAG("rtx", uint32_t, skyUiDrawcallCount, 0, RtxOptionFlags::NoSave, "");
    RTX_OPTION("rtx", uint32_t, skyDrawcallIdThreshold, 0, "It's common in games to render the skybox first, and so, this value provides a simple mechanism to identify those early draw calls that are untextured (textured draw calls can still use the Sky Textures functionality.");
    RTX_OPTION("rtx", float, skyMinZThreshold, 1.f, "If a draw call's viewport has min depth greater than or equal to this threshold, then assume that it's a sky.");
    RTX_OPTION("rtx", SkyAutoDetectMode, skyAutoDetect, SkyAutoDetectMode::None, 
               "Automatically tag sky draw calls using various heuristics.\n"
               "0 = None\n"
               "1 = CameraPosition - assume the first seen camera position is a sky camera.\n"
               "2 = CameraPositionAndDepthFlags - assume the first seen camera position is a sky camera, if its draw call's depth test is disabled. If it's enabled, assume no sky camera.\n"
               "Note: if all draw calls are marked as sky, then assume that there's no sky camera at all.");
    RTX_OPTION("rtx", float, skyAutoDetectUniqueCameraDistance, 1.0f,
               "If multiple cameras are found, this threshold distance (in game units) is used to distinguish a sky camera from a main camera. "
               "Active if sky auto-detect is set to CameraPosition / CameraPositionAndDepthFlags.")
    RTX_OPTION("rtx", bool, skyReprojectToMainCameraSpace, false,
               "Move sky geometry to the main camera space.\n"
               "Useful, if a game has a skybox that contains geometry that can be a part of the main scene (e.g. buildings, mountains). "
               "So with this option enabled, that geometry would be promoted from sky rasterization to ray tracing.");
    RTX_OPTION("rtx", float, skyReprojectScale, 16.0f, "Scaling of the sky geometry on reprojection to main camera space.");

    // TODO (REMIX-656): Remove this once we can transition content to new hash
    RTX_OPTION("rtx", bool, logLegacyHashReplacementMatches, false, "");

    RTX_OPTION("rtx", FusedWorldViewMode, fusedWorldViewMode, FusedWorldViewMode::None, "Set if game uses a fused World-View transform matrix.");

    RTX_OPTION("rtx", bool, useBuffersDirectly, true, "When enabled Remix will use the incoming vertex buffers directly where possible instead of copying data. Note: setting the d3d9.allowDiscard to False will disable this option.");
    RTX_OPTION("rtx", bool, alwaysCopyDecalGeometries, true, "When set to True tells the geometry processor to always copy decals geometry. This is an optimization flag to experiment with when rtx.useBuffersDirectly is True.");

    RTX_OPTION("rtx", bool, ignoreLastTextureStage, false, 
               "Removes the last texture bound to a draw call, when using fixed-function pipeline. Primary textures are untouched.\n"
               "Might be set to true, if a game applies a lightmap as last shading step, to omit the original lightmap data.");

    // Automation Options
    struct Automation {
      RTX_OPTION_FLAG_ENV("rtx.automation", bool, disableBlockingDialogBoxes, false, RtxOptionFlags::NoSave, "RTX_AUTOMATION_DISABLE_BLOCKING_DIALOG_BOXES",
                          "Disables various blocking blocking dialog boxes (such as popup windows) requiring user interaction when set to true, otherwise uses default behavior when set to false.\n"
                          "This option is typically meant for automation-driven execution of Remix where such dialog boxes if present may cause the application to hang due to blocking waiting for user input.");
      RTX_OPTION_FLAG_ENV("rtx.automation", bool, disableDisplayMemoryStatistics, false, RtxOptionFlags::NoSave, "RTX_AUTOMATION_DISABLE_DISPLAY_MEMORY_STATISTICS",
                          "Disables display of memory statistics in the Remix window.\n"
                          "This option is typically meant for automation of tests for which we don't want non-deterministic runtime memory statistics to be shown in GUI that is included as part of test image output.");
      RTX_OPTION_FLAG_ENV("rtx.automation", bool, disableUpdateUpscaleFromDlssPreset, false, RtxOptionFlags::NoSave, "RTX_AUTOMATION_DISABLE_UPDATE_UPSCALER_FROM_DLSS_PRESET",
                          "Disables updating upscaler from DLSS preset.\n"
                          "This option is typically meant for automation of tests for which we don't want upscaler to be updated based on a DLSS preset.");
      RTX_OPTION_FLAG_ENV("rtx.automation", bool, suppressAssetLoadingErrors, false, RtxOptionFlags::NoSave, "RTX_AUTOMATION_SUPPRESS_ASSET_LOADING_ERRORS",
                          "Suppresses asset loading errors by turning them into warnings.\n"
                          "This option is typically meant for automation of tests for which acceptable asset loading issues are known.");
    };

  public:
    LegacyMaterialDefaults legacyMaterial;
    OpaqueMaterialOptions opaqueMaterialOptions;
    TranslucentMaterialOptions translucentMaterialOptions;
    ViewDistanceOptions viewDistanceOptions;

    HashRule GeometryHashGenerationRule = 0;
    HashRule GeometryAssetHashRule = 0;

  private:
    RTX_OPTION("rtx", float, effectLightIntensity, 1.f, "");
    RTX_OPTION("rtx", float, effectLightRadius, 5.f, "");
    RTX_OPTION("rtx", bool, effectLightPlasmaBall, false, "");

    RTX_OPTION("rtx", bool, useObsoleteHashOnTextureUpload, false,
               "Whether or not to use slower XXH64 hash on texture upload.\n"
               "New projects should not enable this option as this solely exists for compatibility with older hashing schemes.");

    RTX_OPTION("rtx", bool, serializeChangedOptionOnly, true, "");

    RTX_OPTION("rtx", uint32_t, applicationId, 102100511, "Used to uniquely identify the application to DLSS. Generally should not be changed without good reason.");

    static std::unique_ptr<RtxOptions> pInstance;
    RtxOptions() { }

    // Note: Should be called whenever the min/max stability history values are changed.
    // Ideally would be done through a setter function but ImGui needs direct access to the original options with how we currently have it set up.
    void updateCachedVolumetricOptions() {
      assert(froxelMaxReservoirSamplesStabilityHistory() >= froxelMinReservoirSamplesStabilityHistory());
      assert(froxelMaxKernelRadiusStabilityHistory() >= froxelMinKernelRadiusStabilityHistory());

      cachedFroxelReservoirSamplesStabilityHistoryRange = froxelMaxReservoirSamplesStabilityHistory() - froxelMinReservoirSamplesStabilityHistory();
      cachedFroxelKernelRadiusStabilityHistoryRange = froxelMaxKernelRadiusStabilityHistory() - froxelMinKernelRadiusStabilityHistory();
    }

  public:

    RtxOptions(const Config& options) {
      if (sourceRootPath() == "./")
        sourceRootPathRef() = getCurrentDirectory() + "/";

      // Needs to be > 0
      RTX_OPTION_CLAMP_MIN(uniqueObjectDistance, FLT_MIN);

      RTX_OPTION_CLAMP_MIN(emissiveIntensity, 0.0f);
      // Note: Clamp to positive values as negative luminance thresholds are not valid.
      RTX_OPTION_CLAMP_MIN(fireflyFilteringLuminanceThreshold, 0.0f);
      RTX_OPTION_CLAMP(vertexColorStrength, 0.0f, 1.0f);
   
      // Render pass modes

      //renderPassVolumeIntegrateRaytraceMode = (RenderPassVolumeIntegrateRaytraceMode) std::min(
      //  options.getOption<uint32_t>("rtx.renderPassVolumeIntegrateRaytraceMode", (uint32_t) renderPassVolumeIntegrateRaytraceMode, "DXVK_RENDER_PASS_VOLUME_INTEGRATE_RAYTRACE_MODE"),
      //  (uint32_t) (RenderPassVolumeIntegrateRaytraceMode::Count) -1);

      renderPassGBufferRaytraceModeRef() = (RenderPassGBufferRaytraceMode) std::min(
        (uint32_t) renderPassGBufferRaytraceMode(),
        (uint32_t) (RenderPassGBufferRaytraceMode::Count) -1);

      renderPassIntegrateDirectRaytraceModeRef() = (RenderPassIntegrateDirectRaytraceMode) std::min(
        (uint32_t) renderPassIntegrateDirectRaytraceMode(),
        (uint32_t) (RenderPassIntegrateDirectRaytraceMode::Count) - 1);
      
      renderPassIntegrateIndirectRaytraceModeRef() = (RenderPassIntegrateIndirectRaytraceMode) std::min(
        (uint32_t) renderPassIntegrateIndirectRaytraceMode(),
        (uint32_t) (RenderPassIntegrateIndirectRaytraceMode::Count) - 1);

      // Pathtracing options
      //enableShaderExecutionReorderingInVolumeIntegrate =
      //  options.getOption<bool>("rtx.enableShaderExecutionReorderingInVolumeIntegrate", enableShaderExecutionReorderingInVolumeIntegrate);
      //enableShaderExecutionReorderingInPathtracerIntegrateDirect =
      //  options.getOption<bool>("rtx.enableShaderExecutionReorderingInPathtracerIntegrateDirect", enableShaderExecutionReorderingInPathtracerIntegrateDirect);

      // Resolve Options

      // Note: Clamped due to 8 bit usage on GPU.
      RTX_OPTION_CLAMP(primaryRayMaxInteractions, static_cast<uint8_t>(1), std::numeric_limits<uint8_t>::max());
      RTX_OPTION_CLAMP(psrRayMaxInteractions, static_cast<uint8_t>(1), std::numeric_limits<uint8_t>::max());
      RTX_OPTION_CLAMP(secondaryRayMaxInteractions, static_cast<uint8_t>(1), std::numeric_limits<uint8_t>::max());
      RTX_OPTION_CLAMP(resolveTransparencyThreshold, 0.f, 1.f);
      RTX_OPTION_CLAMP(resolveOpaquenessThreshold, resolveTransparencyThreshold(), 1.f);

      // PSR Options
      
      // Note: Clamped due to 8 bit usage on GPU.
      RTX_OPTION_CLAMP(psrrMaxBounces, static_cast<uint8_t>(1), static_cast<uint8_t>(254));
      RTX_OPTION_CLAMP(pstrMaxBounces, static_cast<uint8_t>(1), static_cast<uint8_t>(254));
      
      // Path Options
      RTX_OPTION_CLAMP(russianRouletteMaxContinueProbability, 0.0f, 1.0f);
      // Note: Clamped to 15 due to usage on GPU.
      RTX_OPTION_CLAMP(pathMinBounces, static_cast<uint8_t>(0), static_cast<uint8_t>(15));
      // Note: Clamp to the minimum bounce count additionally.
      RTX_OPTION_CLAMP(pathMaxBounces, pathMinBounces(), static_cast<uint8_t>(15));

      // Light Selection/Sampling Options

      // Note: Clamped due to 16 bit usage on GPU.
      RTX_OPTION_CLAMP(risLightSampleCount, static_cast<uint16_t>(1), std::numeric_limits<uint16_t>::max());

      // Volumetrics Options
      RTX_OPTION_CLAMP_MIN(froxelGridResolutionScale, static_cast<uint32_t>(1));
      RTX_OPTION_CLAMP(froxelDepthSlices, static_cast<uint16_t>(1), std::numeric_limits<uint16_t>::max());
      RTX_OPTION_CLAMP(maxAccumulationFrames, static_cast<uint8_t>(1), std::numeric_limits<uint8_t>::max());
      RTX_OPTION_CLAMP_MIN(froxelDepthSliceDistributionExponent, 1e-4f);
      RTX_OPTION_CLAMP_MIN(froxelMaxDistance, 0.0f);
      // Note: Clamp to positive values as negative luminance thresholds are not valid.
      RTX_OPTION_CLAMP_MIN(froxelFireflyFilteringLuminanceThreshold, 0.0f);
      RTX_OPTION_CLAMP_MIN(froxelFilterGaussianSigma, 0.0f);

      RTX_OPTION_CLAMP_MIN(volumetricInitialRISSampleCount, static_cast<uint32_t>(1));
      RTX_OPTION_CLAMP(volumetricTemporalReuseMaxSampleCount, static_cast<uint16_t>(1), std::numeric_limits<uint16_t>::max());
      RTX_OPTION_CLAMP(volumetricClampedReprojectionConfidencePenalty, 0.0f, 1.0f);

      RTX_OPTION_CLAMP(froxelMinReservoirSamples, static_cast<uint8_t>(1), std::numeric_limits<uint8_t>::max());
      RTX_OPTION_CLAMP(froxelMaxReservoirSamples, static_cast<uint8_t>(1), std::numeric_limits<uint8_t>::max());
      RTX_OPTION_CLAMP(froxelMinKernelRadius, static_cast<uint8_t>(1), std::numeric_limits<uint8_t>::max());
      RTX_OPTION_CLAMP(froxelMaxKernelRadius, static_cast<uint8_t>(1), std::numeric_limits<uint8_t>::max());
      RTX_OPTION_CLAMP(froxelMinReservoirSamplesStabilityHistory, static_cast<uint8_t>(1), std::numeric_limits<uint8_t>::max());
      RTX_OPTION_CLAMP(froxelMaxReservoirSamplesStabilityHistory, static_cast<uint8_t>(1), std::numeric_limits<uint8_t>::max());
      RTX_OPTION_CLAMP(froxelMinKernelRadiusStabilityHistory, static_cast<uint8_t>(1), std::numeric_limits<uint8_t>::max());
      RTX_OPTION_CLAMP(froxelMaxKernelRadiusStabilityHistory, static_cast<uint8_t>(1), std::numeric_limits<uint8_t>::max());
      RTX_OPTION_CLAMP_MIN(froxelReservoirSamplesStabilityHistoryPower, 0.0f);
      RTX_OPTION_CLAMP_MIN(froxelKernelRadiusStabilityHistoryPower, 0.0f);

      RTX_OPTION_CLAMP_MAX(froxelMinReservoirSamples, froxelMaxReservoirSamples());
      RTX_OPTION_CLAMP_MIN(froxelMaxReservoirSamples, froxelMinReservoirSamples());
      RTX_OPTION_CLAMP_MAX(froxelMinKernelRadius, froxelMaxKernelRadius());
      RTX_OPTION_CLAMP_MIN(froxelMaxKernelRadius, froxelMinKernelRadius());
      RTX_OPTION_CLAMP_MAX(froxelMinReservoirSamplesStabilityHistory, froxelMaxReservoirSamplesStabilityHistory());
      RTX_OPTION_CLAMP_MIN(froxelMaxReservoirSamplesStabilityHistory, froxelMinReservoirSamplesStabilityHistory());
      RTX_OPTION_CLAMP_MAX(froxelMinKernelRadiusStabilityHistory, froxelMaxKernelRadiusStabilityHistory());
      RTX_OPTION_CLAMP_MIN(froxelMaxKernelRadiusStabilityHistory, froxelMinKernelRadiusStabilityHistory());

      RTX_OPTION_CLAMP_MIN(volumetricTransmittanceMeasurementDistance, 0.0f);
      RTX_OPTION_CLAMP(volumetricAnisotropy, -1.0f, 1.0f);

      volumetricTransmittanceColorRef().x = std::clamp(volumetricTransmittanceColor().x, 0.0f, 1.0f);
      volumetricTransmittanceColorRef().y = std::clamp(volumetricTransmittanceColor().y, 0.0f, 1.0f);
      volumetricTransmittanceColorRef().z = std::clamp(volumetricTransmittanceColor().z, 0.0f, 1.0f);
      volumetricSingleScatteringAlbedoRef().x = std::clamp(volumetricSingleScatteringAlbedo().x, 0.0f, 1.0f);
      volumetricSingleScatteringAlbedoRef().y = std::clamp(volumetricSingleScatteringAlbedo().y, 0.0f, 1.0f);
      volumetricSingleScatteringAlbedoRef().z = std::clamp(volumetricSingleScatteringAlbedo().z, 0.0f, 1.0f);

      RTX_OPTION_CLAMP_MIN(fogRemapMaxDistanceMin, 0.0f);
      RTX_OPTION_CLAMP_MIN(fogRemapMaxDistanceMax, 0.0f);
      RTX_OPTION_CLAMP_MIN(fogRemapTransmittanceMeasurementDistanceMin, 0.0f);
      RTX_OPTION_CLAMP_MIN(fogRemapTransmittanceMeasurementDistanceMax, 0.0f);
      RTX_OPTION_CLAMP_MIN(fogRemapColorMultiscatteringScale, 0.0f);

      fogRemapMaxDistanceMinRef() = std::min(fogRemapMaxDistanceMin(), fogRemapMaxDistanceMax());
      fogRemapMaxDistanceMaxRef() = std::max(fogRemapMaxDistanceMin(), fogRemapMaxDistanceMax());
      fogRemapTransmittanceMeasurementDistanceMinRef() = std::min(fogRemapTransmittanceMeasurementDistanceMin(), fogRemapTransmittanceMeasurementDistanceMax());
      fogRemapTransmittanceMeasurementDistanceMaxRef() = std::max(fogRemapTransmittanceMeasurementDistanceMin(), fogRemapTransmittanceMeasurementDistanceMax());

      updateCachedVolumetricOptions();

      // Alpha Test/Blend Options

      // Note: Clamped to float16 max due to usage on GPU and positive values as emissive intensity values cannot be negative.
      RTX_OPTION_CLAMP(emissiveBlendOverrideEmissiveIntensity, 0.0f, FLOAT16_MAX);
      RTX_OPTION_CLAMP(particleSoftnessFactor, 0.0f, 1.0f);
      
      // Ray Portal Options
      // Note: Ensure the Ray Portal texture hashes are always in pairs of 2
      auto& rayPortalModelTextureHashes = rayPortalModelTextureHashesRef();
      if (rayPortalModelTextureHashes.size() % 2 == 1) {
        rayPortalModelTextureHashes.pop_back();
      }

      if (rayPortalModelTextureHashes.size() > maxRayPortalCount) {
        rayPortalModelTextureHashes.erase(rayPortalModelTextureHashes.begin() + maxRayPortalCount, rayPortalModelTextureHashes.end());
      }

      assert(rayPortalModelTextureHashes.size() % 2 == 0);
      assert(rayPortalModelTextureHashes.size() <= maxRayPortalCount);

      // Note: Ensure the portal sampling weight min and max distance are well defined
      RTX_OPTION_CLAMP_MIN(rayPortalSamplingWeightMinDistance, 0.0f);
      RTX_OPTION_CLAMP_MIN(rayPortalSamplingWeightMaxDistance, 0.0f);
      RTX_OPTION_CLAMP_MAX(rayPortalSamplingWeightMinDistance, rayPortalSamplingWeightMaxDistance());

      assert(rayPortalSamplingWeightMinDistance() >= 0.0f);
      assert(rayPortalSamplingWeightMaxDistance() >= 0.0f);
      assert(rayPortalSamplingWeightMinDistance() <= rayPortalSamplingWeightMaxDistance());
      
      // View Distance Options

      RTX_OPTION_CLAMP_MIN(viewDistanceOptions.distanceThreshold, 0.0f);
      RTX_OPTION_CLAMP_MIN(viewDistanceOptions.distanceFadeMin, 0.0f);
      RTX_OPTION_CLAMP_MIN(viewDistanceOptions.distanceFadeMax, 0.0f);
      RTX_OPTION_CLAMP_MAX(viewDistanceOptions.distanceFadeMin, viewDistanceOptions.distanceFadeMax());
      RTX_OPTION_CLAMP_MIN(viewDistanceOptions.distanceFadeMax, viewDistanceOptions.distanceFadeMin());

      // Replacement options

      if (env::getEnvVar("DXVK_DISABLE_ASSET_REPLACEMENT") == "1") {
        enableReplacementAssetsRef() = false;
        enableReplacementLightsRef() = false;
        enableReplacementMeshesRef() = false;
        enableReplacementMaterialsRef() = false;
      }

      const VirtualKeys& kDefaultRemixMenuKeyBinds { VirtualKey{VK_MENU},VirtualKey{'X'} };
      m_remixMenuKeyBinds = options.getOption<VirtualKeys>("rtx.remixMenuKeyBinds", kDefaultRemixMenuKeyBinds);

      GeometryHashGenerationRule = createRule("Geometry generation", geometryGenerationHashRuleString());
      GeometryAssetHashRule = createRule("Geometry asset", geometryAssetHashRuleString());

      // We deprecated dynamicDecalTextures, singleOffsetDecalTextures, nonOffsetDecalTextures with this change
      //  and replaced all decal texture lists with just a single list.
      // TODO(REMIX-2554): Design a general deprecation solution for configs that are no longer required.
      if (dynamicDecalTextures().size() > 0) {
        decalTexturesRef().insert(dynamicDecalTextures().begin(), dynamicDecalTextures().end());
        dynamicDecalTexturesRef().clear();
        Logger::info("[Deprecated Config] rtx.dynamicDecalTextures has been deprecated, we have moved all your texture's from this list to rtx.decalTextures, no further action is required from you.  Please re-save your rtx config to get rid of this message.");
      }
      if (singleOffsetDecalTextures().size() > 0) {
        decalTexturesRef().insert(singleOffsetDecalTextures().begin(), singleOffsetDecalTextures().end());
        singleOffsetDecalTexturesRef().clear();
        Logger::info("[Deprecated Config] rtx.singleOffsetDecalTextures has been deprecated, we have moved all your texture's from this list to rtx.decalTextures, no further action is required from you.  Please re-save your rtx config to get rid of this message.");
      }
      if (nonOffsetDecalTextures().size() > 0) {
        decalTexturesRef().insert(nonOffsetDecalTextures().begin(), nonOffsetDecalTextures().end());
        nonOffsetDecalTexturesRef().clear();
        Logger::info("[Deprecated Config] rtx.nonOffsetDecalTextures has been deprecated, we have moved all your texture's from this list to rtx.decalTextures, no further action is required from you.  Please re-save your rtx config to get rid of this message.");
      }
    }

    void updateUpscalerFromDlssPreset();
    void updateUpscalerFromNisPreset();
    void updateUpscalerFromTaauPreset();
    void updatePresetFromUpscaler();
    NV_GPU_ARCHITECTURE_ID getNvidiaArch();
    NV_GPU_ARCH_IMPLEMENTATION_ID getNvidiaChipId();
    void updateGraphicsPresets(const DxvkDevice* device);
    void updateLightingSetting();
    void updatePathTracerPreset(PathTracerPreset preset);
    void updateRaytraceModePresets(const uint32_t vendorID, const VkDriverId driverID);

    void resetUpscaler();

    inline static const std::string kRtxConfigFilePath = "rtx.conf";

    void serialize() {
      Config newConfig;
      RtxOption<bool>::writeOptions(newConfig, serializeChangedOptionOnly());
      Config::serializeCustomConfig(newConfig, kRtxConfigFilePath, "rtx.");
    }

    void reset() {
      RtxOption<bool>::resetOptions();
    }

    static std::unique_ptr<RtxOptions>& Create(const Config& options) {
      if (pInstance == nullptr)
        pInstance = std::make_unique<RtxOptions>(options);
      return pInstance;
    }

    static std::unique_ptr<RtxOptions>& Get() { return pInstance; }

    bool getRayPortalTextureIndex(const XXH64_hash_t& h, std::size_t& index) const {
      const auto findResult = std::find(rayPortalModelTextureHashes().begin(), rayPortalModelTextureHashes().end(), h);

      if (findResult == rayPortalModelTextureHashes().end()) {
        return false;
      }

      index = std::distance(rayPortalModelTextureHashes().begin(), findResult);

      return true;
    }

    bool shouldConvertToLight(const XXH64_hash_t& h) const {
      return lightConverter().find(h) != lightConverter().end();
    }

    const ivec2 getDrawCallRange() const { Vector2i v = drawCallRange(); return ivec2{v.x, v.y}; }
    uint32_t getMinPrimsInStaticBLAS() const { return minPrimsInStaticBLAS(); }

    // Camera
    CameraAnimationMode getCameraAnimationMode() { return cameraAnimationMode(); }
    bool isCameraShaking() { return shakeCamera(); }
    int getCameraShakePeriod() { return cameraShakePeriod(); }
    float getCameraAnimationAmplitude() { return cameraAnimationAmplitude(); }
    bool getSkipObjectsWithUnknownCamera() const { return skipObjectsWithUnknownCamera(); }

    bool isRayPortalVirtualInstanceMatchingEnabled() const { return useRayPortalVirtualInstanceMatching(); }
    bool isPortalFadeInEffectEnabled() const { return enablePortalFadeInEffect(); }
    bool isUpscalerEnabled() const { return upscalerType() != UpscalerType::None; }

    bool isRayReconstructionEnabled() const {
      return upscalerType() == UpscalerType::DLSS && enableRayReconstruction() && showRayReconstructionUI();
    }

    bool showRayReconstructionOption() const {
      return RtxOptions::Get()->upscalerType() == UpscalerType::DLSS && showRayReconstructionUI();
    }

    bool isDLSSEnabled() const {
      return upscalerType() == UpscalerType::DLSS && !(enableRayReconstruction() && showRayReconstructionUI());
    }

    bool isDLSSOrRayReconstructionEnabled() const {
      return upscalerType() == UpscalerType::DLSS;
    }

    bool isNISEnabled() const { return upscalerType() == UpscalerType::NIS; }
    bool isTAAEnabled() const { return upscalerType() == UpscalerType::TAAU; }
    bool isDirectLightingEnabled() const { return enableDirectLighting(); }
    bool isSecondaryBouncesEnabled() const { return enableSecondaryBounces(); }
    bool isDenoiserEnabled() const { return useDenoiser(); }
    bool isSeparatedDenoiserEnabled() const { return denoiseDirectAndIndirectLightingSeparately(); }
    bool isReplaceDirectSpecularHitTWithIndirectSpecularHitTEnabled() const { return replaceDirectSpecularHitTWithIndirectSpecularHitT(); }
    void setReplaceDirectSpecularHitTWithIndirectSpecularHitT(const bool enableReplaceDirectSpecularHitTWithIndirectSpecularHitT) {
      replaceDirectSpecularHitTWithIndirectSpecularHitTRef() = enableReplaceDirectSpecularHitTWithIndirectSpecularHitT;
    }
    bool isAdaptiveResolutionDenoisingEnabled() const { return adaptiveResolutionDenoising(); }
    bool shouldCaptureDebugImage() const { return captureDebugImage(); }
    bool isLiveShaderEditModeEnabled() const { return useLiveShaderEditMode(); }
    bool isZUp() const { return zUp(); }
    bool isLeftHandedCoordinateSystem() const { return leftHandedCoordinateSystem(); }
    float getUniqueObjectDistanceSqr() const { return uniqueObjectDistance() * uniqueObjectDistance(); }
    float getResolutionScale() const { return resolutionScale(); }
    DLSSProfile getDLSSQuality() const { return qualityDLSS(); }
    uint32_t getNumFramesToKeepInstances() const { return numFramesToKeepInstances(); }
    uint32_t getNumFramesToKeepBLAS() const { return numFramesToKeepBLAS(); }
    uint32_t getNumFramesToKeepLights() const { return numFramesToKeepLights(); }
    uint32_t getNumFramesToPutLightsToSleep() const { return numFramesToKeepLights() /2; }
    float getMeterToWorldUnitScale() const { return 100.f * getSceneScale(); } // RTX Remix world unit is in 1cm 
    float getSceneScale() const { return sceneScale(); }

    // Render Pass Modes
    //RenderPassVolumeIntegrateRaytraceMode getRenderPassVolumeIntegrateRaytraceMode() const { return renderPassVolumeIntegrateRaytraceMode; }
    RenderPassGBufferRaytraceMode getRenderPassGBufferRaytraceMode() const { return renderPassGBufferRaytraceMode(); }
    RenderPassIntegrateDirectRaytraceMode getRenderPassIntegrateDirectRaytraceMode() const { return renderPassIntegrateDirectRaytraceMode(); }
    RenderPassIntegrateIndirectRaytraceMode getRenderPassIntegrateIndirectRaytraceMode() const { return renderPassIntegrateIndirectRaytraceMode(); }

    // Resolve Options
    uint8_t getPrimaryRayMaxInteractions() const { return primaryRayMaxInteractions(); }
    uint8_t getPSRRayMaxInteractions() const { return psrRayMaxInteractions(); }
    uint8_t getSecondaryRayMaxInteractions() const { return secondaryRayMaxInteractions(); }
    bool areDirectTranslucentShadowsEnabled() const { return enableDirectTranslucentShadows(); }
    bool areIndirectTranslucentShadowsEnabled() const { return enableIndirectTranslucentShadows(); }
    float getResolveTransparencyThreshold() const { return resolveTransparencyThreshold(); }
    float getResolveOpaquenessThreshold() const { return resolveOpaquenessThreshold(); }
    
    // Returns shared enablement composed of multiple enablement inputs
    bool needsMeshBoundingBox();

    // PSR Options
    bool isPSRREnabled() const { return enablePSRR(); }
    bool isPSTREnabled() const { return enablePSTR(); }
    uint8_t getPSRRMaxBounces() const { return psrrMaxBounces(); }
    uint8_t getPSTRMaxBounces() const { return pstrMaxBounces(); }
    bool isPSTROutgoingSplitApproximationEnabled() const { return enablePSTROutgoingSplitApproximation(); }
    bool isPSTRSecondaryIncidentSplitApproximationEnabled() const { return enablePSTRSecondaryIncidentSplitApproximation(); }
    
    bool getIsShaderExecutionReorderingSupported() const { return isShaderExecutionReorderingSupported(); }
    void setIsShaderExecutionReorderingSupported(bool enabled) { isShaderExecutionReorderingSupportedRef() = enabled; }
    //bool isShaderExecutionReorderingInVolumeIntegrateEnabled() const { return enableShaderExecutionReorderingInVolumeIntegrate && isShaderExecutionReorderingSupported; }
    bool isShaderExecutionReorderingInPathtracerGbufferEnabled() const { return enableShaderExecutionReorderingInPathtracerGbuffer() && isShaderExecutionReorderingSupported(); }
    //bool isShaderExecutionReorderingInPathtracerIntegrateDirectEnabled() const { return enableShaderExecutionReorderingInPathtracerIntegrateDirect && isShaderExecutionReorderingSupported; }
    bool isShaderExecutionReorderingInPathtracerIntegrateIndirectEnabled() const { return enableShaderExecutionReorderingInPathtracerIntegrateIndirect() && isShaderExecutionReorderingSupported(); }

    // Path Options
    bool isRussianRouletteEnabled() const { return enableRussianRoulette(); }
    bool isFirstBounceLobeProbabilityDitheringEnabled() const { return enableFirstBounceLobeProbabilityDithering(); }
    bool isUnorderedResolveInIndirectRaysEnabled() const { return enableUnorderedResolveInIndirectRays(); }
    bool isDecalMaterialBlendingEnabled() const { return enableDecalMaterialBlending(); }
    float getTranslucentDecalAlbedoFactor() const { return translucentDecalAlbedoFactor(); }
    float getRussianRoulette1stBounceMinContinueProbability() const { return russianRoulette1stBounceMinContinueProbability(); }
    float getRussianRoulette1stBounceMaxContinueProbability() const { return russianRoulette1stBounceMaxContinueProbability(); }
    uint8_t getPathMinBounces() const { return pathMinBounces(); }
    uint8_t getPathMaxBounces() const { return pathMaxBounces(); }
    float getOpaqueDiffuseLobeSamplingProbabilityZeroThreshold() const { return opaqueDiffuseLobeSamplingProbabilityZeroThreshold(); }
    float getMinOpaqueDiffuseLobeSamplingProbability() const { return minOpaqueDiffuseLobeSamplingProbability(); }
    float getOpaqueSpecularLobeSamplingProbabilityZeroThreshold() const { return opaqueSpecularLobeSamplingProbabilityZeroThreshold(); }
    float getMinOpaqueSpecularLobeSamplingProbability() const { return minOpaqueSpecularLobeSamplingProbability(); }
    float getOpaqueOpacityTransmissionLobeSamplingProbabilityZeroThreshold() const { return opaqueOpacityTransmissionLobeSamplingProbabilityZeroThreshold(); }
    float getMinOpaqueOpacityTransmissionLobeSamplingProbability() const { return minOpaqueOpacityTransmissionLobeSamplingProbability(); }
    float getTranslucentSpecularLobeSamplingProbabilityZeroThreshold() const { return translucentSpecularLobeSamplingProbabilityZeroThreshold(); }
    float getMinTranslucentSpecularLobeSamplingProbability() const { return minTranslucentSpecularLobeSamplingProbability(); }
    float getTranslucentTransmissionLobeSamplingProbabilityZeroThreshold() const { return translucentTransmissionLobeSamplingProbabilityZeroThreshold(); }
    float getMinTranslucentTransmissionLobeSamplingProbability() const { return minTranslucentTransmissionLobeSamplingProbability(); }
    float getIndirectRaySpreadAngleFactor() const { return indirectRaySpreadAngleFactor(); }
    bool getRngSeedWithFrameIndex() const { return rngSeedWithFrameIndex(); }

    // Light Selection/Sampling Options
    uint16_t getRISLightSampleCount() const { return risLightSampleCount(); }

    // Volumetrics Options
    uint32_t getFroxelGridResolutionScale() const { return froxelGridResolutionScale(); }
    uint16_t getFroxelDepthSlices() const { return froxelDepthSlices(); }
    uint8_t getMaxAccumulationFrames() const { return maxAccumulationFrames(); }
    float getFroxelDepthSliceDistributionExponent() const { return froxelDepthSliceDistributionExponent(); }
    float getFroxelMaxDistance() const { return froxelMaxDistance(); }
    float getFroxelFireflyFilteringLuminanceThreshold() const { return froxelFireflyFilteringLuminanceThreshold(); }
    float getFroxelFilterGaussianSigma() const { return froxelFilterGaussianSigma(); }
    bool isVolumetricEnableInitialVisibilityEnabled() const { return volumetricEnableInitialVisibility(); }
    bool isVolumetricEnableTemporalResamplingEnabled() const { return volumetricEnableTemporalResampling(); }
    uint16_t getVolumetricTemporalReuseMaxSampleCount() const { return volumetricTemporalReuseMaxSampleCount(); }
    float getVolumetricClampedReprojectionConfidencePenalty() const { return volumetricClampedReprojectionConfidencePenalty(); }
    uint8_t getFroxelMinReservoirSamples() const { return froxelMinReservoirSamples(); }
    uint8_t getFroxelMaxReservoirSamples() const { return froxelMaxReservoirSamples(); }
    uint8_t getFroxelMinKernelRadius() const { return froxelMinKernelRadius(); }
    uint8_t getFroxelMaxKernelRadius() const { return froxelMaxKernelRadius(); }
    uint8_t getFroxelMinReservoirSamplesStabilityHistory() const { return froxelMinReservoirSamplesStabilityHistory(); }
    uint8_t getFroxelReservoirSamplesStabilityHistoryRange() const { return cachedFroxelReservoirSamplesStabilityHistoryRange; }
    uint8_t getFroxelMinKernelRadiusStabilityHistory() const { return froxelMinKernelRadiusStabilityHistory(); }
    uint8_t getFroxelKernelRadiusStabilityHistoryRange() const { return cachedFroxelKernelRadiusStabilityHistoryRange; }
    float getFroxelReservoirSamplesStabilityHistoryPower() const { return froxelReservoirSamplesStabilityHistoryPower(); }
    float getFroxelKernelRadiusStabilityHistoryPower() const { return froxelKernelRadiusStabilityHistoryPower(); }
    bool isVolumetricLightingEnabled() const { return enableVolumetricLighting(); }
    Vector3 getVolumetricTransmittanceColor() const { return volumetricTransmittanceColor(); }
    float getVolumetricTransmittanceMeasurementDistance() const { return volumetricTransmittanceMeasurementDistance(); };
    Vector3 getVolumetricSingleScatteringAlbedo() const { return volumetricSingleScatteringAlbedo(); };
    float getVolumetricAnisotropy() const { return volumetricAnisotropy(); }
    float getFogRemapMaxDistanceMin() const { return fogRemapMaxDistanceMin(); }
    float getFogRemapMaxDistanceMax() const { return fogRemapMaxDistanceMax(); }
    float getFogRemapTransmittanceMeasurementDistanceMin() const { return fogRemapTransmittanceMeasurementDistanceMin(); }
    float getFogRemapTransmittanceMeasurementDistanceMax() const { return fogRemapTransmittanceMeasurementDistanceMax(); }
    
    // Alpha Test/Blend Options
    bool isAlphaBlendEnabled() const { return enableAlphaBlend(); }
    bool isAlphaTestEnabled() const { return enableAlphaTest(); }
    bool isEmissiveBlendEmissiveOverrideEnabled() const { return enableEmissiveBlendEmissiveOverride(); }
    float getEmissiveBlendOverrideEmissiveIntensity() const { return emissiveBlendOverrideEmissiveIntensity(); }
    float getParticleSoftnessFactor() const { return particleSoftnessFactor(); }

    // Ray Portal Options
    std::size_t getRayPortalPairCount() const { return rayPortalModelTextureHashes().size() / 2; }
    Vector3 getRayPortalWidthAxis() const { return rayPortalModelWidthAxis(); }
    Vector3 getRayPortalHeightAxis() const { return rayPortalModelHeightAxis(); }
    float getRayPortalSamplingWeightMinDistance() const { return rayPortalSamplingWeightMinDistance(); }
    float getRayPortalSamplingWeightMaxDistance() const { return rayPortalSamplingWeightMaxDistance(); }
    bool getRayPortalCameraHistoryCorrection() const { return rayPortalCameraHistoryCorrection(); }
    bool getRayPortalCameraInBetweenPortalsCorrection() const { return rayPortalCameraInBetweenPortalsCorrection(); }

    bool getWhiteMaterialModeEnabled() const { return useWhiteMaterialMode(); }
    bool getHighlightLegacyModeEnabled() const { return useHighlightLegacyMode(); }
    bool getHighlightUnsafeAnchorModeEnabled() const { return useHighlightUnsafeAnchorMode(); }
    bool getHighlightUnsafeReplacementModeEnabled() const { return useHighlightUnsafeReplacementMode(); }
    float getNativeMipBias() const { return nativeMipBias(); }
    bool getAnisotropicFilteringEnabled() const { return useAnisotropicFiltering(); }
    float getMaxAnisotropySamples() const { return maxAnisotropySamples(); }

    // Developer Options
    ivec2 getDrawCallRange() { Vector2i v = drawCallRange(); return ivec2{v.x, v.y}; };
    Vector3 getOverrideWorldOffset() { return instanceOverrideWorldOffset(); }
    uint getInstanceOverrideInstanceIdx() { return instanceOverrideInstanceIdx(); }
    uint getInstanceOverrideInstanceIdxRange() { return instanceOverrideInstanceIdxRange(); }
    bool getInstanceOverrideSelectedPrintMaterialHash() { return instanceOverrideSelectedInstancePrintMaterialHash(); }
    
    bool getIsOpacityMicromapSupported() const { return opacityMicromap.isSupported; }
    void setIsOpacityMicromapSupported(bool enabled) { opacityMicromap.isSupported = enabled; }
    bool getEnableOpacityMicromap() const { return opacityMicromap.enable() && opacityMicromap.isSupported; }
    void setEnableOpacityMicromap(bool enabled) { opacityMicromap.enableRef() = enabled; }

    bool getEnableAnyReplacements() { return enableReplacementAssets() && (enableReplacementLights() || enableReplacementMeshes() || enableReplacementMaterials()); }
    bool getEnableReplacementLights() { return enableReplacementAssets() && enableReplacementLights(); }
    bool getEnableReplacementMeshes() { return enableReplacementAssets() && enableReplacementMeshes(); }
    bool getEnableReplacementMaterials() { return enableReplacementAssets() && enableReplacementMaterials(); }

    // Capture Options
    //   General
    bool getCaptureShowMenuOnHotkey() const { return m_captureShowMenuOnHotkey.getValue(); }
    bool getCaptureInstances() const {
      if(m_captureNoInstance.getValue() != m_captureNoInstance.getDefaultValue()) {
        Logger::warn("rtx.captureNoInstance has been deprecated, but will still be respected for the time being, unless rtx.captureInstances is set.");
        if(m_captureInstances.getValue() != m_captureInstances.getDefaultValue()) {
          return m_captureInstances;
        }
        return !m_captureNoInstance;
      }
      return m_captureInstances;
    }
    std::string getCaptureInstanceStageName() const { return captureInstanceStageName(); }
    //   Multiframe
    bool getCaptureEnableMultiframe() const { return m_captureEnableMultiframe.getValue(); }
    uint32_t getCaptureMaxFrames() const { return captureMaxFrames(); }
    //   Advanced
    uint32_t getCaptureFramesPerSecond() const { return captureFramesPerSecond(); }
    //     Mesh
    float getCaptureMeshPositionDelta() const { return captureMeshPositionDelta(); }
    float getCaptureMeshNormalDelta() const { return captureMeshNormalDelta(); }
    float getCaptureMeshTexcoordDelta() const { return captureMeshTexcoordDelta(); }
    float getCaptureMeshColorDelta() const { return captureMeshColorDelta(); }
    float getCaptureMeshBlendWeightDelta() const { return captureMeshBlendWeightDelta(); }
    
    
    bool isUseVirtualShadingNormalsForDenoisingEnabled() const { return useVirtualShadingNormalsForDenoising(); }
    bool isResetDenoiserHistoryOnSettingsChangeEnabled() const { return resetDenoiserHistoryOnSettingsChange(); }
    
    int32_t getPresentThrottleDelay() const { return enablePresentThrottle() ? presentThrottleDelay() : 0; }
    bool getValidateCPUIndexData() const { return validateCPUIndexData(); }

    float getEffectLightIntensity() const { return effectLightIntensity(); }
    float getEffectLightRadius() const { return effectLightRadius(); }
    bool getEffectLightPlasmaBall() const { return effectLightPlasmaBall(); }
    std::string getCurrentDirectory() const;

    bool shouldUseObsoleteHashOnTextureUpload() const { return useObsoleteHashOnTextureUpload(); }
  };
}
