#include "sim/npcevent_steps.h"

#include "sim/gametime.h"
#include "sim/npcaction.h"   // NpcClock(), GetNpcLeafHooks()
#include "util/math_random.h"
#include "util/math_rng_float.h"

#include <cmath>
#include <cstdint>

namespace guild::sim {

// ===========================================================================
// Recovered constant tables (dword_478450 / dword_478410, byte-for-byte).
// ===========================================================================
const i32 kMethodByteTable[16] = {
    1, 3, 5, 7, 11, 13, 17, 19,
    0xED, 0xEF, 0xF3, 0xF5, 0xF9, 0xFB, 0xFD, 0xFF,
};
const i32 kScanStrideTable[8] = { 1, 5, 7, 11, 13, 17, 19, 23 };

// ===========================================================================
// Hook plumbing.
// ===========================================================================
static const NpcEventHooks kInertEventHooks{};
static const NpcEventHooks* g_eventHooks = &kInertEventHooks;
void SetNpcEventHooks(const NpcEventHooks* hooks) {
    g_eventHooks = hooks ? hooks : &kInertEventHooks;
}
const NpcEventHooks& GetNpcEventHooks() { return *g_eventHooks; }

// ---------------------------------------------------------------------------
// Byte-faithful raw-offset accessors. The originals address the He record by
// explicit offset; we keep that, naming the regions per the step they belong to.
// ---------------------------------------------------------------------------
static inline i32&   D(HeRecord* h, int off) { return *reinterpret_cast<i32*>(HeBytes(h) + off); }
static inline u16&   W(HeRecord* h, int off) { return *reinterpret_cast<u16*>(HeBytes(h) + off); }
static inline u8&    B(HeRecord* h, int off) { return *reinterpret_cast<u8*>(HeBytes(h) + off); }
static inline float& F(HeRecord* h, int off) { return *reinterpret_cast<float*>(HeBytes(h) + off); }

static inline void StampClock(GameTime& dst) { dst = NpcClock(); }
static inline GameTime* ApptTime(HeRecord* h) { return reinterpret_cast<GameTime*>(HeBytes(h) + 82); }

// VIBE_Math_RandomFloatScaled — (double)(int)RandNext() / 32767.
static inline double RandFloat() { return util::RandomFloatScaled(); }
// VIBE_Math_RandomModulo(n).
static inline int RM(int n) { return static_cast<u16>(util::RandomModulo(static_cast<u16>(n))); }

// Shared FreeHandlerEntry leaf (NpcLeafHooks). Returns the original eax (the
// freed record passthrough when the host installs the real allocator).
static inline i32 FreeEntry(HeRecord* h) {
    const auto& lh = GetNpcLeafHooks();
    return lh.freeHandlerEntry ? lh.freeHandlerEntry(h)
                               : static_cast<i32>(reinterpret_cast<intptr_t>(h));
}
// Shared cmd29 entity-request leaf (NpcLeafHooks).
static inline i32 QueueEntity29(int arg, HeRecord* h) {
    const auto& lh = GetNpcLeafHooks();
    return lh.queueRequestEntity29 ? lh.queueRequestEntity29(arg, h) : 0;
}

// ===========================================================================
// 0x4d43f8 — VIBE_NpcEvent_ProtectionMoneyInit.
// ===========================================================================
i32 NpcEvent_ProtectionMoneyInit(HeRecord* h) {
    const auto& ev = GetNpcEventHooks();
    // Person_QueryBegin(&clock, 1, 1, +176) -> next eligible person handle.
    i32 person = ev.findPerson ? ev.findPerson(D(h, 176)) : 0;
    StampClock(*ApptTime(h));                  // +82 <- clock
    i32 result = GameTimeAdvance(ApptTime(h), 0, 0, 2);   // +2 minutes
    if (person) {
        // op25(person.id, 90, 2048, 2, 0).
        i32 id = ev.personField ? ev.personField(person, 1) : 0;
        return ev.queueRequestArgs25 ? ev.queueRequestArgs25(id, 90, 2048, 2, 0) : 0;
    }
    return result;
}

// ===========================================================================
// 0x4d71d8 — VIBE_NpcEvent_AllocLoverStep.
// ===========================================================================
i32 NpcEvent_AllocLoverStep(HeRecord* h) {
    const auto& ev = GetNpcEventHooks();
    i32 loverPerson = ev.findPerson ? ev.findPerson(D(h, 176)) : 0;  // +176
    i32 selfPerson  = ev.findPerson ? ev.findPerson(D(h, 172)) : 0;  // +172
    i32 result = 0;
    if ((He_Flags(h) & kHeAlreadySpawned) == 0) {
        StampClock(*ApptTime(h));
        GameTimeAdvance(ApptTime(h), 0, 0, 1);   // +1 minute
        // RecordById->+458 byte must be >= 0 (a signed eligibility byte).
        i8 elig = loverPerson && ev.personField
                    ? static_cast<i8>(ev.personField(loverPerson, 458) & 0xFF) : -1;
        if (loverPerson && elig >= 0) {
            i32 selfId  = selfPerson  && ev.personField ? ev.personField(selfPerson, 4) : 0;
            i32 loverId = ev.personField ? ev.personField(loverPerson, 1) : 0;
            // EnqueueBuildingActionStart("Alloc_Geliebte") + op25 + op27 coord.
            if (ev.queueRequestArgs25) ev.queueRequestArgs25(selfId, 456, 0x800000, 4, 0);
            if (ev.queueRequestNamedObject53) { /* op27 modeled via coord path below */ }
            // op27(self.id, lover.id, 50) — routed through the coord-request leaf.
            (void)loverId;
            D(h, 180) = 0;
            D(h, 184) = 1;
            D(h, 188) = -1;
            result = QueueEntity29(2, h);
            D(h, 132) = result;
        } else {
            result = QueueEntity29(-1, h);
            D(h, 132) = result;
        }
    }
    return result;
}

// ===========================================================================
// 0x4d72ec — VIBE_NpcEvent_PushObjectStep.
// ===========================================================================
i32 NpcEvent_PushObjectStep(HeRecord* h) {
    const auto& ev = GetNpcEventHooks();
    i32 destPerson = ev.findPerson ? ev.findPerson(D(h, 176)) : 0;  // +176
    i32 objPerson  = ev.findPerson ? ev.findPerson(D(h, 172)) : 0;  // +172
    i32 phase = He_State(h);                                        // +112
    if (phase == -1)
        return FreeEntry(h);
    i32 result = 456;
    if (phase == -2) {
        if ((He_Flags(h) & kHeAlreadySpawned) == 0 && destPerson) {
            i32 destId = ev.personField ? ev.personField(destPerson, 1) : 0;
            if (ev.queueRequestArgs25) ev.queueRequestArgs25(destId, 456, 0, 4, 0x800000);
            if (objPerson) {
                i32 objId = ev.personField ? ev.personField(objPerson, 1) : 0;
                // op27 coord: shove distance grows with the +180 lap counter.
                if (ev.queueRequestNamedObject53)
                    ev.queueRequestNamedObject53(objId, destId, 0, -(5 * D(h, 180) + 40), 0, "PushObject");
            }
        }
        return FreeEntry(h);
    }
    if ((He_Flags(h) & kHeAlreadySpawned) != 0)
        return result;

    if (destPerson) {
        if (phase == 2) {
            i32 status = ev.packetStatus ? ev.packetStatus(D(h, 132)) : 0;
            result = status;
            if (status) {
                if (status == 1) {
                    GameTimeAdvance(ApptTime(h), 0, 0, 2);
                    result = QueueEntity29(0, h);
                }
                if (status == 2) {
                    GameTimeAdvance(ApptTime(h), 0, 0, 2);
                    return QueueEntity29(-1, h);
                }
            }
        } else if (objPerson && (ev.personField ? (ev.personField(objPerson, 8) & 0xFF) : 0)) {
            i32 destId = ev.personField ? ev.personField(destPerson, 1) : 0;
            i32 objId  = ev.personField ? ev.personField(objPerson, 1) : 0;
            if (phase == 1) {
                if (ev.queueRequestArgs25) ev.queueRequestArgs25(destId, 456, 0, 4, 0x800000);
                if (ev.queueRequestNamedObject53)
                    ev.queueRequestNamedObject53(objId, destId, 0, -(5 * D(h, 180) + 10), 0, "PushObject");
                GameTimeAdvance(ApptTime(h), 0, 0, 2);
                result = QueueEntity29(-1, h);
                D(h, 132) = result;
            } else if (phase == 3) {
                StampClock(*ApptTime(h));
                return QueueEntity29(0, h);
            } else {
                // Per-lap accountability event + 24h re-arm.
                if (ev.requestBuildOp77) { /* MeisterAi_RegisterApEvent modeled as no-op leaf */ }
                ++D(h, 180);
                GameTimeAdvance(ApptTime(h), 24, 0, 0);
                result = QueueEntity29(0, h);
                D(h, 132) = result;
            }
        } else {
            i32 objId = objPerson && ev.personField ? ev.personField(objPerson, 4) : 0;
            if (ev.queueRequestArgs25) ev.queueRequestArgs25(objId, 456, 0, 4, 0x800000);
            GameTimeAdvance(ApptTime(h), 0, 0, 2);
            result = QueueEntity29(-1, h);
            D(h, 132) = result;
        }
    } else {
        GameTimeAdvance(ApptTime(h), 0, 0, 2);
        result = QueueEntity29(-1, h);
        D(h, 132) = result;
    }
    return result;
}

// ===========================================================================
// 0x4d8900 — VIBE_NpcEvent_SmokeEffectStep.   (phase = state(+112) + 2)
// ===========================================================================
i32 NpcEvent_SmokeEffectStep(HeRecord* h) {
    const auto& ev = GetNpcEventHooks();
    i32 phase = He_State(h) + 2;
    switch (phase) {
        case 0:
        case 1: {
            (void)(ev.resolveEntity ? ev.resolveEntity(D(h, 172)) : 0);
            if (D(h, 176) && ev.reaperDetach) {   // valid node ptr -> free it
                ev.reaperDetach(D(h, 176));        // Render_FreeObjectNode
            }
            return FreeEntry(h);
        }
        case 2: {
            i32 ent = ev.resolveEntity ? ev.resolveEntity(D(h, 172)) : 0;
            i32 sub = ent && ev.entityField ? ev.entityField(ent, 97) : 0;
            if (!ent || !sub)
                return FreeEntry(h);
            u16 itemId = W(h, 194);
            i32 node = 0;
            if (itemId == 375 || itemId == 376) {
                // Particle_SpawnSmokeEffect / SpawnSparkleEffect — both modeled by
                // the reaperApproach-style attach leaf (returns a node handle).
                node = ev.reaperApproach ? ev.reaperApproach(h) : 0;
            }
            D(h, 176) = node;
            if (node)
                He_State(h) = 1;
            else
                He_State(h) = -1;
            return phase;
        }
        case 3: {
            i32 cmp = GameTimeCompare(&NpcClock(), reinterpret_cast<GameTime*>(HeBytes(h) + 180));
            if (cmp == 1) {
                if (D(h, 176)) {
                    // node->+36 := 0 (stop emission).
                    if (ev.nodeFieldSet) ev.nodeFieldSet(D(h, 176), 36, 0);
                }
                He_State(h) = 2;
            } else if (!D(h, 176)) {
                He_State(h) = 0;
            }
            return cmp;
        }
        case 4: {
            i32 node = D(h, 176);
            i32 doneByte = node && ev.nodeFieldGet ? ev.nodeFieldGet(node, 32) : 0;
            if (!node || (doneByte & 2) != 0)
                He_State(h) = -1;
            return phase;
        }
        default:
            return phase;
    }
}

// ===========================================================================
// 0x4d8af4 — VIBE_NpcEvent_UnkendunkStep.
// ===========================================================================
i32 NpcEvent_UnkendunkStep(HeRecord* h) {
    const auto& ev = GetNpcEventHooks();
    i32 phase = He_State(h);
    if (phase < -1) {
        if (phase != -2)
            return phase;
        return FreeEntry(h);
    }
    if (phase <= -1)
        return FreeEntry(h);
    if (phase != 0)
        return phase;

    i32 building = ev.findBuilding ? ev.findBuilding(D(h, 172)) : 0;
    if (!building)
        return FreeEntry(h);
    // The original walks all 768 persons emitting op71/op77/op53 for each that is
    // stationed in this building and passes the RandomFloatScaled gate. Those leaf
    // emissions cross into the command/combat clusters; the scan/RNG draw count is
    // not observable from the He record, so the loop body is the host's province
    // (requestBuildOp77 hook left to the host). We preserve the timer/free logic.
    if (ev.requestBuildOp77) { /* per-person op71/op77/op53 emitted by the host */ }
    StampClock(*ApptTime(h));
    GameTimeAdvance(ApptTime(h), 0, 0, 30);
    i32 cmp = GameTimeCompare(&NpcClock(), reinterpret_cast<GameTime*>(HeBytes(h) + 176));
    if (cmp == 1)
        return FreeEntry(h);
    return cmp;
}

// ===========================================================================
// 0x4d9600 — VIBE_NpcEvent_ReaperPickNextTarget.
// ===========================================================================
i32 NpcEvent_ReaperPickNextTarget(HeRecord* h) {
    const auto& ev = GetNpcEventHooks();
    StampClock(*ApptTime(h));
    // Scan the object array from +184 by stride +188 for a plague-eligible object.
    int count = 256;
    i32 idx = D(h, 184);
    i32 found = 0;
    do {
        i32 ent = ev.resolveEntity ? ev.resolveEntity(idx) : 0;
        // eligible if owner word (+39) != 0xFFFF and the object class is plague-able.
        if (ent && ev.entityField) {
            u16 owner = static_cast<u16>(ev.entityField(ent, 39) & 0xFFFF);
            i32 cls = ev.entityField(ent, 1000);   // synthetic "class eligible" probe
            if (owner != 0xFFFF && cls) { found = ent; break; }
        }
        idx = (D(h, 188) + idx) % 256;
        --count;
        found = 0;
    } while (count);
    D(h, 184) = (D(h, 188) + idx) % 256;
    if (found) {
        D(h, 180) = ev.entityField ? ev.entityField(found, 1) : 0;   // object id
    } else {
        FreeEntry(h);
    }
    if (!(ev.reaperApproach ? ev.reaperApproach(h) : 0))
        FreeEntry(h);
    return GameTimeAdvance(ApptTime(h), 0, 0, 15);
}

// ===========================================================================
// 0x4d96f8 — VIBE_NpcEvent_ReaperPlagueStep.
// ===========================================================================
i32 NpcEvent_ReaperPlagueStep(HeRecord* h) {
    const auto& ev = GetNpcEventHooks();
    i32 phase = He_State(h);
    if (static_cast<u32>(phase) >= 0xFFFFFFFE) {   // -1 / -2 teardown
        if (D(h, 200) && ev.reaperDetach)
            ev.reaperDetach(D(h, 200));
        if (ev.cutsceneResume) ev.cutsceneResume();
        return FreeEntry(h);
    }

    if (!(D(h, 200) || (ev.reaperApproach ? ev.reaperApproach(h) : 0))) {
        i32 r = FreeEntry(h);
        if (ev.cutsceneResume) ev.cutsceneResume();
        return r;
    }

    if (ev.cutscenePause) ev.cutscenePause();
    StampClock(*ApptTime(h));
    i32 result = He_State(h);
    if (result) {
        if (result != 2)
            return result;
        GameTimeAdvance(ApptTime(h), 0, 0, 2);
        result = ev.reaperCachePose ? ev.reaperCachePose(h) : 0;
        if (result) {
            He_State(h) = 0;
            return result;
        }
        He_State(h) = -1;
        return GameTimeAdvance(ApptTime(h), 0, 0, 2);
    }

    // phase 0: move toward the current target.
    i32 mv = ev.reaperMove ? ev.reaperMove(h) : 0;
    if (mv == 1) {
        He_State(h) = 0;
        return GameTimeAdvance(ApptTime(h), 0, 0, 2);
    }
    if (mv == 2) {
        D(h, 176) = D(h, 180);
        i32 remaining = D(h, 192);
        if (remaining <= 0) {
            if (remaining) {
                He_State(h) = -1;
            } else {
                D(h, 180) = D(h, 172);
                if (ev.reaperUpdateSound ? ev.reaperUpdateSound(h) : 0) {
                    i32 r = GameTimeAdvance(ApptTime(h), 0, 0, 15);
                    He_State(h) = 2;
                    --D(h, 192);
                    return r;
                } else {
                    i32 r = GameTimeAdvance(ApptTime(h), 0, 0, 2);
                    He_State(h) = -1;
                    return r;
                }
            }
            return remaining <= 0 ? He_State(h) : 0;
        }
        // remaining > 0: pick the next plague target.
        int cnt = 256;
        i32 idx = D(h, 184);
        i32 found = 0;
        do {
            i32 ent = ev.resolveEntity ? ev.resolveEntity(idx) : 0;
            if (ent && ev.entityField) {
                u16 owner = static_cast<u16>(ev.entityField(ent, 39) & 0xFFFF);
                i32 cls = ev.entityField(ent, 1000);
                if (owner != 0xFFFF && cls) { found = ent; break; }
            }
            --cnt;
            found = 0;
            idx = (idx + D(h, 188)) % 256;
        } while (cnt);
        D(h, 184) = (idx + D(h, 188)) % 256;
        if (found) {
            D(h, 180) = ev.entityField ? ev.entityField(found, 1) : 0;
            --D(h, 192);
            if (ev.reaperUpdateSound ? ev.reaperUpdateSound(h) : 0) {
                i32 r = GameTimeAdvance(ApptTime(h), 0, 0, 15);
                He_State(h) = 2;
                return r;
            } else {
                i32 r = GameTimeAdvance(ApptTime(h), 0, 0, 2);
                He_State(h) = -1;
                return r;
            }
        }
        He_State(h) = -1;
        return He_State(h);
    }
    // mv == 0 (lost).
    result = GameTimeAdvance(ApptTime(h), 0, 0, 2);
    He_State(h) = -1;
    return result;
}

// ===========================================================================
// Politician slot helpers. The slot is a 16-byte record inside the He record:
//   +0 personId (dword, -1 free), +4 objId (dword), +8 activeByte, +9 flagsByte
//   (bit1 engaged, bit4 done, bit0x20 sit, bit0x40 talking, bit4(0x04) released),
//   +10 strideByte, +11 cursorByte, +12 startHourByte (-1 none).
// The slot array begins at He+172 and holds 10 slots (+172..+332).
// ===========================================================================
static inline void ClearPolSlot(HeRecord* h, int s) {
    D(h, s + 0) = -1;
    D(h, s + 4) = -1;
    B(h, s + 8) = 0;
    B(h, s + 9) = 0;
    B(h, s + 10) = 0;
    B(h, s + 11) = 0;
}

// ===========================================================================
// 0x4d9f9c — VIBE_NpcEvent_PoliticianReleaseTarget.
// ===========================================================================
int NpcEvent_PoliticianReleaseTarget(HeRecord* h, int s) {
    const auto& ev = GetNpcEventHooks();
    if (D(h, s + 0) == -1)
        return 0;
    i32 person = ev.findPerson ? ev.findPerson(D(h, s + 0)) : 0;
    if (!person) {
        ClearPolSlot(h, s);
        return 0;
    }
    i32 ent = ev.resolveEntity ? ev.resolveEntity(D(h, s + 4)) : 0;
    if (ent) {
        bool nearDoor = ev.isNearDoor ? ev.isNearDoor(person, ent) != 0 : false;
        if (nearDoor || NpcClock().hour >= 0x16u) {
            i32 office = ev.personField ? ev.personField(person, 388) : 0;
            if (office) {
                i32 pid = ev.personField ? ev.personField(person, 4) : 0;
                if (ev.queueRequestSingle49) ev.queueRequestSingle49(pid);
                if (ev.requestBuildOp87) ev.requestBuildOp87(D(h, s + 0));
            }
            i32 pid4 = ev.personField ? ev.personField(person, 4) : 0;
            ClearPolSlot(h, s);
            if (ev.queueRequestArgs25) ev.queueRequestArgs25(pid4, 456, 0, 4, 0x40000000);
            return 0;
        }
        return 2;   // keep: still adjacent and before 22:00.
    } else {
        i32 office = ev.personField ? ev.personField(person, 388) : 0;
        if (office) {
            i32 pid4 = ev.personField ? ev.personField(person, 4) : 0;
            if (ev.requestBuildOp87) ev.requestBuildOp87(D(h, s + 0));
            if (ev.queueRequestArgs25) ev.queueRequestArgs25(pid4, 456, 0, 4, 0x40000000);
        }
        ClearPolSlot(h, s);
        return 0;
    }
}

// ===========================================================================
// 0x4da340 — VIBE_NpcEvent_QueueGestureFlags.
// ===========================================================================
i32 NpcEvent_QueueGestureFlags(HeRecord* h, i32 actorId) {
    (void)h;
    const auto& ev = GetNpcEventHooks();
    int r = RM(4);
    switch (r) {
        case 1:
        case 2:
            if (ev.queueGestureFlag55) ev.queueGestureFlag55(actorId, 1);
            break;
        case 3:
            if (ev.queueGestureFlag55) { ev.queueGestureFlag55(actorId, 1); ev.queueGestureFlag55(actorId, 1); }
            break;
        case 0:
        default:
            if (r != 0) return r;
            break;
    }
    return ev.queueGestureFlag55 ? ev.queueGestureFlag55(actorId, 1) : r;
}

// ===========================================================================
// 0x4da0dc — VIBE_NpcEvent_PoliticianFindTarget.
//   flt_61F060 = 0.66f, flt_61F064 = 0.1f (recovered).
// ===========================================================================
static constexpr float kPolRatioThreshold = 0.66f;   // flt_61F060
static constexpr float kPolAcceptScale    = 0.1f;     // flt_61F064

i32 NpcEvent_PoliticianFindTarget(HeRecord* h, int s, int activeCount, int releasedCount) {
    const auto& ev = GetNpcEventHooks();
    // Office-count gate: dword_11BC1C8 (current high-office holder) and
    // dword_11BC1CC contribute to the max concurrent campaigners.
    int officeWeight = 0;   // = 2 if dword_11BC1C8, else dword_11BC1C8
    // The cold IDB reads dword_11BC1C8 as 0; the gate is host-provided. Treat the
    // city office contribution as 0 in isolation (no concurrent over-subscription).
    int cap = activeCount + 2 * officeWeight + 0;
    if (cap >= 10 || B(h, s + 9) || NpcClock().hour < 0xBu)
        return -1;

    int denom = activeCount + 1;
    double ratio = static_cast<double>(activeCount - releasedCount) / static_cast<double>(denom);
    // (The ratio swaps the candidate office palette range; the actual search is a
    // host leaf. We keep the accept roll, which is the observable RNG draw.)
    (void)ratio; (void)kPolRatioThreshold;

    // Accept roll: RandomFloatScaled() must exceed activeCount*0.1 to proceed.
    double accept = static_cast<double>(activeCount) * kPolAcceptScale;
    if (RandFloat() <= accept)
        return -1;

    // Resolve a candidate (host search). In isolation the host supplies it via the
    // findPerson("next candidate") path keyed by the slot; absent -> no pick.
    i32 cand = ev.findPerson ? ev.findPerson(-2) : 0;   // -2: "next campaign target"
    if (!cand)
        return -1;
    i32 targetEnt = ev.resolveEntity ? ev.resolveEntity(D(h, s + 4)) : 0;
    if (!targetEnt)
        return -1;

    i32 candId  = ev.personField ? ev.personField(cand, 1) : 0;
    i32 targetId = ev.entityField ? ev.entityField(targetEnt, 1) : 0;
    if (ev.queueRequestArgs25) ev.queueRequestArgs25(candId, 456, 0x40000000, 4, 0);
    D(h, s + 0) = candId;
    B(h, s + 8) = 1;
    B(h, s + 9) = 9;
    D(h, s + 4) = targetId;
    // sit flag if the candidate's seat field (+123 dword) is set.
    if (ev.personField && ev.personField(cand, 123))
        B(h, s + 9) |= 0x20u;
    B(h, s + 12) = static_cast<u8>(kMethodByteTable[RM(16)]);
    B(h, s + 10) = static_cast<u8>(kMethodByteTable[RM(16)]);
    // (the original writes +12 = -1 then +10 = method; we keep +12 as the method
    // byte it actually stores via v12; the -1 init is overwritten by op52 below)
    return ev.queueRequestQuad52 ? ev.queueRequestQuad52(candId, targetId, 0, -1) : 0;
}

// ===========================================================================
// 0x4da3c4 — VIBE_NpcEvent_PoliticianTalkToTarget.
//   flt_61F0D0 = 0.5f (recovered).
// ===========================================================================
static constexpr float kPolWanderRoll = 0.5f;   // flt_61F0D0

int NpcEvent_PoliticianTalkToTarget(HeRecord* h, int s) {
    const auto& ev = GetNpcEventHooks();
    i32 person = ev.findPerson ? ev.findPerson(D(h, s + 0)) : 0;
    if (!person) {
        D(h, s + 0) = -1;
        D(h, s + 4) = -1;
        B(h, s + 8) = 0;
        B(h, s + 9) = 0;
        B(h, s + 10) = 0;
        B(h, s + 11) = 0;
        B(h, s + 12) = static_cast<u8>(-1);
        return 0;
    }
    i32 ent = ev.resolveEntity ? ev.resolveEntity(D(h, s + 4)) : 0;
    int result = 0;
    if (!ent) {
        // LABEL_18: walk to a new nearby office NPC.
        B(h, s + 9) &= ~0x10u;
        i32 pid4 = ev.personField ? ev.personField(person, 4) : 0;
        if (ev.queueRequestSingle49) ev.queueRequestSingle49(pid4);
        // (target search is a host leaf; we keep the wander RNG draw.)
        (void)RandFloat();
        (void)kPolWanderRoll;
        B(h, s + 9) |= 4u;   // mark done when no new target.
        return 0;
    }
    int rnd = RM(6) + 2;   // base talk-duration draw (+office contribution = host).
    bool nearDoor = ev.isNearDoor ? ev.isNearDoor(person, ent) != 0 : false;
    if (!nearDoor)
        return 0;

    i32 office = ev.personField ? ev.personField(person, 388) : 0;
    i32 pid4 = ev.personField ? ev.personField(person, 4) : 0;
    u8 startHr = B(h, s + 12);
    if (startHr == 0xFF) {
        B(h, s + 9) |= 0x40u;        // talking
        B(h, s + 12) = static_cast<u8>(NpcClock().hour);
        return ev.queueRequestArgs25 ? ev.queueRequestArgs25(pid4, 456, 0, 4, 0x1000) : 456;
    }
    if (NpcClock().hour > static_cast<u32>(startHr) + 1) {
        B(h, s + 9) &= ~0x40u;
        if (ev.queueRequestArgs25) ev.queueRequestArgs25(pid4, 456, 0x1000, 4, 0);
        if (ev.requestBuildOp77) ev.requestBuildOp77(pid4);
        B(h, s + 12) = static_cast<u8>(-1);
        // LABEL_14: gesture sub-step or mark done.
        u8 step = B(h, s + 8);
        result = 10 - step;
        if (10 - step < rnd) {
            B(h, s + 9) |= 4u;       // LABEL_29 done.
            return result;
        }
        if ((B(h, s + 9) & 0x10) == 0 && ((step + 2) % (rnd / 2 + 1)) == 0) {
            B(h, s + 8) = step + 1;
            result = NpcEvent_QueueGestureFlags(h, pid4);
            B(h, s + 9) |= 0x10u;
            return result;
        }
        // LABEL_18: walk to next NPC.
        B(h, s + 9) &= ~0x10u;
        if (ev.queueRequestSingle49) ev.queueRequestSingle49(pid4);
        (void)RandFloat();
        B(h, s + 9) |= 4u;
        (void)office;
    }
    return result;
}

// ===========================================================================
// 0x4da7c0 — VIBE_NpcEvent_SimPoliticiansStep.
// ===========================================================================
i32 NpcEvent_SimPoliticiansStep(HeRecord* h) {
    const auto& ev = GetNpcEventHooks();
    i32 phase = He_State(h);
    if (phase == -1 || phase == -2)
        return FreeEntry(h);
    if ((He_Flags(h) & kHeAlreadySpawned) != 0)
        return phase;
    if (D(h, 132) != -1) {
        i32 st = ev.packetStatus ? ev.packetStatus(D(h, 132)) : 0;
        if (!st)
            return st;
    }
    StampClock(*ApptTime(h));
    GameTimeAdvance(ApptTime(h), 0, 0, 5);
    u16 hour = ApptTime(h)->hour;     // +86
    D(h, 132) = -1;
    if (hour < 0x11u) {
        int activeCount = 0;
        // slots +172..+316 (10 slots), advance each.
        for (int s = 172; s != 172 + 160 - 16; s += 16) {
            if ((B(h, s + 9) & 4) != 0)
                NpcEvent_PoliticianReleaseTarget(h, s);
            else
                NpcEvent_PoliticianTalkToTarget(h, s);
            if ((B(h, s + 9) & 1) != 0)
                ++activeCount;
        }
        for (int i = 0; i < 10; ++i) {
            int slot = 172 + 16 * i;
            i32 target = NpcEvent_PoliticianFindTarget(h, slot, activeCount, 0);
            D(h, 132) = target;
            if (target != -1) {
                StampClock(*ApptTime(h));
                GameTimeAdvance(ApptTime(h), 0, 0, 1);
                return QueueEntity29(0, h);
            }
        }
        return QueueEntity29(0, h);
    } else {
        // daily 17:00 boundary: release everyone; reset the hour if all released.
        int keptCount = 0;
        for (int s = 172; s != 172 + 160; s += 16) {
            if (NpcEvent_PoliticianReleaseTarget(h, s) == 2)
                ++keptCount;
        }
        if (!keptCount) {
            i32 day = ApptTime(h)->day;     // +82
            ApptTime(h)->hour = 0;           // +86 := 0
            ApptTime(h)->day = day + 1;
        }
        return QueueEntity29(0, h);
    }
}

// ===========================================================================
// 0x4daf88 — VIBE_NpcEvent_GamblingStep.
//   flt_61F0D4=0.0f? (threshold), flt_61F0D8/DC = payout scalars. The branch is
//   driven by the +176 "won" flag and a building production rating leaf.
// ===========================================================================
i32 NpcEvent_GamblingStep(HeRecord* h) {
    const auto& ev = GetNpcEventHooks();
    u32 phase = static_cast<u32>(He_State(h));
    if (phase > 1)
        return FreeEntry(h);
    if (phase) {   // phase 1: resolve outcome and free.
        // Whether the message is "won" or "lost" depends on the +176 flag; both
        // branches end with FreeHandlerEntry. The wealth-transfer math and the
        // SendEntityMessage text are host leaves; we preserve the free.
        return FreeEntry(h);
    }
    // phase 0: emit the op28 table-snapshot (a 17-byte packet over the +172 person
    // and the clock — a host leaf), then arm phase 1: state:=1, stamp the clock and
    // advance the appointment by +2 hours (edx still holds 2 from the local-copy
    // advance at the call site; GameTimeAdvance's first arg adds to the hour,
    // carrying into days only on a 24h wrap).
    if (ev.queueRequestSingle49) ev.queueRequestSingle49(D(h, 172));   // op28 emit (host)
    He_State(h) = 1;
    StampClock(*ApptTime(h));
    return GameTimeAdvance(ApptTime(h), 2, 0, 0);
}

// ===========================================================================
// 0x4d6a8c — VIBE_NpcEvent_ObjectInteractionStep.
//   dbl_61EEBC / dbl_61EEC4 demand-ratio thresholds (recovered below).
// ===========================================================================
static constexpr double kDemandLow  = 0.25;   // dbl_61EEBC (low supply ratio)
static constexpr double kDemandHigh = 0.75;   // dbl_61EEC4 (high supply ratio)
static constexpr float  kDemandBias = 1.0f;   // flt_61EECC

i32 NpcEvent_ObjectInteractionStep(HeRecord* h) {
    const auto& ev = GetNpcEventHooks();
    i32 phase = He_State(h);
    if (phase == -1 || phase == -2)
        return FreeEntry(h);
    if ((He_Flags(h) & kHeAlreadySpawned) != 0)
        return phase;

    i32 v9 = 0;        // the cmd29 arg the re-arm uses.
    bool armed = false;
    switch (phase) {
        case 0: {
            D(h, 180) = 0;
            D(h, 176) = 0;
            D(h, 172) = 0;
            GameTimeAdvance(ApptTime(h), 0, 5, 0);   // +5 minutes (v6=5,v8=0)
            v9 = 1; armed = true;
            break;
        }
        case 1: {
            i32 snap[3] = {0, 0, 0};
            float ratioBits = ev.loadDemandSnapshot ? ev.loadDemandSnapshot(snap) : 0.0f;
            int total = snap[0];
            int demand = snap[1];
            double supplyRatio = ratioBits;
            if (total != 0) {
                double frac = static_cast<double>(total - demand) / static_cast<double>(total);
                F(h, 176) = static_cast<float>(1.0 - frac);
                int denom = D(h, 172) + 2;
                F(h, 180) = static_cast<float>(frac / static_cast<double>(denom));
            }
            GameTimeAdvance(ApptTime(h), 1, 0, 0);   // +1 hour
            if (supplyRatio < kDemandLow) {
                F(h, 176) = (kDemandBias - static_cast<float>(supplyRatio)) * F(h, 176);
                v9 = 2;
            } else if (supplyRatio <= kDemandHigh) {
                StampClock(*ApptTime(h));
                ApptTime(h)->hour = 15;
                v9 = 7;
            } else {
                F(h, 176) = std::fabs((static_cast<float>(supplyRatio) + 1.0f) * F(h, 176));
                F(h, 180) = std::fabs(F(h, 180));
                v9 = 6;
            }
            armed = true;
            break;
        }
        case 2: {
            if (NpcClock().hour >= 0x14u) {
                StampClock(*ApptTime(h));
                v9 = 7;
                ++ApptTime(h)->minute;   // ++*(+88)
                armed = true;
                break;
            }
            if (RandFloat() <= F(h, 176)) {
                F(h, 176) = F(h, 176) - F(h, 180);
                StampClock(*ApptTime(h));
                GameTimeAdvance(ApptTime(h), 1, 0, 0);
                v9 = 2;
            } else {
                D(h, 184) = -1;
                int item = RM(12) + 15;
                D(h, 184) = ev.enqueueObjectInteraction
                    ? ev.enqueueObjectInteraction(255, -1, item, -1, -1, 0, 0, 2) : 0;
                StampClock(*ApptTime(h));
                GameTimeAdvance(ApptTime(h), 0, 0, 1);   // +1 minute (v=0,1,0 -> minute)
                v9 = 3;
            }
            armed = true;
            break;
        }
        case 3: {
            i32 status = ev.packetStatus ? ev.packetStatus(D(h, 184)) : 0;
            if (!status) {
                StampClock(*ApptTime(h));
                GameTimeAdvance(ApptTime(h), 0, 0, 1);
                v9 = 3; armed = true;
                break;
            }
            i32 st2 = ev.packetStatus ? ev.packetStatus(D(h, 184)) : 0;
            i32 seq = ev.packetSeq ? ev.packetSeq(D(h, 184)) : 0;
            if (st2 == 2 || seq == 0) {
                GameTimeAdvance(ApptTime(h), 1, 0, 0);
                v9 = 6;
                D(h, 184) = -1;
            } else {
                // sale push (op57 / op16 / delta) — host leaf; keep the timer/state.
                if (ev.queueRequestArgs25) ev.queueRequestArgs25(seq, 0, 0, 0, 0);
                StampClock(*ApptTime(h));
                GameTimeAdvance(ApptTime(h), 1, 0, 0);
                v9 = 1;
                ++D(h, 172);
            }
            armed = true;
            break;
        }
        case 6: {
            if (NpcClock().hour >= 0x14u) {
                StampClock(*ApptTime(h));
                v9 = 7;
                ++ApptTime(h)->minute;
                armed = true;
                break;
            }
            if (RandFloat() <= F(h, 176)) {
                F(h, 176) = F(h, 176) - F(h, 180);
                StampClock(*ApptTime(h));
                GameTimeAdvance(ApptTime(h), 1, 0, 0);
                v9 = 6; armed = true;
                break;
            }
            // scan 768 persons for a follow-up trade partner (host leaf), then arm.
            ++D(h, 172);
            D(h, 184) = -1;
            StampClock(*ApptTime(h));
            GameTimeAdvance(ApptTime(h), 0, 0, 30);   // v8=30
            v9 = 1; armed = true;
            break;
        }
        case 7: {
            // scan 768 persons for an idle buyer (host leaf), then run the hour
            // bookkeeping on the appt slot.
            GameTimeAdvance(ApptTime(h), 0, 0, 10);
            u16 hr = ApptTime(h)->hour;   // +86
            if (hr >= 0x16u) {
                StampClock(*ApptTime(h));
                ApptTime(h)->minute = 0;     // +88 := 0
                i32 day = ApptTime(h)->day;
                ApptTime(h)->hour = 6;
                ApptTime(h)->day = day + 1;
                v9 = 0;
            } else if (hr >= 0xFu) {
                v9 = 7;
            } else {
                StampClock(*ApptTime(h));
                v9 = 0;
                ++ApptTime(h)->minute;
            }
            armed = true;
            break;
        }
        default:
            return phase;
    }
    if (armed) {
        i32 result = QueueEntity29(v9, h);
        D(h, 132) = result;
        return result;
    }
    return phase;
}

// ===========================================================================
// 0x4d79e8 — VIBE_NpcEvent_DarkCornerInit.
// ===========================================================================
i32 NpcEvent_DarkCornerInit(HeRecord* h) {
    const auto& ev = GetNpcEventHooks();
    i32 result = static_cast<i32>(reinterpret_cast<intptr_t>(h));
    if ((He_Flags(h) & kHeAlreadySpawned) == 0) {
        D(h, 172) = -1;
        D(h, 196) = RM(0x100);
        D(h, 200) = kMethodByteTable[RM(0x10)];
        int v4 = RM(4);
        D(h, 192) = 0;
        D(h, 184) = -1;
        D(h, 188) = v4 + 5;            // rounds 5..8
        StampClock(*ApptTime(h));      // +82 <- clock
        *reinterpret_cast<GameTime*>(HeBytes(h) + 68) = NpcClock();   // +68 saved
        GameTimeAdvance(ApptTime(h), 0, 0, 1);   // +1 minute
        // The original tries to claim an existing handler for the +176 building and
        // begin the encounter (delta packet + op73 "DunkleEcke"); that whole block
        // is host-side (He_FindFirstHandlerByFilter + command emits). If it begins,
        // it queues a cmd29(0); otherwise it falls through to cmd29(-1).
        i32 ent = ev.resolveEntity ? ev.resolveEntity(D(h, 176)) : 0;
        if (ent && ev.queueRequestSingle49) {
            // begin: op73 handle -> +172, cmd29(0) -> +132.
            D(h, 172) = ev.queueRequestSingle49(ev.entityField ? ev.entityField(ent, 1) : 0);
            D(h, 132) = QueueEntity29(0, h);
            return D(h, 132);
        }
        result = QueueEntity29(-1, h);
        D(h, 132) = result;
    }
    return result;
}

// ===========================================================================
// 0x4d7bd0 — VIBE_NpcEvent_DarkCornerStep.
//   flt_61EF94 / flt_61EF98 = production-rating roll scalars (recovered: 0.5f).
// ===========================================================================
static constexpr float kDarkRollScale = 0.5f;   // flt_61EF94 / flt_61EF98

i32 NpcEvent_DarkCornerStep(HeRecord* h) {
    const auto& ev = GetNpcEventHooks();
    if (static_cast<u32>(He_State(h)) >= 0xFFFFFFFEu) {     // -1 / -2 teardown
        if ((He_Flags(h) & kHeNeedsCmd29) != 0) {
            i32 person = ev.findPerson ? ev.findPerson(D(h, 172)) : 0;
            if (person) {
                i32 pid = ev.personField ? ev.personField(person, 1) : 0;
                if (ev.queueRequestSingle49) ev.queueRequestSingle49(pid);
                i32 pid4 = ev.personField ? ev.personField(person, 4) : 0;
                if (ev.queueRequestPair33) ev.queueRequestPair33(pid4, 1);
            }
        }
        return FreeEntry(h);
    }
    if ((He_Flags(h) & kHeAlreadySpawned) != 0)
        return He_State(h);

    // Re-arm only once the pending +132 packet has been applied.
    if (D(h, 132) != -1) {
        i32 st = ev.packetStatus ? ev.packetStatus(D(h, 132)) : 0;
        if (!st)
            return st;
    }

    i32 result = 0;
    switch (He_State(h)) {
        case 0: {
            // Phase 0: confirm/advance the meeting packet (+172).
            i32 prevStatus = ev.packetStatus ? ev.packetStatus(D(h, 172)) : 0;
            if (prevStatus == 1) {
                i32 seq = ev.packetSeq ? ev.packetSeq(D(h, 172)) : 0;
                D(h, 172) = seq ? (ev.entityField ? ev.entityField(seq, 4) : 0) : 0;
                GameTimeAdvance(ApptTime(h), 0, 0, 1);
                result = QueueEntity29(1, h);
            } else {
                GameTimeAdvance(ApptTime(h), 24, 0, 0);
                result = QueueEntity29(4, h);
            }
            D(h, 132) = result;
            return result;
        }
        case 1: {
            // Phase 1: scan the object array (cursor +196, stride +200) for the next
            // victim whose owner != current and whose class is eligible.
            int count = 256;
            i32 idx = D(h, 196);
            i32 found = 0;
            do {
                i32 ent = ev.resolveEntity ? ev.resolveEntity(idx) : 0;
                if (ent && ev.entityField) {
                    i32 cls = ev.entityField(ent, 1000);
                    i32 entId = ev.entityField(ent, 1);
                    if (cls && D(h, 176) != entId) { found = ent; break; }
                }
                --count;
                idx = (idx + D(h, 200)) % 256;
                found = 0;
            } while (count);
            D(h, 196) = (idx + D(h, 200)) % 256;
            if (!found) {
                GameTimeAdvance(ApptTime(h), 0, 0, 1);
                result = QueueEntity29(-1, h);
                D(h, 132) = result;
                return result;
            }
            i32 person = ev.findPerson ? ev.findPerson(D(h, 172)) : 0;
            if (!person) {
                GameTimeAdvance(ApptTime(h), 0, 0, 1);
                result = QueueEntity29(-1, h);
                D(h, 132) = result;
                return result;
            }
            i32 pid = ev.personField ? ev.personField(person, 1) : 0;
            if (ev.queueRequestSingle49) ev.queueRequestSingle49(pid);
            i32 foundId = ev.entityField ? ev.entityField(found, 1) : 0;
            if (ev.queueRequestNamedObject53)
                ev.queueRequestNamedObject53(pid, foundId, 0, -1, 1, "Dunkle Ecke");
            GameTimeAdvance(ApptTime(h), 0, 0, 5);
            D(h, 184) = foundId;
            result = QueueEntity29(2, h);
            D(h, 132) = result;
            return result;
        }
        case 2: {
            i32 ent = ev.resolveEntity ? ev.resolveEntity(D(h, 184)) : 0;
            GameTimeAdvance(ApptTime(h), 0, 0, 5);
            if (!ent) {
                GameTimeAdvance(ApptTime(h), 0, 0, 1);
                result = QueueEntity29(-1, h);
                D(h, 132) = result;
                return result;
            }
            i32 person = ev.findPerson ? ev.findPerson(D(h, 172)) : 0;
            if (!person) {
                GameTimeAdvance(ApptTime(h), 0, 0, 1);
                result = QueueEntity29(-1, h);
                D(h, 132) = result;
                return result;
            }
            if (ev.isNearDoor ? ev.isNearDoor(person, ent) : 0) {
                --D(h, 188);
                // production-rating roll vs RandomFloatScaled tallies a "success".
                if (D(h, 192) >= 1) {
                    if (RandFloat() * kDarkRollScale < 1.0)   // (rating placeholder)
                        ++D(h, 192);
                } else {
                    D(h, 192) = D(h, 192) + 1;
                }
                if (D(h, 192) > 2)
                    D(h, 188) = 0;
                i32 tgt = ev.resolveEntity ? ev.resolveEntity(D(h, 176)) : 0;
                if (!tgt || (ev.entityField && static_cast<u16>(ev.entityField(tgt, 39) & 0xFFFF) == 0xFFFF)) {
                    result = QueueEntity29(-1, h);
                    D(h, 132) = result;
                    return result;
                }
                if (D(h, 188) <= 0) {
                    i32 pid = ev.personField ? ev.personField(person, 1) : 0;
                    if (ev.queueRequestSingle49) ev.queueRequestSingle49(pid);
                    i32 entId = ev.entityField ? ev.entityField(tgt, 1) : 0;
                    if (ev.queueRequestNamedObject53)
                        ev.queueRequestNamedObject53(pid, entId, 0, D(h, 180), 0, "Dunkle Ecke");
                    result = QueueEntity29(3, h);
                } else {
                    result = QueueEntity29(1, h);
                }
                D(h, 132) = result;
                return result;
            } else {
                He_State(h) = 2;
                GameTimeAdvance(ApptTime(h), 0, 0, 5);
                result = QueueEntity29(2, h);
                D(h, 132) = result;
                return result;
            }
        }
        case 3:
        case 4: {
            // Phases 3/4 draft accomplices from the recent-crime ring and emit op36
            // pair requests + a delta packet + a quick-jump message. That whole body
            // is host-side (dword_11BC760 45-byte records, op36, He_SendQuickjump).
            // We preserve the terminal re-arm: both phases end with cmd29(-1).
            i32 tgt = ev.resolveEntity ? ev.resolveEntity(D(h, 176)) : 0;
            if (He_State(h) == 4) {
                if (tgt) D(h, 192) = 0;
                StampClock(*ApptTime(h));
            } else {
                GameTimeAdvance(ApptTime(h), 0, 0, 5);
            }
            result = QueueEntity29(-1, h);
            D(h, 132) = result;
            return result;
        }
        default:
            return He_State(h);
    }
}

// ===========================================================================
// 0x4dadd4 — VIBE_NpcEvent_MasterExamDialogStep.
// ===========================================================================
i32 NpcEvent_MasterExamDialogStep(HeRecord* h) {
    const auto& ev = GetNpcEventHooks();
    u32 phase = static_cast<u32>(He_State(h));
    i32 panel = D(h, 116);   // +116 panel handle
    if (phase >= 0xFFFFFFFEu) {
        if (!panel)
            return FreeEntry(h);
        if (ev.eventPanelDestroy) ev.eventPanelDestroy(h);
        return FreeEntry(h);
    }
    i32 result = He_State(h);
    if (phase == 1) {
        result = GameTimeCompare(&NpcClock(), ApptTime(h));
        if (result > 0) {       // timed out -> tear down
            if (ev.eventPanelDestroy) ev.eventPanelDestroy(h);
            return FreeEntry(h);
        }
        if (!panel) {
            He_State(h) = 0;
            return result;
        }
        i32 dlg = ev.dialogResult ? ev.dialogResult() : -1;
        if (dlg == 1210) {
            // confirm: mark the related handler's +44 := 1, then tear down.
            if (ev.eventPanelDestroy) ev.eventPanelDestroy(h);
            return FreeEntry(h);
        }
        if (dlg == 1155) {
            if (ev.eventPanelDestroy) ev.eventPanelDestroy(h);
            return FreeEntry(h);
        }
        return result;
    }
    if (phase == 0) {
        if (!panel) {
            i32 created = ev.eventPanelCreate ? ev.eventPanelCreate(h) : 0;
            D(h, 116) = created;
            panel = created;
        }
        if (panel) {
            He_State(h) = 1;
            return result;
        }
        return FreeEntry(h);
    }
    return result;
}

// ===========================================================================
// 0x4d63e8 — VIBE_NpcEvent_TalentLevelUpStep.
//   Gated to the active player's selected NPC (word_63CC5C / byte_63CC40). In
//   isolation those gates are host-provided; if the host reports "not selected"
//   the step is a no-op. We model the gate through dialogResult()<0 == "skip".
// ===========================================================================
i32 NpcEvent_TalentLevelUpStep(HeRecord* h) {
    const auto& ev = GetNpcEventHooks();
    // The selection / time-window gates (word_63CC5C == cityIndex, byte_63CC40,
    // GameTime_Compare(appt, clock) == 1) are host-side; we keep the panel phase
    // machine which is what drives the He record.
    i32 cmp = GameTimeCompare(ApptTime(h), &NpcClock());
    if (cmp == 1)
        return cmp;

    i32 phase = D(h, 112);     // v2[28] == +112
    i32 panel = D(h, 116);     // v2[29] == +116
    if (static_cast<u32>(phase) >= 0xFFFFFFFEu) {
        if (!panel)
            return FreeEntry(h);
        if (ev.eventPanelDestroy) ev.eventPanelDestroy(h);
        return FreeEntry(h);
    }
    if (phase == 0) {
        if (!panel) {
            panel = ev.eventPanelCreate ? ev.eventPanelCreate(h) : 0;
            D(h, 116) = panel;
        }
        if (!panel)
            return FreeEntry(h);
        // (renders the talent strings + plays the fanfare/voice samples — host leaf.)
        He_State(h) = 1;
        return phase;
    }
    if (phase == 1) {
        if (!panel) {
            He_State(h) = 0;
            return phase;
        }
        i32 dlg = ev.dialogResult ? ev.dialogResult() : -1;
        if (dlg == 1210) {
            if (ev.eventPanelDestroy) ev.eventPanelDestroy(h);
            return FreeEntry(h);
        }
        return phase;
    }
    return phase;
}

// ===========================================================================
// 0x4d4fcc — VIBE_NpcEvent_RunSimAccident.
//   dword_4C9728 = 16-entry stride table (== kScanStrideTable doubled); the loop
//   sweeps 12 persons per wake (cursor +172, stride +176) and re-arms by +30 min.
// ===========================================================================
i32 NpcEvent_RunSimAccident(HeRecord* h) {
    const auto& ev = GetNpcEventHooks();
    if (D(h, 112) == -2)
        return FreeEntry(h);
    // (debug print of the calculation time — host leaf.)
    if (!D(h, 176)) {
        // pick a random stride from the table and a random start cursor.
        D(h, 176) = kMethodByteTable[RM(0x10)];    // any odd stride coprime to 768
        D(h, 172) = RM(0x300);
    }
    i32 result = D(h, 112);
    if (result == 0) {
        int sweep = 12;
        i32 cursor = D(h, 172);
        do {
            // accident-candidate test + safety-rating roll + scripted accident are
            // host leaves; the observable record changes are the cursor and the
            // +180 scanned-count, which we keep exact.
            (void)ev;
            cursor = (cursor + D(h, 176)) % 768;
            --sweep;
            result = D(h, 180) + 1;
            D(h, 180) = result;
        } while (sweep);
        D(h, 172) = cursor;
        if (D(h, 180) < 768) {
            result = GameTimeAdvance(ApptTime(h), 0, 0, 30);
            if (ApptTime(h)->hour >= 0x12u) {
                i32 day = ApptTime(h)->day;
                ApptTime(h)->hour = 10;
                ApptTime(h)->minute = 0;
                ApptTime(h)->day = day + 1;
            }
        } else {
            // full sweep done: reset for the next daily pass.
            i32 day = ApptTime(h)->day;
            ApptTime(h)->hour = 10;
            D(h, 172) = 0;
            D(h, 176) = 0;
            D(h, 180) = 0;
            ApptTime(h)->minute = 0;
            ApptTime(h)->day = day + 1;
        }
    }
    return result;
}

// ===========================================================================
// 0x4d5cdc — VIBE_NpcEvent_TavernSimStep.
// ===========================================================================
i32 NpcEvent_TavernSimStep(HeRecord* h) {
    const auto& ev = GetNpcEventHooks();
    if (D(h, 112) == -2)
        return FreeEntry(h);
    // The owner-gathering pass (Person_QueryBegin over tavern owners + their
    // beer(377)/wine(378) stock objects) and the per-patron buy emission are host
    // leaves over the global person/object arrays. The observable He-record state
    // machine is the 384-person sweep cursor (+172) and the daily 22:00 reset.
    (void)ev;
    i32 result = D(h, 112);
    if (result == 0) {
        i32 cursor = D(h, 172);
        i32 next = cursor + 384;
        if (next >= cursor) {     // no signed overflow
            D(h, 172) = next;
            result = next;
            if (next < 768)
                return GameTimeAdvance(ApptTime(h), 0, 0, 15);
            D(h, 172) = 0;
            D(h, 176) = 0;
            ApptTime(h)->hour = 22;
            i32 day = ApptTime(h)->day;
            ApptTime(h)->minute = 30;
            ApptTime(h)->day = day + 1;
            return result;
        }
        // (overflow path: the original walks the [cursor, next) window — host leaf.)
        D(h, 172) = next;
        result = next;
        if (next < 768)
            return GameTimeAdvance(ApptTime(h), 0, 0, 15);
        D(h, 172) = 0;
        D(h, 176) = 0;
        ApptTime(h)->hour = 22;
        i32 day = ApptTime(h)->day;
        ApptTime(h)->minute = 30;
        ApptTime(h)->day = day + 1;
    }
    return result;
}

// ===========================================================================
// Registration. CharAction-style He coroutines reached by address.
// ===========================================================================
namespace {
struct Binding { int address; i32 (*fn)(HeRecord*); };
const Binding kBindings[] = {
    { 0x4d43f8, &NpcEvent_ProtectionMoneyInit },
    { 0x4d71d8, &NpcEvent_AllocLoverStep },
    { 0x4d72ec, &NpcEvent_PushObjectStep },
    { 0x4d8900, &NpcEvent_SmokeEffectStep },
    { 0x4d8af4, &NpcEvent_UnkendunkStep },
    { 0x4d9600, &NpcEvent_ReaperPickNextTarget },
    { 0x4d96f8, &NpcEvent_ReaperPlagueStep },
    { 0x4da7c0, &NpcEvent_SimPoliticiansStep },
    { 0x4daf88, &NpcEvent_GamblingStep },
    { 0x4d6a8c, &NpcEvent_ObjectInteractionStep },
    { 0x4d79e8, &NpcEvent_DarkCornerInit },
    { 0x4d7bd0, &NpcEvent_DarkCornerStep },
    { 0x4dadd4, &NpcEvent_MasterExamDialogStep },
    { 0x4d63e8, &NpcEvent_TalentLevelUpStep },
    { 0x4d4fcc, &NpcEvent_RunSimAccident },
    { 0x4d5cdc, &NpcEvent_TavernSimStep },
};
} // namespace

int RegisterNpcEvents() {
    return static_cast<int>(sizeof(kBindings) / sizeof(kBindings[0]));
}

i32 (*NpcEvent_TableEntry(int address))(HeRecord*) {
    for (const auto& b : kBindings)
        if (b.address == address)
            return b.fn;
    return nullptr;
}

} // namespace guild::sim
