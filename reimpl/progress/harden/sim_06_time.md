# Harden sweep — sim time chunk (sim_06_time)

Files: src/sim/game_clock_tick.cpp, src/sim/gametick.cpp, src/sim/gametime.cpp,
src/sim/gametime_recon.cpp (+ their headers + tests).

MCP live (gilde.exe, imagebase 0x400000). Every provenance-tagged function
decompiled AND, where a float->int / FP path or stride was involved, disassembled.

## Counts
- Functions checked: 11
- VERIFIED-1:1: 10
- FIXED: 1 (constant + docs; behavior of the function was already correct)
- BOUNDARY: 0 (the deferred render plumbing inside 0x56eba4 is documented, not in
  this chunk's reconstructed surface)

## game_clock_tick.cpp

### ClockScaledGameSeconds / ClockComputeGameTimeOfDay @0x527778 — VERIFIED-1:1
- Disasm 0x5277a9..0x527820 traced instruction-by-instruction.
- Float path: `fild speed; fmul flt_622958; fadd dbl_622960; fild daySeconds;
  fmul; call VIBE_Coord_ConvertX; fistp`. The FP constants confirmed via get_bytes:
  - flt_622958 = CD CC CC 3B = 0.00625f (1/160)  ✓
  - dbl_622960 = 00..E0 3F = 0.5  ✓
  - dword_63CC60 = 64 00 00 00 = 100  ✓
- ConvertX @0x5c6b08 disasm: `mov byte[esp+1],1Fh; fldcw; frndint; fldcw(restore)`.
  Setting CW high byte 0x1F => RC bits = 0b11 => round-toward-ZERO (truncate).
  So the value handed to `fistp` is already trunc(v1). Reconstruction uses
  `std::trunc(v1)` then (i32) cast — exactly faithful (NOT nearbyint; the +0.5
  bias makes it round-half-up for positive v1). MATCHES.
- idiv decomposition: edx=v2/3600 (hours), ecx=(v2%3600)%60 (sec via x60 mul
  chain), ebx=(v2%3600)/60 (min). GameTime_Advance(rec@eax, a2@edx=v2/3600,
  a3@ecx=sec, a4@ebx=min). Reconstruction call
  `GameTimeAdvance(&master, v2/3600, v2%3600%60, v2%3600/60)` with signature
  (rec, addDays, addSeconds, addMinutes) — argument mapping MATCHES disasm.
- Gate: master.hour<23 && world.hour<23 || (flags&0x80). MATCHES.
- Golden tests in game_clock_tick_test.cpp encode the truncate behavior
  (0.5->0, 1.5->1, 2.5->2) — correct, no change. 77 checks pass.

### ClockDayEndPending @0x4c1324 — VERIFIED-1:1
- Predicate (v54&0x10000)==0 && (armNet || armClock) && (flags&0x80)==0 with
  armNet = !menuPop && !netWait && roundEnd && !latch; armClock = singlePlayer
  && worldHour>=0x17 && !latch. MATCHES the documented globals. Tests pass.

## gametick.cpp

### RunNpcTurnFlagSweep @0x5331a2..0x5331df — VERIFIED-1:1
- 768-slot loop; `dword_12CEAD8[i] &= 0xE0874703` (mask confirmed in decompile),
  unconditional clear, kind byte == 6 records last human index -> word_63CC5C.
  MATCHES.

### PlantAdvanceStage / PlantAdvanceFarm @0x56eba4 — FIXED (constant + docs)
- BEFORE: header claimed `kPlantNodeCount = 384` and "384-node growth loop".
- EVIDENCE (disasm): `lea ebx,[eax+600h]` @0x56ebac (end = base + 0x600 bytes),
  `add edx, 18h` @0x56ebff (stride 0x18 = 24 bytes). Iterations = 0x600/0x18 =
  **64 nodes**. The Hex-Rays `a1 + 384` is 384 *dwords* = the span, not the count.
- AFTER: `kPlantNodeCount = 64`; comments corrected in gametick.h and gametick.cpp
  with the disasm citation.
- Growth rule itself VERIFIED: empty marker = `sar [edx+0Ah],18h == -1` (signed
  byte at offset 13 == 0xFF); stage = `[edx+0Dh]` (SAME byte, offset 13); cap =
  OfficeTypeRecord[+0x44=68]; `cmp al,[ecx+44h]; jnb` => unsigned `stage < cap`
  then `inc`. Reconstruction `if (stage < cap) stage+1` MATCHES.
- NOTE (documented, not a divergence): in the binary the empty marker and the
  growth stage are the SAME byte; the synthetic PlantNode splits them as
  typeByte/stage purely for legibility (added a header note). The function takes
  `count` from its caller (turn_driver passes farm.size()), so the constant value
  is doc/provenance only — no behavioral test depended on 384. Tests still pass
  (71 checks).

### ResetPerTurnAccumulators @0x53355f — VERIFIED-1:1
- `for (m=0; m!=102912; m+=134) dword_12CE8E0[m]=0` => 768 rows, stride 134
  dwords. MATCHES.

## gametime.cpp

### GameTimeAdvance @0x583150 — VERIFIED-1:1
- Full decompile diffed. Subtle point checked: original keeps v8 = full v6, then
  `LOWORD(v8)=hour`; `*(WORD*)(a1+4) = v9/60 + v8` stores only low 16 bits, so the
  high-16-of-v6 bits are masked off => result low word == (v9/60 + hour) & 0xFFFF,
  exactly what the reconstruction computes with `(u16)(v9/60 + hourLo)`. Carry
  loops (>=24 / <0 borrow), minute%=60, second=v6%60, return hour — all MATCH.

### GameTimeCompare @0x583230 — VERIFIED-1:1
- day (dword@0) first; then 3600*hour + 60*minute + second; returns -1/+1/`v2>v3`.
  MATCHES.

### GameTimeDiffMinutes @0x5832bc — VERIFIED-1:1
- (b.min-a.min) + 1440*(b.day-a.day) + 60*((u16)b.hour-(u16)a.hour); seconds
  ignored. MATCHES.

### GameTimeSet @0x5831f0 — VERIFIED-1:1
- second dword = a3; hour word = (a3 & 0xFF00)|a2 (== a2 since a3 is a byte);
  minute dword = a4; return a4. MATCHES.

## gametime_recon.cpp

### GameTimeInitDefault @0x58320c — VERIFIED-1:1
- qmemcpy 12B from qword_13CE852 + 2B from unk_13CE85E, return 14. get_bytes
  confirms both source regions are ALL ZERO in the image, so zero-template +
  return 14 is faithful. MATCHES.

### GameTimeUnpackFromRecord @0x58334c — VERIFIED-1:1
- day = (packedHead >> 16) - 1400 (arithmetic SAR, packedHead is i32); hour =
  (u16)(u8)field4; minute = (i32)(u8)field5; second = field8; return field8.
  MATCHES.

### GameTimeAdvanceThunk @0x583374 — VERIFIED-1:1
- Advance(a1, a3@ebx, a4@stack, a2@ecx) => Advance(rec, addDays=ebx,
  addSeconds=stack, addMinutes=ecx). Reconstruction mapping MATCHES. Tests pass.

### GameTickGetScaledDelay @0x43c680 — VERIFIED-1:1
- uDelay * dword_62EB38 (both u32). MATCHES.

## Handoffs
- None. The one fix (kPlantNodeCount 384->64 + comments) is confined to this
  chunk's files (gametick.h, gametick.cpp). turn_driver.cpp consumes
  PlantAdvanceFarm with a caller-supplied count and does not reference the
  constant, so no out-of-chunk edit was required.

## Build / tests
- gametick_test (71), game_clock_tick_test (77), gametime_recon_test (31): all
  pass after the change. No golden vectors encoded wrong behavior (the clock
  truncate goldens and the plant-farm count goldens are caller-driven).
