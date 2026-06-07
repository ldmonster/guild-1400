#pragma once
// =============================================================================
// guild::play — NATIVE CREDITS SCREEN (the main-menu "Credits" button target).
//
// The real boot spine reaches the credits crawl through the main menu:
//   VIBE_Menu_RunMainMenu @0x529d08 -> (Credits button) -> VIBE_Menu_RunCreditsScroll
//   @0x56e524.  That original (decompile-verified):
//     * fades to black, renders the 3D scene, then draws the credits BACKGROUND
//       via VIBE_Window_RenderEntityList(1792) — gfx record _CREDITS_BACKGROUND
//       (#1792, 800x600);
//     * creates a crawl window  VIBE_Window_Create(100, 0, screenW, 600, 16);
//     * fills it with rich text id 0x1BDF (7135) — the credit lines;
//     * seeds the scroll offset  *(window+584) = -(screenH)  (text starts fully
//       below the window, scrolls UP);
//     * per frame: advances the offset every `step` frames  (if (!(frame%step))
//       ++offset), where the step is rate-ramped off the frame-time metric
//       dword_631630  (>=80 -> 4, >=30 -> 2, else 1);
//     * finishes when  textBottom < textHeight*1.5 + offset  (dbl_625324 == 1.5),
//       or on ESC / click (dword_672230 || byte_67225C).
//
// The scroll ARITHMETIC is the reconstructed gui::credits model (gui/credits.h:
// Credits_InitialOffset / Credits_ScrollStep / Credits_AdvanceOffset /
// Credits_ScrollComplete) — this module DRIVES that real model frame-by-frame and
// paints it: the real _CREDITS_BACKGROUND backdrop + the credit lines scrolling
// upward at the real per-frame advance, into an IGraphicsDevice, reading the
// SDL pointer/keys via IPlatform.  Backend-agnostic (headless-testable with a
// MemoryGraphicsDevice + scripted IPlatform), drives real Vulkan+SDL unchanged.
//
// REAL vs INERT: the scroll model + window geometry + background record id are
// the REAL recovered values.  The credit TEXT itself is rich-text resource 7135,
// which lives in the localized text DB and is NOT reconstructed as a literal list
// (BuildTextArray is deferred) — so this screen scrolls a FAITHFUL DEFAULT credit
// list (kDefaultCreditLines).  Pass cfg.lines to supply real strings.
// =============================================================================
#include <cstdint>
#include <string>
#include <vector>

namespace guild::shim { class IGraphicsDevice; class IPlatform; }

namespace guild::play {

struct CreditsScreenConfig {
    std::string gameDir;        // mounted game dir (for gfx/gilde.gfx); empty -> no art
    int fbW = 800, fbH = 600;
    int maxFrames  = -1;        // -1 = until finished/ESC/close (tests bound it)
    int frameCapMs = 16;        // per-frame sleep (~60fps); 0 = uncapped

    // The credit lines to crawl.  Empty -> kDefaultCreditLines (a faithful stand-in
    // for the un-reconstructed rich-text resource 7135).
    std::vector<std::string> lines;

    // Frame-time metric feeding the speed ramp (dword_631630).  Default 30 -> the
    // mid step (2), matching the original's typical steady-state.  >=80 -> 4, <30 -> 1.
    float frameTimeMetric = 30.0f;

    // Pixel height of one credit text line (used for the textHeight geometry that
    // the scroll-complete predicate compares against).  9 == the 5x7 glyph + gap.
    int lineHeight = 9;
};

struct CreditsScreenResult {
    bool finished = false;      // crawl reached the end (Credits_ScrollComplete)
    bool back     = false;      // ESC / click / window-close returned early
    bool quitByWindow = false;
    bool quitByEsc    = false;
    bool quitByClick  = false;
    int  scrollOffset = 0;      // final scroll offset (window+584)
    int  framesPresented = 0;
    bool haveAssets = false;    // the real _CREDITS_BACKGROUND decoded (e2e-visible)
};

// The faithful default credit list (used when cfg.lines is empty — the real
// rich-text resource 7135 is not reconstructed).
extern const std::vector<std::string>& DefaultCreditLines();

// gilde.exe 0x56e524 — render + run the credits crawl over the real
// _CREDITS_BACKGROUND until the crawl finishes, ESC/click returns, the window
// closes, or cfg.maxFrames is reached.  `device` MUST be init()'d to cfg.fbW x
// cfg.fbH (any bpp; composited 32bpp then converted).  `plat`'s window MUST exist.
CreditsScreenResult RunCreditsScreen(shim::IGraphicsDevice& device,
                                     shim::IPlatform& plat,
                                     const CreditsScreenConfig& cfg);

} // namespace guild::play
