#pragma once
// wire_privilege_panels_b — Rule-13 wiring of the SET-B privilege panels (law /
// evidence / espionage + dispatch) into the live office context-menu dispatch tree.
//
// The dispatcher seam is sim::SetPrivilegeLeafHook (interaction_handlers.h): the
// ~45 ContextAction executors (interaction_handlers.cpp / contextaction2.cpp) call
// InvokePrivilegeLeaf(leafId, ContextActor* a, InteractionEventRec* ev) on activate,
// where leafId is the absolute gilde.exe address of the chosen VIBE_Privilege_*
// panel. contextaction2.cpp already invokes the SET-B leaf-ids directly
// (kPrivEnactLaw 0x561bb4, kPrivRemoveFromOffice 0x561fd0, kPrivCounterEspionage
// 0x5628c8, kPrivEmbezzlement 0x562334, kPrivSwapSeats 0x562cdc, kPrivShowDialog
// 0x571218, kPrivEvidenceReview 0x565f9c, …) — before this installer they ran the
// inert g_privilegeHook default (returns 0).
//
// CHAINING: sim::SetPrivilegeLeafHook holds a single function pointer, so SET-A and
// SET-B cannot both own it independently. InstallPrivilegePanelsB therefore installs
// ONE adapter that owns the SET-B leaf-ids and DELEGATES every other leaf-id to a
// chain target (the previously-installed hook — typically the SET-A adapter passed
// from wiring.cpp). This is fully additive: installing B after A keeps A reachable.
//
// HANDOFF (the part this agent does NOT own): the panels' COUPLED engine leaves —
// the live word_12CE910 / byte_12CEA76 person+office arrays, VIBE_Amt_ComputeOfficeWages,
// VIBE_Office_* / VIBE_Panel_RunOfficeSession, the He handler scan, the lockstep
// command queue (VIBE_Command_*) and the Form/HUD frame loop — are supplied via the
// PrivilegePanelBHooks vtable. The session/command cluster owner provides a
// PrivilegePanelsBProvider; this installer wires the dispatch, they wire the leaves.
// Until a provider is set the panels run against the inert PrivilegePanelBHooks
// default (records into the trace, emits nothing) — the SAME deferral posture
// office_recon_privilege.h documents for this cluster.
#include "world/privilege_panels_b.h"
#include "sim/interaction_handlers.h"  // ContextActor, InteractionEventRec, SetPrivilegeLeafHook, PrivilegeLeafFn

namespace guild::world {

// The cluster that owns the live person/office arrays + command queue supplies these:
//   * actorToView / eventToView — copy the live record/event into the value views
//     the panels read (offsets documented on PrivPerson / PrivEvent).
//   * makeHooks — build a PrivilegePanelBHooks bound to the live subsystems.
struct PrivilegePanelsBProvider {
    void (*actorToView)(const sim::ContextActor* a, PrivPerson* out, void* ctx) = nullptr;
    void (*eventToView)(const sim::InteractionEventRec* ev, PrivEvent* out, void* ctx) = nullptr;
    PrivilegePanelBHooks (*makeHooks)(void* ctx) = nullptr;
    void* ctx = nullptr;
};

// True iff `leafId` is one of the SET-B panel addresses this installer owns.
bool IsPrivilegeSetBLeaf(int leafId);

// Install the SET-B dispatch into sim::SetPrivilegeLeafHook.
//   provider : the live-subsystem provider (null => inert hooks default).
//   chainTo  : the previously-installed hook to delegate non-SET-B leaf-ids to
//              (e.g. the SET-A adapter). Null => non-SET-B leaves return 0.
// Returns the count of SET-B leaf-ids handled (9).
int InstallPrivilegePanelsB(const PrivilegePanelsBProvider* provider = nullptr,
                            sim::PrivilegeLeafFn chainTo = nullptr);

} // namespace guild::world
