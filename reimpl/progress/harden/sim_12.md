# Harden sweep — chunk sim_12

Scope: 22 `src/sim/wire_*.cpp` glue files + `src/sim/world_buildingflag_util_recon2.cpp`.

The chunk is overwhelmingly *wiring glue*: each `wire_*.cpp` seeds a hook table from the
module's inert defaults and binds the fields that have a genuine reconstructed target
(reconstructed in OTHER chunks: entity.cpp, util, command builders, building_storage,
io/path, etc.). The only file carrying full inline reconstructed logic is
`world_buildingflag_util_recon2.cpp`. Verification therefore focused on:
1. the inline logic in `world_buildingflag_util_recon2.cpp`, and
2. the concrete byte-offset / field-read / arg-mapping claims the adapters make about the
   originals they bridge (these are the only places a wire file can diverge 1:1).

MCP used: decompile + disasm + get_bytes against gilde.exe (imagebase 0x400000).

---

## VERIFIED-1:1

### world_buildingflag_util_recon2.cpp

- **kBuildingKindSet @0x582F20** — `get_bytes 0x582F20,16` =
  `01 03 04 0B 0D 0E 12 13 15 16 1A 1B 1C 1F 21 00`. Byte-exact match. VERIFIED.
  (Confirmed 0x582F20 == dword_582908+0x618, the `mov esi,(offset dword_582908+618h);
  movsd x4` source in the disasm.)

- **World_LookupBuildingTypeFlag @0x583990** — disasm-walked:
  - `movsx ecx, word ptr [edx]` -> type sign-extended from i16 record field. Recon casts
    `(int)*typePtr` (signed). ✓
  - `shl edx,6; add ecx,edx` -> `65*type`; `+ dword_13CE27C` -> recBase. Recon
    `kindRecordBase + 65*type`. ✓
  - `movsx ecx, byte ptr [edx]` -> kind = (signed char)*recBase. ✓
  - loop: `mov edx,[esp+eax-3]; sar edx,18h` extracts (signed char) of the stack-copied
    `set[eax]`; `cmp ecx,edx` -> `kind == (signed char)set[i]`. Recon
    `(signed char)flagSet[i]`. ✓
  - match path `mov al,[esp+eax+...]` returns `set[i]` as char (u8 byte). ✓
  - `bh = set[i+1]; inc eax; test bh,bh; jnz` -> `next=set[i+1]; ++i; if(!next) ret 0`. ✓
  - `set[0]==0` (`test bl,bl; jz loc_5839D7`) -> early return 0. ✓
  The original copies the 16 bytes to the stack first; the recon reads the static table —
  behaviorally identical (verbatim copy). VERIFIED-1:1.

- **Universe_ApplyHiddenToggle @0x5b371c** — disasm:
  `SceneGraph_WalkAndInvoke(off_649D64, 0, Object_ToggleHiddenState, 6, newState)` then
  `return Light_RefreshAllObjects(1)`. Recon does exactly this via hooks
  `g_walk(g_root,0,g_toggle,6,newState)` then `g_refresh(1u)`, order preserved, returns the
  refresh result. walk/toggle/refresh are render/scene boundary leaves bound via hooks
  (rule 3). VERIFIED-1:1.

- **golden test world_buildingflag_util_recon2_test.cpp** — all 8 cases match verified
  binary behavior (signed-char compare, stride 65, mode 6, refresh(1) returns its result,
  kind=0 returns 0 via terminator-before-compare). VERIFIED-1:1, no churn.

### Adapter field-offset / behavior claims (verified against the originals they bridge)

- **wire_he.cpp:94** `*(person+8)!=0` — GroupInteractStep @0x4d19c0 gate
  `!*((_BYTE*)RecordById+8)`. ✓ VERIFIED.
- **wire_npcaction1.cpp Na5ResolveEntityField97** — @0x4d877c resolves
  `ResolveEntityById(v6,0,*(a1+172),0)`, frees if `!v6[0]` or `!*(v6[0]+97)`. Recon reads
  `*(obj+97)` i32 after resolving with scene/person null; returns 0 if !obj. ✓ VERIFIED.
- **wire_npcaction1.cpp Na6CurrentSeason** — SeasonFromYear @0x583384 = `(*a1>>16)%4`
  (signed sar + signed %). Adapter reads `clk.day` as i32 and forwards. ✓ VERIFIED.
- **wire_npcaction1/3 Na7ShuffleDwords -> InitAndShuffleDwordArray @0x58ba98** — disasm
  confirms init `dst[i]=i` for i in [0,n), early-out n<2, pass-count `(n/2)+max(n/4,1)`,
  inner `RandNext()%n` twice per swap. Adapter casts n to u8 matching arg0 `al`. RNG draw
  order preserved by forwarding to the reconstructed leaf. ✓ VERIFIED (adapter).
- **wire_cutscene.cpp WcQueueSpeech28** — verified the two consumed header fields against
  BuildSpeechPacket @0x4abf04 AND the consumer QueueRequestBuffer28 @0x494910:
  - header[+4]=17 == BuildSpeechPacket `v19=17` (v18 at off0, v19 at off4). ✓
  - header[+8]=person == `v20 = *(a1+4)` (the speaker id) AND ==
    QueueRequestBuffer28's gate `if(!PersonFindRecordById(a1[2]))` (a1[2]=*(header+8)). ✓
  - personFound gate reproduced via `PersonFindRecordById(person)`. ✓
- **wire_cutscene.cpp WcQueueBirthFailure / person reads** — CheckBirthParticipants
  @0x4a7a64: kind read `*(record+2)`, parent `*(record+92)`, kind==15 -> QueueRequestPair33.
  Offsets +2/+92 and the kind==15 branch match. ✓ VERIFIED (adapter).
- **wire_inventory.cpp** — WiResolveEntityById arg-mapping consistent with
  ResolveEntityById @0x583b44 (3 out-ptrs, search a4/a1/a2 -> ret 3/1/2, ecx=id);
  WiRoomWorth forwards (building, mul) matching ComputeRoomWorth @0x59116c arg roles
  (a2=mul used in final `(double)a2*flt_626A10*v19`); WiSpecialTarget sentinels -2/-3/-4
  map to g_lastObjectId/Scene/Trade. Forwards to leaves reconstructed in other chunks.
  ✓ VERIFIED (adapter mapping).

---

## BOUNDARY (documented, not fixable within this chunk)

- **wire_cutscene.cpp WcQueueSpeech28 — partial packet header.** BuildSpeechPacket
  @0x4abf04 also writes payload fields v21=-1(+12), v22=9(+0x36), v23=1418(+0x58),
  v24=a2[5], v25, v26=*a2, v27=12/14|0x10 plus a 2-byte-interleaved body + optional
  strcpy(a3). The hook seam only carries (person, lenTotal, lenB, text), so those payload
  bytes are NOT reproduced. The full builder is render/building/text-coupled
  (SetGrayColorThunk, Building_FindById @0x587b20, IsProductionType @0x587f80,
  QueryFind @0x5857fc, FindStorableObject @0x5877ac) — a cutscene-module reconstruction
  outside this chunk (rule 3/8 boundary). The two fields the consumer actually gates on
  (header[+4], header[+8]) ARE faithful. Pre-existing seam design; flagged for the
  cutscene-cluster owner.

- All other wire_*.cpp fields left inert are documented in-file as rule-3/4/5 render /
  scene / object / widget / audio / net leaves, or rule-8 process-global game-state tables
  (e.g. word_12CE910 person array filters, dword_631EB0 law table) with no standalone
  reconstructed callable. No divergence introduced.

---

## FIXED

None. The in-chunk inline logic (world_buildingflag_util_recon2) and the verifiable
adapter offset/arg claims were already 1:1; no source or golden changes were warranted.

---

## BUILD / TEST STATUS — BLOCKED BY FOREIGN BREAKAGE

Could NOT run ctest. `cmake --build build --target <my test targets>` fails to compile the
shared `guild` library due to a **pre-existing error OUTSIDE this chunk**:

    src/sim/object_lifecycle3.cpp:245: too few arguments to function
    g_hooks.rebindParentMesh(node, prototype)   // typedef wants (node, prototype, meshName)

`object_lifecycle3.cpp` is NOT in sim_12.chunk (another wave's in-progress edit). Per the
absolute rules I did not touch it. Because the whole src/** tree links into one library,
this breakage blocks every test target including mine.

Independent confirmation my chunk is clean: all 22 chunk .cpp files pass
`g++ -std=c++17 -fsyntax-only` against the project headers (ok=22 fail=0). The golden test
`world_buildingflag_util_recon2_test.cpp` was reviewed and its vectors match the verified
binary behavior.

---

## Counts
- Files in chunk: 23 (.cpp) — 22 wire glue + 1 inline-logic module.
- Inline reconstructed functions verified: 2 (World_LookupBuildingTypeFlag 0x583990,
  Universe_ApplyHiddenToggle 0x5b371c) + 1 table (kBuildingKindSet 0x582F20).
- Adapter byte-offset/arg claims verified against binary: 8 originals (0x4d19c0, 0x4d877c,
  0x583384, 0x58ba98, 0x4abf04, 0x494910, 0x4a7a64, 0x583b44/0x59116c).
- FIXED: 0.  VERIFIED-1:1: all above.  BOUNDARY: 1 (WcQueueSpeech28 partial header) +
  documented inert leaves.
- Tests: NOT RUN (foreign build breakage in object_lifecycle3.cpp:245). Chunk syntax-clean.
