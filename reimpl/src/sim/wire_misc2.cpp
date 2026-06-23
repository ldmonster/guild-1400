// See wire_misc2.h. Binds the single signature-compatible field of the misc2
// hook-bridge cluster (PersonnelGuiHooks::randomModulo) into the real reconstructed
// util::RandomModulo. Glue only — no module logic. Seed-from-defaults.
#include "sim/wire_misc2.h"

#include "gui/personnel_gui.h"   // PersonnelGuiHooks / g_personnelGuiHooks / SetPersonnelGuiHooks
#include "util/math_random.h"    // util::RandomModulo @0x58b89c

namespace guild::sim {

namespace {

// PersonnelGuiHooks::randomModulo has shape `int (*)(int n)` and is the bribe-bonus
// RNG source (RecruitOfferBonusRoll). The reconstructed sibling util::RandomModulo
// takes a u16 (matching the original VIBE_Math_RandomModulo signature); this is the
// exact wrapper wire_charaction.cpp uses for the same leaf.
int Misc2RandomModulo(int n) {
    return guild::util::RandomModulo(static_cast<guild::u16>(n));
}

} // namespace

void InstallRealMisc2Wiring() {
    // --- PersonnelGuiHooks (gui/personnel_gui.h) -----------------------------
    // Seed from the module's current inert defaults (non-null stubs) so the many
    // form/render/command/sim fields we do NOT bind keep their safe stubs — the
    // personnel dialog bodies invoke several hooks without a null-check.
    guild::gui::PersonnelGuiHooks h = guild::gui::g_personnelGuiHooks;
    h.randomModulo = &Misc2RandomModulo;
    // Inert (no clean reconstructed leaf):
    //   gameTickFinalize / formCenterChildWindows / formSelectWindow / formDestroy /
    //   formSetChildrenVisible / formGetChildObjectId / textRenderRichString /
    //   objectSetValueOrText / dialogCheckResourceAmount / dialogShowMessageBox /
    //   gameLogicRunFrameLoop / readLastClickedObject / readCancelEdge  — render /
    //     form / frame-loop leaves.
    //   queueHireRequest / commandGetPacketStatus / refreshGuildState — command-queue
    //     / amt leaves.
    //   sumCurrencyHeld — VIBE_Person_SumCurrencyHeld's reconstruction takes a
    //     ContainerView (person+376 child list), not the hook's bare personId; no
    //     clean adapter without the person-record model.
    //   computeRecruitmentCost — RecruitComputeRecruitmentCost takes (recruiterId,
    //     candidateId); the hook drops the recruiter, so no faithful 1-arg bind.
    guild::gui::SetPersonnelGuiHooks(h);

    // --- EventTableHooks / BioCodecHooks / ItemLabelHooks / MissionDialogHooks
    // ZERO-bindable (see wire_misc2.h rationale): nothing installed; their module
    // inert defaults stand. createEvent's default already hands back a non-null
    // dummy handle so the table-append bookkeeping runs 1:1.
}

} // namespace guild::sim
