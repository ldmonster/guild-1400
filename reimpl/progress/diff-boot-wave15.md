# Wave-15 TRUE 1:1 Binary Diff — Boot/Config Spine (W15-BOOT)

MCP LIVE. Every function below was `decompile`/`disasm`/`get_bytes`-diffed against
the reconstruction line-for-line. Segment: `src/app/{config_write,session_init,
real_boot}.{h,cpp}`, `gamelogic.h`, `wiring.*`. Bind-site play/ files NOT touched.

---

## VERIFIED-1:1 (matches the decompile)

### ConfigWriteGfxSettings @0x56af54  (config_write.cpp)
Diffed the full 45-key serializer. The exact `WritePrivateProfileStringA(section,
key, ...)` sequence and section assignment match the source 1:1, including the
INTERNAL [Gfx] byte order (texture_scale, details, lod_handling, shadow_detail,
floor_mipmapping, **gfx_set**, camera_limits, floor_lod, character_detail,
fog_plane, cur_res), the 12 brightness/contrast/gamma floats written as
`(int)(value * flt_6251F0)` with `flt_6251F0 == 100.0f`, the 5 [Sound] keys, and
the 17 [Game] keys with `stadt` emitted verbatim between `nachtwaechter` and
`historie`. AnimationState_Update (0x5d92ec, signed radix-10) wrapper confirmed.

### ReadGfxAndSoundSettings @0x56b834  (config/ini.cpp)
Diffed all 45 reads. Match 1:1: every default (character_detail=1, brightness_a=50,
contrast/gamma=100, nachtwaechter/show_cursor_txt/show_geb_info/mission/net_mission/
panel_mode/help_events/difficulty/hints/panel_help=1, stadt="Augsburg"), the
post-read `byte_1233568 = 0` invert_mouse override (0x56bd42), the read ORDER
(gfx_set/cur_res read AFTER the floats), and `flt_625200 == 0.01f`.
- Resolution table CONFIRMED via get_bytes @0x63D70C:
  `[800,600, 1024,768, 1152,864, 0,0, 800,600, 0,0]`. The decompile's split
  `dword_63D710[2*idx]`(width)/`dword_63D70C[2*idx]`(height) is exactly the
  interleaved `kResTable` with `height=tab[2*idx]`, `width=tab[2*idx+1]`. ✓

### InitOrLoadSession @0x533a54  (session_init.cpp) — orchestration order
Diffed the full 3456-byte body. The recovered SetupStep order + flag-gated
branches match the decompile cascade:
- entry: `v111/v112 = 131079 (0x20007)` frame mask ✓ (kLiveGameFrameMask).
- `if ((flags&1) && !a1) Office_InitTable` — `!a1` is the per-player-loop guard
  (a1 not modeled in the hook ctx; documented approximation, order intact).
- network: `(flags&4)` -> History_SetActiveFlag(0); host `(flags&0x10)` ->
  LoadLibrary(Server.dll)/GetProcAddress("Init_")/Init_()/Sleep(2500)/
  Net_OpenBroadcastSocket(0x3039). Flag constants CONFIRMED: kNewGame=1,
  kLoadSave=2, kNetwork=4, kHost=0x10, kLoadNetSave=0x40, kTutorial=0x80.
- DecodeSessionMode cascade (`&1`{`&4`net-new/cty} / `&0x40` / `&2`) ✓.
- turn loop bound `dword_63CC2C < 800` ✓; per-day clock `while(hour<23)
  GameTime_Advance(+30min)` then roll +24h / Set 06:00 ✓ (RunBoundedTurnLoop).
- 13-step .cty-failure teardown order (Game_ShutdownSubsystems, byte_63CC1C=0,
  loadingActive=1, Widget_ShutdownSystem, **Config_WriteGfxSettings**,
  GameState_FreeAllResources, Universe_SwitchActiveSlot, Table_ResetLightmaps,
  Render_ShutdownEngine, Input_DirectInputShutdown, TimeBase_StopTimer,
  Vfs_Shutdown, MemPool_ShutdownStack, Memory_ShutdownTracker, ErrorLog_Shutdown,
  movie-dll free, Window_DestroyAndUnregisterClass) confirmed against gamelogic.h.

### App spine preamble @0x527de0  (VIBE_App_InitSubsystemsAndMovieDll)
Decompiled. Confirms: ErrorLog_Init -> Memory_InitTracker(32678) ->
MemPool_StartupStack(0x80) -> (if dword_63C8F0) LoadLibrary("moveahead.dll") +
9 GetProcAddress (mov_Init_/Exit_/Play_/Stop_/SetVisible_/GetEvent_/Prepare_/
PrepareDD_/Dispose_) -> CreateDirectory("%sgamedata\Screenshots") ->
**Vfs_Init("\project\gfx\")** -> TimeBase_StartTimer(0xE,0). The moveahead.dll +
DDraw/DInput leaves are rule-4/6 boundaries; the surrounding sequence is 1:1.

---

## FIXED (reconstruction diverged from the binary — corrected + golden-pinned)

### 1. MoneyMultiplyByRate @0x58f19c — WRONG FORMULA  (session_init.{h,cpp})
The decompile/disasm is NOT `amount * rate / 100`:
```
return a1 * dword_649A88[dword_13CD6F2[189 * a2] >> 16];
```
The second arg is the active **city/currency index** (byte_6477A1), NOT a percent.
It indexes the runtime 189-dword-stride city record table `dword_13CD6F2`; that
record's +0 field high word (`>> 16`) selects a multiplier from the static curve
`dword_649A88[]` (get_bytes confirmed: `dword_649A88[0]==0x15`, a 80-byte table).
- The canonical 0x58f19c reconstruction ALREADY exists once at
  `world::AmtMoneyMultiplyByRate` (world/amt.h), modeling the runtime tables as a
  settable rate hook (identity default). The wave-13 `app::MoneyMultiplyByRate`
  was a divergent DUPLICATE with the wrong percent formula.
- FIX: `app::MoneyMultiplyByRate` now FORWARDS to `world::AmtMoneyMultiplyByRate`
  (one copy of the 0x58f19c logic — no ODR/duplicate divergence). The new-game
  start-gold call site now passes `byte_6477A1` (cold image 0), not a literal 100.
- GOLDEN: rewrote `AppSessionInit.MoneyMultiplyByRate` — identity for ANY index
  with no hook (was asserting the wrong 1000/50->500); plus a hook that
  reproduces `amount * dword_649A88[0] (==0x15)` and checks the forward routes
  there. The prior golden (which encoded `*rate/100`) was the wrong value — fixed
  to the binary per the brief.

### 2. GameInitWorldAndSounds @0x52f2ec — MISSING globals + clock over-zero
disasm-confirmed the exact re-init set (edx=1, ebx=2, esi=0, ah=0):
- ADDED `dword_631DB4 = 1` (0x52f303, "world active" — was missing) ->
  SessionState::worldActive.
- ADDED `dword_631284 = 2` (0x52f333, clock-proc state — was missing) ->
  SessionState::clockProcState.
- FIXED the calendar seed: `mov dword ptr ds:qword_13CE852, ebx` sets ONLY the
  low dword (the 14-byte GameTime's `day` field) to 2; the source also zeroed
  hour/minute/second, which the binary does NOT touch here. Now sets only
  `clk.day = 2`.
- FIXED the City_InitParameterTable arg: it is `__thiscall` and ecx == 1 (the
  INTEGER, via `mov ecx, edx` / edx=1); the callee stores it raw into the float
  global flt_641DA8 (`flt_641DA8 = v1`). So the seeded cap divisor is
  `bit_cast<float>(1u)` (denormal 1.401e-45), NOT `1.0f`. Source now memcpy's the
  raw 0x00000001 into the float arg. (g_capDivisor / flt_641DA8 itself is owned
  by world/city.cpp; only the call ARG, in this segment, was wrong.)
  Confirmed City_InitParameterTable @0x577a9c is the 28-good economy seed and its
  only consumer of the arg is flt_641DA8.

### 3. DefaultResourceArchives — provenance corrected  (real_boot.cpp)
NOT a binary constant. App_InitSubsystemsAndMovieDll (0x527de0) only calls
Vfs_Init (0x451f98); the VFS DISCOVERS archives by directory scan
(Vfs_ScanDirectory 0x450234) probing the split-volume extension suffixes
`.BIN/.BIN0../.BIN5` (string table @0x44e8f0..0x44e914, get_string confirmed,
used by Vfs_ScanDirectory/Vfs_OpenFile/Vfs_AddFileSorted). The named list
(animations.BIN, forms.BIN, ...) is an install-specific CONVENIENCE for the host/
test mount layer, not a 1:1 table — comment updated to say so. No binary list to
reconstruct, so no list change.

---

## STILL DEFERRED (confirmed rule-4/6/7 boundaries — surrounding logic is 1:1)

- InitOrLoadSession leaves: Loading_*/Form_SelectWindow/Window_PumpMessages,
  Net_*, Save_*, Scene_Sync*/Scene_RunMainFrameLoop, Groundplan_*/MapView/
  Render_SetupViewTransform/Light/Sky, Command_* lockstep, MeisterAi_*/Amt_*,
  History_*/Tutorial_*/Dialog_*/EventPanel_*, RunFrameLoop. Diffed: their CALL
  ORDER and flag gates inside 0x533a54 match the decompile; the bodies are owned
  by other clusters and routed through the SetupStep hook (recorded, observable).
- App_InitSubsystemsAndMovieDll: moveahead.dll (movie, rule-6 pl_mpeg shim),
  DDraw/DInput (rule-3/4) — leaf OS/vendor boundaries; the sequence around them
  is 1:1.

---

## BUILD STATUS
Headless build/ (GUILD_BACKEND=OFF), green. Segment + downstream suites:
- app_session_init_test 76/0, app_config_write_test 327/0, app_spine_test 61/0,
  app_real_boot_edge_test 52/0, settings_io_test 76/0, config_test 654/0.
- downstream of the Money fix: wire_npcaction3_test 42/0, cutscene_misc5_e2e_test
  15/0, npcaction6_test 63/0, npcaction11_test 96/0.
