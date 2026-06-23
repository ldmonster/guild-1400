# Hardening sweep — command_apply2/3/4 (sim)

Chunk: `src/sim/command_apply2.cpp`, `command_apply3.cpp`, `command_apply4.cpp` (+ their unit tests).
Method: for every `gilde.exe 0xADDR` provenance, decompiled + disassembled the original
(IDA MCP, module gilde.exe) and diffed control flow / offsets / ack stamps / return values /
RNG draws line-for-line. DISASM is the reference of record where Hex-Rays collapses __usercall args.

Build: my 3 sources + 3 test files all pass `g++ -std=c++17 -fsyntax-only -Isrc -Ishim -Iinclude`.
Full `cmake --build build` and the test binaries are BLOCKED by other agents' in-flight files
(untracked `src/gui/widget_layout.cpp` does not compile; many stale `play/`,`io/`,`render/`
objects have undefined refs from concurrent refactors). Those are outside my ownership.

## command_apply2.cpp

| Addr | Function | Status |
|------|----------|--------|
| 0x496888 | ExBindObjectProto | VERIFIED-1:1 (ack +6 ptr modeled as idx) |
| 0x4968D4 | ExRemapAndValidateObject | **FIXED** — remap was sign-extending the id and remapping -3/-4; binary zero-extends the u16, so only raw==0xFFFE remaps (to LOWORD(dword_631288)); -3/-4 are DEAD code (`cmp eax,0xFFFFFFFD/FC` never match a 0..0xFFFF value). |
| 0x496978 | ExRemapObjectPair | VERIFIED-1:1 (proto table dword_13CD6F2 + dword_649890 networked-write deferred — see notes) |
| 0x496A90 | ExUpdateObjectPair | VERIFIED-1:1 (same deferred notes) |
| 0x498890 | ExSetHE | VERIFIED-1:1 for the +68..+115 field copies incl. signed `>>24`; BOUNDARY: the `qmemcpy(FH+172,&unk_1077B60,0xA0)` template tail writes past the modeled 176-byte HeRecord (orig record is larger) |
| 0x499558 | ExSelectObject | VERIFIED-1:1 (person-before-scene order, -1/-2 returns; child QueryFind -> deferred, -2 path unreachable in model) |
| 0x49BBF8 | ExSetObjectState | **FIXED** — success ack only set status; binary writes +0=1,+1=0,+6=0. |
| 0x49BD74 | ExAssignOffice | VERIFIED-1:1 |
| 0x49BDB0 | ExTransferOffice | VERIFIED-1:1 |
| 0x49BDEC | ExReleaseOffice | VERIFIED-1:1 (always returns 0) |
| 0x49D1BC | ExSwapOfficeHolders | **FIXED** — ack status=1 was set on ret==0; disasm 0x49d1dd `test eax,eax; jnz` => `*v3=1` runs ONLY when ret!=0. Inverted. Golden also fixed. |
| 0x49D2E8 | ExUpsertTradeEntry | VERIFIED-1:1 (all node offsets +28/+32/+36/+40/+42/+50/+54, pct clamp<=100; new-node id + dword_649890 networked-write deferred) |

Counts: 8 VERIFIED, 3 FIXED, (ExSetHE has 1 BOUNDARY tail).

## command_apply3.cpp

| Addr | Function | Status |
|------|----------|--------|
| 0x4993A4 | ExAllocCutsceneWithId | VERIFIED-with-leaf (+120 <- payload+20; template/word_63C740/dword_649894 counters deferred) |
| 0x49947C | ExAllocCutscene | VERIFIED-with-leaf (alloc/reject + ack(1,9); template deferred) |
| 0x4994E8 | ExSetCutsceneState | VERIFIED-1:1 (FindById + AddParticipant(id,+20) gates) |
| 0x499520 | ExClearCutsceneState | VERIFIED-1:1 (RemoveParticipant always returns 1 in orig -> FindById is the only gate; removed-participant key is a context register, deferred) |
| 0x49CFC0 | ExCutsceneReady | **FIXED** — loop ran 4 ids at +0x10..+0x1C; disasm 0x49d011-29: edx walks +0x10..+0x4C reading [edx+4] => 16 ids at payload +0x14..+0x50, slot id = *(a1+0x10). int3(kind 6/7) is debug-only; net write record[+520]=slotId for every found person. Golden updated. |
| 0x49D3F0 | ExSetObjectField | VERIFIED-1:1 (entry 2,0,0; master = dword[3]=+12 <- +20; ack=1) |
| 0x4998E4 | ExCharPlaySample | **FIXED** — not-found returned 1; binary `return result` == null find handle == 0. |
| 0x499950 | ExChrWalkToDummy | VERIFIED-with-leaf (not-found ret 1; path/universe/avatar deferred) |
| 0x499B28 | ExCharUseObject | VERIFIED-with-leaf (not-found ret 1; v32=2 success ack; query/avatar deferred) |
| 0x499DC4 | ExCharStandUp | VERIFIED-1:1 (not-found ret 2; ack 2,0,0) |
| 0x499E1C | ExCharSetVisibility | VERIFIED-1:1 (not-found ret 1; ack 2,0,0) |
| 0x499E84 | ExCharQueueAction | VERIFIED-1:1 (not-found ret 1; action 0x38; ack 2,0,0) |
| 0x499EEC | ExCharSpawnAtEntrance | VERIFIED-with-leaf (Spawn(+20,0,+24,+16,0); ack 2,7,result; spawn deferred) |
| 0x499F24 | ExChrGotoBuilding | VERIFIED-with-leaf (not-found ret 1; ack 1,7,actor; ~1.5KB path SM deferred) |
| 0x49AC6C | ExCharUseGate | VERIFIED-with-leaf (not-found ret 1; ack 2,0,0; gate SM deferred) |
| 0x49ADF4 | ExCharPlaySound | VERIFIED-1:1 (not-found ret 2; sound id = byte +0x44; ack 2,0,0) |
| 0x49B8D8 | ExCharApplyInteraction | **FIXED** — success ack set status=1 via stray AckOk; binary 0x49bba0 stamps status=v52==2 (slot=1). Golden updated. (record[97] avatar gate + mesh swap deferred) |
| 0x49BD3C | ExTriggerCharacterAction | VERIFIED-1:1 (ack 1,0,0 unconditional) |
| 0x49BE74 | ExApplyCharacterUpdate | VERIFIED-1:1 (category = byte +20; ack 1,0,0) |
| 0x49BEC4 | ExSpawnAndPlaceCharacter | VERIFIED-with-leaf (kind=byte+20, create args byte+31/+32; g_lastObjectId<-newId; ack 1,1,rec; RandSeed-state write + timeGetTime packet write + spawn deferred) |
| 0x49C094 | ExDeselectObject | VERIFIED-1:1 (find gate; ack 1,0,0) |
| 0x49C0C0 | ExAppendChatLine | VERIFIED-with-leaf (8-id target scan +0x10..+0x2C; append from +0x30; ack 1,0,0; "$A" throttle + dword_631E88 dirty-flag deferred display) |
| 0x49B65C | ExEquipCombatObject | **FIXED** — owner-not-found path stamped ack(2,1,0); binary's PersonQueryBegin-null path `return 1` does NOT touch ack (the ack(2,1,0)/return-2 belongs to the deferred ResolveTargetEntityRef target-fail). Removed the stamp. (RandNext draw + deep combat deferred) |
| 0x49C754 | ExStoreCombatSlot | **FIXED** — gate id read at +0x10 (should be +0x40, a1[16]) and squad key from cmd_id (+4, should be +0x10, a1[4]). Disasm `mov eax,[ecx+40h]` / `mov edi,[ecx+10h]`. Shared StoreCombatSlot fixes both 0x50/0x51. Golden updated. |
| 0x49C824 | ExUpdateCombatSlot | **FIXED** (same shared StoreCombatSlot offsets; priority-merge condition is the deferred FindOrAllocSlot leaf) |
| 0x49C8EC | ExApplyCombatDamage | VERIFIED-1:1 (hp at +0x24 -= +0x14; alive +0x08 <- byte +0x18; ack 1,8,0) |
| 0x49CDA0 | ExDispatchUnitOrder | VERIFIED-with-leaf (gate +0x10; unit id +0x35; order byte +0x14; cases 1-6 alive-gated, 7 unconditional; combat actions deferred; ack 1,8,0) |

Counts: 16 VERIFIED(-with-leaf), 6 FIXED.

## command_apply4.cpp

| Addr | Function | Status |
|------|----------|--------|
| 0x496474 | ExHandleAck | VERIFIED-1:1 |
| 0x496484 | ExHandleNoop | VERIFIED-1:1 (return 1, no ack) |
| 0x49648C | ExSetGlobalFlag | VERIFIED-1:1 (byte_63CC28 \|= byte +0x10) |
| 0x4964A4 | ExLoadBufAlloc | VERIFIED-1:1 (size +0x10; cursor=0; alloc deferred to pool) |
| 0x4964D8 | ExLoadBufAppend | VERIFIED-1:1 (memcpy 0x80 at cursor; cursor+=128) |
| 0x498A4C | ExAckStub | VERIFIED-1:1 (Hud probe; ack=1) |
| 0x497B8C | ExSetObjectTransform | **FIXED** — success ack wrote slot=3; binary 0x497bdd writes ONLY `*a2=1` (status), slot/seq untouched. Golden updated. (proto=HIWORD(dword@+0x12); copies +28/0x1C, +56 word, +58 byte; g_lastTradeId<-obj+2; dword_649890 networked-write deferred) |
| 0x499854 | ExSpawnEffectObject | VERIFIED-1:1 (proto 437; obj[18]=64; +28/+32/+34(cleared 0); g_lastTradeId<-obj+2; ack 1,3,obj) |
| 0x49BC50 | ExAdjustObjectTransform | **FIXED** — success ack only set status; binary writes +0=1,+1=0,+6=0. (int deltas +0x13/+0x17/+0x1B -> slot[4/5/6]; float adds +0x1F/+0x23/+0x27/+0x2B -> slot[8/9/11/14]; pure float adds, no f->i) |
| 0x498F44 | ExAddStraftat | VERIFIED-with-leaf (counter ++/<-+0x10; stride-45 slot copy 0x2D + id overwrite; ack 1,6,slotptr; crime/He/Beweis/RandSeed deferred) |
| 0x4991B0 | ExDispatchStatusResult | VERIFIED-1:1 (switch 1/2/3 -> ret; default ack=1 ret 0) |
| 0x49B610 | ExQueryObjectStatus | VERIFIED-1:1 (Update(+16,+20,+28,+24); status=(n>0)?1:2) |
| 0x499288 | ExSendCutInfo | VERIFIED-with-leaf (slot search by player; time-clear on found; ack 1,8,slot; not-found ret 1; 0x35C blob + cut-target gate deferred) |
| 0x498EAC | ExRemovePersonAndScript | VERIFIED-with-leaf (entry *a1=2; ChangePlayerAction; script-finish via record+388; RemoveAndCleanup(marker,+0x14); status=1 on found; script-handle +40 indirection deferred) |
| 0x49AE60 | ExSetObjectParent | VERIFIED-1:1 (owner QueryBegin +0x10; child +0x18 ret 1; parent +0x14 ret 2; SetObjectParent(owner, parentMarker, childMarker); ack stays 2) |
| 0x49BCF0 | ExActivateObject | VERIFIED-1:1 (chimney gate byte +0x61=+97 != 0 && !IsProductionType; ack 1,0,0) |
| 0x49BE14 | ExChangePlayerHead | **FIXED** — ack only set status; binary writes +1=0,+6=0 too. (guard on marker != -1; arg from +0x14) |
| 0x49C580 | ExStopCharacterScript | **FIXED** — not-found returned 0; JUMPOUT(0x49C0A0) == `mov eax,1; retn` => return 1. Also ack now stamps +1=0,+6=0. |
| 0x49C5CC | ExEnqueueCharacterAction | **FIXED** — tag flag read at byte +0x14; disasm 0x49c5e6 `cmp byte[ecx+10h],0` => flag is byte +0x10 (the sp-name first char; low byte of the id in the integer model). Also ack now stamps +1=0,+6=0. Golden updated. |
| 0x49CEEC | ExDestroyCharacter | **FIXED** — entry ack only set status=2; binary stamps +0=2,+1=0,+6=0. (not-found/no-charPtr ret 1; record+388<-0) |
| 0x49CF40 | ExClearObjectOccupants | **FIXED** — success ack only set status; binary writes +0=1,+1=0,+6=0. (participant loop record[+520]==slotId -> -1; RemoveById) |
| 0x58820C | DefaultSetObjectParent (leaf) | VERIFIED-1:1 for the dword_12CEA80(+368) column slice: bld+0x25<-a2; old-owner clear @0x58827c; bld+0x27<-a3; class==2 owner-populate w/ signed-char type cmp @0x5884b5; kind 6/7 staff fields 357=0,436&=0xFE,(class 5/9)364=0. Render remainder (AttachStorageRooms, 0x2000 scene loop, flag-node refresh) is the documented hook boundary. |

Counts: 14 VERIFIED(-with-leaf), 8 FIXED.

## Totals
- VERIFIED-1:1 / VERIFIED-with-leaf: 38
- FIXED: 17 (apply2: 3, apply3: 6, apply4: 8)
- BOUNDARY tails noted: ExSetHE template copy.
- Goldens corrected to the binary: SwapOfficeHolders (status inversion), CutsceneReady
  (16 ids @ +0x14), StoreCombatSlot (gate +0x40 / squad +0x10), CharApplyInteraction
  (success status=2), SetObjectTransform (no slot stamp), EnqueueCharacterAction (tag @ +0x10).

## Documented deferred globals (Rule 8 boundaries, not modeled in this chunk; not observable via tests)
- `dword_649890 = *(a1+...)` networked-mode write in ExRemapObjectPair/ExUpdateObjectPair/
  ExUpsertTradeEntry/ExSetObjectTransform/ExSpawnEffectObject/ExSpawnAndPlaceCharacter/
  ExEquipCombatObject (gated on `dword_764CE0 != -1`, i.e. NON-standalone).
- `VIBE_Math_RandomSeed_Thunk()` (sets rand state to a register-leftover value; draws nothing)
  in ExSpawnAndPlaceCharacter / ExEquipCombatObject / ExAddStraftat(victim-no-avatar branch).
- `RandNext()` draw stored into the packet (+0x35) in ExEquipCombatObject standalone branch —
  part of the deferred combat-equip subsystem.
- `timeGetTime()` packet write (+0x23) in ExSpawnAndPlaceCharacter standalone branch (rule-4
  platform-time boundary).
- `dword_631E88 = dword_62EB38` chat-dirty flag in ExAppendChatLine (display).

## Handoffs (none required)
All fixes were confined to the three owned .cpp files + their three unit tests. No shared
symbol signatures changed. `RemoveParticipant`/`AddParticipant`/`AllocSlot` (cutscene.cpp,
outside chunk) were inspected only; their existing signatures are consistent with the apply
callers (RemoveParticipant always returns 1 in the binary, so the FindById gate is faithful).
