// Integration test: wire audio_samplebank2's slot allocator against the REAL
// reconstructed sibling guild::audio::Compute3dVolume / Compute3dPan (sound3d.cpp,
// VIBE_Sound3d_UpdateAttenuation @0x4249d0 / UpdatePosition @0x4248d4). This mirrors
// the live flow: pick a free voice slot, then compute the 3d volume/pan that the
// engine would push into that slot. Also exercises the real ValidateSampleFile
// File-I/O hook path end to end (no mock of the function under test).
#include "test.h"
#include "audio/audio_samplebank2.h"
#include "audio/sound3d.h" // real sibling module (NOT a mock)

#include <cstring>

using namespace guild::audio;
using namespace guild::audio::sb2;

TEST(Sb2Itest, SlotThenRealAttenuation) {
    // Reset.
    g_activeBank = nullptr; g_dirtyCount = 0;
    g_fileHooks = FileHooks{}; g_sound3dHooks = Sound3dHooks{};

    // 1) Allocate a free voice slot via our reconstructed scanner.
    uint8_t flag[740 * 4] = {0};
    int32_t w0[740] = {0};
    int32_t w1[740] = {0};
    // Slots 0,1 busy (w0 set); slot 2 free.
    w0[0 * 74] = 1;
    w0[1 * 74] = 1;
    int slot = FindFreeVoiceSlot(flag, w0, w1);
    CHECK_EQ(slot, 2);

    // 2) Feed listener/source geometry into the REAL sound3d sibling that the
    //    engine uses to fill that slot's volume + pan.
    Vec3 listener{0, 0, 0};
    Vec3 sourceNear{1, 0, 0};
    int vol = Compute3dVolume(listener, sourceNear, 100.0f, 100); // real VIBE fn
    CHECK_EQ(vol, 99); // golden (python): near source ~ baseVol

    Vec3 sourceFar{100000, 0, 0};
    CHECK_EQ(Compute3dVolume(listener, sourceFar, 100.0f, 100), 0); // beyond range -> silent

    Vec3 forward{0, 0, 1};
    CHECK_EQ(Compute3dPan(forward, forward), 63);   // straight ahead -> center
    Vec3 right{1, 0, 0};
    CHECK_EQ(Compute3dPan(forward, right), 126);     // 90deg -> sin(1)*127*.5+63
}

TEST(Sb2Itest, ValidateSampleFileHookFlow) {
    g_fileHooks = FileHooks{};

    // Default (inert) hooks: no file system -> failure.
    CHECK_EQ(ValidateSampleFile("missing.wav"), -1);

    // Install hooks that simulate a real VFS: open returns a handle + size, the
    // single full read succeeds.
    static int openCalls = 0, readCalls = 0, closeCalls = 0;
    static char lastPath[64] = {0};
    g_fileHooks.open = [](const char* path, const char* mode, unsigned* outSize) -> void* {
        ++openCalls;
        std::strncpy(lastPath, path, sizeof(lastPath) - 1);
        (void)mode;
        *outSize = 128;
        static int handleStorage = 0;
        return &handleStorage; // non-null handle
    };
    g_fileHooks.read = [](void* h, void* buf, unsigned size, int count) -> int {
        ++readCalls; (void)h; (void)buf; (void)size;
        return count; // requested count read -> success when count==1
    };
    g_fileHooks.close = [](void* h) { ++closeCalls; (void)h; };

    CHECK_EQ(ValidateSampleFile("C:\\snd\\ok.wav"), 0);
    CHECK_EQ(openCalls, 1);
    CHECK_EQ(readCalls, 1);
    CHECK_EQ(closeCalls, 1);
    CHECK_EQ(std::strcmp(lastPath, "C:\\snd\\ok.wav"), 0);

    // A short read (returns 0, not 1) -> -1, still closes.
    g_fileHooks.read = [](void*, void*, unsigned, int) -> int { return 0; };
    CHECK_EQ(ValidateSampleFile("C:\\snd\\short.wav"), -1);
    CHECK_EQ(closeCalls, 2);

    g_fileHooks = FileHooks{};
}

TEST(Sb2Itest, ClassifyDrivesFormatPipeline) {
    // ExtractFileExtension -> ClassifyAudioFormat is the load-time format probe.
    char ext[16];
    CHECK_EQ(ExtractFileExtension("C:\\voices\\hi.mp3", ext, 16), 0);
    CHECK_EQ(std::strncmp(ext, "mp3", 3), 0);
    // The classifier matches on the dotted form, as the original does on the path.
    CHECK_EQ(ClassifyAudioFormat("hi.mp3"), 2);
    CHECK_EQ(ClassifyAudioFormat("hi.wav"), 1);
}
