// E2E: build a sample bank from scratch through the reconstructed leaf API,
// mutate it, and tear it down — exercising AddVariation, sample population,
// CountSamples, SetVariationName, ClearVariationSamples, the dirty counter, and
// the IsValid/Has* introspection, as a single workflow.
#include "test.h"
#include "audio/audio_samplebank2.h"

#include <cstring>

using namespace guild::audio::sb2;

namespace {
Record* MakeSample(const char* name, int size) {
    Record* r = new Record();
    std::strncpy(r->name, name, sizeof(r->name) - 1);
    r->smpSize = size;
    return r;
}
} // namespace

TEST(Sb2E2E, BuildMutateTeardown) {
    g_activeBank = nullptr; g_dirtyCount = 0;
    g_fileHooks = FileHooks{}; g_sound3dHooks = Sound3dHooks{};

    Record bank;
    std::strcpy(bank.name, "creatures");
    bank.size = 0;
    g_activeBank = &bank;

    char buf[64];
    // Fresh bank: valid, no samples, no variations.
    CHECK_EQ(IsValid(buf, 64), 0);
    CHECK_EQ(std::strcmp(buf, "creatures"), 0);
    CHECK_EQ(HasVariations(buf, 64), 0);

    // Add two variations.
    CHECK_EQ(AddVariation("dog"), 0);
    CHECK_EQ(AddVariation("cat"), 0);
    CHECK_EQ(GetDirtyCount(), 2);
    CHECK_EQ(HasVariations(buf, 64), 1);
    CHECK_EQ(std::strcmp(buf, "dog"), 0); // first variation is head

    // Populate "dog" with three samples (sizes 10/20/30 -> 60).
    Record* dog = bank.next;
    CHECK(dog != nullptr);
    if (dog) {
        Record* s0 = MakeSample("bark", 10);
        Record* s1 = MakeSample("growl", 20);
        Record* s2 = MakeSample("whine", 30);
        s0->smpNext = s1; s1->smpNext = s2;
        dog->sampleHead = s0;
        dog->size = 60;
        bank.size = 60;

        CHECK_EQ(CountSamples("dog"), 3);
        CHECK_EQ(CountSamples("cat"), 0);
        CHECK_EQ(CountSamples(nullptr), 3); // sum across all variations

        CHECK_EQ(VariationHasSamples("dog", buf, 64), 1);
        CHECK_EQ(std::strcmp(buf, "bark"), 0);

        // Rename "growl" -> "snarl".
        CHECK_EQ(SetVariationName("dog", "growl", "snarl"), 0);
        CHECK_EQ(std::strcmp(s1->name, "snarl"), 0);

        // Remove the middle sample by name: bank/var size drop by 20.
        CHECK_EQ(ClearVariationSamples("dog", "snarl"), 0);
        CHECK_EQ(dog->size, 40);
        CHECK_EQ(bank.size, 40);
        CHECK_EQ(CountSamples("dog"), 2);

        // Clear the rest (null name): sizes drop to 0, head nulled.
        int before = GetDirtyCount();
        CHECK_EQ(ClearVariationSamples("dog", nullptr), 0);
        CHECK_EQ(dog->size, 0);
        CHECK_EQ(bank.size, 0);
        CHECK(dog->sampleHead == nullptr);
        CHECK_EQ(GetDirtyCount(), before + 2); // two nodes freed
        CHECK_EQ(VariationHasSamples("dog", buf, 64), 0);
    }

    // Teardown: free the variation chain.
    Record* v = bank.next;
    while (v) { Record* n = v->next; delete v; v = n; }
    g_activeBank = nullptr;

    // After teardown, introspection reports no bank.
    CHECK_EQ(IsValid(buf, 64), -1);
    CHECK_EQ(HasVariations(buf, 64), 0);
}
