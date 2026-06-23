# 30 — Audio (Miles → SDL)

How the original `gilde.exe` (Die Gilde / Europa 1400, 32-bit x86, imagebase
`0x400000`) produces sound. This document describes the **original binary** as recovered
from the IDA Pro / Hex-Rays decompilation: the Miles Sound System (MSS32 / `mss32.dll`,
`_AIL_*` imports) driver bring-up, the `.sbf` sample-bank format, voice allocation and
the per-frame voice update, the 3D positional-sound pipeline, MP3 music streaming, and
the ambient / wildlife / dialogue voice updates.

It is also the **platform-boundary reference** for the reimplementation: the engine's
mixing/attenuation/pan math is reconstructed 1:1, but rule 5 replaces **all** of Miles
with SDL. The mapping table at the end lists every `_AIL_*` import the binary uses and
the SDL primitive that replaces it.

Cross-links: [04 — App init](04-app-init-subsystems.md) ·
[14 — Per-frame loop](14-per-frame-loop.md) ·
[17 — Character actions](17-character-actions-ai.md) ·
[29 — VFS](29-vfs-fileio-compression.md).

---

## 1. Summary / architecture

There are **two independent Miles subsystems**, each opened against `mss32.dll` and each
keeping its own array of digital-output drivers (`dword_62EA1C[16]`, the global
"DIG driver" slot table):

1. **The SFX/voice mixer** ("sblib" / `sb_*`). Initialised by `VIBE_Sound_LibInit`
   `@0x445d90`. Owns:
   - the global driver handle `dword_62E8FC`,
   - the **voice channel pool** `dword_62EA08` (`dword_62EA0C` channels × 48 bytes each),
   - the chain of loaded **sample banks** (`dword_62E8F8` head, linked through `+316`).
   This is what plays UI clicks, footsteps, animal calls, weather loops, and dialogue.

2. **The music/streaming subsystem** ("athmos" / `VIBE_Audio_*Track`). Initialised by
   `VIBE_Sound_InitThread` `@0x439bf8`, which opens **two** digital outputs
   (`dword_62DA28` for outdoor music, `dword_62DA2C` for ambient/athmos), spins up a
   worker thread, and serves MP3 streams via `AIL_open_stream`.

A third layer, **3D positional sound** (`d3snd` / `VIBE_Sound3d_*`), sits on top of the
SFX mixer: it owns a pool of attach-points (`dword_62D354`, `dword_62D358`=10 entries ×
84 bytes) plus precomputed sine/cosine tables (`d3sndw`) for the ray-cast
echo/occlusion model used by the listener update.

All three are brought up from a single root, **`VIBE_App_InitEngineAndScriptCommands`
`@0x528560`** (see [04 — App init](04-app-init-subsystems.md)); xrefs confirm it is the
sole caller of `VIBE_Audio_StartupMilesDriver`, `VIBE_Sound_LibInit`,
`VIBE_Sound_InitThread`, and `VIBE_Sound3d_InitPool`.

A note on timestamps: nearly every audio function timestamps events as
`13 * dword_62EB38`, i.e. the global tick counter scaled to milliseconds — this is the
"now" used for fades, retriggers, and voice ageing throughout.

---

## 2. Driver bring-up

### 2.1 `VIBE_Audio_StartupMilesDriver` `@0x449840`

The one-time Miles init. Guarded by `dword_62EADC` (the "Miles is up" flag):

```c
// gilde.exe 0x449840 — VIBE_Audio_StartupMilesDriver
if (dword_62EADC) return -1;   // already started
AIL_startup();                 // _AIL_startup@0
AIL_last_error();              // clears/reads pending error
dword_62EADC = -1;             // mark up
return 0;
```

Every lower-level wrapper checks `dword_62EADC` before touching an `_AIL_*` import, so
this flag is the master gate for the whole `mss32` surface.

### 2.2 `VIBE_Audio_OpenDigitalOutput` `@0x449910`

Opens one digital-output (DIG) driver into a free slot of `dword_62EA1C[16]`. Each slot
is a 0x1C-byte record allocated via `VIBE_Memory_AllocDebug(0x1C, "sfx_OpenDigOutput")`
holding a **WAVEFORMATEX**-shaped header (built at `+4`):

| off  | field                              | source                       |
|------|------------------------------------|------------------------------|
| +0x02| `wFormatTag` = 1 (PCM)              | constant                     |
| +0x06| `nChannels` = a2                   | caller (1 or 2)              |
| +0x0C| `nAvgBytesPerSec` = ch·(bits/8)·rate| computed                     |
| +0x10| `nBlockAlign` = ch·(bits/8)         | computed                     |
| +0x12| `wBitsPerSample` = a3              | caller (8/16)                |
| +0x14| sample-handle table base (handles) | `dword_62EA5C[slot]`         |
| +0x18| handle count = `dword_62EAE0`      | from `AIL_get_preference(1)`  |

The actual device is opened with **`AIL_waveOutOpen(hdr, 0, 0, hdr+4)`**
(`_AIL_waveOutOpen@16`). On the first successful open it queries
`AIL_get_preference(1)` (the max-voices preference) and pins it back with
`AIL_set_preference(1, n)` so all outputs share one voice budget. It then allocates two
parallel per-output arrays sized `4 * dword_62EAE0`: `dword_62EA5C[slot]` (sample
handles) and `dword_62EA9C[slot]` (stream handles), zeroed.

### 2.3 `VIBE_Audio_AllocateSampleHandle` `@0x449e70`

Allocates one Miles sample handle on a given DIG driver:

```c
// gilde.exe 0x449e70
slot = AIL_allocate_sample_handle(*driver);   // _AIL_allocate_sample_handle@4
if (!slot) return -1;
driver[5]++;                                   // bump in-use count
dig_handle_table[freeIdx] = slot;
```

The free index comes from `VIBE_Audio_FindFreeSampleSlot @0x44aac8` scanning
`dword_62EA5C[slot]`. The mirror for streams is
`VIBE_Audio_FindFreeStreamSlot @0x44ab2c` over `dword_62EA9C[slot]`.

### 2.4 `VIBE_Sound_LibInit` `@0x445d90` — the SFX mixer

```c
// gilde.exe 0x445d90 — VIBE_Sound_LibInit (eax=memBudget, edx=numVoices, ebx=bits, ecx=rate)
if (dword_62E8FC) return -1;                   // already inited
VIBE_Audio_OpenDigitalOutput(&dword_62E8FC, bits, rate);
dword_62E8F0 = memBudget ? memBudget : 2097100;// sample-memory budget (~2 MB)
dword_62EA0C = numVoices;
dword_62EA08 = AllocDebug(48 * numVoices, "sb_Init");   // the voice pool
for (i = 0; i < numVoices; i++)
    VIBE_Audio_AllocateSampleHandle(dword_62E8FC, &pool[i].handle);  // +0x00
// "sblib successfully initialzied ! May use %d bytes of memory"
```

So the **voice channel record** is **48 bytes** (`dword_62EA08[i]`):

```cpp
// gilde.exe — SFX voice channel (48 bytes), base = dword_62EA08 + 48*i
struct VoiceChannel {
    i32 ail_handle;     // +0x00  AIL sample handle (from AllocateSampleHandle)
    i32 sample;         // +0x04  -> sample record inside a bank (see §3)
    i32 startTime;      // +0x08  13*tick when (re)started
    i32 loopCount;      // +0x0C  remaining loops
    i32 _pad10;         // +0x10  fade/age timestamp
    u8  flags;          // +0x14  bit0=fadeIn bit1=fadeOutReq bit3=stopped bit4=free
    i32 retriggerGap;   // +0x18  ms between auto-retriggers (0 = none)
    i32 loopGapBase;    // +0x1C  retrigger base time
    i32 _pad20;         // +0x20
    i32 variationIdx;   // +0x24  fixed .sbf variation (when flags&4)
    u8  vol;            // +0x24..  volume 0..127  (set via +0x24/+36 path)
    i32 endCallback;    // +0x2C  one-shot EOS C callback (cleared after fire)
    // +0x09 vol(127) / +0x0A pan(63) set by VIBE_Sound_PlaySample
};
```

`VIBE_Audio_InitDefaultMixer @0x449400` is a thin caller that picks the default
voices/format and calls `VIBE_Sound_LibInit`.

### 2.5 `VIBE_Sound_InitThread` `@0x439bf8` — the music/streaming subsystem

```c
// gilde.exe 0x439bf8
InitializeCriticalSection(&CriticalSection);           // guards all track ops
VIBE_Audio_OpenDigitalOutput(&dword_62DA28, ...) ||    // outdoor-music output
VIBE_Audio_OpenDigitalOutput(&dword_62DA2C, ...);      // ambient/athmos output
QueryPerformanceFrequency(&Frequency);
CreateThread(0,0, StartAddress=0x439b00, 0,0, &tid);   // Miles service/timer thread
```

The two outputs let outdoor music and ambient streams crossfade independently.
`StartAddress @0x439b00` is the Win32 worker thread that drives Miles' timer; under SDL
this becomes the SDL audio callback thread (see §7).

### 2.6 `VIBE_Sound3d_InitPool` `@0x424538` and `VIBE_SoundWave_InitSineTables` `@0x424d40`

```c
// gilde.exe 0x424538 — d3snd pool, dword_62D358 = 10 entries × 84 bytes
dword_62D354 = AllocDebug(84 * dword_62D358, "d3snd_Init"); memset 0;
```

The 3D attach-pool record is **84 bytes** (`dword_62D354 + 84*i`):

```cpp
// gilde.exe — 3D sound attach entry (84 bytes)
struct Sound3dEntry {
    // +0x00 ...                                                  (object/handle refs)
    void* boneChain;    // +0x38  (idx 14) skeleton root for world position
    i32   voice;        // +0x34  (idx 13) -> VoiceChannel currently bound
    i32   sample;       // +0x44  (idx 17) sample to play (one-shot vs loop selector)
    i32   range;        // +0x40  (idx 16) audible radius (set by SetRange)
    i32   loopCount;    // +0x44  (idx 17)
    i32   ownerEntity;  // +0x38  (idx 56 in UpdateAll, the owning game object)
    i32   silenceTicks; // +0x3C  (idx 60) frames with no audible voice (>500 → detach)
    i32   sticky;       // +0x50  (idx 20) keep-alive flag
};
```

`VIBE_SoundWave_InitSineTables @0x424d40` (`d3sndw_Init`) allocates three `4*N`-float
tables — `dword_62D424` (sin), `dword_62D428` (scratch ray attenuation), `dword_62D42C`
(ray state) — where `N = word_62D430` is the number of azimuth rays; it fills the sin
table with `sin(i * 2π/N)` (`flt_611588 = 2π = 6.283185`). These tables drive the
echo/occlusion ray-cast in the listener update (§5.2).

---

## 3. Sample banks — the `.sbf` format

### 3.1 Bank path — `VIBE_Sound_SetBankPath` `@0x4463d8`

Copies the caller's directory into `byte_62E908` and appends a trailing `\` if missing.
All later bank/sample opens are `byte_62E908 + relativeName`.

### 3.2 Loading a bank — `VIBE_Sound_LoadSampleBank` `@0x446b2c`

Opens `<bankPath><name>` in `"rb"` mode (via the VFS — see
[29 — VFS](29-vfs-fileio-compression.md)) and reads a **324-byte (`0x144`) header**, then
allocates `numSamples*64 + 324` bytes for the loaded bank and reads `numSamples` × 64-byte
sample records:

```cpp
// gilde.exe 0x446b2c — .sbf bank header (0x144 = 324 bytes on disk)
struct SbfHeader {
    char  name[50];      // +0x000  bank name (read; copied via StrNCopyPad 50)
    // ... fixed area ...
    i32   numSamples;    // +0x134  v27 — count of 64-byte sample records that follow
};
// In-memory bank object (allocated numSamples*64 + 324):
struct SbfBank {
    char  name[50];      // +0x000
    char  filePath[256]; // +0x032  full path (StrNCopyPad 256)
    i32   numSamples;    // +0x134  (offset 308)
    i32   sampleTable;   // +0x138  (offset 312) -> first 64-byte record (= base+324)
    i32   nextBank;      // +0x13C  (offset 316) linked-list to next loaded bank
    i32   loaded;        // +0x140  (offset 320) = 1
    i32   refCount;      // +0x140  (offset 320 region, ++ when re-requested via FindBankBySample)
};
```

Each loaded bank is appended to the global chain: head `dword_62E8F8`, linked through
`+316` (`VIBE_Sound_GetLastBank @0x446800`). Total resident bytes are tracked in
`dword_62E8F4` and logged ("Samplebank %s loaded. MemUsed is now %d bytes."). If
`a2 != 0` the loader eagerly preloads every sample via `VIBE_Sound_LoadEntry`.

The **64-byte sample record** (`bank.sampleTable + 64*i`):

```cpp
// gilde.exe — .sbf sample record (64 bytes)
struct SbfSample {
    i32   _hdr;          // +0x00
    char  name[50];      // +0x04  sample name (matched case-insensitive, 50 chars — §4.2)
    i32   offset;        // +0x34  (idx 13) byte offset of payload in the .sbf data file
    char  kind;          // +0x36  1 = single WAV/MP3, 2 = variation set
    i32   loaded;        // +0x38  (offset 56) -> decoded payload, 0 until LoadEntry
    i32   lastUsed;      // +0x3C  (offset 60) = 13*tick, for LRU eviction
};
```

### 3.3 Lazy decode — `VIBE_Sound_LoadEntry` `@0x446830`

Seeks to `sample.offset` in the bank's data file and reads a **12-byte payload header**,
then `AllocDebug(payloadLen + 12)` and reads the payload. Two payload kinds:

- **kind 1** (single): 12-byte header → `{type:u8, length:i32, dataPtr}`; `type` 1=`.wav`,
  2=`.mp3`. Stored at `sample+56`.
- **kind 2** (variation set): 12-byte header → `{count:i32, table, totalLen:i32}`; reads
  `count` 12-byte sub-entries, each with its own offset/length, so one logical sample has
  several random variations ("Variation %s loaded ! MemUsed is now %d bytes.").

Before allocating, if `dword_62E8F0 - dword_62E8F4 < need` it calls
`VIBE_Sound_FreeMemoryForLoad @0x446694` to evict least-recently-used decoded samples,
honouring the 2 MB-ish budget set in `LibInit`.

### 3.4 Bulk preload — `VIBE_Sound_PreloadFromIncludeFile` `@0x52f154`

Reads `include_sfx.ini` line by line (`"rt"`). Each line carries a quoted path; lines
whose path begins with `sprache\` are loaded as **language/voice banks** via
`VIBE_Voice_LoadLanguageBank @0x582074`, everything else via
`VIBE_Sound_LoadSampleBank(path, 0)`. Resulting bank pointers are stored in
`dword_122EF18[0..96]` (cap 97). On open failure it pops a `MessageBoxA`
("main_PreloadSound(): Could not open sound-definition!"). The first bank
loaded here, `system.sbf`, holds the always-resident UI/engine SFX.

---

## 4. SFX playback & voice update

### 4.1 Voice allocation — `VIBE_Sound_AllocVoiceChannel` `@0x446730`

Walks the 48-byte voice pool and returns the first channel that is **stopped**
(`GetSampleStatus == 4`, i.e. Miles `SMP_DONE`) or marked free (`flags & 0x10`). If none
is free it **steals the oldest** — the channel with the smallest `startTime` (`+0x08`)
that is not currently playing — then `VIBE_Audio_InitSample` (`AIL_init_sample`) and
clears `flags`.

### 4.2 Finding the sample — `VIBE_Sound_FindSampleInBank` `@0x446288`

Case-insensitive match (`StrCmpNoCaseN ..., 50`) of the requested name against
`sample.name` across the bank chain (recurses through `nextBank @+316`, starting at
`dword_62E8F8` when no bank is specified). Returns the 64-byte `SbfSample*`.

### 4.3 One-shot play — `VIBE_Sound_PlaySample` `@0x4461d0`

Allocates a channel, binds the resolved sample, sets defaults
`loopCount=1, vol=127 (+0x24-region), pan=63`, then pushes them to Miles via
`VIBE_Audio_SetSampleVolume / SetSamplePan / SetSampleLoopCount`. Returns the channel
pointer (used everywhere as a "voice handle").

`VIBE_Audio_StartVoiceSample @0x447214` is the richer entry (used by ambient/weather):
allocates a channel, resolves the sample, stashes `vol(a2)/pan(a4)/loopCount(a3)`, and
calls `VIBE_Audio_PlayVoiceSample`.

### 4.4 Actually starting it — `VIBE_Audio_PlayVoiceSample` `@0x44737c`

```c
// gilde.exe 0x44737c
if (!sample.loaded) LoadEntry(bank, sample);          // lazy-decode (§3.3)
// pick payload: kind1 -> the one blob; kind2 -> random variation (or fixed if flags&4)
name = (payload.type==1) ? ".wav" : ".mp3";
VIBE_Audio_SetNamedSampleFile(handle, name);          // _AIL_set_named_sample_file@20
if (fadeIn) flags |= 1; SetSampleVolume; SetSamplePan;
SetSampleLoopCount; startTime = 13*tick; flags &= ~8;
VIBE_Audio_StartSample();                              // _AIL_start_sample@4
```

`VIBE_Audio_SetNamedSampleFile @0x449f4c` wraps `AIL_set_named_sample_file` and is how
the decoded in-memory WAV/MP3 blob is handed to Miles (Miles parses the RIFF/MP3 header
from the blob). The mirror raw path is `AIL_set_sample_file` (`VIBE_Audio_SetSampleFile`).

### 4.5 Per-frame SFX update — `VIBE_Sound_UpdateVoices` `@0x445f00`

Called every frame from the per-frame loop (see [14](14-per-frame-loop.md)). Two modes:

- **Pause/fade mode** (`dword_62EA14`): for each playing voice, if it has exceeded
  `dword_62EA18` ms it is stopped (`StopVoice`), else its volume is held; the mode clears
  once nothing is left.
- **Normal mode** (`dword_62EA10` = fade duration): for each voice, recompute volume:
  - flag `0x10` (just allocated) → stamp `startTime`;
  - if still playing (`status==4`) and a fade flag (`&3`) is set, scale volume linearly
    over `dword_62EA10` ms (fade in if `&1`, else fade out) and re-push via
    `SetSampleVolume`; at the end either `EndSample` (if fade-out) or hold;
  - if **stopped** but it is a **loop with a retrigger gap** (`+0x18`), and
    `gapBase + gap < now`, decrement `loopCount` and call `PlayVoiceSample` again — this
    is how looping ambience auto-repeats with silence between plays;
  - if a one-shot finished and has an `endCallback` (`+0x2C`), fire it once and clear it.

---

## 5. 3D positional sound

### 5.1 Attach & play on an entity

`VIBE_Sound3d_PlayOnEntity @0x42469c` resolves a game object by handle
(`VIBE_Object_FindByHandle`), plays the sample (`VIBE_Sound_PlaySample`), then attaches
the resulting voice to the entity via `VIBE_Sound3d_AttachToEntity @0x4245f0` (taking a
free 84-byte pool slot from `VIBE_Sound3d_FindFreeSlot @0x424744`, recording the bone
chain and range). `VIBE_Sound3d_PlayOneShot @0x424cd0`, `SetLooping @0x424c88`,
`SetRange @0x424cb0` are the per-entry tweaks.

### 5.2 Per-entry attenuation/pan — `VIBE_Sound3d_UpdateAttenuation` `@0x4249d0`

For each attached entry it transforms the emitter's world position through the bone chain
(`VIBE_Transform_PointThroughBoneChain`), takes the delta to the listener position
(`dword_13FCD1C + 76/80/84`), and computes distance:

```c
dist = sqrt(dx*dx+dy*dy+dz*dz) * dbl_611524;     // 0x611524 world→audio unit scale
remain = entry.range - dist;
if (remain <= 0) volume = 0;                      // out of range → silence
else {
    base = entry.volume;
    vol  = base - dist*(base*dist)/(range*range); // inverse-square-ish falloff
    pan  = (vol-contributions) * flt_61152C;      // 0x61152C pan scale (0.5)
}
```

If audible (`pan > 5`) it binds/keeps a voice (`Sound3d_BindHandle @0x424bd4`) and pushes
`VIBE_Audio_SetVoicePan @0x447574`; out of range it `SetVoicePan` to the edge and
`StopVoice`. Position (left/right) is set in `VIBE_Sound3d_UpdatePosition @0x4248d4`.
**Note:** the engine does its own attenuation/pan math and only ever calls Miles'
*mono* `AIL_set_sample_volume` / `AIL_set_sample_pan` — it does **not** use Miles' own
3D providers, so the reimplementation does likewise.

### 5.3 Per-frame 3D update — `VIBE_Sound3d_UpdateAll` `@0x424790`

Iterates the 10-entry pool; for each live entry whose owner matches the current camera
target (`off_649D64`) it runs `UpdateAttenuation` + `UpdatePosition`, and ages out voices
that stopped: after **500 silent frames** (`+0x3C`) a non-sticky entry is detached
(`Sound3d_DetachEntry @0x4246ec`) or its loop flag cleared. Entries whose owner is no
longer the active object are stopped immediately.

### 5.4 The listener & echo model

`VIBE_Sound3d_SetActiveListener @0x425e10` builds four orientation samples (front, and
three offsets) relative to the camera matrix and creates a driver object-animation
(`VIBE_Anim_CreateObjectAnim`) so the listener tracks the camera smoothly;
`SetListenerOrientation @0x426014` / `SetListenerFromVectors @0x4262a0` are the lower
setters.

`VIBE_Sound3d_UpdateListener @0x425208` is the per-frame **environment** pass. It samples
the floor tile under the camera (`VIBE_Floor_PickTileAtPoint`,
`VIBE_Heightmap_LookupTileAttribute`), reads the tile's "echo class" (top byte of the
tile attribute, 0..3), then casts azimuth rays using the `d3sndw` sin/cos tables
(`word_62D430` rays, ring radius stepping by `dbl_61159C`, up to `flt_62D360 = 30` rings)
to detect nearby reflective surfaces. From the up-to-two strongest reflections it derives
delay/volume and either updates or starts the two ambient "echo" voices
(`dword_62D468[0]`, `dword_62D4BC`) via `VIBE_Audio_StartVoiceSample` /
`SetVoicePan` / `SetVoiceVolume` / `Sound3d_SelectChannel @0x424f00`. The per-class
strengths come from `flt_62D35C[4]` and `dword_62D374[4]`. This is the reverb/occlusion
"feel" of being indoors vs in open terrain.

---

## 6. Music & ambient streaming

Track records are 296-byte voice slots managed under `CriticalSection`. Key fields used
by the track machine:

```cpp
// gilde.exe — music track slot (≈296 bytes, name at +0)
struct TrackSlot {
    char name[256];     // +0x000  file path
    i32  stream;        // +0x100  AIL stream handle (256)
    u8   active;        // +0x104  (260) playing
    u8   queued;        // +0x105  (261) waiting behind another track
    u8   isAmbient;     // +0x106  (262) 0=outdoor output, 1=athmos output
    i32  lenMs;         // +0x108  (264) AIL_stream_ms_length
    i32  resumeMs;      // +0x10C  (268) saved position for resume
    i32  fadeTarget;    // +0x10E.. (272/276/280/284/288) fade state machine
    i32  next;          // +0x124  (292) queued-after pointer
};
```

### 6.1 Load/Start — `VIBE_Audio_LoadTrack` `@0x439ed0` → `VIBE_Audio_StartTrack` `@0x439f8c`

`LoadTrack` finds an active slot for the same file (or a free voice slot), copies the
name, and calls `StartTrack`. `StartTrack`:

- opens the MP3 stream with `VIBE_Audio_OpenStream @0x44a544` →
  **`AIL_open_stream(driver, path, 0)`** on either the outdoor (`dword_62DA28`) or athmos
  (`dword_62DA2C`) output depending on `isAmbient`;
- on first open, queries length via `AIL_stream_ms_position` length call and sets default
  volume 127;
- if resuming and the saved position is within 10 s of the end it seeks to `resumeMs`
  (`SetStreamMsPosition`) paused at volume 0 and arms a fade-in; otherwise it starts fresh
  (`StartStream` → `AIL_start_stream`, position 0, full volume);
- sets loop count (`SetStreamLoopCount` → `AIL_set_stream_loop_count`: loop forever for
  ambient, once for one-shot);
- if another track is already active it **queues** itself (`next @+292`) and fades the
  current one out (`VIBE_Audio_FadeOutTrack @0x43a910`); else it un-pauses and marks
  `active`.

`VIBE_Audio_PlayAmbientTrack @0x43a984` is the athmos-output convenience entry (used by
the wildlife code for `Athmo_*.mp3`); it closes any existing stream on the slot, starts
the track on `dword_62DA2C`, and positions it.

### 6.2 Stop / volume — `VIBE_Audio_StopTrack` `@0x43a2fc`, `VIBE_Audio_SetGlobalVolume` `@0x43a3dc`

`StopTrack` either fades out (`FadeOutTrack(slot, 2)` when `a2`) or hard-stops:
`PauseStream(stream, 1)` + `VIBE_Audio_CloseStream @0x44a5f0` (→ `AIL_close_stream`) and
clears `active`/`stream`. `SetGlobalVolume` just stores the master music volume in
`dword_62DA24` (applied when streams are (re)opened).

The thin `_AIL_*` stream wrappers used here: `StartStream`(`AIL_start_stream`),
`PauseStream`(`AIL_pause_stream`), `SetStreamVolume`(`AIL_set_stream_volume`),
`SetStreamPan`(`AIL_set_stream_pan`), `SetStreamMsPosition`(`AIL_set_stream_ms_position`),
`SetStreamLoopCount`(`AIL_set_stream_loop_count`),
`SetStreamPlaybackRate`(`AIL_set_stream_playback_rate`),
`GetStreamStatus`(`AIL_stream_status`), `GetStreamMsLength`(`AIL_stream_ms_position`),
`RegisterStreamCallback`(`AIL_register_stream_callback`).

### 6.3 Outdoor-track scheduler — `VIBE_Music_UpdateOutdoorTrackPlayback` `@0x581594`

The per-frame music director (intensity-driven, see [17](17-character-actions-ai.md)).
Tracks live in a table at `dword_645F18` (69-dword stride per track; `byte_64602A`/
`byte_645F28` are active/enabled flags, `word_646028`/`unk_646026` carry interrupt
probabilities). Each frame it:

- on **season change** stops the running outdoor track (`StopTrack(slot,1)`) and remembers
  it ("Outdoortrack %s stopped because of seasonchange !");
- when the current track ends, schedules a **silence pause**
  (`pauseLength = (1 - intensity) * flt_626078`) before the next one;
- after `0x9C40` (40 000) ms it may **randomly interrupt** the running track based on the
  per-track probability (`VIBE_Math_RandomModulo(0x80)`), then resumes a
  location-specific track (`VIBE_Music_ResumeLocationTrack @0x580de4`) or picks a new
  seasonal outdoor track (`VIBE_Music_SelectOutdoorSeasonTrack @0x581208`).

---

## 7. Ambient, weather & dialogue voice queues (per-frame)

### 7.1 Weather loops — `VIBE_Weather_UpdateAmbientLoops` `@0x57f190`

Drives the **wind/rain bed**. Reads precipitation intensity (`*dword_11BC1C8`) and
listener height, then maintains up to seven concurrent loop voices
(`dword_123528C/_1235284/_1235294/_123529C/_12352A4/_12352AC/_12352B4`) blended across
intensity bands (`<149`, `<499`, `<999`, `≥999`). For each band it either **starts** a
loop (`VIBE_Audio_StartVoiceSample`) or **adjusts** its pan/volume
(`VIBE_Audio_SetVoicePan`), and `StopVoice`s the voices that no longer belong to the
current band — a crossfading wind model keyed on `dword_63C74C` (the weather sample bank).
It also fires occasional one-shot gusts (`VIBE_Util_RandNext`-gated).

### 7.2 Wildlife — `VIBE_Ambient_UpdateWildlifeSounds` `@0x5800f0`

Season-and-location-driven critters. Indexed by season (`dword_64229C` top byte). Several
independent random emitters (birds, crickets/`hpLocusts`, frogs, etc.) each keep a
next-fire timestamp (`dword_642118/_64212C/_642154/_642140/_642168`) and an interval table
(`dword_642114/_642128/...`); when due and a random gate passes it starts a one-shot
`StartVoiceSample`. The persistent locust drone (`dword_642298`/`dword_64229C`) is started
in summer and stopped in winter / when crowd-density is high; building-type-specific
athmos (`Athmo_Witshaus.mp3` etc.) is started via `VIBE_Audio_PlayAmbientTrack` based on
the building under the camera. Volumes scale with a head-count
(`VIBE_Character_CountByOwner`) so busy areas sound livelier.

### 7.3 Dialogue/voice queue — `VIBE_VoiceQueue_ProcessNext` `@0x57eff0`

Serialises spoken-line playback so two characters don't talk over each other. The queue
head is `dword_6420F8` (linked nodes carrying a voice + delay). Each frame, when not
globally muted (`dword_642014`):

- if the head node hasn't started and its delay elapsed, it **ducks the music**
  (`VIBE_Audio_SetFadeVolume(0.0, 2000)` if a track is playing), plays the line
  (`PlayVoiceSample`), and stamps it started;
- if the line has **finished** (`!VoiceIsPlaying`), it clears the loop flag, frees the
  node (`VIBE_Memory_FreeDebug`), advances the queue, and **un-ducks** the music
  (`SetFadeVolume(1.0, 2000)`) when the queue empties.

`VIBE_Audio_VoiceIsPlaying @0x4471b0` is the shared "still playing?" predicate
(`GetSampleStatus == 4` / Miles `SMP_PLAYING`); `VIBE_Audio_StopVoice @0x447508` either
arms a fade-out (`flags|=2`) or hard-ends (`EndSample`, `flags|=8`).

---

## 8. The thin `_AIL_*` wrapper layer

All Miles calls funnel through one-line wrappers under `VIBE_Audio_*` that (a) check the
`dword_62EADC` gate and (b) translate a channel/stream pointer to its Miles handle via
`VIBE_Audio_LookupSampleHandleIndex @0x44ab90` / `LookupStreamHandleIndex @0x44abd8`.
Examples:

```c
// gilde.exe 0x44a1b8
int VIBE_Audio_StartSample(...) {
  if (!dword_62EADC || LookupSampleHandleIndex(a1) < 0) return -1;
  AIL_start_sample(handle); return 0;
}
// 0x44a2f4 SetSampleVolume -> AIL_set_sample_volume(handle, vol)
// 0x44a360 SetSamplePan    -> AIL_set_sample_pan(handle, pan)
// 0x44a510 RegisterSampleEosCallback -> AIL_register_EOS_callback(handle, cb)
```

This single choke-point is exactly where the SDL backend substitutes (see
[04 — App init](04-app-init-subsystems.md) for how the shim is selected). The engine's
record layouts, banks, scheduling and 3D math above are reconstructed **unchanged**; only
the bodies of these `VIBE_Audio_*` wrappers change.

---

## 9. `_AIL_*` import → SDL mapping (rule 5)

Every `mss32._AIL_*` import the binary references, its purpose, and the SDL replacement.
`mss32.dll` is replaced wholesale by the `IAudioDevice` SDL shim; the device is
`SDL_OpenAudioDevice` with an `SDL_AudioSpec` callback that runs the engine mixer (the
former Miles timer thread, §2.5). "Voices"/"streams" become entries in the shim's mixer
table; PCM comes from `SDL_LoadWAV`/`SDL_AudioStream` (WAV) and the pl_mpeg MP3 decoder
shim (see Rule-6 decisions) feeding `SDL_AudioStream` for `.mp3`.

| `_AIL_*` import | purpose in `gilde.exe` | SDL replacement |
|---|---|---|
| `AIL_startup` / `AIL_shutdown` | init/teardown Miles | `SDL_InitSubSystem(SDL_INIT_AUDIO)` / `SDL_QuitSubSystem` |
| `AIL_last_error` | read/clear last error | `SDL_GetError()` |
| `AIL_get_preference` / `AIL_set_preference` | max-voices (pref #1) budget | shim mixer voice-count constant |
| `AIL_waveOutOpen` / `AIL_waveOutClose` | open/close DIG output (WAVEFORMATEX) | `SDL_OpenAudioDevice` / `SDL_CloseAudioDevice` |
| `AIL_digital_handle_release` / `AIL_digital_handle_reacquire` | yield/regrab device (focus loss) | `SDL_PauseAudioDevice(true/false)` |
| `AIL_allocate_sample_handle` / `AIL_release_sample_handle` | per-voice Miles handle | allocate/free a shim mixer voice slot |
| `AIL_init_sample` | reset a sample handle | reset shim voice state |
| `AIL_set_sample_file` / `AIL_set_named_sample_file` | hand WAV/MP3 blob to Miles | decode blob (SDL_LoadWAV_RW / pl_mpeg) into an `SDL_AudioStream` |
| `AIL_set_sample_type` | declare PCM/format | set `SDL_AudioStream` src format |
| `AIL_start_sample` / `AIL_stop_sample` / `AIL_end_sample` / `AIL_resume_sample` | transport | set shim voice playing/stopped/paused |
| `AIL_sample_status` | playing? (`==4`) | shim voice state query |
| `AIL_set_sample_volume` / `AIL_sample_volume` | per-voice volume 0..127 | scale samples in the mix callback |
| `AIL_set_sample_pan` / `AIL_sample_pan` | per-voice pan 0..127 | L/R gain split in the mix callback |
| `AIL_set_sample_playback_rate` / `AIL_sample_playback_rate` | resample rate | `SDL_AudioStream` src rate |
| `AIL_set_sample_loop_count` / `AIL_sample_loop_count` | loop count | shim voice loop counter |
| `AIL_set_sample_ms_position` / `AIL_sample_ms_position` / `AIL_sample_position` | seek/query | seek the voice's `SDL_AudioStream` |
| `AIL_register_EOS_callback` | one-shot end-of-sample callback | shim invokes stored callback when stream drains |
| `AIL_active_sample_count` | voices in use | count active shim voices |
| `AIL_open_stream` / `AIL_close_stream` | open/close MP3 music stream | open pl_mpeg decoder → `SDL_AudioStream` |
| `AIL_start_stream` / `AIL_pause_stream` | stream transport | shim stream play/pause |
| `AIL_stream_status` | stream playing? | shim stream state |
| `AIL_set_stream_volume` / `AIL_stream_volume` | music volume | scale in mix callback |
| `AIL_set_stream_pan` / `AIL_stream_pan` | music pan | L/R gain split |
| `AIL_set_stream_playback_rate` / `AIL_stream_playback_rate` | stream rate | `SDL_AudioStream` rate |
| `AIL_set_stream_loop_count` / `AIL_stream_loop_count` | loop music | shim stream loop flag |
| `AIL_set_stream_ms_position` / `AIL_stream_ms_position` | seek/length | seek decoder / report duration |
| `AIL_register_stream_callback` | stream service callback | driven by SDL audio callback thread |
| `AIL_set_digital_master_volume` | master volume | global gain in mix callback |
| `AIL_get_timer_highest_delay` | timer jitter telemetry | no-op (logging only) |

---

## 10. Provenance index (roots)

| Address | Function | Role |
|---|---|---|
| `0x528560` | `VIBE_App_InitEngineAndScriptCommands` | sole caller of all four bring-up roots |
| `0x449840` | `VIBE_Audio_StartupMilesDriver` | `AIL_startup`, set `dword_62EADC` |
| `0x449910` | `VIBE_Audio_OpenDigitalOutput` | `AIL_waveOutOpen`, WAVEFORMATEX, slot table |
| `0x449e70` | `VIBE_Audio_AllocateSampleHandle` | `AIL_allocate_sample_handle` |
| `0x445d90` | `VIBE_Sound_LibInit` | SFX mixer, 48-byte voice pool |
| `0x439bf8` | `VIBE_Sound_InitThread` | two music outputs + worker thread |
| `0x424538` | `VIBE_Sound3d_InitPool` | 10×84-byte 3D attach pool |
| `0x424d40` | `VIBE_SoundWave_InitSineTables` | d3sndw sin/cos ray tables |
| `0x4463d8` | `VIBE_Sound_SetBankPath` | bank directory |
| `0x446b2c` | `VIBE_Sound_LoadSampleBank` | `.sbf` load (324-byte header) |
| `0x446830` | `VIBE_Sound_LoadEntry` | lazy WAV/MP3 + variation decode |
| `0x52f154` | `VIBE_Sound_PreloadFromIncludeFile` | `include_sfx.ini` bulk preload |
| `0x446730` | `VIBE_Sound_AllocVoiceChannel` | voice allocation / oldest-steal |
| `0x446288` | `VIBE_Sound_FindSampleInBank` | name → sample lookup |
| `0x4461d0` | `VIBE_Sound_PlaySample` | one-shot play |
| `0x447214` | `VIBE_Audio_StartVoiceSample` | rich play (vol/pan/loop) |
| `0x44737c` | `VIBE_Audio_PlayVoiceSample` | decode + start sample |
| `0x445f00` | `VIBE_Sound_UpdateVoices` | per-frame SFX fade/retrigger |
| `0x42469c` | `VIBE_Sound3d_PlayOnEntity` | 3D positional play |
| `0x4249d0` | `VIBE_Sound3d_UpdateAttenuation` | distance falloff + pan |
| `0x424790` | `VIBE_Sound3d_UpdateAll` | per-frame 3D pool update |
| `0x425208` | `VIBE_Sound3d_UpdateListener` | environment/echo ray-cast |
| `0x425e10` | `VIBE_Sound3d_SetActiveListener` | listener orientation/anim |
| `0x439ed0` | `VIBE_Audio_LoadTrack` | music track load |
| `0x439f8c` | `VIBE_Audio_StartTrack` | `AIL_open_stream` + transport |
| `0x43a2fc` | `VIBE_Audio_StopTrack` | stop/fade + `AIL_close_stream` |
| `0x43a3dc` | `VIBE_Audio_SetGlobalVolume` | master music volume |
| `0x43a984` | `VIBE_Audio_PlayAmbientTrack` | athmos-output MP3 |
| `0x581594` | `VIBE_Music_UpdateOutdoorTrackPlayback` | outdoor music director |
| `0x57f190` | `VIBE_Weather_UpdateAmbientLoops` | wind/rain crossfade bed |
| `0x5800f0` | `VIBE_Ambient_UpdateWildlifeSounds` | seasonal wildlife/athmos |
| `0x57eff0` | `VIBE_VoiceQueue_ProcessNext` | dialogue queue + music ducking |
