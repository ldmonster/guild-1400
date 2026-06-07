#include "sim/npcevent.h"

#include "sim/gametime.h"
#include "sim/npcaction.h"   // NpcClock(), GetNpcLeafHooks()
#include "util/math_random.h"

namespace guild::sim {

// All NpcEvent helpers copy the global clock (NpcClock()) into a He time slot as
// a 14-byte GameTime image. The original does this as a qword + dword + word
// store off the global; copying the GameTime struct is the byte-identical result.
static inline void StampClock(GameTime& dst) { dst = NpcClock(); }

// gilde.exe 0x4d577c — VIBE_NpcEvent_RestorePoseReset.
HeRecord* NpcEvent_RestorePoseReset(HeRecord* h) {
    He_ApptTime(h) = He_SavedTime(h);   // +82 <- +68 (14-byte copy)
    He_State(h) = 0;                    // +112
    He_WaitCounter(h) = 0;              // +180
    return h;
}

// gilde.exe 0x4d63bc — VIBE_NpcEvent_RestorePoseSetRandom.
int NpcEvent_RestorePoseSetRandom(HeRecord* h) {
    He_ApptTime(h) = He_SavedTime(h);                 // +82 <- +68
    int r = static_cast<u16>(util::RandomModulo(6));  // pose index 0..5
    He_WaitCounter(h) = static_cast<u16>(r);          // +180
    return r;
}

// gilde.exe 0x4dada0 — VIBE_NpcEvent_SetupAnim2Reset.
int NpcEvent_SetupAnim2Reset(HeRecord* h) {
    StampClock(He_ApptTime(h));                       // +82 <- clock
    int result = GameTimeAdvance(&He_ApptTime(h), 2, 0, 0);
    He_State(h) = 0;                                  // +112
    return result;
}

// gilde.exe 0x4d75e8 — VIBE_NpcEvent_SetupDuration10Reset.
HeRecord* NpcEvent_SetupDuration10Reset(HeRecord* h) {
    StampClock(He_ApptTime(h));        // +82 <- clock
    He_ApptTime(h).hour   = 10;        // +86 := 10
    He_ApptTime(h).minute = 0;         // +88
    He_ApptTime(h).second = 0;         // +92
    He_Counter(h) = 0;                 // +172
    He_Deadline(h).day = 0;            // +176
    He_WaitCounter(h) = 0;             // +180
    return h;
}

// gilde.exe 0x4d665c — VIBE_NpcEvent_InitRandomDurationEntity.
HeRecord* NpcEvent_InitRandomDurationEntity(HeRecord* h) {
    if ((He_Flags(h) & kHeAlreadySpawned) == 0) {
        const auto& hooks = GetNpcLeafHooks();
        i32 baseTextId = hooks.lawBaseTextId ? hooks.lawBaseTextId() : 51;
        He_WaitCounter(h) = static_cast<u16>(-1);     // +180 := -1
        He_Counter(h) = static_cast<u16>(baseTextId); // +172
        StampClock(He_ApptTime(h));                   // +82 <- clock
        i32 handle = hooks.queueRequestEntity29
                       ? hooks.queueRequestEntity29(0, h) : 0;
        He_ReqHandle(h) = handle;                     // +132
    }
    return h;
}

// gilde.exe 0x4d52b4 — VIBE_NpcEvent_QueueState9Entity.
HeRecord* NpcEvent_QueueState9Entity(HeRecord* h) {
    if ((He_Flags(h) & kHeAlreadySpawned) == 0) {
        StampClock(He_ApptTime(h));                   // +82 <- clock
        GameTimeAdvance(&He_ApptTime(h), 24, 0, 0);   // +24h
        He_ApptTime(h).hour   = 9;                    // +86 := 9
        He_ApptTime(h).minute = 0;                    // +88
        const auto& hooks = GetNpcLeafHooks();
        i32 handle = hooks.queueRequestEntity29
                       ? hooks.queueRequestEntity29(0, h) : 0;
        He_ReqHandle(h) = handle;                     // +132
    }
    return h;
}

// gilde.exe 0x4d493c — VIBE_NpcEvent_ResetAndQueueEntity.
HeRecord* NpcEvent_ResetAndQueueEntity(HeRecord* h) {
    if ((He_Flags(h) & kHeAlreadySpawned) == 0) {
        *reinterpret_cast<i32*>(HeBytes(h) + 212) = -1;  // +212 := -1
        *(HeBytes(h) + 220) = 0;                          // +220 := 0
        He_SavedTime(h) = NpcClock();      // +68 <- clock
        StampClock(He_ApptTime(h));        // +82 <- clock
        GameTimeAdvance(&He_ApptTime(h), 0, 0, 1);  // +1 minute
        const auto& hooks = GetNpcLeafHooks();
        i32 handle = hooks.queueRequestEntity29
                       ? hooks.queueRequestEntity29(0, h) : 0;
        He_ReqHandle(h) = handle;          // +132
    }
    return h;
}

// gilde.exe 0x4daf28 — VIBE_NpcEvent_SetupRandomDurationReset.
int NpcEvent_SetupRandomDurationReset(HeRecord* h) {
    He_State(h) = 0;                                          // +112
    StampClock(He_ApptTime(h));                               // +82 <- clock
    He_ApptTime(h).hour = static_cast<u16>(util::RandomModulo(3) + 9);   // +86
    int rm = util::RandomModulo(4);                           // 0..3
    He_Deadline(h).day = 0;                                   // +176
    He_ApptTime(h).minute = 15 * rm;                          // +88 := 15*rm
    return 15 * rm;
}

// gilde.exe 0x4d8a94 — VIBE_NpcEvent_SetupTargetTimestamp.
int NpcEvent_SetupTargetTimestamp(HeRecord* h) {
    StampClock(He_ApptTime(h));                   // +82 <- clock
    const auto& hooks = GetNpcLeafHooks();
    i32 count = 0;
    int found = hooks.findInventorySlot
                  ? hooks.findInventorySlot(376, &count) : 0;
    if (!found || count == 0)
        return hooks.freeHandlerEntry ? hooks.freeHandlerEntry(h)
                                      : reinterpret_cast<intptr_t>(h);
    He_Deadline(h) = NpcClock();                  // +176 <- clock
    return GameTimeAdvance(&He_Deadline(h), 0, 0, count);  // +count minutes
}

// gilde.exe 0x4da920 — VIBE_NpcEvent_InitTargetSlotsState12.
HeRecord* NpcEvent_InitTargetSlotsState12(HeRecord* h) {
    const auto& hooks = GetNpcLeafHooks();
    // Shuffle 6 dwords into the +176 region.
    i32* shuffleDst = reinterpret_cast<i32*>(HeBytes(h) + 176);
    if (hooks.shuffleDwords)
        hooks.shuffleDwords(6, shuffleDst);
    // For each of the 6 slots write (value+1) into the parallel +172 region.
    // Original: do { v3 = *(+176); result+=4; *(+172) = v3+1; } over 6 dwords,
    // i.e. dst172[i] = src176[i] + 1 for i in 0..5 (the +172 cursor trails +176
    // by 4 bytes; reading slot i and writing it back one position over).
    for (int i = 0; i < 6; ++i) {
        i32 v = *reinterpret_cast<i32*>(HeBytes(h) + 176 + 4 * i);
        *reinterpret_cast<i32*>(HeBytes(h) + 172 + 4 * (i + 1)) = v + 1;
    }
    StampClock(He_ApptTime(h));        // +82 <- clock
    He_ApptTime(h).hour   = 9;         // +86 := 9
    He_ApptTime(h).minute = 0;         // +88
    He_State(h) = 12;                  // +112 := 12
    return h;
}

} // namespace guild::sim
