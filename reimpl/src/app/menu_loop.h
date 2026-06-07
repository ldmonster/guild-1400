#pragma once
// gilde.exe — the top-level main-menu state machine + input-event pump (guild::app).
//
// This module reconstructs the boot->play TOP-LEVEL control flow that sits between
// the engine bring-up and the in-game session:
//
//   VIBE_Menu_RunMainMenu                  @0x529d08 — the main-menu frame loop:
//       latch input, build the 8 sprite buttons, then on each frame read the input
//       state, and when a button is clicked (dword_672228) dispatch the hovered
//       widget id (dword_62D22C) to its screen transition (New Game / Load /
//       Multiplayer / Options / Credits / Quit). ESC (byte_67225C==1) quits.
//   VIBE_Input_LatchMouseState             @0x40dab8 — the per-frame input pump that
//       drains the 32-slot mouse event ring into the "current input" block
//       (dword_672xxx) so the menu/HUD read a single latched click edge.
//   VIBE_GameLogic_MainEntryAndShutdown    @0x534bbc (outer while(1) loop) — the
//       menu<->session driver: run the menu; if a session was armed
//       (dword_631614) run InitOrLoadSession; loop back to the menu unless quit
//       (dword_63CC48) or a display restart (dword_63CC38) was requested.
//
// The GUI/render/audio/3D-scene leaves the original menu drives (the form build,
// the fade, RunOptions*, the credits scroll, the sub-screen runners) are owned by
// other clusters; this module REUSES the already-translated real siblings:
//   * guild::gui::MainMenu_Dispatch / MainMenu_Select  (gui/main_menu.h) — the
//     per-button click->transition dispatch (New Game/Load/Multiplayer/Quit).
//   * guild::gui::g_mouseClick / g_hoverObject          (gui/input.h) — the latched
//     click edge + hovered widget id the menu loop reads.
//   * guild::shim::IPlatform                             (shim/IPlatform.h) — the OS
//     boundary the input pump latches from (PumpMessages + getMouse).
// The DirectInput keyboard device poll (VIBE_Input_PollKeyboardDevice @0x40d920)
// stays a documented OS leaf (no shim keyboard-event ring); the ESC scancode is
// injected by the caller.
//
// ODR: this module DEFINES no globals owned elsewhere. It extern-reuses the gui
// input globals and dispatch entry points; it owns only the small app-level
// outer-loop state (the menu-screen enum + the MenuMain driver).

#include "gui/input.h"
#include "gui/main_menu.h"
#include "shim/IPlatform.h"

#include <cstdint>

namespace guild::app {

// ===========================================================================
// Mouse event ring  (VIBE_Input_ProcessMouseClicks @0x40cdd0 producer ->
//                    VIBE_Input_LatchMouseState   @0x40dab8 consumer)
// ---------------------------------------------------------------------------
// The original keeps a 32-slot ring of pending pointer events based at
// dword_670FEC, each slot a 76-byte (0x4C) record of parallel one-dword fields
// (held flags, edge/click flags, wheel up/down, packed x/y), with a per-slot
// "pending" flag at dword_671028[slot]. The DirectInput cooked-input callback
// fills a slot and raises its flag; LatchMouseState scans for the FIRST pending
// slot, copies its fields into the live "current input" block (dword_672xxx) and
// clears the slot flag — so the menu/HUD see exactly one latched edge per frame.
//
// Reconstructed byte-faithfully: the stride (76 bytes), the slot count (2432/76 =
// 32), the "first pending slot wins" scan, and the field-copy set the menu reads
// (the click edge dword_672228 and the wheel up/down dword_672254/672250).
inline constexpr int kMouseEventStrideBytes = 76;    // 0x4C — per-slot record size
inline constexpr int kMouseEventSlotCount   = 32;    // 2432 / 76 (the scan bound)

// One pending pointer event (the 76-byte slot the ring producer fills). Only the
// fields the latch copies into the current-input block are modeled; the rest of
// the 76-byte record is engine bookkeeping the menu loop never reads.
struct MouseEvent {
    bool    pending = false; // dword_671028[slot] — slot has an unconsumed event
    int32_t clickEdge = 0;   // dword_670FF8[slot] -> dword_672228 (left-click edge)
    int32_t heldEdge = 0;    // dword_670FF0[slot] -> dword_672220 (left held)
    int32_t wheelUp = 0;     // dword_671024[slot] -> dword_672254 (wheel up)
    int32_t wheelDown = 0;   // dword_671020[slot] -> dword_672250 (wheel down)
    int16_t packedX = 0;     // word_670FE6[slot]  -> word_672216 (cursor x)
    int16_t packedY = 0;     // word_670FE8[slot]  -> word_672218 (cursor y)
};

// The latched "current input" the menu/HUD read after a pump (the dword_672xxx
// block fields the menu loop consumes). Mirrors the subset of the live block.
struct LatchedInput {
    int32_t clickEdge = 0;   // dword_672228 (== gui::g_mouseClick)
    int32_t heldEdge = 0;    // dword_672220 (== gui::g_mouseDown)
    int32_t wheelUp = 0;     // dword_672254
    int32_t wheelDown = 0;   // dword_672250
    int16_t cursorX = 0;     // word_672216
    int16_t cursorY = 0;     // word_672218
};

// gilde.exe 0x40dab8 — VIBE_Input_LatchMouseState (the ring-drain core).
// Scan `ring[0..count)` for the first pending slot; copy its fields into `out`,
// clear that slot's pending flag, and MIRROR the latched click/held edges into the
// gui input globals (gui::g_mouseClick/g_mouseDown) the menu dispatch reads. When
// no slot is pending, `out` is zeroed (the original clears the live edge words at
// the top of every call) and the gui edges are cleared. Returns the index of the
// drained slot, or -1 when the ring was empty.
int InputLatchMouseState(MouseEvent* ring, int count, LatchedInput& out);

// gilde.exe 0x40dab8 (PumpMessages + LatchMouseState head) — pump the OS through
// the shim, synthesize a single pending event from the live pointer state, and run
// the ring-drain latch. This is the menu/HUD top-of-frame input pump wired to the
// real shim::IPlatform (the original's Win32 PumpMessages + DirectInput latch).
// Returns the platform pump result (false => a quit was requested).
bool InputPumpAndLatch(shim::IPlatform& plat, LatchedInput& out);

// ===========================================================================
// Main-menu frame loop  (VIBE_Menu_RunMainMenu @0x529d08)
// ===========================================================================
// The outcome of one full run of the main menu: which item closed it, the armed
// session flags (word_63C740), and whether the program should quit (dword_63CC48)
// or restart the display (dword_63CC38).
struct MenuResult {
    gui::MainMenuItem item = gui::MainMenuItem::kQuit;
    bool   close = false;        // dword_631614 — a session/screen transition armed
    int    sessionFlags = 0;     // word_63C740 after the closing transition
    bool   quit = false;         // dword_63CC48 — leave the program
    int    frames = 0;           // frames the menu loop ran (bounded for tests)
};

// One frame of the menu loop's click-dispatch (gilde.exe 0x529d08 do/while body).
// Reads the latched click edge + the hovered widget id, and when a button is
// clicked routes it through the REAL gui::MainMenu_Dispatch sibling. ESC closes
// with Quit. `escDown` is the byte_67225C==1 condition (the keyboard leaf is OS).
// Returns true when the loop should CONTINUE (no close yet), false to exit.
bool MenuDispatchFrame(const LatchedInput& in, int hoverObject, bool escDown,
                       MenuResult& result);

// gilde.exe 0x529d08 — VIBE_Menu_RunMainMenu. Run the menu loop for up to
// `maxFrames`, pumping input through `plat` each frame and dispatching clicks
// supplied by `clickSource` (the hovered widget id this frame, or -1 for none).
// The 3D-scene/fade/form-build leaves are DEFERRED (owned by render/gui); the
// dispatch + close/session-flag bookkeeping is the reconstructed core. Returns the
// MenuResult (item, session flags, close/quit). `escSource` reports the ESC key
// each frame (the OS keyboard leaf).
class MenuClickSource {
public:
    virtual ~MenuClickSource() = default;
    // The hovered widget id this frame (gui radio slot 0..7), or -1 for none.
    virtual int  hoverThisFrame(int frame) = 0;
    // Whether a click edge occurred this frame (drives dword_672228).
    virtual bool clickThisFrame(int frame) = 0;
    // Whether ESC (byte_67225C==1) is down this frame.
    virtual bool escThisFrame(int frame) { (void)frame; return false; }
};

MenuResult MenuRunMainMenu(shim::IPlatform& plat, MenuClickSource& clicks,
                           int maxFrames);

// ===========================================================================
// Menu<->session outer driver  (VIBE_GameLogic_MainEntryAndShutdown @0x534bbc loop)
// ===========================================================================
// One pass of the outer while(1): given a MenuResult, decide what happens next.
enum class MenuMainNext {
    kRunSession,   // a session was armed (word_63C740 != 0 && close) -> InitOrLoadSession
    kRestartMenu,  // no session armed / returned from a session -> show the menu again
    kRestartDisplay, // dword_63CC38 -> tear down + re-init the display, then menu
    kQuit,         // dword_63CC48 -> leave the program
};

// gilde.exe 0x534bbc (the LABEL_97 / dword_63CC48 / dword_63CC38 decision). Map a
// MenuResult to the next outer-loop action exactly as the spine's while(1) does:
//   * quit set                       -> kQuit
//   * restart-display set            -> kRestartDisplay
//   * a session armed (close + flags)-> kRunSession
//   * otherwise                      -> kRestartMenu
MenuMainNext MenuMainDecide(const MenuResult& r, bool restartDisplay);

} // namespace guild::app
