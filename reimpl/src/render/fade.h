#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::render — screen fade / transition alpha interpolation (gilde.exe
// d2_fade / interface). Faithful reconstruction of the t-and-alpha core of:
//
//   0x41f0e8  VIBE_Fade_Register   (alloc + register a fade; 100-byte record)
//   0x41f1cc  VIBE_Fade_Update     (advance one fade; compute t, draw, finish)
//
// The Fade record (d2:fadeinfo, 100 bytes) — offsets recovered from Register +
// Update:
//   +0x00  flags byte (bit0 = fade-in dir, bit1 = fade-out dir, bit2 = done,
//                       bit3 = teardown)
//   +0x44 (68)  rect x       +0x48 (72)  rect y
//   +0x4C (76)  rect w       +0x50 (80)  rect h
//   +0x54 (84)  duration (ticks)
//   +0x58 (88)  start tick   +0x5C (92)  last tick
//   +0x60 (96)  texture handle
//
// INTERPOLATION (the testable core): given now/start/duration and the direction
// bit, the fade alpha is computed as
//   raw   = (now - start)               [but if (now - lastTick) > 3, use
//                                         lastTick + 3 - start instead — a
//                                         per-frame step clamp]
//   t     = raw / duration              (bit0 fade-in)
//         = 1 - raw / duration          (bit1 fade-out)
//   t     = clamp(t, 0, 1)
// At t==1 (fade-in) or t==0 (fade-out) the fade is marked done (bit2). This file
// reproduces that alpha math exactly; the actual draw/teardown calls (DDraw quad
// blit, back-buffer fill) are out of scope and documented in the report.
// =============================================================================
namespace guild::render {

enum FadeDir { kFadeIn = 1, kFadeOut = 2 };

// gilde.exe 0x41f1cc (alpha core). Returns the clamped [0,1] alpha for a fade.
//   dir       : kFadeIn or kFadeOut (the +0 flags bit0/bit1)
//   now       : current global tick (dword_62EB44)
//   startTick : +0x58
//   lastTick  : +0x5C
//   duration  : +0x54  (must be > 0)
// The (now-last)>3 per-frame clamp is applied first (matches the original's
// v24 = (now-start), but if (now-last)>3 then v24 = (last+3-start)).
float FadeAlpha(int dir, u32 now, u32 startTick, u32 lastTick, int duration);

// True when the fade has reached its terminal alpha (bit2 would be set):
//   fade-in  done when alpha == 1.0 ; fade-out done when alpha == 0.0.
bool FadeIsDone(int dir, float alpha);

} // namespace guild::render
