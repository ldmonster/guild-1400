#pragma once
// =============================================================================
// guild::play — NATIVE INTERACTIVE CITY SESSION (PLAYABLE_PLAN: native Linux play).
//
// The actual "play the game" loop, backend-agnostic. The reconstruction renders
// the real city with its own (reconstructed, 1:1) SOFTWARE rasterizer into an
// IGraphicsDevice framebuffer; the device PRESENTS it. On the real host that
// device is shim::VulkanGraphicsDevice (Vulkan swapchain -> the SDL window,
// replacing DirectDraw/Direct3D) and the platform is shim::SdlVulkanPlatform
// (SDL2 window + input, replacing Win32/DInput); SDL2 also drives audio. NONE of
// this needs Wine — Wine was only ever the off-line verification oracle.
//
// This module is the device/platform-AGNOSTIC core so it is fully testable
// headless (shim::MemoryGraphicsDevice + shim::ScriptedPlatform) AND drives the
// real Vulkan+SDL window unchanged. The caller constructs + initializes the
// device (for Vulkan: configureSwapchain() BEFORE init(fbW,fbH,16,false)) and the
// platform window, then hands them in.
//
// Per frame the loop:
//   (1) renders the real loaded city through play::RealCityRenderer::Render into
//       device.backbuffer() (real AGF meshes from Objects.BIN, optionally textured
//       from Textures.BIN),
//   (2) device.present()  — Vulkan blits the framebuffer to the swapchain image,
//   (3) plat.pumpMessages() — false (window close) ends the session,
//   (4) reads plat.getMouse()/keyDown():
//         * left click   -> RealCityRenderer::Pick at the cursor; if it hits a
//                           live object, issue the current-cursor-mode order on it
//                           through the REAL CommandQueue (play::IssueWorldClick /
//                           the order-apply handler) so the world mutates,
//         * SPACE        -> advance one real game-day (play::RunGameDay),
//         * arrows / screen-edge -> pan the camera (play::CameraControl),
//         * ESC          -> quit,
//   (5) frame-caps via plat.sleepMs(cfg.frameCapMs).
//
// Determinism: HashFullWorld is sampled at start and end as a witness; with a
// scripted platform + fixed seed the whole trace is reproducible (the play-layer
// determinism rig applies — ZeroWorldGlobals on load, Srand before each hash).
// =============================================================================
#include <cstdint>
#include <string>

namespace guild::shim { class IFileSystem; class IGraphicsDevice; class IPlatform; }

namespace guild::play {

// Cursor/order tool the left click issues on the picked object (mirrors
// play::CursorMode; kept as a small int here to avoid leaking the enum).
struct SdlSessionConfig {
    std::string gameDir;                                       // assets root (MountRealGameAssets)
    std::string cityPath = "Resources/gamedata/Cities/AUGSBURG.cty";
    std::string iniName  = "Gilde.INI";
    std::string objectsArchive  = "Resources/Objects.BIN";
    std::string texturesArchive = "Resources/Textures.BIN";    // "" -> untextured
    int  fbW = 800, fbH = 600;                                 // device MUST be init'd to this x16bpp
    bool textured = true;
    int  maxFrames = -1;                                       // -1 = until quit (window close/ESC)
    int  frameCapMs = 16;                                      // sleep per frame; 0 = uncapped
    bool advanceDayOnSpace = true;                             // SPACE -> RunGameDay
    unsigned seed = 0x4711;                                    // determinism seed
    // Cursor mode for the left-click order (6 == kConquer, the reliable record-
    // mutating order per the play-layer memory). See play::CursorMode.
    int  clickCursorMode = 6;
};

struct SdlSessionTrace {
    bool mounted = false;          // Objects.BIN mounted + assets mounted
    bool loaded  = false;          // city loaded into the live world
    int  liveObjects = 0;
    int  persons = 0;
    int  framesPresented = 0;      // device.present() calls that succeeded
    int  clicksHandled = 0;        // left-click edges processed
    int  picksHit = 0;             // clicks that resolved to a live object
    int  ordersIssued = 0;         // orders enqueued+applied from clicks
    int  daysAdvanced = 0;         // SPACE-driven game-days run
    int  lastPickedId = 0;
    bool quitByWindow = false;     // pumpMessages() returned false
    bool quitByEsc = false;        // ESC pressed
    bool cleanQuit = false;        // loop exited via quit (not maxFrames)
    std::uint64_t hashStart = 0;   // HashFullWorld after load
    std::uint64_t hashEnd   = 0;   // HashFullWorld at session end
};

// Run the interactive city session. `device` MUST already be init()'d to
// cfg.fbW x cfg.fbH x 16bpp (and, for Vulkan, configureSwapchain()'d before init
// for on-screen present); `plat`'s main window MUST already be created. Mounts the
// assets, loads cfg.cityPath, then runs the render/present/input loop until quit
// (or cfg.maxFrames frames). Returns the trace. Safe headless (dummy SDL drivers /
// MemoryGraphicsDevice + ScriptedPlatform).
SdlSessionTrace RunSdlSession(shim::IFileSystem& fs, shim::IGraphicsDevice& device,
                              shim::IPlatform& plat, const SdlSessionConfig& cfg);

} // namespace guild::play
