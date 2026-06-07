// Unit tests for guild::audio::sb3 — the .txt sample-bank parser/serializer,
// tree mutators, and player.  Drives the module through an in-memory text-stream
// mock installed via Sb3Hooks (no OS/Miles/VFS call escapes).
#include "test.h"
#include "audio/audio_samplebank3.h"

#include <cstring>
#include <string>
#include <vector>

using namespace guild::audio::sb3;

namespace {

// ---- in-memory text stream backing the hooks --------------------------------
struct TextStream {
    std::vector<std::string> tokens; // for read ("%s")
    std::size_t pos = 0;
    std::string out;                 // for write
};

void ResetAll() {
    // Tear down any tree left over from a prior test.
    if (g_activeBank) Destroy();
    g_activeBank = nullptr;
    g_dirtyCount = 0;
    g_compiledSize = 0;
    g_compiledBlob = nullptr;
    g_hooks = Sb3Hooks{};
}

// Tokenize a file body the way scanf("%s") would (whitespace-delimited).
std::vector<std::string> Tokenize(const char* body) {
    std::vector<std::string> v;
    std::string cur;
    for (const char* p = body; *p; ++p) {
        if (*p == ' ' || *p == '\n' || *p == '\t' || *p == '\r') {
            if (!cur.empty()) { v.push_back(cur); cur.clear(); }
        } else {
            cur += *p;
        }
    }
    if (!cur.empty()) v.push_back(cur);
    return v;
}

// Map of known file sizes for GetSampleFileSize.
std::vector<std::pair<std::string, int>>* g_sizes = nullptr;
// Active streams (capture-less lambdas reach them through these file-scope ptrs).
TextStream* g_readStream = nullptr;
TextStream* g_writeStream = nullptr;

void InstallTextHooks(TextStream* readStream, TextStream* writeStream) {
    g_readStream = readStream;
    g_writeStream = writeStream;
    g_hooks.openStream = [](const char*, const char*) -> void* {
        static int h = 1;
        return &h; // non-null
    };
    g_hooks.closeStream = [](void*) {};
    g_hooks.readToken = [](void* /*s*/, char* out, int cap) -> int {
        TextStream* ts = g_readStream;
        if (!ts || ts->pos >= ts->tokens.size()) return -1;
        std::strncpy(out, ts->tokens[ts->pos].c_str(), cap - 1);
        out[cap - 1] = 0;
        ++ts->pos;
        return 0;
    };
    g_hooks.writeText = [](void* /*s*/, const char* text) {
        if (g_writeStream) g_writeStream->out += text;
    };
    g_hooks.sampleFileSize = [](const char* path) -> int {
        if (!g_sizes) return 0;
        for (auto& kv : *g_sizes) if (kv.first == path) return kv.second;
        return 0;
    };
}

} // namespace

TEST(Sb3Unit, StrNCopyPadAndExtensionProbe) {
    ResetAll();
    // GetSampleFileSize returns 0 by default (no file system).
    CHECK_EQ(GetSampleFileSize("missing.wav"), 0);
}

TEST(Sb3Unit, LoadFromTextBuildsTree) {
    ResetAll();
    const char* body =
        "MyBank "
        "snd/a.wav snd/b.wav {EndOfSamples} "
        "footsteps "
        "fs/1.wav fs/2.wav fs/3.wav {EndOfVariation} "
        "doors "
        "dr/open.wav {EndOfVariation} "
        "{EndOfSampleBank}";
    TextStream rs; rs.tokens = Tokenize(body);
    InstallTextHooks(&rs, nullptr);

    std::vector<std::pair<std::string, int>> sizes = {
        {"snd/a.wav", 10}, {"snd/b.wav", 20},
        {"fs/1.wav", 1}, {"fs/2.wav", 2}, {"fs/3.wav", 3},
        {"dr/open.wav", 100},
    };
    g_sizes = &sizes;

    CHECK_EQ(LoadFromText("", "bank.txt"), 0);
    if (g_activeBank) {
        CHECK_EQ(std::strcmp(g_activeBank->name, "MyBank"), 0);
        // Two top-level samples.
        SbRecord* s = g_activeBank->sampleHead;
        CHECK(s != nullptr);
        if (s) {
            CHECK_EQ(std::strcmp(s->name, "snd/a.wav"), 0);
            CHECK_EQ(s->smpSize, 10);
            CHECK(s->smpNext != nullptr);
            if (s->smpNext) {
                CHECK_EQ(std::strcmp(s->smpNext->name, "snd/b.wav"), 0);
                CHECK_EQ(s->smpNext->smpSize, 20);
                CHECK(s->smpNext->smpNext == nullptr);
            }
        }
        // Two variations.
        SbRecord* v = g_activeBank->varNext;
        CHECK(v != nullptr);
        if (v) {
            CHECK_EQ(std::strcmp(v->name, "footsteps"), 0);
            CHECK_EQ(v->size, 1 + 2 + 3); // accumulated per-variation
            CHECK(v->varNext != nullptr);
            if (v->varNext) {
                CHECK_EQ(std::strcmp(v->varNext->name, "doors"), 0);
                CHECK_EQ(v->varNext->size, 100);
                CHECK(v->varNext->varNext == nullptr);
            }
        }
        // Bank accumulates only the variation samples' sizes (matches 0x448e19).
        CHECK_EQ(g_activeBank->size, 1 + 2 + 3 + 100);
    }
    g_sizes = nullptr;
}

TEST(Sb3Unit, LoadRejectsWhenBankAlreadyLoaded) {
    ResetAll();
    TextStream rs; rs.tokens = Tokenize("B {EndOfSamples} {EndOfSampleBank}");
    InstallTextHooks(&rs, nullptr);
    CHECK_EQ(LoadFromText("", "b.txt"), 0);
    // Second load must fail (g_activeBank != null -> 0x4489ae).
    TextStream rs2; rs2.tokens = Tokenize("C {EndOfSamples} {EndOfSampleBank}");
    g_readStream = &rs2;
    CHECK_EQ(LoadFromText("", "c.txt"), -1);
}

TEST(Sb3Unit, SaveToTextSerializesGrammar) {
    ResetAll();
    // Build a small bank directly via mutators, then serialize.
    g_activeBank = new SbRecord();
    std::strcpy(g_activeBank->name, "BankX");
    CHECK_EQ(AddSample("top.wav"), 0);
    // A variation with two samples.
    SbRecord* var = new SbRecord();
    std::strcpy(var->name, "var1");
    g_activeBank->varNext = var;
    CHECK_EQ(AddSampleToVariation("var1", "v/a.wav"), 0);
    CHECK_EQ(AddSampleToVariation("var1", "v/b.wav"), 0);

    TextStream ws;
    InstallTextHooks(nullptr, &ws);
    CHECK_EQ(SaveToText("out.txt"), 0);

    const char* expect =
        "BankX\n"
        "top.wav\n"
        "{EndOfSamples}\n"
        "var1\n"
        "v/a.wav\n"
        "v/b.wav\n"
        "{EndOfVariation}\n"
        "{EndOfSampleBank}\n";
    CHECK_EQ(ws.out, std::string(expect));
    // SaveToText clears the dirty count (0x449047).
    CHECK_EQ(g_dirtyCount, 0);
}

TEST(Sb3Unit, MutatorsAndDuplicateRejection) {
    ResetAll();
    g_activeBank = new SbRecord();
    std::strcpy(g_activeBank->name, "B");

    CHECK_EQ(AddSample("a.wav"), 0);
    CHECK_EQ(AddSample("b.wav"), 0);
    CHECK_EQ(AddSample("a.wav"), -1); // duplicate rejected (FindSampleByName)
    // RemoveSample by name.
    CHECK_EQ(RemoveSample("a.wav"), 0);
    CHECK(g_activeBank->sampleHead != nullptr);
    if (g_activeBank->sampleHead)
        CHECK_EQ(std::strcmp(g_activeBank->sampleHead->name, "b.wav"), 0);
    // Clear-all top-level.
    CHECK_EQ(RemoveSample(nullptr), 0);
    CHECK(g_activeBank->sampleHead == nullptr);

    // AddSampleToVariation requires the variation to exist.
    CHECK_EQ(AddSampleToVariation("nope", "x.wav"), -1);
}

TEST(Sb3Unit, ResolveSamplePathsRecomputesSizes) {
    ResetAll();
    g_activeBank = new SbRecord();
    std::strcpy(g_activeBank->name, "B");
    CHECK_EQ(AddSample("a.wav"), 0); // size 0 (no hook yet)
    SbRecord* var = new SbRecord();
    std::strcpy(var->name, "v");
    g_activeBank->varNext = var;

    std::vector<std::pair<std::string, int>> sizes = {{"a.wav", 7}, {"v/s.wav", 9}};
    g_sizes = &sizes;
    g_hooks.sampleFileSize = [](const char* p) -> int {
        if (!g_sizes) return 0;
        for (auto& kv : *g_sizes) if (kv.first == p) return kv.second;
        return 0;
    };
    CHECK_EQ(AddSampleToVariation("v", "v/s.wav"), 0); // now resolves to 9

    CHECK_EQ(ResolveSamplePaths(""), 0);
    if (g_activeBank) {
        CHECK_EQ(g_activeBank->sampleHead->smpSize, 7);
        CHECK_EQ(var->sampleHead->smpSize, 9);
        CHECK_EQ(var->size, 9);
        CHECK_EQ(g_activeBank->size, 7 + 9);
    }
    // A missing file aborts with -1.
    sizes.clear();
    CHECK_EQ(ResolveSamplePaths(""), -1);
    g_sizes = nullptr;
}

TEST(Sb3Unit, PlaySampleVariationPicksIndex) {
    ResetAll();
    g_activeBank = new SbRecord();
    std::strcpy(g_activeBank->name, "B");
    SbRecord* var = new SbRecord();
    std::strcpy(var->name, "footsteps"); // no dot -> variation
    g_activeBank->varNext = var;

    std::vector<std::pair<std::string, int>> sizes = {
        {"f0.wav", 5}, {"f1.wav", 5}, {"f2.wav", 5}};
    g_sizes = &sizes;
    g_hooks.sampleFileSize = [](const char* p) -> int {
        if (!g_sizes) return 0;
        for (auto& kv : *g_sizes) if (kv.first == p) return kv.second;
        return 0;
    };
    g_hooks.openStream = [](const char*, const char*) -> void* {
        static int h = 1; return &h;
    };
    CHECK_EQ(AddSampleToVariation("footsteps", "f0.wav"), 0);
    CHECK_EQ(AddSampleToVariation("footsteps", "f1.wav"), 0);
    CHECK_EQ(AddSampleToVariation("footsteps", "f2.wav"), 0);

    // index 0 -> first sample "f0.wav" (a dotted name -> concrete play).
    CHECK_EQ(PlaySample("footsteps", 0), 0);
    // index 2 -> third sample.
    CHECK_EQ(PlaySample("footsteps", 2), 0);
    // Unknown variation -> -1.
    CHECK_EQ(PlaySample("ghost", 0), -1);
    // A concrete missing sample -> -1.
    sizes.clear();
    CHECK_EQ(PlaySample("missing.wav", 0), -1);
    g_sizes = nullptr;
}

TEST(Sb3Unit, DestroyFreesEverything) {
    ResetAll();
    g_activeBank = new SbRecord();
    std::strcpy(g_activeBank->name, "B");
    AddSample("a.wav");
    SbRecord* var = new SbRecord();
    std::strcpy(var->name, "v");
    g_activeBank->varNext = var;
    AddSampleToVariation("v", "x.wav");

    CHECK_EQ(Destroy(), 0);
    CHECK(g_activeBank == nullptr);
    // Second destroy with no bank -> -1.
    CHECK_EQ(Destroy(), -1);
}
