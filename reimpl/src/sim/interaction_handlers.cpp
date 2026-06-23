#include "sim/interaction_handlers.h"

#include <cstdint>

#include "sim/entity.h"

// Faithful 1:1 port of the interaction RULES cores from gilde.exe. See the header
// for the per-function provenance and the recovered record/event layouts.
//
// Conventions used throughout:
//   * `result` (al) is the previous evaluator's verdict; a nonzero value makes an
//     Eval handler short-circuit to AiMethodStubReturnZero (returns 0).
//   * `armed` (bl) is the trigger/phase byte.
//   * ContextAction handlers read the event `mode` byte (*a2): 1 tooltip, 2/4 drag,
//     3 activate. Verdicts: 1 reject, 2 handled, 4 next-tier, 9 busy, 10 drag-handled.
//   * On activate the original calls a VIBE_Privilege_* leaf; here that goes through
//     g_privilegeHook (default no-op, recorded in g_leafTrace). On tooltip mode it
//     calls VIBE_Text_RenderFormattedMessage; we record the string id only.

namespace guild::sim {

// --- recovered float/double constants ---------------------------------------
// flt_61A4C0 = 0x3EE147AE; flt_61A4C4 = 0x467A0000 (EvalUseDoor cash gate).
static constexpr float kDoorCashFactor = 0.44f;       // flt_61A4C0
static constexpr float kDoorCashThreshold = 16000.0f; // flt_61A4C4
// byte_6477A1 — current-player / default currency index (cold IDB reads 0).
static constexpr u8 kDefaultCurrencyIndex = 0;        // byte_6477A1

// ===========================================================================
// Leaf hooks + trace.
// ===========================================================================
InteractionLeafTrace g_leafTrace = {};

static int DefaultPrivilege(int leafId, ContextActor*, InteractionEventRec*) {
    g_leafTrace.privilegeLeaf = leafId;
    g_leafTrace.privilegeReturn = 0;
    return 0;
}
static void DefaultCommandEmit(const char* tag, int actionCode) {
    g_leafTrace.commandTag = tag;
    g_leafTrace.commandAction = actionCode;
}
static int DefaultCurrency(ContextActor*, u8) { return 0; }

static PrivilegeLeafFn g_privilegeHook = DefaultPrivilege;
static CommandEmitFn   g_commandHook   = DefaultCommandEmit;
static CurrencyAmountFn g_currencyHook = DefaultCurrency;

void ResetInteractionLeafTrace() { g_leafTrace = {}; }
void SetPrivilegeLeafHook(PrivilegeLeafFn fn) { g_privilegeHook = fn ? fn : DefaultPrivilege; }
PrivilegeLeafFn GetPrivilegeLeafHook() { return g_privilegeHook; }
void SetCommandEmitHook(CommandEmitFn fn) { g_commandHook = fn ? fn : DefaultCommandEmit; }
void SetCurrencyHook(CurrencyAmountFn fn) { g_currencyHook = fn ? fn : DefaultCurrency; }

// Calls a Privilege leaf and returns its result (low bits the callers OR in).
static int Privilege(int leafId, ContextActor* a, InteractionEventRec* ev) {
    int r = g_privilegeHook(leafId, a, ev);
    g_leafTrace.privilegeReturn = r;
    return r;
}
// Records a tooltip render (mode 1). `stringId` is the message id the binary passes
// to VIBE_Text_RenderFormattedMessage(ev->tooltip, "%s", stringId).
static void RenderTooltip(InteractionEventRec*, int stringId) {
    g_leafTrace.tooltipStringId = stringId;
}

// Public wrappers (exported for contextaction2.cpp — same hook/trace state).
int InvokePrivilegeLeaf(int leafId, ContextActor* a, InteractionEventRec* ev) {
    return Privilege(leafId, a, ev);
}
void RecordTooltip(InteractionEventRec* ev, int stringId) {
    RenderTooltip(ev, stringId);
}

// ===========================================================================
// Eval handlers.
// ===========================================================================

// gilde.exe 0x469da8 — VIBE_AiMethod_StubReturnZero (__stdcall).
char AiMethodStubReturnZero(int /*ctx*/) {
    return 0;
}

// gilde.exe 0x469dcc — VIBE_Interaction_EvalReturnEight.
char EvalReturnEight(char result, char armed, int ctx) {
    if (result)
        return AiMethodStubReturnZero(ctx);
    if (!armed)
        return 8;
    return result;
}

// gilde.exe 0x469de0 — VIBE_Interaction_EvalReturnBoolNotArmed.
char EvalReturnBoolNotArmed(char result, char armed, int ctx) {
    if (result)
        return AiMethodStubReturnZero(ctx);
    return static_cast<char>(armed == 0);
}

// gilde.exe 0x469df4 — VIBE_Interaction_EvalReturnTwo.
char EvalReturnTwo(char result, char armed, int ctx) {
    if (result)
        return AiMethodStubReturnZero(ctx);
    if (!armed)
        return 2;
    return result;
}

// gilde.exe 0x469e08 — VIBE_Interaction_EvalReturnThree.
char EvalReturnThree(char result, char armed, int ctx) {
    if (result)
        return AiMethodStubReturnZero(ctx);
    if (!armed)
        return 3;
    return result;
}

// gilde.exe 0x46b8b0 — VIBE_Interaction_EvalRejectStub.
char EvalRejectStub() { return 0; }

// gilde.exe 0x46ebfc — VIBE_Interaction_EvalRestStub.
char EvalRestStub() { return 26; }

// gilde.exe 0x470a18 — VIBE_Interaction_EvalActionCode35.
char EvalActionCode35(const InteractionEventRec* ev, char armed, int ctx) {
    if (ev->mode == 1 && armed == 3)
        return 35;
    return AiMethodStubReturnZero(ctx);
}

// gilde.exe 0x470be8 — VIBE_Interaction_EvalActionCode36.
char EvalActionCode36(const InteractionEventRec* ev, char armed, int ctx) {
    if (ev->mode == 1 && armed == 4)
        return 36;
    return AiMethodStubReturnZero(ctx);
}

// gilde.exe 0x46e8a0 — VIBE_Interaction_EvalUseDoor.
//   armed>1            → 0
//   armed==1 && (ev.mode!=13 || ev.targetId!=2) → 0   (must already be a door event)
//   actor.rank >= 2    → 0
//   (actor.flag457 & 0x40)==0 → 0
//   cash = GetCurrency(actor); cash<0 || cash*0.44 < 16000.0 → 0
//   else ev.mode=13, ev.targetId=2, ev.param=1; return 25.
char EvalUseDoor(ContextActor* actor, InteractionEventRec* ev, u8 armed, int /*ctx*/) {
    if (armed > 1)
        return 0;
    if (armed == 1 && (ev->mode != 13 || ev->targetId != 2))
        return 0;
    if (actor->rank >= 2)
        return 0;
    if ((actor->flag457 & 0x40) == 0)
        return 0;
    int cash = g_currencyHook(actor, kDefaultCurrencyIndex);
    if (cash < 0 ||
        static_cast<double>(cash) * kDoorCashFactor < kDoorCashThreshold)
        return 0;
    ev->mode = 13;
    ev->targetId = 2;
    ev->param = 1;
    return 25;
}

// ===========================================================================
// ContextAction executors.
//
// The shared "single-target" shape (modes 3/1 only):
//   if mode != 3 && mode != 1: reject (1)
//   <eligibility gate on the actor record>: reject (1) [or 4 = next-tier]
//   if mode == 3 (&& usually kind==6): invoke Privilege leaf
//   else if mode == 1 && kind == 6: render tooltip
//   return 2.
// The "drag" shape additionally handles modes 4/2 by reading ev->dragSource.
// ===========================================================================

// gilde.exe 0x56efb0 — VIBE_ContextAction_AssignTask.
char ContextAssignTask(ContextActor* actor, InteractionEventRec* ev) {
    if (ev->mode != 3 && ev->mode != 1)
        return 1;
    if (actor->assocPersonId != -1) {
        Person* rec = PersonFindRecordById(actor->assocPersonId);
        // !rec || (rec.isPlayer && rec.kind != 15) → reject
        if (!rec || (rec->isPlayer && rec->kind != 15))
            return 1;
    }
    if (ev->mode == 3) {
        // VIBE_Recruit_RunRecruitmentOfferWindow(actor) | 2
        int r = Privilege(kPrivChangeProfession /*recruit window leaf*/, actor, ev);
        return static_cast<char>(r | 2);
    }
    if (ev->mode != 1 || actor->kind != 6)
        return 2;
    // He-handler lookup + RenderFormattedMessage tooltip (string 6370 / "%s").
    RenderTooltip(ev, 0x18E1);
    return 2;
}

// gilde.exe 0x56f0ac — VIBE_ContextAction_PromoteRank (rank < 3 to be promotable).
char ContextPromoteRank(ContextActor* actor, InteractionEventRec* ev) {
    if (ev->mode != 3 && ev->mode != 1)
        return 1;
    if (actor->rank >= 3)
        return 4; // already high enough → advance to next tier handler
    if (ev->mode == 3 && actor->kind == 6) {
        Privilege(kPrivShowDialog, actor, ev);
        return 2;
    }
    if (ev->mode != 1 || actor->kind != 6)
        return 2;
    RenderTooltip(ev, 0x1933);
    return 2;
}

// gilde.exe 0x56f15c — VIBE_ContextAction_OpenInventory (kind 5/6).
char ContextOpenInventory(ContextActor* actor, InteractionEventRec* ev) {
    if (ev->mode != 3 && ev->mode != 1)
        return 1;
    u8 k = actor->kind;
    if (k != 6 && k != 5)
        return 1;
    if (ev->mode == 3 && actor->kind == 6) {
        int r = Privilege(kPrivChangeProfession, actor, ev);
        return static_cast<char>(r | 2);
    }
    if (ev->mode != 1 || actor->kind != 6)
        return 2;
    RenderTooltip(ev, 0x1935);
    return 2;
}

// gilde.exe 0x56f1c0 — VIBE_ContextAction_ToggleFollow.
//   reject if (flag456 & 0x40) || (score36 >= -20.0 && (statusBits44 & ~0xF)==0)
char ContextToggleFollow(ContextActor* actor, InteractionEventRec* ev) {
    if (ev->mode != 3 && ev->mode != 1)
        return 1;
    if ((actor->flag456 & 0x40) != 0 ||
        (static_cast<double>(actor->score36) >= -20.0 &&
         (static_cast<u32>(actor->statusBits44) & 0xFFFFFFF0u) == 0))
        return 1;
    if (ev->mode == 3)
        return static_cast<char>(Privilege(kPrivMedicus, actor, ev));
    if (ev->mode == 1 && actor->kind == 6)
        RenderTooltip(ev, 0x193A);
    return 2;
}

// gilde.exe 0x56f234 — VIBE_ContextAction_AssignGuard (profession != 13; He lookup).
char ContextAssignGuard(ContextActor* actor, InteractionEventRec* ev) {
    if ((ev->mode != 3 && ev->mode != 1) || actor->profession == 13)
        return 1;
    // The original walks the He handler list for a record whose +43 dword matches
    // ev->targetId (+4); if a match exists the action is rejected (1). With no He
    // backend wired the list is empty (no match), so it falls through to the panel.
    if (ev->mode == 3) {
        Privilege(kPrivEvidenceReview, actor, ev);
        return 2;
    }
    if (ev->mode == 1 && actor->kind == 6) {
        RenderTooltip(ev, 0x1944);
        return 2;
    }
    return 2;
}

// gilde.exe 0x56f2c0 — VIBE_ContextAction_ToggleFlag457 (flag457 & 1 blocks).
char ContextToggleFlag457(ContextActor* actor, InteractionEventRec* ev) {
    if ((ev->mode != 3 && ev->mode != 1) || (actor->flag457 & 1) != 0)
        return 1;
    if (ev->mode == 3) {
        Privilege(kPrivBlackmail, actor, ev);
        return 2;
    }
    if (ev->mode != 1 || actor->kind != 6)
        return 2;
    RenderTooltip(ev, 0x1954);
    return 2;
}

// Shared rank-tier "Train" body. `wantRank` is the exact rank the tier targets and
// `exact` distinguishes the ==N tiers (return 4 if above, 1 if below) from the >=N
// tiers (return 1 if below). `tooltipId` is the mode-1 string. On activate it
// renders a dialog via ShowDialog (the string-base 272/279 selection by gender is a
// pure UI detail folded into the leaf).
static char ContextTrainTier(ContextActor* actor, InteractionEventRec* ev,
                             u8 wantRank, bool exact, int tooltipId) {
    if (ev->mode != 3 && ev->mode != 1)
        return 1;
    u8 r = actor->rank;
    if (exact) {
        if (r > wantRank)
            return 4;
        if (r < wantRank)
            return 1;
    } else {
        if (r < wantRank)
            return 1;
    }
    if (ev->mode == 3 && actor->kind == 6) {
        Privilege(kPrivShowDialog, actor, ev);
    } else if (ev->mode == 1 && actor->kind == 6) {
        RenderTooltip(ev, tooltipId);
        return 2;
    }
    return 2;
}

// gilde.exe 0x56f314 — VIBE_ContextAction_TrainRank3A (rank == 3).
char ContextTrainRank3A(ContextActor* actor, InteractionEventRec* ev) {
    return ContextTrainTier(actor, ev, 3, true, 0x195E);
}
// gilde.exe 0x56f468 — VIBE_ContextAction_TrainRank4A (rank == 4).
char ContextTrainRank4A(ContextActor* actor, InteractionEventRec* ev) {
    return ContextTrainTier(actor, ev, 4, true, 0x1984);
}
// gilde.exe 0x56f670 — VIBE_ContextAction_TrainRank5A (rank >= 5).
char ContextTrainRank5A(ContextActor* actor, InteractionEventRec* ev) {
    return ContextTrainTier(actor, ev, 5, false, 0x198A);
}

// gilde.exe 0x56f3c0 — VIBE_ContextAction_DemoteIfRank3 (rank >= 3).
char ContextDemoteIfRank3(ContextActor* actor, InteractionEventRec* ev) {
    if ((ev->mode != 3 && ev->mode != 1) || actor->rank < 3)
        return 1;
    if (ev->mode == 3) {
        Privilege(kPrivSendBuildCmd, actor, ev);
        return 2;
    }
    if (ev->mode != 1 || actor->kind != 6)
        return 2;
    RenderTooltip(ev, 0x1960);
    return 2;
}

// gilde.exe 0x56f410 — VIBE_ContextAction_EquipIfRank3 (rank >= 3 && !(457&4)).
char ContextEquipIfRank3(ContextActor* actor, InteractionEventRec* ev) {
    if ((ev->mode != 3 && ev->mode != 1) || actor->rank < 3 ||
        (actor->flag457 & 4) != 0)
        return 1;
    if (ev->mode == 3) {
        Privilege(kPrivSendSimpleCmd, actor, ev);
        return 2;
    }
    if (ev->mode != 1 || actor->kind != 6)
        return 2;
    RenderTooltip(ev, 0x1965);
    return 2;
}

// gilde.exe 0x56f7b0 — VIBE_ContextAction_AssignToSlot.
//   rank >= 5; a free person slot must exist; assocPersonId resolvable + isPlayer.
char ContextAssignToSlot(ContextActor* actor, InteractionEventRec* ev) {
    if (ev->mode != 3 && ev->mode != 1)
        return 1;
    if (actor->rank < 5)
        return 1;
    // Count leading free slots in the person array until the first non-free marker
    // (matches the binary's loop over word_12CE910 stride 268 words == 536 bytes).
    int freeCount = 0;
    for (int i = 0; i < kPersonCapacity; ++i) {
        if (g_persons[i].marker != -1)
            break;
        ++freeCount;
    }
    if (actor->assocPersonId == -1)
        return 1;
    if (freeCount >= kPersonCapacity)
        return 1;
    Person* rec = PersonFindRecordById(actor->assocPersonId);
    if (!rec || !rec->isPlayer)
        return 1;
    if (ev->mode == 3)
        return static_cast<char>(Privilege(kPrivDivorce, actor, ev));
    if (ev->mode == 1 && actor->kind == 6)
        RenderTooltip(ev, 0x198E);
    return 2;
}

// gilde.exe 0x56f9a4 — VIBE_ContextAction_ShowDualProfession (prof || prof2).
char ContextShowDualProfession(ContextActor* actor, InteractionEventRec* ev) {
    if ((ev->mode != 3 && ev->mode != 1) ||
        (actor->profession == 0 && actor->profession2 == 0))
        return 1;
    if (ev->mode == 3 && actor->kind == 6) {
        Privilege(kPrivShowDialog, actor, ev);
        return 2;
    }
    if (ev->mode != 1 || actor->kind != 6)
        return 2;
    RenderTooltip(ev, 0x1996);
    return 2;
}

// gilde.exe 0x56faf0 — VIBE_ContextAction_StartWorkTask.
//   activate path (mode 3/1): profession!=0; busy(457&2)→9; (458&0x20)→1;
//   on mode3 result = RemoveFromOffice; return result|2.
//   drag path (mode 4/2): (457&1) && dragSource valid && src.profession && !(src.458&0x20)
char ContextStartWorkTask(ContextActor* actor, InteractionEventRec* ev) {
    ev->tooltip[0] = 0; // a2[20] = 0
    char v3 = 0;
    if (ev->mode == 3 || ev->mode == 1) {
        if (actor->profession == 0)
            return 1;
        if (actor->kind == 6)
            RenderTooltip(ev, 0x1998);
        if ((actor->flag457 & 2) != 0)
            return 9;
        if ((actor->flag458 & 0x20) != 0)
            return 1;
        if (ev->mode == 3)
            v3 = static_cast<char>(Privilege(kPrivRemoveFromOffice, actor, ev));
        return static_cast<char>(v3 | 2);
    }
    if ((ev->mode == 4 || ev->mode == 2) && (actor->flag457 & 1) != 0) {
        ContextActor* src = ev->dragSource;
        if (src && src->profession != 0 && (src->flag458 & 0x20) == 0) {
            if (ev->mode == 4) {
                v3 = static_cast<char>(Privilege(kPrivRemoveFromOffice, actor, ev));
                return static_cast<char>(v3 | 0xA);
            }
            if (actor->kind != 6)
                return static_cast<char>(v3 | 0xA);
            RenderTooltip(ev, 0x1998);
            return 10;
        }
    }
    return 1;
}

// Shared "profession-menu" body: gate on profession byte == one of `accept`, render
// a dialog (mode 3, kind 6) or a tooltip (mode 1, kind 6).
static char ContextProfessionMenu(ContextActor* actor, InteractionEventRec* ev,
                                  const u8* accept, int acceptCount, int tooltipId) {
    if (ev->mode != 3 && ev->mode != 1)
        return 1;
    bool ok = false;
    for (int i = 0; i < acceptCount; ++i)
        if (actor->profession == accept[i]) { ok = true; break; }
    if (!ok)
        return 1;
    if (ev->mode == 3 && actor->kind == 6) {
        Privilege(kPrivShowDialog, actor, ev);
    } else if (ev->mode == 1 && actor->kind == 6) {
        RenderTooltip(ev, tooltipId);
        return 2;
    }
    return 2;
}

// gilde.exe 0x56fe98 — VIBE_ContextAction_ProfessionMenu11 (profession == 11).
char ContextProfessionMenu11(ContextActor* actor, InteractionEventRec* ev) {
    static const u8 acc[] = {11};
    return ContextProfessionMenu(actor, ev, acc, 1, 0x199F);
}
// gilde.exe 0x56ff58 — VIBE_ContextAction_ProfessionMenuGuard (27/15/21/13).
char ContextProfessionMenuGuard(ContextActor* actor, InteractionEventRec* ev) {
    static const u8 acc[] = {27, 15, 21, 13};
    return ContextProfessionMenu(actor, ev, acc, 4, 0x19A1);
}

// Shared "command-by-profession + drag" body (the AttackOrSteal / TalkOrSocialize /
// CommandPatrol / CommandMultiType / CommandType22Or25 / CommandType27 family).
//   activate (mode 3/1): if kind==6 render tooltip; if profession in `accept`:
//        busy(457&2)→9; on mode3 invoke leaf; return 2. else reject(1).
//   drag (mode 4/2): (457&1) && dragSource valid && src.profession in `accept`:
//        on mode4 invoke leaf, return 10; else (kind6) render tooltip; return 10.
//        else reject(1).
static char ContextCommandByProfession(ContextActor* actor, InteractionEventRec* ev,
                                       const u8* accept, int acceptCount,
                                       int leafId, int tooltipId) {
    u8 mode = ev->mode;
    if (mode == 3 || mode == 1) {
        if (actor->kind == 6)
            RenderTooltip(ev, tooltipId);
        bool ok = false;
        for (int i = 0; i < acceptCount; ++i)
            if (actor->profession == accept[i]) { ok = true; break; }
        if (!ok)
            return 1;
        if ((actor->flag457 & 2) != 0)
            return 9;
        if (ev->mode == 3)
            Privilege(leafId, actor, ev);
        return 2;
    }
    if (mode == 4 || mode == 2) {
        if ((actor->flag457 & 1) == 0)
            return 1;
        ContextActor* src = ev->dragSource;
        if (!src)
            return 1;
        bool ok = false;
        for (int i = 0; i < acceptCount; ++i)
            if (src->profession == accept[i]) { ok = true; break; }
        if (!ok)
            return 1;
        if (ev->mode == 4) {
            Privilege(leafId, actor, ev);
            return 10;
        }
        if (actor->kind != 6)
            return 10;
        RenderTooltip(ev, tooltipId);
        return 10;
    }
    return 1;
}

// gilde.exe 0x5703f8 — VIBE_ContextAction_AttackOrSteal (prof 22/25/18).
char ContextAttackOrSteal(ContextActor* actor, InteractionEventRec* ev) {
    static const u8 acc[] = {22, 25, 18};
    return ContextCommandByProfession(actor, ev, acc, 3, kPrivCharmConfirm, 0x19AD);
}
// gilde.exe 0x5704d4 — VIBE_ContextAction_TalkOrSocialize (prof 19/20/24/26).
char ContextTalkOrSocialize(ContextActor* actor, InteractionEventRec* ev) {
    static const u8 acc[] = {19, 20, 24, 26};
    return ContextCommandByProfession(actor, ev, acc, 4, kPrivCounterEspionage, 0x19B6);
}
// gilde.exe 0x570a1c — VIBE_ContextAction_CommandPatrol (prof 15/21/27).
char ContextCommandPatrol(ContextActor* actor, InteractionEventRec* ev) {
    static const u8 acc[] = {15, 21, 27};
    return ContextCommandByProfession(actor, ev, acc, 3, kPrivSwapSeats, 0x19C8);
}
// gilde.exe 0x570c78 — VIBE_ContextAction_CommandMultiType (prof 23/19/20/13).
char ContextCommandMultiType(ContextActor* actor, InteractionEventRec* ev) {
    static const u8 acc[] = {23, 19, 20, 13};
    return ContextCommandByProfession(actor, ev, acc, 4, kPrivInterrogation, 0x19D6);
}
// gilde.exe 0x570d60 — VIBE_ContextAction_CommandType22Or25 (prof 22/25).
char ContextCommandType22Or25(ContextActor* actor, InteractionEventRec* ev) {
    static const u8 acc[] = {22, 25};
    return ContextCommandByProfession(actor, ev, acc, 2, kPrivExpelWorker, 0x19DB);
}
// gilde.exe 0x570ef8 — VIBE_ContextAction_CommandType27 (prof 27).
char ContextCommandType27(ContextActor* actor, InteractionEventRec* ev) {
    static const u8 acc[] = {27};
    return ContextCommandByProfession(actor, ev, acc, 1, kPrivMakePeace, 0x19E3);
}

// gilde.exe 0x5707f0 — VIBE_ContextAction_StartGuildTask (prof==14 + 458&0x40 gate).
//   This is the CommandByProfession shape but with an extra (458&0x40) block and the
//   leaf result OR'd into the verdict (like StartWorkTask).
char ContextStartGuildTask(ContextActor* actor, InteractionEventRec* ev) {
    u8 mode = ev->mode;
    char v4 = 0;
    if (mode == 3 || mode == 1) {
        if (actor->kind == 6)
            RenderTooltip(ev, 0x19C1);
        if (actor->profession != 14)
            return 1;
        if ((actor->flag457 & 2) != 0)
            return 9;
        if ((actor->flag458 & 0x40) != 0)
            return 1;
        if (ev->mode == 3)
            v4 = static_cast<char>(Privilege(kPrivEmbezzlement, actor, ev));
        return static_cast<char>(v4 | 2);
    }
    if ((mode == 4 || mode == 2) && (actor->flag457 & 1) != 0) {
        ContextActor* src = ev->dragSource;
        if (src && src->profession == 14 && (src->flag458 & 0x40) == 0) {
            if (ev->mode == 4) {
                v4 = static_cast<char>(Privilege(kPrivEmbezzlement, actor, ev));
                return static_cast<char>(v4 | 0xA);
            }
            if (actor->kind != 6)
                return static_cast<char>(v4 | 0xA);
            RenderTooltip(ev, 0x19C1);
            return 10;
        }
    }
    return 1;
}

// ===========================================================================
// Perform* — mutation / command emitters.
// ===========================================================================

// gilde.exe 0x46c30c — VIBE_Interaction_PerformRenovate.
// Builds a 248-byte action buffer (action 28 = "building action"), emits the
// "renovieren" command, then a slot-reset command, then closes the group. Family
// record adjustment is a render-position write (forwarded). Returns 15.
char PerformRenovate(ContextActor* /*building*/, ContextActor* /*tile*/,
                     ContextActor* /*actor*/) {
    g_commandHook("renovieren", 15);
    return 15;
}

// gilde.exe 0x46e1ec — VIBE_Interaction_PerformSabotage.
//   ev.mode must be 4; building id (ev.targetId) must resolve. Emits "sabotage"
//   (the staged action code is 25), returns 23. Else 0.
char PerformSabotage(InteractionEventRec* ev, ContextActor* /*actor*/) {
    if (ev->mode != 4)
        return 0;
    ObjectRec* building = BuildingFindById(ev->targetId);
    if (!building)
        return 0;
    g_commandHook("sabotage", 25);
    return 23;
}

// gilde.exe 0x46e7b4 — VIBE_Interaction_PerformBeating.
//   ev.mode must be 7; person id (ev.targetId) must resolve. Emits "pruegel" (staged
//   action code 26), returns 24. Else 0.
char PerformBeating(InteractionEventRec* ev, ContextActor* /*actor*/) {
    if (ev->mode != 7)
        return 0;
    Person* rec = PersonFindRecordById(ev->targetId);
    if (!rec)
        return 0;
    g_commandHook("pruegel", 26);
    return 24;
}

} // namespace guild::sim
