# Wave-13 1:1 Fidelity Audit — Boot/Config Spine (W13-BOOT)

MCP DOWN — this is the MCP-free part of the 1:1 comparison: cross-check each
reconstructed function on the boot/config spine against in-tree evidence
(provenance comments + progress docs) and PIN its recovered 1:1 values with
golden tests. No live binary diff.

Segment: `src/app/wiring.{cpp,h}`, `real_boot.{h,cpp}`, `session_init.{h,cpp}`,
`config_write.{h,cpp}`, `gamelogic.h` + their tests. (Bind-site play/ files NOT
touched, per ownership.)

---

## 1. INVENTORY (reconstructed functions + gilde.exe provenance)

### config_write.{h,cpp}
| Function | Address | Provenance |
|---|---|---|
| `ConfigWriteGfxSettings` | `0x56af54` | VIBE_Config_WriteGfxSettings — 45-key settings serializer (shutdown step 3) |
| `AnimationState_Update` (itoa) | `0x5d92ec` | VIBE_AnimationState_Update — int->string, radix-10 signed; reuses `crt::StringUIntToString` (`0x609640`, the `0x5d92a0` ultoa core) |

### session_init.{h,cpp}
| Function | Address | Provenance |
|---|---|---|
| `InitOrLoadSession` | `0x533a54` | VIBE_GameLogic_InitOrLoadSession — new/load/network session bootstrap orchestration |
| `GameInitWorldAndSounds_Body` | `0x52f2ec` | VIBE_Game_InitWorldAndSounds (world-reset core) |
| `DecodeSessionMode` | `0x533a72..` | the entry branch cascade (new>loadnet>load) |
| `MoneyMultiplyByRate` | `0x58f19c` | VIBE_Money_MultiplyByRate (amount*rate/100) |
| `NewGameStartGoldBase` | `0x533c5e` | new-game purse base (cheat 75000 / 1250-250*diff) |
| `RunBoundedTurnLoop` | `0x534XX` | the turn-loop day-advance clock cascade (real `sim::GameTimeAdvance`) |
| `WorldClock` | `0x13CE852` | qword_13CE852 calendar mirror (day 2 / 06:00 seed) |
| constants `kLiveGameFrameMask` 0x20007 (`0x533a64`), `kRoundSyncToken` 0x24087 (`0x533a5e`), `kDayStartHour` 6 | | recovered immediates |
| `SessionState` struct fields | various globals | cold-IDB static-init image (word_63C740, dword_63C744, dword_63C79C=1, dword_63C7D0=1, dword_631294=-1, …) |

### wiring.{cpp,h} — INTEGRATION GLUE (not a 1:1 translation)
`RealSubsystems` is a concrete `ISubsystems` adapter; its methods forward to
already-reconstructed modules. The wireable methods carry the address of the
ORIGINAL entry point they adapt to (e.g. `universeCreateDefaultCameras` ->
`0x5b5f48`, `tdConfigWriteGfxSettings` -> `0x56af54`, `soundWaveInitSineTables`
-> `0x424d40`, `cameraCombatScroll` -> `0x487b2c`, `dayCycleAndOutdoorMusic` ->
the audio block of `0x4c09a0`). The WIRED/STUBBED table (wiring.h, 55/64 hooks
REAL) documents each. No subsystem LOGIC lives here, so its 1:1 fidelity is the
fidelity of the modules it forwards to (owned by other segments) + the spine
ORDER (owned by gamelogic.h / app_init.cpp).

### real_boot.{h,cpp} — INTEGRATION GLUE
| Function | Provenance |
|---|---|
| `MountRealGameAssets` | front half of `0x527de0`/`0x527fa4` file-layer (INI parse + VFS bind + archive mounts) — sequences reconstructed code, no new logic |
| `RealCityPath` | engine "Resources/gamedata/Cities/<UPPER>.cty" path build |
| `DefaultResourceArchives` | the shipped Resources/*.BIN set |
| `RealGameAssets::archiveForMember` | member->archive resolve |

### gamelogic.h — the spine contract (implemented in app_init.cpp, NOT in segment)
- Frame-loop feature mask (`mask::*`) — bit meanings recovered from `0x4c09a0`.
- Session flags (`session::*`) — `0x533a54` / word_63C740.
- 13-step shutdown teardown order (each step carries its address; app_init.cpp
  lines 150–162 invoke them in that exact order — VERIFIED matching the header).

---

## 2. INTERNAL CONSISTENCY CHECK (drift)

No drift found. Cross-checked:

- **config_write key set ⇆ reader key set (inverse property).** Writer emits 11
  [Gfx] byte + 12 [Gfx] float + 5 [Sound] + 17 [Game] = 45 keys; the reader
  `ReadGfxAndSoundSettings` (`0x56b834`, src/config/ini.cpp) reads the identical
  key set. The two iterate the [Gfx] byte block in a different INTERNAL order
  (writer: …gfx_set, camera_limits…; reader: …camera_limits…gfx_set…) but reads
  are order-independent, so the round-trip is exact (pinned by
  `AppConfigWrite.RoundTripThroughReadGfxAndSound` + `SettingsIo.SaveLoadRoundTrips…`).
- **Float scale constants.** Writer `flt_6251F0 == 100.0f`; reader
  `flt_625200 == 0.01f`. Exact inverses (50/100/0.5/0.75/1.25 truncate cleanly).
- **Shutdown 13-step order:** gamelogic.h comments ⇆ app_init.cpp invocation
  order ⇆ `AppSpine.ShutdownThirteenStepOrder` — all three agree.
- **Cold defaults:** GfxSettings/SoundSettings/GameSettings struct defaults
  (ini.h) == the reader's documented `0x56b834` defaults (character_detail=1,
  brightness_a=0.5, contrast/gamma=1.0, nachtwaechter/mission/…=1, stadt="Augsburg").
- **SessionState static-init image** == the documented cold-IDB values
  (pinned by `AppSessionInit.StateStaticInitValues`).

---

## 3. GOLDEN PINS ADDED THIS WAVE (test-only; no source edits)

All values traced to the source's own documented recovery / progress docs —
nothing invented. Existing goldens left byte-identical.

1. `tests/unit/app_config_write_test.cpp`
   - **`AppConfigWrite.EmitsExactKeyOrder`** — pins the EXACT ordered 45-tuple
     `(section,key)` sequence of `ConfigWriteGfxSettings` (was only count + first
     key + per-section counts before; the precise order was the load-bearing
     1:1 value the brief calls out). Source: config_write.cpp lines 60..111.
   - **`AppConfigWrite.ColdDefaultEmittedValues`** — pins the WRITE-side emitted
     value strings for the all-default config (float truncation defaults
     brightness_a 0.5->"50", contrast/gamma 1.0->"100"; int defaults
     character_detail/mission/net_mission/…/panel_help == "1"; invert_mouse "0";
     stadt "Augsburg"). Source: ini.h struct defaults (the 0x56b834 read defaults).

2. `tests/unit/app_session_init_test.cpp`
   - **`AppSessionInit.RecoveredBootstrapConstants`** — pins
     `kLiveGameFrameMask == 0x20007 (131079)`, `kRoundSyncToken == 0x24087
     (147591)`, `kDayStartHour == 6`, and the mask bit-decomposition
     (`kInputCommandPoll|0x2|kWidgetMouse|kNetworkCommand`). These bare
     constants were not asserted against `app::*` anywhere.

Build stays green; both edited targets and the full segment suite pass.

---

## 4. CONFIDENCE MAP

### GOLDEN-PINNED (high 1:1 confidence — constants + control flow tested)
- `ConfigWriteGfxSettings` (`0x56af54`) — order, count, sections, float scale,
  cold defaults, stadt verbatim, NUL-termination, extreme values, full
  read→write→read round-trip incl. real Gilde.INI merge.
- `AnimationState_Update` (`0x5d92ec`) — signed radix-10, unsigned radices,
  INT_MIN negate-in-place, radix-2 width bound.
- `InitOrLoadSession` (`0x533a54`) — mode decode (all 4 modes + invalid + new-
  beats-load), NEW-single step order, tutorial/load/network branches, RNG seed,
  start-gold formula, world-reset latches, real Meister-sync wire (rule 13).
- `DecodeSessionMode`, `MoneyMultiplyByRate` (`0x58f19c`),
  `NewGameStartGoldBase` (`0x533c5e`), `GameInitWorldAndSounds_Body` (`0x52f2ec`),
  `WorldClock` seed, `SessionState` static image — all pinned.
- session-bootstrap constants (0x20007 / 0x24087 / kDayStartHour) — pinned now.
- 13-step shutdown order + frame-loop feature-mask dispatch (gamelogic.h /
  app_init.cpp) — pinned by `app_spine_test`.
- `RealCityPath`, `DefaultResourceArchives`, `MountRealGameAssets`,
  `archiveForMember` — pinned by `app_real_boot_edge_test` / `app_real_wiring_test`.

### UNDER-VERIFIED → upgraded
- The exact 45-key serialization ORDER and the cold-default emitted values were
  UNDER-VERIFIED (count/first-key only); the session constants were
  UNDER-VERIFIED (used internally, never asserted). Both now GOLDEN-PINNED above.

### NEEDS-LIVE-MCP (1:1 can't be confirmed from in-tree evidence)
The truth here is the decompile; when MCP returns, binary-diff these:
- `wiring.cpp` STUB hooks — genuine OS/DLL/DDraw/DInput leaves with no portable
  reconstruction (loadMovieDll, renderEnumDisplayModes/ApplyGfxSettings/
  SetAssetPaths, audioStartupMilesDriver, moviePlayIntroSequence, movieDllExit,
  `tdTableResetLightmaps` `0x42e19c`, `tdInputDirectInputShutdown` `0x40cd40`).
  Confirm these are leaves with no reconstructable portable behaviour.
- `session_init.cpp` DEFERRED leaves (listed in the .cpp header): the byte-exact
  body of the original `0x533a54` between the recovered step markers
  (Loading_*/Scene_Sync*/Net_*/Command_QueueRequest*/MeisterAi/Amt/History/
  Tutorial). The ORDER is recovered; the per-step internals belong to other
  clusters. Diff `0x533a54` to confirm step ordering + the branch gates byte-for-
  byte (esp. the post-load LABEL_17 cascade and the turn-loop bound `< 800`).
- `GameInitWorldAndSounds_Body` (`0x52f2ec`) — confirm the global re-init set
  (dword_63CC2C=1, qword_13CE852=2, dword_63CC30/34=0, byte_63CC41=0) and the
  `City_InitParameterTable(1.0f)` arg against the decompile.
- `real_boot.cpp` `DefaultResourceArchives` — the exact Resources/*.BIN file
  list + casing is from a directory listing, not the binary; confirm against the
  shipped install / any embedded VFS enumerate path.

---

## 5. BUILD STATUS
Headless build (`build/`, GUILD_BACKEND=OFF). Segment suites all pass after the
additions:
`app_config_write_test` 327/0, `app_session_init_test` 73/0, `app_spine_test`
61/0, `settings_io_test` 76/0, `app_real_boot_edge_test` 52/0,
`app_real_wiring_test` 3/0, `config_test` 654/0.
