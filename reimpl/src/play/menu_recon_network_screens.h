#pragma once
#include "guild/common/types.h"

#include <string>
#include <vector>

namespace guild::shim { class IGraphicsDevice; class IPlatform; }

// =============================================================================
// guild::play — network / choose-history / round-end menu screen drivers of
// gilde.exe. Faithful 1:1 reconstructions of the embedded *value / index / string*
// math; the surrounding form/widget/frame-loop orchestration is routed through an
// inert-default hooks vtable so this translation unit is self-contained.
//
//   0x529074  VIBE_Menu_EnterNetworkIp            (host-IP entry field editor)
//   0x52dabc  VIBE_Menu_RunChooseHistoryNetwork   (history variant + new-game flow)
//   0x52df18  VIBE_Menu_ChooseHistoryVariant      (history variant radio screen)
//   0x530bcc  VIBE_Menu_ShowPlayerRoundEndReport  (per-player round-end report)
//   0x56a4d4  VIBE_Menu_RunLoadNetworkGame        (network load-game browser)
//   0x56abcc  VIBE_Menu_RunSaveNetworkGame        (network save-game browser)
//
// These are UI event loops in the original; the parts reproduced *exactly* here are
// the pieces with observable arithmetic/semantics independent of the GUI backend:
//
//   * the radio-selection index conversion (-1 -> 0, else idx+1) used to seed and
//     update VIBE_Selection_Update (ChooseHistory variant + network);
//   * the button-id -> history-variant code mapping tables (byte_63C8F4 / 63C8F0);
//   * the wide-char (2-byte stride, NUL-terminated) string copy used to lift the
//     edited IP text into byte_122EE90 / the chosen save-name into byte_122F530;
//   * the save/load slot-table scan strides (544-byte records, 16-slot caps, the
//     952*windowId byte index) and the "%s.SAV" / "Gamedata\network\%s.SAV" paths.
//
// The widget creation, frame pump, text render, INI read/write, fades and cutscene
// launches are engine edges driven through hooks. None of the existing menu/HUD
// symbols are redefined here (this file adds only the not-yet-present 0x529074 /
// 0x52dabc / 0x52df18 / 0x530bcc / 0x56a4d4 / 0x56abcc drivers).
// =============================================================================
namespace guild::play {

// ---------------------------------------------------------------------------
// Shared pure helpers (used by several of the screens above).
// ---------------------------------------------------------------------------

// gilde.exe (52e052 / 52dbaf / 52e0e8) — radio selection seed/update conversion:
//   sel == -1  -> 0
//   else       -> sel + 1
// (VIBE_Selection_Update is called with this; index 0 == "nothing selected".)
inline int RadioSelectionIndex(int sel) { return sel == -1 ? 0 : sel + 1; }

// gilde.exe (52e0dc / 52dc42) — extract the radio-group result code from the packed
// dword at byte offset +1 of the group state: `*(int*)((char*)&dword_63C8F0 + 1) >> 24`.
// byte_63C8F4 holds the selected variant (-1..5); 63C8F0+1>>24 reads that byte
// sign-extended. We model it as: take the signed 8-bit variant code directly.
inline int HistoryVariantFromByte(i8 variantByte) { return (int)variantByte; }

// gilde.exe (4416?) — the ubiquitous wide-text copy: source is a 2-byte-stride
// "fat char" buffer (low byte = char, high byte = attribute); copy until a low byte
// of 0 is hit, preserving the 2-byte stride into `dst`. Returns bytes written
// (including the terminating pair's first byte). `dst` must hold at least
// 2*(len+1) bytes. This mirrors the do/while loops at 0x529181, 0x52917e, 0x56a63e.
int CopyFatString(u8* dst, const u8* src, int dstCap);

// ---------------------------------------------------------------------------
// 0x52df18 — VIBE_Menu_ChooseHistoryVariant: button-id -> variant code.
// The screen has 7 selectable rows; clicking row k sets byte_63C8F4 to:
//   row0 (ChildObjectId) -> -1   row1 -> 0   row2 -> 1   row3 -> 2
//   row4 -> 3              row5 -> 4   row6 -> 5
// (i.e. the first row is the "none/back" sentinel, then 0..5).
// ---------------------------------------------------------------------------
inline constexpr int kHistoryVariantRows = 7;
// Map a clicked row (0..6) to the variant code stored in byte_63C8F4.
inline int HistoryVariantForRow(int row) { return row == 0 ? -1 : row - 1; }

// 0x52dabc — VIBE_Menu_RunChooseHistoryNetwork uses a *different* mapping
// (only 5 rows wired): row0 -> -1, row1 -> 1, row2 -> 2, row3 -> 3, row4(extra) -> 5.
inline int NetworkHistoryVariantForRow(int row) {
    switch (row) {
        case 0: return -1;
        case 1: return 1;
        case 2: return 2;
        case 3: return 3;
        case 4: return 5;   // the optional %it[%s] row when byte_63CC1D > 1
        default: return -1;
    }
}

// ---------------------------------------------------------------------------
// 0x56a4d4 / 0x56abcc — network save/load slot browser geometry.
//   * slot metadata records are 544 bytes (0x220) wide;
//   * the load loop caps at 8704 bytes (16 records) — v8 >= 8704 ends the scan;
//   * the save loop caps at 16 records (v34 >= 16);
//   * the per-player person stride is 238 dwords (dword_67EB80[238*idx]);
//   * the window object index uses 952*windowId (byte index into the object array);
//   * the slider panel is built at (532,360) with arg 130.
// ---------------------------------------------------------------------------
inline constexpr int kSaveSlotStride   = 544;   // bytes per slot metadata record
inline constexpr int kSaveSlotScanEnd  = 8704;  // load-loop byte cap (16 * 544)
inline constexpr int kSaveSlotMax      = 16;    // save-loop record cap
inline constexpr int kPlayerRecordStride = 238; // dwords per player (dword_67EB80)
inline constexpr int kWindowObjectStride = 952; // 952 * windowId

// gilde.exe 0x530bfc — round-end report building-record stride: 589 * type + base.
inline constexpr int kBuildingRecordStride = 589;   // dword_13CE294-relative

// gilde.exe 0x530bcc — round-end report value selection. Given the production-worth
// fields the report renders a line per nonzero value with the listed rich-text ids.
// This helper reproduces the *predicate* the original uses for the v19 "sale value"
// line (id 0x1C1E vs 0x1C1F) — chosen by the building's type byte:
//   type == 4 || type == 16 || type == 19  -> 0x1C1E   else 0x1C1F
inline int RoundEndSaleLineId(i8 buildingType) {
    return (buildingType == 4 || buildingType == 16 || buildingType == 19)
               ? 0x1C1E : 0x1C1F;
}
// And the profit/loss line (v21): >0 -> 0x1C21, <0 -> 0x1C22 (with v19+v21), ==0 none.
inline int RoundEndProfitLineId(int v21) {
    return v21 > 0 ? 0x1C21 : (v21 < 0 ? 0x1C22 : 0);
}

// =============================================================================
// Driver hooks. Each screen below is the original event loop, with the engine
// edges (form lifecycle, frame pump, text render, INI, fades, cutscenes, dialogs)
// routed through these. The pure math above is applied inline at the matching
// decompiled sites. Defaults are inert (loops run zero iterations / return 0).
// =============================================================================
struct MenuScreenHooks {
    // VIBE_GameTick_Finalize(a,b, formName) -> form id.
    int (*loadForm)(const char* formName) = nullptr;
    // VIBE_Form_Destroy(formId).
    void (*destroyForm)(int formId) = nullptr;
    // VIBE_GameLogic_RunFrameLoop(state, sel, scratch) -> keep-running (0 ends loop).
    int (*runFrameLoop)(int state, int sel, void* scratch) = nullptr;
    // VIBE_Selection_Update(group, index) — apply a converted radio index.
    void (*selectionUpdate)(int group, int index) = nullptr;
    // INI: GetPrivateProfileStringA("Network","Host","127.0.0.1", out, 0x104, ini).
    void (*iniReadHost)(char* out, int cap) = nullptr;
    // INI: WritePrivateProfileStringA("Network","Host", value, ini).
    void (*iniWriteHost)(const char* value) = nullptr;
};
const MenuScreenHooks* SetMenuScreenHooks(const MenuScreenHooks* hooks);

// gilde.exe 0x529074 — host-IP entry screen. Returns 1 if a value was committed.
int Menu_EnterNetworkIp();

// gilde.exe 0x52df18 — history-variant radio screen. `historySel` is dword_12335B4
// (the persisted selection, mutated in place). Returns 1 once a row was chosen.
int Menu_ChooseHistoryVariant(i32& historySel);

// gilde.exe 0x52dabc — network new-game launch flow. `historySel` is the persisted
// dword_1233558[22] selection. Returns the launch result (v6).
int Menu_RunChooseHistoryNetwork(i32& historySel);

// gilde.exe 0x530bcc — per-player round-end report. `playerType` is *a1 (the type
// byte that selects the building record and the sale-line id).
void Menu_ShowPlayerRoundEndReport(i8 playerType);

// gilde.exe 0x56a4d4 — network load-game browser. Returns 1 if a slot was chosen.
int Menu_RunLoadNetworkGame();

// gilde.exe 0x56abcc — network save-game browser. Returns v29.
int Menu_RunSaveNetworkGame();

void ResetMenuReconNetworkScreens();

// =============================================================================
// 1:1 VIEW of the NETWORK sub-screens (the main-menu "Сетевая игра" leaf tree):
//   * the choose-network-mode HUB    (Menu\CHOOSENETWORK,    fn 0x529a64)
//   * the host-IP entry leaf          (Menu\CHOOSENETWORK_IP, fn 0x529074)
//   * the LAN server-search leaf      (Menu\SEARCH_NETWORK,   fn 0x529248)
//
// These mirror the proven native_main_menu.cpp model: render the real
// `_OPTIONEN_PIC` background full-screen, lay every form window/object at its
// NATIVE 800x600 position CENTER-TRANSLATED by ((fbW-800)/2,(fbH-600)/2) (never
// scaled), draw `_BUTTON_RED` 3-slice buttons + real `_FONT` glyphs with the
// localized `_OPTIONEN_NETZWERK_*` labels (resolved from the textbin). When the
// game dir / gfx / font are absent (headless), a flat fallback render is used so
// the layout is still pinnable. The render is a pure framebuffer paint into a
// caller-supplied 32bpp 0xAARRGGBB buffer; hit-testing uses the returned rects.
//
// IMPORTANT (decompile is the reference of record, gilde.exe 0x529a64): the HUB
// is a RADIO COLUMN of THREE rows (host / search-join / load-profile), built from
// rich-string ids 0x18B3/0x18B4/0x18B5. The `_OPTIONEN_NETZWERK_MENUE+1..5`
// textbin entries are the localized captions those rich-strings reference; we
// resolve and render the first three (the live wired rows) by name.
// =============================================================================

struct NetViewRect { int x = 0, y = 0, w = 0, h = 0; };

// The HUB layout the renderer produced (one rect per built radio-button row, in
// the live build order: 0=host, 1=search/join, 2=load-profile), the title rect,
// and the resolved (rich-text-stripped, CP1251) captions. Returned so callers /
// tests can hit-test exactly what was drawn.
struct NetHubLayout {
    NetViewRect window;        // CHOOSENETWORK form window (native, translated)
    NetViewRect title;         // title text box
    int         buttonCount = 0;
    NetViewRect buttons[3];    // host / search / profile
    std::string labels[3];     // resolved localized captions (may be empty)
    std::string titleLabel;    // resolved title caption (may be empty)
    bool        usedArt = false; // true when real gfx+font were rendered
};

// The IP-entry layout (CHOOSENETWORK_IP).
struct NetIpLayout {
    NetViewRect window;
    NetViewRect title;
    NetViewRect prompt;        // "Enter IP:" label (form (16,56))
    NetViewRect input;         // the editable IP field (form (16,88))
    NetViewRect okButton;      // the OK/connect button
    std::string titleLabel;
    std::string promptLabel;
    std::string okLabel;
    bool        usedArt = false;
};

// The server-search layout (SEARCH_NETWORK).
struct NetSearchLayout {
    NetViewRect window;
    NetViewRect title;
    NetViewRect status;        // the "waiting / N found" status line
    int         rowCount = 0;
    std::vector<NetViewRect> rows;     // one per discovered server row
    NetViewRect connectButton; // _OPTIONEN_NETZWERK_BUTTON_CONNECT (v50[0])
    NetViewRect refreshButton; // _OPTIONEN_NETZWERK_BUTTON_REFRESH (v50[1])
    NetViewRect directButton;  // _OPTIONEN_NETZWERK_BUTTON_DIRECT  (v50[2])
    NetViewRect cancelButton;  // _OPTIONEN_NETZWERK_BUTTON_CANCEL
    std::string titleLabel;
    std::string statusLabel;
    std::string connectLabel, refreshLabel, directLabel, cancelLabel;
    bool        usedArt = false;
};

// Inputs shared by the three renderers.
struct NetViewContext {
    std::string gameDir;     // install root (for _OPTIONEN_PIC / _FONT / textbin)
    int  fbW = 800;          // framebuffer size (native 800x600 centred inside)
    int  fbH = 600;
    int  hover = -1;         // hovered row/button index (for the hover frame), -1 = none
};

// Render the choose-network-mode HUB into `dst` (32bpp 0xAARRGGBB, fbW*fbH).
// Returns the produced layout (rects + resolved labels).
NetHubLayout RenderNetworkHubView(u32* dst, const NetViewContext& ctx);

// Render the host-IP entry screen. `typedIp` is the current edit-field text
// (seeded from the INI "Host", default "127.0.0.1") drawn inside the input box.
NetIpLayout RenderNetworkIpView(u32* dst, const NetViewContext& ctx,
                                const std::string& typedIp);

// Render the LAN server-search screen. `servers` is the discovered-server display
// list (one row each); `statusOverride` (if non-empty) replaces the default
// "waiting" status line (e.g. the "N servers found" message).
NetSearchLayout RenderNetworkSearchView(u32* dst, const NetViewContext& ctx,
                                        const std::vector<std::string>& servers,
                                        const std::string& statusOverride = std::string());

// Strip rich-text markup ($X / %X codes, [..] brackets kept inner) from a textbin
// entry to its plain display string (CP1251 bytes preserved). Exposed for tests.
std::string StripNetRichMarkup(const char* s);

// =============================================================================
// RunNetworkScreen — the live driver for the main-menu "Сетевая игра" leaf
// (VIBE_Menu_ChooseNetworkMode @0x529a64). Loads the gfx/font/labels ONCE, then
// runs the hub frame loop on the host window: hover/click the three radio rows
// (host / search-join / load-profile), ESC/close → back to the menu. The chosen
// option enters its sub-screen VIEW (the LAN server-search list, the host-IP
// entry) — these render 1:1 and are cancellable.
//
// RULE 6 (networking is NOT a pre-approved swap): the actual LAN discovery /
// socket connect / hosting is NOT performed here. The search list stays empty
// (no fake servers), and CONNECT/host/profile are gated no-ops that return to the
// hub. The MENU is 1:1 and reachable; wiring real multiplayer transport needs the
// user's go-ahead. `chosenMode` reports which option was activated for the caller.
// =============================================================================
struct NetworkScreenConfig {
    std::string gameDir;
    int fbW = 800, fbH = 600;
    int frameCapMs = 16;
    int maxFrames  = -1;     // -1 = until back/close (tests bound it)
};
struct NetworkScreenResult {
    bool confirmed    = false;   // a network session was launched (always false: rule 6)
    bool back         = false;   // ESC / back -> return to the main menu
    bool quitByWindow = false;
    int  chosenMode   = -1;      // 0=host, 1=search/join, 2=load-profile, -1=none
    int  sessionFlags = 0;       // word_63C740 (5 host/search, 4 profile) when chosen
    int  framesPresented = 0;
    int  hoveredRow   = -1;
};
NetworkScreenResult RunNetworkScreen(shim::IGraphicsDevice& device,
                                     shim::IPlatform& plat,
                                     const NetworkScreenConfig& cfg);

} // namespace guild::play
