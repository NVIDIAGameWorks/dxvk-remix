# C++ Coding Style Guide

Welcome to dxvk-remix! We appreciate your interest in contributing. This document outlines our project's C++ code formatting standards, commenting styles, memory management practices, and other essential guidelines. Adhering to these will help maintain the quality and consistency of our codebase.  For shader code guidelines and information, please refer to [this](src/dxvk/shaders/rtx/README.md) specific document.

## Code Formatting Guidelines

### Indentation and Spacing

- **Indentation**: Use 2 spaces for indentation, not tabs.
- **Spaces**:
  - Include a space after control statements like `if`, `for`, `while`, etc.
  - Use spaces around operators (`=`, `+`, `-`, `*`, `/`) and after commas.

  ```cpp
  if (x == 5) {
    x += 1;
  }
  ```
  

### Naming Conventions

- **Member Variables**: Prefix member variables with `m_`.
- **Pointers**: Prefix pointers with `p`.
- **Variables and Namespaces**: and functions: camelCase
- **Constants and macros**: UPPER_CASE
- **Class and struct names**: PascalCase

  ```cpp
  class Example {
    int m_value;     // Member variable
    int* m_pPointer;  // Pointer to an integer
    const int MAX_ITER = 1;
    void someFunc(const int* pInput) {
      int currentIter = 0;
      // do something
    }
  };
  ```

### Braces `{}` Usage

- **Functions, Classes, Control Structures, Namespaces**: Place the opening brace on the same line.

  ```cpp
  void function() {
    // function body
  }

  if (condition) {
    // if body
  } else {
    // else body
  }

  class ClassName {
    // class body
  };

  namespace nameSpace {
    // namespace content
  }
  ```

### Headers and Includes

- **Ordering**: Standard libraries first, followed by third-party libraries, and then local project headers.
- **Separation**: Separate each group with a blank line.

  ```cpp
  #include <vector>
  #include <string>

  #include "third_party/library.h"

  #include "local/header.h"
  ```

### Macro Definitions

- **Location**: Place macro definitions at the top of the file, after `#pragma once`.

  ```cpp
  #pragma once

  #define PI 3.14159
  ```

## Commenting Styles

- **Inline Comments**: Use `//` for short, explanatory comments at the end of a line.

  ```cpp
  int x = 5; // Initialize x to 5
  ```

- **Above-line Comments**: Place descriptive comments above the code block they describe.

  ```cpp
  // Calculate the square of x
  int square(int num) {
    return num * num;
  }
  ```

- **Block Comments**: Use `/* */` for detailed explanations or temporarily commenting out code.

  ```cpp
  /*
  This function calculates the square of a number.
  It is used in the context of energy calculations.
  */
  int square(int num) {
    return num * num;
  }
  ```

## Memory Allocation

- **Smart Pointers**: Use smart pointers for managing dynamic memory.

  ```cpp
  std::unique_ptr<int> ptr = std::make_unique<int>(10);
  std::shared_ptr<int> ptr = std::make_shared<int>(10);
  ```
  
- **Ref Counted Smart Pointers for GPU resources**: Use ref counted pointers for all GPU allocations

  ```cpp
  Rc<DxvkImage> pImage = new DxvkImage(...);
  ```
  
- **STL Containers**: Utilize STL containers which manage memory automatically for dynamic arrays.

  ```cpp
  std::vector<int> numbers;
  numbers.push_back(1);
  numbers.push_back(2);
  ```
  
- **Last Resort**: Use primitive memory management functions.
  ```cpp
  uint8_t* pData = new uint8_t[4];
  delete [] pData;
  ```

## Profiling

- **CPU Profiling Macros**: Use macros to measure the performance of critical sections.

  ```cpp
  void someExpensiveFunction() {
    ScopedCpuProfileZone(); // Automatically profiles the duration of someExpensiveFunction()
    // process data here
  }
  ```
  
- **GPU Profiling Macros**: Use macros to measure the performance of *all* GPU workloads

  ```cpp
  void someExpensiveGpuWork() {
    ScopedGpuProfileZone(ctx, "someExpensiveGpuWork"); // Automatically profiles the duration of someExpensiveGpuWork on the GPU
    dispatchComputeShader();
  }
  ```
  
## Changes to DXVK

- **Comment blocks**: Follow the pattern of comment blocking to define regions of code inside core DXVK files which are diverging from upstream DXVK.

  ```cpp
  // NV-DXVK start: Diverging DXVK change to add new function
  void newDxvkFunctionality() {
    // code change here
  }
  // NV-DXVK end
  ```

## RTX Options
- **Regenerate Markdown**: Run RTX Remix app with `DXVK_DOCUMENTATION_WRITE_RTX_OPTIONS_MD=1`. The RtxOptions.md will be generated in the working directory.
- **Adding New Options**: Add new rtx options within respective feature class, rather than piling them in rtx_options.h
- **Categorize Options**: Add category to the string name: i.e. antiCulling -> `RTX_OPTION("rtx.antiCulling", float, fovScale, "It does ....")`. This makes it easier to see what the setting corresponds to as well as it keeps the members of a category enumerated together in .md doc since it's alphabetically sorted.
- **Example**: Full example of dedicated struct to make string and in-code references match
  ```cpp
  inline static struct BakedTerrain {
    friend class ImGUI; // <-- we want to modify these values directly.

    RTX_OPTION("rtx.bakedTerrain", bool, enable, true, "Enables runtime terrain baking.");

    static struct Cascade {
      friend class ImGUI; // <-- we want to modify these values directly.

      RTX_OPTION("rtx.bakedTerrain.cascade", float, width, 32.f, "Width of a cascade plane [game units].");
      RTX_OPTION("rtx.bakedTerrain.cascade", uint32_t, levels, 1, "Number of cascade levels.");
    } cascade;
  } bakedTerrain;
  ```

- Consider adding more explanation to what XYZ is instead of just  "Enables XYZ"  for `RTX_OPTION("rtx.xyz", bool, enable, false, "Enables XYZ.");`  There's still value in "Enables XYZ" in the .md doc, but it could be more helpful. Not everyone knows what XYZ is.
- It's more clear to read "Enables XYZ" than "Enable XYZ" for a description of a toggle/checkbox.
- Use `RTX_ENV_VAR` instead of `DXVK_ENV_VAR` for env var strings relating to RTX.
- Enumerate enums with their value and any string they are called out with in the GUI in description as applicable, i.e. `0: First Option, 1: Second Option, ...`
- The value is helpful for values written in .md or in rtx.conf, since the string won't be read/wrote there
- Be cautious of string formatting within descriptions so that it's compatible with both tooltips and .md output
- Keep descriptions concise (since they appear as tooltips) but informative

## Pull Requests

- **Small, Focused Commits**: Each commit should represent a logically separate change.
- **Pull Request Descriptions**: Provide context about what the changes do and why they should be made.
- **Code Reviews**: Participate in code reviews to catch issues early and ensure quality.

## Testing

- **Unit Tests**: Add unit tests for new core functionality.


By following these guidelines, we ensure that our codebase remains clean, efficient, and easy to maintain. Thank you for contributing to our project!
