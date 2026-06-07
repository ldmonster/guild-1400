// Integration test for the script_console slice wired against a REAL reconstructed
// sibling: guild::util::StrCmpNoCase (util/string_ops.cpp, gilde.exe 0x5cb8f0).
//
// LookupInclude compares a query name against each loaded sub-script's name via the
// utilStrCmp hook (the original called VIBE_Util_StrCmp).  Here we forward that hook
// into the real case-insensitive string compare exactly as the live engine wiring
// would when an include is matched case-insensitively, and assert the cross-module
// flow: a case-different query still resolves the right include slot, and ParseInclude
// + LookupInclude compose end to end over the real comparator.
#include "test.h"

#include "sim/script_console.h"
#include "util/string_ops.h"
#include <cstring>
#include <vector>

using namespace guild;
using guild::sim::ScriptConsoleHooks;
using guild::sim::SetScriptConsoleHooks;

namespace {

// Forward the LookupInclude comparator into the REAL reconstructed sibling.
int RealStrCmpNoCase(const char* a, const char* b) {
    return guild::util::StrCmpNoCase(a, b);
}

// A loaded-context buffer: byte 0 begins the NUL-terminated name string.
guild::u8 g_loadedA[64];
guild::u8 g_loadedB[64];

guild::u8 Tok7(const char*, guild::u8* out) { if (out) out[0] = 7; return 7; }

} // namespace

// ---------------------------------------------------------------------------
// Case-insensitive include lookup over the real StrCmpNoCase sibling.
// ---------------------------------------------------------------------------
TEST(ScriptConsoleItest, LookupIncludeCaseInsensitiveRealSibling) {
    std::vector<guild::u8> ctx(guild::sim::kScriptContextStride + guild::sim::kScContextSlack, 0);
    const int S = guild::sim::kIncludeSlotStride;

    std::strcpy(reinterpret_cast<char*>(g_loadedA), "Quest_Intro.esc");
    std::strcpy(reinterpret_cast<char*>(g_loadedB), "Town_Market.esc");
    *reinterpret_cast<guild::u8**>(ctx.data() + 2492 + 0 * S) = g_loadedA;
    *reinterpret_cast<guild::u8**>(ctx.data() + 2492 + 1 * S) = g_loadedB;

    ScriptConsoleHooks h{};
    h.utilStrCmp = &RealStrCmpNoCase;   // REAL sibling wiring
    SetScriptConsoleHooks(&h);

    // Exact case resolves.
    guild::i32 exact = guild::sim::LookupInclude(ctx.data(), "Town_Market.esc");
    CHECK(exact != 0);
    // Different case ALSO resolves through the real case-insensitive comparator.
    guild::i32 folded = guild::sim::LookupInclude(ctx.data(), "town_MARKET.esc");
    CHECK(folded != 0);
    CHECK_EQ(exact, folded);   // same slot, same stored pointer value
    // A genuinely absent name misses even case-insensitively.
    guild::i32 miss = guild::sim::LookupInclude(ctx.data(), "Castle.esc");
    CHECK_EQ(miss, 0);

    SetScriptConsoleHooks(nullptr);
}

// ---------------------------------------------------------------------------
// ParseInclude registers a loaded context; a later LookupInclude finds it back via
// the real comparator — the two halves of the include machinery composed.
// ---------------------------------------------------------------------------
namespace {
guild::u8* LoadIntro(char*) {
    std::strcpy(reinterpret_cast<char*>(g_loadedA), "Quest_Intro.esc");
    return g_loadedA;
}
int g_compiled;
int CompileNoop(guild::u8*) { ++g_compiled; return 1; }
}

TEST(ScriptConsoleItest, ParseThenLookupComposeOverRealSibling) {
    std::vector<guild::u8> ctx(guild::sim::kScriptContextStride + guild::sim::kScContextSlack, 0);
    g_compiled = 0;

    ScriptConsoleHooks h{};
    h.nextToken = &Tok7;             // string-literal include path
    h.loadFromDir = &LoadIntro;
    h.compileBlock = &CompileNoop;
    h.utilStrCmp = &RealStrCmpNoCase; // REAL sibling for the subsequent lookup
    SetScriptConsoleHooks(&h);

    guild::i32 slot = guild::sim::ParseInclude(ctx.data());
    CHECK_EQ(slot, 0);
    CHECK_EQ(g_compiled, 1);

    // Now find the just-registered include back, case-folded, via the real compare.
    guild::i32 found = guild::sim::LookupInclude(ctx.data(), "QUEST_intro.esc");
    CHECK(found != 0);
    if (found) {
        // The resolved pointer's name is the loaded context's name.
        const char* name = reinterpret_cast<const char*>(g_loadedA);
        CHECK_EQ(guild::util::StrCmpNoCase(name, "quest_intro.esc"), 0);
    }

    SetScriptConsoleHooks(nullptr);
}
