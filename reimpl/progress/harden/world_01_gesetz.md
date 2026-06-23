# Hardening sweep — world_01_gesetz chunk

Files audited (every `gilde.exe 0xADDR` provenance function diffed line-for-line
against MCP decompile + disasm + get_bytes):

- src/world/gesetz_flow.cpp
- src/world/gesetztable_law_recon.cpp
- src/world/groundplan_recon.cpp

Tests touched: tests/unit/world_office_flow_test.cpp (the only test exercising
GesetzSaveState/GesetzLoadState; gesetztable_law_recon_test.cpp /
groundplan_recon_test.cpp / groundplan_test.cpp reviewed, no changes needed).

## Counts
- Functions audited: 9
- VERIFIED-1:1: 8
- FIXED: 1 (GesetzLoadState clear-remainder loop)
- BOUNDARY: 0 new (command-queue / person / history side effects already modeled
  via hooks; see per-function notes)

---

## gesetz_flow.cpp

### 0x4c247c — VIBE_Gesetz_RequestApply → GesetzRequestApply  — VERIFIED-1:1
- `a2 >= 26` guard, 36-byte record copy, person-present/valid (`*a1 != 0xFFFF`,
  `*(a1+4)` id, null→-1) all match.
- Clamp `[v5[1], v5[2]]` (record +4 lo, +8 hi) matches: `value>=lo` then
  `value>hi → hi`, else `value=lo`.
- Tail call `VIBE_Command_RequestBuildOp70(&v6, v5[1])` → `VIBE_Command_EnqueuePacket`
  (0x4954cc/0x49388c): the command-queue/network enqueue subsystem, out of this
  file's call tree. Modeled by the command hook; the hardcoded `return 0` stands
  in for the enqueue result. BOUNDARY (pre-existing, documented). Packet layout
  (id@+0, lawId@+4, value@+5; RequestBuildOp70 copies 8 bytes + 1 byte) confirmed
  via decompile — matches the GesetzCommand the hook receives.

### 0x4c24f8 — VIBE_Gesetz_ApplyAndNotify → GesetzApplyAndNotify  — VERIFIED-1:1
- `*(char*)(a1+4) >= 26` (signed) guard matches.
- `dword_631EB0[9*id] = *(a1+5)` == `g_lawTable[id].threshold = newThreshold`
  verified: 0x631EB0-0x631E98 = 0x18 = +24 = LawRecord::threshold; `9*id` dwords
  = 36*id bytes = stride. Correct.
- Notify guard `dword_12CE914[134*word_63CC5C] == *a1 || !FindRecordById(*a1)`
  → modeled `initiatorId == localMasterId || !initiatorResolves`. The local-master
  lookup (dword_12CE914 / word_63CC5C) + VIBE_Person_FindRecordById +
  BuildPenaltyText + History_NotifyLawChangeToMaster are the person/history
  subsystems; modeled via the notify hook. BOUNDARY (pre-existing).

### 0x4c258c — VIBE_Gesetz_FindRecordByPair → GesetzFindRecordByPair — VERIFIED-1:1
- `byte_631EB4[v5]` (+28) and `byte_631EB5[v5]` (+29), v5 stride 36, bound
  `v5 >= 936` (26 records). Offsets confirmed: 0x631EB4-0x631E98=28, +29.
- The `v8 = v6 ^ v7; LOBYTE(v8)=byte_631EB5[v5]` obfuscation: reached only when
  `v7==v6`, so `v6^v7==0` and `v8` is the zero-extended +29 byte; `a2==v8` is the
  byte compare the recon's `op == b29` performs. Correct.
- 0x24-byte record copy on hit. Match.

### 0x4c25e0 — VIBE_Gesetz_SaveState → GesetzSaveState  — VERIFIED-1:1
- Order: lawCount(26) → 26×threshold (dword_631EB0 stride 36) → marker
  (dword_632240) → count active crimes (i!=23040 step 45 ⇒ 512 records, id!=-1)
  → count active evidence (j!=4096 step 2, owner!=-1 && crimeId!=-1) →
  crimeCount → evidenceCount → each active crime's 9 fields → each active
  evidence pair. All confirmed.
- WriteCrime field offsets/sizes {+0:4, +4:14, +18:4, +22:4, +26:2, +28:1, +29:4,
  +33:4, +37:4} = 41 bytes match the v8+N writes exactly.

### 0x4c28d8 — VIBE_Gesetz_LoadState → GesetzLoadState  — FIXED
- Header (lawCount==26, 26 thresholds, marker), version gate
  (`dword_649D4C < 0x10041` keeps defaults 128/512 else reads counts), crime
  read loop (45-stride, 9 fields), evidence reset + read loop: VERIFIED.
- Evidence reset confirmed: `VIBE_Light_SetGrayColorThunk(-1, 0x4000, dword_11C2160)`
  → byte memset of **0x4000 = 16384 bytes = 4096 dwords = 2048 pairs** with 0xFF
  (decoded 0x5c6af0 + 0x5f8160). Recon's reset of both 4096-entry modeled arrays
  covers all 2048 pairs. Correct.
- **FIX — clear-remainder loop (LABEL_27, 0x4c2af6..0x4c2b26).** Disassembled the
  loop (0x4c2adb setup: `eax = ebp*4 - ebp = 3v4; eax<<=4; eax-=edi ⇒ 45*v4`;
  `eax += 0x2D` BEFORE each store) and decoded every store against base
  dword_11BC760:
  - `dword_11BC733[eax]=-1` → +0   (id)
  - `dword_11BC745[eax]=-1` → **+18** (unnamed pad4 dword)
  - `dword_11BC749[eax]=-1` → +22  (perpetrator)
  - `dword_11BC758[eax]=0`  → +37  (provenState)
  - `word_11BC74D[eax]=0`   → +26  (wanted, 16-bit store)

  Before: the recon cleared `target` (+33) instead of +18, and never cleared +18:
  ```cpp
  g_crimeTable[i].id          = -1;
  g_crimeTable[i].perpetrator = -1;
  g_crimeTable[i].target      = -1;   // WRONG — binary clears +18, not +33
  g_crimeTable[i].provenState = 0;
  g_crimeTable[i].wanted      = 0;
  ```
  After: clears +0/+18(memcpy)/+22/+37/+26; target (+33) is left untouched, as in
  the binary. Evidence: byte-offset arithmetic above (45*v4+45-27=+18 etc.).
  Added golden test `WorldGesetzFlow.LoadStateClearRemainderMatchesBinary`
  asserting +18→-1 and target(+33) survives; existing SaveLoadRoundtrip
  (active-record path, target serialized) still passes. Both verified by
  standalone compile+run (gesetz_flow.cpp + law.cpp + crime.cpp + rand.cpp).

## gesetztable_law_recon.cpp

### 0x5384d0 — VIBE_GesetzTable_FindByKey → GesetzTableFindByKey — VERIFIED-1:1
- `dword_5383F0 <= 0` empty guard; walk `byte_63CD4C[v1]` (== g_eventTable[0].value
  @ +4) stride 24; bound `v1 >= 24*dword_5383F0` ⇔ index `i >= g_eventTableCount`.
  EventDesc stride 24 / value @+4 confirmed via event.h static_asserts. Returns
  ptr to matching value byte. Index-form sibling is a lossless reformulation.
  Correct.

## groundplan_recon.cpp

### 0x4ae824 — VIBE_Groundplan_RetZero — VERIFIED-1:1  (returns 0, verbatim).

### 0x4af464 — VIBE_Groundplan_GetBuildingState — VERIFIED-1:1
- `cat = MapTypeToCategory(*a1)`; `cat==3 → byte_6317B5=1, return cat(3)`;
  `cat==5 → MapTypeToState(*a1,&st); on hit byte_6317B5=st, return st; else
  return 5`; else return cat. Matches `(char)a1` carry semantics. Table accessors
  + state global modeled via GroundplanHooks (already documented). Correct.

### 0x4ae59c — VIBE_Groundplan_GetWappenLabelId — VERIFIED-1:1
- switch(byte_12335B8): 1→1241(v0), 2→1245, 3→1249, 0→grid walk, default(incl 4)
  →4→1253 else v0. Recon's switch + default arms match.
- Grid walk: `for(i = markers[0]==6; !i; i = markers[v2]==6){ if(markers[v2]==7)
  break; v2+=536; ++v1; if(v2>=411648) break; }` reproduced exactly (411648/536 =
  768 slots; markerCount bound is a defensive test concession only).
- Type word: `HIBYTE(*((_DWORD*)&unk_12CEA71 + 134*v1))` == `(typeWords[134*v1]
  >> 24) & 0xFF` (134 dwords = 536-byte stride). group→label table
  {1,2,10}→1241; {3,4}→1245; {11,12,6}→1249; {5,7,8,9}→1253; else 1241(v0)
  matches the nested-if chain. Correct.

---

## Notes / handoffs
- `src/gui/widget_layout.cpp` (NOT in this chunk) fails to compile in the shared
  `guild` library (`Widget` has no member `ld`), blocking the full test-binary
  link. Pre-existing, owned by another agent — flagged, not touched. My three
  source files compile clean (`-fsyntax-only`) and the LoadState fix is verified
  via a standalone build of the relevant TUs.
- No constant/table literals required changing: law/crime/event offsets and the
  evidence fill count were all confirmed against get_bytes/disasm and already
  correct (only the clear-remainder offset was wrong).
