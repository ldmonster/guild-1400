#pragma once
// =============================================================================
// guild::play — SESSION ATMOSPHERE: day/night brightness + weather state for
// the live city session, reconstructed 1:1 from the gilde.exe frame loop.
//
// CALL-TREE POSITION (rule 7)
// ---------------------------------------------------------------------------
//   VIBE_GameLogic_RunFrameLoop        0x4c09a0   per-frame:
//     ├─ VIBE_Weather_UpdateSky        0x4c0040   weather state + cloud layers
//     ├─ VIBE_Weather_RenderAndThunder 0x4c05ac   rain/snow draw + THUNDER FLASH
//     └─ VIBE_DayCycle_UpdateBrightness 0x4b2504  brightness -> lighting rebuild
//   VIBE_Command_ExSysMessage case 0   0x498ab4   new-day message:
//     ├─ VIBE_Util_RandSeed            0x5cb8e0   srand(packet seed dword)
//     └─ VIBE_Sky_InitScene            0x4b1e94   regenerate the weather day
//
// HOW THE ORIGINAL APPLIES BRIGHTNESS TO THE RENDERED CITY (recovered)
// ---------------------------------------------------------------------------
// VIBE_DayCycle_UpdateBrightness @0x4b2504 maps the wall clock to the 0..600
// brightness step (render::UpdateBrightness), then band = trunc(b*0.01)
// (dbl_61DD68), blend = frac, band mod 7, and APPLIES it through:
//   (1) VIBE_SkyColor_BlendBandLighting @0x5b85e4 (band, blend, scale=1.0f,
//       force):
//        - sky-dome vertex colours: scene walk type 30 -> SkyColor_SetTimeOfDay
//          0x5b83b0 -> InterpolateBand 0x5b80ac (dome gradient verts);
//        - GLOBAL AMBIENT triple flt_64A074/78/7C = lerp(bandRow,(band+1)%7,t)
//          and luma flt_64A070 (render::BlendBandLighting, sky.cpp) — the base
//          colour VIBE_Light_BuildObjectCache 0x5c8218 starts the per-vertex
//          light accumulation from (light.h LightVertex floats [12..14]);
//        - the CURRENT 6-row fog table dword_13FD170/flt_13FD174/flt_13FD178 =
//          per-channel trunc-lerp of the per-band rows (render::LerpChannelTrunc);
//        - VIBE_Light_RefreshAllObjects @0x5c886c (force): scene-graph walk
//          invalidating every object's vertex-light cache (RemoveCacheEntry
//          0x5c7da4; force>1 -> eager BuildObjectCache) + Floor_BuildTilePolys
//          0x5bc45c (terrain relight). The relit caches feed the 8-bit shade
//          index (+66/+68..70) the 16bpp rasterizer consumes via the 768-byte
//          shade ramps — THIS is where brightness reaches the pixels;
//        - sun-flare sprite RGB from the per-band table dword_1408770.
//   (2) VIBE_SkyColor_ApplyAmbientBlend @0x5b8b04 (row 0 -> row 1, t=overcast):
//       lerps the current fog rows and calls VIBE_Render_ConfigureFog @0x5ae384
//       (near*flt_64A018, far*flt_64A018, packed RGB) — distance-fog colour/range.
//       t = darkCloudLayer.fadeByte(+38) * (1/255) (flt_61DD50) — the OVERCAST
//       factor the weather drives.
//   (3) Sun height: at the first band change after init a scene walk (type 6)
//       runs VIBE_Light_SetSunHeight @0x4b24b0 per light: day h=0.3+rand*0.6,
//       at band>=3 (sunset) h=-0.3-rand*0.6 (dbl_61DD48/40/38), written to the
//       light object +420.
//   (4) Lightning: while flashStart+flashDur > nowMs (dword_11BC0F8/FC vs the
//       ms timer dword_62EB38) the fog blend is rows 0 -> 2 at t=1.0 (the white
//       flash), then restored (dword_631DD8 latch).
// There is NO palette/gamma write and NO 16bpp framebuffer modulation — the
// brightness reaches the frame exclusively through the vertex-light rebuild +
// fog + sky-dome colours. Hence NO Apply(fb16,...) here (see NAMED GAPS).
//
// WEATHER DAY (VIBE_Sky_InitScene @0x4b1e94, regen arm 0x4b2146..0x4b2293)
// ---------------------------------------------------------------------------
// Seeded by the new-day sys message (VIBE_Util_RandSeed(packet dword) right
// before the call — the `weatherSeedOrState` of Frame()). Per day:
//   mode: season==3 -> RandomModulo(100) >= rainProb/2 ? 1 (snow, rain
//         destroyed) : 2 (snow + rain); else 0 (rain only). dword_11BC1C0.
//   arc[24] (dword_11BC038): per hour, if RandomModulo(100) < rainProb then
//         v = RandomModulo((u16)(10*rainProb)); banded PRESERVING PARITY:
//         v>700 -> (v&1)+999 ; v>500 -> (v&1)+499 ; v>250 -> (v&1)+149 ; else 0.
//   thunder[24] (dword_11BC098): mode==0 && arc[h]>=999 -> RandomModulo(3), else 0.
//   wind[24] (dword_11BC100/160): angle = RandomFloatScaled()*6.283185
//         (dbl_61DD30), drift = RandNext()<=0x3FFF ? +0.175f : -0.175f;
//         per hour windX=sin(angle), windY=cos(angle),
//         angle = (float)(RandomFloatScaled()*drift + angle).
//
// PER-FRAME WEATHER (VIBE_Weather_UpdateSky @0x4c0040)
// ---------------------------------------------------------------------------
//   hourly grows: snow += arc[h]; rain += snow? (arc[h]&1 ? arc[h]/5 : 0) : arc[h]
//   intensity = peak-of-3 (render::WeatherIntensity), wind-scaled grows
//   (render::Snow/RainGrowAmount), scrollMag (render::CloudScrollMagnitude),
//   cloud texture swap state machine on the MID layer fade byte (0 -> roll a new
//   mid texture, fade 255 over 1500ms; 0xFF -> roll the FRONT texture, fade mid
//   back to 0 over 1500ms; render::SelectCloudLayerIndex), per-layer scroll
//   speeds front=mag*0.75 mid=mag dark=mag*1.5 (dbl_61E4C8/61E4D0), and the
//   DARK (overcast) layer fade target: intensity ? (intensity<<6)/1000 + 96
//   over 1000ms : 0 over 2500ms (disasm @0x4c057d) — the overcast source.
//
// SKY LAYER FADE/SCROLL (VIBE_Sky_SetLayerFade @0x5efca8 + the per-frame step
// in VIBE_Render_UpdateSkyFlares @0x5ef7cc):
//   set: progress=0, step=1/durationMs, from=current, target  (duration==±0:
//        progress=1, step=0, from=target=current)
//   step(dtMs): progress += step*dt; if >1 -> 1, step=0;
//        current = trunc(from + (target-from)*progress) clamped 255;
//        scrollPos = fmod(scrollSpeed*dt + scrollPos, 1.0)
//   SetLayerScrollSpeed @0x5efc78: stored = -speed * 1e-6 (flt_62C164).
//
// THUNDER (VIBE_Weather_RenderAndThunder @0x4c05ac decision core)
// ---------------------------------------------------------------------------
//   thunder[h] != 0 && (RandomModulo(300)==0 || (thunder[h]==3 &&
//   RandomModulo(100)==0, dead: thunder is 0..2)) -> flashStart=nowMs,
//   flashDur=RandomModulo(8)+8 (ms). Sun-ray tail: hour 11..17, arc[h-1]!=0
//   (the original reads dword_11BC034[h] — the dword BEFORE the arc),
//   arc[h]==0, minute>30 -> RandomModulo(128)>0x60 && !dword_62D564 ->
//   VIBE_Light_CreateSunRays (render leaf).
//
// NAMED GAPS (rule 8 — exposed as parameters, NOT faked):
//   * The pixel application point is the renderer: the scene-graph walks of
//     BlendBandLighting (dome vertex colours via InterpolateBand 0x5b80ac over
//     the runtime dome mesh; the light-cache walk over the live scene) and the
//     ConfigureFog render state belong to the session renderer
//     (play/real_city_render, owned elsewhere). SessionAtmos computes every
//     input 1:1 (ambient triple, fog rows/colour, shade-rebuild trigger).
//   * The per-band gradient rows (flt_13FD1B8, 96 B/band), per-band fog rows
//     and the sun-flare colour table dword_1408770 are RUNTIME tables populated
//     at scene load (SkyColor_StoreBandColors 0x5b83f0 from the .ed3 sky rig) —
//     caller-supplied here (skyBands/bandFog), zero outputs when absent.
//   * VIBE_Light_CreateSunRays @0x42dc7c / SetSunDirection @0x42dc40 visuals and
//     the thunder voice sample (Audio_StartVoiceSample, volume flt_634490*127.0)
//     are leaves outside this unit; the DECISIONS (rolls, latches) are 1:1.
//   * The cloud-layer texture records ("Sky_Schoen_01".. 76-byte entries, dims
//     copied to the layer on swap) need the texture system; the layer identity
//     is tracked as (pool, variant) — the name table is 1:1 with that pair.
// =============================================================================
#include "guild/common/types.h"
#include "render/daycycle.h"  // BuildTimeTable, UpdateBrightness, BrightnessToBand
#include "render/weather.h"   // WeatherIntensity, CategoryFor, CloudScroll..., Select...
#include "render/sky.h"       // SkyBandColor, SkyAmbient, BlendBandLighting
#include "render/fog.h"       // FogState, FogBand, ApplyAmbientBlend
#include "sim/types.h"        // sim::GameTime

#include <cstdint>

namespace guild::play {

// ---------------------------------------------------------------------------
// Recovered constants (get_bytes; addresses in gilde.exe).
// ---------------------------------------------------------------------------
constexpr double kWindFullCircle = 6.2831853;    // dbl_61DD30 (0x401921FB53C8D4F1)
constexpr float  kWindDriftStep  = 0.175f;       // 0x3E333333 / 0xBE333333
constexpr float  kOvercastScale  = 0.00392156886f; // flt_61DD50 = 1/255 (0x3B808081)
constexpr double kSunSpan        = 0.6;          // dbl_61DD38
constexpr double kSunNightBase   = -0.3;         // dbl_61DD40
constexpr double kSunDayBase     = 0.3;          // dbl_61DD48
constexpr double kBrightEpsSkip  = 0.1;          // dbl_61DD58
constexpr double kBrightEpsSoft  = 0.2;          // dbl_61DD60
constexpr float  kScrollToPerMs  = 1.0e-6f;      // flt_62C164 (SetLayerScrollSpeed)
constexpr float  kFogDistScale   = 1.0f;         // flt_64A018

// ---------------------------------------------------------------------------
// Per-day weather state — VIBE_Sky_InitScene @0x4b1e94 regen arm. The arrays
// mirror dword_11BC038 / dword_11BC098 / dword_11BC100 / dword_11BC160 and
// dword_11BC1C0 (mode). Generation consumes the CRT LCG (crt::RandNext) in the
// original's exact draw order.
// ---------------------------------------------------------------------------
struct WeatherDayState {
    int   mode = 0;        // dword_11BC1C0: 0 rain scene, 1 snow only, 2 snow+rain
    i32   arc[24]{};       // dword_11BC038 hourly intensity (0/149/150/499/500/999/1000)
    i32   thunder[24]{};   // dword_11BC098 thunder code 0..2 (heavy hours only)
    float windX[24]{};     // dword_11BC100 sin(angle walk)
    float windY[24]{};     // dword_11BC160 cos(angle walk)
};

// gilde.exe 0x4b1e94 (0x4b2146..0x4b2293) — regenerate one weather day.
// `rainProb` is the per-city/season climate dword dword_13CD7A8[189*city+season]
// (runtime world data). Draws from the shared CRT LCG stream.
void BuildWeatherDay(WeatherDayState& w, int season, i32 rainProb);

// ---------------------------------------------------------------------------
// One sky cloud layer's fade + scroll state (the 0x2C-byte layer record of
// VIBE_Sky_CreateLayer @0x5efb78; offsets in comments).
// ---------------------------------------------------------------------------
struct SkyLayer {
    float scrollPos    = 0.0f;  // +4   fmod-wrapped scroll phase
    float scrollSpeed  = 0.0f;  // +8   per-ms phase speed (set via SetScrollSpeed)
    float fadeProgress = 1.0f;  // +12  CreateLayer inits 1.0
    float fadeStepPerMs= 0.0f;  // +16  1/durationMs
    u8    fadeFrom     = 0;     // +36
    u8    fadeTarget   = 0;     // +37
    u8    fade         = 0;     // +38  CURRENT fade byte (the overcast source)
    // texture identity (pool = weather category of the shown texture set,
    // variant = index the swap rolled; 1:1 with the 76-byte name-table entry)
    int   pool    = 0;
    int   variant = 0;
};

// gilde.exe 0x5efca8 — VIBE_Sky_SetLayerFade(target, durationMs).
void SkyLayerSetFade(SkyLayer& l, u8 target, float durationMs);
// gilde.exe 0x5efc78 — VIBE_Sky_SetLayerScrollSpeed: stored = -speed * 1e-6.
void SkyLayerSetScrollSpeed(SkyLayer& l, float speed);
// gilde.exe 0x5ef7cc (per-layer loop body) — advance fade + scroll by dtMs.
void SkyLayerStep(SkyLayer& l, float dtMs);

// ---------------------------------------------------------------------------
// SessionAtmos — drives the REAL render::UpdateBrightness + render::Weather*
// per the original per-frame step and exposes every recovered application
// parameter. See the header block for the recovered math + named gaps.
// ---------------------------------------------------------------------------
struct SessionAtmos {
    // ---- configuration (runtime world data in the original) ----
    i32  climateRainProb[4] = {30, 30, 30, 30}; // dword_13CD7A8[189*city + s]
    int  sunLightNodes   = 1;   // # light nodes the SetSunHeight walk visits
    i32  relightDisabled = 0;   // dword_62D4E8 (debug: skip lighting rebuild)
    i32  sunRayOption    = 0;   // dword_62D564 (options: sun rays off)
    // Optional runtime band tables (.ed3 sky rig; outputs zero when absent).
    render::SkyBandColor skyBands[render::kSkyBands]{};
    bool hasSkyBands = false;
    render::FogBand bandFog[render::kSkyBands][6]{}; // per-band 6 fog rows (13FD1D0..)
    bool hasBandFog = false;

    // Per-frame step, in the original frame-loop order: (new-day? srand(seed) +
    // Sky_InitScene regen) -> Weather_UpdateSky -> DayCycle_UpdateBrightness ->
    // layer fade/scroll step (the render-side UpdateSkyFlares walk). `nowMs` is
    // the engine millisecond timer (dword_62EB38).
    void Frame(const sim::GameTime& clock, std::uint32_t weatherSeedOrState,
               std::uint32_t nowMs);
    // Convenience overload: re-uses the last nowMs (fades/flash hold still).
    void Frame(const sim::GameTime& clock, std::uint32_t weatherSeedOrState);

    // gilde.exe 0x4c05ac decision core — the thunder-flash + sun-ray rolls of
    // VIBE_Weather_RenderAndThunder. Returns true when a flash was triggered
    // (flashStartMs/flashDurMs latched; UpdateBrightness then blends fog rows
    // 0 -> 2 at 1.0 while the window is open).
    bool ThunderTick(const sim::GameTime& clock, std::uint32_t nowMs);

    // ---- outputs: day cycle ----
    i32   dayKeyframes[6]{};        // BuildTimeTable(season) thresholds
    int   brightness = 0;           // 0..600 (render::UpdateBrightness)
    int   band  = 0;                // dword_631DD0 = trunc(b*0.01) % 7
    float blend = 0.0f;             // flt_631DD4  cross-band fraction
    bool  lightingRebuilt = false;  // BlendBandLighting + Light_RefreshAllObjects ran
    int   lightRebuilds = 0;        // total rebuilds (cf. dword_64A064 counter)
    u8    refreshForce = 0;         // the force arg passed to RefreshAllObjects
    int   sunPhase = 0;             // byte_631D9C: 0 pre-dawn, 1 risen, 2 set
    float sunHeight = 0.0f;         // last Light_SetSunHeight value (+420)
    bool  sunEvent = false;         // a sunrise/sunset walk fired this frame
    render::SkyAmbient ambient{};   // flt_64A070..7C (when hasSkyBands)

    // ---- outputs: weather ----
    WeatherDayState day{};
    int   weatherIntensity = 0;     // peak-of-3 of the arc at the clock hour
    render::WeatherCategory weatherCategory = render::kWeatherFair;
    int   peakIntensity = 0;        // max(arc) — Rain/Snow_Create sizing
    bool  snowPresent = false;      // mode != 0
    bool  rainPresent = true;       // mode != 1
    float windX = 0.0f, windY = 0.0f;
    int   hourlySnowGrow = 0;       // GrowFlakeList(1,1,...) count
    int   hourlyRainGrow = 0;       // GrowDropList(1,1,...) count
    int   windSnowGrow = 0;         // trunc(2.0 * -windX * intensity)
    int   windRainGrow = 0;         // trunc(0.5 * -windX * intensity)
    float cloudScroll = 0.0f;       // scrollMag (mid layer)
    float cloudScrollFront = 0.0f;  // scrollMag * 0.75
    float cloudScrollBack = 0.0f;   // scrollMag * 1.5  (dark layer)
    SkyLayer front{}, mid{}, dark{};// the 3 layers (dword_11BC1D0[0]/1D4/1D8)

    // ---- outputs: overcast / fog / flash ----
    float overcast = 0.0f;          // dark.fade * (1/255) — ApplyAmbientBlend t
    bool  flashActive = false;      // lightning window open this frame
    std::uint32_t flashStartMs = 0; // dword_11BC0F8
    std::uint32_t flashDurMs = 0;   // dword_11BC0FC
    bool  flashFired = false;       // ThunderTick triggered a flash
    bool  sunRaysFired = false;     // ThunderTick CreateSunRays decision fired
    render::FogBand fogCurrent[6]{};// 13FD170/174/178 blended rows (hasBandFog)
    render::FogState fog{};         // ConfigureFog state fed by ApplyAmbientBlend
    int   fogRowA = 0, fogRowB = 1; // dword_649F08 rows of the last fog blend
    float fogBlendT = 0.0f;         // its t (overcast, or 1.0 during a flash)
    bool  fogApplied = false;       // ApplyAmbientBlend ran (hasBandFog)

  private:
    void initDay(const sim::GameTime& clock, std::uint32_t seed);
    void weatherStep(const sim::GameTime& clock);
    void brightnessStep(const sim::GameTime& clock, std::uint32_t nowMs);
    void fogBlend(int rowA, int rowB, float t);
    void blendFogRows(int bandA, float t);

    bool  inited_ = false;
    int   lastDay_ = 0;
    int   lastSeason_ = -1;
    std::uint32_t lastMs_ = 0;
    bool  msSeen_ = false;
    // UpdateBrightness latches (original globals)
    int   lastBrightness_ = -100;   // dword_11BC1E8 (InitScene sets -100)
    float transition_ = 0.0f;       // flt_11BC1EC (BSS 0; set 1.0 on rebuild)
    int   lastBand_ = 0;            // dword_631D98
    int   sunPhase_ = 0;            // byte_631D9C
    i32   flashRestore_ = 0;        // dword_631DD8
    int   sunRayHour_ = -1;         // dword_631E90
};

} // namespace guild::play
