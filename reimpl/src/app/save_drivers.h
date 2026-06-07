#pragma once
// gilde.exe — save/load orchestration spine (guild::app).
//
// The "Die Gilde" save/load flow is driven by a small family of GUI/net-coupled
// ORCHESTRATORS that sit ABOVE the byte-exact io/save serializers (io::Write-
// GameState @0x5a348c / io::LoadGameState @0x5a7604). The io agent reconstructed
// the file-format core but DEFERRED these top callers as GUI/net-coupled; this
// module translates the orchestration spine 1:1 and wires it to the real io save
// layer through the app's hook pattern (the same one session_init.cpp/
// frame_drivers.cpp use):
//
//   0x56d984  VIBE_Save_DoQuickSave        — the F-key quicksave driver. Network
//             game: enqueue a sync-blob command and spin the Amt sync barrier;
//             single game: capture a thumbnail, then io::WriteGameState to
//             "Gamedata\Saves\Quicksave.SAV".
//   0x4ff800  VIBE_Input_HandleGameSpeedKeys (quicksave-trigger slice) — the
//             keyboard handler that fires DoQuickSave when the quicksave key is
//             down and the headless-suppress bit is clear.
//   0x56a804  VIBE_Menu_RunSaveGame        — the save-game menu loop: build the
//             slot browser, run frames until a slot is picked + named, confirm
//             overwrite, then io::WriteGameState to the chosen slot file.
//   0x56da74  VIBE_Net_LoadAndSyncSession  — the network load+resync spine: count
//             ready players, write the scenario block, CRC the header, enqueue the
//             sync command, spin the barrier, then write the .SRV + .SAV pair.
//
// Every GUI/net/command/render leaf the originals call (Hud banner, Light gray,
// Form/Window, Command_QueueRequest*, Net_AllPlayersReady, Amt_RefreshGuildState,
// Render_CaptureScreenThumbnail, the frame pump) is NOT owned by this module; it
// is routed through ISaveHooks so a host wires the real subsystems and a test
// records them. The byte-exact file write/load is REUSED from io (no redefine):
//   io::WriteGameState / io::LoadGameState / io::GameState / io::SaveVersion*.
//
// SESSION FLAGS are reused from app/gamelogic.h (guild::app::session::*); the live
// word_63C740 lives once in SessionState (session_init.h) and is reused here.
#include <cstdint>
#include <string>
#include <vector>

#include "app/gamelogic.h"     // guild::app::session::* flag constants
#include "io/gamestate.h"      // io::WriteGameState / LoadGameState / GameState
#include "render/thumbnail_capture.h"  // render::CaptureScreenThumbnail / CaptureSource

namespace guild::app {

// ===========================================================================
// Recovered string/literal constants (from the decompiled call sites).
// ===========================================================================
// aGamedataSavesQ @0x6252b8 — VIBE_Save_DoQuickSave single-game target path.
extern const char kQuickSavePath[];   // "Gamedata\\Saves\\Quicksave.SAV"
// aQuicksave_0 @0x624ef0 — the in-game save NAME stamped into the quicksave.
extern const char kQuickSaveName[];   // "QUICKSAVE"
// aGamedataSavesS @0x624f40 — VIBE_Menu_RunSaveGame per-slot path template.
extern const char kSaveSlotPathFmt[]; // "Gamedata\\Saves\\%s.SAV"
// aGamedataNetwor @0x624f94 / aGamedataNetwor_0 @0x6252fc — net session paths.
extern const char kNetSavePathFmt[];  // "Gamedata\\network\\%s.SAV"
extern const char kNetSrvPathFmt[];   // "Gamedata\\network\\%s.SRV"

// gilde.exe 0x4ff871 — VIBE_Input_HandleGameSpeedKeys: the quicksave scancode the
// keyboard handler compares byte_67225C against before firing DoQuickSave.
constexpr int kQuickSaveScancode = 16;  // byte_67225C == 16

// gilde.exe 0x533a64 / 0x56da41 etc. — the headless-suppress feature-mask bit the
// DoQuickSave trigger gate tests ((dword_11BC2D0 & 0x10000) == 0).
constexpr std::uint32_t kHeadlessSuppressBit = 0x10000;

// ===========================================================================
// Save-driver state: the scattered run-state globals the save spine reads/writes
// that are NOT already owned by SessionState. Recovered byte-for-byte from the
// decompilations (offset comments give the original symbol + absolute address).
// ===========================================================================
struct SaveDriverState {
    // byte_649D50 (0x649D50): the "save slot index / network-save flag" byte the
    // writer stamps into the header just before VIBE_Save_WriteGameFile. The
    // quicksave path sets it to 1; the menu path sets it to the slot number; the
    // net path copies it from dword_63CC70. Static image: 0.
    std::uint8_t saveSlotByte = 0;       // byte_649D50

    // byte_63CC1D (0x63CC1D): the count of ready human players (kind 6/7) the net
    // resync spine counts before spinning the sync barrier. Static image: 0.
    std::uint8_t readyPlayerCount = 0;   // byte_63CC1D

    // dword_63CC70 (0x63CC70): the network-save slot byte source. Static image: 0.
    std::int32_t netSaveSlot = 0;        // dword_63CC70

    // The last header CRC the net resync computed (v28 = Util_Crc32(hdr)). Used as
    // the sync-command payload. Static image: 0.
    std::uint32_t lastHeaderCrc = 0;
};

SaveDriverState& GameSaveDriverState();

// ===========================================================================
// ISaveHooks — every GUI / net / command / render leaf the save spine calls.
// "[hook]" entries are leaves owned by other clusters (gui/net/sim/render); a
// host adapter wires them, a test records them. The file write/load itself is
// performed through io::WriteGameState / io::LoadGameState (real), NOT a hook.
// ===========================================================================
class ISaveHooks {
public:
    virtual ~ISaveHooks() = default;

    // VIBE_Hud_SetStatusBannerText @0x4bcdcc — show a status-line message.
    virtual void hudSetStatusBanner(const std::string& text) = 0;          // [hook]
    // VIBE_Light_SetGrayColorThunk @0x5c6af0 — fade/desaturate the screen.
    virtual void lightSetGrayColor(int a, int b) = 0;                      // [hook]
    // VIBE_Render_CaptureScreenThumbnail @0x56d48c — grab the 160x120 thumbnail.
    virtual void renderCaptureThumbnail() = 0;                             // [hook]
    // VIBE_GameLogic_RunFrameLoop @0x4c09a0 — pump one frame (returns "ran a step").
    virtual int  runFrameLoop(std::uint32_t featureMask) = 0;              // [hook]
    // VIBE_Net_AllPlayersReady @0x56d930 — host barrier: all peers acked.
    virtual bool netAllPlayersReady() = 0;                                 // [hook]
    // VIBE_Command_QueueRequestFlagBlob32 @0x494ab4 — enqueue a sync command,
    // returns its packet id.
    virtual std::uint32_t commandQueueRequestFlagBlob32(int code,
                                                        const void* blob) = 0;// [hook]
    // VIBE_Command_GetPacketStatusById @0x4939d4 — has the packet been acked?
    virtual bool commandGetPacketStatusById(std::uint32_t id) = 0;         // [hook]
    // VIBE_Amt_RefreshGuildState @0x4becdc — one Amt sync-barrier spin step.
    virtual void amtRefreshGuildState() = 0;                               // [hook]
    // VIBE_Net_RunWaitLoopWithStatus @0x4beb80 — block until the peers sync.
    virtual void netRunWaitLoop(const std::string& status) = 0;            // [hook]
};

// ===========================================================================
// RenderThumbnailSaveHooks — an ISaveHooks mixin that wires the ONE render leaf
// the quicksave path needs (renderCaptureThumbnail @0x56d48c) to the REAL render
// sibling render::CaptureScreenThumbnail, instead of leaving it a recorded mock.
//
// This is the "capture into the present/thumbnail path" hook turned mock->real:
// SaveDoQuickSave's `hooks.renderCaptureThumbnail()` now drives the real screen
// resample into the engine-wide thumbnail buffer (render::ThumbnailBuffer). A host
// supplies the live CaptureSource (working-surface base/stride/screen size) via
// SetCaptureSource(); the GUI/net leaves remain pure-virtual so each host/test
// still provides them. (When no source is set the capture is a documented no-op,
// matching the original's "if (DecompressState_Blob) ..." surface gate.)
// ===========================================================================
class RenderThumbnailSaveHooks : public ISaveHooks {
public:
    // Wire the live working surface the capture samples (host fills it from the
    // render present state). With no source set, renderCaptureThumbnail() no-ops.
    void SetCaptureSource(const render::CaptureSource& src) {
        captureSource_ = src;
        haveSource_ = true;
    }

    // mock -> REAL: drive render::CaptureScreenThumbnail into ThumbnailBuffer().
    void renderCaptureThumbnail() override;       // wired in save_drivers.cpp

    // Whether the last renderCaptureThumbnail() actually captured (vs. no source).
    bool lastCaptureRan() const { return lastCaptureRan_; }

private:
    render::CaptureSource captureSource_{};
    bool haveSource_ = false;
    bool lastCaptureRan_ = false;
};

// ===========================================================================
// MaterializeThumbnail — wire the REAL render capture buffer (render::Thumbnail-
// Buffer, written by render::CaptureScreenThumbnail) into the io save path's
// embedded thumbnail blob (io::GameState.thumbnail, the 0xE100 bytes io::Write-
// ScenarioBlock streams into the .SAV header). This is the "thumbnail write into
// save_drivers" hook turned mock->real: the byte layout is identical to VIBE_Save_
// WriteThumbnailFile's (UnpackColor word -> R,B,G per pixel). `fmt` is the native
// pixel format of the captured 16bpp thumbnail (RGB565 by default). After this the
// io::WriteGameState call serialises the genuinely-captured preview, not a stub.
void MaterializeThumbnail(io::GameState& state,
                          const render::ColorFormat& fmt = render::ColorFormat{5, 2, 3, 0, 11, 3});

// ===========================================================================
// gilde.exe 0x56d984 — VIBE_Save_DoQuickSave (the F-key quicksave driver).
// Network game (&4): if host (&0x10) and all peers ready, fade + stamp the
// "QUICKSAVE" name and enqueue a code-15 sync-blob command, then spin the Amt
// barrier until acked; else show the right "can't save now" banner. Single game
// (no &4, no &0x80 tutorial): show the saving banner, pump one frame, capture the
// thumbnail, stamp byte_649D50=1, then io::WriteGameState to Quicksave.SAV and
// restore the banner. Returns true if a file was actually written (single path).
// `state` carries the io::GameState to serialize for the single-player write.
bool SaveDoQuickSave(std::uint16_t sessionFlags, io::GameState& state,
                     ISaveHooks& hooks);

// gilde.exe 0x4ff800 — VIBE_Input_HandleGameSpeedKeys (quicksave-trigger slice).
// Reproduces the exact gate the keyboard handler applies before firing DoQuick-
// Save: the quicksave key is down (scancode == 16), the menu-busy latch is clear
// (`!menuBusy`), and the headless-suppress mask bit is clear. Returns true if the
// quicksave was triggered. `featureMask` is dword_11BC2D0.
bool InputQuickSaveTrigger(int scancode, bool menuBusy, std::uint32_t featureMask,
                           std::uint16_t sessionFlags, io::GameState& state,
                           ISaveHooks& hooks);

// ===========================================================================
// gilde.exe 0x56a804 — VIBE_Menu_RunSaveGame (the save-game menu loop).
// The GUI form build/center, the slot-browser metadata load, the per-slot click
// hit-test and the name-input dialog are GUI leaves (routed through SaveMenuCtx's
// callbacks). The reconstructed spine is the frame-pump loop + the chosen-slot
// resolve + the overwrite-confirm + the real io::WriteGameState to
// "Gamedata\Saves\<name>.SAV". Returns the number of frames pumped.
// ===========================================================================
struct SaveMenuCtx {
    // The frame-pump termination: the original loops while RunFrameLoop != 0; a
    // headless caller bounds it. <0 == unbounded (original behaviour).
    int maxFrames = -1;

    // The slot the user picked this run (-1 == none picked; the loop pumps frames
    // until a slot is chosen). The original resolves this from the click hit-test
    // (dword_672228 + the v16[] object table); here the host/test supplies it.
    int pickedSlot = -1;

    // The name the user typed for the picked slot (VIBE_Menu_RunSaveNameInput).
    // Empty == the name dialog was cancelled (the original re-shows the slot).
    std::string pickedName;

    // Whether the picked slot was already occupied (drives the overwrite confirm).
    bool slotOccupied = false;
    // The overwrite-confirm result (VIBE_Dialog_RunMessageBox). Only consulted when
    // slotOccupied; default true == confirmed.
    bool overwriteConfirmed = true;

    // --- recorded results ---
    int framesPumped = 0;
    bool wrote = false;             // io::WriteGameState was invoked
    std::string writtenPath;        // the path actually written
};

int MenuRunSaveGame(SaveMenuCtx& ctx, io::GameState& state, ISaveHooks& hooks);

// ===========================================================================
// gilde.exe 0x56da74 — VIBE_Net_LoadAndSyncSession (network load+resync spine).
// Marks the net-resync mask bit, clears the per-peer ack table, then (network
// game only): waits for the peers, counts ready human players, copies the session
// name, writes a scenario block to "Gamedata\network\<name>.SAV", re-reads its
// header and CRC32s it, enqueues a code-16 sync-verify command, spins the Amt
// barrier readyPlayerCount times, waits again, then writes the .SRV then .SAV game
// files. Returns 0 for the network path (the original's tail), or result*4 for a
// non-network call (the early `(word_63C740 & 4) == 0` return).
// ===========================================================================
struct NetSyncCtx {
    std::string sessionName;        // a1 — the network session base name
    // The synthetic ready-player roster the original walks (word_12CE910 != -1 &&
    // kind in {6,7}). A flat list of (alive, kind) is enough for the count.
    struct Peer { bool alive = true; std::uint8_t kind = 6; };
    std::vector<Peer> peers;

    // --- recorded results ---
    int framesBarrier = 0;          // Amt barrier spins performed
    std::uint32_t headerCrc = 0;    // the CRC the resync computed
    std::vector<std::string> writtenPaths; // .SAV (scenario), .SRV, .SAV (game)
};

int NetLoadAndSyncSession(std::uint16_t sessionFlags, NetSyncCtx& ctx,
                          io::GameState& state, ISaveHooks& hooks);

} // namespace guild::app
