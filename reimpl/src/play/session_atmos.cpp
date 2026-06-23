#include "play/session_atmos.h"

#include "crt/rand.h"           // crt::RandNext / crt::Srand (VIBE_Util_RandSeed)
#include "util/math_random.h"   // util::RandomModulo (VIBE_Math_RandomModulo)
#include "util/math_rng_float.h"// util::RandomFloatScaled (VIBE_Math_RandomFloatScaled)
#include "render/particle.h"    // render::TruncToward (VIBE_Coord_ConvertX)

#include <cmath>
#include <cstdlib>

namespace guild::play {

// ---------------------------------------------------------------------------
// gilde.exe 0x4b1e94 — VIBE_Sky_InitScene regen arm (0x4b2146..0x4b2293).
// Draw order is byte-faithful: mode roll (winter only), then per hour the
// gate roll + intensity roll + banding + thunder roll, then the wind walk.
// ---------------------------------------------------------------------------
void BuildWeatherDay(WeatherDayState& w, int season, i32 rainProb) {
    // 0x4b2146: season 3 -> winter mode split, else mode 0.
    if (season == 3) {
        int roll = (u16)util::RandomModulo(0x64);          // 0x4b215b
        // 0x4b216e: roll >= rainProb/2 (signed div) -> 1 else 2.
        w.mode = (roll >= rainProb / 2) ? 1 : 2;
    } else {
        w.mode = 0;                                        // 0x4b229a
    }

    for (int i = 0; i != 24; ++i)                          // 0x4b217e zero loop
        w.arc[i] = 0;

    for (int h = 0; h != 24; ++h) {                        // 0x4b219e loop
        if ((u16)util::RandomModulo(0x64) < rainProb)      // 0x4b21b5
            // 16-bit imul: modulo argument is (u16)(10 * rainProb).  0x4b21ce
            w.arc[h] = (u16)util::RandomModulo((u16)(10 * rainProb));
        // Banding PRESERVES PARITY ((v & 1), ecx == 1 in the original) —
        // Weather_UpdateSky later splits rain/snow on arc[h] & 1.
        i32 v = w.arc[h];
        if (v > 700)                                       // 0x4b21da
            w.arc[h] = (v & 1) + 999;                      // 0x4b21e7
        else if (v > 500)                                  // 0x4b22a5
            w.arc[h] = (v & 1) + 499;                      // 0x4b22ae
        else if (v > 250)                                  // 0x4b22b8
            w.arc[h] = (v & 1) + 149;                      // 0x4b22c1
        else
            w.arc[h] = 0;                                  // 0x4b22cb
        // 0x4b21ef..0x4b2216: thunder only in non-winter heavy hours (>= 999).
        if (w.mode == 0 && w.arc[h] >= 999)
            w.thunder[h] = (u16)util::RandomModulo(3);
        else
            w.thunder[h] = 0;                              // 0x4b22d6
    }

    // 0x4b2228..0x4b2282: wind angle walk. The angle is kept as a FLOAT
    // (fstp dword var_20) between iterations, faithful to the original.
    float angle = (float)(util::RandomFloatScaled() * kWindFullCircle);
    float drift = (crt::RandNext() <= 0x3FFF) ? kWindDriftStep   // 0x4b22e1
                                              : -kWindDriftStep; // 0x4b2247
    for (int h = 0; h != 24; ++h) {
        w.windX[h] = (float)std::sin((double)angle);       // fsin  -> 11BC100
        w.windY[h] = (float)std::cos((double)angle);       // fcos  -> 11BC160
        angle = (float)(util::RandomFloatScaled() * (double)drift + (double)angle);
    }
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5efca8 — VIBE_Sky_SetLayerFade.
// ---------------------------------------------------------------------------
void SkyLayerSetFade(SkyLayer& l, u8 target, float durationMs) {
    // (LODWORD(durationMs) & 0x7FFFFFFF) != 0  — duration is not +-0.0.
    if (durationMs != 0.0f) {
        l.fadeProgress  = 0.0f;          // +12 = 0
        l.fadeFrom      = l.fade;        // +36 = current (+38)
        l.fadeTarget    = target;        // +37
        l.fadeStepPerMs = 1.0f / durationMs; // +16
    } else {
        l.fadeProgress  = 1.0f;          // +12 = 1.0
        l.fadeStepPerMs = 0.0f;          // +16 = 0
        l.fadeFrom      = l.fade;        // +36 = current
        l.fadeTarget    = l.fade;        // +37 = current
    }
}

// gilde.exe 0x5efc78 — VIBE_Sky_SetLayerScrollSpeed: -speed * 1e-6 (flt_62C164).
void SkyLayerSetScrollSpeed(SkyLayer& l, float speed) {
    if (speed != 0.0f)                   // (bits & 0x7FFFFFFF) != 0
        l.scrollSpeed = -speed * kScrollToPerMs;
    else
        l.scrollSpeed = 0.0f;
}

// gilde.exe 0x5ef7cc (the per-layer loop body @0x5ef843..0x5ef8c8).
void SkyLayerStep(SkyLayer& l, float dtMs) {
    // progress += step * dt; clamp at 1 (and stop).
    float p = l.fadeStepPerMs * dtMs + l.fadeProgress;     // 0x5ef854
    l.fadeProgress = p;
    if (p > 1.0f) {                                        // 0x5ef861
        l.fadeProgress  = 1.0f;
        l.fadeStepPerMs = 0.0f;
    }
    // current = trunc(from + (target-from)*progress), clamped to 0xFF.
    double v = (double)l.fadeFrom
             + ((double)l.fadeTarget - (double)l.fadeFrom) * (double)l.fadeProgress;
    int iv = render::TruncToward(v);                       // VIBE_Coord_ConvertX
    l.fade = (iv <= 255) ? (u8)iv : (u8)0xFF;              // 0x5ef8a5
    // scrollPos = fmod(speed*dt + pos, 1.0)               // 0x5ef8c3
    l.scrollPos = (float)std::fmod((double)(l.scrollSpeed * dtMs + l.scrollPos), 1.0);
}

// ---------------------------------------------------------------------------
// SessionAtmos
// ---------------------------------------------------------------------------

void SessionAtmos::Frame(const sim::GameTime& clock, std::uint32_t seed) {
    Frame(clock, seed, lastMs_);
}

void SessionAtmos::Frame(const sim::GameTime& clock, std::uint32_t seed,
                         std::uint32_t nowMs) {
    // Season -> keyframe table. The original rebuilds via VIBE_DayCycle_
    // BuildTimeTable @0x4b2438 from InitOrLoadSession 0x533edd and the day-turn
    // ProcessTurnActions 0x52fa20.
    int season = clock.day % 4;     // VIBE_GameTime_GetSeasonFromDay 0x58339c
    if (!inited_ || season != lastSeason_) {
        render::BuildTimeTable(season, dayKeyframes);
        lastSeason_ = season;
    }

    // New day -> the ExSysMessage case-0 path: srand(packet seed dword) then
    // VIBE_Sky_InitScene (which ends with one UpdateBrightness call).
    if (!inited_ || clock.day != lastDay_) {
        initDay(clock, seed);
        brightnessStep(clock, nowMs);   // Sky_InitScene tail @0x4b2138
    }

    // The per-frame pair of VIBE_GameLogic_RunFrameLoop @0x4c09a0:
    weatherStep(clock);                 // VIBE_Weather_UpdateSky      0x4c0040
    brightnessStep(clock, nowMs);       // VIBE_DayCycle_UpdateBrightness 0x4b2504

    // Render-side fade/scroll stepping (VIBE_Render_UpdateSkyFlares 0x5ef7cc,
    // dt = nowMs - sky->lastMs). Runs after the game-logic frame.
    if (msSeen_) {
        float dt = (float)(std::uint32_t)(nowMs - lastMs_);
        SkyLayerStep(front, dt);
        SkyLayerStep(mid, dt);
        SkyLayerStep(dark, dt);
    }
    lastMs_ = nowMs;
    msSeen_ = true;
}

void SessionAtmos::initDay(const sim::GameTime& clock, std::uint32_t seed) {
    // VIBE_Util_RandSeed @0x5cb8e0 (called @0x498b14 before Sky_InitScene).
    crt::Srand(seed);
    BuildWeatherDay(day, clock.day % 4, climateRainProb[(clock.day % 4) & 3]);

    // Sky_InitScene non-regen tail @0x4b1ef4..:
    flashStartMs = 0;                   // dword_11BC0F8 = 0
    flashDurMs   = 0;                   // dword_11BC0FC = 0
    peakIntensity = 0;                  // the max(arc) loop @0x4b1f11
    for (int h = 0; h != 24; ++h)
        if (day.arc[h] > peakIntensity) peakIntensity = day.arc[h];
    // mode -> particle-system presence (0x4b1f2f..): mode 1 destroys rain and
    // creates snow; mode 2 keeps/creates both; mode 0 destroys snow, creates rain.
    snowPresent = (day.mode != 0);
    rainPresent = (day.mode != 1);
    // Layer reset (0x4b1faa removes all, then creates front/mid/dark):
    front = SkyLayer{};                 // "Sky_Schoen_01", a3=255 -> from/target 255
    front.fadeFrom = front.fadeTarget = 255; // CreateLayer bytes +36/+37 = a3
    front.pool = 0; front.variant = 0;
    mid = SkyLayer{};                   // "Sky_Schoen_02", a3=0
    mid.pool = 0; mid.variant = 1;
    dark = SkyLayer{};                  // "sky_dunkel_01" (overcast layer)
    // UpdateBrightness latches reset @0x4b212b/0x4b2132:
    lastBrightness_ = -100;             // dword_11BC1E8 = -100
    sunPhase_ = 0;                      // byte_631D9C = 0
    sunPhase = 0;
    sunRayHour_ = -1;                   // dword_11BC1C4-adjacent latch reset
    lastDay_ = clock.day;
    inited_ = true;
}

// gilde.exe 0x4c0040 — VIBE_Weather_UpdateSky (state core; the Rain_/Snow_
// grow-list and Sky_LayerLoadTexture leaves live with the particle/texture
// subsystems — the counts/identities they receive are computed here 1:1).
void SessionAtmos::weatherStep(const sim::GameTime& clock) {
    int h = (u16)clock.hour;            // WORD2(qword_13CE852)

    // Hourly grow counts (0x4c005e..0x4c00af):
    hourlySnowGrow = snowPresent ? day.arc[h] : 0;
    if (rainPresent) {
        if (snowPresent)
            hourlyRainGrow = (day.arc[h] & 1) != 0 ? day.arc[h] / 5 : 0;
        else
            hourlyRainGrow = day.arc[h];
    } else {
        hourlyRainGrow = 0;
    }

    windX = day.windX[h];               // dword_11BC100[h]
    windY = day.windY[h];               // dword_11BC160[h]
    weatherIntensity = render::WeatherIntensity(day.arc, h); // peak-of-3 core
    windSnowGrow = snowPresent ? render::SnowGrowAmount(windX, weatherIntensity) : 0;
    windRainGrow = rainPresent ? render::RainGrowAmount(windX, weatherIntensity) : 0;

    // Sky block (dword_64A7C8 — the dome always exists in a session):
    cloudScroll = render::CloudScrollMagnitude(windX, windY, weatherIntensity);
    weatherCategory = render::CategoryFor(weatherIntensity);

    // MID layer texture swap state machine (0x4c021f / 0x4c02c9):
    if (mid.fade == 0) {
        int cur = (mid.pool == (int)weatherCategory) ? mid.variant : -1;
        mid.variant = render::SelectCloudLayerIndex(weatherCategory, cur);
        mid.pool = (int)weatherCategory;
        SkyLayerSetFade(mid, 255, 1500.0f);     // 0x4c02bb (1500.0 = 0x44BB8000)
    }
    if (mid.fade == 0xFF) {
        int cur = (front.pool == (int)weatherCategory) ? front.variant : -1;
        front.variant = render::SelectCloudLayerIndex(weatherCategory, cur);
        front.pool = (int)weatherCategory;
        SkyLayerSetFade(mid, 0, 1500.0f);       // 0x4c035d
    }

    // Scroll speeds (0x4c0374..0x4c03d0): front = mag*0.75, mid = mag,
    // dark = mag*1.5 (dbl_61E4C8 / dbl_61E4D0).
    cloudScrollFront = (float)((double)cloudScroll * render::kWxScrollFast);
    cloudScrollBack  = (float)((double)cloudScroll * render::kWxScrollBack);
    SkyLayerSetScrollSpeed(front, cloudScrollFront);
    SkyLayerSetScrollSpeed(mid, cloudScroll);
    SkyLayerSetScrollSpeed(dark, cloudScrollBack);

    // DARK (overcast) layer fade target (0x4c0398/0x4c057d):
    if (weatherIntensity)
        SkyLayerSetFade(dark, (u8)((weatherIntensity << 6) / 1000 + 96), 1000.0f);
    else
        SkyLayerSetFade(dark, 0, 2500.0f);      // 0x4c03ab (2500.0 = 0x451C4000)
}

// gilde.exe 0x4b2504 — VIBE_DayCycle_UpdateBrightness, 1:1 state machine.
void SessionAtmos::brightnessStep(const sim::GameTime& clock, std::uint32_t nowMs) {
    int v10 = render::UpdateBrightness(dayKeyframes, clock.hour, clock.minute);
    brightness = v10;

    // v8 = darkLayer(+38) * (1/255)  (dword_11BC1D8 gate @0x4b2583).
    overcast = (float)((double)dark.fade * (double)kOvercastScale);

    bool skyOn = true; // dword_13ECF74[246*dword_649D60] — session scene has a sky
    // Lightning window: flashStart + flashDur > nowMs (unsigned, dword_62EB38).
    if (skyOn && (std::uint32_t)(flashStartMs + flashDurMs) > nowMs) {
        fogBlend(0, 2, 1.0f);           // 0x4b2859 — the white flash rows 0->2
        flashRestore_ = 1;              // dword_631DD8 = 1
        flashActive = true;
        lightingRebuilt = false;
        sunEvent = false;
        return;
    }
    flashActive = false;

    if (flashRestore_) {                // 0x4b25e3 — restore after the flash
        fogBlend(0, 1, overcast);
        flashRestore_ = 0;
    }

    bool sameSlot = true; // dword_11BC1F0[0] == dword_649D60 (single universe slot)
    // Skip hysteresis (0x4b2620): settled transition + tiny delta, or the
    // global relight-disable flag dword_62D4E8.
    if ((std::fabs((double)transition_ - 1.0) < kBrightEpsSkip
         && std::abs(lastBrightness_ - v10) < 10 && sameSlot)
        || relightDisabled) {
        fogBlend(0, 1, overcast);
        lightingRebuilt = false;
        sunEvent = false;
        return;
    }
    // Soft window (0x4b28c1): force = relightDisabled (0) instead of 1.
    u8 v9 = (std::fabs((double)transition_ - 1.0) <= kBrightEpsSoft
             && std::abs(lastBrightness_ - v10) < 100 && sameSlot)
                ? (u8)relightDisabled
                : (u8)1;

    lastBrightness_ = v10;              // dword_11BC1E8
    transition_ = 1.0f;                 // flt_11BC1EC

    double v3 = (double)v10 * render::kBandScale; // * dbl_61DD68 (0.01)
    int v11 = render::TruncToward(v3);  // VIBE_Coord_ConvertX
    int v5 = v11 % 7;

    sunEvent = false;
    if (v5 != lastBand_ && skyOn) {     // 0x4b26bb band changed
        // Sun-height walk (0x4b26d3..0x4b28f8): scene walk type 6 ->
        // VIBE_Light_SetSunHeight @0x4b24b0 per light node.
        if (v5 < 0 || sunPhase_ != 0) {
            if (!(v11 % 7 < 3 || sunPhase_ != 1)) {
                // sunset: h = -0.3 - rand01*0.6  (dbl_61DD40/61DD38)
                for (int i = 0; i < sunLightNodes; ++i)
                    sunHeight = (float)(kSunNightBase
                                        - util::RandomFloatScaled() * kSunSpan);
                sunEvent = true;
                ++sunPhase_;            // ++byte_631D9C
            }
        } else {
            // sunrise: h = rand01*0.6 + 0.3  (dbl_61DD48)
            for (int i = 0; i < sunLightNodes; ++i)
                sunHeight = (float)(util::RandomFloatScaled() * kSunSpan
                                    + kSunDayBase);
            sunEvent = true;
            ++sunPhase_;
        }
        lastBand_ = v5;                 // dword_631D98
    }
    sunPhase = sunPhase_;

    // 0x4b270d..0x4b2751: latch band/blend and rebuild the lighting tables.
    band  = v11 % 7;                          // dword_631DD0
    blend = (float)(v3 - (double)v11);        // flt_631DD4
    // VIBE_SkyColor_BlendBandLighting(band, blend, 1.0f, v9):
    if (hasSkyBands)
        ambient = render::BlendBandLighting(skyBands, band, blend, 1.0f);
    if (hasBandFog)
        blendFogRows(band, blend);            // the 6-row current-table lerp
    refreshForce = v9;                        // VIBE_Light_RefreshAllObjects(v9)
    lightingRebuilt = true;
    ++lightRebuilds;                          // ++dword_64A064 (RefreshAllObjects)
    // VIBE_SkyColor_ApplyAmbientBlend(0, 1, overcast)  @0x4b276f
    fogBlend(0, 1, overcast);
}

// The 6-entry current fog table of BlendBandLighting (0x5b87c1 loop): each row
// blends band a -> (a+1)%7 with weights (1-t)/t; colour bytes trunc toward zero
// (render::LerpChannelTrunc), near/far as floats.
void SessionAtmos::blendFogRows(int bandA, float t) {
    int bandB = (bandA + 1) % 7;
    float v21 = 1.0f - t;               // v47
    for (int i = 0; i < 6; ++i) {
        const render::FogBand& A = bandFog[bandA][i];
        const render::FogBand& B = bandFog[bandB][i];
        render::FogBand& C = fogCurrent[i];
        C.colorR = (u8)render::LerpChannelTrunc(A.colorR, B.colorR, t); // BYTE2
        C.colorG = (u8)render::LerpChannelTrunc(A.colorG, B.colorG, t); // BYTE1
        C.colorB = (u8)render::LerpChannelTrunc(A.colorB, B.colorB, t); // LOBYTE
        C.nearVal = A.nearVal * v21 + B.nearVal * t; // flt_13FD174
        C.farVal  = A.farVal  * v21 + B.farVal  * t; // flt_13FD178
    }
}

// VIBE_SkyColor_ApplyAmbientBlend @0x5b8b04 (render::ApplyAmbientBlend) over
// the blended current rows; distance scale flt_64A018 = 1.0.
void SessionAtmos::fogBlend(int rowA, int rowB, float t) {
    fogRowA = rowA;                     // dword_649F08 = rowA
    fogRowB = rowB;
    fogBlendT = t;
    fogApplied = hasBandFog
                     ? render::ApplyAmbientBlend(fog, fogCurrent, rowA, rowB, t,
                                                 kFogDistScale,
                                                 /*fogEnabledGlobal=*/true,
                                                 /*featureBit=*/true)
                     : false;
}

// gilde.exe 0x4c05ac — VIBE_Weather_RenderAndThunder decision core. The
// Rain_/Snow_Render leaves and the thunder voice sample (volume = flt_634490 *
// 127.0) are out-of-unit leaves; the rolls/latches here are 1:1.
bool SessionAtmos::ThunderTick(const sim::GameTime& clock, std::uint32_t nowMs) {
    int h = (u16)clock.hour;
    flashFired = false;
    sunRaysFired = false;

    if (day.thunder[h]) {               // 0x4c05d9
        // RandomModulo(300) == 0, OR thunder==3 && RandomModulo(100) == 0
        // (thunder is 0..2, so the second arm is dead — kept 1:1).
        bool hit = (u16)util::RandomModulo(0x12C) == 0;
        if (!hit && day.thunder[h] == 3)
            hit = (u16)util::RandomModulo(0x64) == 0;
        if (hit) {
            flashStartMs = nowMs;                         // dword_11BC0F8
            flashDurMs = (u16)util::RandomModulo(8) + 8;  // dword_11BC0FC
            flashFired = true;
        }
    }

    // Sun-ray tail (universe slot 0 only — modeled as the session slot):
    // hour 11..17, rain in the PREVIOUS hour (the original reads
    // dword_11BC034[h] — the dword right before the arc == arc[h-1]),
    // none this hour, minute > 30.
    if (h >= 0xB && h <= 0x11 && day.arc[h - 1] != 0 && day.arc[h] == 0
        && clock.minute > 30) {
        // RandomModulo(128) > 0x60 && !dword_62D564 -> CreateSunRays @0x42dc7c.
        if ((u16)util::RandomModulo(0x80) > 0x60 && !sunRayOption)
            sunRaysFired = true;
        sunRayHour_ = h;                // dword_631E90 = hour (latched regardless)
    }
    if (sunRayHour_ != -1 && h > sunRayHour_ && clock.minute > 30) {
        // dword_62D564 set -> VIBE_Light_SetSunDirection(1) @0x42dc40 (leaf).
        sunRayHour_ = -1;
    }
    return flashFired;
}

} // namespace guild::play
