// End-to-end test for guild::audio::sb3: drive a whole sample-bank lifecycle
// across the module — LoadFromText -> mutate (Add/Remove) -> ResolveSamplePaths
// -> PlaySample -> SaveToText -> Destroy — through the in-memory stream hooks,
// asserting the byte-exact .txt round-trip and the cross-function consistency.
#include "test.h"
#include "audio/audio_samplebank3.h"

#include <cstring>
#include <string>
#include <vector>

using namespace guild::audio::sb3;

namespace {

struct TextStream {
    std::vector<std::string> tokens;
    std::size_t pos = 0;
    std::string out;
};

TextStream* g_rs = nullptr;
TextStream* g_ws = nullptr;
std::vector<std::pair<std::string, int>>* g_sizes = nullptr;

std::vector<std::string> Tokenize(const char* body) {
    std::vector<std::string> v; std::string cur;
    for (const char* p = body; *p; ++p) {
        if (*p == ' ' || *p == '\n' || *p == '\t' || *p == '\r') {
            if (!cur.empty()) { v.push_back(cur); cur.clear(); }
        } else cur += *p;
    }
    if (!cur.empty()) v.push_back(cur);
    return v;
}

void Install() {
    g_hooks = Sb3Hooks{};
    g_hooks.openStream = [](const char*, const char*) -> void* { static int h = 1; return &h; };
    g_hooks.closeStream = [](void*) {};
    g_hooks.readToken = [](void*, char* out, int cap) -> int {
        if (!g_rs || g_rs->pos >= g_rs->tokens.size()) return -1;
        std::strncpy(out, g_rs->tokens[g_rs->pos].c_str(), cap - 1);
        out[cap - 1] = 0; ++g_rs->pos; return 0;
    };
    g_hooks.writeText = [](void*, const char* t) { if (g_ws) g_ws->out += t; };
    g_hooks.sampleFileSize = [](const char* p) -> int {
        if (!g_sizes) return 0;
        for (auto& kv : *g_sizes) if (kv.first == p) return kv.second;
        return 0;
    };
}

void Reset() {
    if (g_activeBank) Destroy();
    g_activeBank = nullptr; g_dirtyCount = 0;
    g_rs = nullptr; g_ws = nullptr; g_sizes = nullptr;
    g_hooks = Sb3Hooks{};
}

} // namespace

TEST(Sb3E2E, FullLifecycleRoundTrip) {
    Reset();
    Install();

    std::vector<std::pair<std::string, int>> sizes = {
        {"music.wav", 1000},
        {"step/a.wav", 10}, {"step/b.wav", 20},
        {"door.wav", 50},
        {"extra.wav", 5},
    };
    g_sizes = &sizes;

    // 1) Load a bank from text.
    const char* body =
        "GameAudio "
        "music.wav {EndOfSamples} "
        "steps step/a.wav step/b.wav {EndOfVariation} "
        "doors door.wav {EndOfVariation} "
        "{EndOfSampleBank}";
    TextStream rs; rs.tokens = Tokenize(body);
    g_rs = &rs;
    CHECK_EQ(LoadFromText("", "game.txt"), 0);
    CHECK(g_activeBank != nullptr);

    // 2) Mutate: add a top-level sample, add a sample to the "steps" variation.
    CHECK_EQ(AddSample("extra.wav"), 0);
    CHECK_EQ(AddSampleToVariation("steps", "step/c.wav"), 0); // size 0 (unknown)
    sizes.push_back({"step/c.wav", 30});

    // 3) Resolve all paths -> recompute accumulators with the new file.
    CHECK_EQ(ResolveSamplePaths(""), 0);
    if (g_activeBank) {
        // steps variation: 10 + 20 + 30.
        SbRecord* steps = g_activeBank->varNext;
        CHECK(steps != nullptr);
        if (steps) CHECK_EQ(steps->size, 60);
        // bank total = music(1000) + extra(5) + steps(60) + doors(50).
        CHECK_EQ(g_activeBank->size, 1000 + 5 + 60 + 50);
    }

    // 4) Play a sample from a variation (index 1 of "steps" -> step/b.wav).
    g_hooks.openStream = [](const char*, const char*) -> void* { static int h = 1; return &h; };
    CHECK_EQ(PlaySample("steps", 1), 0);
    // Play a concrete top-level sample.
    CHECK_EQ(PlaySample("music.wav", 0), 0);

    // 5) Remove a variation, then serialize.
    CHECK_EQ(RemoveVariation("doors"), 0);
    TextStream ws; g_ws = &ws;
    CHECK_EQ(SaveToText("out.txt"), 0);

    const char* expect =
        "GameAudio\n"
        "music.wav\n"
        "extra.wav\n"
        "{EndOfSamples}\n"
        "steps\n"
        "step/a.wav\n"
        "step/b.wav\n"
        "step/c.wav\n"
        "{EndOfVariation}\n"
        "{EndOfSampleBank}\n";
    CHECK_EQ(ws.out, std::string(expect));
    CHECK_EQ(g_dirtyCount, 0); // SaveToText cleared it

    // 6) Re-parse the serialized text into a *second* bank and confirm the tree
    //    matches (byte-exact round-trip of names).
    Destroy();
    CHECK(g_activeBank == nullptr);
    TextStream rs2; rs2.tokens = Tokenize(ws.out.c_str());
    g_rs = &rs2;
    CHECK_EQ(LoadFromText("", "out.txt"), 0);
    if (g_activeBank) {
        CHECK_EQ(std::strcmp(g_activeBank->name, "GameAudio"), 0);
        CHECK(g_activeBank->sampleHead != nullptr);
        if (g_activeBank->sampleHead)
            CHECK_EQ(std::strcmp(g_activeBank->sampleHead->name, "music.wav"), 0);
        SbRecord* v = g_activeBank->varNext;
        CHECK(v != nullptr);
        if (v) {
            CHECK_EQ(std::strcmp(v->name, "steps"), 0);
            CHECK(v->varNext == nullptr); // doors was removed
        }
    }

    Reset();
}
