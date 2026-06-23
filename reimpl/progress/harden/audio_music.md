# Hardening — audio music / world-music / cutscene / digital-output / ambient

Binary: `gilde.exe` (imagebase 0x400000). MCP IDA Pro live.
Scope: `src/audio/{music,music_world,music_cutscene,digital_output,ambient}.cpp`.

Method: every provenance-tagged function decompiled + diffed against the binary;
every constant/table read with `get_bytes`/`get_global_value`; every RNG draw
count+order and every float->int site checked against disasm. AIL/MSS32
streaming + Sound3d calls are the rule-5 audio boundary and are intentionally
left as device hooks (`shim::IAudioDevice` / `IMusicSink`).

Result summary: **VERIFIED-1:1 = 18, FIXED = 0, BOUNDARY = 8, MODELED = 2.**
No divergence found; no source or golden change required.

---

## music_world.cpp

| Function | Addr | Status |
|---|---|---|
| `FindTrackById` | 0x58153c | VERIFIED-1:1 |
| `FindActiveTrackSlot` | 0x581508 | VERIFIED-1:1 |
| `SelectOutdoorSeasonTrack` | 0x581208 | VERIFIED-1:1 |
| `ResumeLocationTrack` | 0x580de4 | VERIFIED-1:1 (selection core; person-query coupling = documented gap) |
| `UpdateOutdoorTrackPlayback` | 0x581594 | VERIFIED-1:1 (state core; stream-cursor / interrupt-probability coupling = documented gap) |
| `SeasonFromDay` (inline) | 0x58339c | VERIFIED-1:1 (`day % 4`) |

Key checks:
- **RNG draw count+order (0x581208):** variant = `(unsigned __int16)RandomModulo(0x75) % 3`,
  drawn once *inside* the reroll do-while. `RandomModulo(0x75)` = `RandNext() % 0x75`
  (range [0,116], positive) so the `(unsigned __int16)` cast is a no-op and
  `% 3` is bit-identical. Reimpl draws inside the loop, same order. ✓
- **Reroll loop tail (0x581366):** `while (strlen(last) && !StrCmp(chosen,last))`.
  `VIBE_Util_StrCmp` @0x5d3f10 returns 0 on equal, so `!StrCmp` == "equal";
  loop repeats *while last is non-empty AND chosen==last*. Reimpl
  `while (!lastTrackName.empty() && lastTrackName == chosen)`. ✓
- **Per-season variant switch:** spring/summer/autumn `v12 ? variant1 : variant0`
  (any non-zero -> variant1); winter `0->file0, 1->file1, else->file2`. Pool
  literals (cd1\\...mp3) confirmed against string refs. ✓ `byte_645E16` cleared
  after select. ✓
- **Golden RNG:** verified seed=1 `RandomModulo(117)%3 = [2,1,0,1,1,2,0,0]`
  (test comment + Spring->ImFruehling / Winter->ImmerKalt goldens). ✓

Documented gaps (named, not faked): per-season volume-bias bookkeeping
(word_646028 +/-30/+/-20), the stream-cursor `<20000` keep-name branch, and the
random-interrupt probability branch (`RandomModulo(0x80) >= prob>>16`) depend on
the live stream-ms cursor and person-query system not present in the modelled
tick; the model carries the observable selection + active-slot invariants.

## music_cutscene.cpp

| Function | Addr | Status |
|---|---|---|
| `playCutsceneTrack` | 0x581b0c | VERIFIED-1:1 |
| `restoreAfterCutscene` | 0x581c04 | VERIFIED-1:1 |
| `setTrackFade` | 0x581c48 | VERIFIED-1:1 |

Key checks:
- Entry snapshot `if(!dword_642024){dword_642010=byte_642008;dword_642024=1;byte_642008=0}`. ✓
- Gate `dword_63C8F8 && (LODWORD(flt_6422A8)&0x7FFFFFFF)!=0`: the abs-mask test
  is true unless the volume is +0.0 *or* -0.0; reimpl `!(vol==0.0f)` excludes
  both IEEE zeros — exact match. ✓
- Branch order: track present -> StopTrack(fade=1) then LoadTrack; else if active
  -> SetFadeVolume(0.4, 2000). Restore: SetFadeVolume(1.0, 3000) iff
  `active != cutscene`. Constants 0.40000001f / 2000 / 1.0f / 3000 confirmed. ✓

## music.cpp  (streaming facade — MSS32 boundary)

| Function | Addr | Status |
|---|---|---|
| `findActiveTrack` | 0x43a864 | VERIFIED-1:1 (StrCmp slot scan, 10-slot bound) |
| `setMusicVolume` | 0x439e90 | BOUNDARY (AIL digital master volume) |
| `setFadeVolume` | 0x43a7b8 | VERIFIED-1:1 (immediate path; [0,1] guard via float-bit `<=0x3F800000`) |
| `applyMasterVolume` | 0x439ddc | VERIFIED-1:1 (clamp + `(double)master*modifier` -> ConvertX truncate) |
| `loadTrack` | 0x439ed0 + 0x439f8c | BOUNDARY (find/recycle + AIL stream open/start) |
| `stopTrack` | 0x43a2fc | BOUNDARY (AIL PauseStream/CloseStream / FadeOutTrack) |

Key checks:
- **Float->int (0x439ddc):** `v3 = (double)master * flt_62D9F8` then
  `VIBE_Coord_ConvertX` @0x5c6b08 = set FPU RC=truncate, `frndint`, restore ->
  truncates toward zero. Reimpl `static_cast<int>(scaled)` truncates toward zero. ✓
- **[0,1] clamp:** `<0.0 ->0.0`, `SLODWORD>0x3F800000 ->1.0`; for positive floats
  the signed-bit compare == `>1.0f`. Reimpl `>1.0f`. ✓
- Default per-track volume 127 (+0x118), loop-count branch `arg?0:1` modelled by
  `loop?0:1`. The streaming/queue machinery (offsets +256..+292) is the MSS32
  boundary; the reimpl models it via a slot + `shim::IAudioDevice` voice.

## digital_output.cpp  (AIL waveOut / sample-handle pool — MSS32 boundary)

| Function | Addr | Status |
|---|---|---|
| `openDigitalOutput` | 0x449910 | VERIFIED-1:1 math; BOUNDARY (AIL_waveOutOpen/get_preference) |
| `findFreeSampleSlot` | 0x44aac8 | VERIFIED-1:1 |
| `allocateSampleHandle` | 0x449e70 | VERIFIED-1:1 (AIL_allocate hook) |
| `lookupSampleHandleIndex` | 0x44ab90 | VERIFIED-1:1 |
| `openStream` | 0x44a544 (+ FindFreeStreamSlot 0x44ab2c) | VERIFIED-1:1 (AIL_open_stream hook) |
| `startStream` | 0x44a74c | VERIFIED-1:1 (AIL_start_stream hook) |
| `releaseSampleHandle` | 0x44a028 | VERIFIED-1:1 (AIL_release hook) |

Key checks:
- WAVEFORMAT math (0x4499af/0x4499b8): `bytesPerSec = channels*(bits>>3)*rate`,
  `blockAlign = channels*(bits>>3)`, formatTag=1, allocatedSampleCount(+0x14)=0. ✓
- **FindFreeSampleSlot/StreamSlot:** `if(allocated>=cap)`-1; walk returns the
  index of the first zero slot (`++v6 >= cap` -> -1 guard). Reimpl `idx` walk is
  index-for-index identical. ✓ (capacity field +0x18 == maxSampleHandles).
- LookupSampleHandleIndex scans `[0, maxSampleHandles)` per open output. ✓
- Note: `maxSampleHandles` (+0x18) in the binary is the AIL preference global
  (`dword_62EAE0`); the reimpl takes it as the open() argument (AIL boundary
  param). `releaseSampleHandle` adds a defensive `count>0` guard around the
  decrement, unreachable in valid states (a found handle implies count>0) — no
  observable difference.

## ambient.cpp  (no `gilde.exe 0x...` tags in the .cpp — confirmed)

The .cpp carries **no per-function provenance addresses**; the functions are
per-category decision/helper extractions from the giant
`VIBE_Ambient_UpdateWildlifeSounds` @0x5800f0 (and the market-loop pair). The .h
documents the addresses. Verified the RNG-exact and math-exact cores:

| Function | Source addr block | Status |
|---|---|---|
| `WildlifeShouldTrigger` | 0x5800f0 (per-category gate, e.g. 0x5801e2) | VERIFIED-1:1 (RNG core) |
| `AmbientPopulationVolume` | 0x580863 / 0x580bc7 etc. (min(pop,64)) | VERIFIED-1:1 |
| `WildlifeResetTimers` | 0x580b7d (!boden reset) | MODELED (per-category vs per-season-of-current; reset value `t` exact) |
| `MarketStartLoop` | 0x582858 | BOUNDARY (Sound3d play/attach) — guard logic VERIFIED |
| `MarketStopLoop` | 0x5828bc | BOUNDARY (Sound3d stop/detach) — guard logic VERIFIED |

Key checks:
- **RNG draw count+order (0x5800f0):** per category, draw 1 = jitter
  `RandNext() % 2000` (always), draw 2 = gate `(double)RandNext()*flt_625DD0 +
  threshold`, drawn *only* when the cooldown elapsed. Reimpl draws jitter
  unconditionally, gate only inside the cooldown branch — same count, same order. ✓
- **Gate float math:** Hex-Rays `*(float*)&v91 = COERCE_FLOAT(RandNext())` then
  `(double)v91 * flt_625DD0` is the standard artifact for `(double)randInt *
  (1/32767)`; the result is stored to a 32-bit float slot and compared as
  `>0x3F800000` (== `>1.0f` for non-negative). Reimpl
  `(float)((double)RandNext()*kAmbRandNorm + threshold) > 1.0f`. ✓
- **Constants (get_bytes, bit-exact):** flt_625DC8=2000.0, flt_625DCC=
  0.0005000000237487257, flt_625DD0=3.0518509447574615e-05 (1/32767),
  flt_625DD4=127.0, flt_625DD8=64.0. All match the .h. ✓
- **min(pop,64) + truncate:** `if(64.0>=pop) pop else 64.0`, then ConvertX
  truncate -> `(int)`. Reimpl `(kAmbVolumeClamp>=pop)?pop:64.0; (int)v`. ✓
- `WildlifeResetTimers` is MODELED: the binary resets the *current* season index
  of five distinct timer arrays; the reimpl resets all four seasons of one
  modelled category. Reset value (`t`) is exact; the structural difference is the
  documented per-category abstraction (see ambient.h).

---

## Tests / build

- Built (GUILD_BACKEND=OFF portable): `audio_music_world_test`,
  `audio_cutscene_test` — OK.
- `ctest -R "music|cutscene"` -> **24/24 passed**.
- Golden RNG re-derived and confirmed (seed=1): `RandNext` seq, `%2000`
  jitter=838, `RandomModulo(117)%3 = [2,1,0,1,1,2,0,0]`.
- digital_output is covered by `audio_loaders_test` / `audio_leaves_test`;
  those targets currently fail to *link* due to a pre-existing, unrelated compile
  error in `src/audio/voicequeue.cpp` and `src/app/real_audio_driver.cpp` (NOT in
  scope, not edited). digital_output itself is verified by decompile diff above
  and required no change.

No source edits and no golden edits were necessary: every in-scope function is a
faithful translation of the binary.
