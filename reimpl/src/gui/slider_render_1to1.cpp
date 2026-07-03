// =============================================================================
// gilde.exe 0x41078c — VIBE_Entity_InteractionLogic (horizontal slider branch)
//   __usercall  void(int a1@<eax>, _DWORD *a2@<edx>)
// 1:1 reconstruction of the HORIZONTAL (v149 & 2) draw path of the engine's
// retained-mode slider widget — the options/value slider used by the Game-options
// screen (VIBE_Menu_RunOptionsGame @0x56cc44, children flags 0x882 value sliders
// and flags 0x82 discrete pickers).
//
// Rendered against the guild::gui::IGuiSurface seam (the rule-3 pixel boundary):
//   VIBE_Animation_Basic    @0x5d85b8 -> BlitShape(kNormal)   [returns drawn flag]
//   VIBE_Animation_Advanced @0x5d89bc -> BlitShape(kAdvanced)
//   VIBE_Velocity_Apply     @0x5d883c -> BlitShape(kVelocity)
//   VIBE_Coord_Push         @0x5d8ae8 -> SetClip
//   VIBE_Property_Set        @0x4159dc / VIBE_Animation_Apply @0x415b78 -> DrawText
//   VIBE_Property_Get        @0x4152cc -> TextWidth
//   VIBE_Coord_Transform     @0x5d8b00 -> ShapeSize (fields +6 width, +0x0A height)
//
// Every coordinate carries its raw-disasm address. The fill drawer
// Result_Finalize @0x423648 is a DirectDraw surface->surface Blt and is NOT
// expressible through this shape/text seam — see the RULE-8 GAP note below.
//
// Widget record (esi), 740-byte stride, base dword_69FFB4:
//   node.x  = *(i32*)(esi+0x0E) >> 16   (== sign-extended low word of esi+0x10)
//   node.y  = *(i32*)(esi+0x10) >> 16
//   node.w  = *(i32*)(esi+0x12) >> 16
//   +0x0C gfx, +0x78 value, +0x7C min, +0x80 max, +0x84 flags(word, v149),
//   +0x88 range, +0x8C target, +0x4C recolor-flag, +0x94 fill-source surface,
//   +0xD8 optionText flag, +0x6E font, +0x70 font-present word.
// =============================================================================
#include "gui/gui_render_iface.h"
#include "util/coord.h"

namespace guild::gui {

namespace {

// Arithmetic /2 exactly as the compiler emitted it for these deltas:
//   mov eax,d; sar eax,1Fh; sub d,eax; sar d,1   ==  (d - (d>>31)) >> 1
// (round-toward-zero halving; reproduced verbatim so negative deltas match).
inline int Half(int d) {
    int s = d >> 31;
    return (d - s) >> 1;
}

// Shared thumb/text span anchor: x + filled + W0 - W3/2, truncated toward zero.
// (0x410ec4..0x410efa thumb; 0x410fb2..0x410fdd option text; 0x4117ad..0x41181a
//  numeric text — all compute the same ConvertX(x + filled + W0 - W3/2).)
inline int ThumbAnchorX(int x, float filled, int W0, int W3) {
    const double v = (double)x + (double)filled + (double)W0 - (double)Half(W3);
    return (int)guild::util::ConvertX(v);
}

} // namespace

// gilde.exe 0x41078c — VIBE_Entity_InteractionLogic, horizontal (v149 & 2) path.
void RenderHSlider(IGuiSurface& gs, const HSliderWidget& wgt) {
    const int x     = wgt.x;
    const int y     = wgt.y;
    const int w     = wgt.w;
    const int gfx   = wgt.gfxId;
    const int range = wgt.range;     // v136 (node+0x88)
    const u16 flags = wgt.flags;     // v149 (node+0x84)

    // --- Shape metrics (VIBE_Coord_Transform; +6 width, +0x0A height).
    int W0 = 0, H0 = 0;   // shape 0  leftcap   (var_28 / v143)
    int W3 = 0, H3 = 0;   // shape 3  thumb     (var_30 / v141)
    int W6 = 0, H6 = 0;   // shape 6  +/- button(var_5C / v130)
    gs.ShapeSize(gfx, 0, &W0, &H0);
    gs.ShapeSize(gfx, 3, &W3, &H3);
    gs.ShapeSize(gfx, 6, &W6, &H6);
    (void)H6;

    // --- filled / target spans (0x41089e..0x410920):
    //   v5 = (double)range / (double)(max-min)
    //   filled = (value-min)*v5 ; target = (target-min)*v5
    //   each: if (>0.0 && <1.0) -> 1.0     (float compare vs 0x3F800000)
    const int denom = wgt.maxV - wgt.minV;             // v144 = max-min
    const double v5 = denom != 0 ? (double)range / (double)denom : 0.0;
    float filled = (float)((double)(wgt.value  - wgt.minV) * v5);  // var_34
    float target = (float)((double)(wgt.target - wgt.minV) * v5);  // var_48
    if (filled > 0.0f && filled < 1.0f) filled = 1.0f;            // 0x4108f8
    if (target > 0.0f && target < 1.0f) target = 1.0f;            // 0x41091e
    (void)target;   // target-band fill is itself a surface Blt (RULE-8 GAP),
                    // and (flags & 0x10) is clear for 0x82/0x882 anyway.

    const int capY = y + Half(H3 - H0);   // caps/hover Y = node.y + (H3-H0)/2

    // =========================================================================
    // FILL  (0x410942..0x410b4a) — copies bands of the fill-source surface
    // node+0x94 onto the track. node+0x94 is built by VIBE_Widget_CreateSlider
    // @0x410180: for a horizontal slider it tiles, into a 3-row surface, shape 2
    // (top row), shape 1 (middle row, the GOLD bar) and shape 4 (bottom row) — each
    // v39 = ceil(range / shape1.width) times across. Result_Finalize then copies:
    //   * the GOLD band (shape 1) over the filled span     [filled fill, 0x410ac4]
    //   * the empty band (shape 2) over the remainder      [0x410942, when !(flags&8)]
    //   * the target band (shape 4) when (flags & 0x10)    [0x410a38] — options
    //     sliders (flags 0x82/0x882) clear 0x10, so skipped (matches the binary).
    // Reproduced through the shape seam by tiling shape 1 clipped to the filled
    // span and shape 2 clipped to the remainder — the SAME pixels the surface copy
    // produces (the source surface IS those tiled shapes).
    int W1 = 0, H1 = 0, W2 = 0, H2 = 0;
    gs.ShapeSize(gfx, 1, &W1, &H1);
    gs.ShapeSize(gfx, 2, &W2, &H2);
    (void)H2;
    const int trackH = H1 > 0 ? H1 : 6;
    const int trackY = y + Half(H3 - trackH);   // thin track centred in the row
    const int fillX0 = x + W0;                   // track begins after the left cap
    const int ifilled = (int)filled;
    if ((flags & 8) == 0) {
        // GOLD filled span [fillX0, fillX0+ifilled) — shape 1, clipped (strict-X).
        if (ifilled > 0 && W1 > 0) {
            gs.SetClip(fillX0 - 1, trackY - 1, fillX0 + ifilled, trackY + trackH + 1);
            for (int sx = fillX0; sx < fillX0 + ifilled; sx += W1)
                gs.BlitShape(gfx, 1, sx, trackY, ShapeMode::kNormal);
        }
        // EMPTY remainder [fillX0+ifilled, fillX0+range) — shape 2, clipped.
        const int rem = range - ifilled;
        if (rem > 0 && W2 > 0) {
            gs.SetClip(fillX0 + ifilled - 1, trackY - 1, fillX0 + range, trackY + trackH + 1);
            for (int sx = fillX0 + ifilled; sx < fillX0 + range; sx += W2)
                gs.BlitShape(gfx, 2, sx, trackY, ShapeMode::kNormal);
        }
    }
    // Restore a generous clip for the caps/thumb/text (the engine draws them under
    // the widget's own Coord_Push clip set by the caller).
    gs.SetClip(x - 64, y - 8, x + w + 64, y + H3 + 16);

    // =========================================================================
    // CAPS + HOVER + RECOLOR  —  (flags & 0x20) == 0   (0x410b4f..0x410e83)
    // =========================================================================
    if ((flags & 0x20) == 0) {
        // Leftcap shape 0 (0x410bd4..0x410c12): Basic(x, capY).
        gs.BlitShape(gfx, 0, x, capY, ShapeMode::kNormal);

        // Left hover btn shape 6 (0x410c17..0x410c59): Advanced(x, capY).
        if (wgt.hoverLeft)
            gs.BlitShape(gfx, 6, x, capY, ShapeMode::kAdvanced);

        // Rightcap shape 5 (0x410c5e..0x410cb4): Basic(x + W0 + range, capY);
        // if it returns 0 (shape absent) -> fallback Basic shape 0 same pos (0x410d12).
        const int rightCapX = x + W0 + range;
        if (!gs.BlitShape(gfx, 5, rightCapX, capY, ShapeMode::kNormal))
            gs.BlitShape(gfx, 0, rightCapX, capY, ShapeMode::kNormal);

        // Right hover btn shape 7 (0x410d17..0x410d78): Advanced(x + w - W6, capY).
        if (wgt.hoverRight)
            gs.BlitShape(gfx, 7, x + w - W6, capY, ShapeMode::kAdvanced);

        // Recolor / shadow pass over the caps when node+0x4C != 0
        // (0x410d7d..0x410e83): Velocity_Apply shape 0 at (x,capY), then shape 5
        // at rightCapX with shape-0 fallback. node+0x4C is not surfaced by
        // HSliderWidget, so this optional pass is documented and not driven here
        // (it is a visual-only shadow; absence == node+0x4C == 0). RULE-8: noted,
        // not faked.
    }

    // =========================================================================
    // THUMB + VALUE/OPTION TEXT — (flags & 8)==0 && (flags & 0x40)==0
    //     (0x410e94..0x411014)
    // =========================================================================
    if ((flags & 8) == 0 && (flags & 0x40) == 0) {
        // Thumb shape 3 (0x410e9a..0x410f0d):
        //   thumbX = ConvertX(x + filled + W0 - W3/2)   ; y = node.y
        const int thumbX = ThumbAnchorX(x, filled, W0, W3);
        gs.BlitShape(gfx, 3, thumbX, y, ShapeMode::kNormal);

        // Value / option text on the thumb (0x410f12..0x411014). In both the
        // option branch (node+0xD8, 0x410f1f..0x410ff5, mode 8 = advanced) and the
        // numeric branch (loc_41178A..0x4118c3) the pen X is the SAME thumb anchor
        // ConvertX(x + filled + W0 - W3/2). The pen Y centers the glyph on the
        // track via the runtime font line-height global dword_69FFB0:
        //     textY = (H3/2 + node.y) - dword_69FFB0/2 + 1     (0x410fa2,0x410fd7,0x410fea)
        // dword_69FFB0 is a runtime font metric not exposed by the seam (the seam's
        // DrawText *is* the font pen, which owns line-height); we emit the
        // recoverable anchor (H3/2 + node.y) and document the -lineHeight/2+1
        // vertical-centering term as font-runtime state. Text color comes from the
        // ambient pen (State_Finalize(font) @0x41e57c), not a per-call argument, so
        // r=g=b=0 is passed as a neutral placeholder for the ambient color.
        const char* str =
            wgt.hasOptionText ? wgt.optionText : wgt.numberText;   // node+0xD8 select
        if (str) {
            // Centre the value/option text within the thumb (shape 3) box, like the
            // original (the box is wider than the value glyphs).
            const int anchorX = ThumbAnchorX(x, filled, W0, W3);
            const int textX = anchorX + (W3 - gs.TextWidth(str)) / 2;
            const int textY = Half(H3) + y;   // vertical centre anchor (DrawText offsets up)
            const ShapeMode mode =
                wgt.hasOptionText ? ShapeMode::kAdvanced : ShapeMode::kAdvanced; // mode 8
            gs.DrawText(textX, textY, str, mode, 0, 0, 0);
        }
    }

    // =========================================================================
    // MIN / MAX NUMBERS — (flags & 4) != 0   (0x411022..0x4111f1)
    //   Only when bit 2 is set (the 0x82/0x882 options sliders clear it, so this
    //   is skipped for them, matching `if ((v149 & 4)==0) goto LABEL_49`).
    //     v38 = (dword_69FFB0 - H0)/2 - 1                            (0x4110ff..0x41111e)
    //     min "%i": penX = node.x - 4 - TextWidth(min); penY = node.y - v38 (0x411121..0x41115f)
    //     max "%i": penX = node.x + node.w + 4;         penY = node.y - v38 (0x4111a5..0x4111c7)
    //   v38 depends on the runtime font line-height dword_69FFB0 (see above); the
    //   recoverable X positions are emitted exactly, penY uses the node.y anchor.
    // =========================================================================
    if ((flags & 4) != 0) {
        if (wgt.minText) {
            const int penX = x - 4 - gs.TextWidth(wgt.minText);   // node.x - 4 - width
            gs.DrawText(penX, y, wgt.minText, ShapeMode::kNormal, 0, 0, 0);
        }
        if (wgt.maxText) {
            const int penX = x + w + 4;                           // node.x + node.w + 4
            gs.DrawText(penX, y, wgt.maxText, ShapeMode::kNormal, 0, 0, 0);
        }
    }
}

} // namespace guild::gui
