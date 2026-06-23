#pragma once
// ============================================================================
// Die Gilde 1:1 — Groundplan / blueprint ("Riss") window (gilde.exe 0x4ae3b8..
// 0x4b0992). This module reconstructs the *coupled* driver functions of the
// ground-plan window that src/world/groundplan_recon.{h,cpp} deliberately
// OMITTED (it kept only the pure layout/eligibility math). Together the two
// files cover the whole VIBE_Groundplan_* cluster.
//
// FUNCTIONS RECONSTRUCTED HERE (1:1 from the Hex-Rays decompile):
//   0x4ae3b8  VIBE_Groundplan_CreateWindow        -> Groundplan_CreateWindow
//   0x4ae678  VIBE_Groundplan_SetWidgetsVisible   -> Groundplan_SetWidgetsVisible
//   0x4ae828  VIBE_Groundplan_DestroyWidgets      -> Groundplan_DestroyWidgets
//   0x4aea5c  VIBE_Groundplan_LoadBlueprintBmp    -> Groundplan_LoadBlueprintBmp
//   0x4af038  VIBE_Groundplan_RenderBlueprint     -> Groundplan_RenderBlueprint
//   0x4af4a8  VIBE_Groundplan_BuildInfoPanel      -> Groundplan_BuildInfoPanel
//   0x4b0758  VIBE_Groundplan_FadeInScene         -> Groundplan_FadeInScene
//
// COUPLING. The originals are wired into ~50 engine globals (widget-handle
// slots dword_6316E4..dword_631708, surface globals, the screen-resolution
// pair dword_69FFB8/dword_69FFBC, the selected-plot index word_63CC5C, the
// blueprint-bmp surfaces dword_631640/631644, …) and ~30 subsystem calls
// (window/widget/object/form/surface/animation/text). Reproducing them with
// approximate stand-ins would violate rule 8, so instead of faking the engine
// we EXPOSE it: all mutable globals live in `GroundplanState` and every
// subsystem call is routed through `GroundplanBackend` (a vtable of the real
// engine leaves, wired at the call site in production). The CONTROL FLOW,
// CONSTANTS, PATH-SELECTION TABLE and PER-PIXEL BMP SCAN are transcribed
// byte-for-byte; only the leaf calls are indirected. With a null/recording
// backend the path-selection + pixel-scan logic is exercised in isolation
// (see tests).
//
// The genuinely *reconstructable leaf logic* (rule 8: not faked) is:
//   * the blueprint-bmp filename selection switch (riss_%s.bmp + the per
//     building-category special cases), transcribed from 0x4aea5c, and
//   * the "_C.bmp" collision-map name derivation + per-pixel marker scan that
//     spawns the room hotspot widgets (0x4aea5c inner loops), and
//   * the info-panel clock/season layout math (0x4af4a8) incl. the ConvertX
//     truncations and fmod time-of-day hand math.
// ============================================================================
#include "guild/common/types.h"
#include <string>

namespace guild {
namespace gui {

// ---------------------------------------------------------------------------
// GroundplanState — the mutable globals the originals read/write. Field names
// carry the gilde.exe BSS address. Sentinel for an "empty widget slot" is -1
// (matching the binary). All default to the engine's cleared state.
// ---------------------------------------------------------------------------
struct GroundplanState {
    // widget-handle slots (the room-hotspot + label set the panel manages)
    i32 widE4 = -1;   // dword_6316E4
    i32 widE8 = -1;   // dword_6316E8
    i32 widEC = -1;   // dword_6316EC  (city-wappen sprite)
    i32 widF0 = -1;   // dword_6316F0
    i32 widF4 = -1;   // dword_6316F4
    i32 widF8 = -1;   // dword_6316F8
    i32 widFC = -1;   // dword_6316FC  (clock hand sprite)
    i32 wid704 = -1;  // dword_631704  (item label)
    i32 wid708 = -1;  // dword_631708  (money label)
    i32 wid700 = -1;  // dword_631700  (the window handle)
    i32 widAC = -1;   // dword_6317AC  (scroll/zoom child object)
    i32 hotspot710 = -1;   // dword_631710 (top-right hotspot)
    i32 roomHotspots[15] = {  // dword_11BC1F4[15] room pick widgets
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1 };

    // surfaces
    i32 surf63163C = 0;   // dword_63163C  (grabbed window backdrop shape)
    i32 surf631640 = 0;   // dword_631640  (blueprint bmp working surface)
    i32 surf631644 = 0;   // dword_631644  (panel render-target surface)

    // active selection / inputs
    i32 selectedPlot = 0; // word_63CC5C   (selected plot index, 16-bit)
    i32 buildingPtr744 = 0;  // dword_631744 (active building record ptr, or 0)
    i32 buildingPtr748 = 0;  // dword_631748 (alt building record ptr)
    i32 sceneFlag714 = 0;    // dword_631714 (scene id stashed by CreateWindow)
    u8  stateByte6317B5 = 0; // byte_6317B5  (office-storage state, set by GetBuildingState)

    // RenderBlueprint dirty-tracking cache
    i32 cacheDB8 = 0;   // dword_631DB8 (last dword_649D60)
    i32 cacheDBC = 0;   // dword_631DBC (last dword_631744)
    i32 cacheDC0 = 0;   // dword_631DC0 (last dword_63174C)
    i32 dirtyDB4 = 0;   // dword_631DB4 (force-rebuild flag)

    // info-panel scroll-state cache
    i32 cacheDC4 = 0;   // dword_631DC4
    i32 cacheDC8 = 0;   // dword_631DC8

    // screen resolution (read as the two high words). screenW = hi16(res6,9FFB8),
    // screenH = hi16(dword_69FFBC).
    i32 screenW = 800;  // hi16(dword_69FFB8)  (the original reads +2>>16)
    i32 screenH = 600;  // hi16(dword_69FFBC)
};

// ---------------------------------------------------------------------------
// GroundplanBackend — every engine leaf the cluster calls. In production these
// are wired to the real reconstructed subsystems (window.cpp/object.cpp/
// surface/etc.); the headless build leaves them null/no-op so the
// reconstructable path-selection + pixel-scan math runs in isolation.
//
// Naming: each member is the gilde.exe symbol it stands for. Pointers are
// modelled as i32 handles (the engine passed opaque dword handles).
// ---------------------------------------------------------------------------
struct GroundplanBackend {
    // --- building-type table accessors (already in src/sim) -----------------
    // 0x5878b0 VIBE_Building_MapTypeToCategory(typeByte) -> category.
    u8 (*MapTypeToCategory)(u8 typeByte) = nullptr;

    // --- picture / surface --------------------------------------------------
    // 0x422ae4 VIBE_Picture_CreateSurfaceFromBmp(path) -> surface handle (0 fail).
    i32 (*PictureCreateSurfaceFromBmp)(const char* path) = nullptr;
    // 0x4234b0 VIBE_Surface_Destroy.
    void (*SurfaceDestroy)(i32 surf) = nullptr;
    // 0x42311c VIBE_Surface_Create(w,h,bpp) -> handle.
    i32 (*SurfaceCreate)(int w, int h, int bpp) = nullptr;
    // 0x423b6c VIBE_Surface_ColorFill.
    void (*SurfaceColorFill)(i32 surf) = nullptr;
    // 0x423500 VIBE_DecompressState_Blob(surf) -> nonzero on a locked/decoded surf.
    int (*SurfaceLock)(i32 surf) = nullptr;
    // 0x4235dc VIBE_Decompression_Finalize(surf).
    void (*SurfaceUnlock)(i32 surf) = nullptr;
    // 0x423d74 VIBE_Surface_GetPixelRgb(x,y,&rgb[3],surf).
    void (*GetPixelRgb)(int x, int y, u8 out[3], i32 surf) = nullptr;
    // 0x423e5c VIBE_Surface_SetPixelRgb(x,y,r,g,b,surf) (note channel order below).
    void (*SetPixelRgb)(int x, int y, u8 r, u8 g, u8 b, i32 surf) = nullptr;
    // surface width/height accessors (the original read *(surf+4)/*(surf+8)).
    int (*SurfaceWidth)(i32 surf) = nullptr;
    int (*SurfaceHeight)(i32 surf) = nullptr;

    // --- colour compare -----------------------------------------------------
    // 0x4226bc VIBE_Color_NotEqualRgb(&a,&b) -> nonzero when the two RGB triples differ.
    int (*ColorNotEqualRgb)(const u8* a, const u8* b) = nullptr;

    // --- window / widget / object -------------------------------------------
    i32 (*WindowCreate)(int a,int b,int c,int d,int e) = nullptr;            // 0x419c38
    void (*WindowPositionAtCoord)(i32 win,int v) = nullptr;                  // 0x41d7e0
    void (*WindowPositionCentered)(i32 win,int v) = nullptr;                 // 0x41d764
    i32 (*HotspotRegister)(int x,int y,int w,int h,int v) = nullptr;        // 0x421ab8
    void (*CoordPush)(int x,int y,int w,int h) = nullptr;                    // 0x5d8ae8
    i32 (*WidgetCreateObject)(i16 x,i16 y) = nullptr;                        // 0x413214
    void (*WidgetDestroyByType)(i32 wid) = nullptr;                          // 0x414f98
    void (*WidgetSetTooltipText)(const char* s) = nullptr;                   // 0x421a24
    void (*ObjectSetVisibleRecursive)(i32 obj,int vis) = nullptr;           // 0x41dd98
    void (*FormSetObjectsVisible)(i32 form,int vis) = nullptr;              // 0x41d634
    void (*EventPanelSetBarVisible)(int vis) = nullptr;                      // 0x4c5918

    // --- room pick / building queries ---------------------------------------
    // 0x5857fc VIBE_GameObject_QueryFind / 0x58529c IterNext: iterate room objs.
    i32 (*GameObjectQueryFind)(i32 ctx,int a,int b,int slot) = nullptr;
    i32 (*GameObjectIterNext)() = nullptr;
    // 0x587fcc VIBE_Building_LookupTypeStringId(obj) -> -1 if no plan label.
    int (*BuildingLookupTypeStringId)(i32 obj) = nullptr;

    // resolved engine paths/strings used by the bmp loader:
    // byte_69FE80 = the asset base directory (e.g. game dir). Empty -> "".
    const char* assetBaseDir = "";

    // optional capture sink: the room-hotspot widget creates record their
    // (x,y) here for tests. nullptr in production.
    void (*OnRoomHotspot)(int x, int y) = nullptr;
};

// ---------------------------------------------------------------------------
// 0x4aea5c — VIBE_Groundplan_LoadBlueprintBmp.
//   Picks the riss_*.bmp path from the active building's category/type, loads
//   it, copies it (channel-swapped) into the panel render surface, derives the
//   "_C.bmp" collision map, scans it per-pixel for marker colours and spawns a
//   room-hotspot widget per matched room object.
//
//   `typeByte`        = *buildingPtr (the building's type-code byte).
//   `subTypeByte`     = the byte at (589*typeByte + dword_13CE294) used by the
//                       category special-cases (riss_parfuemerie/tinkturei/…).
//   `roomTypeByte`    = the byte at (589*typeByte + dword_13CE294) checked for
//                       9/7/19/etc. in the default category branch.
// Returns the chosen blueprint path (so callers/tests can golden-pin it).
// ---------------------------------------------------------------------------
std::string Groundplan_LoadBlueprintBmp(GroundplanState& st,
                                        const GroundplanBackend& be);

// Pure helper, exposed for golden-pinning: choose the riss_*.bmp file basename
// (without the "%sbmp\groundplans\" prefix) from a building category + the
// building's type byte and room-type byte. Mirrors the 0x4aea5c switch exactly.
//   category : VIBE_Building_MapTypeToCategory(typeByte)
//   typeByte : *buildingPtr            (checked == 22/15/1/5 etc.)
//   roomByte : *(589*typeByte+13CE294) (checked == 14/8/9/7/19)
// Returns e.g. "riss_wirtshaus.bmp". The generic fallback is
// "riss_handwerksbetrieb.bmp".
std::string Groundplan_PickBlueprintName(u8 category, u8 typeByte, u8 roomByte);

// ---------------------------------------------------------------------------
// 0x4af038 — VIBE_Groundplan_RenderBlueprint. Blits the cached blueprint
// surface, dirty-rebuilds the room hotspots when the selection changed, and on
// hover (flag 0x2000) reads the collision-map pixel under the cursor to pick a
// room -> tooltip / enter-room. `flags` = a1 (the frame interaction flags),
// cursorX/cursorY = the de-fixed hover coordinates (the original derived them
// from unk_67220E/dword_672210 >> 16).
// ---------------------------------------------------------------------------
void Groundplan_RenderBlueprint(GroundplanState& st, const GroundplanBackend& be,
                                int flags, int cursorX, int cursorY, bool hoverActive);

// ---------------------------------------------------------------------------
// 0x4ae3b8 — VIBE_Groundplan_CreateWindow. Registers the hotspot, creates the
// 648px window, grabs the backdrop shape, clears the 15 room slots, stashes the
// city-rect, builds the panel surface and the scroll child object.
//   sceneId = a1.  cityRect = {x0,y0,x1,y1} from dword_63CC4C/50/54/58.
// ---------------------------------------------------------------------------
void Groundplan_CreateWindow(GroundplanState& st, const GroundplanBackend& be,
                             int sceneId);

// ---------------------------------------------------------------------------
// 0x4ae678 — VIBE_Groundplan_SetWidgetsVisible(vis). Toggles every managed
// widget/object + the event-panel bar. `vis` nonzero shows, 0 hides.
// ---------------------------------------------------------------------------
void Groundplan_SetWidgetsVisible(GroundplanState& st, const GroundplanBackend& be,
                                  int vis);

// ---------------------------------------------------------------------------
// 0x4ae828 — VIBE_Groundplan_DestroyWidgets. Tears down every managed widget
// (resetting its slot to -1), hides the form + scroll child.
// ---------------------------------------------------------------------------
void Groundplan_DestroyWidgets(GroundplanState& st, const GroundplanBackend& be);

// ---------------------------------------------------------------------------
// 0x4b0758 — VIBE_Groundplan_FadeInScene. Fills the scene surface, runs one
// info-panel build + a black fade-in frame loop. Modelled through the backend;
// returns nothing observable beyond the side effects.
// ---------------------------------------------------------------------------
void Groundplan_FadeInScene(GroundplanState& st, const GroundplanBackend& be);

// ---------------------------------------------------------------------------
// 0x4af4a8 — VIBE_Groundplan_BuildInfoPanel(rebuild). The full info-panel
// layout: (re)creates the label/sprite widgets, renders the blueprint, formats
// the date / season / clock-hand sprites, the item label, the city-wappen and
// the money total. `rebuild` = a1 (bit0 set: in-place refresh; clear: full
// teardown+recreate). The float->int clock math uses ConvertX truncation
// (0x5c6b08) exactly. Returns the original's `result` (last money/text value).
//
// Because the per-widget layout writes go through opaque widget records the
// original addresses by `(740*handle + dword_69FFB4 + field)`, the layout
// field writes are routed through the backend's widget-record accessors when
// present; the date/season/clock MATH (the load-bearing, reconstructable part)
// is computed here exactly. `clock` carries the packed game-time inputs.
// ---------------------------------------------------------------------------
struct GroundplanClockInputs {
    // qword_13CE854 packed game time, qword == (hi<<32)|lo. The original extracts:
    //   minutes  = (qword >> 32) % 60                    == hi % 60
    //   secsTotal= 60*WORD1(qword) + HIDWORD(qword)
    //   hour     = WORD1(qword), sec = HIDWORD(qword)  (for the %02i:%02i text)
    // where WORD1(qword) = bits 16..31 = (lo >> 16) & 0xFFFF (the SECOND 16-bit
    // word, in the LOW dword) and HIDWORD(qword) = hi.
    u32 lo = 0;   // low 32 bits
    u32 hi = 0;   // high 32 bits
    int year = 0;     // v130[0] >> 16 (A.D. year shown)
    int season = 0;   // VIBE_GameTime_GetSeasonFromYear result (index into season names)
    float weatherPhase = 0.0f;  // *(float*)(dword_13FCD1C+136) — drives the moon hand
    bool hasMoon = false;       // !(631748||631744||...) branch selector
};

// Clock-hand layout outputs (pure, golden-pinnable): the three sprite cell
// indices the panel computes from the packed time. Reconstructed 1:1 incl. the
// ConvertX truncation and the %48 / %60 wraps.
struct GroundplanClockHands {
    int minuteCell = 0;  // v133 = (int)(minutes * 0.8)         [no wrap]
    int hourCell = 0;    // v131 = (int)(secsTotal/60 * 4.0)%48
    int moonCell = 0;    // v132 = (int)(0.5 + fmod(phase,2pi)*(1/2pi)*48)%48
};

// Pure clock-hand math extracted from 0x4af4a8 (the ConvertX/fmod block).
// Exposed standalone for golden vectors; ConvertX truncates toward zero.
GroundplanClockHands Groundplan_ComputeClockHands(const GroundplanClockInputs& in);

int Groundplan_BuildInfoPanel(GroundplanState& st, const GroundplanBackend& be,
                              u8 rebuild, const GroundplanClockInputs& clock);

}  // namespace gui
}  // namespace guild
