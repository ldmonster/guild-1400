# Privilege Panels — SET B (law / evidence / espionage + dispatch) — Wave 19

**Agent:** W19-PRIV-B · **Date:** 2026-06-15 · **MCP:** live (`gilde.exe`, imagebase 0x400000)

New module: `src/world/privilege_panels_b.{h,cpp}` + `tests/unit/privilege_panels_b_test.cpp`.
144 golden checks, 0 failures.

## Scope

The SET-B guild-office privilege action panels (the law/evidence/espionage cluster
plus the shared dialog dispatcher), reconstructed 1:1 from the Hex-Rays decompile.
Each `VIBE_Privilege_Panel*` is, in the original, a GUI frame-loop dialog: build a
Form over a `privillegien\…` backdrop, spin `VIBE_GameLogic_RunFrameLoop @0x4c09a0`,
and on the confirm/cancel button (`dword_75BF38` == 1210 / 1155) enqueue a lockstep
network command (`VIBE_Command_*`). Those subsystems (Form/HUD/Vulkan, the command
queue, the live person arrays `word_12CE910` / `byte_12CEA76`) are coupled leaves —
surfaced via the SAME inert-default hook boundary as the sibling privilege modules.
What is reconstructed are the **load-bearing, testable decision predicates, the law
table + clamp math, the embezzle/miracle RNG math, and every early-out return code.**

## Functions reconstructed (addr → what is 1:1 vs deferred)

| addr | name | reconstructed | deferred (coupled leaf) |
|------|------|---------------|--------------------------|
| 0x571218 | `VIBE_Privilege_ShowDialog` | backdrop-bitmap selection by size (0/1/2 → prv_small/prv_big/prv_very_big) | Form build + frame loop |
| 0x561bb4 | `VIBE_Privilege_PanelEnactLaw` | kind gate, class→mask (0x80000/0x100000), record lookup + amount-bound precheck (32/96), penalty-mode (0..3) switch, text base 5·hi+4145, result 128/0 | Form widgets, op70/op90/args25 enqueue |
| 0x561fd0 | `VIBE_Privilege_RemoveFromOffice` | subject pick, office-def gate, non-office category-match (16/96), session result (32/96/-112) | session window, command pair |
| 0x5628c8 | `VIBE_Privilege_PanelCounterEspionage` | 3-arm kind dispatch, rank gate 32, base verdict 2 \| 0x10 (agent reset) | handler scan, spy-state reset cmds |
| 0x562334 | `VIBE_Privilege_PanelEmbezzlement` | amount = trunc((RandomModulo(0xB0)+25)·0.005f·wage), rank gate 32 | wages calc call, command enqueue |
| 0x562cdc | `VIBE_Privilege_PanelSwapSeats` | seat-pair table (15→13/14, 21→19/20, 27→25/26), 768-person holder scan, non-office result 16/32 | promote-await leaf, Form |
| 0x5651bc | `VIBE_Privilege_PanelMiracle` | kind dispatch (1/0/-1), rank gate 16/32, RandomModulo(6) draw | BuildOfficeMemberTable, Form |
| 0x565f9c | `VIBE_Privilege_PanelEvidenceReview` | office-path gate, concrete precheck (judge=13 reject, no-target/no-match → 96), BuildEvidenceEntry mode=0 | HUD person-card list |
| 0x5667a0 | `VIBE_Privilege_PanelEvidenceReviewAlt` | identical to Review except BuildEvidenceEntry mode=1 | HUD person-card list |
| 0x565b88 | `VIBE_Privilege_PanelEvidenceDetails` | **already in `world/office_recon_privilege.h`** (0xFFFF→0, matchCount<1→96, 45-byte stride field37==1 aggregation, self-target suppress, confirm-state 3) — reused, not redefined | — |

### Callees reconstructed to genuine leaves

| addr | name | reconstructed |
|------|------|---------------|
| 0x4c244c | `VIBE_Gesetz_GetRecord` | **REUSED from `world/law.cpp`** (g_lawTable / GesetzGetRecord(u8,LawRecord*)) — not redefined |
| 0x4c247c | `VIBE_Gesetz_RequestApply` | `GesetzClampAmount` (clamp to [v5[1],v5[2]] via +4/+8 accessors on the canonical LawRecord) + `GesetzApplyWouldEnqueue` (-1 gates) |
| 0x56589c | `VIBE_Privilege_BuildEvidenceEntry` | return codes **already in `office_recon_privilege.h`** (`PrivBuildEvidenceResult`: 16 / 64) — reused |

## Golden data (get_bytes, verbatim)

- **`unk_631E98` law/penalty table** (0x631E98, 26×36 B) is ALREADY pinned in
  `world/law.cpp` (`g_lawTable` / `LawTableResetDefaults`) — this module reuses it and
  only adds the two clamp accessors `GesetzRecordMinAmount`/`GesetzRecordMaxAmount`
  (record ints @+4 = `v5[1]`, @+8 = `v5[2]`). Verified against the canonical table:
  min is 0 for every record except 12 (min 10, max 25); max is the per-law ceiling.
  Tested records 0–5, 12, 25.
- **`flt_624C2C`** (0x624C2C) = `0x3ba3d70a` = the float nearest 0.005 but *below*
  it. The embezzle math uses it faithfully, so `(25)·0.005f·1000` truncates to **124**
  (not 125) — `ConvertX@0x5c6b08` truncates toward zero. The unit test pins the exact
  truncated outputs (124 / 999 / 12 / 312 / 0) as proof of the 1:1 float behavior.

## Reuse / ODR (rule: grep before defining)

- No sibling `privilege_common.h` / `privilege_panels_a.*` exists yet (W19-PRIV-A not
  landed). When it lands, the shared confirm/cancel/state predicates already live in
  `world/office_recon_privilege.h` (`kPrivBtnConfirm/Cancel`, `PrivPanelConfirmState`,
  `PrivEvidence*`, `PrivBuildEvidenceResult`) — extern those, do not redefine. My
  module deliberately namespaces its own button constants (`kPrivBBtn*`) to avoid any
  include-order ODR coupling, and does NOT re-declare the EvidenceDetails /
  BuildEvidenceEntry logic (reused from `office_recon_privilege.h`).
- The leaf-id enums (`PrivilegeLeaf` in `sim/interaction_handlers.h`, `PrivilegeLeaf2`
  in `sim/contextaction2.h`) already carry all SET-B addresses as opaque ids — left
  untouched.

## Wiring (rule 13) — the handoff

The live call tree reaches these panels via the established privilege-leaf boundary:
`sim/interaction_handlers.cpp` and `sim/contextaction2.cpp` call
`InvokePrivilegeLeaf(leafId, actor, ev)` (e.g. `kPrivShowDialog` = 0x571218,
`kPrivEnactLaw` = 0x561bb4, `kPrivEvidenceReview` = 0x565f9c, …), which routes through
`g_privilegeHook` (installed via `SetPrivilegeLeafHook`, default no-op recorded in
`g_leafTrace`). This module supplies the **decision logic** a concrete hook
implementation calls to compute the panel's verdict/return code without re-deriving it.

**Handoff (one line):** a host that installs a real privilege hook dispatches by
`leafId` and, for the SET-B ids, computes the verdict using `guild::world::` predicates
here (`EnactLaw*`, `Embezzle*`, `Miracle*`, `SwapSeats*`, `RemoveOffice*`,
`CounterEsp*`, `EvidenceReview*`, `Gesetz*`). I did not edit `interaction_handlers.cpp`
/ `contextaction2.cpp` (not owned, and the hook is the intended boundary).

## Coupled leaves (deferred — NOT 1:1, by the GUI/command/render boundary)

Form/HUD (`VIBE_GameTick_Finalize`, `VIBE_Form_*`, `VIBE_Hud_BuildPersonCard`,
`VIBE_Object_*`, `VIBE_Text_RenderRichString/FormattedMessage`, `VIBE_DragCursor_*`,
`VIBE_Window_CreateScrollButtons`), the lockstep command queue (`VIBE_Command_*`,
`VIBE_Command_GetPacketStatusById`), the live person/handler arrays + iterators
(`VIBE_He_*`, `VIBE_Person_*`, `VIBE_Office_*`, `VIBE_Amt_*`, `VIBE_ObjectSearch_*`),
and `VIBE_Privilege_BuildOfficeMemberTable`. These are the same boundary the existing
privilege modules already defer; reconstructing them is out of scope for this wave.

## Build / tests

- `src/world/privilege_panels_b.cpp` compiles clean standalone (`-Iinclude -Isrc`).
- Unit test: **144 checks, 0 failures** (built against the dependency-free framework).
- NOTE: the full `cmake --build build` currently fails on a PRE-EXISTING ODR clash in
  `src/sim/building_type.cpp` vs `building_lifecycle.cpp` (`BuildingType_GroupFromCode`
  / `Building_ClassifyTypeFlag`) — confirmed present at HEAD/stash, owned by a sibling
  (Building cluster) agent, unrelated to this module. My files add no symbols that clash.
