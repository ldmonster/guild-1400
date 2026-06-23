#include "tests/framework/test.h"
#include "sim/history_parse.h"
#include "sim/command_recon4_resolve.h"
#include "world/history_text_pass.h"

#include <cstring>
#include <string>

// Golden-vector tests for VIBE_History_ParseContext (gilde.exe 0x4fd44c) and the
// funcs_4FD5D2 dispatch table (0x6343d8): table slot map, prefix-mode parse,
// slot-digit parse, role-name scan, the params back-writes, the failure paths,
// and the live dispatch into the reconstructed ResolveTarget* resolvers plus the
// wiring into world::HistoryParseTextSecondPass (the 0x4fdcec caller).

using namespace guild;
using namespace guild::sim;

namespace {

// ---------------------------------------------------------------------------
// Recording hook used for the 5 pending funcs_4FD5D2 slots (0,1,7,8,9).
// ---------------------------------------------------------------------------
struct HookLog {
    int  calls = 0;
    int  lastSlot = -1;       // which trampoline fired (0/1/7/8/9)
    u8   kind = 0xFF;
    u8*  params = nullptr;
    std::string name;
    int  index = -999;
    char* out = nullptr;
    int  ret = 1;             // value the hook returns
};
HookLog* g_log = nullptr;

template <int Slot>
int RecordingHook(u8 kind, u8* params, const char* name, int index, char* out) {
    g_log->calls++;
    g_log->lastSlot = Slot;
    g_log->kind = kind;
    g_log->params = params;
    g_log->name = name ? name : "<null>";
    g_log->index = index;
    g_log->out = out;
    return g_log->ret;
}

void installHistHooks(HookLog& log) {
    g_log = &log;
    HistoryParseHooks h;
    h.resolveGuard        = &RecordingHook<0>;
    h.resolveOfficial     = &RecordingHook<1>;
    h.resolveBestThief    = &RecordingHook<7>;
    h.resolveSelectedStat = &RecordingHook<8>;
    h.resolveBuildingStat = &RecordingHook<9>;
    SetHistoryParseHooks(&h);
}

// A valid 8-slot group row: dword[0] = group-valid gate, then 8-byte slot
// records (entity id at 8*i+4, role byte at 8*i+8).
struct GroupRow {
    u8 bytes[4 + 8 * 8 + 8] = {};
    GroupRow() { setValid(1); }
    void setValid(u32 v) { std::memcpy(bytes, &v, 4); }
    u32 valid() const { u32 v; std::memcpy(&v, bytes, 4); return v; }
    i32 id(int slot) const { i32 v; std::memcpy(&v, bytes + 8 * slot + 4, 4); return v; }
    void setRole(int slot, u8 r) { bytes[8 * slot + 8] = r; }
    u8 role(int slot) const { return bytes[8 * slot + 8]; }
};

// ---------------------------------------------------------------------------
// Recon4ResolveHooks scaffolding for the real-resolver dispatch tests.
// ---------------------------------------------------------------------------
struct ResolveEnv {
    Recon4Record slot[768];
    std::string lastParsedName;
    int parseCalls = 0;
    u16 renderedWord = 0xFFFF;
    int renderCalls = 0;
    const char* renderText = nullptr;  // text renderMessage writes into `out`
};
ResolveEnv* g_env = nullptr;

bool EnvReadRecord(int i, Recon4Record* outRec) {
    if (i < 0 || i >= 768) return false;
    *outRec = g_env->slot[i];
    return true;
}
void EnvParseTokens(const char* name, int* tc, i32* v) {
    g_env->parseCalls++;
    g_env->lastParsedName = name ? name : "<null>";
    *tc = 1;
    *v = 0;
}
int  EnvNotInSel(i32) { return 1; }
int  EnvRand(u16) { return 0; }
void EnvRender(char* out, const char*, u16 w) {
    g_env->renderedWord = w;
    g_env->renderCalls++;
    if (out && g_env->renderText) std::strcpy(out, g_env->renderText);
}
// kind==1 confirm-path leaves: one fake person record.
i32 g_fakeRecId = 0;
u8  g_fakeRec[512];
void* EnvFindById(i32 id) { return id == g_fakeRecId ? g_fakeRec : nullptr; }
u8   EnvRecFlag(void*, int) { return 1; }
i32  EnvRecWord(void*) { return 77; }

void installResolveEnv(ResolveEnv& env) {
    g_env = &env;
    Recon4ResolveHooks h{};
    h.readRecord = &EnvReadRecord;
    h.parseTokens = &EnvParseTokens;
    h.isNotInSelectionList = &EnvNotInSel;
    h.randomModulo = &EnvRand;
    h.renderMessage = &EnvRender;
    h.findRecordById = &EnvFindById;
    h.recordFlag = &EnvRecFlag;
    h.recordTypeWord = &EnvRecWord;
    SetRecon4ResolveHooks(&h);
}

void resetAllHooks() {
    SetHistoryParseHooks(nullptr);
    SetRecon4ResolveHooks(nullptr);
    g_log = nullptr;
    g_env = nullptr;
}

} // namespace

// ===========================================================================
// Table map — funcs_4FD5D2 @0x6343d8, slot-for-slot.
// ===========================================================================
TEST(HistParseCtx, TableMapsTenReconstructedResolvers) {
    CHECK(HistoryResolverSlot(2)  == &ResolveTargetClergy);             // 0x4f9238
    CHECK(HistoryResolverSlot(3)  == &ResolveTargetPersonByName);       // 0x4f9518
    CHECK(HistoryResolverSlot(4)  == &ResolveTargetPersonAlt);          // 0x4f989c
    CHECK(HistoryResolverSlot(5)  == &ResolveTargetPersonScoped);       // 0x4f9c20
    CHECK(HistoryResolverSlot(6)  == &ResolveTargetBestRated);          // 0x4f9f74
    CHECK(HistoryResolverSlot(10) == &ResolveTargetCraftWorker);        // 0x4fa50c
    CHECK(HistoryResolverSlot(11) == &ResolveTargetByStatGroup);        // 0x4fa818
    CHECK(HistoryResolverSlot(12) == &ResolveTargetWoundedPerson);      // 0x4faab8
    CHECK(HistoryResolverSlot(13) == &ResolveTargetByProfessionRange);  // 0x4fac80
    CHECK(HistoryResolverSlot(14) == &ResolveTargetRandomCarried);      // 0x4faf54
}

TEST(HistParseCtx, TableBoundsAndHookSlotsNonNull) {
    CHECK(HistoryResolverSlot(-1) == nullptr);
    CHECK(HistoryResolverSlot(15) == nullptr);
    // Pending slots 0/1/7/8/9 are live trampolines, never null.
    CHECK(HistoryResolverSlot(0) != nullptr);   // 0x4f8fac pending
    CHECK(HistoryResolverSlot(1) != nullptr);   // 0x4f90e0 pending
    CHECK(HistoryResolverSlot(7) != nullptr);   // 0x4fa178 pending
    CHECK(HistoryResolverSlot(8) != nullptr);   // 0x4fa290 pending
    CHECK(HistoryResolverSlot(9) != nullptr);   // 0x4fa3bc pending
}

// ===========================================================================
// Parse edge cases.
// ===========================================================================
TEST(HistParseCtx, NullTokenOrOutReturnsZero) {
    char out[64] = {};
    GroupRow row;
    CHECK_EQ(HistoryParseContext(nullptr, out, row.bytes), 0);
    CHECK_EQ(HistoryParseContext("_GELD", nullptr, row.bytes), 0);
    resetAllHooks();
}

TEST(HistParseCtx, WrongGroupReference) {
    HookLog log; installHistHooks(log);
    char out[64] = {};
    // _NEW with a null group row -> "Wrong group reference", no dispatch.
    CHECK_EQ(HistoryParseContext("_NEW%0%GELD", out, nullptr), 0);
    CHECK_EQ(log.calls, 0);
    // _NEW with *(u32*)params == 0 -> same failure.
    GroupRow row; row.setValid(0);
    CHECK_EQ(HistoryParseContext("_NEW%0%GELD", out, row.bytes), 0);
    CHECK_EQ(log.calls, 0);
    resetAllHooks();
}

TEST(HistParseCtx, WrongSlotReference) {
    HookLog log; installHistHooks(log);
    char out[64] = {};
    GroupRow row;
    // token[5] = '8' -> slot 8 >= 8 -> "Wrong slot reference", no dispatch.
    CHECK_EQ(HistoryParseContext("_NEW%8%GELD", out, row.bytes), 0);
    CHECK_EQ(log.calls, 0);
    CHECK_EQ(HistoryParseContext("_USE%9%GELD", out, row.bytes), 0);
    CHECK_EQ(log.calls, 0);
    resetAllHooks();
}

TEST(HistParseCtx, UnknownReplacement) {
    HookLog log; installHistHooks(log);
    char out[64] = {};
    GroupRow row;
    // _NEW with a name not in the 15-entry table -> repl stays 15 -> failure.
    CHECK_EQ(HistoryParseContext("_NEW%0%FOOBAR", out, row.bytes), 0);
    CHECK_EQ(log.calls, 0);
    // Literal token not in the table -> same.
    CHECK_EQ(HistoryParseContext("_XYZZY", out, row.bytes), 0);
    CHECK_EQ(log.calls, 0);
    resetAllHooks();
}

// ===========================================================================
// Prefix mode + slot digit + role-name scan + back-writes.
// ===========================================================================
TEST(HistParseCtx, LiteralTokenDispatchesKind4NoSlot) {
    HookLog log; installHistHooks(log);
    char out[64] = {};
    // No prefix match -> mode 4, slot -1, scan from token+1; params unchecked.
    CHECK_EQ(HistoryParseContext("_BUERGERMEISTER", out, nullptr), 1);
    CHECK_EQ(log.calls, 1);
    CHECK_EQ(log.lastSlot, 0);                  // BUERGERMEISTER -> slot 0
    CHECK_EQ((int)log.kind, 4);                 // v16 default
    CHECK_EQ(log.index, -1);                    // v13 stays 0xFFFFFFFF
    CHECK(log.params == nullptr);
    CHECK(log.name.empty());                    // tail past the matched keyword
    CHECK(log.out == out);
    resetAllHooks();
}

TEST(HistParseCtx, LiteralKeywordsMapToHookSlots) {
    HookLog log; installHistHooks(log);
    char out[64] = {};
    HistoryParseContext("_BISCHOF", out, nullptr);
    CHECK_EQ(log.lastSlot, 1);
    HistoryParseContext("_BESTES_WIRTSHAUS", out, nullptr);
    CHECK_EQ(log.lastSlot, 7);
    HistoryParseContext("_GELD", out, nullptr);
    CHECK_EQ(log.lastSlot, 8);
    HistoryParseContext("_STADTKASSE", out, nullptr);
    CHECK_EQ(log.lastSlot, 9);
    CHECK_EQ(log.calls, 4);
    resetAllHooks();
}

TEST(HistParseCtx, NewWritesRoleByteAndDispatchesKind0) {
    HookLog log; installHistHooks(log);
    char out[64] = {};
    GroupRow row;
    // "_NEW%3%GELD": slot digit token[5]='3'; scan at token+7 matches GELD (8);
    // _NEW back-writes the role byte at params[8*3+8].
    CHECK_EQ(HistoryParseContext("_NEW%3%GELD", out, row.bytes), 1);
    CHECK_EQ(log.lastSlot, 8);
    CHECK_EQ((int)log.kind, 0);
    CHECK_EQ(log.index, 3);
    CHECK(log.params == row.bytes);
    CHECK(log.name.empty());                    // tail past "GELD"
    CHECK_EQ((int)row.role(3), 8);              // mov [params+8*3+8], 8
    resetAllHooks();
}

TEST(HistParseCtx, NewTailPastKeywordCarriesModeArgs) {
    HookLog log; installHistHooks(log);
    char out[64] = {};
    GroupRow row;
    // The resolver receives the token tail AFTER the matched keyword (here the
    // "--2" StripNameTokens/ParseInt mode argument).
    CHECK_EQ(HistoryParseContext("_NEW%0%STADTKASSE--2", out, row.bytes), 1);
    CHECK_EQ(log.lastSlot, 9);
    CHECK_EQ((int)log.kind, 0);
    CHECK_EQ(log.name, std::string("--2"));
    CHECK_EQ((int)row.role(0), 9);
    resetAllHooks();
}

TEST(HistParseCtx, UseReadsRoleByteBackKind1) {
    HookLog log; installHistHooks(log);
    char out[64] = {};
    GroupRow row;
    row.setRole(5, 9);                          // previously _NEW'd STADTKASSE
    // "_USE%5%X": mode 1 skips the name scan entirely; repl = params[8*5+8];
    // the resolver's name arg is token+6 (NOT advanced past any keyword).
    CHECK_EQ(HistoryParseContext("_USE%5%X", out, row.bytes), 1);
    CHECK_EQ(log.lastSlot, 9);
    CHECK_EQ((int)log.kind, 1);
    CHECK_EQ(log.index, 5);
    CHECK_EQ(log.name, std::string("%X"));
    resetAllHooks();
}

TEST(HistParseCtx, RelDispatchesKind2WithoutBackWrite) {
    HookLog log; installHistHooks(log);
    char out[64] = {};
    GroupRow row;
    row.setRole(2, 0xAA);
    log.ret = 0;                                // resolver fails the token
    CHECK_EQ(HistoryParseContext("_REL%2%GELD", out, row.bytes), 0);
    CHECK_EQ((int)log.kind, 2);
    CHECK_EQ(log.index, 2);
    CHECK_EQ((int)row.role(2), 0xAA);           // _REL never writes the role byte
    CHECK_EQ(row.valid(), 0u);                  // failed resolve zeroes *params
    resetAllHooks();
}

TEST(HistParseCtx, SuccessKeepsGroupValid) {
    HookLog log; installHistHooks(log);
    char out[64] = {};
    GroupRow row;
    CHECK_EQ(HistoryParseContext("_NEW%0%GELD", out, row.bytes), 1);
    CHECK_EQ(row.valid(), 1u);                  // nonzero result: *params untouched
    resetAllHooks();
}

TEST(HistParseCtx, RoleScanOrderPrefersEarlierTableRow) {
    HookLog log; installHistHooks(log);
    char out[64] = {};
    ResolveEnv env; installResolveEnv(env);
    // RND_AMTSTRAEGERIN (row 3) is tested before RND_AMTSTRAEGER (row 4); a
    // token continuing with 'X' fails row 3's full-length memcmp and falls to
    // row 4 (prefix match), leaving "X" as the resolver's name arg.
    HistoryParseContext("_RND_AMTSTRAEGERX", out, nullptr);
    CHECK_EQ(env.parseCalls, 1);                // ResolveTargetPersonAlt (row 4) ran
    CHECK_EQ(env.lastParsedName, std::string("X"));
    resetAllHooks();
}

// ===========================================================================
// Live dispatch into the reconstructed resolvers (the actual rule-13 wiring).
// ===========================================================================
TEST(HistParseCtx, NewDispatchesIntoRealWoundedPersonResolver) {
    HookLog log; installHistHooks(log);
    ResolveEnv env; installResolveEnv(env);
    // Slot 5 of the selection table: a wounded (statusKind 0), alive person.
    env.slot[5].typeWord = 77;
    env.slot[5].kind = 0;
    env.slot[5].alive = 1;
    env.slot[5].entityId = 4242;

    char out[64] = {};
    GroupRow row;
    // RND_NPC_EINWOHNER -> funcs_4FD5D2[12] == ResolveTargetWoundedPerson
    // (0x4faab8); kind 0 back-writes the chosen entity id at params+8*0+4.
    CHECK_EQ(HistoryParseContext("_NEW%0%RND_NPC_EINWOHNER--0", out, row.bytes), 1);
    CHECK_EQ(log.calls, 0);                     // no hook slot involved
    CHECK_EQ(env.lastParsedName, std::string("--0"));
    CHECK_EQ(env.renderedWord, (u16)77);
    CHECK_EQ(row.id(0), 4242);                  // resolver back-write (8*0+4)
    CHECK_EQ((int)row.role(0), 12);             // ParseContext back-write (8*0+8)
    CHECK_EQ(row.valid(), 1u);
    resetAllHooks();
}

TEST(HistParseCtx, UseConfirmChainsThroughRealResolver) {
    HookLog log; installHistHooks(log);
    ResolveEnv env; installResolveEnv(env);
    char out[64] = {};
    GroupRow row;
    // Group state as left by the _NEW above: id 4242 in slot 0, role byte 12.
    std::memcpy(row.bytes + 4, "\x92\x10\x00\x00", 4);  // 4242
    row.setRole(0, 12);
    g_fakeRecId = 4242;
    // _USE re-dispatches slot 12 with kind 1: the confirm path re-resolves the
    // stored id via FindRecordById and renders the record's type word (77).
    CHECK_EQ(HistoryParseContext("_USE%0%--0", out, row.bytes), 1);
    CHECK_EQ(env.renderedWord, (u16)77);
    CHECK_EQ(env.renderCalls, 1);
    g_fakeRecId = 0;
    resetAllHooks();
}

TEST(HistParseCtx, RelIntoRealResolverRejectsAndZeroesGroup) {
    ResolveEnv env; installResolveEnv(env);
    char out[64] = {};
    GroupRow row;
    // RND_SPIELER -> funcs_4FD5D2[14] == ResolveTargetRandomCarried (0x4faf54),
    // which rejects kind==2 outright; ParseContext then zeroes *params.
    CHECK_EQ(HistoryParseContext("_REL%0%RND_SPIELER--1", out, row.bytes), 0);
    CHECK_EQ(row.valid(), 0u);
    resetAllHooks();
}

TEST(HistParseCtx, DefaultHooksAreInertOnPendingSlots) {
    resetAllHooks();                            // inert defaults everywhere
    char out[64] = {};
    GroupRow row;
    // Pending slot 0 (0x4f8fac): default hook returns 0; *params zeroed.
    CHECK_EQ(HistoryParseContext("_NEW%0%BUERGERMEISTER", out, row.bytes), 0);
    CHECK_EQ(row.valid(), 0u);
}

// ===========================================================================
// Wiring into the in-tree caller: world::HistoryParseTextSecondPass (0x4fdcec).
// ===========================================================================
TEST(HistParseCtx, SecondPassResolvesThroughRealParseContext) {
    HookLog log; installHistHooks(log);
    ResolveEnv env; installResolveEnv(env);
    env.slot[5].typeWord = 77;
    env.slot[5].kind = 0;
    env.slot[5].alive = 1;
    env.slot[5].entityId = 4242;
    env.renderText = "Hans";                    // what RenderFormattedMessage emits

    GroupRow row;
    std::string out;
    auto resolver = MakeHistorySubstResolver(row.bytes);
    // Literal token "_RND_NPC_EINWOHNER--0": mode 4 -> slot 12 (WoundedPerson),
    // which renders "Hans" into ParseContext's out buffer; the second pass
    // splices it back into the label text.
    auto rc = guild::world::HistoryParseTextSecondPass(
        "Der _RND_NPC_EINWOHNER--0 kam.", out, resolver);
    CHECK(rc == guild::world::HistoryTextResult::kOk);
    CHECK_EQ(out, std::string("Der Hans kam."));
    resetAllHooks();
}

TEST(HistParseCtx, SecondPassAbortsWhenParseContextFails) {
    resetAllHooks();                            // inert defaults: every slot fails
    GroupRow row;
    std::string out;
    auto resolver = MakeHistorySubstResolver(row.bytes);
    auto rc = guild::world::HistoryParseTextSecondPass(
        "Der _BUERGERMEISTER--0 kam.", out, resolver);
    CHECK(rc == guild::world::HistoryTextResult::kSyntaxErr);
    CHECK_EQ(row.valid(), 0u);                  // the failed resolve zeroed the gate
}
