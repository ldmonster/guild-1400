// wire_privilege_panels_b — see header. Routes the SET-B privilege leaf-ids from
// the office context-menu dispatcher to the reconstructed panels, CHAINING onto the
// previously-installed hook (the SET-A adapter) for every other leaf (Rule 13).
#include "world/wire_privilege_panels_b.h"

namespace guild::world {

namespace {

const PrivilegePanelsBProvider* g_provider = nullptr;
sim::PrivilegeLeafFn            g_chain    = nullptr; // prior hook (e.g. SET-A)

// sim::PrivilegeLeafFn adapter: for SET-B leaf-ids, convert the live ContextActor/
// event into the value views and dispatch to the reconstructed panel; otherwise
// delegate to the chained hook (keeps SET-A reachable through the single slot).
int PrivilegeLeafAdapterB(int leafId, sim::ContextActor* a,
                          sim::InteractionEventRec* ev) {
    if (!IsPrivilegeSetBLeaf(leafId))
        return g_chain ? g_chain(leafId, a, ev) : 0;

    PrivPerson actorView{};
    PrivEvent  evView{};
    PrivilegePanelBHooks hooks{};
    void* pctx = g_provider ? g_provider->ctx : nullptr;

    if (g_provider && g_provider->actorToView)
        g_provider->actorToView(a, &actorView, pctx);
    if (g_provider && g_provider->eventToView)
        g_provider->eventToView(ev, &evView, pctx);
    if (g_provider && g_provider->makeHooks)
        hooks = g_provider->makeHooks(pctx);

    return static_cast<int>(static_cast<signed char>(
        PrivilegeDispatchPanelB(leafId, &actorView, &evView, &hooks)));
}

} // namespace

bool IsPrivilegeSetBLeaf(int leafId) {
    switch (leafId) {
        case 0x561bb4: // EnactLaw
        case 0x561fd0: // RemoveFromOffice
        case 0x5628c8: // CounterEspionage
        case 0x562334: // Embezzlement
        case 0x562cdc: // SwapSeats
        case 0x5651bc: // Miracle
        case 0x565f9c: // EvidenceReview
        case 0x5667a0: // EvidenceReviewAlt
        case 0x565b88: // EvidenceDetails (opened per-row from Evidence Review/Alt)
        case 0x571218: // ShowDialog
            return true;
        default:
            return false;
    }
}

int InstallPrivilegePanelsB(const PrivilegePanelsBProvider* provider,
                            sim::PrivilegeLeafFn chainTo) {
    g_provider = provider;
    // Default the chain to whatever hook is already installed (e.g. the SET-A
    // adapter), so installing B after A keeps both batches reachable.
    g_chain = chainTo ? chainTo : sim::GetPrivilegeLeafHook();
    sim::SetPrivilegeLeafHook(&PrivilegeLeafAdapterB);
    return 10;  // ten SET-B leaf-ids (8 verdict panels + EvidenceDetails + ShowDialog)
}

} // namespace guild::world
