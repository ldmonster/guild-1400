#include "render/texlight_recon3_animset.h"

#include <cstring>
#include <cstdio>

using f32 = float;
using f64 = double;

namespace guild::render {

namespace {

// gilde.exe byte_64A208 @0x64A208 — 256-byte ctype table, verbatim (recovered
// via get_bytes). Only bit 0x20 (digit class) is consumed here; the full table
// is reproduced so the predicate is bit-identical to the original.
const u8 kCharClass[256] = {
    0x00,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x03,0x03,0x03,0x03,0x03,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x0a,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,
    0x0c,0x38,0x38,0x38,0x38,0x38,0x38,0x38,0x38,0x38,0x38,0x0c,0x0c,0x0c,0x0c,0x0c,
    0x0c,0x0c,0x58,0x58,0x58,0x58,0x58,0x58,0x48,0x48,0x48,0x48,0x48,0x48,0x48,0x48,
    0x48,0x48,0x48,0x48,0x48,0x48,0x48,0x48,0x48,0x48,0x48,0x0c,0x0c,0x0c,0x0c,0x0c,
    0x0c,0x98,0x98,0x98,0x98,0x98,0x98,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,
    0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x88,0x0c,0x0c,0x0c,0x0c,0x01,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
};

// VIBE_Util_StrToUpper 0x5e9f50 — in-place ASCII upper-case of 'a'..'z' only.
void StrToUpper(char* s) {
    for (; *s; ++s) {
        char c = *s;
        if (c >= 'a' && c <= 'z') *s = (char)(c - ('a' - 'A'));
    }
}

// VIBE_Util_ParseInt 0x5dc070 — leading signed decimal (atoi-style). The
// original is a lenient signed parser; for the trailing-digit run consumed here
// the input is always a run of '0'..'9' (or empty), so atoi semantics suffice.
int ParseInt(const char* s) {
    int sign = 1;
    if (*s == '-') { sign = -1; ++s; }
    else if (*s == '+') { ++s; }
    int v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); ++s; }
    return sign * v;
}

// The two-byte-stride copy loop the original uses (loc_5DA4D9 etc.). For a
// normal C-string it is exactly a byte-wise strcpy that stops at (and copies)
// the terminating NUL.
void CopyName(char* dst, const char* src) {
    const char* s = src;
    char* d = dst;
    for (;;) {
        char c = *s;
        *d = c;
        if (!c) break;
        char c2 = s[1];
        s += 2;
        d[1] = c2;
        d += 2;
        if (!c2) break;
    }
}

// Default strstr-like matcher mirroring loc_5CB930's "name contains needle"
// observable result (non-null pointer == match). The original handles the
// empty/one-char-needle special cases identically to substring search.
bool DefaultRecordNameContains(const char* hay, const char* needle) {
    if (!hay || !needle) return false;
    return std::strstr(hay, needle) != nullptr;
}

} // namespace

bool AnimSet_IsDigitClass(u8 c) {
    return (kCharClass[(u8)(c + 1)] & 0x20) != 0;
}

AnimSetParse AnimSet_Analyze(const char* name) {
    AnimSetParse p;

    // Three working copies (var_5C / var_9C / var_DC). The originals are 64-byte
    // stack buffers; names longer than 63 are not produced by the engine.
    CopyName(p.working, name);   // var_5C
    CopyName(p.member, p.working);  // var_9C
    CopyName(p.upper, p.working);   // var_DC
    StrToUpper(p.upper);            // 0x5da552

    // edx = strlen(working) - 1  (last char index). 0x5da557..0x5da561
    int len = (int)std::strlen(p.working);
    int edx = len - 1;

    // Trailing digit-class scan (loc_5DA564). Tests working[edx]; while it is a
    // digit-class char AND edx>0, sets sawDigit and decrements edx. Exits when a
    // non-digit char is hit OR edx reaches 0.
    bool sawDigit = false;
    for (;;) {
        u8 c = (u8)p.working[edx];
        if (!AnimSet_IsDigitClass(c)) break;          // jz loc_5DA584
        if (edx <= 0) break;                          // test edx,edx; jbe
        sawDigit = true;                              // bl = 1
        --edx;                                        // dec edx
    }
    p.sawDigit = sawDigit;
    p.sepIndex = edx;

    // ecx = strlen(working) - 2  (0x5da58b..0x5da595, re-scans length).
    int lenMinus2 = len - 2;

    // Gate (loc, unsigned comparisons translated exactly):
    //   if edx >= len-2            -> jnb  return 0
    //   if edx <= 1                -> jbe  return 0
    //   if upper[edx-1] != 'A'     -> jnz  return 0
    //   if upper[edx-2] != '_'     -> jnz  return 0
    //   if !sawDigit               -> jz   return 0
    // Note the asm uses unsigned compares; edx is a non-negative index here.
    if ((unsigned)edx >= (unsigned)lenMinus2) return p;       // 0x5da596
    if (edx <= 1) return p;                                    // 0x5da59a
    if (p.upper[edx - 1] != 'A') return p;                     // 0x5da59f (var_DD)
    if (p.upper[edx - 2] != '_') return p;                     // 0x5da5a9 (var_DE)
    if (!sawDigit) return p;                                    // 0x5da5b3

    // Matched: parse the trailing integer at working+edx+1 (lea eax,[edx+1]+base).
    p.frame   = ParseInt(&p.working[edx + 1]);                 // 0x5da5cd
    p.matched = true;
    return p;
}

// gilde.exe 0x5da4b4 — VIBE_Texture_LoadAnimatedSet.
u8 VIBE_Texture_LoadAnimatedSet(const char* name, int flags, u8 loadFlag,
                                u8 palIdx, AnimSetCallerRecord& rec,
                                const AnimSetHooks& hooks) {
    AnimSetParse p = AnimSet_Analyze(name);
    if (!p.matched) return 0;                                  // gate -> loc_5DA5B7

    // 0x5da5d9: test eax,eax; jz loc_5DA5E7  (if frame==0, fall through to E7)
    //           else test byte [var_1C+2],4; jz loc_5DA5B7 (return 0).
    // (byte [var_1C+2] bit 2 == bit 18 of the dword == 0x40000.)
    // => abort (return 0) iff frame != 0 AND (flags & 0x40000) == 0.
    if (p.frame != 0 && (flags & 0x40000) == 0) return 0;      // 0x5da5e5

    // Read the global anim id (dword_1406A58). 0x5da5fe.
    int curAnimId = hooks.nextAnimId ? *hooks.nextAnimId : 0;

    // Build the flags variant: var_14 = flags, then BYTE2(var_14) |= 4
    // i.e. set bit 0x40000 (0x5da603). 0x5da606: rec[+0x50] = curAnimId.
    int flagsOut = flags | 0x40000;
    rec.curAnimId = curAnimId;
    // 0x5da617: rec[+0x71] = (u8)frame  (LOBYTE of var_18 = ParseInt result).
    rec.frameStamp = (u8)p.frame;

    // member[edx+1] = 0  (0x5da621: var_9B at edx) — truncate the member copy
    // just after the separator so the strstr matcher uses "<base>_A<sep>".
    if ((unsigned)(p.sepIndex + 1) < sizeof(((AnimSetParse*)0)->member))
        p.member[p.sepIndex + 1] = 0;

    // BuildBmpPath(working) (0x5da628). Boundary: inert default -> null -> return 0.
    const char* base = hooks.buildBmpPath ? hooks.buildBmpPath(p.working) : nullptr;
    if (!base) return 0;                                        // 0x5da631

    // The original locates the LAST '/' in the resolved path and NUL-terminates
    // there (VIBE_Util_StrChr returns last occurrence), splitting dir from file;
    // it then formats "%s/%s%i.bmp" with (dir, member, frame+1). We mirror the
    // observable inputs to the existence test without mutating the hook's buffer.
    char dir[260];
    std::snprintf(dir, sizeof(dir), "%s", base);
    char* slash = std::strrchr(dir, '/');
    if (slash) *slash = 0;                                      // 0x5da641

    char candidate[600];
    std::snprintf(candidate, sizeof(candidate), "%s/%s%i.bmp",
                  dir, p.member, p.frame + 1);                  // 0x5da65f

    bool exists = hooks.fileExists ? hooks.fileExists(candidate) : false; // 0x5da669

    if (exists) {
        // 0x5da6d4: next-frame file exists. Format "%s%i" (member, palIdx) and
        // load it through the real loader.
        char loadName[280];
        std::snprintf(loadName, sizeof(loadName), "%s%i", p.member, palIdx); // 0x5da6eb
        if (hooks.loadByName)
            return hooks.loadByName(loadName, flagsOut, palIdx, loadFlag);    // 0x5da705
        return 0;
    }

    // 0x5da672: no next-frame file. Walk the global texture-record bank; for
    // each active record sharing the current anim id whose name contains the
    // member base, stamp its frame field = frame+1. Then increment the anim id.
    if (hooks.bank && hooks.bankCount > 0) {
        bool (*contains)(const char*, const char*) =
            hooks.recordNameContains ? hooks.recordNameContains
                                     : &DefaultRecordNameContains;
        for (int j = 0; j < hooks.bankCount; ++j) {            // loc_5DA684
            AnimSetRecord& r = hooks.bank[j];
            if (r.refCount > 0 && curAnimId == r.animId) {     // 0x5da684/0x5da690
                if (contains(r.name, p.member)) {              // loc_5CB930
                    r.frame = (u8)(p.frame + 1);               // 0x5da6b0
                }
            }
        }
    }
    if (hooks.nextAnimId) ++(*hooks.nextAnimId);               // 0x5da6c4
    return 0;
}

} // namespace guild::render
