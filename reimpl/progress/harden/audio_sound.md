# Audio hardening — sound3d / sound / soundwave / audio_recon_dispatch

1:1 binary diff (IDA Pro MCP, module `gilde.exe`, imagebase 0x400000) of every
provenance-carrying function in:
- `src/audio/sound3d.cpp` (+ `.h`)
- `src/audio/sound.cpp` (+ `.h`)
- `src/audio/soundwave.cpp` (+ `.h`)
- `src/audio/audio_recon_dispatch.cpp`

Verified divergence classes: control flow / branch conditions, switch arms,
constants & tables (`get_bytes`/`get_global_value`), float→int truncation
(`VIBE_Coord_ConvertX` @0x5c6b08), x87 80-bit accumulation (modeled with double),
struct offsets/side-effect order, Miles (AIL_*) boundary calls.

## Constants verified (exact bytes)
| sym | addr | value |
|-----|------|-------|
| dbl_611524 (meters/unit) | 0x611524 | 0.0254000508001016 |
| flt_61152C (atten blend) | 0x61152c | 0.5f |
| dbl_61150C (pan ampl)    | 0x61150c | 127.0 |
| dbl_611514 (pan half)    | 0x611514 | 0.5 |
| dbl_61151C (pan center)  | 0x61151c | 63.0 |
| dbl_628CE0 (eps)         | 0x628ce0 | 1e-07 |
| dbl_628CE8              | 0x628ce8 | **-6.28318530718 (-2*PI)** |
| flt_611588 (2*PI)        | 0x611588 | 6.2831855f |
| flt_5CA2B0..B8 (fwd base)| 0x5ca2b0 | (0,0,1) |
| flt_5CA2D0..D8 (up axis) | 0x5ca2d0 | (0,1,0) |

## sound3d.cpp / .h
| function | addr | status |
|----------|------|--------|
| AngleBetween (VIBE_Math_VectorAngleBetween) | 0x5ca334 | **FIXED** |
| Compute3dVolume (UpdateAttenuation curve)   | 0x4249d0 | VERIFIED-1:1 |
| Compute3dPan (UpdatePosition)               | 0x4248d4 | FIXED (comment+order) |
| Sound3dPool::findFreeSlot                    | 0x424744 | VERIFIED-1:1 |
| Sound3dPool::attach (AttachToEntity)         | 0x4245f0 | VERIFIED-1:1 |
| Sound3dPool::detach (DetachEntry)            | 0x4246ec | VERIFIED-1:1 (in-cluster; +524 back-ptr write is on external entity object) |
| Sound3dPool::stopAll                         | 0x424890 | VERIFIED-1:1 |
| Sound3dPool::updateAll                       | 0x424790 | VERIFIED-1:1 (drives the two curve fns) |

### FIXED — AngleBetween (Rule 8 / Rule 1)
Old code was a *cheap analogue*: full-3D `acos(dot/|a||b|)`, magnitude only.
The real `VIBE_Math_VectorAngleBetween` @0x5ca334 is a **signed horizontal
(XZ-plane) angle**:
- forces the **Y component of BOTH vectors to 0** (v12 = v15 = 0.0 @0x5ca36f/0x5ca373),
- normalizes both (VIBE_Math_VectorNormalize @0x5cb148, 1/sqrt, zero→0),
- **parallel** (vectors equal within 1e-7) → returns uninitialized edx in the binary;
  no deterministic value exists, so we return 0.0 (→ sin 0 → pan 63 "center"),
- **antiparallel** → returns the float literal `-3.1415927f` (−PI),
- else the up-axis cross sign `cross.y = fwd.x*dir.z − fwd.z*dir.x` selects
  `−acos(dot)` (source on +x/right) vs `acos(dot) + (−2*PI)` (source on −x/left).

Consequence for panning: +x source → pan 126 (hard right), −x source → pan 0
(hard left). The old analogue produced 126 for **both** sides (no left/right) — a
genuine behavioral divergence. AcosGuarded @0x5f0b9c modeled as `acos(clamp(dot))`.

Evidence: decompile/disasm 0x5ca334, 0x5cb148, 0x5f0b9c; constants 0x5ca2d0..d8 =
(0,1,0), 0x628ce8 = −2*PI, antiparallel literal −3.1415927f. Goldens recomputed
with a faithful oracle: the pre-existing symmetric goldens (126/107/63) already
matched; added a left/right + Y-ignored regression test (PanIsSignedLeftRight).

## sound.cpp / .h
| function | addr | status |
|----------|------|--------|
| SoundSystem::init (LibInit)        | 0x445d90 | VERIFIED-1:1 (boundary glue) |
| SoundSystem::shutdown              | 0x439ccc | VERIFIED-1:1 |
| SoundSystem::playSample            | 0x4461d0 | **FIXED** |
| SoundSystem::playPositioned        | 0x42469c | VERIFIED-1:1 |

### FIXED — playSample (swapped device args)
Binary seeds +0x24 = 127 (volume), +0x28 = 63 (pan), then
`SetSampleVolume(*v9)` → `AIL_set_sample_volume` (pushes +0x24 = 127) and
`SetSamplePan(*v9)` → `AIL_set_sample_pan` (pushes +0x28 = 63). The recon had the
two device calls **swapped**: `setVolume(handle, pan=63)` / `setPan(handle, vol=127)`.
Fixed to `setVolume←volume(127)`, `setPan←pan(63)`. The original's third call,
`SetSampleLoopCount` (AIL_set_sample_loop_count), is a Miles boundary; loop count is
carried by the shim's `playSample`/`startVoice` and this entry only *primes* the slot
(no StartSample), so no separate device loop call exists here. Documented as BOUNDARY.

## soundwave.cpp / .h
| function | addr | status |
|----------|------|--------|
| InitSineTables (d3sndw_Init)       | 0x424d40 | **FIXED** |
| ParseWavHeader                     | — (no addr; Miles RIFF boundary helper) | BOUNDARY |

### FIXED — InitSineTables float/double domain
Binary @0x424e08: `v22 = flt_611588 / (double)(u16)a1` — the step division is done
in **double** (2*PI promoted, n widened) then rounded to the float step; phase
accumulates in float; `sin()` takes the float phase promoted to double. Recon was
doing float/float division. Matched the exact double-divide → float-store path and
the float-accumulate / double-sin pattern. (Guards `n <= 4 → invalid`, three N-float
tables, table0 = sin(k*step) verified VERIFIED-1:1.)

## audio_recon_dispatch.cpp (control-flow glue over engine bookkeeping)
| function | addr | status |
|----------|------|--------|
| cmdPlaySample (CmdPlaySample)       | 0x440d28 | VERIFIED-1:1 (boundary glue) |
| cmdPlaySample3D (CmdPlaySample3D)   | 0x440d5c | VERIFIED-1:1 (mode==1 one-shot baseVol 60, vol→float) |
| cmdStopSample (CmdStopSample)       | 0x440db0 | VERIFIED-1:1 |
| sound3dBindHandle (Sound3d_BindHandle) | 0x424bd4 | VERIFIED-1:1 (offsets +0x34/+0x44/+0x50/+0x4C/+0x3C confirmed via disasm; +0x4C write is an indeterminate ecx in binary, recon writes 0) |
| stopActiveTrack (StopActiveTrack)   | 0x43a2b4 | VERIFIED-1:1 (CRITICAL_SECTION → no-op per Win32→SDL boundary) |
| updateActiveVoice (UpdateActiveVoice) | 0x439d30 | VERIFIED-1:1 |
| playAmbientTrack (PlayAmbientTrack) | 0x43a984 | VERIFIED-1:1 (296-byte zero-init, word-at-a-time strcpy == C string copy, +256 stream / +262 music flag) |
| initDefaultMixer (InitDefaultMixer) | 0x449400 | VERIFIED-1:1 → LibInit(0,3,2,44100) |
| sampleBankCreate (SampleBank_Create)| 0x447920 | VERIFIED-1:1 (Destroy → pendingBank gate → 0x40 alloc → 64-byte clear → StrNCopyPad(50) → ++bankSeq) |

## Counts
- VERIFIED-1:1: 18
- FIXED: 3  (AngleBetween @0x5ca334, playSample @0x4461d0, InitSineTables @0x424d40)
  (+ Compute3dPan @0x4248d4 comment/order touch-up)
- BOUNDARY (Miles/Win32, documented): ParseWavHeader, SetSampleLoopCount site,
  StopActiveTrack critical section, all AIL_* device calls.

## Tests
- `audio_recon_dispatch_test`: PASS
- `audio_test`: my suites PASS — `Audio3dMath.*` (incl. new `PanIsSignedLeftRight`
  golden regression), `AudioVoice.*`, voice/queue. One UNRELATED failure in the same
  binary: `AudioSampleBank.SizeFormulas` (line 338, `computeTotalSize`) lives in
  `src/audio/samplebank.cpp` (NOT my cluster; another agent's in-flight edit).
- `audio_loaders_test`: PASS (exercises InitSineTables)

Pre-existing/cross-agent failures unrelated to this cluster (not touched here):
- `AudioSampleBank.SizeFormulas` — `computeTotalSize` in `src/audio/samplebank.cpp`.
- `session_audio_test`, `audio_leaves_e2e_test` — `masterVolume` assertions, curve in
  `src/play/session_audio.cpp`.
None link to pan/3D/playSample/sine-table code.
