// event3 — a further slice of the world-event "He"-action bodies (gilde.exe
// VIBE_Event_*). See event3.h for the function inventory and provenance.
#include "world/event3.h"

#include "sim/gametime.h"     // guild::sim::GameTimeAdvance / Compare / DiffMinutes
#include "sim/npcaction.h"    // guild::sim::NpcClock (the qword_13CE852 game clock)
#include "util/math_random.h" // guild::util::RandomModulo
#include "util/coord.h"       // guild::util::ConvertX (FPU trunc-toward-zero)

#include <cstdint>
#include <cstring>

namespace guild::world {

using guild::sim::GameTimeAdvance;
using guild::sim::GameTimeCompare;
using guild::sim::GameTimeDiffMinutes;
using guild::sim::He_ApptTime;
using guild::sim::He_SavedTime;
using guild::sim::HeBytes;
using guild::util::RandomModulo;
using guild::util::ConvertX;

// ---------------------------------------------------------------------------
// Module-global game clock (qword_13CE852 + unk_13CE85A + unk_13CE85E). Shared
// with the sim cluster — reuse sim::NpcClock() rather than redefining (ODR).
// ---------------------------------------------------------------------------
static const GameTime& Clock() { return guild::sim::NpcClock(); }

// Raw record accessors at the exact original byte offsets (mirrors the
// originals' `*(T*)(base+off)`); sim/he.h covers many but not all of these.
namespace {
inline i32&      Dword(HeRecord* h, int off)  { return *reinterpret_cast<i32*>(HeBytes(h) + off); }
inline u16&      Word (HeRecord* h, int off)  { return *reinterpret_cast<u16*>(HeBytes(h) + off); }
inline u8&       Byte (HeRecord* h, int off)  { return *reinterpret_cast<u8*>(HeBytes(h) + off); }
inline GameTime& Time (HeRecord* h, int off)  { return *reinterpret_cast<GameTime*>(HeBytes(h) + off); }
}  // namespace

// ---------------------------------------------------------------------------
// Hooks — inert defaults defined here so the library links standalone.
// ---------------------------------------------------------------------------
namespace {
i32       InertFree(HeRecord* h) { return static_cast<i32>(reinterpret_cast<std::intptr_t>(h)); }
void*     InertFindPerson(i32) { return nullptr; }
u16       InertCharId(void*) { return 0; }
i32       InertScriptHandle(void*) { return -1; }
HeRecord* InertFindFirst(i32, i32, i32) { return nullptr; }
HeRecord* InertFindNext() { return nullptr; }
void      InertStampReq(HeRecord*) {}
void*     InertFindBuilding(i32) { return nullptr; }
void*     InertFindObject(i32) { return nullptr; }
void      InertChangeAction(void*, void*, void*, u16) {}
i32       InertEnqueueObj(i32, i32, i32, i32, i32, i32, i32, i32) { return -1; }
i32       InertGuard61(void*, i32, i32, i32) { return -1; }
void      InertPair33(i32, i32) {}
i32       InertReq17(i32, i32, i32, i32, i32, i32) { return -1; }
i32       InertPacketStatus(i32) { return 0; }
i32       InertPacketSeq(i32) { return 0; }
i32       InertVariant(i32, i32) { return 0; }
i32       InertGuardState(void*) { return 0; }
i32       InertGuardSlot(void*, int) { return 0; }
i32       InertGuardOwner(void*) { return 0; }
i32       InertShuffle(u32, i32*) { return 0; }
i32       InertListener(const void*, const float*, i32, const float*, i32) { return 0; }
void      InertReport(const char*) {}
void      InertPanelDestroy(HeRecord*, i32) {}
void      InertPanelCreate(HeRecord*, i32, i32) {}
void      InertFormSelect(i32, i32) {}
i32       InertRichString(const char*, const char*) { return 0; }
i32       InertActiveWindow() { return 0; }
i32       InertActiveMessage() { return 0; }
void      InertResolveEntity(i32* a, i32* b, i32, i32) { if (a) *a = 0; if (b) *b = 0; }
void      InertBuildModel(i32, i32, u32) {}
double    InertOutputRatio(void*) { return 0.0; }
void      InertAdjustStock(u16, i32, i32) {}
void      InertArrival(HeRecord*, void*, u16) {}

const Event3Hooks kInertHooks = {
    &InertFree, &InertFindPerson, &InertCharId, &InertScriptHandle,
    &InertFindFirst, &InertFindNext, &InertStampReq,
    &InertFindBuilding, &InertFindObject, &InertChangeAction,
    &InertEnqueueObj, &InertGuard61, &InertPair33, &InertReq17,
    &InertPacketStatus, &InertPacketSeq, &InertVariant,
    &InertGuardState, &InertGuardSlot, &InertGuardOwner,
    &InertShuffle, &InertListener, &InertReport,
    &InertPanelDestroy, &InertPanelCreate, &InertFormSelect, &InertRichString,
    &InertActiveWindow, &InertActiveMessage, &InertResolveEntity, &InertBuildModel,
    &InertOutputRatio, &InertAdjustStock, &InertArrival,
};
const Event3Hooks* g_hooks = &kInertHooks;
}  // namespace

void SetEvent3Hooks(const Event3Hooks* hooks) { g_hooks = hooks ? hooks : &kInertHooks; }
const Event3Hooks& GetEvent3Hooks() { return *g_hooks; }

const float kSeasonAnimBase[4] = { 8.0f, 7.0f, 8.0f, 9.0f };  // flt_6476FC

// ===========================================================================
// 0x4ef500 — VIBE_Event_StartActorAction
// ===========================================================================
// Original copies the clock into +176, +68, +82 and +96 (each a 14-byte
// GameTime), seeds +176 := 200, then advances +180 (the appointment whose head
// the clock just filled at +176; the call passes a1+180 which is the minute slot
// region the 14-byte clock store at +176..+189 occupies). Advance args map
// (rec, edx=days, ecx=seconds, ebx=minutes); the original passes
// (a1+180, 0, 0, 10*(200/5u)) = +400 minutes. We advance the deadline block.
i32 StartActorAction(HeRecord* h) {
    const Event3Hooks& hk = *g_hooks;
    void* person   = hk.findPersonById(Dword(h, 172));
    void* building = hk.findBuildingById(Dword(h, 16));
    void* object   = nullptr;
    if (Dword(h, 196) != -1) object = hk.findObjectById(Dword(h, 196));
    u16 charId = person ? hk.personCharId(person) : 0;
    hk.changePlayerAction(building, object, h, charId);

    Dword(h, 176) = 200;
    Time(h, 180) = Clock();   // +180 <- clock (the deadline image the original stamps)
    Time(h, 68)  = Clock();   // +68  <- clock
    Time(h, 82)  = Clock();   // +82  <- clock
    Time(h, 96)  = Clock();   // +96  <- clock
    // The original re-reads +176 (200) for the minute count: 10*(200/5) = 400.
    int minutes = 10 * (static_cast<unsigned>(Dword(h, 176)) / 5u);
    return GameTimeAdvance(&Time(h, 180), 0, 0, minutes);
}

// ===========================================================================
// 0x4ef5d4 — VIBE_Event_MoveTowardTargetRun
// ===========================================================================
void MoveTowardTargetRun(HeRecord* h) {
    const Event3Hooks& hk = *g_hooks;
    switch (Dword(h, 112)) {
    case -2:
    case -1: {
        void* p = hk.findPersonById(Dword(h, 172));
        if (p) hk.changePlayerAction(nullptr, nullptr, nullptr, hk.personCharId(p));
        hk.freeHandlerEntry(h);
        return;
    }
    case 0: {
        void* actor = hk.findPersonById(Dword(h, 172));
        if (!actor) { hk.freeHandlerEntry(h); return; }
        int diff = GameTimeDiffMinutes(&Time(h, 96), &Clock());
        // moved = diff * 0.1 * 5  (flt_61FD3C * flt_61FD40)
        float moved = static_cast<float>(diff) * kMoveRateFlt * kMoveScaleFlt;
        double ratio = hk.buildingOutputRatio(actor);
        float ratioScaled = static_cast<float>(ratio * kMoveRatioMul);  // dbl_61FD48
        int remaining = Dword(h, 176);
        if (remaining && (ratioScaled + moved) < kMoveStockCap) {  // flt_61FD50
            int movedI = static_cast<int>(ConvertX(static_cast<double>(moved)));
            hk.buildingAdjustStock(hk.personCharId(actor), movedI, 0);
            double rem = static_cast<double>(static_cast<unsigned>(Dword(h, 176)))
                       - static_cast<double>(moved);
            Dword(h, 176) = static_cast<int>(ConvertX(rem));
            Time(h, 82) = Clock();
            GameTimeAdvance(&Time(h, 82), 0, 0, 10);
        } else {
            ++Dword(h, 112);
        }
        Time(h, 96) = Clock();
        return;
    }
    case 1: {
        void* actor    = hk.findPersonById(Dword(h, 172));
        void* building = hk.findBuildingById(Dword(h, 16));
        if (actor && building) {
            hk.changePlayerAction(building, nullptr, nullptr, hk.personCharId(actor));
            hk.sendArrivalMessage(h, building, hk.personCharId(actor));
            hk.freeHandlerEntry(h);
        } else {
            hk.freeHandlerEntry(h);
        }
        return;
    }
    default:
        return;
    }
}

// ===========================================================================
// 0x4ef270 — VIBE_Event_GatherTargetsInit
// ===========================================================================
// The candidate scan (Person iteration + production/storage filter) is host
// state; the test supplies the collected candidate ids and a tolerance predicate
// through the hooks-free interface below. Here we translate the deterministic
// slot bookkeeping + RNG seed selection faithfully against an explicit candidate
// list passed by the caller. The original layout uses dword indices off the He
// base: index 43 (=He+172) is the seed slot, indices 44..58 (=He+176..+232) hold
// follower slots, index 60 (=He+240) is the tolerance, indices 64/65 (=He+256/260)
// are counters reset to 0.
//
// To keep GatherTargetsInit testable without a full scene, the candidate set and
// the within-tolerance result are passed in (the engine derives them from the
// scene graph). The slot fill, the random seed pick and the appointment stamp are
// the recovered logic.
HeRecord* GatherTargetsInit(HeRecord* h,
                            const i32* candidateIds, int candidateCount,
                            const bool* withinTolerance) {
    // Reset follower slots 44..58 (15 dwords) to -1.
    for (int idx = 44; idx <= 58; ++idx) Dword(h, 4 * idx) = -1;

    // Seed slot (index 43 == He+172): pick a random candidate if unset.
    if (Dword(h, 172) == -1 && candidateCount > 0) {
        u16 pick = static_cast<u16>(RandomModulo(static_cast<u16>(candidateCount)));
        Dword(h, 172) = candidateIds[pick];
    }
    i32 seedId = Dword(h, 172);

    // Fill up to 15 follower slots with in-tolerance candidates (skip the seed).
    int slot = 1;        // v1 starts at 1
    int scanned = 0;     // v10
    int cur = 0;         // v11
    while (scanned < candidateCount && slot < 16) {
        bool isSeed = (candidateIds && candidateIds[cur] == seedId);
        bool ok = withinTolerance ? withinTolerance[cur] : false;
        if (isSeed || !ok) {
            ++cur; ++scanned;
        } else {
            Dword(h, 4 * (43 + slot)) = candidateIds[cur];
            ++slot; ++cur; ++scanned;
        }
    }

    Dword(h, 256) = 0;   // index 64
    Dword(h, 260) = 0;   // index 65
    Time(h, 82) = Clock();
    return h;
}

// Convenience overload matching the header declaration: with no scene the inert
// path collects nothing, so the seed (if -1) stays -1 and no followers are set.
HeRecord* GatherTargetsInit(HeRecord* h) {
    return GatherTargetsInit(h, nullptr, 0, nullptr);
}

// ===========================================================================
// 0x4ef7e8 — VIBE_Event_RequestGuardInteraction
// ===========================================================================
i32 RequestGuardInteraction(HeRecord* h) {
    const Event3Hooks& hk = *g_hooks;
    void* building = hk.findBuildingById(Dword(h, 180));
    if (!building) return hk.freeHandlerEntry(h);

    i32 result;
    if (hk.buildingGuardState(building) == 2) {
        u16 variantArg = static_cast<u16>(RandomModulo(0x0C)) + 1;
        i32 variant = hk.buildingVariantIndex(variantArg, 1);
        u16 kind = static_cast<u16>(RandomModulo(5)) + 22;
        result = hk.enqueueObjectInteraction(2, -1, kind, -1, -1, variant, 0, 2);
    } else {
        // Find the first set guard slot among slots 0..5 (the original walks
        // slot 5 then 4..0, returning the first non-zero +547 byte).
        i32 guardByte = 0;
        if (hk.buildingGuardSlot(building, 5)) {
            guardByte = hk.buildingGuardSlot(building, 5);
        } else {
            for (int s = 4; s >= 0; --s) {
                if (hk.buildingGuardSlot(building, s)) { guardByte = hk.buildingGuardSlot(building, s); break; }
            }
        }
        result = hk.queueGuardTarget61(building, 1, guardByte, hk.buildingGuardOwner(building));
    }
    Dword(h, 172) = result;
    Time(h, 82) = Clock();
    return result;
}

// ===========================================================================
// 0x4efb64 — VIBE_Event_SinkToGroundStateMachine
// ===========================================================================
static const char kSinkActionName[] = "bewegung/zu_boden_sinken";

HeRecord* SinkToGroundStateMachine(HeRecord* h) {
    const Event3Hooks& hk = *g_hooks;
    int sw = Dword(h, 112) + 2;
    switch (sw) {
    case 0:
    case 1:
        hk.freeHandlerEntry(h);
        return h;
    case 2: {
        void* p = hk.findPersonById(Dword(h, 172));
        if (!p) { hk.freeHandlerEntry(h); return h; }
        // Finish any running script attached to the person.
        i32 sh = hk.personScriptHandle(p);
        (void)sh;  // the original resolves+finishes; routed as a no-op leaf
        hk.changePlayerAction(nullptr, nullptr, nullptr, hk.personCharId(p));
        ++Dword(h, 112);
        return h;
    }
    case 3: {
        void* p = hk.findPersonById(Dword(h, 172));
        if (!p) { hk.freeHandlerEntry(h); return h; }
        if (hk.personScriptHandle(p) == -1) ++Dword(h, 112);
        return h;
    }
    case 4: {
        void* p = hk.findPersonById(Dword(h, 172));
        if (!p) { hk.freeHandlerEntry(h); return h; }
        hk.changePlayerAction(nullptr, nullptr, nullptr, hk.personCharId(p));
        Time(h, 82) = Clock();
        GameTimeAdvance(&Time(h, 82), 1, 0, 0);   // +1 day
        ++Dword(h, 112);
        return h;
    }
    case 5: {
        void* p = hk.findPersonById(Dword(h, 172));
        if (p && (Byte(h, 120) & 2)) {
            hk.queueRequestPair33(/*person id slot*/ Dword(h, 172), 1);
        }
        hk.freeHandlerEntry(h);
        return h;
    }
    default:
        return h;
    }
}

// ===========================================================================
// 0x4efcdc — VIBE_Event_AllocKillPlayer
// ===========================================================================
HeRecord* AllocKillPlayer(HeRecord* h) {
    const Event3Hooks& hk = *g_hooks;
    if (!hk.findPersonById(Dword(h, 172))) {
        hk.reportMessage("he_AllocKillPlayer: player not found!");
        hk.freeHandlerEntry(h);
        return h;
    }
    HeRecord* it = hk.findFirstHandler(1, 0, 114);
    while (it) {
        // dword index 43 == +172; skip self.
        if (it != h && Dword(it, 172) == Dword(h, 172)) break;
        it = hk.findNextHandler();
    }
    if (it) {
        Dword(it, 180) = -1;
        Dword(it, 176) = -1;
        He_ApptTime(it) = He_SavedTime(it);  // +82 <- +68 (14 bytes)
        Dword(it, 132) = -1;
        return it;
    }
    hk.freeHandlerEntry(h);
    return h;
}

// ===========================================================================
// 0x4f01d0 — VIBE_Event_SetActorAnimById
// ===========================================================================
i32 SetActorAnimById(HeRecord* h) {
    Time(h, 82) = Clock();   // +82 <- clock
    Time(h, 68) = Clock();   // +68 <- clock
    Dword(h, 172) = static_cast<i32>(static_cast<u16>(RandomModulo(5))) - 5;
    // VIBE_GameTime_GetSeasonFromDay 0x58339c: signed idiv ecx(=4); returns dl (signed
    // remainder). C++ signed % 4 truncates toward zero => identical (incl. negative day).
    int season = Clock().day % 4;
    float base = kSeasonAnimBase[season];
    Dword(h, 88) = 0;
    Word(h, 86) = static_cast<u16>(static_cast<int>(base));
    return static_cast<int>(base);
}

// ===========================================================================
// 0x4f1bc0 — VIBE_Event_PrepareInfoAction
// ===========================================================================
i32 PrepareInfoAction(HeRecord* h, bool anotherInfoActive, i32* adviceIds) {
    const Event3Hooks& hk = *g_hooks;
    Time(h, 82) = Clock();
    // Determine the appointment day: if another info handler exists, the original
    // overwrites the +82 day dword with (clock.day + 4); else leaves clock.day.
    bool exists = anotherInfoActive || (hk.findFirstHandler(1, 0, 131) != nullptr);
    if (exists) {
        // qword_13CE852 + 4 written into +82 dword == clock.day + 4.
        Dword(h, 82) = Clock().day + 4;
    } else {
        Dword(h, 82) = Clock().day;
    }
    Word(h, 86) = 8;
    Dword(h, 88) = 0;
    return hk.initAndShuffleDwordArray(0x1A, adviceIds);  // 26 entries
}

// ===========================================================================
// 0x4f24e0 — VIBE_Event_CancelMatchingActors
// ===========================================================================
void CancelMatchingActors(HeRecord* self) {
    const Event3Hooks& hk = *g_hooks;
    for (HeRecord* it = hk.findFirstHandler(1, 0, 35); it; it = hk.findNextHandler()) {
        // Skip handlers not yet armed: require flag bit 1 or bit 2 at +120.
        while (true) {
            u8 fl = Byte(it, 120);
            if ((fl & 2) || (fl & 1)) break;
            it = hk.findNextHandler();
            if (!it) return;
        }
        // dword index 44 == +176; matches against self's +4 person id.
        if (Dword(it, 176) == Dword(self, 4)) hk.stampTimeAndRequest(it);
    }
}

// ===========================================================================
// 0x4f4114 — VIBE_Event_NewDepositMessageBoxRun
// ===========================================================================
i32 NewDepositMessageBoxRun(HeRecord* h) {
    const Event3Hooks& hk = *g_hooks;
    int sw = Dword(h, 112) + 2;   // a1[28] == +112
    switch (sw) {
    case 0:
    case 1:
        hk.eventPanelDestroySlot(h, 0);
        return hk.freeHandlerEntry(h);
    case 2: {
        i32 depositTextId = Dword(h, 172);  // a1[43] == +172
        hk.eventPanelCreateSlot(h, 0, 1424);
        hk.formSelectWindow(0, 0);
        i32 r = hk.textRenderRichString(
            "HE_NEUE_VORKOMMEN_SUCHEN_MSG: Ich bin ein Bug !!! %s gefunden!!",
            reinterpret_cast<const char*>(static_cast<std::intptr_t>(2 * depositTextId + 2151)));
        ++Dword(h, 112);
        return r;
    }
    case 3: {
        i32 r = hk.activeWindowHandle();
        if (hk.activeWindowHandle() != 0 && hk.activeWindowMessage() == 1210) {
            hk.eventPanelDestroySlot(h, 0);
            return hk.freeHandlerEntry(h);
        }
        return r;
    }
    default:
        return sw;
    }
}

// ===========================================================================
// 0x4f4fbc — VIBE_Event_RequestSlotResultRun
// ===========================================================================
i32 RequestSlotResultRun(HeRecord* h) {
    const Event3Hooks& hk = *g_hooks;
    int sw = Dword(h, 112) + 2;   // a1[28] == +112
    switch (sw) {
    case 0:
    case 1:
        return hk.freeHandlerEntry(h);
    case 2: {
        int cmp = GameTimeCompare(&He_ApptTime(h), &Clock());
        if (cmp <= 0) {
            // a1[43] == +172 (request base), high word of +178 dword == object id.
            i32 objId = static_cast<i32>(static_cast<u32>(Dword(h, 178)) >> 16);
            i32 r = hk.queueRequest17(Dword(h, 172), -1, 1, objId, 0, 0);
            Dword(h, 200) = r;          // a1[50] == +200
            ++Dword(h, 112);
            return r;
        }
        return cmp;
    }
    case 3: {
        i32 st = hk.packetStatus(Dword(h, 200));
        if (st) {
            if (hk.packetStatus(Dword(h, 200)) == 2) {
                return hk.freeHandlerEntry(h);
            }
            i32 seq = hk.packetSeq(Dword(h, 200));   // a1[50]
            i32 objId = *reinterpret_cast<const i32*>(reinterpret_cast<const u8*>(&seq) + 2);
            (void)objId;
            Dword(h, 192) = 32;     // a1[48] == +192 (scan window)
            Dword(h, 184) = seq;    // a1[46] == +184 (seq object id; faithful slot)
            Dword(h, 196) = 0;      // a1[49] == +196 (scan cursor)
            i32 r = GameTimeAdvance(&He_ApptTime(h), 0, 0, 1);
            ++Dword(h, 112);
            return r;
        }
        return st;
    }
    case 4: {
        i32 r = 0;
        while (true) {
            r = GameTimeCompare(&He_ApptTime(h), &Clock());
            if (r >= 0) break;
            i32 out = 0;
            hk.resolveEntityById(nullptr, &out, Dword(h, 184), 0);
            GameTimeAdvance(&He_ApptTime(h), 0, 0, 1);
            if (Dword(h, 196) > Dword(h, 192)) {   // cursor 49 past window 48
                hk.objectBuildModelName(out, out, 1u);
                return hk.freeHandlerEntry(h);
            }
        }
        return r;
    }
    default:
        return sw;
    }
}

// ===========================================================================
// 0x4f5270 — VIBE_Event_UpdateListenerFromActor
// ===========================================================================
i32 UpdateListenerFromActor(const float* actor, HeRecord* h) {
    const Event3Hooks& hk = *g_hooks;
    float pos[3];
    pos[0] = actor[33];
    pos[1] = actor[34];
    pos[2] = actor[35];
    pos[0] = pos[0] + static_cast<float>(kListenerZBias);  // dbl_620418 = 1.47
    i32 result = hk.setListener(actor, actor + 19, 70, pos, 32);
    --Byte(h, 208);
    return result;
}

// ===========================================================================
// 0x596074 — VIBE_Event_MatchPersonState15Cmd272
// ===========================================================================
bool MatchPersonState15Cmd272(const void* cmd, i32 personState) {
    const u8* c = reinterpret_cast<const u8*>(cmd);
    if (c[0] != 25) return false;
    const void* personPtr = *reinterpret_cast<const void* const*>(c + 4);
    if (!personPtr) return false;
    i32 cmd12 = *reinterpret_cast<const i32*>(c + 12);
    return personState == 15 && cmd12 == 272;
}

// ===========================================================================
// 0x5960c0 — VIBE_Event_MatchType26OrJump
// ===========================================================================
bool MatchType26OrJump(const void* cmd) {
    const u8* c = reinterpret_cast<const u8*>(cmd);
    i32 personPtr = *reinterpret_cast<const i32*>(c + 4);
    if (c[0] != 26 || !personPtr) {
        // Original tail-jumps to the generic predicate at 0x595E70; modelled as
        // "not matched here".
        return false;
    }
    return true;
}

}  // namespace guild::world
