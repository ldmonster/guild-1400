// Unit tests for guild::audio::sb2 (audio_samplebank2.{h,cpp}).
// Golden vectors computed with python3 against the recovered semantics.
#include "test.h"
#include "audio/audio_samplebank2.h"

#include <cstring>

using namespace guild::audio::sb2;

namespace {

// Reset module globals so each test is independent (CHECK does not abort).
void Reset() {
    g_activeBank = nullptr;
    g_dirtyCount = 0;
    g_fileHooks = FileHooks{};
    g_sound3dHooks = Sound3dHooks{};
}

Record* MakeSample(const char* name, int size) {
    Record* r = new Record();
    std::strncpy(r->name, name, sizeof(r->name) - 1);
    r->smpSize = size;
    return r;
}

} // namespace

TEST(Sb2ExtractExt, Basic) {
    Reset();
    char out[32];
    std::memset(out, 0xAA, sizeof(out));
    CHECK_EQ(ExtractFileExtension("C:\\sounds\\bird.wav", out, 32), 0);
    CHECK_EQ(std::strncmp(out, "wav", 3), 0);
    // The original StrNCopyPad copies exactly `needed` (3) bytes; with needed<cap
    // it does NOT pad beyond, so out[3] is the caller's pre-existing byte. (This
    // is a faithful quirk: ExtractFileExtension copies only the length, not cap.)
    CHECK_EQ(static_cast<unsigned char>(out[3]), 0xAA);
}

TEST(Sb2ExtractExt, ErrorsAndCap) {
    Reset();
    char out[8];
    CHECK_EQ(ExtractFileExtension(nullptr, out, 8), -1);
    CHECK_EQ(ExtractFileExtension("x.wav", nullptr, 8), -1);
    CHECK_EQ(ExtractFileExtension("noext", out, 8), -1);     // no dot
    // "wav" needs 3 bytes; cap 3 -> needed(3) >= cap(3) -> -1
    CHECK_EQ(ExtractFileExtension("a.wav", out, 3), -1);
    CHECK_EQ(ExtractFileExtension("a.wav", out, 4), 0);
}

TEST(Sb2Classify, FormatCodes) {
    Reset();
    CHECK_EQ(ClassifyAudioFormat(".wav"), 1);
    CHECK_EQ(ClassifyAudioFormat(".WAV"), 1);
    CHECK_EQ(ClassifyAudioFormat("song.wav"), 1);
    CHECK_EQ(ClassifyAudioFormat(".mp3"), 2);
    CHECK_EQ(ClassifyAudioFormat(".MP3"), 2);
    CHECK_EQ(ClassifyAudioFormat(".ogg"), 0);
    CHECK_EQ(ClassifyAudioFormat(nullptr), 0);
}

TEST(Sb2Dirty, GetDirtyCount) {
    Reset();
    CHECK_EQ(GetDirtyCount(), 0);
    g_dirtyCount = 7;
    CHECK_EQ(GetDirtyCount(), 7);
}

TEST(Sb2Bank, IsValidAndHas) {
    Reset();
    char out[64];
    // No bank.
    CHECK_EQ(IsValid(out, 64), -1);
    CHECK_EQ(HasSamples(out, 64), 0);
    CHECK_EQ(HasVariations(out, 64), 0);

    Record bank;
    std::strcpy(bank.name, "mybank");
    g_activeBank = &bank;
    std::memset(out, 0, sizeof(out));
    CHECK_EQ(IsValid(out, 64), 0);
    CHECK_EQ(std::strcmp(out, "mybank"), 0);

    // No samples / variations yet.
    CHECK_EQ(HasSamples(out, 64), 0);
    CHECK_EQ(HasVariations(out, 64), 0);

    Record sample;
    std::strcpy(sample.name, "shot");
    bank.sampleHead = &sample;
    std::memset(out, 0, sizeof(out));
    CHECK_EQ(HasSamples(out, 64), 1);
    CHECK_EQ(std::strcmp(out, "shot"), 0);

    g_activeBank = nullptr;
}

TEST(Sb2Var, AddAndCount) {
    Reset();
    Record bank;
    std::strcpy(bank.name, "bank");
    g_activeBank = &bank;

    CHECK_EQ(AddVariation("growl"), 0);
    CHECK_EQ(GetDirtyCount(), 1);
    CHECK_EQ(AddVariation("bark"), 0);
    CHECK_EQ(GetDirtyCount(), 2);
    // Duplicate -> -1, no dirty bump.
    CHECK_EQ(AddVariation("growl"), -1);
    CHECK_EQ(GetDirtyCount(), 2);

    char out[64];
    CHECK_EQ(HasVariations(out, 64), 1);
    CHECK_EQ(std::strcmp(out, "growl"), 0); // first appended is head

    // Variation chain: growl -> bark. Attach samples to "bark" and count.
    Record* growl = bank.next;
    Record* bark = growl ? growl->next : nullptr;
    CHECK(bark != nullptr);
    if (bark) {
        bark->sampleHead = MakeSample("s0", 10);
        bark->sampleHead->smpNext = MakeSample("s1", 20);
        CHECK_EQ(CountSamples("bark"), 2);
        CHECK_EQ(CountSamples("growl"), 0);
        CHECK_EQ(CountSamples(nullptr), 2); // sum across variations
        // cleanup
        delete bark->sampleHead->smpNext;
        delete bark->sampleHead;
        bark->sampleHead = nullptr;
    }
    // free variation nodes
    Record* v = bank.next;
    while (v) { Record* n = v->next; delete v; v = n; }
    g_activeBank = nullptr;
}

TEST(Sb2Var, VariationHasSamples) {
    Reset();
    Record bank; std::strcpy(bank.name, "b");
    Record var;  std::strcpy(var.name, "v");
    Record* s = MakeSample("first", 5);
    var.sampleHead = s;
    bank.next = &var;
    g_activeBank = &bank;

    char out[64];
    CHECK_EQ(VariationHasSamples("v", out, 64), 1);
    CHECK_EQ(std::strcmp(out, "first"), 0);
    CHECK_EQ(VariationHasSamples("missing", out, 64), 0);

    delete s;
    g_activeBank = nullptr;
}

TEST(Sb2Var, ClearSamplesSubtractsSize) {
    Reset();
    Record bank; std::strcpy(bank.name, "b"); bank.size = 100;
    Record var;  std::strcpy(var.name, "v"); var.size = 30;
    Record* a = MakeSample("a", 10);
    Record* b = MakeSample("b", 20);
    a->smpNext = b;
    var.sampleHead = a;
    bank.next = &var;
    g_activeBank = &bank;

    // Remove single named sample "a": subtract 10 from var(30->20) and bank(100->90).
    CHECK_EQ(ClearVariationSamples("v", "a"), 0);
    CHECK_EQ(var.size, 20);
    CHECK_EQ(bank.size, 90);
    CHECK_EQ(GetDirtyCount(), 1);
    CHECK(var.sampleHead == b); // head now b (a freed by impl)

    // Remove all remaining (sampleName == null): subtract 20 -> var 0, bank 70.
    CHECK_EQ(ClearVariationSamples("v", nullptr), 0);
    CHECK_EQ(var.size, 0);
    CHECK_EQ(bank.size, 70);
    CHECK_EQ(GetDirtyCount(), 2);
    CHECK(var.sampleHead == nullptr);

    // Missing variation -> -1.
    CHECK_EQ(ClearVariationSamples("nope", nullptr), -1);

    g_activeBank = nullptr;
}

TEST(Sb2Var, SetVariationName) {
    Reset();
    Record bank; std::strcpy(bank.name, "b");
    Record var;  std::strcpy(var.name, "v");
    Record* a = MakeSample("old", 1);
    Record* b = MakeSample("keep", 1);
    a->smpNext = b;
    var.sampleHead = a;
    bank.next = &var;
    g_activeBank = &bank;

    CHECK_EQ(SetVariationName("v", "old", "renamed"), 0);
    CHECK_EQ(std::strcmp(a->name, "renamed"), 0);
    CHECK_EQ(std::strcmp(b->name, "keep"), 0);
    // sample not present -> -1
    CHECK_EQ(SetVariationName("v", "absent", "x"), -1);

    delete a; delete b;
    g_activeBank = nullptr;
}

TEST(Sb2Slots, FindFreeAmbient) {
    Reset();
    // 10 slots, stride 296. flagA active, flagB busy. Free = !busy && active.
    uint8_t a[296 * 10] = {0};
    uint8_t b[296 * 10] = {0};
    // slot 0: active but busy -> skip; slot 1: active & free -> pick.
    a[0 * 296] = 1; b[0 * 296] = 1;
    a[1 * 296] = 1; b[1 * 296] = 0;
    CHECK_EQ(FindFreeAmbientSlot(a, b), 1);
    // none active -> -1
    uint8_t z[296 * 10] = {0};
    CHECK_EQ(FindFreeAmbientSlot(z, z), -1);
}

TEST(Sb2Slots, FindFreeVoice) {
    Reset();
    uint8_t flag[740 * 4] = {0};
    int32_t w0[740] = {0};
    int32_t w1[740] = {0};
    // slot 0 fully free -> pass1 picks 0.
    CHECK_EQ(FindFreeVoiceSlot(flag, w0, w1), 0);
    // Make slot 0 have w1 set so pass1 skips it but pass2 (ignores w1) picks 0.
    w1[0] = 5;
    // slot 1 has w0 set so pass1 also skips; slot 2 fully free -> pass1 picks 2.
    w0[1 * 74] = 9;
    CHECK_EQ(FindFreeVoiceSlot(flag, w0, w1), 2);
    // Now block w0 for all slots except via word1: clear w0, set flag on slot0.
    int32_t w0b[740] = {0};
    int32_t w1b[740] = {0};
    for (int i = 0; i < 740; i += 74) w1b[i] = 1; // all have w1 -> pass1 fails
    CHECK_EQ(FindFreeVoiceSlot(flag, w0b, w1b), 0); // pass2 picks slot 0
}

TEST(Sb2Sound3d, SetRangeAndDetach) {
    Reset();
    Sound3dRangeEntry e;
    CHECK(SetRange(&e, 42) == &e);
    CHECK_EQ(e.range, 42);

    // DetachIfValid: 0 -> 0 (no hook called).
    CHECK_EQ(DetachIfValid(0), 0);
    // inert default (no hook) returns handle unchanged.
    CHECK_EQ(DetachIfValid(99), 99);
    // installed hook is invoked for nonzero handles.
    static int seen = 0;
    g_sound3dHooks.detachEntry = [](int h) { seen = h; return h * 2; };
    CHECK_EQ(DetachIfValid(7), 14);
    CHECK_EQ(seen, 7);
}
