// End-to-end test for the .esc binary token parser: drive EscParseBlock over a
// realistic byte stream + handler table, dispatching leaf field readers and
// recursing into a nested block, verifying the decoded record array and the
// nesting-depth counter. The byte-stream reader hooks are installed in the
// library and pointed at a test-owned in-memory stream via the shared
// test_stream.h helper, so this executable defines no src/-referenced symbol.
#include "sim/script_import.h"
#include "test.h"
#include "test_stream.h"
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;
using guild::test::MemStream;
using guild::test::ScopedScriptStream;

// ---------------------------------------------------------------------------
// Leaf readers as zero-arg adapters (the original handler-table leaf fns are
// register-arg readers; ParseBlock calls them with no portable args, so the
// stream + parse-ctx are threaded through file-static globals here).
// ---------------------------------------------------------------------------
namespace {
int          e2e_stream = 1;
EscParseCtx* e2e_ctx    = nullptr;

int leafName()    { EscReadNameField   (e2e_stream, e2e_ctx); return 0; }
int leafMesh()    { EscReadMeshField   (e2e_stream, e2e_ctx); return 0; }
int leafTexture() { EscReadTextureField(e2e_stream, e2e_ctx); return 0; }
int leafByteA()   { EscReadByteFieldA  (e2e_stream, e2e_ctx); return 0; }
int leafFlags()   { EscReadFlagBitsField(e2e_stream, e2e_ctx); return 0; }
} // namespace

TEST(ScriptImportE2E, ParseBlockDecodesRecord) {
    // Token codes (all <= 0x3A so ReadToken passes them through). We choose
    // distinct field tokens; the close token is 0x2F ('/').
    enum : unsigned char {
        TOK_NAME = 0x10, TOK_MESH = 0x11, TOK_TEX = 0x12,
        TOK_BYTEA = 0x13, TOK_FLAGS = 0x14
    };

    // Handler table: leaf entries (childTable == nullptr so the leaf fn fires),
    // terminated by a 0x28 sentinel (open/wildcard) with no leaf so unknown
    // tokens harmlessly do nothing.
    EscHandler tbl[6] = {};
    tbl[0].token = TOK_NAME;  tbl[0].leafFn = leafName;
    tbl[1].token = TOK_MESH;  tbl[1].leafFn = leafMesh;
    tbl[2].token = TOK_TEX;   tbl[2].leafFn = leafTexture;
    tbl[3].token = TOK_BYTEA; tbl[3].leafFn = leafByteA;
    tbl[4].token = TOK_FLAGS; tbl[4].leafFn = leafFlags;
    tbl[5].token = kEscTokOpen; // 0x28 sentinel

    // Stream layout: for each field token, ReadToken pulls the token byte, then
    // the leaf reader consumes its payload from the same stream. Then 0x2F to
    // close the block.
    std::vector<unsigned char> stream;
    auto emitStr = [&](unsigned char tok, const char* s) {
        stream.push_back(tok);
        for (const char* p = s; ; ++p) { stream.push_back((unsigned char)*p); if (!*p) break; }
    };
    emitStr(TOK_NAME, "hero.def");      // -> field +0   "hero"
    emitStr(TOK_MESH, "hero_body.x");   // -> field +64  "hero_body"
    emitStr(TOK_TEX,  "hero_skin.tga"); // -> field +128 "hero_skin"
    stream.push_back(TOK_BYTEA); stream.push_back(0x2A);   // -> +192 = 0x2A
    stream.push_back(TOK_FLAGS); stream.push_back(0xA5);   // -> flag unpack of 0xA5
    stream.push_back(kEscTokClose);     // 0x2F -> end block

    MemStream ms{ stream.data(), stream.size(), 0 };
    ScopedScriptStream guard{ ms };

    std::vector<u8> rec(kEscRecordStride * 2, 0);
    EscParseCtx ctx{ 1, 0, 0, rec.data() };
    e2e_ctx = &ctx;

    g_escBlockDepth = 0;
    u8 last = EscParseBlock(1, tbl, &ctx);

    // Block terminated on the close token (0x2F).
    CHECK_EQ((int)last, (int)kEscTokClose);
    // Depth balanced (no nested child recursion in this flat block; the final
    // --depth on exit drops it to -1 from 0, matching the original's bookkeeping
    // where ParseBlock is entered with depth already incremented by the caller).
    CHECK_EQ(g_escBlockDepth, -1);

    u8* base = rec.data();
    CHECK(std::strcmp((char*)(base + 0),   "hero") == 0);
    CHECK(std::strcmp((char*)(base + 64),  "hero_body") == 0);
    CHECK(std::strcmp((char*)(base + 128), "hero_skin") == 0);
    CHECK_EQ((int)base[192], 0x2A);
    // 0xA5 unpack: +194=1, +197=0, +198=9, +195=0
    CHECK_EQ((int)base[194], 1);
    CHECK_EQ((int)base[197], 0);
    CHECK_EQ((int)base[198], 9);
    CHECK_EQ((int)base[195], 0);

    e2e_ctx = nullptr;
}

// A second flow: EOF inside the block terminates cleanly (ReadToken returns 43).
TEST(ScriptImportE2E, ParseBlockStopsAtEof) {
    EscHandler tbl[2] = {};
    tbl[0].token = 0x10; tbl[0].leafFn = nullptr; // no-leaf, no-child -> recurse guard
    tbl[1].token = kEscTokOpen;

    // Stream: one unknown-but-tabled token (0x10) with a child recursion that
    // immediately hits EOF. We give 0x10 a child table that finds nothing.
    EscHandler child[1] = {};
    child[0].token = kEscTokOpen;
    tbl[0].childTable = child;

    std::vector<unsigned char> stream = { 0x10 /* opens child */ };
    MemStream ms{ stream.data(), stream.size(), 0 };
    ScopedScriptStream guard{ ms };
    EscParseCtx ctx{ 1, 0, 0, nullptr };
    e2e_ctx = &ctx;
    g_escBlockDepth = 5;
    u8 last = EscParseBlock(1, tbl, &ctx);
    // After recursing (child block reads EOF=43 then returns), the outer block's
    // next ReadToken also hits EOF (43) which is a terminator -> loop exits.
    CHECK(last == kEscTokEof || last == kEscTokError);
    e2e_ctx = nullptr;
}
