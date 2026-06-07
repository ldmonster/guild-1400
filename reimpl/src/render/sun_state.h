#pragma once
#include "guild/common/types.h"

// guild::render — sun / global-lighting state setters. Faithful 1:1 of:
//
//   0x42dc40  VIBE_Light_SetSunDirection   (arm a sun-direction transition)
//   0x42dc5c  VIBE_Light_EnableSun         (enable sun, reset transition state)
//   0x5c8964  VIBE_Light_ResetGlobalState  (clear the lighting accumulator dwords)
//
// These poke a handful of module globals the day/night and lighting passes read.
// They are recovered here as the owning definitions (no other module defines
// dword_62D568/56C/570 or dword_64A054..64A064).
namespace guild::render {

// --- Sun-direction transition state (dword_62D568 / 62D56C / 62D570) ---------
//   g_sunDirState  (dword_62D568) : -1 = "set direction" pending, 1 = sun enabled
//   g_sunDirStamp  (dword_62D56C) : the game tick the transition was armed at
//   g_sunDirParam  (dword_62D570) : the direction argument (0 when enabling)
extern i32 g_sunDirState;   // dword_62D568
extern i32 g_sunDirStamp;   // dword_62D56C
extern i32 g_sunDirParam;   // dword_62D570

// --- Lighting accumulator state cleared by ResetGlobalState -------------------
//   dword_64A054 / 58 / 5C / 60 / 64 — five trailing animation/state dwords the
//   lighting refresh resets between scene loads.
extern i32 g_lightState54;  // dword_64A054
extern i32 g_lightState58;  // dword_64A058
extern i32 g_lightState5C;  // dword_64A05C
extern i32 g_lightState60;  // dword_64A060
extern i32 g_lightState64;  // dword_64A064

// gilde.exe 0x42dc40 — VIBE_Light_SetSunDirection(__fastcall (a1, a2)).
// Arms a "set sun direction" transition: state = -1, stamp = current game tick,
// param = a2. a1 is ignored (ecx scratch). Returns the game tick.
i32 SetSunDirection(i32 a1, i32 a2);

// gilde.exe 0x42dc5c — VIBE_Light_EnableSun().
// Enables the sun: state = 1, stamp = current game tick, param = 0. Returns the
// game tick.
i32 EnableSun();

// gilde.exe 0x5c8964 — VIBE_Light_ResetGlobalState().
// Zeroes the five lighting accumulator dwords (54/58/5C/60/64).
void ResetGlobalState();

} // namespace guild::render
