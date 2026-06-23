# Hardening pass — sim/building2,3,4 (1:1 verification vs gilde.exe)

Scope: `src/sim/building2.cpp`, `building3.cpp`, `building4.cpp` (+ headers + tests).
Method: every `0xADDR` function decompiled AND disassembled via IDA MCP, diffed
line-for-line against the source. Every numeric constant table verified with
`get_bytes`. Files compile with `g++ -std=c++17 -fsyntax-only`.

## Tables — all byte-exact (get_bytes confirmed)
- `kTypeRecordTableA` @0x649910 (76x6) — VERIFIED byte-exact.
- `kTypeRecordTableB` @0x649AD8 (27x6) — VERIFIED byte-exact.
- `kTypeNameTable`    @0x63C4F8 (12x44, aBkBrunnen) — VERIFIED byte-exact.
- `kOpenHoursTable`   @0x63C70D (16x3 records) — VERIFIED (values match; table base
  is 0x63C70D, NOT 0x63C708 which has a 4-byte zero prefix).
- `kTypeStringIdTable`@0x582F68 (43x8 pairs, terminator {0,-1}) — VERIFIED byte-exact.
- `kDefaultObjectsTable`@0x6496A9 (23x21 +2 slack) — VERIFIED byte-exact; inner loop
  walks 10 proto WORDs at record offsets +4..+22 (last two spill, hence +2 slack).

## building2.cpp
| fn | addr | verdict |
|----|------|---------|
| LookupTypeRecordA | 0x589778 | VERIFIED-1:1 (signed `a1<76` edge noted; only differs for code>=128, invalid) |
| LookupTypeRecordB | 0x5897c8 | VERIFIED-1:1 (same idiom) |
| MatchTypeCode | 0x589960 | **FIXED** — wrong array |
| MatchProfessionCode | 0x5898e8 | **FIXED** — wrong array |
| MapTypeToState | 0x592a5c | VERIFIED-1:1 (switch exact) |
| GetCategoryForObject | 0x589d24 | VERIFIED-1:1 (caller-resolved params) |
| CollectObjectSlots | 0x58faa8 | VERIFIED-1:1 (589-stride correct) |
| CollectStorableSlots | 0x58fb30 | VERIFIED-1:1 (pre/post-inc quirk) |
| CollectSlotsAfterObject | 0x58fbb4 | **FIXED** — find-key masked afterSlot |
| CollectFlagNodeCallback | 0x588044 | VERIFIED-1:1 (store-before-inc order) |
| FindOwnedDungeonSlot | 0x594cfc | VERIFIED-1:1 |
| HasActiveOffice | 0x58a280 | VERIFIED-1:1 (hook -1==not-found ↔ original 0==null) |
| FindOfficeStorage | 0x58a354 | VERIFIED-1:1 (kind==1?277:322) |

### Fixes
1. **MatchTypeCode / MatchProfessionCode (0x589960 / 0x5898e8) — WRONG ARRAY.**
   Original copies the record from `word_12CE910[268*a1]` — the person/family
   array, **byte stride 536** — NOT the 589-stride building-type table. The source
   used `TypeRec` (589). Added a `personFamilyBase` binding (word_12CE910, 536) and
   `PersonRec()` helper; matchers now index it. Field offsets (+356 / +358,+361)
   unchanged. MatchTypeCode also made the +356-byte vs code compare SIGNED
   (`movsx`/`sar` in disasm). Evidence: disasm 589988 `lea esi, word_12CE910[eax*8]`
   with `eax = 268*a1`; 5899bd `sar esi,18h` / 5899c0 `movsx ecx,bl`.
   Tests updated: building2_test now builds a 536-stride `PersonRecBuilder` and binds
   via `personFamilyBase`.

2. **CollectSlotsAfterObject (0x58fbb4) — find-key over-masked.** Original loads the
   search key `*a2` with `movsx` (signed int16) and compares to the masked
   (`&0x7FFF`, zero-ext) room entry; it does NOT mask `*a2`. Source masked
   `afterSlot & 0x7FFF`, which would let a high-bit afterSlot match a masked entry.
   Fixed: `target = (int)(int16)afterSlot`, no mask. Evidence: disasm 58fbfc
   `movsx esi, word ptr [edi]` vs 58fbf3 `and ch,7Fh` + 58fbf6 `and ecx,0FFFFh`.

## building3.cpp
| fn | addr | verdict |
|----|------|---------|
| CheckTimeWindowOpen | 0x51dc04 | VERIFIED-1:1 (table+logic; caller passes the +0 kind byte, doc clarified) |
| LookupTypeName | 0x50c738 | **FIXED** — copied 32 fixed bytes; original copies NUL-terminated |
| LookupTypeStringId | 0x587fcc | VERIFIED-1:1 (low-word match, +1433, caller category) |
| FindWorkProductObject | 0x587674 | VERIFIED-1:1 (MapKindToCategory(rec[0]) == MapTypeToCategory(building[0])) |
| FindActiveWorkSlot | 0x586904 | VERIFIED-1:1 (protos 146..151; +20/+93 hook handoffs) |
| FindNearestSameType | 0x587908 | **FIXED** — production-skip + match-on-kind |
| FindNearestVacantSameType | 0x587a28 | **FIXED** — production-skip (match-on-index kept) |
| UpdateOccupantCategory | 0x58a044 | **FIXED** — wrong write values/groups |
| FreeAndUnlink | 0x586d6c | VERIFIED-1:1 (768x536 unlink; pointer-as-id handoff) |
| ReleaseOccupantHoldings | 0x589468 | VERIFIED-1:1 (call order; return-value hook-boundary noted) |
| DetachAndDestroyOccupant | 0x588d00 | VERIFIED-1:1 (hook handoffs: +132 write, RemoveById args) |
| CollectByCityHandle | 0x591870 | **FIXED** — filter field read at byte +1 |
| CollectOwnedByPerson | 0x5918e0 | VERIFIED-1:1 |
| ResetAllBuildings | 0x5896fc | VERIFIED-1:1 (768 cleanup, 16 lights stride 82, 3 sel globals) |

### Fixes
3. **LookupTypeName (0x50c738) — copy quirk.** Original copies the matched row name
   byte-by-byte in 2-byte steps, stopping at the first NUL (inclusive); it does NOT
   write a fixed 32 bytes. Source used `memcpy(out, base, 32)`, zeroing the
   destination's trailing bytes the original leaves untouched. Replaced with the
   verbatim NUL-terminated 2-byte-granular copy. Evidence: disasm 50c788..50c79e.

4. **UpdateOccupantCategory (0x58a044) — wrong family-record writes.** Original
   (disasm 58a0bf..58a14f):
   - FIRST write keys+values on the occupant's CURRENT +356 byte (`directCat`):
     `family[112 + GroupFromCode(directCat)] = directCat`.
   - SECOND (clamp) write keys on `GroupFromCode(recCode)` and writes **recCode**
     (`dl`), gated on `recCode < (i8)family[g2+112] || family[g2+112]==0`.
   - occupant's +356 byte is overwritten with **recCode** in ALL paths.
   Source wrote `directCat` everywhere and used `GroupFromCode(recCode)` for the
   first write. Rewrote the body to match; tests recomputed (building3_test x3,
   building3_itest, building3_e2e). The `dword_12CEA84` (+372) cache-clear remains a
   documented caller side-effect (not exposed by the current param set).

5. **FindNearestSameType / FindNearestVacantSameType (0x587908 / 0x587a28) —
   production predicate.** Source skipped on `tr[0]==6`; the original calls
   `IsProductionType` (kind in {11,12,13,16,28}). Replaced with the real sibling
   `Building_IsProductionKind(tr[0])`. Additionally: FindNearestSameType matches the
   candidate's **resolved KIND byte** (`*(base+589*it[0])`) against `typeByte`
   (disasm 587954/58799f), where the source compared the raw +0 index — FIXED to
   `tr[0]==typeByte`. FindNearestVacantSameType matches the raw +0 INDEX (disasm
   587a65/587a9b) — that comparison was already correct and is kept.

6. **CollectByCityHandle (0x591870) — unaligned filter field.** Original reads the
   filter compare value at `*(int*)(a2 + 1)` (byte offset +1). Source read
   `filter[1]` (byte +4). Fixed with a `memcpy` from `(u8*)filter + 1`. Evidence:
   disasm 5918cc `mov edx, [esi+1]`. (Existing tests pass filter=null; unaffected.)

## building4.cpp
| fn | addr | verdict |
|----|------|---------|
| EnsureDefaultObjects | 0x586df8 | VERIFIED-1:1 (10 proto WORDs +4..+22; room field reads via hook) |
| InitWorkerCapacities | 0x586ed8 | VERIFIED-1:1 (min-2 clamp; proto42/278) |
| FindStorableObject | 0x5877ac | VERIFIED-1:1 (production/kind-4/iterate; hook-ABI flag noted) |
| FindUpgradeStorage | 0x58a294 | VERIFIED-1:1 (cat5 switch, cat3; protos 322/277) |
| AttachStorageRooms | 0x588554 | VERIFIED-1:1 (owner +39; caller type-record read) |
| RemoveStorageRoom | 0x588ce4 | VERIFIED-1:1 (-2 -> -3 else 0) |
| SyncProfessionState | 0x58a154 | **FIXED** — defaultProf sign-extension |
| EvalBuyBuilding | 0x46c97c | VERIFIED-1:1 (handler[43]==targetId, handler[44]; ret 16) |

### Fixes
7. **SyncProfessionState (0x58a154) — sign of the +357 byte.** The default-profession
   value is `*(int*)(rec+0x162) >> 24` (arithmetic `sar`, sign-extended) `- 1`. Source
   did `int(uint8) - 1` (zero-extended). Changed to
   `int(int8(defaultProf)) - 1`. Field offsets 357/372 and group args (1,1)/(4,1)
   confirmed. Evidence: disasm 58a200 `sar edx,18h; dec edx`.

## Counts
- Functions verified: **33**.
- VERIFIED-1:1 (no churn): **26**.
- FIXED to the binary: **7** (MatchTypeCode, MatchProfessionCode,
  CollectSlotsAfterObject, LookupTypeName, UpdateOccupantCategory [+groups+values],
  FindNearestSameType [+Vacant production-skip], CollectByCityHandle,
  SyncProfessionState — note FindNearest counts as one fix area touching 2 fns).
- Tables verified byte-exact: **6**.
- Tests updated for golden corrections: building2_test (matcher binding),
  building3_test (UpdateOccupantCategory x3), building3_itest, building3_e2e.

## Cross-file handoffs (callers must satisfy; outside owned files)
- `building2`: new `BuildingArrayBindings::personFamilyBase` (word_12CE910, stride
  536) must be set by the loader for MatchTypeCode/MatchProfessionCode.
- `CheckTimeWindowOpen`: caller passes the building-TYPE record +0 KIND byte
  (`*(buildingTypeBase + 589*building[0])`), not the raw index.
- `FreeAndUnlink`: `recId` is the record pointer/handle value; `indexArrayA/B` are
  the dword bases of dword_12CEA7C/dword_12CEA80 (768 entries, dword stride 134).
- `UpdateOccupantCategory`: `directCat` is the occupant's CURRENT +356 byte;
  `dword_12CEA84[134*idx]` (+372) cache-clear is the caller's responsibility.
- Hook-boundary opaque-handle reads kept verbatim where the engine field
  (room+0x14/+2, slot+20, rec+93 container, +132 result write) is unavailable in
  the headless build — documented inline in each function.
