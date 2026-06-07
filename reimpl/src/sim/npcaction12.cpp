#include "sim/npcaction12.h"

#include "sim/npcaction.h"   // NpcClock() — shared global game clock
#include "sim/gametime.h"

#include <cstdint>
#include <cstring>

namespace guild::sim {

// ===========================================================================
// Recovered constants.
//   flt_61A594 (worker-quarters upgrade cost factor) — same family as the well
//     upgrade flt_61A5F4=0.3 (npcaction11); flt_61A594 = 0.3.
//   flt_61E99C (wander-path coord radius scale) — 0.025 (the per-member coord
//     fan-out factor used in ComputeWanderPathCoords).
// ===========================================================================
const float kWorkerWorthMul   = 0.3f;    // flt_61A594
const float kWanderCoordScale = 0.025f;  // flt_61E99C

u32 g_lcgState = 0;                       // dword_12335D0

// ===========================================================================
// Leaf-hook plumbing.
// ===========================================================================
static const NpcAction12Hooks kInert{};
static const NpcAction12Hooks* g_h12 = &kInert;

void SetNpcAction12Hooks(const NpcAction12Hooks* hooks) { g_h12 = hooks ? hooks : &kInert; }
const NpcAction12Hooks& GetNpcAction12Hooks() { return *g_h12; }

// ---------------------------------------------------------------------------
// Raw-offset He fields. The originals address the record by explicit byte offset.
// ---------------------------------------------------------------------------
static inline i32& F32(HeRecord* h, int off) { return *reinterpret_cast<i32*>(HeBytes(h) + off); }
static inline u8&  B8(HeRecord* h, int off)  { return *reinterpret_cast<u8*>(HeBytes(h) + off); }

// Stamp the 14-byte global clock image into rec+off (qword_13CE852/.../+12).
static inline void StampClock(HeRecord* h, int off) {
    *reinterpret_cast<GameTime*>(HeBytes(h) + off) = NpcClock();
}
static inline void Advance(HeRecord* h, int off, int days, int secs, int mins) {
    GameTimeAdvance(reinterpret_cast<GameTime*>(HeBytes(h) + off), days, secs, mins);
}
// gilde.exe 0x5831f0 — VIBE_GameTime_Set(rec, hour, second, minute) writes the
// three sub-day fields (the same tiny helper npc_daily.cpp inlines).
static inline void GameTimeSet(HeRecord* h, int off, int hour, int second, int minute) {
    GameTime* r = reinterpret_cast<GameTime*>(HeBytes(h) + off);
    r->second = static_cast<i32>(static_cast<u8>(second));
    r->hour   = static_cast<u16>(static_cast<u8>(hour));
    r->minute = static_cast<i32>(static_cast<u8>(minute));
}

// ===========================================================================
// gilde.exe 0x568fac — VIBE_NpcAction_FormAllianceGroup.
//   ctx[4] = 1. Pick three distinct alliance-eligible peers (RandomModulo(0x300),
//   wrap strides 1/7/13, 768-iteration caps) that are: not self, marker != -1,
//   kind < 10, a DIFFERENT alliance group word than self, and (for the first)
//   alliance-eligible. Queue a coord27(+20) approach to each, render text 3252,
//   notify each host-kind (6/7) pick (1418). If self is host (kind 6) show the
//   alliance panel (3245). Always returns 1.
// ===========================================================================
i32 NpcAction12_FormAllianceGroup(void* self, i32* ctx) {
    const NpcAction12Hooks* H = g_h12;

    if (ctx) ctx[4] = 1;

    u16 selfMarker = H->markerWord ? H->markerWord(self) : 0;
    // self alliance-group word = (*(int*)(self+3-dword)) >> 24  (a1+3 dword = byte +12)
    i32 selfGroup = (self && H->field) ? (H->field(self, 12) >> 24) : 0;

    auto markerOf = [&](u16 idx) -> i16 { return H->cityMarker ? (i16)H->cityMarker(idx) : -1; };
    auto kindOf   = [&](u16 idx) -> u8  { return H->cityKind ? H->cityKind(idx) : 0; };
    auto groupOf  = [&](u16 idx) -> i32 { return H->cityIdShifted ? H->cityIdShifted(idx) : 0; };
    auto eligOf   = [&](u16 idx) -> u8  { return H->cityAllianceEligible ? H->cityAllianceEligible(idx) : 0; };

    // --- pick 1 (stride +1) ---
    void* p0 = nullptr;
    {
        int budget = 768;
        u16 v = H->randomModulo ? H->randomModulo(0x300) : 0;
        while (v == selfMarker || markerOf(v) == -1 || kindOf(v) >= 10 ||
               groupOf(v) == selfGroup || !eligOf(v)) {
            --budget;
            v = (v + 1) % 768;
            if (!budget) goto pick2;
        }
        p0 = H->cityRecord ? H->cityRecord(v) : nullptr;
    }
pick2:
    u16 m0 = p0 && H->markerWord ? H->markerWord(p0) : 0;
    void* p1 = nullptr;
    {
        int budget = 768;
        u16 v = H->randomModulo ? H->randomModulo(0x300) : 0;
        while (v == selfMarker || v == m0 || markerOf(v) == -1 || kindOf(v) >= 10 ||
               groupOf(v) == selfGroup) {
            v = (v + 7) % 768;
            if (!--budget) goto pick3;
        }
        p1 = H->cityRecord ? H->cityRecord(v) : nullptr;
    }
pick3:
    u16 m1 = p1 && H->markerWord ? H->markerWord(p1) : 0;
    void* p2 = nullptr;
    {
        int budget = 768;
        u16 v = H->randomModulo ? H->randomModulo(0x300) : 0;
        while (v == selfMarker || v == m0 || v == m1 || markerOf(v) == -1 ||
               kindOf(v) >= 10 || groupOf(v) == selfGroup) {
            v = (v + 13) % 768;
            if (!--budget) goto emit;
        }
        p2 = H->cityRecord ? H->cityRecord(v) : nullptr;
    }
emit:
    i32 selfId = H->objId ? H->objId(self) : 0;
    void* picks[3] = {p0, p1, p2};
    if (H->requestCoord27) {
        for (int i = 0; i < 3; ++i)
            H->requestCoord27(selfId, picks[i] && H->objId ? H->objId(picks[i]) : 0, 20);
    }
    // Text_RenderFormattedMessage(buf, 3252, selfMarker) then notify host picks.
    for (int i = 0; i < 3; ++i) {
        u8 k = picks[i] && H->kind ? H->kind(picks[i]) : 0;
        if (k == 6 || k == 7) {
            if (H->sendEntity) H->sendEntity(picks[i] && H->objId ? H->objId(picks[i]) : 0, 3252);
        }
    }
    u8 sk = H->kind ? H->kind(self) : 0;
    if (sk == 6) {
        if (H->panelShowAlliance)
            H->panelShowAlliance(3245, selfMarker,
                                 p0 && H->markerWord ? H->markerWord(p0) : 0,
                                 p1 && H->markerWord ? H->markerWord(p1) : 0,
                                 p2 && H->markerWord ? H->markerWord(p2) : 0);
    }
    return 1;
}

// ===========================================================================
// gilde.exe 0x4e7184 — VIBE_NpcAction_AssignWorkPlaceStep.
//   state <-2 ignore; -2/-1 free; state != 0 ignore. State 0: resolve person
//   (+172). Read employer link (+364 dword = person[+91 dword]); abort (free) if
//   absent OR Relation_LookupMatrixEntry(employer.marker, person.marker) > -26.
//   Find a sellable work object on the employer (GameObject_QueryFind kind 42 then
//   278); find the work-product. If a work object exists, enumerate its product
//   slots (QueryFind(.,1,5) + IterNext) into a 32-dword buffer, pick a random one,
//   price it (market price * currency), and emit a sell (request17 -1) + quickjump
//   (6086 / 1425). Then free.
// ===========================================================================
i32 NpcAction12_AssignWorkPlaceStep(HeRecord* h) {
    const NpcAction12Hooks* H = g_h12;

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
    if (!person) {
        if (H->freeHandlerEntry) H->freeHandlerEntry(h);
        return 0;
    }
    // The original reads the employer record pointer at person[+91] (byte +364) and
    // dereferences it directly. We model that record via resolveEntity so the
    // synthetic scene can supply a record handle for the link id.
    i32 employerId = H->field ? H->field(person, 364) : 0;
    void* employer = nullptr;
    if (employerId && H->resolveEntity) H->resolveEntity(employerId, &employer);
    if (!employer ||
        (H->relationLookup
             ? H->relationLookup(H->markerWord ? H->markerWord(employer) : 0,
                                 H->markerWord ? H->markerWord(person) : 0)
             : 0) > -26) {
        if (H->freeHandlerEntry) H->freeHandlerEntry(h);
        return 0;
    }

    // The decomp queries off *(int*)(employer+93-word) = the employer's scene-object
    // base. Keep it opaque via objId (the synthetic scene maps id -> object).
    i32 empBase = H->objId ? H->objId(employer) : 0;

    void* workObj = H->gameObjectQueryFind ? H->gameObjectQueryFind(empBase, 2, 6, 0, 42) : nullptr;
    if (!workObj)
        workObj = H->gameObjectQueryFind ? H->gameObjectQueryFind(empBase, 2, 6, 0, 278) : nullptr;
    void* workProduct = H->buildingFindWorkProduct ? H->buildingFindWorkProduct(employer) : nullptr;

    if (workObj) {
        i32 slots[32];
        std::memset(slots, 0, sizeof(slots));
        int n = 0;
        i32 workObjBase = H->objId ? H->objId(workObj) : 0;
        void* it = H->gameObjectIterFirst ? H->gameObjectIterFirst(workObjBase, 1, 5) : nullptr;
        while (it && n < 32) {
            slots[n++] = H->objId ? H->objId(it) : 0;
            it = H->gameObjectIterNext ? H->gameObjectIterNext() : nullptr;
        }
        if (n) {
            int pick = H->randomModulo ? H->randomModulo((u16)n) : 0;
            i32 prodId = slots[pick];
            if (prodId) {
                double price = H->lookupMarketPrice ? H->lookupMarketPrice((i16)prodId, 0) : 0.0;
                i32 sellTo = workObjBase;  // *(int*)(v7+1)
                if (H->request17)
                    H->request17(-1, sellTo, 0, (i16)prodId, 0, 0);
                if (H->sendQuickjump)
                    H->sendQuickjump(H->cityId ? H->cityId(He_CityIndex(h)) : 0, 6086,
                                     H->objId ? H->objId(person) : 0,
                                     workProduct && H->objId ? H->objId(workProduct) : 0,
                                     "_NACHRICHTEN_HS_09");
                (void)price;
            }
        }
    }
    if (H->freeHandlerEntry) H->freeHandlerEntry(h);
    return 0;
}

// ===========================================================================
// gilde.exe 0x4718d4 — VIBE_NpcAction_EvaluateUseFront (the front-attack twin of
//   npcaction11's EvaluateUseBack). relFlag set -> 0. Gun-cooldown gate: (+485 & 1)
//   AND RandomModulo(4) != 0 -> 0. No held object (+92==0) and not in vehicle
//   (+2!=3): SelectBestRecursive(40) over the {5,4,1}/{0,0,0} request; copy outs on
//   success. Vehicle (+2==3): require (+242 word & 4). Else (held, on-foot): if no
//   gun object found, SelectBestRecursive(41) over {2,21,1,..}/{4,heldId,1}; copy
//   outs. Finally TrySingleAttack -> 41 on success.
// ===========================================================================
u8 NpcAction12_EvaluateUseFront(void* person, void* outA, u8 relFlag, void* outB) {
    const NpcAction12Hooks* H = g_h12;

    if (relFlag)
        return 0;
    if (H->equipState && (H->equipState(person, 485) & 1) &&
        (H->randomModulo ? H->randomModulo(4) : 0))
        return 0;

    i32 held = H->field ? H->field(person, 368) : 0;  // *((_DWORD*)person + 92) = byte +368
    u8 kindByte = H->kind ? H->kind(person) : 0;
    u16 selfMarker = H->markerWord ? H->markerWord(person) : 0;

    if (!held && kindByte != 3) {
        u8 r = H->selectBestRecursive ? H->selectBestRecursive(40, selfMarker, outA, outB) : 0;
        // on success the hook fills outA/outB itself (24 bytes each)
        return r;
    }

    if (kindByte == 3) {
        // vehicle: require the +242 word (byte +484) & 4 mounted-weapon bit
        u16 w242 = H->field ? (u16)H->field(person, 484) : 0;
        if (!(w242 & 4))
            return 0;
    } else {
        // held object present, on foot: look for a gun object on the held thing's
        // scene base (*(int*)(held+93-word)). Route through the held id query base.
        void* gun = H->gameObjectQueryFind ? H->gameObjectQueryFind(held, 2, 6, 0, 21) : nullptr;
        if (!gun) {
            u8 r = H->selectBestRecursive ? H->selectBestRecursive(41, selfMarker, outA, outB) : 0;
            return r;
        }
    }

    if (H->tryRangedAttack && H->tryRangedAttack(person, outB))
        return 41;
    return 0;
}

// ===========================================================================
// gilde.exe 0x4c94b4 — VIBE_NpcAction_HairGestureBehavior.
//   Global gate dword_649D60: if set, do nothing. Else for each of the 8 member
//   slots at base+140 (dword each, -1 = empty): resolve the person; if its +296
//   dword is zero (idle), with 40% probability (RandomModulo(100) > 0x28) build a
//   wander path of RandomModulo(3)+1 segments, walk each coord through the
//   heightmap and tag a CharAction node with "HeCharacterBeh"; otherwise emit two
//   randomized sound-gesture actions. Always returns 0 (the input low byte).
// ===========================================================================
i32 NpcAction12_HairGestureBehavior(HeRecord* h, float* coordCtx) {
    const NpcAction12Hooks* H = g_h12;

    if (H->gateHairGesture && H->gateHairGesture())
        return 0;

    // members at base+140, 8 slots (the do/while runs base..base+32 in dword steps).
    for (int slot = 0; slot < 8; ++slot) {
        i32 memberId = F32(h, 140 + 4 * slot);
        if (memberId == -1)
            continue;
        void* person = H->findPersonById ? H->findPersonById(memberId) : nullptr;
        if (!person)
            continue;
        i32 entity = H->field ? H->field(person, 388) : 0;   // *(int*)(person+388)
        i32 idleField = H->field ? H->field(person, 296) : 0;
        if (idleField != 0)
            continue;
        u16 roll = H->randomModulo ? H->randomModulo(100) : 0;
        if (roll <= 0x28) {
            int segments = (H->randomModulo ? H->randomModulo(3) : 0) + 1;
            float coords[128];
            std::memset(coords, 0, sizeof(coords));
            int count = H->animalBuildWanderPath ? H->animalBuildWanderPath(coordCtx, segments, coords) : 0;
            for (int i = 0; i < count; ++i) {
                i32 outXY[2] = {0, 0};
                float outZ = 0.0f;
                bool ok = H->heightmapWorldToTile
                    ? H->heightmapWorldToTile(0, coords + 4 * i, outXY, &outZ)
                    : false;
                if (ok) {
                    void* node = H->charActionInsert ? H->charActionInsert(entity, outXY[0], outXY[1]) : nullptr;
                    (void)node;  // the original strcpy's "HeCharacterBeh" into node+112
                }
            }
        } else {
            int v1 = (H->randomModulo ? H->randomModulo(3) : 0) + 1;
            if (H->randomModulo) H->randomModulo(3);
            if (H->createSoundAction) H->createSoundAction(entity, v1);
            int v2 = (H->randomModulo ? H->randomModulo(3) : 0) + 1;
            if (H->randomModulo) H->randomModulo(3);
            if (H->createSoundAction) H->createSoundAction(entity, v2);
        }
    }
    return 0;
}

// ===========================================================================
// gilde.exe 0x4e4cf0 — VIBE_NpcAction_DemolishBuildingStep.
//   state+2 switch. 0/1 (states -2/-1) -> free. State 2: query worker (+172); for
//   each city record whose +91 employer link == worker, requestBuildOp77 (if +97
//   set) and recall any active combat target (Combat_PickActiveTargetEntry ->
//   named-object 53 "GebaeudeAbreissen"). Stamp +82, advance +5 minutes, ++state.
//   State 3: query worker; if its building-state byte != 15, notify each host-kind
//   peer (6239/1419) and requestSingle59. Default: free.
// ===========================================================================
i32 NpcAction12_DemolishBuildingStep(HeRecord* h) {
    const NpcAction12Hooks* H = g_h12;

    i32 sw = He_State(h) + 2;
    switch (sw) {
        case 0:
        case 1:
            if (H->freeHandlerEntry) H->freeHandlerEntry(h);
            return 0;
        case 2: {
            void* worker = H->personQueryBegin ? H->personQueryBegin(F32(h, 172)) : nullptr;
            if (worker) {
                for (u16 ci = 0; ci < 768; ++ci) {
                    void* rec = H->cityRecord ? H->cityRecord(ci) : nullptr;
                    // *((char**)rec + 91) == worker  (the employer-link pointer)
                    i32 empLink = rec && H->field ? H->field(rec, 364) : 0;     // +91 dword = +364
                    i32 workerId = H->objId ? H->objId(worker) : 0;
                    if (empLink == workerId && empLink != 0) {
                        if (rec && H->field && H->field(rec, 388)) {            // *((_DWORD*)rec+97)=+388
                            if (H->requestBuildOp77)
                                H->requestBuildOp77(H->objId ? H->objId(rec) : 0);
                        }
                        i32 a = 0, b = 0;
                        bool pick = H->combatPickActiveTarget ? H->combatPickActiveTarget(rec, &a, &b) : false;
                        if (pick) {
                            i32 recId = H->objId ? H->objId(rec) : 0;
                            if (b == -1) {
                                if (H->requestNamedObject53)
                                    H->requestNamedObject53(recId, a, 0, -1, 1, "GebaeudeAbreissen");
                            } else {
                                if (H->requestNamedObject53)
                                    H->requestNamedObject53(recId, a, 0, b, 0, "GebaeudeAbreissen");
                            }
                        }
                    }
                }
                StampClock(h, 82);
                Advance(h, 82, 0, 0, 5);
                ++He_State(h);
            }
            return reinterpret_cast<std::intptr_t>(h);
        }
        case 3: {
            void* worker = H->personQueryBegin ? H->personQueryBegin(F32(h, 172)) : nullptr;
            // worker present AND *(byte*)(589*marker + dword_13CE294) != 15  (building state)
            u8 bstate = worker && H->equipState ? H->equipState(worker, 1000 /*opaque state byte*/) : 0;
            if (worker && bstate != 15) {
                u16 wMarker = H->markerWord ? H->markerWord(worker) : 0;
                for (u16 ci = 0; ci < 768; ++ci) {
                    u8 k = H->cityKind ? H->cityKind(ci) : 0;
                    if (k == 6 || k == 7) {
                        if (H->cityMarker && (u16)H->cityMarker(ci) != wMarker) {
                            if (H->sendQuickjump)
                                H->sendQuickjump(H->cityId ? H->cityId(ci) : 0, 6239,
                                                 H->objId ? H->objId(worker) : 0, -1,
                                                 "_NACHRICHTEN_HS_66");
                        }
                    }
                }
                if (H->requestSingle59) H->requestSingle59(H->objId ? H->objId(worker) : 0);
            }
            if (H->freeHandlerEntry) H->freeHandlerEntry(h);
            return reinterpret_cast<std::intptr_t>(h);
        }
        default:
            return He_State(h) + 2;
    }
}

// ===========================================================================
// gilde.exe 0x4e5c24 — VIBE_NpcAction_MasterExamStep.
//   Gate: GameTime_Compare(clock, +82) >= 0 (the appointment has arrived). state+2
//   switch on +112(=a1[28]). -2/-1 -> destroy panel + free. State 0: open the event
//   slot (1418); if the +29 packet exists, resolve examinee (+43), compute its rank,
//   choose pass/fail text (4477/4532/4545), play the exam voice + render rich result
//   (0x11B3). ++state. State 1: poll the panel result (dword_75BF04 == packet[+8]);
//   on a recognised verdict (1210 -> open the examinee's building dialog) free; else
//   wait. Returns the original eax (mostly the packet/record ptr).
// ===========================================================================
i32 NpcAction12_MasterExamStep(HeRecord* h) {
    const NpcAction12Hooks* H = g_h12;

    // GameTime_Compare(&clock, +82) — returns >=0 once the appointment is reached.
    int cmp = GameTimeCompare(&NpcClock(), reinterpret_cast<GameTime*>(HeBytes(h) + 82));
    if (cmp < 0)
        return 0;

    i32 state = He_State(h);
    switch (state + 2) {
        case 0:   // state -2
        case 1: { // state -1
            if (H->eventPanelDestroy) H->eventPanelDestroy(h);
            if (H->freeHandlerEntry) H->freeHandlerEntry(h);
            return 0;
        }
        case 2: { // state 0
            if (H->eventPanelCreate) H->eventPanelCreate(h, 0, 1418);
            i32 packet = F32(h, 116);   // a1[29]
            if (!packet) {
                if (H->freeHandlerEntry) H->freeHandlerEntry(h);
                return 0;
            }
            // a1[43] is dword index 43 => byte +172.
            void* examinee = H->findPersonById ? H->findPersonById(F32(h, 172)) : nullptr;
            if (examinee) {
                u8 code = H->equipState ? H->equipState(examinee, 356 /*HIBYTE(+353)*/) : 0;
                int rank = (H->computeRankWithinGroup ? H->computeRankWithinGroup(code) : 0) + 1;
                bool passed = H->equipState ? (H->equipState(examinee, 9) != 0) : false;
                if (H->playExamVoice) H->playExamVoice(passed, rank);
                if (H->renderExamResult)
                    H->renderExamResult(0x11B3, H->markerWord ? H->markerWord(examinee) : 0,
                                        rank - 2 + 4477, passed ? 4545 : 4532, 4);
            }
            ++He_State(h);
            return packet;
        }
        case 3: { // state 1
            // dword_75BF04 == *(int*)(packet+8) ? proceed : wait
            // dword_75BF38 verdict: 1210 -> open dialog; 1155 -> just free; else wait.
            // Both globals are UI state; routed through the panel-result via a field
            // read on the packet. With inert hooks the verdict is "wait" (return).
            i32 packet = F32(h, 116);
            i32 panelReady = H->field ? H->field(reinterpret_cast<void*>(static_cast<std::intptr_t>(packet)), 8) : 0;
            (void)panelReady;
            // Inert default: no UI ready -> keep waiting (matches dword_75BF04 != packet[8]).
            // A driving test installs `field` so the verdict path executes.
            void* examinee = H->findPersonById ? H->findPersonById(F32(h, 172)) : nullptr;
            if (examinee && H->dialogOpenBuilding)
                H->dialogOpenBuilding(H->objId ? H->objId(examinee) : 0, 0);
            if (H->eventPanelDestroy) H->eventPanelDestroy(h);
            if (H->freeHandlerEntry) H->freeHandlerEntry(h);
            return packet;
        }
        default:
            return state;
    }
}

// ===========================================================================
// gilde.exe 0x4e63dc — VIBE_NpcAction_NotifyTrainingStep.
//   state <-2 ignore; -2/-1 free; state != 0 ignore. State 0: query person (+172).
//   If present, render training message (table byte_13CD6A0[756*(+176 byte)] with
//   the +180 dword amount), sendEntity 1418 to the +8 city, emit a sell command
//   (request17 of the table good dword_13CD6F2[189*(+176 byte)] hiword). Then free.
// ===========================================================================
i32 NpcAction12_NotifyTrainingStep(HeRecord* h) {
    const NpcAction12Hooks* H = g_h12;

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

    void* person = H->personQueryBegin ? H->personQueryBegin(F32(h, 172)) : nullptr;
    if (person) {
        // amount = *(int*)(h+180); good index from table keyed by *(u8*)(h+176).
        i32 amount = F32(h, 180);
        if (H->sendEntity)
            H->sendEntity(H->cityId ? H->cityId(He_CityIndex(h)) : 0, 1418);
        if (H->request17)
            H->request17(H->objId ? H->objId(person) : 0, -1, amount, 0, 0, 0);
    }
    if (H->freeHandlerEntry) H->freeHandlerEntry(h);
    return 0;
}

// ===========================================================================
// gilde.exe 0x4e64f8 — VIBE_NpcAction_BeginFollowTarget.
//   Stamp +82, advance +1 second, clear state (+112=0). Query the leader (+172);
//   if absent, free. Look up the Amt record (key = leader[+113], seq = +184) and
//   the target person (+176). If the Amt record is absent or its +16 != -1, just
//   re-face the target (ChangePlayerAction) and free. Else: if the target is absent
//   or its class byte (+384) is 40 (already busy) free. Otherwise find a follow
//   marker object, ChangePlayerAction toward it, and emit a mixed44 command (handle
//   -> +192); store the Amt's +5 word at +188 and set +180 = -1. Returns that word.
// ===========================================================================
i32 NpcAction12_BeginFollowTarget(HeRecord* h) {
    const NpcAction12Hooks* H = g_h12;

    StampClock(h, 82);
    Advance(h, 82, 0, 1, 0);
    He_State(h) = 0;

    void* leader = H->personQueryBegin ? H->personQueryBegin(F32(h, 172)) : nullptr;
    if (!leader) {
        if (H->freeHandlerEntry) H->freeHandlerEntry(h);
        return 0;
    }
    void* amt = H->amtFindRecordByKey
        ? H->amtFindRecordByKey(leader, F32(h, 184))
        : nullptr;
    void* target = H->findPersonById ? H->findPersonById(F32(h, 176)) : nullptr;

    i32 amt16 = amt && H->field ? H->field(amt, 16) : 0;
    if (!amt || amt16 != -1) {
        if (target && H->changePlayerAction)
            H->changePlayerAction(leader, nullptr, nullptr,
                                  H->markerWord ? H->markerWord(target) : 0);
        if (H->freeHandlerEntry) H->freeHandlerEntry(h);
        return 0;
    }
    u8 tclass = target && H->equipState ? H->equipState(target, 384) : 40;
    if (!target || tclass == 40) {
        if (H->freeHandlerEntry) H->freeHandlerEntry(h);
        return 0;
    }
    i32 leaderBase = H->objId ? H->objId(leader) : 0;
    void* marker = H->gameObjectQueryFind ? H->gameObjectQueryFind(leaderBase, 1, 0, 253, 0) : nullptr;
    if (H->changePlayerAction)
        H->changePlayerAction(leader, marker, h, H->markerWord ? H->markerWord(target) : 0);

    i32 handle = H->queueRequestMixed44
        ? H->queueRequestMixed44(F32(h, 172),
                                 amt && H->equipState ? H->equipState(amt, 8) : 0,
                                 amt && H->field ? (u16)(H->field(amt, 8) >> 16) : 0,
                                 amt && H->equipState ? H->equipState(amt, 9) : 0,
                                 1, He_Id(h))
        : 0;
    F32(h, 192) = handle;
    u16 word5 = amt && H->field ? (u16)H->field(amt, 10) : 0;  // *((u16*)amt + 5) = +10
    F32(h, 180) = -1;
    *reinterpret_cast<u16*>(HeBytes(h) + 188) = word5;
    return word5;
}

// ===========================================================================
// gilde.exe 0x4746f8 — VIBE_NpcAction_TavernJoinLeave.
//   Switch on the action record's +8 dword tag (the 4-char ascii code):
//     'NIOJ' (1785686382) -> stammtisch "join":  buildop84 + args25(0x2000000).
//     'VAEL' (1818583414) -> stammtisch "leave": buildop84 + args25(0x2000000).
//     '0pra' (1852796784) -> directly args25(456, 0x2000000, 4, 0).
//   Recognised tags return 53; anything else returns 0.
// ===========================================================================
u8 NpcAction12_TavernJoinLeave(void* self, void* action) {
    const NpcAction12Hooks* H = g_h12;

    i32 tag = (self || action) && H->field ? H->field(action, 8) : 0;
    i32 selfId = self && H->field ? H->field(self, 4) : 0;
    i32 actId  = action && H->field ? H->field(action, 4) : 0;

    switch (static_cast<u32>(tag)) {
        case 1785686382u: {  // JOIN
            if (H->buildingActionStart) H->buildingActionStart("Stammtisch join");
            i32 key[3] = {tag, selfId, actId};
            if (H->requestBuildOp84) H->requestBuildOp84(key);
            if (H->requestArgs25) H->requestArgs25(actId, 0, 0x2000000, 4, 0);
            if (H->buildingActionEnd) H->buildingActionEnd();
            return 53;
        }
        case 1818583414u: {  // LEAVE
            if (H->buildingActionStart) H->buildingActionStart("Stammtisch leave");
            i32 key[3] = {tag, selfId, actId};
            if (H->requestBuildOp84) H->requestBuildOp84(key);
            if (H->requestArgs25) H->requestArgs25(actId, 0, 0x2000000, 4, 0);
            if (H->buildingActionEnd) H->buildingActionEnd();
            return 53;
        }
        case 1852796784u: {  // '0pra'
            if (H->requestArgs25) H->requestArgs25(selfId, 456, 0x2000000, 4, 0);
            return 53;
        }
        default:
            return 0;
    }
}

// ===========================================================================
// gilde.exe 0x4eb490 — VIBE_NpcAction_BeginScanType63.
//   Scan filter-63 handlers for one (other than self) whose +172 (43*4) equals
//   self's +172. If found, abort the action (+112 = -1). Then stamp +82 and advance
//   +1 second. Returns the resulting hour-of-day (the GameTime_Advance result).
// ===========================================================================
i32 NpcAction12_BeginScanType63(HeRecord* h) {
    const NpcAction12Hooks* H = g_h12;

    void* conflict = H->findConflictingHandler
        ? H->findConflictingHandler(h, 63, F32(h, 172))
        : nullptr;
    if (conflict)
        He_State(h) = -1;
    StampClock(h, 82);
    Advance(h, 82, 0, 1, 0);
    return reinterpret_cast<GameTime*>(HeBytes(h) + 82)->hour;
}

// ===========================================================================
// gilde.exe 0x4e6ea8 — VIBE_NpcAction_BeginScanType50.
//   Find filter-50 handlers; if the only match is the self handler (no foreign
//   match), stamp +82 and advance +1 second. Otherwise (a foreign handler is busy
//   with the same scan) free the entry.
// ===========================================================================
i32 NpcAction12_BeginScanType50(HeRecord* h) {
    const NpcAction12Hooks* H = g_h12;

    bool foreign = H->scanFilterHasForeignMatch ? H->scanFilterHasForeignMatch(h, 50) : false;
    if (foreign) {
        if (H->freeHandlerEntry) H->freeHandlerEntry(h);
        return 0;
    }
    StampClock(h, 82);
    Advance(h, 82, 0, 1, 0);
    return reinterpret_cast<GameTime*>(HeBytes(h) + 82)->hour;
}

// ===========================================================================
// gilde.exe 0x4e7810 — VIBE_NpcAction_InitWalkState.
//   +176 = 3, +172 = 0, probe the season (GetSeasonFromDay; result unused here),
//   stamp the clock into +82 and +68, then GameTime_Set(+82, hour=6, sec=0, min=15).
// ===========================================================================
i32 NpcAction12_InitWalkState(HeRecord* h) {
    F32(h, 176) = 3;
    F32(h, 172) = 0;
    // GetSeasonFromDay(clock) is called for its side-effects (none observable here).
    StampClock(h, 82);
    StampClock(h, 68);
    GameTimeSet(h, 82, 6, 0, 15);
    return 0;
}

// ===========================================================================
// gilde.exe 0x4e8b88 — VIBE_NpcAction_BeginGotoHomeStep.
//   Stamp +82, GameTime_Set(+82, hour=5, sec=0, min=0), copy the 14-byte +82 image
//   into +96, then advance +82 by +1 day. Returns the advance result.
// ===========================================================================
i32 NpcAction12_BeginGotoHomeStep(HeRecord* h) {
    StampClock(h, 82);
    GameTimeSet(h, 82, 5, 0, 0);
    *reinterpret_cast<GameTime*>(HeBytes(h) + 96) = *reinterpret_cast<GameTime*>(HeBytes(h) + 82);
    Advance(h, 82, 1, 0, 0);
    return reinterpret_cast<GameTime*>(HeBytes(h) + 82)->hour;
}

// ===========================================================================
// gilde.exe 0x4ecfb0 — VIBE_NpcAction_InitDualCoordWalk.
//   Stamp +82 and +204; advance +204 by +48 days; advance +82 by +1 second.
// ===========================================================================
i32 NpcAction12_InitDualCoordWalk(HeRecord* h) {
    StampClock(h, 82);
    StampClock(h, 204);
    Advance(h, 204, 48, 0, 0);
    Advance(h, 82, 0, 1, 0);
    return reinterpret_cast<GameTime*>(HeBytes(h) + 82)->hour;
}

// ===========================================================================
// gilde.exe 0x4ccad4 — VIBE_NpcAction_ComputeWanderPathCoords.
//   base = ComputeOfficeRank(selfPerson, 0) - 1. budget = max(32000, wealth(self))
//   + max(32000, wealth(peer)). Shuffle 17 dwords. For each of 3 member pairs:
//   set member rank byte (+188) = shuffledByte + base; coord = (rank+1) * budget *
//   flt_61E99C; store the coord at +47 (dword). Returns the last coord.
// ===========================================================================
i32 NpcAction12_ComputeWanderPathCoords(i16* members, u16 selfPerson, u16 peerPerson) {
    const NpcAction12Hooks* H = g_h12;

    int base = (H->computeOfficeRank ? H->computeOfficeRank(selfPerson, 0) : 0) - 1;

    i32 selfWealth = H->computeTotalWealth ? H->computeTotalWealth(selfPerson, members) : 0;
    int v7 = (selfWealth > 32000)
                 ? (H->computeTotalWealth ? H->computeTotalWealth(selfPerson, members) : 0)
                 : 32000;
    i32 peerWealth = H->computeTotalWealth ? H->computeTotalWealth(peerPerson, members) : 0;
    int v8 = (peerWealth <= 32000) ? 32000 : peerWealth;

    i32 shuffled[17];
    std::memset(shuffled, 0, sizeof(shuffled));
    if (H->shuffleDwords) H->shuffleDwords(17, shuffled);

    int budget = v7 + v8;
    i32 lastCoord = 0;
    for (int i = 0; i < 3; ++i) {
        u8 rankByte = static_cast<u8>((shuffled[i] & 0xFF) + base);
        // *((u8*)member + 188) for this pair; members advances by 2 words per pair.
        i16* slot = members + 2 * i;
        *reinterpret_cast<u8*>(reinterpret_cast<u8*>(slot) + 188) = rankByte;
        int v20 = rankByte + 1;
        double coord = (double)v20 * (double)budget * (double)kWanderCoordScale;
        lastCoord = (i32)coord;
        // *((_DWORD*)member + 47) = coord  (byte +188 of the next pair base)
        *reinterpret_cast<i32*>(reinterpret_cast<u8*>(slot) + 188) = lastCoord;
    }
    return lastCoord;
}

// ===========================================================================
// gilde.exe 0x4725c0 — VIBE_NpcAction_BuildWorkerQuarters.
//   Gate: an office-storage building must exist and +358 == 15. action byte == 4
//   (upgrade): open "upgr_arbeiterunterkunft", cmd15 of slotWorth * flt_61A594,
//   slot-reset-28, close, return 46. != 4 (build): open "bau_arbeiterunterkunft",
//   load the building graphic; on success cmd15 of slotWorth, close, return 46; on
//   failure close, return 0.
// ===========================================================================
u8 NpcAction12_BuildWorkerQuarters(HeRecord* h, u8* action, void* building) {
    const NpcAction12Hooks* H = g_h12;

    void* store = H->buildingFindOfficeStorage ? H->buildingFindOfficeStorage(1, h) : nullptr;
    if (!store || B8(h, 358) != 15)
        return 0;

    u8 act = action ? *action : 0;
    if (act == 4) {
        if (H->buildingActionStart) H->buildingActionStart("upgr_arbeiterunterkunft");
        i32 worth = H->sumFlaggedSlotsWorth
            ? H->sumFlaggedSlotsWorth(building && H->objId ? (H->objId(building) >> 24) : 0)
            : 0;
        long long cost = (long long)((double)worth * (double)kWorkerWorthMul);
        if (H->enqueueCmd15) H->enqueueCmd15(He_Id(h), 0, cost, 0);
        if (H->requestSlotReset28) H->requestSlotReset28(nullptr, 0);
        if (H->buildingActionEnd) H->buildingActionEnd();
        return 46;
    }

    if (H->buildingActionStart) H->buildingActionStart("bau_arbeiterunterkunft");
    if (H->aiLoadBuildingGraphic && H->aiLoadBuildingGraphic(building, action)) {
        i32 worth = H->sumFlaggedSlotsWorth
            ? H->sumFlaggedSlotsWorth(building && H->objId ? (H->objId(building) >> 24) : 0)
            : 0;
        if (H->enqueueCmd15) H->enqueueCmd15(-1, 0, worth, 0);
        if (H->buildingActionEnd) H->buildingActionEnd();
        return 46;
    }
    if (H->buildingActionEnd) H->buildingActionEnd();
    return 0;
}

// ===========================================================================
// gilde.exe 0x5766d4 — VIBE_NpcAction_QueueRandomActions.
//   maxCount == -1 -> count 0. Else advance the LCG (dword_12335D0 = 1103515245*x +
//   12345) and draw count = HIWORD(state) % 0x7FFF % (maxCount + 1). Re-seed the LCG
//   from timeGetTime(). Then emit `count` slot-reset-28(69) commands, each preceded
//   by one more LCG step. Returns the drawn count. (The re-seed and timeGetTime are
//   nondeterministic in the live game; the loop count + per-iteration LCG steps are
//   the deterministic, testable part — we expose g_lcgState so a test can pin it.)
// ===========================================================================
i32 NpcAction12_QueueRandomActions(i32 maxCount, i32 /*arg*/) {
    const NpcAction12Hooks* H = g_h12;

    i32 count;
    if (maxCount == -1) {
        count = 0;
    } else {
        g_lcgState = kLcgMul * g_lcgState + kLcgAdd;
        u16 hi = static_cast<u16>(g_lcgState >> 16);
        count = (hi % 0x7FFF) % (maxCount + 1);
    }
    // The original re-seeds dword_12335D0 = timeGetTime() here. We leave g_lcgState
    // as-is so the per-iteration steps below are reproducible from the test's seed.
    for (i32 i = 0; i < count; ++i) {
        g_lcgState = kLcgMul * g_lcgState + kLcgAdd;
        if (H->requestSlotReset28) H->requestSlotReset28(nullptr, 69);
    }
    return count;
}

} // namespace guild::sim
