# Wave 4 — W4-C: material/texture fidelity (24-bit BMPs + seasonal texture sets)

> **VERIFIED + AMENDED** — see the `VERIFICATION (V-B)` section at the end of
> this file: every W4-C claim was checked against the binary once IDA was back
> online. The season rule/order/gate claims CONFIRMED; the .TXS format
> CONFIRMED (engine loader found); the resolve-layer SEMANTICS were partially
> REFUTED and fixed (set-0 rows bind at load for EVERY adopted mesh, empty
> rows keep the current binding — not name0); the save-season claim REFUTED
> (no save season byte exists); and the @0x5da34c 24-bit palettizer gap is
> CLOSED (reconstructed 1:1 in render/texture_palettize.{h,cpp}, replacing
> the default-off RGB stand-in as the live path). Numbers quoted below are
> the wave-4 state; the recalibrated ones live in the verification section.

Scope: the 57/951 UNBOUND material slots of the AUGSBURG 3D city frame left
named by wave-3 (progress/terrain-colorkey-wave3.md): (a) 23 slots resolving to
24-bit BMPs, (b) 34 unresolvable names (mostly seasonal vegetation), (c) the
genuinely-absent BMPs. IDA MCP was OFFLINE — every claim below is anchored to
decompile excerpts already in-tree plus the SHIPPED BYTES (cross-validated
archive-wide, the sanctioned evidence source).

---

## 1. Seasonal texture sets — RECONSTRUCTED and LIVE

### The mechanism (full evidence chain)

* **Season**: `season = day % 4` — `VIBE_GameTime_GetSeasonFromDay` @0x58339c
  (in-tree: sim/character_render5.cpp `GetIndexThunk`, sim/npc_daily.h
  `SeasonFromDay`). Season SEMANTICS are decompile-pinned by
  `VIBE_Weather_ApplySeasonalMeshes` @0x505df4 (render/weather_recon.h
  `WeatherSeasonSuffix`): **0=spring, 1=summer, 2=autumn, 3=winter**. The
  independent per-season work-window tables (flt_6476FC/flt_64770C =
  {8,7,8,9}→{20,21,20,19}, sim/character_state.cpp) agree (index 1 = longest
  day, index 3 = shortest). New game starts at **day 0** (newgame_apply.cpp
  0x533b9d GameTime_Set day 0, 06:00) → **season 0**.
* **Application**: `VIBE_Scene_ActivateAndRefreshCharacters` @0x506df4 sets
  `byte_634484 = season` then traverses `HideFoliageDecor`
  (play/scene_recon2_orchestrator.cpp); `VIBE_Object_HideFoliageDecor`
  @0x506388 calls `SelectTextureSet(node, byte_634484)` for nodes whose name
  starts `pfl_` / `vg_` / `!vg_` (case-sensitive strncmp;
  sim/object_lifecycle2.cpp); `VIBE_Object_SelectTextureSet` @0x5b3f54 range-
  gates the set (`set >= *(mesh+484)` → no-op) and swaps each poly group's
  texture ("load + replace"; a not-found new texture keeps the old —
  sim/object_lifecycle8.cpp). **Season index == texture-set index.**
* **The set DATA — recovered from shipped bytes** (new:
  `render/texture_set_table.{h,cpp}`): every set-capable `.bgf` in
  Resources/Objects.BIN ships a sibling **`<member>.TXS`**:

  ```
  u32 magic = 0x23F209AE
  u32 setCount                 (== *(mesh+484) of 0x5b3f54)
  u32 namesPerSet              (== the .bgf fast-chunk materialCount)
  char names[setCount*namesPerSet][]   NUL-terminated, SET-major;
                                       "" = no swap for that material
  ```

  Archive-wide cross-validation (pinned in `materials_w4c_e2e_test`):
  590/590 sidecars parse and consume their bytes exactly;
  `namesPerSet == materialCount` in **588/589** .bgf+.TXS pairs (outlier:
  `Gebaeude/gb_Wohnsitz/gb_PALAZZO_B_0.bgf`, 18 vs 17 — rejected by the count
  gate, no swap applied); of 14718 rows: 8647 empty, 4109 identical to the
  material's name0, 117 `<name0>_<letter>`, 1845 cross-base remaps (e.g.
  winter roofs → `SNOW_256_NS` — the rows are real NAMES, **not** a string
  rewrite rule). Across every seasonal 4-set table the order is invariant
  with ZERO contradictions: **set 0=_F (Frühling), 1=_S (Sommer), 2=_H
  (Herbst), 3=_W (Winter)** — exactly the 0x505df4 season indices.

So the wave-3 mystery name `vg_nm_BIG_Laub_A_s01` is the mesh's BASE material
name (resolves to nothing by design); the LAUBKRONE mesh's `.TXS` supplies
`VG_NM_BIG_LAUB_A_S01_F/_S/_H/_W` per set — including cross-base remaps the
suffix-rewrite hypothesis would MISS (its row for base `..._s03` is `..._S01_F`;
`..._s03_F` does not exist in Textures.BIN).

### What landed (the texture-swap leaf at the resolve layer)

* **`render/texture_set_table.{h,cpp}`** (new): `ParseTextureSetTable` (the
  byte format above), `TextureSetTable::NameFor(set, material)` (empty row →
  null = keep), `IsFoliageMeshName` (the 0x506388 strncmp gate — validated
  case-sensitively over all 40 shipped foliage sidecars),
  `SeasonTextureSetFromDay` (day % 4 @0x58339c).
* **`play/real_texture_source.{h,cpp}`** — `BuildTableFor` (the in-tree
  per-material name→Textures.BIN resolve, the VIBE_Mesh_LoadBgfFile @0x5D2348
  material loop) now applies the ACTIVE texture set for foliage members: the
  sidecar is fetched (installable `SetTxsFetch` hook, else a lazy self-mount
  of Resources/Objects.BIN through the Mount() filesystem), count-gated, and
  the set's row replaces each material's lookup name (failed/empty row keeps
  name0 — the 0x5b3f54 not-found arm). Cache identity carries the set index
  (a season change rebuilds; non-foliage keys unchanged).
  `MaterialTextureTable` gains `appliedTextureSet` / `swappedMaterials` /
  `Entry::setName` (additive).
* **Default = set 0** (`RealTextureSource::SetDefaultActiveTextureSet`,
  process default; instances seed from it): the day-0 / season-0 (_F) state
  the engine applies on scene activation. `SetActiveTextureSet(-1)` restores
  the pre-activation load state (every material binds its own name0).
* **Wired LIVE with no bind-site edits** (rule 13): city_view3d's `bindFor`,
  the universe driver's `buildTextureBind` and scene_view all resolve through
  `BuildTableFor`, so the day-0 seasonal binds flow into every existing city
  render path automatically.

### Results (pinned in tests/e2e/materials_w4c_e2e_test.cpp)

AUGSBURG, 110 rendered members, 951 material slots, 320x240 overview frame:

|                              | set -1 (before) | set 0 (after) |
|------------------------------|-----------------|---------------|
| bound (8-bit)                | 894             | **914** (47 via .TXS rows) |
| 24-bit (unbindable record)   | 23              | 23 (named gap, see §2) |
| unresolvable names           | 34              | **14** |
| frame white-default-gray px  | 11120           | **8271** |
| per-instance bound materials | 3142            | 3636 |

ALL 20 seasonal-vegetation slots resolve. The remaining 14 unresolved are the
genuinely-absent BMPs (`dc_dchz_05a_1n_rautenziegel`,
`dc_mrwr_Schiefer_moos_Stdtmr_Ka2` x4, `mt_silb_01d_1n_konti`,
`sf_mrwr_*` x7, `sf_putz_1a_1n_kirch_Uni`) — absent from Textures.BIN, the
original's load fails too: the level-shaded white default IS the engine state.
Frame dump: `/tmp/guild_w4c_materials.ppm` (foliage now textured).

### Named gaps (rule 8) — seasonal

* **The live season source**: the resolve layer defaults to set 0 (= day 0).
  A session advancing days must call
  `SetActiveTextureSet(render::SeasonTextureSetFromDay(day))` + rebind (the
  engine re-applies on scene activation @0x506df4) — see HANDOFF below. The
  in-game season byte also lives in saves (io/save.h `season` byte_6477A1 /
  header +0x30); loading a save into a city render should drive the set from
  it.
* **applyTextureSwap leaf internals** @0x5b3f54: the per-poly-group record
  surgery (mesh+480 group table, the +528|=4 cache flush) remains hook-level
  in sim/object_lifecycle8; the resolve-layer reconstruction reproduces its
  observable binding (validated by the row↔material correspondence).
* **Winter building snow / floor seasonal textures**: applied by OTHER callers
  (`VIBE_Object_HideFoliageByState` @0x506430 building-state loop;
  `VIBE_Weather_ApplySeasonalMeshes` @0x505df4 floor layers, _SNOW/_HERBST/
  _FRUEHLING floor suffixes) — not in the foliage gate, irrelevant for the
  day-0 default; needed only once seasons advance.
* **gb_PALAZZO_B_0.TXS** (18 rows vs 17 materials): mapping unknowable without
  the 0x5b3f54 leaf decompile; count-gate skips it (no swap — its set-0 state
  is the authored default anyway).
* Foliage gate proxy: 0x506388 tests the SCENE-NODE name; the resolve layer
  gates on the mesh member basename — byte-identical for all shipped foliage
  content (40/40 sidecar basenames match case-sensitively).

---

## 2. 24-bit BMP materials — DECISION: stand-in behind a default-OFF switch

The 23 slots resolve to 24-bit BMPs (e.g. `Bjoern/hz_Dachleisten_Dunkel_AA.bmp`,
~2000 distinct colours). The original SOFTWARE path palettizes such sources via
the **VIBE_Texture_PalettizeSurface family @0x5da34c** (LoadSoftPalettize,
HiColTab "colours actually used") — UNRECONSTRUCTED (needs the decompile; the
HW path uploads via LoadAndStretchTexture @0x5dea50). The original's OUTPUT for
these materials is a ≤256-colour palettized approximation; the in-tree affine
RGB kernel (play::RasterTexturedTriangleAffine — the SAME kernel the legacy
person pass and the wave-4 person binds draw the 24-bit _DYNAMIC/Character BMPs
with) would draw the TRUE colours: chromatically closer than the original, but
NOT the original's algorithm.

**Decision (rules 1/8):** the 1:1 posture stays the DEFAULT — under the missing
palettizer the materials render the engine's level-shaded white default. The
RGB route lands as an EXPLICIT, NAMED stand-in behind an option that defaults
to OFF:

* **`render/texture.{h,cpp}`**: `Rgb24MaterialStandInEnabled()` /
  `SetRgb24MaterialStandIn(bool)` — default `false`; the header documents the
  @0x5da34c gap, both modes, and the bind-cache caveat (flip before first
  bind).
* Both modes are PIXEL-PINNED (`material_rgb24_standin_test`): OFF = every
  covered pixel is the linear gray `PackColor(L,L,L)` of the white default at
  light level L; ON = the affinely-sampled true 24-bit texels (565-quantised).
  The kernel is exercised over a REAL 24-bit city BMP in the e2e (1679 px).
* The 24-bit accounting is pinned: 23 slots before AND after the seasonal fix
  (it is a palettizer gap, not a naming gap).

When @0x5da34c is decompiled, the palettizer replaces this switch outright.

---

## HANDOFF → bind-site owners (files not editable this wave)

**(a) city pass 24-bit stand-in — `src/play/city_view3d.cpp`**: the person pass
already binds 24-bit BMPs (`bindFor(member, /*rgbAffineFallback=*/true)`,
`BoundTex::rgb` → `RasterTexturedTriangleAffine` in `CV3D_SpanTextured`). To
honour the switch on the CITY pass, change the city-instance bind call(s) from
`bindFor(member)` to:

```cpp
const CityView3D::MatBind* mb =
    bindFor(member, render::Rgb24MaterialStandInEnabled());   // texture.h switch
```

(matBinds_ caches per member — evaluate the switch before the first city bind,
or key the cache on it.)

**(b) universe driver 24-bit stand-in — `src/play/universe_render.cpp/.h`**:
mirror the CV3D person bind: add `const render::DecodedBmp* rgb = nullptr;` to
`UniverseFrameDriver::BoundTex`; in `buildTextureBind`'s skip branch:

```cpp
if (!bmp || !bmp->ok || bmp->width <= 0 || bmp->indices.empty()) {
    if (render::Rgb24MaterialStandInEnabled() && bmp && bmp->ok &&
        bmp->width > 0 && !bmp->rgba.empty())
        boundTex_[(std::size_t)mi].rgb = bmp, ++boundMaterials_;
    continue;
}
```

and in `UFD_SpanTextured`, before the white-default fallback:

```cpp
if (bt && bt->rgb &&
    play::RasterTexturedTriangleAffine(fb, vp[0], vp[1], vp[2], *bt->rgb) > 0) {
    if (d) d->addTexturedPoly();
    return 1;
}
```

**(c) live season wiring — session owner (`sdl_session` / `full_session`)**:
on a day change (or save load), re-derive the set and invalidate binds:

```cpp
// the 0x506df4 re-activation: byte_634484 = GetSeasonFromDay()
int set = render::SeasonTextureSetFromDay(clock.day);     // texture_set_table.h
play::RealTextureSource::SetDefaultActiveTextureSet(set); // before view creation
// for a LIVE view: its RealTextureSource needs SetActiveTextureSet(set) plus a
// matBinds_/frameBinds_ rebuild (CityView3D would need a small forwarding
// method — the per-set table cache in RealTextureSource already rebuilds).
```

For a save load, drive `set` from the save's season byte (io/save.h
`SaveScalarBlock::season`, byte_6477A1) instead of the day counter.

---

## Files

* `src/render/texture_set_table.{h,cpp}` — NEW: .TXS parser + foliage gate +
  season→set rule (provenance + archive validation in the banner).
* `src/render/texture.{h,cpp}` — the 24-bit material stand-in switch (default
  OFF; @0x5da34c named-gap docs).
* `src/play/real_texture_source.{h,cpp}` — seasonal set application in
  `BuildTableFor` (+ `SetTxsFetch` / `SetActiveTextureSet` / process default,
  lazy Objects.BIN sidecar mount; additive `MaterialTextureTable` fields).
* `tests/unit/render_texture_set_table_test.cpp` — NEW: 5 tests / 43 checks
  (golden set-major parse, malformed rejects, 0x506388 gate, season rule,
  switch default).
* `tests/unit/material_seasonal_resolve_test.cpp` — NEW: 6 tests / 37 checks
  (set-0/_F + set-3/_W binds + texel pins, per-set cache identity, empty/
  not-found rows keep name0, -1 disable, non-foliage ungated, count gate,
  process default).
* `tests/unit/material_rgb24_standin_test.cpp` — NEW: 2 tests / 21 checks
  (both 24-bit modes pixel-pinned).
* `tests/e2e/materials_w4c_e2e_test.cpp` — NEW guarded: 3 tests / 52 checks
  (590-sidecar survey + set-order zero-contradiction pin + BUCHE table pin;
  AUGSBURG accounting 894/23/34 → 914/23/14 + frame gray 11120 → 8271 pins;
  real 24-bit BMP through the kernel). Dumps /tmp/guild_w4c_materials.ppm.

## Test results

* New suites: 43 + 37 + 21 + 52 checks — green.
* Full ctest: **1420/1420 pass** (portable Debug + GUILD_GAME_DIR), including
  all pre-existing pixel-pinned suites — no recalibration was needed (the
  wave-3 pins are colour-family pins, untouched by the foliage binds).

---

# VERIFICATION (V-B) — claim -> binary evidence -> verdict/fix

IDA Pro MCP back ONLINE (gilde.exe, imagebase 0x400000). Every W4-C claim
re-derived from the decompile; divergences fixed 1:1; the named 24-bit gap
closed. Asset-side numbers re-measured over the shipped archives.

## 1. Season rule + application chain — CONFIRMED

| claim | binary evidence | verdict |
|---|---|---|
| `season = day % 4` @0x58339c | decompiled + disasm: `mov ecx,4; idiv` (SIGNED idiv) over the dword at `qword_13CE852` (the game-clock day) | **CONFIRMED** (signed remainder; day >= 0 in practice) |
| season semantics 0=spring 1=summer 2=autumn 3=winter @0x505df4 | `VIBE_Weather_ApplySeasonalMeshes`: season 3 -> `"%s_SNOW"` floor layers, 2 -> `"%s_HERBST"`, 0 -> `"%s_FRUEHLING"`, 1 -> the suffix-less base name | **CONFIRMED** |
| `byte_634484 = season` @0x506df4 | `VIBE_Scene_ActivateAndRefreshCharacters` 0x506f27: `byte_634484 = GetSeasonFromDay(&qword_13CE852)` (gated `(u8)s <= 2 \|\| s == 3`), then `TraverseTree(HideFoliageDecor)` | **CONFIRMED** |
| foliage gate `pfl_`/`vg_`/`!vg_` case-sensitive @0x506388 | `VIBE_Object_HideFoliageDecor` via `VIBE_Util_StrncmpN` @0x5e9ee0 — plain byte compare, no tolower | **CONFIRMED** (and the season byte is passed DIRECTLY as the set index — no mapping table) |

## 2. .TXS sidecar — ENGINE READER FOUND; format CONFIRMED; semantics AMENDED

* **Loader**: `VIBE_Mesh_LoadTextureSet` @0x5d2240 (found via `find_bytes`
  `AE 09 F2 23` -> 0x5d219d/0x5d2285): builds `"<member>.TXS"`, magic
  compare vs **603064750 = 0x23F209AE**, reads `u32 setCount`,
  `u32 namesPerSet`, then `setCount*namesPerSet` NUL-strings SET-major into
  64-byte records (`"d3_io:LoadtextureSet"`). The matching dev-tool writer
  `VIBE_Mesh_SaveTextureSet` @0x5d20dc (no live callers) confirms the format
  field-for-field. **W4-C's byte format: CONFIRMED exactly.**
* **Adoption** (NEW): `VIBE_Mesh_LoadAndRegister` @0x5d32d4 loads the sidecar
  for **EVERY mesh**; `VIBE_Model_LoadFastChunk` @0x5f8a0e adopts it iff
  `setCount > 0 && namesPerSet == fast-chunk materialCount` ->
  mesh+480/+484/+516. Rejected -> a synthesized **1-set** table from the
  material names (0x5f8d84) and the orphan table is freed (0x5d33c0).
* **gb_PALAZZO_B_0 (18 vs 17)** — RESOLVED: rejected at LOAD by that gate
  (not by SelectTextureSet); the mesh binds its material names via the
  synthesized 1-set table; any `set>0` application no-ops through the
  0x5b403b gate. Observable behaviour identical to W4-C's count-gate skip.
* **SET-0 ROWS BIND AT LOAD** (NEW, was a divergence): the fast-chunk texref
  loop 0x5f8c14 calls `VIBE_Texture_LoadByName` on the **name-table row of
  set 0** — NOT on the .bgf material string. Asset survey: **349 of 3979
  adopted set-0 rows differ from the material name (318 non-foliage —
  banks, walls, characters, furniture)**; 0 set-0 rows are empty. W4-C
  (name0 + foliage-only swap) rendered those 349 wrong; **FIXED** in
  `RealTextureSource::BuildTableFor` (set-0 rows bind for every adopted
  member; the season set applies on top of that baseline for foliage).
* **EMPTY ROW = KEEP CURRENT** (AMENDED): `VIBE_Object_SelectTextureSet`
  @0x5b3f54 decompiled IN FULL — `set == *(obj+381)` -> return 1 (already
  active); `set >= *(mesh+484)` -> return 0 (NO swap, the current set
  stays); per material slot the 64-byte row at
  `*(mesh+516) + (materialCount*set + m)*64` is loaded; an EMPTY row skips
  the slot (0x5b4099 — the CURRENT texture stays, NOT name0); a load failure
  keeps the old texture and clears the return flag (`xor cl,cl` at 0x5b4226
  -> `"SelectTextureSet: cannot replace %s with %s"`). The cumulative
  building-upgrade application (`VIBE_Object_HideFoliageByState` @0x506430
  loops `SelectTextureSet(i)` for i = 0..upgradeLevel on NON-foliage nodes)
  is exactly why empties mean "keep". Resolve-layer fallback for
  empty/unloadable season rows is now the SET-0 binding. (For all 40 shipped
  foliage sidecars the two fallbacks coincide — 0 divergent rows — so no
  visible foliage change.)
* **Set order 0=_F 1=_S 2=_H 3=_W — CONFIRMED engine-side**: the season byte
  is passed directly as the set index (0x5063e4), all 40 foliage sidecars
  are 4-set, zero suffix contradictions archive-wide. Sets are a GENERAL
  variant mechanism elsewhere (building upgrade levels @0x506430, scaffolds
  @0x5063f0, character head variants @0x57c548 `VIBE_Character_
  ApplyHeadVariant`, combat flags, save restore @0x5a986c) — non-foliage
  sidecars ship setCounts 1..9.
* **Material name selector** (NEW): the engine prefers the SCRIPT material's
  **name2** (record +128 — `VIBE_Mesh_LoadBgfFile` 0x5d31b1 arms; the fast
  chunk bakes it as its preferred second string, the 0x5f8b88 `|1`-flag
  load), else name0; the script's name1 is never loaded by the fast path.
  42 shipped fast-chunk materials rely on this (reflection-map props with
  empty name0). FIXED in BuildTableFor (`name2 ? name2 : name0`).

## 3. Live season source — W4-C's save-byte claim REFUTED

* `byte_6477A1` is **NOT** a season byte: all ~100 xrefs are
  `VIBE_MeisterAi_*` / `VIBE_Ai_CalcBankmeister` / stock-economy functions
  (the money-rate scalar, as documented elsewhere). The io/save.h `season`
  note in the W4-C handoff was wrong.
* The TRUTH: the season is ALWAYS derived live from the game-clock day —
  `byte_634484 = GetSeasonFromDay(&qword_13CE852)` at scene activation
  @0x506df4, whose callers are `VIBE_GameLogic_InitOrLoadSession` @0x533a54,
  `VIBE_Save_LoadGameFile` @0x5a7604, `VIBE_Scene_RunMainFrameLoop`
  @0x50f0c0, `VIBE_GameLogic_ProcessTurnActions` @0x52f8d0 (the day
  advance), and the building-enter paths. A loaded save restores the day
  counter; the season follows from it. **No save season byte exists.**
* Wiring: the resolve layer's process default stays 0 (day 0). The exact
  session-side hook (sdl_session/full_session — session owner's files):

  ```cpp
  // the 0x506df4 re-activation: byte_634484 = GetSeasonFromDay(clock day)
  // call ON SESSION INIT, ON SAVE LOAD and ON DAY ADVANCE (0x52f8d0):
  int set = render::SeasonTextureSetFromDay(clock.day);   // day % 4 @0x58339c
  play::RealTextureSource::SetDefaultActiveTextureSet(set); // before views
  // a LIVE view additionally needs its instance updated + binds rebuilt:
  //   view.textureSource().SetActiveTextureSet(set);  + matBinds_/frameBinds_
  //   invalidation (the per-set table cache rebuilds automatically).
  ```

## 4. The 24-bit palettizer — GAP CLOSED (1:1 reconstruction)

The full software call tree decompiled and reconstructed:

```
SelectTextureSet 0x5b3f54 / LoadBgfFile 0x5d2348 / LoadFastChunk 0x5f87b8
  -> VIBE_Texture_LoadByName        0x5da714  (name->record; width==height
                                               gate via Bmp_ReadHeaderInfo
                                               0x5f0c10: planes==1, bpp 8|24,
                                               compression<=1; +125 = bpp)
  -> VIBE_Texture_UploadToSurface   0x5db234  (software arm, byte_649D70==0)
  -> VIBE_Texture_LoadSoftPalettize 0x5da34c  (gated dword_1406A64)
       -> VIBE_Bmp_LoadBuffer       0x5f0ce4  (flags=7; 24-bit sources:)
            -> VIBE_Quant_BuildPalette 0x6029f0 (maxColors=256, dither=1)
                 InitLookupTables   0x602aa4  (3 channel interleave tables:
                                               R->bits 2,5,8,11,14; G->1,4,
                                               7,10,13; B->0,3,6,9,12; +
                                               the d^2 table)
                 AllocColorNodes    0x602be0  (6 octree levels, 24-byte nodes)
                 BuildHistogram     0x602d2c  (15-bit interleaved histogram;
                                               leaf sums accumulate the 5-bit
                                               BUCKET CENTRE (c&0xF8)+4;
                                               ancestor masks + min-heap)
                 HeapSiftDown       0x602f3c
                 HeapReduceColors   0x603068  (merge least-populated leaves
                                               up the octree to <= 256)
                 TreeCollectPalette 0x603180  (depth-first, child bit 7..0
                                               FIRST; entry = rounded mean)
                 FindClosestColor   0x603a30  (sq distance; query bucketed)
                 MapImageToPalette  0x6033b4  (SERPENTINE Floyd–Steinberg,
                                               16ths accumulators in i16,
                                               (acc+8)>>4 consumption, +-20
                                               error clamp, lazy 32768-entry
                                               RGB555 closest-colour cache;
                                               or the undithered table arm)
                 CopyPaletteEntries 0x602a70  (planar 256R/256G/256B out;
                                               PERSISTENT arrays — tail
                                               entries keep the previous
                                               load, indices never reach it)
       -> used-colour compaction + VIBE_HiColTab_FindOrBuild 0x5da04c
          (ALREADY reconstructed: render/texlight_recon.h HiColTabBank +
          hicoltab.cpp AddEntry 0x5d9db8 — only renumbers indices into the
          shared 16-bit shading banks; texel COLOURS are fixed by the
          quantizer)
```

* **NEW `src/render/texture_palettize.{h,cpp}`** — the whole `VIBE_Quant_*`
  family translated 1:1 (provenance per function; Hex-Rays register soup
  mapped in comments; the dither arm's exact buffer layout recovered from
  the disasm: two `3*(width+2)`-i16 row-error buffers, the first pre-zeroed;
  serpentine row turnaround `a1 += 3w+3` after odd rows). Public entries:
  `QuantBuildPalette(rgb, w, h, maxColors, dither, outIdx, outPal768planar)`
  and `PalettizeDecodedBmp(DecodedBmp&)` (fills `indices` + interleaved
  768-byte `palette`, keeps `rgba` as source truth for the compat stand-in).
* **Wired LIVE** (rule 13): `RealTextureSource::DecodeAndPalettize` runs it
  on every 24-bit decode, so the EXISTING palettized bind paths
  (city_view3d `bindFor`, the universe driver's `buildTextureBind`, the
  person pass) consume 24-bit materials exactly like 8-bit ones — no edits
  to those files needed. The former default-off RGB stand-in
  (`SetRgb24MaterialStandIn`) is KEPT for API compatibility but is
  unreachable on the default path (documented in render/texture.h).
* **Cross-validation**: the serpentine-dither index map was reproduced by an
  INDEPENDENT word-level Python reference written directly from the
  decompile — byte-identical output on the regression ramp.

## Recalibrated results (tests/e2e/materials_w4c_e2e_test.cpp, AUGSBURG 320x240)

|                              | W4-C set -1 | W4-C set 0 | V-B set -1 (debug) | V-B set 0 (ENGINE load state) |
|------------------------------|------------|-----------|--------------------|-------------------------------|
| bound (palettized)           | 894        | 914       | **917**            | **937** (143 via .TXS rows)   |
| 24-bit unbindable            | 23         | 23        | **0**              | **0** (palettizer live)       |
| unresolvable names           | 34         | 14        | 34                 | **14** (the absent BMPs)      |
| frame white-default-gray px  | 11120      | 8271      | **11043**          | **8201**                      |
| per-instance bound materials | 3142       | 3636      | **3177**           | **3671**                      |

`viaTXS` 47 -> **143**: every adopted material binds via a row now (the
0x5f8c14 load), not only the foliage swaps. The 14 remaining unresolved are
the genuinely-absent BMPs (white IS the engine state). New golden pins: the
real 24-bit `hz_Dachleisten_Dunkel_AA` palettizes to 143 colours, indices
hash 2456635725, `pal[0]=(132,124,84)` — pinned byte-for-byte.

## Files (V-B)

* `src/render/texture_palettize.{h,cpp}` — NEW: the VIBE_Quant_* palettizer.
* `src/render/texture_set_table.h` — banner rewritten to the verified engine
  truth (loader/writer/adoption/empty-row semantics + survey numbers).
* `src/play/real_texture_source.{h,cpp}` — engine-truth BuildTableFor
  (sidecar adoption for every mesh, set-0 baseline, season-over-baseline for
  foliage, set>=setCount keeps set 0, name2>name0 selector, 24-bit
  palettization on decode; `-1` documented as reimpl-only debug).
* `src/render/texture.h` — the 24-bit policy block flipped to "gap closed";
  stand-in switch kept as compat (unchanged default false).
* `tests/unit/render_texture_palettize_test.cpp` — NEW: 6 tests / 162 checks
  (bucket-centre goldens, collect-order pin, undithered arm, the
  cross-validated serpentine dither regression map, 1024->256 reduce,
  DecodedBmp wiring).
* `tests/unit/material_seasonal_resolve_test.cpp` — rewritten to the engine
  semantics: 10 tests / 53 checks (set-0 row binds, empty/unloadable season
  rows keep set 0, set>=setCount keeps set 0, non-foliage binds set 0 not
  the season, set-0 row beats the material name, name2 preference, count
  gate, -1 debug, process default).
* `tests/e2e/materials_w4c_e2e_test.cpp` — pins recalibrated (table above) +
  the hz_Dachleisten palettize goldens; 3 tests / 74 checks.
* `tests/unit/material_rgb24_standin_test.cpp` — re-scoped as the LEGACY
  compat pins (unchanged behaviour, 21 checks).

## Test results (V-B)

* Owned suites: 162 + 53 + 43 + 21 + 74 = 353 checks — green
  (GUILD_GAME_DIR set for the guarded e2e).
* Full ctest: see the final report (other agents' concurrent WIP may affect
  unrelated suites; my targets verified green).

## Named gaps remaining (rule 8)

* **Building upgrade-level sets** (@0x506430 non-foliage arm: cumulative
  SelectTextureSet(0..level)) and the scaffold pass (@0x5063f0) need
  per-node BUILDING STATE — they live with the scene/session owners. The
  set-0 baseline reconstructed here IS their level-0 state.
* **In-tree model source vs fast chunk**: RealMeshSource parses the .bgf
  SCRIPT (AGF), whose material list can differ from the engine's runtime
  FAST-chunk list (e.g. vg_TANNE_KRONE_06M_GRUEN: script 3 materials vs
  fast-chunk 2) — for such meshes the count gate rejects the sidecar and the
  material names bind (visible only when a season row would differ; the
  set-0 rows coincide with the names for all affected shipped meshes). A
  fast-chunk-faithful mesh source would close this (mesh-source owner).
* **HiColTab re-basing of the palettized indices** into the shared 16-bit
  shading banks (the tail of 0x5da34c): the in-tree bind paths build
  per-texture 565 LUTs (the wave-3 reconstruction of the same data), so the
  renumbering has no observable effect there; HiColTabBank::FindOrBuild is
  already reconstructed for the day a shared-bank raster needs it.
