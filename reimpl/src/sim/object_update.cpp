// =============================================================================
// object_update.cpp — see object_update.h for the module overview + provenance.
// The pure logic of VIBE_Object_Update / VIBE_Animation_Apply /
// VIBE_EntityChild_Process / VIBE_Entity_InteractionLogic. namespace guild::sim.
// =============================================================================
#include "sim/object_update.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace guild::sim {

using namespace anim_const;

// ConvertXTrunc is an inline alias of guild::util::ConvertX (header) — not
// redefined here (one-definition rule).

// ---------------------------------------------------------------------------
// gilde.exe 0x40f044 — count decimal digits, cap 10, 0 for value<=0.
// ---------------------------------------------------------------------------
int CountDigits(int value) {
    int v10 = 0;                 // v10
    int v9 = value;             // v9
    if (v9 > 0) {               // 0x40f04b
        do {                    // 0x40f062
            ++v10;
            v9 /= 10;
        } while (v10 < 10 && v9 > 0);
    }
    return v10;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x40f06d..0x40f3b5 — weight / amount formatting.
// ---------------------------------------------------------------------------
std::string FormatAmount(u8 flags, int count, int unitWeight, int digits) {
    char buf[64];
    if (flags & 4) {                                       // 0x40f06d mass
        const int total = unitWeight * count;             // *(node+79)* *(node+74)
        if (total >= 1000) {                              // 0x40f085
            if (total >= 1000000)                         // 0x40f311
                std::snprintf(buf, sizeof buf, "%.1f t",
                              static_cast<double>(total) * kWeightTon);  // 0x40f368
            else
                std::snprintf(buf, sizeof buf, "%.1f kg",
                              static_cast<double>(total) * kWeightKg);   // 0x40f337
        } else {
            std::snprintf(buf, sizeof buf, "%li g",
                          static_cast<long>(total));  // 0x40f096 ("%li g", int arg)
        }
    } else if (flags & 8) {                               // 0x40f378 zero-padded
        char fmt[32];
        std::snprintf(fmt, sizeof fmt, "%%0%ii ",
                      static_cast<unsigned char>(digits)); // "%%0%ii " 0x40f389
        std::snprintf(buf, sizeof buf, fmt, count * unitWeight);  // 0x40f3a4
    } else {
        std::snprintf(buf, sizeof buf, "%i", count);      // 0x40f3b5
    }
    return std::string(buf);
}

// ---------------------------------------------------------------------------
// gilde.exe 0x40f6b0..0x40f710 — build-progress percent ("%i%%").
//   if (fill>1.0) v40=1.0; else v40=fill;  v33 = v40 * 100.0; ConvertX trunc.
// ---------------------------------------------------------------------------
std::string FormatBuildPercent(float fill, int* outPercent) {
    double v40 = (fill > 1.0f) ? 1.0 : static_cast<double>(fill);  // 0x40f6c3/0x40f770
    double v33 = v40 * kPctScale100;                              // 0x40f6ec
    int pct = static_cast<int>(ConvertXTrunc(v33));              // 0x40f6f2 trunc
    if (outPercent) *outPercent = pct;
    char buf[32];
    std::snprintf(buf, sizeof buf, "%i%%", pct);                 // 0x40f710
    return std::string(buf);
}

// ---------------------------------------------------------------------------
// gilde.exe 0x41089e..0x410920 — fill-bar fraction with the <1.0-bits snap.
// ---------------------------------------------------------------------------
BarFraction ComputeBarFraction(int maxLen, int full, int empty, int cur, int target) {
    const int denom = full - empty;                       // v128 - v131
    const double v5 = static_cast<double>(maxLen) /
                      static_cast<double>(denom);          // 0x41089e
    float filled = static_cast<float>(
        static_cast<double>(cur - empty) * v5);            // 0x4108c8
    float tgt = static_cast<float>(
        static_cast<double>(target - empty) * v5);         // 0x4108d8

    // if (filled > 0.0 && bits(filled) < 1.0bits) filled = 1.0;  0x4108f8
    auto bits = [](float f) -> u32 { u32 b; std::memcpy(&b, &f, 4); return b; };
    if (filled > 0.0f && bits(filled) < 0x3f800000u) filled = 1.0f;  // 0x4108fa
    if (tgt   > 0.0f && bits(tgt)   < 0x3f800000u) tgt   = 1.0f;     // 0x410920
    return {filled, tgt};
}

// ---------------------------------------------------------------------------
// gilde.exe 0x41186d — bar percentage label "%i%%" (cur*100/full + 0.5, trunc).
// ---------------------------------------------------------------------------
std::string FormatBarPercent(int cur, int full, int* outPercent) {
    double v76 = static_cast<double>(cur) * static_cast<double>(kBarH) /
                 static_cast<double>(full) + kHalf;        // 0x41186d
    int pct = static_cast<int>(ConvertXTrunc(v76));        // 0x411873 trunc
    if (outPercent) *outPercent = pct;
    char buf[32];
    std::snprintf(buf, sizeof buf, "%i%%", pct);           // 0x411894
    return std::string(buf);
}

// ---------------------------------------------------------------------------
// gilde.exe 0x418fe4..0x41945f — child priority class (8/6/4/2 -> +608).
// ---------------------------------------------------------------------------
int ChildPriorityClass(int v) {
    if (!v) return 0;                 // 0x418fe4
    if (v % 8 == 0) return 8;         // 0x418ffc
    if (v % 6 == 0) return 6;         // 0x41941f
    if (v % 4 == 0) return 4;         // 0x41943d
    if (v % 2 == 0) return 2;         // 0x41945f
    return 0;
}

// ===========================================================================
// gilde.exe 0x415e11..0x416245 — VIBE_Animation_Apply word-wrap layout.
//
// The original packs words (split on spaces) into the 96-byte-strided v59 line
// buffer until the next word would overflow `maxWidth` (a3=v65) or a '~' break
// is hit. On overflow of a single long word it scans back to the last '~' or
// '-' (a trailing '~' becomes '-'); '~' itself ends a line and is dropped.
// We reconstruct the line-breaking decision exactly (the layout); the per-glyph
// blit (Coord_Transform + Animation_Basic) is the renderer's responsibility.
// ===========================================================================
WrapResult WrapText(const std::string& text, int maxWidth,
                    int (*charAdvance)(int ch), int wordGap, int glyphGap,
                    int lineAdvance) {
    WrapResult out;
    out.lineCount = 0;
    out.contentH  = 0;

    const int total = static_cast<int>(text.size());      // v73 = strlen(a5)
    int pos = 0;                                           // v75 (read cursor)
    int lineWidth = 0;                                     // v6 (accumulated width)
    int lineStart = 0;                                     // start of current line
    std::string cur;                                       // current line text

    auto wordWidth = [&](const std::string& w) -> int {
        // VIBE_Property_Get(word, font) measures the word's pixel width.
        int wpx = 0;
        for (size_t i = 0; i < w.size(); ++i) {
            if (i) wpx += glyphGap;
            wpx += charAdvance(static_cast<unsigned char>(w[i]));
        }
        return wpx;
    };
    auto flushLine = [&]() {
        if (!cur.empty() || out.lineCount == 0) {
            if (out.lineCount) out.lines.push_back('\n');
            out.lines += cur;
            ++out.lineCount;
            out.contentH += lineAdvance;                  // each line adds height
        }
        cur.clear();
        lineWidth = 0;
        lineStart = pos;
    };

    while (pos < total) {                                  // v73 > v75
        // Extract the next token up to the next space or '~' (the inner copy
        // loop 0x415e53; '~' is a hard break, space is a soft break).
        int wordStart = pos;
        bool hardBreak = false;
        while (pos < total && text[pos] != ' ' && text[pos] != '~') ++pos;
        std::string word = text.substr(wordStart, pos - wordStart);
        bool trailingSpace = false;
        if (pos < total) {
            if (text[pos] == '~') hardBreak = true;       // *(v23-1) == 126
            else trailingSpace = true;                    // soft space
            ++pos;                                        // consume separator
        }

        const int wpx = wordWidth(word);                  // v72 = Property_Get
        const int add = (cur.empty() ? 0 : wordGap) + wpx;

        if (!cur.empty() && lineWidth + add > maxWidth) {
            // word doesn't fit on the current line -> wrap before it (0x415f0d).
            flushLine();
            // place the word on the fresh line.
            if (wpx > maxWidth) {
                // single word overflow: break back to last '-' (0x415f69 scan).
                std::string head;
                size_t cut = word.find_last_of('-');
                if (cut != std::string::npos) {
                    head = word.substr(0, cut + 1);
                    word = word.substr(cut + 1);
                    cur = head;
                    lineWidth = wordWidth(head);
                    flushLine();
                }
            }
            cur = word;
            lineWidth = wpx;
        } else {
            if (!cur.empty()) { cur += ' '; lineWidth += wordGap; }
            cur += word;
            lineWidth += wpx;
        }
        (void)trailingSpace;
        if (hardBreak) flushLine();                       // '~' ends the line
    }
    flushLine();

    return out;
}

// ===========================================================================
// Wave-21 — additional fully-recovered renderer-body fragments.
// ===========================================================================

// gilde.exe 0x40eef4..0x40ef12 — Object_Update label-width clamp.
int ObjectUpdateClampWidth(int width, int maxCap) {
    if (width < 48) width = 48;                 // 0x40eef4
    if (maxCap > 0 && width >= maxCap)          // 0x40ef05 / 0x40ef0c
        width = maxCap;                         // 0x40f016
    return width;
}

// gilde.exe 0x419076..0x4190c9 — EntityChild_Process scroll-thumb gate.
ScrollArrowFlags EntityChildScrollArrows(int thumbPos, int pos16, int rangeTop,
                                         int page, int step) {
    ScrollArrowFlags r;
    // node235: first comparison (0x419076) then second (0x4190a9) overwrites.
    //   first  : (thumbPos + pos16) <= rangeTop ? 0 : 1
    //   second : (page + step + pos16) >= rangeTop ? 1 : 0   (wins)
    (void)thumbPos;
    r.node235 = ((page + step + pos16) >= rangeTop) ? 1 : 0;   // 0x4190a9
    // node234 (0x4190c7): step (v10[146]) nonzero -> OFF (0); else ON (1).
    r.node234 = (step != 0) ? 0 : 1;
    return r;
}

// gilde.exe 0x4190fd..0x41910c — EntityChild_Process scroll percent (trunc).
int EntityChildScrollPercent(int rangeTop, int thumbPos) {
    double v14 = static_cast<double>(rangeTop) /
                 static_cast<double>(thumbPos) + kHalf; // dbl_610F14 = 0.5
    return static_cast<int>(ConvertXTrunc(v14));        // 0x419107 truncate
}

// gilde.exe 0x41924a / 0x4192c5 — EntityChild_Process 9-slice tile counts.
BorderTileCounts EntityChildBorderTiles(int width, int height) {
    BorderTileCounts c;
    c.horiz = (width - 16) >> 3;                 // 0x41924a  (top/bottom runs)
    c.vert  = (height - 16) >> 3;                // 0x4192c5  (left/right runs)
    return c;
}

}  // namespace guild::sim
