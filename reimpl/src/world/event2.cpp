// event2 — self-contained world-event "He"-action bodies (gilde.exe VIBE_Event_*).
// See event2.h for the function inventory and provenance.
#include "world/event2.h"

#include "sim/gametime.h"     // guild::sim::GameTimeAdvance
#include "sim/npcaction.h"    // guild::sim::NpcClock (the qword_13CE852 game clock)
#include "util/math_random.h" // guild::util::RandomModulo

#include <cstdint>
#include <cstring>

namespace guild::world {

using guild::sim::GameTimeAdvance;
using guild::sim::He_ApptTime;
using guild::sim::He_SavedTime;
using guild::sim::He_State;
using guild::sim::He_Counter;
using guild::sim::HeBytes;
using guild::util::RandomModulo;

// ---------------------------------------------------------------------------
// Module-global game clock image (qword_13CE852 + unk_13CE85A + unk_13CE85E).
// Shared with the sim cluster — reuse sim::NpcClock() rather than redefining,
// so there is one authoritative clock (ODR).
// ---------------------------------------------------------------------------
static const GameTime& Clock() { return guild::sim::NpcClock(); }
static void StampClock(GameTime& dst) { dst = Clock(); }

// ---------------------------------------------------------------------------
// Raw record accessors at the exact original byte offsets used here but not
// already provided by sim/he.h.
// ---------------------------------------------------------------------------
namespace {
inline i32&  AnimParam(HeRecord* h) { return *reinterpret_cast<i32*>(HeBytes(h) + 88); }
inline u16&  AnimWord(HeRecord* h)  { return *reinterpret_cast<u16*>(HeBytes(h) + 86); }
inline GameTime& ScratchTime(HeRecord* h) {
    return *reinterpret_cast<GameTime*>(HeBytes(h) + 96);
}
inline i32&  Dword(HeRecord* h, int off) { return *reinterpret_cast<i32*>(HeBytes(h) + off); }
inline i32&  CollectedCount(HeRecord* h) { return *reinterpret_cast<i32*>(HeBytes(h) + 260); }
}  // namespace

// ---------------------------------------------------------------------------
// Hooks / gates.
// ---------------------------------------------------------------------------
static i32 InertFree(HeRecord* h) {
    return static_cast<i32>(reinterpret_cast<std::intptr_t>(h));
}
static void InertSend(i32, i32, i32, const void*, i32, i32) {}
static const EventHooks kInertHooks = { &InertFree, &InertSend };
static const EventHooks* g_hooks = &kInertHooks;

void SetEventHooks(const EventHooks* hooks) { g_hooks = hooks ? hooks : &kInertHooks; }
const EventHooks& GetEventHooks() { return *g_hooks; }

static const HelpStep* g_helpSteps = nullptr;
static int             g_helpStepCount = 0;
static const i32*      g_adviceIds = nullptr;
static int             g_adviceCount = 0;
static bool            g_helpEnabled = false;
static bool            g_adviceEnabled = false;

void SetHelpScheduleTable(const HelpStep* steps, int count) {
    g_helpSteps = steps;
    g_helpStepCount = count;
}
void SetAdviceIdTable(const i32* ids, int count) { g_adviceIds = ids; g_adviceCount = count; }
void SetHelpEnabled(bool on)   { g_helpEnabled = on; }
void SetAdviceEnabled(bool on) { g_adviceEnabled = on; }
bool GetHelpEnabled()   { return g_helpEnabled; }
bool GetAdviceEnabled() { return g_adviceEnabled; }

// Read a help schedule step, clamping to an inert empty step out of range. The
// original indexes raw engine arrays; the table-bounds are modelled here.
static HelpStep ReadHelpStep(int i) {
    if (g_helpSteps && i >= 0 && i < g_helpStepCount) return g_helpSteps[i];
    return HelpStep{0, 0, 0, 0};
}
static i32 ReadAdviceId(int i) {
    if (g_adviceIds && i >= 0 && i < g_adviceCount) return g_adviceIds[i];
    return 0;
}

// ===========================================================================
// Action initializers
// ===========================================================================

// gilde.exe 0x4f06bc — VIBE_Event_ResetActionState.
//   v1 = a1 + 82;  *(_QWORD*)v1 = clock; ...;  return GameTime_Advance(v1, 0, 1, 0).
// The qword+dword+word store is the full 14-byte clock image copied into +82.
// Advance args map (rec, edx, ecx, ebx): edx=hours, ecx=seconds, ebx=minutes; so
// (0, 1, 0) adds 1 second.
i32 ResetActionState(HeRecord* h) {
    StampClock(He_ApptTime(h));                       // +82 <- clock (14 bytes)
    return GameTimeAdvance(&He_ApptTime(h), 0, 1, 0);  // +1 second
}

// gilde.exe 0x4f168c — VIBE_Event_ResetActionStateZero.  Advance(rec, 0, 0, 20).
// (0, 0, 20) -> ebx=20 = +20 minutes.
i32 ResetActionStateZero(HeRecord* h) {
    StampClock(He_ApptTime(h));                        // +82 <- clock
    return GameTimeAdvance(&He_ApptTime(h), 0, 0, 20); // +20 minutes
}

// gilde.exe 0x4f1d14 — VIBE_Event_SetActionAnim7.
HeRecord* SetActionAnim7(HeRecord* h) {
    StampClock(He_ApptTime(h));   // +82 <- clock
    AnimWord(h) = 7;              // +86 := 7
    AnimParam(h) = 0;             // +88 := 0
    return h;                     // original returns eax == h
}

// gilde.exe 0x4f3ae0 — VIBE_Event_AllocActionAnim4.
//   +82 <- +68 (saved time);  +96 <- +82 (scratch copy);
//   rng = RandomModulo(60);   return GameTime_Advance(+82, 4, 0, rng).
i32 AllocActionAnim4(HeRecord* h) {
    He_ApptTime(h) = He_SavedTime(h);                       // +82 <- +68
    ScratchTime(h) = He_ApptTime(h);                        // +96 <- +82
    u16 rng = static_cast<u16>(RandomModulo(60));
    return GameTimeAdvance(&He_ApptTime(h), 4, 0, rng);     // +4h, +rng minutes
}

// gilde.exe 0x4f3d28 — VIBE_Event_AllocActionAnim7.
//   rng = RandomModulo(30);  return GameTime_Advance(+82, 7, <ecx>, rng + 30).
// The original passes an uninitialised ecx (the seconds arg). The seconds slot
// is immediately recomputed by the minute->hour carry, so any value gives the
// same observable result for the (rng+30)-minute add; we pass 0 deterministically.
i32 AllocActionAnim7(HeRecord* h) {
    He_ApptTime(h) = He_SavedTime(h);                       // +82 <- +68
    ScratchTime(h) = He_ApptTime(h);                        // +96 <- +82
    u16 rng = static_cast<u16>(RandomModulo(30));
    return GameTimeAdvance(&He_ApptTime(h), 7, 0, rng + 30);
}

// gilde.exe 0x4f4f54 — VIBE_Event_AllocActionResetFields.
i32 AllocActionResetFields(HeRecord* h) {
    He_ApptTime(h) = He_SavedTime(h);                       // +82 <- +68
    ScratchTime(h) = He_ApptTime(h);                        // +96 <- +82
    i32 result = GameTimeAdvance(&He_ApptTime(h), 0, 0, 1); // +1 minute
    Dword(h, 188) = 0;
    Dword(h, 192) = 0;
    Dword(h, 196) = 0;
    return result;
}

// gilde.exe 0x4f6cd4 — VIBE_Event_AllocActionResetFlags.
i32 AllocActionResetFlags(HeRecord* h) {
    He_ApptTime(h) = He_SavedTime(h);                       // +82 <- +68
    ScratchTime(h) = He_ApptTime(h);                        // +96 <- +82
    i32 result = GameTimeAdvance(&He_ApptTime(h), 0, 0, 1);
    Dword(h, 172) = 0;
    Dword(h, 176) = 0;
    Dword(h, 180) = 0;
    return result;
}

// ===========================================================================
// Small record helpers
// ===========================================================================

// gilde.exe 0x4f52d0 — VIBE_Event_AppendCollectedHandle (handle@eax, h@edx).
//   v2 = *(int*)(h+260); *(int*)(h+260) = v2 + 1;
//   *(int*)(h + 4*v2 + 4) = handle;  return *(int*)(h+260) < 64;
bool AppendCollectedHandle(i32 handle, HeRecord* h) {
    i32 old = CollectedCount(h);
    CollectedCount(h) = old + 1;
    Dword(h, 4 + 4 * old) = handle;
    return CollectedCount(h) < 64;
}

// gilde.exe 0x4f24dc — VIBE_Event_RetZero.
i32 RetZero() { return 0; }

// gilde.exe 0x4f3adc / 0x4f3de0 — VIBE_Event_NullSub7 / _NullSub8.
void NullSub7() {}
void NullSub8() {}

// ===========================================================================
// Phase-machine step bodies
// ===========================================================================

// gilde.exe 0x4f1ac4 — VIBE_Event_HelpTextPlaybackRun.
i32 HelpTextPlaybackRun(HeRecord* h, bool helpEnabled) {
    i32 result = He_State(h) + 2;  // switch key = state + 2
    switch (result) {
        case 0:
        case 1:  // state -2 / -1 -> teardown
            return g_hooks->freeHandlerEntry(h);
        case 2: {  // state 0 -> playback body
            // cursor (He+172). The original reads the schedule arrays at 4*cursor.
            i32 cursor = static_cast<i32>(He_Counter(h));
            HelpStep cur = ReadHelpStep(cursor);
            i32 textId = cur.textId;
            // Send only when this step has a valid text id and help is enabled.
            if (textId && textId != -1 && helpEnabled) {
                g_hooks->sendEntityMessage(/*target*/ 0, -1, 64,
                                           /*text*/ nullptr, 1432, 0);
            }
            // ++cursor, then look at the next step's text id.
            cursor += 1;
            He_Counter(h) = static_cast<u16>(cursor);
            HelpStep next = ReadHelpStep(cursor);
            if (next.textId) {
                // Schedule the next appointment: clock then overwrite day/hour/minute
                // from the table.
                StampClock(He_ApptTime(h));                 // +82 <- clock (14 bytes)
                He_ApptTime(h).day = next.day;              // +82 := table day
                He_ApptTime(h).hour = next.hour;            // +86 := table hour
                He_ApptTime(h).minute = next.minute;        // +88 := table minute
                return next.minute;                         // original returns eax = minute
            }
            He_State(h) = 1;  // end of table -> teardown next tick
            return 1;
        }
        default:  // idle passthrough
            return result;
    }
}

i32 HelpTextPlaybackRun(HeRecord* h) { return HelpTextPlaybackRun(h, g_helpEnabled); }

// gilde.exe 0x4f1c2c — VIBE_Event_HelpAdviceLoopRun.
i32 HelpAdviceLoopRun(HeRecord* h, bool adviceEnabled) {
    i32 result = He_State(h) + 2;  // switch key = state + 2
    switch (result) {
        case 0:
        case 1:  // teardown
            return g_hooks->freeHandlerEntry(h);
        case 2: {  // advice body
            if (adviceEnabled) {
                i32 cursor = static_cast<i32>(He_Counter(h));
                (void)ReadAdviceId(cursor);  // dword_8CA888[dword_1229270[cursor]]
                g_hooks->sendEntityMessage(/*target*/ 0, -1, 0,
                                           /*text*/ nullptr, 1432, 0);
                i32 v5 = cursor + 1;
                He_Counter(h) = static_cast<u16>(v5);
                if (v5 > 26)
                    He_State(h) = 1;  // loop exhausted -> teardown next tick
                StampClock(He_ApptTime(h));                 // +82 <- clock
                u16 rng = static_cast<u16>(RandomModulo(4));
                return GameTimeAdvance(&He_ApptTime(h), rng + 8, 0, 0);  // +(rng+8)h
            }
            StampClock(He_ApptTime(h));                     // +82 <- clock
            return GameTimeAdvance(&He_ApptTime(h), 1, 0, 0);  // +1 day
        }
        default:
            return result;
    }
}

}  // namespace guild::world
