#include "sim/npcaction11.h"

#include "sim/npcaction.h"   // NpcClock() — shared global game clock
#include "sim/gametime.h"

#include <cmath>
#include <cstdint>

namespace guild::sim {

// ===========================================================================
// Recovered constants (get_bytes @0x61F650 / 0x61F658 / 0x61A5F4).
// ===========================================================================
const double kPickupMoodMul  = 0.1;   // dbl_61F650
const double kPickupMoodBias = 0.5;   // dbl_61F658
const float  kWellWorthMul   = 0.3f;  // flt_61A5F4

// ===========================================================================
// Leaf-hook plumbing.
// ===========================================================================
static const NpcAction11Hooks kInert{};
static const NpcAction11Hooks* g_h11 = &kInert;

void SetNpcAction11Hooks(const NpcAction11Hooks* hooks) {
    g_h11 = hooks ? hooks : &kInert;
}
const NpcAction11Hooks& GetNpcAction11Hooks() { return *g_h11; }

// ---------------------------------------------------------------------------
// Raw-offset He fields. The originals address the record by explicit byte
// offset off the base register; mirror that exactly.
// ---------------------------------------------------------------------------
static inline i32&   F32(HeRecord* h, int off) { return *reinterpret_cast<i32*>(HeBytes(h) + off); }
static inline u16&   W16(HeRecord* h, int off) { return *reinterpret_cast<u16*>(HeBytes(h) + off); }
static inline u8&    B8(HeRecord* h, int off)  { return *reinterpret_cast<u8*>(HeBytes(h) + off); }

// Stamp the 14-byte global clock image (qword_13CE852/unk_13CE85A/unk_13CE85E)
// into rec+off — the engine's "now". This is the shared NpcClock().
static inline void StampClock(HeRecord* h, int off) {
    *reinterpret_cast<GameTime*>(HeBytes(h) + off) = NpcClock();
}
// VIBE_GameTime_Advance(rec+off, addDays, addSeconds, addMinutes).
static inline void Advance(HeRecord* h, int off, int days, int secs, int mins) {
    GameTimeAdvance(reinterpret_cast<GameTime*>(HeBytes(h) + off), days, secs, mins);
}

// ===========================================================================
// gilde.exe 0x4e4728 — VIBE_NpcAction_BeginEquipObject.
//   Stamp +82 and +68 with the clock; query the carrier person (filter +172).
//   If absent, free. Otherwise Advance the +82 appointment: dword_63C7B8 fast
//   path = 0 days/30 minutes; else (apprentice-span hours, min 24). Record the
//   person's entity id at +16; if the +90 equip bit (0x40) is already set, free;
//   else set it and emit the equip command (arg25 a=90,b=64,c=2,d=0).
// ===========================================================================
i32 NpcAction11_BeginEquipObject(HeRecord* h) {
    const NpcAction11Hooks* H = g_h11;

    StampClock(h, 82);
    StampClock(h, 68);

    void* person = H->personQueryBegin ? H->personQueryBegin(F32(h, 172)) : nullptr;
    if (!person) {
        if (H->freeHandlerEntry) H->freeHandlerEntry(h);
        return 0;
    }

    int days = 0, mins = 0;
    // dword_63C7B8 gate routed through the fastMode hook (disasm @0x4e47fd):
    // fast path = 0 days + 30 minutes (v6=0, v7=30); when clear, compute the
    // apprentice hour count carried in addDays.
    if (H->fastMode && H->fastMode()) {
        days = 0; mins = 30;
    } else {
        i32 span = H->familyDur ? H->familyDur(person, 0) : 0;
        int hours = 24 * span;
        if (hours <= 0) hours = 24;
        days = hours; mins = 0;   // a2(addDays) carries the hour count, like the orig
    }
    Advance(h, 82, days, 0, mins);

    F32(h, 16) = H->objId ? H->objId(person) : 0;
    u8 flags = H->equipFlags ? H->equipFlags(person) : 0;
    if (flags & 0x40) {
        if (H->freeHandlerEntry) H->freeHandlerEntry(h);
        return 0;
    }
    if (H->setEquipFlags) H->setEquipFlags(person, flags | 0x40);
    if (H->requestArgs25)
        H->requestArgs25(H->objId ? H->objId(person) : 0, 90, 64, 2, 0);
    return 0;
}

// ===========================================================================
// gilde.exe 0x4e4ed8 — VIBE_NpcAction_BeginUnequipObject.
//   Resolve the building (+172) for its id, stamp +82, Advance by
//   RandomModulo(10)+10 minutes, emit arg25(a=90,b=512,c=2,d=0).
// ===========================================================================
i32 NpcAction11_BeginUnequipObject(HeRecord* h) {
    const NpcAction11Hooks* H = g_h11;

    void* bldg = H->buildingFindById ? H->buildingFindById(F32(h, 172)) : nullptr;
    StampClock(h, 82);
    u16 r = H->randomModulo ? H->randomModulo(10) : 0;
    Advance(h, 82, 0, 0, r + 10);
    if (H->requestArgs25)
        H->requestArgs25(bldg && H->objId ? H->objId(bldg) : 0, 90, 512, 2, 0);
    return 0;
}

// ===========================================================================
// gilde.exe 0x4e4a48 — VIBE_NpcAction_BeginUseObject.
//   Stamp +82 and +68; Advance +82 by (5 days / 0) when dword_63C7B8 clear,
//   else (0 days / 1 second). Query person; if absent free. Else record +16,
//   and if +90 >= 0 (top bit clear) emit arg25(a=90,b=128,c=2,d=0).
// ===========================================================================
void* NpcAction11_BeginUseObject(HeRecord* h) {
    const NpcAction11Hooks* H = g_h11;

    StampClock(h, 82);
    StampClock(h, 68);
    if (H->fastMode && H->fastMode())
        Advance(h, 82, 0, 1, 0);
    else
        Advance(h, 82, 5, 0, 0);

    void* person = H->personQueryBegin ? H->personQueryBegin(F32(h, 172)) : nullptr;
    if (!person) {
        if (H->freeHandlerEntry) H->freeHandlerEntry(h);
        return nullptr;
    }
    F32(h, 16) = H->objId ? H->objId(person) : 0;
    u8 flags = H->equipFlags ? H->equipFlags(person) : 0;
    if (!(flags & 0x80)) {   // result[90] >= 0  (signed char top-bit clear)
        if (H->requestArgs25)
            H->requestArgs25(H->objId ? H->objId(person) : 0, 90, 128, 2, 0);
    }
    return person;
}

// ===========================================================================
// gilde.exe 0x4e4c84 — VIBE_NpcAction_BeginStoreObject.
//   Query person; if present emit arg25(a=90,b=1024,c=2,d=0). Stamp +82.
// ===========================================================================
void* NpcAction11_BeginStoreObject(HeRecord* h) {
    const NpcAction11Hooks* H = g_h11;

    void* person = H->personQueryBegin ? H->personQueryBegin(F32(h, 172)) : nullptr;
    if (person && H->requestArgs25)
        H->requestArgs25(H->objId ? H->objId(person) : 0, 90, 1024, 2, 0);
    StampClock(h, 82);
    return person;
}

// ===========================================================================
// gilde.exe 0x4e5b20 — VIBE_NpcAction_DecrementCarryStep.
//   State < -2: ignore; -2/-1: free. State 0: decrement the +184 carry counter;
//   resolve the carrier person (+172). When the counter goes negative, if the
//   person is a host kind (6/7) emit a slot-reset-28 (the 38/9 image), then
//   buildop72(id, +356-1) and free.
// ===========================================================================
i32 NpcAction11_DecrementCarryStep(HeRecord* h) {
    const NpcAction11Hooks* H = g_h11;

    i32 state = He_State(h);
    if (state < -1) {
        if (state != -2) return state;
        if (H->freeHandlerEntry) H->freeHandlerEntry(h);
        return state;
    }
    if (state <= -1) {
        if (H->freeHandlerEntry) H->freeHandlerEntry(h);
        return state;
    }
    if (state != 0)
        return state;

    i32 id = F32(h, 172);
    F32(h, 184) -= 1;
    void* person = H->findPersonById ? H->findPersonById(id) : nullptr;
    if (F32(h, 184) < 0) {
        if (person) {
            u8 k = H->kind ? H->kind(person) : 0;
            if (k == 6 || k == 7) {
                if (H->requestSlotReset28) H->requestSlotReset28(nullptr);
            }
            // RequestBuildOp72(person+4, *(person+356) - 1)
            if (H->requestBuildOp72)
                H->requestBuildOp72(H->objId ? H->objId(person) : 0,
                                    (H->personEquipState ? H->personEquipState(person, 356) : 1) - 1);
            if (H->freeHandlerEntry) H->freeHandlerEntry(h);
        }
    }
    return reinterpret_cast<intptr_t>(person);
}

// ===========================================================================
// gilde.exe 0x4ea10c — VIBE_NpcAction_CheckTargetBusyState.
//   Scan other handlers (filter 60) for one whose +180 matches this +180 or
//   whose +172 matches this +172. If a conflict exists, OR the queried person's
//   +101 carry-link is set, abort the action (+112 = -1). If the +172 person has
//   a nonzero +433 rank byte, also abort. Then stamp +82 and Advance +1 second.
// ===========================================================================
void NpcAction11_CheckTargetBusyState(HeRecord* h) {
    const NpcAction11Hooks* H = g_h11;

    void* conflict = H->findConflictingHandler
        ? H->findConflictingHandler(h, 60, F32(h, 180), F32(h, 172))
        : nullptr;
    void* person = H->personQueryBegin ? H->personQueryBegin(F32(h, 180)) : nullptr;
    if (conflict || (person && (H->field101 ? H->field101(person) : -1) != -1))
        He_State(h) = -1;

    void* tgt = H->findPersonById ? H->findPersonById(F32(h, 172)) : nullptr;
    if (tgt && (H->rank ? H->rank(tgt) : 0))
        He_State(h) = -1;

    StampClock(h, 82);
    Advance(h, 82, 0, 1, 0);
}

// ===========================================================================
// gilde.exe 0x4e6d2c — VIBE_NpcAction_DismissStaffStep.
//   State <-2 ignore; -2/-1 free. State 0: resolve person (+172). If present and
//   its +8 byte set: when its +364 staff-link is set, delta-clear that field and
//   recall the linked staffer (coord27 -25). Then buildop71/77 and a named-object
//   53 ("Entlassen"). Otherwise (+8 clear) abort with +112 = -1.
// ===========================================================================
i32 NpcAction11_DismissStaffStep(HeRecord* h) {
    const NpcAction11Hooks* H = g_h11;

    i32 state = He_State(h);
    if (state < -1) {
        if (state != -2) return state;
        if (H->freeHandlerEntry) H->freeHandlerEntry(h);
        return state;
    }
    if (state <= -1) {
        if (H->freeHandlerEntry) H->freeHandlerEntry(h);
        return state;
    }
    if (state != 0)
        return state;

    void* person = H->findPersonById ? H->findPersonById(F32(h, 172)) : nullptr;
    if (person && (H->personEquipState ? H->personEquipState(person, 8) : 0)) {
        i32 link = H->field364 ? H->field364(person) : 0;
        if (link) {
            if (H->beginDelta) H->beginDelta(person, H->objId ? H->objId(person) : 0);
            if (H->appendCopiedField) H->appendCopiedField(person, 364);
            if (H->requestState23) H->requestState23();
            // recall the staffer: coord27(cityId(staffer city), person+4, -25)
            void* staffer = H->findPersonById ? H->findPersonById(link) : nullptr;
            u16 ci = staffer && H->markerWord ? H->markerWord(staffer) : 0;
            if (H->requestCoord27)
                H->requestCoord27(H->cityId ? H->cityId(ci) : 0,
                                  H->objId ? H->objId(person) : 0, -25);
        }
        if (H->requestBuildOp71) H->requestBuildOp71(H->objId ? H->objId(person) : 0, 0, 0, 0);
        if (H->requestBuildOp77) H->requestBuildOp77(H->objId ? H->objId(person) : 0);
        if (H->requestNamedObject53)
            H->requestNamedObject53(H->objId ? H->objId(person) : 0, 0, -1, 1, "Entlassen");
        if (H->freeHandlerEntry) H->freeHandlerEntry(h);
        return reinterpret_cast<intptr_t>(person);
    }
    He_State(h) = -1;
    return reinterpret_cast<intptr_t>(person);
}

// ===========================================================================
// gilde.exe 0x4e73c0 — VIBE_NpcAction_BroadcastMoveToTargetsStep.
//   State <-2 ignore; -2/-1 free. State 0: resolve the rally leader (+16). For
//   each city slot whose marker != -1 and whose aux person == leader, broadcast
//   a coord27 with delta = 20 * (+172 / MoneyMultiplyByRate(1000, currency)).
//   Then, if the caller's own city kind is host (6/7), send a quickjump and free.
// ===========================================================================
i32 NpcAction11_BroadcastMoveToTargetsStep(HeRecord* h) {
    const NpcAction11Hooks* H = g_h11;

    i32 state = He_State(h);
    if (state < -1) {
        if (state != -2) return state;
        if (H->freeHandlerEntry) H->freeHandlerEntry(h);
        return state;
    }
    if (state <= -1) {
        if (H->freeHandlerEntry) H->freeHandlerEntry(h);
        return state;
    }
    if (state != 0)
        return state;

    void* leader = H->personQueryBegin ? H->personQueryBegin(F32(h, 16)) : nullptr;
    if (!leader) {
        if (H->freeHandlerEntry) H->freeHandlerEntry(h);
        return 0;
    }
    i32 rate = H->moneyMultiplyByRate ? H->moneyMultiplyByRate(1000, 0) : 1;
    int delta = 20 * (rate ? (F32(h, 172) / rate) : 0);
    for (u16 ci = 0; ci < 768; ++ci) {
        if ((H->cityMarker ? (i16)H->cityMarker(ci) : -1) == -1)
            continue;
        void* aux = H->cityPersonRecord ? H->cityPersonRecord(ci) : nullptr;
        if (aux == leader && H->requestCoord27)
            H->requestCoord27(H->cityId ? H->cityId(He_CityIndex(h)) : 0,
                              H->cityId ? H->cityId(ci) : 0, delta);
    }
    u8 k = H->cityKind ? H->cityKind(He_CityIndex(h)) : 0;
    if (k == 6 || k == 7) {
        if (H->sendQuickjump)
            H->sendQuickjump(H->cityId ? H->cityId(He_CityIndex(h)) : 0, 5562,
                             H->objId ? H->objId(leader) : 0, nullptr);
    }
    if (H->freeHandlerEntry) H->freeHandlerEntry(h);
    return 0;
}

// ===========================================================================
// gilde.exe 0x4e4834 — VIBE_NpcAction_DropObjectStep.
//   State <-2 ignore; -2 or -1: query person, emit arg25(90,0,2,64), free. State
//   0: resolve person; if the caller's own city kind is 6 (host) send a host
//   quickjump (5114). If kind 5 or 6, walk all 768 city slots and quickjump any
//   host peer (6/7) that isn't the caller (6207). Then single58 + arg25 and free.
// ===========================================================================
i32 NpcAction11_DropObjectStep(HeRecord* h) {
    const NpcAction11Hooks* H = g_h11;

    i32 state = He_State(h);
    if (state < -1) {
        if (state != -2) return state;
        // fallthrough to the free path
    } else if (state > 0) {
        return state;
    }

    if (state != 0) {
        // state -2 / -1
        void* person = H->personQueryBegin ? H->personQueryBegin(F32(h, 172)) : nullptr;
        if (person && H->requestArgs25)
            H->requestArgs25(H->objId ? H->objId(person) : 0, 90, 0, 2, 64);
        if (H->freeHandlerEntry) H->freeHandlerEntry(h);
        return 0;
    }

    // state 0
    void* person = H->personQueryBegin ? H->personQueryBegin(F32(h, 172)) : nullptr;
    if (person) {
        u16 selfCi = He_CityIndex(h);
        u8 selfKind = H->cityKind ? H->cityKind(selfCi) : 0;
        if (selfKind == 6) {
            if (H->sendQuickjump)
                H->sendQuickjump(H->cityId ? H->cityId(selfCi) : 0, 5114,
                                 H->objId ? H->objId(person) : 0, "_NACHRICHTEN_HS_51");
        }
        if (selfKind == 5 || selfKind == 6) {
            for (u16 ci = 0; ci < 768; ++ci) {
                u8 k = H->cityKind ? H->cityKind(ci) : 0;
                if ((k == 6 || k == 7) && ci != selfCi) {
                    if (H->sendQuickjump)
                        H->sendQuickjump(H->cityId ? H->cityId(ci) : 0, 6207,
                                         H->objId ? H->objId(person) : 0,
                                         "_NACHRICHTEN_HS_52");
                }
            }
        }
        if (H->requestSingle58) H->requestSingle58(H->objId ? H->objId(person) : 0);
        if (H->requestArgs25) H->requestArgs25(H->objId ? H->objId(person) : 0, 90, 0, 2, 64);
    }
    if (H->freeHandlerEntry) H->freeHandlerEntry(h);
    return 0;
}

// ===========================================================================
// gilde.exe 0x4e454c — VIBE_NpcAction_PickupObjectStep.
//   State <-2 ignore; -2/-1: query person, emit arg25(90,0,2,4), free. State 0:
//   query person; compute elapsed minutes since +96, scale by 0.1*+ .5 mood, drop
//   that from +176, AdjustMood by it. If +176 still > 0 re-stamp +96/+82 and
//   Advance 10 minutes (keep waiting). Else (done) host quickjump (5086) if the
//   caller is a host kind, and set +112 = -1.
// ===========================================================================
void NpcAction11_PickupObjectStep(HeRecord* h) {
    const NpcAction11Hooks* H = g_h11;

    i32 state = He_State(h);
    if (state < -1) {
        if (state != -2) return;
        void* person = H->personQueryBegin ? H->personQueryBegin(F32(h, 172)) : nullptr;
        if (person && H->requestArgs25)
            H->requestArgs25(H->objId ? H->objId(person) : 0, 90, 0, 2, 4);
        if (H->freeHandlerEntry) H->freeHandlerEntry(h);
        return;
    }
    if (state <= -1) {
        void* person = H->personQueryBegin ? H->personQueryBegin(F32(h, 172)) : nullptr;
        if (person && H->requestArgs25)
            H->requestArgs25(H->objId ? H->objId(person) : 0, 90, 0, 2, 4);
        if (H->freeHandlerEntry) H->freeHandlerEntry(h);
        return;
    }
    if (state != 0)
        return;

    void* person = H->personQueryBegin ? H->personQueryBegin(F32(h, 172)) : nullptr;
    if (!person) {
        if (H->freeHandlerEntry) H->freeHandlerEntry(h);
        return;
    }
    int elapsed = GameTimeDiffMinutes(reinterpret_cast<GameTime*>(HeBytes(h) + 96),
                                      &NpcClock());
    double amt = (double)elapsed * kPickupMoodMul + kPickupMoodBias;
    int iamt = (int)amt;
    F32(h, 176) -= iamt;
    if (H->adjustMood) H->adjustMood(person, iamt);
    if (F32(h, 176) > 0) {
        StampClock(h, 96);
        StampClock(h, 82);
        Advance(h, 82, 0, 0, 10);
        return;
    }
    u16 selfCi = He_CityIndex(h);
    if (selfCi != 0xFFFF) {
        u8 k = H->cityKind ? H->cityKind(selfCi) : 0;
        if (k == 6 || k == 7) {
            if (H->sendQuickjump)
                H->sendQuickjump(H->cityId ? H->cityId(selfCi) : 0, 5086,
                                 H->objId ? H->objId(person) : 0, "_NACHRICHTEN_HS_68");
        }
    }
    He_State(h) = -1;
}

// ===========================================================================
// gilde.exe 0x4e4af4 — VIBE_NpcAction_UseObjectStep.
//   state+2 switch (states -2..1 collapse to "free", 2 = emit, 3 = finish):
//   0/1 -> query person, arg25(90,0,2,128), free. 2 -> query, request17 use +
//   arg25, advance state. 3 -> query, if the held object exists send host
//   quickjump (6200), free.
// ===========================================================================
void* NpcAction11_UseObjectStep(HeRecord* h) {
    const NpcAction11Hooks* H = g_h11;

    i32 sw = He_State(h) + 2;
    switch (sw) {
        case 0:
        case 1: {
            void* p = H->personQueryBegin ? H->personQueryBegin(F32(h, 172)) : nullptr;
            if (p && H->requestArgs25)
                H->requestArgs25(H->objId ? H->objId(p) : 0, 90, 0, 2, 128);
            if (H->freeHandlerEntry) H->freeHandlerEntry(h);
            return p;
        }
        case 2: {
            void* p = H->personQueryBegin ? H->personQueryBegin(F32(h, 172)) : nullptr;
            if (p) {
                if (H->request17)
                    H->request17(H->objId ? H->objId(p) : 0, -1, 1,
                                 (F32(h, 174) >> 16) & 0xFFFF, 0, 0);
                if (H->requestArgs25)
                    H->requestArgs25(H->objId ? H->objId(p) : 0, 90, 0, 2, 128);
            }
            ++He_State(h);
            return p;
        }
        case 3: {
            void* p = H->personQueryBegin ? H->personQueryBegin(F32(h, 172)) : nullptr;
            if (p && H->gameObjectQueryFind &&
                H->gameObjectQueryFind(H->objId ? H->objId(p) : 0, 1, 0,
                                       F32(h, 174) >> 16, 0)) {
                u16 selfCi = He_CityIndex(h);
                if (H->sendQuickjump)
                    H->sendQuickjump(H->cityId ? H->cityId(selfCi) : 0, 6200,
                                     H->objId ? H->objId(p) : 0, "_NACHRICHTEN_HS_53");
                if (H->freeHandlerEntry) H->freeHandlerEntry(h);
            } else {
                if (H->freeHandlerEntry) H->freeHandlerEntry(h);
            }
            return p;
        }
        default:
            return reinterpret_cast<void*>(static_cast<intptr_t>(He_State(h) + 2));
    }
}

// ===========================================================================
// gilde.exe 0x471b10 — VIBE_NpcAction_EvaluateUseBack (ranged-attack AI).
//   relFlag set -> 0. If the gun-cooldown bit (+485 & 2) is set AND
//   RandomModulo(8) != 0 -> 0 (still cooling). If no held object (+92==0) and not
//   in vehicle (+2!=3): build the {5,4,1}/{0,0,0} request and SelectBestRecursive
//   (40). Vehicle (+2==3): require +242 & 4, then ranged attack. Else: if a gun
//   object is found, ranged attack; otherwise build {2,21,1,...}/{4,+92.id,1} and
//   SelectBestRecursive(40). On any success copy 24 bytes into outA/outB.
// ===========================================================================
u8 NpcAction11_EvaluateUseBack(void* person, void* outA, u8 relFlag, void* outB) {
    const NpcAction11Hooks* H = g_h11;

    if (relFlag)
        return 0;
    if (H->personEquipState && (H->personEquipState(person, 485) & 2) &&
        (H->randomModulo ? H->randomModulo(8) : 0))
        return 0;

    i32 held = H->field92 ? H->field92(person) : 0;
    u8 kindByte = H->kind ? H->kind(person) : 0;

    if (!held && kindByte != 3) {
        // request {5,4,1} / score {0,0,0}; SelectBestRecursive(40)
        u8 r = H->selectBestRecursive
            ? H->selectBestRecursive(40, H->markerWord ? H->markerWord(person) : 0, outA, outB)
            : 0;
        return r;   // outA/outB already the scratch the hook filled
    }

    if (kindByte == 3) {
        // vehicle: require the +242 & 4 mounted-weapon bit, then ranged attack
        if (!(H->personEquipState && (H->personEquipState(person, 484) & 4)))
            return 0;
        if (H->tryRangedAttack && H->tryRangedAttack(person, outB))
            return 42;
        return 0;
    }

    // held object present, not vehicle: look for a gun object on the carrier
    if (H->gameObjectQueryFind &&
        H->gameObjectQueryFind(held, 2, 6, 0, 21)) {
        if (H->tryRangedAttack && H->tryRangedAttack(person, outB))
            return 42;
        return 0;
    }
    // fall back to the AI method selection
    u8 r = H->selectBestRecursive
        ? H->selectBestRecursive(40, H->markerWord ? H->markerWord(person) : 0, outA, outB)
        : 0;
    return r;
}

// ===========================================================================
// gilde.exe 0x568650 — VIBE_NpcAction_BeginFriendship.
//   If the caller is a host (+2 == 6): open the relation-overview window to pick a
//   friend candidate. Else resolve the relation context's first party. If no
//   candidate, return 0. Otherwise queue a coord27(+25) approach; if the candidate
//   is a host kind (6/7) send the friendship notice (3246). Return 1.
// ===========================================================================
i32 NpcAction11_BeginFriendship(void* self, void* relCtx) {
    const NpcAction11Hooks* H = g_h11;

    void* target;
    u8 selfKind = H->kind ? H->kind(self) : 0;
    if (selfKind == 6) {
        target = H->runOfficeOverviewWindow ? H->runOfficeOverviewWindow(1) : nullptr;
    } else {
        target = (relCtx && H->findPersonById)
            ? H->findPersonById(H->spouseId ? H->spouseId(relCtx, 0) : 0)
            : nullptr;
    }
    if (!target)
        return 0;

    if (H->requestCoord27)
        H->requestCoord27(H->objId ? H->objId(self) : 0,
                          H->objId ? H->objId(target) : 0, 25);
    u8 tk = H->kind ? H->kind(target) : 0;
    if (tk == 6 || tk == 7) {
        if (H->sendEntity) H->sendEntity(H->objId ? H->objId(target) : 0, 3246);
    }
    return 1;
}

// ===========================================================================
// gilde.exe 0x5692dc — VIBE_NpcAction_BeginDivorce.
//   If the caller is a host (+2 == 6): two relation-overview picks (spouse A then
//   spouse B). Else resolve both spouses from the relation context (+4 / +8). If
//   either party is absent, return 0. Otherwise queue mutual coord27(-25) recalls;
//   notify each host-kind party of the divorce (3251). Return 1.
// ===========================================================================
i32 NpcAction11_BeginDivorce(void* self, void* relCtx) {
    const NpcAction11Hooks* H = g_h11;

    void* a;
    void* b;
    u8 selfKind = H->kind ? H->kind(self) : 0;
    if (selfKind == 6) {
        a = H->runOfficeOverviewWindow ? H->runOfficeOverviewWindow(2) : nullptr;
        if (!a)
            return 0;
        b = H->runOfficeOverviewWindow ? H->runOfficeOverviewWindow(3) : nullptr;
    } else {
        a = (relCtx && H->findPersonById)
            ? H->findPersonById(H->spouseId ? H->spouseId(relCtx, 0) : 0) : nullptr;
        b = (relCtx && H->findPersonById)
            ? H->findPersonById(H->spouseId ? H->spouseId(relCtx, 1) : 0) : nullptr;
    }
    if (!b)
        return 0;

    if (H->requestCoord27) {
        H->requestCoord27(H->objId ? H->objId(a) : 0, H->objId ? H->objId(b) : 0, -25);
        H->requestCoord27(H->objId ? H->objId(b) : 0, H->objId ? H->objId(a) : 0, -25);
    }
    u8 ka = a && H->kind ? H->kind(a) : 0;
    if (ka == 6 || ka == 7) {
        if (H->sendEntity) H->sendEntity(H->objId ? H->objId(a) : 0, 3251);
    }
    u8 kb = b && H->kind ? H->kind(b) : 0;
    if (kb != 6 && kb != 7)
        return 1;
    if (H->sendEntity) H->sendEntity(H->objId ? H->objId(b) : 0, 3251);
    return 1;
}

// ===========================================================================
// gilde.exe 0x473e00 — VIBE_NpcAction_GrantAiCredit.
//   Requires giver byte == 4 and object byte == 20. Resolve the giver entity
//   (giver+4); abort if absent. Resolve the secondary entity (giver+8). Open a
//   building-action bracket ("AI Kredit"), transfer (cmd15) the credit amount
//   (obj+4), slot-reset-28; if the secondary entity exists write a delta raw field
//   (obj+4 at +57). Close the bracket. Always returns 0.
// ===========================================================================
u8 NpcAction11_GrantAiCredit(HeRecord* self, void* giver, void* obj) {
    const NpcAction11Hooks* H = g_h11;

    if ((H->kind ? H->kind(giver) : 0) != 4)
        return 0;
    if ((H->kind ? H->kind(obj) : 0) != 20)
        return 0;

    void* ent = nullptr;
    if (H->resolveEntity) H->resolveEntity(H->spouseId ? H->spouseId(giver, 0) : 0, &ent);
    if (!ent)
        return 0;
    void* ent2 = nullptr;
    if (H->resolveEntity) H->resolveEntity(H->spouseId ? H->spouseId(giver, 1) : 0, &ent2);

    if (H->buildingActionStart) H->buildingActionStart("AI Kredit");
    i32 amount = H->spouseId ? H->spouseId(obj, 0) : 0;   // *(obj+4)
    if (H->enqueueCmd15)
        H->enqueueCmd15(He_Id(self), H->objId ? H->objId(ent) : 0, amount, 0);
    if (H->requestSlotReset28) H->requestSlotReset28(nullptr);
    if (ent2) {
        if (H->beginDelta) H->beginDelta(ent2, H->objId ? H->objId(ent2) : 0);
        if (H->appendRawField) H->appendRawField(ent2, 57, amount);
        if (H->requestState22) H->requestState22();
    }
    if (H->buildingActionEnd) H->buildingActionEnd();
    return 0;
}

// ===========================================================================
// gilde.exe 0x473448 — VIBE_NpcAction_BuildWell.
//   Gate: an office-storage building must exist and the +358 office byte == 10.
//   If the action byte == 4 (upgrade): open "upgr_brunnen", cmd15 of
//   slotWorth*0.3, slot-reset-28, close. If != 4 (new build): open "bau_brunnen",
//   load the building graphic; on success cmd15 of slotWorth, close, return 47;
//   on failure close and return 0.
// ===========================================================================
u8 NpcAction11_BuildWell(HeRecord* h, void* building, u8* action) {
    const NpcAction11Hooks* H = g_h11;

    void* store = H->buildingFindOfficeStorage ? H->buildingFindOfficeStorage(1, h) : nullptr;
    if (!store || B8(h, 358) != 10)
        return 0;

    u8 act = action ? *action : 0;
    if (act == 4) {
        if (H->buildingActionStart) H->buildingActionStart("upgr_brunnen");
        i32 worth = H->buildingSumFlaggedSlotsWorth
            ? H->buildingSumFlaggedSlotsWorth(building ? (H->objId(building) >> 24) : 0)
            : 0;
        i32 cost = (i32)((double)worth * (double)kWellWorthMul);
        if (H->enqueueCmd15) H->enqueueCmd15(He_Id(h), 0, cost, 0);
        if (H->requestSlotReset28) H->requestSlotReset28(nullptr);
        if (H->buildingActionEnd) H->buildingActionEnd();
        return 47;
    }

    if (H->buildingActionStart) H->buildingActionStart("bau_brunnen");
    if (H->aiLoadBuildingGraphic && H->aiLoadBuildingGraphic(building, action)) {
        i32 worth = H->buildingSumFlaggedSlotsWorth
            ? H->buildingSumFlaggedSlotsWorth(building ? (H->objId(building) >> 24) : 0)
            : 0;
        if (H->enqueueCmd15) H->enqueueCmd15(-1, 0, worth, 0);
        if (H->buildingActionEnd) H->buildingActionEnd();
        return 47;
    }
    if (H->buildingActionEnd) H->buildingActionEnd();
    return 0;
}

} // namespace guild::sim
