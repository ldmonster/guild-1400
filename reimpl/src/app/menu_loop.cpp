// gilde.exe — top-level main-menu state machine + input-event pump (guild::app).
// 1:1 reconstructions of VIBE_Input_LatchMouseState @0x40dab8,
// VIBE_Menu_RunMainMenu @0x529d08 (dispatch core) and the menu<->session outer
// loop of VIBE_GameLogic_MainEntryAndShutdown @0x534bbc. See menu_loop.h.
//
// ===========================================================================
// REUSED (extern, not redefined — ODR):
//   gui::MainMenu_Dispatch / MainMenu_Select   (gui/main_menu.h) — per-button
//       click->transition dispatch (the real sibling, mock->real wiring).
//   gui::g_mouseClick / g_mouseDown            (gui/input.h) — the latched click
//       edge globals (dword_672228 / dword_672220) the menu reads (mirrored here).
//   shim::IPlatform                            (shim/IPlatform.h) — the OS input
//       boundary (PumpMessages + getMouse) the pump latches from.
//
// DEFERRED leaves (the render/gui/audio screen plumbing the original menu drives;
// owned by other clusters, NOT reconstructed here):
//   * VIBE_Window_PumpMessages 0x4bea64 / VIBE_Window_RenderEntityList 0x4134f0,
//     VIBE_Fade_Register/Unregister 0x41f0e8/0x41f18c, VIBE_Scene_LoadFromStream
//     0x5e7e38, VIBE_Render_SetupViewTransform 0x5af5f8 (3D scene/fade build).
//   * VIBE_Widget_AddSpriteToWindow 0x41217c, VIBE_RadioGroup_Create 0x412728,
//     VIBE_Form_SelectWindow 0x41e4cc, VIBE_GameTick_Finalize 0x41beb8 (form build).
//   * VIBE_Menu_RunOptionsGfx/Sfx/Game 0x56c21c/0x56c808/0x56cc44,
//     VIBE_Menu_RunCreditsScroll 0x56e524, VIBE_Menu_RunFileSelector 0x569668
//     (sub-screen runners — dispatched through the gui::MainMenuCommandSink).
//   * VIBE_Input_PollKeyboardDevice 0x40d920 (DirectInput keyboard device — OS
//     leaf; the ESC scancode is injected by the caller as `escDown`).
#include "app/menu_loop.h"

namespace guild::app {

// ===========================================================================
// gilde.exe 0x40dab8 — VIBE_Input_LatchMouseState (the ring-drain core).
// ---------------------------------------------------------------------------
// Original head clears the live edge words (dword_67221C/672228/672230/67223C/
// 672240/67224C = 0), then scans the 76-byte-stride ring for the first slot whose
// pending flag (dword_671028[slot]) is set; on a hit it copies the slot's parallel
// one-dword fields into the live "current input" block (dword_672xxx) and clears
// the slot flag. We reproduce the scan + the field copy the menu/HUD consume.
// ===========================================================================
int InputLatchMouseState(MouseEvent* ring, int count, LatchedInput& out) {
    // dword_67221C = dword_672228 = dword_672230 = dword_67223C = dword_672240
    //  = dword_67224C = 0 — clear the live edge words at the top of every call.
    out = LatchedInput{};

    int found = -1;
    if (ring && count > 0) {
        // for ( v0 = 0; ; v0 += 76 ) { if (v0 >= 2432) break; if (ring[v0].pending) ... }
        // The original starts at slot 0 (the `if (dword_671028[0])` head) and, on a
        // miss, advances by one 76-byte slot until the 2432-byte bound — i.e. the
        // FIRST pending slot wins.
        for (int slot = 0; slot < count; ++slot) {
            if (!ring[slot].pending)
                continue;
            const MouseEvent& e = ring[slot];
            // The field copy the menu loop reads (dword_672xxx <- ring[slot].*):
            out.clickEdge = e.clickEdge;   // dword_672228 = dword_670FF8[slot]
            out.heldEdge  = e.heldEdge;    // dword_672220 = dword_670FF0[slot]
            out.wheelUp   = e.wheelUp;     // dword_672254 = dword_671024[slot]
            out.wheelDown = e.wheelDown;   // dword_672250 = dword_671020[slot]
            out.cursorX   = e.packedX;     // word_672216  = word_670FE6[slot]
            out.cursorY   = e.packedY;     // word_672218  = word_670FE8[slot]
            ring[slot].pending = false;    // dword_671028[slot] = 0
            found = slot;
            break;
        }
    }

    // Mirror the latched edges into the gui input globals the menu dispatch reads.
    // (In the original these ARE the same dword_672228 / dword_672220 selectors;
    // here the gui module owns those globals, so the pump publishes into them.)
    gui::g_mouseClick = out.clickEdge;
    gui::g_mouseDown  = out.heldEdge;
    return found;
}

// gilde.exe 0x40dab8 (PumpMessages + LatchMouseState head) — the menu/HUD input
// pump wired to the real shim::IPlatform.
bool InputPumpAndLatch(shim::IPlatform& plat, LatchedInput& out) {
    // VIBE_Window_PumpMessages() — drain the OS message queue (returns false on quit).
    const bool alive = plat.pumpMessages();

    // Synthesize the pending event from the live pointer state, exactly as the
    // DirectInput cooked-input callback would have queued one ring slot: the left
    // button down becomes the held/click edge, the cursor x/y the packed coords.
    shim::MouseState m;
    plat.getMouse(m);
    MouseEvent slot;
    slot.pending   = true;
    slot.clickEdge = m.left ? 1 : 0;   // dword_670FF8 (left-click edge)
    slot.heldEdge  = m.left ? 1 : 0;   // dword_670FF0 (left held)
    slot.packedX   = static_cast<int16_t>(m.x);
    slot.packedY   = static_cast<int16_t>(m.y);

    MouseEvent ring[1] = {slot};
    InputLatchMouseState(ring, 1, out);
    return alive;
}

// ===========================================================================
// gilde.exe 0x529d08 — VIBE_Menu_RunMainMenu (the click-dispatch frame body).
// ---------------------------------------------------------------------------
//   VIBE_InitStateReader(v71);              // read input -> dword_672228 / 62D22C
//   if ( dword_672228 ) {                   // a click edge this frame
//     v22 = dword_62D22C;                   // the hovered/clicked widget id
//     if ( dword_62D22C != -1 ) { ...dispatch the id against each button id... }
//   }
//   if ( byte_67225C == 1 ) {               // ESC -> quit
//     dword_63CC48 = 1; dword_631614 = 1;
//   }
//   while ( VIBE_GameLogic_RunFrameLoop(...) );
// The per-button id->transition cascade IS the real gui::MainMenu_Dispatch.
// ===========================================================================
bool MenuDispatchFrame(const LatchedInput& in, int hoverObject, bool escDown,
                       MenuResult& result) {
    // if ( dword_672228 ) — only act on a click edge (dword_672228 != 0).
    if (in.clickEdge != 0 && hoverObject != -1) {
        // The original compares dword_62D22C against each stored button widget id;
        // gui::MainMenu_Dispatch IS that cascade (New Game / Load / Multiplayer /
        // Options / Credits / Quit), returning the armed close + session flags.
        gui::MainMenuTransition t = gui::MainMenu_Dispatch(hoverObject);
        result.item = t.item;
        if (t.close) {
            // dword_631614 = 1; word_63C740 |= sessionFlags.
            result.close = true;
            result.sessionFlags |= t.sessionFlags;
            // The Quit button sets dword_63CC48 (the program-quit flag).
            if (t.item == gui::MainMenuItem::kQuit)
                result.quit = true;
        }
    }

    // if ( byte_67225C == 1 ) { dword_63CC48 = 1; dword_631614 = 1; } — ESC quits.
    if (escDown) {
        result.quit = true;   // dword_63CC48 = 1
        result.close = true;  // dword_631614 = 1
    }

    // Continue the loop while no close was armed (the do/while exits when a
    // transition closes the menu; the original's RunFrameLoop return drives the
    // frame cadence, here bounded by the caller).
    return !result.close;
}

MenuResult MenuRunMainMenu(shim::IPlatform& plat, MenuClickSource& clicks,
                           int maxFrames) {
    // VIBE_Window_PumpMessages(); VIBE_Input_LatchMouseState(); — the pump head.
    // (Fade register, scene load, view-transform setup, the 8-button form build and
    // the radio-group create are DEFERRED render/gui leaves; see the header.)
    MenuResult r;
    LatchedInput in;
    for (int f = 0; f < maxFrames; ++f) {
        // Top-of-frame pump: latch input through the real shim platform.
        const bool alive = InputPumpAndLatch(plat, in);
        ++r.frames;

        // The synthesized pump only raises a click when this frame's script does;
        // override the latched edge from the click source so the dispatch fires
        // deterministically (the live ProcessMouseClicks edge in a headless run).
        const bool click = clicks.clickThisFrame(f);
        in.clickEdge = click ? 1 : 0;
        gui::g_mouseClick = in.clickEdge;

        const int  hover = clicks.hoverThisFrame(f);
        gui::g_hoverObject = hover;     // dword_62D22C (the hovered widget id)
        const bool esc = clicks.escThisFrame(f);

        const bool cont = MenuDispatchFrame(in, hover, esc, r);
        if (!cont || !alive)            // closed, or the OS requested a quit
            break;
    }
    return r;
}

// ===========================================================================
// gilde.exe 0x534bbc — the menu<->session outer-loop decision (the while(1) body).
// ---------------------------------------------------------------------------
//   VIBE_Menu_RunMainMenu(); if ( dword_63CC38 ) break;   // restart display
// LABEL_97:
//   dword_63CC34 = 1;
//   if ( word_63C740 && !dword_63CC48 )                    // a session armed & !quit
//     for ( i=0; i != dword_63CC34; ) InitOrLoadSession(i, ...);
//   if ( dword_63CC48 ) { ...full shutdown...; return 1; } // quit
// ===========================================================================
MenuMainNext MenuMainDecide(const MenuResult& r, bool restartDisplay) {
    // if ( dword_63CC48 ) -> quit (the highest-priority exit).
    if (r.quit)
        return MenuMainNext::kQuit;
    // if ( dword_63CC38 ) break; -> tear down + re-init the display, then re-enter.
    if (restartDisplay)
        return MenuMainNext::kRestartDisplay;
    // if ( word_63C740 && !dword_63CC48 ) -> a session was armed: run it.
    if (r.close && r.sessionFlags != 0)
        return MenuMainNext::kRunSession;
    // Otherwise loop back to the menu (the option screens / cancelled transitions).
    return MenuMainNext::kRestartMenu;
}

} // namespace guild::app
