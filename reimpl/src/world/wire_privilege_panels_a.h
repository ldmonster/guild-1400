#pragma once
// wire_privilege_panels_a — Rule-13 wiring of the SET-A privilege panels into the
// live office context-menu dispatch tree.
//
// The dispatcher seam is sim::SetPrivilegeLeafHook (interaction_handlers.h): the
// ~45 ContextAction executors (interaction_handlers.cpp / contextaction2.cpp) call
// InvokePrivilegeLeaf(leafId, ContextActor* a, InteractionEventRec* ev) on activate,
// where leafId is the absolute gilde.exe address of the chosen VIBE_Privilege_*
// panel. Before this installer that hook ran the inert module default (returns 0).
//
// InstallPrivilegePanelsA() registers a sim::PrivilegeLeafFn that, for the ten
// SET-A leaf-ids (0x563000 / 0x565304 / 0x5639f4 / 0x560f14 / 0x563f14 / 0x5643e8 /
// 0x563614 / 0x560500 / 0x5608b0 / 0x5647c8 + 0x5627cc), converts the live
// ContextActor + InteractionEventRec into the PrivPerson / PrivEvent views and
// dispatches to the reconstructed panels in privilege_panels_a.cpp.
//
// HANDOFF (the part this agent does NOT own): the panels' COUPLED engine leaves —
// the live word_12CE910 person array (findRecord / the Convert 768-sweep), the
// lockstep command queue (VIBE_Command_*), the Form/HUD frame loop (nextButton ==
// dword_75BF38) and the Text/Dialog/He leaves — are supplied via a
// PrivilegePanelHooks vtable. The owner of those subsystems (the session/command
// cluster) provides a PrivilegePanelHooksProvider; this installer wires the
// dispatch, they wire the leaves. Until a provider is set the panels run against
// the inert PrivilegePanelHooks default (records into the trace, emits nothing) —
// the SAME deferral posture office_recon_privilege.h documents for this cluster.
#include "world/privilege_panels_a.h"
#include "sim/interaction_handlers.h"  // ContextActor, InteractionEventRec, SetPrivilegeLeafHook

namespace guild::world {

// The cluster that owns the live person array + command queue supplies these:
//   * actorToView / eventToView — copy the live record/event into the value views
//     the panels read (the offsets are documented on PrivPerson / PrivEvent).
//   * makeHooks — build a PrivilegePanelHooks bound to the live subsystems.
struct PrivilegePanelsAProvider {
    void (*actorToView)(const sim::ContextActor* a, PrivPerson* out, void* ctx) = nullptr;
    void (*eventToView)(const sim::InteractionEventRec* ev, PrivEvent* out, void* ctx) = nullptr;
    PrivilegePanelHooks (*makeHooks)(void* ctx) = nullptr;
    // For Convert (0x5643e8): the live 768-person array view.
    const PrivPerson* (*peopleArray)(int* outCount, void* ctx) = nullptr;
    void* ctx = nullptr;
};

// Install the SET-A dispatch into sim::SetPrivilegeLeafHook. With no provider, the
// panels run against the inert hooks default. Returns the count of leaf-ids handled.
int InstallPrivilegePanelsA(const PrivilegePanelsAProvider* provider = nullptr);

} // namespace guild::world
