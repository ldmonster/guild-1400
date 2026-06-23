# Wave-6 — Per-distance MESH/NODE LOD switching (W6-LOD)

Per-frame LOD selection: picking the right mesh detail level by camera distance each
frame, so distant buildings/objects use lower-poly LODs exactly as `gilde.exe` does.

Owned modules: `src/render/node_lod.{h,cpp}`, `src/render/mesh_lod_name.{h,cpp}` +
`tests/unit/node_lod_select_test.cpp`.

## Functions (addresses + status)

| Address    | Original                              | Module                         | Status |
|------------|---------------------------------------|--------------------------------|--------|
| `0x5adb6c` | `VIBE_Mesh_SelectLodFrame`            | `node_lod.cpp` `SelectLodFrame`| **verified 1:1** (decompile + disasm) |
| `0x5c6b08` | `VIBE_Coord_ConvertX` (truncate→0)    | `util/coord.cpp` `ConvertX`    | confirmed (`frndint` RC=trunc) |
| `0x5ad1f4` | `VIBE_Render_ClassifyBoundingBoxPlanes` | `node_lod.cpp` (4-arg subset) | verified 1:1 (math) |
| `0x5ad588` | `VIBE_Render_CullNodeAgainstFrustum`  | `node_lod.cpp` `CullNodeAgainstFrustum` | verified 1:1 |
| `0x5d15fc` | `VIBE_Mesh_BuildLodFileName`          | `mesh_lod_name.cpp`            | verified 1:1 (pure string strategy) |
| `0x5d1034` | `VIBE_Mesh_BuildTexturePath`          | `mesh_lod_name.cpp`            | path-compose 1:1; VFS probe hooked |
| `0x5d1824` | `VIBE_Mesh_AttachStockObjectLods`     | `mesh_lod_name.cpp`            | structure 1:1; draw-block writes hooked |

## The per-frame distance→LOD threshold math (the core deliverable)

`SelectLodFrame @0x5adb6c` is the per-frame mesh LOD picker the scene-walk runs before
projecting each object. Reconstructed verbatim and re-verified this wave against the
decompile **and** the disassembly (the `VIBE_Coord_ConvertX` register/FPU contract is the
one easy-to-misread part — see below):

```
drawData = object[+492]                       ; node draw-data block (idx123)
if (!drawData || !drawData[+2316]) return null ; no drawable LOD (lodCount==0)
lodCount = drawData[+2316]                     ; u8 LOD frame count

FORCED branch  if (object[+531] & 0x30) != 0  OR  no active world (dword_13FCD1C==0):
    v4 = ((u8)(4 * object[+531]) >> 6) - 1     ; flag bits 0x10/0x20/0x30 -> 0/1/2
    if ((u32)(lodCount-1) < v4) v4 = lodCount-1 ; unsigned: a 0 selector underflows
    index = v4                                  ;   to 0xFFFFFFFF -> clamps to max LOD

DISTANCE branch otherwise:
    d   = object[+76/80/84] - cam[+76/80/84]    ; cam = dword_13FCD1C
    lod = sqrt(dx²+dy²+dz²) * (float)lodCount * flt_13FC774   ; flt_13FC774 = 1/fov scale
    truncLod = trunc(lod)                        ; VIBE_Coord_ConvertX (RC=toward-zero)
    if (lodCount > truncLod)                      ; NEAR -> low-detail-index branch
        index = trunc( max(lod, 0) )
    else                                          ; FAR  -> max LOD
        index = trunc( lodCount + flt_62807C )    ; flt_62807C == -1.0  =>  lodCount-1

result = drawData + 244 + 384*index             ; the 384-byte LOD frame record
if (!result[+8] || !result[+12]) return null    ; chosen frame has zero polys -> null
if (result != object[+460] || byte_64A068)       ; frame changed, or forced rebuild
    object[+528] |= 0x40                          ;   -> set the cull/dirty bit
return result
```

### Recovered constants (get_bytes)
- `flt_62807C` @ `0x62807C` = `00 00 80 BF` = **-1.0f** (the "max LOD" bias `lodCount + (-1)`).
- `flt_13FC774` @ `0x13FC774` = 0.0 at rest (runtime-set 1/fov screen scale).
- `byte_64A068` @ `0x64A068` = 0 (runtime force-rebuild flag).
- `dword_13FCD1C` @ `0x13FCD1C` = 0 at rest (the active-world/camera ptr; 0 ⇒ forced branch).

### The `VIBE_Coord_ConvertX` contract (the load-bearing detail)
`@0x5c6b08` does `push eax; fstcw; set RC=trunc (HIBYTE=0x1F); fldcw; frndint; fldcw;
pop esp+8` — it rounds **st0** toward zero in place and **leaves eax untouched**. In
`SelectLodFrame` the comparison `cmp eax, var_18` is therefore `lodCount` (loaded into `al`
*before* the call) vs the truncated LOD (`fistp var_18` *after*). So the near/far test is
`if (lodCount > truncLod)`. Modeled by `util::ConvertX == std::trunc`; the C++ cast to int
also truncates toward zero, so `(int)ConvertX(x)` is byte-faithful.

### Index range
In the distance branch the index is naturally in `[0, lodCount-1]` (near: `truncLod <
lodCount`; far: `lodCount-1`), so `node_lod.cpp`'s explicit `[0,lodCount-1]` clamp is
redundant-but-harmless; the forced branch is already clamped by the original. The
reconstruction returns the 0-based **index** (clean entry); the raw-block adapter converts
it back to the original's `drawData+244+384*index` pointer.

## Clean entry (camera distance → LOD frame index for a node)

`render::SelectLodFrame(const LodObject& obj, const LodView& view, bool* outSetCullBit)`
(`node_lod.h`) — returns the selected 0-based LOD frame index, or **-1** for the original's
"return 0 / null frame" (no drawable LOD or the chosen frame has zero polys). `LodObject`
gathers the object fields (+76.. pos, +531 flags, +2316 lodCount, the 384-byte frame
poly-count/cap pairs, +460 active index); `LodView` gathers the per-frame globals
(`worldPresent`=dword_13FCD1C!=0, `camPos`, `fovScale`=flt_13FC774, `forceRebuild`=byte_64A068).
`outSetCullBit` reports whether `object[+528] |= 0x40` fired.

## Handoff — how the universe/object walk selects LOD per node per frame

The scene-walk does NOT call `node_lod.cpp` directly; it calls the raw-object-block
**adapter** that wraps it (owned by the mesh-attach agent, read-only to me):

```
play::CityView3D frame
  -> render::BeginUniverseFrame 0x5b3900  (universe-render-chain.md)
       -> SceneGraph_WalkAndInvoke 0x5ac738  -> per node:
          render::ProcessSceneNodeAppend 0x5add1c          (per-object pipeline)
            step 1: ResolveActiveLodFrame  (render_leaves8.cpp)
                      if (node[+460] != 0) use it
                      else node[+460] = MeshAttachHooks::selectLodFrame(node)
                        == render::SelectLodFrameForNode (mesh_attach_textures.cpp 0x5adb6c)
                            reads node +492 / +531 / +460 / +76..+84 + the 384-byte frames,
                            builds LodObject, runs render::SelectLodFrame(obj, g_lodSelectView),
                            sets node[+528] |= 0x40 on change, returns drawData+244+384*idx
            step 2: cull (ClassifyBoundingBoxPlanes / CullNodeAgainstFrustum)
            step 3: vertex transform (InterpolateMorphVertices 0x5c953c) on the chosen frame
            step 4: append to draw list
  -- also: AttachStockTextures 0x5d1114 step 6 picks the active LOD frame the SAME way --
```

- **Bind site:** `MeshAttachHooksMut().selectLodFrame = &SelectLodFrameForNode;` installed
  by `render::InstallStockTextureAttach` (mesh_attach_textures.cpp ~L507). Already wired.
- **Per-frame inputs the runtime must feed:** `render::SetLodSelectView(LodView)` once per
  frame with the live camera position (`CityCamera3D` eye → cam +76/80/84), `fovScale`
  (flt_13FC774, the 1/fov screen scale), and `forceRebuild`. Until set, the view defaults
  to `worldPresent=false` → the original's `dword_13FCD1C==0` **forced-LOD branch** (a safe,
  faithful no-camera default: each node uses its `+531`-flag forced LOD, exactly as the
  engine does before a world camera is active).
- **Gate:** none beyond drawability — the original always runs the LOD pick for a drawable
  node every frame; distance-vs-forced is selected by `dword_13FCD1C` + the `+531 & 0x30`
  flag, not by an Options toggle. (The stock-object multi-LOD *load* is gated by the
  `byte_64A098` LOD-mode byte; see mesh-lod-stock-object.md.)

## Tests

`tests/unit/node_lod_select_test.cpp` — golden vectors for the threshold math, each asserted
against a verbatim integer/float oracle (`OracleDistanceIndex`) that re-runs the binary's
exact `sqrt * count * fovScale -> ConvertX(trunc) -> near/far` sequence:

| Test | Covers |
|------|--------|
| `ForcedLodIndexFromFlagBits` | `((u8)(4*flags)>>6)-1` for flags 0x10/0x20/0x30 → 0/1/2 |
| `ForcedLodClampsToLodCountMinusOne` | the `(lodCount-1) < v4` clamp |
| `ForcedZeroSelectorUnderflowsToMaxLod` | the unsigned `0u-1` underflow → clamp to max LOD |
| `DistanceNearMidFarBoundaries` | near/mid/far + zero-distance boundary indices |
| `DistanceMatchesOracleOverSweep` | 40-step 3-axis sweep == the verbatim oracle (boundary-sensitive) |
| `NoDrawDataReturnsMinusOne` | the `!drawData` / `lodCount==0` null returns |
| `ZeroPolyFrameReturnsMinusOne` | the frame `+8`/`+12` poly-validity gate |
| `CullBitSetWhenFrameChanges` | `+528 |= 0x40` on frame change + `byte_64A068` force-rebuild |
| `MaxLodBiasConstantIsMinusOne` | the `flt_62807C == -1.0` far-branch result == lodCount-1 |

Result: **9 tests, 105 checks, 0 failures.** (Built/linked standalone against
`node_lod.cpp` + `mesh_lod_name.cpp` + `util/coord.cpp` + the test framework; the full
`guild` lib was transiently broken by concurrent wave-6 edits to non-owned files —
sky.cpp/snow.cpp/sprite_scale.h/water_render.h — none of which touch the LOD modules.)

Existing sibling tests still cover the filename strategy: `mesh_lod_name_test.cpp`,
`mesh_attach_lods_test.cpp` (BuildLodFileName / BuildTexturePath / AttachStockObjectLods).

## Completeness / boundaries (rule 8)

- `SelectLodFrame` distance + forced threshold math: **complete, 1:1.**
- The engine-coupled inputs the original reads from live state are caller-supplied via
  `LodObject`/`LodView` (object byte-block fields, the camera/fov/force-rebuild globals) and
  threaded by `SelectLodFrameForNode` + `SetLodSelectView` — no analogue, just the real
  state lifted to parameters so the math is re-entrant and golden-testable.
- `node_lod.cpp`'s `ClassifyBoundingBoxPlanes` is the self-contained 4-arg math subset; the
  canonical 6-arg form (with the `flt_13FD168`/`flt_13FCF3C` running depth-bound globals)
  lives in `scenegraph.cpp` (different agent). Both are distinct C++ overloads of the same
  `0x5ad1f4` math — no ODR clash; both are consumed by existing tests, so left intact.
