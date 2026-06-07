#pragma once
// Game calendar arithmetic for the Guild simulation (gilde.exe).
//   VIBE_GameTime_Advance 0x583150
// Operates on the packed GameTime record (see types.h, qword_13CE852 @0x13CE852).
#include "guild/common/types.h"
#include "sim/types.h"

namespace guild::sim {

// gilde.exe 0x583150 — VIBE_GameTime_Advance
//   (__usercall: eax=record@a1, edx=addDays@a2, ecx=addSeconds@a3,
//                ebx=addMinutes@a4)
// Adds (addDays, addMinutes, addSeconds) to the packed time record, carrying
// seconds->minutes (/60), minutes->hours (/60), hours->days (wrap 24), with a
// negative-borrow path. Returns the resulting hour-of-day (0..23), which the
// caller also finds stored in record->hour. Used for both the wall clock and
// scheduling future "appointment" times (callers pass +N).
int GameTimeAdvance(GameTime* rec, int addDays, int addSeconds, int addMinutes);

// gilde.exe 0x583230 — VIBE_GameTime_Compare(a@eax, b@edx)
//   Returns -1 if a<b, +1 if a>b, 0 if equal. Compares day (dword) first, then
//   total seconds-of-day (3600*hour + 60*minute + second).
int GameTimeCompare(const GameTime* a, const GameTime* b);

// gilde.exe 0x5832bc — VIBE_GameTime_DiffMinutes(a@eax, b@edx)
//   Returns (b - a) expressed in minutes: 1440*(dayB-dayA) + 60*(hourB-hourA)
//   + (minuteB - minuteA). (The original ignores the seconds field.)
int GameTimeDiffMinutes(const GameTime* a, const GameTime* b);

} // namespace guild::sim
