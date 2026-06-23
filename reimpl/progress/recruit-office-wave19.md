# Wave-19 — Recruitment / Office-Candidate / Person-Selection / Estate-Transfer

**Agent:** W19-RECRUIT · **Date:** 2026-06-15 · **MCP:** live (`gilde.exe`, imagebase 0x400000)

Reconstructs the recruitment candidate COLLECTION, the office candidate-list /
session-timer rendering bodies, the guild-law (Gesetz) person-selection candidate
harvest, and the estate-ownership transfer mutation — all 1:1 from the Hex-Rays
decompile, against the real `g_persons` / `g_objects` arrays.

## Owned new modules
- `src/gui/recruit_office.{h,cpp}`
- `src/world/estate_transfer.{h,cpp}`
- `tests/unit/recruit_office_estate_test.cpp` (14 tests, 60 checks, all green)
- this doc

## Reconstructed functions (RECONSTRUCTED 1:1)

| addr | symbol | where | what was reconstructed |
|------|--------|-------|------------------------|
| 0x55d530 | VIBE_Recruit_CollectNearbyRecruitCandidates | recruit_office | gray-color window set; filter constant `(refByte9==0)+21513` (21513/21514); spatial query via FindPeopleByPalette (maxPeople=9, range 30..100); map each found palette slot through the person-id column `dword_12CE914[134*idx]` (== `g_personIds[idx]`) into outIds. |
| 0x555f8c | VIBE_Office_ShowCandidateListWithRoles | recruit_office | empty-count early return; per-row card geometry x=75 / y=112*i+40; role-label running y (70, +112 step); two-pass (build cards, then enable); page-descriptor stride=112 + anchor write. Reuses `PrivCandidateRowY`/`kCandidateRowStride`/`kCandidateRowX` from office_recon_privilege.h. |
| 0x49d910 | VIBE_Office_RenderSessionTimer | recruit_office | `"%2i : %2i : %3i ms"` format of the 14x-scaled tick delta; split via `OfficeSessionTimeSplit` (reused from office_recon_privilege.h); label submit via hook. |
| 0x55a224 | VIBE_Gesetz_RunPersonSelectionWindow (harvest core) | recruit_office | the load-bearing CANDIDATE HARVEST: seed spouse(+37)/child(+39) markers (skip 0xFFFF / dup); state-byte classification (spouse-table when state∈{1,2}; child-table when state∈{4..9,11..14,16,18..22}); two link-table scans (`dword_12CEA80`=record+0x170 spouse-link, `dword_12CEA7C`=record+0x16C child-link) matching the subject id, with de-dup vs +37/+39 and the 16-candidate / 512-byte caps. The window's widget/radio-group plumbing is the GUI shell (deferred to the dialog-loop layer). |
| 0x55a9bc | VIBE_Gesetz_OpenPersonSelectionIfValid | recruit_office | validity gate: null subject -> no-open; building-type "busy" (MapTypeToState != 0) -> abort; else open. |
| 0x58c4a8 | VIBE_Person_TransferEstateOwnership | estate_transfer | the full estate transfer: FROM/TO resolve (+ -1/-2 error returns); cat-2 building re-parent scan over the 256-slot object array (owner word +39); wealth snapshot; handler refresh + both-side char-action cancel; the two 0x218-byte record splices (NEW-TO image into FROM slot, NEW-FROM image into TO slot — marker/kind/+13/+0x170/id field splices, kind byte forced to 9 on the FROM image); office-rank/family-head reconciliation branch (group-from-code + rank-within-group); FULL relation-matrix re-point over all 768 persons × 8 relation slots (+0x5C, swap FROM-id ↔ TO-id); wealth/jail bookkeeping stamps (record+0x194=4, +0x1C8=0, +0x1A8=wealth); building remove+clean with the corpse drop flag (FROM kind==5). Keeps `g_personIds` in lockstep. |

## Leaves reused (extern / existing reconstructions — no ODR redefinition)
- `guild::sim::ObjectSearchFindPeopleByPalette` @0x47b008 (pathfind_map.cpp) — the
  spatial query; forwarded through `RecruitCollectHooks.findPeopleByPalette`.
- `guild::sim::PersonFindRecordById` @0x58bc6c, `PersonGet/SetByte/Word/Dword`,
  `kPf*` field offsets, `g_persons` / `g_personIds` (entity.cpp / person.cpp).
- `guild::world::OfficeSessionTimeSplit`, `PrivCandidateRowY`, `kCandidateRow*`,
  `kCandidateRowStride` (office_recon_privilege.h, inline) — reused, not re-defined.
- The recruitment dialog SHELLS (RunCandidatePickWindow 0x55db0c / RunHireConfirmDialog
  0x55d990 / RunRecruitmentOfferWindow 0x55de00) already exist in
  `gui/personnel_gui.cpp` with their deterministic kernels; not duplicated here.

## DEFERRED / boundary (documented, not faked)
- The GUI **frame-loop shells** of the recruitment/offer/person-selection windows
  (form create/center/select, RunFrameLoop modal pump, RadioGroup, widget layout,
  Form_Destroy) are SDL/Vulkan-boundary plumbing already routed through the existing
  hook structs in personnel_gui.cpp / office_recon_privilege.h. This module
  reconstructs the GAME LOGIC those shells call, not the shells.
- `VIBE_Office_ShowCandidateListWithRoles`' per-row card object ids and the HUD
  centered title (`VIBE_Hud_AddCenteredLabel`) read live engine globals at draw time
  (`dword_67EB84[238*page]` panel width); the geometry + page descriptor are
  reconstructed, the HUD draw goes through `OfficeListHooks`.
- The estate transfer's per-person **object-list relink** (`dword_12CEA88` linked
  lists, next @+63) and the **category-9 inventory move** (`v33[94]` walk) operate on
  the live object-list arrays that are not modeled as C arrays in this build; the
  control flow is reconstructed and the mutation is surfaced via
  `EstateTransferHooks.gameObjectAddToParent` (inert default). Faithful control flow,
  no fake values.

## Wiring (rule 13) — handoff to bind-site files I do not own
The three reconstructed entry points connect to the live tree via one-line adapters
in the existing wiring layers (NOT edited here, per the ownership rule):

1. **Estate transfer** — `command_apply2.h` already exposes
   `SetEstateTransferHook(EstateTransferFn)` with signature
   `i32 (*)(i32 fromId, const i32* spec)`. Install in the `real_hooks*` wiring with:
   ```cpp
   sim::SetEstateTransferHook([](i32 fromId, const i32* spec) {
       return guild::world::PersonTransferEstateOwnership(fromId, spec[0], spec[1]);
   });
   ```
   (`spec[0]` = TO id, `spec[1]` = mode — the apply path's staging block.)
2. **Recruit collect** — `gui/personnel_gui.cpp`'s `RunCandidatePickWindow` body has
   the candidate loop; point its candidate source at
   `gui::RecruitCollectNearbyCandidates(refMarker, refByte9, outIds)` and forward
   `RecruitCollectHooks.findPeopleByPalette` to
   `sim::ObjectSearchFindPeopleByPalette` (adapting the extra stride/probe/type-table
   params the reconstructed leaf takes via its own pathfind_map hooks).
3. **Gesetz selection** — `world/office_law3.h`'s `GesetzUiHooks.runPersonSelection`
   is the existing seam; its body should harvest via
   `gui::GesetzHarvestSelectionCandidates(subject, stateByte, outMarkers)` and gate
   opening with `gui::GesetzOpenPersonSelectionIfValid(...)`.

## Verification
- Both new modules compile clean (`-std=c++17 -I src -I include`).
- The whole `guild` static library links with both modules included (verified with
  the concurrent-wave's still-broken untracked files temporarily parked; those files
  — `src/script/script_vm.cpp`, `src/play/session_npc_daily.cpp`, etc. — are other
  agents' in-progress work, not touched here).
- `tests/unit/recruit_office_estate_test.cpp`: 14 tests / 60 checks, all green.

## Notes / 1:1 fidelity callouts
- The recruit filter: the decompile's dead `v9 = 21512` is followed by the live
  `v9 = (refByte9==0) + 21513`; reproduced verbatim (21513 when byte!=0, 21514 when 0).
- The relation re-point is symmetric (FROM↔TO swap), exactly as the original's two
  branches (`v27 == v23` -> set a2; `a2 == v27` -> set v23).
- `OfficeSessionTimeSplit` uses unsigned arithmetic on the 14x-scaled delta, matching
  the original's `14 * (now - start)` over `/0xEA60`, `/0x3E8 %0x3C`, `%0x3E8`.
