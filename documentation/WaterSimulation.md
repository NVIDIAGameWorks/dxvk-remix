# Water Simulation

The water simulation moves the surfaces of animated water ([rtx.animatedWaterTextures](../RtxOptions.md)) with waves that the application pushes: splashes, wakes, explosions. It is a height field of waves on a grid around the camera, simulated on the GPU every frame, which the path tracer reads wherever a ray hits animated water. The application only reports what disturbs the water, through the Remix API; the waves, their normals and parallax are made by the runtime.

## Driving it from the application

Two functions of the Remix API (0.6.11, see [RemixApiChangelog.md](RemixApiChangelog.md)):

- `remixapi_SetWaterSim` (`remixapi_WaterSimInfo`): `clearAllDisturbances` flattens the waves and drops the disturbances sent before, for example on a level change.
- `remixapi_AddWaterDisturbance` (`remixapi_WaterDisturbanceInfo`) presses a round dent of `radius` and depth `strength` (game units) into the water at `position`. A negative strength raises the water. Each call is applied once, in the order of the calls. The runtime applies up to 32 disturbances a frame and keeps the rest for later frames, so batch them and send the most important first.

The C++ wrapper has `remix::Interface::SetWaterSim` and `AddWaterDisturbance`, with the `remix::WaterSimInfo` and `remix::WaterDisturbanceInfo` helpers. Over the bridge, every call is marshalled, so a frame should send a handful of disturbances, not one per particle.

[rtx.waterSim.enable](../RtxOptions.md) switches the simulation on. It is off by default, so a game turns it on, in its rtx.conf or with `remixapi_SetConfigVariable`. While it is off, animated water renders as without it.

Lengths and speeds in the options are in meters and follow [rtx.sceneScale](../RtxOptions.md). Their defaults are for 1 meter = 100 game units.

## Simulation

- The grid has [rtx.waterSim.resolution](../RtxOptions.md) cells of [rtx.waterSim.cellSizeMeters](../RtxOptions.md) along each side and follows the camera in whole cells. It is anchored to the world: the waves stay where they are while the window moves, and cells that enter the window start flat. Rays fade from the simulated surface to the flat one from 30% to 44% of the window's width away from the camera.
- The grid has no height. Animated water at any height along the up axis ([rtx.zUp](../RtxOptions.md)) shows the waves at its place, and a disturbance moves the water at every height at its place, so water stacked at the same place shares its waves.
- Each fixed step of 1/60 s (at most 4 a frame) advances an explicit wave equation with [waveSpeedMetersPerSecond](../RtxOptions.md). The waves fade and the surface settles back over [rippleLifetimeSeconds](../RtxOptions.md). A disturbance gives the water a downward velocity, so the dent forms over the next steps and springs back as waves. Heights are clamped at [maxHeightMeters](../RtxOptions.md).
- One compute pass a step writes the state and, on the last step of the frame, a display texture that the ray tracing passes sample.

## Rendering

- **Normals and parallax.** Translucent animated water takes the slopes of the height field on top of its normal maps. A parallax walk along the view ray (up to [parallaxMaxSteps](../RtxOptions.md) samples at grazing angles and a quarter of them looking straight down, bounded by the highest recent wave) finds where the ray meets the displaced surface, so wave crests hide the troughs behind them. The texture coordinates of the water material are shifted the same way, and refraction starts at the displaced surface.
- **Wind waves** ([rtx.waterSim.detail](../RtxOptions.md)). A wave generator replaces the material's scrolling normal maps on horizontal water: two tiles of an FFT wave spectrum (wave slope, peak wavelength and its period, direction and spread, a long swell), remade every frame and looping after 10 minutes. Large patches of calmer and rougher water come and go like gusts. The longest waves also go into the height field, so they get parallax.
- **Droplet rings** ([rtx.waterSim.rings.*](../RtxOptions.md)). A tiling texture of drops ([rtx.waterSim.rings.texture](../RtxOptions.md); per texel the distance from the drop center over its radius, the direction from the center and a phase) makes rings where the spray of steep disturbances lands, and everywhere with [rain](../RtxOptions.md). Behind each ring's front runs a short train of smaller crests. The spray is a field of its own in the simulation: thrown by disturbances deeper than 0.3 of their radius, it lands in a Gaussian around them over 0.3 s and fades over 1.5 s.

Only the G-buffer passes read the simulation (`WATER_SIM_SAMPLE`). The other passes, and the hit and miss shaders of the ray pipelines, see flat water.

## Shores and silhouettes

[rtx.waterSim.silhouettes](../RtxOptions.md) lets wave troughs be hidden by what stands between them and the flat water mesh, such as banks, pillars and props. When the parallax hit of a wave trough lies behind the mesh, a short ray from the mesh to that hit looks for opaque geometry. If it finds some, resolving continues past the water, and that surface shows instead. A POM surface that turns out to lie deeper than the water surface (the water fills the gaps between stones) sends resolving back to the water. A surface found in air under the water mesh gets its lighting without the water mesh in the way.

With silhouettes, the simulated surface rests [restDepthMeters](../RtxOptions.md) below the water mesh, so that the water meets the shore where the shore crosses that level instead of at the mesh's edge.

[rtx.displacement.silhouettesBehindTranslucency](../RtxOptions.md) does the same for opaque POM surfaces seen through water or other thick translucency: a short ray from the low poly hit to the POM hit finds where the view leaves the surface through its side, or what stands in its carved out part.

## Sludge

[rtx.sludgeTextures](../RtxOptions.md), or `REMIXAPI_INSTANCE_CATEGORY_BIT_SLUDGE` for instances drawn through the API, tags opaque surfaces that float on water, like the slime of toxic pools. With [rtx.waterSim.sludge.enable](../RtxOptions.md), every disturbance also clears the sludge around it: a ray that hits cleared sludge finds open water instead, a translucent material of its own ([rtx.waterSim.sludge.water*](../RtxOptions.md)), with ragged edges. The holes ([holeSize](../RtxOptions.md)) close from their edges over [holeLifetimeSeconds](../RtxOptions.md). Sludge that stays rides the waves.

