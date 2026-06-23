#pragma once
// gilde.exe — Game-clock record helpers (the "GameTime" packed record family)
// recovered from the VIBE_GameTime_* cluster. These sit alongside the already
// reconstructed VIBE_GameTime_Advance (src/sim/gametime.cpp, 0x583150) and add
// the record (de)serialization + the register-shuffle thunk + the per-frame
// timer-scaled delay accessor.
//
// Functions reconstructed here:
//   VIBE_GameTime_InitDefault       @0x58320c
//   VIBE_GameTime_UnpackFromRecord  @0x58334c
//   VIBE_GameTime_AdvanceThunk      @0x583374
//   VIBE_GameTick_GetScaledDelay    @0x43c680
//
// All are pure data/integer math; no platform coupling.
#include "guild/common/types.h"
#include "sim/types.h"
#include "sim/gametime.h"  // GameTimeAdvance @0x583150 (reused, not redefined)

namespace guild::sim {

using namespace guild;

// The packed on-disk / in-record source layout consumed by
// VIBE_GameTime_UnpackFromRecord. Recovered from the disassembly at 0x58334c:
//
//   mov ecx,[eax]      ; src+0 dword
//   sar ecx,10h        ; >> 16   (arithmetic)
//   sub ecx,578h       ; - 1400  (Europa 1400 base year)  -> out.day(year)
//   mov cl,[eax+4]     ; src+4 byte (zero-extended)        -> out word
//   mov cl,[eax+5]     ; src+5 byte (zero-extended)        -> out dword
//   mov eax,[eax+8]    ; src+8 dword                        -> out dword
//
// So the high 16 bits of src+0 hold a year value biased by +1400; bytes at
// +4 and +5 hold two small fields; +8 holds a full dword. The low 16 bits of
// src+0 are not used by this routine.
GUILD_PACKED_BEGIN
struct GameTimePackedRecord {
    i32 packedHead;   // +0x00  high 16 bits = year+1400 (read via sar 16)
    u8  field4;       // +0x04  small field -> GameTime.hour word
    u8  field5;       // +0x05  small field -> GameTime.minute dword
    u8  pad6[2];      // +0x06..+0x07 (untouched by the unpack)
    i32 field8;       // +0x08  dword -> GameTime.second dword
} GUILD_PACKED;
GUILD_PACKED_END
static_assert(sizeof(GameTimePackedRecord) == 12,
              "GameTimePackedRecord must be 12 bytes");

// gilde.exe 0x58320c — VIBE_GameTime_InitDefault (__usercall, eax = (rec@eax)).
// Copies the 12-byte template at qword_13CE852 plus the 2-byte tail at
// unk_13CE85E into the 14-byte GameTime record, then returns 14. The template
// bytes in the binary are all zero, so this clears the record. Returns 14
// (the number of bytes written), exactly as the original.
int GameTimeInitDefault(GameTime* rec);

// gilde.exe 0x58334c — VIBE_GameTime_UnpackFromRecord
//   (__usercall, eax = src@eax, edx = out@edx).
// Unpacks a GameTimePackedRecord into a GameTime-layout record:
//   out->day    = (src->packedHead >> 16) - 1400   (arithmetic shift)
//   out->hour   = (u16)src->field4                 (zero-extended byte)
//   out->minute = (u32)src->field5                 (zero-extended byte)
//   out->second = src->field8                       (full dword)
// Returns the dword copied into out->second (the original returns it in eax).
i32 GameTimeUnpackFromRecord(const GameTimePackedRecord* src, GameTime* out);

// gilde.exe 0x583374 — VIBE_GameTime_AdvanceThunk (__userpurge, eax = ...).
// A register-shuffle thunk: the original receives (eax,ecx,ebx,stack) and
// forwards to VIBE_GameTime_Advance(a1@eax, a3@edx, a4@ecx, a2@ebx). Concretely
// the call is GameTimeAdvance(rec, addDays, addSeconds, addMinutes) where the
// thunk maps its (rec, addMinutesIn@ecx, addDaysIn@ebx, addSecondsIn@stack)
// arguments onto Advance's (rec, addDays, addSeconds, addMinutes). We preserve
// the exact argument mapping rather than the register names.
int GameTimeAdvanceThunk(GameTime* rec, int ecxArg, int ebxArg, int stackArg);

// gilde.exe 0x43c680 — VIBE_GameTick_GetScaledDelay.
// Returns uDelay * dword_62EB38: the configured timer delay (ms) multiplied by
// the throttled-tick counter. Exposed to the script VM as the "GetTime" command
// (registered at VIBE_Script_RegisterCommands 0x43c8d4). Both inputs are host-
// supplied runtime state, routed through GameTickClockState (inert defaults 0,
// matching the binary's zero-initialized globals uDelay@0xB53948,
// dword_62EB38@0x62EB38).
struct GameTickClockState {
    u32 uDelay = 0;          // 0xB53948  multimedia-timer delay in ms
    u32 throttledTicks = 0;  // 0x62EB38  throttled interval tick counter
};
u32 GameTickGetScaledDelay(const GameTickClockState& clock);

} // namespace guild::sim
