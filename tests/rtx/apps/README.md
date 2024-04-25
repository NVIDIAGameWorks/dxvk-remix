# Remix Renderer

Remix Renderer is a real-time renderer with a target of a fully path traced illumination.

Contrary to a classic rasterization, with ray tracing there's a possibility of simulating the light interactions in a more straigtforward, realistically-grounded way. 

<details>

<summary>Rasterization vs Ray Tracing [click to open]</summary>

* For example, to draw shadows with rasterization, a renderer would need to keep shadowmaps that have limitations on resolution, requires filtering (as shadows might be sharp at one point in space, but soft a meter away), they're not quite scalable to many light sources (as each would need a separate shadow map), requires object culling etc, but of course, rasterization is classic way, so it's fast -- however, with ray tracing, a shadow is simple in its core: need just to cast a ray from a surface to determine if it's in shadow, or not. The same can be said about refractions and reflections. Still, there are challenges to the path tracing, but of different kind, e.g. denoising, sampling and other.

<br>
</details>

<details>

<summary>Ray tracing vs Path tracing [click to open]</summary>

* Technically, 'ray tracing' is a term to describe a calculation of a hit point for a given ray in a scene.
But in reality, ray tracing can refer to a majority of techniques that use such ray intersection tests (mostly hardware accelerated) in some way: for example, a rasterized game renderer might have only 'ray traced reflections'.
'Path tracing' is a technique to simulate the realistic light propagation, that utilizes ray tracing only as a tool to find ray intersections within a scene.
Hence, ray tracing is a more general term, that can also loosely refer to a path tracing.

</details>

<br>

But the main advantage of using path tracing is the considerable reduction of a development time and increased scene interactivity.
Rasterized games usually bake the illumination into the surfaces and it might take a time to see the results in-game.
But with path tracing, illumination is done completely in real-time, so no time spent waiting for the preparation.
And as a consequence, scenes can be more dynamic (e.g. more objects with simulated physics) as baking usually forces objects to be immovable in rasterization.



<br>

# Remix Runtime

Remix Runtime is a bundle of:
* Direct3D 9 translation layer, to convert original D3D9 draw calls and other resources to be compatible with ray tracing
* Remix Renderer

Remix Runtime is represented as the set of files:
* `d3d9.dll` to hook to D3D9 functions, and also embeds the Remix Renderer
* DLL files of the libraries on which Remix Renderer relies
    * e.g. DLSS Super Resolution (`nvngx_dlss.dll`), Frame Generation (`nvngx_dlssg.dll`), Reflex (`NvLowLatencyVk.dll`) and others

But note that Remix Runtime might also include [Remix Bridge](https://github.com/NVIDIAGameWorks/bridge-remix) to convert Direct3D calls from 32-bit to 64-bit, as the Remix Renderer is 64-bit only.



<br>

# Remix API 

The primary purpose of the Remix API is to access the pure functionality of the Remix Renderer, i.e. without the limitations of the original Direct3D 9 interface. (For example, extended material, light parameters which can't be expressed via D3D9 primitives).

Remix API is provided as a [single self-contained plain-C header](../../../public/include/remix/remix_c.h), that is implemented by Remix Runtime's `d3d9.dll` library.

Optional [C++ header](../../../public/include/remix/remix.h) is also included, but only as a wrapper over the C API, to prevent possibility of ABI incompatibility between different С++ compilers. And for the same reason, C++ header needs to be compiled as a part of the application side sources (i.e. inlined into app's `.cpp`).



<br>

# Remix SDK

The term 'Remix SDK' simply refers to a bundle of the headers (`.h`) and Remix Renderer binaries (`.dll`).

To compile the binaries locally, ensure that [Requirements from the Build instructions](../../../README.md) are met, open a terminal in the repository's root folder, and call:

1. `meson setup --buildtype release _Comp64ReleaseSDK`
1. `cd _Comp64ReleaseSDK`
1. `meson compile copy_sdk`

Upon the successful build, the `public/` folder contains the Remix SDK:
* `public/bin/` -- all needed `.dll` files (Remix Renderer), and `usd/` folder
* `public/include` -- all needed `.h` headers (Remix API)



<br>

# Using the Remix API

> *Note: Remix API is under development, and any feature may be a subject to change.*

As with other rendering engines, there are common steps of
initialiazation, resource registration (meshes, materials, lights), and submitting the said resources to each frame to be rendered.

[remixapi_example_c.c](RemixAPI_C/remixapi_example_c.c) contains a minimal example in C to render a path traced triangle using the Remix API.


<details>

<summary>How to compile remixapi_example_c? [click to open]</summary>

1. Copy RemixSDK contents to the `RemixAPI_C/` folder
    * So there would be `RemixAPI_C/bin/` and `RemixAPI_C/include/` folders
1. From the Windows Start menu, search for `x64 Native Tools Command Prompt for VS 2019` or other version, but must be `x64`, launch it
    * This step is needed to be able to use `cl.exe` (Microsoft C/C++ compiler)
1. `cd <your dxvk-remix-nv folder>\tests\rtx\apps\RemixAPI_C`
1. `cl -Iinclude remixapi_example_c.c user32.lib`
    * This will compile `.exe`
1. Launch the `remixapi_example_c.exe`. A triangle should be rendered:

</details>

<br>

![image info](RemixAPI.jpg)

---

### Init

1. To load the Remix Renderer library, call the helper function `remixapi_lib_loadRemixDllAndInitialize`. It loads the Remix Renderer's dynamic library (`.dll`), and returns a `remixapi_Interface` struct that contains all the RemixAPI functions.

    <details>

    <summary>Why dynamic linking? [click to open]</summary>

    * Remix API is using *explicit* dynamic linking rather than implicit dynamic linking or static linking.
    With explicit dynamic linking, an API user can avoid dealing with build systems in a target application: it is assumed that a target appication may have a legacy build system that might be time-consuming to modify.

    </details>

    <details>
    
    <summary>Versioning [click to open]</summary>
    
    * When a target application (e.g. `.exe`) is being compiled with the API header, the version numbers `REMIXAPI_VERSION_*` are statically bound into the compiled target app binaries.
    That way, the target app is compatible only with specific versions of Remix Renderer binaries (`d3d9.dll`). 
    
    * The Renderer binaries loosely follows the [Semantic Versioning](https://semver.org/), with the main difference on the development `0.y.z` versions:
    `0.a.b` and `0.c.d` are not compatible if `a != c`.

    * If a target app is not compatible with a given Remix Renderer binaries, initialization will return `REMIXAPI_ERROR_CODE_INCOMPATIBLE_VERSION` error code.

    </details>

    <br>

1. To start the Remix Renderer, call `remixapi_Interface::Startup` providing a filled `remixapi_StartupInfo` struct.
    * Note: an extra care is needed for `.sType` member in all of the Remix API structs, as `.sType` value is used internally to determine a type of a pointer.

---

### Resources

The main resource types in the Remix API are: mesh, material, light.
Each needs to be registered to be used in the rendering of a frame.

Mesh:
* To register, call `remixapi_Interface::CreateMesh`
* A mesh (`remixapi_MeshInfo`) consists of a set of surfaces (`remixapi_MeshInfoSurfaceTriangles`)
    * Each surface is a set of triangles, defined by vertex/index buffer
* Each surface can reference a material (i.e. a mesh can consist of different materials)
* *Note: at the time of writing, the structure of a Remix API vertex (`remixapi_HardcodedVertex`) is defined statically, without an option to define the element offsets and types (position, normal, etc). A subject to change.*

Material:
* To register, call `remixapi_Interface::CreateMaterial` specifying `remixapi_MaterialInfo`, but `.pNext` must be a pointer to one of:
    * `remixapi_MaterialInfoOpaqueEXT` -- for a generic material
    * `remixapi_MaterialInfoTranslucentEXT` -- for a glass material
* For the default values, corresponding default constructors can be examined in the C++ wrapper [remix.h](../../../public/include/remix/remix.h)
* *Note: at the time of writing, the material API is still not refined to work with non-file image data, and overall structure just reflects the internal representation of materials, which might be not as simple to use. The primary subject to change.*

Light:
* To register, call `remixapi_Interface::CreateLight` specifying `remixapi_LightInfo`, but `.pNext` must be a pointer to one of:
    * `remixapi_LightInfoSphereEXT` -- sphere light; or spot light if a light shaping is configured
    * `remixapi_LightInfoRectEXT` -- rect
    * `remixapi_LightInfoDiskEXT` -- disk
    * `remixapi_LightInfoCylinderEXT` -- cylinder
    * `remixapi_LightInfoDistantEXT` -- distant (sun)
    * `remixapi_LightInfoDomeEXT` - dome (sky)
* `.hash` should be a unique ID, it is used to identify the light source between the current and previous frame for a more stable denoising / light sampling
* Since lights are usually changing a lot (they move, their intensity varies, etc), to update a light with the new parameters, recreate it by calling `remixapi_Interface::DestroyLight` + `remixapi_Interface::CreateLight` but with the same `.hash` value as the old one

---

### In Each Frame

Push a camera, mesh instances and lights to define a scene for the *current* frame.

* Call `remixapi_Interface::SetupCamera`
    * Either specify view / projection matrices in `remixapi_CameraInfo`
    * Or specify parameters in `remixapi_CameraInfoParameterizedEXT`, and link the struct to `remixapi_CameraInfo::pNext`, so Renderer would calculate matrices internally

* Call `remixapi_Interface::DrawInstance` to push a mesh instance with a corresponding transform. There can be many instances that reference a single `remixapi_MeshHandle` (instancing).

* Call `remixapi_Interface::DrawLightInstance` to push a light to the scene.

* Call `remixapi_Interface::Present` to render a frame and present to the window

*Note: to set `rtx.conf` options at runtime, use `remixapi_Interface::SetConfigVariable`*

*Note: [remixapi_example_c.c](RemixAPI_C/remixapi_example_c.c) contains all the steps listed above, and should draw a triangle.*



<br>

# Remix API in the existing Direct3D 9 apps

While the given example is D3D9-agnostic, the API can be used interchangeably with D3D9. 
The main constraint is that Remix API is 64-bit only, which means that the game also must be built for 64-bit.

So given an existing 64-bit D3D9 application, here are the steps to integrate Remix API:

* Ensure that the app works with the original Remix Runtime
* In the D3D9 app's source files, there will be `IDirect3D9::CreateDevice` call or similar, so after that:

    1. Call `remixapi_lib_loadRemixDllAndInitialize(L"d3d9.dll")` to init the Remix API
    1. Call `remixapi_Interface::dxvk_RegisterD3D9Device`, passing the D3D9 device
        * Make a static_cast to `IDirect3DDevice9Ex*`, if needed

After that, Remix API calls can be used.

For example:
`g_remix.SetConfigVariable("rtx.fallbackLightMode", timer() ? "2" : "0");`
to turn on/off the fallback light programmatically.



# Extensibility

Remix API is designed to be extensible and backward compatible.

To add new struct types, add a type and corresponding entry to `remixapi_StructType` enum. Then follow the instructions in `src/dxvk/rtx_render/rtx_remix_specialization.inl`. Add handling code to the `src/dxvk/rtx_render/rtx_remix_api.cpp` to tie the API with an internal implementation. If a change is not compatible with previous versions, increase `REMIXAPI_VERSION_MAJOR`; or other macros, according to versioning.

If adding a function, append the entry to `remixapi_Interface`, but specififcally at the end of that struct, to preserve the compatibility with previous versions.



<br>
<br>
<br>

---

Note: Remix API was originally designed for the USD Hydra Delegate in the Remix Toolkit renderer, so it has been tested mostly for that specific use case.
