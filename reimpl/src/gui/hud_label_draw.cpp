#include "gui/hud_label_draw.h"

// guild::gui — HUD floating-text drawer passes (the table-walk + projection-gate +
// blit orchestration).  The pure placement / expiry / scale math lives in
// gui/hud_labels.{h,cpp}; this file REUSES those helpers and never re-derives them.
//
// Each original drawer follows the same skeleton:
//     VIBE_State_Finalize(<layer>);
//     for each candidate:
//         if (!present)                       continue;       // table gate
//         if (!Object_ComputeScreenBounds())  continue;       // off-screen gate
//         placement = <centred / anchor math>;                // hud_labels helper
//         VIBE_Animation_Apply(x, y, width, ctx, text, charH); // glyph blit
//         VIBE_State_GetCurrent(x, y, ...);                    // mark dirty
// We faithfully preserve the gate ordering (the State_Finalize is emitted up-front, even
// when zero captions follow) and the per-pass charH / width / inset constants.

namespace guild::gui {

namespace {
// Emit one centred caption (the Animation_Apply + State_GetCurrent pair).
void EmitCaption(HudLabelSink& sink, int x, int y, int width, int charH,
                 const std::string& text) {
    LabelDraw d;
    d.x = x;
    d.y = y;
    d.width = width;
    d.charH = charH;
    d.text = text;
    sink.Emit(d);
}
} // namespace

// ---------------------------------------------------------------------------
// gilde.exe 0x4bbaec — VIBE_Hud_DrawObjectNameLabels.
//   VIBE_State_Finalize(67);
//   for (i = 0; i != 1600; i += 50)
//     if (rec && (...flags...) && ComputeScreenBounds(rec, &box)) {
//       v5 = box[0] + (box[2]-box[0])/2 - 80;  v6 = box[1];
//       Animation_Apply(v5, v6, 160, ctx, name, 40);
//       State_GetCurrent(v5, v6, ...);
//     }
// The `(*(rec+460) || !*(rec+533))` visibility predicate is the caller's; here it has
// been folded into `present` so the table-walk + projection gate stay 1:1.
// ---------------------------------------------------------------------------
int Hud_DrawObjectNameLabels(const std::vector<HudLabelObject>& objects,
                             HudLabelSink& sink) {
    sink.Layer(kHudLabelStateLayer);
    int drawn = 0;
    for (const HudLabelObject& obj : objects) {
        if (!obj.present)
            continue;
        if (!obj.projectable)   // ComputeScreenBounds(...) == 0 -> off-screen
            continue;
        LabelPlacement p = Hud_CenteredLabelPlacement(obj.bounds);
        EmitCaption(sink, p.x, p.y, kHudLabelWidth, kHudLabelCharH40, obj.text);
        ++drawn;
    }
    return drawn;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x4bbbbc — VIBE_Hud_DrawAnimalLabels.
//   if (dword_63174C || dword_631748) {              // a building is selected
//     VIBE_State_Finalize(67);
//     for (rec = QueryFind(...); rec; rec = IterNext())
//       if (*(rec+24) && ComputeScreenBounds(*(rec+24), &box)) {
//         v5 = box[0] + (box[2]-box[0])/2 - 80;  v6 = box[1];
//         Animation_Apply(v5, v6, 160, ctx, species, 40);
//         State_GetCurrent(v5, v6, ...);
//       }
//   }
// The gate is on the selected-building globals; when clear the whole pass (including the
// State_Finalize) is skipped — preserved here by returning before Layer().
// ---------------------------------------------------------------------------
int Hud_DrawAnimalLabels(bool selectedBuilding,
                         const std::vector<HudLabelObject>& animals,
                         HudLabelSink& sink) {
    if (!selectedBuilding)   // dword_63174C == 0 && dword_631748 == 0
        return 0;
    sink.Layer(kHudLabelStateLayer);
    int drawn = 0;
    for (const HudLabelObject& a : animals) {
        if (!a.present)        // *(rec+24) == 0 -> no anchor object
            continue;
        if (!a.projectable)
            continue;
        LabelPlacement p = Hud_CenteredLabelPlacement(a.bounds);
        EmitCaption(sink, p.x, p.y, kHudLabelWidth, kHudLabelCharH40, a.text);
        ++drawn;
    }
    return drawn;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x4bbc8c — VIBE_Hud_DrawCharacterLabels.
//   VIBE_State_Finalize(67);
//   for (i = 0; i != 8208; i += 171)
//     if (dword_676BF0[i] == 1) {                    // slot active
//       v8 = anchorX >> 16;  v9 = (anchorY >> 16) - 20;
//       if (!*(slotWindow + dword_69FFB4 + 52)) {    // not occluded
//         Animation_Apply(v8, v9, 160, ctx, name, 32);
//         State_GetCurrent(v8, v9, ...);
//       }
//     }
// The anchors are already the packed 16.16 world position; the original >>16's them
// (arithmetic, sign-preserving).  charH is 32 here (smaller than the 40 of the others).
// ---------------------------------------------------------------------------
int Hud_DrawCharacterLabels(const std::vector<HudCharacterLabel>& chars,
                            HudLabelSink& sink) {
    sink.Layer(kHudLabelStateLayer);
    int drawn = 0;
    for (const HudCharacterLabel& c : chars) {
        if (!c.active)         // dword_676BF0[i] != 1
            continue;
        if (!c.visible)        // *(window + ... + 52) != 0 -> occluded
            continue;
        int x = c.anchorX >> 16;                       // v8 = anchorX >> 16
        int y = (c.anchorY >> 16) - kCharacterLabelTopInset; // v9 = (anchorY>>16) - 20
        EmitCaption(sink, x, y, kHudLabelWidth, kHudLabelCharH32, c.text);
        ++drawn;
    }
    return drawn;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x4bb7a0 — VIBE_Hud_DrawDamageLabels.
// First loop = the expiry sweep (DamageLabel_ExpireSweep, reused from hud_labels.cpp):
//   for (i = 0; i != 4288; i += 67)
//     if (inUse && timestamp + 300 < now) inUse = 0;
// Then State_Finalize(67) and, per still-live slot whose target projects on-screen:
//   v7 = box[0] + (box[2]-box[0])/2 - 80;  v5 = box[1] - 64;
//   Animation_Apply(v7, box[1]-64, 160, ctx, numberText, 40);
//   State_GetCurrent(v7, v5, ...);
// The slot table is the shared g_damageLabels (owned by hud.cpp); `targets` supplies the
// per-slot projection result + formatted text, indexed parallel to g_damageLabels.
// ---------------------------------------------------------------------------
int Hud_DrawDamageLabels(int now,
                         const std::vector<HudLabelObject>& targets,
                         HudLabelSink& sink) {
    DamageLabel_ExpireSweep(now);   // first loop, reused verbatim
    sink.Layer(kHudLabelStateLayer);
    int drawn = 0;
    const int n = static_cast<int>(targets.size());
    for (int i = 0; i < kDamageLabelCount && i < n; ++i) {
        if (!g_damageLabels[i].inUse)   // v4 = dword_11B73A8[j]; if (!v4) continue
            continue;
        const HudLabelObject& t = targets[i];
        if (!t.present)        // the +492 / +52 record-chain resolved to nothing
            continue;
        if (!t.projectable)    // ComputeScreenBounds(...) == 0
            continue;
        // Same centred x as the other captions; y one notch higher (top - 64).
        LabelPlacement p = Hud_CenteredLabelPlacement(t.bounds);
        EmitCaption(sink, p.x, t.bounds.top - kDamageLabelTopInset,
                    kHudLabelWidth, kHudLabelCharH40, t.text);
        ++drawn;
    }
    return drawn;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x4bcb74 — VIBE_Hud_DrawStatusBanner.
//   if (byte_11B6B20) {                              // banner text present
//     VIBE_State_Finalize(66);
//     v2 = dword_69FFA4 - 300;                        // x = rightEdge - 300
//     v3 = scale * (flag 0x8000 ? flt_61E194 : flt_61E190);
//     VIBE_Coord_ConvertX();
//     Animation_Apply(v2, v3, 600, ctx, banner, 40);
//     State_GetCurrent(v2, v3, ...);
//     if (dword_631678 + 350 <= now) byte_11B6B20 = 0;  // expire
//   }
// The fixed-point scale (v3) is a renderer detail (Coord_ConvertX); the y position the
// blit lands at is the renderer's, so the LabelDraw carries the integer banner x and the
// scale's integer part is not modelled — we emit x + the recovered width/charH and report
// the expiry decision (the caller clears byte_11B6B20 when outExpired is true).
// Hud_StatusBannerX / _Scale / _Expired are reused from hud_labels.h.
// ---------------------------------------------------------------------------
int Hud_DrawStatusBanner(const std::string& bannerText, int startTick, int now,
                         int rightEdge, int modeFlags, HudLabelSink& sink,
                         bool* outExpired) {
    if (bannerText.empty()) {        // byte_11B6B20 == 0
        if (outExpired) *outExpired = false;
        return 0;
    }
    sink.Layer(kStatusBannerStateLayer);   // VIBE_State_Finalize(66)
    int x = Hud_StatusBannerX(rightEdge);  // dword_69FFA4 - 300
    // y is the scaled fixed-point position the renderer resolves via Coord_ConvertX; the
    // scale selector (Hud_StatusBannerScale) is recovered but the final pixel y is a
    // renderer-side product, so we pass the banner x and let the sink place y.  We still
    // touch the scale selector to keep the 0x8000 branch live (matches the original read).
    (void)Hud_StatusBannerScale(modeFlags);
    EmitCaption(sink, x, 0, kStatusBannerWidth, kStatusBannerCharH, bannerText);
    bool expired = Hud_StatusBannerExpired(startTick, now); // dword_631678 + 350 <= now
    if (outExpired) *outExpired = expired;
    return 1;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x4bcafc — VIBE_Hud_DrawNameInputCaption.
//   if (byte_75BE38) {                               // input field active
//     if (byte_123356A) {                            // anchor valid
//       VIBE_State_Finalize(67);
//       Animation_Apply(anchorX2>>16 - 54, anchorY>>16 + 52, 160, ctx, caption, 168);
//       State_GetCurrent(...);
//     }
//   }
// Placement reuses Hud_NameInputCaptionPlacement (anchorX2>>16 - 54, anchorY>>16 + 52).
// ---------------------------------------------------------------------------
int Hud_DrawNameInputCaption(bool inputActive, bool anchorValid,
                             int anchorX2Packed, int anchorYPacked,
                             const std::string& caption, HudLabelSink& sink) {
    if (!inputActive)        // byte_75BE38 == 0
        return 0;
    if (!anchorValid)        // byte_123356A == 0
        return 0;
    sink.Layer(kHudLabelStateLayer);   // VIBE_State_Finalize(67)
    LabelPlacement p = Hud_NameInputCaptionPlacement(anchorX2Packed, anchorYPacked);
    EmitCaption(sink, p.x, p.y, kHudLabelWidth, kNameInputCaptionH, caption);
    return 1;
}

} // namespace guild::gui
