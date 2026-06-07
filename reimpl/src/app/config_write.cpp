// gilde.exe 0x56af54 — VIBE_Config_WriteGfxSettings (guild::app).
// See config_write.h for the design rationale. 1:1 translation of the settings
// serializer the spine runs as shutdown teardown step 3.
//
// REUSED (extern, not redefined — ODR):
//   crt::StringUIntToString (string.h, 0x609640) — the ultoa core that the
//     original's VIBE_Util_IntToStringRadix (0x5d92a0) is byte-identical to;
//     used here under AnimationState_Update's signed wrapper.
#include "app/config_write.h"

#include "crt/string.h"

namespace guild::app {

// gilde.exe 0x5d92ec — VIBE_AnimationState_Update.
//   if (radix == 10 && value < 0) { value = -value; *out++ = '-'; }
//   VIBE_Util_IntToStringRadix((unsigned)value, out, radix);  // == ultoa
char* AnimationState_Update(int value, char* out, unsigned radix) {
    char* p = out;
    if (radix == 10 && value < 0) {
        value = -value;          // formats |value| (the original negates in place)
        *p++ = '-';
    }
    // VIBE_Util_IntToStringRadix(value, p, radix) — the unsigned radix core.
    crt::StringUIntToString(static_cast<u32>(value), p, radix);
    return out;
}

namespace {

// Helper: format `value` (radix 10, signed) into a scratch buffer and emit it.
void Emit(const ProfileWriteSink& sink, const char* section, const char* key,
          int value) {
    char buf[32]; // the original's CHAR String[32]
    AnimationState_Update(value, buf, 10u);
    sink(section, key, buf);
}

} // namespace

// gilde.exe 0x56af54 — VIBE_Config_WriteGfxSettings.
// The exact key order, sections and value formatting of the original. The
// brightness/contrast/gamma floats are written as (int)(value * flt_6251F0),
// flt_6251F0 == 100.0f (the inverse of the read's flt_625200 == 0.01f scale),
// preserving the original's truncate-after-scale (and its float rounding).
void ConfigWriteGfxSettings(const config::GfxSettings& gfx,
                            const config::SoundSettings& snd,
                            const config::GameSettings& game,
                            const ProfileWriteSink& sink) {
    const float kScale = 100.0f; // flt_6251F0

    // ---- [Gfx] (byte fields) ---------------------------------------------
    Emit(sink, "Gfx", "texture_scale",    gfx.textureScale);   // byte_1233515
    Emit(sink, "Gfx", "details",          gfx.details);        // byte_1233514
    Emit(sink, "Gfx", "lod_handling",     gfx.lodHandling);    // byte_1233516
    Emit(sink, "Gfx", "shadow_detail",    gfx.shadowDetail);   // byte_1233517
    Emit(sink, "Gfx", "floor_mipmapping", gfx.floorMipmapping);// byte_1233518
    Emit(sink, "Gfx", "gfx_set",          gfx.gfxSet);         // byte_1233510
    Emit(sink, "Gfx", "camera_limits",    gfx.cameraLimits);   // byte_1233519
    Emit(sink, "Gfx", "floor_lod",        gfx.floorLod);       // byte_123351A
    Emit(sink, "Gfx", "character_detail", gfx.characterDetail);// byte_123351B
    Emit(sink, "Gfx", "fog_plane",        gfx.fogPlane);       // byte_123351C
    Emit(sink, "Gfx", "cur_res",          gfx.curRes);         // byte_63D724

    // ---- [Gfx] (float fields: *100.0 truncate) ---------------------------
    Emit(sink, "Gfx", "brightness_r", static_cast<int>(gfx.brightness[0] * kScale));
    Emit(sink, "Gfx", "brightness_g", static_cast<int>(gfx.brightness[1] * kScale));
    Emit(sink, "Gfx", "brightness_b", static_cast<int>(gfx.brightness[2] * kScale));
    Emit(sink, "Gfx", "brightness_a", static_cast<int>(gfx.brightness[3] * kScale));
    Emit(sink, "Gfx", "contrast_r",   static_cast<int>(gfx.contrast[0] * kScale));
    Emit(sink, "Gfx", "contrast_g",   static_cast<int>(gfx.contrast[1] * kScale));
    Emit(sink, "Gfx", "contrast_b",   static_cast<int>(gfx.contrast[2] * kScale));
    Emit(sink, "Gfx", "contrast_a",   static_cast<int>(gfx.contrast[3] * kScale));
    Emit(sink, "Gfx", "gamma_r",      static_cast<int>(gfx.gamma[0] * kScale));
    Emit(sink, "Gfx", "gamma_g",      static_cast<int>(gfx.gamma[1] * kScale));
    Emit(sink, "Gfx", "gamma_b",      static_cast<int>(gfx.gamma[2] * kScale));
    Emit(sink, "Gfx", "gamma_a",      static_cast<int>(gfx.gamma[3] * kScale));

    // ---- [Sound] ----------------------------------------------------------
    Emit(sink, "Sound", "master_vol", snd.masterVol);  // byte_1233550
    Emit(sink, "Sound", "sfx_vol",    snd.sfxVol);      // byte_1233551
    Emit(sink, "Sound", "msx_vol",    snd.msxVol);      // byte_1233552
    Emit(sink, "Sound", "speech_vol", snd.speechVol);   // byte_1233553
    Emit(sink, "Sound", "msx_freq",   snd.msxFreq);     // byte_1233554

    // ---- [Game] -----------------------------------------------------------
    Emit(sink, "Game", "speed",        game.speed);        // dword_1233558
    Emit(sink, "Game", "mouse_speed",  game.mouseSpeed);   // dword_1233560
    Emit(sink, "Game", "scroll_speed", game.scrollSpeed);  // dword_1233564
    Emit(sink, "Game", "camera_speed", game.cameraSpeed);  // byte_123355C
    Emit(sink, "Game", "invert_mouse", game.invertMouse);  // byte_1233568
    Emit(sink, "Game", "nachtwaechter",game.nachtwaechter);// byte_1233569
    // stadt is written verbatim as text (byte_123356C), no itoa.
    sink("Game", "stadt", game.stadt);
    Emit(sink, "Game", "historie",     game.historie);     // dword_12335AC
    Emit(sink, "Game", "mission",      game.mission);      // dword_12335B0
    Emit(sink, "Game", "net_mission",  game.netMission);   // dword_12335B4
    Emit(sink, "Game", "show_cursor_txt", game.showCursorTxt); // byte_123356A
    Emit(sink, "Game", "show_geb_info",   game.showGebInfo);   // byte_123356B
    Emit(sink, "Game", "panel_mode",   game.panelMode);    // byte_12335B8
    Emit(sink, "Game", "help_events",  game.helpEvents);   // byte_12335B9
    Emit(sink, "Game", "difficulty",   game.difficulty);   // byte_12335BA
    Emit(sink, "Game", "hints",        game.hints);        // byte_12335BB
    Emit(sink, "Game", "panel_help",   game.panelHelp);    // byte_12335BC
}

} // namespace guild::app
