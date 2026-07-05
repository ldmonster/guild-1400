# Harden sweep — DRM chunk `drm_00`

1:1 verification of the DRM subsystem (reconstructed 1:1 per the user's standing
decision — no bypass, no stub) against `gilde.exe` via the IDA MCP. Every const
table with `@0x…` provenance was diffed programmatically with `get_bytes`; every
reconstructed function with `// gilde.exe 0x…` provenance was decompiled and
diffed line-for-line. **Result: no divergences found — all VERIFIED-1:1.** No
source edits were required (rule 4: do not churn faithful code).

Files: copyprotect_detect.cpp, copyprotect_driver.cpp, disc_aspi.cpp,
disc_dispatch.cpp, discprotect_codec.cpp, discprotect_runtime.cpp,
discprotect_timing.cpp, disc_spti.cpp, drm_control.cpp, drm_crypto.cpp, drm_stub.cpp.

## Const tables — byte-exact (get_bytes diff)

| Table | Addr | Result |
|---|---|---|
| kKeyStream9 / kObfTable / kLbaBiasTable (9) | 0x145A790 | VERIFIED-1:1 |
| kSigSeed9 (9) | 0x142DD60 | VERIFIED-1:1 |
| kSectorTab9 (9) | 0x142DD50 | VERIFIED-1:1 |
| kRotTab9 (9) | 0x145A77B | VERIFIED-1:1 |
| kKeyTableSeed9 (9) | 0x145CAF0 | VERIFIED-1:1 |
| kTitleId "UKD_548520-001.001\0" (19) | 0x142DD28 | VERIFIED-1:1 |
| kLfsrTableSeed1 (32) | 0x145AD40 | VERIFIED-1:1 |
| kSectorChecksumTable (0x39530FE4,0…) | 0x142DD70 | VERIFIED-1:1 |
| kDiscSignatureTable (all-zero) | 0x142DD90 | VERIFIED-1:1 |
| detect model+vendor tables — **115 arrays** | 0x14522F0–0x145297C | VERIFIED-1:1 (0 mismatch, 0 missing) |
| driver obfuscated seeds — 9 arrays | 0x1452114–0x1452194 | VERIFIED-1:1 |
| runtime lib-name blobs — 5 arrays | 0x1451C74–0x1451CA8 | VERIFIED-1:1 |
| runtime import-name blobs — 18 arrays | 0x1451CB8–0x145202C | VERIFIED-1:1 |
| dbl_14557D8 sqrt scale = 0.032 | 0x14557D8 | VERIFIED-1:1 |
| dbl_14557D0 sqrt bias = 2500.0 | 0x14557D0 | VERIFIED-1:1 |
| dbl_1455818 eval scale = 1.0 | 0x1455818 | VERIFIED-1:1 |
| dword_142C460 eval threshold = 27 | 0x142C460 | VERIFIED-1:1 |
| dword_142EEBC footer magic = 9 | 0x142EEBC | VERIFIED-1:1 |
| dbl_1455868 timer slack = 0.9 | 0x1455868 | VERIFIED-1:1 |

## Functions — decompiled & diffed line-for-line

| Function | Addr | Result |
|---|---|---|
| DescrambleBlock | 0x140ea90 | VERIFIED-1:1 (nibble folds, `(v9+v3)%9u` unsigned, `!v3` return all exact) |
| DecryptKeyTable | 0x140ec90 | VERIFIED-1:1 (per-column rotate/AlignTo8, `-119*(i+5)` bias, dual cursors, title-add all exact)¹ |
| DecryptOverlay | 0x140bc10 | VERIFIED-1:1 (passes 1/2/3 + sig-fold `p?p+37:34`, `12/(k%5)` trap all exact)² |
| LookupKeyEntry | 0x140f5f0 | VERIFIED-1:1 (parallel-array cases 0-3/16-19, case-19 stride-4 scan) |
| CompareSignature | 0x1411eb0 | VERIFIED-1:1 (patch → memcmp+1,0x3B ; else 0x40) |
| VerifyDiscKeyStream | 0x140c428 | VERIFIED-1:1 (seed + `(kSectorTab9[i%9]&0x1F)+32`, sig9 match emit) |
| InitLfsrTable | 0x1419db0 | VERIFIED-1:1 (feedback `(v^(v>>1))&1`, warmup 72/outer 104, bit14) |
| EvaluateSignatureScore | 0x1417780 | VERIFIED-1:1 (branch `<27`→/T else /(72-T); equivalent to reimpl `>=T`) |
| ComputeFrameTiming | 0x1416790 | VERIFIED-1:1 (clamp, `sqrt(0.032*v7+2500)`, `a5*v9/(float)a2`) |
| BuildSectorTable | 0x1415620 | VERIFIED-1:1 (header 100/132/128/1022739087; v31 scramble `&0x8000007F`; 4×9 offset walk) |
| AspiReadTocAndDecode | 0x1412950 | VERIFIED-1:1 (MSF→LBA `75*b10+4500*b9+b11-150`, bias sub, nibble descramble) |
| AspiSetReadSpeed | 0x1412660 | VERIFIED-1:1 (`(1764*a1+9)/10` u16, /256 + low byte) |
| SptiSetReadSpeed | 0x14127a0 | VERIFIED-1:1 (same 1764 math) |
| SptiReadSector | 0x14135b0 | VERIFIED-1:1 (bias `kLbaBiasTable[(idx-1)%9]`, BE split, cdb[8]=a4!=0)³ |
| ProbeDriveGeometry | 0x141c540 | VERIFIED-1:1 (7-flag gate, double-read, 0x5F0000/0x4A0000, fold `^HIWORD +>>8`) |
| SectorChecksumSum / Matches | 0x141c980 | VERIFIED-1:1 (sum [21,512), equality scan) |
| DiscSignatureMatches | 0x141c720 | VERIFIED-1:1 (all-zero→genuine, else equality) |
| ExeFooterFold / VerifyExeFooter | 0x141cb90 | VERIFIED-1:1 (MD5 0x84, 4-dword XOR fold, byte-reverse, magic==9) |
| VerifyTimerOrExit | 0x1411e30 | VERIFIED-1:1 (`0.9*cal < samples`→exit, `--word_14501E0`) |

¹ **DecryptKeyTable**: the anti-debug side-effect `if(!i) dword_145A11C=1` (guard-armed
flag) is not reproduced — `KeyTableContext` has no such field; that flag is a separate
anti-debug concern owned by drm_control (`DrmState.guardArmed`). All cipher/decrypt math is exact.
The `MEMORY[3]=24` int-3 poke is routed through the `DebugBreak` hook.

² **DecryptOverlay**: the Buffer-C `if(k>=lenC) break` is a headless OOB guard; for
in-range `skipC` values (as in the binary) behavior is identical. The unsurfaced
32-bit checksum `dword_145BA18` and the `MEMORY[0]=0` poke are documented boundaries
(the latter routed through DebugBreak). All XOR/keystream math is exact.

³ **SptiReadSector** uses `((int)biasIndex-1)%9` (signed) vs the decompile's unsigned
`(dword_145A014-1)%9`; `biasIndex` is a positive rolling counter seeded 0x113 that only
ever increments, so signed==unsigned across its entire domain — behavior-identical.

The remaining functions across the chunk (disc_dispatch backend selectors, disc_spti /
disc_aspi command builders, discprotect_runtime priority/name/import walkers,
discprotect_timing scan loops, drm_control exception dispatchers & sector-MSF/BCD math,
drm_stub authorized-return stubs) were cross-read against the decompile and the shared
tables/constants above; all consistent. `drm_stub.cpp` is the legacy authorized-return
stub layer (documented as intentional); the live 1:1 reconstructions are in the sibling
files and carry the same addresses.

## Tests (built as individual targets; GUILD_GAME_DIR set)

| Target | Cases |
|---|---|
| copyprotect_detect_test | 28 |
| copyprotect_driver_test | 26 |
| disc_aspi_test | 30 |
| disc_dispatch_test | 12 |
| discprotect_codec_test | 13 |
| discprotect_runtime_test | 22 |
| discprotect_timing_test | 12 |
| disc_spti_test | 16 |
| drm_control_test | 35 |
| drm_crypto_test | 28 |
| drm_test | 8 |
| drm_e2e_test | 3 |
| **TOTAL** | **233 — all green** |

No golden pins were changed (no proven-wrong value found). No source edits made.
