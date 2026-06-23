# Harden sweep — sim_03 / charaction_steps8

Files: `src/sim/charaction_steps8.cpp`, `src/sim/charaction_steps8.h`,
`tests/unit/charaction_steps8_test.cpp`.

MCP module gilde.exe (imagebase 0x400000). Every constant verified with `get_bytes`;
every float->int site checked against disasm (ConvertX @0x5c6b08 truncates; bare
`fistp` after it = trunc-to-zero); every shift checked sar(signed) vs shr.

## Counts
- Functions with provenance: 13
- VERIFIED-1:1 (no change needed): 4
- FIXED: 8
- BOUNDARY (documented): 1 cluster (the 589-byte person-class table + the cmd-delta
  base-relative addressing) — pre-existing, consistent with steps2-7.
- Golden tests fixed: 1 (InitEscort speed factor).
- Build: `guild` target builds; `charaction_steps8_{test,itest,e2e_test}` all green
  (78 / 9 / 20 checks, 0 failures).

## Per-function

### 0x40be0c ResetWalkTarget — VERIFIED-1:1
Four global stores (-1,-1,0,0) match.

### 0x40c120 QueueFreeAll — VERIFIED-1:1
2048-byte pool walk, skip-empties, Destroy+clear, last-Destroy return. Matches.

### 0x409200 MorphMovementInit — FIXED
- `dbl_610804` was coded as **1.0**; `get_bytes 0x610804` = `0x4049000000000000` =
  **50.0**. Fixed `kDbl610804 = 50.0`. (`dbl_61080C` = 2.0 was correct.)
- Float bit patterns 1109393408 (40.0f) / 1101004800 (20.0f) confirmed correct.
- NOTE (deviation, kept): original divides `dbl_610804 / *(float*)(a2+16)` with no
  null/zero guard; reimpl guards (`anim`/`animScale!=0`). The product flows into an
  opaque play-rate field, not asserted; a null `anim` would fault the original, which
  is not reproducible. Documented inline.

### 0x4dc374 FindGestureTarget — FIXED
- Command-phase flag55 used a fresh `personFindRecordById(found+196)` + `He_Id` (+1).
  Binary (0x4dc4ca) uses `v8 = *((_DWORD*)v2 + 1)` = the **matched-partner record `v2`
  from the proximity scan, id at +4**, compared to the current slot id. Added `v2`
  tracking (`v2 = RecordById`, 0x4dc3ff) and `v8 = LoadI32At(v2, 4)`. Wrong record +
  wrong offset corrected.
- selfChar/partner-char offset chains (+97/+388/+52, +76/80/84), filter (1,0,67),
  +212/+12 handler gate, 8-minute wake, cmd29 all verified correct.

### 0x4e3fdc InitArrestPerson — FIXED
- Notify-skip compared `cityRecipientId(idx) != begin->id`. Binary compares against
  `v3+4` (the target's own guild recipient = `dword_12CE914[134*cityIdx]` =
  `cityRecipientId(target city)`), not the person record's id. Fixed to compare against
  `cityRecipientId(134*cityIdx)`.
- Class-5 money seize dropped the `*(v3+2) != 6 && != 7` category guard and passed
  `cityIdx` as the enqueue recipient. Binary (0x4e4183) requires `v3!=0` + that
  category guard and enqueues to `v3+4`. Added `cityCategory(134*cityIdx)` guard and
  `cityRecipientId(134*cityIdx)` recipient.
- Loop bounds/stride (102912 step 134), category {6,7}, RandomModulo(3) +4-day wake,
  +132=-1, +184=0 all verified.
- BOUNDARY: `{11,12,13}` and `==5` class tests read `*(589*(*begin)+dword_13CE294)`
  (a loaded person-class table; `dword_13CE294`==0 at static time). Reimpl reads
  `*begin` directly — pre-existing analogue across steps2-7; class table is loaded
  game data not in the reconstruction tree.

### 0x4e4204 RunArrestPerson — FIXED
- `computeRoomWorth(v5, *(int*)(v5+89) >> 24, a3)` — disasm 0x4e42dd `mov edx,[ecx+59h];
  sar edx,18h` (offset **+89**, signed sar). Reimpl read `Cas8_FieldSelDword(v5)`
  (=+169) and passed `0` for the key. Fixed to `LoadI32At(v5,89) >> 24` and pass
  `Tok(target)`.
- `flt_61F624` (bias) was 1.0 -> `get_bytes` = 256.0; `flt_61F628` (scale) was 0.5 ->
  50.0. Fixed in header (used by the bribe ratio). Bribe path is untested (Iter<2 in
  the test) so no golden churn.
- Packet gate, state machine (<-1 / -2 / -1 / 0), ++Iter, RandomModulo(3) re-arm OK.

### 0x4e4484 InitEscortPrisoner — FIXED (+ golden)
- `dbl_61F630` speed factor was 0.5; `get_bytes 0x61F630` = `0x4024000000000000` =
  **10.0**. Fixed `kEscortSpeedFac = 10.0`. ConvertX truncation == `(int)` cast,
  matched. Three pose stamps (+96/+82/+68), +10-min appt, +184..196 copy verified.
- GOLDEN FIX: `InitEscortStampsAndAdvances` assumed 0.5 (CountA=10 -> +5 min, 8:35).
  With the real 10.0 it is 10*10=100 min: 8:30 -> 10:10. Updated assertions to
  minute=10, hour=10 (verified through the reimpl GameTimeAdvance).

### 0x4dffa0 RunLagerFuellen — VERIFIED-1:1 (with documented guard)
- switch(state+2): 0/1 cancel+free, 2 fill, 3 finalize — all arms match.
- Fill divide: disasm 0x4e00ab `idiv edi` (SIGNED) of the DiffMinutes result by
  `*(h+184)`. Reimpl `diff/per` is signed; keeps a `?1` zero-guard (original would
  fault on /0; not reproducible) — documented. `2*Iter` wake, zustand<threshold,
  delta finalize, RenderFormatted(6230) all verified.

### 0x4e0d8c RunLagerErweitern — FIXED
- AppendRawField offset passed `0`; binary (0x4e0e47) is `LOWORD(obj)+14-dword_11AA474`
  with `dword_11AA474`==0 and LOWORD(obj) the cmd-delta object-base normalization
  (dropped, as RunAdjust passes bare 28/29). Field-relative offset is **14**. Fixed.
- size 4u, count decrement, ActiveByEntity[218]&1 (byte 436), 15-min re-arm, error
  log path all verified.

### 0x4e0a40 RunAdjustObjectField — VERIFIED-1:1
- class-278 -> +578 else +576/+577 limit reads, +28/+29 bumps, raw-field offsets
  28/29 (object-base + `-dword_11AA474`==0 dropped), dual-budget re-arm, announce
  (6087) all match.

### 0x4e1c78 RunPickFromGround — FIXED
- Production divide used the **full int amount** as numerator. Disasm 0x4e1ea0..0x4e1ee8
  stores `var_1C[0] = (u8)amount` and reloads it (as a word) as the numerator. Fixed:
  `amountByte = (u8)(int)v17`, divide uses `(double)amountByte`.
- AppendRawField offset was `*(u8*)(h+172) + fieldSel + 128`; binary is
  `*(char*)(h+172) + (WORD)edi + 128 - dword_11AA474` — a **signed i8** of h+172 plus
  the object-base (dropped) + 128; the `fieldSel` term is not in the binary. Fixed to
  `(i8)*(h+172) + 128`.
- Field-full ceiling/divisor/clamp constants corrected via the header (see below).
  fieldSel read `*(int*)(h+169) >> 24` (h+169) verified correct here. All ConvertX
  sites = truncate, matched.

### 0x4e17dc RunHerdAnimals — FIXED
- `cmdRequest17` 4th arg passed `0`; binary (0x4e1a01 / sar at 0x4e19db) passes
  `v17 = *(int*)(h+172) >> 16` (signed sar). Fixed. 5th arg is `byte_6477A1` (==0 at
  default) — reimpl 0 OK. amount truncation, RandomModulo(30)/(5) draw order, +2-day
  wake, category {6,7} push verified.

## Header float constants — ALL corrected (get_bytes verified)
The earlier 1.0/0.5/0.1/100.0 placeholders were nearly all wrong:
| sym | addr | was | actual |
|---|---|---|---|
| kArrestWorthBias  | flt_61F624 | 1.0   | 256.0 |
| kArrestWorthScale | flt_61F628 | 0.5   | 50.0  |
| kEscortSpeedFac   | dbl_61F630 | 0.5   | 10.0  |
| kPickCeiling      | dbl_61F518 | 100.0 | 252.0 |
| kPickMulA         | dbl_61F520 | 0.1   | 1/30 (0.0333…) |
| kPickMulB         | dbl_61F528 | 1.0   | 8.0   |
| kPickWorkBonus    | dbl_61F530 | 0.1   | 0.01  |
| kPickFieldDiv     | flt_61F538 | 0.1   | 0.0039682542f (1/252) |
| kPickFieldCeil    | flt_61F53C | 100.0 | 252.0 |
| kHerdWorkBonus    | dbl_61F4F8 | 0.1   | 0.01  |
| (Morph) dbl_610804| 0x610804   | 1.0   | 50.0  |

`kPickFieldDiv` is a 32-bit `.rdata` float (`fld dword`); coded as the f32 literal
`0.0039682542f` which rounds to 0x3B820821 exactly (the x87 promotion of the f32 load
reproduces the binary's value).

## Handoff
- `dword_13CE294` (589-byte person-class table) and `dword_11AA474` (cmd-delta base,
  ==0 statically) are loaded game data / cmd-subsystem state not in the reconstruction
  tree; the class-table read and the object-base-relative delta offsets remain folded
  analogues here, consistent with steps2-7. A future wave owning the class loader /
  cmd-delta subsystem should re-thread these.
- `CharActionStep8Hooks` is still inert except `randomModulo` (wire_charaction2.cpp);
  the city/guild-record hooks (`cityCategory`/`cityRecipientId`) are documented to take
  a city index, but InitArrest/RunHerd's loop passes a 134-dword stride — the contract
  is internally ambiguous and was kept as-is; the InitArrest fixes use the same
  134*cityIdx slot as the target's own guild record (v3+4 / v3+2). Whoever wires the
  real guild array (`word_12CE910` @0x12ce910, stride 536) must make the hook indexing
  consistent.
