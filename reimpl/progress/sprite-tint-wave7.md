# Wave-7 — Billboard node-level effect-tint arm (W7-SPRITETINT)

Owner: W7-SPRITETINT. Files: `src/render/sprite_scale.{h,cpp}` (extend only),
`tests/unit/render_billboard_project_test.cpp` (extend). Closes the rule-8 gap
named in `progress/sprite-render-wave6.md` (the node-level EFFECT-TINT arm of
`VIBE_Particle_UpdateBillboards @0x5AC970`, left documented but unreconstructed
because it needs the node record).

## The gap (from wave-6)

Wave-6 reconstructed `0x5AC970` byte-identically for its three project arms
(depth-fade / no-fade / disabled) plus the always-run per-quad winding/visibility
pass. It explicitly deferred the `type >= 5` arm (`a1+0x215`, the node-level
effect tint that colours every billboard vertex from the node's effect state),
flagging it as needing the node record owned elsewhere. That arm is THIS wave's
deliverable. The three existing project arms + the quad pass are unchanged
(byte-identical).

## Reconstructed this wave

### `gilde.exe 0x5ACAB0` — effect-tint arm of VIBE_Particle_UpdateBillboards (FULL)

Verified instruction-by-instruction from the disassembly at 0x5ACAB0–0x5ACC06.
The arm runs on the **billboards-enabled branch only** (`byte_649D70`), AFTER both
project loops and BEFORE the quad visibility pass:

```asm
loc_5ACAB0:  mov  al, [edi+215h]        ; node type byte (nodeType, +0x215, 0..8)
             cmp  al, 5
             jl   loc_5ACAE0            ; nodeType < 5  -> no tint, go to quad pass
             cmp  al, 8
             jnz  loc_5ACBBC            ; nodeType 5..7 -> pack from node floats
             mov  esi, 1F1FFFh          ; nodeType == 8 -> fixed constant
loc_5ACAC7:  mov  eax, [edi+1CCh]       ; verts container (= a1+460, the wave-6 v2)
             xor  edx, edx
             mov  eax, [eax]            ; verts base = *v2
             test ecx, ecx              ; ecx = vertCount (the wave-6 v3)
             jbe  loc_5ACAE0
loc_5ACAD5:  add  eax, 50h              ; stride 0x50 per BillboardVertex
             inc  edx
             mov  [eax-10h], esi        ; vertex[i] + 0x40 (colorOut) = packed tint
             cmp  edx, ecx
             jb   loc_5ACAD5
```

The pack block (`loc_5ACBBC`, nodeType in 5..7) reads three node tint floats and
narrows each to a byte via `VIBE_Coord_ConvertX @0x5C6B08` (x87 frndint with
RC=truncate-toward-zero) + `fistp` + an 8-bit move (`mov al` / `movzx` / `and 0xFF`):

```
R = (u8)trunc(*(float*)(a1+0x5C))      ; shl 16
G = (u8)trunc(*(float*)(a1+0x60))      ; shl 8
B = (u8)trunc(*(float*)(a1+0x64))      ; low byte
packed = (R<<16) | (G<<8) | B          ; 0x00RRGGBB
```

x87 trace (exact): `fld f60; fld f5C; ConvertX; fxch; ConvertX; fxch;
fistp(f5C)->al->dl; fistp(f60)->al; edx = (dl<<16)|((al&0xFF)<<8); fld f64;
ConvertX; fistp; esi = (al&0xFF) | edx`. The truncate-toward-zero + low-byte
narrowing is the SAME conversion the wave-6 depth-fade alpha uses (`TruncToByte`,
reused here).

`nodeType == 8` constant `0x1F1FFF` decodes to R=0x1F (31), G=0x1F (31), B=0xFF
(255) — a fixed light/glow tint (`mov esi, 1F1FFFh` at 0x5ACAC2). nodeType 8 is
classified for sound/`'s'` nodes in `object_lifecycle4.cpp`; the tint is applied
regardless of what 8 means semantically — the arm is purely `>=5` gated.

Key semantic, byte-verified: the broadcast loop is **unconditional over the
count** — it does NOT test the per-vertex live bit (+0x4C bit7), unlike the
project arms. Dead vertices get the tint written too. And the tint **overwrites**
the per-vertex `colorOut = colorSrc` copy that the project arm just wrote (the
tint wins for `nodeType >= 5`).

Constants recovered with get_bytes:
- `dbl_628074` = 255.0 (`00 00 00 00 00 E0 6F 40`) — the alpha cap (wave-6, unchanged)
- tint constant `0x1F1FFF` is an immediate operand, not a global.

### New entry points (added to `sprite_scale.{h,cpp}`)

```cpp
bool BillboardEffectTintColor(u8 nodeType, float tintR, float tintG, float tintB,
                              u32& outColor);   // the pack (returns false if <5)
void BillboardApplyEffectTint(BillboardVertex* verts, unsigned vertCount,
                              u8 nodeType, float tintR, float tintG, float tintB);
```

`BillboardEffectTintColor` is the pure colour computation (split out so the pack
is golden-testable independent of the broadcast). `BillboardApplyEffectTint` is
the broadcast loop. nodeType/tint floats are passed explicitly (re-entrant /
testable) — they are the node record's +0x215 type byte and +0x5C/+0x60/+0x64
tint floats. No symbol clash: grepped `src/**`, no existing `EffectTint` /
`0x1F1FFF` / tint billboard symbol; `nodeType` exists as a field name on several
SceneNode mirrors (+0x215, 0..8) which matches the type byte exactly.

The three project arms (`ProjectBillboardVertices`) and the quad pass
(`BillboardQuadVisibilityPass`) are **unchanged / byte-identical** to wave-6.

## Call order within 0x5AC970 (now complete)

On the `byte_649D70` (enabled) branch:
1. project arm — depth-fade (`byte_649DD8`) OR no-fade — writes screenX/Y/invZ +
   per-vertex `colorOut = colorSrc` (+ alpha when fading);
2. **effect-tint arm (this wave)** — if `nodeType >= 5`, overwrite every vertex's
   `colorOut` with the broadcast tint;
3. quad visibility pass.

On the `!byte_649D70` (disabled) branch: project + byteOut only, then quad pass
(the tint arm is skipped — it lives inside the enabled branch).

Re-entrant call sequence (for a bind site, enabled path):
```cpp
render::ProjectBillboardVertices(verts, count, bp);
render::BillboardApplyEffectTint(verts, count, node.nodeType,
                                 node.tintR, node.tintG, node.tintB); // +0x5C/+60/+64
render::BillboardQuadVisibilityPass(quads, quadCount);
```

## CityView3D / universe frame handoff (NOTE — no bind-site edit)

The wave-6 doc already documented the project handoff: the live bind sites are
`src/play/universe_render.cpp` (~L342) and `src/play/city_view3d.cpp` (~L1375),
which call `render::ProjectObjectVertices(..., projectAll=false)` (the mesh arm in
`object_project.cpp`) right after `ComputeVertexClipFlags`, in the per-node object
pass of `VIBE_Render_ProcessSceneNode @0x5ADD1C`.

To reproduce the tint, a bind site that switches a billboard/sprite node to
`ProjectBillboardVertices` (the depth-fade-capable arm) should, immediately after
it and before the draw-list append / quad pass, also call
`render::BillboardApplyEffectTint(verts, count, node.nodeType, tintR, tintG,
tintB)` reading the node's +0x215 type byte and +0x5C/+0x60/+0x64 tint floats.
Gate: it is a no-op for `nodeType < 5`, so it is safe to call unconditionally for
billboard nodes — non-tinted nodes (type < 5) are unchanged. This is a documented
handoff for the universe/city-view owner; NO bind-site file was edited here (the
wave-6 ProjectObjectVertices arm is still what is wired today, so the rendered
output is unchanged until that owner switches a node to the billboard arm).

## Relationship to `object_project.{h,cpp}` (another agent, SAME address)

`object_project.cpp` reconstructs `0x5AC970` as `ProjectObjectVertices` but only
the mesh / no-fade arm + winding cull, and explicitly defers the fade alpha and
the tint. `sprite_scale.cpp` is the COMPLETE function: all three project arms +
fade + the tint arm (this wave) + the quad pass. No symbol clash (distinct names,
distinct structs). `paintbox_shape.*` (read, not edited) is the screen-space
sprite blit family, unrelated to the world-billboard projection.

## Tests

`tests/unit/render_billboard_project_test.cpp` — extended from 9 tests/26 checks
to **16 tests / 59 checks**, all pass. New tint tests (golden values hand-derived
from the disasm pack, not from the source):
- `ColorBelowFiveNoTint` — nodeType 0..4 returns false, outColor untouched.
- `ColorTypeEightIsConstant` — nodeType 8 -> 0x1F1FFF (R=0x1F,G=0x1F,B=0xFF).
- `ColorPackedFromFloats` — (R,G,B) -> 0x00RRGGBB for types 5/6/7.
- `ColorTruncatesTowardZeroAndNarrows` — 200.9->200, 0.9->0, 127.5->127; 8-bit
  narrowing 256.0->0, 257.0->1, 511.0->255 (only low byte survives `mov al`).
- `BroadcastOverwritesAllColorOut` — every vertex's colorOut becomes the tint.
- `BroadcastIgnoresLiveBit` — a bit7-clear (dead) vertex is tinted too (the loop
  is unconditional over the count).
- `BroadcastNoOpBelowFive` — nodeType 4 leaves colorOut untouched.

Build note: the full `libguild.a` link currently fails on OTHER agents'
in-progress files (`src/render/mirror_project.h` static_assert,
`src/render/shadow_ground.cpp` `xform` member) — NOT in any owned file. The owned
files compile clean; the test was built by linking
`sprite_scale.o + shape_blit.o + colorformat.o + test_main.o` directly (the
wave-6 method). The pre-existing `render_sprite_scale_test` (299 checks) still
passes against the modified TU.

## Status: COMPLETE

`0x5AC970` is now FULLY reconstructed — all three project arms + depth fade +
the node-level effect-tint arm (this wave) + the per-quad visibility pass — with
no remaining documented non-deliverables. Golden-tested, handoff documented, no
bind-site or other-agent file edited.
