#pragma once
// gilde.exe 0x56c0cc — VIBE_Config_ApplyCameraAndScrollSettings (namespace
// guild::play).  Applies the in-memory options block (dword_1233558 ..) to the
// live camera scroll-speed + mouse wheel + edge-scroll-margin globals.  Called
// from the options-screen accept path and from boot (display init, root 0x527fa4)
// after WriteGfxSettings.
//
// Decompile + disasm (the decompile's uninitialised `v1` was edx == the options
// block base; corrected here):
//   dword_62D0E4 = 0;
//   if ( word_63C740 & 4 )                                   // "advanced scroll" gfx flag
//       scrollSpeed = (float)((double)(unsigned)dword_631284 * 0.25 + 0.5) * 0.8;
//   else
//       scrollSpeed = (float)((double)(int)opt.field0 * 0.00625 + 0.25);
//   dword_62D07C = scrollSpeed;                              // camera scroll-speed scalar
//   VIBE_Input_SetWheelBase(opt.wheel - 64);                // dword_62D0B8 = (wheel-64)+256
//   edgeMargin = opt.sensitivityByte;                       // *(block+4), 0..100
//   dword_6316C8 = 100 - edgeMargin;
//   return edgeMargin;
//
// Recovered FP constants (get_bytes):
//   flt_62520C = 0x3BCCCCCD              == 0.00625  (linear scroll slope)
//   dbl_625214 = 0x3FD0000000000000     == 0.25     (linear base / advanced slope)
//   dbl_62521C = 0x3FE0000000000000     == 0.5      (advanced base)
//   dbl_625224 = 0x3FE999999999999A     == 0.8      (advanced scale)
#include "guild/common/types.h"

namespace guild::play {

namespace cfgcam {
constexpr float  kLinearSlope   = 0.00625f; // flt_62520C
constexpr double kLinearBase    = 0.25;     // dbl_625214 (also advanced slope)
constexpr double kAdvancedBase  = 0.5;      // dbl_62521C
constexpr double kAdvancedScale = 0.8;      // dbl_625224
constexpr u16    kAdvancedFlag  = 4;        // word_63C740 & 4
}  // namespace cfgcam

// The options block fields the function reads (dword_1233558 region):
//   field0          (+0x00) int   — the linear-scroll slider value
//   sensitivityByte (+0x04) u8    — edge-scroll/mouse sensitivity, 0..100
//   wheel           (+0x08) int   — mouse-wheel base
struct CameraScrollOptions {
    i32 field0          = 0;   // dword_1233558
    u8  sensitivityByte = 0;   // byte at 0x123355C
    i32 wheel           = 0;   // dword_1233560
};

// The live globals the function writes (the original's file globals).
struct CameraScrollGlobals {
    i32   focusReset   = 0;   // dword_62D0E4 (cleared to 0)
    float scrollSpeed  = 0.0f; // dword_62D07C (camera scroll-speed scalar)
    i32   wheelBase    = 0;   // dword_62D0B8 (via Input_SetWheelBase)
    i32   edgeMargin   = 0;   // dword_6316C8 (== 100 - sensitivity)
};

// `advancedScroll` models (word_63C740 & 4) != 0; `advancedRaw` is the unsigned
// dword_631284 used in the advanced branch.  Writes `g` and returns the
// sensitivity byte (the original's `result`).
i32 ConfigApplyCameraAndScrollSettings(const CameraScrollOptions& opt,
                                       bool advancedScroll, u32 advancedRaw,
                                       CameraScrollGlobals& g);

}  // namespace guild::play
