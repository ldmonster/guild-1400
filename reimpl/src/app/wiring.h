#pragma once
// gilde.exe — app spine wiring (guild::app).
//
// INTEGRATION GLUE, not a translation. `RealSubsystems` is a concrete
// `ISubsystems` (gamelogic.h) whose methods forward to the actual reconstructed
// subsystem modules under src/ (config, io, audio, net, sim, world, render, gui,
// mem) wherever a real entry point exists, and otherwise record a clearly-marked
// stub so the spine's init->frame->shutdown lifecycle runs end-to-end against
// real code. No subsystem logic is modified here; only thin adapter calls.
//
// All OS resources are reached exclusively through the shim::I* interfaces
// (IPlatform/IGraphicsDevice/IAudioDevice/IFileSystem/INetSocket) which are
// injected; a default headless wiring is provided by RunHeadless() using the
// src/shim_impl backends.
//
// ===========================================================================
// WIRED vs STUBBED — which ISubsystems hook reaches real reconstructed code.
// ===========================================================================
//  Legend: REAL = forwards to a translated module function/method;
//          STUB = records the call (observable) but has no real entry point yet
//                 (the original Win32/engine routine is not reconstructed, or the
//                 hook is coarser than any single translated function).
//
//  ---- init: InitSubsystemsAndMovieDll -------------------------------------
//   errorLogInit                 REAL  config::FormatMessage via recording ILogSink
//   memoryInitTracker            REAL  mem::MemoryTracker::Init
//   memPoolStartupStack          REAL  mem::MemPoolResetBlocks (primes the pool)
//   loadMovieDll                 STUB  LoadLibrary("moveahead.dll") not modeled
//   fileCreateDirectory          REAL  shim::IFileSystem::exists + open("w") (dir ensure)
//   vfsInit                      REAL  io::VfsInit(fs, caseInsensitive)
//                                      [real-asset: bound to the mounted disk VFS]
//   timeBaseStartTimer           REAL  crt::TimeBase::StartTimer (shim clock)
//  ---- render init: InitDisplayAndPaths ------------------------------------
//   configReadGfxAndSound        REAL  config::ReadGfxAndSoundSettings(ini,...)
//                                      [real-asset: reads the parsed real Gilde.INI]
//   renderEnumDisplayModes       STUB  VIBE_Render_EnumDisplayModes not reconstructed
//   renderInitEngineDevice       REAL  render::PresentFrame scene-init + present seed
//   universeCreateDefaultCameras REAL  sim::UniverseCreateDefaultCameras (camera
//                                      bootstrap: spawn+link MegaCam @0x5b5f48; the
//                                      object spawn/link are inert render-hook leaves)
//   inputDirectInputInit         REAL  gui::ResetInputState (DInput latch state reset)
//   renderSetAssetPaths          STUB  path globals not reconstructed
//   renderApplyGfxSettings       STUB  VIBE_Render_ApplyGfxSettings not reconstructed
//   guiLoadGfxFile               REAL  render::ShapeBankAddShape + gui::text::TextDb load
//                                      [real-asset: gui::Form_LoadFromFile(gfx/gilde.gfx),
//                                       1806 real gfx objects]
//   widgetInitSystem             REAL  gui::ResetWindows + ResetInputState + ResetHudSlots
//  ---- engine init: InitEngineAndScriptCommands ----------------------------
//   textLoadDefinitionFile       REAL  gui::text::TextDb::Add + FindIndex (text DB load)
//   netConnectToServer           REAL  net::NetTransport::ConnectToServer
//   commandQueueInitAndSync      REAL  sim::CommandQueue::Init
//   worldLoadBuildingAndObjectData REAL world::CityInitParameterTable + sim::ResetEntityArrays
//                                      [real-asset: world::WorldLoadBuildingAndObjectData
//                                       (data/A_Geb.dat 72 + A_Obj.dat 731) + the
//                                       configured <Stadt>.cty seed load]
//   buildingComputeMarketPrices  REAL  world::CityInitParameterTable + Production-
//                                      ComputeOutputOverTime (economy price input)
//   audioStartupMilesDriver      STUB  Miles driver bring-up (device via shim)
//   soundLibInit                 REAL  audio::SoundSystem::init (voices/ch/rate)
//   sound3dInitPool              REAL  audio::Sound3dPool ctor + findFreeSlot
//                                      (84-byte spatial-voice entry pool init)
//   soundWaveInitSineTables      REAL  audio::InitSineTables (d3sndw sine-table
//                                      build, VIBE_SoundWave_InitSineTables 0x424d40)
//   soundLoadSampleBank          REAL  audio::SampleBank::addSample (seeds the
//                                      market-ambience sample; .sbf parse deferred)
//   soundPreloadIncludeFile      REAL  audio::SampleBank::addSample (per-bank load
//                                      the include_sfx.ini preload drives)
//   soundInitMusicThread         REAL  audio::music_world track-table seed (outdoor)
//   audioApplyVolumeSettings     REAL  audio::MusicPlayer::applyMasterVolume
//   scriptRegisterCommands       REAL  sim::ScriptVm register/invoke command ABI
//  ---- intro movie ---------------------------------------------------------
//   moviePlayIntroSequence       STUB  movie playback not reconstructed
//   movieDllExit                 STUB  mov_Exit_ export not reconstructed
//  ---- per-frame steps (RunFrameLoop) --------------------------------------
//   inputLatchAndPump            REAL  shim::IPlatform::pumpMessages + getMouse
//   widgetDispatchMouseClick     REAL  gui::ResolveClickedSlot + gui::RouteClick
//   hudHandleMouseClick          REAL  gui::Hud_DispatchClick (slot resolve + classify)
//   inputCommandPoll             REAL  sim::CutsceneTable Alloc/FindLowestPriority
//                                      (cutscene-poll head of ProcessActive)
//   commandNetworkPump           REAL  net::NetTransport::SendPacket/ReceivePacket
//   scriptStepAllActive          REAL  sim::StepAllActive(slots, step)
//   gameObjectDispatchInteractions REAL sim::InteractionDispatch + render::FadeAlpha
//   renderMainViewFrame          REAL  render::RenderMainViewFrame(FrameState,hooks)
//                                      with the REAL draw-list pipeline installed on
//                                      the FrameHooks: sceneWalk -> render::Project-
//                                      VerticesToScreen + RadixSortDrawList (build/
//                                      project/cull/sort), flushDrawList -> render::
//                                      RasterizeMeshList (rasterize) -> PresentFrame.
//                                      The two FrameHooks were null(mock) before.
//   weatherUpdateSky             REAL  render::WeatherIntensity/CategoryFor/Cloud-
//                                      ScrollMagnitude/SelectCloudLayerIndex
//   dayCycleAndOutdoorMusic      REAL  render::UpdateBrightness + the full per-frame
//                                      AUDIO TICK (app::AudioTick, the audio block of
//                                      VIBE_GameLogic_RunFrameLoop @0x4c09a0): real
//                                      audio::Sound3dPool::updateAll (3D positional),
//                                      VoiceQueue::processNext (speech),
//                                      music_world::UpdateOutdoorTrackPlayback (music),
//                                      voice recycle, + the market-ambience SFX trigger
//                                      app::StartMarketLoop (VIBE_Ambient_StartMarketLoop
//                                      @0x582858). Was music-only before this pass.
//   hudSelectionAndTargets       REAL  gui::StatusText_Register + DamageLabel_Register
//   tooltipDispatch              REAL  gui::Tooltip_ClassifySubject + SelectBuilder
//   hudLabelsAndCaption          REAL  gui::Hud_LabelLayout + Hud_ButtonRowLayout
//   cameraCombatScroll           REAL  app::ResolveCombatScroll (combat edge-scroll
//                                      decision core, VIBE_Camera_UpdateCombatScroll
//                                      0x487b2c; the arrow blit + camera pan are the
//                                      render leaves, still deferred)
//   presentFrame                 REAL  render::PresentFrame(gfx, mode, state)
//   optionsChatHotkeyPanels      REAL  gui::ChatConsole SetChannel/Submit (real
//                                      colour-prefix line assemble; options/hotkey
//                                      panels still need live UI state)
//   autosaveAndNetWait           REAL  net::RunNetworkLobby (the multiplayer-lobby
//                                      ready/ship/barrier handshake over the real
//                                      CommandQueue) + net::AdvertiseAndDiscover (a
//                                      real host-advertise -> client-discover round-
//                                      trip through the discovery siblings). The
//                                      autosave half still needs live world state.
//   characterCollectByOwner      REAL  sim::CharacterUpdate (live-actor frame driver)
//   quickJumpContact             REAL  sim::GameObjectResolveEntityById (real
//                                      object/scene/person record probe; the
//                                      camera snap still needs live camera state)
//  ---- shutdown teardown (13 steps) ----------------------------------------
//   tdGameShutdownSubsystems     REAL  audio::SoundSystem::shutdown (sound teardown)
//   tdWidgetShutdownSystem       REAL  gui::ResetWindows + ResetInputState + ResetHudSlots
//   tdConfigWriteGfxSettings     REAL  app::ConfigWriteGfxSettings
//                                      (VIBE_Config_WriteGfxSettings 0x56af54 —
//                                       1:1 settings serializer; OS Write-
//                                       PrivateProfileStringA leaf -> record sink)
//   tdGameStateFreeAllResources  REAL  sim::ResetEntityArrays + mem::MemoryTracker probe
//   tdUniverseSwitchActiveSlot0  REAL  sim::UniverseSwitchActiveSlot(0, quiet=1) —
//                                      the byte-exact scene-slot swap @0x5b4a24 (the
//                                      DDraw present tail is the inert render-hook leaf)
//   tdTableResetLightmaps        STUB  not reconstructed
//   tdRenderShutdownEngine       REAL  shim::IGraphicsDevice::shutdown
//   tdInputDirectInputShutdown   STUB  DirectInput shutdown not reconstructed
//   tdTimeBaseStopTimer          REAL  crt::TimeBase::StopTimer
//   tdVfsShutdown                REAL  io::VfsShutdown
//   tdMemPoolShutdownStack       REAL  mem::MemPoolFreeAll
//   tdMemoryShutdownTracker      REAL  mem::MemoryTracker::Shutdown
//   tdErrorLogShutdown           REAL  config::FormatMessage via recording ILogSink
//
// ACCESSORS ADDED to reconstructed modules: none. (Everything wireable was
// reachable through existing public headers / inspection getters.)
//
// WIRED COUNT: 55/64 hooks reach real reconstructed code (unchanged this pass).
//
// LATEST PASS — SIM-LEAF DISPATCH WIRING (no hook flips, but the per-frame/per-turn
// dispatch now reaches the real reconstructed sim leaves it always logically drove).
// The four cross-module installers — sim::InstallRealSimHooks{,2,3,4} (src/sim/
// real_hooks{,2,3,4}.{h,cpp}) — were already reconstructed but were NEVER invoked by
// the app spine, so the simulation dispatch behind several already-"REAL" hooks ran
// against INERT default sim hooks (the command-emit / entity-query / command-APPLY
// entity-mutator / charaction step handlers). commandQueueInitAndSync (the faithful
// engine/command-system init point, where the original binds the command codec + the
// per-opcode apply jump table funcs_4941F4 @0x631298) now installs all four waves and
// registers the apply-3 jump table onto the owned CommandQueue:
//   commandQueueInitAndSync  -> sim::InstallRealSimHooks{,2,3,4}() + RegisterApply-
//                               Handlers3(cmdQueue_). After this, an applied packet
//                               routes through the REAL reconstructed sim leaves over
//                               the SHARED entity arrays (g_sceneNodes/g_buildingPersons/
//                               g_persons): command-emit -> command_codec; entity query
//                               -> the real entity arrays; command-APPLY object-stock /
//                               building-free / person-create -> the real object/
//                               building/person modules; charaction steps -> the real
//                               character.cpp catalog. (Previously these were inert.)
// And gameObjectDispatchInteractions gained a per-turn command-apply step driving ONE
// object-resolve command (opcode 0x4A) through the now-wired entity resolver:
//   gameObjectDispatchInteractions -> applies opcode 0x4A (ExDeselectObject) via the
//                               wired SetObjectFindHook -> sim::GameObjectResolveEntityById
//                               (a pure entity QUERY — mutates no shared state, so it
//                               composes with every other suite). Headless reports "no
//                               live record" (the cold-world resolver outcome), but the
//                               real reconstructed resolver ran. simHooksInstalled() /
//                               cmdApplyReachedReal() / cmdApplyResult() observe it.
// The remaining sim-leaf hooks with no reconstructed target stay inert (real_hooks{,2,3,4}
// LIST them: office/law/privilege/combat-slot/physics leaves, ambiguous field readers).
//
// An earlier pass moved 2 hooks STUB -> REAL — the UNIVERSE (scene-slot) wiring:
//   universeCreateDefaultCameras -> sim::UniverseCreateDefaultCameras @0x5b5f48 (the
//                              camera bootstrap: spawn + link the MegaCam, run after a
//                              cold sim::ResetUniverse; the object spawn/position/link
//                              are render/scene leaves routed through the module's
//                              inert UniverseRenderHooks, but the camera-handle
//                              bookkeeping g_megaCam et al. is fully reconstructed).
//   tdUniverseSwitchActiveSlot0 -> sim::UniverseSwitchActiveSlot(0, quiet=1) @0x5b4a24
//                              (the shutdown step that makes scene slot 0 active: the
//                              byte-exact per-slot save/restore + the active-slot id
//                              dword_649D60 are reconstructed; the heightmap rebuild /
//                              fog config / present-flip DDraw tail are inert render-
//                              hook leaves).
//
// An earlier pass moved 1 hook STUB -> REAL — the NET-LOBBY wiring:
//   autosaveAndNetWait      -> net::RunNetworkLobby + net::AdvertiseAndDiscover
//                              (the multiplayer-lobby ready/ship/barrier handshake
//                              over the owned CommandQueue, plus a real host-
//                              advertise -> client-discover round-trip through the
//                              discovery siblings on an in-process datagram bus).
//                              Built on fresh 1:1 translations of the lobby setup
//                              functions VIBE_Menu_RunHostNetworkSetup @0x528dac /
//                              VIBE_Net_LoadNetworkSaveProfile @0x528f24 and the
//                              lobby spine of VIBE_Net_StartNetworkGame @0x503f78 /
//                              VIBE_Net_LoadSavedNetworkGame @0x50442c (src/net/lobby).
//
// An earlier pass moved 2 hooks STUB -> REAL (the AUDIO subsystem wiring):
//   soundLoadSampleBank     -> audio::SampleBank::addSample (seeds the market-
//                              ambience sample through the real bank index; the
//                              on-disk .sbf parse VIBE_Sound_LoadSampleBank @0x446b2c
//                              is deferred — needs a real sample asset).
//   soundPreloadIncludeFile -> audio::SampleBank::addSample (the per-bank load the
//                              include_sfx.ini preload VIBE_Sound_PreloadFromInclude-
//                              File @0x52f154 drives; the .ini parse is deferred).
// It also UPGRADED dayCycleAndOutdoorMusic from a music-only hook to the full
// per-frame AUDIO TICK by translating a fresh app-side driver 1:1:
//   app::AudioTick           — the audio block of VIBE_GameLogic_RunFrameLoop
//                              @0x4c09a0 (the gated dword_63C900/63C904/63C8F8
//                              dispatch: Sound3d_UpdateAll @0x424790, VoiceQueue_
//                              ProcessNext @0x57eff0, Music_UpdateOutdoorTrack-
//                              Playback @0x581594, Sound_UpdateVoices @0x445f00).
//   app::StartMarketLoop     — VIBE_Ambient_StartMarketLoop @0x582858 (the looping
//   app::StopMarketLoop         market-ambience 3D SFX trigger) / @0x5828bc.
// (Prior pass moved cameraCombatScroll -> app::ResolveCombatScroll @0x487b2c and
//  soundWaveInitSineTables -> audio::InitSineTables @0x424d40.)
// (Prior pass moved tdConfigWriteGfxSettings -> app::ConfigWriteGfxSettings, a 1:1
//  translation of VIBE_Config_WriteGfxSettings @0x56af54 + its itoa helper
//  VIBE_AnimationState_Update @0x5d92ec; the OS WritePrivateProfileStringA call is
//  the genuine leaf, routed to a record sink.)
// (Prior pass moved sound3dInitPool, optionsChatHotkeyPanels, quickJumpContact.
//  The per-frame command-apply leaves — object stock move, building free/remove,
//  person spawn — are wired in the SIM hook installer InstallRealSimHooks4, see
//  sim/real_hooks4.h.)
//
// REAL-ASSET BOOT (BindRealAssets / RunHeadlessRealAssets): when a real "Die
// Gilde" install is mounted, four of the REAL hooks above upgrade from synthetic
// in-memory stand-ins to consuming the actual shipped assets over the bound VFS:
//   vfsInit               -> the mounted disk VFS (Resources/*.BIN indexed)
//   configReadGfxAndSound -> the parsed real Gilde.INI (Stadt=Augsburg, etc.)
//   guiLoadGfxFile        -> gui::Form_LoadFromFile("gfx/gilde.gfx") (1806 objs)
//   worldLoadBuildingAndObjectData -> world::WorldLoadBuildingAndObjectData(
//                            "data/") (A_Geb 72 + A_Obj 731) + the <Stadt>.cty load
// No hook count changes; the same REAL hooks now run over REAL bytes on the
// real-asset path. The stubs below are install-independent (OS/DLL/DDraw/DInput
// leaves) and remain stubbed on both paths.
// REMAINING 9 stubs: loadMovieDll, renderEnumDisplayModes, renderSetAssetPaths,
// renderApplyGfxSettings, audioStartupMilesDriver, moviePlayIntroSequence,
// movieDllExit, tdTableResetLightmaps, tdInputDirectInputShutdown
// — each is a genuine OS/DLL/DDraw/DInput leaf (LoadLibrary, EnumDisplayModes,
// DirectInput device acquire/release, Miles driver bring-up, movie playback) or a
// not-reconstructed engine teardown over engine-private global arrays:
//   renderEnumDisplayModes  VIBE_Render_EnumDisplayModes — DDraw mode enumeration.
//   renderApplyGfxSettings  VIBE_Render_ApplyGfxSettings — DDraw device reconfigure.
//   renderSetAssetPaths     path-global writes (not reconstructed).
//   tdTableResetLightmaps   VIBE_Table_ResetLightmaps @0x42e19c — frees the four
//                           279-entry lightmap pointer arrays (dword_75E9F0/75EE4C/
//                           75F2A8/75F704) via VIBE_Memory_FreeDebug; those engine-
//                           private DDraw lightmap arrays are not reconstructed in
//                           src/, so there is no real sibling to wire against.
// Each has no portable behavior to reconstruct, or is a hook coarser than any single
// translated function. They stay shim-routed stubs on BOTH the synthetic and the
// real-asset boot paths.

#include "app/gamelogic.h"

#include "config/errorlog.h"
#include "config/ini.h"
#include "io/vfs.h"
#include "audio/sound.h"
#include "net/transport.h"
#include "net/lobby.h"        // RunNetworkLobby + discovery advert (real lobby spine)
#include "sim/command.h"
#include "sim/script_vm.h"
#include "sim/gametime.h"
#include "sim/interaction.h"
#include "sim/cutscene.h"
#include "mem/heap.h"
#include "mem/memory_debug.h"
#include "mem/mempool.h"
#include "render/frame.h"
#include "render/present.h"
#include "render/daycycle.h"
#include "render/fade.h"
#include "render/weather.h"
#include "audio/music_world.h"
#include "audio/sound3d.h"
#include "crt/time.h"
#include "gui/text/textdb.h"
#include "gui/chatconsole.h"
#include "app/config_write.h"   // ConfigWriteGfxSettings (real settings serializer)
#include "app/combat_scroll.h"  // ResolveCombatScroll (real combat edge-scroll core)
#include "audio/soundwave.h"     // InitSineTables (real d3sndw sine-table init)
#include "app/audio_tick.h"      // AudioTick + Start/StopMarketLoop (real audio drivers)

#include "shim/IFileSystem.h"
#include "shim/IGraphicsDevice.h"
#include "shim/INetSocket.h"
#include "shim/IPlatform.h"

#include "app/real_boot.h"   // RealGameAssets (real-asset boot mode)

#include <memory>
#include <string>
#include <vector>

namespace guild::app {

// ===========================================================================
// RealSubsystems — concrete ISubsystems forwarding to the reconstructed modules.
// ---------------------------------------------------------------------------
// Owns the real module instances (the originals were process globals); shim
// resources are injected by pointer. A `Recorder` lets tests observe exactly
// which hooks fired and which reached real code without reaching into private
// module state.
class RealSubsystems final : public ISubsystems {
public:
    // Coarse classification recorded per hook invocation.
    enum class Kind { Real, Stub };
    struct Event { const char* hook; Kind kind; };

    // `fs`/`gfx`/`net` may be null; the affected hooks then degrade to STUB
    // (recorded) so the spine still runs. `ini` supplies config; if null an
    // empty provider (all defaults) is used. `plat` drives the per-frame pump.
    RealSubsystems(shim::IPlatform* plat, shim::IGraphicsDevice* gfx,
                   shim::IAudioDevice* audioDev, shim::IFileSystem* fs,
                   shim::INetSocket* net, const config::IProfileProvider* ini);

    // ---- recording / inspection -------------------------------------------
    const std::vector<Event>& events() const { return events_; }
    bool fired(const char* hook) const;
    bool firedReal(const char* hook) const;
    int  presentCount() const { return presentCount_; }
    int  frameCount() const { return frameCount_; }
    bool quitRequested() const override { return quitRequested_; }

    // ---- inspectable real subsystem state ---------------------------------
    const config::GfxSettings&   gfx()   const { return gfx_; }
    const config::SoundSettings& sound() const { return snd_; }
    const config::GameSettings&  game()  const { return game_; }
    audio::SoundSystem&          soundSystem() { return sound_; }
    sim::CommandQueue&           commandQueue() { return cmdQueue_; }
    net::NetTransport&           transport() { return transport_; }
    mem::MemoryTracker&          tracker() { return tracker_; }
    bool memoryTrackerInited() const { return trackerInited_; }
    bool vfsInited() const { return vfsInited_; }
    bool soundInited() const { return soundInited_; }
    int  brightness() const { return brightness_; }
    bool timerRunning() const { return timerRunning_; }
    int  textDbCount() const { return textDb_.Count(); }
    int  shapeBankCount() const;
    int  lastClickedId() const { return lastClickedId_; }
    int  scriptCmdResult() const { return scriptCmdResult_; }
    bool fadeDone() const { return fadeDone_; }
    // Third-wave inspection: weather intensity, HUD layout, character/cutscene/
    // economy state advanced by the newly-wired per-frame + init steps.
    int  weatherIntensity() const { return weatherIntensity_; }
    int  hudLabelX() const { return hudLabelX_; }
    int  characterUpdates() const { return characterUpdates_; }
    int  cutscenesProcessed() const { return cutscenesProcessed_; }
    int  marketWorkMinutes() const { return marketWorkMinutes_; }
    int  musicTrackCount() const { return musicDir_.table.size() ? static_cast<int>(musicDir_.table.size()) : 0; }
    int  musicHandle() const { return musicDir_.currentTrackHandle; }
    // Fourth-wave inspection: spatial-voice pool capacity (sound3dInitPool), chat
    // console line count (optionsChatHotkeyPanels), and the quick-jump resolve
    // outcome (quickJumpContact).
    int  sound3dCapacity() const { return sound3dCapacity_; }
    int  chatLineCount() const { return chatLines_; }
    bool quickJumpResolved() const { return quickJumpResolved_; }
    // Fifth-wave inspection: settings keys written by tdConfigWriteGfxSettings
    // (real config serializer, VIBE_Config_WriteGfxSettings), and the last
    // [Game]/stadt value the serializer emitted.
    int  settingsKeysWritten() const { return settingsKeysWritten_; }
    const std::string& writtenStadt() const { return writtenStadt_; }
    // Sixth-wave inspection: the combat-scroll decision the cameraCombatScroll step
    // resolved (real ResolveCombatScroll), and the sine-table length the
    // soundWaveInitSineTables init step built (real audio::InitSineTables).
    int  combatScrollRightCode() const { return combatScrollRight_; }
    int  combatScrollBottomCode() const { return combatScrollBottom_; }
    int  sineTableCount() const { return sineTableCount_; }
    // Audio-tick inspection: the per-frame audio driver (AudioTick) outcomes and
    // the market-ambience SFX trigger (Start/StopMarketLoop) state.
    int  audioTicks() const { return audioTicks_; }
    bool audioSound3dRan() const { return lastAudioTick_.sound3dRan; }
    bool audioVoicesRan() const { return lastAudioTick_.voicesRan; }
    int  audioMusicAction() const { return static_cast<int>(lastAudioTick_.music); }
    bool marketLoopStarted() const { return marketLoopStarted_; }
    bool marketLoopActive() const { return marketLoop_ != nullptr; }
    // Net-lobby inspection: the autosaveAndNetWait hook now drives the REAL lobby
    // handshake spine (net::RunNetworkLobby) over the owned CommandQueue + a real
    // discovery advert round-trip. Non-zero/true once the net-wait block has run.
    bool lobbyReady() const { return lobbyResult_.ready; }
    bool lobbySavedSent() const { return lobbyResult_.savedSent; }
    int  lobbyDiscovered() const { return lobbyDiscovered_; }
    // Seventh-wave inspection: the real per-frame draw-list pipeline outcomes
    // (renderMainViewFrame now drives sceneWalk -> build/project/cull/sort and
    // flushDrawList -> rasterize across the real render siblings). Non-zero once a
    // world frame has run.
    int  drawListPolys() const { return lastDrawListPolys_; }
    int  rasterTriangles() const { return lastRasterTris_; }
    // The rasterized software framebuffer (16bpp) the real flush wrote, for the
    // e2e BMP dump / pixel assert. Null until a world frame has run.
    const void* frameBufferPixels(int* w, int* h, int* pitch) const;
    // Eighth-wave inspection: the universe/scene-slot bring-up + teardown the
    // newly-wired universeCreateDefaultCameras / tdUniverseSwitchActiveSlot0 hooks
    // run (real sim::UniverseCreateDefaultCameras / UniverseSwitchActiveSlot). The
    // MegaCam handle is non-zero once cameras were created; the active-slot flag is
    // true once the shutdown swap to slot 0 ran.
    unsigned universeMegaCam() const { return universeMegaCam_; }
    bool universeSwitchedToSlot0() const { return universeSwitchedToSlot0_; }
    // Ninth-wave inspection: the SIM-LEAF wiring (commandQueueInitAndSync now calls
    // InstallRealSimHooks{,2,3,4} + RegisterApplyHandlers3, so the per-frame/per-turn
    // command-apply dispatch reaches the real reconstructed sim leaves). True once the
    // installers ran. cmdApplyReachedReal() is true once the per-turn command-apply
    // step dispatched a packet (opcode 0x4A) through the wired real entity-resolve leaf
    // (sim::GameObjectResolveEntityById); cmdApplyResult() is that apply's return code
    // (1 = "no live record at the queried id", the cold-world resolver outcome).
    bool simHooksInstalled() const { return simHooksInstalled_; }
    bool cmdApplyReachedReal() const { return cmdApplyReachedReal_; }
    int  cmdApplyResult() const { return cmdApplyResult_; }

    // ---- REAL-ASSET boot mode ---------------------------------------------
    // When a real "Die Gilde" install is mounted (BindRealAssets), the file-layer
    // hooks (vfsInit / configReadGfxAndSound / guiLoadGfxFile / worldLoad... and
    // the configured-city load) consume the REAL loaders over REAL bytes instead
    // of the synthetic in-memory stand-ins. `assets` MUST outlive this object and
    // its VFS must already be bound to `fs` (MountRealGameAssets did both); `fs`
    // is the same disk filesystem the .dat / .gfx / .cty opens resolve through.
    void BindRealAssets(const RealGameAssets* assets, const std::string& gameDir);
    bool realAssetsMode() const { return realAssets_; }
    // Real-asset load outcomes (only meaningful in real-asset mode).
    int  gfxObjectCount() const { return gfxObjectCount_; }     // gilde.gfx objects (1806)
    int  buildingTypeCount() const { return buildingTypeCount_; } // A_Geb.dat (72)
    int  sceneTypeCount() const { return sceneTypeCount_; }       // A_Obj.dat (731)
    bool cityLoaded() const { return cityLoaded_; }               // <stadt>.cty parsed
    const std::string& cityName() const { return cityName_; }     // parsed header name

    // ---- ISubsystems: init -------------------------------------------------
    void errorLogInit() override;
    void memoryInitTracker(int bytes) override;
    void memPoolStartupStack(int size) override;
    bool loadMovieDll() override;
    void fileCreateDirectory(const std::string& path) override;
    void vfsInit(const std::string& root) override;
    void timeBaseStartTimer(int a, int b) override;

    // ---- ISubsystems: render init -----------------------------------------
    void configReadGfxAndSound() override;
    bool renderEnumDisplayModes() override;
    bool renderInitEngineDevice(int w, int h, int bpp, bool fullscreen) override;
    void universeCreateDefaultCameras() override;
    void inputDirectInputInit(int mode) override;
    void renderSetAssetPaths() override;
    void renderApplyGfxSettings() override;
    bool guiLoadGfxFile(const std::string& name) override;
    void widgetInitSystem() override;

    // ---- ISubsystems: engine init -----------------------------------------
    bool textLoadDefinitionFile(const std::string& path) override;
    void netConnectToServer(const std::string& host, int port) override;
    void commandQueueInitAndSync() override;
    void worldLoadBuildingAndObjectData(const std::string& path) override;
    void buildingComputeMarketPrices() override;
    void audioStartupMilesDriver() override;
    bool soundLibInit(int voices, int channels, int rate) override;
    void sound3dInitPool(int n) override;
    void soundWaveInitSineTables() override;
    void soundLoadSampleBank(const std::string& path) override;
    void soundPreloadIncludeFile(const std::string& path) override;
    void soundInitMusicThread(int rate) override;
    void audioApplyVolumeSettings() override;
    void scriptRegisterCommands() override;

    // ---- ISubsystems: intro movie -----------------------------------------
    void moviePlayIntroSequence() override;
    void movieDllExit() override;

    // ---- ISubsystems: per-frame -------------------------------------------
    void inputLatchAndPump() override;
    void widgetDispatchMouseClick() override;
    void hudHandleMouseClick() override;
    void inputCommandPoll() override;
    void commandNetworkPump() override;
    void scriptStepAllActive() override;
    void gameObjectDispatchInteractions() override;
    void renderMainViewFrame() override;
    void weatherUpdateSky() override;
    void dayCycleAndOutdoorMusic() override;
    void hudSelectionAndTargets() override;
    void tooltipDispatch() override;
    void hudLabelsAndCaption() override;
    void cameraCombatScroll() override;
    void presentFrame() override;
    void optionsChatHotkeyPanels() override;
    void autosaveAndNetWait() override;
    void characterCollectByOwner() override;
    void quickJumpContact() override;

    // ---- ISubsystems: shutdown teardown -----------------------------------
    void tdGameShutdownSubsystems() override;
    void tdWidgetShutdownSystem() override;
    void tdConfigWriteGfxSettings() override;
    void tdGameStateFreeAllResources() override;
    void tdUniverseSwitchActiveSlot0() override;
    void tdTableResetLightmaps() override;
    void tdRenderShutdownEngine() override;
    void tdInputDirectInputShutdown() override;
    void tdTimeBaseStopTimer() override;
    void tdVfsShutdown() override;
    void tdMemPoolShutdownStack() override;
    void tdMemoryShutdownTracker() override;
    void tdErrorLogShutdown() override;

private:
    void rec(const char* hook, Kind kind) { events_.push_back({hook, kind}); }
    // Installs the real build->project->cull->sort->raster pipeline onto frameHooks_.
    void InstallRealFramePipeline();

public:
    // De-inert the LIVE frame's REAL render bridges (PLAYABLE_PLAN P6): the real
    // terrain floor leaf (play::RenderTerrain via frameHooks_.renderTerrain +
    // hasTerrain), the real scene-walk node dispatch (render::ProcessSceneNodeAppend
    // via play::InstallRealSceneBridge) feeding a REAL multi-tri mesh, and the real
    // HUD sprite blit (render::ShapeShowFromBank via play::InstallRealHudBridge).
    // OFF by default (legacy lifecycle/wiring tests unaffected); call once before
    // RunFrameLoop to make the next renderMainViewFrame draw real content.
    void EnableRealRenderBridges();
    bool realRenderBridges() const { return realRenderBridges_; }
    // REAL-BRIDGE live-frame content inspection (non-zero once a bridged world
    // frame has run): floor triangles + composited terrain pixels, dispatched
    // scene nodes, real mesh tris, and HUD sprite shapes.
    int liveTerrainTris() const { return lastTerrainTris_; }
    int liveTerrainPixels() const { return lastTerrainPix_; }
    int liveSceneDispatched() const { return lastSceneDispatch_; }
    int liveMeshTris() const { return lastMeshTris_; }
    int liveHudSprites() const { return lastHudSprites_; }
private:

    // Injected OS resources (any may be null -> the dependent hook is STUB).
    shim::IPlatform*       plat_  = nullptr;
    shim::IGraphicsDevice* gfx_dev_ = nullptr;
    shim::IAudioDevice*    audio_dev_ = nullptr;
    shim::IFileSystem*     fs_    = nullptr;
    shim::INetSocket*      net_   = nullptr;
    const config::IProfileProvider* ini_ = nullptr;

    // Owned real module instances (the originals were process globals).
    mem::Heap          heap_;
    mem::MemoryTracker tracker_;
    mem::MemPool       pool_;
    audio::SoundSystem sound_;
    net::NetTransport  transport_;
    sim::CommandQueue  cmdQueue_;
    crt::TimeBase      timer_;  // winmm-style periodic timer (shim-clock driven)

    // Config snapshots filled by configReadGfxAndSound().
    config::GfxSettings   gfx_;
    config::SoundSettings snd_;
    config::GameSettings  game_;

    // Render orchestration state (RenderMainViewFrame) + present state.
    render::FrameState  frame_;
    render::FrameHooks  frameHooks_{};
    render::PresentState present_{};
    i32 dayKeyframes_[6] = {0, 0, 0, 0, 0, 0};
    // The real per-frame draw-list pipeline (build->project->cull->sort->raster)
    // installed onto frameHooks_.sceneWalk / .flushDrawList. Pimpl'd so wiring.h
    // does not pull the geometry headers; lives in wiring.cpp.
    bool frameSceneInstalled_ = false;
    int  lastDrawListPolys_ = 0;   // entries appended by the last real scene walk
    int  lastRasterTris_ = 0;      // triangles rasterized by the last real flush
    // REAL-BRIDGE live-frame state (PLAYABLE_PLAN P6; OFF by default).
    bool realRenderBridges_ = false;
    int  lastTerrainTris_ = 0;
    int  lastTerrainPix_ = 0;
    int  lastSceneDispatch_ = 0;
    int  lastMeshTris_ = 0;
    int  lastHudSprites_ = 0;

    // Script context table stepped each frame.
    std::vector<sim::ScriptSlot> scriptSlots_;

    // Token program + last command result for the script register/invoke ABI.
    std::vector<sim::ScriptToken_t> scriptProgram_;
    i32 scriptCmdResult_ = 0;

    // Wall clock advanced each frame (drives day-cycle brightness).
    sim::GameTime clock_{};

    // GUI text/label database loaded by textLoadDefinitionFile/guiLoadGfxFile.
    gui::text::TextDb textDb_;

    // Sprite shape bank populated by guiLoadGfxFile (one contiguous buffer).
    std::vector<u8> shapeBank_;

    // Per-frame fade tick (drives render::FadeAlpha in the GameObjects block).
    u32 fadeTick_ = 0;
    bool fadeDone_ = false;
    int lastTooltipKind_ = 0;

    // ---- third-wave wired state -------------------------------------------
    // Weather: a synthetic 24-hour intensity arc + last computed intensity
    // (weatherUpdateSky now drives the real render::Weather* core).
    i32 weatherArc_[24] = {0};
    int weatherIntensity_ = 0;
    // HUD labels/caption: last computed label x via the real layout math.
    int hudLabelX_ = 0;
    // Character: the live-actor per-frame driver advance count.
    int characterUpdates_ = 0;
    // Cutscene poll: the real CutsceneTable the inputCommandPoll step scans.
    sim::CutsceneTable cutscenes_;
    int cutscenesProcessed_ = 0;
    // Market/economy: last work-minutes from the real production integrator.
    int marketWorkMinutes_ = 0;
    // Outdoor-music director driven by soundInitMusicThread + dayCycleAndOutdoorMusic.
    // A recording IMusicSink stands in for the streaming layer (returns a real
    // non-zero handle from load, matching the original's dword_642018 != 0).
    struct RecordingMusicSink : public audio::IMusicSink {
        int loaded = 0, stopped = 0;
        int loadTrack(const std::string&, int) override { return ++loaded; }
        void stopTrack(int, int) override { ++stopped; }
    };
    RecordingMusicSink musicSink_;
    audio::MusicDirector musicDir_;

    // ---- fourth-wave wired state ------------------------------------------
    // Spatial-voice pool brought up by sound3dInitPool (real audio::Sound3dPool
    // over the SoundSystem voice pool); lazily constructed (needs sound_ inited).
    std::unique_ptr<audio::Sound3dPool> sound3dPool_;
    int sound3dCapacity_ = 0;
    // Chat console driven by optionsChatHotkeyPanels (real gui::ChatConsole).
    gui::ChatConsole chat_;
    int chatLines_ = 0;
    // Quick-jump contact resolve (real sim::GameObjectResolveEntityById).
    i32  quickJumpContactId_ = -1;
    bool quickJumpResolved_ = false;
    // ---- fifth-wave wired state -------------------------------------------
    // Settings serializer outcome (tdConfigWriteGfxSettings -> real
    // ConfigWriteGfxSettings); a record sink stands in for the OS
    // WritePrivateProfileStringA leaf.
    int settingsKeysWritten_ = 0;
    std::string writtenStadt_;
    // ---- sixth-wave wired state -------------------------------------------
    // Combat-scroll decision (cameraCombatScroll -> real ResolveCombatScroll). A
    // synthetic edge-motion block + scroll-direction inputs drive the real core.
    float combatEdgeVecs_[4][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
    int   combatScrollRight_ = -1;
    int   combatScrollBottom_ = -1;
    // Sine-table length built by soundWaveInitSineTables (real audio::InitSineTables).
    int   sineTableCount_ = 0;
    // ---- audio-tick wired state -------------------------------------------
    // The per-frame audio driver (AudioTick) state + the market-ambience loop
    // handle the audioStartupMilesDriver SFX trigger creates (Start/StopMarketLoop).
    AudioEnable      audioEnable_{};         // dword_63C900/63C904/63C8F8 gates
    AudioTickResult  lastAudioTick_{};       // last frame's audio-tick transitions
    audio::Sound3dEntry* marketLoop_ = nullptr; // dword_6420F4 (market-loop handle)
    int  audioTicks_ = 0;                    // frames the audio tick ran
    bool marketLoopStarted_ = false;         // the market SFX trigger fired real

    // ---- net-lobby wired state --------------------------------------------
    // The autosaveAndNetWait hook drives the REAL multiplayer-lobby handshake
    // (net::RunNetworkLobby): wait-ready -> host ship savegame -> ready-count cmd6
    // -> all-ready barrier, over the owned cmdQueue_; plus a real host-advertise ->
    // client-discover round-trip through the discovery siblings on an in-process
    // datagram bus. Run once (the original's per-round net-wait fires on entry).
    net::LobbyRunResult lobbyResult_{};
    int  lobbyDiscovered_ = 0;               // servers the discover round-trip found
    bool lobbyDriven_ = false;               // ran the lobby spine once already

    // ---- eighth-wave wired state ------------------------------------------
    // The scene-slot ("Universe") bring-up + teardown. universeCreateDefaultCameras
    // runs the real camera bootstrap (g_megaCam handle snapshotted here);
    // tdUniverseSwitchActiveSlot0 runs the real quiet swap to slot 0 at shutdown.
    unsigned universeMegaCam_ = 0;
    bool universeSwitchedToSlot0_ = false;

    // ---- ninth-wave wired state -------------------------------------------
    // The sim-leaf wiring: commandQueueInitAndSync installs the four real sim-hook
    // waves + the apply-3 jump table, so the per-frame/per-turn command-apply
    // dispatch reaches the real reconstructed sim leaves (entity mutation, person
    // create, charaction handlers, command codec). The per-turn driver in
    // gameObjectDispatchInteractions applies one object-resolve command (opcode 0x4A)
    // through the wired real entity resolver (GameObjectResolveEntityById) — a pure
    // query that mutates no shared state, so it composes with every other suite.
    bool simHooksInstalled_ = false;
    bool cmdTurnDriven_ = false;          // applied the per-turn command once
    bool cmdApplyReachedReal_ = false;    // the wired apply path dispatched through real code
    int  cmdApplyResult_ = -1;            // the apply return code (1 = no live record)

    // ---- real-asset boot state --------------------------------------------
    // When set, the file-layer hooks consume the real loaders (see BindRealAssets).
    const RealGameAssets* assets_ = nullptr;
    std::string gameDir_;
    bool realAssets_ = false;
    int  gfxObjectCount_ = 0;
    int  buildingTypeCount_ = 0;
    int  sceneTypeCount_ = 0;
    bool cityLoaded_ = false;
    std::string cityName_;

    std::vector<Event> events_;
    int  presentCount_ = 0;
    int  frameCount_ = 0;
    bool quitRequested_ = false;   // set by inputLatchAndPump on a false pump
    int  brightness_ = 0;
    int  lastClickedId_ = -1;
    bool trackerInited_ = false;
    bool vfsInited_ = false;
    bool soundInited_ = false;
    bool timerRunning_ = false;
};

// ===========================================================================
// Headless run convenience.
// ---------------------------------------------------------------------------
// Constructs a GameApp wired to RealSubsystems with the src/shim_impl headless
// backends (NullPlatform, MemoryGraphicsDevice, NullAudioDevice, MemFileSystem,
// LoopbackSocket) and runs the full lifecycle (CreateMainWindow -> init ->
// `frames` session frames -> 13-step shutdown). Returns the GameApp::Run exit
// code. `out` (optional) receives the RealSubsystems for post-run inspection.
struct HeadlessResult {
    int exitCode = 0;
    int presentCount = 0;
    int frameCount = 0;
    std::uint32_t lastFeatureMask = 0;
    bool memoryTrackerInited = false;
    bool vfsInited = false;
    bool soundInited = false;
    // Settings serializer outcome (tdConfigWriteGfxSettings -> real
    // ConfigWriteGfxSettings). Non-zero iff the real shutdown-step-3 serializer
    // ran (it writes one entry per [Gfx]/[Sound]/[Game] key).
    int settingsKeysWritten = 0;
    std::string writtenStadt; // the [Game]/stadt value the serializer emitted
    // Sine-table length built by the soundWaveInitSineTables init step (real
    // audio::InitSineTables). Non-zero (256) iff that real init step ran — it
    // always does during InitEngineAndScriptCommands, on BOTH boot paths.
    int sineTableCount = 0;
};

HeadlessResult RunHeadless(int displayMode = 1, bool showIntro = false,
                           bool networkClient = false, int frames = 10);

// ===========================================================================
// REAL-ASSET headless run.
// ---------------------------------------------------------------------------
// Boots the spine against a REAL "Die Gilde — Europe 1400" install rooted at
// `gameDir`: mounts the real assets (Gilde.INI + Resources/*.BIN via the VFS,
// MountRealGameAssets), wires the file-layer hooks to the real loaders
// (configReadGfxAndSound -> the parsed real INI; vfsInit -> the mounted disk VFS;
// guiLoadGfxFile -> Form_LoadFromFile("gfx/gilde.gfx"); worldLoadBuildingAnd-
// ObjectData -> WorldLoadBuildingAndObjectData("data/"); plus the configured
// <Stadt>.cty load), then runs the full CreateMainWindow -> init -> `frames`
// session frames -> 13-step shutdown lifecycle ENTIRELY through the spine.
//
// If `gameDir` is empty or the four key assets are absent, `r.assetsPresent` is
// false and NO spine run happens (caller should skip); otherwise the lifecycle
// runs and `r.exitCode` is the GameApp::Run result (0 on a clean run).
struct RealHeadlessResult {
    HeadlessResult base;            // exit/frame/present + tracker/vfs/sound flags
    bool assetsPresent = false;     // the real game dir + key assets resolved
    bool iniLoaded = false;         // Gilde.INI parsed through the real INI parser
    std::string stadt;              // [General] Stadt (e.g. "Augsburg")
    std::size_t archivesMounted = 0;// Resources/*.BIN mounted
    std::size_t totalMembers = 0;   // indexed members across all archives
    int  gfxObjectCount = 0;        // gilde.gfx objects (expected 1806)
    int  buildingTypeCount = 0;     // A_Geb.dat records (expected 72)
    int  sceneTypeCount = 0;        // A_Obj.dat records (expected 731)
    bool cityLoaded = false;        // <Stadt>.cty parsed
    std::string cityName;           // parsed .cty header name
};

RealHeadlessResult RunHeadlessRealAssets(const std::string& gameDir,
                                         int frames = 5, int displayMode = 1);

} // namespace guild::app
