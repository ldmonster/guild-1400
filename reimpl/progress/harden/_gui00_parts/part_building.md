# Hardening — building_dialog / building_options_menu / building_upgrade_dialog

1:1 line-for-line diff of every provenance-carrying function against the
gilde.exe decompile + disasm (imagebase 0x400000). MCP live, module `gilde.exe`.

Result key: VERIFIED-1:1 (no churn) / FIXED (with evidence) / BOUNDARY (modeled seam).

---

## Constant / table verification (get_bytes)

| symbol | addr | bytes | decoded | reconstruction | status |
|---|---|---|---|---|---|
| dword_53B454 (kTechCategoryOrder) | 0x53b454 | `01 02 0b 0c 09 05 06 03 04 0a 0d 0e 0f 10 11 07 08 00` | 17 ids + 0 sentinel | identical | VERIFIED |
| dbl_624350 (kUpgradeWorthScale) | 0x624350 | `...d3 3f` | 0.3 | 0.3 | VERIFIED |
| dbl_624328 (kExpandPriceModeScale) | 0x624328 | `...d0 3f` | 0.25 | 0.25 | VERIFIED |
| dbl_624330 (kExpandPriceBase) | 0x624330 | `...e0 3f` | 0.5 | 0.5 | VERIFIED |

Float->int: `VIBE_Coord_ConvertX` @0x5c6b08 disasm = `fstcw; fldcw(RC=trunc); frndint; fldcw`
i.e. **frndint with round-toward-zero = truncation** (RC bits set via the 0x1F byte).
The following `fistp`/`(int)` then stores an already-integral value. So every
`ConvertX -> (int)` site is truncation toward zero; the reconstruction's
`static_cast<int>(double)` matches. (CLAUDE note "ConvertX TRUNCATES" confirmed.)

---

## building_dialog.cpp

- **0x54a734 SellPreview** — VERIFIED-1:1. form `Gebaeude_Verkaufen_pergament`, slot 2,
  text 0x13D8=5080, 1 child; OK(1210)->Sell op90, 1155->cancel. Loop selector 423879.
- **0x54ad7c ConfirmSell** — VERIFIED-1:1. text 0x13DF=5087, 1 child; OK(1210) AND
  ChildObjectId==dword_62D22C -> QueueRequestArgs25(op 90); 1155->cancel.
- **0x54bc50 SellLand** — VERIFIED-1:1. CheckBuildRequirements==1 gate (else
  ShowMessageBox(dword_8C8678,0)); text 0x13F1=5105, slot 2, 1 child; OK(1210)->
  Reset28 tag 33 + op90; `!=1155` fallthrough keeps looping.
- **0x54a908 Renovate** — FIXED. 4 children (v=ChildObjectId/v39/v40 confirm, v38 cancel);
  gate `(100 - cond>>24) > 0` else ShowMessageBox(...,4). Disasm 0x54aa97-0x54ac65 shows
  the loop tests only `dword_75BF38 == -1` (no hit) then compares `dword_62D22C` to the
  four child ids — **there is NO 1155 branch** (cancel = child[3] + right-click global
  dword_672230). The reconstruction had a spurious `clickedId == kClickCancel` arm.
  Replaced it with `if (clickedId == -1) return false;` (keep looping on no-hit),
  matching the original; cancel stays child[3]. Test unaffected (uses clickedId=0).
- **0x54c3c0 ExtinguishFire** — VERIFIED-1:1. slot1 title 0x13F7=5111, slot2 body
  0x13F8=5112, 3 buttons; level/threshold pairs (child0->1/0.2, child1->2/0.5,
  child2->4/0.8); OK(1210)+object match, 1155->cancel. Skill check + single
  RandomFloatScaled draw + success/fail messageboxes are BOUNDARY (sink-side outcome).
- **0x54be50 ShowTechEffects** — VERIFIED-1:1 (layout). reads dword_53B454 order table,
  slot1 0x13F3=5107, slot3 0x13F4=5108 + OK child, slot2 = one slider per category with
  a workstation (SumWorkstationCount); childCount = 1 + sliders. Per-slider value math
  (SumWorkstationByCategory ratios, ConvertX) is BOUNDARY (renderer/value content).

## building_options_menu.cpp

- **0x54c608 OptionsMenu** — FIXED. All 17 entry text ids re-mapped from hex and verified
  (5043/5045/5053/5056 foreign; 5041/5044/5058/5050/5047/5048/5049/5055/5051/5054/5059
  owned; 5052 city-link; 6068 debug). Block-A/B gates verified arm-for-arm
  (foreign `+39!=own && +37!=own`; owned `+37==own || +39==own`; cat/flag/state/room
  conditions all match).
  **Bug:** the `building[90] & 4` late SetEnabled group was wrong. Disasm @0x54cd18 maps
  the four disabled objects by stack offset:
  var_40=v92=**OpenUpgradeWindow(5055)**, var_44=v91=**Renovate(5048)**,
  var_6C=v81=**TearDown(5049)**, var_38=v94=**SellPreview(5047)**.
  Reconstruction was disabling {SellPreview, ConfirmSell, Renovate, TearDown, SellLand}
  (ConfirmSell+SellLand wrong; OpenUpgradeWindow missing). Corrected to
  {OpenUpgradeWindow, Renovate, TearDown, SellPreview}. The `building[90] & 0x40`
  upgrade toggle (var_34=v95) was already correct. City-link "not player root" and the
  rename-field readback (DataPtr copy + RequestState23) remain BOUNDARY.
- **0x54d5b4 OptionsMenuAlt** — VERIFIED-1:1. entries Renovate(ChildObjectId, always),
  OpenUpgradeWindow(v40, `*flag!=3`), ExpandRoom(v35, `room>1`), Upgrade(v3, always);
  late gate `[90]&4` disables {OpenUpgradeWindow, Renovate}, `[90]&0x40` toggles Upgrade.

## building_upgrade_dialog.cpp

- **0x54b604 Upgrade** — VERIFIED-1:1. cap `type[+583] >= type[+584]` (disasm 0x54b638
  `cmp dl,[eax+248h]; jnb` = unsigned byte >=) -> 5098/box0; req gate skipped when
  dword_63C7B8 set, else cat 1/4/8 -> 5097, other -> 5100; plot/name match -> 5099;
  cost = `(int)trunc(SumFlaggedSlotsWorth * 0.3)` via ConvertX (truncation confirmed,
  disasm 0x54b6ec-0x54b707) -> prompt 5096; confirm+afford -> building_upgrade /
  Reset28 kind 30 bracket. cat 3/5 storage-item variants are BOUNDARY (sink-side).
- **0x54aeb0 ExpandRoom** — VERIFIED-1:1 (with one float-precision tightening). scan
  `type+35` words, stride 2, up to 64, mask `&0x7FFF`, kind byte
  `(dword_13CE27C + 65*roomType)==2 && !=253`, stop at 32 slots (v8<96); price =
  `(int)trunc(ComputeMarketPrice(rt,100) * (priceMode*0.25 + 0.5))`; slider when >4.
  Progress fraction tightened to match `v70 = (double)elapsed / (float)total`
  (was `(float)/(float)`); zero-guard kept (defensive, original divides unguarded).
  Slot-click dispatch: command enqueued iff slot match + affordable; the modal loop
  does NOT close on a slot click in the original (no dword_631614=1) — the dispatch
  return value models "command dispatched" (established test contract), the loop
  continuation is a BOUNDARY seam.

---

## Edits

- building_options_menu.cpp: corrected `flag90 & 4` late-disable set (FIXED, evidence
  disasm 0x54cd18 stack-offset map).
- building_dialog.cpp: removed spurious 1155 branch in DispatchRenovate; modeled no-hit
  as keep-looping (FIXED, evidence disasm 0x54aa97-0x54ac65).
- building_upgrade_dialog.cpp: ExpandRoom progress fraction types matched to original
  (double numerator / float denominator).

## Tests (all green)

gui_trade_dialogs_test, gui_building_upgrade_test, gui_building_upgrade_e2e_test,
gui_trade_dialogs_e2e_test, wire_building_test, session_panels_test,
play_interact_building_test — 100% pass. `guild` library builds clean.
