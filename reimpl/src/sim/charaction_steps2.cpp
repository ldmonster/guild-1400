// charaction_steps2 — remaining self-contained CharAction step leaves.
// See charaction_steps2.h for the module overview and the recovered He-record
// field map. Each function carries its gilde.exe address; struct field accesses
// use the Cas2_* accessors (byte-faithful offsets into the He handler record).
#include "sim/charaction_steps2.h"

#include "sim/gametime.h"     // GameTimeAdvance
#include "sim/npcaction.h"    // NpcClock(), GetNpcLeafHooks()
#include "util/math_random.h" // RandomModulo

namespace guild::sim {

// ---------------------------------------------------------------------------
// Hook table plumbing (inert default — every leaf reports "absent"/no-op).
// ---------------------------------------------------------------------------
namespace {

HeRecord* InertFindFirst(int, const int*, const int*) { return nullptr; }
HeRecord* InertFindNext() { return nullptr; }
HeRecord* InertFindPerson(i32) { return nullptr; }
void      InertChangePlayerAction(HeRecord*, u16) {}
void      InertEnqueueCmd15(i32, i32) {}
void      InertRegisterApEvent(u16, i32) {}
i32       InertResolveCityId(u16 index) { return index; }

const CharActionStep2Hooks kInertHooks = {
    InertFindFirst, InertFindNext, InertFindPerson, InertChangePlayerAction,
    InertEnqueueCmd15, InertRegisterApEvent, InertResolveCityId,
};
const CharActionStep2Hooks* g_hooks = &kInertHooks;

// Copy the 14-byte global clock image into a GameTime slot. The original copies
// qword_13CE852 (+0..+7), unk_13CE85A (+8..+11) and unk_13CE85E (+12..+13) — the
// full 14-byte time image. NpcClock() holds that single module-global GameTime.
inline void StampClock(GameTime& dst) { dst = NpcClock(); }

} // namespace

void SetCharActionStep2Hooks(const CharActionStep2Hooks* hooks) {
    g_hooks = hooks ? hooks : &kInertHooks;
}
const CharActionStep2Hooks& GetCharActionStep2Hooks() { return *g_hooks; }

// ===========================================================================
// Timestamp / appointment reset leaves.
// ===========================================================================

// gilde.exe 0x4d0a98 — VIBE_CharAction_StateReset24
// gilde.exe 0x4d113c — VIBE_CharAction_StateReset24Alt  (byte-identical twin)
int StateReset24(HeRecord* h) {
    StampClock(Cas2_ApptTime(h));
    return GameTimeAdvance(&Cas2_ApptTime(h), 24, 0, 0);
}

// gilde.exe 0x4d0af0 — VIBE_CharAction_StateAdvancePos
int StateAdvancePos(HeRecord* h) {
    StampClock(Cas2_ApptTime(h));
    Cas2_RemIter(h) = Cas2_Counter(h) + 1;   // a1+176 := a1+172 + 1
    return GameTimeAdvance(&Cas2_ApptTime(h), 24, 0, 0);
}

// gilde.exe 0x4d17b4 — VIBE_CharAction_StateReset96
int StateReset96(HeRecord* h) {
    StampClock(Cas2_ApptTime(h));
    return GameTimeAdvance(&Cas2_ApptTime(h), 96, 0, 0);
}

// gilde.exe 0x4d1844 — VIBE_CharAction_StateReset0
// gilde.exe 0x4d1904 — VIBE_CharAction_StateReset0Alt   (byte-identical twin)
int StateReset0(HeRecord* h) {
    StampClock(Cas2_ApptTime(h));
    return GameTimeAdvance(&Cas2_ApptTime(h), 0, 0, 5);   // edx=0,ecx=0,ebx=5 minutes
}

// gilde.exe 0x4d38ec — VIBE_CharAction_StateReset0Alt2
int StateReset0Alt2(HeRecord* h) {
    StampClock(Cas2_ApptTime(h));
    return GameTimeAdvance(&Cas2_ApptTime(h), 0, 0, 2);   // +2 minutes
}

// gilde.exe 0x4d0f30 — VIBE_CharAction_StateCopyPos3
int StateCopyPos3(HeRecord* h) {
    Cas2_ApptTime(h) = Cas2_SavedTime(h);   // +68 -> +82 (14 bytes)
    return GameTimeAdvance(&Cas2_ApptTime(h), 3, 0, 0);
}

// gilde.exe 0x4dd574 — VIBE_CharAction_CopyGoalToTarget
// gilde.exe 0x4dda88 — VIBE_CharAction_CopyGoalToTargetDup (byte-identical twin)
int CopyGoalToTarget(HeRecord* h) {
    Cas2_ApptTime(h) = Cas2_SavedTime(h);
    return GameTimeAdvance(&Cas2_ApptTime(h), 0, 0, 15);  // +15 minutes
}

// gilde.exe 0x4e0a08 — VIBE_CharAction_CopyGoalToTargetState2
// gilde.exe 0x4e0d54 — VIBE_CharAction_CopyGoalToTargetState2Dup (byte-identical)
int CopyGoalToTargetState2(HeRecord* h) {
    Cas2_ApptTime(h) = Cas2_SavedTime(h);
    return GameTimeAdvance(&Cas2_ApptTime(h), 2, 0, 0);   // +2 days
}

// gilde.exe 0x4cf990 — VIBE_CharAction_ArrestReset
//   Stamps the clock into BOTH the saved pose (+68) and the appointment (+82).
int ArrestReset(HeRecord* h) {
    StampClock(Cas2_SavedTime(h));   // +68 = clock
    StampClock(Cas2_ApptTime(h));    // +82 = clock
    return GameTimeAdvance(&Cas2_ApptTime(h), 2, 0, 0);   // +2 days
}

// ===========================================================================
// Saved-pose restore + branch / finish leaves.
// ===========================================================================

// gilde.exe 0x4d0b38 — VIBE_CharAction_RestorePosAndBranch
int RestorePosAndBranch(HeRecord* h) {
    Cas2_RemIter(h) = -1;                  // *(a1+176) = -1
    Cas2_ApptTime(h) = Cas2_SavedTime(h);  // +68 -> +82 (the original emits the
    Cas2_ApptTime(h) = Cas2_SavedTime(h);  // copy block twice — preserved 1:1)
    int r = util::RandomModulo(4) + 2;     // RandomModulo(4) + 2 (range 2..5)
    u16 hour = Cas2_ApptHour(h);           // bx = *(_WORD*)(a1+86)
    Cas2_State(h) = 0;                     // *(a1+112) = 0
    Cas2_ApptHour(h) = static_cast<u16>(r + hour); // *(a1+86) = r + hour (16-bit wrap)
    return r;
}

// gilde.exe 0x4d1384 — VIBE_CharAction_RestorePosFinish
// gilde.exe 0x4d1674 — VIBE_CharAction_RestorePosFinishAlt (byte-identical twin)
int RestorePosFinish(HeRecord* h) {
    Cas2_ApptTime(h) = Cas2_SavedTime(h);  // +68 -> +82
    int r = util::RandomModulo(2);         // 0 or 1
    if (static_cast<u16>(r)) {             // if ((_WORD)result)
        GetNpcLeafHooks().freeHandlerEntry(h);
        return r;
    }
    return r;
}

// gilde.exe 0x4d19ac — VIBE_CharAction_ClearStateAndTimer
HeRecord* ClearStateAndTimer(HeRecord* h) {
    Cas2_State(h) = 0;     // *(result+112) = 0
    Cas2_Counter(h) = 0;   // *(result+172) = 0
    return h;
}

// gilde.exe 0x4dc070 — VIBE_CharAction_RetZero
int RetZero() { return 0; }

// ===========================================================================
// Terminal-state finalizers.
// ===========================================================================

// gilde.exe 0x4d0b20 — VIBE_CharAction_FinishIfTerminal
//   The original tests: if (state >= -2 && (state <= -2 || !state)) free; else
//   return the record. That predicate is true exactly for state == -2 and
//   state == 0 ... but note: the free path is VIBE_He_FreeHandlerEntry, and the
//   decompiled guard `v3 >= -2 && (v3 <= -2 || !v3)` reduces to (state == -2) ||
//   (state == 0 && state >= -2) i.e. state==-2 or state==0. We translate the
//   predicate verbatim.
HeRecord* FinishIfTerminal(HeRecord* h) {
    i32 state = Cas2_State(h);
    if (state >= -2 && (state <= -2 || !state)) {
        GetNpcLeafHooks().freeHandlerEntry(h);
        return h;
    }
    return h;
}

// gilde.exe 0x4d17e0 — VIBE_CharAction_FinalizeEntityStep
i32 FinalizeEntityStep(HeRecord* h) {
    i32 state = Cas2_State(h);
    if (state < -1) {
        if (state != -2)
            return state;
        return GetNpcLeafHooks().freeHandlerEntry(h);
    }
    if (state <= -1)   // state == -1
        return GetNpcLeafHooks().freeHandlerEntry(h);
    if (state == 0 && (Cas2_Flags(h) & 2) != 0) {
        StampClock(Cas2_ApptTime(h));
        GameTimeAdvance(&Cas2_ApptTime(h), 0, 0, 2);   // +2 minutes
        state = GetNpcLeafHooks().queueRequestEntity29(-1, h);
        Cas2_Packet(h) = state;
    }
    return state;
}

// gilde.exe 0x4db514 — VIBE_CharAction_RequestEntityFinish
//   NB: when the (flags & 4) gate is set the original returns eax == the record
//   pointer (a truthy value the callers do not consume); we return 0 there since
//   the meaningful result is the cmd29 packet handle stored into +132.
i32 RequestEntityFinish(HeRecord* h) {
    if ((Cas2_Flags(h) & 4) == 0) {
        StampClock(Cas2_ApptTime(h));
        GameTimeAdvance(&Cas2_ApptTime(h), 24, 0, 0);  // +24 days
        i32 handle = GetNpcLeafHooks().queueRequestEntity29(0, h);
        Cas2_Packet(h) = handle;
        return handle;
    }
    return 0;
}

// gilde.exe 0x4db558 — VIBE_CharAction_RequestEntityIfValid
i32 RequestEntityIfValid(HeRecord* h) {
    i32 state = Cas2_State(h);
    if (state == -1 || state == -2)
        return GetNpcLeafHooks().freeHandlerEntry(h);
    if ((Cas2_Flags(h) & 4) == 0) {
        StampClock(Cas2_ApptTime(h));   // no GameTimeAdvance in this variant
        i32 handle = GetNpcLeafHooks().queueRequestEntity29(-1, h);
        Cas2_Packet(h) = handle;
        return handle;
    }
    return 0;
}

// gilde.exe 0x4d3bdc — VIBE_CharAction_ExtortInit
i32 ExtortInit(HeRecord* h) {
    Cas2_Packet(h) = -1;   // *(result+132) = -1
    if ((Cas2_Flags(h) & 4) == 0) {
        Cas2_SuccId(h) = -1;   // *(result+184) = -1
        StampClock(Cas2_ApptTime(h));
        GameTimeAdvance(&Cas2_ApptTime(h), 0, 0, 2);   // +2 minutes
        Cas2_State(h) = 1;     // *(result+112) = 1
        i32 handle = GetNpcLeafHooks().queueRequestEntity29(1, h);
        Cas2_Packet(h) = handle;
        return handle;
    }
    // (flags & 4) set: the original returns eax == the record pointer (unused by
    // callers); we return 0 — the meaningful effect is +132 := -1, set above.
    return 0;
}

// ===========================================================================
// Per-frame "repeat" emitters.
// ===========================================================================

// Shared terminal-state dispatch for the repeat steps (state -1/-2 -> free, !=0
// non-terminal -> return state). Returns true if the caller should continue into
// the state-0 body; otherwise *out holds the value to return.
namespace {
bool RepeatTerminalGate(HeRecord* h, i32* out) {
    i32 state = Cas2_State(h);
    if (state < -1) {
        if (state != -2) { *out = state; return false; }
        *out = GetNpcLeafHooks().freeHandlerEntry(h);
        return false;
    }
    if (state <= -1) { *out = GetNpcLeafHooks().freeHandlerEntry(h); return false; }
    if (state != 0) { *out = state; return false; }
    return true;   // state == 0 -> run the emit body
}
} // namespace

// gilde.exe 0x4d1870 — VIBE_CharAction_RepeatCommandStep
i32 RepeatCommandStep(HeRecord* h) {
    i32 out;
    if (!RepeatTerminalGate(h, &out))
        return out;
    // state == 0: enqueue cmd15(-1, cityId(+8), value(+172), byte_6477A1).
    i32 cityId = GetCharActionStep2Hooks().resolveCityId(He_CityIndex(h));
    GetCharActionStep2Hooks().enqueueCmd15(cityId, Cas2_Counter(h));
    i32 rem = Cas2_RemIter(h) - 1;
    Cas2_RemIter(h) = rem;
    if (rem) {
        StampClock(Cas2_ApptTime(h));
        return GameTimeAdvance(&Cas2_ApptTime(h), 24, 0, 0);  // +24 days
    }
    return GetNpcLeafHooks().freeHandlerEntry(h);
}

// gilde.exe 0x4d1930 — VIBE_CharAction_RepeatTalkStep
i32 RepeatTalkStep(HeRecord* h) {
    i32 out;
    if (!RepeatTerminalGate(h, &out))
        return out;
    // state == 0: VIBE_MeisterAi_RegisterApEvent(index(+8), 0, -value(+172)).
    GetCharActionStep2Hooks().registerApEvent(He_CityIndex(h), -Cas2_Counter(h));
    i32 rem = Cas2_RemIter(h) - 1;
    Cas2_RemIter(h) = rem;
    if (rem) {
        StampClock(Cas2_ApptTime(h));
        return GameTimeAdvance(&Cas2_ApptTime(h), 24, 0, 0);  // +24 days
    }
    return GetNpcLeafHooks().freeHandlerEntry(h);
}

// ===========================================================================
// Group-action retargeters.
// ===========================================================================

// gilde.exe 0x4e0fa0 — VIBE_CharAction_ChangeGroupAction
i32 ChangeGroupAction(HeRecord* h) {
    int any = 0;
    int count = *reinterpret_cast<u8*>(HeBytes(h) + 172);  // byte@+172 member count
    for (int i = 0; i < count; ++i) {
        i32 personId = *reinterpret_cast<i32*>(HeBytes(h) + 140 + 4 * i);
        HeRecord* rec = GetCharActionStep2Hooks().findPersonById(personId);
        if (rec) {
            any |= 1;   // LOBYTE(v7) = v7 | 1
            // VIBE_Character_ChangePlayerAction(0,0,a1, *RecordById) — *rec is the
            // resolved person's marker word (record +0).
            GetCharActionStep2Hooks().changePlayerAction(h, *reinterpret_cast<u16*>(rec));
        }
    }
    if (!any)
        return GetNpcLeafHooks().freeHandlerEntry(h);
    StampClock(Cas2_ApptTime(h));
    Cas2_State(h) = 1;   // *(a1+112) = 1
    return GameTimeAdvance(&Cas2_ApptTime(h), 0, 0, 5);   // +5 minutes
}

// gilde.exe 0x4e1764 — VIBE_CharAction_ChangeGroupActionAndGoal
i32 ChangeGroupActionAndGoal(HeRecord* h) {
    int count = *reinterpret_cast<u8*>(HeBytes(h) + 172);
    for (int i = 0; i < count; ++i) {
        i32 personId = *reinterpret_cast<i32*>(HeBytes(h) + 140 + 4 * i);
        HeRecord* rec = GetCharActionStep2Hooks().findPersonById(personId);
        if (rec)
            GetCharActionStep2Hooks().changePlayerAction(h, *reinterpret_cast<u16*>(rec));
    }
    Cas2_ApptTime(h) = Cas2_SavedTime(h);   // +68 -> +82
    return GameTimeAdvance(&Cas2_ApptTime(h), 0, 0, 1);   // +1 minute
}

// ===========================================================================
// Handler-pool find-by-filter scans.
// ===========================================================================

// gilde.exe 0x4dc5dc — VIBE_CharAction_FindPairedEntityForward(a@eax, b@edx)
//   Filter: 1 pair (selector 0 == kind, value 46). For each match, if
//   match[+172(0xAC)] == b->id(+4) AND match[+176(0xB0)] == a->id(+4), return
//   a->id ^ match[+176] (== 0, "found"); else continue. No match -> 1.
i32 FindPairedEntityForward(HeRecord* a, HeRecord* b) {
    const int sel[1] = {0};
    const int val[1] = {46};
    HeRecord* m = GetCharActionStep2Hooks().findFirstByFilter(1, sel, val);
    if (!m)
        return 1;
    for (;;) {
        i32 bId = *reinterpret_cast<i32*>(HeBytes(b) + 4);   // [ecx+4]
        if (bId == *reinterpret_cast<i32*>(HeBytes(m) + 172)) {   // [eax+0ACh]
            i32 mField = *reinterpret_cast<i32*>(HeBytes(m) + 176); // [eax+0B0h]
            i32 aId = *reinterpret_cast<i32*>(HeBytes(a) + 4);      // [ebx+4]
            if (mField == aId)
                return aId ^ mField;   // xor eax, edi  (== 0 here)
        }
        m = GetCharActionStep2Hooks().findNextMatching();
        if (!m)
            return 1;
    }
}

// gilde.exe 0x4dc678 — VIBE_CharAction_FindActionByActor(marker@eax, rec@edx,
//                                                        outCount@ebx)
//   Filter: 2 pairs (selector 2 == index@+8, value *marker; selector 0 == kind,
//   value 53). For each match, if rec->id(+4) == match[+188(0xBC)] return 0
//   ("found / already held"). Walk, counting non-matches in edx. End -> if
//   outCount != null write the count, return 1.
i32 FindActionByActor(const u16* marker, HeRecord* rec, i32* outCount) {
    const int sel[2] = {2, 0};
    const int val[2] = {*marker, 53};
    HeRecord* m = GetCharActionStep2Hooks().findFirstByFilter(2, sel, val);
    i32 count = 0;   // edx (xor edx,edx then inc per FindNext)
    if (m) {
        i32 recId = *reinterpret_cast<i32*>(HeBytes(rec) + 4);   // [esi+4]
        while (recId != *reinterpret_cast<i32*>(HeBytes(m) + 188)) {  // [eax+0BCh]
            m = GetCharActionStep2Hooks().findNextMatching();
            ++count;   // inc edx
            if (!m)
                goto notFound;
        }
        return 0;   // match found
    }
notFound:
    if (outCount)
        *outCount = count;
    return 1;
}

} // namespace guild::sim
