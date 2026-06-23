// ui_recon4_hud_surface.cpp — see ui_recon4_hud_surface.h for scope/provenance.
#include "play/ui_recon4_hud_surface.h"

namespace guild {
namespace play {

// ---------------------------------------------------------------------------
// gilde.exe 0x553f30 — VIBE_Hud_BuildPersonCard (pure layout/centering math).
//
// Original (Hex-Rays, paraphrased):
//   v24 = (theme.cardSpriteDims >> 16) / 2 + baseX;              // 0x553f61
//   v29.lo = baseY + 28;                                         // window slot id
//   ... AddToWindow(... baseY+28, baseX, 1401)                   // base frame
//   if (*a4) {                                                   // record present
//     v9 = (*a4)[99];                                            // portrait sprite idx
//     a4[6] = AddToWindow(... baseY+29,
//                 v24 - (theme[84*v9 + 78] >> 16)/2 - 3, v9);    // 0x553ff1
//     AddToWindow(... baseY+31, baseX+3, a4[7]);                 // sub icons...
//     name-label text id select (0x5540d7);
//     AddCenteredLabel(v24, 202, ...);                           // name row
//     if ((*a4)[2] != 6) AddSlider(v24-35, ...);                 // slider
//     extra icon row if kind in {5,6,7}
//   } else AddToWindow(... baseY+29,
//             v24 - (theme.noRecordSpriteDims >> 16)/2 - 3, 1190);
//
// We reproduce the integer centering exactly (>>16 then /2, signed) and the
// gating branches; the AddToWindow / AddCenteredLabel / AddSlider calls are the
// inert windowing shell (rule 4) and are NOT performed here.
// ---------------------------------------------------------------------------
PersonCardLayout BuildPersonCardLayout(const UiTheme& theme,
                                       const PortraitSprite& portrait,
                                       const PersonCardRec* rec,
                                       i32 baseX, i16 baseY,
                                       bool withLabels) {
    PersonCardLayout L{};
    const i32 cardW = theme.cardSpriteDims >> 16; // signed arithmetic shift
    const i32 v24   = cardW / 2 + baseX;          // 0x553f61
    L.cardCenterX   = v24;
    L.nameLabelX    = v24;
    L.nameLabelY    = 202;       // constant column id passed to AddCenteredLabel
    L.portraitY     = static_cast<i16>(baseY + 28); // slot column the original uses

    if (!rec) {
        L.hasRecord     = false;
        const i32 noW   = theme.noRecordSpriteDims >> 16;
        L.noRecPortraitX = static_cast<i16>(v24 - noW / 2 - 3); // 0x55432d
        return L;
    }

    L.hasRecord = true;
    const i32 portW = portrait.dims >> 16;             // theme[84*v9 + 78] >> 16
    L.portraitX = static_cast<i16>(v24 - portW / 2 - 3); // 0x553ff1
    (void)withLabels; // gates name-label text id (see PersonCardNameTextId)

    L.hasSlider    = (rec->kind != 6);   // 0x554170: (*a4)[2] != 6
    L.sliderX      = static_cast<i16>(v24 - 35); // 0x5541a4
    L.sliderIsFull = (rec->kind == 7);   // 0x5541b2: byte[2]==7 -> value 0

    const u8 k = rec->kind;
    L.hasIconRow = (k == 5 || k == 6 || k == 7); // 0x55425f
    return L;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x55433c — VIBE_Hud_BuildPersonCardSimple (pure layout math).
// Same centering (v14) as the full card; portrait slot column is baseY (not +28).
//   v14 = (theme.cardSpriteDims >> 16)/2 + baseX;                // 0x554377
//   portrait x = v14 - (theme[84*(*a3)[396>>2] + 78] >> 16)/2 - 3;
//   slider gated on (*a3)[2] != 6; slider x = v14 - 35.
// ---------------------------------------------------------------------------
PersonCardLayout BuildPersonCardSimpleLayout(const UiTheme& theme,
                                             const PortraitSprite& portrait,
                                             const PersonCardRec* rec,
                                             i32 baseX, i16 baseY) {
    PersonCardLayout L{};
    const i32 cardW = theme.cardSpriteDims >> 16;
    const i32 v14   = cardW / 2 + baseX; // 0x554377
    L.cardCenterX   = v14;
    L.nameLabelX    = v14;
    L.nameLabelY    = 202;
    L.portraitY     = baseY; // simple card slots portrait at baseY (a2)

    if (!rec) {
        L.hasRecord      = false;
        const i32 noW    = theme.noRecordSpriteDims >> 16;
        L.noRecPortraitX = static_cast<i16>(v14 - noW / 2 - 3); // 0x554691
        return L;
    }

    L.hasRecord = true;
    const i32 portW = portrait.dims >> 16;               // theme[84*idx + 78] >> 16
    L.portraitX = static_cast<i16>(v14 - portW / 2 - 3); // 0x5543e5

    L.hasSlider    = (rec->kind != 6);   // 0x5544b8
    L.sliderX      = static_cast<i16>(v14 - 35);
    L.sliderIsFull = (rec->kind == 7);

    const u8 k = rec->kind;
    L.hasIconRow = (k == 5 || k == 6 || k == 7); // 0x5545f4
    return L;
}

// gilde.exe 0x5540d7 — full-card name-label text id selection.
i32 PersonCardNameTextId(const PersonCardRec& rec, bool withLabels) {
    if (rec.labelCount != 0 && withLabels) {       // *((BYTE*)*a4 + 13) && a5
        const i32 base = rec.genderFlag ? 279 : 272; // 0x55426b / 0x5540ed
        return base + rec.labelCount;                 // + *((u8*)*a4 + 13)
    }
    return -1; // plain "%1N7" path, no numeric id
}

// ---------------------------------------------------------------------------
// gilde.exe 0x4243d8 — VIBE_Surface_DrawText colour packing.
// Disasm (0x424430..0x42444c):
//   al = var_10 (=cl=a4); eax = a4 << 8
//   cl = var_14 (=bl=a3); ecx = a3
//   ecx = a3 | (a4 << 8)
//   al = arg_0 (=a5);     eax = a5 << 16
//   color = a3 | (a4 << 8) | (a5 << 16)
// Register->arg: bl=a3 (bit0), cl=a4 (bit8), stack=a5 (bit16). COLORREF == 0x00BBGGRR,
// so a3=Red(bit0), a4=Green(bit8), a5=Blue(bit16).
// ---------------------------------------------------------------------------
u32 SurfaceTextColorRef(u8 a3_red, u8 a4_green, u8 a5_blue) {
    return  static_cast<u32>(a3_red)
         | (static_cast<u32>(a4_green) << 8)
         | (static_cast<u32>(a5_blue) << 16);
}

// ---------------------------------------------------------------------------
// gilde.exe 0x41eee8 — VIBE_Paintbox_ClearAlt gating.
//   if (!v5[160]) -> "Invalidate window!"          (0x41ef05/0x41ef11)
//   if (!v5[10])  -> "Window has no paintbox!"      (0x41ef13/0x41ef1a)
//   else ColorFillRect(v5[10])                      (0x41ef34)
// ---------------------------------------------------------------------------
PaintboxClearResult PaintboxClearAlt(const PaintboxWindowRec& win) {
    if (win.windowValid == 0)     return PaintboxClearResult::kInvalidateWindow;
    if (win.paintboxSurface == 0) return PaintboxClearResult::kNoPaintbox;
    return PaintboxClearResult::kFilled;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x51adb4 — VIBE_Credit_ShowLenderDialog pure pieces.
// ---------------------------------------------------------------------------
//   v3 = 0;
//   do { v3 += 3; (&v32)[v3] = -1; v34[v3-1] = -1; v34[v3] = 0; } while (v3 != 48);
// With v32 sitting at v34[-2], this writes, for v3 = 3,6,...,48 (16 iterations):
//   slot k (k=0..15): {.a = -1, .b = -1, .c = 0}.
int LenderInitRowSlots(LenderRowSlot slots[16]) {
    for (int k = 0; k < 16; ++k) {
        slots[k].a = -1;
        slots[k].b = -1;
        slots[k].c = 0;
    }
    return 16;
}

// dword_67EF18/1C/20/24[(14+224)*idx] = {14,24,14,14}  (0x51aea3..0x51aeb8)
LenderSliderValues LenderSliderPanelValues() {
    return LenderSliderValues{14, 24, 14, 14};
}

// inner aggregation: v22 += *(v25 + 180) over each owned building (0x51b108).
// The original accumulates in a 32-bit signed `int v22` (overflow wraps mod 2^32);
// the result is passed straight to VIBE_Text_RenderRichString as a 32-bit int. We
// accumulate in i32 (wrapping) to match, then sign-extend into the i64 return.
i64 LenderSumBuildingValues(const i32* buildingValues, int count) {
    i32 sum = 0; // v22 (32-bit, wraps)
    for (int i = 0; i < count; ++i)
        sum = static_cast<i32>(static_cast<u32>(sum) + static_cast<u32>(buildingValues[i]));
    return static_cast<i64>(sum);
}

// ---------------------------------------------------------------------------
// gilde.exe 0x51d9a4 — VIBE_Credit_ShowAssetOverview param setup.
//   SetGrayColorThunk(0, 40, &v7); v10 = 6; v8 = 1024;
//   RenderFormattedMessage(v6, 5371); RunOfficeOverviewWindow(...);
// ---------------------------------------------------------------------------
AssetOverviewParams AssetOverviewSetup() {
    return AssetOverviewParams{/*mode*/ 6, /*capacity*/ 1024, /*textId*/ 5371,
                               /*grayShade*/ 40};
}

} // namespace play
} // namespace guild
