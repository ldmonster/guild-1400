# Wave-H1b FIX-COMMAND — command_apply2/3/4 + actionqueue

All 4 assigned failing tests are now GREEN. Each failure was a 1:1 divergence frozen
by the stash accident; every fix is cited against the gilde.exe disasm/decompile
(reference of record). No git was run.

Build/run (build dir is `build-vk`):
```
cmake --build build-vk -j --target sim_command_apply2_test sim_command_apply3_test \
      sim_command_apply4_test actionqueue_boundary_test
cd build-vk && GUILD_GAME_DIR=$PWD/../europe_guild_1400_original \
  ctest -R 'sim_command_apply2_test|sim_command_apply3_test|sim_command_apply4_test|actionqueue_boundary_test' --output-on-failure
```
Result: 4/4 passed. `sim_charaction_misc_test` re-run (charaction.cpp is shared) — still passes, no regression.

---

## 1. actionqueue_boundary_test — RegisterHandlersCatalogGolden (SOURCE fix)

Failure: `ActionType(7).animName` expected `"bewegung/dreh_90_rechts"`, source gave `"bewegung/dreh"`.

Evidence — `VIBE_CharAction_RegisterHandlers` @0x40be30 disasm:
- 0x40be6a `mov ecx, offset aBewegungDreh90 ; "bewegung/dreh_90_rechts"` (string @0x61070c)
- 0x40be6f `call VIBE_Light_SetGrayColorThunk` (preserves ecx)
- 0x40be85 `call VIBE_Character_DeclareAction` with eax=7

`VIBE_Character_DeclareAction` @0x405558 takes animName in `a3@<ecx>` and copies it
byte-by-byte. So type 7's animName is the FULL `"bewegung/dreh_90_rechts"`, not the
truncated literal.

Fix: `src/sim/charaction.cpp:287` — DeclareAction(7, ...) animName literal corrected to
`"bewegung/dreh_90_rechts"`. (Test golden was already binary-correct.)

Note on the DispatchCurrent gate flagged in the brief: verified `VIBE_ActionQueue_DispatchCurrent`
@0x404768 — gate is `cmp dword ptr [edx+14h],0; jz ->ret 1` (owner +0x14), then
`cmp dword ptr [edx],0; jnz` (step ptr at +0). `src/sim/actionqueue.cpp` DispatchCurrent
already uses `node->owner` (+0x14) — already binary-correct, no change needed.

## 2. sim_command_apply2_test — SwapOfficeHolders_StatusOnSuccess (GOLDEN fix)

`VIBE_Command_ExSwapOfficeHolders` @0x49D1BC disasm:
- entry stamp (if ack): `[edx]=2, [edx+1]=0, [edx+6]=0`
- call SwapHolders -> eax(=ecx)
- 0x49d1dd `test eax,eax; jnz loc_49D1ED` -> `mov byte [edx],1` runs ONLY when result != 0
- return `setz al` = (result==0)

So: result==0 -> return 1, status stays 2; result!=0 -> return 0, status=1.
The golden had the two `ack.status` asserts inverted. Source (command_apply2.cpp:414)
was already binary-correct.

Fix: `tests/unit/sim_command_apply2_test.cpp` — status 1->2 (result==0 case), 2->1 (result==3 case).

## 3. sim_command_apply3_test — 4 sub-failures (all GOLDEN fixes; source binary-correct)

- **CharPlaySample_MissingActorRejected** — `ExCharPlaySample` @0x4998E4: null find
  -> `return result` where result == the null handle (0). Missing actor returns 0
  (not 1), ack untouched. Golden return expectation 1 -> 0.
- **CharApplyInteraction_NeedsPerson** — `ExCharApplyInteraction` @0x49B8D8 success
  path @0x49bba0: `v41=v52(=2); *(a2)=2 (status), *(a2+1)=1 (slot), *(a2+6)=person`.
  Status stays 2 on success, NOT 1. Golden status 1 -> 2.
- **StoreCombatSlot_LocalGate** — `ExStoreCombatSlot` @0x49C754: 0x49c777
  `mov eax,[ecx+40h]` -> the local-battle gate id is `a1[16]` == payload **+0x40**,
  not +0x10. Golden wrote the local id to +0x10; corrected to +0x40 (both the
  not-local and local cases). Ack stamp (`*(a2)=1, *(a2+1)=8`) is unconditional past
  the gate (status=1, slot=8) — already matched.
- **EquipCombatObject_RequiresOwner** — `ExEquipCombatObject` @0x49B65C: owner gate
  0x49b6a5 `if (!QueryBegin(...,*(a1+16))) return 1` leaves the ack UNTOUCHED. The
  status==2 / return 2 stamp (0x49b726) belongs only to the later target-resolve-fail
  path (deferred ResolveTargetEntityRef leaf). Golden expected status 2 on the
  return-1 owner-gate path; corrected to 0 (untouched).

Fix: `tests/unit/sim_command_apply3_test.cpp` (4 spots above).

## 4. sim_command_apply4_test — SetObjectTransform_CreatesAndCopies (GOLDEN fix)

`VIBE_Command_ExSetObjectTransform` @0x497B8C: 0x497bdf `mov byte ptr [ebp+0],1` writes
ONLY the status byte (+0); slot (+1) and seq (+6) are left untouched. Golden expected
`ack.slot == 3`; corrected to 0 (untouched). Source (command_apply4.cpp:417, sets only
`ack->status = 1`) was already binary-correct.

Fix: `tests/unit/sim_command_apply4_test.cpp`.

---

## Files touched
- `src/sim/charaction.cpp` (shared; type-7 animName literal — required by the
  actionqueue boundary golden which is binary-correct)
- `tests/unit/sim_command_apply2_test.cpp`
- `tests/unit/sim_command_apply3_test.cpp`
- `tests/unit/sim_command_apply4_test.cpp`

Source handlers in command_apply2/3/4.cpp were ALL already binary-correct; the
divergences were inverted/wrong golden expectations (and one truncated catalog literal).
