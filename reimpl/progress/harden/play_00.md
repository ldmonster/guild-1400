# HARDEN play_00 — 1:1 verification vs gilde.exe (IDA MCP)

Chunk: 22 files in `src/play/` (building_scene_attach … hud_recon_credit).
Method: per-function decompile/disasm diff; per-table `get_bytes` byte-diff.
Evidence for every claim is the cited binary address.

## FIXED (proven divergences, binary evidence)

### src/play/camera_target.cpp — VIBE_Camera_ComputeWorldTarget @0x4c0864
- **FIXED (x87 width)**: in the `mask & 0x100` branch the binary keeps
  `var_4 - 1.0*6.0` (`fsubr` @0x4c08b8) and `1.0*10.0 + var_8` (`fadd`
  @0x4c08cd) on the x87 stack straight into ConvertX + `fistp` — no float
  spill. The reimpl narrowed both to `float` before ConvertX, which can round
  up across an integer boundary and change the truncation. Now modeled with
  `double` intermediates.
- Constants byte-verified: flt_61E510=0.5, flt_61E514=6.0, flt_61E518=10.0,
  flt_62D224=1.0.
- VIBE_Coord_Push @0x5d8ae8: **VERIFIED-1:1** (64A1B8=edx, 64A1BC=ebx,
  64A1C0=ecx, 64A1B4=eax=result).

### src/play/cutscene_recon2_movie.cpp/.h — VIBE_Movie_PlayOutro @0x534924
- **FIXED (inverted loop exit)**: the fade pre-roll spin `fldz; fcomp
  flt_62DA00; jbe loc_534974` (@0x534a83..0x534a8e) jumps BACK into the loop
  body while `0.0 <= flt_62DA00`; the exit requires the timer NEGATIVE.
  flt_62DA00 is the music-fade target — VIBE_Audio_MixerUpdate @0x43a3e4
  parks it at **-1.0 when the fade completes**. The reimpl (and its tests)
  had the condition inverted (exited when timer >= 0). Code + 3 unit tests +
  video_movie_backing test fixture updated (old: exit on `timer >= 0`; new:
  exit on `0.0 > timer`).
- **FIXED (constant transcription)**: `kMovieMusicVolumeScale` literal
  `0.00787353515625f` encodes 0x3C010000; `get_bytes` @0x623640 gives
  `04 02 01 3C` = 0x3C010204 = 0x1.020408p-7f (≈1/127). Literal replaced
  with the exact hexfloat.
- Rest of the control flow (moveahead.dll lazy bind + proc list, save/restore
  byte_642008, Sleep 500/200, prepare arg 138, focus/input sequence):
  **VERIFIED-1:1**.

### src/play/cutscene_recon2_theatre.cpp — VIBE_Theatre_RunEventMenu @0x536a30
- **FIXED (Birth path)**: reimpl set `master3 = masterDword`; the binary
  Birth block (@0x536df5 clears the 276-byte record, then @0x536e34/38/3c
  writes only v36/v43/v44) leaves v45 (master3) = 0. Assignment removed.
- GatherTenancyParticipants (0x536e7e-0x536ea4): **VERIFIED** (roles
  {6,7,5,4}, byte-offset cap v21<32, 768-record bound).
- GatherDuelParticipants (0x537275-0x5372f7): **VERIFIED** (compare word col
  0x12CE910, store dword col 0x12CE914, v29<8 cap = max one stored, 411648
  bound).
- FindByRole (0x536c44-0x536c8c): **VERIFIED** (102912-dword bound; not-found
  leaves output untouched, modeled by the fallback param).
- ComputeDisasterOffset (0x536fde/0x5371a7/0x5371d3): **VERIFIED**
  (rand(2)→days, rand(4)(+7 iff month word >= 0x17)→months, RNG order).
- Event/disaster codes, subKinds, amounts (5/6/7/8/9/10/4; 89/78/80; 32/34;
  16000/2000/1000; 3*rand(3) plague): **VERIFIED**.

### src/play/gamelogic_recon.cpp/.h — VIBE_Gfx_CrossFadeStep @0x41e814
- **FIXED (phantom field)**: the teardown gate `v3[7] > 288` is int index 7 =
  byte +28 — the SAME dword as the alpha written @0x41e830 (the alpha doubles
  as the teardown timer; matches the twin reconstruction in
  sim/command_leaves.cpp). The reimpl gated on a separate invented `age`
  field. Field removed from CrossFadeRec; gate now `rec.alpha > 288`; test
  re-pinned (old: `age=300` fires; new: alpha 288→296 fires) plus a new
  no-teardown-at-258 test.
- Orchestrators (declared hook-models; verified the concrete claims only):
  - RunFrameLoop @0x4c09a0: all fmask bit tests (1/4/0x80/0x100/0x200/0x4000/
    0x10000/0x20000/0x100000/0x200000) match the binary; frame counter
    @0x4c12e5, skip decay @0x4c14a3, return v10 @0x4c14b0 — **VERIFIED**.
    Large engine blocks are explicitly delegated to hooks (documented in-code).
  - Interactions @0x4139a8: type-switch thresholds, 510-slot walk, checkbox
    flush — **VERIFIED** (hook model).
  - ProcessTurnActions @0x52f8d0: handler kinds 102/112/104/133/124/131/132/76,
    rich-string ids 0x7D/0x1C2C/0x1AD4..0x1ADA/0x1AD5, 75*SumCurrencyHeld,
    rand(0x5A), frame-loop id 147591 — **VERIFIED**. Comment fix: StandUp arg
    stride is `dword_12CEA94[134*i]` (was mis-annotated 218).
  - RunTurnTransition @0x5310a4: row ids 0x1BE2..0x1BF9/0x1BE0/0x1C23-0x1C29/
    0x1C04 + rec offsets — **VERIFIED**. **Fixed pin**: the per-tax row id base
    is DECIMAL 6975 (`(raw>>24) + 6975` @0x5318f0), not 0x6975.
  - CleanupTurnHandlers @0x530e50: filters "B`K[\ZF.-A"/"EG", stride 332,
    caption pools dword_8C4320[rand(0x70)] / dword_8C400C[rand(0xBF)], state 5,
    actor roles {3,4,5,6,7,9} over 96×69 — **VERIFIED**.
  - SetupHomeSweetHome @0x52aa34: type order 6,32,42,30,31,18,20,50,33,25,52,54;
    action bursts (21/23/24/20; 25@464; 20@464,15@471,15@470,10@344; 247;
    10@459/460/464 ×2) — **VERIFIED**.
- characterSetVisible/characterStandUp defaults delegate to sim::SetVisible
  (0x401894) / sim::StandUp (0x405504); both bodies live in sim/ (out of
  chunk) — delegation edges verified against the decompiles.

### src/play/gametick_recon4_orchestration.cpp — 10 functions
- **FIXED (table)**: `kEventWeightTableA` @0x577954 — `get_bytes` gives
  `0,0,1,1,1,1,1,1,`**`1`**`,2,2,2,2,2,2,2` (index 8 is 1; old transcription
  had eight 2s). Test pin updated (idx 8: picked 2 → kind 78; new idx-9 case
  pins kind 80). Tables B @0x577994 / C @0x5779d4: **byte-exact**.
- **FIXED (x87)**: AdvanceTurnTimer @0x57957c — `fmul/fadd/fadd`
  (@0x579674..0x579682) accumulate at register width; `fst var_28` narrows
  only the STORED accum; `fcomp var_24` @0x579690 compares the still-wide
  value against the threshold. Reimpl rounded each step to float and compared
  the narrowed value. Now: double accumulate, `(float)` store, wide compare.
- Universe_DisplayLogAndCleanup @0x5b3030 / InitLogAndInflate @0x5b3410:
  **VERIFIED** (flag bits 0x02/0x01 of the high byte, `~flag & 3` suspend,
  terrain-build gate).
- Universe_RunObjectScriptPass @0x5b3628: **VERIFIED** (two walks v9=1/0,
  texture upload, light refresh return).
- Universe_DestroySlot @0x5b5050: **VERIFIED** (0x40 bound, active-slot reset,
  246-dword slot stride, drain/free/switch-back order).
- GameTick_RequestStartTurn @0x579530: **VERIFIED** (36-byte snapshot,
  word[0]=0, tag 1685283436 in word[9] AND as RequestBuildOp86 arg).
- GameTick_RunAdvanceGameDialog @0x4c0750: **VERIFIED** (TestHandlerFlag(8)
  gate, panel event 0xB open/close, rich string 0x82, dword_1233558=750
  behind the finalize hook; return modeled — dispatchPanelEvent hook is void).
- Game_ShutdownSubsystems @0x5278cc / AllSubsystems @0x52794c /
  WorldAndSubsystems @0x52f44c: **VERIFIED** (exact call order incl. 96-bank
  loop, dual 63C900 blocks, 512-character loop, 6 sky layers, 6 world banks).

## VERIFIED-1:1 (no churn)

### src/play/config_apply.cpp — @0x56c0cc
Constants byte-verified (flt_62520C=0.00625f, dbl_625214=0.25,
dbl_62521C=0.5, dbl_625224=0.8); edx = &dword_1233558 config block (+4
sensitivity byte, +8 wheel); unsigned fild for the advanced branch; store
order fstp → SetWheelBase → 100-sensitivity.

### src/play/cutscene_recon2_tutorial.cpp — @0x5976a8
"$C" token (aC_10 @0x5976ba), SelectWindow→RenderRichString→SetObjectsVisible
order, +0x30 form / +0x10 voice gate-stop-clear.

### src/play/building_scene_attach.cpp
- ReadCityObjectRecord @0x5e67c8: full stream framing diffed — version gates
  (0x3A6C000B/0D/A0/A1/A3/A4/A6/A7/A8/A9/AB/AC/AF/B2/B4/B8/B9/BA), old-kind
  ladder, '!'-strip rule, per-type reads (case 0/1/4/2/3/5-8), anchors loop
  exactly 10 (disasm @0x5e7040 `cmp ecx,0Ah; jl`), light 4/6/7 keyframe
  counts, STRANGEFUCK placeholder + empty-leaf free, child/sibling recursion,
  event-bindings framing. Strings/consts byte-verified ("gb_" @0x621458,
  patterns @0x621474/0x62145c/0x6214e0, "sonstiges\sp_WIMPEL.baf" @0x6214f8,
  +Z axis @0x5CA2B0).
- BuildingComputePlacementHeight @0x50cec0, BuildingAlignMeshToTerrain
  @0x50cfd0: **VERIFIED**.
- ResolveGebaeudeOgrVariants (0x50d178..0x50d26d): **VERIFIED** (128.0f bound
  as signed bit-compare vs 0x43000000, char = trunc(counter+64.0),
  miss-at-zero retries).
- BuildingLoadAndAlignGebaeudeModel @0x50d01c: **VERIFIED** — dialog ids 5125/
  5122, text id 14*btype+1078, bestDist 1e9, byte_67225C='2' (disasm
  0x50d569), wimpel colors (192,128,160) + transparency 65664/ecx=128 (disasm
  0x50d438..0x50d47a), preview transparency 65757, pulse 90.0f
  (0x42B40000 signed bit-compare), dbl_621558=0.5, flt_621564=400.0,
  flt_621568=pi, frame-loop id 415687, exit uses paths[randIdx] (v110).
- **FOUND (root out of chunk)**: the binary @0x50d370 passes v79
  (typeNameBuf) into VIBE_Building_FilterBlockedBauplatze; the walk callback
  @0x50c7b0 matches plot names against it and only falls back to "bk_" when
  NULL. The reimpl's `sim::Building_FilterBlockedBauplatze` (building5.cpp,
  NOT in this chunk) takes `i32` and hardcodes `g_bauplatzWant = nullptr`, so
  the name never reaches the walk — plot collection is wider than the binary.
  5 of 6 other binary call sites (0x479e27, 0x4815fb, 0x4f51c8, 0x502909)
  also pass a name; only 0x577482 passes 0. Documented at the call site;
  needs a building5 owner to widen the API (name pointer through the walk).
- ObjectBuildModelName / ObjectRebuildModelByOwner adapters (0x4ffe0c /
  0x5a8140): re-read-+512-every-iteration semantics and link writes verified;
  bodies reuse sim/object_lifecycle7/9 (out of chunk).

### src/play/camera_controls.cpp
Declared model over VIBE_Camera_UpdatePan's decision core; ALL pinned engine
constants byte-verified @0x61DDB8..0x61DE08 (0.4, 4.0, 5.0, 50.0, -50.0, 1.5,
0.6, 1.1, 0.1f) plus kick seeds 0x41200000/0xC1200000.

### src/play/city_view3d.cpp (verify-only per brief)
- BuildEngineFrustum @0x5accd0: **VERIFIED** — `fst qword var_88/var_90`
  keeps the atan2 results at double width into fsin/fcos (reimpl double
  intermediates correct); eps dwords 0x360637BD/0xB60637BD with the exact
  per-plane sign pattern (DAC=-, DBC=+, DCC=+, DDC=-); atan2 #2's fchs on
  flt_13FCAF8 (pre-negated scale) folds to +viewScale. Not churned.
- FindEntranceDummyNode: tags "dummy_EINGANG" then "dummy_TUER" (pushes
  @0x57ca38/0x57ca47, string @0x625914) — order verified.
- Remainder is declared integration/hook code (Vulkan-boundary stand-ins).

### src/play/hud_recon_credit.cpp
- 0x519dbc NewLoan_CompactOffers: **VERIFIED** (3 candidates stride 3,
  pre-incremented v5+=5, slots 0/1/2/4, +3 childId written later by the
  widget builder — all in the decompile).
- 0x51ab48 LoanRelease_ComputeRepay: **VERIFIED** (held-repay vs repay-held
  payer split, dword_63170C preview).
- 0x51d64c AccountInfo_HandleClick: **VERIFIED** (tab 1/2 + dirty).
- 0x51a868 LoanList_InitRowTable: **VERIFIED** (exact `for(i=0;i!=27;
  v24[i+2]=0){i+=3;...=-1;...=-1;}` net writes, 30-dword buffer).

### src/play/game_day.cpp
kDayOrder pins diffed against the 0x533188 decompile call sites
(0x533191..0x5336e1) and the 0x498954 callee set — order and addresses
**VERIFIED** (Character_SyncAllTurnStates pins the 0x5320f0 entry, noted).

### Integration/model files (no 1:1 provenance to diff)
camera_pick.cpp, choosecity_scene.cpp, city_frame.cpp, city_info.cpp,
determinism.cpp, dialog_bank.cpp, dialog_council.cpp, dialog_market.cpp
(layout offsets come verbatim from gui:: modules owned elsewhere),
full_session.cpp, hud_binder.cpp — declared integrators over real
reconstructions in other modules; no reconstructed-function claims to verify
here beyond the cross-references, which resolve to the named real symbols.

## Tests (all run with GUILD_GAME_DIR=$PWD/europe_guild_1400_original)

| target | checks | failures |
|---|---|---|
| skycam_wave20_test | 277 | 0 |
| config_apply_test | 12 | 0 |
| cutscene_recon2_test | 119 | 0 |
| video_movie_backing_test | 18 | 0 |
| gamelogic_recon_test | 86 | 0 |
| gametick_recon4_orchestration_test | 323 | 0 |
| building_scene_attach_test | 170 | 0 |
| hud_recon_credit_panel_test | 86 | 0 |
| camera_controls_test / _e2e | 34 / 18 | 0 |
| camera_pick_test | 43 | 0 |
| city_frame_test | 29 | 0 |
| determinism_test | 22 | 0 |
| full_session_test | 38 | 0 |
| game_day_test | 19 | 0 |
| dialog_market/bank/council_test | 52/49/48 | 0 |
| city_view3d_test / _e2e | 52 / 52 | 0 |
| choosecity_scene_itest | 16 | 0 |
| gui_options_run_e2e_test | 14 | 0 |
| playable_flow_e2e_test | 154 | 0 |

Updated golden pins (all with binary evidence): fade-timer exit direction
(0x534a8e), kMovieMusicVolumeScale bits (0x623640), CrossFade teardown field
(0x41e897 = +28), kEventWeightTableA[8] (0x577954).
