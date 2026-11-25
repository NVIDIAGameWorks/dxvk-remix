# NUMOS Sky

[NUMOS](https://github.com/Kim2091/numos-sky) renders a physically based sky with a sun, moons, stars, volumetric clouds, haze and weather. Remix can draw it in place of the game's sky.

## For modders

### Turning it on

Set `rtx.skyMode = 1`, or pick **NUMOS Atmosphere** as the **Sky Mode** in the developer menu under **Game Setup > Step 2: Parameter Tuning > Sky Tuning**. The game's rasterized sky is no longer captured while NUMOS draws the sky. Set the mode back to **Original Game Sky** to get it back.

Geometry that the game draws as a sky has to be categorized as sky, as it does for the original sky mode. Reprojected 3D skybox geometry stays ray traced, so it appears in front of the NUMOS sky. A dome light replaces the NUMOS sky behind the clouds and turns the haze off.

NUMOS takes the scale and up axis from `rtx.sceneScale` and `rtx.zUp`. The **Setup** tab has overrides for both, and the ground level.

### Haze and Remix volumetrics

NUMOS haze and Remix volumetrics are never drawn together.

| `rtx.atmosphere.aerialPerspective` | `rtx.volumetrics.enable` | Air in front of surfaces |
|---|---|---|
| On or off | On (the default) | Remix volumetrics |
| On | Off | NUMOS haze, which replaces the game's fog |
| Off | Off | The game's fog |

The sky and the clouds draw in every case, and the clouds keep their own distance haze. NUMOS ground fog is part of the haze, so it is off while Remix volumetrics are on.

Neither option changes the other. While volumetrics are on, the **Haze** tab and the haze section of the user's graphics menu show a **Use NUMOS haze** button, which turns volumetrics off and the haze on.

### Weather presets

The **Weather** tab opens the Weather Presets window. Each preset is a `.conf` file in `rtx.weather.presetDirectory`, which is `rtx-remix/weather` in the game's folder by default. The built-in presets are written there the first time NUMOS runs. A mod can ship its own files in that folder.

```ini
weather.bindings = Rain, 12
weather.transitionSeconds = 20
weather.enabled = true

rtx.atmosphere.cloudCoverageMean = 0.9
rtx.atmosphere.aerosolDensity = x1.5
```

- A preset overrides only the settings it lists, and only while it shows.
- A value written `x<number>` multiplies the user's own setting. Colors also accept three factors.
- `weather.bindings` lists the game's weather ids that the preset stands for.
- `rtx.weather.preset` selects the preset that shows while the game reports no bound weather.

The window can also tune a preset with the sky's sliders, and share a sky as a text seed.

### Game integrations

An integration can set these options, which are never saved.

| Option | Use |
|---|---|
| `rtx.weather.gameWeather` | The current weather id, or several separated by commas with the most specific first. |
| `rtx.weather.gameWeatherOutgoing` | The weather that the game blends away from. |
| `rtx.weather.gameWeatherBlend` | The game's blend from 0 to 1. A negative value lets Remix blend over the preset's transition time. |
| `rtx.skyProvider.worldChangeCount` | A change makes NUMOS measure the ground level again, for example after loading a save. |

A game that moves its world origin can use `rtx.atmosphere.useCameraWorldOverride` and `rtx.atmosphere.cameraWorldOverride`.

All NUMOS settings are listed under `rtx.atmosphere` in [RtxOptions.md](../RtxOptions.md).

### Installed files

Distribute the whole `numos` folder next to the Remix runtime's `d3d9.dll`, which is `.trex/numos` for games that use the bridge.

| Path | Content |
|---|---|
| `numos/spirv/` | Shaders. Without them, the game's sky shows and the log has an error. |
| `numos/nightsky/` | Star, Milky Way and moon data, with `CREDITS.md`. |
| `numos/LICENSE`, `numos/NOTICE` | The Apache-2.0 license and notice of NUMOS. |
| `numos/THIRD_PARTY_NOTICES.md`, `numos/licenses/` | Notices of the code and data in NUMOS. |

`milkyway.bin` and `starlight_low.bin` are licensed CC BY-SA 3.0 IGO. Keep the credits with them, and share changed versions under that license.

## For developers

### Architecture

NUMOS is an unmodified submodule in `submodules/numos`. Its `docs/DESIGN.md` describes the library. Remix reaches it through these files.

| File | Role |
|---|---|
| `rtx_numos.*` | Owns the renderer, its inputs, outputs, bindings and lights. |
| `rtx_numos_gpu.*` | Implements `numos::IGpu` on DXVK. |
| `rtx_numos_settings.*` | Registers the library's settings table as RTX options. |
| `rtx_numos_weather.*` | Loads and blends the weather presets. |
| `rtx_numos_ui.*` | Draws the developer and user menu controls. |
| `rtx/algorithm/sky_provider.slangh` | Is the interface for the path tracer. |
| `rtx/pass/composite/composite_sky.slangh` | Composites the sky, clouds and haze. |
| `rtx/utility/sky_foreground.slangh` | Packs what the G-buffer found in front of the sky. |

### Frame flow

1. `prewarmShaders` registers the shaders once NUMOS is selected, so ray tracing waits for their compilation.
2. `updateLights` applies the lights that the previous frame emitted, before `SceneManager::prepareSceneData`.
3. `beginFrame` blends the weather, resolves the haze, and records the passes that come before path tracing.
4. `dispatchPostDepth` renders the screen space clouds from the G-buffer's linear view Z.
5. The composite draws the sky, clouds and haze with `composite_sky_provider.comp`.

Nothing of NUMOS is created before it is first selected. After switching back, its resources stay allocated until the device is destroyed, except the sky foreground image.

### Scale

NUMOS stores pre-exposed radiance, scaled by `radianceScale`. The renderer's radiance is unscaled, so ray tracing multiplies by `1 / radianceScale` and the composite converts around each NUMOS function.

`rtx.skyBrightness` applies only to the game's sky.

### Haze and volumetrics

`RtxNumos::beginFrame` resolves the exclusivity once per frame, after the weather is blended. It never writes an option.

While volumetrics are on, it clears `aerialPerspective` and `fogVisibilityMeters` in the settings that go to the renderer. The height fog needs its own clear, because the cloud composite applies it independently of aerial perspective. The distance haze of the clouds comes from the cloud march, which depends on neither setting. `volumetricsHandoffWorldUnits` stays 0, because the two never share a range.

`skyHazeActive` needs aerial perspective, no dome light, and a camera outside of media. While it is set, every `calculateFog` site is skipped.

| Composite path | Haze distance |
|---|---|
| Opaque surface | View distance |
| Alpha blended surface, also over the sky | Its hit distance |
| Emissive or attenuating layers in front of a surface | The nearest such layer |
| PSR paths at active pixels | The virtual distance of each path |
| The rest of a PSR pixel, and inactive sparse PSR pixels | The PSR surface |
| Miss | The air up to the sky |

A miss never gets the depth based fog, which would paint over the sky. The froxel radiance cache still runs while the haze is on, because other systems read it.

### Sky paths

The sun and moons are distant lights, so their disks appear only on paths that no light sample follows.

| Path | Disks | Indirect scale |
|---|---|---|
| Primary ray, drawn in the composite | Yes | No |
| PSR miss, evaluated in the G-buffer | Yes | No |
| SHaRC update at a G-buffer miss | No | No |
| Indirect miss after a diffuse lobe | No | Yes |
| Indirect miss after any other lobe | No | No |

`PathState` stores the diffuse gather in the sign bit of `_coneSpreadAngle`, so the payload does not grow. Always use the `coneSpreadAngle` property, and initialize the field before setting either property.

### Lights

The sun, moons and lightning are externally tracked lights, one frame behind the renderer. Distant lights with `lightFlagCloudShadowed` get the cloud shadow in direct and indirect next event estimation, and in the volume integration. Resampling ignores the shadow.

`updateLights` has to run before `prepareSceneData`, whose garbage collection releases the light lock. The GPU resident scene skips that collection, so `updateLights` runs it after a change.

### Composite

The base composite variant has none of the sky code, because that code raises the register count of the whole pass. The sky variant is compiled only when NUMOS is selected.

The G-buffer stores a `SkyForeground` per pixel in every sparse mode, as the attenuation and geometry flags are compacted. It holds the radiance, the nearest distance and the attenuation of transparent layers, or the distance of the PSR surface as a negative value. Only active pixels can separate the PSR paths. The sky behind transparent layers is weighted by their attenuation.

Volumetric in-scatter stays in front of the clouds. Particles store no distance. Over the sky they appear in front of the clouds and haze, and over a surface they take that surface's distance.

Where clouds cover a surface, the composite writes their depth and motion vectors for the passes that follow it. These include the depth copy of the Remix API. The clouds are reprojected without wind, so DLSS Super Resolution and XeSS also get the share of clouds and haze in the bias or responsive mask. Ray Reconstruction takes no mask. XeSS creates its context again when the use of the mask changes.

### Bindings

- The bindings 400 to 413 of `common_binding_indices.h` are reserved in every pass with common ray tracing bindings.
- The sky variant of the composite adds 42 to 46 and 70 to 94.
- The G-buffer writes the sky foreground at 158.

The resources that `numos/host/numos_host.slangh` uses by name keep the names it expects.

### Shader includes

NUMOS carries copies of a few Remix helpers in `numos/util`. `src/dxvk/shaders/numos_shim` is earlier in the include path and maps them onto Remix's files, so a shader never gets both definitions. A NUMOS update that adds a helper there can need a new shim. `Camera` and `numos::Camera` have to keep the same layout.

### Threads

The UI runs on the present thread. It takes `RtxNumos::lockState()` around every use of the renderer and the weather. Only the render thread creates the renderer.

### Settings

The options register during static initialization, before the config files load. Registration must not look options up, because reading the option map makes every later registration copy it. Deprecated keys of NUMOS are not registered.

### Build and updates

`scripts-common/numos_shaders.py` compiles the shaders of NUMOS with Remix's Slang compiler, checks them against the library's pass table, and installs them with the data and notices. A failed check means the compiler or the submodule does not match that table. Fix this in NUMOS or the pin, and never edit the submodule in place.

After updating the submodule, check these.

- The source list of `numos_lib` in `src/dxvk/meson.build`.
- The `numos/util` shims and the layouts of `Camera` and `AtmosphereArgs`.
- `numos_host.slangh` and the resource names it expects.
- The NUMOS section of `ThirdPartyLicenses.txt`.
- `RtxOptions.md`, which has to be regenerated.
