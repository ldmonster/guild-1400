# VIBE_Character cluster — script-command dispatchers (character_recon2_cmds)

Cluster: `/tmp/wl6/VIBE_Character.txt` (remaining character record / control-flow logic).
Files: `src/sim/character_recon2_cmds.{h,cpp}`, `tests/unit/character_recon2_cmds_test.cpp`.
Namespace: `guild::sim::character_recon2`. Self-contained (hook boundary; no edits to
existing files / CMakeLists / shared headers).

## Translated (1:1, this slice)

| addr | function | notes |
|------|----------|-------|
| 0x43d260 | CmdTakeObject | resume-guard, validity, CreateTakeObjectAction, stmt-mode re-arm |
| 0x43d30c | CmdTakeObjectLeft | forwards to CreateTakeObjectActionAlt (already-present builder) via hook |
| 0x43d3b8 | CmdTakeObjectScript | InsertActionArgs(cb=PlayAnimationScript, flags\|0x31<<32, *a4); copies name→+240, obj-name→+304, scriptName→+144, ownerId→+380 |
| 0x43d548 | CmdDropObject | validity+FindByHandle gate BEFORE resume-guard; CreateDropObjectAction |
| 0x43d600 | CmdDropObjectLeft | same gate; forwards to CreateDropObjectActionAlt via hook |
| 0x43d0f8 | CmdPlayAnimationScript | InsertActionArgs(cb=LoadRunAndStoreResult, flags\|0x2E<<32, *objPtr); strlen<0x5F guard; StrNCopyPad *edx→+240(63), *a4→+144(95); ownerId→+380; *a5→+384 |
| 0x43d844 | CmdSetCharacterCamera | StrCmp CLOSEUP/LEFT_SHOULDER/RIGHT_SHOULDER/EGO → ApplyAttachOffset mode 0/1/2/3 |
| 0x43dbc8 | CmdLookAtCharacter | AngleToTargetSigned, SnapVectorToAxis, +132/136/140 add, angle*180/π, InsertActionVararg(packed{hi=7,lo=char}) |
| 0x43dcb0 | CmdLookAtObject | PointThroughBoneChain, AngleToTargetSigned, angle*180/π, InsertActionVararg |
| 0x43dd38 | CmdSetCharacterToDummy | PointThroughBoneChain, ApplyVisibilityState, RotateVectorByHierarchy, VectorAngleBetween, SetWorldTranslation |
| 0x43debc | CmdAttachObjectToBone | StrCmp d3_LeftHand/RightHand/Head → AttachItemToBone bone 1/2/3; unknown-bone path reads an uninitialized stack byte (modeled as sentinel kUninitBone, see below) |
| 0x43df30 | CmdPlayCharacterAni | LightSetGrayColor, packed&=~BYTE2.bit0, LOBYTE=*a2, ChangeTransparency on scene rec + transport (+292) |
| 0x43d9a4 | PreloadAnimation | validity → PreloadAniSet(char, 4, *a, *b, *c, *d) |

Float constants recovered with get_bytes: flt_616EA4/616EDC = 180.0 (0x43340000);
flt_616EA8/616EE0 = 1/π = 0.31830987 (0x3ea2f983). Both LookAt commands convert
radians→degrees via `angle * 180.0 * (1/π)`.

### Edge case (rule 1, not faked)
`CmdAttachObjectToBone` (disasm 0x43debc): the bone id is a single stack byte (`var_C`).
LeftHand→1, RightHand→2, Head→3. For an **unknown bone name** the original `jnz`s to the
shared `AttachItemToBone` tail with `var_C` UNINITIALIZED (read via `[esp-3] sar 0x18`).
Reproduced faithfully as the inspectable sentinel `kUninitBone` rather than papered over.

## Already present (NOT redefined — ODR)
- `VIBE_Character_CreateTakeObjectActionAlt` (0x405e68) and
  `VIBE_Character_CreateDropObjectActionAlt` (0x4060dc) are already reconstructed in
  `src/sim/character_render4` as the `hand==1` (alt) path of `CreateTakeObjectAction` /
  `CreateDropObjectAction`. The Left dispatchers here reach them through the
  `createTakeObjectAlt` / `createDropObjectAlt` hooks (no duplicate definition).

## Deferred (rule 8 — scene-graph / render / animation coupling, not pure character math)
Reported, not half-translated:
- 0x4015cc FadeOutSlots — light/transparency/object scene-graph slot fade.
- 0x40238c SetAllFreezeState — universe slot switching + low-poly mesh swap over the
  512-slot actor table.
- 0x402e40 AttachTransport / 0x402f70 UpdateTransportAttach — object matrix transforms,
  heightmap/floor tile picking, bone-chain physics.
- 0x403708 ReleaseMorphAni — anim mesh-slot release.
- 0x4b5fa4 RefreshAllFlags — person/object query iterators + scene-graph walk + texture set.
- 0x505074 EnsureObjectAvatar / 0x505134 EnsureBuildingAvatar — global avatar byte-table
  allocation + scene loading.
- 0x50650c PreloadSceneAnimations — 1729-byte anim-name table scan + scene preload.
- 0x531e60 SyncTurnState — command-delta packet building + building production gauge.
- 0x57c744 SpawnOfficeStaffActor / 0x57c8f0 SpawnAtBuildingEntrance — actor spawn over
  scene/object/person records, model resolution, head variant.

## Skipped
- 0x43cb20 DestroyThunk (size 8, thunk) — below size threshold.
- 0x43d9f0 CmdPreloadSitMeshStub (size 3, stub).

## Wiring
- Pending: the Cmd* handlers are registered into the script-command dispatch table, which
  lives in existing files (script VM); wiring requires editing those (out of scope under the
  new-files-only constraint). The hook boundary lets a future RegisterScriptCommands patch
  bind these directly.
- Done within this TU: CmdTakeObjectLeft / CmdDropObjectLeft are wired to the
  already-reconstructed alt builders (character_render4) via hooks; the take/drop/script
  dispatchers route through the action-queue allocator hooks that mirror the real
  guild::sim::QueueInsertEntry / InsertActionArgs siblings.

## Tests
`tests/unit/character_recon2_cmds_test.cpp` — 26 TEST cases / 111 golden checks. Build-clean
under `-Wall -Wextra` and ASan+UBSan, 0 failures. Covers: success/invalid/yield paths, the
arg-pack constants (0x31/0x2E << 32), field-offset writes (+144/+240/+304/+380/+384),
the resume re-arm protocol, FindByHandle gating, camera modes, bone ids, angle scaling
(180/π → int truncation), and the alt-builder forwarding.
