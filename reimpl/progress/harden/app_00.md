# Harden report — app_00 (src/app/*, 20 files)

Method: every `// gilde.exe 0x…` provenance in the chunk decompiled via the IDA
MCP CLI and diffed line-for-line against the reimpl; suspicious decompiler
artifacts re-checked at the instruction level (`disasm`); every `@0x…` const
table/string byte-diffed with `get_bytes`. Only proven divergences were edited;
each fix cites its binary evidence. Files marked GLUE are integration wiring
with no 1:1 claims (they bind already-reconstructed modules owned by other
clusters); their embedded address references were spot-verified.

## Verdicts by file / function

### src/app/app_init.cpp (GameApp spine adapter)
- 0x52895c `VIBE_Window_CreateMainWindow` — **FIXED**. The style branch tests
  `mode == 3` ONLY (0x52895c body: `if (a2 == 3)` → WS_POPUP|WS_VISIBLE
  0x90000000 + WS_EX_TOPMOST 8, else 0x06000000 windowed). The reimpl treated
  mode 4 as fullscreen too (`mode == 3 || mode == 4`). Mode 4 only joins mode 3
  later, in 0x527fa4's topmost `SetWindowPos`. Fixed to `mode == 3`.
- 0x527de0 `VIBE_App_InitSubsystemsAndMovieDll` — VERIFIED-1:1 (order:
  ErrorLog → Memory_InitTracker(32678) → MemPool(0x80) → [gate dword_63C8F0]
  moveahead.dll → CreateDirectory "%sgamedata\Screenshots" → Vfs_Init →
  TimeBase_StartTimer(0xE,0) → return 1).
- 0x527fa4 `VIBE_Render_InitDisplayAndPaths` — VERIFIED as modeled adapter
  (order of engine steps preserved; DirectInput mode `mode==3 ? 1 : 6` matches
  0x527fa4; fullscreen SetWindowPos `mode==3||4` matches). NOTE (documented
  gap, not fixed): the original also calls
  `VIBE_Config_ApplyCameraAndScrollSettings` after DirectInputInit and
  `VIBE_Input_SetWheelBase(80)` in the mode-3 branch; the ISubsystems adapter
  has no hooks for these (the strict 1:1 sequence for the engine-init variant
  lives in engine_init_app_recon.cpp, which does dispatch it).
- 0x528560 (adapter variant) — VERIFIED (delegates; strict 1:1 is
  engine_init_app_recon.cpp below).
- 0x534bbc `Run` / 13-step `Shutdown` — VERIFIED-1:1. The teardown block
  (Game_ShutdownSubsystems → Widget_ShutdownSystem → Config_WriteGfxSettings →
  GameState_FreeAllResources → Universe_SwitchActiveSlot(0) →
  Table_ResetLightmaps → Render_ShutdownEngine → Input_DirectInputShutdown →
  TimeBase_StopTimer → Vfs_Shutdown → MemPool_ShutdownStack →
  Memory_ShutdownTracker → ErrorLog_Shutdown) appears verbatim at every exit of
  0x534bbc in exactly this order; run order (mutex → window → subsystems →
  intro → display → engine → menu/session → teardown → DestroyWindow) matches.

### src/app/frame_drivers.cpp / .h
- 0x50c720 `RunFrameLoopWrapper` — VERIFIED-1:1 (disasm 0x50c721:
  `mov edx, 67FFFh`; kMainSessionMask == 425983 == 0x67FFF).
- 0x56e7e0 `RunPauseLoop` — **FIXED**. The paused mask is NOT zero. Disasm
  0x56e7e7 `mov ecx, ds:dword_11BC2D0` (the last published frame mask),
  0x56e7f2 `or ecx, 100000h`, 0x56e805 `and ch, 0DFh` (clear 0x2000) —
  i.e. `(lastMask | kInputSuppress) & ~kOptionsAndPanels`, computed once before
  the loop. Reimpl passed a 0 mask (comment claimed "uninitialised-but-zero").
  Test pins updated: unit `lastFeatureMask` 0u → 0x100000 (fresh app) plus a new
  derive-from-0x67FFF test (0x165FFF); e2e pin 0u → 0x165FFF.
- 0x527be8 `RunEndRoundScreen` — **FIXED**. Disasm 0x527c00 `xor ecx,ecx`,
  0x527c14 `cmp edx, 4BAh` / 0x527c1c `mov ecx, 1`: a pending action of exactly
  0x4BA (1210) latches the post-loop mode switch (`if (ecx) { dword_63CC3C = 1;
  Hud_FindModeIndex(InitOrLoadSession); }`); dword_631614 = 1 is raised for ANY
  pending != -1. The reimpl only modeled the advance flag. Added
  `kEndRoundModeSwitchAction = 0x4BA` + `modeSwitchRaised` out-param + tests.

### src/app/frameloop.cpp (0x4c09a0 RunFrameLoop, mask-gated dispatch)
- All 17 mask-bit gates diffed against the binary (0x4c0ab7 bit4, 0x4c0aec
  0x100&!0x100000, 0x4c0b55 bit0, 0x4c0be5 0x20000, 0x4c0c19 0x200, 0x4c0c31
  0x80, 0x4c0c52 0x40 (+0x4c0c61 !0x10000&&0x40000), 0x4c0cb6 !0x8000, 0x4c0d1d
  0x4000, 0x4c0e0c !0x100000, 0x4c0e80 0x10&!0x100000, 0x4c0e8f 0x20, 0x4c0ea3
  0x400, 0x4c0fcd 0x80000, 0x4c104b.. 0x2000, 0x4c13fc/0x4c147a !0x200000) —
  all match. Return-value semantics are a documented host adaptation (binary
  returns the mouse-latch v10 and early-outs `return 1` on byte_63CC14).
- **FIXED** (one gate): the standalone music emission. Binary 0x4c0d56:
  `if (dword_63C8F8 && (v54 & 0x40000))` — bit 0x40000 ALONE, no kRenderWorld /
  kHeadlessSuppress terms. The fused `dayCycleAndOutdoorMusic` hook previously
  never fired for masks like 0x40000|0x40|0x10000; now it fires iff 0x40000 is
  set. Test pin updated in app_spine_test (0 → 1 for the headless case) with the
  0x4c0d56 evidence.

### src/app/menu_loop.cpp
- 0x40dab8 `InputLatchMouseState` — VERIFIED-1:1 (clears the 6 live edge words,
  76-byte-stride scan bounded at 2432, first-pending-wins, field copy
  670FF8→672228 / 670FF0→672220 / 671024→672254 / 671020→672250 /
  670FE6/670FE8→word_672216/8, slot flag cleared; PollKeyboardDevice tail is a
  documented deferred OS leaf).
- 0x529d08 `MenuDispatchFrame` — VERIFIED (click gate dword_672228, hovered id
  dword_62D22C != -1, per-button cascade == gui::MainMenu_Dispatch, ESC
  byte_67225C==1 → dword_63CC48=1 + dword_631614=1; menu frame mask 147591 is
  the caller-driven RunFrameLoop cadence, host-bounded here).
- 0x534bbc outer-loop decision `MenuMainDecide` — VERIFIED (quit > restart >
  session-armed > re-menu priority).

### src/app/combat_scroll.cpp
- 0x487b2c `ResolveCombatScroll` (decision half) — VERIFIED-1:1 at instruction
  level (edge offsets +0xFC right / +0xE4 left / +0xB4 top / +0xCC bottom;
  tolerance 0x3E4CCCCD = 0.2f vs flt_5CA2E0 zero vector; codes: directed 1→2,
  neutral 0→1; the decompiler's `v4 + 1` / `v9` artifacts resolved to 2 and 1
  by disasm at 0x487bc3/0x487d7a).

### src/app/entity_movement.cpp
- 0x412f18 `EntitySubRefcount` — VERIFIED-1:1 (stride 84; offsets +60 type,
  +48 suppress, +76 link, +64 refcount; kinds 5/8 redirect; underflow guard).
  Message string byte-diffed @0x610e8c: "d2_SubRefcount: invalid refcount!".

### src/app/config_write.cpp
- 0x5d92ec `AnimationState_Update` — VERIFIED-1:1 (radix==10 && v<0 → neg +
  '-', ultoa core).
- 0x56af54 `ConfigWriteGfxSettings` — VERIFIED-1:1 (all 39 keys, exact order and
  sections; byte fields zero-extended; floats ×flt_6251F0 then truncating (int);
  stadt written verbatim). flt_6251F0 byte-diffed @0x6251F0 = 0x42C80000 =
  100.0f.

### src/app/engine_init_app_recon.cpp
- 0x527d48 mutex / 0x527d8c CloseHandle / 0x527db8 screensaver — VERIFIED-1:1
  (CreateMutexA(0,1,"Die Gilde"), !handle→-1, GetLastError()==183;
  pvParam[2]=a1,[1]=a2,[0]=0, SPI 0x10).
- 0x4520d0 guard-state call edge — VERIFIED (fused AiMethod_LoadDataFile →
  world::GameLogicInitGuardState; the table body is world-cluster).
- 0x528560 `VIBE_App_InitEngineAndScriptCommands` — **FIXED** (one gate) +
  VERIFIED otherwise. The full 30+-step sequence, the 7 market-price calls
  (469/471/468/474/473/470/472, qty 0x64, first qty = uninit dl modeled 0), the
  fade (BLACK, 90, mode 1, done bit 4, wait mask 147591), the 140-byte scratch
  clear, and the 5-step script registration order all match. Strings
  byte-diffed: "gilde_text"@0x622ab0, "\project\game\"@0x63ca0c,
  "Misc\Loading_main"@0x622af8, "BLACK"@0x622b44, the openengine error
  @0x622ac8. FIX: `Sound_LibInit` failure clears BOTH gates (0x528784
  dword_63C900=0, 0x52878a dword_63C8F8=0) and the music block RE-READS
  dword_63C8F8 at 0x528800 — so a failed lib init must skip the music bring-up
  even when music was enabled. The reimpl ignored the clear. Added the
  `musicEnabled = false` mirror + a new unit test
  (SfxLibInitFailureSkipsMusicBringup).

### src/app/render_submit.cpp / .h
- 0x412fa0 `GameLogicObjects` — VERIFIED-1:1 (bounds/alloc gates, all widget
  field writes +8/+12/+16/+18/+24/+26/+28..+34/+72/+104/+105/+116/+448..+472,
  kind dispatch 0/1/4/5/8/17 with exact unsigned-compare shape).
- 0x413580 `GameLogicEntities` — **FIXED** (four instruction-proven divergences):
  1. Animation_Basic's 3rd arg is the STATE HANDLE ecx (State_Update result at
     0x413614/0x413752, else the post-redirect record's +52 at 0x413680) — the
     reimpl passed the blob value and discarded the stateUpdate return.
  2. The redirect-path 5th arg is a BYTE delta: 0x41377d..0x41378f
     `sub al, ah; and eax, 0FFh` → `(u8)(v7 - idx)` zero-extended — reimpl used
     a full signed int difference.
  3. The per-kind dispatch selector is the BLOB VALUE itself (edx from 0x4136a4
     `mov edx,[base+84*blobIdx+0x3C]` compared at 0x4136b3) — the +60 dword of
     the PRE-redirect (blob) record — not the post-redirect record's kind.
  4. State_GetCurrent args were swapped: 0x413700 `mov ecx,[rec+0x50]` /
     0x413703 `mov ebx,[rec+0x4E]` + `sar 16` → arg3 = i16 @+82, arg4 = i16
     @+80. Struct fields renamed subStateLo(+80)/subStateHi(+82) (old comments
     mislabeled +78) and the call corrected.
  New unit test Entities_RedirectByteDeltaAndGetCurrentArgs pins all four.

### src/app/session_init.cpp
- 0x533a54 `InitOrLoadSession` — VERIFIED as the documented orchestration
  (office gate `(flags&1) && !a1`; net branch: History_SetActiveFlag(0), host →
  Server.dll + Sleep(0x9C4) + OpenBroadcastSocket(0x3039); Connect → QueueInit →
  loading screen → InitWorldAndSounds → inheritance gate (flags&1 && 63C78C);
  mode cascade &1/&0x40/&2; start gold cheat 75000 / 1250-250*difficulty
  (0x533f35/0x533f3b/0x5340e5 — provenance comment corrected from 0x533c5e);
  536-stride player scan, kinds 6/7, Money_MultiplyByRate(base, byte_6477A1),
  EnqueueCmd15; post-load tail order incl. flags&1&&!0x40 scene/daylight gate;
  turn loop mask v111=131079=0x20007 @0x533a60, round bound 800, +30-min
  advance to hour 0x17 then +24h and GameTime_Set(6,0,0)).
- 0x52f2ec `GameInitWorldAndSounds_Body` — VERIFIED (global re-init set at
  0x52f303..0x52f333 exact: 631DB4=1, 63CC2C=1, LODWORD(qword_13CE852)=2,
  63CC30=0, 63CC34=0, byte_63CC41=0, 631284=2; leaf order matches; the
  City_InitParameterTable ecx-raw-1 denormal claim is prior-wave analysis,
  consistent with `mov ecx, edx` at 0x52f309 — left untouched).
- 0x58f19c forward to world::AmtMoneyMultiplyByRate — VERIFIED call edge.

### src/app/save_drivers.cpp
- 0x56d984 `SaveDoQuickSave` — VERIFIED-1:1 (net/host/ready branch shape, blob=1
  cmd 15 + ack spin, banner ids, single-player banner→frame→thumbnail→
  byte_649D50=1→WriteGameFile(mode 1)→banner-restore; tutorial no-op).
- 0x4ff800 quicksave-trigger slice — VERIFIED (scancode 16, !dword_62D328,
  !(mask & 0x10000)).
- 0x56a804 `MenuRunSaveGame` — VERIFIED as modeled (name-input cancel loop,
  `!occupied || Dialog_RunMessageBox(...,257,..)`, byte_649D50=slot,
  "Gamedata\Saves\%s.SAV", WriteGameFile mode 1, dword_631614 latch).
- 0x56da74 `NetLoadAndSyncSession` — VERIFIED as modeled (non-net return
  result*4 == 64 verbatim; wait → 536-stride alive/kind 6|7 count →
  byte_649D50=dword_63CC70 → scenario pre-write mode 5 → header CRC (100-byte
  scratch, VIBE_Util_Crc32) → blob{personId, crc} cmd 16 → ack barrier →
  wait → SRV mode 6 → SAV mode 5 → banner restore). Path strings byte-diffed
  @0x6252b8/0x624ef0/0x624f40/0x624f94/0x6252fc — all exact.

### src/app/thumbnail_io.cpp
- 0x56d75c `WriteThumbnailFile` — VERIFIED-1:1 (render → capture → open "wb" →
  0xE100 alloc → 120×160 UnpackColor with the r→+0, g→+2, b→+1 pointer mapping →
  WriteStream(57600) → close).
- 0x56d870 `ReadThumbnailFile` — VERIFIED-1:1 (open "rb" → ReadStream(57600) →
  120×160 Result_Handler_Final(buf[3i],[3i+1],[3i+2]) into word_13CED78 (the
  decompiler's word_13CED76[v5] pre-increment aliases to the same address); the
  G/B round-trip swap quirk preserved).

### src/app/audio_tick.cpp
- Per-frame audio block of 0x4c09a0 — VERIFIED (wildlife + VoiceQueue under
  dword_63C900 @0x4c0d39; music under dword_63C8F8 && 0x40000 @0x4c0d56;
  Sound3d_UpdateAll @0x4c0ddf + UpdateListener under dword_63C904 @0x4c0deb +
  Sound_UpdateVoices @0x4c0df2 all inside the 63C900 block — matches).
- Market loop 0x582858/0x5828bc — VERIFIED (start gate !dword_6420F4, scene
  record gate, PlaySample→AttachToEntity(…,127, 2200.0)→SetLooping(1); stop:
  SetLooping(0)/StopEntry(1)/DetachEntry/clear — the reimpl's fused
  SoundSystem calls are the documented model).

### src/app/wiring.cpp (GLUE — RealSubsystems host wiring)
- **FIXED**: `soundWaveInitSineTables` built 256-entry tables; the only live
  call site passes **0x30 (48)** — 0x52879f `VIBE_SoundWave_InitSineTables(0x30u)`
  (d3sndw_Init @0x424d40 stores it to word_62D430). Changed to
  `audio::InitSineTables(0x30)`; test pins 256 → 48 in
  app_frame_drivers_test/app_frame_drivers_e2e_test (documented old→new with
  the address).
- **FIXED**: `moviePlayIntroSequence` played only "Intro.mpg"; 0x5347d4 plays a
  THREE-clip sequence — "%sjowood.mpg", "%s4head.mpg", "%sintro.mpg" (each via
  mov_prepare/mov_play with Sleep pacing). The wiring now plays all three in
  order (per-clip casing fallback kept; decoder itself is the rule-6-approved
  pl_mpeg swap).
- Everything else: verified as declared glue (no engine logic; address
  references spot-checked: 0x43dfb0, 0x5b5f48, 0x424d40, 0x446b2c, 0x52f154,
  0x487b2c, 0x5b4a24, funcs_4941F4 @0x631298).

### src/app/real_boot.cpp, real_audio_driver.cpp, real_forms_driver.cpp,
### real_scene_driver.cpp, real_text_driver.cpp, real_texture_driver.cpp
- GLUE (integration drivers; no `gilde.exe 0x…` 1:1 claims). The .BIN split-
  volume suffix table the boot comment cites was byte-diffed @0x44e8f0:
  ".BIN5\0.BIN4\0.BIN3\0.BIN2\0.BIN1\0.BIN0\0.BIN" — matches. The archive list
  in DefaultResourceArchives is documented as install-specific convenience (not
  a binary table) — correct per 0x527de0/0x450234.

## Documented gaps (not churned, per brief rule 4)
- GameApp::InitDisplayAndPaths (adapter) lists but does not dispatch
  Config_ApplyCameraAndScrollSettings / Input_SetWheelBase(80) — no ISubsystems
  hook exists; the strict 0x528560 sequence in engine_init_app_recon.cpp does
  dispatch configApplyCameraAndScrollSettings.
- RunFrameLoop return-value semantics (binary returns mouse-latch v10 /
  early `return 1` on byte_63CC14) are host-adapted; documented in frameloop.cpp.

## Tests
Targets built individually (never the whole tree) and run with
GUILD_GAME_DIR=$PWD/europe_guild_1400_original — all green:

| target | checks |
|---|---|
| app_config_write_e2e_test | 6 |
| app_config_write_test | 327 |
| app_frame_drivers_e2e_test | 22 |
| app_frame_drivers_test | 44 |
| app_frameloop_e2e_test | 16 |
| app_frameloop_itest | 17 |
| app_full_wired_playthrough_e2e_test | 14 |
| app_live_world_e2e_test | 5 |
| app_menu_loop_e2e_test | 15 |
| app_menu_loop_itest | 23 |
| app_menu_loop_test | 40 |
| app_movie_video_e2e_test | 4 |
| app_real_boot_e2e_test | 22 |
| app_real_boot_edge_test | 52 |
| app_real_run_e2e_test | 15 |
| app_real_wiring_test | 23 |
| app_render_frame_e2e_test | 1 |
| app_render_frame_itest | 10 |
| app_render_submit_test | 35 |
| app_save_drivers_e2e_test | 8 |
| app_save_drivers_itest | 18 |
| app_save_drivers_test | 44 |
| app_session_init_e2e_test | 19 |
| app_session_init_test | 76 |
| app_spine_e2e_test | 25 |
| app_spine_test | 61 |
| app_wiring2_e2e_test | 26 |
| app_wiring2_itest | 15 |
| app_wiring2_test | 32 |
| app_wiring3_e2e_test | 26 |
| app_wiring3_test | 24 |
| app_wiring4_e2e_test | 23 |
| app_wiring4_test | 26 |
| app_wiring_e2e_test | 13 |
| app_wiring_real_bridges_e2e_test | 32 |
| app_wiring_test | 42 |
| app_wiring_universe_e2e_test | 11 |
| engine_init_app_recon_test | 89 |
| render_thumbnail_capture_test | 23 |
| audio_tick_e2e_test | 35 |
| session_atmos_e2e_test | 13415 |

Total: 41 targets, 0 failures.

## Updated golden pins (old → new, with evidence)
- pause-loop mask 0u → (lastMask|0x100000)&~0x2000 (0x56e7e7/0x56e7f2/0x56e805)
- sine-table length 256 → 48 (0x30 @0x52879f)
- headless dayCycleMusic hook count 0 → 1 (music gate 0x4c0d56 has no 0x10000 term)
