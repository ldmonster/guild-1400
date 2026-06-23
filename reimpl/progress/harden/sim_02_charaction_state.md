# Hardening sweep — sim_02: charaction.cpp + character_state.cpp

MCP live (gilde.exe). Every provenanced function decompiled AND disassembled, diffed
line-for-line against the reconstruction. Disasm is the reference of record.

Files owned: `src/sim/charaction.cpp`, `src/sim/character_state.cpp`, and the test
`tests/unit/actionqueue_boundary_test.cpp`.

## Summary counts
- VERIFIED-1:1: 16
- FIXED: 3 (one with a golden-test correction)
- BOUNDARY / documented handoff: 2 (+ several render/heap leaves already hooked)

---

## charaction.cpp

### 0x40431c ActionQueue_GetFreeEntry — VERIFIED-1:1 (with documented equivalence)
Disasm: `edx=count` (post-inc), `eax=byte offset`; reads `pool[eax]` (= node->step at
+0), advances 404; loop while `step!=0 && eax<0x7E400`; if `count<1280` return
`base+404*(count-1)` else 0. The binary's free marker is **node->step (+0) ONLY**; it
does NOT zero the returned node and does NOT consult owner.
The reconstruction additionally requires `owner==nullptr` and memsets the slot. This is
an intentional, **observably-equivalent** deviation: in the real call tree GetFreeEntry
is only reached from QueueInsertEntry (which immediately memsets via SetGrayColorThunk)
and InsertAction (which immediately fills step), so the binary's "step==0 means free"
and the reconstruction's "step==0 && owner==0" select the same slots for the real
allocate-then-fill sequence. The owner reservation additionally lets the unit tests call
QueueInsertEntry standalone (no following fill) and still get distinct nodes — the binary
relies on the immediate fill for that. Left as-is (no churn); equivalence documented.

### 0x404370 ActionQueue_UnlinkEntry — VERIFIED-1:1
Head case `!node[40] && node[20]!=0 && node==owner[296]` → clear owner head; else
relink prev[40]/next[36] and, when prev==0, promote next to owner[296]. Frees via
SetGrayColorThunk(0,404,node) = memset(node,0,404). Return 1 (node!=0) / 0 (null). All
matches, incl. pointer-assignment order.

### 0x40442c ActionQueue_ValidateLinks — VERIFIED-1:1 (debug)
Binary `__debugbreak()`s on link inconsistency; reconstruction tolerates silently
(release no-op). Behaviorally identical for valid queues (rule-1 readability path).

### 0x4043dc ActionQueue_ClearAll — VERIFIED-1:1 / BOUNDARY (waypoint heap)
Loops `while owner[296]` unlinking each head. Binary additionally frees the type-45
node's +244 waypoint buffer via VIBE_Memory_FreeDebug before unlinking. The waypoint
heap block is render/pathfinder data not modeled in this tree → BOUNDARY (documented).

### 0x40c15c CharAction_QueueInsertEntry — VERIFIED-1:1
GetFreeEntry → memset → append to tail (walk `node[40]` chain, set `tail[40]=node`,
`node[36]=tail`) or set head `a1[296]=node`; then `node[12]=0; node[0]=0; node[40]=0;
node[16]=0; node[20]=a1`; ValidateLinks. Empty-head case leaves node[36] (prev) at its
memset-0 value (no explicit write) — matches. Returns node / 0 on exhaustion.

### 0x40c1e4 CharAction_InsertActionVararg — FIXED (node[48] latch site relocated)
Common fill verified: `node[0]=step; node[12]=0; node[9]=type; node[20]=ch`; arg loop
`v2+=4` BEFORE store ⇒ i-th arg at node+48+4*i (args[1+i]); `node[40]=0; node[16]=0`.
Arg count from `dword_66FD18[19*type]`; reconstruction clamps to argc/63 (defensive,
max real argCount is 5 ⇒ unobservable). Type coercion to 0 on unregistered step matches.
FIX: the `node[48]=type` latch (0x40c2cb: `if(step==RunActionOrFree) node[48]=node9`)
was previously inside the shared FillActionNode and keyed on the same predicate; moved
it into this builder unchanged (reads back the stored type byte, matching
`*(int*)(v3+6)>>24`). No behavior change for the vararg path; the move was required to
let InsertAction use its own (different) latch condition. (No golden affected.)

### 0x404470 ActionQueue_InsertAction (InsertActionAfter) — FIXED (node[48] latch condition)
Link-after wiring verified via disasm: `v5=after[36]; after[36]=node; node[36]=v5;
node[40]=after; if(v5) v5[40]=node else after->owner[296]=node`. Fill identical to the
vararg builder, then the post-fill latch.
DIVERGENCE FOUND: the binary's latch is **`if (BYTE4(a2)==0) node[48]=var_1C`**
(0x404607), i.e. keyed on the *coerced type byte being 0*, storing the *raw* (pre-
coercion) type — NOT on `step==RunActionOrFree`. The two coincide for type 0 and
unregistered types, but DIVERGE for the reconstruction's type-51 placeholder (mapped to
RunActionOrFree but with a non-zero type byte): the shared FillActionNode would wrongly
latch node[48]=51, whereas the binary does not latch for type 51.
FIX: InsertActionAfter now captures `rawType` before coercion and does
`if (type == 0) node->args[1] = rawType;` — matching 0x404607 exactly. (var_1C is only
written on the unregistered-step path; for a type that already has a step the binary
reads loop-uninitialised stack — modeled as the raw type, its value on the only path
that consumes it.) Evidence: disasm 0x40449e (var_1C save), 0x404607 (`if(!BYTE4) ...`).
Also corrected the comment: head set is via `after->owner[296]` (0x40453b), == ch->actions.

### 0x40c3c8 CharAction_CancelForObject — VERIFIED-1:1
For each pool node: `if (node[9] type!=0 && node[0] step!=0)`, `v5=node[20] owner`,
`if v5 && a1 == *(DWORD*)(v5+136) owner->universe && node[1] chained != 0 → node[1]=0`.
Reconstruction matches exactly (owner->universe == +136).

### 0x405558 Character_DeclareAction — VERIFIED-1:1
`if (type>63) return 0; if (dword_66FCD0[19*type]) return 0;` (already declared);
stores step, ready→byte_66FCD4[76*type], argCount→dword_66FD18[19*type], copies animName
(2-byte unrolled do-while into the 71-byte entry tail). Reconstruction matches.
NOTE: byte_66FCD4 ("ready") is **write-only — never read anywhere in the binary**
(xrefs_to 0x66fcd4 = only DeclareAction). See the ready note under RegisterHandlers.

### 0x40be30 CharAction_RegisterHandlers — FIXED (type-7 animName) + golden corrected
Allocs the 0x7E400 (=404*1280) pool, zeroes the catalog (4864 B) and live array
(2048 B), then DeclareAction tuples. All (type,argCount) verified against the push
constants: 7→2, 0→0, 23→0, 45→3, (58→3, 57→1 deferred), 46→1, 47/48/49/50→0, 51→3,
53→1, 54→0, 55→1, 56→1, 59→1, 52→5. All ready=1.
DIVERGENCE FOUND: type 7's animName. Disasm 0x40be6a loads `ecx = "bewegung/dreh_90_rechts"`
for the preceding SetGrayColorThunk call; that thunk **preserves ecx** (push/pop ecx at
0x5c6af0 / 0x5c6b06), so DeclareAction(7) at 0x40be85 receives that same string. The
reconstruction had `"bewegung/dreh"`.
FIX: type 7 animName → `"bewegung/dreh_90_rechts"`. GOLDEN CORRECTED:
`tests/unit/actionqueue_boundary_test.cpp` RegisterHandlersCatalogGolden now asserts the
correct string (it encoded the wrong "bewegung/dreh"). Evidence: disasm 0x40be6a,
0x5c6af0/0x5c6b06.
BOUNDARY/handoff (already documented in the file): types 45/51 use placeholder steps
(LoadAnim / RunActionOrFree) because the real WalkUpdate / Move2UniverseActionUpdate
steps are render/scene-entangled and owned by charaction_walk / character_universe; types
58/57 (Command_Dispatcher / QueueWalk2RndDummy) are registered by those owner modules.

### 0x40c07c CharAction_QueueShutdown — VERIFIED-1:1 (with benign deviation)
Binary frees the map node, the type-45 waypoint buffers, destroys all 512 live
characters, frees the pool, dword_62CEFC=0. Reconstruction zeroes the pool + sets
poolReady=false (+ also clears the catalog, which the binary does NOT — but
RegisterHandlers re-zeroes the catalog on the next init, so init/teardown is observably
identical; the only difference is ActionType() after shutdown-without-reinit, an
unreachable state in the call tree). Character-destroy / heap-free are out-of-tree
leaves. BOUNDARY noted.

### Cross-file HANDOFF (actionqueue.cpp — NOT owned here)
The binary's dispatch *gate* (0x40477c, `if(!v1[5])`) is the **owner pointer at node+20**;
node+8 is a *priority* byte the dispatcher compares as `(u8)node[8] > *(int*)motion`
(0x4047cf). The reconstruction's `actionqueue.cpp` DispatchCurrent instead gates on
`node->ready` (+8), so the builders here must populate node->ready to keep that internal
contract (every catalog type declares ready=1 and owner is set in lockstep ⇒ the dispatch
decision is observably identical to the binary's owner-gate). FillActionNode keeps
`node->ready = def.ready` for this reason, with a code comment recording the handoff:
**actionqueue.cpp DispatchCurrent should gate on owner(+20) to be byte-faithful to
0x40477c**, and node+8 should model the priority byte (0 on the vararg path).

---

## character_state.cpp

### 0x45263c IsActiveType — VERIFIED-1:1
`type ∈ {2,5,4,3}`. Matches.

### 0x452660 IsActiveTypeForTurn — VERIFIED-1:1
`TypeActive && (!stride || (standalone==-1 || id%stride==764CF4) && (standalone!=-1 ||
id%stride==63CC20))`. Modulo unsigned (binary `(unsigned int)(u8)byte_63CC1D`, id read as
DWORD). Reconstruction casts both operands unsigned, stride 0 ⇒ open. Matches.

### 0x453228 IsObjectForTurn — VERIFIED-1:1 (defensive div-0 guard)
`standalone==-1 || type==6 || id%stride==764CF4`. Order (standalone, type==6, modulo)
matches. The binary has NO stride==0 guard ⇒ a (standalone!=-1, type!=6, stride==0)
config divides by zero; that config is unreachable (networked ⇒ stride≥1, single ⇒
standalone==-1 short-circuits). Reconstruction's `if(stride==0) return false` is an
unreachable defensive guard. VERIFIED.

### 0x453260 IsOwnerForTurn — VERIFIED-1:1
`if (type>1u) return 0;` key = self; if `owner && *(WORD*)(owner+39)!=0xFFFF` key =
persons[owner.ownerPlayer]; return `standalone==-1 || key.id(+4) % stride == 764CF4`.
Reconstruction models the caller-resolved owner id (ownerId). Same unreachable stride-0
guard. Matches.

### 0x4532d0 IsAiControllableForTurn — VERIFIED-1:1
`(type==1||type==2) && a1[356] isMaster && owner && *(WORD*)(owner+39)!=0xFFFF`; kind =
persons[owner.ownerPlayer].kind (byte_12CE912); `if kind!=7 && (standalone==-1 || kind==6
|| persons.id(dword_12CE914) % stride == 764CF4) return 1`. Reconstruction matches
field-for-field (caller-resolved ownerKindByte / ownerId). Same unreachable stride-0 guard.

### 0x43d9f4 IsIdle — VERIFIED-1:1
Binary: `if (*(DWORD*)a1)` (inner record at +0) `return *(DWORD*)(inner+296)==0` else
ReportError+0. Reconstruction `a && a->action==nullptr` (a->action models inner+296). Null
inner → error/return 0 maps to a==nullptr → false. Model-equivalent.

### 0x43ddd8 IsSitting — VERIFIED-1:1
`(*(BYTE*)(inner+140) & 0x10) != 0`. kLaSitting==0x10, flagsA at +140. Matches exactly.

### 0x40204c ProcessFlaggedLocal — VERIFIED-1:1
`for i!=512: r=dword_66F0D0[i]; if (r && (r[140]&1) && off_649D64 == r[136])
SetPivotVector(r[52], r+84)`. flag bit 0 = kLaRedraw, universe +136 == g_activeUniverse,
SetPivotVector routed through applyPivot hook (render leaf). Loop bound 512 == kLiveCapacity.
Return value: binary returns the last loop value (effectively-dead garbage); reconstruction
returns a processed count — an unobservable substitute for the dead return. Matches.

### 0x40208c RefreshFlaggedLocal — VERIFIED-1:1
Same scan/condition; computes the pivot vec (actor +84/88/92 + mesh +76/80/84) — render
detail routed through the hook; `v2=record[296]; visibility re-applied UNLESS (v2!=0 &&
v2[9]==45)`; then clears flag bit 0. Reconstruction: `midTalk = action!=nullptr &&
actionType==45; if(!midTalk) applyVisibility; clear flag`. Logic matches exactly. Dead
return modeled as count.

### 0x4b9a5c (extracted seasonal work-window gate) — VERIFIED-1:1 (tables byte-confirmed)
Extracted test at 0x4b9f22: `v9=(double)month; v9 < flt_64770C[season] && v9 >=
(double)flt_6476FC[season]` (computed `< MAX` then `>= MIN`, both as doubles).
Reconstruction `m < kWorkSeasonMax[s] && m >= kWorkSeasonMin[s]` matches order + double
promotion. Tables confirmed via get_bytes:
- flt_6476FC (=kWorkSeasonMin) [0..7] = 8,7,8,9,20,21,20,19 — exact.
- flt_64770C (=kWorkSeasonMax) = flt_6476FC+16B ⇒ [0..3] = 20,21,20,19 — exact.
The season index is `GetSeasonFromDay (0x58339c) = day % 4` ⇒ **always 0..3**; the only
reachable entries (0..3 of each table) match byte-for-byte. Entries 4..15 in either table
overlap adjacent globals (denormals / 40.0 / 15.0) and are never indexed (verified day%4),
so the reconstruction's zeros there are unreachable and the `>=16` guard is unreachable.
No source change needed; provenance/range confirmed.

---

## Verification
- Both owned source files + the edited golden compile clean with the project flags
  (`g++ -std=c++17 -Wall -Wextra -Wno-unused-parameter`, project includes) — no errors.
- Pre-existing unrelated build break in `src/gui/widget_layout.cpp` (`Widget::ld<>` — a
  different wave's in-progress file) prevents a full `cmake --build`; it is outside this
  chunk and untouched here.
- No other test or source references the corrected type-7 string; no test exercises the
  InsertActionAfter arg-latch path, so the latch-condition fix is risk-free.
