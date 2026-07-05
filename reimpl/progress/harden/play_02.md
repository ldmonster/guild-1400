# HARDEN play_02 — 1:1 verification vs gilde.exe (IDA MCP)

Chunk: 22 files under `src/play/` (picksel_recon, playable_slice, playthrough,
real_audio, real_city_render, real_mesh_source, real_session, real_texture_source,
render_binder, run_interactive_app, save_roundtrip, scene_interior,
scene_main_loop, scene_persp_render, scene_pick, scene_recon2_orchestrator,
scene_view, sdl_charcreate/charintro/choosehistory/chooseplayer screens,
sdl_city_screen3d). Evidence = IDA decompile/disasm/get_bytes only; addresses cited.

## FIXED (proven divergences, binary evidence)

### src/play/scene_main_loop.cpp — 0x50f0c0 VIBE_Scene_RunMainFrameLoop
- **FIXED** auto-enter request-1 (0x50f2d8..0x50f336/0x50f55c..0x50f5b5): when
  `IsProductionType(b)` is true but `InvokeHandlerSlot60(27,b,0) != 1`, the binary
  **falls through** to the storage/op-25 block (`jnz loc_50F57C` @0x50f32e →
  IsStorageType @0x50f57e → Invoke(25,…) @0x50f59a). The reimpl's nested-if skipped
  the whole else-arm in that case. Now `if (isProd && invoke27==1) … else if (!isStorage) …`.
  Test strengthened: `scene_main_loop_test` AutoEnterRequest1Production now pins the
  fall-through (`slot60(25,900,0)` after a failed op-27).
- VERIFIED-1:1 everything else, including: kSmokeHourImage byte-exact vs
  0x6476F0..0x64771F (get_bytes: 0,0,0,8,7,8,9,20,21,20,19,0); family scan stride
  0x218 / 768 records / type order {6,7,5} (0x50f139..0x50f3e5); loop-head
  RunFrameLoop eax=&self @0x50f1b2 / final eax=0 @0x50f684, edx=0x67FFF both;
  cwde+dword store of selection flags (0x50f2d3..d4); smoke phase machine; request-2
  block incl. the double IsStorageType (0x50f5c8/0x50f5e5) and the not-cleared
  dispatch path; teardown ConvertY operands (sar 16 of 0x75BF48/0x75BF46 ==
  sext i16 words @0x75BF4A/0x75BF48); ecx=-1/edx=0 register artifacts vs prologue
  disasm @0x50f0ce/0x50f0d8. 0x58339c GetSeasonFromDay `*a1 % 4` VERIFIED.

### src/play/picksel_recon.cpp — pick/selection cluster
- **FIXED** 0x5b7134 ComputeSelectionVolume, triangle-1 normal x-component:
  `nx = e2z*e1y - e2y*e1z` (v71 = v64*v75 − v63*v76). Reimpl had `e2z*e1x`.
  Proven by raw fmul/fsubrp chain @0x5b74b7..0x5b74e6 (D−A = [eax+8]*[ebx+4] −
  [eax+4]*[ebx+8]).
- **FIXED** triangle-2 normal x-component: `nx = e1z*e2y - e1y*e2z` (same v71
  formula with v62..64 = e1, v74..76 = e2 @0x5b78ce). Reimpl had `e1z*e2x`.
- **FIXED** triangle-2 barycentric average term: `qx/e2x` (v46/v74 @0x5b7ac6).
  Reimpl had `qx/e1x`.
- **FIXED** x87 truncation points in both triangles: the plane/ray denominator is
  stored to a FLOAT before the divide (`fst var_150` @0x5b7583, `fdiv` @0x5b75c3;
  v54 @0x5b7925) — now `/(float)denom`; the tri-1 acceptance compare adds the
  FULL-precision t (v31 on the x87 stack) to the float v58 (@0x5b7743) — lambda now
  returns double t, compare is `t_full + (double)s_first <= 1.0`; tri-2 range checks
  and (1.0 − x) interpolation use the FLOAT stores v85/v86 (@0x5b7ae1..0x5b7b6a) —
  now truncated before use.
- **FIXED** 0x4bdc3c DragUnitCentroidHit: the screen x/y are ONE float store each
  with `+ centre` inside the truncation (`v25 = scaleX*v28*v20 + flt_13FCD18`
  @0x4bde16, @0x4bde30); reimpl truncated before adding the centre. Also
  `1.0/(0.125(double) * sumZ(float))` modeled without a float pre-round.
- VERIFIED-1:1 (no change): rodata consts dbl_628708=1e-07, flt_628710 bits
  0x3eaaaaab, flt_62852C=0.5, flt_6282F4/dbl_61E208/dbl_61E210=0.125,
  loc_5B3E28/+4 both decode 1e10f, bucket seed 1343554297==1e10f (all get_bytes);
  min/max corner reduction + rebase (u−minU, 1−(v−minV)) and corner selection
  (span/maxSum/minSum) @0x5b71e4..0x5b7379; bucket seed loop 0x5b5aec (9 slots at
  0x13FD464, obj=0/dist=1e10f) and nearest scan 0x5b5b6c (strict <, double compare);
  0x5b5938 dist test (`v7 < slot.dist && sqrt(v10) < v14`, float store of dist);
  DragClamp idiom + BeginBox stores 0x4bdb98 (11BC250/254/258/25C, 11BC24C=1);
  ApplyToUnits normalize (min/max of the 4 corner dwords) + containment order;
  DragCursor 0x41f860/0x41f878/0x4c094c mode→(dx,dy) map; interaction predicates
  0x595e54/0x595f70 and 0x595e74 InvokeHandlerSlot60 (gate order enabled→handler→
  blocked→slot60, and the a4/a3 argument swap at the call).

### src/play/scene_recon2_orchestrator.{h,cpp} — scene orchestration cluster
- **FIXED** Scene_SyncDecorObjects (0x501c34): per-slot emit shape —
  primary Request17 mode arg is `2 - (v5 != 0)` on the non-type-30 path (@0x501e85)
  vs constant 2 on the type-30 path (@0x501d4e); the random `o254` emit
  (RandomModulo(0x14)+20) happens ONLY on the non-30 path when v5==0 (@0x501e8c)
  and carries the decor id as arg4 (not 0); the switch-secondary emit passes
  `(mode=2-(v5!=0), secondaryId)` (@0x501d7b) — the reimpl had the secondary id in
  the mode slot. Return value: `return (__int16)Begin` @0x501ddb — the iterator is
  exhausted (0) on every exit, so the function always returns 0; reimpl returned
  the last person handle. Test pin updated old→new: `CHECK_EQ(last, 1)` →
  `CHECK_EQ(last, 0)` (scene_recon2_orchestrator_test, address cited in test).
- **FIXED** Scene_RunMainFrameLoop (coarse model of 0x50f0c0): same op-27
  fall-through fix as scene_main_loop (jnz loc_50F57C @0x50f32e).
- **FIXED** Scene_HandleDebugKeyToggle (0x5e872c), key 0x14 wireframe toggle:
  `LOBYTE(dword_649D7D) = ((_BYTE)dword_649D7D == 0)` @0x5e88dd — only the low
  byte is rewritten, upper 3 bytes preserved in both directions; reimpl clobbered
  the whole dword to 1. Rest of the ladder VERIFIED-1:1 (0x15 ZEnable, 0x1E/0x20/
  0x30 render-state bit toggles, 0x1F shadow tri-state + traverse(511), 0x2E
  track-target walk, 50 cycle 3→1→2→3, 0x23 return 1, 38 light refresh, LABEL_8/9
  apply gating).
- **FIXED** SpawnBuildingMesh_MergeBBox (0x5036bc): the merge loop covers verts
  **1..7** only (v16 = v10+80 .. v10+640 step 80 bytes @0x503821/0x503838/0x503977;
  vert 0 is the seed @0x50383e..0x503860) — the reimpl merged 20 verts. Header
  comment + unit test updated (old pin used vert 9; per the binary a vert beyond
  index 7 is never read).
- VERIFIED-1:1 (no change): 0x506d34 ResetVoicesAndScript (voices loop, 64A05C=1024,
  64A054=64, script-finish order, CancelForObject + 62D080=-1 after);
  0x506df4 ActivateAndRefreshCharacters (early-out on dword_649D60, anchor zbits
  1051260355, family-y sentinel dbl_62109C=-1.0, season write `<=2||==3`,
  traverse 224/192, brightness `1.0 - byte*dbl_6210A4` with dbl_6210A4=0.01
  byte-verified, Present(byte_64A01C?0:1)); kCityBuildGate byte-exact vs 0x4FFAFC
  (29 bytes); kDecorSeasonList byte-exact vs 0x4FFAF8 {32,31,30,0}; decor id
  quartets 30→{439,440,441} 31→{442,443,444} 32→{445,447,446,448} and the
  439→458…448→467 secondary switch (442 absent = default) vs the 0x501c34
  decompile; 0x5e860c SaveObjectGroup (parent validation, magic 980156603,
  shared-parent single-object write vs count+all write); 0x50456c SyncWorldOnEnter
  (rand-seed gate, home type-6 scan shape, charSlot exclusion + count args,
  stock-up QueryByGoodType(1)→QueryFind(...,277)→Request16(-1,480000), return 1);
  0x500270/0x5006a8 Load*Scene claims (mask &8 + IsProcActive, "scenes/*ob_%s.ed3"/
  "*gb_%s.ed3", traverse 480+6 vs 352+6, UpdateBuildingVisualState(.,1), tail
  SetProcInterval(0) gating); smoke tables {8,7,8,9}/{20,21,20,19}. 0x502568
  SyncCityBuildings remains the documented PARTIAL skeleton (unchanged).
- NOTED (model surrogates, documented in code, unchanged): Gebaeude fade gate uses
  `dword_11BC27C >= 0` in place of the unmodeled `dword_631638 <= 0 ||
  dword_11BC27C` (0x5009ce); SyncWorkshopProduction/SyncObjectHeights/
  SyncMovableObjects keep their documented delegation shapes.

### src/play/real_audio.cpp — .sbf sample-bank reader
- **FIXED** entry-record framing (off-by-4 → wrong sample pairing). Binary truth
  (VIBE_Sound_LoadSampleBank @0x446b2c + VIBE_Sound_LoadEntry @0x446830): header
  0x144 bytes (count u32 @0x134), entry table at file 0x144, 0x40 per entry;
  per entry **+0 u32 data-block offset** (the `VIBE_File_Seek(*(_DWORD*)entry, 0)`
  target @0x44688c), **+4 name(50)** (error printf uses entry+4 @0x4469a6),
  **+54 format byte** (@0x44689d; 1 single / 2 variation), +60 runtime scratch
  (written `13*dword_62EB38` @0x44697a — not persisted). The reimpl framed entries
  at 0x148 with the data offset at +0x3C — i.e. each entry read the NEXT entry's
  offset dword (entry0 got entry1's sample; the last entry got garbage).
  Validated on the real asset: europe_guild_1400_original/sfx/allgemein.sbf —
  corrected framing resolves BOTH entries to RIFF blocks (0x1C4, 0x13BA40).
  Synthetic-bank builders in real_audio_test / real_audio_itest updated to the
  binary layout. All 7 audio targets green.
- OUT-OF-CHUNK note: src/play/session_audio.cpp (not in this chunk) carries a now
  stale comment ("athmos.sbf entry record carries no usable data offset" — that was
  this bug); its RIFF-scan fallback still behaves identically. Not edited.

## VERIFIED-1:1 / VERIFIED-model (no change)

- **src/play/scene_interior.cpp** — 0x5878b0 MapTypeToCategory + 0x587f80
  IsProductionType forwarded to guild::sim (switch + {11,13,12,16,28} match the
  decompile exactly); 0x51db4c SelectRoom (disasm-verified: edx=1, jle write[0],
  IterNext loop, exit at edx>=count writes the (count−1)-th, null → no write);
  0x4adef4 gate head (busy 11BC27C, active==-1 / byte_12CEAC1 row, kind byte 6 / 4
  + owner dword vs dword_12CE914[134*active]); 0x485e88 AttachObjectMesh
  ("ob_%s" upper, detach-if-present then attach, no-def detach-only, StrCmpNoCase
  gate); 0x5066b8 enter core (season `(u8)<=2 || ==3`, IsProductionType decor hide,
  type 30 + roomFilter 253 plant load, `*v4==1 ? 4 : 8` char cap).
- **src/play/save_roundtrip.cpp** — writers are exact inverses: 0x5a7ffc tile table
  (count; +0/2,+2/4,+6/4,+10/4,+14/4,+18/1,+19/1,+28/31; stride 67);
  0x5a86d0 counters (all 27 field offsets and the 0x1002C / 0x10014 / 0x10015 /
  0x10018 gates match, including the out-of-order +0x3C and trailing +0x6C;
  16 records stride 164); 0x5a8d3c preamble (word_63CC5C, count, idA, idB,
  8 handler ids @≥0x10017; per-record leading marker word).
- **src/play/real_texture_source.cpp** — adoption gate (setCount>0 &&
  namesPerSet==materialCount) verified at 0x5f89f3..0x5f8a0e; SelectTextureSet
  gates verified (season>=setCount return @0x5b403b; empty row skip @0x5b4099);
  foliage prefixes pfl_/vg_/!vg_ from byte 0 verified at 0x5063c8..0x5063e4 with
  StrncmpN 0x5e9ee0 confirmed case-sensitive; season source byte_634484 = day%4.
- **src/play/scene_pick.cpp** — reuses render::ProjectVerticesToScreen (0x5c5120);
  flt_628B94=0.875 / flt_628B98=254.0 byte-verified.
- **src/play/scene_view.cpp** — host (Vulkan/SDL) view driver; its engine citations
  check out: pick seed 1.0e10 + `d2 < best && sqrt(d2) < projR` (0x5b5a38/0x5b5938),
  0.125 corner average (flt_6282F4), camera-from-dummy field mapping (+92/+144).
- **src/play/real_city_render.cpp** — person gate marker!=-1 && kind<10 verified vs
  0x4f8e60; model resolution delegates to sim::ResolveStaffModel (0x57c1e8, owned
  elsewhere). Grid seating is the documented rule-8 substitution note.
- **src/play/real_mesh_source.cpp** — glue; carries a documented deliberate
  deviation (AGF token script preferred over the 0x5D2348 FAST-CHUNK order so the
  person morph pipeline stays poseable) with in-code rationale and a named
  follow-up task. Left as documented.
- **src/play/sdl_charcreate_screen.cpp** — profession table {1,2,3,4,5,6,7,11}
  byte-exact vs 0x527604 (get_bytes); gfx id beruf+1349 (0x52c693) and wappen base
  1342 (0x52cd75, range check 1342..1350 @0x52d603) verified.
- **src/play/sdl_city_screen3d.cpp** — ChooseCity model verified vs 0x52e6d8:
  "stadt_%s" markers (0x52e86f), SpawnCityPointMarker (0x52e7d9), single
  sp_STADTTURM attach (0x52ecb7) + wimpel anim (0x52ecfc), and the four offset
  vectors @0x52762C/0x527648/0x52763C/0x527658 all zero (get_bytes).
- **sdl_charintro / sdl_choosehistory / sdl_chooseplayer screens** — native SDL
  screens (rule-4 swap); their binary-cited anchors are the menu functions
  0x52e4e0 / 0x52d684 / 0x52ccd8; no transcribed tables beyond the above.
- **playable_slice / playthrough / real_session / render_binder /
  run_interactive_app / scene_persp_render** — integration glue over reconstructed
  siblings; no `gilde.exe 0x…` provenance of their own to diff. VERIFIED-glue.

## Test targets run (all green)

| target | checks |
|---|---|
| scene_main_loop_test | 145/145 (was 141; +4 new fall-through pins) |
| picksel_recon_test | 86/86 |
| scene_recon2_orchestrator_test | 159/159 (bbox + decor-return pins updated) |
| session_input_test | 119/119 |
| dragselect_wave20_test | 73/73 |
| real_audio_test / itest / e2e | 32 / 30 / 14 |
| real_audio_driver_test / itest / e2e | 33 / 18 / 12 |
| session_audio_test | 99/99 |
| scene_interior_test | 338/338 |
| scene_pick_test / itest / e2e | 29 / 19 / 12 |
| scene_view_e2e_test | 45/45 |
| real_city_render_test / itest / e2e | 20 / 13 / 39 |
| real_texture_source_itest / e2e | 23 / 17 |
| real_mesh_source_itest | 31/31 |
| save_roundtrip_test / itest / e2e | 24 / 17 / 32 |
| playable_slice_test / itest / e2e | 26 / 9 / 22 |
| playthrough_test / itest / e2e | 16 / 15 / 12 |
| real_session_test / itest / e2e | 22 / 14 / 31 |
| render_binder_test / e2e | 24 / 32 |
| render_binder_real_bridges_test / itest / e2e | 47 / 31 / 29 |
| scene_persp_render_e2e_test | 12/12 |
| sdl_charcreate_screen_test / itest / e2e | 42 / 18 / 12 |
| charintro_markup_test / charintro_screen_e2e_test | 21 / 13 |
| choosehistory_run_test / choosehistory_screen_e2e_test | 44 / 59 |
| chooseplayer_run_test / _screen_e2e / _textedit | 20 / 9 / 19 |
| sdl_city_screen3d_itest | 16/16 |
| sdl_city_screen_test / itest / e2e | 38 / 15 / 13 |
| playable_flow_e2e_test | 154/154 |

(e2e/itest runs with `GUILD_GAME_DIR=$PWD/europe_guild_1400_original`.)
