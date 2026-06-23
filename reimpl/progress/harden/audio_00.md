# Harden sweep — chunk `audio_00` (src/audio/*.cpp, 20 files)

MCP-driven 1:1 verification of every `gilde.exe 0xADDR` provenance function in the audio
chunk. Each function was decompiled + disassembled and diffed line-for-line (control flow,
get_bytes constants, float->int truncate vs round, fixed-point sign, struct stride/offsets,
RNG draw count/order, return incl edx; disasm = reference of record). Fanned out to 6
parallel sub-agents; per-cluster detail in the sibling files listed below.

## Totals
- **VERIFIED-1:1: 120**
- **FIXED: 16** (real behavioral divergences corrected to the binary; goldens updated where they encoded wrong behavior)
- **BOUNDARY: rule-5 Miles/MSS32 + Win32 device calls** kept as hooks (math/branch structure around them verified 1:1)
- **MODELED: 2** (uninitialised-lib returns / per-category reset abstractions; exact values documented)

## Per-cluster results
| Cluster (files) | V | F | B | Detail file |
|---|---|---|---|---|
| audio_recon_engine | 17 | 2 (3 fixes) | 2 | progress/harden/audio_recon_engine.md |
| audio_leaves + audio_leaves2 | 25 | 4 | 1 layer | progress/harden/audio_leaves.md |
| samplebank3/2 + samplebank + samplebank_load | 29 | 5 | 1 | progress/harden/audio_samplebank.md |
| voice + voice_builder + voice_comment + voicequeue | 13 | 2 | 2 | progress/harden/audio_voice.md |
| sound3d + sound + soundwave + audio_recon_dispatch | 18 | 3 | several | progress/harden/audio_sound.md |
| music + music_world + music_cutscene + digital_output + ambient | 18 | 0 | 8 | progress/harden/audio_music.md |

## Notable fixes (addr — evidence)
- **0x43a3e4 MixerUpdate** — per-slot used-flag offset 4 → **260** (byte_62DB34 - unk_62DA30 = 0x104; disasm `cmp ds:byte_62DB34[esi],0`). CRITICAL: old offset landed inside name[256], breaking the whole mixer scan. Plus fade-out compare signed → unsigned (`jbe`).
- **0x56c148 ApplyVolumeSettings** — `v6` is **float32 not double** (`fstp dword`/`fmul dword` @56c16b/56c172): music vol 113→114, sfx 49→50 with shipped INI. Also Hex-Rays operand mislabel: master uses **musicByte**, soundByte only feeds v6.
- **0x447614 computeTotalSize** — dropped `+12*V` (variations) term; full formula `12*(S+C)+12*V+(S+V)<<6+324+bankSize` (`lea/sub/shl` @44767c). Fixed source + SizeFormulas golden.
- **0x4490e4 PlaySample (samplebank3)** — variation pick is **RNG-driven** (one `crt::RandNext` LCG draw, `r % CountSamples` signed idiv @449348 + recurse), not a caller index. Rewired to draw exactly one RandNext.
- **0x447770 ExtractFileExtension** — binary extracts **base filename** (between last `\` and trailing `.`, `v10=v8-v7-1`), not the extension. Wrong source + length fixed.
- **0x448958/0x448ed0 LoadFromText/SaveToText** — `~`<->space in-place StrStr substitutions were omitted; added.
- **0x5ca334 AngleBetween (sound3d)** — Rule-8 cheap-analogue: replaced full-3D acos with the binary's **signed XZ-plane angle** (Y zeroed, normalize, up-axis cross sign; `dbl_628CE8 = -2π`). +x→pan126, -x→pan0 (old gave 126 for both).
- **0x57eff0 processNext (voicequeue)** — Rule-8 cheap-analogue: ambient ducking is **name-gated** ("Fanfare" to duck, "MausclickLinks" to restore) via strstr @0x5CB930; was unconditional. Added `sampleName` to the node.
- **0x58269c playBuildingFavorComment** — favor bucket `(int)/33` is **signed** (`sar edx,31`); negative bucket must **return with no playback** (`cmp ebx,2/jl`); old code played GUTE for negatives.
- **0x4461d0 SoundSystem::playSample** — volume/pan device args were swapped (+0x24=127→vol, +0x28=63→pan).
- **0x424d40 InitSineTables** — step `flt_611588/(double)(u16)n` divided in **double** then stored float; was float/float.
- Return-value fixes: 0x4475b8 SetVoiceLoopCount (leaf eax not voice ptr), 0x449cc8 reacquireDigitalDriver (64 not 0), 0x449d2c reacquireAllDigitalDrivers (-1 not 0), 0x505ba8 AppendAmbientVoice (handle not count), 0x44a028 releaseSampleHandleSlot (unconditional decrement).

## Goldens corrected to the binary (cited in cluster files)
tests/unit/{audio_test, audio_leaves_test, audio_leaves2_test, audio_samplebank2_test,
audio_samplebank3_test, session_audio_test}.cpp; tests/integration/{audio_leaves2_itest,
audio_samplebank2_itest}.cpp; tests/e2e/audio_leaves_e2e_test.cpp; tests/unit/audio_test.cpp
(SizeFormulas, AngleBetween left/right pin).

## Build / test status
- All 20 audio translation units recompile cleanly (forced rebuild of `guild.dir/src/audio/*.o`: 20 objects, 0 errors).
- Each cluster built and ran its own test targets to green during the pass (see cluster files; recon_engine 81 checks, music/cutscene 24/24, leaves 7/7, voice 4/4, samplebank 8 targets, sound dispatch+audio_test pass).

## External blocker (NOT in this chunk — left untouched)
The full `guild` library currently fails to link due to a pre-existing error in
**`src/play/slice_council.h:89`** (and `dialog_council.h`): `sim::CommandPacket encode() const;`
is declared without including `src/sim/command.h` or forward-declaring `namespace sim`.
This file is in the `src/play/` chunk (council dialog), modified by another wave — it is
outside audio_00, so per the absolute chunk-isolation rule it was not edited here. Once the
council header is fixed by its owner, the consolidated audio test targets will link and run.
The audio code itself is verified clean and independently compilable.
