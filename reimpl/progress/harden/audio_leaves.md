# Hardening report — audio_leaves.cpp / audio_leaves2.cpp

1:1 binary diff of every provenance-carrying function against `gilde.exe`
(Hex-Rays + disasm; disasm = reference of record). MCP IDA Pro, module gilde.exe.

## Counts
- audio_leaves.cpp:  12 functions — 10 VERIFIED-1:1, 2 FIXED
- audio_leaves2.cpp: 17 functions — 15 VERIFIED-1:1, 2 FIXED, 1 BOUNDARY (Miles/Win32 hooks)
- Total: 25 VERIFIED, 4 FIXED, 1 BOUNDARY layer.

Tests (build dir, GUILD_GAME_DIR set): all 7 audio_leaves/session_audio targets pass
(audio_leaves_test, audio_leaves2_test, audio_leaves2_itest, audio_leaves2_e2e_test,
audio_leaves_e2e_test, session_audio_test, session_audio_e2e_test).

## audio_leaves.cpp

### FIXED — ApplyVolumeSettings @0x56c148
Two divergences, both proven from disasm (opcodes confirmed via get_bytes):
1. **v6 is a 32-bit float, not double.** 56c16b `fstp dword [esp]` (bytes `D9 1C 24`,
   D9 /3 = fstp m32fp) stores v6; 56c172 `fmul dword [esp]` (bytes `D8 0C 24`, D8 /1
   = fmul m32fp) reloads it. So `v6 = (float)(soundByte*scale0)` rounds through
   float32 before every master/sfx/music product. The prior reconstruction kept v6
   as double.
   - Observable impact: with the shipped INI (sound=127, scale0=0x3C010204), the
     double product 0.99999999627 rounds to **1.0f**, so `music = trunc(114*1.0) = 114`
     (not 113 as the old double-precision golden claimed). Same for sfx (50 not 49).
2. **master & music both use musicByte (byte_1233551); soundByte only feeds v6.**
   Trace of 56c14d..56c182: SetMasterVolume = trunc(musicByte * v6). Hex-Rays
   mislabelled the operands (attributed 1233550/552 to the wrong sites). The old
   reconstruction set master = (int)(soundByte*scale0) = v6 itself — wrong.
   - before: `masterVolume = (int)(soundByte*scale0)`
   - after:  `masterVolume = (int)(musicByte * (double)(float)(soundByte*scale0))`
Goldens corrected to the binary in:
  tests/unit/audio_leaves_test.cpp (ApplyVolumeSettingsGolden, all 5 cases),
  tests/e2e/audio_leaves_e2e_test.cpp (line 69-75; inputs adjusted so clamp stays
  in range while exercising the corrected master = trunc(musicByte*v6) math),
  tests/unit/session_audio_test.cpp (113/49 -> 114/50; v6 float32 round-trip).

### FIXED — AppendAmbientVoice @0x505ba8
When the list is full (count >= 16) the binary executes `return result;` where
`result` = the StartVoiceSample handle (== the handle passed in), NOT the count.
  - before: `return prior;`  (= 16)
  - after:  `return static_cast<int>(handle);`
Golden corrected: tests/unit/audio_leaves_test.cpp AmbientVoiceListAppendAndStop
(full case 999 -> returns 999). No live xrefs (function unreferenced), so behavior
change is contained, but now 1:1.

### VERIFIED-1:1
- FindFreeStreamSlot @0x44ab2c — walks dword_62EA9C[idx] to first zero, cap check
  `*(a1+20) >= *(a1+24)`, `if (cap<=0) -1`. Dead `if(v3<0)` guard correctly omitted.
- LookupStreamHandleIndex @0x44abd8 — per-output [0, +0x18 maxSampleHandles); first match.
- LookupSampleDriverIndex @0x44ac20 — v6 advances every slot (incl. closed); returns
  owning output index. Loop bound +0x18.
- LookupStreamDriverIndex @0x44ac88 — same shape over stream slots.
- FindDriverIndex @0x44aa94 — pointer-identity match adapted to the slot index.
- CountAllocatedVoices @0x449528 — sum of allocatedSampleCount over open outputs
  (faithful equivalent of the 0x10 flag scan over the global voice array).
- GetGlobalPreference @0x449dd4 — driver gate; `if(!count && !pref) -1`; *out=pref.
- SetGlobalPreference @0x449e20 — driver gate; `if(count>1) -1`; store pref (AIL_set_
  preference is the BOUNDARY, not modelled here).
- ClampDigitalMasterVolume @0x4498b4 — `(unsigned)vol >= 0x80` rejects >127 & wrapped.
- FadeOutTrack @0x43a910 — arms fade on active track; msPos/fadeStartMs/fadeBaseVol.
  Return value (track ptr / fadeBaseVol) is discarded by both callers (StartTrack
  0x43a006 does xor eax,eax after; StopTrack 0x43a32f ignores) — void return is faithful.
- SetTrackNamePrefix @0x43a95c — word-copy loop produces the same NUL-terminated
  buffer as a byte copy for all well-formed strings; returns 0 (al).
- StopAmbientVoices @0x505da8 — clears slot i (`*(122DC9C + v1_new)` == `122DCA0[i]`,
  the +4/122DC9C is obfuscation), resets count to 0. Return value is the device
  StopVoice result in the binary; we return the stopped count (documented adaptation,
  the StopVoice call itself is the voice-layer boundary).

## audio_leaves2.cpp (DriverManager)

### FIXED — reacquireDigitalDriver @0x449cc8
Not-found path returns **64**, not 0. eax is used as a byte offset (0,4,...) while
searching dword_62EA1C; on the not-found break it has reached 0x40 and that value is
returned (disasm 449cf8 `jge loc_449D06`; loc_449D06 = retn with eax==0x40).
  - before: not-found -> result stays 0.
  - after:  `else { result = 64; }`
Other paths verified: driver-down / msg<0x400 returns entry handle (eax never
modified); found+reacquire!=0 returns reacquire value; found+reacquire==0 posts.
Golden corrected: tests/integration/audio_leaves2_itest.cpp line 133 (0 -> 64).

### FIXED — reacquireAllDigitalDrivers @0x449d2c
Return value is eax carried across the 16-slot loop. When a USED slot fails the msg
check, the binary has just done `eax = dword_62EADC` (449d66) which is **-1** once
the driver is up, then `jb loc_449D4D` (449d79) — so result becomes -1, not 0.
Empty slots leave eax untouched. Initial eax = the hwnd argument (returned if no
used slot is processed / driver down).
  - before: msg-gate path left result = 0; seed = 0.
  - after:  per used slot `result = -1` before the msg check; seed = (int)hwnd.
The not-found (eax>=64) path is unreachable here (each slot's handle is its own table
entry, always found). Golden corrected: tests/unit/audio_leaves2_test.cpp
ReacquireAllDigitalDriversPostsPerSuccess (msg-gate 0 -> -1).

### FIXED — releaseSampleHandleSlot (mirror of @0x44a028)
The owning output's allocatedSampleCount decrement is UNCONDITIONAL in the binary
(`--*(dword_62EA1C[idx]+20)` at 44a079, no `>0` guard).
  - before: `if (allocatedSampleCount > 0) --allocatedSampleCount;`
  - after:  `--allocatedSampleCount;`  (matches 44a079)
Existing goldens unchanged (all release exactly the allocated count, so the value is
identical).

### BOUNDARY — Miles/Win32 hooks (MilesHooks, installed by ctor)
Rule-5 audio boundary. The terminal AIL_* / PostMessageA calls are not reconstructed
math; they route through installable hooks with inert success-shaped defaults:
  AIL_startup/shutdown (no-op), AIL_waveOutClose (no-op), AIL_close_stream (no-op),
  AIL_digital_handle_release (return 1 = ok), AIL_digital_handle_reacquire (return
  0 = ok), AIL_set_digital_master_volume (no-op), AIL_active_sample_count (return 0),
  PostMessageA (return 1 = posted), AIL_get_timer_highest_delay (return 0).
All surrounding integer bookkeeping over dword_62EA1C/EA5C/EA9C/EADC/EAE0 is 1:1.

### VERIFIED-1:1
- startupMilesDriver @0x449840 — gate on dword_62EADC; AIL_startup; AIL_last_error
  discarded; set flag (-1); return 0 / -1.
- shutdownMilesDriver @0x449878 — order: CloseAllStreams, ReleaseAllSampleHandles,
  CloseAllDigitalOutputs, AIL_shutdown, clear flag.
- getTimerHighestDelay @0x44ad0c — passthrough.
- findOutputByHandle @0x44aa94 — slot0 checked separately (unrolled original), then
  1..15; index or -1.
- closeDigitalOutput @0x449b28 — find idx, AIL_waveOutClose, free slot arrays + descriptor,
  clear table slot; return 0 / -1 (frees are the internal allocator, modelled by reset).
- closeAllDigitalOutputs @0x449bdc — close every used; `if(... == -1) v0=-1`.
- releaseDigitalDriver @0x449c20 — find idx; `if(AIL_digital_handle_release(*a1)) 0 else -1`.
- releaseAllDigitalDrivers @0x449c80 — `if(Release...) v0=-1` (non-zero == -1 == failure).
- closeStream @0x44a5f0 — LookupStreamDriver/HandleIndex; AIL_close_stream;
  `--*(+0x14)` (unconditional); clear stream slot.
- closeAllStreams @0x44a6d8 — loop slots [0, +0x18) of every used output.
- closeDriverStreams @0x44a64c — find idx; `if(*(a1+24)>0)` loop [0, capacity).
- releaseDriverSampleHandles @0x44a084 — find idx; loop [0, +0x18);
  `if(ReleaseSampleHandle(h) == -1) v2=-1`.
- releaseAllSampleHandles @0x44a110 — same over every used output.
- getActiveSampleCount @0x449fcc — find idx; `*a2 = AIL_active_sample_count(*a1)` (hook).

## Notes / evidence anchors
- ConvertX @0x5c6b08: sets x87 RC to chop (HIBYTE(cw)=0x1F), frndint, restores cw —
  TRUNCATE toward zero. Every fistp in ApplyVolumeSettings is preceded by it.
- flt_62522C = 0x3C010204 (~0.0078740157), flt_625230 = 0x3E800000 (0.25) — get_global_value.
- dword_62EADC static image value 0; runtime -1 after StartupMilesDriver (relevant to
  reacquireAll's `result = dword_62EADC` = -1).
