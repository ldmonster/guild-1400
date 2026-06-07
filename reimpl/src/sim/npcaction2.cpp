#include "sim/npcaction2.h"

#include "sim/npcaction.h"   // NpcClock() (shared global game clock)
#include "sim/gametime.h"
#include "util/math_random.h"

namespace guild::sim {

// ===========================================================================
// dword_478450 — coprime-to-256 scan-step table (recovered byte-for-byte).
// ===========================================================================
const i32 kPlagueScanSteps[16] = {
    1, 3, 5, 7, 11, 13, 17, 19, 237, 239, 243, 245, 249, 251, 253, 255,
};

// ===========================================================================
// Leaf-hook plumbing.
// ===========================================================================
static const NpcAction2Hooks kInert{};
static const NpcAction2Hooks* g_h2 = &kInert;

void SetNpcAction2Hooks(const NpcAction2Hooks* hooks) {
    g_h2 = hooks ? hooks : &kInert;
}
const NpcAction2Hooks& GetNpcAction2Hooks() { return *g_h2; }

// Convenience: stamp the global clock into +82 (the 14-byte GameTime image copy
// the originals do via qword/dword/word writes at +82/+90/+94).
static inline void StampAppt(HeRecord* h) { He_ApptTime(h) = NpcClock(); }

// The member-id arrays are 4 slots. The originals iterate `do{...;p+=4}while(p!=base+16)`.
static constexpr int kMembers = 4;

// ===========================================================================
// gilde.exe 0x4d25c4 — VIBE_CharAction_PlagueSelectTarget.
// ===========================================================================
void NpcAction2_PlagueSelectTarget(HeRecord* h) {
    StampAppt(h);                                  // +82 <- clock

    // Walk the object array from a random start, up to 256 probes, for an object
    // whose AiPlayer type == 7 (townsfolk/city). v1 = start, v2 = 256 probes.
    int slot = static_cast<u16>(util::RandomModulo(0x100));   // RandomModulo(256)
    int probes = 256;
    bool found = false;
    do {
        u8 aiType = g_h2->objectAiPlayerType ? g_h2->objectAiPlayerType(slot) : 0;
        if (aiType == 7) {
            found = true;
            break;
        }
        slot = (slot + 1) % 256;
        --probes;
    } while (probes);

    if (!found) {
        i32 r = g_h2->queueRequestEntity29
                    ? g_h2->queueRequestEntity29(-1, h) : 0;
        He_ReqHandle(h) = r;                       // +132
        return;
    }

    i32 objId = g_h2->objectId ? g_h2->objectId(slot) : -1;

    // Scan cursor (+176) and step (+180) for the spread phase's modular walk.
    He_TargetObjId(h) = static_cast<u16>(util::RandomModulo(0x100));    // +176
    He_ScanStep(h) = kPlagueScanSteps[static_cast<u16>(util::RandomModulo(0x10))]; // +180

    // Per-citizen slot-reset accumulation (cmd28) over kind 6/7 persons. The
    // original iterates the 768 persons by 134-dword stride; we delegate the
    // accumulate to the hook (one call per qualifying citizen is the host's job).
    if (g_h2->beginSlotResetPacket)
        g_h2->beginSlotResetPacket(objId);

    // 4 "PEST" slot packets -> member array (+188..+200).
    for (int i = 0; i < kMembers; ++i) {
        i32 pk = g_h2->requestBuildOp73Str ? g_h2->requestBuildOp73Str(objId) : -1;
        He_PacketId(h, i) = pk;                    // +188 + 4*i
    }
    He_PlagueSource(h) = objId;                    // +208 (member 52)

    GameTimeAdvance(&He_ApptTime(h), 0, 0, 1);     // +1 minute
    i32 r = g_h2->queueRequestEntity29 ? g_h2->queueRequestEntity29(0, h) : 0;
    He_ReqHandle(h) = r;                           // +132

    if (g_h2->broadcastOutbreak)
        g_h2->broadcastOutbreak(objId);
}

// ===========================================================================
// gilde.exe 0x4d2900 — VIBE_CharAction_PlagueSpreadStep.
// ===========================================================================
HeRecord* NpcAction2_PlagueSpreadStep(HeRecord* h) {
    // state == -1 or -2 (unsigned >= 0xFFFFFFFE): teardown.
    if (static_cast<u32>(He_State(h)) >= 0xFFFFFFFEu) {
        if ((He_Flags(h) & 0x02) != 0) {
            for (int i = 0; i < kMembers; ++i) {
                i32 id = He_PacketId(h, i);
                if (g_h2->personPresent && g_h2->personPresent(id)) {
                    if (g_h2->queueSingle49)
                        g_h2->queueSingle49(id);
                    if (g_h2->queuePair33)
                        g_h2->queuePair33(id, 1);
                }
            }
        }
        i32 r = g_h2->freeHandlerEntry ? g_h2->freeHandlerEntry(h) : 0;
        return reinterpret_cast<HeRecord*>(static_cast<intptr_t>(r));
    }

    // Active gate: skip while flag 0x04 set, or the prior packet still pending.
    if ((He_Flags(h) & 0x04) != 0)
        return h;
    if (He_ReqHandle(h) != -1) {
        i32 st = g_h2->packetStatus ? g_h2->packetStatus(He_ReqHandle(h)) : 1;
        if (st == 0)
            return h;
    }

    if (He_State(h) == 5)
        NpcAction2_PlagueSelectTarget(h);

    switch (He_State(h)) {
    case 0: {
        // Refresh each member's packet id from its applied seq record.
        for (int i = 0; i < kMembers; ++i) {
            i32 handle = He_PacketId(h, i);
            i32 st = g_h2->packetStatus ? g_h2->packetStatus(handle) : 0;
            if (st == 1) {
                i32 seq = g_h2->packetSeq ? g_h2->packetSeq(handle) : 0;
                He_PacketId(h, i) = seq ? seq : -1;
            } else {
                He_PacketId(h, i) = -1;
            }
        }
        GameTimeAdvance(&He_ApptTime(h), 0, 0, 1);   // +1 min
        i32 r = g_h2->queueRequestEntity29 ? g_h2->queueRequestEntity29(1, h) : 0;
        He_ReqHandle(h) = r;
        return h;
    }
    case 1: {
        // Find the next susceptible object from the cursor (+176), modular by +180.
        int slot = He_TargetObjId(h);
        int step = He_ScanStep(h);
        int probes = 256;
        i32 targetObj = -1;
        bool ok = false;
        while (probes) {
            if (g_h2->objectSusceptible && g_h2->objectSusceptible(slot)) {
                targetObj = g_h2->objectId ? g_h2->objectId(slot) : -1;
                ok = true;
                break;
            }
            --probes;
            slot = (slot + step) % 256;
        }
        // Advance the persistent cursor by one more step (matches the original's
        // `*(+176) = (slot + step) % 256` after the loop).
        He_TargetObjId(h) = (slot + step) % 256;

        if (!ok) {
            GameTimeAdvance(&He_ApptTime(h), 0, 0, 1);
            i32 r = g_h2->queueRequestEntity29
                        ? g_h2->queueRequestEntity29(-1, h) : 0;
            He_ReqHandle(h) = r;
            return h;
        }

        int present = 0;
        for (int i = 0; i < kMembers; ++i) {
            i32 id = He_PacketId(h, i);
            if (g_h2->personPresent && g_h2->personPresent(id)) {
                if (g_h2->queueSingle49)
                    g_h2->queueSingle49(id);
                if (g_h2->queueNamedObject53)
                    g_h2->queueNamedObject53(id, targetObj, 0, -1);
                ++present;
            }
        }
        int delta = (present > 0) ? 5 : 20;
        GameTimeAdvance(&He_ApptTime(h), 0, 0, delta);
        He_PlagueTarget(h) = targetObj;              // +204
        i32 r = g_h2->queueRequestEntity29 ? g_h2->queueRequestEntity29(2, h) : 0;
        He_ReqHandle(h) = r;
        return h;
    }
    case 2: {
        bool tgtOk = g_h2->resolveObjectId
                         ? g_h2->resolveObjectId(He_PlagueTarget(h)) : false;
        GameTimeAdvance(&He_ApptTime(h), 0, 0, 5);
        if (!tgtOk) {
            i32 r = g_h2->queueRequestEntity29
                        ? g_h2->queueRequestEntity29(-1, h) : 0;
            He_ReqHandle(h) = r;
            return h;
        }
        // Wait until every member has reached the target door; otherwise re-arm.
        for (int i = 0; i < kMembers; ++i) {
            i32 id = He_PacketId(h, i);
            if (g_h2->personPresent && g_h2->personPresent(id)
                && !(g_h2->personNearDoor
                     && g_h2->personNearDoor(id, He_PlagueTarget(h)))) {
                He_State(h) = 2;
                GameTimeAdvance(&He_ApptTime(h), 0, 0, 5);
                i32 r = g_h2->queueRequestEntity29
                            ? g_h2->queueRequestEntity29(2, h) : 0;
                He_ReqHandle(h) = r;
                return h;
            }
        }

        // Opaque infection scatter (delegated; control flow around it is exact).
        if (g_h2->plagueInfectNearby)
            g_h2->plagueInfectNearby(He_PlagueTarget(h));

        i32 remaining = He_Counter172(h) - 1;        // +172
        He_Counter172(h) = remaining;
        if (remaining > 0) {
            i32 r = g_h2->queueRequestEntity29
                        ? g_h2->queueRequestEntity29(1, h) : 0;
            He_ReqHandle(h) = r;
            return h;
        }

        // Send the carriers home (the +208 source object).
        bool homeOk = g_h2->resolveObjectId
                          ? g_h2->resolveObjectId(He_PlagueSource(h)) : false;
        if (homeOk) {
            int present = 0;
            for (int i = 0; i < kMembers; ++i) {
                i32 id = He_PacketId(h, i);
                if (g_h2->personPresent && g_h2->personPresent(id)) {
                    if (g_h2->queueSingle49)
                        g_h2->queueSingle49(id);
                    if (g_h2->queueNamedObject53)
                        g_h2->queueNamedObject53(id, He_PlagueSource(h), 0, -1);
                    ++present;
                }
            }
            int delta = (present > 0) ? 1 : 10;
            GameTimeAdvance(&He_ApptTime(h), 0, 0, delta);
            i32 r = g_h2->queueRequestEntity29
                        ? g_h2->queueRequestEntity29(3, h) : 0;
            He_ReqHandle(h) = r;
        } else {
            i32 r = g_h2->queueRequestEntity29
                        ? g_h2->queueRequestEntity29(-1, h) : 0;
            He_ReqHandle(h) = r;
        }
        return h;
    }
    case 3: {
        GameTimeAdvance(&He_ApptTime(h), 0, 0, 5);
        bool homeOk = g_h2->resolveObjectId
                          ? g_h2->resolveObjectId(He_PlagueSource(h)) : false;
        if (!homeOk) {
            i32 r = g_h2->queueRequestEntity29
                        ? g_h2->queueRequestEntity29(-1, h) : 0;
            He_ReqHandle(h) = r;
            return h;
        }
        for (int i = 0; i < kMembers; ++i) {
            i32 id = He_PacketId(h, i);
            if (g_h2->personPresent && g_h2->personPresent(id)
                && !(g_h2->personNearDoor
                     && g_h2->personNearDoor(id, He_PlagueSource(h)))) {
                He_State(h) = 3;
                GameTimeAdvance(&He_ApptTime(h), 0, 0, 5);
                i32 r = g_h2->queueRequestEntity29
                            ? g_h2->queueRequestEntity29(3, h) : 0;
                He_ReqHandle(h) = r;
                return h;
            }
        }
        if (g_h2->broadcastSpreadDone)
            g_h2->broadcastSpreadDone(He_PlagueSource(h));
        i32 r = g_h2->queueRequestEntity29
                    ? g_h2->queueRequestEntity29(-1, h) : 0;
        He_ReqHandle(h) = r;
        return h;
    }
    default:
        return h;
    }
}

// ===========================================================================
// RegisterNpcActions2 — placeholder hook so the host can wire these step
// functions into the NpcAction dispatch table without clobbering the first
// agent's NpcAction_TableEntry mapping. The plague steps are CharAction coroutines
// dispatched off the He record rather than the 0x63d964 NpcAction jump table, so
// there is no new dispatch-table entry to register here yet; this entry point is
// provided for symmetry and future wiring (see the DEFERRED list).
// ===========================================================================
void RegisterNpcActions2() {
    // no dispatch-table types claimed by this batch (CharAction-driven).
}

} // namespace guild::sim
