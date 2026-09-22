# Bridge Input Neutralization

## How it works

While the Remix UI is active, `bridge/src/client/di_hook.cpp` and `window.cpp` stop the game from
seeing real mouse/keyboard input, so its camera doesn't move underneath the menu. A game can read
input through several independent OS/DirectInput APIs, so each one is hooked and neutralized on
its own — there is no single choke point.

Cursor position is not simply frozen: the game is echoed whatever position it last told the OS via
`SetCursorPos` (`g_uiActiveCursorPos`), so a recenter-driven mouse-look loop (read cursor, compute
delta from window center, `SetCursorPos` back to center) always computes a zero delta. On UI close,
the OS cursor is restored to that same echoed position — not wherever it was before the UI opened —
so the game's first post-close delta is also zero, even if its recenter target moved while the menu
was open (e.g. after a resolution change made from the menu itself).

## Supported input types

- **Cursor recenter loop** (`GetCursorPos`/`SetCursorPos`) — echoes the game's own recenter target.
- **Raw input, per-message** (`GetRawInputData`) — zeroes relative mouse deltas and button
  transitions; replaces the raw keyboard report with the last known-neutral sample.
- **Raw input, batched** (`GetRawInputBuffer`) — used by some engines (e.g. Source) to poll
  accumulated raw input directly, bypassing `GetRawInputData`. Drained so it doesn't pile up
  and replay once the UI closes, but reported to the caller as empty.
- **DirectInput, polled and buffered** (`GetDeviceState`/`GetDeviceData`) — device state and
  buffered events are wiped after being captured for tracking purposes.
- **Window messages** (`RemixWndProc`) — messages classified as input (`WM_MOUSEMOVE`,
  `WM_KEYDOWN`, `WM_INPUT`, etc.) are swallowed rather than forwarded to the game's own WndProc.

## Known limitations

- Raw input button/key state is edge-encoded (a report carries a transition, not a level), but
  neutralization zeroes/replaces whole reports rather than synthesizing the correct up-transition.
  A control released while the UI is open generates no release event once it closes, so a consumer
  tracking state purely from transitions can read it as still held.
- `GetRawInputBuffer` drops whole reports rather than sanitizing them in place, for every report
  type and regardless of DirectInput usage — coarser than `GetRawInputData`'s per-field zeroing.
  Per-report rewriting of the batched buffer was judged more complex and risk-prone than justified
  here (WOW64 layout, `NEXTRAWINPUTBLOCK` alignment), but is the natural next step if a title needs
  it.

## Previously incorrect approaches

- **Bailing out of all raw-mouse handling whenever a DirectInput device existed.** The reasoning
  was "don't touch raw input for DirectInput titles," but Source-engine games commonly create a
  DirectInput device for a controller while still driving mouse-look through raw input — so this
  bail-out silently disabled neutralization for exactly the titles that needed it, causing the
  camera to spin continuously when the menu opened.
- **Resampling the live OS cursor position on UI activation.** Activation is handled off the
  game's own input frame, so a live resample injects a one-time delta the game hadn't produced
  itself, snapping the camera the instant the menu opened.
- **Restoring the OS cursor to its pre-UI position on close, unconditionally.** This ignored any
  recenter target the game set while the UI was open, so the game's first post-close delta was
  measured against a position it had never been told about. It would also warp the cursor to
  (0, 0) for a title that never calls `GetCursorPos`/`SetCursorPos` at all — that global is never
  written to any real position without at least one game- or fallback-driven sample first.
