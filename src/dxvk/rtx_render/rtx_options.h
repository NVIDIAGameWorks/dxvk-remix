/*
* Copyright (c) 2021-2024, NVIDIA CORPORATION. All rights reserved.
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
#include "rtx_global_volumetrics.h"
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

  using RenderPassVolumeIntegrateRaytraceMode = RtxGlobalVolumetrics::RaytraceMode;
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
    UltraPerformance = 0,
    Performance,
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

  enum class IntegrateIndirectMode : int {
    ImportanceSampled = 0,   // Importance sampled integration - provides the noisiest output and used primarily for reference comparisons
    ReSTIRGI = 1,            // Importance Sampled + ReSTIR GI integrations
    NeuralRadianceCache = 2, // Implements a live trained neural network to provide a world space radiance cache and allow the pathtracer to terminate paths earlier into the cache.
  
    Count
  };

  class RtxOptions {
    friend class ImGUI; // <-- we want to modify these values directly.
    friend class ImGuiSplash; // <-- we want to modify these values directly.
    friend class ImGuiCapture; // <-- we want to modify these values directly.
    friend class NeuralRadianceCache; // <-- we want to modify these values directly.
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
    RW_RTX_OPTION("rtx", fast_unordered_set, ignoreTransparencyLayerTextures, {},
                  "Textures on draw calls that should not be stored in the transparency layer, when DLSS-RR is on.\n"
                  "The transparency layer stores noise-free transparent objects which bypasses DLSS-RR denoising, but it has lower anti-aliasing quality.\n"
                  "Transparent objects that have aliasing/flickering issues, like laser beams, can be added to this list to achieve better anti-aliasing quality.");
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
    RW_RTX_OPTION("rtx", fast_unordered_set, raytracedRenderTargetTextures, {}, "DescriptorHashes for Render Targets. (Screens that should display the output of another camera).");
    
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


    // Shader Compilation
    struct Shader {
      // Note: Shader recompilation is only useful with a development setup for the most part and is disabled when REMIX_DEVELOPMENT is not defined,
      // so these options will not take effect in such builds. They are however still included rather than ifdeffed out to keep consistent options documentation
      // across builds.
      // Todo: This is currently not hooked into the build system, still need to determine what the external process for compiling shaders should be (either via the build system or a script).
      // See REMIX-3930 for more information.
      RTX_OPTION_ENV("rtx.shader", std::string, shaderBinaryPath, "", "RTX_SHADER_BINARY_PATH", "A relative or absolute path pointing at a folder containing SPIR-V binaries of Remix shaders to use for runtime shader recompilation. A blank string indicates that the path generated by the build system should be used instead.");
      RTX_OPTION("rtx.shader", bool, recompileOnLaunch, false,
                 "When set to true runtime shader recompilation will execute on the first frame after launch.\n"
                 "This option is mainly meant for development use and should not be set for user-facing operation. Also see rtx.useLiveShaderEditMode for a similar option which auto-detects shader changes instead.");
      RTX_OPTION("rtx.shader", bool, useLiveEditMode, false,
                 "When set to true shaders will be automatically recompiled when any shader file is updated (saved for instance) in addition to the usual manual recompilation trigger.\n"
                 "This option is mainly meant for development use and should not be set for user-facing operation.");

      RTX_OPTION_ENV("rtx.shader", bool, prewarmAllVariants, false, "RTX_PREWARM_ALL_VARIANTS",
                     "When set to true, all variants of shaders will be prewarmed at launch. Only takes effect when rtx.initializer.asyncShaderPrewarming is set to true.\n"
                     "By default Remix only prewarms shaders which may actually be used at runtime or are accessible by user-facing graphics menus rather than all shader variants accessible by changing options in the developer menu.\n"
                     "This has the benefit of minimizing shader compilation cost for typical users, but may cause shader compilation stalls when changing various options in the developer menu. As such, this option is useful to enable during development to minimize these stalls.\n"
                     "Do note however that enabling this option will have a significant performance impact whenever shaders are uncached (e.g. on first load) due to requiring many more shaders to be compiled. As such using the enviornment variable to set this option locally on a developer's machine is recommended over a configuration file change to ensure it is not accidently enabled for users.");
      RTX_OPTION_ENV("rtx.shader", bool, enableAsyncCompilation, true, "RTX_ENABLE_ASYNC_COMPILATION",
                 "When set to true shader compilation (especially that of prewarming) will be done asynchronously rather than blocking.\n"
                 "Typically shader prewarming with async finalization is done to attempt to compile all required shader variants before they are used, often by overlapping this work with a startup sequence (e.g. a game's loading screen). Often times however this prewarming takes longer than the time available, or an application may not have a startup sequence to begin with and immediately begin using Remix shaders.\n"
                 "To accomodate this, async shader compilation allows for this work to be done asynchronously to avoid blocking the application at the cost of being unable to render anything until the process is complete.\n"
                 "This is typically better choice than blocking however and is recommended to be enabled as on Windows Remix blocking will cause the application to stop responding, making it seem as if the application has crashed if shader compilation takes a long time. Additionally, when combined with rtx.shader.enableAsyncCompilationUI the progress of the compilation process can be shown to the user as a UI, improving user experience.\n"
                 "The main downside to this approach is that when blocking shader compilation is allowed to take up more of the CPU, whereas async shader compilation will have to compete with the application which can make compilation take slightly longer than it would otherwise (especially true if the application's framerate is uncapped).\n"
                 "To mitigate this, Remix can optionally throttle the application during async compilation via rtx.shader.asyncCompilationThrottleMilliseconds to ensure enough time is available for compilation.\n"
                 "Finally, a more minor downside is that when async shader compilation is in use Remix currently has no way of keeping the application in a startup sequence (e.g. keeping a game on its loading screen) while it waits for shaders to compile.\n"
                 "This will mean for instance a game's menu may be active but not be able to render until the compilation is complete, rather than blocking on the loading screen and transitioning to the menu only once all shaders are loaded. Not blocking the application is typically better for user experience regardless though as long as some sort of progress UI is displayed to indicate what is happening.");
      RTX_OPTION("rtx.shader", bool, enableAsyncCompilationUI, true,
                 "Enables a UI message when async shader compilation is in progress to indicate the current compilation progress. Only takes effect when rtx.shader.enableAsyncCompilation is true.\n"
                 "This should usually be enabled as providing information to the user about the current progress of compilation is useful. May be disabled however for automated testing purposes if the nondeterministic behavior of the UI's rendered text interferes with testing.");
      RTX_OPTION("rtx.shader", std::uint32_t, asyncCompilationThrottleMilliseconds, 33,
                 "Specifies a time in milliseconds to throttle each application frame when async shader compilation is in progress. Set to 0 to disable, and only takes effect when rtx.shader.enableAsyncCompilation is true.\n"
                 "This generally should be set to a value low enough to not impact the application framerate significantly (especially if non-ray traced visuals are capable of being displayed by the application while loading, e.g. an intro video), but also high enough to get the desired shader compilation performance (especially relevant if the application is fairly heavy on the CPU during async shader compilation, or on CPUs with few hardware threads).");
    } shader;

    struct RaytracedRenderTarget {
      RTX_OPTION("rtx.raytracedRenderTarget", bool, enable, true, "Enables or disables raytracing for render-to-texture effects.  The render target to be raytraced must be specified in the texture selection menu.");
    } raytracedRenderTarget;

    struct ViewModel {
      friend class ImGUI;
      RTX_OPTION("rtx.viewModel", bool, enable, false, "If true, try to resolve view models (e.g. first-person weapons). World geometry doesn't have shadows / reflections / etc from the view models.");
      RTX_OPTION("rtx.viewModel", float, rangeMeters, 1.0f, "[meters] Max distance at which to find a portal for view model virtual instances. If rtx.viewModel.separateRays is true, this is also max length of view model rays.");
      RTX_OPTION("rtx.viewModel", float, scale, 1.0f, "Scale for view models. Minimize to prevent clipping.");
      RTX_OPTION("rtx.viewModel", bool, enableVirtualInstances, true, "If true, virtual instances are created to render the view models behind a portal.");
      RTX_OPTION("rtx.viewModel", bool, perspectiveCorrection, true, "If true, apply correction to view models (e.g. different FOV is used for view models).");
      RTX_OPTION("rtx.viewModel", float, maxZThreshold, 0.0f, "If a draw call's viewport has max depth less than or equal to this threshold, then assume that it's a view model.");
    } viewModel;

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

    RTX_OPTION("rtx", uint32_t, minPrimsInDynamicBLAS, 1000, "The minimum number of triangles required to promote a mesh to it's own BLAS, otherwise it lands in the merged BLAS with multiple other meshes.");
    RTX_OPTION("rtx", uint32_t, maxPrimsInMergedBLAS, 50000, "The maximum number of triangles for a mesh that can be in the merged BLAS.  ");
    RTX_OPTION_FLAG("rtx", bool, forceMergeAllMeshes, false, RtxOptionFlags::NoSave, "Force merges all meshes into as few BLAS as possible.  This is generally not desirable for performance, but can be a useful debugging tool.");
    RTX_OPTION_FLAG("rtx", bool, minimizeBlasMerging, false, RtxOptionFlags::NoSave, "Minimize BLAS merging to the minimum possible, this option tries to give all meshes their own BLAS.  This is generally not desirable forperformance, but can be a useful debugging tool.");

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
    RTX_OPTION_ENV("rtx", IntegrateIndirectMode, integrateIndirectMode, IntegrateIndirectMode::NeuralRadianceCache, "RTX_INTEGRATE_INDIRECT_MODE",
                   "Indirect integration mode:\n"
                   "0: Importance Sampled. Importance sampled mode uses typical GI sampling and it is not recommended for general use as it provides the noisiest output.\n"
                   "   It serves as a reference integration mode for validation of other indirect integration modes.\n"
                   "1: ReSTIR GI. ReSTIR GI provides improved indirect path sampling over \"Importance Sampled\" mode \n"
                   "   with better indirect diffuse and specular GI quality at increased performance cost.\n"
                   "2: Neural Radiance Cache (NRC). NRC is an AI based world space radiance cache. It is live trained by the path tracer\n"
                   "   and allows paths to terminate early by looking up the cached value and saving performance.\n"
                   "   NRC supports infinite bounces and often provides results closer to that of reference than ReSTIR GI\n"
                   "   while improving performance in scenarios where ray paths have 2 or more bounces on average.\n");
    RTX_OPTION_ENV("rtx", UpscalerType, upscalerType, UpscalerType::DLSS, "DXVK_UPSCALER_TYPE", "Upscaling boosts performance with varying degrees of image quality tradeoff depending on the type of upscaler and the quality mode/preset.");
    RTX_OPTION_ENV("rtx", bool, enableRayReconstruction, true, "DXVK_RAY_RECONSTRUCTION", "Enable ray reconstruction.");

    RW_RTX_OPTION_FLAG("rtx", bool, lowMemoryGpu, false, RtxOptionFlags::NoSave, "Enables low memory mode, where we aggressively detune caches and streaming systems to accomodate the lower memory available.");

    RTX_OPTION("rtx", float, resolutionScale, 0.75f, "");
    RTX_OPTION("rtx", bool, forceCameraJitter, false, "Force enables camera jitter frame to frame.");
    RTX_OPTION("rtx", uint32_t, cameraJitterSequenceLength, 64, "Sets a camera jitter sequence length [number of frames]. It will loop around once the length is reached.");
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

    RW_RTX_OPTION_ENV("rtx", DLSSProfile, qualityDLSS, DLSSProfile::Auto, "RTX_QUALITY_DLSS", "Adjusts internal DLSS scaling factor, trades quality for performance.");
    // Note: All ray tracing modes depend on the rtx.raytraceModePreset option as they may be overridden by automatic defaults for a specific vendor if the preset is set to Auto. Set
    // to Custom to ensure these settings are not overridden.
    //RenderPassVolumeIntegrateRaytraceMode renderPassVolumeIntegrateRaytraceMode = RenderPassVolumeIntegrateRaytraceMode::RayQuery;
    RTX_OPTION_ENV("rtx", RenderPassGBufferRaytraceMode, renderPassGBufferRaytraceMode, RenderPassGBufferRaytraceMode::RayQuery, "DXVK_RENDER_PASS_GBUFFER_RAYTRACE_MODE",
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
    RTX_OPTION("rtx", uint32_t, numFramesToKeepBLAS, 1, "");
    RTX_OPTION("rtx", uint32_t, numFramesToKeepLights, 100, ""); // NOTE: This was the default we've had for a while, can probably be reduced...

    static uint32_t numFramesToKeepGeometryData() {
      return numFramesToKeepBLAS();
    }

    static uint32_t numFramesToKeepMaterialTextures() {
      return numFramesToKeepBLAS();
    }

    static bool enablePreviousTLAS() {
      return !isRayReconstructionEnabled() || useReSTIRGI();
    }

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
    RTX_OPTION_ENV("rtx", bool, enableDirectTranslucentShadows, false, "RTX_ENABLE_DIRECT_TRANSLUCENT_SHADOWS", "Calculate coloured shadows for translucent materials (i.e. glass, water) in direct lighting. In engineering terms: include OBJECT_MASK_TRANSLUCENT into primary visibility rays.");
    RTX_OPTION_ENV("rtx", bool, enableDirectAlphaBlendShadows, true, "RTX_ENABLE_DIRECT_ALPHABLEND_SHADOWS", "Calculate shadows for semi-transparent materials (alpha blended) in direct lighting. In engineering terms: include OBJECT_MASK_ALPHA_BLEND into primary visibility rays.");
    RTX_OPTION_ENV("rtx", bool, enableIndirectTranslucentShadows, false, "RTX_ENABLE_INDIRECT_TRANSLUCENT_SHADOWS", "Calculate coloured shadows for translucent materials (i.e. glass, water) in indirect lighting (i.e. reflections and GI). In engineering terms: include OBJECT_MASK_TRANSLUCENT into secondary visibility rays.");
    RTX_OPTION_ENV("rtx", bool, enableIndirectAlphaBlendShadows, true, "RTX_ENABLE_INDIRECT_ALPHABLEND_SHADOWS", "Calculate shadows for semi-transparent (alpha blended) objects in indirect lighting (i.e. reflections and GI). In engineering terms: include OBJECT_MASK_ALPHA_BLEND into secondary visibility rays.");

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

    // Subsurface Scattering
    struct SubsurfaceScattering {
      friend class RtxOptions;
      friend class ImGUI;

      RTX_OPTION("rtx.subsurface", bool, enableThinOpaque, true, "Enable thin opaque material. The materials withthin opaque properties will fallback to normal opaque material.");
      RTX_OPTION("rtx.subsurface", bool, enableTextureMaps, true, "Enable texture maps such as thickness map or scattering albedo map. The corresponding subsurface properties will fallback to per-material constants if this is disabled.");
      RTX_OPTION("rtx.subsurface", float, surfaceThicknessScale, 1.0f, "Scalar of the subsurface thickness.");
      RTX_OPTION("rtx.subsurface", bool, enableDiffusionProfile, true, "Enable subsurface material. Solve subsurface rendering equation with (burley/SOTO) diffusion profile.");
      RTX_OPTION("rtx.subsurface", float, diffusionProfileScale, 1.0f, "Scalar of the diffusion profile scale.");
      RTX_OPTION("rtx.subsurface", bool, enableTransmission, true, "Enable subsurface transmission. Implement single scattering transmission for thin or curved SSS surface.");
      RTX_OPTION("rtx.subsurface", bool, enableTransmissionSingleScattering, true, "Enable single scattering for subsurface transmission. If this option is disabled, then the refracted ray will not be scattered again inside of the volume.");
      RTX_OPTION("rtx.subsurface", bool, enableTransmissionDiffusionProfileCorrection, false,
        "Enable diffusion profile correction when enabling SSS Transmission.\n"
        "Both burley's diffusion profile and SSS Transmission includes the single scattering energy.\n"
        "The correction removes the single scattering part from diffusion profile to avoid double counting the single scattering energy.");
      RTX_OPTION("rtx.subsurface", uint8_t, transmissionBsdfSampleCount, 1, "The sample count for transmission BSDF.(1spp as default)");
      RTX_OPTION("rtx.subsurface", uint8_t, transmissionSingleScatteringSampleCount, 1, "The sample count for every single scattering on BSDF transmission (refracted) ray.(1spp as default)");
      RTX_OPTION("rtx.subsurface", Vector2i, diffusionProfileDebugPixelPosition, Vector2i(INT32_MAX, INT32_MAX), "Pixel position where we show debugging sampling positions for diffusion profile. Requires set debug view to 'SSS Diffusion Profile Sampling'.");
    };

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
    RTX_OPTION_FLAG_ENV("rtx", bool, enableBreakIntoDebuggerOnPressingB, false, RtxOptionFlags::NoSave, "RTX_BREAK_INTO_DEBUGGER_ON_PRESSING_B",
                    "Enables a break into a debugger at the start of InjectRTX() on a press of key \'B\'.\n"
                    "If debugger is not attached at the time, it will wait until a debugger is attached and break into it then.");
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
    RTX_OPTION("rtx", std::uint32_t, presentThrottleDelay, 16U,
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
    RTX_OPTION("rtx", bool, enableReplacementInstancerMeshRendering, true,
               "Enables or disables rendering GeomPointInstancer meshes using an optimized path.\n"
               "Requires reloading replacement assets.");
    RTX_OPTION("rtx", uint, adaptiveResolutionReservedGPUMemoryGiB, 2,
               "The amount of GPU memory in gibibytes to reserve away from consideration for adaptive resolution replacement textures.\n"
               "This value should only be changed to reflect the estimated amount of memory Remix itself consumes on the GPU (aside from texture loading, mostly from rendering-related buffers) and should not be changed otherwise.\n"
               "Only relevant when force high resolution replacement textures is disabled and adaptive resolution replacement textures is enabled. See asset estimated size parameter for more information.\n");
    RTX_OPTION("rtx", uint, limitedBonesPerVertex, 4,
               "Limit the number of bone influences per vertex for replacement geometry.  D3D9 games were limited to 4, which is the default.  In rare instances you may want to increase this based on your preference for replaced assets.  This config only takes affect when set on startup via the rtx.conf.");

    struct TextureManager {
      RTX_OPTION("rtx.texturemanager", int, budgetPercentageOfAvailableVram, 50,
                 "The percentage of available VRAM we should use for material textures.  If material textures are required beyond "
                 "this budget, then those textures will be loaded at lower quality.  Important note, it's impossible to perfectly "
                 "match the budget while maintaining reasonable quality levels, so use this as more of a guideline.  If the "
                 "replacements assets are simply too large for the target GPUs available vid mem, we may end up going overbudget "
                 "regularly.  Defaults to 50% of the available VRAM.");
      RTX_OPTION("rtx.texturemanager", bool, fixedBudgetEnable, false, "If true, rtx.texturemanager.fixedBudgetMiB is used instead of rtx.texturemanager.budgetPercentageOfAvailableVram.");
      RTX_OPTION("rtx.texturemanager", int, fixedBudgetMiB, 2048, "Fixed-size VRAM budget for replacement textures. In mebibytes. To use, set rtx.texturemanager.fixedBudgetEnable to True.");
      RTX_OPTION_ENV("rtx.texturemanager", bool, samplerFeedbackEnable, true, "DXVK_TEXTURES_SAMPLER_FEEDBACK_ENABLE",
                 "Enable texture sampler feedback. If true, a texture prioritization logic considers the amount of mip-levels that was sampled by a GPU while rendering a scene."
                 "(For example, if a texture is in the distance, it will have a lower priority compared to a texture rendered just in front of the camera).");
      RTX_OPTION_FLAG_ENV("rtx.texturemanager", bool, neverDowngradeTextures, false, RtxOptionFlags::NoSave, "DXVK_TEXTURES_NEVER_DOWNGRADE", 
                 "Debug option to forcibly prevent uploading lower resolution data, if the texture already has been promoted to a high resolution.");
      RTX_OPTION("rtx.texturemanager", int, stagingBufferSizeMiB, 96,
                 "Size of a pre-allocated staging (intermediate) buffer to use when sending a texture from a RAM to GPU VRAM. "
                 "If a texture size exceeds this limit, it will not be considered for the texture streaming. In mebibytes.");
    };
    RTX_OPTION("rtx", bool, reloadTextureWhenResolutionChanged, false, "Reload texture when resolution changed.");
    RTX_OPTION_FLAG_ENV("rtx", bool, alwaysWaitForAsyncTextures, false, RtxOptionFlags::NoSave, "DXVK_WAIT_ASYNC_TEXTURES", 
               "Force CPU to wait for the texture upload. Do not use an asynchronous thread for textures. If true, a frame stutter should be expected.");
    RTX_OPTION_FLAG_ENV("rtx.initializer", bool, asyncAssetLoading, true, RtxOptionFlags::NoSave, "DXVK_ASYNC_ASSET_LOADING", "If true, a separate thread is created to load USD assets asynchronously.");
    RTX_OPTION("rtx", bool, usePartialDdsLoader, true,
               "A flag controlling if the partial DDS loader should be used, true to enable, false to disable and use GLI instead.\n"
               "Generally this should be always enabled as it allows for simple parsing of DDS header information without loading the entire texture into memory like GLI does to retrieve similar information.\n"
               "Should only be set to false for debugging purposes if the partial DDS loader's logic is suspected to be incorrect to compare against GLI's implementation.");

    RTX_OPTION("rtx", TonemappingMode, tonemappingMode, TonemappingMode::Local,
               "The tonemapping type to use, 0 for Global, 1 for Local (Default).\n"
               "Global tonemapping tonemaps the image with respect to global parameters, usually based on statistics about the rendered image as a whole.\n"
               "Local tonemapping on the other hand uses more spatially-local parameters determined by regions of the rendered image rather than the whole image.\n"
               "Local tonemapping can result in better preservation of highlights and shadows in scenes with high amounts of dynamic range whereas global tonemapping may have to comprimise between over or underexposure.");
    RTX_OPTION("rtx", bool, useLegacyACES, true,
               "Use a luminance-only approximation of ACES that over-saturates the highlights. If false, use a refined ACES transform that converts between color spaces with more precision.");
    RTX_OPTION("rtx", bool, showLegacyACESOption, false,
               "Show \'rtx.useLegacyACES\' in the developer menu. Default is OFF, as the non-legacy ACES is currently experimental and the implementation is a subject to change.");

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

    RTX_OPTION("rtx", bool, fogIgnoreSky, false, "If true, sky draw calls will be skipped when searching for the D3D9 fog values.")

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

    RTX_OPTION("rtx.terrain", bool, terrainAsDecalsEnabledIfNoBaker, false, "If terrain baker is disabled, attempt to blend with the decals.");
    RTX_OPTION("rtx.terrain", bool, terrainAsDecalsAllowOverModulate, false, "Set to true, if it's known that terrain layers with ModulateX2 / ModulateX4 flags do not contain a lighting info, but ModulateX2 / ModulateX4 are used only to blend layers.");

    RTX_OPTION("rtx.userBrightness", int, userBrightness, 50, "How bright the final image should be. [0,100] range.");
    RTX_OPTION("rtx.userBrightnessEVRange", float, userBrightnessEVRange, 3.f, "The exposure value (EV) range for \'rtx.userBrightness\' slider, i.e. how much of EV there is between 0 and 100 slider values.");

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
    RTX_OPTION("rtx", Vector3, effectLightColor, Vector3(1, 1, 1), "Colour of the effect light, if not using plasma ball mode.  Effect lights can be attached to materials from the remix runtime menu, using the `Add Light to Texture` texture tag in game setup.");
    RTX_OPTION("rtx", float, effectLightIntensity, 1.f, "The intensity of the effect light.  Effect lights can be attached to materials from the remix runtime menu, using the `Add Light to Texture` texture tag in game setup.");
    RTX_OPTION("rtx", float, effectLightRadius, 5.f, "The sphere radius of the effect light.  Effect lights can be attached to materials from the remix runtime menu, using the `Add Light to Texture` texture tag in game setup.");
    RTX_OPTION("rtx", bool, effectLightPlasmaBall, false, "Use plasma ball mode, in this mode the effect light color is ignored.  Effect lights can be attached to materials from the remix runtime menu, using the `Add Light to Texture` texture tag in game setup.");

    RTX_OPTION("rtx", bool, useObsoleteHashOnTextureUpload, false,
               "Whether or not to use slower XXH64 hash on texture upload.\n"
               "New projects should not enable this option as this solely exists for compatibility with older hashing schemes.");

    RTX_OPTION("rtx", bool, serializeChangedOptionOnly, true, "");

    RTX_OPTION("rtx", uint32_t, applicationId, 102100511, "Used to uniquely identify the application to DLSS. Generally should not be changed without good reason.");

    static std::unique_ptr<RtxOptions> pInstance;
    RtxOptions() { }

  public:

    RtxOptions(const Config& options) {
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
    void updateGraphicsPresets(DxvkDevice* device);
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

    static bool useReSTIRGI() {
      return integrateIndirectMode() == IntegrateIndirectMode::ReSTIRGI;
    }

    bool shouldConvertToLight(const XXH64_hash_t& h) const {
      return lightConverter().find(h) != lightConverter().end();
    }

    const ivec2 getDrawCallRange() const { Vector2i v = drawCallRange(); return ivec2{v.x, v.y}; }

    // Camera
    CameraAnimationMode getCameraAnimationMode() { return cameraAnimationMode(); }
    bool isCameraShaking() { return shakeCamera(); }
    int getCameraShakePeriod() { return cameraShakePeriod(); }
    float getCameraAnimationAmplitude() { return cameraAnimationAmplitude(); }
    bool getSkipObjectsWithUnknownCamera() const { return skipObjectsWithUnknownCamera(); }

    bool isRayPortalVirtualInstanceMatchingEnabled() const { return useRayPortalVirtualInstanceMatching(); }
    bool isPortalFadeInEffectEnabled() const { return enablePortalFadeInEffect(); }
    bool isUpscalerEnabled() const { return upscalerType() != UpscalerType::None; }

    static bool isRayReconstructionEnabled() {
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
      if (m_captureNoInstance.getValue() != m_captureNoInstance.getDefaultValue()) {
        Logger::warn("rtx.captureNoInstance has been deprecated, but will still be respected for the time being, unless rtx.captureInstances is set.");
        if (m_captureInstances.getValue() != m_captureInstances.getDefaultValue()) {
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
    
    std::uint32_t getPresentThrottleDelay() const { return enablePresentThrottle() ? presentThrottleDelay() : 0; }
    bool getValidateCPUIndexData() const { return validateCPUIndexData(); }

    float getEffectLightIntensity() const { return effectLightIntensity(); }
    float getEffectLightRadius() const { return effectLightRadius(); }
    bool getEffectLightPlasmaBall() const { return effectLightPlasmaBall(); }
    std::string getCurrentDirectory() const;

    bool shouldUseObsoleteHashOnTextureUpload() const { return useObsoleteHashOnTextureUpload(); }

    static float calcUserEVBias() {
      return (float(RtxOptions::userBrightness() - 50) / 100.f)
        * RtxOptions::userBrightnessEVRange();
    }
  };
}
