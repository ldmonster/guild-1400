# Hardening sweep — command_apply5 / command_apply6 / command_apply7

Full 1:1 MCP diff of every provenance-tagged function in the three Command_Ex /
command-queue apply files. IDA Pro MCP live (module gilde.exe). Each function below
was decompiled AND disasm'd at its address and diffed against the reconstruction.

Owned files: `src/sim/command_apply5.cpp`, `command_apply6.cpp`, `command_apply7.cpp`
(+ `tests/unit/sim_command_apply6_test.cpp`).

NOTE: `command_apply5.cpp` was overwritten once mid-sweep by an external/concurrent
write (mtime 09:52) which reverted a first round of fixes AND introduced a regression
(ExCreatePersonA reverted to a person-array parent resolve). All fixes below were
re-applied to the current on-disk version and the regression re-fixed.

================================================================================
command_apply7.cpp — queue builders (ALL VERIFIED-1:1, no changes)
================================================================================
Every enqueue/queue-request builder verified field-by-field against the decompile
AND the stack-slot offsets in the disasm:

- 0x493dec SetGameSpeed                VERIFIED-1:1 (>=4 clamp, ==4 early-return, blob32)
- 0x493e2c IncreaseGameSpeed          VERIFIED-1:1
- 0x493e58 DecreaseGameSpeed          VERIFIED-1:1
- 0x493e80 GetGameSpeed               VERIFIED-1:1
- 0x494ab4 QueueRequestFlagBlob32     VERIFIED-1:1 (+0x10 flag, +0x11 blob 124)
- 0x493478 EnqueueKeepAlive           VERIFIED-1:1 (+0x10 = 0xB325939D)
- 0x49444c EnqueueBuildingActionEnd   VERIFIED-1:1 (StrNCopyPad +0x10, 128)
- 0x49447c EnqueueTargetedAction      VERIFIED-1:1 (!a1=>-1; +14/+16/+1A/+1C/+20)
- 0x494548 EnqueueTradeRequest        VERIFIED-1:1 (+14/+18/+1C/+1E/+1F/+23/+24/+25/+35)
- 0x4945c0 EnqueueCmd13               VERIFIED-1:1
- 0x4945e4 EnqueueCmd14               VERIFIED-1:1
- 0x4946a4 QueueRequest18             VERIFIED-1:1 (a3 dropped to [ebp-4])
- 0x4946cc QueueRequest19             VERIFIED-1:1 (a3 dropped)
- 0x494718 QueueRequestBlob21         VERIFIED-1:1 (+0x16 blob 31)
- 0x494a9c QueueRequest31             VERIFIED-1:1
- 0x494cf0 QueueRequestMixed44        VERIFIED-1:1 (+14/+18/+19/+1A/+1C/+1D)
- 0x494d34 QueueRequestMixed45        VERIFIED-1:1 (+1E = BYTE2(a3))
- 0x494d90 QueueRequestString47       VERIFIED-1:1 (2-byte HeStrCopy +0x1C)
- 0x494e6c QueueRequestVectors50      VERIFIED-1:1 (vecA +14/18/1C, vecB +24/28/2C)
- 0x495230 QueueRequestString62       VERIFIED-1:1
- 0x495274 QueueRequestFlagBlob63     VERIFIED-1:1 (+0x10 flag, +0x11 blob 128)
- 0x4956c8 RequestBuildOp77           VERIFIED-1:1
- 0x4956e8 RequestBuildOp78DualStr    VERIFIED-1:1 (HeStrCopy +0x10 / +0x30, +28/+2C)
- 0x49592c RequestBuildOp82           VERIFIED-1:1 (a3 dropped)
- 0x495b58 RequestBuildOp90           VERIFIED-1:1

================================================================================
command_apply5.cpp — Command_Ex create/state handlers
================================================================================

- 0x49644C ExHandleStateGate          VERIFIED-1:1 (`*a1 && !=5/6/7` == op!=0&&...)
- 0x497AD0 ExUseObjectCheck           VERIFIED-1:1 (type 42/278; child+7==1;
                                       container=scn+20; proto sar; RemoveByProt)
- 0x49C19C ExCreateGebaeude           VERIFIED-1:1 (owner+16, seed+20, type+24,
                                       owner marker word, dword_63128C, ack seq=geb)
- 0x49C6AC ExSetCharacterChatBuffer   VERIFIED-1:1 (cap+44, start+40, 2-byte loop,
                                       cap-gate; within chat-hook model)
- 0x49C944 ExRemoveBuildingLink       BOUNDARY (tags 'rmpl'/'rml '; type-300 scene
                                       node not modeled -> faithful return-1-on-miss;
                                       QueryBegin offsets +24/+20 verified)
- 0x49CAC4 ExUpdateBuildingLinks      BOUNDARY (tags next/remv/rmpl; type-301 node
                                       not modeled -> return 1; QueryBegin +24 verified)
- 0x499638 ExAssignPersonToOffice     VERIFIED-1:1 within the office-hook model
                                       (status 2/1, ack slot 5; the Amt placement/
                                       Universe-switch leaves are hooked). Note the
                                       binary's standalone dword_63D734=*(a1+16) write
                                       is to a global not owned by this chunk (DEFERRED).

FIXED:
- 0x496520 ExCreateBuildingDirect — ack->seq was not set. Binary 0x496588 does
  `*(a2+6) = Gebaeude`. Added `ack->seq = (i32)(geb ptr)`.

- 0x496614 ExCreatePersonA —
  (1) standalone RNG draw missing. Binary 0x4966bc: `*(a1+38)=RandNext()` then the
      unconditional RandSeed thunk (edx==new LCG state, writes it back => no-op), NET
      = ONE RandNext draw. Added `pkt.put32(38, crt::RandNext())` in the standalone
      branch so the stream Person_CreateAndSpawn consumes stays in sync.
  (2) parent resolve regression. The current on-disk version resolved the +31 parent
      via FindOwnerIndexById (PERSON array). The binary calls Person_QueryBegin(1,1,id)
      whose cursor seeds from dword_13CE298 (0x586cb9) = the OBJECT/BUILDING array =>
      BuildingFindById semantics. Switched to `BuildingFindById(resolved)`.
  BOUNDARY: non-standalone RandSeed(edx==ack ptr) seeds the RNG with an indeterminate
      host pointer — not reproducible; RNG state left untouched there.

- 0x496714 ExCreatePersonB —
  (1) standalone RNG draw missing (binary 0x496727 `*(a1+69)=RandNext()` + no-op
      RandSeed => ONE draw). Added.
  (2) StrNCopyPad fidelity: `memcpy(rec+48,...,16); rec[63]=0` replaced with a proper
      StrNCopyPad (0x5d9360): stop at first NUL, zero-pad the rest, no terminator for a
      full 16-char name. (The hardcoded rec[63]=0 truncated a full name.) Added a local
      StrNCopyPad helper mirroring the binary.
  Family block (0x4967bf..0x49681c): left as the documented DEFERRED skip (the family
      table word_13C3110 + the CreateAndSpawn family allocation are not in-tree; the
      default backend leaves +81>=0 so GetFamilyRecord returns null and the binary skips
      the block — behavior-identical). +0x54 wappen and +12 faith writes verified.

- 0x49790C ExMoveObjectBetweenLists —
  (1) scene list-head offset wrong: ResolveListHead used childPtr(+63); binary 0x497951
      uses node+20 (entityPtr) as the list HEAD (next links are +63, head is +20).
      Switched to `offsetof(SceneNode, entityPtr)`.
  (2) owner write wrong field & width: binary 0x4979d8 `*(_DWORD*)(v11+6) = a1[4]` writes
      the dst owner id as a DWORD at node byte +6 (pad6), not the i16 ownerId at +0x0A.
      Replaced `moved->ownerId = (i16)dstId` with a 4-byte write at node+6.
  (resolve order src+20 / dst+16 / node+24 and the unlink/relink verified).

- 0x498AB4 ExSysMessage —
  case 0x11: dropped condition restored. Binary 0x498e30 only `++dword_63127C` when
      word_63CC5C!=-1 AND (`*(v20+17) != dword_12CEB18[134*idx]` OR `*(v20+17)==-1`),
      where dword_12CEB18 = person record +0x208. Reconstruction had only the !=-1 gate;
      added the +0x208 slot-id compare.
  case 0x12: precise behavior documented. Binary writes dword_631284 (== g_gameSpeed,
      owned by apply7), dword_631280, reads dword_492EB0/dword_8C98EC tables and calls
      HUD/Config. HANDOFF/DEFERRED (cross-chunk g_gameSpeed + tables + UI leaves);
      routed through the leaf hook (comment expanded; was a one-line "UI leaf").
  case 3: documented the DEFERRED secondary-clock mirror qword_122F840 + the
      Clock timer-proc toggle (not owned / TimeBase leaf). Primary clock write verified.
  cases 0/2/4/5/6/7/8/9/0xA/0xC/0xD/0xF/0x10/0x13 + ack tail: VERIFIED-1:1.
  (case 0/5 RandomSeed_Thunk = RandSeed(indeterminate edx) — BOUNDARY, leaf-routed.)

- 0x49AFF0 ExGebUpgrade —
  standalone branch missing. Binary 0x49b00e: `if (dword_764CE0!=-1) dword_649890 =
      *(a1+20)`. Added `if (!g_standalone5) g_buildingNextId = RdI32(pkt,20)`.
  Verified 1:1: ++*Begin; `Begin[92] = (u8)hi + (100 - (sar v89 by 24))/2` (unsigned hi
      base + SIGNED sar in the subtraction — confirmed correct). Mesh/scene/script block
      is leaf-routed (g_capsHook/g_meshHook). ack status=2 on success (v54 never set to 1).
  BOUNDARY: the "already max" cap test reads the AiPlayer table dword_13CE294[589*lvl]
      (+583>=+584); that table is not in-tree, modeled as a `rec[0]>=250` ceiling (a
      documented analogue — flagged for a real AiPlayer-table owner).

- 0x49B4F4 ExRemoveBuilding —
  (1) occupant entity-id offset wrong: binary 0x49b543 `mov edx,[eax+1Ch]` => occupant
      field at +0x1C (+28), not +7. Fixed.
  (2) person home-building column offset wrong: binary 0x49b55a walks dword_12CEA7C
      [134*i] = person record +0x16C (+364), not +0x178. Fixed (id-model: person_create
      stores b->id at +0x16C, so the bId compare is correct).
  QueryBegin/free/ack verified.

================================================================================
command_apply6.cpp — final Command_Ex batch + group framing
================================================================================

- 0x496B90 ExSellObjekt               VERIFIED-1:1 (DECODE): src+20(remap if !=-1),
                                       dst+16(remap), proto=(+22)>>16, qty+31, raw+35,
                                       good+30, cmdcnt+8, standalone +26. Deep transfer
                                       = TradeSellObjektResolve (separate module). The
                                       dword_631290 last-trade latch is in
                                       buildingtype_callers (documented handoff).
- 0x49818C ExComputeObjectCoords      VERIFIED-1:1. The relation grids A/B are signed
                                       i8 (binary reads via `*(int*)>>24` sar). Band
                                       table (mode4) {threshold,lowBound,highDelta,
                                       lowDelta} confirmed for bands 0..4 incl the
                                       `(u32)band>=4` arm. Clamp [-127,127] (the binary's
                                       `<= -127` is value-identical to `< -127`). mode3
                                       float scale * B-cell -> ConvertX truncate-to-zero
                                       == (int) cast. modes 0/1/2 cell math + the case-1
                                       half = (delta<0)?a:a/2 verified. Other modes fall
                                       through to ack with no mutation.
- 0x49AF00 ExMoveObjectToRoom6        BOUNDARY (room subsystem via MoveRoomView hook;
                                       room+16/person+20 decode, capacity bump/detach/
                                       reparent structure verified 1:1).
- 0x4942C0 ExGroupBegin/End/Skip      VERIFIED-1:1 (pure-ACK markers).

FIXED:
- 0x498954 ExAdvanceGameTick —
  (1) dword_62EB98 (g_tickSubCounter) was zeroed unconditionally after the clock commit.
      Binary clears it at 0x4989f1 INSIDE the dword_63C8E0 (needsAi) block, after the
      LightGray pass. Moved it there.
  (2) threat-stats gate field+sign wrong. Binary 0x4989aa reads the SIGNED dword at
      clock+6 (== g_tickClock.minute in the 14-byte image) `% 6 == 0` via idiv, NOT
      `(u32)g_tickClock.day % 6`. Changed to `g_tickClock.minute % 6`.
  Clock 14-byte commit, gates, always-passes, ack=1 verified.

- 0x497538 ExComputeSellableAmount —
  ack stamping on the resolve-hook failure removed. Binary's source-entity/owner resolve
  failures (0x49759a/0x497798) return 1 WITHOUT writing the ack (handler never pre-stamps
  it); reconstruction wrongly stamped ack->slot=8 there. Now only the deeper "nothing
  producible" path stamps 8. DECODE (src+16 remap, recipe (+18)>>16, startQty+22,
  player=byte_6477A1, standalone+26) verified; deep produce = TradeComputeSellableAmount
  (separate module). The 6-vs-8 ack distinction belongs to that module (documented).

- 0x498678 ExShowMessageBox —
  the !ack flag block corrected. The v4/a2 arg He_AllocHandlerEntry receives is CLOBBERED
  inside it (reused as a FindRecordById scratch) so its value is moot; the MEANINGFUL
  effect is the in-place mutation of BYTE2(desc[13]) == the desc flags byte at +54 (which
  He copies to rec[120] and tests &8): when (desc[13]&0x20000), set it to (orig&0xF9)|4.
  Added that desc[+54] write; corrected curFlags. desc[13]&0x10000 person gate, k==7 /
  (k!=6 && !ack) rejects, kind-17 chat-buffer length (strlen+1+strlen / strlen, +1)
  verified. He alloc itself = deferred He-pool leaf (hook); the desc template unk_1077B60
  is not in-tree (model boundary).

- 0x4942C0 ExecCommandGroup (vector variant) —
  rewritten to the binary's three behaviors (the faithful pointer-walk version already
  lives in command_receive.cpp; this vector wrapper diverged):
    * gate pre-pass dispatches frames AFTER begin, STOPPING on the first failure
      (`while (v3 && *v2!=6)`) — was: dispatch all members, no early stop;
    * accept => stamp ONLY begin(front)=1 and the end(op6) frame=1; middle frames keep
      their dispatch status — was: stamp ALL members=1;
    * reject => stamp begin..end inclusive=2 (unchanged).
  GOLDEN FIXED: tests/unit/sim_command_apply6_test.cpp ExecCommandGroupAllSucceed now
  asserts m1.bytes[0]==0x55 (the middle frame keeps its opcode/dispatch byte), not 1.
  Cited 0x4942fd.

NOTE (out-of-edit): command_apply6.cpp's RelationState was changed by an external edit to
self-contained std::vector<i8> grids (previously aliased to world::g_relationMatrix). Both
grids are signed i8 (matches the binary's sar-24 reads), so the relation math is faithful;
the world-grid aliasing is a wiring choice left as-is (outside my edits).

================================================================================
BUILD
================================================================================
All three owned files + the edited test compile clean (g++ -std=c++17 -fsyntax-only,
build include set). The FULL `guild` library link is currently blocked by an UNRELATED,
untracked WIP file `src/gui/widget_layout.cpp` (11 uses of a non-existent `Widget::ld<>`
accessor) from another chunk — NOT my files. Existing apply5 unit tests re-checked: my
behavioral fixes are value-preserving for them (RNG draws hit unused packet bytes;
offset fixes touch fields the tests don't seed).

COUNTS
  command_apply7.cpp : 25 functions  — 25 VERIFIED-1:1, 0 FIXED
  command_apply5.cpp : 16 functions  — 5 VERIFIED-1:1, 8 FIXED, 3 BOUNDARY
  command_apply6.cpp : 13 functions  — 5 VERIFIED-1:1, 4 FIXED, 4 BOUNDARY
  golden tests fixed : 1 (ExecCommandGroupAllSucceed)
