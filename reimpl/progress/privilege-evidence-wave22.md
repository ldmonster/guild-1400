# Privilege EVIDENCE sub-cluster — RECONSTRUCTED, VERIFIED & WIRED — Wave 22

**Agent:** W22-EVIDENCE · **Date:** 2026-06-16 · **MCP:** live (`gilde.exe`, imagebase 0x400000)

Closes the wave-21 gap: the EVIDENCE detail/build leaves (0x565b88, 0x56589c) were
cited as reused predicates only; the EvidenceReview/Alt **office HUD-list arm** was a
stub that never opened EvidenceDetails. Wave-22 reconstructs the EvidenceDetails
**panel body** 1:1, makes the Review/Alt office arm actually call it per row-click,
verifies EvidenceReviewAlt (0x5667a0) is the full 1:1 function, and wires
EvidenceDetails into the SET-B dispatcher.

## Targets — status

| addr | name | status |
|------|------|--------|
| 0x565b88 | VIBE_Privilege_PanelEvidenceDetails | **RECONSTRUCTED (panel body) + WIRED** |
| 0x56589c | VIBE_Privilege_BuildEvidenceEntry   | VERIFIED 1:1 (predicate `PrivBuildEvidenceResult` in office_recon_privilege.h is exact — see below); surfaced as the `buildEvidenceEntry` hook |
| 0x5667a0 | VIBE_Privilege_PanelEvidenceReviewAlt | VERIFIED full 1:1 + COMPLETED office arm + provenance present |
| 0x565f9c | VIBE_Privilege_PanelEvidenceReview    | COMPLETED office arm (row-click → EvidenceDetails) |

## Files owned / touched

| file | change |
|------|--------|
| `src/world/privilege_panels_b.h` | + `evidenceRowField37` hook (the 45-byte-row field37 aggregation source); + `PrivilegePanelEvidenceDetails` decl + provenance |
| `src/world/privilege_panels_b.cpp` | + EvidenceDetails panel body (0x565b88); rewrote `EvidenceReviewImpl` office arm to open EvidenceDetails per row click; + dispatcher case 0x565b88; + include office_recon_privilege.h (reuse predicates, no ODR) |
| `src/world/wire_privilege_panels_b.cpp` | + 0x565b88 in `IsPrivilegeSetBLeaf`; `InstallPrivilegePanelsB` returns 10 |
| `tests/unit/privilege_panels_b_test.cpp` | + 8 EvidenceDetails/office-arm/dispatcher tests (now **171 checks, 0 failures**) |
| `tests/integration/wire_privilege_panels_b_itest.cpp` | + 0x565b88 membership; install count 9→10 |
| `progress/privilege-evidence-wave22.md` | this file |

## EvidenceDetails @0x565b88 — 1:1 control flow (decompiled this wave)

```
v30=a1 (inspecting actor), v31=a2 (evidence target)
0x565bb4  v2 = *a2 (target id word); if v2 == 0xFFFF -> return 0
0x565bed  matchCount = He_FindMatchingEntityIndices(a2.handle, &v25, a1.id)
0x565bfe  if matchCount < 1 -> return 96
0x565c2d  do { copy 45-byte row dword_11BC760+45*v25[i];
             v8 = row+37; v28 |= (v8 == 1); } while (++i < matchCount)   // aggregation
0x565c7e  if (v30 == v31) v28 = 0                                        // self-target suppress
          ... Form build / scroll buttons / text labels (dword_8C7774 id table,
              GesetzGetRecord law rows) — coupled GUI leaves (hooks) ...
0x565f88  if (!v28) Object_SetEnabled(confirmBtn, 0)                     // confirm gated by v28
0x565ebf  frame loop:
0x565edd    if (dword_672230)             dword_631614 = 1               // window close
0x565efd    else if confirm click (v35==dword_62D22C, confirm exists):
0x565f16        BuildEvidenceEntry(a1, a2, 0, matchCount)               // ALWAYS mode 0 here
0x565f1b        dword_631614 = 3
          return 0   (always; verdict carried by dword_631614 / parent's latched v49)
```

The pure decision predicates were already golden-pinned in
`world/office_recon_privilege.h` and are REUSED verbatim (no ODR):
`PrivEvidenceHasTarget` (0xFFFF→0), `PrivEvidencePrecheck` (<1→96),
`PrivEvidenceAnyActionable` (`v28 |= field37==1`),
`PrivEvidenceConfirmEnabled` (self-target → false). The panel body adds the
frame-loop confirm/close state writes (3 / 1) and the BuildEvidenceEntry call.

## EvidenceReview (0x565f9c) vs EvidenceReviewAlt (0x5667a0) — verified diff

Decompiled both fresh. They are **identical** except:
- non-office concrete path: BuildEvidenceEntry mode arg **0** (Review, 0x566045) vs
  **1** (ReviewAlt, 0x56684e) — already modelled by `EvidenceReviewImpl(...,mode)`.
- ReviewAlt's office row-build loop has one extra store
  `*(&v38[12]+v35) = 1` (0x566b1f) — a per-row UI/select-flag slot in the local
  card buffer; it has **no observable state effect** outside the (deferred) Form/HUD
  card layout, so it is not part of the reconstructed verdict/command surface.
- BOTH office arms: on a row click they resolve the row's evidence target and call
  `v49/v50 = VIBE_Privilege_PanelEvidenceDetails(actor, rowTarget)` (0x5663a1 /
  0x566b83), latching its return (always 0) as the verdict; init verdict 32; a
  window close sets dword_631614 = 1. This is now reproduced in `EvidenceReviewImpl`.

ReviewAlt provenance `gilde.exe 0x5667a0` is present in privilege_panels_b.{h,cpp};
the function body is the full 1:1 (office arm + non-office concrete arm), not a stub.

## BuildEvidenceEntry @0x56589c — verified 1:1 (predicate exact)

```
scan persons (QueryBegin kind 1..6) for office byte == 15 (judge); break on first
0x5658f2  if no judge found -> return 16                       // judgeFound==false
          build the lockstep entry (a3=mode selects witness sourcing branch)
0x565a6b  if !FindOneByPaletteRange(...) -> return 64          // first witness miss
0x565ad0  if !FindOneByPaletteRange(...) -> return 64          // second witness miss
0x565b76  QueueRequestSlotReset28(...)
0x565adf  return 16
```
`PrivBuildEvidenceResult(judgeFound, witnessesFound)` in office_recon_privilege.h
matches exactly: `!judgeFound→16`, `witnessesFound?16:64`. Surfaced through the
`buildEvidenceEntry` hook (returns 16/64); the person-scan / palette-range entity
search + the SlotReset28 command are coupled engine leaves (deferred boundary).

## ConvertX / truncation audit

EvidenceDetails / BuildEvidenceEntry / EvidenceReview(Alt) contain **no float→int
sites** — no ConvertX or fistp in any of the four functions (only integer compares,
45-byte memcpy strides, RNG `RandomModulo(2)` in BuildEvidenceEntry's witness pick).
Nothing to truncate here. (The embezzle ConvertX path stays as wave-21 pinned.)

## Golden pins (EvidenceDetails state effects)

- target id == 0xFFFF → return 0 (no state). Pinned.
- matchCount < 1 → return 96. Pinned.
- aggregation: any row.field37==1 ⇒ confirm enabled; self-target (actorIsTarget)
  forces it off regardless. Pinned (`SelfTargetDisablesConfirm`,
  `NoActionableRowDisablesConfirm`).
- confirm click (enabled) → BuildEvidenceEntry(mode 0, matchCount) + dword_631614=3.
  Pinned (`ConfirmBuildsEntryStateThree`).
- window close → dword_631614=1. Pinned (`WindowCloseStateOne`).
- office Review/Alt arm row-click → latches EvidenceDetails return (0). Pinned.

## Wiring (rule 13)

`PrivilegeDispatchPanelB` now routes **0x565b88**: resolve target via
`hooks.findRecord(ev->targetId)`, compute `actorIsTarget = target==actor`, call
`PrivilegePanelEvidenceDetails`. `IsPrivilegeSetBLeaf(0x565b88)` returns true and
`InstallPrivilegePanelsB` returns 10. The seam (`sim::InvokePrivilegeLeaf` →
`g_privilegeHook`, installed by `wiring.cpp`) is unchanged. EvidenceDetails is also
reachable indirectly through the Review/Alt office arm (the in-game path: the office
HUD list opens it per row). With no provider the inert hooks compute the gates and
emit nothing — the documented headless posture.

## Build / tests

- `privilege_panels_b.cpp`, `wire_privilege_panels_b.cpp` and the test/itest all
  compile clean (`-std=c++17`, syntax + standalone link verified).
- **`privilege_panels_b_test`: 171 checks, 0 failures** (was 158; +13 this wave).
- **NOTE (not my file):** the full `libguild.a` build is currently blocked by an
  unrelated, untracked sibling-agent file `src/gui/gui_object_state.h`
  (`static_assert(sizeof(ObjectStateRecord)==84)` fails: 88!=84). It is outside this
  agent's ownership and was left untouched. My TUs were verified by isolated
  compile + a standalone test link (`privilege_panels_b.cpp + law.cpp + coord.cpp +
  crt/rand.cpp + test`) which runs 171/0.

*Addresses are gilde.exe, imagebase 0x400000.*
