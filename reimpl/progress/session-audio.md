# Session audio — real music + ambience + 3D SFX in the native city session

**Module:** `src/play/session_audio.{h,cpp}` (`play::SessionAudio`, `play::SessionMusicSink`)
**Tests:** `tests/unit/session_audio_test.cpp` (13 tests, 99 checks),
`tests/e2e/session_audio_e2e_test.cpp` (3 tests, 40 checks on the SDL build /
30 headless; real-asset guarded, honors `GUILD_GAME_DIR`)

## What it is

The audio integrator for the native city session: brings the reconstructed
audio cores up on the shim device (SDL on the host, `NullAudioDevice`
headless) over the real install assets, then drives the original per-frame
audio tick. The audible-output path is proven end to end on the real
`SdlAudioDevice` (dummy driver): the device mixer produces non-silent PCM from
the real decoded assets (`SessionAudioE2E.SdlDeviceMixesAudibleSessionAudio`,
mix peak ≈ 5700, real `cd1\ZurSommerzeit.mp3` streaming).

## Reconstruction map (addresses)

| Piece | Original | Reimplementation |
|---|---|---|
| Sound lib init (48 voices/2ch/44100) | `VIBE_Sound_LibInit` @0x445d90 | `audio::SoundSystem::init` (engine constants) |
| Volume sliders -> products | `VIBE_Audio_ApplyVolumeSettings` @0x56c148 | `audio::ApplyVolumeSettings` with the exact scales `flt_62522C`=0x3C010204 (~1/127), `flt_625230`=0.25; byte map per the INI writer: 1233550=master_vol, 51=sfx_vol, 52=msx_vol, 53=speech_vol, 54=msx_freq. Chop truncation preserved (master 127/sfx 114 ⇒ device master **113**, not 114) |
| SFX bank preload | `VIBE_Sound_PreloadFromIncludeFile` @0x52f154 + `VIBE_Sound_LoadSampleBank` @0x446b2c | `app::ParseSfxIncludeList` + `app::LoadOneSfxBank` over the real `include_sfx.ini` (71 banks / 551 entries indexed from the install) |
| Market ambience sample | `VIBE_Ambient_StartMarketLoop` @0x582858 plays `"Athmo_Marktplatz_Mono_4Bit"` (string @0x6263f0), `Sound3d_AttachToEntity(v,_,127,2200.0)` | the entry's PCM is faulted from the real `sfx/Lebewesen/athmos.sbf` (IMA ADPCM mono 22050, fmt 0x11, blockAlign 512) and decoded by `play::DecodeImaAdpcm`; `app::StartMarketLoop` then resolves it through the real `SampleBank` |
| Market loop start/stop scope | `VIBE_Scene_RunMainFrameLoop` @0x50f0c0 calls Start @0x50f19a once **before** its frame loop, Stop @0x50f60e at scene exit | `Frame` starts it on the first tick (gated on the resolved sample — the `*(dword_6477A4+97)` scene-record gate), `Shutdown` stops it; `VIBE_Sound3d_SetLooping(h,1)` is applied as a device-loop re-arm once the 3D update binds the voice |
| Per-frame audio tick | audio block of `VIBE_GameLogic_RunFrameLoop` @0x4c09a0 | `app::AudioTick` (already reconstructed) driven with the real listener pose/season/location |
| Enable gates | `VIBE_GameLogic_MainEntryAndShutdown` @0x534bbc: INI `[Sound] msx`→dword_63C8F8, `sfx`→dword_63C900, `weather`→dword_63C904 | `SessionAudioInit{msxOn,sfxOn,weatherOn}` (the reconstructed `config::SoundSettings` schema does not carry these keys) |
| Music updater entry gate | @0x5815c4: `if (!byte_63CC40 \|\| flt_6422A8 <= 0) return` | `Frame` masks `musicOn` with `musicFreqScale > 0` (msx_freq·0.25) |
| Outdoor season selection | `VIBE_Music_SelectOutdoorSeasonTrack` @0x581208 | `audio::SelectOutdoorSeasonTrack` (existing recon) + `SessionMusicSink` |
| Track streaming boundary | `VIBE_Audio_LoadTrack` @0x439ed0 / `StartTrack` @0x439f8c / `StopTrack` @0x43a2fc, `"msx\"` prefix @0x622b3e | `SessionMusicSink`: resolves `msx/cd1/*.mp3` case-insensitively, decodes via `play::DecodeMp3File` (the proven menu path), drives one device voice with the StartTrack order (vol 0 → start → vol 127) |

## 1:1 fixes made to `src/audio/music_world.cpp` (verified in IDA)

1. **Avoid-repeat reroll polarity** in `SelectOutdoorSeasonTrack` was inverted.
   Original tail: `while (strlen(byte_645E16) && !VIBE_Util_StrCmp(v11, byte_645E16))`
   with `VIBE_Util_StrCmp` @0x5d3f10 = plain `strcmp` (0 == equal) ⇒ reroll
   **while the pick equals the last track**. The recon rerolled while it
   *differed*, which infinite-loops after a season change (last track never in
   the new season's pool).
2. **Name clear on season stop**: @0x581594 does `byte_645F28[276*slot] = 0`
   (the entry NAME) — without it the next select resumes the old season's
   track. Added.
3. **Name clear on track end**: the original clears the name when
   `lengthMs-positionMs < 20000` (the played-to-end case, which is what the
   modelled `atStreamEnd` represents). Added with the gap note below.

## API

```cpp
play::SessionAudio sa;
sa.Init(dev, fs, soundSettings, {.gameDir = "<install>", .msxOn=1, .sfxOn=1, .weatherOn=1});
sa.SetMarketPosition(marketXYZ);                  // the dword_6477A4+97 entity pos
sa.Frame(listenerPos, listenerFwd, locationId, season, gameTick);  // per frame
sa.Shutdown();                                    // scene exit
// inspection: sa.status(), sa.sound(), sa.director(), sa.musicSink(), sa.marketLoopEntry()
```

Helpers exposed for tests/other callers: `play::DecodeImaAdpcm` (IMA/DVI
standard decode — part of the approved MSS→SDL substitution, rule 5),
`play::MusicTrackRelPath`, `play::kMarketAmbienceSample/Bank`.

A `StreamHeadroomDevice` adapter adds 10 stream-voice headroom on top of the
voice pool's 48 at `device->init` time (MSS kept its stream handles outside the
48 sb sample handles; the shim device pools all voices from one init count).

## Real assets consumed

* `include_sfx.ini` → 71 `sfx/**/*.sbf` banks (551 sample entries indexed)
* `sfx/Lebewesen/athmos.sbf` → `Athmo_Marktplatz_Mono_4Bit` IMA ADPCM decoded
  to 2 578 256 frames (`fact`-capped) of S16 @22050, mono→stereo expanded
* `msx/cd1/*.mp3` outdoor season tracks (e.g. `cd1\ZurSommerzeit.mp3`),
  case-insensitively resolved (`msx/CD1/...` on disk) and mpg123-decoded when
  `GUILD_HAVE_MP3` (the vulkan-sdl build)

## Tests

Unit (`session_audio_test`, NullAudioDevice + MemFileSystem, 99 checks):
`InitBringsUpRealCoresAndVolumes` (exact 0x56c148 products: master 113, freq
0.75), `InitRejectsNullDeviceOrFs`, `FrameRunsGatedTickAndSelectsSeasonTrack`,
`MusicGateMsxOff`, `MusicGateMsxFreqZero`, `SfxGateOffSkips3dBlocks`,
`SeasonChangeStopsThenReselects`, `TrackEndRotatesVariantViaStatusProbe`,
`MarketLoopStartsOnFirstFrameStopsOnShutdown`, `MarketLoopNeedsTheRealSample`,
`ImaAdpcmGoldenBlock` (hand-computed reference vector), 
`ImaAdpcmHonorsFactSampleLimit`, `ImaAdpcmRejectsBadArgs`,
`MusicTrackRelPathMapsMsxPrefix`.

E2E (`session_audio_e2e_test`, real install, guarded):
`InitLoadsRealBanksAndMarketAdpcm` (71 banks, exact decoded byte count,
non-silent PCM), `SessionFramesPlayMarketLoopAndSeasonTrack` (market loop
attached + 3D-audible vol 126, summer-pool track selected; asserts mp3
streaming under `GUILD_HAVE_MP3`), `SdlDeviceMixesAudibleSessionAudio`
(SDL build only: real `SdlAudioDevice`, dummy driver — `pullMix` PCM is
non-silent ⇒ audible output proof).

All neighbouring audio suites re-run green after the music_world fixes
(`audio_music_world_test` 128, `audio_music_world_e2e_test` 258,
`audio_tick_test` 22, `audio_tick_e2e_test` 17, `audio_test` 73,
`real_audio_test` 32, `real_audio_driver_test` 33, ...).

## Named gaps (rule 8 — absent, not faked)

* **.sbf PCM faulting** (`VIBE_Sound_LoadEntry` @0x446830) is performed only
  for the market-ambience bank; the include preload indexes every bank's entry
  names but leaves their PCM unfaulted (same as `app::LoadOneSfxBank`).
* **Stream-end detection on the SDL device**: the original polls the MSS
  stream cursor (`*(handle+260/261)`); `shim::IAudioDevice` has no per-voice
  status query. With an `audio::IAudioStatusDevice` device the real
  ended→pause→reselect rotation runs (unit-tested); on `SdlAudioDevice` the
  sink loops the selected season track at the device so music stays audible —
  the rotation cannot fire until the SDL backend grows a status query.
* **Interior location tracks** (`VIBE_Music_ResumeLocationTrack` @0x580de4
  full id→`Athmos\Athmo_*.mp3` table): only the OUTDOOR entry (id 9876) is
  seeded; non-zero `locationId` resolves to no entry.
* **Pause length / random interruption** of `UpdateOutdoorTrackPlayback`
  (@0x581594: `pauseLength=(1-flt_6422A8)*flt_626078`, the `RandomModulo(0x80)`
  interrupt-probability block, `dword_6476DC/D8/E4` timers) is not in the
  reconstructed state machine; `flt_6422A8` itself is computed
  (`status().musicFreqScale`) and its `<= 0` entry gate is applied.
* **Resume-distant-stop branch**: the keep-name case at track end
  (`lengthMs-positionMs >= 20000`) needs the stream ms cursor, which the
  modelled `PlaybackTick` does not carry.
* **Terrain-echo listener layer** (`VIBE_Sound3d_UpdateListener` @0x425208) —
  documented stub inside `app::AudioTick` (heightmap/floor-octree coupled).
* **Wiring into the session host**: `src/play/sdl_session.*` is owned by the
  session-host slice; `SessionAudio` is the drop-in (Init at session start,
  Frame per tick with the camera pose, Shutdown at exit).

Note: `src/app/wiring.cpp`'s `kMarketSampleName` (`"Athmos\\Markt"`) is a
placeholder — the real sample name is `"Athmo_Marktplatz_Mono_4Bit"`
(@0x6263f0); that file is outside this slice and was left untouched.
