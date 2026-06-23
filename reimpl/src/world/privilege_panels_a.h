#pragma once
// privilege_panels_a.h — the guild-office PRIVILEGE action panels, SET A (the
// social/personnel actions), reconstructed 1:1 from gilde.exe.
//
// These are the VIBE_Privilege_Panel* leaves the office context-menu dispatch
// (interaction_handlers / contextaction2, routed through g_privilegeHook /
// SetPrivilegeLeafHook) invokes when the player picks a privilege action. In the
// original each is a GUI frame-loop dialog: it builds a Form (VIBE_GameTick_Finalize
// / VIBE_Form_*), spins VIBE_GameLogic_RunFrameLoop, and on a confirm/cancel button
// (dword_75BF38 == 1210 / 1155, or a child-object click == dword_62D22C) runs the
// action — which mutates person/family/relation state and enqueues lockstep network
// commands — then writes the done-state code dword_631614 and returns a verdict.
//
// SET A panels (this module):
//   0x563000 VIBE_Privilege_PanelGenerateHatred
//   0x565304 VIBE_Privilege_PanelChangeProfession
//   0x5639f4 VIBE_Privilege_PanelExpelWorker
//   0x560f14 VIBE_Privilege_PanelBlackmail  + 0x560c1c BlackmailConfirm
//   0x563f14 VIBE_Privilege_PanelMakePeace
//   0x5643e8 VIBE_Privilege_PanelConvert
//   0x563614 VIBE_Privilege_PanelInterrogation
//   0x560500 VIBE_Privilege_PanelMedicus
//   0x5608b0 VIBE_Privilege_PanelDivorce
//   0x5647c8 VIBE_Privilege_PanelApology  + 0x5627cc CharmConfirm
//
// FIDELITY MODEL (per the project's office_recon_privilege.h precedent): the Form/
// HUD/Vulkan window machinery, the lockstep command queue (VIBE_Command_*), the
// Text/Dialog rendering and the live word_12CE910 person array are coupled engine
// leaves. They are surfaced through a single mockable PrivilegePanelHooks vtable so
// the LOAD-BEARING, TESTABLE state effects each panel produces are reconstructed
// EXACTLY 1:1 and golden-pinnable:
//   * the cost computation (wealth * flt_table, ConvertX-truncated to int),
//   * the RNG draw ORDER and modulus per panel (VIBE_Math_RandomModulo),
//   * the relation-matrix delta math (GenerateHatred/MakePeace),
//   * the exact sequence + arguments of the emitted commands,
//   * the success roll (Blackmail: RandomModulo(8) <= matchCount),
//   * the convert sweep over the 768-person array (kind/religion gates, +132 charm
//     vs +0x7E roll),
//   * every numeric return code and dword_631614 state value.
// The button event each frame is delivered by hooks->NextButton() (the original's
// dword_75BF38 latched inside the frame loop); a null/loop-exit ends the dialog.
//
// Reuses (no ODR redefinition): guild::util::ConvertX (truncate), RandomModulo;
// the pure predicates in world/privilege.h + world/office_recon_privilege.h stay
// authoritative for the gate idioms. Cost-table float constants are decoded here.
#include "guild/common/types.h"

namespace guild::world {

// ===========================================================================
// Cost-table constants (decoded from gilde.exe .rdata, little-endian IEEE754).
// Each panel multiplies a person's total wealth by its factor then ConvertX-
// truncates to an int cost.
// ===========================================================================
constexpr float  kPrivMedicusWealthFactor  = 0.02f;  // flt_624B48  (2%)
constexpr float  kPrivConvertWealthFactor  = 0.06f;  // flt_624D34  (6%)
constexpr float  kPrivHatredWealthFactor   = 0.02f;  // flt_624CBC  (2%)
constexpr double kPrivDivorceWealthFactorA = 0.08;   // dbl_624B64  (8%)
constexpr double kPrivDivorceWealthFactorB = 0.04;   // dbl_624B6C  (4%)
constexpr double kPrivDivorceAffordRatio   = 0.12;   // dbl_624B74  (12%)

// ===========================================================================
// Person/actor record field offsets the panels read/write (byte offsets into the
// 536-byte person record == word_12CE910 stride; matches ContextActor layout).
// ===========================================================================
constexpr int kPersonStride        = 536;   // word_12CE910 stride (v13 += 536)
constexpr int kPersonArrayCount     = 768;   // 411648 / 536; convert/expel sweep
constexpr int kPersonArrayByteSpan  = 411648;// 768 * 536 (the do/while bound)

// ===========================================================================
// A minimal value-view of the person record the panels touch. The live game owns
// the real record; this view models exactly the fields SET A reads/writes so the
// state effects are testable. Field names carry the original +offset.
// ===========================================================================
struct PrivPerson {
    u16 id        = 0;      // +0x00  *(_WORD*)a1  (person id)
    i32 handle    = 0;      // +0x04  *(a1+4)/(a1+1 dword) entity handle
    u8  kind      = 0;      // +0x02  6/7 office-holder, else concrete person
    u8  gender    = 0;      // +0x09  *(a1+9) gender/married flag
    u8  rank      = 0;      // +0x0D  *(a1+13) rank byte
    i32 office404 = 0;      // +0x194 (+404) office level (*(int*)(a1+404)/[101])
    i32 flags456  = 0;      // +0x1C8 (+456) flag dword (bit8 0x100 holds office)
    u8  flag457   = 0;      // +0x1C9 (+457) flag byte (bit0,bit1=0x2,sign bit)
    u8  religion  = 0;      // high byte of +9 area (dword_12CE919>>24 religion id)
    u8  charm132  = 0;      // +0x84 (+132) charm/persuasion stat
    u8  byte994   = 0;      // +? byte_12CE994 convert-resistance stat
    i32 partnerId = 0;      // spouse/partner id (divorce/family link)
};

// ===========================================================================
// Hook vtable — the coupled engine leaves each panel calls. Defaults are inert
// (record-into-trace) so the headless build + tests need no Form/Command/Vulkan.
// Production wires the real leaves (see InstallProductionPrivilegeHooksA()).
// ===========================================================================

// One emitted lockstep command (the VIBE_Command_* calls), recorded in order so
// tests can assert the exact command sequence + args each confirm produces.
struct PrivCommand {
    enum Op {
        kBuildOp90,        // VIBE_Command_RequestBuildOp90(handle, delta)  (skill cost)
        kBuildOp72,        // VIBE_Command_RequestBuildOp72(handle, newProf)
        kCoord27,          // VIBE_Command_QueueRequestCoord27(a, b, value) (relation)
        kEnqueueCmd15,     // VIBE_Command_EnqueueCmd15(dst, src, amount, cur) (money)
        kArgs25,           // VIBE_Command_QueueRequestArgs25(handle, field, val, sz, 0)
        kPair33,           // VIBE_Command_QueueRequestPair33(handle, 1)   (dismiss)
        kDeltaField,       // VIBE_Command_AppendDeltaField/State22 (family link write)
        kSlotReset28,      // VIBE_Command_QueueRequestSlotReset28 (blackmail jail rec)
        kRivalPairs,       // VIBE_He_RequestRivalEntityPairs (apology)
        kBuildOp90Neg2,    // VIBE_Command_RequestBuildOp90(handle,-2) blackmail-fail
    };
    int op = 0;
    i32 a = 0, b = 0, c = 0, d = 0; // up to four args (op-specific)
};

struct PrivilegePanelTrace {
    PrivCommand cmds[64];
    int         cmdCount = 0;
    int         lastFormScene = 0;   // GameTick_Finalize scene id (string base)
    int         lastMessageId = 0;   // last RenderFormattedMessage/MessageBox id
    int         lastEntityMsgId = 0; // last He_SendEntityMessage notification id (1418)
    int         doneState = 0;       // dword_631614 written
    int         cost = 0;            // last computed ConvertX cost
    void Reset() { cmdCount = 0; lastFormScene = 0; lastMessageId = 0;
                   lastEntityMsgId = 0; doneState = 0; cost = 0; }
};

// The frame-loop button source. Each call returns the next dword_75BF38 value:
//   1210 = OK/confirm, 1155 = cancel, a positive child-object id == a clicked
//   widget (matched against the panel's confirm/cancel child ids), or
//   kPrivLoopExit to break the frame loop (RunFrameLoop returned 0 / window closed).
constexpr int kPrivLoopExit  = -2;   // RunFrameLoop -> 0 (close window)
constexpr int kPrivLoopIdle  = -1;   // dword_75BF38 == -1 (no button this frame)
constexpr int kPrivBtnOk     = 1210; // confirm
constexpr int kPrivBtnCancelId = 1155; // cancel

struct PrivilegePanelHooks {
    PrivilegePanelTrace* trace = nullptr;

    // Person record resolution (VIBE_Person_FindRecordById @0x58bc6c). Returns a
    // PrivPerson view, or null if id resolves to nothing.
    const PrivPerson* (*findRecord)(i32 id, void* ctx) = nullptr;

    // VIBE_Person_ComputeTotalWealth @0x591f7c — total wealth of a person record.
    i32 (*computeTotalWealth)(const PrivPerson* p, void* ctx) = nullptr;
    // VIBE_Person_SumCurrencyHeld @0x59152c — liquid currency held (afford gate).
    i32 (*sumCurrencyHeld)(const PrivPerson* p, void* ctx) = nullptr;

    // VIBE_Math_RandomModulo @0x58b89c — RNG draw in 0..n-1 (drives every roll).
    int (*randomModulo)(u16 n, void* ctx) = nullptr;

    // VIBE_Relation_LookupMatrixEntry @0x5942fc — relationship value a->b.
    int (*relationEntry)(u16 a, u16 b, void* ctx) = nullptr;

    // VIBE_Dialog_CheckSkillRequirement @0x4ad594 — actor has the privilege skill
    // (cost level). VIBE_Dialog_CheckResourceAmount @0x4ad62c — actor can pay cost.
    bool (*checkSkill)(const PrivPerson* a, int cost, void* ctx) = nullptr;
    bool (*checkResource)(int cost, u8 currency, void* ctx) = nullptr;

    // VIBE_He_FindMatchingEntityIds @0x4c4388 — count of incriminating-evidence
    // entities for (subjectHandle, actorId). Drives the blackmail success roll.
    int (*findMatchingEntityIds)(i32 subjectHandle, u16 actorId, void* ctx) = nullptr;

    // VIBE_Amt_RunOfficeOverviewWindow @0x5575c8 — modal office-holder picker.
    // Returns the picked person (or null on cancel).
    const PrivPerson* (*pickOfficeHolder)(int promptMsgId, void* ctx) = nullptr;

    // The frame-loop button source (see kPrivBtn* above). ctx is the panel's.
    int (*nextButton)(void* ctx) = nullptr;

    // VIBE_He_SendEntityMessage @0x4c5c54 — fire notification (id 1418) to an
    // office-holder kind 6/7 entity (recorded as lastEntityMsgId).
    void (*sendEntityMessage)(i32 entityHandle, int notifyId, void* ctx) = nullptr;

    // VIBE_Dialog_ShowMessageBox @0x4ad6f0 — modal confirm; returns true if OK
    // pressed (used by ChangeProfession's final confirm).
    bool (*confirmBox)(int msgId, void* ctx) = nullptr;

    void* ctx = nullptr;
};

// ===========================================================================
// Cost helpers (the wealth * factor, ConvertX-truncate-to-int idiom). Exposed for
// golden tests; the panels use them internally.
// ===========================================================================
int PrivCostFromWealth(i32 wealth, float factor);   // (int)trunc(wealth*factor)
int PrivCostFromWealth(i32 wealth, double factor);   // double variant (divorce)

// ===========================================================================
// The ten SET-A panels. Each returns the original's verdict byte (signed char):
//   passive (non-office-holder) path: 16/17/32/34/96/-127 action codes;
//   GUI (office-holder kind 6) path: the dialog result (0/1/2/16/-127 etc.) after
//   the frame loop. `actor` is a1 (the acting privilege-holder); `ev` carries the
//   target/drag-source ids (a2: targetId @+532, partnerId @+536, dragMode @+0).
//   `hooks` supplies the coupled leaves.
// ===========================================================================
struct PrivEvent {
    u8  mode = 0;        // *(_BYTE*)a2 dispatch mode (3 activate / 4 drag)
    i32 targetId = 0;    // *(a2+532) primary target person id
    i32 partnerId = 0;   // *(a2+536) secondary (peace/divorce partner) id
    i32 dragField = 0;   // *(a2+4) numeric field (cost level / lawsuit type)
    const PrivPerson* dragSource = nullptr; // *(a2+540) drag-source record
};

char PrivilegePanelGenerateHatred(const PrivPerson* actor, const PrivEvent* ev,
                                  PrivilegePanelHooks* h);                  // 0x563000
char PrivilegePanelChangeProfession(const PrivPerson* actor, PrivilegePanelHooks* h);// 0x565304
char PrivilegePanelExpelWorker(const PrivPerson* actor, const PrivEvent* ev,
                               PrivilegePanelHooks* h);                     // 0x5639f4
char PrivilegePanelBlackmail(const PrivPerson* actor, const PrivEvent* ev,
                             PrivilegePanelHooks* h);                       // 0x560f14
int  PrivilegeBlackmailConfirm(const PrivPerson* actor, const PrivPerson* subject,
                               PrivilegePanelHooks* h);                     // 0x560c1c
char PrivilegePanelMakePeace(const PrivPerson* actor, const PrivEvent* ev,
                             PrivilegePanelHooks* h);                       // 0x563f14
char PrivilegePanelConvert(const PrivPerson* actor, const PrivEvent* ev,
                           PrivilegePanelHooks* h,
                           const PrivPerson* people, int peopleCount);      // 0x5643e8
char PrivilegePanelInterrogation(const PrivPerson* actor, const PrivEvent* ev,
                                 PrivilegePanelHooks* h);                   // 0x563614
char PrivilegePanelMedicus(const PrivPerson* actor, PrivilegePanelHooks* h);// 0x560500
char PrivilegePanelDivorce(const PrivPerson* actor, PrivilegePanelHooks* h);// 0x5608b0
char PrivilegePanelApology(const PrivPerson* actor, const PrivEvent* ev,
                           PrivilegePanelHooks* h);                         // 0x5647c8
int  PrivilegeCharmConfirm(const PrivPerson* actor, const PrivEvent* ev,
                           PrivilegePanelHooks* h);                         // 0x5627cc

// ===========================================================================
// Dispatcher wiring. Routes a SET-A leaf-id (the absolute gilde.exe address used
// as the opaque id in interaction_handlers' g_leafTrace) to the reconstructed
// panel. Returns the panel verdict. Unknown ids return 0.
// ===========================================================================
int PrivilegeDispatchPanelA(int leafId, const PrivPerson* actor,
                            const PrivEvent* ev, PrivilegePanelHooks* h);

} // namespace guild::world
