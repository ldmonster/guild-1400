# Harden pass — sim_00 needs / actionqueue / animals / avatar

Scope (5 owned files + headers/tests):
- `src/sim/ai_needs.cpp`      (0x4764e8 BuildScoreTable, 0x468a40 LoadDataFile read path)
- `src/sim/actionqueue.cpp`   (0x404768 DispatchCurrent, 0x40bdd8 CheckDurationExpiry,
                                0x40b974 FinishSetVisible, 0x406a00 RunActionOrFree)
- `src/sim/animal.cpp`        (0x48364c Animal_Update + pool/alloc helpers)
- `src/sim/animal_wander.cpp` (0x484200/0x48432c/0x484374/0x4839f0/0x484160/0x483e34/
                                0x483fd4/0x484424/0x484468)
- `src/sim/avatar.cpp`        (0x4859b0 LookupById, 0x4859e0 FindOrAllocForPerson)

Method: every provenance-tagged fn was `decompile`d AND `disasm`d via IDA MCP and
diffed line-for-line (control flow, constants, float->int conversions, RNG draw
count/order, fixed-point shift signedness, struct offsets/stride, side-effect
order, returns incl edx). Disasm = reference of record.

---

## Per-function verdict

### ai_needs.cpp
- **AiNeeds_BuildScoreTable (0x4764e8)** — VERIFIED-1:1.
  Confirmed from disasm of the first build block (0x4764e8..0x476583): record is
  0x94 (148) bytes; id at +0 (var_AC), name 2-byte copy loop at +1, scorer at +36
  (var_88), execA at +40 (var_84), execB at +44 (var_80), flag word at +144
  (var_1C); each block ends `call RegisterFromIni; test eax,eax; jz -> ret 0`.
  First row (id=1, flag=1, scorer=0x4796b0, execA=0x469de0, execB=0x469df0) matches
  the table exactly. The 60-entry table (incl out-of-order ids 12/13/60) is the
  binary's code literals; structure + abort/commit logic correct.
- **AiNeeds_OverlayFromDfn / AiNeeds_LoadDataFile (0x468a40, read branch)** —
  VERIFIED-1:1. Confirmed the per-record 73-byte on-disk schedule from the
  ReadStream chain at 0x468d42..0x468f26: id(1) name(32) then
  [attr(1)@+48 change(4)@+52]x4 short, [attr(1)@+112 change(4)@+116]x4 long, then
  `MemMove(+80,+48,0x20)` (prev mirrors the 4 short slots). Loop bound 61
  (`++v15 >= 61` -> ret 1). Matches kAiNeedsDfnRecordBytes=73, count=61, prev-mirror.

### actionqueue.cpp (already in corrected state on entry — see below)
- **DispatchCurrent (0x404768)** — VERIFIED-1:1 (entry guards) + documented model
  (motion gate). Disasm (0x40476d..): `edx=*(char+296)` node, `ecx=char`.
  Entry guards: node!=0 (else ret 0); `[edx+14h]` (node+0x14 = **owner**) != 0
  (else ret 1); `[edx]` (step) != 0 (else ret 1). NOTE: the 2nd guard is
  **owner (node+0x14)**, NOT node->ready (+8); the file already encodes
  `!node->owner` with a disasm cite. Step is `mov eax,edx; call [edx]` -> step
  receives the **node** in eax (resolves the "char vs node" ambiguity for all
  step handlers). After step: `++[edx+0Ch]` callCount, `ebp=[edx+4]` chained.
  Motion gate (0x4047ab): skip-chain if `!motion(char+112)` || `char+128`(nextAnim)
  || `char+133 == motion+108` || `(u8)node->ready(+8) > (int)*motion`. The latch
  write is `char+133 = *(motion+108)`. The last two sub-conditions read render/anim
  frame counters (out of tree); file models them and documents it. The
  head-still-live re-check is a host memory-safety boundary (original re-uses stale
  edx = UB); documented.
- **CheckDurationExpiry (0x40bdd8)** — VERIFIED-1:1 (modeling note on abort).
  Disasm: eax=node. `if(!node[12]) node[52]=gameTick`; expire if
  `node[48]+node[52] < (unsigned)gameTick` (unsigned add+cmp) `|| [node+190h]`.
  +0x30=+48 duration, +0x34=+52 start, +0x190=+400 abort. The abort byte is on the
  **node** (+400), which the builders never write (node is zeroed) -> dead in the
  original. File reads `node->owner->abort`; this is the project's deliberate model
  (character.h documents +400 as the Character abort that "forces motion/duration
  handlers to finish"; same offset, unified onto the live owner). MODELING NOTE,
  not a stream-affecting divergence (the original path never fires).
- **FinishSetVisible (0x40b974)** — VERIFIED-1:1. eax=node. `if(!result[3])` =
  callCount(+0x0C)==0 gate, `SetVisible(result[5]=owner+0x14, result[12]=+0x30=
  args[1])`, then Unlink. File now has the `callCount!=0 -> no-op` gate + owner +
  args[1] visible. Correct.
- **RunActionOrFree (0x406a00) / StepMotionQueue (0x4041e8)** — VERIFIED (model).
  Original: `r=StepMotionQueue(node); if(r==-1) Unlink`. StepMotionQueue (eax=node)
  reads owner=node+0x14, owner+112 motion, node+0x0C callCount, attaches motion on
  first call returning node type byte (`*(node+13)>>24`), else polls anim-done
  (motion+109 & 0x20) / abort (node+400) -> -1. The file models the observable
  result via GetCharActionHooks (anim entangled, documented in header). No
  constant/branch divergence in the modeled surface.

### animal.cpp
- **Animal_Update (0x48364c)** — FIXED (float conv) + spawn pick VERIFIED.
  * Spawn type pick: re-derived the shared jump table at 0x48360c (8 dwords):
    idx0=Cat idx1=Dog idx2=none idx3=Cow idx4=Sheep idx5=none idx6=Pig idx7=Horse.
    Switch A (commonRoll<=30, 0x4838c9) `RandomModulo(2)` -> jpt[r]: 0=Cat,1=Dog.
    Switch B (commonRoll>30, 0x4836de) `RandomModulo(5)` -> `jpt[r+3]`:
    r0=Cow r1=Sheep r2=none r3=Pig r4=Horse. `Animal_SpawnKindForRoll` matches
    exactly (kind bytes Cat=0 Dog=1 Cow=3 Sheep=4 Pig=6 Horse=7). VERIFIED-1:1.
    (Spawner fns SpawnCat/SpawnDog have swapped *model* strings vs IDA names, but
    the stamped **kind bytes** 0/1 are what the pool uses — confirmed 0x483bc4/
    0x483b58; no impact.)
  * **FIX — despawn lifetime float conversion** (was a latent signedness bug):
    disasm 0x4837b5..cd is `sub ecx,eax` (unsigned), `xor edx,edx; div ecx`
    (UNSIGNED quotient), store {eax, edx=0}, `fild qword` (loads as non-negative),
    `fstp`. So `v14 = (float)(u32 quotient)`. Source had
    `(float)(int)(threshold/(unsigned)denom)` — the intermediate `(int)` mis-signs
    quotients >= 2^31. Changed to `static_cast<float>(threshold / denom)` with both
    operands unsigned. Comparison direction (`RandomFloatScaled() > v14` -> despawn)
    re-verified against `fcomp/jbe`.
  * Spawn gate `(u16)RandomModulo(2*cap)-cap > count && season!=3 && AllocSlot()`,
    throttle `(unsigned)(last+350) < (unsigned)gameTick`, cap basis {0,16,32},
    cursor `(cursor+1)%32`, despawn cond `culled || winter || cap<count` — all
    VERIFIED-1:1. (Return value `(cursor+1)/32` is dropped: file returns void; no
    in-tree caller consumes it — noted, not load-bearing.)

### animal_wander.cpp
- **Animal_BuildWanderPath (0x484200)** — VERIFIED-1:1. 2x `RandomModulo(10)-4`
  offsets, clamp `min(size-2,..)` then `max(2,..)` (branch order matches),
  `TraceLineOfSight(...,600,0)`, on success use trace output else start tile,
  `TileToWorld(map, col, out, row)` (arg order confirmed via 0x5c65d4:
  a2=col@edx, a3=out@ecx, a4=row@ebx; wrapper(hm,tileX,tileY,out) call matches).
  Float->int tile conversion happens inside render::WorldToTileWithHeight/TileToWorld
  (ConvertX truncation) — those are render-module owned (HANDOFF below).
- **Animal_CollectSpawnBuilding (0x48432c)** — VERIFIED-1:1. Confirmed unguarded
  `v6=ctx+160; ctx+160=v6+1; ctx[32+4*v6]=record; return ctx[+160] < 32`. (A prior
  HARDEN bounds-guard was a non-1:1 addition; it is removed — the unguarded form
  is the binary, and the 32-cap is enforced by the walk's return value.)
- **Animal_PickSpawnBuilding (0x484374)** — VERIFIED-1:1. name copy into ctx,
  WalkAndInvoke(CollectSpawnBuilding), `if(count) return records[RandomModulo(count)]`.
  1 RNG draw. Matches.
- **Animal_FindHerdGrouping (0x4839f0)** — VERIFIED-1:1 (observable). anchor =
  cand[RandomModulo(n)] (1 draw), loop `while v7<n && v9<16`, v9 += 3 per match,
  stamp `*(rec+8)=v9`. Bone anchor = frame, frame+76 bytes (= frame+19 floats).
  The strided stack buffer (x@+0,y@+5,z@+10 within 12-dword spans) is discarded by
  the original (only the count is stored) — the test helper's tight-triple
  `outPoints` layout is a documented modeling choice; the stamped count (the only
  observable) is correct.
- **Animal_FindDoorTarget (0x484160)** — VERIFIED-1:1. pick = cand[RandomModulo(n)]
  (1 draw), FindByHandle("dummy_TUER") door frame else building frame,
  PointThroughBoneChain(frame, frame+19, outPos), ret 1. (n==0 guard added vs the
  original's uninitialized read — documented safety deviation.)
- **Animal_UpdateCat (0x483e34) / Animal_UpdateSheep (0x483fd4)** — **FIXED**
  (RNG draw count). Both: idle gate actor+296; `RandomModulo(100) <= odds`
  (cat 0x1E=30, sheep 0x14=20) -> wander (RandomModulo(3)+1 steps -> BuildWanderPath
  -> per-waypoint WorldToTile + walk action); else SOUND branch.
  **Sound branch d100 burn divergence:** cat (0x483e7b) burns d100 only when
  `rec->kind==0`; sheep (0x484015) burns d100 **UNCONDITIONALLY**. Source only
  burned for cats (`catSoundBurn` flag false for sheep) -> sheep was missing one
  RNG draw, desyncing the stream. Replaced with a `SoundBurnPolicy`
  (kCatSoundBurn = burn iff kind==0; kSheepSoundBurn = always burn). Verified by a
  standalone harness: same seed -> sheep sound=1 (gate,burn,d3), cat(kind1) sound=2
  (gate,d3); the values differ exactly because of the extra draw.
- **Animal_ResetModelHandles (0x484424) / Animal_LoadModels (0x484468)** —
  VERIFIED (model). 7 meshes in order hund/katze/kuh/pferd/schaf/pferd/schwein,
  count bump each, reset releases handles. The original's `dword_B59BBC[i+1]`
  off-by-one write is documented; net table-empty behavior preserved.

### avatar.cpp
- **Avatar_LookupById (0x4859b0)** — VERIFIED-1:1. Disasm: `movsx ecx,bx`
  (id sign-ext), `sar edx,16` (signed) compare. Stride 0x5C (92) bytes, bound
  0x398 (=23*10*4) -> 10 entries. Sign-extended equality `(i16)a==(i16)b` iff
  `(u16)a==(u16)b`, so the u16 compare is observationally identical. Returns
  `entry+2` payload (file returns entry; documented). Free-entry `(dword>>16)==0`
  iff high word==0 -> `ownerId==0` identical.
- **Avatar_FindOrAllocForPerson (0x4859e0)** — VERIFIED-1:1. Inner LookupById per
  owned entity id (first match returned), else first free entry (high word 0),
  else 0. Modeling: owned-entity id list passed in lieu of the QueryFind iterator
  (documented in header).

---

## Fixes applied (addr + evidence)
1. animal.cpp despawn float conversion — drop the signed `(int)` intermediate;
   `(float)(u32 quotient)` per `div`+`fild qword` at 0x4837bf..0x4837cd. Latent
   mis-sign for quotient >= 2^31.
2. animal_wander.cpp sheep sound burn — sheep now ALWAYS burns a d100 in the sound
   branch (0x484015), cat only when kind==0 (0x483e7b). RNG-stream-affecting.
3. (actionqueue.cpp DispatchCurrent owner-guard + FinishSetVisible callCount-gate
   were already corrected in the working tree on entry; re-verified against disasm
   and the disasm cites are accurate.)

## Golden vectors
- Added `SimAnimalWander.SheepSoundBranchBurnsExtraD100` (tests/unit/
  sim_animal_wander_test.cpp): inline LCG oracle pins sheep = gate,burn,d3 and
  cat(kind1) = gate,d3 for the same seed; asserts the resulting sound values
  (sheep differs from cat) to lock the extra draw. No existing golden contradicted
  the fix (no test pinned the sheep sound-branch draw count).

## Build / verification
- All 5 owned `.cpp` pass `g++ -std=c++17 -fsyntax-only` cleanly.
- New test TU passes `-fsyntax-only`.
- Behavior of the sheep fix verified with a standalone link of
  animal_wander.cpp + animal.cpp + rng/coord deps (sheep=1/cat=2, matches oracle).
- NOTE: the full-library link target currently fails due to an UNTRACKED,
  unrelated WIP file `src/gui/widget_layout.cpp` (uses a nonexistent `Widget::ld<>`
  member) that the CMake `src/**` glob pulls in. That file is outside this agent's
  scope and was not touched. Once it is fixed/removed, `cmake --build build
  --target sim_animal_wander_test` will pick up the new golden.

## Cross-file handoffs
- **render::WorldToTileWithHeight (0x5c6644) / TileToWorld (0x5c65d4)** own the
  float->tile conversion: tile = (int) of ConvertX(...) where ConvertX (0x5c6b08)
  **truncates toward zero** (cite 0x5c6674 `v16=(int)v5` after ConvertX). These are
  render-module files (src/render/heightmap.cpp); BuildWanderPath/UpdateCat/Sheep
  only call them. Verify their ConvertX truncation in the render hardening pass.
- **ActionNode struct (charaction.h, owned by the charaction cluster)** has no
  +400 abort field; the duration/motion handlers read node+400 in the binary.
  The project unifies abort onto Character+0x190 (same offset) via node->owner.
  If charaction is ever re-modeled to give the node its own (always-zero) abort,
  CheckDurationExpiry/RunActionOrFree should follow.
- **g_gameTick (dword_62EB38)** is defined once in actionqueue.cpp and shared by
  animal.cpp (extern). Unchanged.
