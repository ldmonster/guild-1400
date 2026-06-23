# Hardening — sim/history_parse + sim/illness (1:1 verification)

Scope: line-for-line diff of `src/sim/history_parse.cpp` and `src/sim/illness.cpp`
against the gilde.exe originals (decompile + disasm; tables via get_bytes).
MCP module gilde.exe, imagebase 0x400000.

## Tables verified byte-exact (get_bytes)

| Table | Addr | Result |
| --- | --- | --- |
| funcs_4FD5D2 (15 ptrs, 4-byte stride) | 0x6343d8 | VERIFIED-1:1 — matches kHistoryResolverTable slot targets exactly (0x4f8fac,0x4f90e0,0x4f9238,0x4f9518,0x4f989c,0x4f9c20,0x4f9f74,0x4fa178,0x4fa290,0x4fa3bc,0x4fa50c,0x4fa818,0x4faab8,0x4fac80,0x4faf54) |
| dword_6343B8 (prefix table) | 0x6343b8 | VERIFIED — `_NEW\0_USE\0_REL\0_SET\0_USE\0` 5-byte stride; only first 3 used |
| aBuergermeister_3 (role names) | 0x633ff8 | VERIFIED — "BUERGERMEISTER" at [0], 64-byte stride |
| unk_647728 (disease events, 10×12B) | 0x647728 | VERIFIED-1:1 — id/scale/count all match (scale floats: 0,40,15,20,10,0,8,5,3,12) |
| dword_5830C0 (free-table init) | 0x5830c0 | VERIFIED — 8 dwords all zero (source inits freeTable={0}) |

## history_parse.cpp — VIBE_History_ParseContext @0x4fd44c

VERDICT: VERIFIED-1:1 (no code change; resolver-arg note below).

Diffed every branch against disasm:
- Prefix match: original is a 4-byte integer compare `*(dword*)token == dword_6343B8[5*i]`,
  loop i<3, no break, last match wins. Source `strncmp(token,prefix,4)==0` is equivalent
  for the 4-byte non-NUL prefixes "_NEW"/"_USE"/"_REL". VERIFIED.
- Slot extraction: `buf[0]=token[5]; slot=ParseInt(buf); tail=token+6`. Gate
  `slot<0 || slot>=8` is the SIGNED `jl`/`jge` pair @0x4fd510/0x4fd519. VERIFIED.
- mode==1 (_USE): `repl=(i8)params[8*slot+8]` (disasm @0x4fd659 shl3+add+[+8]). VERIFIED.
- Role scan: pointer = tail+1 (`inc ebp`), 15 entries 64-byte stride, memcmp over
  strlen(name); on hit `tail=p+len`, `repl=i`, and `if(mode==0) params[8*slot+8]=i`
  (write-back guarded by `test ch,ch` = mode==0). VERIFIED.
- Unknown-replacement gate: `cmp [var_14],0Fh; jge` = SIGNED byte compare; `repl>=15`
  -> return 0. VERIFIED.
- Dispatch @0x4fd5d2: `funcs_4FD5D2[esi*4]` where esi=`sar(dword@0x10d,24)`=(i8)repl
  sign-extended; al=mode, edx=params, ecx=tail, ebx=slot, esi=repl, push out. Source
  calls `table[repl](mode,params,tail,slot,out)`. The binary ALSO passes esi=repl as a
  5th register arg; the reconstructed resolver family (__userpurge, command_recon4_resolve
  — out of scope for this file) does not declare/read esi, so omitting it is faithful.
- Failure-tail `if(!result && params) *(u32*)params=0` (disasm @0x4fd5dd..0x4fd5ea). VERIFIED.

Documented divergence (unchanged): `repl<0` (a _USE row byte >=0x80) sign-extends to a
negative dispatch index in the original -> wild call below the table (UB; engine only
stores 0..14 via _NEW). Source fails the token instead. Not reproducible; documented.

Dead-store error buffers (Wrong group / Wrong slot %i / Unknown Replacement) reproduced
as snprintf into a never-read 256-byte stack buffer — unobservable, kept for fidelity.

## illness.cpp

### VIBE_Character_IsDiseaseCandidate @0x4d762c — VERIFIED-1:1
`marker==0xFFFF` (word) -> 0; `active==0` (byte+8) -> 0; `(i8)kind>=10` (`jl` SIGNED) -> 0;
then IsOwnerForTurn ? 1 : IsObjectForTurn(0/1). Source matches incl. signed-char kind
(0x80 stays a candidate). VERIFIED.

### Group "free" masks @0x58ab13..0x58ab96 — VERIFIED-1:1
All 8 test sites match kGroupMask[]: 0xF0, 0xF00, 0x3000, 0x1C000, 0xE0000, 0x700000,
0x180·<<16 (0x1800000), 0x1E000000. (byte/word tests folded into dword masks correctly.)

### IllnessPackGroup @0x58ac5d..0x58adb7 — VERIFIED-1:1
Packing operates on ecx = `*(dword*)(rec+44)` (loaded @0x58abdd) = the disease state.
Severity is edx (zero-extended u16). All 8 arms verified clear-mask + shift + value-mask:
g2 (~0xF0,<<4,&F); g3 (~0xF00,<<8,&F); g4 (~0x3000,<<12,&3); g5 (0xFFFE3FFF,<<14,&7);
g6 (0xFFF1FFFF,<<17,&7); g7 (0xFF8FFFFF,<<20,&7); g8 (0xFE7FFFFF,<<23,&3);
g9 (0xE1FFFFFF,<<25,&F). default unchanged. VERIFIED.

### VIBE_Building_PickRandomDiseaseEvent @0x58aaf4 — VERIFIED-1:1 (comment fix only)
- free table = qmemcpy(dword_5830C0,0x20) = all zero, then per-group test sets slot=1
  when sub-field clear. Group 0 is NOT special-cased / NO early bail (disasm @0x58ab13
  jz->0x58ace4 sets freeTable[0]=1 then continues; non-zero high nibble of byte0 just
  falls through). FIXED: source comment previously claimed "bail immediately (return 0)"
  for group 0 — code was already correct (conditional set), comment corrected.
- RNG draw count/order VERIFIED: draw 1 (start = (i32)RandNext()%8, signed idiv but
  RandNext∈[0,0x7FFF] so == unsigned, low 16 bits) ALWAYS after the 8 tests; linear
  probe (probe+1)%8, max 8 iters, checks freeTable[probe]==1 first. Draw 2 (severity =
  RandNext()%(u16)countMod) only when a free group is found AND (u16)countMod!=0.
  Early returns before draw 2 if no free group. Source matches exactly.
- cost = fild(u16 severity) * costScale (FPU), ConvertX (RC=chop / trunc toward zero),
  fistp int. Source: `(int)trunc((double)(u16)severity * costScale)`. Product exact in
  double for these ranges. VERIFIED.
- eventByte = severity ? group : 0 (group∈2..9, positive). severity==0 -> return 0.
  Otherwise newState = PackGroup(state, group, severity&0xF), return 1. VERIFIED.

## Changes made
- illness.cpp: corrected two comments (header block + inline) that wrongly described
  group 0 as an early-bail special case; code unchanged (was already 1:1).
- No source-logic or golden changes required — all goldens encode correct behavior.

## Tests (built target-only; ctest with GUILD_GAME_DIR)
- sim_illness_test ........... PASS (EventTable, GroupIsFree, PackGroup, DiseaseCandidate,
  PickGoldenSeed1, PickGoldenSeed12345, PickGoldenForcedGroup5, NoFreeGroup)
- history_parse_test ......... PASS
- world_history2_test ........ PASS
- world_history_full_test .... PASS
4/4 suites green.
