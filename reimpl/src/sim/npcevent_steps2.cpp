#include "sim/npcevent_steps2.h"

#include "sim/gametime.h"
#include "sim/npcaction.h"   // NpcClock(), GetNpcLeafHooks()
#include "util/math_random.h"
#include "util/math_rng_float.h"

#include <cstdint>

namespace guild::sim {

// ===========================================================================
// Hook plumbing (mirrors npcevent_steps.cpp).
// ===========================================================================
static const NpcEventHooks2 kInertEventHooks2{};
static const NpcEventHooks2* g_eventHooks2 = &kInertEventHooks2;
void SetNpcEventHooks2(const NpcEventHooks2* hooks) {
    g_eventHooks2 = hooks ? hooks : &kInertEventHooks2;
}
const NpcEventHooks2& GetNpcEventHooks2() { return *g_eventHooks2; }

// ---------------------------------------------------------------------------
// Byte-faithful raw-offset accessors (same as npcevent_steps.cpp). The originals
// address the He record by explicit offset; we keep that, naming nothing.
// ---------------------------------------------------------------------------
static inline i32&   D(HeRecord* h, int off) { return *reinterpret_cast<i32*>(HeBytes(h) + off); }
static inline u16&   W(HeRecord* h, int off) { return *reinterpret_cast<u16*>(HeBytes(h) + off); }
static inline u8&    B(HeRecord* h, int off) { return *reinterpret_cast<u8*>(HeBytes(h) + off); }

static inline void StampClock(GameTime& dst) { dst = NpcClock(); }
static inline GameTime* ApptTime(HeRecord* h) { return reinterpret_cast<GameTime*>(HeBytes(h) + 82); }
static inline GameTime* SavedTime(HeRecord* h) { return reinterpret_cast<GameTime*>(HeBytes(h) + 68); }

// VIBE_Math_RandomFloatScaled — (double)(int)RandNext() / 32767.
static inline double RandFloat() { return util::RandomFloatScaled(); }
// VIBE_Math_RandomModulo(n).
static inline int RM(int n) { return static_cast<u16>(util::RandomModulo(static_cast<u16>(n))); }

// Shared NpcLeafHooks leaves (FreeHandlerEntry / cmd29). Identical to the helpers
// in npcevent_steps.cpp; declared static here so the two TUs don't collide.
static inline i32 FreeEntry(HeRecord* h) {
    const auto& lh = GetNpcLeafHooks();
    return lh.freeHandlerEntry ? lh.freeHandlerEntry(h)
                               : static_cast<i32>(reinterpret_cast<intptr_t>(h));
}
static inline i32 QueueEntity29(int arg, HeRecord* h) {
    const auto& lh = GetNpcLeafHooks();
    return lh.queueRequestEntity29 ? lh.queueRequestEntity29(arg, h) : 0;
}

// ===========================================================================
// 0x4d3c50 — VIBE_NpcEvent_ProtectionMoneyStep.
//
//   Begin   = Person_QueryBegin(+176)        (the racketeer's shop object)
//   self    = Person_FindRecordById(+172)    (the racketeer Person)
//   phase   = +112
// Faithful: the original tests `phase < 0xFFFFFFFE` (i.e. phase not in {-1,-2}) to
// run the machine, else (phase == -2/-1, the teardown branch) it issues a final
// op25 if the +120&2 flag is set and frees.
// ===========================================================================
i32 NpcEvent_ProtectionMoneyStep(HeRecord* h) {
    const auto& ev = GetNpcEventHooks2();
    i32 begin = ev.queryBegin ? ev.queryBegin(D(h, 176)) : 0;
    i32 self  = ev.findPerson ? ev.findPerson(D(h, 172)) : 0;

    if (static_cast<u32>(D(h, 112)) >= 0xFFFFFFFEu) {
        // teardown: phase -2/-1.
        if (begin) {
            if ((He_Flags(h) & 2) != 0) {
                i32 shopId = ev.objectField ? ev.objectField(begin, 1) : 0;
                if (ev.queueRequestArgs25) ev.queueRequestArgs25(shopId, 90, 0, 2, 256);
            }
        }
        return FreeEntry(h);
    }

    i32 result = self;
    // Re-arm gate: flag 0x04 clear and the prior packet (+132) is applied.
    if ((He_Flags(h) & 4) != 0)
        return result;
    if (!(D(h, 132) == -1 || (result = (ev.packetStatus ? ev.packetStatus(D(h, 132)) : 0)) != 0))
        return result;
    D(h, 132) = -1;

    // Requires a self Person, a shop object, and a valid owner word (+39 != -1).
    i32 ownerWord = begin && ev.objectField ? ev.objectField(begin, 39) : 0xFFFF;
    if (!(self && begin && static_cast<u16>(ownerWord) != 0xFFFF)) {
        StampClock(*ApptTime(h));
        GameTimeAdvance(ApptTime(h), 0, 0, 2);
        D(h, 112) = -1;
        result = QueueEntity29(-1, h);
        D(h, 132) = result;
        return result;
    }

    i32 selfId = ev.personField ? ev.personField(self, 1) : 0;   // *((DWORD*)self+1)
    i32 selfMethodWord = ev.personField ? ev.personField(self, 0) : 0; // *self (word)

    switch (D(h, 112)) {
    case 1: {
        if (ev.changePlayerAction)
            ev.changePlayerAction(begin, reinterpret_cast<intptr_t>(h),
                                  static_cast<u16>(selfMethodWord));
        if (ev.queueRequestSingle49) ev.queueRequestSingle49(selfId);
        if (ev.queueRequestNamedObject53)
            ev.queueRequestNamedObject53(selfId, /*shopId*/ ev.objectField ? ev.objectField(begin, 1) : 0,
                                         0, -1, 1, "Schutzgeld");
        StampClock(*ApptTime(h));
        GameTimeAdvance(ApptTime(h), 0, 0, 10);
        D(h, 112) = 2;
        result = QueueEntity29(2, h);
        D(h, 132) = result;
        break;
    }
    case 2: {
        // Wait on the live actor (+296 of the self's live ch_t, reached via +97).
        i32 liveActor = ev.personField ? ev.personField(self, 97 * 4) : 0;
        i32 busy = liveActor && ev.personField ? ev.personField(liveActor, 296) : 0;
        if (busy) {
            StampClock(*ApptTime(h));
            GameTimeAdvance(ApptTime(h), 0, 0, 4);
            D(h, 112) = 2;
            result = QueueEntity29(2, h);
            D(h, 132) = result;
            break;
        }
        // Method roll. Room worth at 100% scaled by flt_61EC30 -> +180 demand.
        i32 worth = ev.computeRoomWorth ? ev.computeRoomWorth(begin, 100, self) : 0;
        D(h, 180) = static_cast<i32>(static_cast<double>(worth) * 1.0);  // flt_61EC30 host scale
        // Gesture byte off the owner person (byte_12CE912[536*owner]) — host leaf.
        i32 gesture = self && ev.personField ? (ev.personField(self, 0x10) & 0xFF) : 0;
        if (gesture == 6 || gesture == 7) {
            // op28 slot-reset, longer hold.
            if (ev.queueRequestArgs25) ev.queueRequestArgs25(selfId, 90, 256, 2, 0);
            StampClock(*ApptTime(h));
            GameTimeAdvance(ApptTime(h), 0, 0, 0);
            D(h, 112) = 3;
            D(h, 184) = 2;
            result = QueueEntity29(3, h);
            D(h, 132) = result;
        } else {
            StampClock(*ApptTime(h));
            int waitMin = RM(16) + 2;
            GameTimeAdvance(ApptTime(h), 0, 0, waitMin);
            D(h, 112) = 3;
            // Building "always-pay" flag (Begin[91]&1) -> outcome 2.
            i32 alwaysPay = ev.objectField ? (ev.objectField(begin, 91) & 1) : 0;
            if (alwaysPay) {
                D(h, 184) = 2;
            } else {
                // High-security building (owner cat == 11) or prob roll -> pay (1).
                i32 cat = ev.objectField ? (ev.objectField(begin, 39) & 0xFFFF) : 0;
                (void)cat;
                double prob = 0.0;  // flt_12CEA24[134*owner] * flt_61EC34 — host leaf.
                if (RandFloat() < prob) {
                    D(h, 184) = 1;
                } else {
                    D(h, 184) = RM(2);
                }
            }
            result = QueueEntity29(3, h);
            D(h, 132) = result;
        }
        break;
    }
    case 3: {
        i32 outcome = D(h, 184);
        if (outcome == 1) {
            // pay: op25(shop,90,256,2,0) + op16 payout.
            i32 shopId = ev.objectField ? ev.objectField(begin, 1) : 0;
            if (ev.queueRequestArgs25) ev.queueRequestArgs25(shopId, 90, 256, 2, 0);
            if (ev.queueRequest16) ev.queueRequest16(selfId, /*ownerObj*/ shopId, D(h, 180), 0);
            // optional kickback to +188 target.
            if (D(h, 188) != -1) {
                i32 kb = ev.queryBegin ? ev.queryBegin(D(h, 188)) : 0;
                (void)kb;  // *(kb+77) += +180 — host leaf (object cash field).
            }
            StampClock(*ApptTime(h));
            StampClock(*SavedTime(h));
            GameTimeAdvance(ApptTime(h), 48, 0, 0);
            D(h, 112) = 4;
            D(h, 132) = QueueEntity29(4, h);
            if (ev.sendEntityMessage)
                ev.sendEntityMessage(selfId, selfId, nullptr, 1426, "_NACHRICHTEN_HS_84");
            result = selfId;
        } else {
            StampClock(*ApptTime(h));
            GameTimeAdvance(ApptTime(h), 0, 0, 5);
            D(h, 112) = 4;
            D(h, 132) = QueueEntity29(4, h);
            const char* tag = outcome ? nullptr : "_NACHRICHTEN_HS_85";
            (void)tag;
            if (ev.sendEntityMessage)
                ev.sendEntityMessage(selfId, selfId, nullptr, 1426, "_NACHRICHTEN_HS_85");
            result = selfId;
        }
        break;
    }
    case 4: {
        if (D(h, 184) == 1) {
            if (ev.sendEntityMessage) {
                ev.sendEntityMessage(selfId, selfId, nullptr, 1426, nullptr);
                i32 ownerId = begin && ev.objectField ? ev.objectField(begin, 39) : 0;
                ev.sendEntityMessage(ownerId, ownerId, nullptr, 1426, nullptr);
            }
        }
        if (ev.changePlayerAction)
            ev.changePlayerAction(begin, 0, static_cast<u16>(selfMethodWord));
        if (ev.queueRequestSingle49) ev.queueRequestSingle49(selfId);
        if (ev.queueRequestNamedObject53)
            ev.queueRequestNamedObject53(selfId, D(h, 188), 0, D(h, 192), 0, "Schutzgeld");
        StampClock(*ApptTime(h));
        GameTimeAdvance(ApptTime(h), 0, 0, 2);
        D(h, 112) = -1;
        result = QueueEntity29(-1, h);
        D(h, 132) = result;
        break;
    }
    default:
        return result;
    }
    return result;
}

// ===========================================================================
// 0x4d4460 — VIBE_NpcEvent_ExtortionStep.
//
//   Begin = Person_QueryBegin(+176)       (the inspected shop object)
//   self  = Person_FindRecordById(+172)   (the inspector Person; *((DWORD*)self+43))
//   phase = +112  (*((DWORD*)self+28))
// ===========================================================================
i32 NpcEvent_ExtortionStep(HeRecord* h) {
    const auto& ev = GetNpcEventHooks2();
    i32 begin = ev.queryBegin ? ev.queryBegin(D(h, 176)) : 0;
    i32 self  = ev.findPerson ? ev.findPerson(D(h, 172)) : 0;
    u32 phase = static_cast<u32>(D(h, 112));

    i32 ownerWord = begin && ev.objectField ? ev.objectField(begin, 39) : 0xFFFF;
    if (phase == 0xFFFFFFFEu || !self || !begin || static_cast<u16>(ownerWord) == 0xFFFF)
        return FreeEntry(h);

    i32 result = static_cast<i32>(phase);
    i32 selfId = ev.personField ? ev.personField(self, 1) : 0;
    i32 selfMethodWord = ev.personField ? ev.personField(self, 0) : 0;
    i32 shopId = ev.objectField ? ev.objectField(begin, 1) : 0;

    if (phase == 0) {
        if (ev.changePlayerAction)
            ev.changePlayerAction(begin, reinterpret_cast<intptr_t>(h),
                                  static_cast<u16>(selfMethodWord));
        if (ev.queueRequestSingle49) ev.queueRequestSingle49(selfId);
        if (ev.queueRequestNamedObject53)
            ev.queueRequestNamedObject53(selfId, shopId, 0, -1, 1, "BetriebsPr");
        StampClock(*ApptTime(h));
        GameTimeAdvance(ApptTime(h), 0, 0, 10);
        D(h, 112) = 1;
        return static_cast<i32>(reinterpret_cast<intptr_t>(h));
    }

    if (phase <= 1) {
        StampClock(*ApptTime(h));
        GameTimeAdvance(ApptTime(h), 0, 0, 4);
        // Busy actor (self live ch_t +296) -> just keep waiting (stay phase 1).
        i32 liveActor = ev.personField ? ev.personField(self, 97 * 4) : 0;
        i32 busy = liveActor && ev.personField ? ev.personField(liveActor, 296) : 0;
        if (busy) {
            D(h, 112) = 1;
            return static_cast<i32>(reinterpret_cast<intptr_t>(h));
        }
        // Fail probability from upgrade level: (flt_61EC84 - level) * flt_61EC88.
        int level = ev.buildingUpgradeLevel ? ev.buildingUpgradeLevel(begin) : 0;
        double failProb = (static_cast<double>(0.0) - static_cast<double>(level)) * 1.0; // host scale
        // Scan for a contraband object in this shop (QueryFind 0,2,7,4,29).
        int contraband = ev.patrolBrawlEligible ? 0 : 0;  // not this leaf
        (void)contraband;
        bool triggered = (failProb > 0.0);  // dbl_61EC8C threshold == 0.0 host approx
        if (triggered) {
            i32 worth = ev.computeRoomWorth ? ev.computeRoomWorth(begin, 100, 0) : 0;
            i32 fine = static_cast<i32>(static_cast<double>(worth) * failProb * 1.0);
            if (ev.queueRequest16) ev.queueRequest16(selfId, selfId, fine, 0);
            // success / failure messages gated on each party's gesture byte (6/7).
            i32 inspGesture = ev.personField ? (ev.personField(self, 0x10) & 0xFF) : 0;
            if (inspGesture == 6 || inspGesture == 7) {
                if (ev.sendEntityMessage)
                    ev.sendEntityMessage(selfId, selfId, nullptr, 1422, "_NACHRICHTEN_HS_87");
            }
            i32 ownerGesture = ev.objectField ? (ev.objectField(begin, 0x10) & 0xFF) : 0;
            if (ownerGesture == 6 || ownerGesture == 7) {
                i32 ownerId = ev.objectField ? ev.objectField(begin, 39) : 0;
                if (ev.sendQuickjumpMessage)
                    ev.sendQuickjumpMessage(ownerId, ownerId, nullptr, 1422, shopId, "_NACHRICHTEN_HS_86");
            }
        } else {
            if (ev.sendEntityMessage)
                ev.sendEntityMessage(selfId, selfId, nullptr, 1422, "_NACHRICHTEN_HS_88");
            i32 ownerId = ev.objectField ? ev.objectField(begin, 39) : 0;
            if (ev.sendQuickjumpMessage)
                ev.sendQuickjumpMessage(ownerId, ownerId, nullptr, 1422, shopId, "_NACHRICHTEN_HS_86");
        }
        result = static_cast<i32>(reinterpret_cast<intptr_t>(h));
        D(h, 112) = 2;
        return result;
    }

    if (phase == 2) {
        if (ev.changePlayerAction)
            ev.changePlayerAction(begin, 0, static_cast<u16>(selfMethodWord));
        if (ev.queueRequestSingle49) ev.queueRequestSingle49(selfId);
        if (ev.queueRequestNamedObject53)
            ev.queueRequestNamedObject53(selfId, D(h, 180), 0, D(h, 184), 0, "BetriebsPr");
        return FreeEntry(h);
    }
    return result;
}

// ===========================================================================
// 0x4d49b8 — VIBE_NpcEvent_PatrolStep.   (phases -2..5; +140 = 6 member-id slots)
// ===========================================================================
i32 NpcEvent_PatrolStep(HeRecord* h) {
    const auto& ev = GetNpcEventHooks2();
    i32 phase = D(h, 112);
    int finalize = 0;       // v49 — set when -2 reroutes into phase 5 (then free).

    if (phase == -2) {
        if ((He_Flags(h) & 4) == 0) {
            D(h, 112) = 5;
            finalize = 1;
            // fall through to LABEL_4 with phase==5.
        } else {
            return FreeEntry(h);
        }
    } else if (phase == -1) {
        return FreeEntry(h);
    }

    if ((He_Flags(h) & 4) != 0)
        return phase;

    StampClock(*ApptTime(h));
    i32 result = D(h, 112);
    switch (result) {
    case 0: {
        // start "Patrol" for up to 6 members (+140 .. +160).
        for (int i = 0; i < 6; ++i) {
            i32 member = ev.findPerson ? ev.findPerson(D(h, 140 + 4 * i)) : 0;
            if (member) {
                i32 liveActor = ev.personField ? ev.personField(member, 97 * 4) : 0;
                if (liveActor) {
                    i32 mWord = ev.personField ? ev.personField(member, 0) : 0;
                    if (ev.changePlayerAction)
                        ev.changePlayerAction(0, reinterpret_cast<intptr_t>(h), static_cast<u16>(mWord));
                    i32 mId = ev.personField ? ev.personField(member, 1) : 0;
                    if (ev.queueRequestSingle49) ev.queueRequestSingle49(mId);
                    if (ev.queueRequestNamedObject53)
                        ev.queueRequestNamedObject53(mId, -1, 0, -1, 1, "Patrol");
                }
            }
        }
        GameTimeAdvance(ApptTime(h), 0, 0, 5);
        result = QueueEntity29(1, h);
        D(h, 132) = result;
        return result;
    }
    case 1: {
        // wait for an active patroller; after 6 misses go to phase 2 with +68 stamp.
        int found = 0;
        for (int i = 0; i < 6; ++i) {
            i32 member = ev.findPerson ? ev.findPerson(D(h, 140 + 4 * i)) : 0;
            if (member) {
                i32 liveActor = ev.personField ? ev.personField(member, 97 * 4) : 0;
                if (liveActor) {
                    i32 busy = ev.personField ? ev.personField(liveActor, 296) : 0;
                    if (busy) { found = 1; break; }
                }
            }
        }
        if (found) {
            GameTimeAdvance(ApptTime(h), 0, 0, 4);
            result = QueueEntity29(1, h);
        } else {
            StampClock(*SavedTime(h));
            GameTimeAdvance(ApptTime(h), 0, 0, 10);
            result = QueueEntity29(2, h);
        }
        D(h, 132) = result;
        return result;
    }
    case 2: {
        // 24h cooldown on +68; before then -> +2 min, phase 5.
        GameTime cd = *SavedTime(h);
        GameTimeAdvance(&cd, 24, 0, 0);
        if (GameTimeCompare(&NpcClock(), &cd) > 0) {
            GameTimeAdvance(ApptTime(h), 0, 0, 2);
            result = QueueEntity29(5, h);
            D(h, 132) = result;
            return result;
        }
        // find a rival patrol + accept roll -> draft op39 brawl (phase 3) or
        // hair-gesture re-hold (phase 2).
        int rival = ev.patrolBrawlEligible ? ev.patrolBrawlEligible(h) : 0;
        if (rival && RM(2)) {
            // op39 fight packet (6 attackers from rival, 6 defenders from self).
            if (ev.queueRequest39) {
                u8 packet[64] = {};
                D(h, 216) = ev.queueRequest39(packet);
            } else {
                D(h, 216) = 0;
            }
            D(h, 112) = 3;
            GameTimeAdvance(ApptTime(h), 0, 0, 10);
            result = QueueEntity29(3, h);
        } else {
            GameTimeAdvance(ApptTime(h), 0, 0, 10);
            // hair-gesture behaviour leaf (object-find + transform) — host no-op.
            result = QueueEntity29(2, h);
        }
        D(h, 132) = result;
        return result;
    }
    case 3: {
        GameTimeAdvance(ApptTime(h), 0, 0, 1);
        i32 status = ev.packetStatus ? ev.packetStatus(D(h, 216)) : 0;
        if (!status) {
            result = QueueEntity29(3, h);
            D(h, 132) = result;
            return result;
        }
        i32 seq = ev.packetSeq ? ev.packetSeq(D(h, 216)) : 0;
        if (!seq)
            D(h, 132) = QueueEntity29(5, h);
        D(h, 212) = seq;
        result = QueueEntity29(4, h);
        D(h, 132) = result;
        return result;
    }
    case 4: {
        GameTimeAdvance(ApptTime(h), 0, 0, 5);
        if (!(ev.cutsceneActive ? ev.cutsceneActive(D(h, 212)) : 0)) {
            GameTimeAdvance(ApptTime(h), 0, 0, 2);
            result = QueueEntity29(5, h);
        } else {
            result = QueueEntity29(4, h);
        }
        D(h, 132) = result;
        return result;
    }
    case 5: {
        // end every member's "Patrol" action and free.
        for (int i = 0; i < 6; ++i) {
            i32 member = ev.findPerson ? ev.findPerson(D(h, 140 + 4 * i)) : 0;
            if (member) {
                i32 liveActor = ev.personField ? ev.personField(member, 97 * 4) : 0;
                if (liveActor) {
                    i32 mWord = ev.personField ? ev.personField(member, 0) : 0;
                    if (ev.changePlayerAction)
                        ev.changePlayerAction(0, 0, static_cast<u16>(mWord));
                    i32 mId = ev.personField ? ev.personField(member, 1) : 0;
                    if (ev.queueRequestSingle49) ev.queueRequestSingle49(mId);
                    if (ev.queueRequestNamedObject53)
                        ev.queueRequestNamedObject53(mId, D(h, 204), 0, D(h, 208), 0, "Patrol");
                }
            }
        }
        GameTimeAdvance(ApptTime(h), 0, 0, 2);
        if (finalize)
            return FreeEntry(h);
        result = QueueEntity29(-1, h);
        D(h, 132) = result;
        return result;
    }
    default:
        return result;
    }
}

// ===========================================================================
// 0x4d5308 — VIBE_NpcEvent_GatherGuildMembersStep.   (phases 0/1, -1 free)
// ===========================================================================
i32 NpcEvent_GatherGuildMembersStep(HeRecord* h) {
    const auto& ev = GetNpcEventHooks2();
    if (D(h, 112) == -1)
        return FreeEntry(h);
    if ((He_Flags(h) & 4) != 0)
        return D(h, 112);

    i32 result = D(h, 112);
    if (result == 0) {
        // phase 0: broadcast the meeting message to every live guild master, arm
        // phase 1 (+1 min via the raw +82 dword increment the original uses).
        i32 begin = ev.queryBegin ? ev.queryBegin(D(h, 172)) : 0;  // *((DWORD*)h+43)
        if (begin) {
            // RenderFormattedMessage(3617) + per-master SendEntityMessage(1418):
            // the 768-slot person scan is a host leaf; we model the broadcast hook.
            if (ev.sendEntityMessage)
                ev.sendEntityMessage(begin, begin, nullptr, 1418, nullptr);
            W(h, 172) = 13;                 // *((WORD*)h+43) = 13  (gather method id)
            D(h, 176) = 30;                 // *((DWORD*)h+22) = 30 (+176 -> 88? see note)
            // NOTE: the original advances +82 by a single tick: *(+82) = *(+82)+1.
            D(h, 82) = D(h, 82) + 1;
            result = QueueEntity29(1, h);
            D(h, 132) = result;
        } else {
            return QueueEntity29(-1, h);
        }
    } else if (result == 1) {
        // phase 1: rank up to 8 wealthy/eligible members and emit the gather packet.
        // Both ranking passes (the +176-threshold masters and the wealthiest
        // members by Person_SumCurrencyHeld insertion sort) are host leaves over
        // the global person array; the host reports how many were gathered.
        int gathered = 0;
        if (ev.sumCurrencyHeld) {
            // a representative draw so the threshold/sort path is exercised; the
            // real selection writes the chosen ids into the op39 packet.
            i32 cash = ev.sumCurrencyHeld(0);
            if (cash >= D(h, 176))
                gathered = 1;
        }
        if (gathered && ev.queueRequest39) {
            u8 packet[256] = {};
            ev.queueRequest39(packet);
        }
        StampClock(*ApptTime(h));
        GameTimeAdvance(ApptTime(h), 24, 0, 0);
        result = QueueEntity29(-1, h);
        D(h, 132) = result;
    }
    return result;
}

// ===========================================================================
// 0x4d57a0 — VIBE_NpcEvent_AwardTitleStep.
//
//   self    = h (the He record; the original is called with eax = record)
//   target  = Person_FindRecordById(+176)   (*((DWORD*)self+44))
//   phase   = +112  (*((DWORD*)self+28))
//   panel   = +116  (*((DWORD*)self+29))     (EventPanel slot handle)
//   v19     = &word_12CE910[268 * self+8]    (the active-player self person slot)
// ===========================================================================
i32 NpcEvent_AwardTitleStep(HeRecord* h) {
    const auto& ev = GetNpcEventHooks2();

    // gate: active player's selected NPC (word_63CC5C == +8) and byte_63CC40 set.
    if (!(ev.awardActivePlayerGate ? ev.awardActivePlayerGate(h) : 0))
        return 0;
    // pre-cutoff: GameTime_Compare(self+41, &clock) must not be 1. The "person
    // handle" the compare leaf reads is the He record itself (self+41 GameTime).
    i32 selfHandle = static_cast<i32>(reinterpret_cast<intptr_t>(h));
    if ((ev.compareAwardTime ? ev.compareAwardTime(selfHandle) : 0) == 1)
        return 0;

    i32 target = ev.findPerson ? ev.findPerson(D(h, 176)) : 0;
    i32 phase = D(h, 112);

    // self person slot present? (the *v19 == -1 guard) + a valid target.
    i32 selfSlotPresent = ev.findPerson ? ev.findPerson(He_CityIndex(h)) : 0;
    if (static_cast<u32>(phase) >= 0xFFFFFFFEu || !selfSlotPresent || !target) {
        if (D(h, 116) != 0) {                 // panel open -> destroy then free.
            if (ev.eventPanelDestroy) ev.eventPanelDestroy(h);
        }
        return FreeEntry(h);
    }

    if (phase == 0) {
        if (D(h, 116) == 0) {
            // create the panel (variant differs for self vs other player).
            int variant = (target == selfSlotPresent) ? 0 : 1;
            (void)variant;
            i32 panel = ev.eventPanelCreate ? ev.eventPanelCreate(h) : 0;
            D(h, 116) = panel;
        }
        if (D(h, 116) == 0)
            return FreeEntry(h);
        // render the title rich-text + voice fanfare; if it's the player's OWN
        // award, apply the title via a delta packet (op22) once (+180 latch).
        if (target == selfSlotPresent) {
            if (D(h, 180) == 0) {
                i32 ownerObjId = ev.personField ? ev.personField(selfSlotPresent, 1) : 0;
                i32 newTitle = ev.personField ? ev.personField(selfHandle, 43 * 4) : 0;
                if (ev.applyTitleDelta) ev.applyTitleDelta(target, ownerObjId, newTitle);
                D(h, 180) = 1;
            }
            if (ev.renderAwardText) ev.renderAwardText(h, target, /*own*/ 0);
            if (ev.playAwardVoice)  ev.playAwardVoice(h, target, /*own*/ 0);
        } else {
            if (ev.renderAwardText) ev.renderAwardText(h, target, /*other*/ 1);
            if (ev.playAwardVoice)  ev.playAwardVoice(h, target, /*other*/ 1);
        }
        D(h, 112) = 1;
        return 0;
    }

    if (phase == 1) {
        if (D(h, 116) == 0) {
            D(h, 112) = 0;
            return 0;
        }
        // wait for the dialog button: 1210 confirm -> destroy + free.
        i32 dlg = ev.awardDialogResult ? ev.awardDialogResult() : -1;
        if (dlg == 1210) {
            if (ev.eventPanelDestroy) ev.eventPanelDestroy(h);
            return FreeEntry(h);
        }
        return 0;
    }
    return 0;
}

// ===========================================================================
// Registration. Same address-keyed table form as RegisterNpcEvents().
// ===========================================================================
namespace {
struct Binding2 { int address; i32 (*fn)(HeRecord*); };
const Binding2 kBindings2[] = {
    { 0x4d3c50, &NpcEvent_ProtectionMoneyStep },
    { 0x4d4460, &NpcEvent_ExtortionStep },
    { 0x4d49b8, &NpcEvent_PatrolStep },
    { 0x4d5308, &NpcEvent_GatherGuildMembersStep },
    { 0x4d57a0, &NpcEvent_AwardTitleStep },
};
} // namespace

int RegisterNpcEvents2() {
    return static_cast<int>(sizeof(kBindings2) / sizeof(kBindings2[0]));
}

i32 (*NpcEvent2_TableEntry(int address))(HeRecord*) {
    for (const auto& b : kBindings2)
        if (b.address == address)
            return b.fn;
    return nullptr;
}

} // namespace guild::sim
