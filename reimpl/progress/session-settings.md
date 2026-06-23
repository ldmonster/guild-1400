# Session settings — native Options screens bound to the real Gilde.INI

Status: **done** (load → edit → persist → apply data flow live).

The native Game/Gfx/Sfx options screens (`src/play/sdl_options_screen.*`,
reachable from the native main menu's Game-/Gfx-/Sfx-Options buttons) now
present the SAME settings the originals do, mutate the reconstructed
`config::*Settings` structs 1:1, and persist them into the real `Gilde.INI`
through the real serializer.

## Original functions (reference of record)

| Address  | Function | What was taken from it |
|----------|----------|------------------------|
| 0x56c21c | `VIBE_Menu_RunOptionsGfx` (form `menu\options_gfx`, title 0x1867) | child build order 0..8, every `SetValueOrText(min,max,seed)` range, res-cap logic (`byte_62D59A/B` → range 1/2), in-game hide of the res row (`byte_63CC40` → `SetVisibleRecursive(0)`), gamma inversion (seed `100-fog_plane` on a [50,100] slider; save `fog_plane = 50-(live-50)`), OK path order: save-back → `WriteGfxSettings` → `Render_ApplyGfxSettings` → (res changed) `byte_63D724`=new → `WriteGfxSettings` AGAIN → `ReadGfxAndSoundSettings` |
| 0x56c808 | `VIBE_Menu_RunOptionsSfx` (form `menu\options_sfx`, title 0x1868) | children 0..4: master/sfx/msx/speech volume sliders (0..127), `msx_freq` dropdown (0..4, 5 lines); OK: save-back → `WriteGfxSettings` → `Audio_ApplyVolumeSettings` (0x56c148) |
| 0x56cc44 | `VIBE_Menu_RunOptionsGame` (form `menu\options_game`, title 0x1866) | children 0,1,2,3,4,5,6,9,10,11,12; ranges speed 0..160, mouse 0..500, scroll/camera 0..100, panel_mode 0..4; `invert_mouse` (child 4) built then FORCE-HIDDEN (0x56cf04) and force-saved 0 (0x56d1d2); OK: save-back → `WriteGfxSettings` → `Config_ApplyCameraAndScrollSettings` (0x56c0cc) |
| 0x56af54 | `VIBE_Config_WriteGfxSettings` | the persistence serializer (already reconstructed in `src/app/config_write.*`): exact section/key order, signed itoa, ×100 float truncate; one `WritePrivateProfileStringA(section,key,value,Gilde.INI)` per key |
| 0x56b834 | `VIBE_Config_ReadGfxAndSoundSettings` | the seed reader (already reconstructed in `src/config/`) |

## Bound settings (exact INI keys, build order)

* **Gfx**: `cur_res` (0..caps, hidden in-game), `details` 0..2, `texture_scale`
  0..2, `floor_lod` 0..2, `floor_mipmapping` 0..1, `lod_handling` 0..1,
  `shadow_detail` 0..2, `fog_plane` (gamma slider [50,100], stored inverted),
  `camera_limits` 0..2. (`character_detail`/`gfx_set`/brightness/contrast/gamma
  are not menu-exposed — exactly like the original — but persist through the
  serializer with their loaded values.)
* **Sound**: `master_vol`, `sfx_vol`, `msx_vol`, `speech_vol` (0..127),
  `msx_freq` (0..4).
* **Game**: `speed` 0..160, `mouse_speed` 0..500, `scroll_speed` 0..100,
  `camera_speed` 0..100, `invert_mouse` (hidden, forced 0), `show_cursor_txt`,
  `show_geb_info`, `panel_mode` 0..4, `help_events`, `hints`, `panel_help`.

## New module: `src/play/settings_io.{h,cpp}`

* `play::SettingsBundle` — {GfxSettings, SoundSettings, GameSettings}.
* `play::LoadSettings(IFileSystem&, iniName, out)` — reads the INI through the
  shim filesystem and parses with the real reader (0x56b834). Missing file →
  reader defaults (Win32 GetPrivateProfile* behavior), returns false.
* `play::SaveSettings(IFileSystem&, iniName, s)` — runs the REAL
  `app::ConfigWriteGfxSettings` (0x56af54) with an in-memory
  WritePrivateProfileStringA-merge sink, then writes the merged text back.
* `play::IniWriteProfileString(text, section, key, value)` — the merge core,
  exposed for golden tests.

### WritePrivateProfileStringA merge semantics implemented (1:1 observable)

* section + key match case-insensitively; `;` lines are comments, not keys;
* existing key → ONLY that line is rewritten, the file's original key spelling
  is preserved (Win32/Wine keep the stored name, replace the value); every
  other line — comments, blank lines, foreign keys (`fog`, `screen_x`, `msx`,
  `weather`, `ambient`), foreign sections (`[General]`, `[Network]`,
  `[Compat]`) — survives byte-for-byte;
* missing key → inserted at the END of the section (after its last non-blank
  line, before the next `[section]` header);
* missing section → `[section]` + `key=value` appended at end of file;
* CRLF written for new/empty files (Win32 writes CRLF); an LF-only file keeps
  LF for its inserted/updated lines (round-trip preservation).

## Screen integration (`src/play/sdl_options_screen.*`)

* `OptionsConfig` grew the live INI binding: `bindIni` (default **true**),
  `iniDir` (default = `gameDir`), `iniName` (default `"Gilde.INI"`), plus the
  Gfx caps (`resCap1024`/`resCap1280`/`inGame` ↔ `byte_62D59A/B`/`byte_63CC40`).
  Because the binding defaults on, the existing native menu path
  (`native_main_menu.cpp` → `RunOptionsScreen` with `gameDir`) now loads its
  seed from `<gameDir>/Gilde.INI` and persists on Back/OK with **zero caller
  changes** — the same observable behavior as the original (boot reader seeds
  the globals; OK writes the INI).
* `OptionRow` gained `hidden` (built-but-invisible rows keep their layout slot,
  are never drawn or hit — `invert_mouse`, in-game `cur_res`).
* OK path reproduces each original's order: save-back → persist → apply sink;
  Gfx reproduces the separate resolution step incl. the SECOND
  `WriteGfxSettings` and the `ResolutionForIndex` re-derivation
  (`OptionsResult::resChanged`).
* `OptionsResult` gained `persisted` + `resChanged`.
* `play::OptionsApplyHooks()` — process-wide apply sinks
  (`applyVolumeSettings` ≙ 0x56c148, `applyGfxSettings` ≙ 0x56be58,
  `applyCameraScroll` ≙ 0x56c0cc). Default no-op; the changed
  `SoundSettings`/`GfxSettings`/`GameSettings` reach the audio/render/camera
  clusters at the API level. **Wave-2**: app wiring installs
  `applyVolumeSettings` to re-apply volumes into the running SoundSystem.

## Tests

* `tests/unit/settings_io_test.cpp` — **12 tests / 103 checks**: 7 merge-core
  golden tests (replace-in-place, case-insensitivity + key-spelling
  preservation, end-of-section insert, end-of-file section append, CRLF/LF
  conventions, comment immunity), reader/writer round-trips over
  MemFileSystem, and the GUARDED real-`Gilde.INI` round-trip (loads the real
  file, mutates `master_vol` via `SaveSettings` into a copy, reloads, asserts
  the mutation + 13 foreign lines + 8 untouched writer-owned keys; honors
  `GUILD_GAME_DIR`, never writes the real file).
* `tests/unit/sdl_options_screen_test.cpp` — **13 tests / 95 checks**: row
  tables vs. the original build order/ranges (incl. `lod_handling` max 1,
  `camera_limits` max 2, gamma [50,100], res caps/visibility), scripted clicks
  mutating the bound settings (sfx volume step, game toggle, res cycle with
  derived size, gamma inversion), hidden `invert_mouse` forced 0, ESC discard,
  apply-sink delivery, and the INI binding (seed loaded from the bound file,
  persist on apply, no write on ESC).
* `tests/integration/sdl_options_screen_itest.cpp` — 4 tests / 22 checks
  (unchanged, still green).
* `tests/e2e/sdl_options_screen_e2e_test.cpp` — 2 tests / 18 checks: the Sfx
  page over the REAL menu background seeded from the REAL `Gilde.INI`,
  persisting into a sandbox copy, reload through the real reader, foreign
  sections preserved, real file asserted untouched. GUARDED on the game dir.

All four binaries green; full-tree build clean; the real
`europe_guild_1400_original/Gilde.INI` verified byte-identical after every run.

## Named gaps (deliberate, with reasons)

* **Live volume preview while dragging** (0x56cae0 block in RunOptionsSfx:
  `dword_672220` drag → `Audio_SetMasterVolume`/`ApplyMasterVolume` ×
  `flt_62526C` scaling + `Audio_StartVoiceSample` test sample): needs the live
  audio cluster wired into the screen loop — wave-2 with the
  `applyVolumeSettings` hook consumer.
* **In-game panel_mode groundplan rebuild** (0x56d21b: `byte_63CC40` →
  `Groundplan_DestroyWindow/CreateWindow` + `Transition_FadeOutToBlack`): the
  options screens are only reachable from the main menu in the current native
  tree (`inGame` always false); the rebuild belongs to the groundplan module.
* **Resolution dropdown option text / rich-string labels** (`dword_8C9880..`
  runtime-loaded strings, `Text_AppendWideLines` line sets): the native panel
  draws English labels + numeric values; the real localized strings come with
  the forms/text cluster.
* **`byte_62D59A/B` device-cap detection**: the native screen defaults both
  caps on (modern displays); the original derives them from the DirectDraw
  mode enumeration. Wire from the Vulkan device's mode list when the display
  cluster lands.
