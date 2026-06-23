// widget_layout.{h,cpp} — see header. 1:1 translation of the Hex-Rays pseudocode for
// VIBE_Widget_LayoutBounds @0x413220, VIBE_Property_Get @0x4152cc,
// VIBE_State_GetCurrent @0x40e728 and the pure leaf VIBE_Coord_Transform @0x5d8b00.
//
// Fidelity notes:
//  - All bound clamps are integer `>> 16` on signed 16.16 dwords + integer compares; no
//    float / ConvertX is involved, so the shifts are reproduced exactly (arithmetic >>).
//  - The widget bound fields (+28..+34) are read by the original at OVERLAPPING /
//    unaligned dword offsets (e.g. `*(int*)(v4+30) >> 16`). We reproduce those reads
//    byte-exactly via Widget::ld<i32>(off) (memcpy-backed unaligned load), matching the
//    x86 unaligned `mov`. Writes are the same word/byte stores the original makes.
#include "gui/widget_layout.h"
#include "gui/object.h"   // guild::gui::g_widgets (dword_69FFB4), REUSED

#include <cstring>

namespace guild::gui {

// ---------------------------------------------------------------------------
// 0x5d8b00 — VIBE_Coord_Transform.
// ---------------------------------------------------------------------------
// if (rec) rec += *(u32*)(base + rec_relative? ) ... The original takes `rec` as a raw
// pointer and reads *(u32*)(rec + 4*idx + 69). To stay host-pointer-clean while keeping
// the +69 unaligned read byte-exact, callers pass the record's backing bytes via `base`
// (the memory `rec` points at, with `rec` being its handle/offset 0). When `base` is
// null the handle is returned unchanged for a non-zero rec (the inert default).
i32 CoordTransform(const u8* base, i32 rec, u16 idx) {
    if (rec) {
        if (base) {
            i32 add;
            std::memcpy(&add, base + 4 * static_cast<int>(idx) + 69, sizeof(add));
            rec += add;   // result += *(u32*)(result + 4*a2 + 69)
        }
    }
    return rec;
}

// ---------------------------------------------------------------------------
// 0x4152cc — VIBE_Property_Get.
// ---------------------------------------------------------------------------
i32 PropertyGet(const TextMetricEnv& env, const char* text, int font) {
    const char* v2 = text;                              // v2 = a1
    unsigned v3 = static_cast<unsigned>(std::strlen(text)) + 1;  // strlen(a1) + 1
    i32 v4 = 0;                                          // accumulated width
    i32 v5 = env.stateUpdate ? env.stateUpdate(env.ctx, font) : 0;  // State_Update(a2)

    int i = 0;
    for (; i < static_cast<int>(v3 - 1); ++v2) {        // i < n-1
        char v9 = *v2;
        if (*v2 != 126) {                               // '~' (0x7E) => skip, no advance
            i32 g = env.coordTransform
                        ? env.coordTransform(env.ctx, v5, static_cast<u8>(*v2))
                        : 0;                            // Coord_Transform(state, (u8)c)
            // if ( i && text[i-1] != ' ' ) w -= *(u16*)(g + 22);
            if (i && *(v2 - 1) != 32)
                v4 -= env.glyphWord ? env.glyphWord(env.ctx, g, 22) : 0;
            // advance: space gets the extra space-width; both get the letter spacing.
            i32 adv26 = env.glyphWord ? env.glyphWord(env.ctx, g, 26) : 0;
            if (v9 == 32)
                v4 += env.letterSpacing + env.spaceExtra + adv26;
            else
                v4 += env.letterSpacing + adv26;
        }
        ++i;
    }
    return v4 + env.letterSpacing;                       // + dword_62D274 trailing pad
}

// ---------------------------------------------------------------------------
// 0x40e728 — VIBE_State_GetCurrent (dirty-rect clamp + append).
// ---------------------------------------------------------------------------
i32 StateGetCurrent(DirtyRectState& st, i32 x, i32 y, i32 h, i32 w) {
    // result=x@eax, a2=y@edx, a3=h@ecx (esi), a4=w@ebx.
    i32 result = x;
    i32 a2 = y;
    i32 a3 = h;
    i32 a4 = w;

    // if ( dword_62D2E0[bucket] < 512 ) { ... } else return result;
    if (st.count < st.capacity) {
        // if ( result > clipRight ) result = clipRight;
        if (result > st.clipRight)
            result = st.clipRight;
        // odd-x even alignment: a4 += 2; --result; a4 &= ~1;
        if ((result & 1) != 0) {
            a4 += 2;
            --result;
            a4 = a4 & 0xFFFFFFFE;   // and bl,0FEh — clears low byte's bit0 (a4 is small)
        }
        // if ( a2 + a3 > clipBottom ) a3 = clipBottom - a2;
        if (a2 + a3 > st.clipBottom)
            a3 = st.clipBottom - a2;
        // if ( result + a4 > clipRight ) a4 = clipRight - result;
        if (result + a4 > st.clipRight)
            a4 = st.clipRight - result;
        // if ( a2 < clipTop ) a2 = clipTop;
        if (a2 < st.clipTop)
            a2 = st.clipTop;
        // if ( result < clipLeft ) result = clipLeft;
        if (result < st.clipLeft)
            result = st.clipLeft;

        // if ( a4 > 0 && a3 > 0 && pokes-outside-scroll-window ) append.
        if (a4 > 0 && a3 > 0 &&
            (a2 <= st.scrollTop || a2 + a3 >= st.scrollBottom ||
             result <= st.scrollLeft || result + a4 >= st.scrollRight)) {
            if (st.rects) {
                DirtyRect& r = st.rects[st.count];
                r.x = result;   // [bucket*16 + 0]
                r.y = a2;       // [+4]
                r.w = a4;       // [+8]
                r.h = a3;       // [+12]
            }
            result = st.count + 1;   // result = dword_62D2E0[bucket] + 1
            st.count = result;       // dword_62D2E0[bucket] = result
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// 0x413220 — VIBE_Widget_LayoutBounds.
// ---------------------------------------------------------------------------
u8 WidgetLayoutBounds(const LayoutEnv& env, i16 a1, i16 a2, int a3) {
    Widget& w = g_widgets[a3];                           // 740*a3 + dword_69FFB4

    w.at<u8>(104) = 8;                                   // *(v4+104) = 8
    w.at<u8>(105) = 8;                                   // *(v4+105) = 8
    w.at<i16>(16) = a1;                                  // *(v4+16) = a1 (x)
    u8 typeAtEntry = w.at<u8>(24);                       // v5 = *(v4+24) (saved early)
    w.at<i16>(18) = a2;                                  // *(v4+18) = a2 (y)

    if (typeAtEntry == 64) {                             // '@' window-backing
        i16 v6 = w.at<i16>(22);                          // *(v4+22)
        w.at<i16>(32) = a2;                              // *(v4+32) = a2
        i16 v7 = static_cast<i16>(v6 + a2);
        i16 v8 = w.at<i16>(20);                          // *(v4+20)
        w.at<i16>(34) = v7;                              // *(v4+34) = v6 + a2
        w.at<i16>(28) = a1;                              // *(v4+28) = a1
        w.at<i16>(30) = static_cast<i16>(v8 + a1);       // *(v4+30) = v8 + a1
    } else {
        w.at<i16>(32) = 0;                               // *(v4+32) = 0
        i16 v18 = static_cast<i16>(env.screenExtBC);     // LOWORD(dword_69FFBC)
        w.at<i16>(28) = 0;                               // *(v4+28) = 0
        w.at<i16>(34) = v18;                             // *(v4+34) = LOWORD(dword_69FFBC)
        w.at<i16>(30) = static_cast<i16>(env.screenExtBC >> 16); // *(v4+30)=HIWORD(BC)
    }

    // Parent-bounds clamp (the +44 link).
    i32 v9 = w.at<i32>(44);                              // v9 = *(v4+44)
    if (v9) {
        auto pD = [&](int off) -> i32 {
            return env.parentBoundsDword ? env.parentBoundsDword(env.ctx, v9, off) : 0;
        };
        auto pW = [&](int off) -> i16 {
            return env.parentBoundsWord ? static_cast<i16>(env.parentBoundsWord(env.ctx, v9, off)) : 0;
        };

        i16 v10;
        if ((pD(4) >> 16) <= (w.ld<i32>(30) >> 16))     // *(v9+4)>>16 <= *(v4+30)>>16
            v10 = w.at<i16>(32);                         // *(v4+32)
        else
            v10 = pW(6);                                 // *(v9+6)
        w.at<i16>(32) = v10;                             // *(v4+32) = v10

        i32 v11 = w.ld<i32>(32) >> 16;                   // *(v4+32)>>16
        if (((pD(8) >> 16) + (pD(4) >> 16)) < v11)       // *(v9+8)>>16 + *(v9+4)>>16 < v11
            v11 = (pD(8) >> 16) + (pD(4) >> 16);
        w.at<i16>(34) = static_cast<i16>(v11);           // *(v4+34) = v11

        i16 v12;
        if ((w.ld<i32>(32) >> 16) >= (env.screenExtB8 >> 16))  // *(v4+32)>>16 >= (B8+2)>>16
            v12 = static_cast<i16>(env.screenExtBC);     // LOWORD(dword_69FFBC)
        else
            v12 = w.at<i16>(34);                         // *(v4+34)
        w.at<i16>(34) = v12;                             // *(v4+34) = v12

        i32 v13 = w.at<i32>(44);                         // re-read *(v4+44)
        auto pD13 = [&](int off) -> i32 {
            return env.parentBoundsDword ? env.parentBoundsDword(env.ctx, v13, off) : 0;
        };
        auto pW13 = [&](int off) -> i16 {
            return env.parentBoundsWord ? static_cast<i16>(env.parentBoundsWord(env.ctx, v13, off)) : 0;
        };

        i16 v14;
        if ((pD13(2) >> 16) <= (w.ld<i32>(26) >> 16))    // *(v13+2)>>16 <= *(v4+26)>>16
            v14 = w.at<i16>(28);                         // *(v4+28)
        else
            v14 = pW13(4);                               // *(v13+4)
        w.at<i16>(28) = v14;                             // *(v4+28) = v14

        // v15 = (*(v9+2)>>16) + (*(v9+6)>>16);
        i32 v15 = (pD(2) >> 16) + (pD(6) >> 16);
        if (v15 >= (w.ld<i32>(28) >> 16))                // v15 >= *(v4+28)>>16
            v15 = w.ld<i32>(28) >> 16;
        w.at<i16>(30) = static_cast<i16>(v15);           // *(v4+30) = v15

        i16 v16;
        if ((w.ld<i32>(28) >> 16) >= (env.screenExtBC >> 16))  // *(v4+28)>>16 >= BC>>16
            v16 = static_cast<i16>(env.screenExtBC >> 16);     // HIWORD(dword_69FFBC)
        else
            v16 = w.at<i16>(30);                         // *(v4+30)
        w.at<i16>(30) = v16;                             // *(v4+30) = v16
    }

    // Type dispatch (re-read the type byte: v17 = *(v4+24)).
    u8 v17 = w.at<u8>(24);
    if (v17 >= 0x40u) {
        if (v17 > 0x40u) {
            if (v17 == 65) {                            // 'A' 3D/anim anchor
                int win = w.at<i32>(116);                // *(v4+116) (ownerWindow)
                i32 xHi = w.ld<i32>(14) >> 16;           // *(v4+14)>>16
                i32 yHi = w.ld<i32>(16) >> 16;           // *(v4+16)>>16
                if (env.anchorTypeA)
                    env.anchorTypeA(env.ctx, win, xHi, yHi);
                // dword_695084[87*win] = xHi; dword_695088[87*win] = yHi;
            }
            // else (v17 in 0x41<.. but not 65 handled above) -> fall through to return
        } else {
            // v17 == 0x40 ('@'): window-backing — recurse into the owning window's
            // child widget list.
            int win = w.at<i32>(116);                    // *(v4+116) (ownerWindow)
            // Capture the OLD child-origin, then stamp the new origin (a1,a2):
            //   v26 = *(int*)(window+2) >> 16;  v25 = *(int*)(window+4) >> 16;
            //   *((WORD*)window+2) = a1; *((WORD*)window+3) = a2;   (window+4 / +6)
            i32 v26 = env.windowOriginX ? env.windowOriginX(env.ctx, win) : 0;
            i32 v25 = env.windowOriginY ? env.windowOriginY(env.ctx, win) : 0;
            if (env.windowStampOrigin)
                env.windowStampOrigin(env.ctx, win, a1, a2);

            // Clamp the two bound fields against dword_69FFBC before recursing
            // (loc_4133FD / loc_413419):
            if ((w.ld<i32>(28) >> 16) > (env.screenExtBC >> 16))
                w.at<i16>(30) = static_cast<i16>(env.screenExtBC >> 16); // HIWORD(BC)
            if ((w.ld<i32>(32) >> 16) > (env.screenExtB8 >> 16))
                w.at<i16>(34) = static_cast<i16>(env.screenExtBC);       // LOWORD(BC)

            int childCount = env.windowChildCount ? env.windowChildCount(env.ctx, win) : 0;
            for (int v21 = 0; v21 < childCount; ++v21) {
                int childId = env.windowChildId ? env.windowChildId(env.ctx, win, v21) : 0;
                // child new x = newOriginX(a1) + child.x(+16) - v26
                // child new y = newOriginY(a2) + child.y(+18) - v25
                Widget& c = g_widgets[childId];
                i16 cx = static_cast<i16>(a1 + c.at<i16>(16) - v26);
                i16 cy = static_cast<i16>(a2 + c.at<i16>(18) - v25);
                WidgetLayoutBounds(env, cx, cy, childId);   // recurse
            }
        }
    } else if (v17 == 4) {                              // type 4 corner anchor
        int win = w.at<i32>(116);                        // *(v4+116)
        i16 ax = w.at<i16>(16);                          // *(v4+16)
        i16 ay = w.at<i16>(18);                          // *(v4+18)
        if (env.anchorType4)
            env.anchorType4(env.ctx, win, ax, ay);
        // word_140642D[17*win] = ax; word_140642F[17*win] = ay;
        v17 = static_cast<u8>(ay);   // the original's v17 = the +18 word (returned LOBYTE)
    }

    return v17;
}

} // namespace guild::gui
