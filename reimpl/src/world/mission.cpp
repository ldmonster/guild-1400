#include "world/mission.h"

#include <cstring>

#include "world/event.h"

// Faithful 1:1 port of the VIBE_Mission_* slot/pick/requirement core.

namespace guild::world {

MissionSlot g_missionSlots[kMissionSlotCount];
u8          g_missionSlotMode = 0;

void MissionSlotTableReset() {
    std::memset(g_missionSlots, 0, sizeof(g_missionSlots));
    g_missionSlotMode = 0;
}

// gilde.exe 0x53872c — VIBE_Mission_SlotRegister  (eax=owner, dl=type).
int MissionSlotRegister(i32 owner, u8 type) {
    if (g_missionSlotMode == 5) {           // byte_63C8F4 == 5: single-slot mode
        g_missionSlots[0].type  = type;
        g_missionSlots[0].owner = owner;
        // Zero the deadline/progress block (dword_122FEC8 = 0 then GameTime_Set).
        std::memset(&g_missionSlots[0].deadline, 0, sizeof(g_missionSlots[0].deadline));
        g_missionSlots[0].state = 0;        // byte_122FEE0[0] = 0
        return 0;
    }
    int slot = 0;
    if (g_missionSlots[0].type != 0) {      // byte_122FEC0[0] occupied -> scan
        while (true) {
            ++slot;                         // v2 += 36 (one slot)
            if (slot >= kMissionSlotCount)  // v2 >= 4608 (128 * 36)
                return -1;
            if (g_missionSlots[slot].type == 0)  // !byte_122FEC0[v2]
                break;
        }
    }
    g_missionSlots[slot].type  = type;
    g_missionSlots[slot].owner = owner;
    std::memset(&g_missionSlots[slot].deadline, 0, sizeof(g_missionSlots[slot].deadline));
    g_missionSlots[slot].state = 0;
    return slot;
}

// gilde.exe 0x538668 — VIBE_Mission_PickAndRegisterRandom.
int MissionPickAndRegisterRandom(i32 owner, u8 category) {
    int value = EventPickRandomByCategory(category);
    // The original passes the (possibly -1) pick straight into SlotRegister as
    // the type byte; we preserve that, registering 0xFF when no descriptor hit.
    return MissionSlotRegister(owner, static_cast<u8>(value));
}

// gilde.exe 0x53846c — VIBE_StraftatTable_FindBySource.
int MissionFindBySource(i32 sourceOwner) {
    for (int i = 0; i < kMissionSlotCount; ++i) {
        if (g_missionSlots[i].type != 0 && g_missionSlots[i].owner == sourceOwner)
            return i;
    }
    return -1;
}

// gilde.exe 0x539054 — VIBE_Mission_TrackCrimeProgress (type-gate portion).
bool MissionTypeIsTrackable(u8 crimeType) {
    if (crimeType < 0x17u) {            // < 23
        if (crimeType >= 0x0Bu) {      // 11..22
            if (crimeType > 0x0Bu && crimeType != 19)
                return false;          // 12..22 except 19 -> reject
            return true;               // 11 or 19 -> accept
        }
        return false;                  // < 11 -> reject
    }
    // crimeType >= 23
    if (crimeType > 0x17u && (crimeType < 0x1Cu || (crimeType > 0x1Cu && crimeType != 40)))
        return false;                  // 24..27, 29..39, 41.. -> reject
    return true;                       // 23, 28, or 40 -> accept
}

// gilde.exe 0x539054 — progress-advance portion.
// Mirrors the tail of VIBE_Mission_TrackCrimeProgress exactly: once a slot has
// been found (FindBySource != 0) and the crime type is trackable, the original
// ALWAYS returns 1 — it only conditionally bumps the progress counter when the
// slot's type byte equals the crime type ((u8)*result == v2 -> ++*(result+7)).
// The not-found / not-trackable paths are the only ones that return 0.
bool MissionRequirementAdvance(int slot, u8 crimeType) {
    if (slot < 0 || slot >= kMissionSlotCount)
        return false;
    if (g_missionSlots[slot].type == 0)        // FindBySource missed -> return 0
        return false;
    if (!MissionTypeIsTrackable(crimeType))    // type-gate failed -> return 0
        return false;
    if (g_missionSlots[slot].type == crimeType)  // (u8)*result == v2
        ++g_missionSlots[slot].fieldAt28;          // ++*((_DWORD*)result + 7)
    return true;                                 // mov eax, 1 (unconditional)
}

} // namespace guild::world
