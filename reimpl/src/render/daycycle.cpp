#include "render/daycycle.h"
#include "render/particle.h" // TruncToward

namespace guild::render {

// dword_4AD160 — recovered byte-for-byte (get_bytes @0x4ad160, 96 bytes).
const i32 kDayKeyframeMinutes[4][6] = {
    { 390, 510, 630, 1050, 1170, 1290 }, // season 0
    { 300, 420, 540, 1140, 1260, 1380 }, // season 1
    { 420, 540, 660, 1020, 1140, 1260 }, // season 2
    { 480, 600, 690,  960, 1020, 1140 }, // season 3
};

// gilde.exe 0x4b2438 — VIBE_DayCycle_BuildTimeTable. The original loops 6 times,
// computing h = min/60 (byte), m = min - 60*h, then result = 3600*h + 60*m. Note
// the byte casts are lossless for all table values (max minute 1380 -> h=23 fits
// a byte). Equivalent to the straight seconds conversion below.
void BuildTimeTable(int season, i32 outSeconds[6]) {
    const i32* row = kDayKeyframeMinutes[season & 3];
    for (int i = 0; i < 6; ++i) {
        u8 h = (u8)(row[i] / 60);          // h = (char)(min/60)
        // m = LOBYTE(min) - 60*h, stored/reloaded as a byte (wraps mod 256).
        // For all table values this byte-wrap yields the true 0..59 minute.
        u8 m = (u8)((u8)row[i] - 60 * (int)h);
        outSeconds[i] = 3600 * (int)h + 60 * (int)m;
    }
}

// gilde.exe 0x4b2504 (brightness piecewise core).
int UpdateBrightness(const i32 kf[6], int hour, int minute) {
    int t = 3600 * (u16)hour + 60 * minute; // v1
    if (t < kf[0])
        return 0;
    if (t < kf[1])
        return 100 * (t - kf[0]) / (kf[1] - kf[0]);
    if (t < kf[2])
        return 100 * (t - kf[1]) / (kf[2] - kf[1]) + 100;
    if (t < kf[3])
        return 200 * (t - kf[2]) / (kf[3] - kf[2]) + 200;
    if (t < kf[4])
        return 100 * (t - kf[3]) / (kf[4] - kf[3]) + 400;
    if (t < kf[5])
        return 100 * (t - kf[4]) / (kf[5] - kf[4]) + 500;
    return 600;
}

SkyBandSelect BrightnessToBand(int brightness) {
    double scaled = (double)brightness * kBandScale; // v3
    int whole = TruncToward(scaled);                 // (int)v3 toward zero
    SkyBandSelect s;
    s.band = whole % 7;            // dword_631DD0 = v11 % 7
    s.blend = (float)(scaled - (double)whole); // flt_631DD4 = v7 - v11
    return s;
}

} // namespace guild::render
