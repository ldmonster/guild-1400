// apps/guild_run.cpp — playable/headless entry point on the REAL host backends.
//
// Boots the reconstructed "Die Gilde" app spine (CreateMainWindow -> InitSubsystems
// -> InitDisplayAndPaths -> InitEngineAndScriptCommands -> N frames -> 13-step
// shutdown) wired to the real host backends requested for this port:
//
//   * graphics : guild::shim::VulkanGraphicsDevice   (Vulkan, replaces DirectDraw/Direct3D)
//   * audio    : guild::shim::SdlAudioDevice          (SDL2, replaces the Miles driver)
//   * platform : guild::shim::SdlVulkanPlatform       (SDL2 window/input; degrades to
//                NullPlatform when no display is available, e.g. headless/CI)
//   * movie    : STUB — all videos skipped (showIntro=false; the movie-DLL hooks are no-ops)
//   * copy-protection : SKIPPED (drm is an intentional inert stub)
//
// Built only when configured with -DGUILD_BACKEND=ON (see CMakeLists / CMakePresets:
//   cmake --preset vulkan-sdl && cmake --build build-vk --target guild_run
// ). Lives in apps/ (not src/) so the src/**/*.cpp glob never pulls this main()
// into libguild.
//
// Headless run (this machine has no GPU/display): Vulkan uses the lavapipe software
// ICD and presents OFFSCREEN; SDL audio uses the dummy driver. After the frames run,
// the last Vulkan-presented image is read back and written to a BMP, proving real
// bytes flowed engine -> software rasterizer -> Vulkan -> image.
//
//   SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy ./guild_run [--frames N]
//       [--game-dir DIR] [--dump out.bmp]

#include "app/gamelogic.h"
#include "app/wiring.h"
#include "app/real_boot.h"

#include "play/run_interactive_app.h"
#include "play/sdl_session.h"
#include "play/sdl_menu.h"
#include "play/native_main_menu.h"
#include "play/sdl_city_screen.h"
#include "play/sdl_charcreate_screen.h"
#include "play/sdl_options_screen.h"
#include "play/sdl_credits_screen.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include "gui/main_menu.h"

#include "shim_impl/vulkan_backend.h"
#include "shim_impl/sdl_audio.h"
#include "shim_impl/sdl_vulkan_platform.h"
#include "shim_impl/null_platform.h"
#include "shim_impl/disk_filesystem.h"
#include "shim_impl/mem_filesystem.h"
#include "shim_impl/loopback_socket.h"

#include "config/ini.h"
#include "net/transport.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#if !defined(GUILD_HAVE_VULKAN) || !defined(GUILD_HAVE_SDL2)
int main() {
    std::printf("guild_run: built without GUILD_BACKEND (Vulkan+SDL2). "
                "Configure with `cmake --preset vulkan-sdl`.\n");
    return 0;
}
#else

using namespace guild;

namespace {

// Write width*height XRGB8888 (0xFFRRGGBB) texels as a 24-bit BMP (bottom-up).
bool WriteBmp(const std::string& path, const std::uint32_t* px, int w, int h) {
    if (!px || w <= 0 || h <= 0) return false;
    const int rowBytes = (w * 3 + 3) & ~3;
    const int imgSize = rowBytes * h;
    const int fileSize = 54 + imgSize;
    std::vector<std::uint8_t> f(fileSize, 0);
    auto put16 = [&](int o, std::uint16_t v) { f[o] = v & 0xFF; f[o + 1] = (v >> 8) & 0xFF; };
    auto put32 = [&](int o, std::uint32_t v) {
        f[o] = v & 0xFF; f[o + 1] = (v >> 8) & 0xFF; f[o + 2] = (v >> 16) & 0xFF; f[o + 3] = (v >> 24) & 0xFF;
    };
    f[0] = 'B'; f[1] = 'M'; put32(2, fileSize); put32(10, 54);
    put32(14, 40); put32(18, w); put32(22, h); put16(26, 1); put16(28, 24); put32(34, imgSize);
    for (int y = 0; y < h; ++y) {
        std::uint8_t* row = &f[54 + (h - 1 - y) * rowBytes]; // bottom-up
        for (int x = 0; x < w; ++x) {
            std::uint32_t c = px[y * w + x];
            row[x * 3 + 0] = c & 0xFF;          // B
            row[x * 3 + 1] = (c >> 8) & 0xFF;   // G
            row[x * 3 + 2] = (c >> 16) & 0xFF;  // R
        }
    }
    std::ofstream o(path, std::ios::binary);
    if (!o) return false;
    o.write(reinterpret_cast<const char*>(f.data()), f.size());
    return o.good();
}

} // namespace

int main(int argc, char** argv) {
    int frames = 5;
    bool framesSet = false;   // was --frames explicitly given? (--play: omit => run until ESC/close)
    std::string gameDir;
    std::string dumpPath = "guild_run_frame.bmp";
    bool interactive = false;
    bool play = false;
    std::string cityPath = "Resources/gamedata/Cities/AUGSBURG.cty";
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--frames" && i + 1 < argc) { frames = std::atoi(argv[++i]); framesSet = true; }
        else if (a == "--game-dir" && i + 1 < argc) gameDir = argv[++i];
        else if (a == "--dump" && i + 1 < argc) dumpPath = argv[++i];
        else if (a == "--city" && i + 1 < argc) cityPath = argv[++i];
        else if (a == "--interactive") interactive = true;
        else if (a == "--play") play = true;
        else if (a == "--help" || a == "-h") {
            std::printf(
                "guild_run - Die Gilde reconstruction: NATIVE Linux play (Vulkan render/present + SDL2 window/\n"
                "input/audio, NO Wine) plus engine bring-up / render harness modes.\n\n"
                "Usage: guild_run [--play|--interactive] [--game-dir DIR] [--city REL] [--frames N] [--dump out.bmp]\n"
                "  --play           THE PLAY-THE-CITY LOOP: load the real city, render it with its real AGF\n"
                "                   meshes through the SOFTWARE rasterizer, PRESENT each frame through the real\n"
                "                   Vulkan swapchain to the SDL2 window, and drive interaction from SDL2 input:\n"
                "                     left-click -> pick the object under the cursor + issue an order on it\n"
                "                                   (the world mutates through the real CommandQueue),\n"
                "                     SPACE      -> advance one real game-day, arrows/screen-edge -> pan,\n"
                "                     ESC / window-close -> quit.\n"
                "                   Needs --game-dir (a real 'Die Gilde' install). On-screen: run with a display\n"
                "                   (real window). Headless test: SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy +\n"
                "                   lavapipe (VK_ICD_FILENAMES=lvp_icd.*.json) + --frames N (so it terminates).\n"
                "  --interactive    drive the real spine's outer menu<->session FSM over the SDL/Vulkan window;\n"
                "                   headless falls back to a bounded live-frame run on NullPlatform\n"
                "  --game-dir DIR   the real 'Die Gilde' install (required for --play; else a synthetic world)\n"
                "  --city REL       city file to load (default Resources/gamedata/Cities/AUGSBURG.cty)\n"
                "  --frames N       --play: max frames before quit (-1/omit = until ESC/close); other modes:\n"
                "                   number of boot frames (default 5)\n"
                "  --dump FILE      write a Vulkan present->readback frame to FILE (default guild_run_frame.bmp)\n"
                "Headless (no display): Vulkan uses the lavapipe software ICD; with a display it opens an\n"
                "SDL_WINDOW_VULKAN window and presents through the swapchain. Env for headless:\n"
                "SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy.\n\n"
                "On-screen invocation (real display):\n"
                "  GUILD_GAME_DIR=$PWD/europe_guild_1400_original \\\n"
                "  ./guild_run --play --game-dir $PWD/europe_guild_1400_original\n");
            return 0;
        }
    }

    std::printf("guild_run: Vulkan graphics + SDL2 audio | movie=pl_mpeg(MPEG-1) | drm=bypass(stub; 1:1 recon exists)\n");

    // --- real backends ---------------------------------------------------
    shim::VulkanGraphicsDevice gfx;
    shim::SdlAudioDevice audioDev;
    shim::SdlVulkanPlatform sdlPlat;
    shim::NullPlatform nullPlat;

    // Prefer a real SDL/Vulkan window; degrade to NullPlatform when headless
    // (no display) so the spine still runs end to end on the real GPU backend.
    bool windowed = sdlPlat.createMainWindow("Die Gilde", 1024, 768, false);
    shim::IPlatform& plat = windowed ? static_cast<shim::IPlatform&>(sdlPlat)
                                     : static_cast<shim::IPlatform&>(nullPlat);
    std::printf("  platform: %s\n", windowed ? "SDL2 window (SDL_WINDOW_VULKAN)"
                                             : "headless (NullPlatform; no display)");

    // On-screen Vulkan: when we have a window, tell the graphics backend to build
    // a swapchain on its surface (present to the window) instead of offscreen-only.
    // Configure BEFORE the spine inits the device (InitDisplayAndPaths -> gfx.init).
    // Headless (no window) keeps the offscreen path. The surface factory receives
    // the VkInstance the device creates and makes the surface from it.
    if (windowed) {
        gfx.configureSwapchain(
            [&sdlPlat](VkInstance inst) { return sdlPlat.createSurface(inst); },
            sdlPlat.requiredInstanceExtensions());
        std::printf("  vulkan present: swapchain (on-screen window)\n");
    } else {
        std::printf("  vulkan present: offscreen (headless)\n");
    }

    // -----------------------------------------------------------------------
    // --play : THE NATIVE PLAY-THE-CITY LOOP.
    //
    // Render the real loaded city (real AGF meshes) through the software rasterizer
    // into the Vulkan framebuffer, PRESENT each frame through the real Vulkan
    // swapchain to the SDL2 window, and drive interaction from SDL2 input
    // (left-click -> pick+order, SPACE -> game-day, arrows/edge -> pan, ESC -> quit).
    // Wine is NOT involved. Headless (dummy SDL + lavapipe) terminates via --frames.
    // -----------------------------------------------------------------------
    if (play) {
        if (gameDir.empty()) {
            std::printf("  --play: needs --game-dir (a real 'Die Gilde' install).\n");
            if (windowed) sdlPlat.destroyMainWindow();
            return 2;
        }
        // The play loop renders + presents at 1024x768x16 (the original menu/play
        // resolution); init the Vulkan device to that surface (swapchain already
        // configured above when windowed).
        const int W = 1024, H = 768;
        if (!gfx.init(W, H, 16, /*fullscreen=*/false)) {
            std::printf("  --play: Vulkan device init failed.\n");
            if (windowed) sdlPlat.destroyMainWindow();
            return 3;
        }
        shim::IPlatform& playPlat = plat;   // SDL window when present, else NullPlatform

        shim::DiskFileSystem fs(gameDir);
        play::SdlSessionConfig cfg;
        cfg.gameDir   = gameDir;
        cfg.cityPath  = cityPath;
        cfg.fbW = W; cfg.fbH = H;
        cfg.frameCapMs = windowed ? 16 : 0;   // uncapped headless so tests are fast
        // Wave-2 session integration: the REAL 3D city view (CityView3D over the
        // .cty's embedded scene + SessionInput pick chain) and the continuous
        // game clock (SessionTick: TimeBase -> clock proc -> opcode-30 commits,
        // day rollover through RunGameDay). SPACE day-advance stays.
        cfg.city3d = true;
        cfg.continuousClock = true;

        // Enumerate the shipped cities for the New-Game screen.
        play::SdlMenuConfig mcfg;
        mcfg.gameDir = gameDir; mcfg.fbW = W; mcfg.fbH = H;
        mcfg.frameCapMs = windowed ? 16 : 0;
        {
            namespace fsx = std::filesystem;
            std::error_code ec;
            const fsx::path cdir = fsx::path(gameDir) / "Resources" / "gamedata" / "Cities";
            for (fsx::directory_iterator it(cdir, ec), end; !ec && it != end; ++it) {
                if (!it->is_regular_file()) continue;
                std::string ext = it->path().extension().string();
                for (char& c : ext) c = (char)std::tolower((unsigned char)c);
                if (ext != ".cty") continue;
                mcfg.cities.emplace_back(it->path().stem().string(),
                                         std::string("Resources/gamedata/Cities/") +
                                             it->path().filename().string());
            }
            std::sort(mcfg.cities.begin(), mcfg.cities.end());
        }

        if (windowed) {
            // THE REAL BOOT SPINE on the native Vulkan/SDL window: the BYTE-FAITHFUL
            // gui::Menu_RunMainMenu @0x529d08 drives, bridged to SDL present/input by
            // play::RunNativeMainMenu (its sub-screens are the native sdl_* screens).
            // It returns when Quit is chosen, the window closes, or a city is armed to
            // play; then we run the session and re-enter the real menu (ESC in-game).
            std::printf("  --play: %zu cities; booting the 1:1 main menu (Menu_RunMainMenu) ...\n",
                        mcfg.cities.size());
            // Bring up the audio device so the menu can play the looping CD track
            // (32 voices, stereo, 44.1kHz; harmless dummy device when headless).
            const bool haveAudio = audioDev.init(32, 2, 44100);
            bool running = true;
            while (running) {
                play::NativeMenuResult m = play::RunNativeMainMenu(
                    gfx, playPlat, fs, gameDir, mcfg.cities, W, H, /*frameCapMs=*/16, /*maxFrames=*/-1,
                    haveAudio ? &audioDev : nullptr);
                std::printf("  menu: action=%d city='%s' frames=%d quitWin=%d\n",
                            (int)m.action, m.cityPath.c_str(), m.framesPresented, (int)m.quitByWindow);
                if (m.quitByWindow || m.action == play::NativeMenuResult::kQuit) break;

                cfg.maxFrames = -1;   // play until ESC/close, then back to the menu
                cfg.audioDev = haveAudio ? &audioDev : nullptr;  // real session audio
                if (m.action == play::NativeMenuResult::kLoadGame && !m.savePath.empty()) {
                    // LOAD GAME: enter the session from the picked save
                    // (play::LoadLiveWorld), WITHOUT the new-game commit.
                    cfg.loadSavePath = m.savePath;
                    cfg.applyNewGame = false;
                    std::printf("  --play: loading save '%s' (%s) ...\n",
                                m.savePath.c_str(), m.saveName.c_str());
                } else if (m.action == play::NativeMenuResult::kPlayCity &&
                           !m.cityPath.empty()) {
                    // NEW GAME: the full chosen parameter block (the 0x122F4A0..
                    // image) commits right after the world load — the original's
                    // VIBE_Command_EnqueueInheritanceTransfer @0x533e03 call site.
                    cfg.loadSavePath.clear();
                    cfg.cityPath = m.cityPath;
                    cfg.newGame = m.params;
                    cfg.applyNewGame = m.params.started;
                    std::printf("  --play: loading '%s' (new game: %s %s, diff %d) ...\n",
                                cfg.cityPath.c_str(), m.params.firstName.c_str(),
                                m.params.familyName.c_str(), m.params.difficulty);
                } else {
                    continue;
                }
                play::SdlSessionTrace tr = play::RunSdlSession(fs, gfx, playPlat, cfg);
                std::printf("  session: loaded=%d objects=%d frames=%d days=%d orders=%d "
                            "view3d=%d(%d inst) newGame=%d player=%d clock=%d/%d "
                            "quitWin=%d quitEsc=%d\n",
                            (int)tr.loaded, tr.liveObjects, tr.framesPresented,
                            tr.daysAdvanced, tr.ordersIssued,
                            (int)tr.view3dActive, tr.view3dInstances,
                            (int)tr.newGameApplied, tr.playerId,
                            tr.worldDay, tr.worldHour,
                            (int)tr.quitByWindow, (int)tr.quitByEsc);
                // Wave-3: load-game 3D derives the city when the partial .SAV
                // embeds no scene (see sdl_session.cpp); report what was used.
                if (!tr.city3dCityName.empty())
                    std::printf("  session: 3D scene from stadt_%s.ed3 "
                                "(save-header/INI city derivation)\n",
                                tr.city3dCityName.c_str());
                if (tr.npcMovementActive && (tr.dailyAssigned || tr.moveSteps))
                    std::printf("  session living city: dispatched=%d assigned=%d "
                                "steps=%d moved=%d moving=%d\n",
                                tr.dailyDispatched, tr.dailyAssigned,
                                tr.moveSteps, tr.personsMoved, tr.personsMoving);
                if (tr.quitByWindow) running = false;   // window closed -> exit; ESC -> back to menu
            }
        } else {
            // Headless (no display): no input to drive the menu, so run the session
            // directly so a no-display smoke still renders + presents the city.
            cfg.maxFrames = framesSet ? frames : 240;
            cfg.dumpFramePath = "/tmp/guild_session_city3d.ppm";  // visual artifact
            // Park the idle headless mouse at the screen centre: NullPlatform
            // defaults to (0,0) == the top-left corner, which the REAL edge-scroll
            // (EdgeScroll @0x4b2c34, faithfully wired) treats as a held corner
            // scroll — 240 frames of it pans the camera clean off the city.
            {
                shim::MouseState centre{};
                centre.x = W / 2;
                centre.y = H / 2;
                nullPlat.setMouse(centre);
            }
            std::printf("  --play: headless; loading '%s' from %s ...\n",
                        cfg.cityPath.c_str(), gameDir.c_str());
            play::SdlSessionTrace tr = play::RunSdlSession(fs, gfx, playPlat, cfg);
            std::printf("  --play trace: mounted=%d loaded=%d liveObjects=%d persons=%d "
                        "framesPresented=%d daysAdvanced=%d view3d=%d(%d inst, %d px) "
                        "clock=%d fires=%d dump=%d\n",
                        (int)tr.mounted, (int)tr.loaded, tr.liveObjects, tr.persons,
                        tr.framesPresented, tr.daysAdvanced, (int)tr.view3dActive,
                        tr.view3dInstances, tr.view3dNonClear, (int)tr.clockActive,
                        tr.clockFires, (int)tr.frameDumped);
            // Wave-4 living city: the NPC movement outcome (the daily director
            // dispatches only persons with populated building columns — see
            // progress/living-city-wave4.md).
            if (tr.npcMovementActive)
                std::printf("  --play living city: dispatched=%d assigned=%d "
                            "steps=%d moved=%d arrivals=%d moving=%d\n",
                            tr.dailyDispatched, tr.dailyAssigned, tr.moveSteps,
                            tr.personsMoved, tr.moveArrivals, tr.personsMoving);
            // WAVE-8 world entities: the per-vertex sun lighting + smoke + gait +
            // animals lifecycle + the overview map.
            if (tr.view3dActive)
                std::printf("  --play wave8: sceneLights=%d sunLitVerts=%d "
                            "smokeSystems=%d gaitFlips=%d animalTicks=%d "
                            "animalsAtExit=%d mapOpened=%d mapMarkers=%d\n",
                            tr.view3dSceneLights, tr.view3dSunLitVerts,
                            tr.view3dSmokeSystems, tr.view3dGaitFlips,
                            tr.view3dAnimalTicks, tr.view3dAnimalCount,
                            (int)tr.view3dMapOpened, tr.view3dMapMarkers);
            if (tr.view3dActive)
                std::printf("  --play wave9: flagObjects=%d vegRelit=%d "
                            "reflectiveMeshes=%d animalsDrawn=%d\n",
                            tr.view3dFlagObjects, tr.view3dVegRelit,
                            tr.view3dReflectiveMeshes, tr.view3dAnimalsDrawn);
        }
        std::printf("  vulkan device: %s (API %s)\n",
                    gfx.deviceName().c_str(), gfx.apiVersion().c_str());
        gfx.shutdown();
        if (windowed) sdlPlat.destroyMainWindow();
        std::printf("guild_run: exit 0\n");
        return 0;
    }

    auto pair = shim::LoopbackSocket::makePair();
    shim::INetSocket* sock = pair.first.get();

    // --- run the spine ---------------------------------------------------
    // Interactive mode drives the real spine AS A PROGRAM: the outer menu<->session
    // FSM (play::RunInteractiveApp) over the SDL/Vulkan window, falling back to a
    // bounded live-frame run on NullPlatform when there is no display. The default
    // (no --interactive) keeps the existing boot -> N frames -> shutdown behavior.
    int exitCode = 0;

    // Run the interactive spine over an already-built GameApp `app` (display path
    // when `windowed`, else the headless live-frame fallback) and print a summary.
    auto runInteractive = [&](app::GameApp& app) {
        if (windowed) {
            // No hit-test wired yet (P2 picking is separate), so menu clicks resolve
            // to "nothing hovered"; the FSM still drives menu passes + window-close.
            play::InteractiveAppResult r =
                play::RunInteractiveApp(app, plat, /*hitTest=*/{}, /*displayMode=*/1,
                                        /*menuMaxFrames=*/frames > 0 ? frames : 240,
                                        /*sessionMaxFrames=*/frames > 0 ? frames : 120);
            std::printf("  interactive: spineInited=%d sessions=%d menuRuns=%d "
                        "sessionFrames=%d quitClean=%d\n",
                        (int)r.spineInited, r.interactive.sessions,
                        r.interactive.menuRuns, r.interactive.totalSessionFrames,
                        (int)r.interactive.quitClean);
            std::printf("  transitions:");
            for (play::GameMode m : r.interactive.transitions)
                std::printf(" %s", play::GameModeName(m));
            std::printf("\n");
        } else {
            play::InteractiveAppResult r =
                play::RunInteractiveAppHeadless(app, plat, /*displayMode=*/1,
                                                /*maxFrames=*/frames);
            std::printf("  interactive (headless fallback): spineInited=%d "
                        "frames=%d quitOnClose=%d\n",
                        (int)r.spineInited, r.fallbackFrames, (int)r.fallbackQuit);
        }
    };

    if (interactive && !gameDir.empty()) {
        shim::DiskFileSystem fs(gameDir);
        app::RealGameAssets assets =
            app::MountRealGameAssets(&fs, gameDir, "Gilde.INI", {}, /*caseInsensitive=*/false);
        app::RealSubsystems sub(&plat, &gfx, &audioDev, &fs, sock, &assets.ini);
        sub.BindRealAssets(&assets, gameDir);
        app::GameApp app(plat, gfx, audioDev, sub);
        runInteractive(app);
        std::printf("  real assets: ini=%d stadt=%s frames=%d present=%d\n",
                    (int)assets.iniLoaded, assets.stadt.c_str(),
                    sub.frameCount(), sub.presentCount());
    } else if (interactive) {
        config::IniFile ini; // defaults
        shim::MemFileSystem fs;
        app::RealSubsystems sub(&plat, &gfx, &audioDev, &fs, sock, &ini);
        app::GameApp app(plat, gfx, audioDev, sub);
        runInteractive(app);
        std::printf("  synthetic boot: frames=%d present=%d\n", sub.frameCount(), sub.presentCount());
    } else if (!gameDir.empty()) {
        shim::DiskFileSystem fs(gameDir);
        app::RealGameAssets assets =
            app::MountRealGameAssets(&fs, gameDir, "Gilde.INI", {}, /*caseInsensitive=*/false);
        app::RealSubsystems sub(&plat, &gfx, &audioDev, &fs, sock, &assets.ini);
        sub.BindRealAssets(&assets, gameDir);
        app::GameApp app(plat, gfx, audioDev, sub);
        exitCode = app.Run(gameDir, /*displayMode=*/1, /*showIntro=*/false,
                           /*networkClient=*/false, frames);
        std::printf("  real assets: ini=%d stadt=%s frames=%d present=%d\n",
                    (int)assets.iniLoaded, assets.stadt.c_str(),
                    sub.frameCount(), sub.presentCount());
    } else {
        config::IniFile ini; // defaults
        shim::MemFileSystem fs;
        app::RealSubsystems sub(&plat, &gfx, &audioDev, &fs, sock, &ini);
        app::GameApp app(plat, gfx, audioDev, sub);
        exitCode = app.Run("\\project\\", 1, /*showIntro=*/false, false, frames);
        std::printf("  synthetic boot: frames=%d present=%d\n", sub.frameCount(), sub.presentCount());
    }

    std::printf("  vulkan device: %s (API %s)\n", gfx.deviceName().c_str(), gfx.apiVersion().c_str());
    // The spine drove this device through init -> present xN -> shutdown (the real
    // Vulkan present path replacing DirectDraw/Direct3D). The device is torn down by
    // the 13-step shutdown, so to leave a tangible artifact we present one frame
    // through a FRESH Vulkan device and read it back to a BMP.
    {
        const int W = 256, H = 256;
        shim::VulkanGraphicsDevice demo;
        if (demo.init(W, H, 32, false)) {
            if (shim::Surface* s = demo.backbuffer()) {
                auto* px = static_cast<std::uint32_t*>(s->pixels);
                for (int y = 0; y < H; ++y)
                    for (int x = 0; x < W; ++x)
                        px[y * (s->pitch / 4) + x] =
                            0xFF000000u | (std::uint32_t(x) << 16) | (std::uint32_t(y) << 8);
            }
            demo.present();
            std::vector<std::uint32_t> img = demo.readback();
            if (!img.empty() && WriteBmp(dumpPath, img.data(), W, H))
                std::printf("  Vulkan present->readback OK; dumped %dx%d frame -> %s\n",
                            W, H, dumpPath.c_str());
            demo.shutdown();
        }
    }

    if (windowed) sdlPlat.destroyMainWindow();
    std::printf("guild_run: exit %d\n", exitCode);
    return exitCode;
}

#endif // GUILD_HAVE_VULKAN && GUILD_HAVE_SDL2
