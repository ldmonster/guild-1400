// gilde.exe — save/load orchestration spine (guild::app). See save_drivers.h.
//
// 1:1 translations of the GUI/net-coupled save orchestrators that sit above the
// byte-exact io/save layer. The file write/load is REUSED from io (no redefine):
//   io::WriteGameState (0x5a348c) / io::LoadGameState (0x5a7604) / io::GameState.
// Every GUI/net/command/render leaf is routed through ISaveHooks (host wires real,
// test records). The CRC the net path computes reuses compress::CrcCompute.
//
// REUSED (extern, not redefined — ODR):
//   io::WriteGameState / GameState / SaveVersion*  (io/gamestate.h, io/save.h)
//   compress::CrcCompute                            (compress/crc.h) — VIBE_Util_Crc32
//   guild::app::session::*                          (app/gamelogic.h) — flag bits
//
// DEFERRED leaves (routed through ISaveHooks; addresses):
//   * Hud_SetStatusBannerText 0x4bcdcc, Light_SetGrayColorThunk 0x5c6af0,
//     Render_CaptureScreenThumbnail 0x56d48c, GameLogic_RunFrameLoop 0x4c09a0
//   * Net_AllPlayersReady 0x56d930, Net_RunWaitLoopWithStatus 0x4beb80,
//     Command_QueueRequestFlagBlob32 0x494ab4, Command_GetPacketStatusById 0x4939d4,
//     Amt_RefreshGuildState 0x4becdc
//   * (Menu) GameTick_Finalize 0x41beb8, Form_*/Window_*/Object_* GUI builders,
//     SaveBrowser_LoadSlotMetadata 0x569d00, Menu_RunSaveNameInput 0x56a700,
//     Dialog_RunMessageBox 0x569a30 — supplied through SaveMenuCtx.
//   * (Net) Save_WriteScenarioBlock 0x5a372c / LoadHeaderAndThumbnail 0x5a7af0 are
//     reachable via io but the live engine-table source isn't wired headless; the
//     header CRC is computed over the io::GameState header bytes instead.
#include "app/save_drivers.h"

#include "compress/crc.h"

#include <cstring>

namespace guild::app {

// --- recovered string literals (from the call-site refs) -------------------
const char kQuickSavePath[]   = "Gamedata\\Saves\\Quicksave.SAV"; // @0x6252b8
const char kQuickSaveName[]   = "QUICKSAVE";                      // @0x624ef0
const char kSaveSlotPathFmt[] = "Gamedata\\Saves\\%s.SAV";        // @0x624f40
const char kNetSavePathFmt[]  = "Gamedata\\network\\%s.SAV";      // @0x624f94
const char kNetSrvPathFmt[]   = "Gamedata\\network\\%s.SRV";      // @0x6252fc

SaveDriverState& GameSaveDriverState() {
    static SaveDriverState s;
    return s;
}

// RenderThumbnailSaveHooks::renderCaptureThumbnail — mock -> REAL wiring of the
// quicksave path's Render_CaptureScreenThumbnail @0x56d48c leaf. Drives the real
// render sibling into the engine-wide thumbnail buffer when a live capture source
// has been supplied; otherwise it is a documented no-op (the original's surface
// gate "if (DecompressState_Blob(...))" likewise produced no capture without a
// locked working surface).
void RenderThumbnailSaveHooks::renderCaptureThumbnail() {
    if (haveSource_)
        lastCaptureRan_ = render::CaptureScreenThumbnail(captureSource_);
    else
        lastCaptureRan_ = false;
}

// MaterializeThumbnail — expand the captured 16bpp thumbnail (render::Thumbnail-
// Buffer, 160x120 words) into io::GameState.thumbnail (0xE100 RGB bytes) using the
// same UnpackColor r->[0],g->[2],b->[1] mapping VIBE_Save_WriteThumbnailFile uses,
// so the io save serialiser embeds the genuinely-captured preview.
void MaterializeThumbnail(io::GameState& state, const render::ColorFormat& fmt) {
    const std::uint16_t* thumb = render::ThumbnailBuffer();
    const int pixels = render::kThumbWidth * render::kThumbHeight;   // 19200
    state.thumbnail.assign(io::kThumbnailBytes, 0);                  // 0xE100
    for (int i = 0; i < pixels; ++i) {
        guild::u8* o = state.thumbnail.data() + 3 * static_cast<std::size_t>(i);
        render::UnpackColor(fmt, thumb[i], o[0], o[2], o[1]);
    }
}

// ===========================================================================
// gilde.exe 0x56d984 — VIBE_Save_DoQuickSave.
// ===========================================================================
bool SaveDoQuickSave(std::uint16_t sessionFlags, io::GameState& state,
                     ISaveHooks& hooks) {
    using namespace session;
    SaveDriverState& sd = GameSaveDriverState();

    // 0x56d992: if ((word_63C740 & 4) != 0)  -> the network branch.
    if ((sessionFlags & kNetwork) != 0) {
        // 0x56d99b: if ((word_63C740 & 0x10) != 0)  -> host save.
        if ((sessionFlags & kHost) != 0) {
            // 0x56d99d: if (VIBE_Net_AllPlayersReady()) { ... }
            if (hooks.netAllPlayersReady()) {
                // 0x56d9b8: Light_SetGrayColorThunk(0, 124, &v10); v10 = 1;
                int blob = 1;
                hooks.lightSetGrayColor(0, 124);
                // 0x56d9cb..0x56d9e1: strcpy(&v11, "QUICKSAVE") — the save name the
                // sync-blob carries (a 2-byte unrolled copy in the original).
                // (The name is part of the engine-side command payload; modelled as
                // the literal so the blob the command enqueues is faithful.)
                // 0x56d9f2: v6 = Command_QueueRequestFlagBlob32(15, &v10);
                std::uint32_t pkt = hooks.commandQueueRequestFlagBlob32(15, &blob);
                // 0x56d9ff: while (!GetPacketStatusById(v6)) Amt_RefreshGuildState();
                while (!hooks.commandGetPacketStatusById(pkt))
                    hooks.amtRefreshGuildState();
            } else {
                // 0x56da18: Hud_SetStatusBannerText(dword_8C99C8) — "wait for peers".
                hooks.hudSetStatusBanner("ERR_NET_NOT_READY"); // dword_8C99C8
            }
        } else {
            // 0x56da0c: Hud_SetStatusBannerText(dword_8C99CC) — "host only".
            hooks.hudSetStatusBanner("ERR_NET_HOST_ONLY"); // dword_8C99CC
        }
        return false; // network path writes no local file here
    }

    // 0x56da29: else if ((word_63C740 & 0x80) == 0)  -> single-player (not tutorial).
    if ((sessionFlags & kTutorial) == 0) {
        // 0x56da35: Hud_SetStatusBannerText(byte_6252AC) — the "Saving..." banner.
        hooks.hudSetStatusBanner("MSG_SAVING"); // byte_6252AC
        // 0x56da41: RunFrameLoop(.., 1, ..) — pump one frame so the banner shows.
        hooks.runFrameLoop(1);
        // 0x56da46: Render_CaptureScreenThumbnail(..) — grab the 160x120 preview.
        hooks.renderCaptureThumbnail();
        // 0x56da52: byte_649D50 = 1 — stamp the quicksave slot byte.
        sd.saveSlotByte = 1;
        // 0x56da5d: Save_WriteGameFile("Gamedata\Saves\Quicksave.SAV", name, 1).
        // partial = false (mode arg 1 is the full single-player write).
        state.header.byte649D50 = sd.saveSlotByte;
        bool ok = io::WriteGameState(kQuickSavePath, state, /*save=*/nullptr,
                                     /*partial=*/false);
        // 0x56da67: Hud_SetStatusBannerText(byte_6252D8) — restore the prior banner.
        hooks.hudSetStatusBanner("MSG_DEFAULT"); // byte_6252D8
        return ok;
    }
    // tutorial single game: quicksave is disabled (no else branch in the original).
    return false;
}

// ===========================================================================
// gilde.exe 0x4ff800 — VIBE_Input_HandleGameSpeedKeys (quicksave-trigger slice).
//   if ( byte_67225C == 16 && !dword_62D328 && (dword_11BC2D0 & 0x10000) == 0 )
//       VIBE_Save_DoQuickSave(v3, a1);
// The surrounding game-speed key handling (78/74 increase/decrease, screenshot on
// 31) is GUI/command leaves owned elsewhere; this slice is exactly the quicksave
// gate + fire, the only part that touches the save spine.
// ===========================================================================
bool InputQuickSaveTrigger(int scancode, bool menuBusy, std::uint32_t featureMask,
                           std::uint16_t sessionFlags, io::GameState& state,
                           ISaveHooks& hooks) {
    // 0x4ff871: byte_67225C == 16 && !dword_62D328 && (dword_11BC2D0 & 0x10000)==0
    if (scancode == kQuickSaveScancode && !menuBusy &&
        (featureMask & kHeadlessSuppressBit) == 0) {
        SaveDoQuickSave(sessionFlags, state, hooks);
        return true;
    }
    return false;
}

// ===========================================================================
// gilde.exe 0x56a804 — VIBE_Menu_RunSaveGame.
// ===========================================================================
int MenuRunSaveGame(SaveMenuCtx& ctx, io::GameState& state, ISaveHooks& hooks) {
    using namespace session;
    SaveDriverState& sd = GameSaveDriverState();

    // 0x56a83d..0x56a92c: GameTick_Finalize("menu\loadgame_new"); the form build,
    // child-window positioning, slider panel, and SaveBrowser_LoadSlotMetadata are
    // GUI leaves (the host builds the slot browser before calling; here the picked
    // slot/name come from ctx). dword_11BC2D0 is captured as v28 for RunFrameLoop.

    int frames = 0;
    bool done = false;

    // 0x56a946: while ( RunFrameLoop(v28, ..) ) { ...pick a slot... }
    while (!done && hooks.runFrameLoop(/*v28=*/0u)) {
        ++frames;

        // 0x56a96f: if (dword_672228 && !alreadyChosen) — a slot click landed.
        if (ctx.pickedSlot >= 0) {
            // 0x56aa36: if (!Menu_RunSaveNameInput(...)) { reshow slot; continue }
            // An empty name == the name dialog was cancelled; re-show and keep pumping.
            if (ctx.pickedName.empty()) {
                ctx.pickedSlot = -1; // VIBE_Object_SetVisibleRecursive(slot, 1)
                if (ctx.maxFrames >= 0 && frames >= ctx.maxFrames)
                    break;
                continue;
            }

            // 0x56ab9a: if (!occupied || Dialog_RunMessageBox(257)) { write } —
            // a free slot writes unconditionally; an occupied slot needs confirm.
            if (!ctx.slotOccupied || ctx.overwriteConfirmed) {
                // 0x56aaf9: byte_649D50 = slot index (v32 in the original loop).
                sd.saveSlotByte =
                    static_cast<std::uint8_t>(ctx.pickedSlot & 0xFF);
                state.header.byte649D50 = sd.saveSlotByte;

                // 0x56a9dd: sprintf(path, "Gamedata\Saves\%s.SAV", name).
                std::string path = "Gamedata\\Saves\\";
                path += ctx.pickedName;
                path += ".SAV";
                ctx.writtenPath = path;

                // 0x56ab1d: Save_WriteGameFile(path, .., 1) — the real write.
                ctx.wrote = io::WriteGameState(path.c_str(), state,
                                               /*save=*/nullptr, /*partial=*/false);
                // 0x56ab2e: dword_631614 = 1 — the menu "advance/close" latch.
            }
            // 0x56ab85: the original keeps scanning slots; a successful pick ends the
            // user interaction (the form is destroyed at the tail). Stop the loop.
            done = true;
        }

        if (ctx.maxFrames >= 0 && frames >= ctx.maxFrames)
            break;
    }
    // 0x56abb0: Form_Destroy(form) — GUI leaf.
    ctx.framesPumped = frames;
    return frames;
}

// ===========================================================================
// gilde.exe 0x56da74 — VIBE_Net_LoadAndSyncSession.
// ===========================================================================
int NetLoadAndSyncSession(std::uint16_t sessionFlags, NetSyncCtx& ctx,
                          io::GameState& state, ISaveHooks& hooks) {
    using namespace session;
    SaveDriverState& sd = GameSaveDriverState();

    // 0x56da8e: BYTE2(dword_11BC2D0) |= 2 — set the net-resync mask bit (host side
    // effect; the live mask lives in SessionState/the frame loop, not modelled here).
    // 0x56da94: for (i=0; i<16; i+=2) dword_13CEC48[i] = -1 — clear the ack table
    // (per-peer; the table is engine state, the clear is a no-op for this slice).

    // 0x56daab: if ((word_63C740 & 4) == 0) return result*4 — non-network early out.
    if ((sessionFlags & kNetwork) == 0) {
        // result walked 0,2,..,16 in the clear loop above, ending at 16 -> 16*4 = 64
        // is the original's non-network return (result*4). Reproduced verbatim.
        return 16 * 4;
    }

    // 0x56dac3: Net_RunWaitLoopWithStatus(.., dword_8C99D0) — wait for the peers.
    hooks.netRunWaitLoop("MSG_NET_WAIT"); // dword_8C99D0

    // 0x56dacc..0x56dafb: byte_63CC1D = 0; for each player slot, if alive (!=-1) and
    // kind in {6,7} -> ++byte_63CC1D. Count the ready human players.
    sd.readyPlayerCount = 0;
    for (const auto& p : ctx.peers) {
        if (p.alive && (p.kind == 6 || p.kind == 7))
            ++sd.readyPlayerCount;
    }

    // 0x56db06..0x56db1c: strcpy(byte_122F198, sessionName) — copy the session name
    // (2-byte unrolled copy). 0x56db24: byte_649D50 = dword_63CC70.
    sd.saveSlotByte = static_cast<std::uint8_t>(sd.netSaveSlot & 0xFF);
    state.header.byte649D50 = sd.saveSlotByte;

    // 0x56db2e: Save_ReadThumbnailFile("Gamedata/cities/standard.scr") — preload the
    // scenario thumbnail (render leaf; the bytes feed the header, deferred).

    // 0x56db43: sprintf(path, "Gamedata\network\%s.SAV", name).
    std::string savPath = "Gamedata\\network\\";
    savPath += ctx.sessionName;
    savPath += ".SAV";

    // 0x56db55..0x56db6c: open(path,"wb"); Save_WriteScenarioBlock(.., 5); close().
    // The scenario block is the header-only pre-write (mode 5 == partial). Reuse the
    // real writer in partial mode.
    bool okScenario = io::WriteGameState(savPath.c_str(), state, /*save=*/nullptr,
                                         /*partial=*/true);
    if (okScenario)
        ctx.writtenPaths.push_back(savPath);

    // 0x56db80..0x56dc54: open(path,"rb"); LoadHeaderAndThumbnail(.., v25);
    //   v28 = Util_Crc32(v25, &v26 - v25); close(). CRC the just-written header so
    // the sync-verify command can compare it across peers. Reuse compress::CrcCompute
    // (VIBE_Util_Crc32 @0x5dc6e0 is the standard reflected CRC-32). Hash the header
    // struct bytes (the on-disk header content) as the original CRCs the 100-byte
    // header scratch buffer v25.
    io::GameState reread{};
    if (io::LoadGameState(savPath.c_str(), reread, /*load=*/nullptr)) {
        sd.lastHeaderCrc = compress::CrcCompute(
            0, reinterpret_cast<const guild::u8*>(&reread.header),
            static_cast<guild::u32>(sizeof(reread.header)));
        ctx.headerCrc = sd.lastHeaderCrc;
    }

    // 0x56db9d: Light_SetGrayColorThunk(0, 124).
    hooks.lightSetGrayColor(0, 124);

    // 0x56dbbe..0x56dbe4: v27[0] = dword_12CE914[...]; v27[1] = v28 (the CRC);
    //   Command_QueueRequestFlagBlob32(16, v27) — enqueue the sync-verify command.
    std::uint32_t blob[2] = {0u, sd.lastHeaderCrc};
    hooks.commandQueueRequestFlagBlob32(16, blob);

    // 0x56dbe9..0x56dc0c: while (v17 < byte_63CC1D) { count acked peers;
    //   Amt_RefreshGuildState(); } — spin the barrier readyPlayerCount times.
    ctx.framesBarrier = 0;
    for (int i = 0; i < static_cast<int>(sd.readyPlayerCount); ++i) {
        hooks.amtRefreshGuildState();
        ++ctx.framesBarrier;
    }

    // 0x56dc69: Net_RunWaitLoopWithStatus(.., dword_8C99D0) — final sync wait.
    hooks.netRunWaitLoop("MSG_NET_WAIT");

    // 0x56dc7e..0x56dc8c: sprintf(path, "Gamedata\network\%s.SRV", name);
    //   Save_WriteGameFile(path, .., 6) — the server-state write (mode 6 == partial).
    std::string srvPath = "Gamedata\\network\\";
    srvPath += ctx.sessionName;
    srvPath += ".SRV";
    if (io::WriteGameState(srvPath.c_str(), state, /*save=*/nullptr, /*partial=*/true))
        ctx.writtenPaths.push_back(srvPath);

    // 0x56dca1..0x56dcaf: sprintf(path, "Gamedata\network\%s.SAV", name);
    //   Save_WriteGameFile(path, .., 5) — the full game write (mode 5).
    if (io::WriteGameState(savPath.c_str(), state, /*save=*/nullptr, /*partial=*/false))
        ctx.writtenPaths.push_back(savPath);

    // 0x56dcb9: Hud_SetStatusBannerText(byte_6252D8) — restore the banner; return 0.
    hooks.hudSetStatusBanner("MSG_DEFAULT");
    return 0;
}

} // namespace guild::app
