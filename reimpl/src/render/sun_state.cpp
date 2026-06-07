#include "render/sun_state.h"

#include "sim/actionqueue.h"  // extern u32 g_gameTick (dword_62EB38), owned in sim

namespace guild::render {

// Owning definitions of the sun-direction transition globals.
i32 g_sunDirState = 0;   // dword_62D568
i32 g_sunDirStamp = 0;   // dword_62D56C
i32 g_sunDirParam = 0;   // dword_62D570

// Owning definitions of the lighting accumulator globals.
i32 g_lightState54 = 0;  // dword_64A054
i32 g_lightState58 = 0;  // dword_64A058
i32 g_lightState5C = 0;  // dword_64A05C
i32 g_lightState60 = 0;  // dword_64A060
i32 g_lightState64 = 0;  // dword_64A064

// gilde.exe 0x42dc40 — VIBE_Light_SetSunDirection
i32 SetSunDirection(i32 a1, i32 a2) {
    (void)a1;                                       // ecx scratch, unused
    g_sunDirState = -1;                             // dword_62D568 = -1
    g_sunDirStamp = static_cast<i32>(sim::g_gameTick);  // = dword_62EB38
    g_sunDirParam = a2;                             // dword_62D570 = a2
    return static_cast<i32>(sim::g_gameTick);       // return dword_62EB38
}

// gilde.exe 0x42dc5c — VIBE_Light_EnableSun
i32 EnableSun() {
    g_sunDirState = 1;                              // dword_62D568 = 1
    g_sunDirStamp = static_cast<i32>(sim::g_gameTick);
    g_sunDirParam = 0;                              // dword_62D570 = 0
    return static_cast<i32>(sim::g_gameTick);
}

// gilde.exe 0x5c8964 — VIBE_Light_ResetGlobalState
void ResetGlobalState() {
    g_lightState5C = 0;   // dword_64A05C
    g_lightState60 = 0;   // dword_64A060
    g_lightState58 = 0;   // dword_64A058
    g_lightState64 = 0;   // dword_64A064
    g_lightState54 = 0;   // dword_64A054
}

} // namespace guild::render
