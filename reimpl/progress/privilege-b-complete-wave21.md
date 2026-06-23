# Privilege Panels SET-B — VERIFIED, COMPLETED & WIRED — Wave 21

**Agent:** W21-PRIVB · **Date:** 2026-06-16 · **MCP:** live (`gilde.exe`, imagebase 0x400000)

Closes the wave-20 coverage-audit flag: *"a privilege_panels_b.cpp that didn't fully
land — 8 fns ~7.4kB — the biggest remaining logic cluster."* SET-B is now genuinely
in the live call tree.

## What wave-19 left vs. what wave-21 added

Wave-19 (W19-PRIV-B) reconstructed the **load-bearing decision predicates** of each
SET-B panel (verdict math, law clamp, embezzle/miracle RNG, every early-out code) but
shipped them as loose helper functions with **no panel-shaped entry points and no
dispatcher**, so the coverage audit still counted the 10 addresses MISSING (no
panel body was reachable from the live tree).

Wave-21:
1. **VERIFIED** every predicate against a fresh decompile of all 10 addresses — they
   are 1:1 (see "Decompile re-verification" below). Fixed `EmbezzleAmount` to route the
   truncation through `guild::util::ConvertX` (the binary's `VIBE_Coord_ConvertX@0x5c6b08`)
   instead of a bare C++ cast, matching the leaf exactly (golden output still 124/999).
2. **COMPLETED** the cluster by adding full **panel-shaped entry points** that reproduce
   each function's FULL control flow (subject-kind gate → early-outs → office GUI
   frame-loop arm vs. non-office direct arm → confirm/cancel verdict → `dword_631614`
   done-state), reusing the verified predicates internally and calling the coupled
   Form/command/person-array leaves through a `PrivilegePanelBHooks` vtable (the SAME
   deferral boundary as SET-A's `PrivilegePanelHooks`).
3. **WIRED** SET-B into the live dispatcher (`sim::InvokePrivilegeLeaf` →
   `g_privilegeHook`) via a new `InstallPrivilegePanelsB()`, mirroring
   `InstallPrivilegePanelsA()` and **chaining** onto the prior hook so both batches
   share the single hook slot. Added to `src/app/wiring.cpp` immediately after A.

## Files owned / touched

| file | change |
|------|--------|
| `src/world/privilege_panels_b.h` | + `PrivPerson`/`PrivEvent` reuse from panels_a; + `PrivilegePanelBHooks`, `PrivBTrace`, `PrivBCommand`; + 8 panel decls + `PrivilegeShowDialog` + `PrivilegeDispatchPanelB` |
| `src/world/privilege_panels_b.cpp` | + 8 panel bodies + ShowDialog + dispatcher (full control flow); `EmbezzleAmount` now uses `ConvertX` |
| `src/world/wire_privilege_panels_b.{h,cpp}` | **NEW** — `InstallPrivilegePanelsB(provider, chainTo)`, `IsPrivilegeSetBLeaf`, chaining adapter |
| `src/sim/interaction_handlers.{h,cpp}` | + `GetPrivilegeLeafHook()` (one-line getter enabling B→A chaining; additive, no behavior change) |
| `src/app/wiring.cpp` | + include + `world::InstallPrivilegePanelsB();` after A (additive) |
| `tests/unit/privilege_panels_b_test.cpp` | + panel/dispatcher golden tests (now 158 checks) |
| `tests/integration/wire_privilege_panels_b_itest.cpp` | **NEW** — seam reachability + A/B chaining (12 checks) |

> Note on `interaction_handlers.{h,cpp}`: the brief named these as the dispatcher
> path. The single `g_privilegeHook` slot is shared by A and B; to chain B onto A
> without clobbering, a getter for the currently-installed hook is required. The added
> `GetPrivilegeLeafHook()` is a pure read-only accessor (no behavior change to existing
> callers). If the seam owner prefers to keep it untouched, the alternative one-line
> handoff is: `InstallPrivilegePanelsB(provider, theSetAHook)` (the `chainTo` param is
> already supported), which needs no seam edit.

## Decompile re-verification (all 1:1, per-panel)

| addr | name | verdict | COMPLETE+WIRED? |
|------|------|---------|-----------------|
| 0x561bb4 | EnactLaw | subj-kind(3/4→96) · class mask 1→0x80000/2→0x100000/else 96 · non-office rank<2→32, no-rec→96, amount bounds→96, RequestApply+op90+args25; office: GetRecord→0, penalty switch 0..3, frame loop, confirm v52=128/cancel 0 | **COMPLETE + WIRED** |
| 0x561fd0 | RemoveFromOffice | subj pick · `Office_GetDefinition`→96; non-office: FindRecordById(+532)→96, holder-entry+cat-match(>>24==>>24)→16/96; office: session→32, holder→96, byte+16!=1→32, packet==2→32, clean→-112 | **COMPLETE + WIRED** |
| 0x5628c8 | CounterEspionage | office arm → always 2 (+handler scan, op90 -4); non-office: rank<4→32, else base 2 \| 0x10 if any agent reset | **COMPLETE + WIRED** |
| 0x562334 | Embezzlement | subj pick→96 · amount=trunc((Rand(0xB0)+25)·0.005f·wage) via ConvertX · office GUI confirm(skill 4, Request16+op90 -4+args25); non-office rank<4→32 | **COMPLETE + WIRED** |
| 0x562cdc | SwapSeats | subj.office byte: 15→13/14, 21→19/20, 27→25/26, else 32 · 768-scan remaining=2 · non-office rank>=6 && both && promote→16 else 32; office both-missing→0 / GUI→1 | **COMPLETE + WIRED** |
| 0x5651bc | Miracle | kind6→GUI(skill4, op90 -4, Rand(6), BuildOfficeMemberTable)→1; kind7→0; non-office rank>=4→16(+draw) else 32 | **COMPLETE + WIRED** |
| 0x565f9c | EvidenceReview | office HUD-list arm (verdict latched by EvidenceDetails clicks, init 32); non-office: actor.office==13→96, no-target→96, target.office==13→96, no-match→96, else BuildEvidenceEntry(mode 0) | **COMPLETE + WIRED** |
| 0x5667a0 | EvidenceReviewAlt | identical to Review, BuildEvidenceEntry **mode 1** | **COMPLETE + WIRED** |
| 0x571218 | ShowDialog | backdrop by size: 0→prv_small, 1→prv_big, else prv_very_big; centre, RenderRichString, frame loop, destroy | **COMPLETE + WIRED** (void) |
| 0x565b88 | EvidenceDetails | **already in `world/office_recon_privilege.h`** (0xFFFF→0, matchCount<1→96, field37==1 aggregation, confirm-state 3) — reused | reused |

Callees confirmed reused (no ODR redefinition):
- `VIBE_Gesetz_GetRecord@0x4c244c` + `g_lawTable` / 26×36-B table @0x631E98 — `world/law.cpp`.
- `VIBE_Privilege_BuildEvidenceEntry@0x56589c` return codes (16/64) — `office_recon_privilege.h`.

## Golden pins (binary-truncated state effects)

- **Embezzle truncation:** `flt_624C2C`=0x3ba3d70a (just below 0.005). `(25)·0.005f·1000`
  → ConvertX trunc → **124** (not 125); `(200)·…·1000` → **999**. Pinned.
- **Law clamp:** record 12 is the only one with min>0 (min 10, max 25); clamp [10,25]
  pinned (0→10, 18→18, 999→25). Other records min 0.
- **Verdict codes** pinned per panel: EnactLaw 96/32/128/0; Embezzle 96/32/0; Miracle
  0/16/32; CounterEsp 2/32/0x12; SwapSeats 32; Evidence 96/32/0; RemoveOffice 96.
- **Command sequences** pinned via `PrivBTrace.cmds[]` (e.g. Embezzle confirm stages
  Request16+BuildOp90+Args25; Miracle stages BuildOp90+BuildMemberTable).
- **Dispatcher** routes 9 SET-B leaf-ids; unknown → 0.

## Wiring contract (rule 13)

`InvokePrivilegeLeaf(leafId, ContextActor*, InteractionEventRec*)` (live callers in
`sim/contextaction2.cpp` for EnactLaw / ShowDialog and the generic-leaf paths) →
`g_privilegeHook`. `InstallPrivilegePanelsB()` installs an adapter that owns the 9
SET-B ids and **delegates every other id to the chained prior hook** (auto-captured
via `GetPrivilegeLeafHook()`, default the SET-A adapter). `wiring.cpp` installs A then
B, so both are reachable. With no provider the panels run against the inert
`PrivilegePanelBHooks` default (compute verdict, emit nothing) — the documented
deferral posture. **HANDOFF:** the session/command cluster owner supplies a
`PrivilegePanelsBProvider` (actorToView / eventToView / makeHooks binding the live
`word_12CE910`/`byte_12CEA76` arrays, `VIBE_Amt_ComputeOfficeWages`, `VIBE_Office_*`,
the He handler scan, and the lockstep command queue) — the dispatch is already wired.

## Coupled leaves (deferred — the GUI/command/person-array boundary, same as SET-A)

`VIBE_GameTick_Finalize`/`VIBE_Form_*`/`VIBE_DragCursor_*`/`VIBE_Window_CreateScrollButtons`,
the frame loop `VIBE_GameLogic_RunFrameLoop@0x4c09a0` + `dword_75BF38`/`dword_631614`,
`VIBE_Command_*` (op90/args25/request16/coord27/entity29/slotReset28/build-action),
`VIBE_Amt_ComputeOfficeWages@0x57b480`, `VIBE_Office_*`/`VIBE_Panel_RunOfficeSession@0x5546e4`,
`VIBE_He_FindFirst/Next/MatchingEntityIndices`, `VIBE_Person_FindRecordById@0x58bc6c`,
`VIBE_Privilege_BuildOfficeMemberTable@0x56499c`, `VIBE_Hud_BuildPersonCard`,
`VIBE_Text_*`/`VIBE_Dialog_*`. Surfaced through `PrivilegePanelBHooks`.

## Build / tests

- `src/world/privilege_panels_b.cpp`, `wire_privilege_panels_b.cpp`,
  `interaction_handlers.cpp`, `wiring.cpp` all compile into `libguild.a` (clean).
- **`privilege_panels_b_test`: 158 checks, 0 failures** (predicates + panels + dispatcher).
- **`wire_privilege_panels_b_itest`: 12 checks, 0 failures** (seam reachability + A/B chaining).
- Regression: `app_real_wiring_test` 3/0, `sim_interaction_handlers_e2e_test` 26/0 — SET-A
  stays reachable through the chained hook.
- Note: transient parallel-build link races were observed while sibling agents wrote
  files concurrently (e.g. `gamelogic_recon.cpp`, `cutscene_misc2_test`); both built
  clean on retry and are unrelated to this module.
```
```
*Addresses are gilde.exe, imagebase 0x400000.*
