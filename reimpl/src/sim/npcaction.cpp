#include "sim/npcaction.h"

#include "sim/gametime.h"
#include "util/math_random.h"
#include "util/math_rng_float.h"   // RandomFloatScaled (VIBE_Math_RandomFloatScaled 0x58b910)

#include <cstdint>

namespace guild::sim {

// ===========================================================================
// Global game clock + leaf-hook plumbing.
// ===========================================================================
static GameTime g_npcClock{};

GameTime& NpcClock() { return g_npcClock; }
void SetNpcClock(const GameTime& t) { g_npcClock = t; }

// Inert default hooks: every effect a no-op, queries return "absent".
static const NpcLeafHooks kInertHooks{};
static const NpcLeafHooks* g_hooks = &kInertHooks;

void SetNpcLeafHooks(const NpcLeafHooks* hooks) {
    g_hooks = hooks ? hooks : &kInertHooks;
}
const NpcLeafHooks& GetNpcLeafHooks() { return *g_hooks; }

// Convenience: stamp the global clock into a GameTime slot (14-byte image copy).
static inline void StampClock(GameTime& dst) { dst = g_npcClock; }

// The Person id column dword_12CE914[134*i] gives a city/person id from an index.
// The full Person-array substrate lives in entity.{h,cpp}; this module only needs
// the id-by-index mapping, which it receives through the caller-supplied value.
// SetTargetCityRef below resolves it via a tiny indirection the host installs;
// when absent, the index itself is echoed (sufficient for isolation tests).
static i32 (*g_cityIdFromIndex)(u16 index) = nullptr;
void SetNpcCityIdResolver(i32 (*fn)(u16)) { g_cityIdFromIndex = fn; }

// ===========================================================================
// Recovered float constants for NpcAdjustRelationByMood (0x624E30 block).
// ===========================================================================
static constexpr double kMoodCeiling    = 252.0;       // dbl_624E30
static constexpr float  kMoodCeilingF    = 252.0f;      // flt_624E38
// flt_624E3C @0x624E3C: raw bytes 21 08 82 3B == 0x3B820821 == float
// 0.003968254197388887 (float(1/252)). Was mistranscribed as 0.0039682314f
// (~49 ulps off) — fixed against get_bytes @0x624E3C.
static constexpr float  kMoodInvCeiling  = 0x1.041042p-8f; // flt_624E3C
static constexpr double kMoodCurveA      = 0.6;          // dbl_624E40
static constexpr double kMoodCurveB      = 0.2;          // dbl_624E48
static constexpr float  kMoodScale       = 42.0f;        // flt_624E50
// VIBE_Math_RandomFloatScaled @0x58b910 = (int)RandNext() * flt_62675C, where
// flt_62675C @0x62675C = 0x38000100 = float(1/32767) — NOT 1/32768. The local
// copy that multiplied by 1/32768 is replaced by the canonical
// util::RandomFloatScaled (math_rng_float.cpp, kCrtScale).

// ===========================================================================
// Dispatch table (funcs_5766CB @0x63d964) — recovered addresses, in order.
// Index == action type (0..0x44). Entry 0x45 onward is out of range (->-2).
// ===========================================================================
const u32 kNpcActionTableAddrs[kNpcActionTableSize] = {
    0x5712a0, 0x5713a4, 0x571498, 0x5716a0, 0x571794, 0x571898, 0x571990,
    0x571a74, 0x571bb4, 0x571c98, 0x571d7c, 0x571e60, 0x571f44, 0x572038,
    0x572120, 0x572204, 0x572308, 0x572400, 0x572538, 0x57261c, 0x572700,
    0x5727f8, 0x572904, 0x572a14, 0x572b24, 0x572d18, 0x572ed8, 0x572fe8,
    0x5730f8, 0x573204, 0x57340c, 0x573618, 0x5737b0, 0x573870, 0x573930,
    0x573bd8, 0x573ebc, 0x5740ec, 0x574388, 0x57458c, 0x57477c, 0x574784,
    0x5749f8, 0x574c3c, 0x574e88, 0x575028, 0x57502c, 0x575274, 0x575414,
    0x5755ac, 0x575948, 0x575a4c, 0x575b58, 0x57477c, 0x57477c, 0x57477c,
    0x575c64, 0x575da0, 0x575eac, 0x575fac, 0x5760a8, 0x576198, 0x576258,
    0x576334, 0x5763d0, 0x57646c, 0x576508, 0x576590, 0x576618,
};

// Adapter wrappers: the table entries take (record, type); our translated step
// functions take just the record. Only the entries we have translated are wired;
// the rest are nullptr (deferred — see DEFERRED list at the bottom).
namespace {
i32 Step_DebugRetZero(HeRecord*) { return 0; }  // 0x575028 VIBE_DebugCmd_RetZero
} // namespace

// Map a dispatch-table address to a translated step fn, or nullptr if deferred.
// Only a handful of the 69 entries fall inside this module's representative set;
// the address-keyed switch keeps the mapping explicit and auditable.
NpcActionStepFn NpcAction_TableEntry(int type) {
    if (type < 0 || type >= kNpcActionTableSize)
        return nullptr;
    switch (kNpcActionTableAddrs[type]) {
        case 0x575028:           // VIBE_DebugCmd_RetZero (entry 0x2D)
            return &Step_DebugRetZero;
        default:
            return nullptr;      // deferred / not in this module's scope
    }
}

// gilde.exe 0x5766a0 — VIBE_NpcAction_Dispatch.
i32 NpcAction_Dispatch(HeRecord* h) {
    // if (gameClock.day < 8) return VIBE_DebugCmd_RetZero();  ( == 0 )
    if (g_npcClock.day < 8)
        return 0;
    if (!h)
        return -1;
    // type = *(u16*)(h + 4). NOTE: the original reads the action type from the
    // record's +4 *word* (the low half of the id slot doubles as the type tag in
    // the dispatched-record view). type >= 0x45 -> -2.
    u16 type = *reinterpret_cast<u16*>(HeBytes(h) + 4);
    if (type >= 0x45u)
        return -2;
    NpcActionStepFn fn = NpcAction_TableEntry(type);
    if (!fn)
        return 0;                // deferred entry: behave as the no-op leaf
    return fn(h);
}

// ===========================================================================
// Appointment / timestamp helpers.
// ===========================================================================

// gilde.exe 0x4c9458 — VIBE_NpcAction_StampTimeAndRequestEntity.
HeRecord* NpcAction_StampTimeAndRequestEntity(HeRecord* h) {
    StampClock(He_ApptTime(h));               // +82 <- clock
    if ((He_Flags(h) & kHeNeedsCmd29) != 0) { // flag 0x02
        if (g_hooks->queueRequestEntity29)
            g_hooks->queueRequestEntity29(-1, h);
    }
    return h;
}

// gilde.exe 0x4e5b10 — VIBE_NpcAction_CopyTargetCoord.
HeRecord* NpcAction_CopyTargetCoord(HeRecord* h) {
    He_ApptTime(h) = He_SavedTime(h);   // +82 <- +68
    return h;
}

// gilde.exe 0x4e5c00 — VIBE_NpcAction_ClearTargetCoord.
HeRecord* NpcAction_ClearTargetCoord(HeRecord* h) {
    StampClock(He_ApptTime(h));         // +82 <- clock
    return h;
}

// gilde.exe 0x4c9484 — VIBE_NpcAction_SetTargetCityRef.
i32 NpcAction_SetTargetCityRef(HeRecord* h, u16 cityIndex) {
    He_CityIndex(h) = cityIndex;        // +8 := index
    i32 result;
    if (cityIndex == 0xFFFFu) {
        result = -1;
    } else {
        result = g_cityIdFromIndex ? g_cityIdFromIndex(cityIndex)
                                   : static_cast<i32>(cityIndex);
    }
    He_CityId(h) = result;              // +12 := resolved id
    return result;
}

// gilde.exe 0x4cc018 — VIBE_NpcAction_ResetToState0.
int NpcAction_ResetToState0(HeRecord* h) {
    StampClock(He_ApptTime(h));                       // +82 <- clock
    int result = GameTimeAdvance(&He_ApptTime(h), 2, 0, 0);  // +2 days
    He_State(h) = 0;                                  // +112
    return result;
}

// gilde.exe 0x4cdcc0 — VIBE_NpcAction_AddTimeToActionDuration.
//   hour(+86) += RandomModulo(21 - hour); second(+92) := 0; minute(+88) := result
//   where result = RandomModulo(0x3B) (0..58). Returns result.
int NpcAction_AddTimeToActionDuration(HeRecord* h) {
    StampClock(He_ApptTime(h));                       // +82 <- clock
    u16 hour = He_ApptTime(h).hour;                   // +86
    hour = static_cast<u16>(hour + util::RandomModulo(static_cast<u16>(21 - hour)));
    He_ApptTime(h).hour = hour;
    int result = static_cast<u16>(util::RandomModulo(0x3B));  // 0..58
    He_ApptTime(h).second = 0;                        // +92 := 0
    He_ApptTime(h).minute = result;                   // +88 := result
    return result;
}

// ===========================================================================
// Walk-begin family — arm an appointment N units ahead. Each stamps the clock
// into +82 then advances by a fixed delta.
// ===========================================================================
int NpcAction_BeginWalkAndFace(HeRecord* h) {       // 0x4e6d00 (+1 second)
    StampClock(He_ApptTime(h));
    return GameTimeAdvance(&He_ApptTime(h), 0, 1, 0);
}
int NpcAction_BeginWalkPhase4(HeRecord* h) {        // 0x4e7394 (+4 days)
    StampClock(He_ApptTime(h));
    return GameTimeAdvance(&He_ApptTime(h), 4, 0, 0);
}
int NpcAction_BeginGenericWalkStep(HeRecord* h) {   // 0x4ec6b0 (+1 second)
    StampClock(He_ApptTime(h));
    return GameTimeAdvance(&He_ApptTime(h), 0, 1, 0);
}
int NpcAction_BeginCombatWalkStep(HeRecord* h) {    // 0x4ed910 (+1 second)
    StampClock(He_ApptTime(h));
    return GameTimeAdvance(&He_ApptTime(h), 0, 1, 0);
}

// ===========================================================================
// Idle-anim arming.
// ===========================================================================
// gilde.exe 0x4e7538 / 0x4eaeb0 (byte-identical twin).
int NpcAction_BeginIdleAnim(HeRecord* h) {
    StampClock(He_ApptTime(h));                       // +82 <- clock
    u16 v2 = static_cast<u16>(util::RandomModulo(0x1E));  // 0..29
    // Disasm 0x4e754c/0x4e754e (and twin 0x4eaec4/0x4eaec6): xor ebx,ebx /
    // xor ecx,ecx — addSeconds(ecx) IS zeroed (Hex-Rays shows it as an uninit
    // local, but the instructions prove 0). addMinutes = v2 - 20 (range -20..9).
    return GameTimeAdvance(&He_ApptTime(h), 1, 0, static_cast<i16>(v2) - 20);
}

// gilde.exe 0x4c9d58 — VIBE_NpcAction_BeginIdleWaitState.
i32 NpcAction_BeginIdleWaitState(HeRecord* h) {
    i32 result = reinterpret_cast<intptr_t>(h);
    if ((He_Flags(h) & kHeAlreadySpawned) == 0) {   // flag 0x04
        StampClock(He_ApptTime(h));                  // +82 <- clock
        GameTimeAdvance(&He_ApptTime(h), 1, 0, 0);   // +1 day
        He_Deadline(h) = g_npcClock;                 // +176 <- clock
        GameTimeAdvance(&He_Deadline(h), 24, 0, 0);  // +24 h
        // +182 (deadline.minute) := 0; +180 (deadline.hour / wait counter) :=
        // RandomModulo(4)+19; +192 region :=1 (state-ish marker)
        He_Deadline(h).minute = 0;                   // +182
        He_WaitCounter(h) = static_cast<u16>(util::RandomModulo(4) + 19);  // +180
        *reinterpret_cast<i32*>(HeBytes(h) + 192) = 1;
        result = g_hooks->queueRequestEntity29
                   ? g_hooks->queueRequestEntity29(0, h) : 0;
        He_ReqHandle(h) = result;                    // +132
    }
    return result;
}

// ===========================================================================
// Social greet/flirt/compliment trio + the relation-mood adjuster.
// ---------------------------------------------------------------------------
// The originals branch on the actor's guild rank byte (record+2 == 6): rank-6
// NPCs resolve an "office" target (VIBE_Amt_OpenOfficeWindow), everyone else
// resolves the Person record by the target id (target+4). The mood delta differs
// per verb (greet 1, flirt 3, compliment 4). The office-resolution path is UI/Amt
// coupled; in isolation it is routed through the leaf hooks (resolveOfficeTarget),
// and the Person-record path is provided directly by the caller.
// ===========================================================================
namespace {
int ResolveTargetAndAdjust(HeRecord* h, HeRecord* target, i8 moodKind) {
    if (*(HeBytes(h) + 2) == 6) {
        // rank-6: office path. In isolation the office target equals `target`
        // when provided; the original opens the Amt office window first.
        HeRecord* office = target;
        if (office) {
            NpcAdjustRelationByMood(office, moodKind);
            return 1;
        }
    } else {
        // Resolve the Person record by id (target+4). The caller passes the
        // resolved record directly (entity.cpp owns the id->record lookup).
        if (target) {
            NpcAdjustRelationByMood(target, moodKind);
            return 1;
        }
    }
    return 0;
}
} // namespace

int NpcAction_ResolveTargetAndGreet(HeRecord* h, HeRecord* target) {
    return ResolveTargetAndAdjust(h, target, 1);    // 0x56850c
}
int NpcAction_ResolveTargetAndFlirt(HeRecord* h, HeRecord* target) {
    return ResolveTargetAndAdjust(h, target, 3);    // 0x568578
}
int NpcAction_ResolveTargetAndCompliment(HeRecord* h, HeRecord* target) {
    return ResolveTargetAndAdjust(h, target, 4);    // 0x5685e4
}

// gilde.exe 0x56840c — VIBE_Npc_AdjustRelationByMood(person@eax, kind@dl).
//   The relation byte lives at person[+128+kind] (a 0..252 "relationship" level).
//   A randomized increment is computed from a curve over the current level and a
//   noise factor, clamped so the level cannot exceed the 252 ceiling, and emitted
//   as a RequestBuildOp93 command. Returns 0 if kind>=5 or already at ceiling.
int NpcAdjustRelationByMood(HeRecord* person, i8 kind) {
    if (kind >= 5)
        return 0;
    u8* base = HeBytes(person);
    u8& level = base[128 + kind];                    // person[+128+kind]
    if (static_cast<double>(level) >= kMoodCeiling)  // already at ceiling
        return 0;

    // v5 = (252 - level) * (1/252)            (normalised headroom, 0..1)
    // 0x56840c: the headroom (v12) and amplitude (v11) locals are spilled to
    // 4-byte FLOAT stack slots (fstp dword) — model the float rounding exactly.
    double v5 = (static_cast<double>(kMoodCeilingF) - static_cast<double>(level))
              * static_cast<double>(kMoodInvCeiling);
    float headroom = static_cast<float>(v5);                          // v12 (float slot)
    // v11 = (float)(v5 * 0.6 + 0.2)           (noise amplitude, float slot)
    float amp = static_cast<float>(v5 * kMoodCurveA + kMoodCurveB);   // v11
    // v6 = (RandFloat*amp + 1 - amp) * 42 * headroom + 1   -> increment
    double inc = (util::RandomFloatScaled() * amp + 1.0 - amp)
               * static_cast<double>(kMoodScale) * headroom + 1.0;
    u8 delta = static_cast<u8>(static_cast<int>(inc));  // truncate (ftol)

    // Clamp: if level+delta would exceed the ceiling, delta := 252 - level.
    if (static_cast<double>(delta + level) > kMoodCeiling) {
        double clamped = static_cast<double>(kMoodCeilingF) - static_cast<double>(level);
        delta = static_cast<u8>(static_cast<int>(clamped));
    }

    if (delta) {
        i32 id = He_Id(person);                      // person+4
        if (g_hooks->requestBuildOp93)
            g_hooks->requestBuildOp93(id, kind, id, delta);
    }
    return 1;
}

} // namespace guild::sim

// ===========================================================================
// DEFERRED — large render/pathfinder/cutscene/command-coupled NpcAction steps and
// the remaining dispatch-table targets, intentionally left out of this module's
// representative slice. Each is a 0.3-5KB state machine that calls into the
// render/anim/pathfinder/HUD clusters this module forward-declares but does not
// own. Recover them in a later pass (one at a time, diffing per basic block).
//
//   0x4e8bdc VIBE_NpcAction_RunMarktSupervisorStep   (0x152d) market supervisor
//   0x4eb518 VIBE_NpcAction_BurglaryStep             (0x1198) burglary
//   0x4cce04 VIBE_NpcAction_RecruitmentState         (0x0ebc) recruitment
//   0x4e7e88 VIBE_NpcAction_DailyRoutineStep         (0x0cfd) daily routine driver
//   0x4c9dec VIBE_NpcAction_NotifyJoinLeaveGroup     (0x086a)
//   0x4ec6f0 VIBE_NpcAction_BuildingEspionageStep    (0x08bf)
//   0x4ed018 VIBE_NpcAction_CityFormationMoveStep    (0x08f7)
//   0x4ea1e8 VIBE_NpcAction_JailCellStep             (0x0921)
//   0x4ed95c VIBE_NpcAction_AttackTargetStep         (0x0929)
//   0x4e6618 VIBE_NpcAction_HarvestScriptStep        (0x06e8)
//   0x4e4f70 VIBE_NpcAction_EventMessageboxStep      (0x064b)
//   0x4cc04c VIBE_NpcAction_FollowTargetState        (0x0643)
//   0x4737fc VIBE_NpcAction_EvaluateCombatTarget     (0x0604)
//   0x4eaf04 VIBE_NpcAction_ThievesGuildRansomStep   (0x058b)
//   0x4cb074 VIBE_NpcAction_ConversationState        (0x0566)
//   0x472068 VIBE_NpcAction_EvaluateGuardPatrol      (0x0557)
//   0x4e5e4c VIBE_NpcAction_MasterExamPassedStep     (0x052a)
//   0x54dae0 VIBE_NpcAction_DistributeCreditToItems  (0x04fc)
//   0x568a5c VIBE_NpcAction_SpreadRumorToGroup       (0x0467)
//   0x4caa10 VIBE_NpcAction_EvaluateAttackState      (0x0467)
//   0x4747dc VIBE_NpcAction_EvaluateCourtTrial       (0x0439)
//   0x4cdf74 VIBE_CharAction_PatrolStep              (0x036b) patrol (render+cmd)
//   0x4cc914 VIBE_NpcAction_ScanNeighborsState       (0x01bf) (Building/Coord cpl)
//   0x4c9ac4 VIBE_NpcAction_HandleAccidentRandom     (0x0271) (Text/He/Script cpl)
//   0x4c97c0 VIBE_NpcAction_HandleAccidentScripted   (0x0304) (Text/He/Script cpl)
//   0x474dd4 VIBE_NpcTarget_FindNearestEnemy         (0x0211) (spatial query)
//   0x474fe8 VIBE_NpcTarget_PickDirectionSeqA        (0x028a) (spatial query)
//   0x475274 VIBE_NpcTarget_PickDirectionSeqB        (0x03c2) (spatial query)
//   ... plus the remaining ~115 NpcAction / 35 NpcEvent / ContextAction / AiAction
//   funcs (the long mechanical tail + the UI/Amt-coupled ContextAction menu set).
//   Dispatch-table entries with no translated step return 0 (inert no-op leaf),
//   exactly as the engine's debug/unimplemented slots do.
// ===========================================================================
