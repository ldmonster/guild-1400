// guild::play — REAL audio in the native city session. See session_audio.h for
// the reconstruction map (addresses + named gaps).
#include "play/session_audio.h"

#include "play/real_audio.h"        // LoadRealSampleBank / FindEntry / DecodeEntryToPcm
#include "play/mp3_decode.h"        // DecodeMp3File (the proven menu music path)
#include "app/real_audio_driver.h"  // ParseSfxIncludeList / LoadOneSfxBank
#include "audio/audio_leaves.h"     // ApplyVolumeSettings (VIBE @0x56c148)
#include "audio/soundwave.h"        // ParseWavHeader

#include <cctype>
#include <cstring>

namespace guild::play {

// VIBE_Ambient_StartMarketLoop @0x582858 plays this exact sample (string @0x6263f0).
const char kMarketAmbienceSample[] = "Athmo_Marktplatz_Mono_4Bit";
// The shipping bank that contains it (include_sfx.ini: "Lebewesen\athmos.sbf").
const char kMarketAmbienceBank[] = "sfx/Lebewesen/athmos.sbf";

namespace {

// ---------------------------------------------------------------------------
// Exact float constants of VIBE_Audio_ApplyVolumeSettings @0x56c148 (get_int):
//   flt_62522C @0x62522C = 0x3C010204 (~1/127, the slider->fraction scale)
//   flt_625230 @0x625230 = 0x3E800000 (0.25, the msx_freq scale)
// ---------------------------------------------------------------------------
float F32FromBits(u32 bits) {
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}
const u32 kVolScale0Bits = 0x3C010204u; // flt_62522C
const u32 kVolScale1Bits = 0x3E800000u; // flt_625230

// The original keeps up to 10 streaming-track slots (VIBE_Audio_LoadTrack's slot
// table) NEXT TO the 48 sb sample handles — MSS streams are not sample voices.
// The shim device pools voices from one init() count, so the session asks for
// this much stream headroom on top of whatever the voice pool requests.
constexpr int kStreamVoiceHeadroom = 10;

// IAudioDevice adapter that adds the stream headroom at init() time and forwards
// everything else (the music stream voices live in the same device mix as the
// sample voices, exactly like MSS mixed streams + samples into one DIG output).
class StreamHeadroomDevice : public shim::IAudioDevice,
                             public audio::IAudioStatusDevice {
public:
    explicit StreamHeadroomDevice(shim::IAudioDevice* inner)
        : inner_(inner),
          innerStatus_(dynamic_cast<audio::IAudioStatusDevice*>(inner)) {}

    bool init(int voices, int channels, int sampleRate) override {
        return inner_->init(voices + kStreamVoiceHeadroom, channels, sampleRate);
    }
    void shutdown() override { inner_->shutdown(); }
    shim::VoiceHandle allocVoice() override { return inner_->allocVoice(); }
    void freeVoice(shim::VoiceHandle v) override { inner_->freeVoice(v); }
    void playSample(shim::VoiceHandle v, const void* pcm, std::size_t bytes,
                    int sampleRate, int loops) override {
        inner_->playSample(v, pcm, bytes, sampleRate, loops);
    }
    void stop(shim::VoiceHandle v) override { inner_->stop(v); }
    void setVolume(shim::VoiceHandle v, int vol) override { inner_->setVolume(v, vol); }
    void setPan(shim::VoiceHandle v, int pan) override { inner_->setPan(v, pan); }
    void setMasterVolume(int vol) override { inner_->setMasterVolume(vol); }

    // audio::IAudioStatusDevice — forward when the real device can report status;
    // otherwise "not playing" (the same never-playing default VoicePool applies
    // to non-status devices).
    int sampleStatus(shim::VoiceHandle v) override {
        return innerStatus_ ? innerStatus_->sampleStatus(v) : 0;
    }

private:
    shim::IAudioDevice* inner_;
    audio::IAudioStatusDevice* innerStatus_;
};

bool EqualNoCase(const std::string& a, const char* b, std::size_t blen) {
    if (a.size() != blen)
        return false;
    for (std::size_t i = 0; i < blen; ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

// Resolve `relPath` over `fs` matching each path segment case-insensitively
// against the directory listing (Win32 opens were case-insensitive; the install
// mixes "CD1"/"Cd2" vs the binary's "cd1\..." literals). "" when unresolved.
std::string ResolveCaseInsensitive(shim::IFileSystem* fs, const std::string& relPath) {
    if (!fs)
        return {};
    if (fs->exists(relPath.c_str()))
        return relPath;

    std::vector<std::string> segs;
    std::string cur;
    for (char c : relPath) {
        if (c == '/') {
            if (!cur.empty()) segs.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) segs.push_back(cur);
    if (segs.empty())
        return {};

    std::string built;
    for (const std::string& want : segs) {
        std::string dir = built.empty() ? std::string(".") : built;
        shim::IDirListing* listing = fs->listDir(dir.c_str());
        if (!listing)
            return {};
        std::string match;
        for (std::size_t k = 0; k < listing->count(); ++k) {
            const shim::DirEntry& e = listing->at(k);
            if (e.name && EqualNoCase(want, e.name, std::strlen(e.name))) {
                match = e.name;
                break;
            }
        }
        delete listing;
        if (match.empty())
            return {};
        built = built.empty() ? match : (built + "/" + match);
    }
    return fs->exists(built.c_str()) ? built : std::string{};
}

// Read a whole file off `fs` (the only OS boundary in this module).
bool SlurpFile(shim::IFileSystem* fs, const char* path, std::vector<u8>& out) {
    shim::IFile* f = fs->open(path, "rb");
    if (!f)
        return false;
    std::int64_t n = f->size();
    if (n < 0) { fs->close(f); return false; }
    out.resize(static_cast<std::size_t>(n));
    std::size_t got = n > 0 ? f->read(out.data(), out.size()) : 0;
    out.resize(got);
    fs->close(f);
    return true;
}

u32 ReadLe32(const u8* p) {
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) |
           (static_cast<u32>(p[2]) << 16) | (static_cast<u32>(p[3]) << 24);
}

// ---------------------------------------------------------------------------
// IMA ADPCM tables — the IMA/DVI standard step + index tables (the tables MSS's
// "IMA DVI ADPCM" provider implements; WAVE fmt tag 0x11).
// ---------------------------------------------------------------------------
const int kImaIndexTable[16] = {-1, -1, -1, -1, 2, 4, 6, 8,
                                -1, -1, -1, -1, 2, 4, 6, 8};
const int kImaStepTable[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,
    19,    21,    23,    25,    28,    31,    34,    37,    41,    45,
    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,
    337,   371,   408,   449,   494,   544,   598,   658,   724,   796,
    876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,
    5894,  6484,  7132,  7845,  8630,  9493,  10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

int ClampS16(int v) {
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return v;
}

// Decode one 4-bit IMA nibble against (pred, index).
i16 ImaStep(int nibble, int& pred, int& index) {
    const int step = kImaStepTable[index];
    int diff = step >> 3;
    if (nibble & 1) diff += step >> 2;
    if (nibble & 2) diff += step >> 1;
    if (nibble & 4) diff += step;
    if (nibble & 8)
        pred = ClampS16(pred - diff);
    else
        pred = ClampS16(pred + diff);
    index += kImaIndexTable[nibble & 0xF];
    if (index < 0) index = 0;
    else if (index > 88) index = 88;
    return static_cast<i16>(pred);
}

} // namespace

std::size_t DecodeImaAdpcm(const u8* payload, std::size_t bytes, int channels,
                           int blockAlign, std::size_t sampleLimit,
                           std::vector<i16>& out) {
    out.clear();
    if (!payload || bytes == 0 || channels < 1 || channels > 2 ||
        blockAlign < 4 * channels)
        return 0;

    const std::size_t hdrBytes = static_cast<std::size_t>(4) * channels;
    std::size_t frames = 0;

    for (std::size_t blockOff = 0; blockOff + hdrBytes <= bytes;
         blockOff += static_cast<std::size_t>(blockAlign)) {
        const std::size_t blockBytes =
            (bytes - blockOff < static_cast<std::size_t>(blockAlign))
                ? bytes - blockOff
                : static_cast<std::size_t>(blockAlign);
        if (blockBytes < hdrBytes)
            break;
        const u8* blk = payload + blockOff;

        int pred[2] = {0, 0};
        int index[2] = {0, 0};
        for (int ch = 0; ch < channels; ++ch) {
            const u8* h = blk + static_cast<std::size_t>(4) * ch;
            pred[ch] = static_cast<i16>(static_cast<u16>(h[0]) |
                                        (static_cast<u16>(h[1]) << 8));
            index[ch] = h[2];
            if (index[ch] > 88) index[ch] = 88;
        }

        // The block header carries the first decoded frame verbatim.
        for (int ch = 0; ch < channels; ++ch)
            out.push_back(static_cast<i16>(pred[ch]));
        ++frames;
        if (sampleLimit && frames >= sampleLimit) {
            out.resize(sampleLimit * static_cast<std::size_t>(channels));
            return sampleLimit;
        }

        // Data nibbles: 4-byte words per channel, channels interleaved word-wise
        // (mono degenerates to a flat nibble stream). Each word = 8 samples.
        const std::size_t dataBytes = blockBytes - hdrBytes;
        const u8* data = blk + hdrBytes;
        const std::size_t wordsPerChan =
            dataBytes / (static_cast<std::size_t>(4) * channels);
        for (std::size_t w = 0; w < wordsPerChan; ++w) {
            i16 chunk[2][8];
            for (int ch = 0; ch < channels; ++ch) {
                const u8* word =
                    data + (w * channels + static_cast<std::size_t>(ch)) * 4;
                for (int b = 0; b < 4; ++b) {
                    chunk[ch][b * 2 + 0] = ImaStep(word[b] & 0xF, pred[ch], index[ch]);
                    chunk[ch][b * 2 + 1] = ImaStep(word[b] >> 4, pred[ch], index[ch]);
                }
            }
            for (int s = 0; s < 8; ++s) {
                for (int ch = 0; ch < channels; ++ch)
                    out.push_back(chunk[ch][s]);
                ++frames;
                if (sampleLimit && frames >= sampleLimit) {
                    out.resize(sampleLimit * static_cast<std::size_t>(channels));
                    return sampleLimit;
                }
            }
        }
    }
    return frames;
}

std::string MusicTrackRelPath(const std::string& trackName) {
    // VIBE_Audio_LoadTrack composes "msx\" (string @0x622b3e) + the table name.
    std::string p = "msx/";
    for (char c : trackName)
        p.push_back(c == '\\' ? '/' : c);
    return p;
}

// ---------------------------------------------------------------------------
// SessionMusicSink
// ---------------------------------------------------------------------------

void SessionMusicSink::bind(shim::IAudioDevice* dev, shim::IFileSystem* fs,
                            std::string gameDir) {
    dev_ = dev;
    status_ = dynamic_cast<audio::IAudioStatusDevice*>(dev);
    fs_ = fs;
    gameDir_ = std::move(gameDir);
}

int SessionMusicSink::loadTrack(const std::string& name, int loop) {
    ++loads_;
    currentName_ = name;
    if (!dev_) {
        currentHandle_ = nextHandle_++;
        return currentHandle_;
    }

    // VIBE_Audio_LoadTrack recycles the stream slot: stop any previous stream.
    if (voice_ >= 0) {
        dev_->stop(voice_);
        dev_->freeVoice(voice_);
        voice_ = -1;
        streaming_ = false;
    }

    // Resolve + decode the real msx/ mp3 (the menu-music path). With no install
    // dir (headless) the stream voice still runs so the state machine is exact.
    pcm_.clear();
    rate_ = 44100;
    bool decoded = false;
    if (fs_ && !gameDir_.empty()) {
        std::string resolved = ResolveCaseInsensitive(fs_, MusicTrackRelPath(name));
        if (!resolved.empty())
            decoded = DecodeMp3File(gameDir_ + "/" + resolved, pcm_, rate_);
    }

    voice_ = dev_->allocVoice();
    if (voice_ < 0) {
        currentHandle_ = 0; // VIBE_Audio_LoadTrack returns 0 when no slot opens
        return 0;
    }

    // Loop mapping. The director calls loadTrack(name, 0) = play once (the MSS
    // loop-count-1 branch of VIBE_Audio_StartTrack @0x439f8c); the shim contract
    // is loops 0 = once, -1 = forever. NAMED GAP: without a per-voice status
    // probe (audio::IAudioStatusDevice) the once-ended stream is undetectable,
    // so the track is looped at the device to keep music audible — the original
    // ended->pause->reselect rotation needs the probe to fire.
    int loops = (loop != 0) ? -1 : (status_ ? 0 : -1);
    lastLoops_ = loops;

    // VIBE_Audio_StartTrack order: SetStreamVolume(...,0) -> start -> volume.
    dev_->setVolume(voice_, 0);
    dev_->playSample(voice_, pcm_.empty() ? nullptr : pcm_.data(),
                     pcm_.size() * sizeof(i16), rate_, loops);
    dev_->setVolume(voice_, audio::kDefaultTrackVolume); // +0x118 = 127
    streaming_ = decoded && !pcm_.empty();
    currentHandle_ = nextHandle_++;
    return currentHandle_;
}

void SessionMusicSink::stopTrack(int handle, int fade) {
    ++stops_;
    if (handle == 0 || handle != currentHandle_)
        return;
    if (dev_ && voice_ >= 0) {
        if (fade)
            dev_->setVolume(voice_, 0); // VIBE_Audio_FadeOutTrack(.., 2): timed
                                        // fade modeled as an immediate mute (the
                                        // same model audio::MusicPlayer uses)
        dev_->stop(voice_);             // PauseStream + CloseStream
        dev_->freeVoice(voice_);
        voice_ = -1;
    }
    streaming_ = false;
    currentHandle_ = 0;
}

bool SessionMusicSink::streamEnded() const {
    // The original's *(handle+260/261)==0 stream-cursor-at-end check, via the
    // status probe (AIL_sample_status != SMP_PLAYING).
    if (!status_ || voice_ < 0 || currentHandle_ == 0)
        return false;
    return status_->sampleStatus(voice_) != audio::kSampleStatusPlaying;
}

void SessionMusicSink::stopAll() {
    if (currentHandle_)
        stopTrack(currentHandle_, /*fade=*/0);
    pcm_.clear();
    currentName_.clear();
    streaming_ = false;
}

// ---------------------------------------------------------------------------
// SessionAudio
// ---------------------------------------------------------------------------

bool SessionAudio::Init(shim::IAudioDevice* dev, shim::IFileSystem* fs,
                        const config::SoundSettings& snd,
                        const SessionAudioInit& opt) {
    Shutdown();
    status_ = SessionAudioStatus{};
    if (!dev || !fs)
        return false;
    dev_ = dev;
    fs_ = fs;
    opt_ = opt;

    // VIBE_Sound_LibInit @0x445d90 — 48 voices / 2ch / 44100 (engine constants;
    // the [Sound] sliders are volumes, not the device format). The adapter adds
    // the streaming-track headroom MSS kept outside the 48 sample handles.
    deviceAdapter_ = std::make_unique<StreamHeadroomDevice>(dev);
    sound_ = std::make_unique<audio::SoundSystem>(deviceAdapter_.get());
    bool ok = sound_->init(audio::kDefaultVoices, audio::kDefaultChannels,
                           audio::kDefaultSampleRate);
    if (!ok && !sound_->voices().initialized()) {
        // Device open failed outright (the original returns -1 from LibInit and
        // the game runs silent); a partial voice-bind keeps the pool (also -1).
        sound_.reset();
        deviceAdapter_.reset();
        return false;
    }
    status_.soundInited = true;

    sink_.bind(dev, fs, opt.gameDir);

    applyVolumeSettings(snd);          // VIBE_Audio_ApplyVolumeSettings @0x56c148
    if (opt.preloadSfxBanks)
        preloadSampleBanks();          // VIBE_Sound_PreloadFromIncludeFile @0x52f154
    loadMarketAmbiencePcm();           // VIBE_Sound_LoadEntry for the market bank
    seedMusicTable();                  // the OUTDOOR (9876) track entry

    // The subsystem-enable gates (VIBE_GameLogic_MainEntryAndShutdown @0x534bbc:
    // [Sound] msx -> dword_63C8F8, sfx -> dword_63C900, weather -> dword_63C904).
    enable_.sound3dOn = opt.sfxOn;
    enable_.weatherOn = opt.weatherOn;
    enable_.musicOn = opt.msxOn;

    marketHandle_ = nullptr;
    marketStartTried_ = false;
    marketDeviceLoopSet_ = false;
    inited_ = true;
    return true;
}

void SessionAudio::applyVolumeSettings(const config::SoundSettings& snd) {
    // Byte mapping per the INI writer (VIBE_Config_WriteGfxSettings):
    //   byte_1233550 = master_vol, 1233551 = sfx_vol, 1233552 = msx_vol,
    //   1233553 = speech_vol, 1233554 = msx_freq.
    audio::VolumeSettingsIn in;
    in.soundByte = snd.masterVol;   // byte_1233550
    in.musicByte = snd.sfxVol;      // byte_1233551 (-> VIBE_Audio_SetMusicVolume)
    in.sfxByte = snd.msxVol;        // byte_1233552 (-> VIBE_Audio_ApplyMasterVolume)
    in.ambByte = snd.speechVol;     // byte_1233553 (-> flt_64200C)
    in.amb2Byte = snd.msxFreq;      // byte_1233554 (-> flt_6422A8)
    in.scale0 = F32FromBits(kVolScale0Bits);
    in.scale1 = F32FromBits(kVolScale1Bits);
    audio::VolumeSettingsOut out = audio::ApplyVolumeSettings(in);

    // Original call order: SetMasterVolume((int)v6) [sb voice-layer master],
    // ApplyMasterVolume((int)(msx_vol*v6)), SetMusicVolume((int)(sfx_vol*v6)).
    // The shim device exposes ONE master; the same calls are issued in the same
    // order through the reconstructed MusicPlayer (last write wins on the single
    // master, mirroring how the digital master ends at the SetMusicVolume value).
    sound_->music().applyMasterVolume(out.sfxVolume);  // VIBE @0x439ddc
    sound_->music().setMusicVolume(out.musicVolume);   // VIBE @0x439e90
    status_.masterVolumeApplied = out.musicVolume;
    status_.musicFreqScale = out.ambient2Scale;        // flt_6422A8 (computed,
                                                       // unconsumed — named gap)
}

void SessionAudio::preloadSampleBanks() {
    // VIBE_Sound_PreloadFromIncludeFile @0x52f154: read include_sfx.ini, then
    // VIBE_Sound_LoadSampleBank @0x446b2c per listed bank (app::LoadOneSfxBank
    // bridges each parsed index into the live SampleBank; PCM stays unfaulted).
    std::vector<u8> ini;
    std::string resolved = ResolveCaseInsensitive(fs_, "include_sfx.ini");
    if (resolved.empty() || !SlurpFile(fs_, resolved.c_str(), ini) || ini.empty())
        return;
    std::string text(reinterpret_cast<const char*>(ini.data()), ini.size());
    for (const std::string& inc : app::ParseSfxIncludeList(text)) {
        app::SfxBankLoad b = app::LoadOneSfxBank(fs_, *sound_, "sfx", inc);
        if (b.parsed) {
            ++status_.banksIndexed;
            status_.samplesIndexed += b.entryCount;
        }
    }
}

void SessionAudio::loadMarketAmbiencePcm() {
    // Fault the market-ambience entry's PCM in (the VIBE_Sound_LoadEntry
    // @0x446830 on-demand load, for the one sample the session's ambience
    // trigger plays). The shipped block is an IMA ADPCM .wav (fmt 0x11, mono,
    // 22050 Hz) — decoded by the standard IMA decoder above.
    std::vector<u8> fileBytes;
    RealSbBank bank;
    if (!LoadRealSampleBank(*fs_, kMarketAmbienceBank, fileBytes, bank) || !bank.ok)
        return; // bank absent (no install) — market loop stays unstarted
    const RealSbEntry* entry = FindEntry(bank, kMarketAmbienceSample);
    if (!entry)
        return;

    std::vector<i16> mono;
    int rate = 0, channels = 0;

    if (!DecodeEntryToPcm(fileBytes, *entry, mono, rate, channels)) {
        // Not plain 16-bit PCM. Locate the RIFF (athmos.sbf's on-disk entry
        // record carries no usable data offset — the format-1 data block
        // directly follows the entry array; scan for the RIFF tag) and decode
        // the IMA ADPCM payload.
        std::size_t scanFrom = 0x148 + bank.entries.size() * 0x40;
        if (scanFrom > fileBytes.size())
            scanFrom = 0;
        std::size_t riff = std::string::npos;
        for (std::size_t i = scanFrom; i + 4 <= fileBytes.size(); ++i) {
            if (std::memcmp(fileBytes.data() + i, "RIFF", 4) == 0) {
                riff = i;
                break;
            }
        }
        if (riff == std::string::npos)
            return;
        audio::WavHeader h =
            audio::ParseWavHeader(fileBytes.data() + riff, fileBytes.size() - riff);
        if (!h.ok || h.audioFormat != 17 || h.blockAlign == 0)
            return; // unsupported codec — NAMED GAP, no analogue
        // `fact` chunk = total sample frames (chunk walk from the fmt chunk on).
        std::size_t factFrames = 0;
        {
            const u8* base = fileBytes.data() + riff;
            std::size_t avail = fileBytes.size() - riff;
            std::size_t off = 12;
            while (off + 8 <= avail) {
                const u8* ck = base + off;
                u32 ckSize = ReadLe32(ck + 4);
                if (std::memcmp(ck, "fact", 4) == 0 && ckSize >= 4 &&
                    off + 8 + 4 <= avail) {
                    factFrames = ReadLe32(ck + 8);
                    break;
                }
                if (std::memcmp(ck, "data", 4) == 0)
                    break;
                off += 8 + ckSize + (ckSize & 1);
            }
        }
        std::size_t dataOff = riff + h.dataOffset;
        std::size_t dataBytes = h.dataSize;
        if (dataOff > fileBytes.size())
            return;
        if (dataOff + dataBytes > fileBytes.size())
            dataBytes = fileBytes.size() - dataOff;
        DecodeImaAdpcm(fileBytes.data() + dataOff, dataBytes, h.numChannels,
                       h.blockAlign, factFrames, mono);
        rate = static_cast<int>(h.sampleRate);
        channels = h.numChannels;
    }
    if (mono.empty() || channels < 1)
        return;

    // The device mix consumes interleaved PCM at the DEVICE channel count (2);
    // expand mono to stereo (the format conversion MSS performed on output).
    std::vector<i16> inter;
    if (channels == 1) {
        inter.reserve(mono.size() * 2);
        for (i16 s : mono) {
            inter.push_back(s);
            inter.push_back(s);
        }
    } else {
        inter = std::move(mono);
    }

    audio::SampleRecord* rec = sound_->bank().findSampleByName(kMarketAmbienceSample);
    if (!rec)
        rec = &sound_->bank().addSample(kMarketAmbienceSample);
    rec->format = 1; // decoded to a playable .wav-style PCM block
    rec->sampleRate = rate;
    rec->pcm.resize(inter.size() * sizeof(i16));
    std::memcpy(rec->pcm.data(), inter.data(), rec->pcm.size());

    status_.marketSampleLoaded = true;
    status_.marketSampleRate = rate;
    status_.marketPcmBytes = rec->pcm.size();
}

void SessionAudio::seedMusicTable() {
    // The OUTDOOR track entry (sentinel id 9876) the season selector acts on.
    // NAMED GAP: the interior location entries (Athmos\Athmo_*.mp3 per location
    // id) are not seeded — VIBE_Music_ResumeLocationTrack's full table is not
    // reconstructed yet, so non-zero locationIds resolve to no entry.
    musicDir_ = audio::MusicDirector{};
    audio::TrackEntry outdoor;
    outdoor.ids = {audio::kOutdoorTrackId};
    musicDir_.table.push_back(std::move(outdoor));
    musicDir_.sink = &sink_;
}

void SessionAudio::SetMarketPosition(const float pos[3]) {
    if (pos)
        marketPos_ = audio::Vec3{pos[0], pos[1], pos[2]};
}

void SessionAudio::Frame(const float listenerPos[3], const float listenerFwd[3],
                         int locationId, int season, std::uint32_t gameTick) {
    if (!inited_ || !sound_)
        return;

    app::AudioListener listener{};
    if (listenerPos)
        listener.pos = audio::Vec3{listenerPos[0], listenerPos[1], listenerPos[2]};
    if (listenerFwd)
        listener.forward =
            audio::Vec3{listenerFwd[0], listenerFwd[1], listenerFwd[2]};

    // --- scene-entry market-ambience trigger -------------------------------
    // VIBE_Scene_RunMainFrameLoop @0x50f0c0 calls VIBE_Ambient_StartMarketLoop
    // ONCE before entering its frame loop (0x50f19a) and StopMarketLoop at scene
    // exit (0x50f60e) — the loop persists for the whole city session and the 3D
    // attenuation (radius 2200) handles audibility by distance. StartMarketLoop
    // itself gates on the live market scene record (*(dword_6477A4+97)); here the
    // gate is "the market sample resolved from the real bank".
    if (!marketStartTried_ && enable_.sound3dOn) {
        marketStartTried_ = true;
        if (sound_->bank().findSampleByName(kMarketAmbienceSample)) {
            app::StartMarketLoop(*sound_, marketHandle_, marketPos_, listener,
                                 kMarketAmbienceSample);
        }
    }
    // VIBE_Sound3d_SetLooping(dword_6420F4, 1) — the original marks the entry's
    // stream looping right after attach; apply the loop at the device once the
    // 3D update has bound a voice (Sound3dPool starts voices with loops=0).
    if (marketHandle_ && !marketDeviceLoopSet_ && marketHandle_->voice) {
        sound_->voices().startVoice(marketHandle_->voice, marketHandle_->pcm,
                                    marketHandle_->pcmBytes,
                                    marketHandle_->sampleRate, /*loops=*/-1);
        marketDeviceLoopSet_ = true;
    }
    status_.marketLoopPlaying = (marketHandle_ != nullptr);

    // --- the audio block of VIBE_GameLogic_RunFrameLoop @0x4c09a0 ----------
    // VIBE_Music_UpdateOutdoorTrackPlayback's own entry gate (@0x5815c4):
    //   if (!byte_63CC40 || flt_6422A8 <= 0.0) return;
    // byte_63CC40 is the in-scene flag (set around the scene frame loop — being
    // inside Frame IS that scope); flt_6422A8 = msx_freq * 0.25, so msx_freq == 0
    // silences the outdoor-music director entirely.
    app::AudioEnable en = enable_;
    en.musicOn = enable_.musicOn && (status_.musicFreqScale > 0.0f);
    const bool atStreamEnd = sink_.streamEnded();
    status_.lastTick = app::AudioTick(
        *sound_, musicDir_, en, opt_.featureMask,
        static_cast<int>(gameTick), listener, season, atStreamEnd, locationId);
    ++status_.frames;
}

void SessionAudio::Shutdown() {
    if (!inited_) {
        sound_.reset();
        deviceAdapter_.reset();
        return;
    }
    // Scene exit: VIBE_Ambient_StopMarketLoop @0x5828bc, stop the music stream
    // (VIBE_Audio_StopTrack @0x43a2fc), VIBE_Sound_Shutdown @0x439ccc.
    app::StopMarketLoop(*sound_, marketHandle_);
    marketDeviceLoopSet_ = false;
    if (musicDir_.currentTrackHandle) {
        sink_.stopTrack(musicDir_.currentTrackHandle, /*fade=*/0);
        musicDir_.currentTrackHandle = 0;
    }
    sink_.stopAll();
    sound_->shutdown();
    sound_.reset();
    deviceAdapter_.reset();
    status_.marketLoopPlaying = false;
    inited_ = false;
}

} // namespace guild::play
