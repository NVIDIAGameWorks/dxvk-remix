# RTX Remix Shader Overview

This document provides an overview for the general structure, standards and conventions in the shader codebase for the RTX Remix project. Note that originally this codebase used GLSL primarily but has since switched to [Slang](https://github.com/shader-slang/slang) which provides back-compatibility in terms of syntax. In the future more of the codebase will be written to leverage Slang syntax, but much of it still is designed around its GLSL origins for now.

## File Conventions

### Naming

Files and folders should use `lower_snake_case` for their names and generally should be named the same as the primary type they contain or functionality they provide. For example a file named `surface_material.slangh` should contain the implementation of the `SurfaceMaterial` type.

For files which contain various shared helper functions related to another file or set of files, the `_helpers` suffix is also common. Similarly `_bindings` and `_binding_indices` are also common suffixes in pass-related files for holding information about bindings, and `_args` for constant buffer parameter structures. Generally patterns like these should be followed when apparent for consistency.

### Extensions

The following extensions are used in most of the project for general purpose files declaring/defining types and functions in an organized manner.

- `*.h` - Primary structure definitions due to the one pass compilation model causing issues when structures are defined after includes within a file. Only to be used for big structures shared across files, not tiny internal or helper structures only used within a file. Shared with CPU occasionally for memory and code consistency. Note these headers should only be included in their respective `*.slangh` file (with some exceptions for bindings/macro definition files, or aforementioned CPU-shared helpers). Additionally, other files do not need to include these and should only include the respective `*.slangh` file for the desired types and functionality provided by the implementation.
- `*.slangh` - Implementations of struct "member" functions or other helper types and functions. These files are what should be included into others to bring in the functionality and types they offer.

The remainder of the extensions used on the other hand are used only for "pass" implementations, other functionality should be split into `.slangh` files. Additionally, note that the extensions in these cases are important for how the shader is compiled as otherwise indicating which shader stage to compile for would have to be done in another way.

- `*.slang` - Generic shader files, typically used when multiple types of shader stages will be generated via permutations (e.g. compute for ray query implementations and ray generation for trace ray implementations in the same file).
- `*.comp.slang` - Compute shader files.
- `*.rgen.slang` - Ray generation shader files.
- `*.rchit.slang` - Ray closest hit shader files.
- `*.rahit.slang` - Ray any hit shader files.
- `*.rmiss.slang` - Ray miss shader files.

### Organization

All RTX Remix shaders are to be put in the `rtx` shader folder, with the following subfolders utilized as applicable based on the Architecture section below:

- `external` - External pre-existing frameworks or helper files, typically with a differing license from the rest of RTX Remix.
- `utility` - General helper functionality for common operations. Examples include packing, texture sampling, random number generation and math.
- `concept` - Concept files for major organizational units in the codebase. Examples include surfaces, materials, lights and rays.
- `algorithm` - Algorithm files for major rendering logic.
- `pass` - Shader files for entry points into various rendering passes.

### Structure

Generally a file's structure should follow these guidelines, though they are not a strict requirement as some files may have unique requirements which may break from this pattern. Comments can also be used in larger files to help break up sections of the file into logical organizational parts. For example, breaking up a `*.slangh` file that implements member functions for multiple structures can group each set of functions and separate them with a comment indicating which section applies to which type.

#### `*.h` Files

```glsl
// [License]

#pragma once

// Constants

static const uint exampleConstant = 0u;

// Structures

struct Example
{
  uint foo;
};
```

#### `*.slangh` Files

```glsl
// [License]

#pragma once

// Includes

// Note: Respective *.h file must be first include to define required structures before including helpers
#include "rtx/concept/example.h"
#include "rtx/utility/helper.slangh"

// Helper Structs and Helper/Member Functions

struct InputData
{
  uint bar;
};

Example exampleCreate(InputStruct inputData)
{
  Example example;

  example.foo = helperFunc(inputData.bar);

  return example;
}
```

#### `*.slang` Files

```glsl
// [License]

// Special Variant Comments (If present)

//!variant foo.comp.slang
//!>       SOME_DEFINE
//!>       ANOTHER_DEFINE

//!end-variants

// Includes

// Note: It is common to put defines in pass files like this to define behavior which should
// apply to everything included below (similar to how the variant defines also are used for this).
#define ENABLE_SOME_FEATURE 1

// Note: Binding files must be included first to provide bindings for subsequent includes
#include "rtx/pass/foo/binding_indices.h"
#include "rtx/pass/foo/bindings.h"
#include "rtx/concept/example.slangh"

// Additional Buffers (Anything not included in the bindings if needed)

// Note: Format should match what is defined on the CPU side.
layout(r16ui, binding = SOME_BINDING_INPUT)
RWTexture2D<uint> SomeBinding;

// Compute Implementation

[shader("compute")]
[numthreads(16, 8, 1)]
void main(uvec2 threadID : SV_DispatchThreadID)
{
  // Implementation
}
```

## Programming Conventions

### Style

- 2 space indentation (Not 2 space wide tabs).
- lowerCamelCase for constant, function, parameter and variable names.
- UpperCamelCase for structure names and buffers.
- UPPER\_SNAKE\_CASE for macro names.
- Next-line braces for functions, conditionals, loops, etc.
- Additionally, braces should always be added, even when optional (no one-line conditionals/loops).
- New lines should be used to separate code for better readability, for example after a block or after a comment or other significant logical code section.
- Spaces should surround binary operators on both sides, but should not be added around parentheses (be it in an expression, function call or otherwise).
- Delimiter commas (function calls or parameter lists) or semicolons (in for loops) should be followed by a space (or a new line if needed to split across lines).
- Lines should be kept at a reasonable length (100-120 ish characters max typically). Excessively long lines should be split up with newlines.
- Splitting across lines should be done ideally at delimiters or other logical points, for example after each item in a function call, after a bitwise or boolean and/or in a chain of expressions, or even at "important" binary math operators.
- An empty space should be between if/for/while/switch and (.
- Header files should be guarded with #pragma once.
A combined example of these stylistic choices in use is as follows:

```glsl
#pragma once

struct Foo
{
  uint fooBarBaz;

  uint16_t anotherFoo;
  float16_t anotherBaz;
};

// An example function, takes in a Foo object `foo` and a boolean `test` to
// do whatever with.
vec3 exampleHelper(Foo foo, bool test, inout bool specialFlag)
{
  // Return early if test is set

  if (test)
  {
    return vec3(0.0f, 0.0f, 0.0f);
  }

  // Do some calculations conditionally

  int loopCounter = 2;
  while (loopCounter-- > 0)
  {
    if (foo.anotherFoo > someConstant)
    {
      switch (foo.anotherFoo)
      {
      // Note: Fallthrough to Apple type.
      default:
      case anotherFooTypeApple:
        someExcessivelyLongFunctionNameForDemonstrationPurposes(
          foo.anotherBaz + 1.0h,
          foo.anotherBaz / 2.0h,
          foo.anotherBaz * 5.0h);

        break;
      case anotherFooTypeOrange:
        if (
          (foo.fooBarBaz == 26 && foo.anotherBaz == 2.0h) ||
          foo.fooBarBaz == 5
        )
        {
          const uint someScalar = uint(foo.anotherFoo) * foo.fooBarBaz;

          specialFlag = true;

          return vec3(float(someScalar));
        }

        break;
      }
    }
  }

  // Perform the sum operation

  float counter = 0.0f;

  for (uint i = 0u; i < foo.fooBarBaz; ++i)
  {
    counter += float(i) + 2.0f;
  }

  return vec3(counter, counter, counter);
}
```

### Naming

#### Structures

Structures should be named as some sort of noun describing what it is. An example is `Camera`, a type which contains data pertaining to a scene's camera.

#### Functions

Functions take two forms generally, "member" functions (conceptually part of a structure), and helper or other general functions. Below are a few common conventions:

- Member functions should take the naming form `[type][Function Name]`, for example `cameraSetMatrix` for a function which sets the matrix of a `Camera` object.
- Constuctors are a special kind of member function which always take the form `[type]Create*` and return the type they are constructing, for example `cameraCreate` to construct a `Camera` object. Note there may be more than one constructor in which case additional information can be provided, such as `rayCreateDirection` or `rayCreatePosition`.
- Functions acting as members which should not be called externally should be suffixed with `*Internal`, for example `rayCreateInternal`.
- These general "member" function conventions may change in the future when Slang's member function syntax is used more commonly.
- General additional function conventions are `calc*` or `eval*` for computing some quantity, or `*Helper` for a helper member function.

#### Enums

"Enum" value constants should contain their respective type name for clarity of which enum they belong to in the scheme `[type][Enum Value]`. For example `lightTypeSphere` for the `Sphere` value in the hypothetical enum type `LightType`. This may change once Slang enums are properly integrated into the codebase.

### Types

- Currently GLSL types are preferred over the HLSL types which also work in Slang, for example `vec3` should be used over `float3`. If a file is already predominantly using HLSL types however it is fine to keep consistent in such cases. This may change in the future if Slang is fully adopted into the codebase.
- Use "default" GLSL types where applicable, such as `vec3` or `uvec3` instead of `f32vec3` and `u32vec3` (as they mean the same thing).
- Prefer 16 bit types where possible such as `float16_t`, `uint16_t`, `f16vec3` or `u16vec3` as these will help reduce register pressure and increase compute performance.
- 16 bit floating point types should be used for values with small ranges such as normalized values (0 to 1, -1 to 1, etc) or direction/normal vectors. Note the maximum 16 bit float value is 65504 and the smallest non-denormalized value is around 0.00006, so values that will never exceed these orders of magnitude are generally fine, as long as the quantization from the reduced precision is acceptable (generally is for something like a color or a direction).
- 32 bit floating point types should be used for values with large ranges such as positions, distances, or anything that requires a high degree of precision.
- Never use 64 bit types, such operations are extremely slow on current consumer GPU hardware.

### Misc

- Comment code as needed, though typically comments starting with `// Note:` or `// Todo:` can help highlight a specific choice, clarify confusing code or mark something that needs to be done still. Additionally, placing a comment above a function to explain its usage or expectations for parameters, or simply commenting various logical sections of code is similarly useful. Remember that code will likely be read by someone else (or even yourself) in the future, so good commenting can reduce how long it takes to understand a section of code.
- Include paths should ideally be absolute paths from the root folder (rtx) always to avoid usage of relative paths. For example `#include "rtx/concepts/example.slangh"` instead of `#include "example.slangh"`.
- Use `inout` when needed, but do not use `in` to stay consistent with using default language behavior when available.
- Avoid usage of `out` parameters due to their ability to cause easy to miss undefined behavior when not written to within a function (e.g. when complex control flow is involved). This rule may be changed in the future once Slang's compiler can properly warn about incorrect use of out parameters.
- Use `const` where possible to prevent accidental modification of variables that are not meant to be changed. Additionally, use `static const` variables as constants where possible instead of defines as these will have better type checking (the static is required when defining a global constant otherwise Slang interprets it as something else).
- Use a shared definition between host and a shader exposed via respective `*.h` files for any shared types or constants such as constant buffers, bindings, enums, etc. See the section on file types for more info.
- Avoid nesting scopes excessively when it can be avoided. Often various returns/continues/breaks in a function/loop can be modeled with a guard clause model, reducing the indentation depth of the rest of the code. See [here](https://en.wikibooks.org/wiki/Computer_Programming/Coding_Style/Minimize_nesting#Early_return) for more info.
- Try to use literals that match the type of whatever they are being assigned to to minimize the implicit conversions going on. `0` is a signed integer, `0u` is an unsigned integer, `0.0f` is a 32 bit float and `0.0h` is a 16 bit float.

## Architecture

### Concepts

Concepts are core types which represent general ideas for the renderer. They typically contain most of the actual information about what should be rendered as well as the functionality to implement rendering algorithms easily. They may additionally be polymorphic or specific to a polymorphic type. A list of the current concepts is as follows:

- Camera - Camera-related functionality such as its position and orientation for current and previous frames.
- Ray/Ray Interaction - Ray-related functionality for both spawning and manipulating as well as their raw interactions from ray queries.
- Ray Portal - Transformations and other information for handling ray portal teleportation.
- Surface/Surface Interaction - Information about scene geometry beyond just that which the hardware stores, as well calculated interaction information at a given hit point such as normals, texture coordinates and gradients, and motion vectors.
- Surface Material/Surface Material Interaction - Information about materials applied to scene surfaces, as well as calculated interaction information at a given hit point and functions to sample or evaluate the BSDF representing the material. Typically polymorphic.
- Volume/Volume Interaction - Unused currently.
- Volume Material/Volume Material Interaction - Unused currently.
- Light - Information about lights in the scene containing functions to sample or evaluate the light. Typically polymorphic.

### Algorithm

Algorithms are generally functions which implement commonly used algorithms for rendering, such as various integrators for computing radiance in the scene. A list of some of the current algorithms is as follows:

- Resolver - Handles logic which occurs on the majority of typical ray casts such as particles (separate unordered approximations), ray portals, decals, and creation of various hit structures.
- Geometry Resolver - Handles various "primary ray" logic to resolve the G-Buffer(s). Includes viewmodel code, [PSR](https://developer.nvidia.com/blog/rendering-perfect-reflections-and-refractions-in-path-traced-games/) and primary/secondary G-Buffer creation.
- Integrator - Direct/Indirect integration path tracing logic, handles the evaluation of direct lighting (initial hit next event estimation), as well as indirect lighting (material sampling, path continuation, further next event estimation).
- Volume Integrator - Froxel radiance cache integration logic, handles next event estimation on froxel grid cells.

### Pass

Passes are actual ray tracing or compute passes which invoke algorithms or perform other functions and make up the rendering pipeline. Typically they also are responsible for the various inputs and outputs a compute shader may be responsible to read or write data to. A list of the current passes in general order of execution is as follows:

- Froxel Radiance Cache (Volumetrics) - Handles lighting and filtering of the froxel radiance cache for usage in various volume-based lighting techniques. Invokes the volume integrator.
- G-Buffer - Primarily invokes the geometry resolver, see the Algorithm second for more details.
- RTXDI - Handles direct lighting ReSTIR preparation via the RTXDI SDK.
- Integration - Primarily invokes the integrator, see the Algorithm section for more details.
- RTXDI Confidence - Computes RTXDI confidence and temporal gradients for usage in denoising.
- ReSTIR GI - Handles ReSTIR GI evaluation and final shading.
- Demodulate - Demodulates the rendered signals when denoising is enabled to preserve various noise-free aspects of detail across the denoising process.
- (Denoise) - Denoising if enabled will implicitly be inserted at this stage (not an actual shader pass on Remix's side).
- Composite - Handles remodulation of denoised signals and compositing together into a single image.
- (Upscaling) - Upscaling if enabled will implicitly be inserted at this stage (not an actual shader pass on Remix's side except in the case of TAA).
- Bloom - Applies a post processing bloom effect.
- Post FX - Applies various additional post processing such as motion blur, vignetting and chromatic aberration.
- Tonemapping - Preforms tonemapping of the fully rendered image to convert from the linear HDR space all rendering is done in to the desired output space.

Other passes which execute outside the main rendering pipeline are as follows:

- Opacity Micromap Generation - Handles dynamic generation of opacity micromap information for use in rendering.
- View Model Correction - Applies a correction transformation to view model vertices, specifically meant to assist in how viewmodels are rendered in Remix.
- Triangle List Generation - Handles generation of triangle lists from topologies such as triangle strips for usage in ray tracing (as BLAS generation only supports triangle lists currently).
- GPU Skinning - Applies bone-based skinning to vertices for handling specific types of animated objects.
- Geometry Interleaving - Handles interleaving of separate buffers (for instance separate vertex and normal buffers) into a singular buffer to match Remix's expectations.