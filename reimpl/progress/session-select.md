# City-session selection layer (set-selection commit / deselect / status latch)

Module: `src/play/session_select.{h,cpp}` (namespace `guild::play`).
Tests: `tests/unit/session_select_test.cpp` — 18 tests / 106 checks, all passing.

## The recovered original selection pipeline

All driven per frame from `VIBE_GameLogic_RunFrameLoop @0x4c09a0`
(call sites 0x4c0e19 / 0x4c0e82 / 0x4c0e91 / 0x4c0e96):

| Addr | Symbol | Role |
|---|---|---|
| 0x4b8ba8 | `VIBE_Object_UpdateGateContact` | HOVER resolution: writes the hover latch `dword_631724` (object record), `dword_63172C` (contact game-object sub-record), `dword_631730` (contact "selectable" flag), `dword_631734` (person record), `dword_63173C` (door record). NOT reconstructed (needs the live scene-graph walk) — the latch is the boundary. |
| 0x4b950c | `VIBE_Object_ResolveQuickJumpContact` | **THE SET-SELECTION** — the missing counterpart of `Selection_Reset`. Commits the hover latch into the anchors on click; empty latch + no build-mode (`byte_6317B4`) → `VIBE_Selection_Reset` (the real "click empty ground deselects"). **Reconstructed 1:1 here** (`Selection_CommitContact`). |
| 0x4b9444 | `VIBE_Selection_Reset` | The deselect (already reconstructed in `input_recon_select`; REUSED, not redefined). |
| 0x4b94d8 | `VIBE_Selection_ClearAll` | Full worker deselect: `dword_11BC270=0`, `dword_631740=0`, the `byte_12CE880` 536-stride sweep (768 records), `dword_6317B0=0`. **Reconstructed 1:1 here** (`Selection_ClearAll`). |
| 0x4bc280 | `VIBE_Hud_HandleMouseClick` (tail 0x4bc45a..0x4bc51b) | Status-text latch: owner = `dword_631744` else `dword_631748`; on change vs `dword_631E54`/`dword_631E58`/`dword_631754` → `word_631758 = ComputeSelectionFlags(...)`, `VIBE_StatusText_ResetEntries @0x4bcc4c` (= `gui::ResetStatusText`), re-latch. **Reconstructed 1:1 here** (`Selection_UpdateStatusTextLatch`). |

### What the commit (0x4b950c) does, store by store

- Clears `11BC2F4/11BC2F0/631720/11BC2F8/631738` unconditionally (function entry).
- Drops a stale worker latch (`dword_11BC270` whose record byte +392 == 0).
- Pending QuickJump (`dword_11BC27C/280/284`, name `byte_11BC290`): when the
  owner/room match, resolves the handle via `VIBE_Object_FindByHandle @0x5b7be4`
  into `dword_631724` and FORCES the commit past the cursor gate; failure formats
  `"sv_HandleObjects(): Could not find QuickJump.ContactName: %s"` (stack buffer).
- Cursor gate: `dword_67221C && !dword_62D4E8 && cursor(>>16) strictly inside
  dword_63CC4C/50/54/58 && dword_75BF08==-1 && dword_62D31C==-1`, OR forced.
- Commit: copy latch → `631720/11BC2F0/11BC2F4/11BC2F8/631738`; door-gate
  `v14 = !StrCmp("tp_TUER", rec) || typeByte(contact)==6 || door`; highlight
  pulse `(rec[529]&1) && !v14` → `VIBE_Character_ApplyBoneTransform @0x4263fc`,
  `dword_631728=rec`, handler `(*(rec+468)+264)(0,2)`, `dword_63161C=dword_631610`;
  `dword_631E50 = rec`.
- Worker branch (`dword_631734` with active byte +8): hi-byte of
  `VIBE_Building_ComputeSelectionFlags @0x588dec` — bit 3 (flags & 0x800) marks
  `byte_12CEA98[536*id]=1`, `dword_62D098 = dword_12CEA94[134*id]`,
  `dword_6317B0=1`, `dword_11BC270=person`, voice
  `VIBE_Voice_PlayWorkerClickComment @0x5823a4`; the second `dword_62D098` store
  is UNCONDITIONAL (exactly as compiled).
- Anchor stores: `dword_11BC278 = dword_631730`, `dword_11BC274 = dword_63172C`,
  `dword_631740 = dword_631724` (guarded variants 0x4b986d/0x4b9895/0x4b98be);
  a committed contact of object-type 29 voids the selection again (street/decor
  unselectable — matches the same type-29 reset in `Hud_HandleMouseClick`).

## 64-bit fidelity note

`dword_631740` / `dword_11BC274` hold RECORD POINTERS in the 32-bit original.
`input_recon_select` models them as `i32` (its `Selection_Reset` only zeroes
them); this module carries the pointer-width view (`SelectionAnchorRecords`) of
the SAME dwords as the live store (precedent: `g_selectionOwnerB`). The default
deselect zeroes both views in one step, so they never disagree. `dword_631744`/
`dword_63174C` likewise get a pointer-width view (`SelectionOwnerRecords`);
`dword_631748` REUSES `g_selectionOwnerB`.

## Coupled leaves → hooks (rule 8: named, not faked)

`SelectCommitHooks`: `VIBE_Object_FindByHandle @0x5b7be4`,
`VIBE_Building_ComputeSelectionFlags @0x588dec` (NOT reconstructed anywhere yet
— also hooked by `sim/building6.h`), `VIBE_Character_ApplyBoneTransform
@0x4263fc`, the record's `+468/+264` handler block (pointer inside the record —
hook boundary), `VIBE_Voice_PlayWorkerClickComment @0x5823a4`, the object-type
table `dword_13CE27C` (65-stride), and the worker-table mark ops
(`byte_12CE880` sweep / `byte_12CEA98[536*id]` mark / `dword_12CEA94[134*id]`
mesh — the sim person table, same boundary reasoning as `input_recon_select`'s
`hotspotListRecord`). `StatusLatchHooks`: selection flags + the status reset
(default = the real `gui::ResetStatusText`, `VIBE_StatusText_ResetEntries
@0x4bcc4c`).

## Selection feedback recovered (the original's "selection visual")

- **Worker tint**: `VIBE_Hud_UpdateSelectionAndTargets @0x4ba614` walks the
  `byte_12CEA98` marks per frame and re-tints via `VIBE_Mesh_ResetVertexColors
  @0x428898` (reconstructed in `src/render/mesh_transform.*`). The driver
  0x4ba614 itself is a NAMED GAP (needs the command queue + drag-select unit
  list).
- **Name captions**: the status-text table `dword_11B5220` drawn by
  `VIBE_Hud_DrawObjectNameLabels @0x4bbaec` (reconstructed in
  `src/gui/hud_label_draw.*`): anchored at the projected screen-bounds centre,
  `x = left + (right-left)/2 - 80`, width 160, char height 40 (constants
  exported as `kSelectCaption*`).
- **Click chatter**: `VIBE_Hud_DrawSelectedUnitInfo @0x4bae88` (random text keys
  3756..3803 via `VIBE_Text_RenderFormattedMessage @0x59f99c`) — NAMED GAP
  (needs the economy demand-snapshot cluster).

## Name resolution recovered (what the original shows for a selection)

- Building/object header (`VIBE_InfoPanel_BuildStandard @0x4b6db8`):
  `"$Z%s$A%s$A%s"` = item label (`VIBE_Text_FormatItemLabelWithIcon @0x59ccf4`,
  reconstructed in `play/text_recon3_itemlabel`), the object-KIND name =
  **text-array id `1078 + 14*typeByte`** (constants mirrored from
  `gui/infopanel_build.h`), and the record's custom-name string at `+5`.
- Person header (`VIBE_InfoPanel_BuildPerson @0x4b7104`):
  `FormatItemLabelWithIcon(*(u16*)record, kind=4)` — job-title string-table id
  `+ "$A" +` the person record's name field at `+48`.
- `SessionSelect::current()` resolves through these real paths: persons via
  `FormatItemLabelWithIcon(kind=4)` over the committed `dword_11BC270` record;
  objects via the `1078 + 14*code` entry of the supplied `gui::text::TextDb`
  (custom name preferred, exactly the panel's most-specific line).

## SessionSelect (the wiring layer)

`play::SessionSelect` — selection for the city session:
- `OnPick(pick, roster, n, cx, cy, radius)` — left-click: materialises shadow
  records carrying exactly the byte fields 0x4b950c dereferences (+0 handle,
  +8 person-active, +48 person name, +529 highlight bit, id words), fills the
  hover latch the way 0x4b8ba8 would, then runs the REAL
  `Selection_CommitContact` + status latch. A miss leaves the latch empty →
  the commit's real empty-click branch → `VIBE_Selection_Reset`.
- `Clear()` — right-click/ESC: the REAL `play::Selection_Reset @0x4b9444` +
  `Selection_ClearAll @0x4b94d8` (the pair the engine's deselect paths use),
  then the status latch.
- `current()` → `Info{has,id,kind,name[64]}` (real name resolution above).
- `highlight()` → `Highlight{id, screenX, screenY, radius}` per-frame marker
  descriptor for the renderer/HUD (caption/tint addresses above).
- `workerSelected(id)` — the `byte_12CEA98[536*id]` mark view.

**BOUNDARY (documented per task):** the hit-test itself stays the session's
`RealCityRenderer::Pick → play::ScenePickResult`; the original's hover walk
`VIBE_Object_UpdateGateContact @0x4b8ba8` and the actor-volume test
`VIBE_SelectEntity_ComputeResult @0x4147cc` (already reconstructed in
`input_recon_select`) need the live scene graph / 48-actor array. Everything
downstream of the hit (latch → commit → anchors → deselect → status latch) is
the 1:1 reconstruction.

## Wiring

- Reuses (extern, no redefinition): `play::Selection_Reset` /
  `g_selectionAnchors` / `g_selectionOwnerB` (input_recon_select),
  `gui::ResetStatusText` (gui/hud), `util::ReconStrCmp` (@0x5d3f10),
  `play::FormatItemLabelWithIcon` (@0x59ccf4), `gui::text::TextDb`.
- `sdl_session.cpp` (owned by another agent / DO-NOT-EDIT for this slice) can
  instantiate `SessionSelect` next to its pick: call `OnPick` on left-click
  edge before/after `IssuePickedOrder`, `Clear()` on right-click/ESC, and feed
  `highlight()` to the renderer. Wiring into that file is PENDING (file owned
  elsewhere).
- xrefs of 0x4b950c / 0x4b94d8: `VIBE_GameLogic_RunFrameLoop @0x4c09a0`
  (commit, per frame at click flags), and the location/building dialogs for
  ClearAll — those callers reach this module when they stand up.

## Tests (18 / 106 checks)

`SessionSelectCommit`: ObjectPickSetsAnchors, DoorSuppressesHighlight (3 door
variants), EmptyClickRoutesSelectionReset (drives the REAL Reset + its person
query), EmptyClickSkippedWhenBuildMode, GateRejects, WorkerSelectMarks,
WorkerFlagsLowNoMark, Type29ContactClears, QuickJumpForcesCommit (+ error
string), StaleWorkerDropped.
`SessionSelectClearAll`: ClearsWorkerSelection.
`SessionSelectStatus`: LatchFiresOncePerChange.
`SessionSelectFlow`: PickInfoClear (TextDb kind-name 1078+14*code),
EmptyClickDeselects, PersonSelectAndName (FormatItemLabelWithIcon kind=4),
ForeignWorkerNotMarked, CustomNamePreferred, Type29EntryRejected.

Regression: `input_recon_select_test` still passes (40 checks).

## Named gaps (rule 8 — omitted, not faked)

| Addr | Symbol | Why |
|---|---|---|
| 0x4b8ba8 | `VIBE_Object_UpdateGateContact` | hover walk over the live scene graph (gate sub-objects, contact resolution) — boundary, latch filled by the session pick |
| 0x4ba614 | `VIBE_Hud_UpdateSelectionAndTargets` | per-frame selection visual driver (mesh tint + drag-select unit list + command queue) |
| 0x4bae88 | `VIBE_Hud_DrawSelectedUnitInfo` | click chatter labels; needs the economy demand snapshot cluster |
| 0x588dec | `VIBE_Building_ComputeSelectionFlags` | the big eligibility bitfield; hooked (also hooked by sim/building6) |
| — | worker mesh records (`dword_12CEA94`) | render-cluster table; hook reports a stable per-id slot so `dword_62D098` keeps the identity |
