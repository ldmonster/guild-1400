# HARDEN play_04 — 1:1 verification vs gilde.exe (IDA MCP)

Chunk: 22 files under `src/play/` (slices, turn_*, text_recon*, ui_recon*,
wire_*, terrain/universe render drivers, tutorial stepvoice, video backing).
Evidence: IDA decompile/disasm/get_bytes only; addresses cited per claim.

## Verdicts per file / function

### src/play/slice_council.cpp — VERIFIED-1:1
- `0x495454 VIBE_Command_RequestBuildOp68`: packet buffer `char v3[16]` @ebp-0xAC,
  applicant dword at +0x10 (v4 @ebp-0x9C), 3 holder bytes at +0x14..+0x16
  (v5 @ebp-0x98), opcode 68. Header constants (slice_council.h:74-77) match.
- Caller `0x47e1b8 VIBE_Office_ApplyForCandidacy` cross-checked: v17=applicant
  (`*(a1+4)`), v18=holderA, v19=holderB (default -1), v20=officeType(a2). The
  single-slot path stamps holderB=0xFF — matches `BuildCouncilPacket`.

### src/play/slice_estate.cpp — VERIFIED-1:1
- `0x495098 VIBE_Command_QueueRequestQuad56`: v6=a1@+0x10, v7=a2@+0x14,
  v8=a4@+0x18, opcode 56 — matches header offsets.
- `0x58820c VIBE_Building_SetObjectParent`: `*(WORD*)(rec+37)=parent`,
  `*(WORD*)(rec+39)=owner` confirmed (kEstateParentFieldOff/kEstateOwnerFieldOff).

### src/play/slice_market.cpp — VERIFIED-1:1
- `0x49465c VIBE_Command_QueueRequest17`: v8@+0x10, v9@+0x14, v10(word)@+0x18,
  v11(byte)@+0x1E, v12(dword)@+0x1F, v13(dword)@+0x23, opcode 17 — all six
  header offsets match. Price trunc-to-int matches ConvertX semantics.

### src/play/slice_personnel.cpp — VERIFIED-1:1
- `0x55d5c0 VIBE_Recruit_CheckRecruitProximity`: employer field = dword index 23
  (byte +0x5C == kPnEmployerOff); reject codes -1024..-1028; issued gate
  `proximity >= 0` matches.
- `0x55d990 VIBE_Recruit_RunHireConfirmDialog`: staging kind byte `v19 = 2`
  confirmed (kPnHire == 2), enqueue via QueueRequestSlotReset28 @0x4948c8.

### src/play/slice_production.cpp — VERIFIED-1:1
- `0x5087bc VIBE_TradePanel_BuildProductionWindow`: staging `v69[0]=1`,
  `v70 = HIWORD(dword_122E94E[...])` (prot), `v73 = *(v14+2)` (building id),
  then `VIBE_Command_QueueRequestSlotReset28(&v60, 0)` + frame-loop id
  131079/248 — matches the scratch.words[0..2] staging.

### src/play/slice_tavern.cpp — VERIFIED-1:1
- `0x495954 VIBE_Command_RequestBuildOp83`: 5 dwords a1[0..4] at +0x10..+0x20,
  opcode 83 — matches TavernPacket::encode (16/20/24/28/32).
- `0x5186b4 VIBE_Location_TavernDarkCornerBuy`: `v25[0] = 1651865888`
  (== 0x62757920 "buy " tag), v25[1]=player, v25[2]=location, v25[3]=target,
  v25[4]=price; CheckResourceAmount affordability gate on the price — matches.

### src/play/text_recon.cpp — VERIFIED-1:1 (tables byte-diffed)
- `@0x622990` version string 23 bytes: byte-identical (get_bytes).
- `@0x622984` "Oct 10 2002": byte-identical.
- `@0x5271c2` month table 48 bytes ("Jan\0".."Dec\0"): byte-identical.
- `0x527c68 FormatBuildVersionString`: 2-byte copy loop, strtok-on-space into
  16-byte-stride slots, month scan (result discarded), sprintf(byte_622990)
  with no conversions — matches (3-token cap is behaviour-identical for the
  fixed compiled date).
- `0x44add4 LookupLabelEntry`: stride-80 name walk (unk_77F6B0), value column
  dword_76BEB0[i], cap 0x3FFF, stricmp — matches (hook-backed table).
- `0x5e9eb0 PrintfWrapper`: ignores varargs, flushes &unk_64A5AA — matches.
- `0x5e9e30 ReadLine`: pre-decrement count (max size-1), getc==-1/0x0A breaks,
  null-return rule `last==-1 && (dst==buf || mode&0x20)`, 0x30-bit save/restore
  with &=0xCF — matches. (size<=1 reads uninitialized `v7` in the binary — UB,
  not reproducible; reimpl's last=-1 is the only defined-behaviour reading.)
- `0x5d3f10 StrCmp` / `0x5cb8f0 StrCmpNoCase` / `0x5e9cd0 Strtok`: local copies
  are equality-faithful (only ==0 is consumed in this file); strtok cursor
  semantics (leading-delim skip, end-of-string -> cursor null) match.

### src/play/text_recon3_itemlabel.cpp — FIXED (1 divergence)
- `0x59ccf4 VIBE_Text_FormatItemLabelWithIcon` case 4 (both branches):
  the binary computes the declension id as `*(int*)(rec+353) >> 24` /
  `*(int*)(rec+354) >> 24` with **`sar ecx, 18h`** (@0x59d181 and @0x59d255)
  — the SIGN-EXTENDED byte at offsets 356/357. The reimpl zero-extended
  (`record[356]` as u8). FIXED: `static_cast<signed char>(record[356/357])`.
  Divergent only for byte values >= 0x80 (id would be negative in the binary).
  Test pins use 11/4/7/3 — unaffected; goldens unchanged.
- All other branches verified line-for-line: plural-blocker end-byte address
  `rec + (off-1) + strlen(rec+off)` for offsets 48/64; bases 272/279 (byte13),
  525/560 (byte358), 294/370 (byte356), 471/498 (byte357) keyed on byte9;
  format strings "%s", "%ss", "%s$A%s", "%s$A%ss", "%s %s", "%s %s %s";
  suffix strings @0x627e00/@0x627e04 byte-diffed: both empty (all zero) —
  reimpl "\0\0" correct. WideCopy loop shape identical.

### src/play/turn_events.cpp / .h — FIXED (1 divergence) + VERIFIED
- `0x4c7004 VIBE_MeisterAi_ExpireEventSlots`: loop counter 0..10496 step 41 ==
  **256 iterations** (lead byte at counter*4, i.e. 164-byte slot stride);
  `0x4c70c0` AP ring: 0..5120 step 20 == **256 iterations**. The header's
  `kEventSlotCount/kApEventSlotCount = 64` ("10496/41/4") mis-applied the *4
  byte-scaling as a count divisor. FIXED old=64 -> new=256 (both), comment
  corrected. Per-slot rule verified: event ring clears payload on lead==0
  (exact-zero, unsigned); AP ring clears on signed `v1 <= 0` and zeroes the
  payload dword + two siblings (reimpl models one payload; documented).
- Driver order vs `0x533188 VIBE_GameTick_BeginPlayerRound`: TickRegisteredEvents
  -> ExpireEventSlots -> ExpireApEventSlots -> ... -> He_ProcessAllPlayerNews —
  relative order matches RunEventsTurn.
- `0x579f70 AdvanceCalendarClock`: reimpl is the documented diff-minutes-integral
  model. NOTE (model boundary, no edit): the binary snapshots qword_1235262 when
  diff==0 OR when the EMA guard holds (`flt_641DAC != 0 && 6 < hour < 21`); the
  flt_641DA8 EMA half is owned by sim/command_apply6 (out of chunk).

### src/play/turn_economy.cpp — VERIFIED-1:1 (order)
- Amt pass order replayed from `0x533188`: RunProductionPass ->
  UpdateOfficeProsperity -> RunBuildingTaxPass(3) -> ProcessLoanRepayments ->
  ProcessAllOfficeWages -> UpdateOffices, then City_TickStatsAndBroadcast —
  decompile confirms exactly this sequence. Rule cores live in world/ (out of
  chunk).

### src/play/turn_driver.cpp — VERIFIED (wiring)
- Pure orchestration over out-of-chunk reconstructions (EconomyTickPriceLevel,
  ProductionComputeOutputOverTime, FireEventBurnTick, GameTimeAdvance). No
  in-file binary constants to diff.

### src/play/turn_ai.cpp — VERIFIED (wiring)
- Turn-flag sweep mask `& 0xE0874703` confirmed @0x533188 (dword_12CEAD8 walk).
- worker+61/+65 offsets and rule cores (0x53265f/0x5328a3/0x532485) owned by
  ai/ (out of chunk).

### src/play/tutorial_recon3_stepvoice.cpp — VERIFIED-1:1
- `0x597300 ShowStepWithVoice`: guard chain (!off[5] -> -4; SetDialogTexts
  result != -4 propagation), text branch (step+8 in {1,2} && off+1 in {10,11}
  -> fixed 0x1D6B/0x1D6C pair, else step pair), voice restart (only on step
  pair when *(step+16); stop-if-playing + off[4]=0 when handle live; then
  PlayPositionalSample) — all match.

### src/play/ui_recon3_widget.cpp — VERIFIED-1:1
- `0x410178`: disasm is exactly `call VIBE_Widget_DestroyByType ; retn`.

### src/play/ui_recon4_hud_surface.cpp — VERIFIED-1:1
- `0x553f30 BuildPersonCard`: centering `(theme+117762 >>16)/2 + baseX`;
  portrait x `v24 - (theme[84*v9+78]>>16)/2 - 3`; base frame a2+28 / portrait
  a2+29; name-id select byte13/byte9 (272/279); label column 202; slider gate
  byte2 != 6 at v24-35, byte2==7 -> value 0; icon row {5,6,7}; no-record
  sprite 1190 with theme+100038 — all confirmed.
- `0x55433c BuildPersonCardSimple`: same centering; base frame at a2; portrait
  idx from *(rec+396); same slider/icon/no-record shapes — confirmed.
- `0x5540d7` name text id: byte13 && a5 -> (byte9 ? 279:272) + byte13 — match.
- `0x4243d8` colour packing: disasm @0x424430..0x42444c — a3|a4<<8|a5<<16 — match.
- `0x41eee8 PaintboxClearAlt`: v5[160] then v5[10] gates then ColorFillRect — match.
- `0x51adb4 ShowLenderDialog`: init loop 16 slots {-1,-1,0} (v3+=3 to 48);
  slider panel {14,24,14,14} @dword_67EF18/1C/20/24; `v22 += *(v25+180)` in
  32-bit int — all confirmed.
- `0x51d9a4 ShowAssetOverview`: gray 40, mode byte 6, cap 1024, text 5371 — match.

### src/play/ui_recon5_panels.cpp — FIXED (1 divergence) + VERIFIED
- `0x4b1ba8 PlayerBar_Create` init loop @0x4b1d17: i+=10 pre-store, 32 slots,
  sentinels {-1,-1,-1,0xFFFF,-1,-1} + flag 0 — decompile-identical.
- `0x548c54 AbductChooseDestination` gate FSM: skill2 -> null target -> office
  pick -> office confirm -> count<=0 (msg 4969 + edge-scroll) -> skill3
  (edge-scroll) -> perga_rolle picker — confirmed in order.
- `0x55bff8 InfoPanel trait rows`: all 9 masks/ids confirmed (word22&0xF/0xF0
  ->4657/4658, byte45&0xF/0x30 ->4659/4660, dword11&0x1C000 ->4661,
  word23&0xE/0x70/0x180 ->4662/4663/4664, byte47&0x1E ->4665).
- `0x55a9f8 CityTowerPennantPos` — **FIXED x87 rounding**: the binary forms
  `v7 = (double)corner * scale` as a DOUBLE and folds `v7*dx + linksOben`
  on the FPU with ONE final float store; the reimpl multiplied corner*scale
  in float and rounded before the add. Now: double product, double chain,
  single f32 cast. Test values are exact — pins unchanged.
- `0x5441d0 MapView dispatcher`: radio rows (432, {88,166,218,270,354,406,458,
  536}) with sprites {1334,1338,1340,1337,1335,1339,1336,1341} confirmed;
  scroll keys byte_671E28/E30/E2B/E2D -> (0,-4)/(0,+4)/(-4,0)/(+4,0)
  confirmed; clamp 512/360 vs dword_1233440/44 confirmed.
- `0x4ba614 HudShadowSelection`: 768-entry stride-536 walk, shadow copy
  byte_11B4F1F[v4+1] = cur at loop tail, ++dword_6317B0 per selected —
  matches the modeled shadow/count (binary additionally requires the entity
  ptr dword_12CEA94 non-null for the count; the model's `cur` table is the
  filtered selection byte — documented).

### src/play/universe_render.cpp — VERIFIED (driver)
- Terrain arm: `0x5B3900` calls `VIBE_Floor_RenderTerrain(dword_64A028, a2)`
  gated on dword_64A028 — matches the hooks wiring.
- Colour-key producer claim confirmed @0x5dad52: `v61 = !dword_140809C &&
  bpp > 8; flags104 = (4*v61) | (flags104 & ~4)` (bit 2 = colour key), and
  bit 3 (value 8) set from an "_NM" name match — exactly as the W5-CKEY
  comments state. Raster/project/transform leaves owned by render/ (verified
  by render waves).

### src/play/terrain_render.cpp — VERIFIED (driver; all in-file annotations)
- `0x5bd44c` loader: gate `if (floor+20 && floor+16)` (line ~508); mask writes
  `+8 = (N-1)|(N*N-1)`, `+12 = N-1` (@0x5bd54e region) — floor_.mask = N*N-1
  faithful; `+7268 = 0x41A00000 (20.0)`, `+7272 = 0x42200000 (40.0)`;
  `*(floor+7280) &= 0xE1`; minLod nibble `byte_64A02D & 0xF` — all confirmed.
- `0x5bf22c` UV assignment disasm @0x5c1f78..0x5c201b re-verified instruction-
  for-instruction: `byte_13DCE58[(quad&0xFF)+base] & 0x3F`, GetOrBuildTile
  @0x5c1fc8, `uvBase = subTexId*0x60`, poly+0x14 = texRec, `test bl,80h`,
  poly+0x3c, `flt_13FE540 + uvBase` -> poly+0x10, +0x18 -> poly+0x38.
- Documented host models remain as declared (transition-tile bake kernel with
  the @0x603180 quantiser named gap; BuildTerrainLightMap kept only as a test
  helper — the live path uses the type grid). No hidden divergence claims.

### src/play/wire_char_anim.cpp / wire_hud_bridge.cpp / wire_atmos_bridge.cpp — VERIFIED (bridges)
- wire_hud_bridge bank layout vs `0x5d861c ShowFromBank`: count u16 @+42 with
  `idx > count` reject, offsets @+69, depth byte @+12 (0 -> 0, 1 ->
  BlitColored16, 2 -> 1), dst+16 widthPx — all confirmed.
- wire_atmos_bridge: `0x5f4428 VIBE_Shadow_ResetLightList` confirmed
  (dword_1408A5C=0 + scene walk) and BeginUniverseFrame @0x5B3900 calls it,
  then the particle loop, then UpdateSkyFlares — the bridge order matches.
- wire_char_anim: AdvanceFrameIndex(flags, cur, last, first, count) call
  matches render/skeleton.h `@0x5ccf18` prototype; leaves owned by render/.

### src/play/video_movie_backing.cpp — VERIFIED (approved shim)
- pl_mpeg/IVideo backing per the rule-6 decision (video -> pl_mpeg shim);
  no binary counterpart to diff.

## Fixes applied (3)
1. `text_recon3_itemlabel.cpp` case 4: zero-extend -> sign-extend of the
   declension id byte at offsets 356/357 (`sar ecx,18h` @0x59d181/@0x59d255).
2. `turn_events.h`: kEventSlotCount/kApEventSlotCount 64 -> 256
   (0x4c7004: 10496/41 iterations; 0x4c70c0: 5120/20 iterations).
3. `ui_recon5_panels.cpp` CityTowerPennantPos: x87 double product + single
   final float rounding (0x55a9f8 v7/v8 are double locals; one fstp).

## Out-of-chunk findings
- none proven. (0x579f70's EMA/snapshot guard noted above is owned by
  sim/command_apply6 — its kCalendarClock model should be checked by that
  chunk's owner against the `flt_641DAC != 0 && 6 < hour < 21` guard.)

## Test targets run (all green, GUILD_GAME_DIR set)
text_recon3_itemlabel_test 62 | turn_events_test 29 | turn_events_itest 7 |
turn_events_e2e_test 12 | ui_recon5_panels_test 377 | game_day_test 19 |
game_day_itest 58 | game_day_e2e_test 40 | slice_council_test 33 |
slice_council_itest 20 | slice_council_e2e_test 19 | slice_church_itest 14 |
slice_combat_itest 28 | playthrough_test 16 | play_slice_estate_test 35 |
play_slice_estate_itest 19 | play_slice_estate_e2e_test 19 |
play_slice_market_test 52 | play_slice_market_itest 28 |
play_slice_market_e2e_test 21 | slice_personnel_test 38 |
slice_personnel_itest 16 | slice_personnel_e2e_test 15 |
slice_production_test 49 | slice_production_itest 13 |
slice_production_e2e_test 21 | slice_tavern_test 40 | slice_tavern_itest 20 |
slice_tavern_e2e_test 23 | play_text_recon_test 21 | turn_driver_test 18 |
turn_driver_e2e_test 21 | turn_ai_itest 17 | turn_ai_e2e_test 15 |
turn_economy_test 24 | turn_economy_itest 32 | turn_economy_e2e_test 11 |
tutorial_recon3_stepvoice_test 28 | ui_recon3_widget_test 14 |
ui_recon4_hud_surface_test 149 | wire_atmos_bridge_test 47 |
wire_atmos_bridge_itest 14 | wire_atmos_bridge_e2e_test 18 |
wire_char_anim_test 25 | wire_char_anim_itest 14 | wire_char_anim_e2e_test 24 |
wire_hud_bridge_test 19 | wire_hud_bridge_itest 9 | wire_hud_bridge_e2e_test 10 |
universe_render_e2e_test 22 | play_terrain_render_test 32 |
play_terrain_render_itest 15 | play_terrain_render_e2e_test 12 |
terrain_ground_test 281 | terrain_texturing_test 32 |
video_movie_backing_test 18.
Total: 56 targets, 1746 checks, 0 failures.
