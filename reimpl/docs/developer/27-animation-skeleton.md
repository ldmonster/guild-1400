# 27 — Animation & skeleton

> Living, moving geometry in *Die Gilde* comes in two flavours, both driven by the
> same per-frame engine documented here. The first is **skeletal animation**: a
> character mesh (doc 26) carries up to three *bone tracks* per LOD layer, each
> playing a `.baf` *animation stream* whose keyframes store bone-local positions
> and Euler rotations; the pose driver advances each track's frame index, blends
> the in/out windows, builds the bone matrices and stamps the resulting bone poses
> back onto the scene-graph child objects. The second is **object/vegetation
> morph animation**: a single per-object keyframe channel (used by flags, blood
> pools, book pages, the grim reaper, swaying plants) advances along the *same*
> `.baf` keyframe table but interpolates the object's own transform with a
> **Catmull-Rom** spline and, for vegetation, rebuilds a per-vertex light cache.
> The deformed mesh's vertex normals are recomputed once at attach time by
> [`VIBE_Anim_CalculateAnimNormals`](#7-deformed-mesh-normals) and the posed,
> morphed vertices are fed straight into the lighting stage
> [`VIBE_Mesh_ComputeVertexLighting`](25-lighting-fx.md) (doc 25) and then the
> render-universe chain (doc 23).
>
> The original renders these meshes through DirectDraw / Direct3D fixed-function;
> in this port the projection/raster math is reconstructed 1:1 and only the GPU
> API underneath is **Vulkan** (rules 3). All file I/O goes through the engine VFS
> (doc 29). Nothing in the animation math itself is a tech swap.

All addresses are RVA-style absolute (imagebase `0x400000`); the binary
(`gilde.exe`, 32-bit x86) is the source of truth. Struct offsets below are
recovered from the accessor instructions in the decompilation, not from symbols.

Cross-links: [16 — Characters](16-characters-persons.md) ·
[17 — Character actions / AI](17-character-actions-ai.md) ·
[23 — Render universe chain](23-render-universe-chain.md) ·
[25 — Lighting & FX](25-lighting-fx.md) ·
[26 — Mesh, asset, LOD](26-mesh-asset-lod.md).

---

## 1. The pipeline at a glance

```
VIBE_Character_PreloadAniSetByName @0x403f14   build "character/%s/%s_%s.baf" paths
        └── VIBE_Anim_LoadStreamToStock        @0x5d3858   prefix "animations/", de-dupe
                └── VIBE_ModelIo_LoadBinaryAnimation @0x5e450c  ← the .baf PARSER (§2)
                        → AnimStream record (0x16C bytes)

VIBE_Anim_AttachToBone        @0x5d0b64   bind a stream to a bone-track slot
        └── VIBE_Anim_CalculateAnimNormals @0x5d0020   one-shot deformed normals (§7)

--- per frame, from the object update tree (doc 23) ---
VIBE_Anim_UpdateSkeletonPose  @0x5cd1d8   the 6.6 KB pose DRIVER (§4–§6)
        ├── VIBE_Texture_AdvanceAnimFrames @0x5daf78   (animated-texture sibling)
        ├── VIBE_Anim_AdvanceFrameIndex    @0x5ccf18   frame-index stepper (§3)
        ├── VIBE_Anim_ComputeBoneDelta     @0x5cba40   root-motion delta on settle
        ├── VIBE_Anim_SampleBoneTranslation@0x5cc920   accumulated translation
        ├── VIBE_Anim_InterpolateBoneFrame @0x5cbc10   per-frame translation (tangent meshes)
        ├── VIBE_Anim_ComputeFrameTangents @0x5ccf70   Hermite tangents for ping-pong
        ├── VIBE_Anim_ComputeBoneMatrices  @0x5cc0d0   collapse tracks → bone matrices → children
        ├── VIBE_Math_CatmullRomInterp     @0x5ca9d8   morph spline (§6)
        ├── VIBE_Anim_PruneExpiredAttachments @0x5d0d38 drop finished one-shots
        ├── VIBE_Anim_FindHighestPriorityLayer@0x5d0e84 pick LOD layer for the light cache
        └── VIBE_Light_BuildVegetationCache @0x5c8560   vegetation relight (§8)

--- deform + relight (called from the render walk, doc 23/25) ---
VIBE_Mesh_InterpolateMorphVertices @0x5c953c  apply bone morph weights to verts
        └── VIBE_Mesh_ComputeVertexLighting @0x5c9054  (doc 25)
```

Two helper queries underpin loading: `VIBE_Anim_FindFreeMeshSlot @0x5cf114`
(returns an existing stream of the same name, used both as a de-dupe check and as
a "find loaded stream" lookup) and the blend-vector helper
`VIBE_Character_ComputeAnimBlendVectors @0x428f38` (§6.4).

---

## 2. The `.baf` on-disk format

A `.baf` ("**b**inary **a**nimation **f**ile") is a **token-tagged binary chunk
stream**, parsed by `VIBE_ModelIo_LoadBinaryAnimation @0x5e450c`. It is opened
through the VFS (`VIBE_Vfs_OpenFile @0x450bc8`, mode `"rb"`), an unused 4-byte
magic is read, then a sequence of single-byte **tokens** is consumed with
`VIBE_Script_ReadToken @0x5e3bd0`; each token selects a reader. Multi-byte fields
go through the byte-swapping binary I/O helpers `VIBE_Bio_ReadDwordSwapArgs
@0x5dc8b0`, `VIBE_Bio_ReadByte @0x5dc850`, `VIBE_Bio_ReadVec3 @0x5dc938`,
`VIBE_Bio_ReadString @0x5dc86c` (the *SwapArgs* naming implies the on-disk
integers are endian-swapped on read).

### 2.1 Token table

Token values are the raw byte the reader compares against (`VIBE_Script_ReadToken`
returns the token id). Observed in `VIBE_ModelIo_LoadBinaryAnimation`:

| Token | Meaning | Payload |
|------:|---------|---------|
| 48 | format version sentinel | dword (discarded) |
| 1  | version code | dword; reject if `> 0xABCD0001` |
| 35 | **frame count** | dword `framecount`; allocates the keyframe table |
| 51 | has-points flag | byte → `hasPoints` (default 1) |
| 36 | has-uvfaces flag | byte → `hasUVfaces` (default 0) |
| 55 | **attachment-bone count** | dword `boneCount` → record +0x144 (+81 dw) |
| 54 | sub-frame count | dword `subFrames` (default 1) |
| 52 | override vertex count | dword (`-1` = use per-frame) |
| 53 | override wpoint count | dword (`-1` = use per-frame) |
| 41 | **start frame** | dword → record +0x150 (+84 dw) |
| 42 | **end frame**   | dword → record +0x154 (+85 dw) |
| 24 | per-frame **time stamp** | dword → frame +0x00 (ms-domain time) |
| 25 | per-frame **vertex count** | dword `nVerts` |
| 33 | **vertex block** | `nVerts` × Vec3 positions, terminated by token 40 (`EXP_DONE`) |
| 26 | UV-face count | dword, paired with 34 |
| 34 | UV-face block | `n` × (Vec3,Vec3), read+discarded, terminated by 40 |
| 49 | **frame bbox** | two Vec3 → frame +0x20 (min) / +0x2C (max) |
| 56 | bone **name** | string → bone slot +0x00 (64-byte stride) |
| 57 | bone **position** | Vec3 → frame +0x54 + 24·boneIdx |
| 58 | bone **rotation** (Euler) | Vec3 → frame +0x54 + 24·boneIdx + 12 |
| 50 | global bbox | two Vec3 (discarded after loop) |
| 47 | **end-of-chunk** (`EXP_EOCHUNK`) | — (anything else → `ReportMessage`) |
| 40 | `EXP_DONE` block terminator | — |

Parse errors are reported but not fatal: `"Expected EXP_DONE … ReadVertices"`
(token 33 without 40), `"… ReadUVFaces"` (token 34), `"Expected EXP_EOCHUNK …
EndOfFrameLoop"` (missing 47), and `"Inconsistent number of vertices in
d3_LoadBinaryAnimStream()"` when per-frame vertex counts disagree.

### 2.2 The AnimStream record (0x16C bytes)

Allocated only after token 35 supplies the frame count
(`VIBE_Memory_AllocDebug(0x16C, "…tp_veranimstream")`). Field offsets recovered
from the writer (`v3` = base, dword index in parens):

| Offset | (dw) | Field |
|------:|----:|-------|
| +0x00 | | name, copied via `VIBE_Util_StrNCopyPad(…, 63)` (zero-padded) |
| +0x140 | 80 | vertex count per frame (`nVerts`, from token 25, or override 52) |
| +0x144 | 81 | attachment-bone count (token 55) |
| +0x148 | 82 | **frame count** (`framecount`, token 35); also the `× 192` stride base |
| +0x14C | 83 | reserved |
| +0x150 | 84 | start frame (token 41, clamped 0…count-1) |
| +0x154 | 85 | end frame (token 42, clamped 0…count-1) |
| +0x15C | 87 | **frame table** base pointer (`VIBE_Memory_AllocDebug(192·framecount)`) |
| +0x168 | | reserved |
| +0x160 | 88/89 | intrusive stock-list links (`VIBE_Anim_LoadStreamToStock`) |
| +0x168 | | `+0x168/+0x16A` flags: `+0x168` = looping flag, `+0x169` = "is tangent mesh" |

The two trailing flag bytes deserve naming: `+0x168` (`hasPoints`/looping byte,
defaulted via token 51) and **`+0x169` = `isTangentMesh`** (set from the `a3`
argument of the loader, see §4.4). The whole record is the unit returned by the
loader; `VIBE_Anim_LoadStreamToStock` then threads it onto the global
intrusive list (`dword_13FC8E4` head, `unk_13FC780` sentinel) with the name
prefixed by `"animations/"`.

### 2.3 The keyframe (192-byte stride)

One entry per frame in the `+0x15C` table, indexed `192·frame`. Offsets from the
loader's post-pass and from every sampler that reads `*(stream+348) + 192·frame`:

| Offset | Field |
|------:|-------|
| +0x00 | frame **time** (ms; token 24) |
| +0x04 | **duration** = `time[next] − time[this]`; for the last frame copied from the previous; later `×= 3` (§2.4) |
| +0x08..+0x10 | per-frame AABB min (`x,y,z`) of the vertex block |
| +0x14..+0x1C | per-frame AABB extent · `1/255` (quantisation scale; `flt_62BD40 = 0.003922`) |
| +0x20..+0x28 | bone-root **position** (token 49); after load made *relative to frame 0* |
| +0x2C..+0x34 | bone-root **rotation** (Euler) (token 49); also made relative to frame 0 |
| +0x54 + 24·b | per-bone **position** (token 57) (24-byte stride per bone) |
| +0x54 + 24·b + 12 | per-bone **rotation** Euler (token 58) |
| +0x36/+0x40 +0x48/+0x54 | Hermite **tangents** filled by `ComputeFrameTangents` (§5) |
| +0xB4 (188) | pointer to **points** array (Vec3 per vertex) — freed after quantisation |
| +0xB4 (180) | pointer to **wpoints** = quantised 3-byte-per-vertex deltas |

### 2.4 Load-time post-processing

After the chunk loop, with `framecount = N`:

1. **Durations** — for each frame `k<N-1`: `dur[k] = time[k+1] − time[k]`;
   `dur[N-1] = dur[N-2]`.
2. **Root-relative** — bone-root pos/rot of every frame `k≥1` is made relative to
   frame 0; frame 0's root pos/rot is then zeroed.
3. **Points zero-init** for `nVerts` entries.
4. **Tangent meshes** (`+0x169` set): for every frame, each vertex point and each
   bone-attachment pos is rebased by subtracting that frame's bone-root pos.
5. **Quantise** each frame's vertex points into a 3-byte-per-vertex `wpoints`
   block: per frame compute the AABB (`±1e10` seed), store min at +0x08, store
   `extent · (1/255)` at +0x14, and write `(p − min) · 255 / extent` (clamped via
   `VIBE_Coord_ConvertX @0x5c6b08` truncation; consts `flt_62BD44 = 255.0`). The
   float `points` array is then freed; the runtime samplers reconstruct positions
   from the byte deltas times the +0x14 scale plus the +0x08 base.
6. Each frame's duration is finally `dur[k] *= 3`.

So at runtime a frame's reconstructed vertex is
`min[+0x08] + byte_wpoint · scale[+0x14]` — exactly the reconstruction performed
in `VIBE_Anim_CalculateAnimNormals` (§7) and `VIBE_Mesh_InterpolateMorphVertices`.

---

## 3. The frame-index stepper — `VIBE_Anim_AdvanceFrameIndex @0x5ccf18`

A tiny `__userpurge` helper (register args `ah`=mode, `edx`=cur, `ecx`=lastFwd,
`ebx`=loopStart, stack=frameCount). It returns the *next* frame index given a
direction/wrap mode bitfield in `ah`:

```c
// gilde.exe 0x5ccf18 — VIBE_Anim_AdvanceFrameIndex
int Advance(u8 mode, int cur, int lastFwd, int loopStart, int frameCount) {
    int dec = cur - 1;
    if (mode & 0x02) {                 // playing BACKWARD (ping-pong return leg)
        if (cur <= loopStart) return loopStart + 1;
        return dec;
    }
    int inc = cur + 1;
    if (cur < lastFwd) return inc;     // normal forward
    if ((mode & 0x10) == 0) {          // not "hold/settle"
        if ((mode & 0x01) == 0) return loopStart;   // LOOP back to start
        return dec;                                  // PING-PONG: bounce
    }
    return (inc <= frameCount - 1) ? inc : frameCount - 1;  // CLAMP at end
}
```

The three play modes are therefore encoded by `mode` bit `0x01` (ping-pong vs
loop), bit `0x02` (currently on the return leg), and bit `0x10` (clamp/hold at
the terminal frame). These same bits live in the bone-track flags (§4.2).

---

## 4. The pose driver — `VIBE_Anim_UpdateSkeletonPose @0x5cd1d8`

`__usercall` (`eax`=object, `edx`=newTime). Returns 1. The high bit of `edx`
(`v190`) is a "no relight" suppress flag; `v185 = newTime & 0x7FFFFFFF`.

### 4.1 Time split / early-out

```
prevTime = obj[+0x40];  obj[+0x40] = newTime;       // store new game time
dt = newTime - prevTime;                            // signed delta -> v179
if (newTime == prevTime) return 1;                  // nothing moved this tick
rig = obj[+0x1EC];                                   // skeleton/anim controller
if (!rig) → skip straight to bone-matrix build (no tracks)
```

If the object is an animated-texture host (`obj+533 == 4`, `obj[+115]` set, and
the asset's flag byte `*(rig?+2317)&0x40 == 0`) it also pumps animated **texture**
frames via `VIBE_Texture_AdvanceAnimFrames @0x5daf78` — the sibling system to
skeletal frames.

### 4.2 The LOD-layer × 3-bone-track advance ladder

The controller holds a small array of **LOD layers** (`*(rig+2316)` = layer
count, 384-byte stride at `i`). Each active layer (gate byte `+0x270`) carries
**three bone tracks** (`j` = 0, 116, 232 — a 116-byte stride, offset from
`rig+244+28`). A track is live when its stream pointer `+0x68` is non-zero and
flag bit `+0x6E & 2` is set.

Per-track state (offsets relative to the track base `v5`):

| Offset | Field |
|------:|-------|
| +0x00 | current frame |
| +0x04 | next forward frame (from `AdvanceFrameIndex`) |
| +0x08 | sub-frame accumulator (ticks within the current frame's duration) |
| +0x0C | "from" frame for tangent meshes |
| +0x14/+0x18/+0x1C | **attachment window**: start / mid / end frames (`-1` = none) |
| +0x20/+0x24/+0x28 | cached attachment base position |
| +0x30 | attached object pointer (the thing parented during the window) |
| +0x34 | blend-window base time |
| +0x38 | blend-window end time |
| +0x40 | blend weight (`0..1`, float) |
| +0x44/+0x48 | blend weight start/end |
| +0x4C | settle-tolerance squared and current settle vector compare |
| +0x60 | blend rate scale (per-ms) |
| +0x64 | blend weight accumulator |
| +0x68 | **stream pointer** (the AnimStream from §2) |
| +0x6C | byte: repeat counter (decremented per loop; 2→ sets clamp bit) |
| +0x6D | flags A: `0x01`=ping-pong, `0x02`=return-leg, `0x10`=clamp, `0x20`=needs-recompute |
| +0x6E | flags B: `0x02`=active, `0x04`=settle-track, `0x08`=terminal, `0x10`=hold, `0x40`=attach-armed |

The driver pulls the stream's loop bounds: `loopStart = stream+0x150` normally,
but when flag `+0x6D & 0x10` (clamp) is set it uses `stream+0x148-1` (last
frame) and, for non-tangent streams, resets the from-frame `+0x0C = 0`. It marks
the rig dirty (`VIBE_Object_PropagateDirtyFlag @0x5af2c0`).

### 4.3 Blend window weight & sub-frame integration

```
if (blendStart != blendEnd) {
    if (blendEnd > now) {                                 // inside window
        weight = lerp(weightStart, weightEnd,
                      (now - blendStart) / (blendEnd - blendStart));   // +0x40
    } else {                                              // window closed
        weight = weightEnd; blendStart = blendEnd = 0;
        if (weight == 0) settleNow = 1;
    }
}
dirSign = (flagsB<<6)>>7;     // sign bit of bit 0x02 -> direction
acc += dirSign * blendRate(+0x60) * dt;                   // +0x64 accumulator
subFrame += (int)acc;  acc -= (int)acc;                   // integer/frac split (Coord_ConvertX)
track[+8] += subFrame    (forward)   or  -= subFrame  (return leg, flag 0x02)
```

`VIBE_Coord_ConvertX @0x5c6b08` is the float→int truncation primitive used
throughout to split the fractional sub-frame accumulator.

### 4.4 The advance loop (loop / ping-pong / clamp / hold + repeat counter)

The inner `while(1)` consumes the sub-frame accumulator `+0x08` one frame's
duration at a time, in either direction. The forward branch
(`!(flagsA & 2)`) reads the current frame duration
`dur = frame[+0x04]` and, while `acc ≥ dur`:

* if `cur+1 < lastFwd` → `acc -= dur; cur++` (plain step);
* else, at the terminal frame, it branches on `flagsA`:
  * `& 0x10` (clamp/hold) → pin `acc = dur-1`, set `settleNow` (`v188`);
  * `& 0x01` (ping-pong) → set return-leg bit `0x02`, reflect `acc`, jump
    `cur = nextFrame[+0x04]`, and **decrement the repeat counter `+0x6C`** (when
    it hits the value 2 it raises the clamp bit `0x10`);
  * else (**loop**) → clear hold bit `0x10`, decrement repeat counter, wrap
    `cur = loopStart`, and — for non-tangent streams — emit a **root-motion
    delta** via `VIBE_Anim_ComputeBoneDelta` (§5) so a looped walk keeps moving.

The backward branch (`flagsA & 2`, ping-pong return leg) mirrors this against
`loopStart`. Every iteration recomputes `+0x04` with
`VIBE_Anim_AdvanceFrameIndex` (§3), passing the repeat-counter byte
`BYTE1(track[+0x6C])` as the mode and the stream's frame count / loop start.

**Settle break** (`flagsB & 0x04`, settle-tracks only): each iteration samples the
accumulated bone translation with `VIBE_Anim_SampleBoneTranslation @0x5cc920`
(§5) at the mid sub-frame, and if the sampled XZ position is within the
per-track tolerance `+0x5C` of the target (`fabs(dx) < tol && fabs(dz) < tol`)
it `break`s — the character has "arrived". Tolerance is `track[+0x5C]` (`v142`).

When the accumulator is fully consumed (or settled) it falls to **`LABEL_31`**.

### 4.5 Per-frame pose application

At `LABEL_31`:

* Tangent streams (`stream+0x169`) → `VIBE_Anim_InterpolateBoneFrame @0x5cbc10`
  applies the inter-keyframe translation (§5).
* **Attachment window** (`+0x14/+0x18/+0x1C ≠ -1`, attached object `+0x30`): while
  `cur` is inside `[start, mid)` the driver arms (`flagsB |= 0x40`, caches the
  attached object's world pos into `+0x20`) then drives the attached object's
  position by lerping the bone's start/end pose
  (`VIBE_Math_VectorLerp @0x5ca2fc`), rotating the offset into the bone's frame
  with `VIBE_Transform_RotateVectorByHierarchy @0x5c8990`, and writing it back via
  `VIBE_Object_SetPosition @0x5af38c`. Past `mid` it disarms and clears the window
  (`+0x1C = -1`, `+0x30 = 0`). This is how a person "carries" / "drops" an object
  at the right point of an animation.
* **Settle finish** (`settleNow`): clears active bit `0x02`; if the recompute bit
  `0x08` is set it just sets `0x20` (recompute-later); otherwise it emits the
  terminal `ComputeBoneDelta` root-motion and **prunes expired attachments** via
  `VIBE_Anim_PruneExpiredAttachments @0x5d0d38` (drops finished one-shot meshes
  off the bone, reassigns sub-mesh bones, rebuilds matrices).
* `v193` (relight-needed) is latched when `now - obj[+0x44]` exceeds the track's
  light interval `+0x3C`; `v180` caches the active frame's keyframe pointer for
  the vegetation light cache (§8).

### 4.6 Collapse to bone matrices — `VIBE_Anim_ComputeBoneMatrices @0x5cc0d0`

After all layers/tracks advance, the driver calls
`VIBE_Anim_ComputeBoneMatrices`. With 1–4 distinct bones (`v82` scratch, 164-byte
per-bone stride, `v2` distinct count), for each track it:

1. de-dupes bone names across tracks (`VIBE_Util_StrCmp @0x5d3f10`),
2. lerps each bone's pos/rot between its two keyframes
   (`VIBE_Math_VectorLerp` with `t = subFrame/duration`),
3. converts each Euler triple to a basis with `VIBE_Math_MatrixFromEuler
   @0x5cb1bc`, accumulates per-bone matrices and translations (weighted by the
   track's blend weight `+0x40`),
4. averages by the contribution count and converts back with
   `VIBE_Math_MatrixToEuler @0x5cb2cc`,
5. walks the rig's scene-graph children (`obj+508`, next at child[+124]); for each
   child whose bone name (`child[+123]+180`) matches a computed bone, writes the
   blended pose with `VIBE_Object_SetPosition` + `VIBE_Object_SetWorldTranslation
   @0x5af50c`.

When there are **zero** computed bones (no active tracks) it instead binds each
child directly to its named bind-pose bone from the rig's skeleton table
(`rig+260` → bone records at `+116`, 88-byte stride, count `+520`). This is the
bridge that pushes the skeleton pose onto the renderable child objects.

---

## 5. Translation samplers, deltas & tangents

These four helpers all read the same 192-byte keyframe table at
`*(stream+0x15C) + 192·frame` and feed translations into the object transform.

* **`VIBE_Anim_SampleBoneTranslation @0x5cc920`** — accumulates the bone-root
  position delta across the span `[fromFrame, toFrame]` (summing whole-frame
  deltas of `+0x20..+0x28`, plus fractional head/tail by `subFrame/duration`),
  rotates it through the object's bone matrix (`obj` floats at dword 99–109,
  i.e. +0x18C..) and adds the object's base translation (dwords 19–21 and 30–32).
  Tangent streams (`stream+0x169`) take the interpolating branch; non-tangent
  streams take a simpler single-frame lerp.
* **`VIBE_Anim_InterpolateBoneFrame @0x5cbc10`** — same span accumulation but for
  the *per-frame* translation actually written to the object each tick (used only
  by tangent meshes); writes via `VIBE_Object_SetPosition`. It also caches the
  from/to frames in `track+0x0C/+0x10` so the next tick continues the integration.
* **`VIBE_Anim_ComputeBoneDelta @0x5cba40`** — `a5` bit 0x01 → rotate the
  position delta `(frameA − frameB)` of `+0x20..+0x28` through the object matrix
  (+0x18C..) and `SetPosition`; bit 0x02 → add the rotation delta
  `+0x2C..+0x34` and `SetWorldTranslation`. This is the **root-motion** applied
  on each loop/settle so a looping walk cycle advances the character in the world.
* **`VIBE_Anim_ComputeFrameTangents @0x5ccf70`** — builds **Hermite tangents**
  for ping-pong/Catmull-Rom playback. For a frame pair it computes
  `tangent = ((next − cur)/dur_cur + (cur − prev)/dur_prev) · 0.5`
  (`flt_628D4C = 0.5`), scaling by each frame's duration, and stores in/out
  tangents at keyframe `+0x20..+0x28` / `+0x30..+0x38` (and the rotation set at
  `+0x40..` / `+0x4C..`). Invoked from the morph path (§6) when entering the
  bounce leg.

`VIBE_Anim_GetBoneFramePose @0x5ccea0` is the plain accessor that copies a single
frame's root pos (`+0x20`) and rot (`+0x2C`) — used by the blend-vector helper.

---

## 6. Object / vegetation morph animation (Catmull-Rom)

A second, simpler channel lives at `obj[+116]` (`v59`). It plays the *same*
keyframe table but drives the object's own transform with a spline. It is used by
the `.baf` props seen in the strings: `anim_FLUCHTFLAGGE.baf`, `ub_Blutlache.baf`,
`sp_WIMPEL.baf`, `Buch_vorblaettern/zurueckblaettern.baf`, `*sensenmann.baf`.

### 6.1 Morph channel state (offsets relative to `v59`)

| Offset | Field |
|------:|-------|
| +0x00 | frame count |
| +0x04 | "from" frame |
| +0x08 | "to" frame |
| +0x0C | sub-frame accumulator |
| +0x10 | light-cache interval |
| +0x14..+0x1C | base translation |
| +0x20..+0x28 | base offset (added to interpolated pos) |
| +0x2C | repeat counter |
| +0x2D | flagsA (same 0x01/0x02/0x10 semantics as §4.2) |
| +0x2E | flagsB (`0x02`=morph enabled, `0x10`=hold, `0x20`=settled, `0x40`=tangents-valid) |
| +0x30 | blend rate per-ms |
| +0x34 | blend accumulator |
| +0x38 | **keyframe table** pointer (88-byte stride here, not 192) |

The morph table uses an **88-byte** per-frame stride (`*(+0x38) + 88·frame`):
`+0x00`=duration, `+0x04..+0x0C` and `+0x14..+0x1C` two Vec3 channels (pos / rot),
with Hermite tangents in `+0x20..` and `+0x30..` / `+0x40..` / `+0x50..`.

### 6.2 The Catmull-Rom advance

`weight = subFrame / duration` (or `1 − that` on the return leg). When the
"tangents valid" bit (`flagsB & 0x04`) is set, the position is a cubic
**Catmull-Rom / Hermite** blend of the two endpoints and their tangents via
`VIBE_Math_CatmullRomInterp @0x5ca9d8`:

```c
// gilde.exe 0x5ca9d8 — VIBE_Math_CatmullRomInterp(p0,p1,m0,m1,t)
// flt_628D28 = 2.0, flt_628D2C = 3.0, flt_628D30 = -2.0
double t2 = t*t, t3 = t2*t;
return (t + t3 - 2.0*t2)        * m0     // outgoing tangent
     + (3.0*t2 - 2.0*t3)        * p1     // wait: layout is Hermite h00/h10/h01/h11
     + (2.0*t3 - 3.0*t2 + 1.0)  * p0
     + (t3 - t2)                * m1;
```

i.e. the standard Hermite basis `h00·p0 + h10·m0 + h01·p1 + h11·m1` with the
canonical coefficients `2,−3,1` and `1,−2,1` / `−2,3` / `1,−1`. Without valid
tangents (`!(flagsB & 0x04)`) it falls back to a plain linear blend
`p = p_from·(1−w) + p_to·w`, gated by
`VIBE_Math_VectorWithinTolerance @0x5caa4c` (skip the write if the move is below
`flt_5CA2E0`). The interpolated pos goes to `VIBE_Object_SetPosition`, the rot to
`VIBE_Object_SetWorldTranslation`.

The morph driver runs the **same loop/ping-pong/clamp ladder** as §4.4 against the
88-byte table (its own `AdvanceFrameIndex` call at `LABEL_164`), with the same
repeat-counter `+0x2C` / clamp-bit handling and a **terminal free** at the end:
when a non-looping morph settles (`v192`), if it is not flagged sticky
(`flagsA & 0x08`) the channel's resources at `obj[+116]+56` are released with
`VIBE_Memory_FreeDebug @0x43923c` and `obj[+116]` is nulled — the one-shot prop
animation is destroyed. Entering the bounce leg calls
`VIBE_Anim_ComputeFrameTangents` (§5) to (re)build the tangents, and a special
"zero the last two frames' channels" cleanup runs when `flagsA & 0x14` and the
end window is hit.

### 6.3 The vegetation light-cache gate

After the morph advance, the driver decides whether to rebuild the vegetation
light cache. The gate (§8 reached from `LABEL_126`) requires: `v191||v193` (a
track or the morph signalled a relight), the time delta is **forward**
(`!v190`, i.e. `newTime` high bit clear), the object is vegetation
(`obj+533 == 4`), it has a controller, and the asset flag `(rig+2317)&0x40` is
clear.

### 6.4 Blend vectors for transitions — `VIBE_Character_ComputeAnimBlendVectors @0x428f38`

Given a controller with a valid blend source (`rig+376`), it samples the two
frames bracketing the current blend (`VIBE_Anim_GetBoneFramePose` ×2), lerps the
position (`VIBE_Math_VectorLerp`, `t = subFrame/duration`) and composes the two
rotations as matrices (`MatrixFromEuler` → `VIBE_Math_MatrixTransformVectors
@0x5caaa4` → `MatrixToEuler`). The output position+rotation vector pair is what
the action/AI layer (doc 17) feeds when cross-fading between two animations; when
there is no blend source it returns zero vectors.

---

## 7. Deformed-mesh normals — `VIBE_Anim_CalculateAnimNormals @0x5d0020`

Called **once** by `VIBE_Anim_AttachToBone @0x5d0b64` the first time a stream is
bound to a bone slot (then the slot's "normals done" byte `+0x168` is set so it
never re-runs). It precomputes, for **every frame** of the stream, a quantised
per-vertex normal plus a neighbour-smoothed bounding box. Inputs:
`a1` = stream record, `a2` = the mesh (vertex/face tables).

Per frame `f` (`192·f`, count `stream[82]`):

1. **Reconstruct world points** into a scratch `points[16·nVerts]`. Two cases:
   * the frame has a `points` float array (`+0xBC = frame+188`) → add the mesh
     base vertex (`mesh+64`) to it;
   * otherwise reconstruct from the quantised `wpoints` byte deltas at
     `frame+180`: `p = byteDelta · scale[frame+0x14] + min[frame+0x08]` (§2.4) plus
     the mesh base vertex.
2. `frame[+0x38] = mesh[+0x1D4] − VIBE_Math_MaxVectorLength(points,nVerts)`
   (`VIBE_Math_MaxVectorLength @0x5cffac`) — a per-frame radius/cull metric.
3. **Face normals** for every triangle (`mesh+72`, 56-byte face stride, indices
   at +24/+28/+32) via `VIBE_Math_TriangleNormal @0x5cb824`.
4. **Per-vertex normal** = sum of incident face normals (a face contributes if any
   of its three vertex indices equals the current vertex), normalised
   (`VIBE_Math_VectorNormalize @0x5cb148`), biased by `dword_5CBA30 = {1,1,1,1}`,
   then quantised to 3 bytes through `VIBE_Coord_ConvertX` with the scale
   `dbl_628F04 = 0.5` and range `dbl_628F0C = 255.0` (i.e. `(n·0.5)·255` → byte,
   the classic signed-normal-to-unsigned-byte pack). Stored at the frame's normal
   buffer (`frame+184`, allocated `3·nVerts`).
5. While walking vertices it also tracks the frame's **AABB** (seed `±1e10`),
   stored at `frame+0x3C` (min) / `frame+0x48` (max).

After all frames, a second pass **smooths the per-frame bounding boxes against
neighbour frames** (frame `k` widened against `k−1` and `k+1`): copy each frame's
bbox into a temp `[8 dwords/frame]`, then for interior frames min/max-merge with
the previous and next frame's bbox, and write the smoothed result back to
`frame+0x3C..`. This produces a temporally stable, slightly inflated bbox so a
fast-moving animation doesn't pop in/out of culling.

Memory is `VIBE_Memory_AllocDebug`/`FreeDebug` with the tags
`"d3_anim:CalculateAnimNormals(points)"`, `(normals)`, `(bbox_points)`.

---

## 8. Relight glue — feeding posed vertices into lighting (doc 25)

The animated normals and deformed positions flow into the lighting stage two ways:

### 8.1 Per-vertex deform + lighting — `VIBE_Mesh_InterpolateMorphVertices @0x5c953c`

Called from the render walk. If the object has active bone tracks (`obj+380`), it
walks the three tracks; for each live, non-zero-weight track it pulls the two
keyframes' quantised vertex deltas, computes per-channel **morph weights** via
`VIBE_Anim_ComputeMorphWeights @0x5c9394`, and writes
`vertex = (delta_from·w_from·s_from + base_from) + (delta_to·w_to·s_to + base_to)`
into the live vertex buffer (20-float vertex stride). It then transforms each
deformed vertex by the object matrix (`v111`, a 4×4 at the object), tracks the
min/max world-Z into `flt_13FD168` / `flt_13FCF3C` (the global depth bounds), and
finally tail-calls **`VIBE_Mesh_ComputeVertexLighting @0x5c9054`** (doc 25) on the
freshly posed mesh. The no-track fast paths skip the morph and just transform +
light. This is the single hand-off point where skeletal/morph output becomes lit
geometry for the rasterizer (doc 24).

### 8.2 Vegetation light cache — `VIBE_Light_BuildVegetationCache @0x5c8560`

For vegetation the pose driver instead rebuilds a per-object **light cache**
(gated in §6.3). It selects the active LOD frame (`VIBE_Mesh_SelectLodFrame
@0x5adb6c`), transforms the packed vertices into bone-local space
(`VIBE_Transform_PointToBoneLocalSpace @0x5c8c40`,
`VIBE_Mesh_TransformPackedVertices @0x5c9d04`), walks nearby light-casting
objects (`VIBE_SceneGraph_WalkAndInvoke @0x5ac738` →
`VIBE_Light_CollectAffectedObject @0x5c80a0`), applies them
(`VIBE_Light_ApplyToCachedVertices @0x5c6f90`), then packs each vertex's RGB into
a byte. Two pack modes (`byte_649D70`): full-colour (clamp the max channel to
`flt_628CA4`/`dbl_628CAC`) or a single **luminance** byte using the weights
`flt_628C98 = 0.590`, `flt_628C9C = 0.300`, `flt_628CA0 = 0.110` (clamped to
0xFF). The relevant LOD layer is chosen by
`VIBE_Anim_FindHighestPriorityLayer @0x5d0e84` (the layer among the ≤3 with the
greatest blend weight `+0x5C`). The cached colours are read back by the renderer
(doc 23) so swaying plants get cheap baked lighting that only refreshes when the
animation moved past its interval.

---

## 9. Animation constants (recovered bytes)

| Symbol | Addr | Value | Use |
|--------|------|------:|-----|
| `flt_628D28` | 0x628D28 | `2.0` | Catmull-Rom/Hermite coeff |
| `flt_628D2C` | 0x628D2C | `3.0` | Catmull-Rom/Hermite coeff |
| `flt_628D30` | 0x628D30 | `-2.0` | Catmull-Rom/Hermite coeff |
| `flt_628D4C` | 0x628D4C | `0.5` | tangent averaging factor |
| `flt_62BD40` | 0x62BD40 | `0.0039216` (≈1/255) | vertex-delta quantise scale |
| `flt_62BD44` | 0x62BD44 | `255.0` | quantise range |
| `dbl_628F04` | 0x628F04 | `0.5` | normal pack scale |
| `dbl_628F0C` | 0x628F0C | `255.0` | normal pack range |
| `dword_5CBA30` | 0x5CBA30 | `{1.0,1.0,1.0,1.0}` | normal bias vector |
| `flt_628C98/9C/A0` | 0x628C98.. | `0.590 / 0.300 / 0.110` | luminance weights (veg cache) |
| version reject | (immediate) | `> 0xABCD0001` | `.baf` version guard |

`dword_649D58` (the per-frame dirty stamp written into the stream at `+0x158`) and
`dword_64A050` (recursion guard around attach/cache) are runtime counters, not
constants.

---

## 10. Provenance summary

| Function | Addr | Role |
|----------|------|------|
| `VIBE_ModelIo_LoadBinaryAnimation` | 0x5e450c | `.baf` parser → AnimStream record |
| `VIBE_Anim_LoadStreamToStock` | 0x5d3858 | de-dupe + register stream in stock list |
| `VIBE_Character_PreloadAniSetByName` | 0x403f14 | build `.baf` paths, preload an ani-set |
| `VIBE_Anim_AttachToBone` | 0x5d0b64 | bind stream to a bone-track slot |
| `VIBE_Anim_CalculateAnimNormals` | 0x5d0020 | one-shot per-frame deformed normals + bbox |
| `VIBE_Anim_UpdateSkeletonPose` | 0x5cd1d8 | per-frame pose driver |
| `VIBE_Anim_AdvanceFrameIndex` | 0x5ccf18 | frame-index stepper (loop/ping-pong/clamp) |
| `VIBE_Anim_ComputeBoneMatrices` | 0x5cc0d0 | collapse tracks → matrices → child objects |
| `VIBE_Anim_SampleBoneTranslation` | 0x5cc920 | accumulated bone translation sample |
| `VIBE_Anim_InterpolateBoneFrame` | 0x5cbc10 | per-frame translation (tangent meshes) |
| `VIBE_Anim_ComputeBoneDelta` | 0x5cba40 | root-motion delta on loop/settle |
| `VIBE_Anim_ComputeFrameTangents` | 0x5ccf70 | Hermite tangents |
| `VIBE_Anim_GetBoneFramePose` | 0x5ccea0 | single-frame pose accessor |
| `VIBE_Anim_PruneExpiredAttachments` | 0x5d0d38 | drop finished one-shot meshes |
| `VIBE_Anim_FindHighestPriorityLayer` | 0x5d0e84 | pick LOD layer for the light cache |
| `VIBE_Math_CatmullRomInterp` | 0x5ca9d8 | morph cubic spline |
| `VIBE_Character_ComputeAnimBlendVectors` | 0x428f38 | cross-fade blend vectors |
| `VIBE_Character_CacheActiveMesh` | 0x4262c0 | cache the active anim mesh (object-anim) |
| `VIBE_Mesh_InterpolateMorphVertices` | 0x5c953c | deform verts → lighting |
| `VIBE_Mesh_ComputeVertexLighting` | 0x5c9054 | relight (doc 25) |
| `VIBE_Light_BuildVegetationCache` | 0x5c8560 | vegetation light cache |
