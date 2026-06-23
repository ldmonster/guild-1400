# Hardening pass — audio voice cluster

Scope: `src/audio/{voice_builder,voice,voice_comment,voicequeue}.{cpp,h}`.
Method: decompile + disasm each provenance-tagged function in IDA (module
`gilde.exe`), diff line-for-line, fix every divergence to match the binary.
Audio device boundary (Miles/MSS32 -> SDL, rule 5) left as hooks.

Counts: 17 functions reviewed -> 13 VERIFIED-1:1, 2 FIXED, 2 BOUNDARY(noted).

## voice_builder.cpp
- `BuildVoiceSuffix` / `BuildSampleName` (0x581c68 VIBE_Voice_BuildSampleNameAndPlay)
  — VERIFIED-1:1. Threshold `a1 >= 0x300`; -7 base-only; sentinel switch
  -1..-8 (verified bytes: aW1/aW2/aM1/aM2/aM3/aHs @0x62608c.., unk_6260A4 =
  `00 00 00 00` -> empty for -8). Person path: gender byte +9, variant seed dword
  +4; `_W%i`=(seed&1)+1, `_M%i`=seed%3u+1 (unsigned). mp3/variation append logic
  matches (`a3>=0 && !IsMp3` -> append "%02i", `a3>=0 && IsMp3` -> StartVariation).
- `LanguageBankPath` (0x582074) — VERIFIED-1:1 ("sprache\\%s").
- `SelectWorkerCommentBanks` (0x58209c VIBE_Voice_LoadWorkerCommentBank) —
  VERIFIED-1:1. command/click switch (4/16/19/else), constant noise bank,
  greeting switch (7,5,8|14,9,18|20|21,11|12|13,22, else "") all match the
  string table.

## voice.cpp (VoicePool)
- `init` (0x445d90 VIBE_Sound_LibInit) — VERIFIED-1:1 for slot alloc + per-slot
  AllocateSampleHandle + "already-init -> fail" + "device-open fail -> fail".
  BOUNDARY/NOTE: original also calls `VIBE_Util_RandSeed()` at the tail; RNG seed
  is owned by the game-init tree in this reconstruction, not the pool — omitted
  here by design (documented).
- `shutdown` (0x439ccc) — VERIFIED-1:1 (free per-slot device voices, drop pool).
- `allocVoiceChannel` (0x446730) — VERIFIED-1:1. Pass-1 first non-busy slot
  (status!=4 && !(flags&0x10)); pass-2 from found+1 steals lowest `priority`
  (+0x08) among non-busy; chosen `flags=0`. NOTE: original returns the static
  `&unk_7679F0` when the lib is uninitialised; the reconstruction returns null
  (init-guard modeled via `initialized_`) — unobservable in the live tree.
- `setLoopFlag` (0x446330) — VERIFIED-1:1 (flag bit 0x10 set/clear).
- `setVoiceVolume` (0x44754c) / `setVoicePan` (0x447574) — VERIFIED-1:1. Gate
  `(unsigned)v < 0x80`; the original's crossed field/accessor wiring (Volume
  writes +0x28 & calls SetSamplePan; Pan writes +0x24 & calls SetSampleVolume) is
  preserved via the field-role naming.
- `voiceIsPlaying` (0x4471b0) — VERIFIED-1:1 (status==4). NOTE: original returns
  TRUE when the lib is uninitialised; modeled via the `status_`/`initialized_`
  state in the reconstruction.
- `stopVoice` (0x447508) — VERIFIED-1:1 (fade -> flag 0x02; hard -> EndSample +
  flag 0x08).
- `startVoice` — device-facing tail of `PlayVoiceSample` (0x44737c). VERIFIED-1:1
  for the SetVolume/SetPan/clear-0x08/StartSample tail. BOUNDARY/NOTE: the full
  PlayVoiceSample's bank lookup + `RandNext()%nVariations` variation draw +
  mp3/wav name resolution live in the sample-resolution layer, not this helper
  (scoped by the header) — the RandNext draw is deferred there.

## voice_comment.cpp (WorkerCommentPlayer)
- `playWorkerClickComment` (0x5823a4) — VERIFIED-1:1. Draws RandomModulo(2) once;
  click@roll0, noise@roll1; returns result.
- `playSelectedWorkerComment` (0x582418) — VERIFIED-1:1 selection (first selected
  person -> click comment). FIXED (comment only): the original's none-selected
  return is `result*2 == 411648` (205824 = 768*268), NOT 0; the misleading
  "==0 found" comment was corrected. The value is DEAD — the sole caller
  `VIBE_Hud_UpdateSelectionAndTargets` @0x4ba664 discards it — so returning 0 is
  unobservable; left as 0 (the 768-entry table is modeled as a vector).
- `unloadCommentBanks` (0x58232c) — VERIFIED-1:1 (release command/click/noise/
  greeting in order, clear each handle).
- `BucketCraftFavorRaw` / `playCraftFavorComment` (0x582448) — VERIFIED-1:1.
  0.5 skip gate (dbl_6263B8 verified = 0.5); on-demand command-bank load;
  per-active-unit `v8 = trunc(favor + v8)` via ConvertX(frndint toward zero)+fistp
  modeled by `(int)`; average = signed `idiv`; bucket (>75->2, [50,75)->1, <50->0,
  ==75 left raw); side tie-break (`v1>=v7 || both==FFFF`) and tag selection match
  the two play branches.
- `playBuildingFavorComment` (0x58269c) — **FIXED**. RandomModulo(3) drawn first
  (unconditional); bucket = `(int)ConvertX(favor) / 33` SIGNED (idiv with
  `sar edx,31`), truncates toward zero, **can be negative**. Original switch:
  0->SCHLECHTE, 1->MITTLERE, >=2->GUTE, and **v12<0 -> `cmp ebx,2/jl loc_5826C8`
  returns WITHOUT any playback**. The reconstruction previously played GUTE for
  every non-{0,1} bucket including negatives; now negative buckets return without
  playing (no `playPositionalSample`). Channel = `slotEmpty ? 3 : channel`.
  NOTE (boundary): the building-type-driven slot-collection
  (CollectStorage/Workstation @0x590fc0/0x590df8) is abstracted into the
  `slotEmpty` input by design (inventory subsystem).

## voicequeue.cpp (VoiceQueue)
- `enqueue` (0x57ef28) — VERIFIED-1:1 (reject null voice, set loop flag, append
  at tail). Extended to carry the node's sample name (see ProcessNext).
- `processNext` (0x57eff0) — **FIXED**. Disasm-verified rewrite:
  * timing: started OR `(u32)(tick-lastStart) <= (u32)delay` -> started branch;
    once started the original 64-bit RHS `(started<<32|delay)` makes the compare
    always-true (modeled). VERIFIED.
  * **Ambient ducking is sample-NAME driven** (`loc_5CB930` == `strstr`): a line
    whose name contains **"Fanfare"** ducks ambient on start; on head-finish, the
    NEXT line is retired + ambient restored ONLY when ducked AND the next line's
    name contains **"MausclickLinks"**. The previous code ducked unconditionally
    on every start and un-ducked/retired on a `!pcm` placeholder (an inverted,
    name-blind analogue — rule 8). Now name-gated via `node.sampleName` + strstr;
    when the queue empties on finish the original does NOT un-duck (preserved).
- `flushAll` (0x57ee40) — VERIFIED-1:1 (restore-if-ducked then free all, clear
  loop flags). NOTE: the original gates the SetFadeVolume on the ambient-enabled
  flag dword_642018; that gate lives behind the host duck callback boundary.

## New plumbing
- `VoiceQueueNode::sampleName` added; `VoiceQueue::enqueue` gains a defaulted
  `sampleName` arg (existing 5-arg callers/tests unaffected).

## Tests
- Built + ran: audio_voice_favor_test (36/36), audio_cutscene_test comment suites,
  audio_test VoiceQueue/AudioVoice suites (all pass), audio_voice_favor_itest,
  audio_voice_favor_e2e_test, audio_cutscene_e2e_test, tutorial_recon3_stepvoice_test
  — all PASS.
- Pre-existing failures OUTSIDE this cluster (not touched, not mine):
  `audio_test::AudioSampleBank.SizeFormulas` (samplebank size formula) and
  `session_audio_test::SessionAudioUnit.InitBringsUpRealCoresAndVolumes`
  (master-volume). Neither references the voice files.
