#include "sim/npc_daily.h"

#include "sim/npcaction.h"   // NpcClock() (shared global game clock)
#include "sim/gametime.h"
#include "util/math_random.h"

#include <cstdint>

namespace guild::sim {

// ===========================================================================
// Recovered season tables and window constants (get_bytes @0x6476FC / 0x64770C
// / 0x61F968 / 0x61F964 / 0x61F938). Byte-faithful.
// ===========================================================================
const float  kWorkStartHour[4] = { 8.0f, 7.0f, 8.0f, 9.0f };   // flt_6476FC
const float  kWorkEndHour[4]   = { 20.0f, 21.0f, 20.0f, 19.0f }; // flt_64770C
const float  kWorkStartSlack   = -1.0f;   // flt_61F968
const float  kEveningOffset    = 2.0f;    // flt_61F964
const double kPickWeightFactor = 0.0125;  // dbl_61F938

// ===========================================================================
// Leaf-hook plumbing.
// ===========================================================================
static const NpcDailyHooks kInert{};
static const NpcDailyHooks* g_hd = &kInert;

void SetNpcDailyHooks(const NpcDailyHooks* hooks) { g_hd = hooks ? hooks : &kInert; }
const NpcDailyHooks& GetNpcDailyHooks() { return *g_hd; }

// ---------------------------------------------------------------------------
// Shared helpers (mirror the original's clock-image copy into +82 then advance).
// ---------------------------------------------------------------------------
static inline void StampAppt(HeRecord* h) { He_ApptTime(h) = NpcClock(); }
static inline HeRecord* FreeResult(HeRecord* h) {
    i32 r = g_hd->freeHandlerEntry ? g_hd->freeHandlerEntry(h) : 0;
    return reinterpret_cast<HeRecord*>(static_cast<std::intptr_t>(r));
}
// gilde.exe 0x5831f0 — VIBE_GameTime_Set(rec@eax, hour@dl, second@cl, minute@bl):
//   rec.second(+10)=second; rec.hour(+4)=hour; rec.minute(+6)=minute. (Tiny,
//   self-contained; inlined here to avoid a cross-module dep.)
static inline void GameTimeSet(GameTime* rec, int hour, int second, int minute) {
    rec->second = static_cast<i32>(static_cast<u8>(second));
    rec->hour   = static_cast<u16>(static_cast<u8>(hour));
    rec->minute = static_cast<i32>(static_cast<u8>(minute));
}
// The clock hour (WORD2(qword_13CE852)) — the hour-of-day used in the season
// window comparisons. Treated as a double in the original (it casts the WORD2).
static inline double ClockHour() { return static_cast<double>(NpcClock().hour); }

// "is this person live this round?" — byte_12CEA75 || byte_12CEA74 nonzero.
static inline bool RowLive(const DailyPersonRow& r) {
    return r.valid && (r.activeB != 0 || r.activeA != 0);
}

// ===========================================================================
// The pure RULE: schedule selection by state + season + hour + person row.
//   Morning (state 0): if hour < workStart-1 (flt_6476FC[s] + flt_61F968), and
//     the person has a home/work building + a dest + not skip-bit (0x1000) + not
//     already-dispatched (0x100000) -> GoToWork.
//   Evening (state 1): if hour > workEnd+2 (flt_64770C[s] + flt_61F964) -> GoHome.
//     Otherwise (work-end window) -> tavern if eligible (currency>3200 + roll),
//     else home, gated by the social bits (0x800 / 0x80000).
// This is a faithful extraction of the per-person decision (sans the entity
// searches / command emits, which the director below performs around it).
// ===========================================================================
DailyActivity SelectDailyActivity(int state, int season, int hour,
                                  const DailyPersonRow& row, bool tavernEligible) {
    if (!RowLive(row))
        return DailyActivity::kNone;
    if (row.homeBld == 0 || row.destBld == 0)
        return DailyActivity::kNone;

    const double h = static_cast<double>(hour);
    if (state == 0) {
        if ((row.turnBits & kDailySkip) != 0)
            return DailyActivity::kNone;
        const double startWindow =
            static_cast<double>(kWorkStartHour[season] + kWorkStartSlack);
        if (h < startWindow)
            return DailyActivity::kGoToWork;
        return DailyActivity::kNone;
    }
    if (state == 1) {
        const double homeWindow =
            static_cast<double>(kWorkEndHour[season] + kEveningOffset);
        if (h > homeWindow)
            return DailyActivity::kGoHome;
        // Work-end window: social dispatch already requested -> none.
        if ((row.turnBits & kDailySocialReq) != 0)
            return DailyActivity::kNone;
        return tavernEligible ? DailyActivity::kGoToTavern : DailyActivity::kGoHome;
    }
    return DailyActivity::kNone;
}

// ===========================================================================
// gilde.exe 0x4e7e88 — VIBE_NpcAction_DailyRoutineStep.
//   Disasm-cross-checked: the parallel columns byte_12CEA74/75 (+356/+357),
//   dword_12CEA7C (+364) / dword_12CEA80 (+368) / dword_12CEA94 (+388) /
//   dword_12CEAD8 (+456 turn-bits), id column dword_12CE914 (+4); the season is
//   GameTime_GetSeasonFromDay (day % 4) keying flt_6476FC / flt_64770C. The
//   Hex-Rays output carried the usual uninitialised-temp artifacts (v11/v25/v30
//   etc.); the control flow below is pinned against the disassembly basic blocks
//   at 0x4e7ee0 (state-0 morning sweep), 0x4e81f0 (work-start second sweep),
//   0x4e8383 (state-1 candidate gather), 0x4e85bc (social sweep) and 0x4e882a
//   (evening go-home sweep). Cross-cluster leaves go through NpcDailyHooks.
// ===========================================================================
HeRecord* NpcDaily_DailyRoutineStep(HeRecord* h) {
    const NpcDailyHooks* H = g_hd;
    const GameTime& clk = NpcClock();
    const int season = SeasonFromDay(clk.day);   // v77[4] = day % 4

    const i32 state = He_State(h);
    if (state == -1 || state == -2)
        return FreeResult(h);                     // LABEL_2: free handler entry

    const int count = H->personCount ? H->personCount() : 0;

    // ----------------------------- STATE 0 -----------------------------------
    // Morning work-dispatch sweep. Loop over persons until a single dispatch
    // (v4 < 1 guard: the original breaks the morning loop after one success).
    if (state == 0) {
        int dispatched = 0;   // v4
        for (int i = 0; i < count && dispatched < 1; ++i) {
            DailyPersonRow row = H->personRow ? H->personRow(i) : DailyPersonRow{};
            if (!RowLive(row)) continue;
            if (row.homeBld == 0) continue;       // dword_12CEA7C
            if (row.destBld == 0) continue;       // dword_12CEA94
            if ((row.turnBits & kDailySkip) != 0) continue;  // & 0x1000

            i32 u = 0, obj = 0;
            if (!(H->findCarryTarget && H->findCarryTarget(i, &u, &obj)))
                continue;
            i32 d44 = 0, d48 = 0;
            if (!(H->destDoorIds && H->destDoorIds(i, &d44, &d48)))
                continue;
            if (u == d44 && obj == d48) continue;  // already at destination

            // If home is a production building OR has no mesh (+97 == 0): a plain
            // "go to work" — RequestBuildOp77 + ChrMove + String47, set 0x100000.
            bool prod = H->homeIsProduction && H->homeIsProduction(i);
            bool hasMesh = H->homeHasMesh && H->homeHasMesh(i);
            if (prod || !hasMesh) {
                if (H->requestBuildOp77) H->requestBuildOp77(row.personId);
                if (H->requestChrMoveToUniverse)
                    H->requestChrMoveToUniverse(row.personId, u, obj, "dummy_EINGANG");
                if (H->queueRequestString47)
                    H->queueRequestString47(row.personId, u, obj);
                row.turnBits |= kDailyDispWork;
                if (H->setTurnBits) H->setTurnBits(i, row.turnBits);
                // (the original keeps scanning; this is not counted as v4 success)
                continue;
            }
            // Else: owner-kind gated "named go-to-work" (kind 6/7 == player-owned
            // skip), capped by the character budget and the bone-chain distance.
            u8 ownerKind = H->ownerKind ? H->ownerKind(i) : 0;
            if (ownerKind == 6 || ownerKind == 7) continue;
            if (!(H->characterBudgetOk && H->characterBudgetOk())) continue;
            if (!(H->workDistanceOk && H->workDistanceOk(i))) continue;
            // Season window: hour < workStart-1.
            const double startWindow =
                static_cast<double>(kWorkStartHour[season] + kWorkStartSlack);
            if (ClockHour() < startWindow) {
                if (H->requestBuildOp77) H->requestBuildOp77(row.personId);
                if (H->queueRequestNamedObject53)
                    H->queueRequestNamedObject53(row.personId, u, 0, obj, 0, "Go to work");
                ++dispatched;
                row.turnBits |= kDailyDispWork;
                if (H->setTurnBits) H->setTurnBits(i, row.turnBits);
            }
        }
        if (dispatched) {
            StampAppt(h);
            GameTimeAdvance(&He_ApptTime(h), 0, 0, 5);   // +5 min
        }

        // Work-start transition: once hour >= workStart, do the second sweep
        // (chr-move all live workers to dummy_EINGANG) then set state=1 with the
        // appointment at the work-END hour (GameTime_Set), reset counter.
        if (ClockHour() >= static_cast<double>(kWorkStartHour[season])) {
            for (int i = 0; i < count; ++i) {
                DailyPersonRow row = H->personRow ? H->personRow(i) : DailyPersonRow{};
                if (!RowLive(row)) continue;
                if (row.homeBld == 0 || row.destBld == 0) continue;
                if ((row.turnBits & kDailySkip) != 0) continue;
                i32 u = 0, obj = 0;
                if (!(H->findCarryTarget && H->findCarryTarget(i, &u, &obj))) continue;
                i32 d44 = 0, d48 = 0;
                if (!(H->destDoorIds && H->destDoorIds(i, &d44, &d48))) continue;
                if (u == d44 && obj == d48) continue;
                if (H->requestBuildOp77) H->requestBuildOp77(row.personId);
                if (H->requestChrMoveToUniverse)
                    H->requestChrMoveToUniverse(row.personId, u, obj, "dummy_EINGANG");
                if (H->queueRequestString47)
                    H->queueRequestString47(row.personId, u, obj);
                row.turnBits |= kDailyDispWork;
                if (H->setTurnBits) H->setTurnBits(i, row.turnBits);
            }
            // Appointment := work-END hour-of-day (Coord_ConvertX truncate of
            // flt_64770C[season]); state := 1; counter (+172) := 0.
            StampAppt(h);
            int endHour = static_cast<int>(kWorkEndHour[season]);
            GameTimeSet(&He_ApptTime(h), endHour, 0, 0);
            He_State(h) = 1;
            He_Counter172(h) = 0;
        }
        return h;
    }

    // ----------------------------- STATE 1 -----------------------------------
    // Social / evening sweep. Entry requires the saved-day == clock-day (the
    // original compares (DWORD)qword_13CE852 == *(+68)); else free.
    if (state == 1) {
        if (clk.day != He_SavedTime(h).day) {
            (void)FreeResult(h);
            return h;
        }

        const int candidates = H->candidateCount ? H->candidateCount() : 0;  // v21
        int dispatched = 0;   // v4

        // Pass A: social dispatch for player-owned (kind 6/7) persons; one per
        // round (the original counts v4 and stops at >=1 in pass B).
        for (int i = 0; i < count; ++i) {
            DailyPersonRow row = H->personRow ? H->personRow(i) : DailyPersonRow{};
            if (!RowLive(row)) continue;
            if (row.homeBld == 0 || row.destBld == 0) continue;
            u8 cls = H->aiPlayerClass ? H->aiPlayerClass(i) : 0;
            if (cls == 4 || cls == 16 || cls == 19) continue;
            i32 u = 0, obj = 0;
            if (!(H->findInteractionTarget && H->findInteractionTarget(i, &u, &obj)))
                continue;
            i32 d44 = 0, d48 = 0;
            if (!(H->destDoorIds && H->destDoorIds(i, &d44, &d48))) continue;
            if (u == d44 && obj == d48) continue;
            if ((row.turnBits & kDailySocialReq) != 0) continue;  // & 0x800
            u8 ownerKind = H->ownerKind ? H->ownerKind(i) : 0;
            if (ownerKind != 6 && ownerKind != 7) continue;

            if (H->requestBuildOp77) H->requestBuildOp77(row.personId);
            // Tavern roll: candidates>0 && currency>3200 && RandomModulo(2).
            bool tavern = false;
            if (candidates && H->currencyHeld && H->currencyHeld(i) > 3200
                && (util::RandomModulo(2) & 0xFFFF)) {
                i32 tu = 0, tobj = 0;
                if (H->pickTavern && H->pickTavern(i, &tu, &tobj)) {
                    if (H->queueRequestNamedObject53)
                        H->queueRequestNamedObject53(row.personId, tu, 0, tobj, 0,
                                                     "Go to wirtshaus");
                    tavern = true;
                }
            }
            if (!tavern) {
                if (obj == -1) {
                    if (H->queueRequestNamedObject53)
                        H->queueRequestNamedObject53(row.personId, u, 0, -1, 1, "Go home");
                } else {
                    if (H->queueRequestNamedObject53)
                        H->queueRequestNamedObject53(row.personId, u, 0, obj, 0, "Go home");
                }
            }
            // QueueRequestArgs25(id, fieldOffset(+456 col), 2048, 4, 4096); set 0x80000.
            if (H->queueRequestArgs25)
                H->queueRequestArgs25(row.personId, kPfTurnBits, 2048, 4, 4096);
            row.turnBits |= kDailyDispSocial;
            if (H->setTurnBits) H->setTurnBits(i, row.turnBits);
            ++dispatched;
            He_Counter172(h) = i;
            break;
        }

        if (dispatched) {
            StampAppt(h);
            GameTimeAdvance(&He_ApptTime(h), 0, 0, 5);   // +5 min
        }

        // Evening go-home sweep: once hour > workEnd+2, send everyone home.
        const double homeWindow =
            static_cast<double>(kWorkEndHour[season] + kEveningOffset);
        if (ClockHour() > homeWindow) {
            for (int i = 0; i < count; ++i) {
                DailyPersonRow row = H->personRow ? H->personRow(i) : DailyPersonRow{};
                if (!RowLive(row)) continue;
                if (row.homeBld == 0) continue;
                if (row.workBld == 0) continue;   // dword_12CEA80
                if (row.destBld == 0) continue;
                u8 cls = H->aiPlayerClass ? H->aiPlayerClass(i) : 0;
                if (cls == 4 || cls == 16 || cls == 19) continue;
                i32 u = 0, obj = 0;
                if (!(H->findInteractionTarget && H->findInteractionTarget(i, &u, &obj)))
                    continue;
                i32 d44 = 0, d48 = 0;
                if (!(H->destDoorIds && H->destDoorIds(i, &d44, &d48))) continue;
                if (u == d44 && obj == d48) continue;
                if ((row.turnBits & kDailySocialReq) != 0) continue;  // & 0x800
                if (H->requestBuildOp77) H->requestBuildOp77(row.personId);
                if (obj == -1) {
                    if (H->requestChrMoveToUniverse)
                        H->requestChrMoveToUniverse(row.personId, u, -1, "dummy_TUER");
                } else {
                    if (H->requestChrMoveToUniverse)
                        H->requestChrMoveToUniverse(row.personId, u, obj, "dummy_EINGANG");
                    if (H->queueRequestString47)
                        H->queueRequestString47(row.personId, u, obj);
                }
                row.turnBits |= kDailyDispSocial;
                if (H->setTurnBits) H->setTurnBits(i, row.turnBits);
                if (H->queueRequestArgs25)
                    H->queueRequestArgs25(row.personId, kPfTurnBits, 2048, 4, 4096);
            }
        }
        return h;
    }

    // default: noop
    return h;
}

} // namespace guild::sim
