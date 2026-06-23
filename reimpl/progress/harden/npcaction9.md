# Harden: src/sim/npcaction9.cpp — 1:1 audit vs gilde.exe (imagebase 0x400000)

Per-function line-for-line diff of the AI Evaluate* action scorers against IDA
decompile + disasm. Float->int sites, RNG draw count/order, struct offsets, control
flow, switch arms, signed/unsigned compares, and side-effect order all checked.

## Float constants — all VERIFIED via get_bytes

| sym | addr | bytes | value | C++ const | ok |
|-----|------|-------|-------|-----------|----|
| dbl_61A4F0 | 0x61A4F0 | 0x4024000000000000 | 10.0  | kShootCloudBias | yes |
| flt_61A4F8 | 0x61A4F8 | 0x3BA3D70A | 0.005f | kShootWealthMul | yes |
| flt_61A4FC | 0x61A4FC | 0x3E6147AE | 0.22f | kShootCurrencyMul | yes |
| flt_61A50C | 0x61A50C | 0x3BA3D70A | 0.005f | kRivalWealthMul | yes |
| flt_61A510 | 0x61A510 | 0x43480000 | 200.0f | kRivalFavBase | yes |
| flt_61A514 | 0x61A514 | 0x3C23D70A | 0.01f | kRivalFavMul | yes |
| dbl_61A518 | 0x61A518 | 0x3F847AE147AE147B | 0.01 | kRivalScale | yes |
| dbl_61A520 | 0x61A520 | 0x40A9000000000000 | 3200.0 | kRivalLoClamp | yes |
| dbl_61A528 | 0x61A528 | 0x4113880000000000 | 320000.0 | kRivalHiClamp | yes |
| flt_61A530 | 0x61A530 | 0x3EE147AE | 0.44f | kRivalAffordMul | yes |
| flt_61A598 | 0x61A598 | 0x459C4000 | 5000.0f | kRecruitHiThresh | yes |
| flt_61A59C | 0x61A59C | 0x449C4000 | 1250.0f | kRecruitLoThresh | yes |
| flt_61A5A0 | 0x61A5A0 | 0x3ECCCCCD | 0.4f | kRecruitWorthMul | yes |
| flt_61A674 | 0x61A674 | 0x3C23D70A | 0.01f | kArrestWealthMul | yes |
| flt_61A678 | 0x61A678 | 0x43200000 | 160.0f | kArrestWealthFloor | yes |
| flt_61A67C | 0x61A67C | 0x3E6147AE | 0.22f | kArrestCurrencyMul | yes |
| flt_61A62C | 0x61A62C | 0x3EA8F5C3 | 0.33f | kSocializeNoneProb | yes |
| flt_61A630 | 0x61A630 | 0x3F400000 | 0.75f | kSocializeLeaveProb | yes |
| flt_61A634 | 0x61A634 | 0x3F2A7EFA | 0.666f | kSocializeFavGate | yes |
| flt_61A638 | 0x61A638 | 0x3E2AAAC1 | 0.166667f | kSocializeFavBias | yes |

- 320.0 constant (line 59 / ThrowAtRival 0x471316): original sets `v22 = 1091799040 =
  0x41100000` as HIDWORD of v36 with LODWORD 0 => bits 0x4110000000000000 = 320.0.
  VERIFIED. C++ uses literal 320.0; identical.
- dword_478450 stride table (0x478450, 16 dwords): 1,3,5,7,11,13,17,19,237,239,243,
  245,249,251,253,255 — VERIFIED byte-exact vs kSocializeStrideTable.
- byte_469D84[0..2] / byte_469D90[0..2] (BribeJailed v23..v28 seed): all 0 — C++ zero
  init is faithful.

## ConvertX / float->int

`VIBE_Coord_ConvertX` (0x5c6b08) sets x87 truncation; every `(int)stN` after it is
trunc-toward-zero, modelled by NpcAction9_TruncToInt = (i32)(double). All sites
checked: Shoot v18, ThrowAtRival v40/v38, Throw budget+price, Arrest v18+budget,
Recruit worthGate. All 1:1.

## Per-function status

| addr | fn | status |
|------|----|--------|
| 0x470d38 | EvaluateShoot | VERIFIED-1:1 |
| 0x470f24 | EvaluateThrow | VERIFIED-1:1 |
| 0x4710c4 | EvaluateThrowAtRival | VERIFIED-1:1 |
| 0x471380 | EvaluateUseItemOnTarget | VERIFIED-1:1 (control flow); command-emitter side effects are a documented boundary (no-op hooks) |
| 0x471650 | EvaluateEnterBuilding | VERIFIED-1:1 |
| 0x47285c | EvaluateRecruitWorker | FIXED (mood signed-byte) then VERIFIED-1:1 |
| 0x472c8c | EvaluateRecruitFromBuilding | FIXED (shares RecruitCandidate) then VERIFIED-1:1 |
| 0x472720 | EvaluateHirePersonnel | VERIFIED-1:1 |
| 0x4730cc | EvaluateBribeJailed | VERIFIED-1:1 (grid-walk is documented populated-grid boundary) |
| 0x474c4c | EvaluateArrest | VERIFIED-1:1 (city-id table scan is documented boundary) |
| 0x473700 | EvaluateShopInteract | VERIFIED-1:1 |
| 0x474340 | EvaluateSocializeGroup | VERIFIED-1:1 (group-handling branches are documented grid boundary) |

## Divergence found + fixed

### Mood byte signedness (RecruitCandidate shared body; RecruitWorker 0x472950 / 0x472a3f)

Disasm:
```
472950  mov dl, [eax+5Ch]      ; mood = targetRec[92]
472953  cmp dl, 21h            ; 33
472956  jge loc_472A3F         ; SIGNED compare
472a3f  cmp dl, 42h            ; 66
472a42  jge short loc_472A51   ; SIGNED compare
```
Both compares are signed (`jge` on a byte loaded into dl with no movzx). A mood byte
with bit7 set (>=128) is therefore negative and selects the 50 bucket. The C++ read
`u8 mood = targetRec[92]` made these unsigned, diverging for mood >= 128 (it would pick
10/30 instead of 50). Fixed to `i8 mood = (i8)targetRec[92]`. Affects both
EvaluateRecruitWorker and EvaluateRecruitFromBuilding (shared RecruitCandidate). Note:
the same mood block appears in BribeJailed's grid walk (0x4732a3), which the C++ leaves
as a deferred populated-grid boundary, so no change needed there.

This block's output (reqA[4]) is only consumed by SelectBestRecursive (inert in the
headless build), so it is not observed by the current golden tests; the fix is for
on-world fidelity. No golden vector required correction (none exercised mood>=128).

## RNG draw count / order — checked

- Shoot: RandomModulo(25) drawn after currency, before color search. 1:1.
- ThrowAtRival: clock-phase guards (no RNG), then SumPlayerHandlerValues,
  CountActiveByTarget, RandomModulo(v8 — uninit cx; modelled 0/0/0 faithful inert),
  then iterate, then RandomModulo(count) pick. 1:1.
- Arrest: RandomModulo(0x20)+32 radius. 1:1.
- Recruit*: RandomModulo(0x40) loyalty, RandomModulo(0x200|0x400) demand. 1:1.
- BribeJailed: RandomModulo(0xBB8) vault gate, RandomModulo(8) tick gate. The
  per-eligible-person RandomModulo(0x40) inside the grid walk is NOT drawn against the
  empty inert grid (the `*v8 &&` guard is false for all 256 slots), so the inert draw
  count matches; populated-grid walk is a documented boundary. 1:1 for inert world.
- SocializeGroup: RandomModulo(0x10) stride (drawn before the bail gate — order
  preserved), RandomModulo(0x100) start slot, then RandomFloatScaled none-prob. 1:1.

## Signed/unsigned + shift sites

- Loyalty: `*(int*)(rec+89) >> 24` arithmetic (signed) shift — C++ RdI32 (i32) >> 24. 1:1.
- Inventory desc: `*(int*)&desc[2] >> 16` signed — C++ RdI32(desc,2) >> 16. 1:1.
- HIWORD reads (Throw slotKey/priceKey): `(x >> 16) & 0xFFFF`. 1:1.
- Throw unit gate: `*(float*)(slot+20) > 0 && *(int*)(slot+20) < 1065353216`. 1:1.
- Mood compares: SIGNED (fixed above).

## Build / tests

- `cmake --build build --target npcaction9_test npcaction9_itest npcaction9_e2e_test`
  — compiles + links clean (file is in libguild.a).
- ctest -R npcaction9: 3/3 pass (npcaction9_test, npcaction9_itest, npcaction9_e2e_test).
- Pure-core golden vectors (TruncToInt, ShootTooExpensive, ArrestBounty, RivalBounty,
  LoyaltyRoll) re-derived against disasm float math — all correct, no golden edits.
