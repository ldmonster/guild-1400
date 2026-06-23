# Wave-13 1:1 audit — the render frame spine (W13-FRAME)

MCP was DOWN for this wave, so this is the MCP-free half of the 1:1 comparison:
cross-check each frame-spine function against the in-tree evidence (provenance
comments + progress docs), PIN its recovered 1:1 values with golden tests, and
produce a confidence map. The Hex-Rays decompile remains the reference of record;
a live binary diff for the NEEDS-LIVE-MCP items is listed at the end.

SEGMENT (render-side frame spine, NOT city_view3d / universe_render / scene_view):
- `src/render/frame.{h,cpp}`     — the per-frame top-level orchestration
- `src/render/meshlist.{h,cpp}`  — the sorted draw-list flush + dispatch table
- `src/render/clip.{h,cpp}`      — the Sutherland-Hodgman polygon clipper
- `src/render/object_project.{h,cpp}` — the per-object perspective project + cull

`object_light_shade` and its tests are a separate segment (skipped, per brief).

---

## 1. Inventory (function ↔ gilde.exe provenance)

| Function | Address | File | Provenance comment? |
|---|---|---|---|
| `RenderMainViewFrame`   | `0x5B6074` | frame.cpp:5  | YES |
| `RenderUniverseFrame`   | `0x5B3DE8` | frame.cpp:27 | YES |
| `BeginUniverseFrame`    | `0x5B3900` | frame.cpp:35 | YES |
| `DrawUniverseAndStats`  | `0x5B3BBC` | frame.cpp:106 | YES |
| `RasterizeMeshList`     | `0x5AEC88` | meshlist.cpp:137 | YES |
| `SpanFillNullStub`      | `0x5F6EE8` | meshlist.cpp:44 | YES |
| `RasterTri` (host leaf adapter) | — | meshlist.cpp:60 | host adapter (slots 3/4) |
| `SpanDispatch::SpanDispatch` (table init) | `0x5AF984` (InitEngineDevice writer) | meshlist.cpp:116 | YES |
| `present_shim::AcquireBackBuffer` | `0x4345D4` | meshlist.cpp:23 | YES (boundary, shimmed) |
| `present_shim::UnlockBackBuffer`  | `0x434680` | meshlist.cpp:24 | YES (boundary, shimmed) |
| `ClipPolygonToPlane`    | `0x5AD7D8` | clip.cpp:30 | YES |
| `ProjectObjectVertices` | `0x5AC970` | object_project.cpp / .h | YES |

NO red flags: every reconstructed function carries a `// gilde.exe 0x… —` header.

Adjacent values pinned by the segment but OWNED elsewhere (cross-referenced, not
edited here):
- The draw-list SORT KEY `768 * max(vertex +66 light idx)` is built in
  `ProjectVerticesToScreen @0x5C5120` (scene.cpp, another segment). meshlist only
  CONSUMES the high byte of the *dispatch* key (`v45[1] >> 24`, value 3 or 4) —
  a DISTINCT key from the sort key (meshlist.h is explicit about this).
- `RadixSortDrawList @0x5AEF34` lives in scene.cpp (not this segment).

---

## 2. 1:1 value pins (golden tests)

### NEW this wave — `tests/unit/render_frame_spine_test.cpp` (suite RenderFrameSpine, 55 checks)
The frame spine (frame.cpp) had NO dedicated unit golden before — only e2e/
integration drivers exercised it indirectly. This pins its 1:1 control flow and
arithmetic, every value sourced from the frame.{h,cpp} provenance comments:

- **BeginUniverseFrame** — the EXACT recovered subsystem call ORDER:
  `clear → terrain → resetLights → sceneWalk → particles → skyFlares → mirrors`,
  the `a2` flag threaded into every a2-taking hook, the `sceneWalk` return
  snapshotted into `appendedPolys`, and the reentrancy guard balanced (`++`/`--`).
- **BeginUniverseFrame** — clear-selection branch (`useViewportClear` →
  ClearViewport vs ClearRect), the `hasTerrain` gate on the terrain hook.
- **BeginUniverseFrame** — running depth-bound reset (`runningFar=0`,
  `runningNear=1e10`) + the **1e10 near-sentinel collapse to 0.0** (`flt_13FD168[0]`
  sentinel fix-up) when nothing expanded it.
- **BeginUniverseFrame** — gates: engine-off / no-world / nonzero-reentrancy all
  skip the body (no hooks fire); the negative-reentrancy clamp to 0 runs first.
- **DrawUniverseAndStats** — fps WINDOW A: fires only when `dt > 0x3C` (60),
  `value = 1000*frames/(dt*uDelay)` integer division, accumulator + last-time
  reset on report, no report below threshold.
- **DrawUniverseAndStats** — fps WINDOW B: fires when `dt > 0xA` (10), same
  integer-division form.
- **DrawUniverseAndStats** — `uDelay` divides the denominator (golden: 1000*1/
  (100*4)=2 int-div).
- **DrawUniverseAndStats** — `a3` gates the anim pose walk; `a4` bumps the frame
  counter; `a2=0` suppresses the entire fps/anim block; the engine/world gate
  returns the `appendedPolys` snapshot unchanged and does NOT bump `a4`.
- **RenderUniverseFrame** — composition Begin→Draw, returns the appended-poly
  snapshot (8 hook calls = Begin's 7 + Draw's anim).
- **RenderMainViewFrame** — entry gate (`engineOn && reentrancy<=0`) and the
  three-way clear selection incl. the `clearSuppressed` skip (exactly one clear
  in the trace when suppressed).

### EXISTING — `tests/unit/render_clip_test.cpp` (suite RenderClip, 48 checks) — verified intact
- CLIP straddle-near-plane **4-vertex golden** (v0,v1,(3,2,1),(1,2,1)) — the brief's
  4-vertex straddle golden.
- CLIP colour-byte lerp at the two new vertices (t=0.5 → 150 and 120).
- CLIP fully-inside (3 ptrs unchanged) / fully-outside (0) / zero-plane / empty.
- CLIP near-capacity (120 verts) no-overflow (ASAN boundary).
- DISPATCH **6-slot table** all-NullStub default, slot[3]=blend / slot[4]=opaque,
  the `key>>24` selector (0x04000000→4, 0x03000000→3), NullStub returns continue.
- REPROJECT scalars via the flush: `screenX=D0C*x*(1/z)+D18`,
  `screenY=AF8*y*(1/z)+D10` (golden D0C=2,D18=320,AF8=2,D10=240 → apex 320,240).
- FLUSH empty/null-poly/direct-translucent-slot-3 edge cases.

### EXISTING — `tests/unit/object_project_test.cpp` (suite ObjectProject, 10 checks) — verified intact
- Perspective divide `screenX=xScale*x/z+xOffset`, `screenY=yScale*y/z+yOffset`.
- The `+76 & 0x80` visible gate (+ projectAll override).
- The projected signed-area backface cull (front kept / back culled / `+38&4`
  no-cull keeps a back-facing poly).

---

## 3. Internal-consistency check (drift)

No code-vs-comment drift requiring a fix was found. Notes:

- **frame.cpp fps WINDOW A vs WINDOW B numerator (stylistic, behavior-identical).**
  Window A computes `framesA = fpsFrameAccA + 1` (pre-increment snapshot) then
  `++fpsFrameAccA`, using `framesA` as the numerator. Window B uses the
  post-increment `fpsFrameAccB` directly. After the `++`, `fpsFrameAccA == framesA`,
  so BOTH numerators equal the post-increment accumulator — there is **no
  behavioral difference**; only the spelling differs. The provenance comment shows
  both windows as `++…E0`/`++…E8` then `1000*frames`, consistent with the
  post-increment count. NOT a drift; documented for clarity. (A live decompile
  would confirm the exact source spelling — see §4.)

- **clip.cpp address.** clip.{h,cpp} carry `0x5AD7D8`; the wave-13 brief text
  named "ClipPolygonToPlane" without an address. The in-tree provenance and the
  universe-render-chain doc both use `0x5AD7D8` — consistent.

- **meshlist dispatch table.** Source, header, and render_clip_test agree: SIX
  slots (`dword_13D8780[0..5]`), all NullStub in the binary (only writer
  `InitEngineDevice @0x5AF984`), `dword_13D8798` is the clip-input array not a 7th
  slot. slots 3/4 wired to the host textured leaf is a documented HOST CHOICE
  (rule 3 software flush), not a 1:1 binary value. Consistent.

- **RenderMainViewFrame call literal.** frame.cpp:24 comment annotates
  `RenderUniverseFrame(64,1,1)` (the original a1=64 / a2=1 / a3=1); the
  reconstruction drops the opaque `a1=64` (an engine handle, modeled by FrameState/
  hooks) and threads `a2=1,a3=1`. Documented in the header; behavior-identical.

---

## 4. Confidence map

GOLDEN-PINNED (constants + control flow tested → high 1:1 confidence):
- `ClipPolygonToPlane` `0x5AD7D8` — 4-vtx straddle, colour lerp, inside/outside,
  capacity. The plane math, inside test (`dot>=d`), edge `t=(d-dotPrev)/(dotCur-
  dotPrev)`, and byte-truncated colour lerp are all pinned.
- `RasterizeMeshList` `0x5AEC88` — dispatch selector `key>>24`, the 6-slot table,
  clip-vs-direct branch, the clipped-vertex reproject scalars, the triangle-fan,
  empty/null edge cases.
- `SpanDispatch` table init (`0x5AF984` writer) — 6 slots, NullStub default,
  slot 3/4 host wiring (the host-choice caveat documented).
- `SpanFillNullStub` `0x5F6EE8` — draws nothing, returns continue.
- `ProjectObjectVertices` `0x5AC970` — perspective divide, +76 gate, signed-area
  cull (incl. +38&4 no-cull).
- `RenderMainViewFrame` `0x5B6074` — gate + 3-way clear selection (NEW pin).
- `RenderUniverseFrame` `0x5B3DE8` — Begin→Draw composition + return (NEW pin).
- `BeginUniverseFrame` `0x5B3900` — subsystem ORDER, clear branch, depth reset +
  1e10 sentinel collapse, all four gates, a2 threading (NEW pin).
- `DrawUniverseAndStats` `0x5B3BBC` — both fps windows (thresholds 0x3C/0xA,
  int-div, uDelay divisor), a2/a3/a4 gates, frame counter, gate-return snapshot
  (NEW pin).

NEEDS-LIVE-MCP (1:1 fidelity needs a binary diff when MCP returns):
- **frame.cpp BeginUniverseFrame — the elided per-frame poly-counter resets.**
  The comment lists `*(obj+492)+252/+256 = 0` and `dword_64A060/58/13FCF3C` and
  `flt_13FD168[0]` writes; the reconstruction models the depth bounds + appended-
  poly snapshot but NOT every counter. Live targets to confirm: `0x5B3900`
  (the resets between the clear and the terrain step), `dword_64A060`,
  `dword_64A058`, `flt_13FCF3C`.
- **frame.cpp DrawUniverseAndStats — ScrollUvCoords + the obj+492 counter resets.**
  The `t=dword_62EB38; ScrollUvCoords(t)` call at the top of the a2 block and the
  trailing per-frame poly-counter resets (`*(obj+492)+252/+256`,
  `dword_1408A64/68`) are summarized but not separately modeled/pinned. Live
  target: `0x5B3BBC`.
- **frame.cpp fps numerator spelling** — confirm window A vs B numerator source
  form at `0x5B3BBC` (behavior is identical either way; this is cosmetic-fidelity).
- **The clipped-vertex reproject `+76 >= 0` sign-gate** in RasterizeMeshList
  (`0x5AEC88`) — pinned indirectly via the flush reproject test, but the exact
  per-survivor sign-bit branch (`*(char*)(v+76) >= 0`) should be diffed.
- **DrawUniverseAndStats anim walk** — the nested 64-character-list WalkAndInvoke
  (a3-gated) is modeled as a single `updateAnim(a2)` hook; the 64-list iteration
  itself is in the anim/skeleton segment, not here. Live target for the loop:
  `0x5B3BBC` inner block.

UNDER-VERIFIED: none remaining in this segment after the new pins.

---

## 5. Build / test status

- New: `tests/unit/render_frame_spine_test.cpp` — suite RenderFrameSpine, **55
  checks, 0 failures**.
- Verified intact (byte-identical, no edits): `render_clip_test` (48/0),
  `object_project_test` (10/0).
- No source edits this wave (no evidence-backed drift found; the one stylistic
  fps-numerator note is behavior-identical and left as-is per the "fix only clear
  drift" rule). Did NOT edit any bind-site file or another segment's files; did
  NOT edit progress/INDEX.md.
- Build green (the `build/` configure picked up the new glob'd test).
