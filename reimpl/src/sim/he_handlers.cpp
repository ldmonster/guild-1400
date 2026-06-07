// he_handlers — per-type He handler step functions. Faithful 1:1 port of the
// gilde.exe control flow; cross-cluster leaves (NpcAction dispatch, free-handler,
// packet status, cmd29 queue, subsystem ticks) are routed through HeHandlerHooks.
#include "sim/he_handlers.h"

#include "sim/gametime.h"
#include "sim/npcaction.h"   // NpcClock()

#include <cstdint>

namespace guild::sim {

// Stamp the global clock into a 14-byte GameTime slot (qword+dword+word in the
// original; copying the GameTime struct is the byte-identical result).
static inline void StampClock(GameTime& dst) { dst = NpcClock(); }

// ===========================================================================
// Hook plumbing.
// ===========================================================================
static const HeHandlerHooks kInertHooks{};
static const HeHandlerHooks* g_hooks = &kInertHooks;
void SetHeHandlerHooks(const HeHandlerHooks* hooks) {
    g_hooks = hooks ? hooks : &kInertHooks;
}
const HeHandlerHooks& GetHeHandlerHooks() { return *g_hooks; }

// Person profession column (byte_12CE912: Person record +2, stride 536).
static const u8* g_profColumn = nullptr;
static int       g_profSlots  = 0;
void SetHeProfessionColumn(const u8* base, int slotCount) {
    g_profColumn = base;
    g_profSlots  = slotCount;
}

// Inert free: return the record base as an int (the original returns eax; on the
// passthrough paths the record pointer is what survives in eax).
static i32 FreeOrPassthrough(HeRecord* h) {
    return g_hooks->freeHandlerEntry
               ? g_hooks->freeHandlerEntry(h)
               : static_cast<i32>(reinterpret_cast<intptr_t>(h));
}

// Pool geometry for the profession scan (Person array word_12CE910).
constexpr int kPersonStride2  = 536;
constexpr int kPersonScanBound2 = 411648;  // 536 * 768
constexpr u8  kProfession6      = 6;

// ===========================================================================
// gilde.exe 0x4cdd10 — VIBE_He_NpcActionHandler.
// ===========================================================================
i32 He_NpcActionHandler(HeRecord* h) {
    if (He_State(h) == -2)                  // *(+112) abort sentinel
        return FreeOrPassthrough(h);
    if (g_hooks->npcActionDispatch) {
        // The original dispatches on the embedded action sub-record at h+172.
        HeRecord* sub = reinterpret_cast<HeRecord*>(HeBytes(h) + 172);
        g_hooks->npcActionDispatch(sub);
    }
    return FreeOrPassthrough(h);
}

// ===========================================================================
// gilde.exe 0x4d0ac4 — VIBE_He_CounterWaitHandler.
// ===========================================================================
i32 He_CounterWaitHandler(HeRecord* h) {
    i32 state = He_State(h);                // *(+112)
    if (state >= -2) {
        if (state <= -2)                    // state == -2
            return FreeOrPassthrough(h);
        if (state)                          // state > 0: still waiting
            return static_cast<i32>(reinterpret_cast<intptr_t>(h));
        // state == 0: decrement the +172 byte countdown; free when it hits 0.
        u8 count = *reinterpret_cast<u8*>(HeBytes(h) + 172);
        if (!count)
            return FreeOrPassthrough(h);
        *reinterpret_cast<u8*>(HeBytes(h) + 172) = static_cast<u8>(count - 1);
    }
    return static_cast<i32>(reinterpret_cast<intptr_t>(h));
}

// ===========================================================================
// gilde.exe 0x4d0a10 — VIBE_He_Entity29RequestHandler.
// ===========================================================================
i32 He_Entity29RequestHandler(HeRecord* h) {
    i32 handle = He_ReqHandle(h);           // *(+132)
    // Gate: if a packet is pending (handle != -1) and not yet acked, passthrough
    // with the (still-zero) status.
    i32 result;
    if (handle != -1) {
        result = g_hooks->packetStatus ? g_hooks->packetStatus(handle) : 0;
        if (result == 0)
            return result;                  // still pending
    }
    // Packet acked (or none pending): clear the handle and branch on state.
    result = He_State(h);                   // *(+112)
    He_ReqHandle(h) = -1;                    // *(+132) := -1
    if (result < -1) {
        if (result != -2)
            return result;
        return FreeOrPassthrough(h);
    }
    if (result <= -1)
        return FreeOrPassthrough(h);
    if (result == 0 && (He_Flags(h) & kHeNeedsCmd29) != 0) {
        StampClock(He_ApptTime(h));                 // +82 <- clock
        GameTimeAdvance(&He_ApptTime(h), 0, 0, 2);  // +2 minutes
        result = g_hooks->queueRequestEntity29
                     ? g_hooks->queueRequestEntity29(-1, h) : 0;
        He_ReqHandle(h) = result;                   // +132
    }
    return result;
}

// ===========================================================================
// gilde.exe 0x4c57e0 — VIBE_He_FindEntityHandlerOrdinal.
// ===========================================================================
// The original indexes byte_12CE912[byteOffset] where byteOffset steps by 536, so
// the profession byte of record k is at column slot k. Returns the profession byte
// of the record at byte offset `off` (0 when out of range / no column installed).
static u8 ProfAt(int off) {
    int idx = off / kPersonStride2;
    if (!g_profColumn || idx < 0 || idx >= g_profSlots)
        return 0;
    return g_profColumn[idx];
}

int He_FindEntityHandlerOrdinal(int slot) {
    int ordinal = 0;                        // v1
    int target  = 8 * (67 * slot);          // v4 = 8 * 67 * slot == 536 * slot
    int cursor  = 0;                         // v3 (byte offset into the column)
    while (ProfAt(cursor) != kProfession6) { // skip non-prof-6 records
        cursor += kPersonStride2;
        if (cursor >= kPersonScanBound2)
            return -1;
    }
    while (cursor != target) {              // count prof-6 records before `slot`
        ++ordinal;
        do {
            cursor += kPersonStride2;
            if (cursor >= kPersonScanBound2)
                return -1;
        } while (ProfAt(cursor) != kProfession6);
    }
    return ordinal;
}

// ===========================================================================
// gilde.exe 0x4c5370 — VIBE_He_UpdateSubsystems.
// ===========================================================================
i32 He_UpdateSubsystems(i32 (*tickActive)()) {
    if (tickActive)
        tickActive();                       // VIBE_He_TickActiveHandlers(a1, a2)
    if (g_hooks->charActionTick && g_hooks->charActionTick())
        return 1;
    if (g_hooks->eventTick && g_hooks->eventTick())
        return 1;
    i32 result = g_hooks->buildingTick ? g_hooks->buildingTick() : 0;
    if (result)
        return 1;
    return result;
}

} // namespace guild::sim
