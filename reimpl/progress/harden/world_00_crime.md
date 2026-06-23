# Hardening sweep — world chunk 00 (crime.cpp + data_load.cpp)

MCP IDA Pro LIVE (module gilde.exe, imagebase 0x400000). Every function carrying a
`gilde.exe 0xADDR` provenance comment in the two owned files was decompiled (and
disasm'd where Hex-Rays could be wrong) and diffed line-for-line against the binary.

Files owned: `src/world/crime.cpp`, `src/world/data_load.cpp` (+ their headers
`crime.h` / `data_load.h` and tests `straftat_table_test.cpp`,
`straftat_table_itest.cpp`, `straftat_table_e2e_test.cpp`, `world_data_load_test.cpp`).

NB: the brief's generic mention of ParseInt (0x5dc070) / ParseCsvFieldList (0x50704c)
/ InitParameterTable (0x577a9c) does NOT apply to these files — those live in
`src/sim/` (history_parse.cpp, command_apply9.cpp, etc.) and are owned by another
chunk. The functions actually present in my files are the Straftat/Beweis slot-table
ops and the two building/object .dat loaders.

---

## crime.cpp

Struct offsets re-verified against the named globals in every decompile:
id=dword_11BC760(+0), perpetrator=dword_11BC776(+22), wanted=word_11BC77A(+26),
location=byte_11BC77C(+28), target=dword_11BC781(+33), provenState=dword_11BC785(+37).
Crime stride 45, count 512 (loop bound 23040 = 45*512). Evidence: dword_11C2160
(owner) / dword_11C2164 (crimeId), stride-2, bound 4096 dwords = 2048 pairs. All
match `law_types.h` CrimeRecord static_asserts.

| Function | Addr | Verdict | Evidence |
|---|---|---|---|
| StraftatFindFreeSlot | 0x4c3390 | VERIFIED-1:1 | scans provenState(+37)==0; first hit then `i=512` to break; returns -1 if full — exact. |
| StraftatFindIndexById | 0x4c33bc | VERIFIED-1:1 | index-0 special compare (`a1==dword_11BC760`), then v3+=45/++v2 loop, `v3>=23040` break-before-compare, return -1. Matches the inlined scan. |
| StraftatSetRecordState | 0x4c3874 | VERIFIED-1:1 | `a2>=0x200 → -2`; `a1<2 → -3`; write provenState=a1; return a2. Unsigned compares preserved. |
| StraftatCountActiveByTarget | 0x4c3a48 | VERIFIED-1:1 | counts perpetrator(+22)==a1 AND provenState(+37)==1, step 45, bound 23040. |
| StraftatResolveAndClear | 0x4c354c | VERIFIED-1:1 (notify/grid via hook = BOUNDARY) | id lookup, wanted-- (signed i16 `>0` guard), `wanted&&!force→2`, `state!=1 && (!force||!state)→1`, RemoveCrimeFromGrid(target@+33, location@+28) UNCONDITIONAL once, evidence loop clears pairs where crimeId==dword_11C2164[i], then clears provenState/perpetrator/id. **Float→int: none. RNG: none.** |
| BeweisFindOrAllocSlot | 0x4c347c | VERIFIED-1:1 | free-probe gated by `!v6` (stops probing once a free slot is recorded, keeps existence scan); `(owner,crimeId)` dup → -2; else free idx or -1. Reconstruction's `!haveFree` guard is the same as the binary's `if(!v6) goto LABEL_2`. |
| BeweisAdd | 0x4c3338 | VERIFIED-1:1 | DISASM confirmed eax=crimeId, edx=owner; FindOrAllocSlot(owner,crimeId); slot<=-1→0; `dword_11C2160[eax*8]=owner`, `dword_11C2164[eax*8]=crimeId` (8 = 4-byte dword * stride-2). sprintf log is a no-op. |
| BeweisExistsForPair | 0x4c3518 | VERIFIED-1:1 | scan step 2, bound 4096, return 1 on (owner,crimeId) match else 0. |
| BeweisCollectByOwner | 0x4c32ec | VERIFIED-1:1 (+ benign bounds guard) | do/while: `key==provenState(+37)` → out[count]=recIdx; bound `v5<512 && v7<128`; `maxBytes=min(128,cap*4)`. The wave-12 `count<outCapacity` guard is provably a no-op for the engine's fixed 128-byte/32-entry buffer (count hits 32 only after outBytes hits 128, which exits first). |

BOUNDARY in ResolveAndClear (documented, default no-op hooks; cross-cluster):
- `VIBE_City_RemoveCrimeFromGrid` (0x578110) — City grid leaf → `g_removeGridFn`.
- `VIBE_History_NotifyRivalEvent` (0x536070), gated in the binary by
  `word_63CC5C!=-1 && dword_11C2160[i]==dword_12CE914[134*(u16)word_63CC5C] &&
  RecordById && !a2` (local-player + Person record) → `g_resolveNotifyFn`. The binary
  passes the resolved Person record pointer; the hook receives `(i*4, perpetrator)`.
  This is the only place the reconstruction does not reproduce a branch verbatim, and
  it is a genuine cross-module (Person/History/City) boundary, not a logic shortcut.

No source edits to crime.cpp were needed — all logic already matched the binary.

---

## data_load.cpp

| Function | Addr | Verdict |
|---|---|---|
| WorldInitBuildingTypeTable | 0x5833b4 | VERIFIED-1:1 (1 comment-only clarification) |
| WorldLoadBuildingAndObjectData | 0x5835f8 | FIXED (1 extra branch removed) + BOUNDARY items |
| WorldTypeRemapAt | (test accessor, no addr) | n/a — read helper for byte_13CE862 |

### WorldInitBuildingTypeTable @0x5833b4 — VERIFIED-1:1

Diffed every step:
- kindWorth zero loop: the original comma-loop `for(i=0;i<25;byte_13CEB3C[i]=0)++i;`
  increments BEFORE the iter-write, so it writes byte[1..25] — it also zeroes byte[25]
  (== byte_13CEB55), one past the 25-byte table. **Verified via `get_bytes 0x13CEB3C`
  (40 bytes all zero) + `xrefs_to 0x13CEB55` (no other reference):** the extra write
  lands in all-zero BSS that nothing reads → observational no-op. Reconstruction zeroes
  [1..24] in-bounds; behavior-identical. Added a comment documenting this exactly.
- Seeded entries byte_13CEB43/44/45/46/47/48/49/4B/4C/4D = 41/50/47/20/33/23/53/30/31/32
  → kindWorth[7..17] (idx 14 deliberately left 0). Byte-for-byte match to the disasm.
- byte_13CE862[0..730]=72 seed loop (`*((BYTE*)&word_13CE860 + ++v2 + 1)=72`, v2:1..731
  writes 731 entries). Match.
- Group/propagation loop: kind compare `v3 == dword@v18[1]>>24` (== curKind byte);
  group-size run; room loop bound `byte(+34)`, room word @ `v6+35`, `==-1`→skip,
  `objType = v7&0x7FFF`. Fallback read
  `v10 = *(int)(&unk_13CEB3A + ObjByte(objType,33)) >> 24` resolves to
  byte_13CEB3D[subtype] = kindWorth[subtype+1] — matches `kindWorthD[subtype]`.
  Interpolation `v15 = groupPos*j/groupSize + v14`; keep-min compare
  `*(int)(&unk_13CE85F + objType)>>24 <= v15` resolves to `g_typeRemap[objType] <= v15`
  → update only when `g_typeRemap > v15`. Match.
- **Signed `>>24` audit:** both fallback reads sign-extend the high byte. The seeded
  table is all values < 0x80 (max 53) and the rest is 0, so sign-extension never yields
  a negative — no observable divergence; the u8 return is exact. Documented at the call.
- Final fallback loop: `if(remap==72) remap = byte_13CEB3D[ObjByte(i,33)]` (raw byte
  read, no >>24) for i in 0..730. Match. Returns 731.
- **Float→int: none. RNG: none.**

### WorldLoadBuildingAndObjectData @0x5835f8 — FIXED + BOUNDARY

FIXED — extra branch the binary does not have:
- The original gates ONLY on the OPEN result (`if(!v8) return -3;` / `if(!v11) return
  -6;`) and then calls `VIBE_File_Read` UNCONDITIONALLY, **ignoring the returned byte
  count**. The reconstruction's `ReadDatBlob` previously also returned the -3/-6 error
  on a SHORT read (`got == recSize*count`), an extra branch absent from the binary —
  the original would proceed with a partially-filled buffer.
  **Fix:** `ReadDatBlob` now returns false only on open failure and discards the read
  count, mirroring the binary's control flow. (No test regressed: synthetic blobs are
  full size; `MissingFileReturnsErrorCode` still exercises the open-fail → -3 path.)

DATA-path verified byte-exact:
- A_Geb.dat: recSize 0x24D=589, count 72; A_Obj.dat: 0x41=65, count 731 — match the
  fread args. sprintf "%s%s"(dir, name) prefix preserved. Error codes -3/-6 on open.
- Room-count fixup loop (0x583810): inner `v14` from base+v19 step+2 to +128 (64 room
  words), `*(WORD)(v14+35)`==-1→skip; `HIBYTE&=~0x80`; tests `objKind(65*objType)==2 &&
  objType!=253 && *(char)(v14+36)<0` (the +36 byte = the SAME room word's high byte =
  bit15/present, read from the UNMODIFIED memory). `geb[+33] += cnt` byte-add. Outer
  bound 42408 = 589*72. Verified field-for-field — already correct.

BOUNDARY (out-of-scope side effects owned by other modules — documented handoffs, not
faked, not silently dropped):
- Production-slot pre-seed loop (`dword_13C3B50/54/58/5C`, `word_13C3B60` from
  `dword_13CD708/758`, `dword_6477AA`) — owned by `src/sim/building_production.*`.
- Render/INST allocations + `VIBE_Light_SetGrayColorThunk` — render leaves (Vulkan
  boundary / fixed arrays here).
- `VIBE_Building_ResetAllBuildings` (0x5896fc) — owned by `src/sim/building3.cpp`
  (`Building3_ResetAllBuildings`).
- RNG reseed `timeGetTime(); VIBE_Util_RandSeed()` (0x5cb8e0) at function end — owned
  by `src/util/util_misc.*` (`SetMsvcRandSeed`). It reseeds from wall-clock, so it is
  inherently non-reproducible; omitting it keeps the reimpl deterministic and does not
  affect the data path. **Handoff:** if an integration owner ever wants the full
  bring-up sequence, these four calls belong in the wiring between data_load and the
  sim/util modules (their symbols already exist in the reimpl).

---

## Build status

Both owned files compile cleanly in isolation with the exact project flags
(`-g -std=c++17 -Wall -Wextra -Wno-unused-parameter -I. -Iinclude -Isrc -Ishim`),
producing valid .o files. Headers and the `world_data_load_test.cpp` golden also
syntax-check clean.

PRE-EXISTING BREAK (not mine): the full `guild` library currently fails to link/compile
because of an untracked, in-progress file from another wave —
`src/gui/widget_layout.cpp` (uses a non-existent `Widget::ld<>()` member). This blocks
running the CTest targets but is unrelated to crime.cpp/data_load.cpp. Handoff to the
GUI wave owning widget_layout.cpp.

---

## Counts

- Functions with provenance reviewed: **11** (9 crime + 2 data_load; +1 test accessor).
- VERIFIED-1:1: **10** (all 9 crime ops + WorldInitBuildingTypeTable).
- FIXED: **1** (WorldLoadBuildingAndObjectData — removed the extra short-read error
  branch in ReadDatBlob so it gates on OPEN only, per 0x5835f8).
- Comment-only clarifications (no behavior change): 2 (kindWorth byte[25] over-write,
  signed >>24 audit — both already 1:1).
- BOUNDARY hooks documented: 6 (Person/History/City notify+grid in ResolveAndClear;
  production pre-seed, render allocs, ResetAllBuildings, RandSeed in the loader).
- Golden tests changed: **0** (no golden encoded wrong behavior; all remain valid).
