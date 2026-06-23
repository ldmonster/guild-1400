#pragma once
// guild::play — REAL audio for the native city session (music + ambience + 3D SFX).
//
// SessionAudio is the audio integrator for the native city session: it brings up
// the reconstructed audio cores over the real shim device (SDL on the host,
// NullAudioDevice headless) and real install assets, then drives the per-frame
// audio tick that the original game loop ran. It is the session counterpart of
// the menu-music path proven by play::RunNativeMainMenu (mp3 decode -> device
// voice), generalized to the full reconstructed audio stack:
//
//   Init   = the audio half of the session bring-up:
//     * audio::SoundSystem::init        — VIBE_Sound_LibInit @0x445d90
//                                         (48 voices / 2ch / 44100, engine consts)
//     * audio::ApplyVolumeSettings      — VIBE_Audio_ApplyVolumeSettings @0x56c148
//                                         (the [Sound] master/sfx/msx/speech/msx_freq
//                                         sliders -> master/music volume products,
//                                         exact flt_62522C / flt_625230 scales)
//     * SFX sample-bank preload         — VIBE_Sound_PreloadFromIncludeFile @0x52f154
//                                         (include_sfx.ini -> per-bank
//                                         VIBE_Sound_LoadSampleBank @0x446b2c, via
//                                         app::LoadOneSfxBank over the real sfx/*.sbf)
//     * market-ambience PCM             — the "Athmo_Marktplatz_Mono_4Bit" sample
//                                         (string @0x6263f0) decoded from the real
//                                         sfx/Lebewesen/athmos.sbf (IMA ADPCM .wav,
//                                         fmt 0x11 — the codec MSS decoded; see
//                                         DecodeImaAdpcm below)
//     * music director                  — audio::MusicDirector + the real OUTDOOR
//                                         track entry (id 9876) and an IMusicSink
//                                         that streams the real msx/cd1/*.mp3 tracks
//                                         through the device (the menu pattern)
//
//   Frame  = one tick of the audio block of VIBE_GameLogic_RunFrameLoop @0x4c09a0
//            (app::AudioTick: Sound3d_UpdateAll @0x424790, VoiceQueue_ProcessNext
//            @0x57eff0, Music_UpdateOutdoorTrackPlayback @0x581594,
//            Sound_UpdateVoices @0x445f00) plus the scene-entry market-ambience
//            trigger VIBE_Ambient_StartMarketLoop @0x582858 (the original
//            VIBE_Scene_RunMainFrameLoop @0x50f0c0 fires it once at scene entry,
//            before its frame loop, and StopMarketLoop @0x5828bc at scene exit —
//            Frame starts it on the first tick, Shutdown stops it).
//
// AUDIO-ENABLE GATES: the original reads the [Sound] msx / sfx / weather INI keys
// at startup (VIBE_GameLogic_MainEntryAndShutdown @0x534bbc:
// GetPrivateProfileIntA("Sound","msx") -> dword_63C8F8, "sfx" -> dword_63C900,
// "weather" -> dword_63C904). config::SoundSettings (the reconstructed
// ReadGfxAndSoundSettings schema) does not carry those three keys, so they are
// taken from SessionAudioInit (defaults 1/1/1, the shipped Gilde.INI values).
//
// NAMED GAPS (rule 8 — no analogues; absent pieces are absent and said so):
//   * .sbf entry PCM faulting (VIBE_Sound_LoadEntry @0x446830) is only performed
//     for the market-ambience bank; the include_sfx.ini preload indexes every
//     bank's entry NAMES through the real loader but leaves their PCM unfaulted
//     (placeholder payload), exactly like app::LoadOneSfxBank.
//   * Outdoor-track END detection: the original polls the MSS stream cursor
//     (*(handle+260/261)). shim::IAudioDevice has no per-voice status query; when
//     the injected device implements audio::IAudioStatusDevice the end is detected
//     for real (sampleStatus != 4) and the original ended->pause->reselect rotation
//     runs; otherwise the sink plays the selected season track looped on the
//     device so music stays audible, and the rotation cannot fire (SAY SO).
//   * Interior location tracks (the Athmos\Athmo_*.mp3 entries the original
//     registers for location ids; VIBE_Music_ResumeLocationTrack @0x580de4 full
//     table) are not seeded — only the OUTDOOR entry (id 9876) exists, so a
//     non-zero locationId yields PlaybackAction::kNone like an unknown id.
//   * The msx_freq-derived random-interrupt probability (flt_6422A8 read inside
//     VIBE_Music_UpdateOutdoorTrackPlayback) is not part of the reconstructed
//     music_world state machine yet; the product is computed (status.musicFreqScale)
//     but not consumed.
//   * The terrain-echo listener layer (VIBE_Sound3d_UpdateListener @0x425208) is
//     the documented stub inside app::AudioTick (heightmap/floor-octree coupled).
#include "guild/common/types.h"
#include "app/audio_tick.h"        // app::AudioTick / Start/StopMarketLoop / AudioEnable
#include "audio/sound.h"           // audio::SoundSystem
#include "audio/music_world.h"     // audio::MusicDirector / IMusicSink
#include "audio/voice.h"           // audio::IAudioStatusDevice (status probe)
#include "config/ini.h"            // config::SoundSettings
#include "shim/IAudioDevice.h"
#include "shim/IFileSystem.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace guild::play {

// The market-ambience sample the original plays (string @0x6263f0, used by
// VIBE_Ambient_StartMarketLoop @0x582858: VIBE_Sound_PlaySample(rec,
// "Athmo_Marktplatz_Mono_4Bit") -> Sound3d_AttachToEntity(v, _, 127, 2200.0)).
extern const char kMarketAmbienceSample[];
// The bank that ships it (include_sfx.ini: #include"Lebewesen\athmos.sbf").
extern const char kMarketAmbienceBank[];

// ---------------------------------------------------------------------------
// IMA ADPCM (WAVE fmt tag 0x11) block decoder — the codec of the shipped
// market-ambience .wav inside sfx/Lebewesen/athmos.sbf (mono, 4-bit, 22050 Hz,
// 512-byte blocks). The original decoded it inside Miles Sound System; MSS's
// "IMA DVI ADPCM" is the standard IMA spec (step/index tables below), so this
// decode is part of the approved MSS -> SDL substitution (rule 5), not an
// analogue. Layout per block and channel: {i16 predictor; u8 stepIndex; u8 rsv}
// then 4-bit nibbles (low nibble first), 2 samples per byte.
//   `payload`/`bytes`  the RIFF data-chunk bytes
//   `channels`         fmt numChannels (interleaved per-channel 4-byte words)
//   `blockAlign`       fmt blockAlign (bytes per block)
//   `sampleLimit`      total sample frames (the RIFF `fact` count; 0 = no cap)
// Returns interleaved S16 frames in `out` (frames * channels samples).
std::size_t DecodeImaAdpcm(const u8* payload, std::size_t bytes, int channels,
                           int blockAlign, std::size_t sampleLimit,
                           std::vector<i16>& out);

// Map an engine music-track name to its install-relative path: the original
// prefixes "msx\" (string @0x622b3e) to the track-table names ("cd1\X.mp3"),
// so "cd1\\ImFruehling.mp3" -> "msx/cd1/ImFruehling.mp3" (separators normalized;
// on-disk casing is resolved later, case-insensitively, like Win32 did).
std::string MusicTrackRelPath(const std::string& trackName);

// ---------------------------------------------------------------------------
// The streaming-music sink (VIBE_Audio_LoadTrack @0x439ed0 / StopTrack @0x43a2fc
// boundary) over the shim device: resolves the real msx/ mp3, decodes it to PCM
// (play::DecodeMp3File — the proven menu path) and drives one device voice.
// Start order mirrors VIBE_Audio_StartTrack @0x439f8c: volume 0 -> start ->
// volume (+0x118 default 127).
// ---------------------------------------------------------------------------
class SessionMusicSink : public audio::IMusicSink {
public:
    void bind(shim::IAudioDevice* dev, shim::IFileSystem* fs, std::string gameDir);

    // audio::IMusicSink
    int loadTrack(const std::string& name, int loop) override;
    void stopTrack(int handle, int fade) override;

    // True iff the device can report per-voice status (audio::IAudioStatusDevice)
    // and the current stream voice has stopped playing — the reconstruction of
    // the original's stream-cursor-at-end check (*(handle+260/261) == 0).
    bool streamEnded() const;

    void stopAll();

    // Inspection (tests / status).
    const std::string& currentName() const { return currentName_; }
    bool streaming() const { return streaming_; }   // real PCM decoded + playing
    shim::VoiceHandle voice() const { return voice_; }
    int loadCount() const { return loads_; }
    int stopCount() const { return stops_; }
    int lastLoops() const { return lastLoops_; }

private:
    shim::IAudioDevice* dev_ = nullptr;
    audio::IAudioStatusDevice* status_ = nullptr;  // null if not queryable
    shim::IFileSystem* fs_ = nullptr;
    std::string gameDir_;
    shim::VoiceHandle voice_ = -1;
    std::vector<i16> pcm_;       // decoded stream PCM (owned for the voice's life)
    int rate_ = 44100;
    std::string currentName_;
    int nextHandle_ = 1;         // opaque non-zero handles (dword_642018 contract)
    int currentHandle_ = 0;
    bool streaming_ = false;
    int loads_ = 0, stops_ = 0, lastLoops_ = 0;
};

// Init-time options that are NOT part of config::SoundSettings (see header note).
struct SessionAudioInit {
    std::string gameDir;     // absolute install root (mp3 decode needs a host path;
                             // empty = no music PCM decode, state machine still runs)
    bool msxOn = true;       // INI [Sound] msx     -> dword_63C8F8 (@0x534bbc)
    bool sfxOn = true;       // INI [Sound] sfx     -> dword_63C900
    bool weatherOn = true;   // INI [Sound] weather -> dword_63C904
    bool preloadSfxBanks = true; // run the include_sfx.ini preload at Init
    // The frame-loop feature mask (v54 of VIBE_GameLogic_RunFrameLoop); the city
    // session runs with the day-cycle-music bit set.
    std::uint32_t featureMask = app::mask::kDayCycleMusic;
};

// What SessionAudio did / is doing (for callers and tests).
struct SessionAudioStatus {
    bool soundInited = false;        // SoundSystem::init succeeded
    bool marketSampleLoaded = false; // real ADPCM decoded into the bank
    int  marketSampleRate = 0;       // decoded sample rate (22050 for the real asset)
    std::size_t marketPcmBytes = 0;  // decoded S16 bytes (stereo-interleaved)
    bool marketLoopPlaying = false;  // the 3D market loop entry is attached
    std::size_t banksIndexed = 0;    // .sbf banks parsed via include_sfx.ini
    std::size_t samplesIndexed = 0;  // entries bridged into the SampleBank
    int  masterVolumeApplied = 0;    // the (int)(sfx_vol * v6) SetMusicVolume product
    float musicFreqScale = 0.0f;     // msx_freq * 0.25 (flt_6422A8; computed, unused)
    app::AudioTickResult lastTick{}; // last Frame's tick transitions
    std::uint32_t frames = 0;        // Frame calls since Init
};

// ---------------------------------------------------------------------------
// SessionAudio — real audio in the native city session.
// ---------------------------------------------------------------------------
class SessionAudio {
public:
    // Bring up the real sound stack on `dev` over the install behind `fs`
    // (a shim::IFileSystem rooted at the game dir). `snd` is the [Sound] INI
    // block (config::ReadGfxAndSoundSettings). Returns false if the device/fs is
    // missing or the voice pool could not initialize.
    bool Init(shim::IAudioDevice* dev, shim::IFileSystem* fs,
              const config::SoundSettings& snd,
              const SessionAudioInit& opt = SessionAudioInit{});

    // One audio tick (the 0x4c09a0 audio block; see header map). `listenerPos`/
    // `listenerFwd` are the camera pose the 3D pool mixes against; `locationId`
    // feeds the music state machine (0 = outdoor); `season` is
    // VIBE_GameTime_GetSeasonFromDay (day % 4); `gameTick` is the frame counter
    // (the original's dword_62EB38 time base — the voice queue applies the *13).
    void Frame(const float listenerPos[3], const float listenerFwd[3],
               int locationId, int season, std::uint32_t gameTick);

    // Scene exit: stop the market loop (VIBE_Ambient_StopMarketLoop @0x5828bc),
    // stop the music stream, shut the sound system down (VIBE_Sound_Shutdown
    // @0x439ccc). Safe to call twice. Does NOT shut the device down (caller owns).
    void Shutdown();

    // The market-stall world position the loop emitter binds to (the original
    // attaches to the live market scene entity, dword_6477A4+97; the session
    // host passes the placed market object's position).
    void SetMarketPosition(const float pos[3]);

    // Inspection / wiring hooks.
    bool initialized() const { return inited_; }
    const SessionAudioStatus& status() const { return status_; }
    audio::SoundSystem* sound() { return sound_.get(); }
    audio::MusicDirector& director() { return musicDir_; }
    SessionMusicSink& musicSink() { return sink_; }
    const app::AudioEnable& enables() const { return enable_; }
    audio::Sound3dEntry* marketLoopEntry() { return marketHandle_; }

private:
    void applyVolumeSettings(const config::SoundSettings& snd);
    void preloadSampleBanks();
    void loadMarketAmbiencePcm();
    void seedMusicTable();

    shim::IAudioDevice* dev_ = nullptr;
    shim::IFileSystem* fs_ = nullptr;
    SessionAudioInit opt_;
    // Adapter that adds the streaming-track voice headroom (the original kept up
    // to 10 MSS stream slots OUTSIDE the 48 sb sample handles; the shim device
    // pools all voices from one init() count). SoundSystem talks to the adapter;
    // the music sink talks to the raw device (same underlying voice pool).
    std::unique_ptr<shim::IAudioDevice> deviceAdapter_;
    std::unique_ptr<audio::SoundSystem> sound_;
    audio::MusicDirector musicDir_;
    SessionMusicSink sink_;
    app::AudioEnable enable_;
    audio::Sound3dEntry* marketHandle_ = nullptr; // dword_6420F4
    audio::Vec3 marketPos_{};
    bool marketStartTried_ = false;  // scene-entry one-shot (0x50f19a call site)
    bool marketDeviceLoopSet_ = false; // VIBE_Sound3d_SetLooping(h,1) applied
    bool inited_ = false;
    SessionAudioStatus status_;
};

} // namespace guild::play
