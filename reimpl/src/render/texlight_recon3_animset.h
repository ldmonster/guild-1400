#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::render — texlight_recon3: the animated-texture-set loader leaf.
//
// Reconstructed 1:1 from the Hex-Rays decompile / disassembly of:
//   0x5da4b4  VIBE_Texture_LoadAnimatedSet
//             __usercall: name@<eax>, flags@<edx>, loadFlag@<bl>, palIdx@<cl>
//             returns char@<al>
//
// WHAT THIS FUNCTION DOES (pure, recoverable logic — reconstructed here):
//   Given a texture name, it detects the "animated set" naming convention
//   "<base>_A<sep><digits>" (e.g. the trailing digits form a frame number and
//   the uppercased name contains the literal "_A" two/one chars before the
//   non-digit separator that precedes the run of trailing digit-class chars).
//   It:
//     1. makes three working copies of the name (working / member / upper),
//     2. uppercases the third copy,
//     3. scans the trailing run of digit-class characters (ctype bit 0x20 of
//        byte_64A208[(u8)(c+1)]) recording whether any digit was seen,
//     4. gates on the exact "_A" pattern + bounds (see .cpp for the precise
//        unsigned comparisons translated from the asm),
//     5. parses the trailing integer (VIBE_Util_ParseInt) as the frame number,
//     6. if (frame!=0 && (flags & 0x40000)==0) it aborts (returns 0),
//     7. otherwise stamps the caller record (off +0x50 = current anim id,
//        off +0x71 = (u8)frame — the parsed frame's LOBYTE, NOT frame+1; the
//        +1 only applies to the bank records' +0x70 below), formats the
//        candidate next-frame BMP path
//        "%s/%s%i.bmp" and tests it for existence,
//        - if the next frame file EXISTS: format "%s%i" and call
//          VIBE_Texture_LoadByName(name, flags|.., palIdx, loadFlag),
//        - else: walk the global 128-byte texture-record bank
//          (dword_1406A84 base, dword_1406A80 count); for each record whose
//          refcount(+0x40) > 0 and whose anim id(+0x50) == the current id
//          (dword_1406A58), if its name contains the member base (strstr-like
//          loc_5CB930) stamp its frame field(+0x70) = frame+1; then increment
//          the global anim id (dword_1406A58).
//
// PLATFORM / ENGINE BOUNDARY (Rule 3/8): the actual BMP path resolution
// (VIBE_Texture_BuildBmpPath 0x5d97e8), VFS existence test
// (VIBE_Vfs_FileExists 0x5dc770), the real loader (VIBE_Texture_LoadByName
// 0x5da714), and the global record bank + strstr record matcher (loc_5CB930)
// are NOT reconstructed in this leaf — they live elsewhere in the engine and
// touch unreconstructed global texture state. They are routed through an
// inert-default AnimSetHooks struct so the pure naming/branch logic above is
// testable headless. No cheap analogue stands in for the engine's loader.
//
// Types per include/guild/common/types.h.
// =============================================================================
namespace guild::render {

// gilde.exe byte_64A208 digit-class test: bit 0x20 of byte_64A208[(u8)(c+1)].
// (Same 256-byte ctype table reconstructed verbatim in src/sim/script_lexer.cpp;
// this leaf only needs the digit-class predicate, reproduced bit-for-bit.)
bool AnimSet_IsDigitClass(u8 c);

// One record in the global 128-byte texture-record bank that LoadAnimatedSet
// walks. Only the fields the function touches are modelled, at their exact
// byte offsets.
struct AnimSetRecord {
    int  refCount;   // +0x40  (dword [ecx+40h])  > 0 == active
    int  animId;     // +0x50  (dword [ecx+50h])  current anim-set id
    u8   frame;      // +0x70  (byte  [ecx+70h])  frame index stamp
    const char* name = nullptr; // record name passed to the strstr matcher
};

// Hooks for the engine/VFS boundary callees. Defaults are INERT (Rule 3/8):
// no path resolves, no file exists, the loader reports failure — so only the
// pure naming/branch logic runs in a headless build.
struct AnimSetHooks {
    // VIBE_Texture_BuildBmpPath(name) 0x5d97e8 — resolve a base directory for
    // the texture's BMP. Returns null on failure (default: null).
    const char* (*buildBmpPath)(const char* name) = nullptr;
    // VIBE_Vfs_FileExists(path) 0x5dc770 — true iff the path resolves to a file
    // (default: false).
    bool (*fileExists)(const char* path) = nullptr;
    // VIBE_Texture_LoadByName(name, flags, palIdx, loadFlag) 0x5da714 — the real
    // texture loader. Returns the loader result byte (default: 0).
    u8 (*loadByName)(const char* name, int flags, u8 palIdx, u8 loadFlag) = nullptr;

    // The global texture-record bank (dword_1406A84 base / dword_1406A80 count).
    AnimSetRecord* bank = nullptr;
    int            bankCount = 0;
    // The global current anim-set id (dword_1406A58). Read AND incremented.
    int*           nextAnimId = nullptr;
    // loc_5CB930 strstr-like matcher: returns true iff record name `hay`
    // contains the member base `needle` (default: built-in strstr semantics).
    bool (*recordNameContains)(const char* hay, const char* needle) = nullptr;
};

// The caller record (addressed as `ebp` / a1 in the original): only the two
// fields the function stamps are modelled at their exact offsets.
struct AnimSetCallerRecord {
    int curAnimId;   // +0x50  (dword [ebp+50h]) <- dword_1406A58
    u8  frameStamp;  // +0x71  (byte  [ebp+71h]) <- (u8)frame  [LOBYTE, 0x5da609]
};

// Result of the analysis phase (exposed for golden tests). `matched` is true
// when the "_A<...><digits>" gate passed AND a digit was present (i.e. the
// original would proceed past loc_5DA5C1). `frame` is the parsed integer.
struct AnimSetParse {
    bool matched   = false;  // gate at 0x5da59a..0x5da5b5 passed
    bool sawDigit  = false;  // bl set during the trailing-digit scan
    int  frame     = 0;      // VIBE_Util_ParseInt of the trailing digits
    int  sepIndex  = 0;      // edx: index of the non-digit char before digits
    char working[64] = {0};  // var_5C  copy of the name
    char member[64]  = {0};   // var_9C  second copy
    char upper[64]   = {0};   // var_DC  uppercased copy
};

// Pure analysis half: copies/uppercases/scans/gates and (if matched) ParseInt.
// Reproduces the original through the `loc_5DA5C1` decision point. Returns the
// parse; `matched==false` means the original returns 0 immediately.
AnimSetParse AnimSet_Analyze(const char* name);

// gilde.exe 0x5da4b4 — VIBE_Texture_LoadAnimatedSet (full leaf).
// `name`=a1 (also the caller record), `flags`=a2/edx, `loadFlag`=a3/bl,
// `palIdx`=cl. `rec` is the caller record stamped at +0x50/+0x71. Boundary
// callees go through `hooks`. Returns the original's `char` result.
u8 VIBE_Texture_LoadAnimatedSet(const char* name, int flags, u8 loadFlag,
                                u8 palIdx, AnimSetCallerRecord& rec,
                                const AnimSetHooks& hooks);

} // namespace guild::render
