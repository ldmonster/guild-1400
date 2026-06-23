# Hardening — audio_recon_engine.cpp

1:1 binary-diff pass over `src/audio/audio_recon_engine.cpp` against gilde.exe (IDA MCP).
Every function carrying a `gilde.exe 0xADDR` provenance comment was decompiled + disassembled
and diffed line-for-line (control flow, constants, signed/unsigned compares, float math,
field offsets, return values incl. boundary-leaf eax).

## Per-function results

| addr | function | status |
|------|----------|--------|
| 0x445ef8 | VIBE_Sound_SetMasterEnable / setMasterEnable | VERIFIED-1:1 |
| 0x44644c | VIBE_Sound_FindBankByName / findBankByName | VERIFIED-1:1 |
| 0x4464b0 | VIBE_Sound_FindVoiceBySample / findVoiceBySample | VERIFIED-1:1 |
| 0x446640 | VIBE_Sound_FindBankContainingVoice / findBankContainingVoice | VERIFIED-1:1 |
| 0x446800 | VIBE_Sound_GetLastBank / getLastBank | VERIFIED-1:1 |
| 0x446540 | VIBE_Sound_FindOldestActiveSample / findOldestActiveSample | VERIFIED-1:1 |
| 0x446694 | VIBE_Sound_FreeMemoryForLoad / freeMemoryForLoad | VERIFIED-1:1 |
| 0x447058 | VIBE_Audio_UnloadAllSampleBanks / unloadAllSampleBanks | VERIFIED-1:1 |
| 0x446d88 | VIBE_Sound_UpdatePlayback / updatePlayback | VERIFIED-1:1 |
| 0x4494a4 | VIBE_Audio_CountActiveVoices / countActiveVoices | VERIFIED-1:1 |
| 0x4475b8 | VIBE_Audio_SetVoiceLoopCount / setVoiceLoopCount | FIXED (return value) + BOUNDARY |
| 0x44727c | VIBE_Audio_RestartVoice / restartVoice | VERIFIED-1:1 |
| 0x4472c4 | VIBE_Audio_StartVoiceVariation / startVoiceVariation | VERIFIED-1:1 |
| 0x439d74 | VIBE_Sound_SetVoicePosition / setVoicePosition | VERIFIED-1:1 (CS bracketing = Win32->SDL boundary) |
| 0x43a3e4 | VIBE_Audio_MixerUpdate / mixerUpdate | FIXED (used-flag offset) + FIXED (fade-out unsigned cmp) |
| 0x445ea4 | VIBE_Sound_LibShutdown / libShutdown | VERIFIED-1:1 |
| 0x424c70 | VIBE_Sound3d_StopEntry / sound3dStopEntry | VERIFIED-1:1 |
| 0x424f00 | VIBE_Sound3d_SelectChannel / sound3dSelectChannel | VERIFIED-1:1 (math/branch) + BOUNDARY (PlaySample/VoiceIsPlaying/StrNCopyPad leaves) |
| 0x5e0db0 | VIBE_Util_StrCmpNoCaseN / vibeStrCmpNoCaseN | VERIFIED-1:1 |
| VIBE_Util_StrCmp | vibeStrCmp | VERIFIED-1:1 (model) |

Counts: **VERIFIED-1:1 = 17**, **FIXED = 2 functions (3 fixes)**, **BOUNDARY = 2 functions** (3d
crossfade leaves; SetSampleLoopCount eax). All boundary leaves are the rule-5 Miles/MSS → SDL
audio API; only the genuine backend call is left as a hook — the bank/voice/listener/crossfade
MATH is reconstructed 1:1.

## FIXED — details

### 0x43a3e4 VIBE_Audio_MixerUpdate — slot used-flag offset 4 → 260  (CRITICAL)
**Before:** `if (!u8at(st.trackSlots, i + 4)) continue;` with comment "byte_62DB34 == unk_62DA30 + 4".
**Evidence:** `byte_62DB34` = 0x62DB34, `unk_62DA30` = 0x62DA30 → delta = 0x104 = **260**, not 4.
Disasm `0x43a3fc: cmp ds:byte_62DB34[esi], 0; jz` is the per-slot iteration guard, reading
`*(slotBase + i + 260)` (the in-record "active" byte). Corroborated by `VIBE_Audio_FindFreeVoiceSlot`
(0x43a8c4): `byte_62DB34[i*4]` with i stride 74 (= 296-byte slot stride) and the sibling fields
`dword_62DB30` (=base+256, stream handle) / `byte_62DB34` (=base+260).
**After:** `if (!u8at(st.trackSlots, i + 260)) continue;` The original reconstruction read the
wrong byte (offset 4 lands inside the name[256] field) — every slot would have been treated as
unused unless name[4]!=0, completely breaking the mixer's slot scan.
**Tests fixed to match binary:** MixerUpdateFadeInComputesVolume / MixerUpdateFadeInCompletes /
MixerUpdateFadeInExtremeTargetZeroLen — `B(slots, 4) = 1` → `B(slots, 260) = 1`.

### 0x43a3e4 VIBE_Audio_MixerUpdate — fade-out v20 compare signed → unsigned
**Before:** `if ((int)v20 > 2 && ...)`.
**Evidence:** `0x43a6a7: cmp eax, 2; 0x43a6aa: jbe loc_43A6C0` — `jbe` is the **unsigned** below-or-equal
test (the continue/play branch requires `v20 > 2` unsigned). `v20` is `unsigned int` in the
decompile (`v20 = v17 * (v18-v19) / v18`, the `xor edx,edi`-cleared `div ebx` at 0x43a69d-0x43a69f).
The companion `0x43a6b8: cmp ebx,eax; jge` stays SIGNED (`v18 >= v19`) — left unchanged.
**After:** `if (v20 > 2u && ...)`. Differs for v20 > 0x7FFFFFFF (would be treated as ≤2 under the
old signed cast and snap volume to 0 instead of playing).

### 0x4475b8 VIBE_Audio_SetVoiceLoopCount — return value (was returning voice)
**Before:** live+nonzero path `return voice;` after calling the loop-count hook.
**Evidence:** `0x4475cb: return VIBE_Audio_SetSampleLoopCount();` — the function returns the leaf's
eax (0x44a3cc: returns 0 on success, **-1** when `!dword_62EADC` i.e. the digital driver is not
live), NOT the voice pointer. The voice pointer is only returned on the `!soundEnabled || !voice`
fall-through (`return result` = eax-in = voice).
**After:** live+nonzero path returns `(Addr)(i32)(-1)`. BOUNDARY: `VIBE_Audio_SetSampleLoopCount`
forwards to `AIL_set_sample_loop_count` (Miles/MSS, rule-5 → SDL). Its eax is conveyed via the
void `setSampleLoopCount` hook's side effect; the faithful not-live value is -1, returned here.
A live SDL backend reporting success would return 0 — wire through when the leaf is reconstructed.
(Hook kept `void` to avoid breaking the sibling `audio_recon_dispatch_test` build, which is not in
this agent's edit scope.)
**Test fixed to match binary:** SetVoiceLoopCountWritesField12AndHooks — live-path return assertion
`== v` → `== (Addr)(i32)-1`.

## Notable VERIFIED checks (non-obvious, confirmed against disasm)

- **MixerUpdate master-fade tail (0x43a47d):** `13*clock` via `(clock<<3)+(clock<<2)+clock`;
  `(13*clock - fadeStart) < fadeLen` is an **unsigned** compare (`jb`); interpolation divides by
  `(double)(unsigned)fadeLen` and multiplies `(double)v4`; cleared to `-1.0` (0xBF800000).
  `flt_62DA00 >= 0.0` gate confirmed (fldz/fcomp/ja). All match.
- **MixerUpdate fade-in (0x43a5a3..):** v13 = `(v12*clock)/slot+280` UNSIGNED (`div ebx`);
  the 64-bit branch-free abs `(HIDWORD^x)-HIDWORD` < 2 (abs64lo) and the embedded
  `slot+284 = slot+280*v14/v13` assignment order match. `slot+260/276 = 0` on close use `dl`
  which is provably 0 in that branch (`test dl,dl; jnz` guards entry).
- **FindOldestActiveSample (0x446540):** the `LOBYTE(v8)=v8|0x10; if(v8)` post-scan makes the
  `*(ch+4)=0` write unreachable; status==4 short-circuits to 0. Reproduced incl. dead write.
- **StrCmpNoCaseN (0x5e0db0):** returns normalized `v5-v6`; null-terminator break on the b-side
  normalized char is equivalent to the recon's a-side check since `la==lb` at that point.

## Test status
`cmake --build build --target audio_recon_engine_test` → OK.
`./build/audio_recon_engine_test` → **81 checks, 0 failures** (all suites pass).
Sibling `audio_recon_dispatch_test` still builds clean (header hook signature unchanged).
