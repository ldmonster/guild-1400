# HARDEN world_06 — 1:1 verification vs gilde.exe (IDA MCP)

Chunk: `src/world/{wire_event_office, wire_history_amt, wire_location, wire_meister_loc,
wire_privilege_panels_a, wire_privilege_panels_b, wire_production, wire_worldnet,
world_economy2, world_economy3, world_history2, world_io_save_recon, world_setup}`.

Method: every function with `gilde.exe 0x…` provenance decompiled (and disasm'd where
the decompiler degraded registers); every `@0x…` const table fetched with `get_bytes`
and diffed programmatically. Fixes only where the binary proves a divergence.

## world_io_save_recon.cpp — 3 FIXED, rest VERIFIED-1:1

| Item | Address | Verdict |
|---|---|---|
| WrObject (WriteObject) | 0x5e5ab4 | VERIFIED-1:1 — flag bits, name select (disasm 0x5e5ae7: subtype==1 → switchVar=node[534], `!`-prefix skip), dword-pair fields (+512, sar24 of +532, movsx switchVar), all 5 switch bodies (case2/3 vec3 bases 0x9C/0xA8 stride 24 and case5–8 first vec3 +0x5C confirmed by disasm 0x5e5db7/0x5e5e5a), child/sibling link walks, trailing WriteEventNames |
| WrBuildingData | 0x5e5f74 | **FIXED**: the room-scenery `+24` array was written when `roomCount == 0`; the binary writes it when roomCount is **nonzero** (0x5e6054 `cmp byte [edi+1C6Dh],0; ja loc_5E6186`; 0x5e6186 = `WriteArray(n, [edi+18h], n)` then the room loop). Condition inverted to `roomCount != 0`. Everything else (WriteArray count/stride order per call-site disasm 0x5e6011/0x5e6024, room fields +0x72&0xF / +0x68&1 / +8 / +9 / +10 bits / dwords 20-12-16 / vec4 24/40 / sar24 of +337, 8×64-byte string block 0x5e5fb4, tail +0xA0/+0xC4/vec3 +0x90) VERIFIED-1:1 |
| WrObjectCallback | 0x5e61ec | VERIFIED-1:1 (WriteByte(1); sever +496; recurse; restore — disasm confirmed) |
| Bio_WriteByte/Dword/DwordPair/String/Vec3/Vec4/Array | 0x5dc8cc/0x5dc918/0x5dcac0/0x5dc8ec/0x5dc9dc/0x5dca40/0x5dcbb0 | VERIFIED-1:1 (DwordPair stages two dwords, writes only the first; Array writes count dword then stride dword then count×stride payload) |
| UtilStrChr | 0x5d3ef0 | **FIXED**: the binary scans the whole string overwriting the hit on every match — it returns the **LAST** occurrence (strrchr semantics: `do { if (c==*s) v2=s; } while (*s++);`). The reimpl returned the first occurrence, which mis-trims multi-underscore sub-record names in WrObject case 1/4 |
| kCType64A208[128] | @0x64a208 | **FIXED**: 4-byte transcription shift — indices 92/98/104/124 were 0x48/0x0c/0x98/0x88, binary has 0x0c/0x98/0x88/0x0c (the `[`…`` ` `` gap, `a–f`, `g–z`, `{…~` runs were each off by one). Byte-diff now identical. (No behavioral impact through the sole `&0x20` use — bit5 only lives in the digit entries — but the table is provenance-carrying and must be byte-exact) |
| WriteEventNames default | 0x5f4b60 | **FIXED**: the binary's null-list terminator is `Bio_WriteDwordPair(h, 0)` — a **4-byte** 0 count dword, not the single 0 byte the default hook emitted. Default now writes the dword; a non-null list has its 7×132 nonzero-entry count reproduced, and a non-empty list without the cross-module `VIBE_EventTable_LookupIdToName` hook fails the sink instead of faking bytes. Golden pins updated in `world_io_save_recon_test.cpp` (3 sites, 1-byte → 4-byte terminator, old→new documented in-test) |

## world_economy2.cpp/.h — 3 FIXED, rest VERIFIED-1:1

| Item | Address | Verdict |
|---|---|---|
| Constants kPay*/kMenuPrice*/kActionRollGate | @0x61AF20..0x61AF60 | VERIFIED-1:1 via get_bytes (0.3f, 0.2, 0.4f, 1.2f, 15.0f, 5.0, 2.55, 0.3f; the election/action copies bit-identical) |
| ComputeOfficePaymentAmount | 0x482299 / 0x4829xx | VERIFIED-1:1 (fstp rating to float, then one double x87 chain into ConvertX; hook precision fixed below) |
| ComputeAppointmentPrice | 0x481f1c..0x481f40 | **FIXED**: the high (objectKind==1) branch was a single-precision float chain; the binary keeps BOTH branches on the FPU at double precision with one trailing `fstp` to the 4-byte float v35. Now `(float)(r1 * ((double)rating * 0.4f) + 1.2f)` etc. |
| RollActionDirection | 0x4825c9..0x4825e5 | **FIXED** (precision): 0x4825ce `fst v64; fcomp flt_61AF50` compares the **double** roll against 0.3f; the reimpl rounded the roll to float first. Now double compare |
| WorldEconomy2Hooks.randFloatScaled | 0x58b910 | **FIXED**: `VIBE_Math_RandomFloatScaled` returns DOUBLE (`(double)(int)RandNext() * flt_62675C`); the hook returned float, rounding every roll before use. Hook type changed to `double (*)()` (installers = this module's tests only; wire_economy2 does not bind it) |
| ScanMaxCandidateRank | 0x481e3e..0x481e6f | VERIFIED-1:1 (u8 max over +583 of 589-stride records; caller aborts on 0) |
| CountOfficeDependents | 0x4822c6..0x482390 | **FIXED** (3 proven divergences): (1) per-head severance is `trunc(totalAmount/count) + 32` — `add ebp, 20h` @0x482390 — the +32 was missing; (2) the coord27 fan-out value is `-payment` (the freshly rolled v27 from var_18), not `-totalAmount` (the a2+8 severance pool) — the two sources are independent in the binary, so a separate `paymentAmount` parameter was added; (3) both coord27 ids come from `dword_12CE914[134*…]` (the +4 object-id), not the compared +364 entity nor the raw index — dead `officeIds` param replaced by `personObjIds`. Test pins updated (250→282, 450→482, 500→532, 200→232, e2e 900→932) with the address cited in-test |
| ComputeGridLabelOffset | 0x558200 (@0x5583c0 site) | VERIFIED-1:1 (`(slotX>>16) + 64 - (camX>>16)`) |

## world_economy3.cpp/.h — 1 FIXED, rest VERIFIED-1:1

| Item | Address | Verdict |
|---|---|---|
| GuildOfficeNameTextId | 0x520f98/0x5210e4/0x521234/0x520838 | VERIFIED-1:1 (promoted byte → defKind+560 else +525, confirmed in all four dialogs) |
| GuildCandidateOfficeNameId | 0x557d27 | VERIFIED-1:1 (`(rec+12 == 0) + 1596`) |
| GuildCandidateOutputRatingId | 0x557e4e | VERIFIED-1:1 (bit-pattern compares 1120403456/1135542272/1143111680 == float compares for non-NaN; exact nesting 1602/1603/1604/1605) |
| GuildElectionTitleTextId | 0x557aa9 | VERIFIED-1:1 (0x9D vs `$Z$[%s$]`) |
| GuildElectionCollectMode | 0x557b37 | VERIFIED-1:1 (0<a1<7 category / ==7 elective / else abort) |
| GuildLevel3RankAction / DialogForDefKind | 0x5213ad / 0x521412 | VERIFIED-1:1 (1/-1/else; 30→A, 31/33→B, 32→C) |
| GuildLevel3ContactStatusId | 0x521537 | VERIFIED-1:1 (30→4751, 31/33→4733, 32→4743) |
| GuildLevel2JoinFee | 0x520838 (0x520880) | **FIXED**: the `>` branch `fstp`s the fee into a **4-byte float** (0x520a8e `fstp [ebp+var_10]`, the same slot loaded as float 160.0 at 0x520886) before the ConvertX truncation — the reimpl truncated the double. E.g. wealth 1'000'000 → 9999.99978 → **10000.0f** → fee 10000 (was 9999). Constants flt_62235C/flt_622360 byte-verified (0x3C23D70A / 0x43200000). Pins updated: 9999→10000, 19999→20000, 49999→50000, e2e 499→500 |
| GuildAgendaBucket | 0x5584c8 (0x5585d5/0x558666) | VERIFIED-1:1 (24-byte entries, +16 state: 2→member, 3→successor) |

## world_history2.cpp — all VERIFIED-1:1

| Function | Address | Verdict |
|---|---|---|
| He_ValidatePunishmentType | 0x4c45e4 | VERIFIED-1:1 (-1 / ≥10→-3 / 5→+358 gate / 7→+13 gate) |
| He_NullHandler | 0x4c5244 | VERIFIED-1:1 (empty) |
| He_LogInvalidAllocType / He_LogInvalidRunType | 0x4c51b4 / 0x4c51fc | VERIFIED-1:1 (format strings + 260/256 buffers; player name = word_12CE910[268*u16(rec+8)+24] surfaced as caller param, documented) |
| He_ProcessAllPlayerNews | 0x4c5018 | VERIFIED-1:1 via disasm (edx 0..0x2FF, ax=dx per call) |
| He_DestroyIconGfx | 0x4c68d8 | VERIFIED-1:1 (mesh clear, slot-switch bracket via prev-slot global == hook return contract, FindByHandle-gated ArrangeIconsInCircle, detach, -1/0 resets; both branches) |
| He_DestroyStaleIcons | 0x4c6c50 | VERIFIED-1:1 (64 slots, `*(parent+4) != entityId \|\| !iconsEnabled` → destroy; advance semantics identical) |
| He_DestroyIconsForEntity | 0x4c6ca4 | VERIFIED-1:1 (matches the +8 field — the owning-parent pointer, per CreateIconMesh @0x4c662c `*(a1+8)=a2`) |
| He_CreateGfxInfo | 0x4c67e0 | VERIFIED-1:1 (enabled gate, first-free scan over the +4 parent column with the 0-entry pre-check, pool-full ==64 abort, FindByHandle gate, slot writes v5[1]/v5[2]/*v5/*(v3+136), CreateIconMesh, restore switch, invalid-parent sprintf). Return value in the binary is spoiled al garbage (SwitchActiveSlot's return in both branches); callers (VIBE_He_AssignIconForHandler 0x4c6964) never consume it |

## world_setup.cpp — all VERIFIED-1:1

| Function | Address | Verdict |
|---|---|---|
| WorldCountActiveObjects | 0x5839f0 | VERIFIED-1:1 (StrCmpNoCase walk over dword_13CE27C +1, stride 65, bound 47515=65*731, post-increment bound check, index return) |
| StrCmpNoCase (local leaf) | 0x5cb8f0 | VERIFIED-1:1 (A–Z +32 fold, `v3 != v4 \|\| !v4` exit, unsigned-byte difference) |

## wire_*.cpp — glue only, claims spot-verified

- `wire_event_office`: adapter claim for 0x539534 (CheckCumulativeStats) VERIFIED — the
  decompile reads only the row (a2+16), never the person (a1). Hook targets' addresses match.
- `wire_meister_loc`: Building_LookupTypeRecordA @0x589778 VERIFIED to produce a 6-byte
  record (dword @+0, word @+4 from byte_649910) — the RealMissionNameHooks 6-byte copy and
  return 6 are faithful to the 0x59b8cc qmemcpy site.
- `wire_privilege_panels_a/b`: all leaf-id constants resolve to the named
  VIBE_Privilege_Panel* function starts (spot-checked 0x563000/0x561bb4/0x5651bc/0x571218/
  0x5643e8/0x565b88 via lookup_funcs).
- `wire_history_amt`, `wire_location`, `wire_production`, `wire_worldnet`: pure hook
  installers with documented inert fields; the cited leaf addresses/reasons are consistent
  with the binary — no logic to diff. VERIFIED as glue.

## Tests (all green after fixes)

| Target | Checks |
|---|---|
| world_economy2_test / _itest / _e2e_test | 21 / 10 / 9 |
| world_economy3_test / _itest / _e2e_test | 59 / 7 / 30 |
| world_io_save_recon_test | 13 |
| world_history2_test / _itest / _e2e_test | 53 / 9 / 27 |
| wire_event_office_test | 23 |
| wire_history_amt_test | 27 |
| wire_location_test | 6 |
| wire_meister_loc_test | 23 |
| wire_production_test | 20 |
| wire_worldnet_test | 29 |
| wire_privilege_panels_b_itest | 13 |
| world_bootstrap_test / _e2e_test | 79 / 36 |
| he_messaging_test | 61 |

Total: 555 checks, 0 failures across 20 targets (built individually per the campaign
build rule; no whole-tree build, no git commands).
