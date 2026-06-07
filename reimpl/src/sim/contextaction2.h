#pragma once
// ContextAction2 — the deferred ContextAction command-executor variants for the
// Guild simulation (gilde.exe, VIBE_ContextAction_* family, 0x56xxxx range). The
// first ~28 handlers live in interaction_handlers.{h,cpp}; this file translates the
// remaining 21 near-duplicate variants that interaction_handlers.cpp listed as
// deferred (the extra rank-train tiers, the profession-menu-by-submethod/profession
// variants, the FileLawsuit pair, and the remaining command-by-profession +
// drag-drop variants).
//
// Each is a context-menu entry executor: it reads the InteractionEventRec `mode`
// byte (1 tooltip / 3 activate / 4,2 drag) and the ContextActor record's rank
// (+13), profession (+358), submethod (+361), gender (+9), kind (+2) and flag bytes
// (+457/+458) to decide a verdict (1 reject / 2 handled / 4 next-tier / 9 busy /
// 10 drag-handled), invoking a VIBE_Privilege_* panel leaf on activate (routed
// through the same g_privilegeHook the first batch installs) and rendering a
// tooltip on mode 1 (recorded as g_leafTrace.tooltipStringId).
//
// These reuse the InteractionEventRec / ContextActor layouts, the leaf-hook trace,
// and the shared Privilege()/RenderTooltip() plumbing from interaction_handlers.h —
// no record/event layout is redefined here (ODR: see interaction_handlers.h).
//
// Translated functions (addresses absolute, imagebase 0x400000):
//   0x56f514 TrainRank4B           0x56f5c0 TrainRank4Plus
//   0x56f710 TrainRank5B           0x56f85c TrainRank6A
//   0x56f900 TrainRank6B
//   0x56fbe0 FileLawsuitType1      0x56fd3c FileLawsuitType2
//   0x570028 ProfessionMenu28      0x5700e8 ProfessionMenu29
//   0x5701a8 ProfessionMenu34      0x570268 ProfessionMenuRange30
//   0x57032c ProfessionMenuOffice  0x5705bc ProfessionMenuType17A
//   0x570684 ProfessionMenuType17B 0x57074c ProfessionMenuClergy
//   0x5708dc ProfessionMenuType10  0x57097c ProfessionMenuType15
//   0x570bb0 ProfessionMenuType23
//   0x570af8 CommandType24         0x570e2c CommandType21Or26
//   0x570fb0 CommandType24Drag     0x571068 CommandType18Or22
//   0x571134 CommandType26
#include "guild/common/types.h"
#include "sim/interaction_handlers.h"  // ContextActor, InteractionEventRec, hooks

namespace guild::sim {

// ===========================================================================
// Additional Privilege panel-leaf ids reached by this batch (absolute addresses
// of the real VIBE_Privilege_* leaves, used as stable opaque ids in g_leafTrace).
// ===========================================================================
enum PrivilegeLeaf2 : int {
    kPrivGenerateHatred      = 0x563000, // VIBE_Privilege_PanelGenerateHatred
    kPrivConvert             = 0x5643e8, // VIBE_Privilege_PanelConvert
    kPrivApology             = 0x5647c8, // VIBE_Privilege_PanelApology
    kPrivEvidenceReviewAlt   = 0x5667a0, // VIBE_Privilege_PanelEvidenceReviewAlt
    kPrivMiracle             = 0x5651bc, // VIBE_Privilege_PanelMiracle
    // kPrivEnactLaw (0x561bb4) is already declared in interaction_handlers.h.
};

// ---------------------------------------------------------------------------
// Gesetz (law) leaf used by the FileLawsuit pair. VIBE_Gesetz_FindRecordByPair
// (0x4c258c) looks up a law/penalty record for (actor, lawsuitType) and, on a
// match, writes the penalty record's high byte (the dispatched action code) into
// the caller's scratch. Tests install a mock; the default reports "no record".
// Returns: true if a record exists. `outActionCode` receives the record's high
// byte (the value the original stores via `v13[0] >> 24` into ev->targetId).
// ---------------------------------------------------------------------------
using GesetzFindRecordFn = bool (*)(ContextActor* actor, int lawsuitType,
                                    int* outActionCode);
void SetGesetzFindRecordHook(GesetzFindRecordFn fn);

// ===========================================================================
// Rank-train tier variants (the ContextTrainTier shape with extra modes).
// ===========================================================================

// gilde.exe 0x56f514 — VIBE_ContextAction_TrainRank4B (rank == 4 exact).
char ContextTrainRank4B(ContextActor* actor, InteractionEventRec* ev);
// gilde.exe 0x56f5c0 — VIBE_ContextAction_TrainRank4Plus (rank==6 -> 4; rank<4 -> 1).
char ContextTrainRank4Plus(ContextActor* actor, InteractionEventRec* ev);
// gilde.exe 0x56f710 — VIBE_ContextAction_TrainRank5B (rank >= 5).
char ContextTrainRank5B(ContextActor* actor, InteractionEventRec* ev);
// gilde.exe 0x56f85c — VIBE_ContextAction_TrainRank6A (rank >= 6).
char ContextTrainRank6A(ContextActor* actor, InteractionEventRec* ev);
// gilde.exe 0x56f900 — VIBE_ContextAction_TrainRank6B (rank >= 6).
char ContextTrainRank6B(ContextActor* actor, InteractionEventRec* ev);

// ===========================================================================
// FileLawsuit pair — activate (mode 3/1) files a lawsuit if the actor has a
// profession (+358) and a matching Gesetz record; drag (mode 4/2) files against
// the drag-source. Gated by the (458 & mask) "already filed" flag and the
// (457 & 2) busy flag. Returns the leaf result OR'd with 2 (activate) / 0xA (drag).
// ===========================================================================

// gilde.exe 0x56fbe0 — VIBE_ContextAction_FileLawsuitType1 (lawsuit type 1, 458&8).
char ContextFileLawsuitType1(ContextActor* actor, InteractionEventRec* ev);
// gilde.exe 0x56fd3c — VIBE_ContextAction_FileLawsuitType2 (lawsuit type 2, 458&0x10).
char ContextFileLawsuitType2(ContextActor* actor, InteractionEventRec* ev);

// ===========================================================================
// Profession-menu variants — gate on either the submethod byte (+361) or the
// profession byte (+358) matching a set, then render a dialog (mode 3) / tooltip
// (mode 1). All return 1 (reject) / 2 (handled). (The ShowDialog flag arg 0/1/2 is
// a pure UI detail folded into the privilege leaf, matching the first batch.)
// ===========================================================================

// Submethod (+361)-gated:
// gilde.exe 0x570028 — VIBE_ContextAction_ProfessionMenu28 (submethod == 28).
char ContextProfessionMenu28(ContextActor* actor, InteractionEventRec* ev);
// gilde.exe 0x5700e8 — VIBE_ContextAction_ProfessionMenu29 (submethod == 29).
char ContextProfessionMenu29(ContextActor* actor, InteractionEventRec* ev);
// gilde.exe 0x5701a8 — VIBE_ContextAction_ProfessionMenu34 (submethod == 34).
char ContextProfessionMenu34(ContextActor* actor, InteractionEventRec* ev);
// gilde.exe 0x570268 — VIBE_ContextAction_ProfessionMenuRange30 (submethod 30..33).
char ContextProfessionMenuRange30(ContextActor* actor, InteractionEventRec* ev);

// Profession (+358)-gated:
// gilde.exe 0x57032c — VIBE_ContextAction_ProfessionMenuOffice (prof 20/24/25).
char ContextProfessionMenuOffice(ContextActor* actor, InteractionEventRec* ev);
// gilde.exe 0x5705bc — VIBE_ContextAction_ProfessionMenuType17A (prof == 17).
char ContextProfessionMenuType17A(ContextActor* actor, InteractionEventRec* ev);
// gilde.exe 0x570684 — VIBE_ContextAction_ProfessionMenuType17B (prof == 17).
char ContextProfessionMenuType17B(ContextActor* actor, InteractionEventRec* ev);
// gilde.exe 0x57074c — VIBE_ContextAction_ProfessionMenuClergy (prof 14/10).
char ContextProfessionMenuClergy(ContextActor* actor, InteractionEventRec* ev);
// gilde.exe 0x5708dc — VIBE_ContextAction_ProfessionMenuType10 (prof == 10).
char ContextProfessionMenuType10(ContextActor* actor, InteractionEventRec* ev);
// gilde.exe 0x57097c — VIBE_ContextAction_ProfessionMenuType15 (prof == 15).
char ContextProfessionMenuType15(ContextActor* actor, InteractionEventRec* ev);
// gilde.exe 0x570bb0 — VIBE_ContextAction_ProfessionMenuType23 (prof 23/19).
char ContextProfessionMenuType23(ContextActor* actor, InteractionEventRec* ev);

// ===========================================================================
// Command-by-profession + drag variants (the ContextCommandByProfession shape).
// ===========================================================================

// gilde.exe 0x570af8 — VIBE_ContextAction_CommandType24 (prof 24).
char ContextCommandType24(ContextActor* actor, InteractionEventRec* ev);
// gilde.exe 0x570e2c — VIBE_ContextAction_CommandType21Or26 (prof 21/26).
char ContextCommandType21Or26(ContextActor* actor, InteractionEventRec* ev);
// gilde.exe 0x570fb0 — VIBE_ContextAction_CommandType24Drag (prof 24, Apology leaf).
char ContextCommandType24Drag(ContextActor* actor, InteractionEventRec* ev);
// gilde.exe 0x571068 — VIBE_ContextAction_CommandType18Or22 (prof 18/22).
char ContextCommandType18Or22(ContextActor* actor, InteractionEventRec* ev);
// gilde.exe 0x571134 — VIBE_ContextAction_CommandType26 (prof 26, Miracle leaf).
char ContextCommandType26(ContextActor* actor, InteractionEventRec* ev);

// ===========================================================================
// Registration. Records the (action-id -> fn) bindings for this batch without
// clobbering interaction_handlers' set. Returns the count (21). A test can call
// ContextAction2_Lookup(addr) to fetch a handler by its binary address.
// ===========================================================================
using ContextAction2Fn = char (*)(ContextActor*, InteractionEventRec*);
int RegisterContextActions();
ContextAction2Fn ContextAction2_Lookup(int address);

} // namespace guild::sim
