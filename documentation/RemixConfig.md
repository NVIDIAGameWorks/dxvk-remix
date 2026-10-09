# The RtxOption System

The RtxOption system within RTX Remix is how all of Remix's global configuration options are controlled. It offers a flexible system to manage various sources of configuration, and is mainly interacted with via the runtime's `User Graphics Settings` and `Developer Settings Menu`.

This document describes how the RtxOption system works and how the code is organized.

For a complete reference of all available options and their default values, see [RtxOptions.md](../RtxOptions.md).

## Understanding Layers

At the heart of the RtxOption system is the concept of **layers**. Think of layers like sheets of transparent paper stacked on top of each other. Each sheet can have settings written on it, and when you look down through the stack, you see the combined result.

### Why Layers?

RTX Remix needs to combine settings from many different sources:

- **Default values** built into the application
- **Mod developer settings** in configuration files (rtx.conf)
- **End-user preferences** from the graphics settings menu
- **Quality presets** like "Ultra" or "Performance"
- **Remix Logic graphs** that respond to game state (e.g., indoors vs outdoors)

The layer system gives each source its own layer with a defined priority. This provides:

- **Clean separation**: Mod settings stay in `rtx.conf`, user preferences stay in `user.conf`
- **Easy resets**: Clear a single layer to reset those settings without affecting others
- **Predictable overrides**: Higher-priority layers always win, so user preferences override mod defaults
- **Scene-specific tuning**: Dynamic layers can adjust settings for different areas or game states

### Sparse Layers

Each layer is **sparse**. It only contains values for settings that were explicitly set in that layer. If a layer doesn't have an opinion about a particular setting, it's simply not present in that layer. This is important because:

- Config files only need to specify the settings they want to change
- Users don't accidentally override mod settings they never touched
- The system can tell which layer a setting came from

### How Layers Stack

Layers are ordered by priority (higher values override lower values). When multiple layers share the same priority, they're ordered alphabetically by name (`a.conf` overrides `z.conf`).

```text
┌─────────────────────────────┐  ← Highest priority (top of stack)
│  Quality Presets            │     Settings applied by the selected preset
├─────────────────────────────┤
│  User Settings (user.conf)  │     End-user preferences
├─────────────────────────────┤
│  Dynamic Layers             │     Runtime changes (e.g., indoor areas)
├─────────────────────────────┤
│  Remix Config (rtx.conf)    │     Mod developer and game compatibility settings
├─────────────────────────────┤
│  Default Values             │     Built-in fallbacks
└─────────────────────────────┘  ← Lowest priority (bottom of stack)
```

This means:
- User preferences override mod developer settings
- When a quality preset is selected (not "Custom"), the settings it applies override user preferences
- Remix Logic graphs can activate dynamic layers to temporarily merge with or override settings

### Layer Blending

For numeric settings (like brightness or exposure), layers can **blend** together smoothly. Each layer has a "blend strength" from 0% to 100%:

- **0% strength**: The layer has no effect (as if it doesn't exist)
- **100% strength**: The layer fully applies its values
- **50% strength**: The layer's values are mixed halfway with the layers below

This enables smooth transitions. For example, when entering a dark cave, a "cave lighting" layer can gradually fade in from 0% to 100% over a few seconds, creating a seamless visual transition rather than an abrupt change.

For on/off settings (like enabling a feature), blending works differently. These settings use a "threshold", so they only take effect once the blend strength crosses a certain percentage.

### Debugging Layers

To understand why an option has a particular value:

- **Hover over any option** in the Developer Menu to see its tooltip. The tooltip shows the current value from each layer that has an opinion about that option.
- **Open the Option Layers section** in the Developer Settings tab to see all active layers, their blend strengths, and their contents. You can expand each layer to see what values it contains.

**Note:** If you manually adjust blend strengths in the Option Layers section while Remix Logic graphs are running, the graphs will immediately override your changes. To experiment with blend values, pause graph execution first using the controls at the top of the Option Layers section.

## The Built-in Layers

RTX Remix includes several built-in layers, each serving a specific purpose:

### For End Users

| Layer | What It Does |
|-------|--------------|
| **User Settings** | Your personal preferences. Saved to `user.conf`. This includes graphics preset choices, direct graphics settings, and other per-user preferences like UI configuration. |
| **Quality Presets** | Contains settings *derived from* the presets you've selected. For example, selecting "Ultra" graphics sets specific values for path bounces, resolution scale, etc. The User layer stores *which preset* you chose; the Quality layer stores *what values* that preset applies. |

### For Mod Developers

| Layer | What It Does |
|-------|--------------|
| **Remix Config** | The main `rtx.conf` file where mod developers configure game-specific settings. |
| **baseGameMod Config** | **Deprecated** An additional `rtx.conf` that is included for legacy support, but generally shouldn't be used.  Overrides the main `rtx.conf`. |

### Per-GPU Overrides

Mod developers can set per-GPU defaults for the graphics preset, DLSS mode and Ray Reconstruction. Configure them in the Developer Menu under **Rendering → General → Per-GPU Overrides**, or add them to `rtx.conf`:

```ini
rtx.gpuOverride.graphicsPreset.10DE_2684 = Ultra
rtx.gpuOverride.dlssMode.10DE_2684 = Quality
rtx.gpuOverride.rayReconstruction.1002_744C = False
```

The last part of each key is the GPU's Vulkan vendor ID and device ID in uppercase hexadecimal, at least four digits each, joined with `_`. The Developer Menu shows the ID of the current GPU. Keys in any other form, such as lowercase hexadecimal, never match and are reported in the log.

| Key prefix | Values | Applies when |
|---|---|---|
| `rtx.gpuOverride.graphicsPreset.` | `Ultra`, `High`, `Medium`, `Low` | `rtx.graphicsPreset` is `Auto`. The override replaces the architecture and video memory heuristics. |
| `rtx.gpuOverride.dlssMode.` | `UltraPerformance`, `Performance`, `Balanced`, `Quality`, `FullResolution` | `rtx.qualityDLSS` is `Auto`. The override replaces the resolution-based choice and is not lowered by the Medium or Low presets. |
| `rtx.gpuOverride.rayReconstruction.` | `True`, `False` | Always, as the default for `rtx.enableRayReconstruction`. A value in `user.conf` or the `DXVK_RAY_RECONSTRUCTION` environment variable takes precedence. |

Values are case-insensitive. Unrecognized values, and values that are only set in layers below their blend threshold, are ignored; unrecognized values are also logged. Entries in `rtx.conf` that fail to parse, such as a misspelled `True`, are shown in red in the Per-GPU Overrides table so they can be replaced or removed. Overrides are read when the graphics preset is applied: at startup, when the graphics preset changes, and after **Delete All User Settings** resets the preset to Auto. Edits take effect at the next of these.

These keys are dynamic options; see [Dynamic Option Namespaces](#dynamic-option-namespaces).

### RTX Spark

On RTX Spark, Auto graphics selection defaults to Medium, and Auto DLSS selection defaults to Performance. Per-GPU graphics and DLSS overrides take precedence over these defaults.

When `rtx.graphicsPreset` is Auto, `rtx.lowMemoryGpu` is enabled when the Vulkan device-local memory heaps total 8 GiB or less and disabled otherwise. This memory default also applies with a per-GPU graphics override.

Selecting a graphics preset other than Auto bypasses automatic graphics selection. To preserve an explicit `rtx.qualityDLSS` value, select the Custom DLSS preset (`rtx.dlssPreset = 2`). The On preset resets the DLSS mode to Auto.

RTX Spark detection requires Vulkan to report an NVIDIA integrated GPU and NVAPI to identify the same adapter by LUID as an integrated Blackwell GPU. Core count does not affect detection. If the required facts are unavailable or do not match, the standard automatic heuristics apply. The log records detection results and query failures, including `RTX Spark detected` on success.

### Internal Layers

| Layer | What It Does |
|-------|--------------|
| **Default Values** | The built-in defaults for every setting. Used when no other layer provides a value. |
| **DXVK Config** | General DXVK settings from `dxvk.conf` (rarely needed for Remix). |
| **Hardcoded EXE Config** | Special overrides compiled into Remix for specific games. |
| **Environment Variables** | Settings from system environment variables (mainly for testing/automation). |
| **Derived Settings** | Automatically computed values based on other settings and values that shouldn't be saved. |

### Dynamic Layers

Beyond the built-in layers, mod developers can create **dynamic layers** that are activated by Remix Logic graphs. These are useful for:

- **Location-based settings**: Different lighting for indoors vs outdoors
- **Game state changes**: Special effects during cutscenes or boss fights
- **Smooth transitions**: Gradually blending between different visual styles

Dynamic layers are controlled through the Remix Logic system using `RtxOptionLayerAction` components. See [RtxOptionLayerAction documentation](components/RtxOptionLayerAction.md) for details.

#### Creating a Config File for Dynamic Layers

To create a `.conf` file that can be loaded by a dynamic layer:

1. Launch the game.
2. Open the Developer Menu and make the changes you want for your dynamic layer (e.g., indoor lighting settings). Avoid changing user settings like graphics quality or UI preferences.
3. To preview what will be exported, expand the **Remix Config** layer in the bottom of the Developer Menu, and expand **View Changes**. Unsaved changes listed there will be saved to your new file.
4. Find the **Create .conf file for Logic** section.
5. Enter a filename (e.g., `indoor_lighting.conf`).
6. Click **Create**. This exports only the unsaved changes from the **Remix Config** layer to your new file.
7. Move the created file into your mod (e.g., `rtx-remix/mods/YourMod/`). Treat these files as assets: they can be placed in subfolders, and paths to them should be relative to your USD file.
8. In the Remix Toolkit, configure an `RtxOptionLayerAction` component and set its Config Path to point to your new `.conf` file.

**Notes:**
- The export captures all changes made to non-user settings since the game launched (or since rtx.conf was last saved).
- If the file already exists, new changes are merged into it.
- To reset change tracking, reload or save rtx.conf.

## Configuration Files

Settings are stored in simple text files with a `.conf` extension. Each line sets one option:

```ini
# This is a comment
rtx.someOption = value
```

### System Config Files

All of these live in the game's executable's folder.

- **rtx.conf**: Main Remix configuration, for game compatibility and mod configuration.
- **user.conf**: End-user settings, managed by the Remix UI
- **dxvk.conf**: General DXVK settings (rarely needed)

### Value Types

```ini
# Numbers
rtx.exposure = 1.5
rtx.maxLights = 100

# Booleans (True/False)
rtx.enableRaytracing = True

# Vectors (comma-separated)
rtx.skyColor = 0.5, 0.7, 1.0

# Strings (no quotes needed)
rtx.captureFolder = screenshots

# Hash lists (for texture categorization)
rtx.ignoreTextures = 0x8DD6F568BD126398, 0xEEF8EFD4B8A1B2A5
```

For hash lists, you can also remove items that were added by a lower-priority layer by prefixing with `-`:

```ini
# Remove a texture that was ignored by a lower-priority layer
rtx.ignoreTextures = -0x8DD6F568BD126398
```

## Environment Variables

Some settings can be controlled via environment variables.  

| Variable | Purpose |
|----------|---------|
| `DXVK_CONFIG_FILE` | Override path to dxvk.conf |
| `DXVK_RTX_CONFIG_FILE` | Override path to rtx.conf |
| `DXVK_DOCUMENTATION_WRITE_RTX_OPTIONS_MD` | Set to 1 to regenerate RtxOptions.md |

---

# Technical Reference

The following sections provide implementation details for developers working on RTX Remix code.

## Core Components

### RtxOption

`RtxOption<T>` is a templated class that represents a single configurable setting. It is responsible for:

- **Per-option behavior**: Getting/setting values, type conversion, validation (min/max)
- **Change detection**: Callbacks when the resolved value changes
- **Value resolution**: How this option's value is computed from layers (blending, thresholds)
- **Type specific behavior**: Any behavior that depends on the Options type should be handled in RtxOption.

Options are declared using macros:

```cpp
// Basic option
RTX_OPTION_ARGS("rtx", bool, enableRaytracing, true, "Enable ray tracing.");

// Option with min/max values and change callback
RTX_OPTION_ARGS("rtx", float, exposure, 1.0f, "Exposure value.",
    args.minValue = 0.0f,
    args.maxValue = 10.0f,
    args.onChangeCallback = onExposureChanged);

// Option with flags
RTX_OPTION_ARGS("rtx", bool, debugMode, false, "Debug toggle.",
    args.flags = RtxOptionFlags::NoSave);
```

Options are accessed using the function call operator or `get()`:

```cpp
if (RtxOptions::enableRaytracing()) {
    // Ray tracing is enabled
}
float exp = RtxOptions::exposure.get();
```

#### Setting Values

Values are set using `setDeferred()` (preferred) or `setImmediately()`:

```cpp
// Queue for end-of-frame resolution (preferred)
RtxOptions::exposure.setDeferred(2.0f);

// Apply immediately (use sparingly)
RtxOptions::exposure.setImmediately(2.0f);
```

**Important:** Option values are resolved in a batch at the end of each frame. This means if you call `setDeferred()` and then immediately read the option's value, you'll get the *old* value, not the new one. This can cause logic bugs if you're not careful.

- **Use `setDeferred()`** for most cases. Changes are batched and applied together, which is more efficient and avoids mid-frame inconsistencies.
- **Use `setImmediately()`** only when code later in the same frame must see the new value and cannot wait for end-of-frame resolution.

#### OnChange Callbacks

If code needs to react when an option's value changes, use an `onChangeCallback`:

```cpp
RTX_OPTION_ARGS("rtx", float, exposure, 1.0f, "Exposure value.",
    args.onChangeCallback = &onExposureChanged);

// Callback signature
void onExposureChanged(DxvkDevice* device) {
    // React to the change - device is always valid
}
```

**When callbacks are invoked:**
- **At startup:** All options with callbacks are invoked once during initialization, even if values haven't changed from defaults. During this initial call, `device` is `nullptr`.
- **At end of frame:** Callbacks are invoked for options whose resolved value actually changed during that frame. The `device` pointer is valid.

Use `if (device == nullptr) { return; }` at the start of callbacks that require a valid device and don't need to run during initialization.

#### Option Flags

Flags control how options behave and which layer they belong to:

| Flag | When to Use |
|------|-------------|
| `NoSave` | For runtime-only options that should never be written to config files. Examples: debug toggles, temporary state, values computed from other settings. These options are routed to the Derived layer. |
| `NoReset` | For options that should persist when the user clicks "Reset" in the UI. Use sparingly, as most options should be resettable. |
| `UserSetting` | For end-user preferences that belong in `user.conf`, not `rtx.conf`. Examples: graphics quality settings, UI preferences, keybindings. Without this flag, options are treated as mod/developer settings and saved to `rtx.conf`. |

Flags can be combined: `RtxOptionFlags::NoSave | RtxOptionFlags::UserSetting`

### RtxOptionLayer

`RtxOptionLayer` represents a single source of configuration values (one "sheet" in the layer stack). It is responsible for:

- **Per-layer state**: Blend strength, threshold, enabled/disabled status
- **Serialization**: Reading from or writing to a `.conf` file
- **Layer-specific queries**: "What values does this layer contain?" or "Does this layer have a value for option X?"
- **Layer comparison**: Detecting changes, finding redundant values

Don't add option-specific logic here. That belongs in `RtxOption`.

### RtxOptionManager

`RtxOptionManager` is the central coordinator. It is responsible for:

- **Cross-option operations**: Iterating over all options, bulk resets, documentation generation
- **Cross-layer operations**: Managing the layer registry, resolving values across all layers
- **Frame lifecycle**: Applying pending changes, invoking callbacks at end-of-frame
- **Global state**: The dirty option tracking system, layer acquisition/release

### RtxOptionLayerTarget

RAII helper that controls which layer receives option changes in the current scope:

```cpp
void showUserMenu() {
    RtxOptionLayerTarget target(RtxOptionEditTarget::User);
    // All setDeferred() calls are routed based on the UserSetting flag
    someOption.setDeferred(newValue);
}

void updateQualityPreset() {
    RtxOptionLayerTarget target(RtxOptionEditTarget::Derived);
    // All setDeferred() calls are routed based on the UserSetting flag
    someOption.setDeferred(newValue);
}
```

#### How Layer Routing Works

The actual layer that receives changes is determined by **both** the edit target and the option's `UserSetting` flag:

**User-Driven Changes** (User target - any UI interaction):
- Options **with** `UserSetting` flag → **User Settings** layer (`user.conf`)
- Options **without** `UserSetting` flag → **Remix Config** layer (`rtx.conf`)

**Code-Driven Changes** (Derived target - presets, callbacks, automation):
- Options **with** `UserSetting` flag → **Quality Presets** layer
  - **Special case:** When Graphics Preset is Custom → **User Settings** layer instead
- Options **without** `UserSetting` flag → **Derived Settings** layer

**Override Rules:**
- Options with `NoSave` flag **always** go to **Derived Settings** layer, regardless of edit target
- Explicit layer specifications bypass these rules (used for testing and migration)

This design ensures that:
- User preferences always go to `user.conf` when the user makes changes (via any UI)
- Mod developer settings always go to `rtx.conf` when edited in any menu
- Quality preset values go to the Quality layer, but redirect to User Settings when in Custom preset mode
- Runtime-computed values go to Derived layer and are never saved

## System Layer Priorities

| Layer | Priority | Notes |
|-------|----------|-------|
| Default Values | 0 | Hardcoded defaults |
| DXVK Config | 1 | dxvk.conf |
| Hardcoded EXE Config | 2 | config.cpp overrides |
| Remix Config | 3 | rtx.conf |
| baseGameMod Config | 4 | baseGameModPath/rtx.conf |
| Environment Variables | 5 | Env var overrides |
| Derived Settings | 6 | Computed values |
| User Settings | 0xFFFFFFFE | user.conf |
| Quality Presets | 0xFFFFFFFF | Highest priority |

Dynamic layers use priorities in the range 100 to 0xFFFFFFFF - 100.

## Creating Dynamic Layers

For graph-based control, use the `RtxOptionLayerAction` component.

If adding a new system that controls dynamic layers, use `RtxOptionManager::acquireLayer()` to create or obtain a reference to a layer:

```cpp
RtxOptionLayerKey key = { 10000, "MyCustomLayer" };
RtxOptionLayer* layer = RtxOptionManager::acquireLayer("my_layer.conf", key);

// Use the layer...

// Release when done
RtxOptionManager::releaseLayer(layer);
```

## Dynamic Option Namespaces

Some systems need options whose names are only known at runtime, such as one entry per device. Such a system registers a namespace, and every key under the namespace prefix becomes an option of the namespace's type.

```cpp
DynamicOptionNamespace ns;
ns.prefix = "rtx.example.";
ns.type = OptionType::Float;
ns.defaultValue = "1.0";
ns.description = "Example values.";
RtxOptionManager::registerDynamicNamespace(ns);

// With `rtx.example.foo = 0.5` in any config layer:
std::optional<DynamicOptionValue> value = RtxOptionManager::getDynamicValue("rtx.example.foo");
auto entries = RtxOptionManager::enumerateDynamicOptions("rtx.example.", /* withOpinionsOnly */ true);
```

**Namespaces**
- The prefix starts with `rtx.` and ends with `.`. It may not overlap another namespace or contain a compile-time option.
- Types: `Bool`, `Int`, `Float`, `String`, `Vector2`, `Vector3`, `Vector4`, `Vector2i`. Flags: `UserSetting` and `NoSave` only.
- Registering an identical namespace again succeeds without effect. Namespaces are never unregistered.
- Registration may happen before or after config files load. When registered late, keys found in enabled layers are created and resolved before `registerDynamicNamespace` returns; keys in disabled layers appear when the layer is enabled.

**Keys**
- Keys are case-sensitive. The part after the prefix is one or more segments of letters, digits, and `_`, separated by single dots.
- The config parser drops lines whose key contains any other character, so such lines cannot be reported.
- Keys under a registered prefix that break these rules are logged and ignored.

**Values**
- Values are parsed strictly: numbers must use the whole string, floats must be finite, booleans are `true`, `false`, `1`, or `0` (case-insensitive), and vectors need exactly one value per component.
- Strings have leading and trailing spaces and tabs removed. The rest must be non-empty and contain no quotes or control characters. An empty value means the layer has no opinion.
- An invalid value in a layer is logged and ignored, so weaker layers still apply. Saving the layer keeps the original line.

**Behavior**
- Dynamic options are never deleted. `enumerateDynamicOptions(prefix, true)` and `getDynamicValue(key, true)` only report keys that have a value in some layer, including layers with zero blend strength.
- `setDynamicValue` and `clearDynamicValue` edit a key the same way RtxOption setters do: without an explicit layer, the namespace flags and the active `RtxOptionLayerTarget` choose the layer. Changes are validated immediately and applied at the next frame's option update. Clearing a key also drops an invalid line for it from that layer, so saving removes it. Edits to a disabled layer are rejected, and an edit is dropped if its layer is reloaded, cleared, disabled, or released before the edit is applied.
- Removing redundant values keeps a dynamic value unless a weaker layer other than the default layer resolves to the same value, because consumers can tell an explicit value from the namespace default.
- Dynamic options are not listed in [RtxOptions.md](../RtxOptions.md).
- `remixapi_Interface::SetConfigVariable` validates keys in a registered namespace immediately. It returns `REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS` for an invalid key or value, and `REMIXAPI_ERROR_CODE_GENERAL_FAILURE` for a key that no option or namespace owns. Accepted values are written to the user layer at the next frame's option update, or at the first update after startup if the call happens earlier. A value is dropped if the user layer is reloaded or cleared first. Through the 32-bit bridge the call always reports success, so rejections appear only in the log.

Saving any layer keeps `rtx.` keys that no option manages, such as keys for a namespace that registers later.

Code that iterates all options must keep the snapshot returned by `RtxOptionImpl::getGlobalOptionMap()` for the whole loop. Creating dynamic options publishes a new snapshot rather than modifying the existing one.

## Value Resolution

The system maintains a **resolved value** for each option.  This represents a cached result of merging all layers. It is updated once per frame (after the render thread dispatches but before the main thread starts the next frame), ensuring thread-safe access during rendering.

When resolving values:

1. **Float options**: Blended using linear interpolation weighted by blend strength
2. **Non-float options**: First active layer (blend strength >= threshold) wins
3. **Hash set options**: Merged across all layers with add/remove semantics

## Best Practices

1. **Use `setDeferred()` over `setImmediately()`**: Deferred changes are batched and resolved at end-of-frame. Only use `setImmediately()` when subsequent code in the same frame needs the updated value.

2. **Use appropriate flags**: Mark user-facing options with `UserSetting`, runtime-only options with `NoSave`. See [Option Flags](#option-flags) for details.

3. **Use `onChangeCallback` for reactive logic**: Don't read an option immediately after setting it. The value won't be updated yet. Instead, use callbacks to react to changes.

4. **Provide min/max values**: Enables proper UI sliders and validation.

5. **Write descriptive documentation**: Description strings appear in auto-generated docs and tooltips.

6. **Prefer dynamic layers for runtime changes**: Use `RtxOptionLayerAction` rather than directly modifying option values from game logic.

## See Also

- [RtxOptions.md](../RtxOptions.md) - Complete reference of all options
- [RtxOptionLayerAction](components/RtxOptionLayerAction.md) - Graph component for dynamic layer control
- [RtxOptionLayerSensor](components/RtxOptionLayerSensor.md) - Graph component for reading layer state
