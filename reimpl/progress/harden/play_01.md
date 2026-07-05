# play_01 hardening — hotkeys / transitions / person models / menu-play glue

Chunk (22 files): src/play/{hud_recon_giftpanel, hud_recon_selaction, hud_render,
input_command, input_icon_text, input_recon4_hotkey, input_recon_select, input_router,
interact_building, interactive, map_view, menu_assets, menu_recon_network_screens,
menu_recon_transition, mode_fsm, mp3_decode, multi_city, native_main_menu,
newgame_apply, object_mesh_render, object_transform, person_render}.cpp (+ owned
headers). Evidence = IDA MCP decompile / disasm / get_bytes only; addresses cited.
Prior-wave reports consulted (flow_menu1/2, flow_town1/2, gui_03,
MENU-SUBSCREENS-GROUNDTRUTH) — overlapping files re-verified, not redone, EXCEPT
where the raw bytes contradicted a prior conclusion (see FIXED #4).

## FIXED (proven divergences, binary evidence cited)

### 1. input_recon4_hotkey.cpp — Hotkey_StoreDefaultEntry @0x4ff954
- Object id was read at byte offset **+1**; the binary reads **+2**.
- Evidence: disasm 0x4ff97a `mov edx, [ecx+2]`. The decompile's
  `*(WorkProductObject + 1)` types WorkProductObject as `__int16 *`, so the `+1`
  is pointer arithmetic = byte +2 (the brief's int*/short* trap) — the same
  object-id field AssignFromSelection @0x4fef54 / ActivateBuilding @0x4feec4
  read at `*(obj+2)`.
- Fix: `read4at(obj, 1)` -> `read4at(obj, 2)`; comment corrected.
- Test pins updated (old->new): tests/unit/input_recon4_hotkey_test.cpp
  StoreDefaultEntry_WorkProduct / _StorableFallback seed the object id at +2
  (was +1).

### 2. hud_recon_giftpanel.{h,cpp} — Gift_ComputeSliderRange / Gift_ConvertX @0x55d03c
- The lower endpoint is FLOAT-SPILLED in the binary; the reimpl did all math in
  double. Evidence:
  - cap spill:   0x55d0fe `fstp dword` (4-byte float) before the compare;
  - compare:     0x55d10a/0x55d10e `fild held / fcomp dword` (held vs float cap);
  - min spill:   0x55d14f `fstp dword` (held path jumps into the same spill via
    0x55d313), round-2 spills @0x55d36e/0x55d3bb — the value truncated by
    ConvertX/fistp is the FLOAT-rounded min. Behavioral for held > 2^24
    ((float)held rounds) and for products crossing an integer under float
    rounding. (flow_town2 had noted the spill but declined as non-behavioral;
    it is behavioral in-range.)
  - upper bound: 0x55d07d..0x55d08c `fild wealth / fmul flt_624A3C / ConvertX /
    fistp` — the product NEVER leaves the x87 stack (exact 80-bit; a double
    product of 31-bit int x 24-bit float can round across an integer). Modeled
    with long double.
- Gift_ConvertX rewritten to the fistp contract: truncate toward zero (ConvertX
  @0x5c6b08 arms RC=11), x87 integer indefinite 0x80000000 for NaN/out-of-range.
- Float constants re-verified via get_bytes: flt_624A3C = 0x3ba3d70a,
  flt_624A40 = 0x3d4ccccd. Floors 1600/3200 and the (dead-in-practice)
  `v38 <= 0 -> return 0` abort preserved 1:1.
- Existing goldens unchanged (values away from the knees) — all still pass.

### 3. input_recon_select.cpp — SelectEntity_ComputeResult @0x4147cc
- The products `v9*v29` / `v9*v28` (u32 @Ptr+116 x selection-volume floats) stay
  on the x87 stack (st6/st7) until ConvertX's frndint truncates in-register;
  the reimpl computed them in double (rounds at 56 mantissa bits).
- Fix: long double products + std::trunc (long double), then (int) — matching
  the exact 80-bit intermediate. Rest of the function verified line-for-line
  (see VERIFIED below).

### 4. newgame_apply.cpp — NewGameProfessionTalents @0x52d9f9..0x52da0e
- Talents are TypeRecordA bytes **[0..4]**, not [1..5]. This REVERTS the
  flow_menu2 wave's change (their disasm quote elided the displacement:
  `mov dl,[esp+eax+...]`).
- Evidence (definitive, raw opcode bytes @0x52da01): `8a 54 04 ff` =
  `mov dl, [esp + eax*1 - 1]` (ModRM 54 -> SIB 04 = base esp + index eax,
  disp8 = 0xff = -1). IDA text `[esp+eax+48h+var_49]` = esp+eax-1.
  The record base == esp at that instruction: 0x52d9f4 `mov esi, esp`,
  LookupTypeRecordA @0x589778 writes the 6-byte record at [esi] and is
  stack-balanced (push edx/edi, sub esp,8 ... add esp,8, pop, retn — disasm
  0x589778..0x5897c6). So eax=1..5 reads record[0..4]; stores land at
  0x122F4F0..0x122F4F4 (`88 90 ef f4 22 01` = mov [eax+0x122F4EF], dl).
- Fix: out[0..4] = {dword0&0xFF, >>8, >>16, >>24, word4&0xFF}.
- Golden pins updated (old->new, byte_649910 records via get_bytes:
  rec1 = 69 69 BD 93 69 04, rec2 = 3F 3F 93 69 3F 03):
  - tests/unit/newgame_diff_w15_test.cpp TalentTableGoldenBytes:
    variant1 {69,BD,93,69,04} -> {69,69,BD,93,69};
    variant2 {3F,93,69,3F,03} -> {3F,3F,93,69,3F}.
  - tests/unit/newgame_apply_test.cpp TalentsGoldenFromTypeRecordA: same;
    FullCommit golden r.talents[2] 0x93 -> 0xBD.
- Also corrected the mission-gate comment: 0x533a15 is
  `mov eax, [0x63C8F1]; sar eax, 0x18` -> the SIGNED BYTE @0x63C8F4 is tested
  (flow_menu2's comment fix to "byte 0x63C8F1" was wrong; behavior unchanged).

## VERIFIED-1:1 (no churn)

### person_render.cpp
- All 7 staff-model tables byte-diffed via get_bytes — MATCH byte-for-byte:
  kChildBoy @0x63DA78 (40B), kChildGirl @0x63DAA0 (40B),
  kMaleProfession @0x63DAC8 (27x40), kFemaleProfession @0x63DF00 (27x40, 7
  explicit + 20 zero), kMaleOffice @0x63E338 (76x40), kFemaleOffice @0x63EF18
  (76x40, 70 explicit + 6 zero), kReaper @0x6405E8 (3x40).
- ResolveStaffModel @0x57c1e8 — decompile diffed line-for-line: adult gate
  (office byte OR (double)u16@+0x0A >= flt@+0x20), child pick by gender byte;
  stored-name path (+0x1F0 set AND +0x18C != -1): (cursor+1)%8 rotation,
  2-byte name copy, texVariants[0..3] = LOBYTE(texSet)+68, scratch return;
  kind-17 random trio (40 * (u16)RandomModulo(3)); office scan <=76 stop at
  code 0, signed-byte compare @0x57c388, stop-record return @0x57c3e1;
  profession scan <=27 with v3>=27 falling to LABEL_39 gender defaults
  @0x57c459..0x57c47e. PersonModelView offsets (0x02/0x09/0x0A/0x20/0x164/
  0x165/0x18C/0x1F0, stride 536) all match the dword_12CE910-relative reads.
  (The nullptr guard for gender outside {0,1} replaces the original's null
  deref — documented, unreachable on live data.)
- CharacterAnimMemberName format "character/%s/%s_%s.baf" per 0x403c34.

### input_recon4_hotkey.cpp (rest)
- Hotkey_ValidateAssignments @0x4fee18 — 11 slots, three -1-clear branches all
  target slot[i]+4/+8; flags gate `(ComputeSelectionFlags(word_63CC5C,bld,0,obj)
  & 1) == 0`; obj-null-with-id clear. Match.
- Hotkey_ActivateBuilding @0x4feec4 — char match + suppress re-test, Validate
  inside loop, break on buildingId != -1, v0 += 3 / >= 33 (11 slots); FindById
  result unused; dialog arg = *(obj+2) or -1 (+ uninit low dword modeled 0). Match.
- Hotkey_AssignFromSelection @0x4fef54 — sel = dword_631744 else dword_631748;
  buildingId = *(sel+1); objectId = dword_63174C ? *(obj+2) : -1; fallback
  *(dword_6477A4+1)/-1. The status-banner sprintf is the documented HUD hook
  boundary. Match.
- Input_CharToScancode @0x40c790 — 12 switch cases (R48 O49 P50 Q51 K52 L53 M54
  G55 H56 I57 J45 N43), BYTE1 = 1 (shift assigns) |2 ctrl |4 alt, 256-entry
  u16 table scan, 0 on miss. Match.
- DragSlot_ResetGridTable @0x54f884 — 32 rows x 16 dwords; row {0,-1,-1,-1};
  do-while result=64i+8..+48 writing dword -1 @base+8+result and word 0
  @base+12+result; returns 2032. Match (leading zero-init models the word-0
  stores; the original leaves inter-field pad bytes untouched — documented).

### menu_recon_transition.cpp
- Hotspot_Register @0x421ab8 — count incremented even past 64 (>64 returns 0),
  rec = base+16*v6, active=1, w0=ax w1=dx w2=bx w3=cx payload dword. Match.
- Hotspot_Remove @0x421b18 — count>=1 && index>=0 -> 16-byte zero-fill
  (SetGrayColorThunk(0,16)) + --count. Match (bounds guard added, documented).
- Fade_UpdateAll @0x41f47c / Fade_UnregisterAll @0x41f4b8 — byte-stepped (i+=4)
  skip-empty inner while, per-slot Update/Unregister(+slot=0), i==128 exit. Match.
- Transition_FadeOutToBlack @0x56d2cc — v7 = (state|0x100000) with BYTE1 &=
  0xDF; register(0,0,scrW,scrH,"BLACK",30,1); do-RunFrameLoop-while !done;
  scene rect {scrH,0,0,scrW}; state=147591; Toggle(1); RenderScene;
  RenderList(1770); Unregister; Groundplan(0); return register(...,50,10). Match.
- Transition_FadeInScene @0x56d3b0 — register 30,1; while !done RunFrameLoop
  (147591,147591); snapshot branch (renderScene(h,snap), unregister, dirty
  flag) vs rect-restore branch (63CC54/4C/50/58 in that store order,
  renderScene(h,0), Groundplan(1), GroundplanFadeIn, Toggle(0), unregister,
  register 50,10). Match.

### input_recon_select.cpp (rest)
- Input_PollMouseAndKeyboard @0x40da88 — poll mouse, 0x4C packet mirror,
  poll keyboard(0) return. Match (hook boundary).
- SelectEntity_ComputeResult @0x4147cc — actor loop (stride 171 dwords, gates
  +400/+408/+412, 48 actors), the LABEL_6/LABEL_27/LABEL_29 goto graph incl.
  the subtle re-entry into the while-CONDITION (not LABEL_6) after ++v6==1,
  v7 = v5+428 / v5+492 slots, (float)((double)y + 5.0) bias (flt_610EBC),
  v32 += 256 iff ecx-flag==1, byte-pair name copy, hotspot walk (count
  *(rec+26)>>16, ids at rec[6], rect = 740*id + dword_69FFB4, words @+16/+18/
  +20/+22 via the int>>16 reads, type-64 secondary continue, hit returns
  *(rect+8)), final dword_62D22C=-1/-1. Match.
- Selection_Reset @0x4b9444 — anchors zeroed; A/B owner ladder incl. the
  result==0 early return when A set but B null; v2 = *(result+93);
  byte_6317B4=0; QueryFind/IterNext loop; type byte (65-stride) ==29 ->
  rec[32] &= ~2. Match.

### input_icon_text.cpp — Input_SetIconTextById @0x40fb4c
Gate (wrec+44 pointer AND obj+0x150), remove-previous MemMove, insertedLen
=strlen(new), gap-open MemMove (tail re-measured after the first move — the
lambda re-strlens, matching the in-place edit), qmemcpy, Property_Get width to
obj+0x10 + word store to wrec+20. Match (returns the stored word; the
original's return value is an address — documented model boundary).

### hud_recon_selaction.cpp — Hud_BuildSelectedObjectAction @0x54e7b4
Gate dword_631724; free-act predicate dword_6317B0 && slot &&
(slot[13]>1 || dword_63C7B8) && dword_67221C; entity walk 411648/536=768 on
byte_12CEA98[i]; else rank<=1 generic tooltip vs sprintf'd resource name
indexed (slot+8)>>16 (arithmetic). Match (command/voice/tooltip = hooks).

### input_command.cpp — ClassifyOrder vs IssueOnObject @0x488ff0
Ladder re-verified at the disasm level: armed latch dword_67221C; picked
+535==2 -> attack iff (+364 owner differs OR byte_671D96) and
BuildAttackPacket != -1; label dispatch "sp_ESCAPE" (full StrCmp, 0x48906c) ->
MoveTo, "sp_CONQUER" (StrncmpN 10, 0x48907f) -> LabeledMove, "WARE"
(StrncmpN 4, 0x489097) -> Conquer, else nothing; no-pick -> heightmap raycast
-> op80. Bridge/queue plumbing is reuse of sim reconstructions.

### interact_building.cpp
- QueueRequestArgs26 @0x494848 — packet byte[0]=26, three dwords at
  +0x10/+0x14/+0x18 (id/field/value). Header constants match exactly.
- BuildingDialogKindToActionGroup vs EnterAndDispatch @0x51defc — all 10 mapped
  codes confirmed in the binary ladder: 19 GuildMaster, 20 Sabotage (0x14),
  21 Threat (0x15), 22 WineCellar (0x16, the >0x15/<0x17 window), 24 Mistress,
  116 Smith (0x74), 133 Carpenter, 155 Stonemason (0x9B), 247 Production
  (0xF7), 276 Treasury (0x114). NOTE (scope): the binary ladder has ~20 more
  location kinds (tavern 0x120, bank 165, town hall 0x10E/272, church
  0xE5/0xE6/0xF0/242, dungeon 0xB7/0xBA, guard 0x145/0x146/332, guild
  311/313/0x138, brewery 0x12E, perfumery 280, mixing 0xCF, thief 84/0x65,
  flytrap 0xDD, equip 0x5C, night 96, idle 0x4D/187, empty 214) which this
  vertical slice deliberately maps to kNone (plain frame loop) — documented
  partial coverage, not a mis-mapping.

### newgame_apply.cpp (rest) — ApplyNewGameParams vs 0x5336f0
Re-diffed against the full decompile: arm gate BYTE2(dword_122F4A0); player
TradeRequest arg list; mother (flag 1, signed byte 0x122F4A1) then father
(flag 0, 0x122F4A0), each RandomModulo(4)+32; family stamps in exact order
(father+0x40 StrNCopyPad 16; mother 0x50/0x54/0x68; mother name female table
RandomModulo(0x70) -> +0x30; ResolveStaffModel; father same with male table
RandomModulo(0xBF); spouse links +0x5C (mother first); player +0x60/+0x64;
both parents +0x68 re-stamp; player+0x1F0 avatar copy; +0x18C portrait);
six QueueRequestCoord27(...,127) in binary order; purses 32*RandomModulo(0x200)
+16000 father-then-mother; mission gate signed byte @0x63C8F4; talents to
+0x80..+0x84. Match.
- OUT-OF-CHUNK observation (not touched): sim/building2.cpp
  Building_LookupTypeRecordA compares `code < 76` UNSIGNED, but the binary
  0x589778 does `cmp al,4Ch; jl` + `movsx` — SIGNED, so codes >= 128 index
  NEGATIVELY (e.g. 200 reads 6 bytes @0x6497E0 = 01 65 01 66 01 00, not
  record 0). Live callers pass small codes; flagged for the sim chunk owner.

### menu_assets.cpp
- MenuFont::MeasureWidth vs Property_Get @0x4152cc — '~' skip with i++ still
  counting, kern subtract when i&&prev!=32, space adds tracking+extra+advance,
  tail +tracking. dword_62D270=8 / dword_62D274=2 confirmed via get_bytes
  (static, only read by the layout functions). Match.
- MenuFont::DrawText vs Property_Set @0x4159dc — kern subtracted on EVERY char
  (before the '~' test), space adds ONLY dword_62D270 (no tracking/advance),
  glyph adds tracking+advance. Match; the original's `pen >= screenW` break is
  replaced by per-pixel clipping (documented; menu labels fit).
- ResolveMainMenuLabels — button rows 10/53/96/139/182/225/268/311 confirmed in
  the 0x529d08 decompile (Widget_AddSpriteToWindow(32,row,174,label)); the
  label-global -> _OPTIONEN_MENUE_* mapping is the MENU-SUBSCREENS-GROUNDTRUTH
  result (asset-level, re-used not re-derived).

### native_main_menu.cpp / menu_recon_network_screens.cpp
Presentation glue (flow_menu1-hardened). Spot re-verified the binary-grounded
claims: `(int)RandNext() % 3` music pick (0x529d08 decompile), sprite-kind-9
button width = Property_Get(label) + cap0 + cap1 + 4 (RecomputeSize @0x41b164),
PositionAtCoord mode-3 centering v3 = x - A/s + A (0x41d7e0: the w/2 terms
cancel; A = dword_69FFA4, s = flt_62D224). Network screens' pure cores were
verified by flow_menu1; nothing new to fix.

### object_transform.cpp / object_mesh_render.cpp
- Node offsets proven: SetPosition @0x5af38c writes node dwords 19/20/21
  (+76/+80/+84); SetWorldTranslation @0x5af50c reads eulers +132/136/140 and
  MatrixFromEuler writes the 16-float frame at node+396; type byte +533
  (0x5add1c reads *(a1+533)); ProcessSceneNode hands the +72 world matrix to
  InterpolateMorphVertices (translation = m[12..14]). Match.
- object_mesh_render is orchestration over REAL render reconstructions
  (ProjectVerticesToScreen 0x5c5120, RadixSortDrawList 0x5AEF34,
  RasterizeMeshList 0x5AEC88 — owned/hardened by render waves); its local
  ComposeWorldMatrix/quad-fallback are documented bridge scaffolding.

### hud_render.cpp / map_view.cpp / input_router.cpp
- hud_render: host compositor over verified leaves (gui_03 verified the
  0x4b1542 pct conversion; flow_town2 verified the binder). No provenance'd
  logic of its own diverges. VERIFIED (as glue).
- map_view: flow_town1-verified host layer; the REAL marker sort
  (MapViewSortMarkersByScreenY @0x54457e) and MapView_ComputeMarkerScreenPos
  @0x5440b4 are reused reconstructions.
- input_router: widget hit-test offsets +16/+18/+20/+22 (x/y/w/h words) and
  window +4/+6/+8 confirmed against the 0x4147cc rect reads
  (*(int*)(rec+14)>>16 etc.) and 0x41d7e0's window field reads. Match.

### mode_fsm.cpp / interactive.cpp / multi_city.cpp / mp3_decode.cpp
No `gilde.exe 0x…` function bodies of their own — glue over app::MenuRunMainMenu
/ MenuMainDecide (session-flag bits 1/2/4 consistent with word_63C740 writes
9/5/|1 in 0x534bbc) and the shim boundary. mp3_decode is the pre-approved
audio-tech swap surface (libmpg123 behind GUILD_HAVE_MP3; decode only, no
engine logic). Nothing to diff.

## Tests (all green)

Rebuilt + ran (GUILD_GAME_DIR set): input_recon4_hotkey_test 567,
hud_recon_credit_panel_test 86, newgame_apply_test 174, newgame_diff_w15_test 16,
input_recon_select_test 42, session_input_test 119, person_model_resolve_test 58,
menu_recon_transition_test 75, menu_assets_test 14, menu_font_test 29,
mode_fsm_test 66, play_input_command_test 18, play_interact_building_test 58,
object_transform_test 28, object_transform_ops_test 31, object_mesh_render_test 46,
input_router_test 48, hud_render_test 23, mapview_test 58, multi_city_test 23,
interactive_test 12, newgame_apply_e2e_test 23, menu_network_screens_view_test 92,
session_select_test 106, playable_flow_e2e_test 154, native_main_menu_itest 10,
mock_input_platform_test 139, oneone_screens_wave14_test 73, session_card_test 17,
hud_render_itest 14, choosehistory_screen_e2e_test 59, interactive_itest 19,
interactive_e2e_test 13, mode_fsm_e2e_test 17, newgame_result_itest 24,
input_router_e2e_test 21 — **36 targets, 0 failures**.
