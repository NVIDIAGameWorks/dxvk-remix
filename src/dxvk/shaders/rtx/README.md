# Classic RTX Overview

This document provides an overview for the general standards in the Classic RTX project (on the shader side, at least).

## File Conventions

### Naming

Files and folders should use `lower_snake_case` for their names and generally should be named the same as the primary type they contain or functionality they provide. For example a file named `surface_material.glsli` should contain the implementation of the `SurfaceMaterial` type.

### Extensions

- `*.h` - Primary structure definitions due to GLSL's one pass compilation model causing issues when structures are defined after includes within a file. Only to be used for big structures shared across files, not tiny internal or helper structures only used within a file. Shared with CPU occasionally for memory and code consistency. Note these headers should only be included in their respective `*.glsli` file (with some exceptions for bindings), other files do not need to include these and should only include the `*.glsli` file for the desired types and functionality.
- `*.glsli` - Implementations of struct "member" functions or other helper functions. These files are what should be included into others to bring in the functionality and types they offer.
- `*.comp` - Compute shader files, only to be used as passes, other functionality should be split out into `*.glsli` files. Note the `comp` extension is used to indicate to the GLSL compiler that the shader stage is a compute shader as otherwise this would have to be specified in other ways.

### Organization

All Classic RTX shaders are to be put in the `rtx` shader folder, with the following subfolders utilized as applicable based on the Architecture section below:

- `utility` - General helper functionality for common operations. Examples include packing, texture sampling, random number generation and math.
- `concept` - Concept files.
- `algorithm` - Algorithm files.
- `pass` - Pass files.

### Structure

Generally a file's structure should follow these guidelines, though they are not a strict requirement as some files may have unique requirements which may break from this pattern. Comments can also be used in larger files to help break up sections of the file into logical organizational parts. For example, breaking up a `*.glsli` file that implements member functions for multiple structures can group each set of functions and separate them with a comment indicating which section applies to which type.

#### `*.h` Files

```glsl
// Include Guard Start

// Note: *_H naming to not conflict with other include guards
#ifndef EXAMPLE_H
#define EXAMPLE_H

// Constants

const uint exampleConstant = 0u;

// Structures

struct Example {
  uint foo;
};

// Include Guard End

#endif
```

#### `*.glsli` Files

```glsl
// Include Guard Start

// Note: *_GLSLI naming to not conflict with other include guards
#ifndef EXAMPLE_GLSLI
#define EXAMPLE_GLSLI

// Includes

// Note: Respective *.h file must be first include to define required structures before including helpers
#include "rtx/concept/example.h"
#include "rtx/utility/helper.glsli"

// Helper Structs and Helper/Member Functions

struct InputData {
  uint bar;
};

Example exampleCreate(InputStruct inputData) {
  Example example;

  example.foo = helperFunc(inputData.bar);

  return example;
}

// Include Guard End

#endif
```

#### `*.comp` Files

```glsl
// GLSL Version Indicator

#version 460

// Extensions
// Note: These extensions should cover the needs of all included files as well

#extension GL_GOOGLE_include_directive : enable

// Includes

// Note: Bindings files must be included first to provide bindings for subsequent includes
#include "rtx/bindings.h"
#include "rtx/concept/example.glsli"

// Layouts/Buffers/Uniforms

layout(local_size_x = 16, local_size_y = 8, local_size_z = 1) in;

// Compute Implementation

void main()  {
  // Stuff
}
```

## Programming Conventions

### Style

- 2 space indentation
- Same-line braces for functions, conditionals, loops, etc
- lowerCamelCase for constant, function, parameter and variable names
- UpperCamelCase for structure names
- UPPER\_SNAKE\_CASE for macro names
- Various specifics on spacing (see example below)

An example of these stylistic choices in use is as follows:

```glsl
struct Foo {
  uint fooBarBaz;
};

vec3 exampleHelper(Foo foo, bool test) {
  // Return early if test is set
  
  if (test) {
    return vec3(0.0f, 0.0f, 0.0f);
  }
  
  // Perform the operation
  
  float counter = 0.0f;
  
  for (uint i = 0u; i < foo.fooBarBaz; ++i) {
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
- General additional function conventions are `calc*` or `eval*` for computing some quantity, or `*Helper` for a helper member function.

#### Enums

"Enum" value constants should contain their respective type name for clarity of which enum they belong to in the scheme `[type][Enum Value]`. For example `lightTypeSphere` for the `Sphere` value in the hypothetical enum type `LightType`.

### Types

- Use "default" GLSL types where applicable, such as `vec3` or `uvec3` instead of `f32vec3` and `u32vec3` (as they mean the same thing).
- Prefer 16 bit types where possible such as `float16_t`, `uint16_t`, `f16vec3` or `u16vec3` as these will help reduce register pressure and increase compute performance.
- 16 bit floating point types should be used for values with small ranges such as normalized values (0 to 1, -1 to 1, etc) or direction/normal vectors. Note the maximum 16 bit float value is 65504 and the smallest non-denormalized value is around 0.00006, so values that will never exceed these orders of magnitude are generally fine, as long as the quantization from the reduced precision is acceptable (generally is for something like a color or a direction).
- 32 bit floating point types should be used for values with large ranges such as positions, distances, or anything that requires a high degree of precision.

### Misc

- Include paths should ideally be absolute paths from the root folder (rtx) always to avoid usage of relative paths. For example `#include "rtx/concepts/example.glsli"` instead of `#include "example.glsli"`.
- Use `inout` when needed, but do not use `in` to stay consistent with using default language behavior when available.
- Avoid usage of `out` parameters due to their ability to cause easy to miss undefined behavior when not written to within a function (e.g. when complex control flow is involved).
- Use `const` where possible to prevent accidental modification of variables that are not meant to be changed.
- Comment code as needed, though typically comments starting with `// Note:` or `// Todo:` can help highlight a specific choice, clarify confusing code or mark something that needs to be done still.
- Use a shared definition between host and a shader exposed via respective `*.h` files for any shared types or constants such as constant buffers, bindings, enums, etc. See the section on file types for more info.

## Architecture

### Concepts

Concepts are core types which represent general ideas for the renderer. They typically contain most of the actual information about what should be rendered as well as the functionality to implement rendering algorithms easily. They may additionally be polymorphic or specific to a polymorphic type. A list of the current concepts is as follows:

- Camera
- Ray/Ray Interaction
- Surface/Surface Interaction
- Surface Material/Surface Material Interaction
- Volume/Volume Interaction
- Volume Material/Volume Material Interaction
- Light
- Path

### Algorithm

Algorithms are generally functions which implement commonly used algorithms for rendering, such as various integrators for computing radiance in the scene.

### Pass

Passes are actual compute passes which invoke algorithms or perform other functions and make up the rendering pipeline. Typically they also are responsible for the various inputs and outputs a compute shader may be responsible to read or write data to.
