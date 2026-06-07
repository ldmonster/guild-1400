#pragma once
// gilde.exe — app spine init/shutdown ordering helpers (guild::app).
//
// The init and shutdown ORDER is the load-bearing property of the spine, so it
// is named here as explicit ordered step lists for tests/documentation. The
// implementations live in app_init.cpp (init + Run + Shutdown) and
// frameloop.cpp (RunFrameLoop). See gamelogic.h for the bit tables.
#include <cstdint>

namespace guild::app {

// The 13 teardown steps run at *every* exit of VIBE_GameLogic_MainEntryAndShutdown
// @0x534bbc, in this exact order (the "shutdown sequence" of recon §1.12). The
// movie-DLL exit + FreeLibrary and screensaver-restore are conditional tails
// that follow these 13 core steps.
enum class TeardownStep : int {
    GameShutdownSubsystems = 0, // VIBE_Game_ShutdownSubsystems   0x5278cc
    WidgetShutdownSystem,       // VIBE_Widget_ShutdownSystem      0x4201f4
    ConfigWriteGfxSettings,     // VIBE_Config_WriteGfxSettings    0x56af54
    GameStateFreeAllResources,  // VIBE_GameState_FreeAllResources 0x40e308
    UniverseSwitchActiveSlot0,  // VIBE_Universe_SwitchActiveSlot(0) 0x5b4a24
    TableResetLightmaps,        // VIBE_Table_ResetLightmaps       0x42e19c
    RenderShutdownEngine,       // VIBE_Render_ShutdownEngine      0x5b0228
    InputDirectInputShutdown,   // VIBE_Input_DirectInputShutdown  0x40cd40
    TimeBaseStopTimer,          // VIBE_TimeBase_StopTimer         0x44e2c4
    VfsShutdown,                // VIBE_Vfs_Shutdown               0x452004
    MemPoolShutdownStack,       // VIBE_MemPool_ShutdownStack      0x44e544
    MemoryShutdownTracker,      // VIBE_Memory_ShutdownTracker     0x439640
    ErrorLogShutdown,           // VIBE_ErrorLog_Shutdown          0x438c0c
    Count
};

} // namespace guild::app
