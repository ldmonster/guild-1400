# Harden pass — sim/ai_needs, animal, animal_wander, avatar

Scope: 1:1 line-for-line diff of every `gilde.exe 0xADDR`-provenanced function in
`src/sim/{ai_needs,animal,animal_wander,avatar}.cpp` against the live binary
(IDA MCP, module gilde.exe, imagebase 0x400000). Each function decompiled AND
disassembled where load-bearing.

Result: **all functions VERIFIED-1:1**. One genuine 1:1 fix applied
(animal.cpp `SpawnInto`). Files syntax-check clean (g++ -std=c++17 -fsyntax-only,
each -I as a separate arg).

---

## PRIORITY 1 — ai_needs.cpp

### kAiNeedsMethodDefs 60-row table — BYTE/VA-EXACT ✓
Paged the full 2605-instruction disasm of `VIBE_AiNeeds_BuildScoreTable`
(0x4764e8) in 6 windows (0..2605) and verified, per RegisterFromIni block, the
six columns against the binary's immediates/operands:
- id  : `mov [esp+..+var_AC], <imm8>`
- name: `mov esi, offset a<Name>`
- scorer (var_88, +36), execA (var_84, +40), execB (var_80, +44): the three
  `mov [esp+..+var_8x], offset VIBE_...` stores
- flag (var_1C, +144): `mov [esp+..+var_1C], <imm16>`

Every one of the 60 rows matches the reimpl table exactly (id immediate, name
string ref, all three fn VAs, flag word). Spot list of the verified VAs:
rows 1-2 (pre-confirmed) re-checked OK; 3..11,13..37 sequential ids OK;
**non-sequential region confirmed in exact build order**:
…35 (id 0x23) ,36 (0x24) ,37 (0x25) ,**12 (id 0x0C, `mov bh,0Ch`)** ,38 (0x26)
,39 (0x27) … 49 (0x31) ,**60 (id 0x3C, `mov dl,3Ch`)** ,50 (0x32) ,51 (0x33) …
59 (0x3B). The reimpl rows are listed in this same order (12 after 37; 60
after 49), so the source ORDER matches the binary's RegisterFromIni call
sequence exactly. Tail `mov eax,1 / retn` at 0x4783ef.

### BuildScoreTable control flow ✓
Per block: zero the 0x94 record (`rep stos`), write id/name/fn-ptrs/flag,
`call VIBE_AiMethod_RegisterFromIni` (commits to catalog[148*id]),
`test eax,eax / jz loc_476576` (abort -> early `return 0`). After all 60,
`mov eax,1`. Reimpl `AiNeeds_BuildScoreTable` reproduces: per-row WriteFixedFields
+ FillDesireSlots(RegisterFromIni), abort on 0, return 1. Verdict VERIFIED-1:1.

### LoadDataFile read-path schedule (0x468a40) ✓
Decompiled. Read branch (dword_63C7D8==0): open ai_data.dfn "rb" ->
BuildScoreTable(0) -> loop 61 records of ReadStream into byte_B57210 at the EXACT
in-memory offsets:
id +0(1), name +1(32), short[0..3] {+48(1)/+52(4)},{+56/+60},{+64/+68},{+72/+76},
long[0..3] {+112(1)/+116(4)},{+120/+124},{+128/+132},{+136/+140},
then `MemMove(+80, +48, 0x20)` (prev mirrors the 4 short slots). Loop returns 1
only after `++v15 >= 61` (LABEL_28); any short read -> CloseStream, return 0.
Reimpl `AiNeeds_OverlayFromDfn` reconstructs the equivalent CONTIGUOUS 73-byte
on-disk record (1+32 + 4*(1+4) + 4*(1+4) = 73) — what each ReadStream actually
consumes from the gzip-decompressed stream — then mirrors short->prev. The
in-memory padding gaps (+48..+47 etc.) are not on disk; the per-record disk
footprint is 73 bytes, matching the reimpl. `AiNeeds_LoadDataFile` returns
`overlaid==61`. VERIFIED-1:1. (VFS open + gzip is the documented caller seam.)

---

## PRIORITY 2 — animal.cpp / animal_wander.cpp

### Critical float->int sites
- **Animal_Update lifetime decay (0x4837a4..0x4837cd)** — the rain/charaction
  trap class. Binary: `eax = spawnTick+0x834 (2100)`; gate `v9 < gameTick`
  UNSIGNED (`jnb`); `ecx = gameTick - v9`; `xor edx,edx / div ecx` (UNSIGNED
  quotient in eax); `{eax, edx=0}` stored then `fild qword / fstp` -> non-negative
  64-bit load -> float. So `v14 = (float)(u32)(v9 / (gameTick - v9))` with NO
  signed (int) step. Reimpl uses `unsigned threshold`, `unsigned denom`,
  `static_cast<float>(threshold / denom)` (unsigned div, unsigned->float) and the
  unsigned `threshold < (unsigned)gameTick` gate. EXACT. VERIFIED-1:1.
- **WorldToTileWithHeight (0x5c6644, sibling, not edited)** — its col/row are
  `(int)` TRUNCATING casts of ConvertX-processed coords, written through `int*`
  out-params (NOT __int16*). Reimpl passes `int` outputs (`&col,&row`) — correct
  typing, no byte-offset trap.
- **TileToWorld (0x5c65d4, sibling)** — out[0]=x, out[1]=height, out[2]=z; arg
  order (hm@eax, col@edx, out@ecx, row@ebx). Reimpl call
  `TileToWorld(hm, chosenCol, chosenRow, outWorld+i*4)` matches.

### RNG draw count + order — verified per function
- **Animal_Update spawn pick (0x4836ca..0x483928):** d100 (common) FIRST; if
  `<=0x1E` then `RandomModulo(2)` -> {0=Cat,1=Dog}; else `RandomModulo(5)` then
  `add al,3` -> switch {0=Cow,1=Sheep,2=none,3=Pig,4=Horse}. (Two distinct jump
  tables jpt_4838E0 / jpt_4836F3 share target labels.) Reimpl
  `Animal_SpawnKindForRoll` + Update draw order matches: common, then d2 (<=30)
  or d5 (>30). Kind bytes Cat=0,Dog=1,Cow=3,Sheep=4,Pig=6,Horse=7 confirmed
  against the spawners.
- **BuildWanderPath (0x484200):** per step `RandomModulo(10)` (col) then
  `RandomModulo(10)` (row), -4 + start tile; clamp `min(size-2)` THEN `max(2)`;
  TraceLineOfSight(hm,a1,col,a1,row,&col,&row,600,0); fall back to start tile;
  TileToWorld. Reimpl exact (RNG order col-then-row, clamp hi-then-lo).
- **UpdateCat (0x483e34):** idle gate `*(actor+296)==0`; d100; if `<=0x1E`
  wander (`RandomModulo(3)`+1 steps, per-point WorldToTileWithHeight ->
  InsertActionVararg "anm_UpdateCat"); else sound branch — extra `RandomModulo(100)`
  burn ONLY when `*(rec+4)==0` (kind==0), then `RandomModulo(3)`+1 sound.
- **UpdateSheep (0x483fd4):** same shape, odds `<=0x14`, tag "anm_UpdateSheep";
  sound branch ALWAYS burns `RandomModulo(100)` (no kind guard), then
  `RandomModulo(3)`+1 sound. Reimpl `UpdateAnimalAI` soundBurn policy
  (kCatSoundBurn iff kind==0 / kSheepSoundBurn always) matches both. VERIFIED-1:1.
- **PickSpawnBuilding (0x484374):** copies name, WalkAndInvoke collect,
  `records[RandomModulo(count)]`. **CollectSpawnBuilding (0x48432c):** name match
  -> append at ctx+160 count, `return count<32`. Reimpl matches; the explicit
  `n<32` write-guard in the reimpl is a documented hardening (the walk already
  stops at count==32, so it never changes behavior on the in-bounds path).
- **FindHerdGrouping (0x4839f0):** gate `*(rec+8)==-1`; gather
  (Person_QueryBegin(0,1,6), !Production && !Storage && *(rec+97)!=0);
  anchor = cand[RandomModulo(n)]; PointThroughBoneChain(frame, frame+76 bytes ==
  +19 floats); loop `while(seen<n && group<0x10)`: skip if anchor==cand or
  !VectorWithinTolerance(anchor,cand,6000.0); else store triple, `group+=3`;
  stamp `*(rec+8)=group`. Reimpl `HerdGroupFrom` matches the COUNT math exactly
  (the only value written back). Out-buffer striding in the reimpl is the
  simplified test-only contiguous form (engine passes null) — documented.
- **FindDoorTarget (0x484160):** gather as herd; `cand[RandomModulo(n)]`;
  Object_FindByHandle(frame,768,"dummy_TUER",0,rec) -> door frame else building
  frame; PointThroughBoneChain(frame, frame+19, outPos); return 1. Reimpl matches.
  NOTE: binary indexes cand[RandomModulo(0)] when n==0 (reads stack garbage, UB);
  reimpl guards `n<=0 -> return 0` (documented; reproducing the UB is neither
  feasible nor desirable).

### Pool / model machinery
- AllocSlot (0x483960): walk 0..9344 step 292, break first `*(pool+i)==0`,
  count==32 -> 0 else `++count`, `*(slot+32)=gameTick`. Exact.
- AllocPool (0x4835c0): AllocDebug(0x2480=292*32,"anm:animals"), count=0, zero.
- FreeSlot (0x4839cc): Character_Destroy(*a1), memset(a1,0,292), --count. Exact
  (reimpl adds a harmless null guard).
- LoadModels (0x484468): reset-if-count, then 7 LoadOrAddRef in order
  hund,katze,kuh,pferd,schaf,pferd,schwein; ++count each. Exact (reimpl
  kModelNames identical).
- ResetModelHandles (0x484424): release nonzero handles, off-by-one zero of
  dword_B59BBC[i+1] (== dword_B59BC0[i]); reimpl zeros index i. Documented.

### FIX APPLIED (1:1) — animal.cpp `SpawnInto`
The spawners VIBE_Animal_SpawnCat/Dog/Cow/Sheep/Livestock (0x483b58..0x483d20)
stamp ONLY actor(+0), kind(+4), position(+16/+20/+24); they do NOT touch the
herd-count field (+8). The reimpl glue `SpawnInto` was writing `herdCount = -1`.
Removed that store so the field stays 0 (from AllocSlot's zeroed slot), matching
the binary. No observable effect (kind-2/herd animals are never spawned, so +8 is
never read on a spawned animal), but now byte-faithful. Note added in source.

Also confirmed a binary quirk (lives behind the SpawnAnimal hook, not in source):
SpawnCat loads `hund_HUND`, SpawnDog loads `katze_KATZE` (cat/dog MODEL names are
swapped); the kind BYTE stamped (Cat=0 / Dog=1) is correct either way.

---

## avatar.cpp

- **Avatar_LookupById (0x4859b0):** `while(dword_630E4A[v2]>>16 != id){v2+=23;
  if(v2>=230) return 0;} return &dword_630E4A[v2]+2`. Stride 23 dwords (=92
  bytes), bound 230 (=23*10). Reimpl scans 10 entries by `ownerId` (high word).
  ABI note (documented): original returns a pointer to entry+2 bytes (the ownerId/
  payload region); reimpl returns the AvatarEntry* (callers are reimpl-side). Scan
  logic exact, incl. the "no free-entry guard so id 0 finds first free" behavior.
- **Avatar_FindOrAllocForPerson (0x4859e0):** for each owned scene entity
  (QueryFind(person+376,1,5) iter): LookupById(entity); first hit returns; else
  first free entry (`!(dword>>16)`), step 23, bound 230; else 0. Reimpl takes the
  resolved owned-id list and mirrors this (LookupById per id, then first
  ownerId==0). VERIFIED-1:1.

---

## Build check
`g++ -std=c++17 -fsyntax-only` on each of the 4 files with each include path as a
separate `-I` arg (-I<repo> -I<repo>/include -I<repo>/src -I<repo>/shim
-I<repo>/third_party -I<repo>/build/generated): all 4 exit 0. (Full link is
blocked by an unrelated pre-existing break in
command_apply5.cpp/charaction_steps5.cpp — not in these files.)

## Counts
- Functions verified: 17 (BuildScoreTable, LoadDataFile, OverlayFromDfn helper;
  Animal AllocPool/FreePool/AllocSlot/FreeSlot/Update; SpawnCat/Dog/Cow/
  Sheep/Livestock; BuildWanderPath/CollectSpawnBuilding/PickSpawnBuilding/
  FindHerdGrouping/FindDoorTarget/UpdateCat/UpdateSheep/ResetModelHandles/
  LoadModels; Avatar LookupById/FindOrAllocForPerson) + 5 siblings confirmed
  (MapTraceLineOfSight, TileToWorld, WorldToTileWithHeight, VectorWithinTolerance,
  PointThroughBoneChain) — call shapes only, not edited.
- 60-row method table: 60/60 rows byte/VA-exact, order-exact.
- Fixes applied: 1 (SpawnInto herdCount).
- Divergences from strict 1:1, all pre-documented and behavior-neutral: FindDoorTarget
  n==0 guard; CollectSpawnBuilding write-guard; FreeSlot null guard; avatar return-ptr
  ABI; ResetModelHandles off-by-one normalization; HerdGroupFrom test-only out-buffer.
