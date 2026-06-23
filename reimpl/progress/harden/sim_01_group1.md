# Hardening sweep — sim group 1 (building_type / buildingtype_recon / buildingtype_callers)

MCP live (gilde.exe, imagebase 0x400000). Every provenance-carrying function in the
three owned .cpp files was decompiled + disassembled and diffed line-for-line against
the binary. Epilogue return constants were confirmed via `get_bytes` (raw `B0 NN C3`
== `mov al,NN; ret`).

## building_type.cpp

Shared epilogue table verified by raw bytes:
- 0x589850 `b0 02 c3`→2 · 0x5898E2 `30 c0 c3`→0
- 0x589A32→15 35→4 38→7 3B→23 3E→24 41→25 44→26
- 0x589AA1→5 A4→8 A7→9 AA→14 AD→16 B0→19 B3→20 B6→21 B9→22
- 0x589C74→6 C77→3 · 0x58A272→11 275→12 278→1 27B→10

| Function | Addr | Verdict |
|---|---|---|
| BuildingType_GroupFromCode | 0x58a4c8 | VERIFIED-1:1 (all 13 ranges + default match decompile + epilogues) |
| BuildingType_GroupFromPairCode | 0x58a54c | VERIFIED-1:1 (cases 9-12 absent → default 0, matches) |
| BuildingType_ComputeRankWithinGroup | 0x58a560 | VERIFIED-1:1 (12 ranges, `hi - code + 1`, signed/u8 equivalent in range) |
| BuildingType_MapToActionCode | 0x589a7c | VERIFIED-1:1 |
| BuildingType_MapToCategoryCode | 0x589af0 | VERIFIED-1:1 |
| BuildingType_MapToProfessionCode | 0x589be4 | VERIFIED-1:1 (case6→23,7→20,8→26,9→14 via epilogues) |
| BuildingType_MapActionToCategory | 0x58a25c | VERIFIED-1:1 |
| BuildingType_ComputeVariantIndex | 0x589cb0 | VERIFIED-1:1 (`v3=a2-1`, byte `base-v3`, all 12 bases) |
| BuildingType_ClassifyByRange | 0x589c20 | VERIFIED-1:1 (signed cmp; ReturnCode7=0x589A38→7, ReturnCode4=0x589A35→4) |
| Building_ClassifyTypeFlag | 0x589818 | VERIFIED-1:1 (unsigned cmp; a1==52, a1==1 edges exact) |
| Building_IsTypeInGroup | 0x5898cc | VERIFIED-1:1 (default→0x589850→2) |
| Building_MapKindToCategory | 0x5878b0 | VERIFIED-1:1 (switch arms exact; the `*(base+589*idx)` deref is hoisted into callers per codebase-wide contract — many files depend on the `(u8 kind)` signature) |
| Building_IsStorageKind | 0x587f50 | VERIFIED-1:1 (== 10; deref hoisted) |
| Building_IsProductionKind | 0x587f80 | VERIFIED-1:1 (11/13/12/16/28; deref + null-guard hoisted) |

## buildingtype_recon.cpp

| Function | Addr | Verdict |
|---|---|---|
| Util_StrCmp | 0x5d3f10 | VERIFIED-1:1 (dword-unrolled original; byte-loop equivalent — all callers test ==0 only, magnitude irrelevant; original normalizes to ±1, ours returns byte diff, same sign) |
| Util_StrCmpNoCase | 0x5cb8f0 | VERIFIED-1:1 (disasm-confirmed `(int)v3-(int)v4`, A-Z fold +32) |
| Bauplatz_GetSize | 0x577628 | VERIFIED-1:1 (stride 96, StrCmpNoCase loop, count guard) |
| Bauplatz_MapOneToSupermap | 0x5774b8 | VERIFIED-1:1 (4 corner picks dword[16/17/18/20/22] exact: (16,17,18)(20,17,18)(20,17,22)(16,17,22); raster fill 255) |
| Building_StorageSecurityClamp | 0x588988 | VERIFIED-1:1 (floor 2 storage & floor 6 market clamp arms verified; marketSingle max-with-floor exact) |
| Building_AllocStorageRoom | 0x588988 | VERIFIED-1:1 (arithmetic core; AddObjekt/QueryFind room enumeration = scene-graph hook boundary, Rule 8) |
| Building_PickName | 0x504a54 | VERIFIED-1:1 (empty→taken(v15>=256)→len>=0x20 filter order; exactly one RandomModulo draw when v30!=0; chosen-too-long reject; survivor cap is safety no-op for count<=12) |

## buildingtype_callers.cpp

| Function | Addr | Verdict |
|---|---|---|
| Building_IsNameTaken169 | 0x504b65/0x5871b4 | VERIFIED-1:1 (169 stride × 256, `n<256`==taken) |
| ObjectTypeByte helper | (65-stride) | VERIFIED-1:1 (`base[65*proto]`; stride 65 confirmed at 0x49735c/0x4973f8/0x496d8c) |
| Scene_SyncMeisterBuildings | 0x504ce0 | VERIFIED-1:1 (anchor scan rec[2]==12/rec[12], 536-byte stride, v5 early-stop; kind-11 shop scan; flags 0x40/4 gate RegisterNames) |
| Building_RegisterNames | 0x504a54 | VERIFIED-1:1 (two passes, kind!=28 gate, NameRegistry169 pick) |
| Building_CreateGebaeudeFlow | 0x586fb8 | **FIXED** — LABEL_71 (AddObjekt 437 + node stamp +28/+29=0xFF,+30=-1,+34=0) was gated on `prot==20\|\|prot==22`; disasm at 0x5874c0 (`cmp bl,15h;jnb 5874C0` then `jbe loc_5874D9`) proves **prot 21 (0x15)** also reaches LABEL_71. Added `\|\| prot==21`. All other field stores / room-slot loop / storage-node queries verified. |
| Sell_DepleteSourceStockNode | 0x496b90 (0x496ec2) | VERIFIED-1:1 (`*(short*v56+7)`=byte+14 count decrement; v20<=0 → type 2/6 RemoveStorageRoom else RemoveByProt; !srcBuildingRec→abort) |
| Sell_EnsureDestStorageNode | 0x496b90 (0x496f4b) | VERIFIED-1:1 (no-node: type 2/6 AllocStorageRoom else AddObjekt+container bump with (tb 23/37 & dt 42/278 & owner kind 6/7)→+28=4; existing: +14 += qty; LABEL_58 dword_631290 = node+2) |
| Bauplatz_MapAllToSupermap | 0x577464 | VERIFIED-1:1 (FilterBlockedBauplatze frame, loop v6[v5/4] over 4*result, MapOne per survivor; 256-cap is safety) |

Note: 0x5cb8bc (brief's "maybe strcmp helper") is **VIBE_Util_RandNext** (the RNG, adjacent to
StrCmpNoCase@0x5cb8f0) — not referenced by any owned file. No issue.

## Counts
- Total provenance functions verified: **24** (14 building_type, 7 buildingtype_recon, 9-ish caller entries — see tables; helpers counted).
- VERIFIED-1:1: 23
- FIXED: 1 (Building_CreateGebaeudeFlow — dropped prot-21 LABEL_71 branch)
- BOUNDARY: scene-graph leaves (AddObjekt/QueryFind/room enumeration) correctly routed
  through hooks per Rule 8; ExSellObjekt non-storage phases & the +35 raw-material tail
  are documented named gaps owned by trade_sell — out of this chunk's scope.

## Tests
Added 2 golden tests (`Label71Prot21AlsoStampsNode`, `Label71Prot22AlsoStampsNode`) in
tests/unit/buildingtype_callers_test.cpp encoding the now-correct LABEL_71 reachability.
Built + ran: buildingtype_callers_test (152 checks), buildingtype_recon_test (89),
sim_building_test (109) — all pass, 0 failures.
