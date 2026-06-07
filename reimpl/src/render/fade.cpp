#include "render/fade.h"

namespace guild::render {

// gilde.exe 0x41f1cc — alpha core of VIBE_Fade_Update.
float FadeAlpha(int dir, u32 now, u32 startTick, u32 lastTick, int duration) {
    // v24 = (float)(unsigned)(now - start). The per-frame step clamp: if the
    // gap since the last update exceeds 3 ticks, advance by at most 3.
    float raw = (float)(u32)(now - startTick);
    if ((u32)(now - lastTick) > 3u)
        raw = (float)((i32)lastTick + 3 - (i32)startTick);

    double t;
    if (dir & kFadeIn)
        t = raw / (double)duration;
    else if (dir & kFadeOut)
        t = 1.0 - raw / (double)duration;
    else
        t = 0.0; // neither bit set: original leaves t undefined; treat as 0

    // clamp(t, 0, 1) exactly as the two staged compares in the original.
    double low = (t <= 0.0) ? 0.0 : t;
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
