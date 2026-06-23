# Wave-15 TRUE 1:1 Binary Diff — segment: world/save LOAD (MCP LIVE)

Segment files: `src/io/save_world_load.{h,cpp}`, `src/io/gamestate.{h,cpp}`,
`src/play/building_scene_attach.{h,cpp}`, `src/render/scene_floor.{h,cpp}`.

Method: decompiled every target on the live IDA MCP (module `gilde.exe`,
imagebase 0x400000) and compared LINE-FOR-LINE against the reconstruction;
get_bytes for every asserted constant; fixed divergences 1:1 and golden-pinned.

---

## VERIFIED-1:1 (decompile matches the reconstruction)

| addr | function | reconstruction | notes |
|---|---|---|---|
| 0x5a7ffc | VIBE_Save_LoadPersonIndexTable | `io::LoadPersonIndexTable` | count read, 67-stride, 8 sub-reads `2/4/4/4/4/1/1/0x1F`, `dword_6498C0=count`. Identical. The original also calls `VIBE_World_RelinkObjectOwners(a2)` first (clears the tile array); `LoadWorld` memsets `sceneTiles` to match. |
| 0x5a86d0 | VIBE_Save_LoadGlobalCounters | `io::LoadGlobalCounters` | All 28 field reads, offsets, sizes and version gates (`0x1002C/0x10014/0x10015/0x10018`) match the per-field-pointer destinations exactly (`+0/+2/+0x14/+0x28/+0x1C/+0x20/+0x2C/+0x30/+0x34/+0x38/+0x40/+0x44/+0x48/+0x4C/+0x50` … `+0x54/+0x58/+0x5C/+0x3C/+0x60/+0x64/+0x68` … `+0x70(0xE)` … `+0x80/+0x84(0x10)/+0x94(0xC)/+0xA0` … `+0x6C`). 16×164. |
| 0x5a8d3c | VIBE_Save_LoadCityRecords | `io::LoadCityRecords` | Preamble (`word_63CC5C`, count, idA, idB, 8 handlers if ver>=0x10017), 536-stride scatter by leading word, every version gate (`0x1003E/0x1003B/0x10031/0x10024/0x10020/0x1002A/0x10021/0x10036`), biases `+84 += 1342` / `+396 += 1468`, the `+364/+368/+380` zero-then-read, `+400=4` (ver<0x1003E), `+388 = v10`. Identical. |
| 0x5aa058 | VIBE_Save_LoadBuildingSlotTables | `io::LoadBuildingSlotTables` | 5×7952 slot tables (16-byte hdr + 62×128-byte sub-records, sub-read order `+0(2)/+4/+8/+12/+32/+36/+48/+52/+56/+60(2)/+44(4)` — `+44` LAST, matches), 4×756 city-info (full field map incl. 10×8 @+100/+180, 8×18 @+324, `+476(0xD0)`, `>=0x10037 +748(8)`). The trailing localized-name lookup (`_STADTAUSWAHL_%s_INFO+0`) is the documented out-of-scope tail. |
| 0x5a96c0 | VIBE_Save_LoadCharacterSlot | `io::LoadCharacterSlot` | First read size **0x20** confirmed via disasm (edx=0x20 @0x5a96f4). Stream order: `+5(0x20)/+44/+48/+140(2)/+368(0x30)/+304(0x40)/+56(0x10)/+72(0xC)`, hidden byte (`==1 -> +140 |= 0x20`), personId(4) `-> +300`, `[>=0x10013] +424(0x40)`, then `*(WORD+140) &= 0xDFFB`. Identical. |
| 0x5a8140 | VIBE_Object_RebuildModelByOwner | `play::ObjectRebuildModelByOwner` | `i != 43264; i += 169`, `*(byte)(i+dword_13CE298)` nonzero AND `*(a1+512) == *(DWORD)(v3+1)`, `BuildModelName(a1,v3,2)`. The +512 **re-read every iteration** is preserved 1:1. |
| 0x5e67c8 | VIBE_WorldIo_ReadObject | `play::ReadCityObjectRecord` | Spawn-kind ladder, spawn-type-from-name rule, the kind-0/1·4/2·3/5–8 ladders with every version gate, `+76 pos / +132 euler / +512 owner / +535 class / +533 type`, '!'-strip, child/sibling recursion, EventBindings tail. Structural translation tracks the (very large) decompile. |
| 0x5dcca0 | VIBE_Bio_ReadArrayQuick | `render::ReadArrayQuick` | `[elemSize][count]`, accept gate `count == expected` -> alloc+read `count*elemSize`; else seek-forward `count*elemSize` and return null. Identical. |
| 0x5e78a8 | VIBE_WorldIo_LoadFloorRegions | `render::ParseFloorRegions` | Full block grammar with EVERY version gate confirmed against the decompile and get_bytes'd constants (see below). Identical. |
| 0x5a348c / 0x5a7604 (hdr+scalar slice) | gamestate | `io::WriteGameState`/`LoadGameState` + `SaveLoadScalarBlock` | The scalar reads at 0x5a76d6..0x5a7806 (`dword_649890/632244/qword_13CE852(0xE)/byte_6477A1/dword_6498E4/64771C/647720/647724`, `[>=0x10022] 6477A8(4)`, `B56450(0x18)`, `[>=0x10030] 649894(4)`, `[<0x1003D] 632240=1000000 else read`) match `SaveLoadScalarBlock` byte-for-byte. Version range gate `0x10026..0x10045`. |

### Constants get_bytes-verified this wave
- `flt_62BEA0 @0x62BEA0 = 0x3EAAAAAB` (~1/3, legacy water vec4 scale) ✓
- `flt_628ADC @0x628ADC = 0x3F000000` (0.5) ✓
- `flt_628AE0 @0x628AE0 = 0xC2800000` (**-64.0**, confirms wave-13 supersession of the -50.0 misread) ✓
- `flt_628BA8 @0x628BA8 = 0x3B81848E` (terrain Y norm) ✓
- Floor version gates all confirmed: min `0x3A6C0009`, grids `0x3A6C00AD`, lightOffsets `0x3A6C00BB`, water `0x3A6C00AA`, vec4-raw `0x3A6C00AC`, water-v20 `0x3A6C00B6`, water-tail `0x3A6C00B9`, typeNames8 `0x3A6C00B0`, origin-vec3 `0x3A6C000E`, legacy-strs `0x3A6C000F`.
- Water `param20` default `1115422720 == 0x427C0000 == 63.0f` (reconstruction correct). ✓

---

## FIXED (reconstruction diverged from the binary — corrected 1:1)

### `RelinkPersonRecordColumns @0x5abb84` — the four-column transform
Decompiled the full `VIBE_Save_RelinkLoadedPointers` (0x5abb84) and the resolve
leaf `VIBE_GameObject_ResolveEntityById` (0x583b44) + `VIBE_He_FindFirstHandler-
ByFilter` (0x4c63f8). The column slice 0x5abbe2..0x5abc4f:

```
v3 = +364;  if (v3 == -1) +364 = 0;  else ResolveEntityById(&+364, 0, v3, 0);
            if (+368 == -1) +368 = 0;  else ResolveEntityById(&+368, 0, +368, 0);
            if (+380 == -1) +380 = 0;  else +380 = He_FindFirstHandlerByFilter(1,1,+380);
            +388 = 0;
```

`ResolveEntityById(a1, 0, id, 0)` clears `*a1=0` then scans the 169-stride
OBJECT/BUILDING array `dword_13CE298` (id @+1) — bit-identical to
`VIBE_Building_FindById @0x587b20`'s scan — writing the matched record on a hit,
leaving 0 on a miss.

**Divergence 1 (FIXED):** the reconstruction's `+364/+368` clear gate was
`if (id == -1 || id == 0 || !BuildingFindById(id))`. The binary has **no
`id == 0` short-circuit** — it resolves any `id != -1`. Removed the `id == 0`
special case so the gate is exactly `-1 -> 0, else resolve (miss -> 0)`.

**Divergence 2 (FIXED):** the reconstruction's `+380` He gate was
`if (he != 0) -> 0`. The binary clears ONLY on `he == -1`; otherwise it calls
`He_FindFirstHandlerByFilter(1,1,he)`, which on the partial .cty path (no He
records loaded — `byte_11D6040` is the full-save handler array, never populated)
returns null (0). Rewrote the gate to the binary form (`he == -1 || He-resolve`)
with the partial-path resolve result documented as null. The observable result on
the partial path is unchanged (every column ends 0), but the GATE is now binary-
correct (it would diverge under a populated He registry, which the old `!= 0`
form silently mishandled).

`+388` (always 0) and the player-resolve gate (`dword_6498E4` via
`Person_FindRecordById`, return false if unresolved) were already correct.

Golden-pinned: `tests/unit/io_save_world_test.cpp` →
**+TEST `relink_columns_resolve_clear_he_zero`** — constructs live object id
0x4242, a player record (gate), and two person records with link columns
`{-1, hit, miss, He, always-0}`; asserts `-1 -> 0`, `hit kept`, `miss -> 0`,
`He -> 0`, `+388 -> 0`, free slot (marker==-1) untouched, and the gate returns
false for an unresolvable player id.

This RESOLVES the wave-13 UNDER-VERIFIED item (isolated 1:1 column-transform
golden) AND the wave-13 NEEDS-LIVE-MCP item for the He gate.

---

## RESOLVED wave-13 NEEDS-LIVE-MCP queue

1. **Non-partial .SAV tail of 0x5a7604** — DECOMPILED. The exact tail
   order/version gates are now recorded verbatim in `LoadWorldEx`'s comment
   (`Amt<0x10045 / Gesetz / MapTiles>=0x10016 / GameGlobals / Avatar>=0x1002A /
   ObjectTable / AmtTable / HistoryAndCarts / ActionQueues / HotkeyTable>=0x1002E
   / RelinkLoadedPointers / Universe+Scene+Light / LoadCharacters / Mission /
   Hotkey>=0x10029`). Each is a separate loader OWNED BY ANOTHER MODULE
   (Amt/Gesetz/MapTiles/GameGlobals/Avatar/ObjectTable/History/ActionQueues/
   Hotkey/Characters/Mission) needing the live render/universe subsystem; the
   shipped cities are PARTIAL (flag&2) so this tail is never on the city flow.
   Out of this slice's four files — addresses listed for the owning modules
   (rule 8: genuinely unreachable from the partial city flow this slice serves).

2. **Full He-relink (0x5abe4f / 0x5abb84)** — DECOMPILED. The column-slice He
   gate is now fixed 1:1 (above). The remaining full-relink passes inside
   0x5abb84 (the scene-tile owner-chain rebuild @0x5abc5b, the 332-stride
   `byte_11D6040` handler dispatch @0x5abcae, the `dword_11CB620` He pass, and
   the `dword_B5FB66`/`byte_B5FB61` typed-table pass with tags 1=Person/2=Building
   /3=Object/9=Cutscene) operate on full-save structures never populated on the
   partial .cty path — owned by the He / event / cutscene modules. Documented;
   not in this slice's four files.

3. **Relink-column transform** — RESOLVED (the FIXED section + golden above).

4. Floor block UNPARSED remainder / `Floor_LoadFromHeightmap` texture-water side
   effects — confirmed owned by the floorwater (W5-WATER) segment; the
   pure-math placement mirrors here are VERIFIED-1:1.

---

## Still-deferred (address + reason — rule 8, unreachable from this slice)

- Full-save tail loaders @0x4832d0/0x4c28d8/0x5aa7a8/0x5aa8dc/0x4846ec/0x5aae40/
  0x5ab3ac/0x5ab59c/0x5ab704/0x5aba98/0x5a986c/0x53b25c — other modules; never
  on the partial city flow.
- The non-column passes of 0x5abb84 (scene-tile/He/B5FB66) — full-save state.
- `Scene_LoadObjectGroup @0x5e84f4` live VFS wildcard hook + `ParseNameAndBind
  @0x4ffb40` — framing reused; the live resolve is a host hook (Groups.BIN),
  e2e-proven, the hook bodies are named gaps (building-scene-attach.md).

---

## TEST RESULTS (this wave)

Portable (no assets):
- `io_save_world_test` — **99 checks, 0 failures** (was 88; +1 test
  `relink_columns_resolve_clear_he_zero`, +11 checks).
- `io_save_e2e_test` — 19 checks, 0 failures.
- `render_scene_floor_test` — 138 checks, 0 failures.
- `building_scene_attach_test` — 99 checks, 0 failures (real-AUGSBURG e2e
  passes; full city attach unchanged by the relink fix).

Real-asset e2e (`io_save_world_real_e2e_test`) guards on `GUILD_GAME_DIR`/repo
assets and skips cleanly when absent (this env has no asset dir).

Build kept green; only segment source + the one test edited. No bind-site /
other-segment files touched. No git commit.
