#pragma once
// Mission system — the active-mission slot table plus the descriptor-driven
// pick/register/find/requirement logic. Faithful 1:1 port of the VIBE_Mission_*
// core (gilde.exe), built on the shared descriptor table in world/event.h.
//
// Slot table (gilde.exe byte_122FEC0 @0x122FEC0, 128 slots x 36 bytes), recovered
// byte-for-byte from VIBE_Mission_SlotRegister 0x53872c and the serializer
// VIBE_Mission_SaveSlotTable 0x53b148 (writes +0:1B, +4:4B, +8:14B, +24:4B,
// +28:4B, +32:1B per slot, 128 slots).
#include <cstddef>

#include "guild/common/types.h"
#include "sim/types.h"   // guild::sim::GameTime (14-byte packed record)

namespace guild::world {

// ===========================================================================
// Active-mission slot  (gilde.exe byte_122FEC0, stride 36 bytes, 128 slots)
// ===========================================================================
GUILD_PACKED_BEGIN
struct MissionSlot {
    u8  type;                 // +0x00  mission subtype byte (0 == empty slot)
    u8  pad1[3];              // +0x01  alignment to the owner dword
    i32 owner;                // +0x04  owning entity id (dword_122FEC4)
    // +0x08: the deadline GameTime record (14 bytes). SlotRegister zeroes its
    // first dword (dword_122FEC8) and then GameTime_Set-s the whole block; the
    // serializer writes it as one 14-byte field (+8..+21). The first dword (day)
    // doubles as the progress accumulator the rest of the system reads.
    guild::sim::GameTime deadline; // +0x08  deadline / progress time record (14B)
    u8  pad22[2];             // +0x16  gap to the +24 dword
    i32 fieldAt24;            // +0x18  serialized dword (+24)
    i32 fieldAt28;            // +0x1C  serialized dword (+28); TrackCrimeProgress
                              //         bumps this (++*((_DWORD*)slot + 7))
    u8  state;                // +0x20  state byte (+32, byte_122FEE0)
    u8  pad33[3];             // +0x21  pad to the 36-byte stride
} GUILD_PACKED;
GUILD_PACKED_END
static_assert(sizeof(MissionSlot) == 36, "MissionSlot stride must be 36 bytes");
static_assert(offsetof(MissionSlot, owner)     == 4,  "owner @+4");
static_assert(offsetof(MissionSlot, deadline)  == 8,  "deadline @+8");
static_assert(offsetof(MissionSlot, fieldAt24) == 24, "fieldAt24 @+24");
static_assert(offsetof(MissionSlot, fieldAt28) == 28, "fieldAt28 @+28");
static_assert(offsetof(MissionSlot, state)     == 32, "state @+32");

constexpr int kMissionSlotCount = 128;

extern MissionSlot g_missionSlots[kMissionSlotCount];

// Mirrors byte_63C8F4: when == 5 the register call overwrites slot 0 (single-slot
// mode) instead of scanning for a free slot.
extern u8 g_missionSlotMode;

// Clears the slot table and resets the single-slot mode flag.
void MissionSlotTableReset();

// gilde.exe 0x53872c — VIBE_Mission_SlotRegister  (eax=owner, dl=type).
// When mode==5: writes (type, owner) into slot 0, zeroes progress/deadline/state,
// returns slot 0. Otherwise finds the first empty slot (type==0), fills it, and
// returns its index. Returns -1 when the table is full. (The original returns a
// pointer; we return the slot index, or -1.)
int MissionSlotRegister(i32 owner, u8 type);

// gilde.exe 0x538668 — VIBE_Mission_PickAndRegisterRandom.
// Picks a random descriptor value of `category` (EventPickRandomByCategory) and
// registers it for `owner`. Returns the slot index, or -1.
int MissionPickAndRegisterRandom(i32 owner, u8 category);

// gilde.exe 0x53846c — VIBE_StraftatTable_FindBySource.
// Scans slots for the first occupied slot whose owner == `sourceOwner`; returns
// the slot index, or -1.
int MissionFindBySource(i32 sourceOwner);

// ---------------------------------------------------------------------------
// Mission requirement evaluation.
// ---------------------------------------------------------------------------
// gilde.exe 0x539054 — VIBE_Mission_TrackCrimeProgress: a crime/mission-type
// gate. Reconstructing the original's nested range tests, the qualifying crime
// subtypes are exactly {11, 19, 23, 28, 40}: <11 rejected; 11..22 accepts only
// 11 or 19; ==23 accepts; >23 accepts only 28 or 40. Returns true when
// `crimeType` is a trackable mission type.
bool MissionTypeIsTrackable(u8 crimeType);

// Evaluates whether `crimeType` advances the mission in `slot`: the slot must be
// occupied, its type must equal `crimeType`, and `crimeType` must be trackable.
// On a match the slot's progress counter is incremented (mirrors
// `++*((_DWORD*)result + 7)` at slot+28 in the original). Returns true if it
// advanced. (slot+28 == fieldAt28 is the progress dword the tracker bumps.)
bool MissionRequirementAdvance(int slot, u8 crimeType);

} // namespace guild::world
