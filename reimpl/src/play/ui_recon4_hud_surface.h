// ui_recon4_hud_surface.h — gilde.exe credit/HUD/surface UI pure logic (recon wave 4).
//
// Scope (PURE math + record-field reads only; window/GPU/frame-loop shells behind
// inert hooks):
//   0x553f30  VIBE_Hud_BuildPersonCard        — person-card layout/centering math
//   0x55433c  VIBE_Hud_BuildPersonCardSimple  — simple person-card layout math
//   0x4243d8  VIBE_Surface_DrawText           — GDI text colour-packing (COLORREF)
//   0x41eee8  VIBE_Paintbox_ClearAlt          — paintbox clear-rect gating + record index
//   0x51adb4  VIBE_Credit_ShowLenderDialog    — lender-dialog slot table + asset aggregation
//   0x51d9a4  VIBE_Credit_ShowAssetOverview   — asset-overview window param setup
//
// Additive: new file, namespace guild::play. No edits to existing files.
#pragma once
#include <cstdint>
#include "guild/common/types.h"

namespace guild {
namespace play {

using namespace guild;
using f32 = float;

// ===========================================================================
// Shared sprite-dimension layout from the global UI-theme record (dword_62D204).
// The original reads packed 32-bit cells whose HIWORD is a coordinate and whose
// (>>16) yields a pixel extent.  We model the three cells the card builders use:
//   +117762 (0x1CC02)  "card sprite" packed dims; (>>16) == card sprite WIDTH
//   +117764 (0x1CC04)  packed Y/extent;  HIWORD == y-offset, (>>16) == y-extent
//   +117766 (0x1CC06)  packed X offset;  low word == x-offset added to columns
//   +100038 (0x186C6)  fallback "no-record" sprite dims; (>>16) == width
// Portrait sprites live in a stride-84 table at dword_62D204; field +78 is the
// packed portrait dims, (>>16) == portrait WIDTH.
// ---------------------------------------------------------------------------
struct UiTheme {
    i32 cardSpriteDims;     // dword_62D204 + 117762
    i32 packedYExtent;      // dword_62D204 + 117764 (HIWORD=yOff, >>16 = yExtent)
    u16 xOffset;            // dword_62D204 + 117766 low word
    i32 noRecordSpriteDims; // dword_62D204 + 100038
};

// Portrait-sprite record (stride 84) — only the dims field is read by the cards.
struct PortraitSprite {
    i32 dims; // +78: (>>16) == portrait width
};

// ===========================================================================
// Person-card record (the *a4 / *a3 object the builders read).  Field offsets
// are byte offsets into the original record; we expose only the fields the
// pure layout/branch math reads.
//   +0    u16 personId
//   +2    u8  kind        (1..7; ==6 -> no slider; 5/6/7 -> extra icon row)
//   +9    u8  genderFlag  (full card: selects text id 279 vs 272)
//   +13   u8  labelCount  (full card: added to the text base id)
//   +396  i32 portraitSpriteIndex (simple card portrait lookup; full card uses [99])
//   +524  i32 cityKey     (simple card: compared against active-city key)
//   +84   i32 iconObjId   (5/6/7 extra-row object id; full card uses [21])
// We carry the few dword-indexed reads ([99],[21]) too.
struct PersonCardRec {
    u16 personId;            // +0
    u8  kind;                // +2
    u8  genderFlag;          // +9
    u8  labelCount;          // +13
    i32 portraitSpriteIndex; // +396 (simple) — dword[99] for full == +396 also
    i32 cityKey;             // +524
    i32 iconObjId;           // +84  (dword[21] == +84 also)
};

// Computed layout for a person card (the values fed to VIBE_Object_AddToWindow /
// AddCenteredLabel / AddSlider in the original).  All coordinates are post-centering.
struct PersonCardLayout {
    bool hasRecord;       // *a4 != 0
    i32  cardCenterX;     // v24 = baseX + cardSpriteWidth/2
    // portrait
    i16  portraitX;       // cardCenterX - portraitWidth/2 - 3
    i16  portraitY;       // baseY + 28 (full) / baseY (simple)  [the slot column]
    // name/title label rows
    i32  nameLabelX;      // == cardCenterX (AddCenteredLabel x)
    i32  nameLabelY;      // 202 (constant column id passed to AddCenteredLabel)
    // slider
    bool hasSlider;       // kind != 6
    i16  sliderX;         // cardCenterX - 35
    bool sliderIsFull;    // kind == 7  -> value 0 (full bar), else favorability
    // extra icon row (kinds 5/6/7)
    bool hasIconRow;      // kind in {5,6,7}
    // fallback (no record)
    i16  noRecPortraitX;  // cardCenterX - noRecWidth/2 - 3
};

// 0x553f30 — VIBE_Hud_BuildPersonCard: full card layout/centering math.
// baseX (a1), baseY (a2) are the card origin; `withLabels` (a5) gates the text rows.
PersonCardLayout BuildPersonCardLayout(const UiTheme& theme,
                                       const PortraitSprite& portrait,
                                       const PersonCardRec* rec, // nullptr => no record
                                       i32 baseX, i16 baseY,
                                       bool withLabels);

// 0x55433c — VIBE_Hud_BuildPersonCardSimple: simple card layout/centering math.
PersonCardLayout BuildPersonCardSimpleLayout(const UiTheme& theme,
                                             const PortraitSprite& portrait,
                                             const PersonCardRec* rec,
                                             i32 baseX, i16 baseY);

// Full-card name-label text id selection (0x5540d7 branch).
// withLabels && labelCount!=0 => base(279 if genderFlag else 272) + labelCount.
// else => the plain "%1N7" path (returns -1 to signal "no numeric id").
i32 PersonCardNameTextId(const PersonCardRec& rec, bool withLabels);

// ===========================================================================
// 0x4243d8 — VIBE_Surface_DrawText colour packing.
// Disasm: color = a3 | (a4<<8) | (a5<<16) where a3=bl, a4=cl, a5=stack.
// Win32 COLORREF == 0x00BBGGRR, so this maps R=a3 (bit0), G=a4 (bit8), B=a5 (bit16).
// We expose the pure packing; the GDI TextOutA + DDraw GetDC/ReleaseDC blit is inert.
u32 SurfaceTextColorRef(u8 a3_red, u8 a4_green, u8 a5_blue);

// Convenience: pack from logical (r,g,b) -> (a3=r, a4=g, a5=b).
inline u32 SurfaceColorRefRGB(u8 r, u8 g, u8 b) { return SurfaceTextColorRef(r, g, b); }

// ===========================================================================
// 0x41eee8 — VIBE_Paintbox_ClearAlt gating + record index.
// The original indexes a window-record array (stride 238 dwords) by the active
// window index, then checks two fields:
//   v5[160]  == window "valid" flag (0 => "Invalidate window!" error)
//   v5[10]   == paintbox surface ptr (0 => "Window has no paintbox!" error)
// When both present, it ColorFillRects the paintbox surface (v5[10]).
struct PaintboxWindowRec {
    i32 paintboxSurface; // v5[10]  (dword index 10)
    i32 windowValid;     // v5[160] (dword index 160)
};

enum class PaintboxClearResult {
    kInvalidateWindow = 0, // v5[160]==0
    kNoPaintbox       = 1, // v5[10]==0
    kFilled           = 2, // both present -> ColorFillRect
};

// activeWindowIndex == dword_62D230.  Returns which branch the original takes.
PaintboxClearResult PaintboxClearAlt(const PaintboxWindowRec& win);

// The flat-array index of the active window record: 238 * activeWindowIndex.
inline i32 PaintboxWindowRecordIndex(i32 activeWindowIndex) {
    return 238 * activeWindowIndex; // dword_67EB80[238 * dword_62D230]
}

// ===========================================================================
// 0x51adb4 — VIBE_Credit_ShowLenderDialog pure pieces.
// (a) v34[49] slot-table init: groups of 3 starting at index 0 with a leading
//     header; the do/while writes v3+=3 then sets [v3]=-1,[v3-1]=-1,[v3]=0
//     across v34/v32 — i.e. 16 row-slots of {id=-1, win=-1, page=0}.
// (b) the dword_67EF18..24 quad write (slider-panel value record).
// (c) per-person owned-building VALUE aggregation: sum of building field +180.
struct LenderRowSlot { i32 a; i32 b; i32 c; }; // {-1,-1,0} after init

// Initialise the 16-row slot table the way the do/while loop does.
// Returns the number of slots initialised (16).
int LenderInitRowSlots(LenderRowSlot slots[16]);

// Slider-panel value record write: dword_67EF18/1C/20/24 at index
// (14+224)*activeWindowIndex get {14,24,14,14}.
struct LenderSliderValues { i32 a, b, c, d; };
LenderSliderValues LenderSliderPanelValues(); // always {14,24,14,14}

// Per-person owned-building value aggregation (the inner v22 += *(v25+180) loop).
// `buildingValues` are the +180 fields of each owned building for one person.
i64 LenderSumBuildingValues(const i32* buildingValues, int count);

// ===========================================================================
// 0x51d9a4 — VIBE_Credit_ShowAssetOverview pure setup.
// The original fills a small param block before running the office-overview window:
//   v10 = 6   (mode byte), v8 = 1024 (capacity), text id 5371, gray-color thunk.
// We expose the constants as a struct; the window-run + grayscale set are inert.
struct AssetOverviewParams {
    i32 mode;       // v10 == 6
    i32 capacity;   // v8  == 1024
    i32 textId;     // 5371 (VIBE_Text_RenderFormattedMessage)
    i32 grayShade;  // 40 (VIBE_Light_SetGrayColorThunk arg)
};
AssetOverviewParams AssetOverviewSetup();

} // namespace play
} // namespace guild
