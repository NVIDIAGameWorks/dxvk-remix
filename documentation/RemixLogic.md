# Remix Logic

When creating content for remix mods, modders often want to make the replacement content react to the state of the game. The Remix Logic system provides a flexible way to sense what's happening in the game, make decisions based on that information, and then apply visual changes accordingly.

Remix Logic is built from two simple building blocks:
- **Components** are individual functions that take inputs, process them, and produce outputs (think of them as single LEGO blocks)
- **Graphs** connect multiple Components together, where the output of one Component feeds into the input of another (like snapping LEGO blocks together to build something complex)

By connecting Components in different ways, you can create sophisticated behaviors that respond dynamically to what's happening in the game.

**Important:** Components must be connected in a way that has a clear starting point and doesn't create loops - each Component can only use information from Components that come before it in the chain.

# Creating Graphs

This page is primarily intended for people who wish to create their own custom components in c++. If this is your first introduction to Remix Logic, you should probably see the [Remix Toolkit documentation](https://docs.omniverse.nvidia.com/kit/docs/rtx_remix/latest/docs/howto/learning-logic.html) for a high level overview and graph creation tutorials.

# Component List

Documentation for each component is available here: [Component List](components/index.md)

# Types of Components

There are four major categories of components:

## Sense Components

These components generate output values by examining the renderable state, or other aspects of Remix's current state. Examples include:
* Output the number of seconds since the graph was created
* Output how many times the game used a specific texture this frame
* Output the current camera position
* Determine if a given key combination is currently pressed
* Read a global value stored by another graph

## Transform Components

These components allow data that is already in the graph to be transformed.

 Examples include:
* Add two values together
* Check if a number is smaller than another number
* Combine three numbers into an RGB Color
* Calculate the angle between a ray (i.e. "Camera Forward") and a point in space (i.e. the center of a mesh)
* Map from an input range (i.e. 0 to 1) to an output range (i.e. 90 to 180).

## Action Components

These components alter the renderable state.  

 Examples include:
* Alter the Remix configuration by applying an RtxOptionLayer (and animating its blend strength)
* Store a global value so another graph can read it.
* Change the brightness of a light.
* Change the scale of a mesh.
* Change the roughness of a material
* Swap which replacements are applied to a given mesh hash
* Alter the time multiplier (pausing or slowing all animations)

## Const Components

These components simply output a single constant value of a given type.  This can be useful for sharing a value between multiple components, but primarily exist as a way to set properties with a flexible type.

# Component Data Types

The component system supports the following data types as defined in `rtx_graph_types.h`:

| Type | C++ Type | Description | Example Values |
|------|----------|-------------|----------------|
| `Bool` | `uint32_t` | True or False boolean values (stored as uint32_t for variant compatibility) | `true`, `false` |
| `Float` | `float` | A number, including decimal places. Single precision floating point | `1.0`, `-3.14` |
| `Float2` | `Vector2` | 2D vector of floats | `Vector2(1.0f, 2.0f)` |
| `Float3` | `Vector3` | 3D vector of floats | `Vector3(1.0f, 2.0f, 3.0f)` |
| `Float4` | `Vector4` | 4D vector of floats, often used for colors | `Vector4(1.0f, 2.0f, 3.0f, 0.0f)` |
| `Enum` | `uint32_t` | A selection from a limited list of options | `InterpolationType::Linear`, `LoopingType::Loop` |
| `String` | `std::string` | Some text | `"hello"`, `"world"` |
| `AssetPath` | `std::string` | Path to an asset file | `"textures/myfile.dds"` |
| `Hash` | `uint64_t` | 64-bit hash value | `0x123456789ABCDEF0` |
| `Prim` | `PrimTarget` | Identifies another object - a mesh, light, material, particle effect, graph, etc. | `</RootNode/meshes/mesh_123456789ABCDEF0/mesh>` |
| `NumberOrVector` | `std::variant<float, Vector2, Vector3, Vector4>` | Flexible numeric or vector type (resolved at load time) | `1.0f`, `Vector2(1.0f, 2.0f)` |
| `Any` | `std::variant<...>` | Flexible type that supports all non-flexible types | `1.0f`, `true`, `"textures/myfile.dds"` |

Components with flexible types may not accept all combinations of those types - i.e. you cannot check if a Float is greater than a Vector3.  The Toolkit may not allow you to set a flexible property directly on the node - you'll instead need to create a Const component of the appropriate type, and connect that to the flexible property.  This is to ensure safe type resolution.

# Creating Components

Before creating a new component, check the list of existing components to see if one already exists that matches your needs:
[Component List](components/index.md)

Components are created in C++, using macros. The [TestComponent](https://github.com/NVIDIAGameWorks/dxvk-remix/blob/main/tests/rtx/unit/graph/test_component.h) is a good example component using every data type.

Defining a component has three mandatory pieces: the parameter lists, the component definition, and the `updateRange` function.

## Creating Components with AI

The individual component files have been kept intentionally small.  If you provide this RemixLogic.md as context to a coding AI, it should be able to create simple sensor or logic components relatively easily.

## Defining Parameters

Parameters are how components accept Input, store internal State, and expose Output. Each of these are defined in separate lists using macros.

### Input Parameters
Inputs can be set to a constant value or connected to another component's Output. They should be read-only during a component's update function.

```cpp
#define LIST_INPUTS(X) \
  X(RtComponentPropertyType::Bool, false, inputBool, "Input Bool", "An example of a boolean input parameter") \
  X(RtComponentPropertyType::Float, 1.f, inputFloat, "Input Float", "An example of a float input parameter") \
  X(RtComponentPropertyType::Float3, Vector3(1.f, 2.f, 3.f), inputFloat3, "Input Float3", "An example of a Float3 input parameter")
```

### State Parameters
State values are not shared with any other components and can be used however the component needs. They persist between updates.

```cpp
#define LIST_STATES(X) \
  X(RtComponentPropertyType::Bool, false, stateBool, "", "An example of a Bool state parameter") \
  X(RtComponentPropertyType::Float, 2.f, stateFloat, "", "An example of a Float state parameter")
```

### Output Parameters
Output values can be read by other components but should only be set by the owning component.

```cpp
#define LIST_OUTPUTS(X) \
  X(RtComponentPropertyType::Bool, false, outputBool, "Output Bool", "An example of a Bool output parameter") \
  X(RtComponentPropertyType::Float, 3.f, outputFloat, "Output Float", "An example of a Float output parameter")
```

### Parameter Options

Each parameter can have optional properties set after the docString:

```cpp
X(RtComponentPropertyType::Float, 1.f, inputFloat, "Input Float", "test for Float", \
  property.minValue = 0.0f, property.maxValue = 10.0f, property.optional = true)
```

Available options include:
* `property.minValue` / `property.maxValue` - Range constraints (currently UI hints only)
* `property.optional` - Whether the component functions without this property being set
* `property.oldUsdNames` - For backwards compatibility when renaming properties
* `property.enumValues` - For displaying as an enum in the UI
* `property.isSettableOutput` - Used for `const` components, use this on an Input property to create an output property which can be set in the toolkit's property panel.
* `property.treatAsColor` - If set on a Float3 or Float4, this indicates to the toolkit that a color picking widget should be displayed for this property.
* `property.allowedPrimTypes` - Allows for filtering of `Prim` properties, if the target must be of a specific type (i.e. `PrimType::UsdGeomMesh`  or `PrimType::OmniGraph`)

### Enum Values Example

The `property.enumValues` option allows you to define a set of named values for a property, which will be displayed as a dropdown in the UI. This is particularly useful for properties that should only accept specific predefined values.

First, define your enum class:

```cpp
enum class LightType : uint32_t {
  Point = 0,
  Spot = 1,
  Directional = 2,
  Area = 3,
};
```

Then use it in your parameter definition:

```cpp
#define LIST_INPUTS(X) \
  X(RtComponentPropertyType::Enum, 0, lightType, "Light Type", "The type of light to create", \
    property.enumValues = { \
      {"Point", {LightType::Point, "Omnidirectional point light"}}, \
      {"Spot", {LightType::Spot, "Directional spot light with falloff"}}, \
      {"Directional", {LightType::Directional, "Infinite directional light"}}, \
      {"Area", {LightType::Area, "Area light with physical size"}} \
    })
```

The enum values are stored as the underlying type (e.g., `uint32_t` for `LightType`) but displayed with user-friendly names and descriptions in the UI.

### Allowed Prim Types Example

The `property.allowedPrimTypes` option restricts which USD prim types can be selected for a `Prim` property. This is useful when a component only works with specific object types.

Available `PrimType` values:
* `PrimType::UsdGeomMesh` - Mesh geometry
* `PrimType::UsdLuxSphereLight` - Sphere light
* `PrimType::UsdLuxCylinderLight` - Cylinder light
* `PrimType::UsdLuxDiskLight` - Disk light
* `PrimType::UsdLuxDistantLight` - Distant/directional light
* `PrimType::UsdLuxRectLight` - Rectangular area light
* `PrimType::OmniGraph` - Another graph

```cpp
#define LIST_INPUTS(X) \
  X(RtComponentPropertyType::Prim, kInvalidPrimTarget, targetMesh, "Target Mesh", \
    "The mesh to modify.", \
    property.allowedPrimTypes = {PrimType::UsdGeomMesh}) \
  X(RtComponentPropertyType::Prim, kInvalidPrimTarget, targetLight, "Target Light", \
    "The light to control.", \
    property.allowedPrimTypes = {PrimType::UsdLuxSphereLight, PrimType::UsdLuxRectLight})
```

## Defining the Component

Invoke the `REMIX_COMPONENT` macro to define your component. Make sure the component name uses UpperCamelCase.

```cpp
REMIX_COMPONENT( \
  /* the Component name */ MyComponent, \
  /* the UI name */        "My Component", \
  /* the UI categories */  "animation,transform", \
  /* the doc string */     "A component that does something useful.", \
  /* the version number */ 1, \
  LIST_INPUTS, LIST_STATES, LIST_OUTPUTS);
```

## Defining the `updateRange` function

The `updateRange` function is responsible for updating a batch of components and will usually take the form:

```cpp
void MyComponent::updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) {
  // Update each component instance in the batch
  for (size_t i = start; i < end; i++) {
    // Read input values:
    if (m_inputBool[i]) {
      // Use state values
      m_stateFloat[i] = m_stateFloat[i] + 1.0f;
    }
    
    // Write output values:
    m_outputFloat[i] = m_stateFloat[i];
  }
}
```

**Important Notes:**
* Graph updates usually happen between rendering frames, so any state being read will be from frame N, but any changes the graph makes will happen on frame N+1
* The exception is the first frame the graph exists on - it will be updated once immediately on creation to avoid rendering any default values
* Always iterate from `start` to `end` to process the correct range of component instances
* Access properties using the `m_` prefix and array indexing `[i]`

## Optional Component Functions

Components can optionally define additional callback functions beyond `updateRange`. These functions allow for custom initialization, cleanup, and scene modification behaviors. To use these functions, you must manually declare your component class instead of using the `REMIX_COMPONENT` macro.

### Manual Component Declaration

When you need optional functions, declare your class manually and use `REMIX_COMPONENT_BODY` inside it:

```cpp
class MyComponent : public RtRegisteredComponentBatch<MyComponent> {
private:
  REMIX_COMPONENT_GENERATE_PROP_TYPES(LIST_INPUTS, LIST_STATES, LIST_OUTPUTS)
  REMIX_COMPONENT_BODY(
    /* the Component class name */ MyComponent,
    /* the UI name */        "My Component",
    /* the UI categories */  "Act",
    /* the doc string */     "A component that does something useful.",
    /* the version number */ 1,
    LIST_INPUTS, LIST_STATES, LIST_OUTPUTS,
    /* optional arguments: */
    spec.initialize = initialize;
    spec.cleanup = cleanup;
    spec.applySceneOverrides = applySceneOverrides;
  )

  void updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) final;
  
  // Static wrapper functions
  static void initialize(const Rc<DxvkContext>& context, RtComponentBatch& batch, const size_t index) {
    static_cast<MyComponent&>(batch).initializeInstance(context, index);
  }
  static void cleanup(RtComponentBatch& batch, const size_t index) {
    static_cast<MyComponent&>(batch).cleanupInstance(index);
  }
  static void applySceneOverrides(const Rc<DxvkContext>& context, RtComponentBatch& batch, const size_t start, const size_t end) {
    static_cast<MyComponent&>(batch).applySceneOverridesInstance(context, start, end);
  }
  
  // Instance methods
  void initializeInstance(const Rc<DxvkContext>& context, const size_t index);
  void cleanupInstance(const size_t index);
  void applySceneOverridesInstance(const Rc<DxvkContext>& context, const size_t start, const size_t end);
};
```

### The `initialize` Function

The `initialize` function is called once when a component instance is created. Use it to acquire resources, create objects, or perform setup that should happen once per instance.

**Signature:**
```cpp
static void initialize(const Rc<DxvkContext>& context, RtComponentBatch& batch, const size_t index)
```

**Example:** Acquiring a resource on initialization:

```cpp
void MyComponent::initializeInstance(const Rc<DxvkContext>& context, const size_t index) {
  // Acquire a resource using the config path
  const Resource* resource = ResourceManager::acquire(
    m_configPath[index],
    m_priority[index],
    m_initialValue[index]
  );
  
  if (resource != nullptr) {
    // Cache the resource pointer in a state variable
    m_cachedResourcePtr[index] = reinterpret_cast<uint64_t>(resource);
  } else {
    Logger::err(str::format("MyComponent: Failed to acquire resource '", m_configPath[index], "'."));
    m_cachedResourcePtr[index] = 0;
  }
}
```

### The `cleanup` Function

The `cleanup` function is called once when a component instance is destroyed. Use it to release resources, delete objects, or perform teardown.

**Signature:**
```cpp
static void cleanup(RtComponentBatch& batch, const size_t index)
```

**Example:** Releasing a resource on cleanup:

```cpp
void MyComponent::cleanupInstance(const size_t index) {
  // Release the resource through the manager
  Resource* resource = reinterpret_cast<Resource*>(m_cachedResourcePtr[index]);
  if (m_cachedResourcePtr[index] != 0 && resource != nullptr) {
    ResourceManager::release(resource);
  }
  m_cachedResourcePtr[index] = 0;
}
```

### The `applySceneOverrides` Function

The `applySceneOverrides` function is not currently safe to use - the rendering pipeline will need to be refactored first.

**When to use each function:**
* **initialize**: Resource acquisition, one-time setup, cache creation
* **cleanup**: Resource release, memory cleanup, reference counting
* **applySceneOverrides**: Don't use this yet
* **updateRange**: Component logic, state updates, output calculations

## Flexible Type Components

Flexible types (`NumberOrVector` and `Any`) allow a single component definition to work with multiple data types. The actual type is resolved when the graph is loaded from USD. Creating components with flexible types requires using C++ templates and manual class declaration (as shown above).

### Single Flexible Type (Shared Across Properties)

When all flexible-typed properties share the same resolved type (e.g., input and output are both `Float3`), use a single template parameter:

```cpp
#include "../rtx_graph_component_macros.h"

#define LIST_INPUTS(X) \
  X(RtComponentPropertyType::NumberOrVector, 0.0f, input, "Input", "The value to smooth.") \
  X(RtComponentPropertyType::Float, 0.1f, smoothingFactor, "Smoothing Factor", "How fast to smooth.")

#define LIST_STATES(X) \
  X(RtComponentPropertyType::Bool, false, initialized, "", "Tracks initialization.")

#define LIST_OUTPUTS(X) \
  X(RtComponentPropertyType::NumberOrVector, 0.0f, output, "Output", "The smoothed value.")

// Template class with one type parameter for all NumberOrVector properties
template <RtComponentPropertyType valuePropertyType>
class Smooth : public RtRegisteredComponentBatch<Smooth<valuePropertyType>> {
private:
  // Override the property types for flexible properties to use the template parameter
  static constexpr RtComponentPropertyType inputPropertyType = valuePropertyType;
  static constexpr RtComponentPropertyType smoothingFactorPropertyType = RtComponentPropertyType::Float;
  static constexpr RtComponentPropertyType initializedPropertyType = RtComponentPropertyType::Bool;
  static constexpr RtComponentPropertyType outputPropertyType = valuePropertyType;
  
  REMIX_COMPONENT_BODY(
    Smooth,
    "Smooth",
    "Transform",
    "Applies exponential smoothing to a value over time.",
    1,
    LIST_INPUTS, LIST_STATES, LIST_OUTPUTS
  )
  
  void updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) {
    for (size_t i = start; i < end; i++) {
      m_output[i] = lerp(m_input[i], m_output[i], m_smoothingFactor[i]);
    }
  }
};

#undef LIST_INPUTS
#undef LIST_STATES
#undef LIST_OUTPUTS
```

**Key points:**
* Define `static constexpr RtComponentPropertyType` for each property, using the template parameter for flexible properties
* The template parameter replaces `REMIX_COMPONENT_GENERATE_PROP_TYPES` for flexible properties
* Non-flexible properties (like `smoothingFactor`) still use their concrete types

### Template Instantiation

Flexible type components must be explicitly instantiated for each supported type. Add instantiations to `rtx_component_list.cpp`:

```cpp
// For NumberOrVector types (Float, Float2, Float3, Float4)
INSTANTIATE_NUMBER_OR_VECTOR_TYPES(Smooth)

// For Any types (all non-flexible types)
INSTANTIATE_ANY_TYPES(Select)
```

### Binary Operations with Different Input Types

Some components (like `Add`, `Multiply`) need separate flexible types for each input that may differ. These use multiple template parameters and helper macros from `rtx_graph_flexible_types.h`:

```cpp
#include "../rtx_graph_component_macros.h"
#include "../rtx_graph_flexible_types.h"

#define LIST_INPUTS(X) \
  X(RtComponentPropertyType::NumberOrVector, 0, a, "A", "First value.") \
  X(RtComponentPropertyType::NumberOrVector, 0, b, "B", "Second value.")

#define LIST_STATES(X)

#define LIST_OUTPUTS(X) \
  X(RtComponentPropertyType::NumberOrVector, 0, sum, "Sum", "A + B")

// Three template parameters: input A type, input B type, and result type
template <RtComponentPropertyType aPropertyType, RtComponentPropertyType bPropertyType, RtComponentPropertyType sumPropertyType>
class Add : public RtRegisteredComponentBatch<Add<aPropertyType, bPropertyType, sumPropertyType>> {
  REMIX_COMPONENT_BODY(
    Add,
    "Add",
    "Transform",
    "Adds two values together.",
    1,
    LIST_INPUTS, LIST_STATES, LIST_OUTPUTS
  )
  
  void updateRange(const Rc<DxvkContext>& context, const size_t start, const size_t end) {
    for (size_t i = start; i < end; i++) {
      m_sum[i] = m_a[i] + m_b[i];
    }
  }
};

#undef LIST_INPUTS
#undef LIST_STATES
#undef LIST_OUTPUTS

// Forward declaration for the instantiation function
void createTypeVariantsForAdd();
```

In the `.cpp` file, use the macro to automatically instantiate all valid type combinations:

```cpp
#include "add.h"
#include "../rtx_graph_flexible_types.h"

// Automatically instantiates all valid A + B combinations and computes result types
DEFINE_BINARY_OP_COMPONENT_CPP(Add, std::declval<A>() + std::declval<B>(), RtComponentPropertyNumberOrVector)
```

For comparison operations that always return `Bool`:

```cpp
DEFINE_COMPARISON_OP_COMPONENT_CPP(EqualTo, std::declval<A>() == std::declval<B>(), RtComponentPropertyNumberOrVector)
```

Finally, call the instantiation function in `rtx_component_list.h`:

```cpp
inline void forceAllFlexibleComponentInstantiations() {
  // ...existing calls...
  createTypeVariantsForAdd();
}
```

# Component Versioning

Great care needs to be taken when changing components that are already in use to avoid breaking already published data. When updating existing components, follow these guidelines.

## Not a Versioned Change

Making any of these changes does not require incrementing the version number in the Component Definition:

* Change the UI name or tooltip of a property
* Change the name of a component
  * Make sure to add the old name to the component definition as `spec.oldNames = {"OriginalName"}`. See `test_component.h` for example.
* Fix logical bugs in the component
* Make algorithmic changes in the component
  * i.e., change the way the component works, but not the inputs or outputs

## Non-Breaking Change

Making these changes requires bumping the version number and will prevent newly defined copies of this component from working in older runtimes.

* Change a property name
  * Make sure to add the old name to the property definition as `property.oldUsdNames = {"oldName"}`
  * If a Component's USD prim defines multiple versions of the same property (due to layering or incorrect data migration), the strongest version will be used, as defined by:
    * Whichever version is defined on the strongest layer
    * If the strongest layer has multiple versions, then prefer the newest name
    * If the strongest layer does not have the newest name, use the earliest name in `oldUsdNames`
  * That means that if there are multiple `oldUsdNames`, then the oldest name should be on the right:
    ```cpp
    property.oldUsdNames = {"oldName", "olderName", "originalName"}
    ```
* Add a new property
* Change the type or contents of a state property

## Breaking Change

Rather than making these changes to an existing component, a new component should be made (possibly by copying the old component and changing the name).

* Change the type of an input or output property
  * We can't automatically convert a type that is connected to another component
* Change the contents of an input or output property (i.e., float to unorm)
  * This would need to convert the connected values every frame. Better to just use a different update function.
* Remove an input or output property
* Repurpose a component name to a different component type
  * Once a name maps to a bit of functionality, that pairing should be permanent.

**Note:** The UI name for a component can be changed, so while the new copy could use the original UI name, the original component's UI name could be changed to "My Component (deprecated)".

Also note that ideally, the old component and new component should share as much code as possible via helper functions.

# Component Registration

Components are automatically registered when they are defined using the `REMIX_COMPONENT` macro, and the header file is added to `rtx_component_list.h`. The registration system:

1. Generates a unique hash for each component type
2. Stores the component specification for runtime lookup
3. Enables automatic generation of documentation and schemas

# Component Schema

Remix Components are using a subset of the OmniGraph system to enable the Toolkit UI and USD encoding.  It's important to note that while we expose .ogn schema for Remix Components, they are not functional OmniGraph nodes, and the Remix Runtime does not support non-remix OmniGraph components.

## Regenerating Schema

The Toolkit UI for Components is driven by schema files that are auto generated from the C++ component class. To get new components to show up in the Toolkit, you need to regenerate those schema files and then notify the toolkit.

1. Create a new custom component, or change the properties of an existing one.
2. Build the runtime, and install the updated runtime in your app.
3. Set an environment variable `RTX_GRAPH_WRITE_OGN_SCHEMA=1`.
4. Run your app. During startup, new node schema will be generated in `<gameFolder>\rtx-remix\schemas\`
5. copy the schems into your toolkit install:
    - In the Toolkit's folder, find `exts\lightspeed.trex.logic.ogn\lightspeed\trex\logic\ogn\ogn\python\nodes\` 
    - Copy/Paste the files in `schemas` into that `nodes` folder. Most of them should overwrite existing files.

# Graph Execution

Components in a graph are executed in topological order based on their connections. The system:

1. Determines the execution order to ensure all inputs are available before components that depend on them
2. Updates components in batches for performance
3. Calls `updateRange` on each component batch with the appropriate start/end indices
4. Optionally calls `applySceneOverrides` for components that need to modify the scene directly

Note that graphs are batched by a topological hash.  This means that large numbers of graphs that have the same component and connections (but different input values) can be updated very performantly.  If a graph has a different component or connection, it will be part of a separate batch.

# Best Practices

1. **Keep components focused**: Each component should do one thing well
2. **Use meaningful names**: Component and property names should clearly indicate their purpose
3. **Document thoroughly**: Provide clear docStrings for all components and properties
4. **Test thoroughly**: Components should be tested with various input combinations
5. **Consider performance**: Batch operations efficiently and avoid unnecessary computations
6. **Plan for versioning**: Design components with future changes in mind.  Consider using Enums instead of booleans 
7. **Use appropriate data types**: Choose the most specific type that fits your needs
8. **Handle edge cases**: Consider what happens with invalid or missing inputs

# Conf file creation

To create new .conf files to use with RtxOptionLayerAction, follow these steps:

## Initial Setup:

Do this before creating new conf files.

1. Delete user.conf file if you have it. It would be in the same folder as rtx.conf.  (It it contains valuable data, you can back it up and restore it after creating your new conf files).
2. Launch the game
3. Open the User Graphics Settings menu by pressing Alt+X.
    (If this opened the RTX Remix Developer Menu, click "Graphics Settings Menu" at the top.)
4. Press the Save Settings button
5. Close the game.

This ensures all of the automatic graphics quality settings are saved to user.conf.  If you don't do this, they pollute the new .conf files.

## Generating a new .conf file:

1. Launch the game.
2. Make the changes you want in the RTX Remix Developer Menu. Avoid changing anything you don't want to save, like graphics quality settings or UI preferences.
3. At the bottom of the Developer Menu, ensure the save settings look like this:
   - `new.conf` for Settings Save Location
   - Save Changed Settings Only = Checked
   - Override configs = Unchecked
4. Click the save button.
5. In a file browser, navigate to "new.conf" (same folder as rtx.conf and the game's executable).
6. Rename "new.conf", and move it into your mod (somewhere inside of `rtx-remix/mods/YourMod/`).
   - These new .conf files are assets - treat them like a texture or mesh file.
   - In the toolkit, you can select an RtxOptionLayerAction and set the "Config Path" to the relative path from your target layer to your new .conf file.

The new.conf file will capture all changes made since the game was launched. If you continue making changes and save out more new.conf files, the changes will continue to build up. To reset the change tracking, relaunch the game.

