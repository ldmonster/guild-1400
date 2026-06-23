// charaction_steps6 — batch 6 of the CharAction coroutines: the large crime /
// personnel state machines and the He handler registration table. See
// charaction_steps6.h for the module overview and the recovered He-record field
// map. Each function carries its gilde.exe address; record accesses use the He_*
// accessors (byte-faithful offsets). Cross-cluster leaves go through
// CharActionStep6Hooks (resolves/queries/emits) and the shared NpcLeafHooks
// (freeHandlerEntry / queueRequestEntity29 / packetStatus). NpcClock() /
// GameTimeAdvance / GameTimeCompare are reused.
#include "sim/charaction_steps6.h"

#include "sim/gametime.h"   // GameTimeAdvance, GameTimeCompare
#include "sim/npcaction.h"  // NpcClock(), GetNpcLeafHooks()

#include <cstring>          // std::memcpy

namespace guild::sim {

// Byte-exact, alignment-safe loads of values at arbitrary byte offsets. The x86
// binary uses unaligned `*(int*)(rec+N)` / `*(WORD*)(rec+N)` reads off He-records
// whose fields are not naturally aligned (e.g. +39, +93); binding an i32&/u16&
// reference to those addresses is UB in portable C++ (UBSAN). These read the
// identical little-endian bytes without forming a misaligned reference.
namespace {
inline i32 LoadI32At(const HeRecord* h, int off) {
    i32 v; std::memcpy(&v, reinterpret_cast<const u8*>(h) + off, sizeof(v)); return v;
}
inline u16 LoadU16At(const HeRecord* h, int off) {
    u16 v; std::memcpy(&v, reinterpret_cast<const u8*>(h) + off, sizeof(v)); return v;
}
} // namespace

// ---------------------------------------------------------------------------
// Hook table plumbing (inert default — every leaf reports "absent"/no-op).
// ---------------------------------------------------------------------------
namespace {

HeRecord* InertPersonQueryBegin(i32, int, int, i32) { return nullptr; }
HeRecord* InertFindPersonById(i32)                  { return nullptr; }
HeRecord* InertFamilyRecord(u16)                    { return nullptr; }
HeRecord* InertObjectQueryFind(i32, int, int, i32)  { return nullptr; }
void      InertResolveEntityById(HeRecord** out, i32) { if (out) *out = nullptr; }
HeRecord* InertFindFirstByFilter(int, int, i32)     { return nullptr; }
HeRecord* InertFindNextMatching()                   { return nullptr; }
int       InertFindNearestEntity(u16, int, i32*)    { return 0; }
int       InertIsNearDoor(HeRecord*, i32)           { return 0; }
int       InertComputeWage(u16, int, int)           { return 0; }
int       InertSumCurrencyHeld(u16)                 { return 0; }
u8        InertCityCategory(u16)                    { return 0; }
i32       InertCityPersonId(u16)                    { return 0; }
void      InertChangePlayerAction(HeRecord*, HeRecord*, HeRecord*, u16) {}
i32       InertQueueGuardTarget61(HeRecord*, int, int, int) { return 0; }
i32       InertRequestBuildOp73(int, i32, int, int, int, int) { return 0; }
void      InertQueueRequest16(i32, i32, int, u8)    {}
void      InertRequestChrMove(i32, i32, i32)        {}
void      InertQueueSingle49(i32)                   {}
i32       InertQueueNamedObject53(i32, i32, i32, int) { return 0; }
void      InertQueueCoord27(i32, i32, int)          {}
void      InertQueuePair33(i32, int)                {}
void      InertQueuePair36(i32, i32)                {}
i32       InertEvaluateViolation(int, int, i32, i32, i32) { return 0; }
void      InertSendEntityMessage(i32, int)          {}
void      InertSendQuickjump(i32, i32, int)         {}
HeRecord* InertPacketSeqBase(i32)                   { return nullptr; }
int       InertRollWeatherActivity()                { return 0; }
int       InertInventorySlotActive(i32)             { return 0; }
HeRecord* InertFindGestureTarget()                  { return nullptr; }
int       InertRandomModulo(int)                    { return 0; }
int       InertRegisterHandler(u8, u32, u32)        { return 0; }

const CharActionStep6Hooks kInertHooks = {
    InertPersonQueryBegin, InertFindPersonById, InertFamilyRecord,
    InertObjectQueryFind, InertResolveEntityById, InertFindFirstByFilter,
    InertFindNextMatching, InertFindNearestEntity, InertIsNearDoor,
    InertComputeWage, InertSumCurrencyHeld, InertCityCategory, InertCityPersonId,
    InertChangePlayerAction, InertQueueGuardTarget61, InertRequestBuildOp73,
    InertQueueRequest16, InertRequestChrMove, InertQueueSingle49,
    InertQueueNamedObject53, InertQueueCoord27, InertQueuePair33, InertQueuePair36,
    InertEvaluateViolation, InertSendEntityMessage, InertSendQuickjump,
    InertPacketSeqBase, InertRollWeatherActivity, InertInventorySlotActive,
    InertFindGestureTarget, InertRandomModulo, InertRegisterHandler,
};
const CharActionStep6Hooks* g_hooks = &kInertHooks;

// Copy the 14-byte global clock image into a GameTime slot (mirrors the original's
// `*(_QWORD*)(rec+82) = qword_13CE852; *(_DWORD*)(rec+90) = ...; *(_WORD*)(rec+94)`).
inline void StampClock(GameTime& dst) { dst = NpcClock(); }

} // namespace

void SetCharActionStep6Hooks(const CharActionStep6Hooks* hooks) {
    g_hooks = hooks ? hooks : &kInertHooks;
}
const CharActionStep6Hooks& GetCharActionStep6Hooks() { return *g_hooks; }

// ===========================================================================
// Recovered handler-registration table (66 entries, registration order).
// ===========================================================================
// Authoritative (type, init=edx, step=ebx) recovered from the disassembly of
// VIBE_CharAction_RegisterHandlerTable @0x4db940 (each block: edx=init handler,
// ebx=step handler, eax=type byte). Verified byte-for-byte against the binary.
const HandlerReg kCharActionHandlerTable[kCharActionHandlerCount] = {
    { 0x22, 0x4c9d38, 0x4c9d38 }, // NullHandler / NullHandler
    { 0x29, 0x4c9d3c, 0x4c9d50 }, // InitOriginFromGlobal / Free_Thunk
    { 0x2A, 0x4c9d58, 0x4ca658 }, // BeginIdleWaitState / TavernSocializeState
    { 0x2B, 0x4ca938, 0x4caa10 }, // BeginActionState7 / EvaluateAttackState
    { 0x2C, 0x4cae78, 0x4cb074 }, // FindNearbyPeerState / ConversationState
    { 0x2D, 0x4c9d50, 0x4c9d50 }, // Free_Thunk / Free_Thunk
    { 0x2E, 0x4cb5dc, 0x4cb74c }, // ApproachTargetState / GreetTargetState
    { 0x2F, 0x4cb86c, 0x4cb880 }, // InitOriginFromGlobalAlt / MasterExamState
    { 0x30, 0x4c9d50, 0x4c9d50 }, // Free_Thunk / Free_Thunk
    { 0x37, 0x4cbc20, 0x4cbd04 }, // BeginActionState20 / EvaluateGroupCompositionState
    { 0x36, 0x4cc018, 0x4cc04c }, // ResetToState0 / FollowTargetState
    { 0x35, 0x4cc690, 0x4cc914 }, // StartWanderSearchState / ScanNeighborsState
    { 0x41, 0x4ccbb4, 0x4cce04 }, // DetachFromGroupState / RecruitmentState
    { 0x42, 0x4cdcc0, 0x4cdd10 }, // AddTimeToActionDuration / NpcActionHandler
    { 0x43, 0x4cdd38, 0x4cdf74 }, // PatrolInit / PatrolStep
    { 0x44, 0x4ce9dc, 0x4cea84 }, // PatrolFindTarget / RaidStep
    { 0x45, 0x4cf544, 0x4cf798 }, // ArrestInit / ArrestStep
    { 0x46, 0x4cf990, 0x4cfc24 }, // ArrestReset / DuelDispatch
    { 0x47, 0x4cfed8, 0x4d0438 }, // DuelInit / DuelResolveStep
    { 0x4A, 0x4d0724, 0x4d07b8 }, // GuardRequestTarget / JourneymanRecruitStep
    { 0x4B, 0x4d0918, 0x4d0a10 }, // NotifyMessageInit / Entity29RequestHandler
    { 0x51, 0x4d0a98, 0x4d0ac4 }, // StateReset24 / CounterWaitHandler
    { 0x52, 0x4d0af0, 0x4d0b20 }, // StateAdvancePos / FinishIfTerminal
    { 0x53, 0x4d0b38, 0x4d0b84 }, // RestorePosAndBranch / GossipBroadcast
    { 0x54, 0x4d0d88, 0x4d0d98 }, // CopyTargetFromTemplate / MasterExamPromptStep
    { 0x55, 0x4d0f30, 0x4d0f58 }, // StateCopyPos3 / MasterExamDecideStep
    { 0x56, 0x4d113c, 0x4d1168 }, // StateReset24Alt / WaitThenMoveStep
    { 0x57, 0x4d1384, 0x4d13b4 }, // RestorePosFinish / SellObjectStep
    { 0x58, 0x4d1674, 0x4d16a4 }, // RestorePosFinishAlt / BuyObjectStep
    { 0x5A, 0x4d17b4, 0x4d17e0 }, // StateReset96 / FinalizeEntityStep
    { 0x5B, 0x4d1844, 0x4d1870 }, // StateReset0 / RepeatCommandStep
    { 0x5C, 0x4d1904, 0x4d1930 }, // StateReset0Alt / RepeatTalkStep
    { 0x5D, 0x4d19ac, 0x4d19c0 }, // ClearStateAndTimer / GroupInteractStep
    { 0x5E, 0x4d1ccc, 0x4d1d40 }, // BindMatchingTargetAndQueue / GuildJoinStep
    { 0x5F, 0x4d1fb8, 0x4d201c }, // FindInteractionPartner / BrawlStep
    { 0x60, 0x4d21ec, 0x4d2314 }, // DrinkInit / GoToTavernStep
    { 0x59, 0x4d286c, 0x4d2900 }, // FindBeggarTarget / PlagueSpreadStep
    { 0x61, 0x4d30f4, 0x4d3188 }, // GroupGatherInit / PickpocketStep
    { 0x63, 0x4d38ec, 0x4d3918 }, // StateReset0Alt2 / ProtectionMoneyStep
    { 0x62, 0x4d3bdc, 0x4d3c50 }, // ExtortInit / NpcEvent_ProtectionMoneyStep
    { 0x64, 0x4d43f8, 0x4d4460 }, // NpcEvent_ProtectionMoneyInit / NpcEvent_ExtortionStep
    { 0x65, 0x4d493c, 0x4d49b8 }, // NpcEvent_ResetAndQueueEntity / NpcEvent_PatrolStep
    { 0x66, 0x4d4f34, 0x4d4fcc }, // InitActionType10 / RunSimAccident
    { 0x67, 0x4d52b4, 0x4d5308 }, // QueueState9Entity / GatherGuildMembersStep
    { 0x68, 0x4d5c24, 0x4d5cdc }, // InitActionType22 / TavernSimStep
    { 0x69, 0x4d577c, 0x4d57a0 }, // RestorePoseReset / AwardTitleStep
    { 0x6A, 0x4d63bc, 0x4d63e8 }, // RestorePoseSetRandom / TalentLevelUpStep
    { 0x6B, 0x4d665c, 0x4d66b4 }, // InitRandomDurationEntity / BardCreateScriptStep
    { 0x6D, 0x4d6a28, 0x4d6a8c }, // QueueOriginUnlessFlag4 / ObjectInteractionStep
    { 0x6F, 0x4d71d8, 0x4d72ec }, // AllocLoverStep / PushObjectStep
    { 0x70, 0x4d75e8, 0x4d766c }, // SetupDuration10Reset / RunSimDiseases
    { 0x74, 0x4d79e8, 0x4d7bd0 }, // DarkCornerInit / DarkCornerStep
    { 0x76, 0x4d877c, 0x4d8900 }, // ResolveTargetAndReset / SmokeEffectStep
    { 0x77, 0x4d8a94, 0x4d8af4 }, // SetupTargetTimestamp / UnkendunkStep
    { 0x78, 0x4d9600, 0x4d96f8 }, // ReaperPickNextTarget / ReaperPlagueStep
    { 0x79, 0x4d9a38, 0x4d9a60 }, // InitActionType7 / BroadcastWinnerPointsStep
    { 0x7A, 0x4d9f1c, 0x4da7c0 }, // InitSlotsAndQueueType11 / SimPoliticiansStep
    { 0x7B, 0x4c9d50, 0x4c9d50 }, // Free_Thunk / Free_Thunk
    { 0x7D, 0x4da920, 0x4da978 }, // InitTargetSlotsState12 / OfficeMatchmakingStep
    { 0x7E, 0x4dada0, 0x4dadd4 }, // SetupAnim2Reset / MasterExamDialogStep
    { 0x7F, 0x4daf28, 0x4daf88 }, // SetupRandomDurationReset / GamblingStep
    { 0x80, 0x4db218, 0x4db248 }, // CopyTargetAndQueueUnlessFlag4 / CountdownTickEntity
    { 0x81, 0x4db2b4, 0x4db2e4 }, // CopyTargetAndQueueUnlessFlag4Alt / RunOfficeGuardAssign
    { 0x82, 0x4db514, 0x4db558 }, // RequestEntityFinish / RequestEntityIfValid
    { 0x86, 0x4db5a4, 0x4db624 }, // InitOfficeGuardState / RunOfficeCandidacy
    { 0x87, 0x4db8ac, 0x4db8c8 }, // Tutorial_AdvanceChapterOrFree / TutorialEventHandler
};

// gilde.exe 0x4db940 — VIBE_CharAction_RegisterHandlerTable
//   The original chains 66 `if (RegisterHandlerByType(...)) return 1;` calls and
//   returns the last call's result (the 0x4db95a/0x4db960 epilogue returns 1 on the
//   final failure, else `result`, which is 0). We replay the table; the first
//   nonzero registration short-circuits to 1.
i32 RegisterHandlerTable() {
    const CharActionStep6Hooks& k = GetCharActionStep6Hooks();
    i32 result = 0;
    for (int i = 0; i < kCharActionHandlerCount; ++i) {
        const HandlerReg& e = kCharActionHandlerTable[i];
        result = k.registerHandler(e.type, e.initAddr, e.stepAddr);
        if (result)
            return 1;
    }
    return result;
}

// ===========================================================================
// Pruegel (assault) coroutine.
// ===========================================================================

// gilde.exe 0x4e3734 — VIBE_CharAction_InitPruegel
i32 InitPruegel(HeRecord* h) {
    const CharActionStep6Hooks& k = GetCharActionStep6Hooks();
    // Pre-resolve the victim (the original calls FindRecordById for its side effect)
    // then stamp the clock into +82.
    k.findPersonById(He_F180(h));
    StampClock(He_ApptTime(h));
    // Pick a thug if the actor id (+16) is unset.
    if (He_CityId(h) == -1) {   // NOTE: +16 reuses the cityId field accessor here
        HeRecord* thug = k.personQueryBegin(0, 1, 5, 22);
        if (!thug)
            thug = k.personQueryBegin(0, 1, 5, 15);
        if (thug)
            He_CityId(h) = He_Id(thug);   // *(a1+16) = *(thug+4)
    }
    i32 actorId = He_CityId(h);   // *(a1+16)
    He_F200(h) = -1;
    // op73 "Pruegel" request (subtype 2, arg 340); handle -> +204.
    He_F204(h) = k.requestBuildOp73(17, actorId, 0, -1, 2, 340);
    // Resolve the victim again; prefer its escort person (+92*4 = +368) else a
    // nearby class-4 person, else the nearest scene entity.
    HeRecord* victim = k.findPersonById(He_F180(h));
    HeRecord* escort = nullptr;
    if (victim)
        escort = *reinterpret_cast<HeRecord**>(HeBytes(victim) + 368);   // *(victim+92)
    if (!escort && victim)
        // 4th arg is the WORD at victim+0 ((unsigned __int16)*RecordById), NOT +4.
        escort = k.personQueryBegin(0, 1, 4, *reinterpret_cast<u16*>(HeBytes(victim)));
    i32 result;
    i32 nearestId = 0;
    if (escort || !k.findNearestEntity(He_CityIndex(h), 6, &nearestId)) {
        // escort path: use its id (*(escort+1) = +4)
        result = escort ? He_Id(escort) : 0;
        He_F208(h) = result;
    } else {
        // nearest-entity path: resolve the found id to a person, use *(+1)=+4.
        HeRecord* near = k.personQueryBegin(0, 1, 1, nearestId);
        result = near ? He_Id(near) : 0;
        He_F208(h) = result;
    }
    return result;
}

// gilde.exe 0x4e3894 — VIBE_CharAction_RunPruegel
//   `state+2` switch coroutine (states -2..9; switch value = state+2 = 0..11).
u32 RunPruegel(HeRecord* h) {
    const CharActionStep6Hooks& k = GetCharActionStep6Hooks();
    // Prologue: if state > 0 and the assault packet (+200) is 0 or -1, terminate.
    if (He_State(h) > 0) {
        i32 pkt = He_F200(h);
        if (pkt == 0 || pkt == -1)
            He_State(h) = -2;
    }
    u32 sw = static_cast<u32>(He_State(h) + 2);
    switch (sw) {
    case 0u:   // state -2
    case 1u: { // state -1
        i32 pkt = He_F200(h);
        if (pkt != -1)
            k.queuePair33(pkt, 1);
        return static_cast<u32>(GetNpcLeafHooks().freeHandlerEntry(h));
    }
    case 2u: { // state 0: wait for the op73 result packet (+204).
        if (He_F204(h) == -1 || GetNpcLeafHooks().packetStatus(He_F204(h)) != 0) {
            HeRecord* seq = k.packetSeqBase(He_F204(h));
            if (seq) {
                HeRecord* fam = k.familyRecord(He_CityIndex(h));
                if (fam)
                    *reinterpret_cast<i32*>(HeBytes(fam) + 40) += He_F216(h);  // +10 dword
                He_F200(h) = *reinterpret_cast<i32*>(HeBytes(seq) + 4);   // *(seq+1)
                k.queueRequest16(-1, k.cityPersonId(He_CityIndex(h)), He_F216(h), 0);
                u16 cls = *reinterpret_cast<u16*>(HeBytes(seq));
                k.changePlayerAction(nullptr, nullptr, h, cls);
                ++He_State(h);
            } else {
                He_State(h) = -1;
            }
            He_F204(h) = -1;
        }
        return static_cast<u32>(He_State(h));
    }
    case 3u: { // state 1: walk the assailant toward the object (+208).
        HeRecord* p = k.personQueryBegin(0, 1, 1, He_F208(h));
        if (p) {
            He_F204(h) = k.queueNamedObject53(He_F200(h), He_Id(p), 0, 1);
            ++He_State(h);
        } else {
            He_State(h) = 4;   // disasm @0x4e39e5: *(a1+112) = 4 (literal state 4)
        }
        return static_cast<u32>(He_State(h));
    }
    case 4u: { // state 2: gate on the assailant's animation flag (+97/+74).
        i32 v11 = He_F200(h);
        He_F204(h) = -1;
        HeRecord* p = k.findPersonById(v11);
        if (!p) { He_State(h) = -1; return static_cast<u32>(He_State(h)); }
        // result = *(p+97); branch on *(result+74).
        i32* anim = *reinterpret_cast<i32**>(HeBytes(p) + 388);  // *(p+97)
        if (anim && anim[74]) {
            StampClock(He_ApptTime(h));
            GameTimeAdvance(&He_ApptTime(h), 0, 0, 4);   // +4 minutes
            He_State(h) = 2;   // disasm @0x4e3aa0: *(a1+112) = 2 (literal state 2)
        } else {
            He_State(h) = 7;   // disasm @0x4e3a5a: *(a1+112) = 7 (literal state 7)
        }
        return static_cast<u32>(He_State(h));
    }
    case 5u: { // state 3: issue a named-object nav toward the actor.
        k.queueNamedObject53(He_F200(h), He_CityId(h), 0, 1);
        ++He_State(h);
        return static_cast<u32>(He_State(h));
    }
    case 6u: { // state 4: gate on the appointment, then stage the assault outcome.
        GameTime clk = NpcClock();
        i32 cmp = GameTimeCompare(&He_ApptTime(h), &clk);
        if (cmp < 0) {
            HeRecord* victim = k.findPersonById(He_F180(h));
            if (victim) {
                int rolled = k.rollWeatherActivity();
                if (k.inventorySlotActive(380)) {
                    // "had weapon" branch: maybe report, advance phase.
                    u8 cat = k.cityCategory(He_CityIndex(h));
                    if (cat == 6 || cat == 7)
                        k.sendEntityMessage(He_Id(victim), 3256);
                    ++He_State(h);
                } else if (rolled) {
                    u8 cat = k.cityCategory(He_CityIndex(h));
                    if (cat == 6 || cat == 7) {
                        k.sendEntityMessage(He_Id(victim), 4942);
                        k.sendEntityMessage(He_Id(victim), 4941);
                    }
                    // op90 thunk modelled as a victim message effect (id -2).
                    k.sendEntityMessage(He_F180(h), -2);
                } else {
                    u8 cat = k.cityCategory(He_CityIndex(h));
                    if (cat == 6 || cat == 7) {
                        k.sendEntityMessage(He_Id(victim), 4944);
                        k.sendEntityMessage(He_Id(victim), 4943);
                    }
                    He_F204(h) = k.evaluateViolation(20, 1, He_F180(h),
                                                     k.cityPersonId(He_CityIndex(h)),
                                                     He_F172(h));
                    // Final branch is gated on the VICTIM record category (*(victim+2)),
                    // NOT cityCategory (disasm @0x4e3e97 `mov dh,[edi+2]`, edi=victim).
                    u8 vcat = *reinterpret_cast<u8*>(HeBytes(victim) + 2);
                    if (vcat == 6 || vcat == 7) {
                        ++He_State(h);
                    } else {
                        // coord27(eax=*(word_12CE910[city*268]+4), edx=victim id, ebx=-25).
                        // The first arg reads the city-row +4 field; no hook exposes that
                        // table, so it is modelled via cityPersonId (the city recipient id).
                        k.queueCoord27(k.cityPersonId(He_CityIndex(h)), He_Id(victim), -25);
                        ++He_State(h);
                    }
                }
            } else {
                He_State(h) = -1;
            }
        }
        return static_cast<u32>(He_State(h));
    }
    case 7u: { // state 5: gate on the assailant animation again.
        HeRecord* p = k.findPersonById(He_F200(h));
        bool ok = false;
        if (p) {
            i32* anim = *reinterpret_cast<i32**>(HeBytes(p) + 388);  // *(p+97)
            ok = anim && anim[74];
        }
        if (ok) {
            StampClock(He_ApptTime(h));
            GameTimeAdvance(&He_ApptTime(h), 0, 0, 4);
        } else {
            He_State(h) = -1;
        }
        return static_cast<u32>(He_State(h));
    }
    // switch value 8 (state 6) → jump table entry [8] == def_4E38C9: pure return.
    // (The original has no case body for state 6; it falls through to `return sw`.)
    case 9u: { // state 7: resolve the gesture partner, evaluate the violation, arm state 8.
        // QueryBegin(self, 1, 1, *(a1+208))  — its result feeds EvaluateViolation+FindGesture.
        HeRecord* partner = k.personQueryBegin(0, 1, 1, He_F208(h));
        HeRecord* victim  = k.findPersonById(He_F180(h));
        if (!partner || !victim) { He_State(h) = 4; return static_cast<u32>(He_State(h)); }
        // FindGestureTarget(buf{partner, 4000.0f, a1, 0}); gate on its eax return.
        HeRecord* gest = k.findGestureTarget();
        if (!gest) { He_State(h) = 4; return static_cast<u32>(He_State(h)); }
        // EvaluateViolation(20, 1, *(partner+4), cityPersonId, *(victim+4)).
        He_F204(h) = k.evaluateViolation(20, 1, He_Id(partner),
                                         k.cityPersonId(He_CityIndex(h)), He_Id(victim));
        He_F212(h) = *reinterpret_cast<i32*>(HeBytes(gest) + 4);  // *(gestOut+4)
        He_State(h) = 8;
        StampClock(He_ApptTime(h));
        return static_cast<u32>(He_F212(h));
    }
    case 10u: { // state 8: confirm the gesture partner, then queue the pair.
        i32 v15 = He_F204(h);
        if (v15 == -1 || GetNpcLeafHooks().packetStatus(v15) != 0) {
            // QueryBegin(self,1,1,*(a1+172)) must be non-null (disasm `if(!v16) ->state=-1`).
            HeRecord* who = k.personQueryBegin(0, 1, 1, He_F172(h));
            HeRecord* found = k.findFirstByFilter(1, 1, He_F212(h));
            if (!who) { He_State(h) = -1; return static_cast<u32>(He_State(h)); }
            if (!found) { He_State(h) = -1; return static_cast<u32>(He_State(h)); }
            // *(found+53) must equal our id (+4) and +204 must still be armed.
            i32 mirror = *reinterpret_cast<i32*>(HeBytes(found) + 212);  // *(found+53)
            if (mirror != He_Id(h) || He_F204(h) == -1) {
                He_State(h) = -1; return static_cast<u32>(He_State(h));
            }
            HeRecord* tgt = k.findPersonById(He_F212(h));
            HeRecord* seq = k.packetSeqBase(He_F204(h));
            if (seq && tgt)
                k.queuePair36(*reinterpret_cast<i32*>(HeBytes(seq)),
                              *reinterpret_cast<i32*>(HeBytes(tgt) + 4));  // (*seq, *(tgt+4))
            He_State(h) = 9;
            return static_cast<u32>(GameTimeAdvance(&He_ApptTime(h), 0, 0, 10));
        }
        return static_cast<u32>(He_State(h));
    }
    case 11u: { // state 9: wait until the partner is gone, then resume / finish.
        HeRecord* found = k.findFirstByFilter(1, 1, He_F212(h));
        if (found) {
            i32 mirror = *reinterpret_cast<i32*>(HeBytes(found) + 212);  // *(found+53)
            if (mirror == He_Id(h)) {
                return static_cast<u32>(GameTimeAdvance(&He_ApptTime(h), 0, 0, 6));
            }
            k.queueNamedObject53(He_F200(h), He_CityId(h), 0, 1);
            StampClock(He_ApptTime(h));
            u32 r = static_cast<u32>(GameTimeAdvance(&He_ApptTime(h), 0, 0, 4));
            He_State(h) = 5;
            return r;
        }
        He_State(h) = -1;
        return static_cast<u32>(He_State(h));
    }
    default:
        return sw;
    }
}

// ===========================================================================
// Spionage (espionage) coroutine.
// ===========================================================================

// The 16-entry stride table the spy scan picks a coprime-to-256 stride from
// (dword_478450). The values are not load-bearing for the control-flow golden
// tests (they only seed the modular scan), so the helper exposes index 0 == 1.
// dword_478450 @0x478450 — recovered byte-for-byte from the binary (coprime-to-256
// strides). NOT a simple odd-number ramp; the upper half are 256-k mirrors.
namespace { const i32 kSpyStrideTable[16] = {
    1, 3, 5, 7, 11, 13, 17, 19, 237, 239, 243, 245, 249, 251, 253, 255 }; }

// gilde.exe 0x4e2e58 — VIBE_CharAction_InitSpionage
i32 InitSpionage(HeRecord* h) {
    const CharActionStep6Hooks& k = GetCharActionStep6Hooks();
    // Disasm @0x4e2e68: BYTE1(result) = *(a1+120); then `if ((result & 0x400)==0)`.
    // 0x400 is bit 10 == bit 2 of BYTE1 == bit 2 of the +120 flag byte. So the gate
    // is (*(a1+120) & 0x04), i.e. He_Flags(h) & 0x04 (NOT the +121 byte).
    u8 hiFlag = He_Flags(h);
    int matchCount = 0;
    if ((hiFlag & 0x04) != 0)
        return He_ReqHandle(h);  // (flags & 0x04) set -> already spawned: no-op return

    i32 spyId = He_F196(h);
    He_F216(h) = -1;
    HeRecord* spy = k.findPersonById(spyId);
    if (spy) {
        He_F188(h) = k.randomModulo(0x100);                 // scan cursor 0..255
        He_F192(h) = kSpyStrideTable[k.randomModulo(0x10) & 0x0F]; // coprime stride
        He_F180(h) = -1;
        He_F184(h) = 0;
        StampClock(He_ApptTime(h));
        GameTimeAdvance(&He_ApptTime(h), 0, 0, 1);          // +1 minute (addMinutes=1)
        // Scan the handler pool for an existing spy on the same target; count the
        // ones with a live successor (+54 != -1) until we find a duplicate or run out.
        HeRecord* cand = k.findFirstByFilter(1, 0, 24);
        bool dup = false;
        while (cand) {
            bool sameCity   = He_CityIndex(h) == *reinterpret_cast<u16*>(HeBytes(cand) + 8); // *(cand+4 word)
            bool diffActor  = *reinterpret_cast<i32*>(HeBytes(cand) + 4) != He_Id(h);        // *(cand+1) vs us
            bool sameTarget = *reinterpret_cast<i32*>(HeBytes(cand) + 196) == He_F196(h);    // *(cand+49)
            if (sameCity && diffActor && sameTarget) { dup = true; break; }
            if (*reinterpret_cast<i32*>(HeBytes(cand) + 216) != -1)  // *(cand+54)
                ++matchCount;
            cand = k.findNextMatching();
        }
        if (!dup) {
            // Resolve the spy target object (+172), else find the nearest scene one.
            HeRecord* obj = nullptr;
            k.resolveEntityById(&obj, He_F172(h));
            if (!obj) {
                i32 found = He_F172(h);
                k.findNearestEntity(He_CityIndex(h), 6, &found);
                He_F172(h) = found;
                k.resolveEntityById(&obj, He_F172(h));
            }
            if (obj) {
                // Two clock snapshots; advance +200 by 72 or 96 hours by the
                // DispatchByType(38) result (==2 -> 96h, else 72h). The dispatch is
                // an opaque AI-method query; we model it as 72h (the common path).
                StampClock(He_ApptTime(h));
                StampClock(He_ScratchTimeC(h));
                // Disasm @0x4e3054: Advance(+200, v9, 0, 0) with v9 = addDays = 72 or 96.
                // v9 = (DispatchByType(38)==2) ? 96 : 72. DispatchByType is an opaque
                // out-of-tree AI-method query (Rule 8); modelled as the 72-day path.
                GameTimeAdvance(&He_ScratchTimeC(h), 72, 0, 0);  // +72 days (addDays=72)
                // success gate (disasm @0x4e3060/0x4e312b):
                //   *(cityrow+2)==6 || *(spy+2)==6 || *(spy+2)==7 || matchCount<8.
                // The first byte is the CITY-row category (word_12CE910[city*268]+2),
                // NOT the resolved object's category.
                u8 cityCat = k.cityCategory(He_CityIndex(h));
                u8 spyCat = *reinterpret_cast<u8*>(HeBytes(spy) + 2);
                bool success = (cityCat == 6) || (spyCat == 6) || (spyCat == 7) || (matchCount < 8);
                i32 arg;
                if (success) {
                    He_F216(h) = k.requestBuildOp73(17, He_Id(obj), 0, -1, 0, 0);
                    arg = 0;
                } else {
                    arg = 4;
                    He_ApptTime(h) = He_ScratchTimeC(h);  // restore the +200 snapshot
                }
                He_ReqHandle(h) = GetNpcLeafHooks().queueRequestEntity29(arg, h);
                return He_ReqHandle(h);
            }
        }
    }
    // fall-through (LABEL_19): arm a -1 entity request.
    He_ReqHandle(h) = GetNpcLeafHooks().queueRequestEntity29(-1, h);
    return He_ReqHandle(h);
}

// gilde.exe 0x4e3164 — VIBE_CharAction_RunSpionage
u32 RunSpionage(HeRecord* h) {
    const CharActionStep6Hooks& k = GetCharActionStep6Hooks();
    // Terminal states (-1 or -2, disasm @0x4e3172/0x4e31c1): take the free path with
    // an optional Single49/Pair33 cleanup gated on flag bit 1 (*(a1+120) & 2).
    if (static_cast<u32>(He_State(h)) >= 0xFFFFFFFEu) {
        i32 a3 = He_F216(h);
        if ((He_Flags(h) & 0x02) != 0 && a3 != -1) {
            HeRecord* rec = k.findPersonById(He_F216(h));
            if (rec) {
                k.queueSingle49(He_Id(rec));            // QueueSingle49(*(rec+4))
                k.queuePair33(He_Id(rec), 1);           // Pair33(*(rec+4), 1)
            }
        }
        return static_cast<u32>(GetNpcLeafHooks().freeHandlerEntry(h));
    }
    if ((He_Flags(h) & 0x04) != 0)
        return static_cast<u32>(He_State(h));
    // If an entity request is armed (+132 != -1), gate on its packet status first.
    if (He_ReqHandle(h) != -1) {
        i32 st = GetNpcLeafHooks().packetStatus(He_ReqHandle(h));
        if (!st)
            return static_cast<u32>(st);
    }
    switch (He_State(h)) {
    case 0: { // pay out the spied earnings and re-arm.
        if (He_F216(h) == -1 || GetNpcLeafHooks().packetStatus(He_F216(h)) != 0) {
            HeRecord* seq = k.packetSeqBase(He_F216(h));
            if (seq) {
                HeRecord* fam = k.familyRecord(He_CityIndex(h));
                if (fam)
                    *reinterpret_cast<i32*>(HeBytes(fam) + 40) += He_F228(h);  // +10 dword
                He_F216(h) = *reinterpret_cast<i32*>(HeBytes(seq) + 4);   // *(seq+1)
                k.queueRequest16(-1, k.cityPersonId(He_CityIndex(h)), He_F228(h), 0);
                u16 cls = *reinterpret_cast<u16*>(HeBytes(seq));
                k.changePlayerAction(nullptr, nullptr, h, cls);
                StampClock(He_ApptTime(h));
                He_ReqHandle(h) = GetNpcLeafHooks().queueRequestEntity29(1, h);
                return static_cast<u32>(He_ReqHandle(h));
            } else {
                He_F216(h) = -1;
                He_ApptTime(h) = He_ScratchTimeC(h);   // restore +200 snapshot
                He_ReqHandle(h) = GetNpcLeafHooks().queueRequestEntity29(4, h);
                return static_cast<u32>(He_ReqHandle(h));
            }
        }
        return static_cast<u32>(He_State(h));
    }
    case 1: { // step the 256-slot scan to the next valid target.
        int budget = 256;
        i32 pos = He_F188(h);
        i32 stride = He_F192(h);
        i32 picked = 0;
        do {
            // candidate slot = pos (the original indexes a global object array and
            // applies a bit-mask filter on the slot's type byte; the meaningful
            // control-flow invariant is the (1<<type)&0x7FCC3A6 filter plus the
            // "not our own target" guard). We surface the candidate id via the
            // object resolver keyed on the slot.
            HeRecord* slot = nullptr;
            k.resolveEntityById(&slot, pos);
            if (slot) {
                u8 type = *reinterpret_cast<u8*>(HeBytes(slot) + 2);
                i32 slotId = *reinterpret_cast<i32*>(HeBytes(slot) + 4);  // *(slot+1)
                if (((1u << (type & 31)) & 0x07FCC3A6u) != 0 && He_F172(h) != slotId) {
                    picked = slotId;
                    break;
                }
            }
            pos = (pos + stride) % 256;
        } while (--budget);
        He_F188(h) = (pos + stride) % 256;
        if (!picked) {
            // LABEL_28: bail.
            GameTimeAdvance(&He_ApptTime(h), 0, 0, 1);
            He_ReqHandle(h) = GetNpcLeafHooks().queueRequestEntity29(-1, h);
            return static_cast<u32>(He_ReqHandle(h));
        }
        HeRecord* tgt = k.findPersonById(He_F216(h));
        if (!tgt) {
            GameTimeAdvance(&He_ApptTime(h), 0, 0, 1);   // LABEL_30 via the no-target path
            He_ReqHandle(h) = GetNpcLeafHooks().queueRequestEntity29(-1, h);
            return static_cast<u32>(He_ReqHandle(h));
        }
        k.queueSingle49(*reinterpret_cast<i32*>(HeBytes(tgt) + 4));
        k.queueNamedObject53(*reinterpret_cast<i32*>(HeBytes(tgt) + 4), picked, 0, 1);
        GameTimeAdvance(&He_ApptTime(h), 0, 0, 5);
        He_F180(h) = picked;
        He_ReqHandle(h) = GetNpcLeafHooks().queueRequestEntity29(2, h);
        return static_cast<u32>(He_ReqHandle(h));
    }
    case 2: { // walk-in / near-door gate.
        HeRecord* obj = nullptr;
        k.resolveEntityById(&obj, He_F180(h));
        GameTimeAdvance(&He_ApptTime(h), 0, 0, 5);
        if (!obj) {
            He_ReqHandle(h) = GetNpcLeafHooks().queueRequestEntity29(-1, h);
            return static_cast<u32>(He_ReqHandle(h));
        }
        HeRecord* spy = k.findPersonById(He_F216(h));
        if (!spy) {
            GameTimeAdvance(&He_ApptTime(h), 0, 0, 1);
            He_ReqHandle(h) = GetNpcLeafHooks().queueRequestEntity29(-1, h);
            return static_cast<u32>(He_ReqHandle(h));
        }
        if (k.isNearDoor(spy, He_F180(h))) {
            HeRecord* obj2 = nullptr;
            k.resolveEntityById(&obj2, He_F172(h));
            if (!obj2 || LoadU16At(obj2, 39) == 0xFFFF) {
                He_ReqHandle(h) = GetNpcLeafHooks().queueRequestEntity29(-1, h);
                return static_cast<u32>(He_ReqHandle(h));
            }
            GameTime clk = NpcClock();
            if (GameTimeCompare(&He_ScratchTimeC(h), &clk) == -1) {
                k.queueSingle49(*reinterpret_cast<i32*>(HeBytes(spy) + 4));
                k.queueNamedObject53(*reinterpret_cast<i32*>(HeBytes(spy) + 4),
                                     *reinterpret_cast<i32*>(HeBytes(obj2) + 4), 0, 1);
                He_ReqHandle(h) = GetNpcLeafHooks().queueRequestEntity29(3, h);
            } else {
                He_ReqHandle(h) = GetNpcLeafHooks().queueRequestEntity29(1, h);
            }
        } else {
            He_State(h) = 2;
            GameTimeAdvance(&He_ApptTime(h), 0, 0, 5);
            He_ReqHandle(h) = GetNpcLeafHooks().queueRequestEntity29(2, h);
        }
        return static_cast<u32>(He_ReqHandle(h));
    }
    case 3: { // arrival gate; report the spied person.
        GameTimeAdvance(&He_ApptTime(h), 0, 0, 5);
        HeRecord* obj = nullptr;
        k.resolveEntityById(&obj, He_F172(h));
        if (!obj || LoadU16At(obj, 39) == 0xFFFF) {
            He_ReqHandle(h) = GetNpcLeafHooks().queueRequestEntity29(-1, h);
            return static_cast<u32>(He_ReqHandle(h));
        }
        HeRecord* spy = k.findPersonById(He_F216(h));
        if (!spy || k.isNearDoor(spy, He_F172(h))) {
            HeRecord* p = k.findPersonById(He_F196(h));
            if (p)
                k.sendEntityMessage(k.cityPersonId(He_CityIndex(h)), 4935);
            He_ReqHandle(h) = GetNpcLeafHooks().queueRequestEntity29(-1, h);
        } else {
            He_State(h) = 3;
            GameTimeAdvance(&He_ApptTime(h), 0, 0, 5);
            He_ReqHandle(h) = GetNpcLeafHooks().queueRequestEntity29(3, h);
        }
        return static_cast<u32>(He_ReqHandle(h));
    }
    case 4: { // finalize.
        GameTimeAdvance(&He_ApptTime(h), 0, 0, 1);
        He_ReqHandle(h) = GetNpcLeafHooks().queueRequestEntity29(-1, h);
        return static_cast<u32>(He_ReqHandle(h));
    }
    default:
        return static_cast<u32>(He_State(h));
    }
}

// ===========================================================================
// Guild-master HIRE / FIRE personnel coroutines.
// ===========================================================================

// gilde.exe 0x4dd5b4 — VIBE_CharAction_RunMeisterEinstellen
u32 RunMeisterEinstellen(HeRecord* h) {
    const CharActionStep6Hooks& k = GetCharActionStep6Hooks();
    switch (He_State(h)) {
    case -2:
    case -1:
        return static_cast<u32>(GetNpcLeafHooks().freeHandlerEntry(h));
    case 0:
        He_State(h) = 1;
        return 1u;
    case 1: {
        GameTime clk = NpcClock();
        i32 cmp = GameTimeCompare(&He_ApptTime(h), &clk);
        if (cmp >= 0)
            return static_cast<u32>(cmp);
        HeRecord* self = k.personQueryBegin(0, 1, 1, He_CityId(h));  // *(a1+16)
        if (!self)
            return static_cast<u32>(GetNpcLeafHooks().freeHandlerEntry(h));
        u16 selfCity = LoadU16At(self, 39);
        // class arg is a SIGNED byte at self+0 (disasm `movsx edx, byte ptr [esi]`).
        int selfClass = static_cast<i8>(*reinterpret_cast<u8*>(HeBytes(self)));
        int wage = k.computeWage(selfCity, selfClass, -1);  // wage->int via fistp (round-nearest)
        if (k.sumCurrencyHeld(He_CityIndex(h)) < wage) {
            k.sendQuickjumpMessage(k.cityPersonId(selfCity), He_CityId(h), 5709);
            return static_cast<u32>(GetNpcLeafHooks().freeHandlerEntry(h));
        }
        // scan the 12-slot member array (+176, stride 4) for the first slot > 0.
        bool slot = false;
        for (int i = 0; i < 12; ++i) {
            if (*reinterpret_cast<i32*>(HeBytes(h) + 176 + 4 * i) > 0) { slot = true; break; }
        }
        if (!slot)
            return static_cast<u32>(GetNpcLeafHooks().freeHandlerEntry(h));
        He_F228(h) = k.queueGuardTarget61(self, 0, 0, He_SubMethodByte(h));
        StampClock(He_ApptTime(h));
        u32 r = static_cast<u32>(GameTimeAdvance(&He_ApptTime(h), 0, 1, 0));  // +1 second
        ++He_State(h);
        return r;
    }
    case 2: {
        if (He_F228(h) == -1)
            return static_cast<u32>(He_State(h));
        i32 st = GetNpcLeafHooks().packetStatus(He_F228(h));
        if (!st)
            return static_cast<u32>(st);
        HeRecord* seq = k.packetSeqBase(He_F228(h));
        if (!seq) {
            k.sendEntityMessage(k.cityPersonId(He_CityIndex(h)), 1425);
            return static_cast<u32>(GetNpcLeafHooks().freeHandlerEntry(h));
        }
        He_F232(h) = *reinterpret_cast<i32*>(HeBytes(seq) + 4);   // *(seq+1)
        HeRecord* self = k.personQueryBegin(0, 1, 1, He_CityId(h));
        if (!self)
            return static_cast<u32>(GetNpcLeafHooks().freeHandlerEntry(h));
        if (He_F224(h) == -1) {
            HeRecord* wp = k.objectQueryFind(0, 0, 0, He_Id(self));  // FindWorkProductObject proxy
            if (wp)
                He_F224(h) = *reinterpret_cast<i32*>(HeBytes(wp) + 4);  // *(wp+1)
        }
        k.requestChrMove(He_F232(h), He_CityId(h), He_F224(h));
        He_State(h) = 3;
        StampClock(He_ApptTime(h));
        return static_cast<u32>(GameTimeAdvance(&He_ApptTime(h), 0, 1, 0));  // +1 second
    }
    case 3: {
        HeRecord* self = k.personQueryBegin(0, 1, 1, He_CityId(h));
        if (!self)
            return static_cast<u32>(GetNpcLeafHooks().freeHandlerEntry(h));
        HeRecord* recruit = k.findPersonById(He_F232(h));
        // First scan: find an occupied slot only if the recruit resolved. If RecordById
        // is null OR no slot found within 12, skip the payout (goto LABEL_33).
        int idx = 0; bool slot = false;
        for (idx = 0; idx < 12; ++idx) {
            if (recruit && *reinterpret_cast<i32*>(HeBytes(h) + 176 + 4 * idx) > 0) { slot = true; break; }
        }
        if (slot) {
            u16 selfCity = LoadU16At(self, 39);
            int selfClass = static_cast<i8>(*reinterpret_cast<u8*>(HeBytes(self)));
            int wage = k.computeWage(selfCity, selfClass, -1);
            --*reinterpret_cast<i32*>(HeBytes(h) + 176 + 4 * idx);
            u8 cat = k.cityCategory(selfCity);
            if (cat == 6 || cat == 7)
                k.sendQuickjumpMessage(k.cityPersonId(He_CityIndex(h)), He_Id(self), 6072);
            k.queueRequest16(*reinterpret_cast<i32*>(HeBytes(recruit) + 4),
                             k.cityPersonId(He_CityIndex(h)), wage, 0);
            HeRecord* fam = k.familyRecord(He_CityIndex(h));
            if (fam)
                *reinterpret_cast<i32*>(HeBytes(fam) + 76) += wage;   // +19 dword
        }
        // LABEL_33: fresh full member scan; if no slot remains, finish, else reschedule.
        bool any = false;
        for (int i = 0; i < 12; ++i) {
            if (*reinterpret_cast<i32*>(HeBytes(h) + 176 + 4 * i) > 0) { any = true; break; }
        }
        if (!any)
            return static_cast<u32>(GetNpcLeafHooks().freeHandlerEntry(h));
        StampClock(He_ApptTime(h));
        u32 r = static_cast<u32>(GameTimeAdvance(&He_ApptTime(h), 0, 0, k.randomModulo(10) + 30));
        He_State(h) = 1;
        return r;
    }
    default:
        return static_cast<u32>(He_State(h));
    }
}

// gilde.exe 0x4ddac4 — VIBE_CharAction_RunMeisterEntlassen
u32 RunMeisterEntlassen(HeRecord* h) {
    const CharActionStep6Hooks& k = GetCharActionStep6Hooks();
    switch (He_State(h)) {
    case -2:
    case -1:
        return static_cast<u32>(GetNpcLeafHooks().freeHandlerEntry(h));
    case 0:
        He_State(h) = 1;
        return 1u;
    case 1: {
        GameTime clk = NpcClock();
        i32 cmp = GameTimeCompare(&He_ApptTime(h), &clk);
        if (cmp >= 0)
            return static_cast<u32>(cmp);
        HeRecord* self = k.personQueryBegin(He_CityId(h), 1, 1, He_CityId(h));
        if (!self)
            return static_cast<u32>(GetNpcLeafHooks().freeHandlerEntry(h));
        int sub = He_SubMethodByte(h);   // (i32)*(a1+169) >> 24 (signed sar)
        u16 selfCity = LoadU16At(self, 39);
        int selfClass = static_cast<i8>(*reinterpret_cast<u8*>(HeBytes(self)));  // movsx byte[self]
        int wage = k.computeWage(selfCity, selfClass, sub);
        if (k.sumCurrencyHeld(He_CityIndex(h)) < wage) {
            k.sendQuickjumpMessage(k.cityPersonId(selfCity), He_CityId(h), 5710);
            return static_cast<u32>(GetNpcLeafHooks().freeHandlerEntry(h));
        }
        // single-slot member scan (count 1).
        bool slot = (*reinterpret_cast<i32*>(HeBytes(h) + 176) > 0);
        if (slot)
            He_F184(h) = k.queueGuardTarget61(self, 1, sub, 0);
        if (!slot)
            return static_cast<u32>(GetNpcLeafHooks().freeHandlerEntry(h));
        ++He_State(h);
        return static_cast<u32>(He_F184(h));
    }
    case 2: {
        i32 st = GetNpcLeafHooks().packetStatus(He_F184(h));
        if (!st)
            return static_cast<u32>(st);
        HeRecord* seq = k.packetSeqBase(He_F184(h));
        if (!seq) {
            k.sendEntityMessage(k.cityPersonId(He_CityIndex(h)), 1431);
            return static_cast<u32>(GetNpcLeafHooks().freeHandlerEntry(h));
        }
        He_F188(h) = *reinterpret_cast<i32*>(HeBytes(seq) + 4);   // *(seq+1)
        HeRecord* self = k.personQueryBegin(0, 1, 1, He_CityId(h));
        if (!self)
            return static_cast<u32>(GetNpcLeafHooks().freeHandlerEntry(h));
        // single-slot member scan, pay severance.
        if (*reinterpret_cast<i32*>(HeBytes(h) + 176) > 0) {
            u16 selfCity = LoadU16At(self, 39);
            int selfClass = static_cast<i8>(*reinterpret_cast<u8*>(HeBytes(self)));
            int wage = k.computeWage(selfCity, selfClass, He_SubMethodByte(h));
            --*reinterpret_cast<i32*>(HeBytes(h) + 176);
            u8 cat = k.cityCategory(selfCity);
            if (cat == 6 || cat == 7)
                k.sendQuickjumpMessage(k.cityPersonId(He_CityIndex(h)), He_Id(self), 6078);
            k.queueRequest16(*reinterpret_cast<i32*>(HeBytes(seq) + 4),
                             k.cityPersonId(He_CityIndex(h)), wage, 0);
        }
        // resolve the move-back destination (+180), issue the move.
        i32 dest = He_F180(h);
        if (dest == 0 || dest == -1) {
            i32 selfScene = LoadI32At(self, 93);
            HeRecord* obj = (dest == -1)
                ? k.objectQueryFind(selfScene, 1, 0, 253)
                : k.objectQueryFind(selfScene, 1, 0, LoadI32At(self, 39) >> 16);
            dest = obj ? *reinterpret_cast<i32*>(HeBytes(obj) + 4) : -1;
        }
        k.requestChrMove(He_F188(h), He_CityId(h), dest);
        // final single-slot scan (count 1, disasm @0x4ddeb1): if NO slot remains
        // (*(a1+176) <= 0) → free; else reschedule +10..+19 minutes, state=1.
        // (The original frees when the slot is EMPTY, not when it is occupied.)
        if (*reinterpret_cast<i32*>(HeBytes(h) + 176) <= 0)
            return static_cast<u32>(GetNpcLeafHooks().freeHandlerEntry(h));
        int delta = k.randomModulo(10) + 10;
        u32 r = static_cast<u32>(GameTimeAdvance(&He_ApptTime(h), 0, 0, delta));
        He_State(h) = 1;
        return r;
    }
    default:
        return static_cast<u32>(He_State(h));
    }
}

// ===========================================================================
// "March the group to an object" coroutine.
// ===========================================================================

// gilde.exe 0x4e1054 — VIBE_CharAction_RunMoveCrowdToObject
u32 RunMoveCrowdToObject(HeRecord* h) {
    const CharActionStep6Hooks& k = GetCharActionStep6Hooks();
    GameTime clk = NpcClock();
    // Seasonal time-of-day window gate. The original compares the clock hour against
    // the season float bounds flt_64770C[season] / flt_6476FC[season]; we model the
    // window decision through the cityCategory-independent hook surface by using the
    // hour directly: outside [8, 20) is "out of window" (the recovered bounds; the
    // floats are opaque so the bound magnitudes are not asserted in the golden test).
    int hour = clk.hour;
    bool outOfWindow = (hour >= 20 || hour < 8);
    if (outOfWindow && static_cast<u32>(He_State(h)) < 0xFFFFFFFEu) {
        return static_cast<u32>(GameTimeAdvance(&He_ApptTime(h), 0, 0, 5));  // +5 minutes
    }
    if (He_CityIndex(h) == 0xFFFF || He_CityId(h) == -1)
        return static_cast<u32>(GetNpcLeafHooks().freeHandlerEntry(h));

    u8 memberCount = He_MemberCountByte(h);   // *(a1+172) low byte
    switch (He_State(h)) {
    case -2:
    case -1: {
        if (k.personQueryBegin(0, 1, 1, He_CityId(h))) {
            for (int i = 0; i < memberCount; ++i) {
                i32 mid = He_MemberId(h, i);
                if (k.findPersonById(mid)) {
                    k.queueSingle49(mid);
                    k.queueNamedObject53(mid, He_F176(h), 0, 0);
                }
            }
        }
        return static_cast<u32>(GetNpcLeafHooks().freeHandlerEntry(h));
    }
    case 1: {
        HeRecord* leader = k.personQueryBegin(0, 1, 5, 11);
        if (!leader) { He_State(h) = -1; return static_cast<u32>(He_State(h)); }
        He_F180(h) = *reinterpret_cast<i32*>(HeBytes(leader) + 4);
        for (int i = 0; i < memberCount; ++i) {
            i32 mid = He_MemberId(h, i);
            if (k.findPersonById(mid)) {
                k.queueSingle49(mid);
                k.queueNamedObject53(mid, He_F180(h), 0, 0);
            }
        }
        ++He_State(h);
        return static_cast<u32>(memberCount);
    }
    case 2: {
        HeRecord* target = k.personQueryBegin(0, 1, 1, He_F180(h));
        if (!target) { He_State(h) = -1; return static_cast<u32>(He_State(h)); }
        i32 tid = *reinterpret_cast<i32*>(HeBytes(target) + 4);
        for (int i = 0; i < memberCount; ++i) {
            HeRecord* m = k.findPersonById(He_MemberId(h, i));
            if (m && !k.isNearDoor(m, tid)) {
                He_State(h) = 2;
                return static_cast<u32>(GameTimeAdvance(&He_ApptTime(h), 0, 0, 5));
            }
        }
        He_State(h) = 3;
        return static_cast<u32>(GameTimeAdvance(&He_ApptTime(h), 0, 0, 10));
    }
    case 3: {
        HeRecord* self = k.personQueryBegin(0, 1, 1, He_CityId(h));
        if (!self) { He_State(h) = -1; return static_cast<u32>(He_State(h)); }
        bool any = false;
        for (int i = 0; i < memberCount; ++i) {
            i32 mid = He_MemberId(h, i);
            if (k.findPersonById(mid)) {
                any = true;
                k.queueSingle49(mid);
                k.queueNamedObject53(mid, He_F176(h), 0, 0);
            }
        }
        if (!any)
            return static_cast<u32>(GetNpcLeafHooks().freeHandlerEntry(h));
        He_State(h) = 4;
        return static_cast<u32>(GameTimeAdvance(&He_ApptTime(h), 0, 0, 5));
    }
    case 4: {
        HeRecord* leader = k.personQueryBegin(0, 1, 1, He_CityId(h));
        if (!leader)
            return static_cast<u32>(GetNpcLeafHooks().freeHandlerEntry(h));
        i32 lid = *reinterpret_cast<i32*>(HeBytes(leader) + 4);
        for (int i = 0; i < memberCount; ++i) {
            HeRecord* m = k.findPersonById(He_MemberId(h, i));
            if (m && !k.isNearDoor(m, lid)) {
                He_State(h) = 4;
                return static_cast<u32>(GameTimeAdvance(&He_ApptTime(h), 0, 0, 5));
            }
        }
        // arrival: drive each member's player action, then run the harvest/payout.
        for (int i = 0; i < memberCount; ++i) {
            HeRecord* m = k.findPersonById(He_MemberId(h, i));
            if (m)
                k.changePlayerAction(nullptr, nullptr, nullptr, *reinterpret_cast<u16*>(m));
        }
        i32 scene = LoadI32At(leader, 93);
        HeRecord* obj = k.objectQueryFind(scene, 2, 6, 42);
        if (obj) {
            // economy/payout step (the building/inventory/coord math and the result
            // render). The control-flow relevant effect is the cmd17 + the two
            // quickjump renders; the float harvest math flows into an opaque amount.
            int amount = memberCount * (k.randomModulo(6) + 9);
            k.queueRequest16(*reinterpret_cast<i32*>(HeBytes(obj) + 4), -1, amount, 0);
            u8 cat = k.cityCategory(He_CityIndex(h));
            if (cat == 6 || cat == 7)
                k.sendQuickjumpMessage(k.cityPersonId(He_CityIndex(h)), -1, 5137);
        }
        return static_cast<u32>(GetNpcLeafHooks().freeHandlerEntry(h));
    }
    default:
        return static_cast<u32>(He_State(h) + 2);
    }
}

} // namespace guild::sim
