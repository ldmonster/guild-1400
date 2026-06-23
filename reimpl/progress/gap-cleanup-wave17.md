# Wave-17 — gap cleanup (W17-CLEANUP)

Three documented deferred items from the wave-15/16 diff docs, each decompiled and
either reconstructed 1:1 (CLOSED) or decompile-confirmed as a genuine boundary
(VERIFIED-BOUNDARY).

Owned files: `src/render/shadow_ground.{h,cpp}`, `src/sim/he.h`,
`tests/unit/shadow_ground_test.cpp`, this doc.

---

## Item 1 — ProjectGroundQuad @0x5f216c tile body — **CLOSED (1:1 + golden)**

**What the decompile showed.** Wave-7/16 left the projected-terrain-tile path as an
inert hook, calling the tile record "engine-private".  Tracing the call site shows
it is NOT opaque:

- `VIBE_Shadow_BuildGroundShadow @0x5f3048` is called from
  `VIBE_Shadow_CastFromLight @0x5f428d` as
  `BuildGroundShadow((int*)v5, (float*)dword_64A028)`.
- BuildGroundShadow's `v48` (the `if (v48)` dispatch guard @0x5f3286) is therefore
  `dword_64A028` — the **live terrain scene record** (the same global used by
  terrain collision @0x427b60, snow @0x42a2cc, weather @0x505df4, scene-load, …).
- So the projected-tile path is the **LIVE in-game path**; RasterizeHeightField is
  the fallback taken only when the scene ptr is null.

The full body (decompile @0x5f216c + disasm) is concretely recoverable.  Recovered
input layout (byte offsets confirmed in the disasm):

| field | access | meaning |
|---|---|---|
| `cellStride0` | `*(int*)(a1+0)` (v53) | height-byte row stride |
| `cellStride`  | `*(int*)(a1+4)` (v6)  | tile-cell divisor (clip/coords ÷ this) |
| `heights`     | `*(int*)(a1+16)` (v64)| per-cell u8 height byte base |
| `xBase/baseY/zBase` | `a1[36]/[37]/[38]` | projection origin (baseY lifted +0.5 = flt_62C26C) |
| `xStep/zStep/yScale` | `a1[40]/[46]/[49]` | projection steps |
| tile grid | `a1+0xE0`, 800 B/outer-row, 100 B/outer-col | per-cell step byte @+0x5E (== a1+318+…), enable/winding bit7, vertBase = `a1[(step>>1)+9]` |

Constants pinned via get_bytes: `flt_62C26C = 0x3f000000 = 0.5`.

**What was reconstructed.**  `ProjectGroundQuad` now performs the full two-phase
walk 1:1:

- Clip rect → tile coords (signed divide by `cellStride`), outer-row guard
  `v33 >= v45` (0x5f2267).
- Outer/inner tile loops; per cell the `v45<8 && v49<8 && enable` grid-window gate
  (0x5f233f) and `stepByte != 0` participation.
- **Phase 1** (0x5f24b3..): per-vertex ground samples into vertexA, lifting Y by
  `heights[j + cellStride0*v61]*yScale + (baseY+0.5)`; bumps `dword_1408A64`
  (vertexCountA) and `a3[2]` (drawCount0), stamps draw0 +68 = colour key, +72 =
  vertex slot.
- **Phase 2** (0x5f268a..): stitches the cell's vertex sub-grid into two-triangle
  quads in the draw pool, with the **winding flip** on the enable bit
  (the original `*v27 >= 0` test): windingA = (TL,BR,BL), windingB = (TL,BL,BR);
  writes the −1.0f (`0xBF800000`) homogeneous-w sentinel into +24/+64, clears the
  +38 edge bit, fills the six-float vertexB UV payload, bumps `dword_1408A68` += 2
  and `a3[3]` += 2.
- Returns `v76` (1 if any cell emitted).

The over-budget dispatch guard (0x5f3295/0x5f32c3) in BuildGroundShadow is now
wired faithfully (estimate `v18*(v17/stride+1)+v46+drawCount0+v17*(v18/stride+1)`
vs `25*(cap>>4)`), and the tile path is dispatched to the real body.

The grid PRODUCER (the terrain-projection subsystem that fills the +318 step/vert
grid in `dword_64A028`) is a separate, out-of-tree subsystem — exactly like
RasterizeHeightField's `heights` buffer, which is also caller-supplied.  The body
is therefore golden-pinned with **synthetic tile inputs** (a portable
`GroundShadowTile` view of precisely the fields the decompile reads), driving the
real entry — no real asset scene required (per wave brief).

**Golden tests (tests/unit/shadow_ground_test.cpp).** Added 6 ProjectGroundQuad
cases: null-tile fallthrough, single-vertex minimal (clip {0,0,0,0}; Y golden
`40*0.25+5.5 = 15.5`), 2×2 quad stitch (windingA indices (0,3,2)), winding flip
(bit7 → (0,2,3)), empty grid (no participating cell → returns 0), and outer-guard
reject.  Full suite: **94 checks, 0 failures.**

---

## Item 2 — He-record alignment-clean accessors — **CLOSED (UBSAN-clean, zero churn)**

**Verdict facts (decompiled).**  `HeRecord` is `GUILD_PACKED`
(`__attribute__((packed))`, alignment 1); `sizeof(HeRecord) == 514`, and the
largest accessor offset is +361 (1 byte) — all accesses are **in-bounds**.

A representative call site confirms the engine does **native unaligned x86**
read/writes: `VIBE_NpcAction_StampTimeAndRequestEntity @0x4c9458`:
```
*(_QWORD*)(result + 82) = qword_13CE852;   // appointment GameTime at ODD +82
*(_DWORD*)(result + 90) = unk_13CE85A;
*(_WORD*)(result + 94)  = unk_13CE85E;
if ((*(_BYTE*)(result + 120) & 2) != 0) ...  // He_Flags
```
+82 is the `He_ApptTime` GameTime (odd offset → genuinely misaligned in the
original).  Our reference accessors reproduce these byte-for-byte — they were 1:1
and benign on x86; the only issue was UBSAN's `-fsanitize=alignment` flagging the
in-bounds-but-unaligned reference binds.

**What was done (he.h only — I own this file).**  Converting all ~907 call sites to
memcpy get/set is NOT mechanical (lvalue writes `He_X(h)=v`, `++He_X(h)`, compound
ops across ~15 consumer files I don't own) and the brief says to document rather
than churn in that case.  Instead, a **zero-churn in-place fix**: the reference
accessors now return through concrete `__attribute__((aligned(1)))` typedefs
(`HeU_i32`, `HeU_u16`, `HeU_GameTime`) so the emitted load/store is the *same*
unaligned access the binary performs, but the compiler knows alignment is 1 and
UBSAN no longer faults.  The lvalue interface is unchanged (`He_X(h)=v`,
`++He_X(h)`, GameTime member writes all still compile/behave identically).

(A `template using` alias drops the attribute on GCC — verified — so concrete
typedefs + a small `HeUnalignedSel` selector are used.)

**Verification.**  A micro-test at a deliberately MISALIGNED record base, built with
`-fsanitize=alignment,undefined -fno-sanitize-recover=all`, now exits 0 (was a
runtime alignment error with the raw `reinterpret_cast<i32*>`), with all lvalue
ops (assignment, `++`, GameTime member write, indexed scratch write) producing the
correct values.  All He consumer suites rebuild and pass unchanged:
charaction_steps3..8, charaction_npcaction_recon2, he_recon3_worldpos (616),
npcaction6/8/9/10/11/12, npctarget_golden, npcevent_reaper_full — **0 failures**.

---

## Item 3 — slider / favorability ConvertX sites — **VERIFIED-BOUNDARY**

All three flagged sites were decompiled / cross-checked against the diff docs:

- **`Coord_ConvertX` @0x5c6b08** truncates toward zero; this was fixed in wave-15
  (`src/play/input_recon_select.cpp` now returns `std::trunc(v)` — confirmed in
  source).  ConvertX `cvttsd2si` semantics, not round-to-nearest.
- **Favorability `(int)v17`** (VIBE_Hud_BuildPersonCard @0x553f30, diff-slice) — a
  slider VALUE side effect computed for the live card render, truncated via
  ConvertX.  Runtime preview, not a persisted golden value.
- **Options Sfx drag preview** / **Text_RenderRichString @0x59d6e8 side effects**
  (diff-hud) — runtime command/preview boundaries (RenderRichString projected to
  plain glyph emission; the drag preview is a transient slider readout).

None of these is a persisted golden value; the rounding idiom (ConvertX = trunc)
is already correct in the canonical helper.  These live in files owned by other
agents and need no source change here — documented as a confirmed runtime/preview
boundary.

---

## Build status

`shadow_ground_test`: 94 checks, 0 failures.  `guild` library + all He consumer
test targets build clean and pass.  No `progress/INDEX.md` edits; no commits.
