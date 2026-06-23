# Hardening sweep — sim chunk 04: command_apply8 / command_apply9 / command_apply10

MCP live (module gilde.exe). Every function carrying a `gilde.exe 0xADDR`
provenance was decompiled AND disassembled, then diff'd line-for-line against the
reconstruction. Where Hex-Rays disagreed with disasm, disasm is the reference of
record. Golden tests that encoded wrong behavior were fixed to the binary.

Files owned: `src/sim/command_apply8.{cpp,h}`, `command_apply9.{cpp,h}`,
`command_apply10.{cpp,h}` + their tests under `tests/{unit,integration,e2e}/`.

Build: my three .cpp compile clean (`-std=c++17 -Wall -Wextra`, 0 warnings).
The full `guild` lib link is broken by the UNRELATED untracked
`src/gui/widget_layout.cpp` (`Widget::ld` missing) — NOT mine. I linked the
affected unit/e2e/itest TUs standalone against freshly-built objects and ran them.

## Test results (standalone, post-fix)
- command_apply8_test:  4058 checks, 0 failures
- command_apply9_test:  5123 checks, 0 failures (incl. fixed Op85 + ACK offsets)
- command_apply9 e2e:   20 checks, 0 failures (ACK status accessor +0)
- command_apply9 itest: 43 checks, 0 failures
- command_apply10_test: 47 checks, 0 failures (incl. new office-scan + thief tests)
- command_apply10 e2e:  9 checks, 0 failures (corrected Ownership expectations)
- command_apply10 itest: exercises only CheckTargetNotInUse/CheckSyncRangeAcked
  (both unchanged & verified 1:1); not relinked due to wider entity dep chain.

---

## command_apply8.cpp

- 0x4946f4 QueueRequest20 — VERIFIED-1:1 (opcode 20; a1@+0x10, a2@+0x14).
- 0x494b98 QueueRequestPair36 — VERIFIED-1:1 (opcode 36; a1@+0x10, a2@+0x14;
  original writes v5 before v4 — independent slots, no observable diff).
- 0x494790 QueueRequestState23 — VERIFIED-1:1 (124-byte blob @+0x10).
- 0x4947d0 QueueRequestState24 — VERIFIED-1:1.
- 0x494c58 QueueRequest40 — VERIFIED-1:1 (StagePendingBlock 0x114 then opcode 40).
- 0x494b28 QueueRequestObject34 — VERIFIED-1:1 (copy 0x2D template, [0]=dword_632240,
  +0x25=1, +0x29=RandNext; v4[38]=a2 lands at packet+0xA8 outside the 153-byte
  record → dropped, documented). objectTemplate34/randNext = BOUNDARY (file globals).
- 0x49441c EnqueueBuildingActionStart — VERIFIED-1:1 (StrNCopyPad copies ≤128 +
  NUL-pad to 128; checked StrNCopyPad @0x5d9360; memset(129) harmless on zeroed pkt).
- 0x493e88 SendPlayerMoneyState — VERIFIED-1:1 (money snapshot → FlagBlob32(19)).
  localPlayerMoney = BOUNDARY (dword_12CE914[134*word_63CC5C]).
- 0x594c8c RequestBuildOp90_Thunk — VERIFIED-1:1 (RequestBuildOp90(eax,edx) reg-map).
- 0x4fbd00 QueueSetJusticeSeverity / JusticeClampAbsolute — VERIFIED-1:1. Disasm
  0x4fbd96 (mov ecx,eax) confirms the clamp operand v9 == parsed value v8; out-of-band
  reject `(v8<min||v8>max)&&adjustable`; clamp == clamp(value,min,max). LawRecord
  {min,max,adjustable} matches frame var_34/var_30/var_37. gesetz hooks = BOUNDARY.
- 0x4fbe04 QueueAdjustJusticeSeverity / JusticeClampDelta — VERIFIED-1:1
  (target=current+sign*delta; clamp(target,min,max); sign=-1/+1 from MINUS/PLUS).

## command_apply9.cpp

- 0x493308 QueueReset / 0x4933c0 QueueResetAlt — **FIXED (ACK status offset)**.
  Ring free-list (prev@+0x91, next@+0x95) modeled as offsets-from-ring-base — the
  binary stores absolute pointers; offset model is internally consistent → VERIFIED.
  ACK pass: binary writes `*(&dword_B5FB58+result) = -1` (= byte_B5FB56+result+2)
  and `byte_B5FB56[result] = 1` (= byte_B5FB56+result+0). The reader
  CheckSyncRangeAcked @0x493a34 reads status at `byte_B5FB60[10*seq]` (==
  byte_B5FB56[10*(seq+1)]), proving **status is at entry offset +0, not +8**.
  - BEFORE: status written at `(result-10)+8`; accessor `ack_status(i)=ack[i*10+8]`.
  - AFTER:  status written at `(result-10)+0`; accessor `ack_status(i)=ack[i*10+0]`.
  - Evidence: disasm/decompile of 0x493308 (byte_B5FB56 vs dword_B5FB58 = 2 apart)
    + 0x493a34 reader base byte_B5FB60. ack_ring (+2) was already correct.
  - Header doc + struct comments updated to "status @+0, ring @+2".
- 0x493f34 WaitForPacketType — VERIFIED-1:1 (deadline=tick+timeout; wrap/0 → null;
  scan +149 chain for opcode; tick>=deadline → null). +149 link = BOUNDARY (64-bit).
- 0x5681cc EncodeFlagStateMask / EncodeFlagState — VERIFIED-1:1. All 9 ladder
  constants confirmed: 7,48,768,4096,49152,0x20000,0x100000,0x800000,0x6000000;
  all masks (0xF,0xF0,0xF,0x30,0x1C000,0xE,0x70,0x180(word46),0x1E) match.
- 0x496124 CheckObjectFlagClear — VERIFIED-1:1 (`!Begin || (Begin[90]&2)==0`).
- 0x49614c CheckCanRunForOffice — VERIFIED-1:1 (`!CanRunForOffice(cmd+16,a2)`).
- 0x496160 CheckOfficePrerequisites — VERIFIED-1:1 (`CheckPrerequisitesMet(cmd+16)==0`).
- 0x4958d0 RequestBuildOp81 — VERIFIED-1:1 (opcode 81; a1@+0x10, body44@+0x14,
  snapshot@+0x40; checked twin Op80 @0x495874). op80SnapshotDword = BOUNDARY.
- 0x4959a8 RequestBuildOp85Unit — **FIXED (body & transform offsets)**. Disasm
  0x4959c2 `lea edi,[esp+var_77]` proves the 44-byte body (v16 int[11]) lands at
  payload **+0x35**, not +0x15; and the six transform dwords sit at the exact
  non-contiguous frame offsets 0x15,0x19,0x1D,0x25,0x29,0x2D (gap 0x21..0x24),
  field36 (v15) @+0x31. v16[4]@+0x45 keys combatUnitField9; v16[10]@+0x5D is
  overwritten when mode==2.
  - BEFORE: body @+0x15; transforms @0x15+i*4; v16[4]@+0x25; v16[10]@+0x3D.
  - AFTER:  body @+0x35; transforms via {0x15,0x19,0x1D,0x25,0x29,0x2D}; field36
    @+0x31; v16[4]@+0x45; v16[10]@+0x5D.
  - Tests `Op85UnitTransform` / `Op85NullUnitJustBody` were FIXED to the new
    offsets (they had symmetric-but-wrong asserts). Header doc updated.
- 0x494dd8 RequestChrMoveToUniverse — VERIFIED-1:1 (opcode 48; a1/a2/a4 @+0x10/14/18;
  strlen≥0x20 → errorLog + no copy; 2-byte-stride pairwise copy @+0x1C, stop at NUL
  in either lane; exact error string matched).
- 0x4fbbcc QueueSetSelectedFlag — VERIFIED-1:1. AppendRawField arg-order checked vs
  0x493c14: original `(width,count,values,fieldOffset)`; the reconstructed codec
  wrapper reorders to `(width,count,fieldOffset,values)`, so `AppendRawField(1,1,433,
  &flag)` is the correct mapping of `(1,1,&flag,433)`. The `idx>=8` clamp +
  `*(v5+8*v4+4)` table read = BOUNDARY (selectedPersonRecord hook).
- 0x4fbc40 QueueRevealAllPersons — VERIFIED-1:1 structurally (loop 768, '-'+≥4 tail,
  ToLower skipped (caller buffer), active count, QueueRequest17(...,active,flagByte,0)).
  **Header doc FIXED**: the per-slot stride is 134 — outEntityId is dword_12CE914[134*i]
  and the slot tests use 268*i/536*i/(134*i*4); doc previously said `[i]`. The math
  lives in revealableSlot = BOUNDARY.

## command_apply10.cpp

- SubstituteSpecialTarget (-2/-3/-4 → dword_631288/8C/90) — VERIFIED-1:1 (switch
  prologue present in every Check* target gate).
- 0x495cf8 CheckSourceTargetReachable — **FIXED (found field offset)**. v6 is
  `__int16*`; `*(_DWORD*)(v6+7)` is +14 BYTES (disasm 0x495d63 `mov eax,[eax+0Eh]`),
  compared to cmd+31. Keys: imm+93 / parent+20(v13+10w) / object+376(v16[0]+188w);
  CountAtLocation(v9+376). ComputeFree/CarryCapacity use cmd+22 **sar 16** (signed,
  disasm 0x495ea9/0x495ed1 — Hex-Rays "HIWORD" is wrong) and cmd+31 — recon's signed
  `>>16` is correct.
  - BEFORE: `rdI32(found, 28)`. AFTER: `rdI32(found, 14)`.
- 0x496080 CheckTargetOwnership — **FIXED (inverted guard logic)**. Disasm
  0x4960b9..0x4960dd: returns 1 (loc_4960DF) ONLY when ALL hold — object!=null &&
  v5==object+456 && (cmd30 & 0x80) i.e. (i8)cmd30<0 && (object458 & 0x80) i.e.
  (i8)obj458<0; any failure jumps to loc_496116 = return 0.
  - BEFORE: `if (object && v5==obj+456 && cmd30>=0 && obj458>=0) return 0;` — wrong
    sign on both guard bytes AND inverted return polarity.
  - AFTER: `keep = object && v5==obj+456 && cmd30<0 && obj458<0; if(!keep) return 0;`.
  - Tests fixed: unit `AcceptWhenFieldNegative` (was wrong) → split into
    `AcceptWhenBothGuardsNegative` / `RejectWhenOnlyCmd30Negative` /
    `AcceptWhenV5NotReservedSlot` / renamed `RejectWhenGuardFieldsNonNegative`; e2e
    expectation `cmd20==0 → 1` corrected to `→ 0` + added the all-negative accept case.
- 0x495c64 CheckTargetCooldown — VERIFIED-1:1 (keys imm+93/parent+20/object+376;
  `CountAtLocation(key)-cmd+29 >= 0 → 0` else 1).
- 0x4963ec CheckTargetNotInUse — VERIFIED-1:1 (ownerKey=cmd+16; up to 16 entries
  read at cmd+20+4*i; state 6/7; bound=rec+520; bound!=-1 && bound!=ownerKey → 1).
- 0x496174 CheckPersonHasOfficeTag — **FIXED (' adm' scan stride)**. The ' adm'
  (1651865888) branch walks v9 (`__int16*`) by `v9 += 2` (= +4 BYTES) reading
  `*((_DWORD*)v9+7)` (+28); entry k field is off+28+4*k, NOT +8*k.
  - BEFORE: `rdI32(off, 28 + 8*k)`. AFTER: `rdI32(off, 28 + 4*k)`.
  - 'rdpm' branch (tick compare off+40, recordEntityId live-tick hook) = BOUNDARY,
    structure VERIFIED. Added unit tests AdmMatchAtSlot0/AdmMatchAtSlot1Stride4/AdmNoMatch.
- 0x49623c CheckOfficeSlotByTag (+ OfficeSlotScan helper) — **FIXED (scan stride)**.
  Tags 'nmab'(0x6A6F696E=1785686382)/'vmba'(0x6C656176=1818583414)/'rdpm'(0x636C6572
  =1668048242); QueryFind selector 301; want=cmd+20 (`mov ecx,eax` ⇒ v6==cmd). Same
  `__int16* += 2` (+4 bytes) stride as above.
  - BEFORE: OfficeSlotScan + 'vmba' loop used `28 + 8*k`. AFTER: `28 + 4*k`.
  - 'rdpm' selection fallback (word_12CE910 / dword_6498E4 sentinel) = BOUNDARY.
  - Added unit tests NmabFoundWantStride4/NmabFreeSlotReject/VmbaNoWantAccept/
    VmbaWantAtSlot2Reject.
- 0x495ef8 CheckParamRefsValid — VERIFIED-1:1. Descriptor stride 4 (width@+0,count@+1,
  fieldOff u16@+2); object guards v8==object+4 (+2w) reject, v8==object+92 (+46w)
  owner-link validate (rec+92 vs object+4); inline-skip widths: 1→+count, 2→+2*count,
  4→+4*count, else (incl. 3,0) → none. All confirmed.
- 0x493a34 CheckSyncRangeAcked — VERIFIED-1:1 (start==end→1; unsigned start>=end→1;
  status `ackTable[10*(seq&0x7FFF)]`; 0→break(→0); 2→NAK(result=-1); ++seq>=end→result).
  Confirms ack status entry offset +0 (consistent with the apply9 fix above).
- 0x4fa178 ResolveTargetBestThief — **FIXED (slot index + write-back value)**.
  mode==1 reads `*(_DWORD*)&a2[4*a4+2]` = byte 8*a4+4 = i32 index 2*a4+1 (disasm
  0x4fa1a4 `shl edi,3; lea ecx,[esi+edi]; mov ecx,[ecx+4]`). mode==0 store
  `*(_DWORD*)&a2[8*a4+4] = *(_DWORD*)(chosen+1)` (disasm 0x4fa221 `mov eax,[ebx+1];
  mov [esi+edi*8+4],eax`) — UNALIGNED read at chosen+1, slot index 2*a4+1. RoomWorth
  arg `*(p+89) sar 24` (signed) confirmed.
  - BEFORE: read `slotTable[slotIdx]`; store `slotTable[slotIdx]=rdI32(chosen,4)`.
  - AFTER:  read `slotTable[2*slotIdx+1]`; store `slotTable[2*slotIdx+1]=rdI32(chosen,1)`.
  - Unit test `IteratePicksHighestWorth` FIXED to slot 2*idx+1 and value *(chosen+1).
  - QueryBegin(a2,...) first arg vs recon's 0 = BOUNDARY (hook-driven iteration).
- ResolveByClass / ResolveTargetGuard / ResolveTargetOfficial — NO `0xADDR`
  provenance in source (shared-body abstraction; the real binary funcs are separate
  and were not pinned). Their slot indexing `2*idx+1` and `*(rec+4)` store / `rdU16(
  rec,0)` render were CROSS-CHECKED against the same-pattern PersonScoped @0x4f9c20
  (`*(a2+8*v34+4)=*((_DWORD*)v11+1)=*(v11+4)`, render `(u16)*v11`) and are consistent
  — left unchanged. (Out-of-strict-scope; noted for the next wave to pin addresses.)

## Counts
- VERIFIED-1:1: 22
- FIXED: 7 (apply9: ACK status offset, Op85 offsets, RevealAll doc-stride;
  apply10: Reachable +14, Ownership guard logic, OfficeTag/OfficeSlot scan stride,
  BestThief slot index+value)
- BOUNDARY hooks (genuine cross-module file globals / 64-bit links / data tables):
  numerous, all documented inline.

## Handoffs (out of chunk — none required)
None. All fixes were confined to the three owned .cpp/.h + their tests. The apply9
ACK-status offset fix keeps writer (apply9 QueueReset) and reader (apply10
CheckSyncRangeAcked, already +0) consistent — both owned here.
