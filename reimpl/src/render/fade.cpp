#include "render/fade.h"

namespace guild::render {

// gilde.exe 0x41f1cc — alpha core of VIBE_Fade_Update.
float FadeAlpha(int dir, u32 now, u32 startTick, u32 lastTick, int duration) {
    // v24 = (float)(unsigned)(now - start). The per-frame step clamp: if the
    // gap since the last update exceeds 3 ticks, advance by at most 3.
    float raw = (float)(u32)(now - startTick);
    if ((u32)(now - lastTick) > 3u)
        raw = (float)((i32)lastTick + 3 - (i32)startTick);

    // v23 is a FLOAT store (fstp dword) of the double ratio; the clamp compares
    // and widens the float copy, not the 80-bit value.
    float v23;
    if (dir & kFadeIn)
        v23 = (float)(raw / (double)duration);
    else if (dir & kFadeOut)
        v23 = (float)(1.0 - raw / (double)duration);
    else
        v23 = 0.0f; // neither bit set: original leaves v23 undefined; treat as 0

    // clamp(v23, 0, 1) exactly as the two staged compares in the original
    // (v20 = (double)v23; v21/v25).
    double low = (v23 <= 0.0f) ? 0.0 : (double)v23;
    float alpha = (low >= 1.0) ? 1.0f : (float)low;
    return alpha;
}

bool FadeIsDone(int dir, float alpha) {
    if ((dir & kFadeIn) && alpha == 1.0f)
        return true;
    if ((dir & kFadeOut) && alpha == 0.0f)
        return true;
    return false;
}

} // namespace guild::render
