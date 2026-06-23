// guild::play — MP3 → PCM decode. See header.
#include "play/mp3_decode.h"

namespace guild::play {

#ifdef GUILD_HAVE_MP3

// Minimal libmpg123 C API, declared here so we don't need the -dev header. The
// ABI is stable across the 1.x series; we link against libmpg123.so.0.
extern "C" {
typedef struct mpg123_handle_struct mpg123_handle;
int            mpg123_init(void);
mpg123_handle* mpg123_new(const char* decoder, int* error);
void           mpg123_delete(mpg123_handle*);
int            mpg123_open(mpg123_handle*, const char* path);
int            mpg123_close(mpg123_handle*);
int            mpg123_format_none(mpg123_handle*);
int            mpg123_format(mpg123_handle*, long rate, int channels, int encodings);
int            mpg123_getformat(mpg123_handle*, long* rate, int* channels, int* encoding);
int            mpg123_read(mpg123_handle*, unsigned char* out, size_t size, size_t* done);
}

namespace {
constexpr int kMpg123Ok        = 0;
constexpr int kMpg123Stereo    = 2;
// MPG123_ENC_SIGNED_16 = ENC_16(0x40) | ENC_SIGNED(0x80) | 0x10.
constexpr int kEncSigned16     = 0xD0;
} // namespace

bool DecodeMp3File(const std::string& path, std::vector<std::int16_t>& pcm,
                   int& sampleRate) {
    static bool inited = false;
    if (!inited) {
        if (mpg123_init() != kMpg123Ok) return false;
        inited = true;
    }
    int err = 0;
    mpg123_handle* h = mpg123_new(nullptr, &err);
    if (!h) return false;

    if (mpg123_open(h, path.c_str()) != kMpg123Ok) { mpg123_delete(h); return false; }

    long rate = 0; int ch = 0, enc = 0;
    if (mpg123_getformat(h, &rate, &ch, &enc) != kMpg123Ok) {
        mpg123_close(h); mpg123_delete(h); return false;
    }
    // Lock output to S16 stereo at the native rate (prevents mid-stream format
    // changes; forces mono sources up to stereo so the device mixer sees 2ch).
    mpg123_format_none(h);
    mpg123_format(h, rate, kMpg123Stereo, kEncSigned16);
    sampleRate = static_cast<int>(rate);

    pcm.clear();
    unsigned char buf[16384];
    size_t done = 0;
    int rc;
    do {
        rc = mpg123_read(h, buf, sizeof(buf), &done);
        if (done) {
            const std::int16_t* s = reinterpret_cast<const std::int16_t*>(buf);
            pcm.insert(pcm.end(), s, s + done / sizeof(std::int16_t));
        }
    } while (rc == kMpg123Ok);

    mpg123_close(h);
    mpg123_delete(h);
    return !pcm.empty();
}

#else  // !GUILD_HAVE_MP3

bool DecodeMp3File(const std::string&, std::vector<std::int16_t>&, int&) {
    return false;  // no libmpg123 in this build
}

#endif

} // namespace guild::play
