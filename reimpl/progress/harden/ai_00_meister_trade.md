# Harden report — AI meister trade decision cores

Scope files:
- `src/ai/meister_trade.cpp` / `.h`
- `src/ai/meister_trade2.cpp` / `.h`
- `src/ai/meister_general_trade.cpp` / `.h`

Method: every function carrying `gilde.exe 0xADDR` provenance was DECOMPILED + DISASM'd
in IDA (module `gilde.exe`, imagebase 0x400000) and diffed line-for-line against the
reimplementation. Constants confirmed from disasm immediates; float->int sites checked
against `VIBE_Coord_ConvertX @0x5c6b08`; fixed-point shifts checked `sar` vs `shr`.

Source addresses covered: **0x58658c, 0x45e71c, 0x45f0a0, 0x4c763c** (all four).

## Per-function verdicts

| Function | Source addr | Verdict | Evidence |
|---|---|---|---|
| StockCapacity | 0x58658c | VERIFIED-1:1 | `if(type==477) 5*level+10; elif level==3 → 80; else 20*level` matches both pseudocode arms (0x586751 / 0x5867a8 / 0x5867af) and the dup branch (0x586885…). |
| StockDeficit | 0x58658c | VERIFIED-1:1 | `shr edx,2` @0x58675e is a **logical** shift → `(u32)(cap*count)>>2`; source uses `static_cast<u32>(cap*count)>>2`. Special-set `{42,278,475,476}` decrements `count-1` (`cmp si,2Ah` … 16-bit `si`, but values fit 16 bits so the `int` param is identical). Clamp `>=0` matches `if (v24>=0)`. |
| SortStockByValue | 0x58658c | VERIFIED-1:1 | Selection sort: outer i, inner j>i, swap when `record[j].value > record[i].value` (0x586653) → **descending**. Source swap condition identical. Original compares j-value promoted to `double` vs i `float`; result identical to float compare. |
| TransporterAuditRuns | 0x45e71c | VERIFIED-1:1 | even hour (`WORD2(clock)%2==0`) clears `[+436]&0x80` and returns false; odd hour runs only if `[+436] >= 0` (high bit clear). 0x45e748 / 0x45e770. |
| TransporterBuyDecision | 0x45e71c | VERIFIED-1:1 | All three class paths diffed against disasm. **Caravan (type 9)**: balanced (`cartCount==highCartCount`, `sub;jz` @0x45e8b9)→no buy; split on highCartCount (`test edi;jnz` @0x45e8c1); busy (`[+456]&8`)→quota (`[+583] jle`)→`RandomModulo(0x2EE)<2`→ConvertX price→`2*price<budget`; 309/310 = `309 + (RandomModulo(8)>=4)` (@0x45eccf), else 310 (0x136). **Non-caravan cartCount>0**: class 6/7 + `[+436]&4` gate → busy → quota → roll → 309 (0x135), order @0x45ef07-0x45ef7d. **cartCount==0**: class 6/7 + flag gate, then unconditional 308 (0x134) buy @0x45ee1d. |
| TavernRefillAmount | 0x45f0a0 | VERIFIED-1:1 | `fillLevel < RandomModulo(0x20)+40` → amount `100 - fillLevel` @0x45f11b / 0x45f1bc. Constants 0x20/40/100 confirmed. |
| CityWealthFloor | 0x4c763c | VERIFIED-1:1 | `8000*dword_63C744 + 32000` @0x4c7699. Source `32000 + 8000*difficulty`. |
| BalancePersonGrant | 0x4c763c | VERIFIED-1:1 | class 5 → break/stop (@0x4c76b2); class 4 → always; class 2 → `tick%4 == (personId&3)` (@0x4c7761); grant `floor-held` when `held<floor` (@0x4c76d3). `held = Person_SumCurrencyHeld`. |
| BalanceCityGoods | 0x4c763c | VERIFIED-1:1 (RULES) | scan order + stop-at-class-5 + grant emission match the loop body. Outer cadence gate (`tick/(tick/10+1)` @0x4c7656), the 768-person stride, the 32-grant packet-status flush ring (`GetPacketStatusById`/`Amt_RefreshGuildState`) are the documented BOUNDARY (engine plumbing); the per-person decision is exact. |
| CartIsLost | 0x45e71c | VERIFIED-1:1 | lost ⟺ no prod-handler (`i`) AND no route-handler (`j`) AND `v47 != 0 && v47 != ownBuilding`. Source: false on prod/route handler, false on owner 0, false on own owner, else true. |
| CollectTransporters | 0x45e71c | VERIFIED-1:1 (orchestration) | reroute-each-lost + tally (cartCount=v49, highCount=v48 for id 310) + feed buy decision. He-handler match, `ResolveEntityById` chain-walk, scene-tree stride-67 sweep, German Sprintf log lines are the documented BOUNDARY (entity-array plumbing). |

## Float -> int audit (every ComputeMarketPrice site)

`VIBE_Coord_ConvertX @0x5c6b08` = `fldcw 0x*F` (RC=11 truncate-toward-zero) + `frndint`
+ restore. So the value left on the x87 stack is already `trunc(price)` toward zero; the
following `fistp` stores it exactly. Sites: @0x45e918, @0x45e95a, @0x45ee48, @0x45ef92,
@0x45efe7, @0x45ecde, @0x45ed30. Source models this as
`static_cast<i32>(static_cast<double>(price))` (truncation toward zero) — **identical**
for all values. The original re-calls ComputeMarketPrice for the EnqueueCmd15 cost
(deterministic, same cartId → same value); source reuses the single `price_of` result,
behavior-identical for the value (commands are hooked).

## Fixed-point / shift audit

- StockDeficit `>>2`: `shr` (logical), not `sar`. Source `static_cast<u32>(...) >> 2`. Correct.
- No other `>>16` / fixed-point sites in scope; cart pricing goes through ConvertX (above).

## Result

- Functions audited: **11** (across 4 source addresses).
- VERIFIED-1:1: **11**.
- FIXED: **0** (no divergence found; goldens already match the binary).
- BOUNDARY (documented, hooked): BalanceCityGoods flush ring, CollectTransporters
  scene/He/chain-walk, Sprintf log lines — pre-existing, untouched.

## Build / test

Built: `ai_meister_trade_test ai_meister_bank_test ai_meister_economy_test
ai_meister_workstation_test` + e2e. All trade/trade2/general_trade decision-core tests
PASS:
- `ai_meister_trade_test` — PASS (CollectTransporters/CartIsLost + storage cores).
- `ai_meister_economy_test` — PASS (StockCapacity/StockDeficit/SortStockByValue).
- `ai_meister_workstation_test` — TransporterAudit/BuyDecision/TavernRefill/
  CityWealthFloor/BalancePersonGrant/BalanceCityGoods all PASS.
- `ai_meister_bank_test`, all e2e — PASS.

Pre-existing unrelated failures (OUT OF SCOPE — `meister_workstation.cpp`,
`CheckWorkstationCapacity @ tests/unit/ai_meister_workstation_test.cpp:221,241`): 2
checks. Not touched by this hardening pass; not part of the trade decision cores.
