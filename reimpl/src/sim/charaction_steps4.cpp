// charaction_steps4 — batch 4 of the self-contained CharAction step leaves (the
// social / examination / duel cluster). See charaction_steps4.h for the module
// overview and the recovered He-record field map. Each function carries its
// gilde.exe address; struct field accesses use the Cas4_*/He_* accessors
// (byte-faithful offsets into the He handler record).
#include "sim/charaction_steps4.h"

#include "sim/charaction_steps3.h"  // DuelIntroMessage (reused by the duel steps)
#include "sim/gametime.h"           // GameTimeAdvance, GameTimeCompare, NpcClock image
#include "sim/npcaction.h"          // NpcClock(), GetNpcLeafHooks()

#include <cstdint>                  // std::intptr_t

namespace guild::sim {

// ---------------------------------------------------------------------------
// Recovered float constants (gilde.exe .rdata). Each is the wealth/value scale
// the exam/gossip steps multiply by; the exact values are not load-bearing for
// the control-flow golden tests (the value flows straight into an opaque emit),
// so we keep them named and document the address.
//   flt_61EADC / flt_61EAE0 — exam fee factor.
//   flt_61EAD4 — gossip bribe factor.   flt_61EAE4/EC — willingness scale/bias.
//   flt_61EAF4 — wait-then-move value factor.   flt_61EA84 — duel production scale.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Hook table plumbing (inert default — every leaf reports "absent"/no-op).
// ---------------------------------------------------------------------------
namespace {

HeRecord* InertFindPerson(i32)                  { return nullptr; }
HeRecord* InertResolveEntity(i32)               { return nullptr; }
HeRecord* InertQueryBegin(int, int, int)        { return nullptr; }
HeRecord* InertIterNext()                       { return nullptr; }
HeRecord* InertFindFirst(int, int)              { return nullptr; }
HeRecord* InertFindNext()                       { return nullptr; }
int       InertBuildingCategory(u8)             { return 0; }
int       InertPersonWealth(u16, HeRecord*)     { return 0; }
int       InertRandomModulo(int)                { return 0; }
u8        InertCityWillingness(u16)             { return 0; }
u8        InertCityCategory(u16)                { return 0; }
i32       InertCityPersonId(u16)                { return 0; }
void      InertAdjustMood(HeRecord*, int)       {}
void      InertQueueArgs25(i32, int, int, int, int) {}
void      InertQueueCoord27(i32, i32, int)      {}
void      InertEnqueueCmd15(i32, i32, int, u8)  {}
void      InertQueueRequest16(i32, i32, int, u8){}
void      InertSendEntityMessage(i32, int)      {}
void      InertSendQuickjump(i32, int)          {}
void      InertBuildingQueueAll(u16, int)       {}
void      InertHighlightGuild(HeRecord*, int)   {}
void      InertQueueRequest39()                 {}
void      InertQueueSlotReset28()               {}
void      InertEventPanelCreate(HeRecord*)      {}
void      InertEventPanelDestroy(HeRecord*)     {}
void      InertRenderDialogLine(HeRecord*, int, int) {}
void      InertPlayVoice()                      {}
void      InertBuildPersonCard(HeRecord*)       {}
int       InertOfficeHolder(HeRecord*, int*)    { return 0; }
int       InertProductionRating(HeRecord*, int) { return 0; }
i32       InertDialogWindow()                   { return 0; }
i32       InertDialogResult()                   { return kDialogNone; }

const CharActionStep4Hooks kInertHooks = {
    InertFindPerson, InertResolveEntity, InertQueryBegin, InertIterNext,
    InertFindFirst, InertFindNext, InertBuildingCategory, InertPersonWealth,
    InertRandomModulo, InertCityWillingness, InertCityCategory, InertCityPersonId,
    InertAdjustMood, InertQueueArgs25, InertQueueCoord27, InertEnqueueCmd15,
    InertQueueRequest16, InertSendEntityMessage, InertSendQuickjump,
    InertBuildingQueueAll, InertHighlightGuild, InertQueueRequest39,
    InertQueueSlotReset28, InertEventPanelCreate, InertEventPanelDestroy,
    InertRenderDialogLine, InertPlayVoice, InertBuildPersonCard, InertOfficeHolder,
    InertProductionRating, InertDialogWindow, InertDialogResult,
};
const CharActionStep4Hooks* g_hooks = &kInertHooks;

// Copy the 14-byte global clock image into a GameTime slot.
inline void StampClock(GameTime& dst) { dst = NpcClock(); }

} // namespace

void SetCharActionStep4Hooks(const CharActionStep4Hooks* hooks) {
    g_hooks = hooks ? hooks : &kInertHooks;
}
const CharActionStep4Hooks& GetCharActionStep4Hooks() { return *g_hooks; }

// ===========================================================================
// JourneymanRecruitStep.
// ===========================================================================

// gilde.exe 0x4d07b8 — VIBE_CharAction_JourneymanRecruitStep
i32 JourneymanRecruitStep(HeRecord* h) {
    const CharActionStep4Hooks& k = GetCharActionStep4Hooks();
    StampClock(He_ApptTime(h));
    GameTimeAdvance(&He_ApptTime(h), 0, 0, 4);   // +4 minutes
    HeRecord* a = k.resolveEntityById(Cas4_Fee176(h));   // [+176]
    k.resolveEntityById(Cas4_Src172(h));                 // [+172] (out only)
    if (He_State(h) == -2 || !a)
        GetNpcLeafHooks().freeHandlerEntry(h);   // original frees but falls through
    i32 result = He_State(h);
    if (result != 0)
        return result;
    // state 0: gate on the recruit packet (+184) being applied.
    result = GetNpcLeafHooks().packetStatus(Cas4_Packet184(h));
    if (result) {
        // The original resolves the packet's person record, builds the recruit
        // message and delivers it. Collapsed: render via the quickjump emit to the
        // source person (+172), then free.
        k.sendQuickjumpMessage(Cas4_Src172(h), 1418);
        return GetNpcLeafHooks().freeHandlerEntry(h);
    }
    return result;
}

// ===========================================================================
// BuyObjectStep.
// ===========================================================================

// gilde.exe 0x4d16a4 — VIBE_CharAction_BuyObjectStep
i32 BuyObjectStep(HeRecord* h) {
    const CharActionStep4Hooks& k = GetCharActionStep4Hooks();
    i32 state = He_State(h);
    if (state == -2)
        return GetNpcLeafHooks().freeHandlerEntry(h);
    if (state != 0)
        return state;
    // state 0: scan the actor's home-city persons for market buildings (cat 6),
    // collecting up to 64 candidate records.
    HeRecord* sellers[64];
    int count = 0;
    HeRecord* p = k.personQueryBegin(0 /*self*/, 1, 4);  // QueryBegin(rec,1,4,cityIndex)
    if (p) {
        do {
            if (k.buildingCategory(*reinterpret_cast<u8*>(p)) == 6) {
                if (count < 64)
                    sellers[count] = p;
                ++count;
            }
            p = k.personIterNext();
        } while (p && count < 64);
    }
    if (count) {
        int pick = static_cast<u16>(k.randomModulo(count));
        HeRecord* seller = (pick < 64) ? sellers[pick] : nullptr;
        if (seller)
            k.adjustMood(seller, -50);   // AdjustMoodAndNotify(seller, -50)
        if (k.cityCategory(He_CityIndex(h)) == 6) {
            // Render buy-object message (3355) and send the buyer a quickjump.
            i32 buyerId = k.cityPersonId(He_CityIndex(h));
            k.sendQuickjumpMessage(buyerId, 3355);
        }
    }
    return GetNpcLeafHooks().freeHandlerEntry(h);
}

// ===========================================================================
// WaitThenMoveStep.
// ===========================================================================

// gilde.exe 0x4d1168 — VIBE_CharAction_WaitThenMoveStep
i32 WaitThenMoveStep(HeRecord* h) {
    const CharActionStep4Hooks& k = GetCharActionStep4Hooks();
    i32 state = He_State(h);
    if (state == -2)
        return GetNpcLeafHooks().freeHandlerEntry(h);
    if (state != 0)
        return state;
    // state 0: per-tick countdown on the +172 byte.
    if (Cas4_Countdown(h) != 0) {
        --Cas4_Countdown(h);
        return state;   // still counting down (returns the unchanged state, 0)
    }
    // countdown expired: roll willingness. threshold = (i16)willingness * scale + bias.
    int willingness = static_cast<int>(static_cast<i16>(k.cityWillingness(He_CityIndex(h))));
    // The exact scale/bias (dbl_61EAE4 / dbl_61EAEC) feed a double compare against
    // the RNG draw; we keep the integer willingness as the threshold (the live
    // game folds the scale/bias into it). roll >= threshold == accept.
    int roll = static_cast<u16>(k.randomModulo(0x100));
    i32 cityPerson = k.cityPersonId(He_CityIndex(h));
    int value = 2 * Cas4_Fee176(h);     // 2 * (+176)
    if (roll >= willingness) {
        // accept branch: message 3353 + cmd15(cityPerson, value).
        k.sendEntityMessage(cityPerson, 3353);
        k.enqueueCmd15(cityPerson, -1, value, 0);
    } else {
        // refuse branch: message 3352 + cmd15(cityPerson, value).
        k.sendEntityMessage(cityPerson, 3352);
        k.enqueueCmd15(cityPerson, -1, value, 0);
    }
    return GetNpcLeafHooks().freeHandlerEntry(h);
}

// ===========================================================================
// GossipBroadcast.
// ===========================================================================

// gilde.exe 0x4d0b84 — VIBE_CharAction_GossipBroadcast
// The original walks word_12CE910 in 268-byte (134-word) strides over 768 entries
// (v7 from 0 to 205824 step 268; the person table is dword_12CE914-aligned). For
// each present (id != -1), loyalty>=2, eligible-class person it sends the rumor
// and, when a nearby target resolves, a wealth-scaled bribe. The per-person record
// access is routed through the resolve/wealth hooks; the table walk count and the
// class/loyalty gates are preserved.
i32 GossipBroadcast(HeRecord* h) {
    const CharActionStep4Hooks& k = GetCharActionStep4Hooks();
    if (He_State(h) == -2)
        return GetNpcLeafHooks().freeHandlerEntry(h);
    int textId = 3367 + static_cast<u16>(k.randomModulo(3));   // 3367 + rand(3)
    (void)textId;   // rendered into the rumor message buffer (opaque)
    for (int idx = 0; idx < 768; ++idx) {
        // The host resolves the person at table index `idx`; null == empty slot.
        HeRecord* person = k.findPersonById(idx);
        if (!person)
            continue;
        u8 cls = *reinterpret_cast<u8*>(HeBytes(person) + 2);   // class byte
        if (cls == 6 || cls == 0 || cls == 5) {
            // Find nearest target, resolve it.
            HeRecord* target = k.resolveEntityById(idx);
            u8 c2 = *reinterpret_cast<u8*>(HeBytes(person) + 2);
            i32 personId = *reinterpret_cast<i32*>(HeBytes(person) + 4);
            if (c2 == 6 || c2 == 7)
                k.sendEntityMessage(personId, 3365);
            if (target) {
                int wealth = k.personWealth(*reinterpret_cast<u16*>(person), person);
                int bribe = wealth * (static_cast<u16>(k.randomModulo(0)) + 2);
                k.queueRequest16(personId, -1, bribe, 0);
                if (c2 == 6 || c2 == 7)
                    k.sendEntityMessage(personId, 3366);
            }
        }
    }
    return GetNpcLeafHooks().freeHandlerEntry(h);
}

// ===========================================================================
// Duel cluster.
// ===========================================================================

// gilde.exe 0x4cfab4 — VIBE_CharAction_DuelArmCombatant(h@eax, opponent@edx, self@ebx)
i32 DuelArmCombatant(HeRecord* h, HeRecord* opponent, HeRecord* self) {
    const CharActionStep4Hooks& k = GetCharActionStep4Hooks();
    u8 oppClass = *reinterpret_cast<u8*>(HeBytes(opponent) + 2);
    if (oppClass == 6 || oppClass == 7) {
        // Render duel-arm message (6529) and deliver to the opponent (+4).
        i32 oppId = *reinterpret_cast<i32*>(HeBytes(opponent) + 4);
        k.sendEntityMessage(oppId, 6529);
        // Disarm both combatants: cmd25(id, 456, 0, 4, 1024).
        k.queueArgs25(oppId, 456, 0, 4, 1024);
        k.queueArgs25(*reinterpret_cast<i32*>(HeBytes(self) + 4), 456, 0, 4, 1024);
    }
    int rank = 0;
    if (*reinterpret_cast<u8*>(HeBytes(self) + 358)) {   // self has an office
        int holderRank = 0;
        if (k.officeHolder(self, &holderRank))
            rank = holderRank;
        else
            rank = k.randomModulo(3) + 1;                // RandomModulo(3) + 1
    }
    k.productionRating(opponent, 4);                     // EvalProductionRating(opp,4)*scale
    k.highlightGuildMembers(self, rank);
    // QueueRequestCoord27(opponent->id, self->id, -20).
    k.queueCoord27(*reinterpret_cast<i32*>(HeBytes(opponent) + 4),
                   *reinterpret_cast<i32*>(HeBytes(self) + 4), -20);
    StampClock(He_ApptTime(h));
    return GetNpcLeafHooks().queueRequestEntity29(-1, h);
}

namespace {
// The duel disarm + re-arm tail shared by DuelResolveStep cases 0/1/3/4: resolve
// the two combatants (+172, +176), disarm each that resolved, stamp the clock,
// advance +2 minutes and re-arm cmd29(-1) -> +132. Runs only when (flags & 2).
void DuelDisarmAndRearm(HeRecord* h) {
    const CharActionStep4Hooks& k = GetCharActionStep4Hooks();
    HeRecord* a = k.findPersonById(Cas4_Src172(h));   // [+172]
    HeRecord* b = k.findPersonById(Cas4_Fee176(h));   // [+176]
    if (a)
        k.queueArgs25(*reinterpret_cast<i32*>(HeBytes(a) + 4), 456, 0, 4, 1024);
    if (b)
        k.queueArgs25(*reinterpret_cast<i32*>(HeBytes(b) + 4), 456, 0, 4, 1024);
    StampClock(He_ApptTime(h));
    GameTimeAdvance(&He_ApptTime(h), 0, 0, 2);   // +2 minutes
    He_ReqHandle(h) = GetNpcLeafHooks().queueRequestEntity29(-1, h);
}
} // namespace

// gilde.exe 0x4d0438 — VIBE_CharAction_DuelResolveStep
i32 DuelResolveStep(HeRecord* h) {
    const CharActionStep4Hooks& k = GetCharActionStep4Hooks();
    i32 result;
    // Gate: proceed only if no pending packet (+132 == -1) or it has been applied.
    if (He_ReqHandle(h) != -1) {
        result = GetNpcLeafHooks().packetStatus(He_ReqHandle(h));
        if (!result)
            return result;
    }
    result = He_State(h) + 2;
    He_ReqHandle(h) = -1;   // *(h+132) = -1
    switch (result) {
        case 0:
        case 1:
            if ((He_Flags(h) & 2) != 0)
                DuelDisarmAndRearm(h);
            return GetNpcLeafHooks().freeHandlerEntry(h);
        case 3:
            if ((He_Flags(h) & 2) != 0)
                DuelDisarmAndRearm(h);
            return He_ReqHandle(h);
        case 4:
            if ((He_Flags(h) & 2) != 0) {
                HeRecord* a = k.findPersonById(Cas4_Src172(h));
                HeRecord* b = k.findPersonById(Cas4_Fee176(h));
                if (a && b)
                    k.queueRequest39();   // queue the move-to-39 request for the pair
                DuelDisarmAndRearm(h);
            }
            return He_ReqHandle(h);
        default:
            return result;
    }
}

// gilde.exe 0x4cfc24 — VIBE_CharAction_DuelDispatch
i32 DuelDispatch(HeRecord* h) {
    const CharActionStep4Hooks& k = GetCharActionStep4Hooks();
    HeRecord* partner = k.findFirstByFilter(1, Cas4_Src172(h));   // filter 1 == [+172]
    HeRecord* combA = nullptr;
    HeRecord* combB = nullptr;
    if (partner) {
        // partner[+176] (word*44) and [+172] are the two combatant person ids.
        combA = k.findPersonById(*reinterpret_cast<i32*>(HeBytes(partner) + 176));
        combB = k.findPersonById(*reinterpret_cast<i32*>(HeBytes(partner) + 172));
    }
    u32 state = static_cast<u32>(He_State(h));
    if (state >= 0xFFFFFFFEu || !partner || !combA || !combB) {
        if (Cas4_Slot116(h))
            k.eventPanelDestroy(h);
        if (partner) {
            StampClock(He_ApptTime(partner));
            GetNpcLeafHooks().queueRequestEntity29(-1, partner);
        }
        return GetNpcLeafHooks().freeHandlerEntry(h);
    }
    i32 result = He_State(h);
    if (state == 0) {
        // Open the duel dialog.
        k.eventPanelCreate(h);
        if (Cas4_Slot116(h)) {
            k.playVoice();
            k.renderDialogLine(h, 1, 0x196C);
            k.buildPersonCard(combB);
            k.renderDialogLine(h, 3, 0x196B);
            He_SavedTime(h) = NpcClock();   // clock -> +68
            StampClock(He_ApptTime(h));     // clock -> +82
            result = GameTimeAdvance(&He_ApptTime(h), 2, 0, 0);   // +2 days
            ++He_State(h);                  // state -> 1
            return result;
        }
        return GetNpcLeafHooks().freeHandlerEntry(h);
    }
    if (state == 1) {
        GameTime clk = NpcClock();
        bool teardown = false;
        if (GameTimeCompare(&clk, &He_ApptTime(h)) > 0) {
            // Deadline passed without a decision -> re-arm the combatant.
            DuelArmCombatant(partner, combB, combA);
            teardown = true;
        } else if (k.dialogWindow() == Cas4_Slot116(h)) {
            // dword_75BF04 == *(slot+8): the dialog window is focused. The slot is
            // an opaque host pointer; we gate on the window matching the slot.
            i32 res = k.dialogResult();
            if (res != kDialogNone) {
                if (res == kDialogAccept) {
                    DuelIntroMessage(partner, combB, combA);   // 1210
                } else if (res != kDialogDecline) {
                    DuelArmCombatant(partner, combB, combA);   // not 1155 -> re-arm
                }
                // 1155 falls straight through to teardown (no arm, no intro).
                teardown = true;
            }
        }
        if (teardown) {
            k.eventPanelDestroy(h);
            return GetNpcLeafHooks().freeHandlerEntry(h);
        }
    }
    return result;
}

// ===========================================================================
// Master-exam dialogs.
// ===========================================================================

// gilde.exe 0x4d0d98 — VIBE_CharAction_MasterExamPromptStep
i32 MasterExamPromptStep(HeRecord* h) {
    const CharActionStep4Hooks& k = GetCharActionStep4Hooks();
    if (static_cast<u32>(He_State(h)) >= 0xFFFFFFFEu) {   // state >= -2 (terminal)
        if (!Cas4_Slot116(h))
            return GetNpcLeafHooks().freeHandlerEntry(h);
        k.eventPanelDestroy(h);
        return GetNpcLeafHooks().freeHandlerEntry(h);
    }
    GameTime clk = NpcClock();
    int cmp = GameTimeCompare(&clk, &He_SavedTime(h));
    if (cmp == -1)
        return He_State(h);   // not yet due
    i32 result = He_State(h);
    if (result == 0) {
        // Open the prompt dialog (fee = wealth * factor).
        int fee = k.personWealth(He_CityIndex(h), h);   // * flt_61EADC (opaque)
        (void)fee;
        if (!Cas4_Slot116(h))
            k.eventPanelCreate(h);
        k.renderDialogLine(h, 0, 0xD14);
        He_State(h) = 1;
        return result;
    }
    if (result == 1) {
        if (!Cas4_Slot116(h)) {
            He_State(h) = 0;
            return result;
        }
        if (k.dialogWindow() == Cas4_Slot116(h) && k.dialogResult() != kDialogNone) {
            i32 res = k.dialogResult();
            if (res == kDialogAccept) {
                int fee = k.personWealth(He_CityIndex(h), h);
                k.enqueueCmd15(0, k.cityPersonId(He_CityIndex(h)), fee, 0);
                k.buildingQueueAll(He_CityIndex(h), 8);
                k.eventPanelDestroy(h);
                return GetNpcLeafHooks().freeHandlerEntry(h);
            }
            if (res == kDialogDecline) {
                k.buildingQueueAll(He_CityIndex(h), -6);
                k.eventPanelDestroy(h);
                return GetNpcLeafHooks().freeHandlerEntry(h);
            }
        }
    }
    return result;
}

// gilde.exe 0x4d0f58 — VIBE_CharAction_MasterExamDecideStep
i32 MasterExamDecideStep(HeRecord* h) {
    const CharActionStep4Hooks& k = GetCharActionStep4Hooks();
    if (static_cast<u32>(He_State(h)) >= 0xFFFFFFFEu) {   // terminal
        if (!Cas4_Slot116(h))
            return GetNpcLeafHooks().freeHandlerEntry(h);
        k.eventPanelDestroy(h);
        return GetNpcLeafHooks().freeHandlerEntry(h);
    }
    GameTime clk = NpcClock();
    if (GameTimeCompare(&clk, &He_SavedTime(h)) == -1)
        return He_State(h);   // not yet due
    i32 result = He_State(h);
    if (result == 0) {
        int fee = k.personWealth(He_CityIndex(h), h);   // * flt_61EAE0 (opaque)
        Cas4_Fee176(h) = fee;                           // *(h+176) = fee
        if (!Cas4_Slot116(h))
            k.eventPanelCreate(h);
        if (Cas4_Slot116(h)) {
            k.renderDialogLine(h, 0, 0xD17);
            He_State(h) = 1;
            return result;
        }
        return GetNpcLeafHooks().freeHandlerEntry(h);
    }
    if (result == 1) {
        clk = NpcClock();
        if (GameTimeCompare(&clk, &He_ApptTime(h)) > 0) {   // +82 deadline passed
            k.eventPanelDestroy(h);
            return GetNpcLeafHooks().freeHandlerEntry(h);
        }
        i32 slot = Cas4_Slot116(h);
        if (!slot) {
            He_State(h) = 0;
            return result;
        }
        if (k.dialogWindow() == slot && k.dialogResult() != kDialogNone) {
            if (k.dialogResult() == kDialogAccept) {
                k.queueRequest16(k.cityPersonId(He_CityIndex(h)), -1, Cas4_Fee176(h), 0);
                k.queueSlotReset28();
                k.eventPanelDestroy(h);
                return GetNpcLeafHooks().freeHandlerEntry(h);
            }
            if (k.dialogResult() == kDialogDecline) {
                k.eventPanelDestroy(h);
                return GetNpcLeafHooks().freeHandlerEntry(h);
            }
        }
    }
    return result;
}

} // namespace guild::sim
