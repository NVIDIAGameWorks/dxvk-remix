# Agentic Remix

Agentic Remix lets an agent act on what the player sees in a running game, enabling the player to 
edit a mod while playing. The player can ask, for example: "what asset am I looking at?", or perform
actions like "select the box on the left and frame it in the toolkit viewport", and "make this door red".

The runtime's part is to describe the scene when the agent asks. The Toolkit uses that description
to identify objects and make tool calls. This page covers the runtime's side.

## Visible Set Dump

Describes every live instance in the scene to an out-of-process consumer: its identity, which
cameras drew it, whether it is in the main camera's view, where the frame drew it and which instance
is under the screen center. A request can instead ask for only what the main camera can see, and can
ask for the image that says which instance each pixel shows.

Enable it with `rtx.visibleSetDump.enable`, which is off by default, or with the
`RTX_VISIBLE_SET_DUMP_ENABLE` environment variable. While it is enabled, the renderer computes a
bounding box for every draw, as it does by default for the NEE cache, so that culling has bounds.
Nothing else happens until a consumer asks. A request is a Windows message. The answer is a JSON
file written next to the log a few frames later, and one line in the log that names it.

### Requesting a snapshot

```cpp
UINT   msg    = RegisterWindowMessageW(L"UWM_REMIX_VISIBLE_SET_DUMP");
HWND   hwnd   = FindWindowExW(HWND_MESSAGE, nullptr, L"RemixVisibleSetDump", nullptr);
// requestId is any uint32 the consumer picks. The flags and the detail level are optional.
WPARAM wParam = (WPARAM(detail) << 40) | (WPARAM(flags) << 32) | requestId;
PostMessageW(hwnd, msg, wParam, 0);
```

The low half of `wParam` is the request id, which the answer echoes so a consumer can match its own
answer and ignore snapshots requested by anyone else. Bits 32 to 39 hold flags:

| Flag | Value | Meaning |
|---|---|---|
| In view only | `1` | List only what the main camera can see. See [Which instances are listed](#which-instances-are-listed). |
| Save image | `2` | Also save the frame's surface-index image. See [The surface-index image](#the-surface-index-image). |

Bits 40 to 47 set how much of each instance the answer holds:

| Detail | Value | Holds |
|---|---|---|
| Summary | `1` | The ids, the original hashes, the replacement, and whether the instance is hidden or traced. Use it to list everything cheaply. |
| Standard | `2` | Adds positions, the transform, material and geometry facts, the remaining flags, categories and cameras. |
| Full | `3` | Adds the renderer's own bookkeeping. Most of it only means something inside the renderer, and it makes the answer almost twice as large. |

A 0 uses `rtx.visibleSetDump.detailLevel`, which is Standard by default. The option counts from 0,
so its values are one lower than the request's.

A request is a single `PostMessage`, which carries nothing but `wParam` and `lParam`, so every
parameter is packed into `wParam`. `lParam` is not used.

`WPARAM` is 64-bit in 64-bit processes, and `PostMessage` between two 64-bit processes passes it
unchanged. The renderer always runs in a 64-bit process: the game for 64-bit titles, or
`NvRemixBridge.exe` for 32-bit titles through the bridge. A 32-bit consumer can only send an id.

The same request from Python, asking for what is in view and for the image:

```python
import ctypes
from ctypes import wintypes

user32 = ctypes.WinDLL("user32")
user32.FindWindowExW.restype = wintypes.HWND
user32.FindWindowExW.argtypes = [wintypes.HWND, wintypes.HWND, wintypes.LPCWSTR, wintypes.LPCWSTR]
user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]

msg = user32.RegisterWindowMessageW("UWM_REMIX_VISIBLE_SET_DUMP")
hwnd = user32.FindWindowExW(wintypes.HWND(-3), None, "RemixVisibleSetDump", None)  # -3 is HWND_MESSAGE
user32.PostMessageW(hwnd, msg, (1 | 2) << 32 | request_id, 0)
```

`RegisterWindowMessage` returns the same id in every process for a given name, and the window is
found by class name, so a consumer needs no handle, pid or port from the runtime. The window is a
message-only window created by the renderer, in the process that owns the scene.

### Reading the answer

The snapshot is written to `remix-visible-set.json` in the log folder, `rtx-remix/logs` by default.
Each request replaces the file. Each top-level member and each instance is on a line of its own,
so searching the file for a hash returns that instance's whole entry.
Once the file is complete, one line goes through the normal logger,
which writes it to both `remix-dxvk.log` and the standard error stream:

```text
[14:22:31.507] info:  RemixVisibleSet id=4242 {"file":"C:\\Games\\Portal\\rtx-remix\\logs\\remix-visible-set.json"}
```

Match the literal marker `RemixVisibleSet` and the `id=` field first, then parse the rest of the
line as JSON. `file` is the path of the snapshot. When the file could not be written, the line
holds `error` instead of `file`. Read the file after seeing the line, and before sending another
request, because the next answer replaces it. The snapshot's `requestId` says which request a file
answers.

A consumer can tail the log file, which works no matter who launched the game, or capture the
process's standard error, which only the process that launched it can do. Both receive the same
line.

### Response body

| Field | Meaning |
|---|---|
| `schemaVersion` | Payload version, currently `1`. |
| `requestId` | The id of the request the snapshot answers. |
| `frameId` | The main camera's frame that the snapshot describes. |
| `totalInstances` | How many instances were listed before the `maxInstances` cap. |
| `inViewOnly` | Whether the request asked for only what the main camera can see. |
| `detailLevel` | The detail level the instances were written with: `0` Summary, `1` Standard, `2` Full. |
| `cameras` | An object keyed by camera type name, each with its `worldPos`. `Main` is always present. `ViewModel`, `Portal0`, `Portal1`, `Sky` and `RenderToTexture` are present when that camera was updated on the snapshot's frame. |
| `center` | Present when the frame was read back. `hit` is the entry of the instance under the center pixel, and is absent when the center shows no geometry, such as sky. |
| `drawn` | Present with `center`. `size` is the render resolution the drawn pixels were counted at. |
| `surfaceImage` | Present when the request asked for the image and it was saved. See [The surface-index image](#the-surface-index-image). |
| `instances[]` | One entry per listed instance, nearest first. |

Each entry of `instances`, and `center.hit`, holds two objects. `instance` describes the instance
itself, and is what `RtInstance::printDebugInfo` logs, indented. `screen` says where the instance is on the
snapshot's frame, as the main camera sees it.

```json
{
  "instance": {
    "instanceId": 1234,
    "drawCallId": 57,
    "hashInfo": { "meshHash": "e29c846546b4b268", "materialHash": "5d1a8c0e7f3b2a91" },
    "replacement": { "replaced": true, "primPath": "/RootNode/meshes/mesh_E29C846546B4B268/Box" },
    "flags": { "hidden": false, "traced": true }
  },
  "screen": {
    "distance": 212.5,
    "rectNdc": [-0.4, -0.3, 0.1, 0.2],
    "inFrustum": true,
    "drawn": { "pixels": 5310, "rectNdc": [-0.38, -0.3, 0.1, 0.18] }
  }
}
```

The members of `instance`, by the lowest detail level that holds them:

| Member | Level | Meaning |
|---|---|---|
| `instanceId` | Summary | The renderer instance's id, stable for as long as the instance lives. Absent for renderer-created instances. |
| `drawCallId` | Summary | The instance's object-picking value, which the Remix API picking functions use. For a game draw it is the draw's ordinal within its last frame, so it only means something on the snapshot's frame. Every prim of one replacement shares it. A Remix API instance keeps the value its caller set. |
| `hashInfo.meshHash`, `hashInfo.materialHash` | Summary | The original draw call's hashes, as 16-digit lowercase hex. |
| `replacement.replaced` | Summary | Whether a mesh or material replacement is active for the instance. |
| `replacement.primPath` | Summary | The USD path of the replacement mesh prim the instance draws, when it has one. |
| `flags.hidden` | Summary | Whether the instance is hidden from ray tracing, as sky-cubemap draws are. |
| `flags.traced` | Summary | Whether the instance is in the ray-traced scene, through its geometry or its billboards, so primary rays can hit it. |
| `ageFrames` | Standard | Frames from the renderer creating the instance to its last update. For an instance drawn on this frame, `frameId - ageFrames` is the frame it was created on. |
| `transform.worldPosition`, `transform.prevWorldPosition` | Standard | The instance's position now and at its previous update. They differ when the game moved the instance, and are equal for a static draw and after a teleport. Skinned and vertex-shader animation leave them unchanged. |
| `transform.world` | Standard | The object-to-world matrix, 16 floats in column-major order. |
| `transform.prevWorld` | Full | The object-to-world matrix at the instance's previous update, in the same form, which also shows a rotation. |
| `material.alphaBlend`, `material.alphaTest` | Standard | Whether the draw blends with what is behind it, and whether it uses an alpha test. |
| `geometry.linkedBlas` | Standard | Whether the instance has geometry in the ray-traced scene. The hash and the counts below are present only when it has. |
| `geometry.albedoTextureHash` | Standard | The hash of the draw's color texture, as 16-digit lowercase hex. |
| `geometry.vertexCount`, `geometry.indexCount` | Standard | The size of the geometry the renderer traces. |
| `flags.isPlayerModel` | Standard | Whether the instance is part of the player's third-person model. |
| `flags.createdByRenderer` | Standard | Whether the renderer created the instance, such as a view-model copy, rather than the game drawing it. |
| `flags.animated` | Standard | Whether the instance's material animates, with a sprite sheet or a scrolling texture. |
| `viewModel.isViewModel` | Standard | Whether the instance is part of the player's view model. |
| `categoryFlags` | Standard | The instance's categories. Bit *n* is value *n* of `InstanceCategories` in `rtx_types.h`. |
| `cameras` | Standard | The camera types that drew the instance on this frame. |
| `vectorIndex`, `frames`, `vulkan`, `surface`, `billboards`, `indexOffsets`, and further members of the groups above | Full | The renderer's own bookkeeping, such as the frames the instance was created and last updated on, its other hashes, its TLAS instance fields, its bindless slots and its surface indices. `surface.index` is the instance's value in the surface-index image, and is left out for an instance that is not in the frame's surfaces. |

The members of `screen`:

| Member | Meaning |
|---|---|
| `distance` | Distance from the main camera to the center of the instance's bounding box, or to its position when the draw has no box. |
| `rectNdc` | The bounding box projected to `[x0, y0, x1, y1]` in NDC with +y up, clamped to ±2. Absent when the draw has no box or the box could not be projected. |
| `inFrustum` | Whether the bounding box intersects the main camera's frustum. True for a draw without a box. Absent while the request asks for in-view instances only. |
| `drawn.pixels` | Pixels whose first hit is the instance. `drawn` is present when the snapshot has `drawn`. |
| `drawn.rectNdc` | The bounds of those pixels, in the same form as `rectNdc`. Present when `drawn.pixels` is not 0. |
| `surfaces` | The values the instance's pixels hold in the surface-index image. Present when the snapshot has `surfaceImage`. |

Optional members are omitted rather than written as null, and a group without members is omitted
too. A float that is NaN or infinite is written as `null`, never as a plausible `0`.

### Which instances are listed

By default the snapshot lists every live instance, meaning every instance in the instance table
except those waiting to be garbage-collected. That includes hidden sky-cubemap draws, the game's own
view-model instances, which are not traced, and the copies the renderer creates of them.

With the in-view flag, an instance is listed when the frame drew any of its pixels, or when it is
traced and its bounding box is in the main camera's frustum. The frustum test keeps instances that
nothing on screen shows, such as geometry behind a wall or an object seen only through glass. These
report `drawn.pixels` 0. A draw without a bounding box may be anywhere, so it counts as in the
frustum. This happens for Remix API meshes and for draws prepared in the first frames after the
option is turned on.

Instances are sorted nearest first by `distance`, with ties kept in a stable order.
`rtx.visibleSetDump.maxInstances`, 512 by default, caps the list. The cap keeps drawn instances
first, then traced instances in the frustum, then the rest, each nearest first, and the result is
still sorted nearest first. Compare the length of `instances` with `totalInstances` to detect
truncation, or set the option to 0 to remove the cap.

### Identity

- `hashInfo.meshHash` and `hashInfo.materialHash` are the original draw call's identity, even for a replaced draw, so
  they match capture prims, tags and replacement lookups. Capture prim names (`mesh_<HASH>`) use
  uppercase hex. `meshHash` can change from frame to frame while `instanceId` holds, for meshes the
  game animates on the CPU, such as skinned characters.
- `instanceId` names one renderer instance for as long as it lives, and is the one to key on. The
  renderer recreates some instances every frame, such as particles and geometry the game rebuilds
  each frame, and each new instance gets a new id. View models, player-model clones and ray-portal
  virtual instances are created by the renderer and have no id.
- `ageFrames` stays near 0 for an instance that keeps being recreated. A large age shows how long
  the id has held so far, which is evidence of stability, not a guarantee.
- `replacement.primPath` is the path in the composed mod stage. It tells apart the prims of a
  multi-prim replacement, which share a `meshHash`. It is absent for draws without a mesh
  replacement, which map to the capture's `mesh_<HASH>` and `inst_<HASH>_<n>` prims.

### Cameras

An instance's `cameras` lists the camera types the game drew it with on this frame. `Sky` means it
was drawn into the sky, `ViewModel` that it belongs to the player's view model and `RenderToTexture`
that it was drawn into a ray-traced render target, such as a security monitor's screen. An empty
list means the renderer kept the instance, for example for anti-culling, without the game drawing it
on this frame. Everything in `screen` always refers to the main camera.

### Where an instance is drawn

`screen.drawn` says where the frame actually shows each instance, which its box cannot. A world
mesh's box often spans the whole room, so its `screen.rectNdc` is the whole screen wherever it is
drawn.

- `drawn.pixels` counts first hits at render resolution. It is 0 for an instance whose pixels are
  all covered by something nearer, including glass, particles and decals in front of it, and for an
  instance that is not traced.
- `drawn.rectNdc` bounds those pixels. It does not say how they are spread, so one mesh drawn at
  both edges of the screen has a rect that spans the middle too. The surface-index image answers
  that.
- The counts are per instance, so each prim of a multi-prim replacement reports its own pixels.

### The surface-index image

A request with the save-image flag also writes `remix-visible-set-surface-index.bin` to the log
folder, and each request that asks for it replaces the file. It holds, for every pixel at render
resolution, which instance the primary ray hit first, so a consumer can answer "what is at this
point" or "what is in this region" exactly, for any point or region, without asking the runtime
again.

- The file is two little-endian `uint32` values, the width and the height, then width × height
  little-endian `uint32` surface indices, row by row from the top left.
- The snapshot's `surfaceImage` gives the file's `path`, the `headerBytes` before the first pixel,
  and `noHit`, the value of a pixel that shows no geometry.
- Each entry's `screen.surfaces` lists the values that belong to that instance. An instance can
  have several, and a value that no listed entry holds belongs to an instance the request filtered
  out or the cap dropped.
- The values are only meaningful with the snapshot written by the same request.

### What is under the center

`center.hit` names the instance whose surface the primary ray hits at the center pixel of the same
frame, so a consumer does not have to guess it from the rects that cover the center. The primary ray
stops at the first surface, so through glass, a mirror or a portal, `hit` names that surface itself.

### Other fields

`replacement.replaced` reads false for everything while `rtx.enableReplacementMeshes` and
`rtx.enableReplacementMaterials` are off. `screen.rectNdc` is clamped to ±2, because geometry that
straddles the camera plane projects arbitrarily far outside the frame, and nothing past the `[-1,1]`
edge tells a consumer more than "past that edge".

### Timing and cost

- The request is read at the end of a ray-traced frame. The snapshot describes the frame whose
  first-hit image was copied, and is written once its read-back lands, typically four or five
  frames after the request. If the read-back has not landed after twenty frames, the snapshot is
  written without `center` and `drawn`. If the game is not rendering, the request is answered when
  it resumes.
- Nothing is allocated or written until a request arrives. One request reads back 4 bytes per
  render pixel, about 8 MB at 1080p, once.
- One request is answered at a time. A request that arrives while another is answered waits for it,
  and if several arrive, only the latest is answered.
- A frame on which replacements finished loading or changed clears the scene part-way through. The
  request is left queued rather than answered with an empty set, so it costs one more frame.
- No answer at all means the dump is disabled, the game is not rendering, the request window could
  not be created, which is logged once as a warning, or the log level is above `info`. The line
  that names the snapshot is an `info` line, so `DXVK_LOG_LEVEL=warn` and above discard it, although
  the file is still written.

### Known limitations

- **Only first hits count.** An object seen only through glass, a mirror or a portal reports 0
  pixels, and `center.hit` names the glass.
- **PointInstancer batches count once.** A replacement that scatters many copies through a
  PointInstancer is reported as one instance at the batch's base transform, because the per-copy
  transforms live outside the instance's own transform.
- **Third-person player models can appear.** They carry a non-zero ray mask, so they are traced even
  when `rtx.playerModel.enableInPrimarySpace` is off and no primary ray hits them. They sit at the
  camera, so they sort first.
- **GPU-skinned meshes use a conservative box.** Their bind-pose box does not follow the bones, so
  it is widened to cover the bind pose under every bone the draw uses. Their `screen.rectNdc` can be
  larger than the posed mesh, and their `distance` is measured to the widened box.
- **Off-center projections are approximated by the frustum test.** The frustum is rebuilt from fov
  and aspect, which can only describe a symmetric frustum, while `screen.rectNdc` comes from the
  game's real projection. An asymmetric or lens-shifted projection can disagree at the frame edge.
  `screen.drawn` is unaffected.

### Notes for maintainers

#### How an answer is built

- `RtInstance::asJson` writes an instance as one JSON object, for the dump and for
  `RtInstance::printDebugInfo`, which logs it across lines. In developer builds
  `rtx.dumpAllInstancesOnFrame` prints every instance this way, at `rtx.visibleSetDump.detailLevel`. It builds the object from the
  `asJson` helpers in `util_string.h`, `util_vector.h` and `util_matrix.h`. A new member goes in the
  group it belongs to, at the lowest `InstanceDetailLevel` whose consumers can use it.
- A value that needs the frame's camera or read-back is not part of the instance. It belongs in the
  `screen` object, which `VisibleSetExporter` fills and `rtx_visible_set_serialize.h` writes.
- Each record keeps the instance's JSON and the few values the dump filters and sorts on, because
  the read-back arrives frames after the instances are described, when they may be gone.
- A request creates the first-hit surface-index image, at render resolution. The G-buffer writes
  each pixel's first hit to it only while it exists, and it is destroyed once copied. It does not
  need the object-picking image, and the dump never changes an instance's picking value, so
  nothing runs per frame or per instance while no request waits.
- `reduceDrawnCoverage` counts each surface's pixels on a worker thread. The exporter maps each
  surface to its instance with `AccelManager::getOrderedInstances`, and adds up the surfaces of an
  instance that spans several.
- Surface indices are only valid on the frame that wrote them, so the exporter describes the
  instances on the frame the image was copied from.
- The worker thread saves the image file before it reports the read-back, and `emit` writes the
  snapshot file before it logs the line, so both files are complete when a consumer sees the line.
- Each read-back runs on a thread of its own and they all save to one image file. The exporter marks
  a read-back it stops waiting for as abandoned, and the workers save the image under one lock and
  skip an abandoned one, so a late read-back never replaces the image of a later request.

#### Pitfalls

- **`traced` must count instances that rays hit only through billboards.** Player-model instances
  with particles have a geometric mask of 0. `AccelManager` marks them when it submits their
  billboards. The in-view filter keeps undrawn instances only when they are traced, so a wrong
  `traced` drops them from in-view answers.
- **Every write to the object-picking image needs its surface-index twin.** The helpers in
  `gbuffer_bindings.slangh` write both images. A new G-buffer path that writes a picking value
  directly makes `screen.drawn` and `center.hit` miss that hit.
- **`replaced` comes from the replacements the renderer drew.** `RtInstance::asJson` reads the
  owner's `activeReplacements`, or the copy a clone carries. A lookup by `meshHash` misses
  legacy-hash and variant matches.
- **The frustum test is exact and built from fov and aspect.** `boundingBoxIntersectsFrustum`
  reports some rotated boxes as visible, and `RtCamera::getFrustum()` is widened for anti-culling
  and only updated while `rtx.antiCulling.object.enable` is on.

#### External contract

The transport is a window message, a log line and files in the log folder, because the Remix API needs a host that links
`remixapi`, which an unmodified D3D9 title does not have. Consumers depend on the marker string, the
window class, the message name, the request encoding and the image file's layout, so changing any of them is a breaking
change. A change to the JSON layout needs a new `schemaVersion`.
