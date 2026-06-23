// wire_privilege_panels_a — see header. Routes the SET-A privilege leaf-ids from
// the office context-menu dispatcher to the reconstructed panels (Rule 13).
#include "world/wire_privilege_panels_a.h"

namespace guild::world {

namespace {

const PrivilegePanelsAProvider* g_provider = nullptr;

// The set of leaf-ids this batch owns (the SET-A panel addresses).
bool IsSetALeaf(int leafId) {
    switch (leafId) {
        case 0x563000: case 0x565304: case 0x5639f4: case 0x560f14:
        case 0x563f14: case 0x5643e8: case 0x563614: case 0x560500:
        case 0x5608b0: case 0x5647c8: case 0x5627cc:
            return true;
        default:
            return false;
    }
}

// sim::PrivilegeLeafFn adapter: convert the live ContextActor/event into the value
// views and dispatch to the reconstructed panel. Returns the panel verdict (the
// low result bits the ContextAction caller ORs into its action code).
int PrivilegeLeafAdapter(int leafId, sim::ContextActor* a,
                         sim::InteractionEventRec* ev) {
    if (!IsSetALeaf(leafId)) return 0;

    PrivPerson actorView{};
    PrivEvent  evView{};
    PrivilegePanelHooks hooks{};
    void* pctx = g_provider ? g_provider->ctx : nullptr;

    if (g_provider && g_provider->actorToView)
        g_provider->actorToView(a, &actorView, pctx);
    if (g_provider && g_provider->eventToView)
        g_provider->eventToView(ev, &evView, pctx);
    if (g_provider && g_provider->makeHooks)
        hooks = g_provider->makeHooks(pctx);

    if (leafId == 0x5643e8) {  // Convert needs the live 768-person array
        int count = 0;
        const PrivPerson* people = nullptr;
        if (g_provider && g_provider->peopleArray)
            people = g_provider->peopleArray(&count, pctx);
        return (int)(signed char)PrivilegePanelConvert(&actorView, &evView, &hooks,
                                                       people, count);
    }
    return (int)(signed char)PrivilegeDispatchPanelA(leafId, &actorView, &evView,
                                                     &hooks);
}

} // namespace

int InstallPrivilegePanelsA(const PrivilegePanelsAProvider* provider) {
    g_provider = provider;
    sim::SetPrivilegeLeafHook(&PrivilegeLeafAdapter);
    return 11;  // ten panels + CharmConfirm
}

} // namespace guild::world
