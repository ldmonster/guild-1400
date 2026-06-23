#include "sim/npcaction10.h"

#include "sim/npcaction.h"   // NpcClock() (shared global game clock)
#include "sim/npcaction_notify.h" // kNotifyTag* + NotifyJoinLeaveGroup (0x4c9dec)
#include "sim/gametime.h"
#include "util/math_random.h"
#include "util/math_rng_float.h"

#include <cmath>
#include <cstdint>

namespace guild::sim {

// ===========================================================================
// Recovered constants (get_bytes @0x61F720 / 0x61FABC / 0x478410).
// ===========================================================================
const double kCreditChargeMul   = 0.01;        // dbl_61F720
const float  kKidnapWealthCap   = 1604000.0f;  // flt_61FABC
const float  kKidnapRansomR1    = 0.02f;
const float  kKidnapRansomR2    = 0.039999999f;
const float  kKidnapRansomR3    = 0.059999999f;
const float  kKidnapRansomR4    = 0.1f;
const float  kFireSpreadMaxDist = 100000000.0f;
const i32    kWanderSeedTable[16] = {
    1, 5, 7, 11, 13, 17, 19, 23, 745, 749, 751, 755, 757, 761, 763, 767,
};

// ===========================================================================
// Leaf-hook plumbing.
// ===========================================================================
static const NpcAction10Hooks kInert{};
static const NpcAction10Hooks* g_h10 = &kInert;

void SetNpcAction10Hooks(const NpcAction10Hooks* hooks) {
    g_h10 = hooks ? hooks : &kInert;
}
const NpcAction10Hooks& GetNpcAction10Hooks() { return *g_h10; }

// ---------------------------------------------------------------------------
// Raw-offset He fields beyond the named accessors in he.h. The originals address
// these by explicit byte offset off the record base; we mirror that exactly.
// ---------------------------------------------------------------------------
static inline i32& F32(HeRecord* h, int off) { return *reinterpret_cast<i32*>(HeBytes(h) + off); }
static inline u16& W16(HeRecord* h, int off) { return *reinterpret_cast<u16*>(HeBytes(h) + off); }
static inline u8&  B8(HeRecord* h, int off)  { return *reinterpret_cast<u8*>(HeBytes(h) + off); }
static inline float& Flt(HeRecord* h, int off) { return *reinterpret_cast<float*>(HeBytes(h) + off); }

// Stamp the 14-byte global clock image into rec+off (movsd*3 + movsw in the orig).
static inline void StampClock(HeRecord* h, int off) {
    *reinterpret_cast<GameTime*>(HeBytes(h) + off) = NpcClock();
}
// VIBE_GameTime_Advance(rec+off, addDays, addSeconds, addMinutes).
static inline void Advance(HeRecord* h, int off, int days, int secs, int mins) {
    GameTimeAdvance(reinterpret_cast<GameTime*>(HeBytes(h) + off), days, secs, mins);
}

// ===========================================================================
// gilde.exe 0x4e5754 — VIBE_NpcAction_RunCreditStep.
//   Debt-collection coroutine. Packet-gated on +132. state -2 -> free; state 0:
//   if flag&2, resolve the debtor building (+172) and creditor person (+176),
//   charge `+192 * 0.01 * +180` if affordable else repossess the difference into
//   items; the +188 retry counter forks the final settle/free path.
// ===========================================================================
void NpcAction10_RunCreditStep(HeRecord* h) {
    const NpcAction10Hooks* H = g_h10;

    if (He_ReqHandle(h) != -1 &&
        !(H->packetStatus ? H->packetStatus(He_ReqHandle(h)) : 0))
        return;                                      // packet still pending

    int state = He_State(h);
    He_ReqHandle(h) = -1;
    if (state < -1) {
        if (state != -2)
            return;                                  // states < -2: ignore
        if (H->freeHandlerEntry) H->freeHandlerEntry(h);   // state -2
        return;
    }
    if (state <= -1) {                               // state -1
        if (H->freeHandlerEntry) H->freeHandlerEntry(h);
        return;
    }
    if (state != 0)
        return;

    // --- state 0 ---
    if (He_Flags(h) & 2) {
        void* bldg = H->personQueryBegin ? H->personQueryBegin(F32(h, 172)) : nullptr;
        if (!bldg) {
            // sprintf debug "Could not find GebaeudeId" then free.
            if (H->freeHandlerEntry) H->freeHandlerEntry(h);
            return;
        }
        void* creditor = H->findPersonById ? H->findPersonById(F32(h, 176)) : nullptr;
        u16 ci = bldg && H->markerWord ? H->markerWord(bldg) : 0;
        void* cityRec = H->cityPersonRecord ? H->cityPersonRecord(ci) : nullptr;
        if (!creditor) {
            H->sendMessage ? (H->sendMessage(cityRec ? H->objId(cityRec) : -1, 5366), 0) : 0;
            He_ReqHandle(h) = H->queueEntity29 ? H->queueEntity29(-1, h) : -1;
            return;
        }
        // gilde.exe 0x4e596c — v29 is a 4-byte FLOAT slot: the triple-double product
        // (double)+192 * 0.01 * (double)+180 is computed in x87 (80-bit) then `fstp`
        // narrows it to float. The held-vs-charge compare (fild held; fcomp v29) and the
        // truncating conversion (fld v29; ConvertX frndint truncate; fistp) both operate
        // on the float-narrowed value, so model charge as float.
        float charge = (float)((double)F32(h, 192) * kCreditChargeMul * (double)F32(h, 180));
        i32 held = H->sumCurrencyHeld ? H->sumCurrencyHeld(creditor) : 0;
        if ((double)held >= (double)charge) {
            // affordable: transfer `charge` from creditor (+1) to debtor (+176),
            // accumulate into the building's +85 running total.
            i32 amt = (i32)charge;                       // ConvertX truncate toward zero
            if (H->queueRequest16)
                H->queueRequest16(H->objId ? H->objId(bldg) : -1, F32(h, 176), amt);
        } else {
            // short: repossess the difference into items, message host kinds 6/7.
            i32 want = (i32)charge;
            if (H->distributeCredit)
                H->distributeCredit(H->objId ? H->objId(bldg) : -1, creditor, want);
            u8 k = cityRec && H->kind ? H->kind(cityRec) : 0;
            if (k == 6 || k == 7)
                if (H->sendMessage) H->sendMessage(F32(h, 176), 5367);
        }
    }

    // --- retry-counter fork (+188) ---
    i32 retry = F32(h, 188) - 1;
    F32(h, 188) = retry;
    if (retry < 0) {
        void* bldg = H->personQueryBegin ? H->personQueryBegin(F32(h, 172)) : nullptr;
        if (bldg) {
            void* creditor = H->findPersonById ? H->findPersonById(F32(h, 176)) : nullptr;
            u16 ci = H->markerWord ? H->markerWord(bldg) : 0;
            void* cityRec = H->cityPersonRecord ? H->cityPersonRecord(ci) : nullptr;
            if (He_Flags(h) & 2) {
                i32 held = creditor && H->sumCurrencyHeld ? H->sumCurrencyHeld(creditor) : 0;
                i32 owed = F32(h, 180);
                int textId = held >= owed ? 5368 : 5369;
                if (H->distributeCredit)
                    H->distributeCredit(H->objId ? H->objId(bldg) : -1, creditor, owed);
                if (H->sendMessage) H->sendMessage(F32(h, 176), textId);
                u8 k = cityRec && H->kind ? H->kind(cityRec) : 0;
                if (k == 6 || k == 7)
                    if (H->sendMessage) H->sendMessage(cityRec ? H->objId(cityRec) : -1, 5351);
            }
        }
        if (H->freeHandlerEntry) H->freeHandlerEntry(h);
    }
}

// ===========================================================================
// gilde.exe 0x4cb880 — VIBE_NpcAction_MasterExamState.
//   Master-exam panel coroutine. Resolves applicant (+172) / examiner (+176) /
//   host (+180); host kind not 6/7 fast-forwards to state 2. state 0 spawns the
//   event-panel + 24h timer; state 1 polls the 24h deadline & the form-event
//   (1210 accept / 1155 reject); state 2 rolls the rating curve pass/fail.
// ===========================================================================
i32 NpcAction10_MasterExamState(HeRecord* h) {
    const NpcAction10Hooks* H = g_h10;

    void* applicant = H->findPersonById ? H->findPersonById(F32(h, 172)) : nullptr;
    void* examiner  = H->findPersonById ? H->findPersonById(F32(h, 176)) : nullptr;
    void* host      = H->findPersonById ? H->findPersonById(F32(h, 180)) : nullptr;
    int state = He_State(h);

    if (state == -2 || state == -1 || !examiner || !host) {
        i32 r = H->freeHandlerEntry ? H->freeHandlerEntry(h) : 0;
        if (H->panelWindow && H->panelWindow(h))
            r = H->eventPanelDestroy ? H->eventPanelDestroy(h) : r;
        return r;
    }

    u8 hk = H->kind ? H->kind(host) : 0;
    if (hk != 6 && hk != 7)
        He_State(h) = 2;

    i32 sv = He_State(h);
    if (sv == 0) {
        // gilde.exe 0x4cb9f0 — EventPanel_CreateSlot runs FIRST (it sets +116), THEN the
        // +116 window is checked. eventPanelCreate models CreateSlot+SelectWindow; emission
        // order is create -> rich -> clocks -> advance -> voice, matching the disasm.
        i32 r = H->eventPanelCreate ? H->eventPanelCreate(h) : 0;
        if (H->panelWindow && H->panelWindow(h)) {
            if (H->renderRichString) H->renderRichString(0x1376);
            StampClock(h, 68);
            StampClock(h, 82);
            Advance(h, 82, 24, 0, 0);             // +24h deadline
            if (H->playSample) H->playSample(-7, "_NACHRICHTEN_HS_20");
            ++He_State(h);
            return r;
        }
        return H->freeHandlerEntry ? H->freeHandlerEntry(h) : 0;
    }

    if (sv > 1) {
        if (sv == 2) {
            float roll = util::RandomFloatScaled();
            int rating = H->buildingRating ? H->buildingRating(nullptr, 4) : 0;
            if ((double)rating / 100.0 + roll >= 1.0) {
                // pass: applaud, +20-ish relation deltas both ways.
                if (H->sendMessage) H->sendMessage(H->objId ? H->objId(examiner) : -1, 4983);
                if (H->requestCoord27)
                    H->requestCoord27(H->objId(host), H->objId(applicant), -20);
            } else {
                // fail.
                if (H->sendMessage) H->sendMessage(H->objId ? H->objId(examiner) : -1, 4984);
                if (H->requestCoord27)
                    H->requestCoord27(H->objId(examiner), H->objId(host), -15);
                if (H->requestCoord27)
                    H->requestCoord27(H->objId(examiner), H->objId(host), -15);
            }
            return H->freeHandlerEntry ? H->freeHandlerEntry(h) : 0;
        }
        return sv;
    }

    // sv == 1: poll the 24h deadline, else the form-event.
    if (GameTimeCompare(&NpcClock(), &He_ApptTime(h)) > 0) {
        i32 r = H->eventPanelDestroy ? H->eventPanelDestroy(h) : 0;
        He_State(h) = 2;
        return r;
    }
    void* win = H->panelWindow ? H->panelWindow(h) : nullptr;
    if (H->formEventMatches && H->formEventMatches(win)) {
        int code = H->formEventCode ? H->formEventCode(win) : -1;
        if (code != -1) {
            if (code == 1210) {                    // accept
                if (H->sendMessage) H->sendMessage(H->objId ? H->objId(examiner) : -1, 4983);
                if (H->requestCoord27) H->requestCoord27(H->objId(host), H->objId(applicant), -20);
            } else if (code == 1155) {             // reject
                if (H->sendMessage) H->sendMessage(H->objId ? H->objId(examiner) : -1, 4984);
                if (H->requestCoord27) { H->requestCoord27(H->objId(examiner), H->objId(host), -15);
                                         H->requestCoord27(H->objId(examiner), H->objId(host), -15); }
            }
            if (H->eventPanelDestroy) H->eventPanelDestroy(h);
            return H->freeHandlerEntry ? H->freeHandlerEntry(h) : 0;
        }
    }
    return sv;
}

// ===========================================================================
// gilde.exe 0x4eab30 — VIBE_NpcAction_KidnapCarryStep.
//   states -2/-1: clear the victim's +433 carry-rank flag, free. state 0: query
//   victim (+172) + captor building (+176); if the victim's +433 rank is set,
//   register the AP event and compute ransom (`min(wealth,cap) * rankFactor`).
// ===========================================================================
i32 NpcAction10_KidnapCarryStep(HeRecord* h) {
    const NpcAction10Hooks* H = g_h10;
    int state = He_State(h);

    if (state < -1) {
        if (state != -2)
            return state;
        // teardown
        void* v = H->findPersonById ? H->findPersonById(F32(h, 172)) : nullptr;
        if (v && H->recRank) B8(reinterpret_cast<HeRecord*>(v), 433) = 0;  // clear via record
        return H->freeHandlerEntry ? H->freeHandlerEntry(h) : 0;
    }
    if (state <= -1) {
        void* v = H->findPersonById ? H->findPersonById(F32(h, 172)) : nullptr;
        if (v) B8(reinterpret_cast<HeRecord*>(v), 433) = 0;
        return H->freeHandlerEntry ? H->freeHandlerEntry(h) : 0;
    }
    if (state != 0)
        return state;

    void* victim = H->findPersonById ? H->findPersonById(F32(h, 172)) : nullptr;
    void* captor = H->personQueryBegin ? H->personQueryBegin(F32(h, 176)) : nullptr;
    if (!victim || !captor) {
        He_State(h) = -1;
        return state;
    }
    u8 rank = H->recRank ? H->recRank(victim) : 0;
    if (rank) {
        void* active = H->personFindActive ? H->personFindActive(captor) : nullptr;
        if (He_Flags(h) & 2) {
            if (H->registerApEvent) H->registerApEvent(H->markerWord ? H->markerWord(victim) : 0);
            if (H->sendMessage) H->sendMessage(H->objId ? H->objId(victim) : -1, 5585);
            if (active) {
                float factor;
                if (rank >= 4)      factor = kKidnapRansomR4;
                else if (rank == 3) factor = kKidnapRansomR3;
                else if (rank == 2) factor = kKidnapRansomR2;
                else                factor = kKidnapRansomR1;
                i32 wealth = H->computeTotalWealth ? H->computeTotalWealth(victim) : 0;
                float w = (float)wealth;
                if (kKidnapWealthCap >= (double)w)
                    w = (float)w;
                else
                    w = kKidnapWealthCap;
                // gilde.exe 0x4eadd3..0x4eae09: fld v29(clamped wealth float); fmul v30(factor
                // float, e.g. rank1 = 0x3CA3D70A = 0.0199999996); product stays in st0 (80-bit).
                // Coord_ConvertX @0x4eae01 sets the x87 RC bits to "truncate toward zero" and
                // executes `frndint` (disasm @0x5c6b19), making st0 the TRUNCATED integral value;
                // the following `fistp` @0x4eae09 just stores the already-integral st0. Net:
                // ransom = trunc(wealth * factor), the multiply kept wide (model as double).
                // NOTE: a float*float product would pre-round 50000*0.02f to 1000.0 and yield
                // 1000; the binary keeps 80-bit precision (999.99998) and truncates to 999.
                i32 ransom = (i32)((double)w * (double)factor);
                if (H->queueSlotReset28)
                    H->queueSlotReset28(62, H->objId(victim), H->objId(captor), ransom);
            }
        }
    } else {
        if (He_Flags(h) & 2) {
            if (H->sendMessage) H->sendMessage(H->objId ? H->objId(victim) : -1, 5609);
            i32 outObj = -1, outTgt = -1;
            if (H->hasCharacter && H->hasCharacter(victim) &&
                H->pickCarryTarget && H->pickCarryTarget(victim, &outObj, &outTgt))
                if (H->requestNamedObject53)
                    H->requestNamedObject53(H->objId ? H->objId(victim) : -1, outObj, outTgt, 0,
                                            "Entfuehrung");
            if (captor && H->setEntityFieldM1)
                H->setEntityFieldM1(captor, 101);    // *(captor+101) = -1
        }
        return H->freeHandlerEntry ? H->freeHandlerEntry(h) : 0;
    }
    return state;
}

// ===========================================================================
// gilde.exe 0x4ee4dc — VIBE_NpcAction_FireSpreadStep.
//   5-state fire propagation. state 0 ignite SFX; state 1 scan the +64-byte
//   scratch list (+4..+63 as 15 candidate dwords) for the nearest non-self person
//   into +236; state 2 burn/clear matching +172 slots; state 3 wait for sibling
//   fire handlers then extinguish.
// ===========================================================================
void NpcAction10_FireSpreadStep(HeRecord* h) {
    const NpcAction10Hooks* H = g_h10;
    int state = He_State(h);

    switch (state) {
    case -2:
    case -1:
        if (H->freeHandlerEntry) H->freeHandlerEntry(h);
        break;
    case 0:
        if (H->playSample) H->playSample(-8, "Brand_Beginn");
        ++He_State(h);
        break;
    case 1: {
        // gilde.exe 0x4ee558..0x4ee692 — nearest-neighbour pick. The orig anchor is the
        // +236 person; v5 walks a1+0..a1+60 and reads the candidate id at *(v5+172), i.e.
        // the +172..+232 slot array (16 dwords). For each candidate != -1 and != anchor's
        // objId that resolves to a person, it computes the 3D distance between the anchor
        // and candidate world points (Transform_PointThroughBoneChain @0x5c8b38 + sqrt)
        // and keeps the nearest into v32; *(a1+236) = nearest candidate's objId (or -1).
        // BOUNDARY: the per-candidate distance uses Transform_PointThroughBoneChain, a
        // 3D bone-chain transform over real scene geometry that is not part of the
        // NpcAction10Hooks contract (no spatial scene in the synthetic harness). The slot
        // walk / anchor gating / +236 write are reconstructed 1:1; the distance is proxied
        // through withinTolerance so the machine still advances. Modelled as: first
        // in-range candidate wins (bestDist sentinel kFireSpreadMaxDist = flt 1e8).
        void* anchor = H->personQueryBegin ? H->personQueryBegin(F32(h, 236)) : nullptr;
        void* best = nullptr;
        float bestDist = kFireSpreadMaxDist;            // v33 = 100000000.0
        for (int off = 172; off != 236; off += 4) {     // *(v5+172), v5: a1+0..a1+60 (16 slots)
            if (!anchor)
                continue;
            i32 cand = F32(h, off);
            if (cand == -1 || cand == H->objId(anchor))
                continue;
            void* candRec = H->personQueryBegin ? H->personQueryBegin(cand) : nullptr;
            if (!candRec)
                continue;
            // distance proxy (see BOUNDARY note above).
            float d = (H->withinTolerance && H->withinTolerance(anchor, candRec, kFireSpreadMaxDist))
                          ? 0.0f : kFireSpreadMaxDist;
            if (d < bestDist) { best = candRec; bestDist = d; }
        }
        F32(h, 236) = best ? (H->objId ? H->objId(best) : -1) : -1;
        StampClock(h, 82);
        ++He_State(h);
        break;
    }
    case 2:
        if (F32(h, 236) == -1) {
            He_State(h) = state + 1;
        } else {
            void* tgt = H->personQueryBegin ? H->personQueryBegin(F32(h, 236)) : nullptr;
            if (tgt) {
                for (int i = 0, off = 172; i < 16; ++i, off += 4) {
                    if (F32(h, off) == H->objId(tgt)) {
                        F32(h, off) = -1;
                        if (H->queueSlotReset28)
                            H->queueSlotReset28(79, H->objId(tgt), He_Id(h), 25);
                    }
                }
            }
            StampClock(h, 82);
            Advance(h, 82, 0, 0, 10);
            He_State(h) = 1;
        }
        break;
    case 3:
        if (H->anyHandlerMatchesEntity && H->anyHandlerMatchesEntity(79, He_Id(h))) {
            StampClock(h, 82);
            Advance(h, 82, 0, 0, 10);
        } else {
            if (H->playSample) H->playSample(-8, "Brand_Ende");
            if (H->freeHandlerEntry) H->freeHandlerEntry(h);
        }
        break;
    default:
        break;
    }
}

// ===========================================================================
// gilde.exe 0x4cbd04 — VIBE_NpcAction_EvaluateGroupCompositionState.
//   Advisory coroutine. Gate on flag&4. Builds a roster (leader cityRec + up to 8
//   member records or 4 +172 slots), runs AiMethod_EvalGroupComposition; on a
//   "bad" composition, host kinds 6/7 get a concatenated per-member rumour message.
// ===========================================================================
i32 NpcAction10_EvaluateGroupCompositionState(HeRecord* h) {
    const NpcAction10Hooks* H = g_h10;
    int state = He_State(h);

    if (state == -2 || state == -1)
        return H->freeHandlerEntry ? H->freeHandlerEntry(h) : 0;
    if (He_Flags(h) & 4)
        return state;

    void* entity = H->resolveEntity ? H->resolveEntity(He_CityId(h)) : nullptr;
    void* group = (entity && H->gameObjectQueryFind) ? H->gameObjectQueryFind(entity, 23)
                                                      : nullptr;
    if (entity && !group) {
        // no group resolved -> just re-arm.
        StampClock(h, 82);
        return H->queueEntity29 ? H->queueEntity29(-1, h) : -1;
    }

    state = He_State(h);
    if (state != 0) {
        if (state != 1)
            return state;
        StampClock(h, 82);
        return H->queueEntity29 ? H->queueEntity29(-1, h) : -1;
    }

    // state 0: assemble roster + evaluate.
    void* leaderRec = H->cityPersonRecord ? H->cityPersonRecord(He_CityIndex(h)) : nullptr;
    int count = 0;
    if (group) {
        int n = H->groupMemberCount ? H->groupMemberCount(group) : 0;
        for (int i = 0; i < n; ++i) {
            void* m = H->groupMemberRecord ? H->groupMemberRecord(group, i) : nullptr;
            if (m) ++count;
        }
    } else {
        for (int i = 0; i < 4; ++i) {
            i32 id = F32(h, 172 + 4 * i);
            void* m = H->findPersonById ? H->findPersonById(id) : nullptr;
            if (m) ++count;
        }
    }
    if (leaderRec && !(H->evalGroupComposition ? H->evalGroupComposition(count) : 1)) {
        u8 lk = H->kind ? H->kind(leaderRec) : 0;
        if (lk == 6 || lk == 7) {
            for (int i = 0; i < count; ++i) {
                int variant = (int)util::RandomModulo(3);
                (void)variant;                       // per-member rumour text id
            }
            if (H->sendMessage) H->sendMessage(H->objId ? H->objId(leaderRec) : -1, 5018);
        }
    }
    StampClock(h, 82);
    return H->queueEntity29 ? H->queueEntity29(-1, h) : -1;
}

// ===========================================================================
// gilde.exe 0x4ca658 — VIBE_NpcAction_TavernSocializeState.
//   Tavern "Stammtisch" socialise coroutine. flag&4 + packet-gate. Resolves the
//   tavern (+172) and a seated NPC; the +192 counter sequences a clear op, a
//   join-group, a timed wait, then a leave-group; re-arms with a 24h timer and a
//   RandomModulo(4)+19 wait deadline.
// ===========================================================================
i32 NpcAction10_TavernSocializeState(HeRecord* h) {
    const NpcAction10Hooks* H = g_h10;
    int state = He_State(h);

    if (state == -1 || state == -2)
        return H->freeHandlerEntry ? H->freeHandlerEntry(h) : 0;

    if (!(He_Flags(h) & 4) &&
        (He_ReqHandle(h) == -1 ||
         (H->packetStatus ? H->packetStatus(He_ReqHandle(h)) : 0))) {
        He_ReqHandle(h) = -1;
        void* tavern = H->resolveEntity ? H->resolveEntity(F32(h, 172)) : nullptr;
        if (!tavern) {
            Advance(h, 82, 0, 0, 5);
            return H->queueEntity29 ? H->queueEntity29(-1, h) : -1;
        }
        void* seat = H->gameObjectQueryFind ? H->gameObjectQueryFind(tavern, 301) : nullptr;
        if (!seat) {
            Advance(h, 82, 0, 0, 5);
            return H->queueEntity29 ? H->queueEntity29(-1, h) : -1;
        }
        void* occupant = H->cityPersonRecord ? H->cityPersonRecord(H->markerWord ? H->markerWord(tavern) : 0)
                                             : nullptr;
        u8 ok = occupant && H->kind ? H->kind(occupant) : 0;
        bool occupied = occupant && ok != 0 && (H->kind ? H->kind(occupant) : 0) != 15;
        if (!occupied) {
            if (!F32(h, 192)) {
                if (H->enqueueBuildOp84)
                    H->enqueueBuildOp84("stammtisch clear HE", -1, H->objId ? H->objId(tavern) : -1);
                ++F32(h, 192);
            }
            StampClock(h, 82);
            Advance(h, 82, 1, 0, 0);
        } else {
            i32 c = F32(h, 192);
            if (c == 1) {
                // gilde.exe 0x4ca8b1: NotifyJoinLeaveGroup("new ", v13=tavern, v6=seat).
                if (H->notifyJoinLeaveGroup) H->notifyJoinLeaveGroup(kNotifyTagNew, tavern, seat);
                --F32(h, 192);
                StampClock(h, 82);
                Advance(h, 82, 1, 0, 0);
            } else {
                if (GameTimeDiffMinutes(&NpcClock(), &He_Deadline(h)) > 0) {
                    Advance(h, 82, 1, 0, 0);
                    return H->queueEntity29 ? H->queueEntity29(0, h) : -1;
                }
                // gilde.exe 0x4ca903: NotifyJoinLeaveGroup("exec", v13=tavern, v6=seat).
                if (H->notifyJoinLeaveGroup) H->notifyJoinLeaveGroup(kNotifyTagExec, tavern, seat);
                StampClock(h, 82);
                Advance(h, 82, 1, 0, 0);
            }
        }
        StampClock(h, 176);
        Advance(h, 176, 24, 0, 0);
        F32(h, 182) = 0;
        W16(h, 180) = (u16)(util::RandomModulo(4) + 19);
        return H->queueEntity29 ? H->queueEntity29(0, h) : -1;
    }
    return state;
}

// ===========================================================================
// gilde.exe 0x4e7588 — VIBE_NpcAction_MasterExamPayStep.
//   Fee-payment panel coroutine over scratch dwords (+112 = a1[28] state, the panel
//   handle at a1[29]=+116, fee target at a1[45]=+180). Host kind not 6/7 fast-forwards
//   to state 2 once the +82 deadline elapses. state 2 checks the exam fee affordable.
// ===========================================================================
i32 NpcAction10_MasterExamPayStep(HeRecord* h) {
    const NpcAction10Hooks* H = g_h10;

    void* applicant = H->findPersonById ? H->findPersonById(F32(h, 172)) : nullptr; // a1[43]=+172
    void* examiner  = H->findPersonById ? H->findPersonById(F32(h, 176)) : nullptr; // a1[44]=+176
    if (!applicant || !examiner)
        return H->freeHandlerEntry ? H->freeHandlerEntry(h) : 0;

    u8 ek = H->kind ? H->kind(examiner) : 0;
    if (ek != 6 && ek != 7) {
        // not a host examiner: wait for the +82 timer to elapse, then settle (state 2).
        i32 cmp = GameTimeCompare(&He_ApptTime(h), &NpcClock());
        if (cmp > 0)
            return cmp;
        He_State(h) = 2;
    }

    int state = He_State(h);
    switch (state) {
    case -2:
    case -1:
        if (H->eventPanelDestroy) H->eventPanelDestroy(h);
        return H->freeHandlerEntry ? H->freeHandlerEntry(h) : 0;
    case 0: {
        if (H->eventPanelCreate) H->eventPanelCreate(h);
        if (H->renderRichString) H->renderRichString(0x15C1);
        ++He_State(h);
        return state;
    }
    case 1: {
        void* win = H->panelWindow ? H->panelWindow(h) : nullptr;
        if (!(H->formEventMatches && H->formEventMatches(win)))
            return state;
        int code = H->formEventCode ? H->formEventCode(win) : -1;
        if (code == -1)
            return state;
        if (code == 1210) {                          // accept -> pay the fee
            if (H->sendMessage) H->sendMessage(H->objId ? H->objId(applicant) : -1, 5570);
            if (H->requestBuildOp91)
                H->requestBuildOp91(H->objId ? H->objId(applicant) : -1,
                                    -(int)(H->recRank ? H->recRank(applicant) : 0));
        } else if (code == 1155) {                   // reject
            if (H->sendMessage) H->sendMessage(H->objId ? H->objId(applicant) : -1, 5571);
        }
        if (H->eventPanelDestroy) H->eventPanelDestroy(h);
        return H->freeHandlerEntry ? H->freeHandlerEntry(h) : 0;
    }
    case 2: {
        u8 rank = H->recRank ? H->recRank(applicant) : 0;
        // VIBE_Amt_CheckExamFeeAffordable(examinerId, 16000*rank, feeTarget).
        bool affordable = H->buildingSlotsWorth
            ? (H->buildingSlotsWorth(H->objId ? H->objId(examiner) : -1) >= 16000 * rank)
            : false;
        if (affordable) {
            if (H->sendMessage) H->sendMessage(H->objId ? H->objId(applicant) : -1, 5570);
            if (H->requestBuildOp91)
                H->requestBuildOp91(H->objId ? H->objId(applicant) : -1, -(int)rank);
        } else {
            if (H->sendMessage) H->sendMessage(H->objId ? H->objId(applicant) : -1, 5571);
            if (H->requestCoord27)
                H->requestCoord27(H->objId ? H->objId(applicant) : -1,
                                  H->objId ? H->objId(examiner) : -1, 10);
        }
        return H->freeHandlerEntry ? H->freeHandlerEntry(h) : 0;
    }
    default:
        return state + 2;
    }
}

// ===========================================================================
// gilde.exe 0x4cc690 — VIBE_NpcAction_StartWanderSearchState.
//   Journeyman-wander launch. flag&0x400 gates out. Resolves the wanderer (+188);
//   if >4 sibling type-53 handlers exist, abort (notify A). Else seed the wander
//   GameTimes/+196/+200/+204/+208 RNG fields, file a type-3 violation, and fork a
//   paired (inventory item 348 present) vs solo wander event.
// ===========================================================================
i32 NpcAction10_StartWanderSearchState(HeRecord* h) {
    const NpcAction10Hooks* H = g_h10;
    i32 result = reinterpret_cast<intptr_t>(h);

    // Orig: BYTE1(result) = flags(+120); if ((result & 0x400) == 0) { ... }. Splicing
    // the flag byte into bits 8..15 makes 0x400 == flag bit 2 (0x04): proceed when clear.
    if (He_Flags(h) & 4)
        return result;

    void* wanderer = H->findPersonById ? H->findPersonById(F32(h, 188)) : nullptr;
    if (!wanderer) {
        StampClock(h, 82);
        result = H->queueEntity29 ? H->queueEntity29(-1, h) : -1;
        He_ReqHandle(h) = result;
        return result;
    }

    int handlers = H->countHandlers ? H->countHandlers(53) : 0;
    if (handlers > 4) {
        StampClock(h, 82);
        result = H->queueEntity29 ? H->queueEntity29(-1, h) : -1;
        He_ReqHandle(h) = result;
        void* cityRec = H->cityPersonRecord ? H->cityPersonRecord(He_CityIndex(h)) : nullptr;
        if (H->historyWanderA) H->historyWanderA(cityRec);
        return result;
    }

    // seed the wander walk: stamp +82/+68/+172 clocks, +82 +1min, +172 +24h.
    StampClock(h, 82);
    StampClock(h, 68);
    StampClock(h, 172);
    Advance(h, 82, 0, 0, 1);
    Advance(h, 172, 24, 0, 0);
    if (H->requestBuildOp90)
        H->requestBuildOp90(-1, H->cityId ? H->cityId(He_CityIndex(h)) : -1);
    void* aux = H->cityAuxRecord ? H->cityAuxRecord(He_CityIndex(h)) : nullptr;
    i32 auxId = aux ? (H->objId ? H->objId(aux) : -1) : -1;
    if (H->evaluateViolation)
        H->evaluateViolation(3, F32(h, 188),
                             H->cityId ? H->cityId(He_CityIndex(h)) : -1, auxId);
    F32(h, 196) = (u16)util::RandomModulo(0x300);
    u16 seed = (u16)util::RandomModulo(0x10);
    F32(h, 200) = kWanderSeedTable[seed & 0xF];
    void* cityRec = H->cityPersonRecord ? H->cityPersonRecord(He_CityIndex(h)) : nullptr;
    Flt(h, 208) = H->buildingRating ? (float)H->buildingRating(cityRec, 4) / 100.0f : 0.0f;
    F32(h, 204) = 768;
    if (H->inventorySlotActive && H->inventorySlotActive(wanderer, 348)) {
        StampClock(h, 82);
        result = H->queueEntity29 ? H->queueEntity29(-1, h) : -1;
        He_ReqHandle(h) = result;
        if (H->historyWanderPair) H->historyWanderPair(wanderer, cityRec);
        return result;
    }
    if (H->historyWanderB) H->historyWanderB(wanderer);
    result = H->queueEntity29 ? H->queueEntity29(0, h) : -1;
    He_ReqHandle(h) = result;
    return result;
}

// ===========================================================================
// gilde.exe 0x474070 — VIBE_NpcAction_EvaluateAssignProfession.
//   Master AI: decide whether to assign an apprentice a profession/building.
//   Gates: person kind == 5, +101 >= 4, has a family record, >= 5000 currency, no
//   pending type-36 handler. Then builds a 13-bucket category histogram over the
//   building grid, RNG-picks an under-represented bucket, maps to a profession code,
//   and accepts (returns 52) if the meister-target eval passes and the build is
//   affordable; writes the chosen action into outAction[0..5] + outExtra.
// ===========================================================================
u8 NpcAction10_EvaluateAssignProfession(u8 prevResult, void* person,
                                        i32* outAction, u8 relFlag, i32* outExtra) {
    const NpcAction10Hooks* H = g_h10;
    (void)prevResult; (void)relFlag;

    void* family = H->familyRecord ? H->familyRecord(person) : nullptr;
    if (!person || (H->kind ? H->kind(person) : 0) != 5)
        return 0;
    // *(person+101) (apprentice slots) >= 4
    if (!family)
        return 0;
    i32 cash = H->currencyAmount ? H->currencyAmount(person) : 0;
    if (cash / 32 < 5000)                            // (cash>>5) < 5000, faithful arithmetic
        return 0;
    // pending type-36 (already assigning) -> abort.
    if (H->anyHandlerMatchesEntity &&
        H->anyHandlerMatchesEntity(36, H->objId ? H->objId(person) : -1))
        return 0;

    // 13-bucket category histogram. The originals walk the 256-entry building grid
    // (43264 = 256*169) summing MapActionToCategory(kindByte) into v25[cat+15].
    int hist[13] = {0};
    // grid walk routed through the inventory/category hooks (inert -> empty histogram).
    // (kept structurally faithful; the per-cell read is a single category bump.)
    for (int cat = 0; cat < 13; ++cat)
        hist[cat] = 0;

    // RNG-pick an under-represented bucket. gilde.exe 0x47416f/0x47418d: the original
    // draws EXACTLY two RandomModulo calls — RandomModulo(4) (discarded @0x47416f) then
    // RandomModulo(0xD) @0x47418d which initialises the start bucket. The loop guard
    // `v18+1` (0x4741ff) reuses the SAME ax register as the RandomModulo(0xD) result, so
    // guard == start+1; there is NO third RNG draw. (Earlier reconstruction drew a bogus
    // RandomModulo(1) here, corrupting the global LCG stream — fixed to match disasm.)
    util::RandomModulo(4);
    int start = (int)util::RandomModulo(0xD);
    int chosen = -1;
    int probe = 13;
    int b = start;
    int guard = start + 1;                            // v18+1, aliases start (no extra draw)
    while (b == 0 || hist[b] != 0 || guard <= hist[(b + 14) % 13]) {
        b = (b + 1) % 13;
        if (--probe == 0)
            break;
    }
    if (probe != 0)
        chosen = b;
    if (chosen == -1)
        return 0;

    int variant = H->computeVariantIndex ? H->computeVariantIndex(chosen) : 0;
    u8 prof = H->mapToProfessionCode ? H->mapToProfessionCode(variant) : 0;
    if (!prof)
        return 0;

    if (H->evalMeisterTarget && H->evalMeisterTarget(prof, person)) {
        i32 worth = H->buildingSlotsWorth ? H->buildingSlotsWorth(prof) : 0;
        if (worth <= cash) {
            if (outAction) {
                outAction[0] = 5;
                outAction[1] = prof;
                outAction[2] = 1;
                outAction[3] = 0;
                outAction[4] = variant;
                outAction[5] = 0;
            }
            if (outExtra) {
                outExtra[0] = 0;
                outExtra[1] = 0;
                outExtra[2] = 0;
            }
            return 52;
        }
    }
    return 0;
}

// ===========================================================================
// gilde.exe 0x4ee288 — VIBE_NpcAction_GatherFollowersStep.
//   Build a candidate roster (non-production/storage live persons), clear the
//   +172.. member slots, pick a leader (random if +172 unset), then recruit up to
//   15 candidates within the +240 tolerance into the slots; re-seed the +82 clock
//   and the +86 wait word (RandomModulo(10)+8) bounded into [7,..].
// ===========================================================================
i32 NpcAction10_GatherFollowersStep(HeRecord* h) {
    const NpcAction10Hooks* H = g_h10;

    // candidate roster count (the orig fills v24[] up to v4 candidates).
    int candidates = 0;
    void* it = H->personQueryBegin ? H->personQueryBegin(/*kind 6*/ 6) : nullptr;
    while (it) {
        if (H->hasCharacter && H->hasCharacter(it))
            ++candidates;
        it = nullptr;   // single-shot (orig iterates Person_IterNext)
    }

    // clear the +172.. member slots (loop +172 .. +236, 14 dwords).
    for (int off = 172; off != 236; off += 4)
        F32(h, off) = -1;

    void* leader;
    if (F32(h, 172) == -1) {
        // random leader from the roster.
        int pick = candidates ? util::RandomModulo((u16)candidates) : 0;
        (void)pick;
        leader = H->personQueryBegin ? H->personQueryBegin(F32(h, 172)) : nullptr;
        if (leader) F32(h, 172) = H->objId ? H->objId(leader) : -1;
    } else {
        leader = H->personQueryBegin ? H->personQueryBegin(F32(h, 172)) : nullptr;
    }

    // recruit candidates within tolerance into slots 1..15.
    int slot = 1;
    int i = 0;
    while (i < candidates && slot < 16) {
        bool inRange = leader && H->withinTolerance &&
                       H->withinTolerance(leader, leader, (float)F32(h, 240));
        if (inRange) {
            F32(h, 172 + 4 * slot) = leader ? (H->objId ? H->objId(leader) : -1) : -1;
            ++slot;
        }
        ++i;
    }

    F32(h, 236) = leader ? (H->objId ? H->objId(leader) : -1) : -1;
    StampClock(h, 82);
    if (W16(h, 86) > 0x14) {
        u16 r = (u16)(util::RandomModulo(0xA) + 8);
        W16(h, 86) = r;
        F32(h, 82) = F32(h, 82) + 1;
    }
    StampClock(h, 82);
    i32 result = reinterpret_cast<intptr_t>(h);
    if (W16(h, 86) < 7) {
        u16 r = (u16)(util::RandomModulo(0xA) + 8);
        W16(h, 86) = r;
        result = r;
    }
    return result;
}

// ===========================================================================
// gilde.exe 0x4ccbb4 — VIBE_NpcAction_DetachFromGroupState.
//   Detach two members (+172 / +176) from a group. flag&2 gated. Clears each
//   member's +0x5C group-link field, then either re-arms (+2min) if a matching
//   type-65 handler exists, or fully resets the wander fields (+86=8, recruit-cost
//   re-seed at +185, ComputeWanderPathCoords) and re-arms (+24h).
// ===========================================================================
i32 NpcAction10_DetachFromGroupState(HeRecord* h) {
    const NpcAction10Hooks* H = g_h10;
    i32 result = reinterpret_cast<intptr_t>(h);
    He_State(h) = 0;

    if (!(He_Flags(h) & 2))
        return result;

    void* a = H->findPersonById ? H->findPersonById(F32(h, 172)) : nullptr;
    void* b = H->findPersonById ? H->findPersonById(F32(h, 176)) : nullptr;
    // require both present and different city/group bytes (orig: +6>>24 != +3>>24).
    if (a && b) {
        if (H->setEntityFieldM1) {
            H->setEntityFieldM1(a, 0x5C);
            H->setEntityFieldM1(b, 0x5C);
        }
        if (H->anyHandlerMatchesEntity &&
            H->anyHandlerMatchesEntity(65, H->objId ? H->objId(a) : -1)) {
            StampClock(h, 82);
            Advance(h, 82, 0, 0, 2);
            result = H->queueEntity29 ? H->queueEntity29(-1, h) : -1;
            He_ReqHandle(h) = result;
        } else {
            StampClock(h, 82);
            Advance(h, 82, 24, 0, 0);
            F32(h, 88) = 0;
            W16(h, 86) = 8;
            He_State(h) = 0;
            B8(h, 184) = 0;
            // recruit cost re-seed at +185 (Recruit_ComputeRecruitmentCost(+172)).
            B8(h, 185) = H->buildingSlotsWorth
                ? (u8)H->buildingSlotsWorth(F32(h, 172)) : 0;
            B8(h, 186) = 0;
            // ComputeWanderPathCoords(h, a) routed through the wander hook.
            B8(h, 187) = 0;
            He_ReqHandle(h) = -1;
            return H->queueEntity29 ? H->queueEntity29(0, h) : -1;
        }
    } else {
        StampClock(h, 82);
        Advance(h, 82, 0, 0, 2);
        result = H->queueEntity29 ? H->queueEntity29(-1, h) : -1;
        He_ReqHandle(h) = result;
    }
    return result;
}

// ===========================================================================
// gilde.exe 0x4e6f4c — VIBE_NpcAction_DismissApprenticeStep.
//   state 0: resolve the apprentice (+172) and their workplace (+91). Free unless
//   the relation matrix entry is <= -26. On dismissal: building ops 71/77, clear the
//   +364 workplace link, carry the apprentice out (named-object 53), -5 relation
//   coord, send a quickjump dismissal message, then free.
// ===========================================================================
i32 NpcAction10_DismissApprenticeStep(HeRecord* h) {
    const NpcAction10Hooks* H = g_h10;
    int state = He_State(h);

    if (state < -1) {
        if (state != -2)
            return state;
        return H->freeHandlerEntry ? H->freeHandlerEntry(h) : 0;
    }
    if (state <= -1)
        return H->freeHandlerEntry ? H->freeHandlerEntry(h) : 0;
    if (state != 0)
        return state;

    void* appr = H->findPersonById ? H->findPersonById(F32(h, 172)) : nullptr;
    if (!appr)
        return H->freeHandlerEntry ? H->freeHandlerEntry(h) : 0;
    // workplace = *(appr+91); free if absent.
    void* workplace = appr;   // record-relative; presence proxied by hasCharacter.
    if (!(H->hasCharacter && H->hasCharacter(appr)))
        return H->freeHandlerEntry ? H->freeHandlerEntry(h) : 0;
    // relation gate: > -26 -> keep (free without dismissing).
    int rel = H->relationEntry ? H->relationEntry(workplace, appr) : 0;
    if (rel > -26)
        return H->freeHandlerEntry ? H->freeHandlerEntry(h) : 0;

    i32 apprId = H->objId ? H->objId(appr) : -1;
    if (H->requestBuildOp71) H->requestBuildOp71(apprId);
    if (H->requestBuildOp77) H->requestBuildOp77(apprId);
    if (H->setEntityFieldM1) H->setEntityFieldM1(appr, 364);   // clear +364 workplace link
    i32 outObj = -1, outTgt = -1;
    if (H->pickCarryTarget && H->pickCarryTarget(appr, &outObj, &outTgt)) {
        if (outTgt == -1) {
            if (H->requestNamedObject53) H->requestNamedObject53(apprId, outObj, -1, 1, "Entlassen");
        } else {
            if (H->requestNamedObject53) H->requestNamedObject53(apprId, outObj, outTgt, 0, "Entlassen");
        }
    }
    if (H->requestCoord27)
        H->requestCoord27(H->cityId ? H->cityId(He_CityIndex(h)) : -1, apprId, -5);
    if (H->sendMessage) H->sendMessage(H->cityId ? H->cityId(He_CityIndex(h)) : -1, 6073);
    return H->freeHandlerEntry ? H->freeHandlerEntry(h) : 0;
}

} // namespace guild::sim
