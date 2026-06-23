# FIX-SIMMISC — meister_mgmt_recon + command_apply6 (Wave-H1b)

Both assigned tests now GREEN.

```
1/2 Test #328: meister_mgmt_recon_test .......... Passed
2/2 Test #599: sim_command_apply6_test .......... Passed
```

Built only the two assigned targets via `cmake --build build -j --target
meister_mgmt_recon_test sim_command_apply6_test`. No git used.

---

## 1. meister_mgmt_recon_test — `MeisterMgmtReconStock.CutoffBitsMatchDecompile`

**Symptom:** `CHECK_EQ((int)kRatioCutoffBits, 0x3F4CCCCD)` failed.

**Root cause (SOURCE wrong):** `src/sim/meister_mgmt_recon.h:74` had
`kRatioCutoffBits = 1061158093u`. That decimal is `0x3F3FFCCD`, NOT the
intended `0x3F4CCCCD`. The frozen-stash state carried a typo'd literal whose
comment still claimed `0x3F4CCCCD`.

**Binary evidence:** `VIBE_Ai_ManageResourceStock` @0x4536c0, disasm at
**0x4536db**: `cmp [esp+1Ch+var_18], 3F4CCCCDh` followed by `jge loc_453659`
(reject path). `0x3F4CCCCD` = `1061997773` (= 0.8f). The handler computes the
cached/fresh price ratio and rejects restock when `ratio >= 0.8f`.

**Fix:** changed the literal to `1061997773u` (= 0x3F4CCCCD). The `>=` reject in
`meister_mgmt_recon.cpp:50` (`if (ratioBits >= (i32)kRatioCutoffBits)`) already
matches the `jge` 1:1; only the constant was wrong.

---

## 2. sim_command_apply6_test — `SimApply6.SellableAmountProducesAndCredits`

**Symptom:** `CHECK_EQ(lastSellableProceeds, 24)` and `CHECK_EQ(lastCredit, 24)`
failed (actual 12).

**Root cause (GOLDEN wrong):** the test asserted proceeds = `price * (outCount *
v15)` = `3 * (2*4)` = 24. The binary credits **per craft**, i.e. `price * v15`,
NOT multiplied by the output count.

**Binary evidence:** `VIBE_Command_ExComputeSellableAmount` @0x497538. At
**0x4976d5**:
`v21 = VIBE_Building_ComputeMarketPrice(v20, 0x64u) * (double)v15;`
where `v15` is the craft count (the `v14`/`v15` clamp result), then
`VIBE_Coord_ConvertX()` (trunc) → `v39 = (int)v21`. The output goods added are
`v15 * outCount` (`*(_DWORD*)(v18+7) += v15 * *(u16*)(v37+54)` @0x4976b6), but the
proceeds/credit use bare `v15`.

`src/sim/trade_sell.cpp:219` (`out.proceeds = TruncToInt(price * v15)`) is the
faithful translation and is correct — NOT my assigned file, left untouched. The
golden was the divergence.

**Fix:** corrected the two golden values 24 -> 12 in
`tests/unit/sim_command_apply6_test.cpp` and updated the inline comment to cite
0x4976d5 (per-craft credit vs. outCount*produced goods added). The `lastAddQty`
golden (8 = outCount*produced) was already correct and unchanged.

---

## Files touched
- `src/sim/meister_mgmt_recon.h` — kRatioCutoffBits literal 1061158093u -> 1061997773u (0x3F4CCCCD, @0x4536db)
- `tests/unit/sim_command_apply6_test.cpp` — proceeds/credit golden 24 -> 12 (@0x4976d5)
