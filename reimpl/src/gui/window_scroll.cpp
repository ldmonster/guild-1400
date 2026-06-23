// window_scroll.cpp — see header for provenance and the inert-hook rationale.
//
// gilde.exe 0x41536c VIBE_Window_LayoutScrollContent, translated 1:1 from the Hex-Rays
// decompile + disassembly. Variable names follow the decompile (v8 = accumulated line
// width, v36 = per-gap spacing, v30 = justify remainder, v17 = running pen, etc.).
#include "gui/window_scroll.h"
#include <cstddef>

namespace guild::gui {

namespace {

// *(u16*)(glyph+26) advance for char `ch`, via the env hook (Coord_Transform path).
inline int GlyphAdvance(const WinScrollEnv& env, unsigned char ch) {
    return env.glyphAdvance ? env.glyphAdvance(ch) : 0;
}

// One child record, or a zeroed default when out of range — the original always has a
// valid 740-byte slot, but the env may expose fewer for a focused test.
inline WinScrollChild ChildAt(const WinScrollWindow& win, int idx) {
    if (win.children && idx >= 0 && idx < win.childN) return win.children[idx];
    return WinScrollChild{};
}

} // namespace

int Window_LayoutScrollContent(WinScrollEnv& env, int y, void* ctx,
                               unsigned char flushFlag, int alignMode,
                               int xStart, int xEnd) {
    const int a2 = y;
    (void)flushFlag;
    const WinScrollWindow& win = *env.window;

    int v8  = 0;                 // 0x41538c: accumulated line width
    int v30 = 0;                 // 0x415395: justify remainder (var_70)
    int v37 = 0;                 // 0x41539f: "centre next word vertically" pending (var_48)
    int v36 = env.spaceWidth;    // 0x4153a3: per-gap spacing (var_4C), default dword_62D270
    const int v35 = win.flags;   // 0x4153ae: saved flags (var_54), the return value

    // 0x4153b4: if (alignMode) win.flags = alignMode;  (note: modifies a copy here — the
    // original writes the live record then restores v35 before returning, so the net
    // effect on the window flags is zero; we keep the local `flags` consistent.)
    int flags = win.flags;
    if (alignMode)              // 0x415461: mov [ecx+0Ch], esi
        flags = alignMode;

    // 0x4153ba: re-entry guard. If set, restore flags (no-op) and bail with v35.
    if (env.reentryGuard == 1)
        return v35;

    // ---- 0x4153ce: pass 1 — sum every word's pixel width into v8 ----------------------
    if (env.wordCount > 0) {
        for (int j = 0; j < env.wordCount; ++j)      // do/while v10 += 262 until 262*count
            v8 += env.words[j].width;                // *(int*)(&dword_672402+v10)>>16
    }

    // ---- 0x415413: starting child index from the window's reserve flags --------------
    // v12 = (flags&8)?1:0;  if (flags&0x20) v12 = ((flags&8)?1:0)+2;
    int v12 = (flags & 8) != 0;
    if ((flags & 0x20) != 0)
        v12 = ((flags & 8) != 0) + 2;

    // ---- 0x41541a: add the row's existing child widths into v8 ------------------------
    // for (; v12 < win.childCount; ++v12) { child = children[ idxArray[v12] ];
    //   if (child.dead || child.base>=a2+lineHeight || a2>=child.height+child.base
    //       || alignMode==64) continue;
    //   v8 += tracking + child.height(+18>>16) + 6; }
    //
    // NOTE the original indexes a per-window index array (win[6]) then the 740-byte child
    // table.  WinScrollChild already presents the resolved record; we iterate the same
    // [v12, childCount) range and treat children[] as that resolved sequence.
    {
        const int clip = a2 + env.lineHeightAccum;   // dword_69FFB0 + a2 (var_50)
        for (; v12 < win.childCount; ++v12) {
            WinScrollChild c = ChildAt(win, v12);
            if (c.dead)
                continue;                            // [eax+64h]!=0
            int v16 = c.base;                        // *(int*)(child+14)>>16
            if (v16 >= clip)                         // 0x41548b: base >= a2+lineHeight
                continue;
            if (a2 >= c.yw + v16)                    // 0x415497: a2 >= (child+14>>16)+(child+20>>16)
                continue;
            if (alignMode == 64)                     // 0x41549b
                continue;
            // 0x4154a2: v8 += tracking + (child+18>>16) + 6
            v8 += env.tracking + c.height + 6;
        }
    }

    // ---- 0x4154c5: choose the start pen v17 by alignment -----------------------------
    int v17 = xStart;                                // esi = a6 (var arg_8)
    int v36_eff = v36;                               // per-gap spacing for the draw pass
    // 0x4154cc: if (byte+12 < 0)  i.e. flags low byte sign bit (0x80) set  AND
    //           wordCount > 1  AND  flushFlag == 0  -> justify; else align.
    bool justify = ((char)(flags & 0xFF) < 0) && env.wordCount > 1 && flushFlag == 0;
    if (justify) {
        // 0x4154ed: spread (xEnd - xStart - v8) across (wordCount-1) gaps (signed).
        int span = xEnd - xStart - v8;               // a7 - a6 - v8
        int denom = env.wordCount - 1;
        v30     = span % denom;                       // idiv remainder (var_70)
        v36_eff = span / denom;                       // idiv quotient  (var_4C)
    } else {
        // 0x41567a: v21 = v36*(wordCount-1) + v8
        int v21 = v36 * (env.wordCount - 1) + v8;
        if ((flags & 0x40) != 0) {                    // right-align
            v17 = xEnd - v21;                         // 0x41568d
        } else if (((flags >> 8) & 1) != 0) {         // centre (byte+13 bit0)
            v17 = (xEnd - xStart - v21) / 2 + xStart; // 0x4156b9 (signed /2)
        }
    }
    env.penStart = v17;   // computed alignment start (diagnostic for tests)

    // ---- 0x415519: draw pass over each word ------------------------------------------
    if (env.wordCount > 0) {
        for (int wi = 0; wi < env.wordCount; ++wi) {
            const WinScrollWord& w = env.words[wi];

            // 0x41553c: per-word starting child index (same flag logic as pass 1).
            int v45 = (flags & 8) != 0;
            if ((flags & 0x20) != 0)
                v45 = ((flags & 8) != 0) + 2;

            int  v32 = -1;          // resolved "special" colour-context (var_68)
            bool v49 = false;       // suppress draw + post-advance (var_C)
            bool drewSpecial = false;

            // 0x41556d: if (ch0 == '$') decode the command byte ch1.
            if (w.ch0 == '$') {
                unsigned char c = w.ch1;
                if (c >= 0x54) {                       // 'T' and above
                    if (c > 0x54) {                    // > 'T'
                        if (c >= 0x5B) {               // '[' and above
                            if (c > 0x5B) {            // > '['
                                if (c == ']') {        // 0x4156fa
                                    v32 = 0;           // var_68 = 0
                                    // var_6C = dword_62D244 (font) — not needed headless
                                }
                            } else {                   // '[' : 0x4156e4
                                v32 = 0;
                            }
                        } else {                       // 'U'..'Z' below '['
                            // 0x4156c6: $Y etc -> set pen from this word's width field.
                            v49 = true;
                            v17 = w.width;             // *(int*)(&dword_672402+..)>>16
                        }
                    } else {                           // == 'T' : 0x4156c0 falls to draw
                        // (handled by the markup builder; here behaves as a normal word)
                    }
                } else if (c >= 0x48) {                // 'H'..'S'
                    if (c > 0x48) {                    // > 'H'
                        v49 = (c == 'M');              // 0x415732
                    } else {                           // == 'H' : 0x41571d
                        v37 = 1;                       // centre-pending
                        v49 = true;
                    }
                }
            }

            // 0x41559d: coloured-word path (flags byte bit6) selects the masked drawer.
            bool colored = (w.flagsByte & 0x40) != 0;
            if (colored && env.setMaskColor)
                env.setMaskColor(w.color0, w.color1); // Shape_SetMaskColor

            if (!v49) {
                // 0x4155fb: step the pen past child widgets occupying this row.
                {
                    const int rowClip = a2 + env.lineHeightAccum;   // var_18
                    for (; v45 < win.childCount; ++v45) {
                        WinScrollChild c = ChildAt(win, v45);
                        if (c.dead)
                            continue;
                        int v20 = c.base;              // *(int*)(child+14)>>16
                        // inner do/while: advance v17 past this child if it overlaps.
                        for (;;) {
                            if (v17 + w.width <= v20)            // 0x415651
                                break;
                            if (v17 >= v20 + c.height + 6)       // 0x415756
                                break;
                            int v22 = c.x;                      // child[4]>>16
                            if (v22 >= a2 + env.lineHeightAccum) // 0x415768
                                break;
                            if (a2 >= c.yw + v22)               // 0x415778
                                break;
                            if (alignMode == 64)               // 0x415783
                                break;
                            v17 = v20 + c.height + 6;           // 0x415789
                            if (c.dead) break;                 // while(!child[25])
                        }
                    }
                }

                if (v32 == -1) {
                    // 0x41584d: no special colour-context -> draw the word.
                    // The original then walks the word's individual glyphs (the v37 branch
                    // at 0x415861 uses Animation_Basic+Advanced, the normal branch at
                    // 0x41592a uses the selected drawer), pre-/post-advancing the pen by
                    // each glyph's metrics. At the env (line) granularity we draw the word
                    // through the hook and advance the pen by (tracking + advance); the
                    // per-glyph blit detail is the rule-3 render boundary (inert headless).
                    if (env.drawBasic && env.drawBasic(v17, a2, ctx, w.ch0)) {
                        if (v37 && env.drawAdvanced)
                            env.drawAdvanced(v17, a2, ctx, w.ch0);  // 0x4158e3
                        v17 += env.tracking + GlyphAdvance(env, w.ch0); // +26 advance
                    }
                    v37 = 0;       // 0x4159d1
                } else {
                    // 0x4157b5: draw the single special glyph from v32's context.
                    if (env.drawBasic && env.drawBasic(v17, a2, ctx, (unsigned char)v32))
                        v17 += env.tracking + GlyphAdvance(env, (unsigned char)v32);
                    drewSpecial = true;
                }
            }

            // 0x4157ef: gap advance — skip for '~' and for suppressed words.
            if (w.ch0 != 126 && !v49) {
                v17 += v36_eff;                        // 0x4157f9
                if (v30) {                             // distribute justify remainder
                    ++v17;
                    --v30;
                }
            }
            (void)drewSpecial;
        }
    }

    // 0x415835: dword_62D254 = 0;  win.flags = v35;  return v35;
    env.wordCount = 0;
    return v35;
}

} // namespace guild::gui
