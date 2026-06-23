// misc_recon4_textlayout.cpp — see header for provenance and inert-hook notes.
#include "sim/misc_recon4_textlayout.h"
#include <cstring>  // strlen

namespace guild::sim {

u16 PropertySetDrawText(const TextLayoutEnv& env, int startX, const char* str,
                        int y, void* ctx, unsigned char mode,
                        TextLayoutResult* out) {
    int   v5   = startX;           // pen X (a1 -> v5)
    void* font = env.font;
    u16   lineHeight = env.lineHeight;
    int   clipRight  = env.clipRight;

    // 0x415a23: for (v13 = 0; strlen(a2) > v13; ++i)  -- strlen re-read each pass.
    unsigned int v13 = 0;
    const u8* i = reinterpret_cast<const u8*>(str);
    while (std::strlen(str) > v13) {
        u8 ch = *i;                                 // v15 = *i / v8

        GlyphMetrics gm{};
        if (env.glyphMetrics) gm = env.glyphMetrics(font, ch);  // Coord_Transform

        // 0x415a98: v5 -= *(u16*)(glyph+22)
        v5 -= gm.leftBearing;

        if (ch != '~') {                            // 0x415a9d
            if (ch == ' ') {
                v5 += env.spaceWidth;               // 0x415b56: v5 += dword_62D270
            } else {
                // 0x415ab1: if (mode & 4) shadow/outline pass
                if ((mode & 4) != 0 && env.drawVelocity)
                    env.drawVelocity(font, v5 + 6, y + 6, ctx, ch);   // 0x415ac3
                if (env.drawBasic)
                    env.drawBasic(font, v5, y, ctx, ch);              // 0x415adc
                if ((mode & 2) != 0 && env.drawAdvanced)
                    env.drawAdvanced(font, v5, y, ctx, ch);           // 0x415af7
                if ((mode & 1) != 0 && env.drawVelocity)
                    env.drawVelocity(font, v5, y, ctx, ch);           // 0x415b18
                // 0x415b29: v5 += dword_62D274 + *(u16*)(glyph+26)
                v5 += env.tracking + gm.advance;
            }
        }
        // 0x415b39: if (v5 >= clipRight) break;
        if (v5 >= clipRight)
            break;
        ++v13;
        ++i;
    }

    // 0x415a47/0x415a4c: publish extent
    if (out) {
        out->lineHeight = lineHeight;
        out->width      = v5 - startX;
    }
    return lineHeight;   // 0x415a52: return dword_69FFB0
}

} // namespace guild::sim
