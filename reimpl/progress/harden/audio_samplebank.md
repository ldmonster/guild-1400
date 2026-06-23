# Harden — audio sample-bank slice (1:1 binary diff)

Module: `gilde.exe` (imagebase 0x400000). MCP IDA Pro live.

Files hardened:
- `src/audio/samplebank.cpp` / `.h`            (OOP facade)
- `src/audio/samplebank_load.cpp` / `.h`       (.sbf binary index loader)
- `src/audio/audio_samplebank2.cpp` / `.h`     (sb2: raw-record leaves)
- `src/audio/audio_samplebank3.cpp` / `.h`     (sb3: .txt parser/serializer + tree + player)

Tests: `audio_samplebank3_test`, `audio_samplebank2_test`, `audio_loaders_test`
(+ touched goldens in `audio_samplebank2_itest`, `audio_test` — same functions).

Status: **all 8 audio_samplebank/loaders ctest targets pass; full 40-test audio
sweep passes.**

---

## samplebank.cpp (facade — provenance maps to the sb2/sb3 raw-record fns)

| fn | addr | verdict |
|----|------|---------|
| findSampleByName            | 0x447704 | VERIFIED-1:1 (linear StrCmp over +56/+260 list) |
| findVariationByName         | 0x44773c | VERIFIED-1:1 (+60/+60 chain) |
| findSampleInVariation       | 0x447888 | VERIFIED-1:1 (var+56 / +260 walk) |
| getLastSample               | 0x4476dc | VERIFIED-1:1 (walk +260 to tail) |
| computeVariationSize        | 0x4478d4 | VERIFIED-1:1 — `12*CountSamples(var) + var->size`; facade maps `var->size` to the `variationBaseSize` param (model note) |
| **computeTotalSize**        | 0x447614 | **FIXED** |

### FIXED — computeTotalSize @0x447614
Disasm-exact formula is `12*(S+C) + 12*V + (S+V)<<6 + 324 + bankSize`
(S=top samples, V=variations, C=CountSamples(0)=sum of variation samples).
The earlier reconstruction was `12*C + 12*S + (S+V)<<6 + 324 + base` — it
**dropped the `+12*V` term** produced by `lea eax,[edx*4]; sub eax,edx; shl eax,2`
at 0x44767c..0x447685 (edx=V). Restored. Golden `tests/unit/audio_test.cpp`
(`SizeFormulas`, NOT in the assigned test list but the only consumer of this fn)
encoded the same wrong formula → fixed the golden too (cite 0x447614 / 0x44767c).

---

## samplebank_load.cpp (.sbf binary index)

| fn | addr | verdict |
|----|------|---------|
| LoadSampleBankFromBuffer | 0x446b2c | VERIFIED-1:1 — header 0x144 (name@0/50, count@0x134), entries@bank+324 stride 0x40, name@+4, fmt@+54; short-read fails. Original I/O via VIBE_File_Read = BOUNDARY (buffer-fed here). |
| FindSampleInBank         | 0x446288 | VERIFIED-1:1 — StrCmpNoCaseN(entry+4, name, 50), stride 64, base bank+312. |
| SampleIsMp3              | 0x4471f0 | VERIFIED-1:1 — audio-uninit→1; else entry must exist AND fmt(+54)==2. |

---

## audio_samplebank2.cpp (raw-record leaves)

| fn | addr | verdict |
|----|------|---------|
| **ExtractFileExtension**  | 0x447770 | **FIXED** |
| ClassifyAudioFormat       | 0x4477c8 | VERIFIED-1:1 — StrStr(".wav"/".WAV")→1, (".mp3"/".MP3")→2, case-sensitive. |
| ValidateSampleFile        | 0x447830 | VERIFIED-1:1 — open/read-once/close; File I/O = BOUNDARY (hook). |
| GetDirtyCount             | 0x4493f8 | VERIFIED-1:1 |
| CountSamples              | 0x4481ec | VERIFIED-1:1 — null→sum all variations (recurse); else walk var+56/+260. |
| HasSamples                | 0x44823c | VERIFIED-1:1* |
| VariationHasSamples       | 0x448290 | VERIFIED-1:1* |
| HasVariations             | 0x4482c8 | VERIFIED-1:1* |
| IsValid                   | 0x448318 | VERIFIED-1:1* (returns 0 = intended success; see note) |
| AddVariation              | 0x4479dc | VERIFIED-1:1 — dup reject, append to +60 tail, zero-fill 0x40, StrNCopyPad 50, ++dirty. |
| SetVariationName          | 0x447b5c | VERIFIED-1:1 (behavioral) — walk var sample list, StrCmp oldName, byte-copy newName over name; the binary's clobbered-ecx return alias is modeled at the Record* level. |
| ClearVariationSamples     | 0x447cb0 | VERIFIED-1:1 — named unlink (sub +256 from var+52 & bank+52, ++dirty) / null = free all. |
| FindFreeAmbientSlot       | 0x43a898 | VERIFIED-1:1 — 10 slots stride 296, !busy && active. |
| FindFreeVoiceSlot         | 0x43a8c4 | VERIFIED-1:1 — 2-pass, stride 74, count 10. |
| SetRange                  | 0x424cb0 | VERIFIED-1:1 — entry+0x40 = range. |
| DetachIfValid             | 0x4248c8 | VERIFIED-1:1 — handle!=0 → DetachEntry (hook). |

\* The Has*/IsValid success returns are `mov eax, ecx` where ecx was clobbered by
StrNCopyPad (cl = last copied byte). For Has*/VariationHasSamples ecx held the
head pointer (high bytes non-zero) → always truthy → reconstruction returns 1
(faithful). For IsValid, ecx was xor-zeroed and only cl is touched → the binary
returns the last name byte; the clear design intent is `return 0` on success
(the no-bank path explicitly loads -1), so the reconstruction returns 0. No
in-binary callers exist to disambiguate. Documented.

### FIXED — ExtractFileExtension @0x447770
Despite the recovered name, the binary extracts the **BASE FILENAME** (bytes
between the last `\` and the trailing `.`), **not** the extension:
- `v7 = StrChr(path,'\\')` (last `\`, strrchr-semantics — StrChr@0x5d3ef0 returns
  the LAST match), `v8 = StrChr(v7,'.')`.
- `v10 = v8 - v7 - 1` (0x4477b1 `sub ebx,edx`), copy from `v7+1` for `v10` bytes
  (0x4477b9), cap check `v10 >= a3` signed (`jge` 0x4477b5).

Earlier reconstruction copied `dot+1` for `strlen(dot+1)` (the extension) — wrong
source pointer AND length. Fixed. Goldens `audio_samplebank2_test`
(`Sb2ExtractExt`) and `audio_samplebank2_itest` (`ClassifyDrivesFormatPipeline`)
both encoded the extension result → rewritten to the basename result with
disasm citations.

---

## audio_samplebank3.cpp (.txt parser/serializer + tree + player)

| fn | addr | verdict |
|----|------|---------|
| GetSampleFileSize        | 0x4475d4 | VERIFIED-1:1 — open/seek-end/len; File I/O = BOUNDARY (hook). |
| Destroy                  | 0x4493b4 | VERIFIED-1:1 — RemoveVariation(0); RemoveSample(0); free bank; clear dirty. |
| RemoveVariation          | 0x447bf4 | VERIFIED-1:1 — null = loop removing head (++dirty/iter); named = clear samples then unlink+free (++dirty). |
| RemoveSample             | 0x4480cc | VERIFIED-1:1 — named unlink (sub +256 from bank+52) / null = free all. |
| AddSample                | 0x447f44 | VERIFIED-1:1 — dup reject, alloc 0x108, StrNCopyPad 256, size@+256, bank+52 +=, ++dirty. |
| AddSampleToVariation     | 0x447da0 | VERIFIED-1:1 — var+52 (idx13) AND bank+52 both accumulate; sampleHead@+56 (idx14), next@+260 (idx65). |
| **LoadFromText**         | 0x448958 | **FIXED** (`~`→space decode) |
| **SaveToText**           | 0x448ed0 | **FIXED** (space→`~` encode, in place) |
| SaveBinary               | 0x449058 | BOUNDARY — ComputeTotalSize() size + Vfs_WriteBuffered of dword_62E8F8 blob; modeled via hooks/compiled-blob globals (no Miles/VFS escape). |
| ResolveSamplePaths       | 0x449580 | VERIFIED-1:1 — bank+52=0, recompute +256 per sample via %lang rewrite, accumulate var+52 & bank+52, first 0-size aborts -1. |
| **PlaySample**           | 0x4490e4 | **FIXED** (RNG-driven variation pick) |

### FIXED — LoadFromText / SaveToText `~`↔space token codec
`loc_5CB930` is `VIBE_Util_StrStr`. The .txt format encodes spaces in names as
`~` (scanf "%s" cannot read spaces). The originals do in-place StrStr-scan
substitutions the reconstruction had omitted:
- LoadFromText: after each token read, `for(i=StrStr(tok,"~");i;...) *i=' '`
  (0x448b25 bank, 0x448ab9 sample, 0x448c92 var, 0x448d5e var-sample) — decode
  BEFORE the `%lang` rewrite / store.
- SaveToText: before each `"%s\n"` write, `for(i=StrStr(nm," ");i;...) *i='~'`
  (0x448f18 bank, 0x448f62 sample, 0x448fc3 var, 0x448ff5 var-sample) — encodes
  IN PLACE, mutating the record's name buffer (observable side effect).
Added `DecodeTildeToSpace` / `EncodeSpaceToTilde`. Names without spaces/`~`
round-trip identically (so existing goldens are unaffected).

### FIXED — PlaySample @0x4490e4 variation pick is RNG, not a caller index
Disasm order 0x449333..0x44935e: `r = VIBE_Util_RandNext()`
(= `crt::RandNext`, the 1103515245 LCG returning `(state>>16)&0x7FFF`), then
`cnt = CountSamples(name)`, then `idiv cnt` (`idx = r % cnt`, signed), then
`FindVariationByName`, then walk `var->sampleHead` decrementing `idx` to -1 and
recurse with that (dotted) sample's name. The original takes the name in BOTH
eax+edx and has **no index parameter** — selection is purely random.
The reconstruction had a fictional `index` parameter driving the pick.
- Now draws exactly one `crt::RandNext()` (state advances once), picks
  `r % cnt`, recurses. `index` retained as a vestigial, ignored arg.
- The binary does `idiv cnt` BEFORE the var-existence check, so an unknown
  variation (cnt==0) is a divide-by-zero crash in the original; callers never
  pass one. The reconstruction returns -1 instead of crashing (RNG already
  drawn, so RNG order/state is preserved). Documented.
Golden `audio_samplebank3_test` (renamed `PlaySampleVariationPicksRandom`):
seeds `crt::Srand`, asserts the LCG advances by exactly one step per call, and
that the random pick still resolves to 0.

---

## Counts
- Functions with provenance reviewed: **35**
- VERIFIED-1:1: **29**  (incl. 4 with documented clobbered-return / model notes)
- FIXED: **5** — computeTotalSize (0x447614), ExtractFileExtension (0x447770),
  LoadFromText (0x448958), SaveToText (0x448ed0), PlaySample (0x4490e4)
- BOUNDARY (genuine OS/VFS/Miles hooks, logic verified): **SaveBinary (0x449058)**
  plus the I/O leaves GetSampleFileSize/ValidateSampleFile/LoadSampleBank reads.
- Goldens fixed: 4 (audio_samplebank2_test, audio_samplebank3_test,
  audio_samplebank2_itest, audio_test) — all cite addr + evidence.

## Test status
`cd build && GUILD_GAME_DIR=.../europe_guild_1400_original ctest -R
"audio_samplebank|audio_loaders"` → **8/8 pass**. Broader `-R audio` → **40/40 pass**.
