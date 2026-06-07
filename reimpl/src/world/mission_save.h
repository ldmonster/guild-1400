#pragma once
// Mission slot-table serialization + the single-slot setter — the deferred
// save/load pair of the VIBE_Mission_* core (gilde.exe). Builds on the slot table
// modelled in world/mission.h (byte_122FEC0, 128 slots x 36 bytes).
//
// Translated functions:
//   VIBE_Mission_SlotSetSingle  0x538638  (force slot 0 = (owner,type), clear rest)
//   VIBE_Mission_SaveSlotTable  0x53b148  (mode byte + per-slot field writes)
//   VIBE_Mission_LoadSlotTable  0x53b25c  (the byte-exact read counterpart)
//
// Save/Load write the mode byte (byte_63C8F4 == g_missionSlotMode) once, then for
// each of the 128 slots six fields in this exact order and width:
//   +0 type (1)   +4 owner (4)   +8 deadline (14)   +24 fieldAt24 (4)
//   +28 fieldAt28 (4)   +32 state (1)
// i.e. 28 payload bytes per slot, 1 + 128*28 == 3585 bytes total. The 7 pad bytes
// of each 36-byte slot are NOT serialized (the original streams the six fields,
// never the whole stride).
#include <cstddef>

#include "guild/common/types.h"

namespace guild::world {

// Bytes the serializer emits per slot (1+4+14+4+4+1) and for the whole table.
constexpr std::size_t kMissionSlotSerializedBytes = 28;
constexpr std::size_t kMissionTableSerializedBytes = 1 + 128 * kMissionSlotSerializedBytes; // 3585

// ===========================================================================
// VIBE_Mission_SlotSetSingle 0x538638  (eax=owner, dl=type).
// ===========================================================================
// Writes (type, owner) into slot 0, zeroes its deadline/progress block
// (dword_122FEC8 = 0 then VIBE_GameTime_Set(...,0,0,0)) and its state byte
// (byte_122FEE0[0] = 0). Unlike SlotRegister this never consults the single-slot
// mode flag and never scans — it unconditionally stamps slot 0.
void MissionSlotSetSingle(i32 owner, u8 type);

// ===========================================================================
// Stream model for the save/load pair (mirrors VIBE_Vfs_WriteStream /
// VIBE_Vfs_ReadStreamBool over a flat buffer).
// ===========================================================================
struct MissionStream {
    u8*         data = nullptr;  // backing buffer
    std::size_t size = 0;        // capacity in bytes
    std::size_t pos  = 0;        // cursor
};

// gilde.exe 0x53b148 — VIBE_Mission_SaveSlotTable. Writes the mode byte then the
// six per-slot fields for all 128 slots. Returns true on success (mirrors the
// original returning 1 after the 128th slot, 0 on any short write).
bool MissionSaveSlotTable(MissionStream& s);

// gilde.exe 0x53b25c — VIBE_Mission_LoadSlotTable. The byte-exact read
// counterpart. Returns true once all 128 slots are read back.
bool MissionLoadSlotTable(MissionStream& s);

} // namespace guild::world
