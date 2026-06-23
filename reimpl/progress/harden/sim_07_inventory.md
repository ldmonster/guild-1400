# Harden — sim_07_inventory (inventory cluster + item_use)

Scope: 1:1 disasm-vs-source diff of the five inventory cpp files plus item_use.cpp.
MCP IDA Pro (gilde.exe) used: decompile + disasm for every addressed function;
the slot-capacity logic @0x592474 and the Collect* engines @0x590fc0/0x590df8
checked instruction-by-instruction. Disasm taken as authority over Hex-Rays.

Tests run green: `sim_inventory_test`, `inventory2_test`, `wire_inventory_test`,
`uipanels_wave20_test` (item_use goldens), plus `inventory2_itest`,
`inventory2_e2e_test`, `sim_inventory_e2e_test`.

## Per-function verdicts

### src/sim/inventory.cpp
- 0x5923fc GetEffectiveStock — reserve set {42,278,475,476} reads `*(a2+14)`,
  type from `*a1`. Matches (consts in types.h: kItemReserveA..D). **VERIFIED-1:1.**
- 0x592474 GetSlotCapacity — `type 477 -> 5*lvl+10; lvl 3 -> 80; else 20*lvl`.
  Disasm `lea[edx*4];add edx;shl 2` == 20*lvl confirmed. NOTE: 0x592474 is a
  *function* that inlines the table, not a data table — no get_bytes target.
  **VERIFIED-1:1.**
- 0x54f04c FindSlotByItemId / 0x54f090 FindSlotIndexByItemId — original walks the
  global slot tables (word_63D1D8 stride 24, parallel marker word_63D1F0). The
  reimpl models slots as an InvGridSlot array and terminates on a zero-marker
  successor; faithful at the model level (the real globals are not reproduced;
  documented). Index/return-pointer arithmetic (12*idx words) matches.
  **VERIFIED-1:1 (model).**
- Carried-item helpers (Find/FreeSpace/Add/Remove/GetOrCreate) — glue around the
  verified pure rules; no own provenance address. Noted as glue, not faked.

### src/sim/inventory_capacity.cpp
- 0x592428 FindItemStock — QueryFind child, reserve-1 rule, missing->0.
  **VERIFIED-1:1.**
- 0x59266c ComputeFreeSpaceForItem — disasm: cap=SlotCapacity(self); found:
  `result=cap-child[+14]; if(result<=ceiling) return result else ceiling`; not
  found: count children (filter 5 + IterNext), `if(count>=fill28[u8]) 0; if(cap<
  ceiling) cap else ceiling`. C++ matches exactly incl. unsigned fill28 byte.
  **VERIFIED-1:1.**
- 0x5924a8 ComputeFreeCapacity — ResolveRoot guard, MapTypeToCategory side effect,
  cap from STOCK record (a1+0/a1+14), found branch reserve-keyed on stock type,
  type-42 dual-gate (fill28 & fill29) vs non-42 single-gate (fill28). C++ model
  matches. **VERIFIED-1:1 (model).**
- 0x592710 ComputeCarryCapacity — category 9 unlimited; (kind 6/7 && avatar) ||
  377/378 || !carriable -> 0; carried child: ceiling+lvl>3 -> 3-lvl else ceiling;
  else childCount>=6 / ceiling<=3 / 3; clamp v4>=0. C++ matches. **VERIFIED-1:1.**
- 0x590fc0 CollectStorageSlots / 0x590df8 CollectWorkstationSlots — disasm-checked:
  278-room mode early-return; keep predicate storage `(cat==23&&mode)||(cat!=23&&
  !mode)`, workstation adds `||cat==37` arms and `||child==278`; per-slot writes
  effStock(+104, reserve via child type), cap(+8), type(+72), count(+4) in that
  order. C++ matches (the `*v7` cap-hint output field is not in CollectedSlots and
  is documented as fidelity-only). **VERIFIED-1:1.**

### src/sim/inventory_wealth.cpp
- 0x5915b8 GetCurrencyAmount — QueryFind by currencyProto(player), return
  `*(v2+14)`, missing->0. **VERIFIED-1:1 (model).**
- 0x59152c SumCurrencyHeld — disasm: edx=0; loop edx += `[i+0Eh]`; return edx.
  Currency proto stride 189 dwords (756 bytes) confirmed; `>>16` proto. C++
  matches. **VERIFIED-1:1 (model).**
- 0x591f7c ComputeTotalWealth — `a1>=0x300 -> -1`, person marker `*v2==-1 -> -1`,
  sum currency + per-building (roomWorth + storageWorth). C++ matches.
  **VERIFIED-1:1 (model).**

### src/sim/inventory2.cpp
- Grid-UI cluster (0x5513a0/0x5513d8/0x54ebd8/0x54ecb0/0x54f8dc/0x567538) — fully
  hook-modeled widget/scene leaves; addressed-line comments preserved. Out of
  direct scope for golden divergence (no numeric goldens own these); spot-checked
  against listed addresses, no changes needed. Noted.

### src/sim/item_use.cpp — 0x5671f4 VIBE_Item_UseObjectAction
Three real divergences found and FIXED (disasm-confirmed):

1. **Permission-mask gate INVERTED.** Disasm 0x56724c `test [slot+10h],permMask`;
   0x56724f `jnz -> 0x5674DF (mov eax,-5)`. So the use is BLOCKED when the AND is
   NON-zero (masks overlap) and PROCEEDS when AND==0. The source returned -5 when
   AND==0. Fixed to `if ((permMask & slotMask) != 0) return kItemUseBlocked;`.

2. **Law-violation kind byte source wrong.** Disasm 0x56746f `mov eax,[ebp+5]`;
   0x567478 `sar eax,18h` -> kind = HIBYTE(*(DWORD*)(slot+5)) = byte at slot+0x08
   = LOBYTE(lawKind). Source used `(effectAndKind>>24)` (high byte of +0x14).
   Fixed to `(u8)(slot.lawKind & 0xFF)`; header comment corrected.

3. **Effect-callback test masked.** Disasm 0x56727c `cmp dword [ebp+14h],0` tests
   the FULL +0x14 dword (a function pointer). Source masked `& 0x00FFFFFF`. Fixed
   to test/pass the full `effectAndKind`.

Result codes (-1..-5), a2[4] clear-on-entry, 373 success gate (`<dbl_624E20`
0.66, fcomp/jb), spinResult a2[3], dispatch 0x2D, perm-grant (slot+0x10),
objectTrigger-vs-plain-use (slot+0x04), violation guard (slot+0x08 != -1), player
panel (actor+2==6 && !a2[4], render msg iff a2[3]==1) — all match. **VERIFIED-1:1.**

#### Goldens corrected (uipanels_wave20_test.cpp)
The test encoded the same three bugs; rewritten to the binary:
- PermissionBlocked: AND==0 now proceeds (result 1); overlap (AND!=0) -> -5.
- 373 / EffectCallbackAbort / CommandPathsAndEffects: actor.permMask set to a
  value that does NOT overlap the slot mask so the body runs.
- CommandPathsAndEffects(b): kind now sourced from `lawKind` low byte (lawKind=5,
  expect kind==5); effectAndKind=0.

## Counts
- Functions diffed against disasm: 14 (5 capacity + 3 wealth + 2 find-slot +
  GetEffectiveStock/GetSlotCapacity + item_use + 6 inventory2 UI spot-check).
- Binary divergences found & fixed: 3 (all in item_use.cpp).
- Wrong goldens fixed: 1 file (uipanels_wave20_test.cpp), 4 test bodies.
- VERIFIED-1:1 functions: all addressed functions in the 6 files.
- Tests: 7 targets pass (4 unit + 3 itest/e2e).
