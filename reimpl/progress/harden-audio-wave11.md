# Wave-11 hardening — audio cluster (`src/audio/**`)

Agent **W11-AUDIO**. MCP was DOWN → hardening only, NO new 1:1 reconstruction.
Built every owned test target with `-fsanitize=address,undefined
-fno-sanitize-recover=all`, added malformed/oversized/empty/index-out-of-range
edge tests, and fixed the OOB found. Golden values and the valid-input paths are
unchanged.

## Build / run

```
cmake -S . -B build-w11audio -DCMAKE_BUILD_TYPE=Debug -DGUILD_BACKEND=OFF \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -g"
cmake --build build-w11audio --target \
  audio_loaders_test audio_samplebank2_test audio_samplebank3_test \
  audio_recon_engine_test audio_tick_test audio_voice_favor_test audio_test \
  audio_leaves_test audio_leaves2_test audio_music_world_test \
  audio_cutscene_test audio_recon_dispatch_test -j$(nproc)
```

All 12 cluster test targets **PASS** under ASAN+UBSAN. The normal `build/` (no
sanitizers) stays green for the modified files too.

## Fixes (real OOB — faithful: the original's arrays were exactly sized; a
## malformed/inconsistent record must fail safe, not run off the buffer)

### 1. `src/audio/audio_leaves.cpp` — heap-buffer-overflow in the slot scans
`LookupStreamHandleIndex`, `LookupSampleDriverIndex`, `LookupStreamDriverIndex`
all scanned `streamSlots[idx]` / `sampleSlots[s]` for `idx/s` in
`[0, maxSampleHandles)`, indexing the `std::vector` purely by the capacity field.
A `DigitalOutput` whose `maxSampleHandles` exceeds its actual slot-vector size
(an inconsistent / hand-built / malformed record) drove a heap read past the
allocation. **Confirmed**: the new `AudioLeaves.LookupClampsToActualSlotSize`
test trips ASAN `heap-buffer-overflow READ of size 4 ... LookupStreamHandleIndex`
on the pre-fix code.

Fix: clamp the scan to `min(slots.size(), maxSampleHandles)`. Well-formed records
(the only ones `openDigitalOutput` produces — `sampleSlots.size() ==
streamSlots.size() == maxSampleHandles`) are unaffected, so all golden lookups
keep their exact indices. This is the same guard `audio_leaves2.cpp` already
applied to every slot access (`s < o.streamSlots.size() ? ... : 0`).

### 2. `src/audio/audio_samplebank3.cpp` — stack-buffer-overflow in `RewritePath`
`RewritePath` (used by `LoadFromText`, `ResolveSamplePaths`, `PlaySample`)
spliced `[prefix][root][tail]` into the caller's 256-byte `resolved` stack buffer
with no bound. A `%lang` token plus a long runtime `root` (or a long tail)
overran the destination. Also `StrNCopyPad(scratch, name, 256)` does not
NUL-terminate when `name` fills all 256 bytes, so the subsequent `StrStr`/`strlen`
could read past `scratch`. **Confirmed**: temporarily reverting the fix and
running `Sb3Unit.LoadFromTextLongRootRewriteStaysInBounds` trips ASAN
`stack-buffer-overflow ... RewritePath` (`audio_samplebank3.cpp:115`).

Fix: reserve one byte and NUL-terminate `scratch` (read guard), and bound every
write into `dst` to the 256-byte capacity (write guard). On well-formed tokens
(≤255 chars from the tokenizer, empty/short root) the output is byte-identical —
the no-token fast path still `memcpy`s 256 bytes unchanged; the splice path only
stops early when it would have overrun.

## Tests added (all exercise ASAN bounds; goldens untouched)

| File | New tests | Coverage |
|---|---|---|
| `audio_leaves_test.cpp` | `LookupClampsToActualSlotSize` | inconsistent capacity vs slot vector (fix #1) |
| `audio_samplebank3_test.cpp` | `LoadFromTextLongRootRewriteStaysInBounds`, `LoadFromTextMaxTokenRewriteStaysInBounds`, `ResolveSamplePathsLongRootStaysInBounds`, `PlaySampleLongRootStaysInBounds`, `LoadFromTextDegenerateTokens` | long-root/long-token `%lang` rewrite (fix #2), degenerate tokens |
| `audio_recon_engine_test.cpp` | `ZeroChannelsCountAndUpdate`, `FindVoiceBySampleIdxOutOfRange`, `MixerUpdateZeroActiveSlots`, `MixerUpdateFadeInExtremeTargetZeroLen`, `MasterVolumeFadeAtExactBoundary` | empty channel table, voice index past count, audio tick with 0 active voices, fade math at boundaries / extreme volume / zero fade-length, master-volume fade boundary |
| `audio_test.cpp` | `SlotAtIndexOutOfRange`, `ZeroVoicePoolIsSafe`, `ChannelPoolExhaustionRepeated`, `VolumePanIntegerExtremes`, `StartVoiceZeroLengthBuffer` | voice index out of range, 0-voice pool, channel-pool exhaustion (more sounds than channels), volume/pan at INT extremes, 0-length/null PCM buffer |
| `audio_music_world_test.cpp` | `EmptyPlaylistLookups`, `ManyTrackPlaylistScans` | music playlist with 0 tracks and with 256 tracks (incl. an empty id list) |

## Behavioral items — needs MCP (NOT changed; the engine's faulting envelope)

- **`AudioEngine::mixerUpdate` fade-in / fade-out division by `slot+280`
  (targetVol)** — `audio_recon_engine.cpp:490` (`v13 = v12 * clock / *(slot+280)`)
  and `:513` (fade-out `/ v17`). A degenerate `targetVol == 0` with a non-zero
  `fadeLen` divides by zero, which is what the original x86 `idiv` would also
  fault on — the engine's own envelope. Left as-is (a test deliberately uses a
  non-zero target so it does not abort under `-fno-sanitize-recover`). Confirming
  the original's pre-divide guard (if any) needs the decompile (MCP).
- **`ParseWavHeader` (`soundwave.cpp`) declared `dataSize` may exceed the buffer**
  — a truncated WAV whose `data` chunk advertises more PCM than is present still
  returns `ok=true` with `dataSize`/`dataOffset` pointing past the end. The parser
  itself performs no OOB read (it never touches the PCM), so there is no ASAN
  finding; clamping `dataSize` would change observable output on a malformed input
  (a 1:1 question — Miles would see the same declared size). Left as-is.
- **`sb2.ExtractFileExtension` (`audio_samplebank2.cpp`) missing-backslash path**
  — already documented in-source: the original would deref a null from
  `StrChr(0,'.')` on a path with no `\\`; the reconstruction already falls back to
  scanning the whole path (a safe, documented divergence on degenerate input).

## Not touched (already hardened / out of scope)

- `audio_leaves2.cpp` (`DriverManager`): every slot access is already bounded
  (`s < o.streamSlots.size() ? ... : 0`) and `outputs_` is a fixed
  `std::array<…,16>`. Clean under ASAN, no change needed.
- `digital_output.cpp`: slot vectors are always `assign(maxSampleHandles, 0)`, so
  the member scans are consistent; clean.
- `app/audio_tick.*` and `play/` bind-sites: NOT owned (other clusters / bind
  files). `audio_tick_test` runs clean as-is.
