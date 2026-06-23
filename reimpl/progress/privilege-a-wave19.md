# Wave-19 — Privilege/office-power cluster, SET A (social/personnel actions)

**Agent:** W19-PRIV-A · **Date:** 2026-06-15 · **MCP:** live (`gilde.exe`, imagebase 0x400000)

Reconstructed the ten guild-office PRIVILEGE action panels of "set A" (the
social/personnel actions) 1:1 from the Hex-Rays decompile, plus their two confirm
sub-dialogs. New module: `src/world/privilege_panels_a.{h,cpp}`. Wiring:
`src/world/wire_privilege_panels_a.{h,cpp}`. Tests:
`tests/unit/privilege_panels_a_test.cpp` (40 cases, 117 checks, 0 failures).

## Functions reconstructed (1:1)

| addr | symbol | reconstructed | passive return codes |
|------|--------|---------------|----------------------|
| 0x563000 | VIBE_Privilege_PanelGenerateHatred | full control flow, 2x RNG draw order, relation deltas, command sequence, both paths | 16 / 32 |
| 0x565304 | VIBE_Privilege_PanelChangeProfession | office-only grid path, same-prof skip, BuildOp72+BuildOp90 on confirm | 16 / 0 |
| 0x5639f4 | VIBE_Privilege_PanelExpelWorker | mode 3/4 gates, 456-sign immune, Args25(456)+Pair33 dismiss, notify | 16 / 32 / 96 / -127 |
| 0x560f14 | VIBE_Privilege_PanelBlackmail | office scroll-list + row dispatch → BlackmailConfirm; passive roll | 16 / 96 |
| 0x560c1c | VIBE_Privilege_BlackmailConfirm | RandomModulo(8) <= matchCount success roll; slot-reset vs relation -26/-2 | 1 / 0 |
| 0x563f14 | VIBE_Privilege_PanelMakePeace | up-front RNG (passive), +relation both dirs (15+rnd28), -3 cost, notify | 16 / 32 / 96 |
| 0x5643e8 | VIBE_Privilege_PanelConvert | 768-person sweep, kind/religion gates, charm132 vs byte994+rnd0x7E roll, count | 16 / 32 |
| 0x563614 | VIBE_Privilege_PanelInterrogation | -2/-3 cost, Args25(456), 457-sign immune→-127, office picker | 16 / 32 / 96 / -127 |
| 0x560500 | VIBE_Privilege_PanelMedicus | 2%-wealth fee, RandomModulo(2) success flip, fee transfer | 17 / 0 (GUI 2/129) |
| 0x5608b0 | VIBE_Privilege_PanelDivorce | office>=8, 12%-afford gate, +0x5C link delta on both, 8% fee | 17 / 34 |
| 0x5647c8 | VIBE_Privilege_PanelApology | dragField 1..9 range gate, rival-pairs request + skill cost | 0 / 96 (GUI 1) |
| 0x5627cc | VIBE_Privilege_CharmConfirm | passive [1,5] gate + distance-penalty relation write; office picker→1 | 16 / 96 (office 1/0) |

All RNG draw counts/moduli, command emission order+args, relation-matrix delta math,
the float32 cost factors and their ConvertX truncation, and every return code /
`dword_631614` done-state are reproduced exactly.

## Cost-table constants (get_bytes, decoded little-endian IEEE754)

| sym | addr | value | used by |
|-----|------|-------|---------|
| flt_624B48 | 0x624b48 | 0.019999999552965164 (≈2%) | Medicus |
| flt_624D34 | 0x624d34 | 0.05999999865889549 (≈6%) | Convert |
| flt_624CBC | 0x624cbc | 0.019999999552965164 (≈2%) | GenerateHatred |
| dbl_624B64 | 0x624b64 | 0.08 (8%) | Divorce fee A |
| dbl_624B6C | 0x624b6c | 0.04 (4%) | Divorce fee B |
| dbl_624B74 | 0x624b74 | 0.12 (12%) | Divorce afford ratio |

**Load-bearing 1:1 detail:** the 2%/6% factors are float32 literals that are JUST
under the exact fraction, so `wealth * factor` falls under the integer and ConvertX
truncates DOWN (e.g. 2% of 1050 = 20.9999995 → **20**, not 21; 6% of 1000 = 59.999 →
**59**). Golden tests pin these exact truncated costs (CostTruncation, the per-panel
cost asserts). This is exactly the integer/float edge case Rule 1 requires.

## Fidelity model (how the GUI/command coupling is handled)

Each panel is, in the original, a Form frame-loop dialog (VIBE_GameTick_Finalize /
VIBE_Form_* / VIBE_GameLogic_RunFrameLoop) that on a button (dword_75BF38 == 1210
OK / 1155 cancel, or a child-object click == dword_62D22C) mutates state and enqueues
lockstep commands (VIBE_Command_*), then writes dword_631614 and returns a verdict.

Following the precedent set by `world/office_recon_privilege.h` for this exact
cluster, the coupled engine leaves (Form/HUD/Vulkan window, the lockstep command
queue, Text/Dialog render, the live word_12CE910 person array, He notifications) are
surfaced through ONE mockable `PrivilegePanelHooks` vtable. The LOAD-BEARING,
TESTABLE state effects — cost math, RNG order, relation deltas, command sequence,
success rolls, return codes, done-states — are reconstructed EXACTLY and golden-
pinned. The frame-loop button event each iteration is delivered by `hooks.nextButton()`
(the original's latched dword_75BF38); `kPrivLoopExit` ends the loop (RunFrameLoop→0).

This is NOT a cheap analogue (Rule 8): the decision/mutation logic is the real engine
logic, byte-for-byte from the decompile; only the window/GPU/network *transport* (the
pre-approved rule 3-5 boundaries) is behind the hook.

## Wiring (Rule 13) + handoff

`src/world/wire_privilege_panels_a.cpp` installs a `sim::PrivilegeLeafFn` adapter via
`sim::SetPrivilegeLeafHook` (the dispatcher seam in `sim/interaction_handlers.h`).
The ~45 ContextAction executors (interaction_handlers.cpp / contextaction2.cpp) call
`InvokePrivilegeLeaf(leafId, ContextActor*, InteractionEventRec*)` on activate; the
adapter recognizes the eleven SET-A leaf-ids, converts the live record/event into the
`PrivPerson`/`PrivEvent` views, and dispatches to the reconstructed panels. Before
this installer that hook ran the inert module default (returned 0). `InstallPrivilegePanelsA()`
returns 11.

**One-line handoff** (the part this agent does NOT own — the live person array +
command queue + Form loop): the session/command cluster supplies a
`PrivilegePanelsAProvider` (actorToView / eventToView / makeHooks / peopleArray)
binding the panels to the live subsystems, then calls `InstallPrivilegePanelsA(&provider)`.
Until then the panels run against the inert `PrivilegePanelHooks` default (record-into-
trace, emit nothing) — the same deferral posture this cluster already documents.

## SET B coordination (W19-PRIV-B)

No shared helper was extracted into a `privilege_common.h`: the SET-A panels share
nothing with SET-B beyond the generic `PrivCostFromWealth(wealth, factor)` cost helper
and the `PrivilegePanelHooks` vtable, both already public in `privilege_panels_a.h`.
If SET-B (Evidence/EnactLaw/Embezzlement/Miracle/RemoveFromOffice/SwapSeats/
CounterEspionage) wants them, it should `#include "world/privilege_panels_a.h"` and
extern the helpers rather than redefine — no `privilege_common.h` was created.

## ODR / reuse

Grepped src/** before defining every symbol — zero clashes (the prior privilege
modules `world/privilege.h`, `world/office_recon_privilege.h` hold only the pure
gate PREDICATES; this module holds the full PANEL bodies, distinct symbols). The
enum stubs at these addrs in `sim/contextaction2.h` / `sim/interaction_handlers.h`
remain authoritative as the opaque leaf-ids; this module's dispatcher matches them.
Reused `guild::util::ConvertX` (truncate) and the existing test framework.

## Build status

`src/world/privilege_panels_a.cpp`, `wire_privilege_panels_a.cpp` and the test all
compile cleanly against the real headers (verified standalone + -fsyntax-only). The
full `build/` `guild` target is currently RED due to UNRELATED in-flight sibling
files (`src/render/water_render.cpp`, `src/script/script_vm.cpp` — both untracked /
mid-edit by other wave-19 agents, not touched here). My module is self-contained and
adds no error. Test result (linked standalone with coord + test_main):
**117 checks, 0 failures.**

## Leaves followed to genuine boundaries

The panels' callees were decompiled and classified: VIBE_Person_FindRecordById/
ComputeTotalWealth/SumCurrencyHeld (person subsystem, hooked), VIBE_Command_*
(lockstep queue boundary, hooked), VIBE_Math_RandomModulo (util::RandomModulo,
real), VIBE_Coord_ConvertX (util::ConvertX, real, truncates), VIBE_Relation_
LookupMatrixEntry (relation matrix, hooked), VIBE_He_* / VIBE_Amt_RunOfficeOverview
Window / VIBE_Form_* / VIBE_Text_* / VIBE_Dialog_* (HUD/Form/Text render boundary,
hooked). No reconstructable game-logic leaf was papered over.
