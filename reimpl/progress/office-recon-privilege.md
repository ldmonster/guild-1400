# Module: office_recon_privilege (political-office / privilege panels)

Cluster: `VIBE_Office.txt` + `VIBE_Privilege.txt` (Ämter / Privilegien systems).

Files added:
- `src/world/office_recon_privilege.h`
- `src/world/office_recon_privilege.cpp`
- `tests/unit/office_recon_privilege_test.cpp`  (14 TEST cases, 79 checks, all pass)

## Key finding
The bulk of this cluster's table/rule logic was **already reconstructed** in the
existing `office.cpp` / `office_assign.cpp` / `law_types.h` / `privilege.h`:
- office-def table `dword_62EC8E` (446 bytes) — in `office.cpp`
- `VIBE_Office_AddTableEntry` 0x47e750, `AddEntryDefault` 0x47e6c4 — in `office_assign.cpp`
- `VIBE_Office_IsNextRankInCategory` 0x47f6a4 — in `office.cpp`
- `SendSimpleCmd`/`SendBuildCmd` return codes — in `privilege.h`

To avoid ODR clashes those are **reused**, not redefined. This module adds the
remaining cluster members, which are GUI frame-loop / session functions; their
*pure decision predicates* are extracted 1:1, the coupled GUI / command-queue /
entity-array leaves are inert-default hooks (project rule: coupled leaves -> hooks).

## Functions handled (addr -> symbol -> disposition)
| addr | symbol | disposition |
|------|--------|-------------|
| 0x561400 | VIBE_Privilege_PanelLawScroll | gate `+457&4`, text-id `(+9!=0)+6504`, confirm-state 3 / cancel 2 reconstructed; Form loop deferred |
| 0x561878 | VIBE_Privilege_PanelInstillFear | confirm-state 4 / cancel 2; skill-req gate is `VIBE_Dialog_CheckSkillRequirement` (engine) — dispatch only |
| 0x5625b4 | VIBE_Privilege_PanelCharm | same-office-category refusal gate + refusal text 6578/6579, confirm-state 4 reconstructed; Form loop deferred |
| 0x560c1c | VIBE_Privilege_BlackmailConfirm | subject gate (`+2==6||7`), success roll `rand(8)<=matchCount`, `+456&0x100` follow-up, result 1/0, state 1 reconstructed; Form loop + command emit deferred |
| 0x565b88 | VIBE_Privilege_PanelEvidenceDetails | target gate (0xFFFF), matchCount<1 -> 96, any-actionable aggregation, self-target suppression, confirm-state 3 reconstructed; HUD list deferred |
| 0x5663b8 | VIBE_Privilege_PanelEvidenceDetailsAlt | same decision shape as 0x565b88 (covered) |
| 0x56589c | VIBE_Privilege_BuildEvidenceEntry | return codes 16 / 64 (judge-found / witness-found) reconstructed; entity scan + command emit deferred |
| 0x49d910 | VIBE_Office_RenderSessionTimer | timer split min:sec:ms from `14*(now-start)` reconstructed; sprintf + render deferred |
| 0x49d9e0 | VIBE_Office_DestroySessionActors | full 16-slot teardown loop reconstructed 1:1 (Character_Destroy = hook) |
| 0x555f8c | VIBE_Office_ShowCandidateListWithRoles | paging math (stride 112, x 75, y0 40), count==0 early-out reconstructed; HUD/person-card deferred |
| 0x5564ac/0x5566dc/0x556858/0x556a2c | ShowCandidateList* siblings | same paging shape (covered) |
| 0x57c1e8 | VIBE_Office_ResolveStaffModel | DEFERRED — pure model-table lookup but driven entirely off live person array `word_12CE910`/`dword_12CE919` (no standalone inputs); no faithful pure slice |
| 0x49da18 | VIBE_Office_SpawnSessionActor | DEFERRED — object/transform/character-create engine glue |
| 0x4a03f4 | VIBE_Office_PrepareSuccessorChoiceA | DEFERRED — Person record + dialog-builder + sprintf glue |
| 0x4a04f4 | VIBE_Office_PrepareSuccessorChoiceB | DEFERRED — same shape, AI-player builder glue |
| 0x51fc50 | VIBE_Office_ShowCandidacyDialog | DEFERRED — large Form/HUD candidacy dialog (1218 B) |
| 0x47e6c4 | VIBE_Office_AddEntryDefault | ALREADY DONE in office_assign.cpp (reused) |

## Deferred reasons (rule 8: no fakes)
The deferred functions are not faithfully reducible to a pure slice with
self-contained inputs: they are dominated by the live `word_12CE910` person
array, the Form/HUD dialog runtime, the lockstep `VIBE_Command_*` queue, and
3D object/transform/character creation — all coupled leaves that, per project
rules 3-5/operational notes, belong behind shim interfaces and are wired via
hooks rather than substituted with analogues.

## Wiring notes (xrefs_to)
Hooks (`OfficeDestroyActorHook`, etc.) are inert by default. The reconstructed
predicates are consumed by their parent panels once those Form dialogs are
brought up on the SDL/Vulkan shim; the parent dialogs (PanelLawScroll, etc.)
are the natural call sites. No existing file was modified (hard constraint),
so the inline predicates are header-only and ready to be `#include`d by the
panel reconstructions when they land.
