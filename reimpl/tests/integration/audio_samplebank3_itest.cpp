// Integration test: wire audio_samplebank3's path-rewrite (the loc_5CB930 token
// scan in LoadFromText / ResolveSamplePaths / PlaySample) against the REAL
// reconstructed sibling guild::util::StrStr (string_ops.cpp, VIBE_Util_StrStr).
// This mirrors the live wiring: the original calls loc_5CB930 == StrStr to find
// the "%lang" token before splicing in the locale path.  We forward the strStr
// hook into the real util function (NOT a mock) and assert the cross-module
// substitution actually fires.
#include "test.h"
#include "audio/audio_samplebank3.h"
#include "util/string_ops.h" // real sibling module (NOT a mock)

#include <cstring>
#include <string>
#include <vector>

using namespace guild::audio::sb3;

namespace {

struct TextStream {
    std::vector<std::string> tokens;
    std::size_t pos = 0;
};

TextStream* g_rs = nullptr;
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

void Reset() {
    if (g_activeBank) Destroy();
    g_activeBank = nullptr; g_dirtyCount = 0;
    g_hooks = Sb3Hooks{};
}

} // namespace

TEST(Sb3Itest, RealStrStrDrivesLangRewrite) {
    Reset();

    // Wire the path-token scanner to the REAL util::StrStr, exactly as the engine
    // does (loc_5CB930 == VIBE_Util_StrStr).
    g_hooks.strStr = [](char* hay, const char* needle) -> char* {
        return guild::util::StrStr(hay, needle); // real reconstructed sibling
    };

    // The sample-file-size probe records the resolved path that the rewrite
    // produced, so we can prove the %lang -> root substitution went through the
    // real StrStr split.
    static std::vector<std::string> seenPaths;
    seenPaths.clear();
    static std::vector<std::pair<std::string, int>> sizes = {
        {"snd/de/hello.wav", 42},
        {"snd/de/bye.wav", 7},
    };
    g_sizes = &sizes;
    g_hooks.sampleFileSize = [](const char* p) -> int {
        seenPaths.push_back(p);
        for (auto& kv : *g_sizes) if (kv.first == p) return kv.second;
        return 0;
    };
    g_hooks.openStream = [](const char*, const char*) -> void* { static int h = 1; return &h; };
    g_hooks.closeStream = [](void*) {};
    g_hooks.readToken = [](void*, char* out, int cap) -> int {
        if (!g_rs || g_rs->pos >= g_rs->tokens.size()) return -1;
        std::strncpy(out, g_rs->tokens[g_rs->pos].c_str(), cap - 1);
        out[cap - 1] = 0; ++g_rs->pos; return 0;
    };

    // Two top-level samples whose stored names embed the "%lang" token.
    const char* body =
        "VoiceBank "
        "snd/%lang/hello.wav snd/%lang/bye.wav {EndOfSamples} "
        "{EndOfSampleBank}";
    TextStream rs; rs.tokens = Tokenize(body);
    g_rs = &rs;

    // root = "de"; the real StrStr finds "%lang" (5 chars) and we splice "de".
    CHECK_EQ(LoadFromText("de", "voices.txt"), 0);

    if (g_activeBank) {
        // Stored token keeps the raw "%lang" form...
        SbRecord* s = g_activeBank->sampleHead;
        CHECK(s != nullptr);
        if (s) {
            CHECK_EQ(std::strcmp(s->name, "snd/%lang/hello.wav"), 0);
            // ...but the resolved size came from the rewritten path "snd/de/hello.wav".
            CHECK_EQ(s->smpSize, 42);
            if (s->smpNext) CHECK_EQ(s->smpNext->smpSize, 7);
        }
    }
    // The probe must have been handed the de-substituted paths produced by the
    // real StrStr split (proves the cross-module flow).
    bool sawHello = false, sawBye = false;
    for (auto& p : seenPaths) {
        if (p == "snd/de/hello.wav") sawHello = true;
        if (p == "snd/de/bye.wav") sawBye = true;
    }
    CHECK(sawHello);
    CHECK(sawBye);

    g_sizes = nullptr;
    Reset();
}

TEST(Sb3Itest, RealStrStrResolveAfterRootChange) {
    Reset();
    g_hooks.strStr = [](char* hay, const char* needle) -> char* {
        return guild::util::StrStr(hay, needle);
    };
    static std::vector<std::string> seen;
    seen.clear();
    static std::vector<std::pair<std::string, int>> sizes = {
        {"snd/en/a.wav", 11}, {"snd/fr/a.wav", 99},
    };
    g_sizes = &sizes;
    g_hooks.sampleFileSize = [](const char* p) -> int {
        seen.push_back(p);
        for (auto& kv : *g_sizes) if (kv.first == p) return kv.second;
        return 0;
    };

    // Build a one-sample bank with a %lang token directly.
    g_activeBank = new SbRecord();
    std::strcpy(g_activeBank->name, "B");
    SbRecord* s = new SbRecord();
    std::strcpy(s->name, "snd/%lang/a.wav");
    g_activeBank->sampleHead = s;

    // Resolve with root "en" -> finds "snd/en/a.wav" (size 11).
    CHECK_EQ(ResolveSamplePaths("en"), 0);
    CHECK_EQ(s->smpSize, 11);
    CHECK_EQ(g_activeBank->size, 11);

    // Re-resolve with root "fr" -> the real StrStr re-splits -> size 99.
    CHECK_EQ(ResolveSamplePaths("fr"), 0);
    CHECK_EQ(s->smpSize, 99);
    CHECK_EQ(g_activeBank->size, 99);

    g_sizes = nullptr;
    Reset();
}
