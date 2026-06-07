// Unit tests for src/sim/script_import.cpp — the .esc binary token parser and
// the script context/command table scan helpers. Golden vectors computed with
// python3 (bit-unpacking) and hand-derived expected scan results.
#include "sim/script_import.h"
#include "test.h"
#include <cstring>
#include <string>
#include <vector>

#include "test_stream.h"

using namespace guild;
using namespace guild::sim;

// A tiny in-memory byte stream backs the VFS reads so EscReadToken / the field
// readers behave deterministically. The reader hooks (installed in the library)
// route the opaque `int` stream handle back to the test-owned MemStream via the
// shared test_stream.h helper — no library symbol (no bare g_stream) is defined
// here, so both this TU and the e2e TU link without touching src/ symbols.
using guild::test::MemStream;
using guild::test::ScopedScriptStream;

// ---------------------------------------------------------------------------
// EscReadToken
// ---------------------------------------------------------------------------
TEST(ScriptImport, EscReadToken_basic) {
    unsigned char bytes[] = { 0x05, 0x3A, 0x3B, 0x40 }; // 5, 0x3A(ok), 0x3B(>max), 0x40(>max)
    MemStream s{ bytes, sizeof(bytes), 0 };
    ScopedScriptStream guard{ s };
    CHECK_EQ((int)EscReadToken(1), 5);            // raw byte passthrough
    CHECK_EQ((int)EscReadToken(1), 0x3A);         // exactly max -> passthrough
    CHECK_EQ((int)EscReadToken(1), (int)kEscTokError);  // 0x3B > 0x3A -> 39
    CHECK_EQ((int)EscReadToken(1), (int)kEscTokError);  // 0x40 -> 39
    CHECK_EQ((int)EscReadToken(1), (int)kEscTokEof);    // EOF -> 43
}

// ---------------------------------------------------------------------------
// EscFindTokenHandler
// ---------------------------------------------------------------------------
TEST(ScriptImport, EscFindTokenHandler) {
    EscHandler tbl[4] = {};
    tbl[0].token = 0x10;
    tbl[1].token = 0x11;
    tbl[2].token = 0x12;
    tbl[3].token = kEscTokOpen; // 0x28 sentinel
    CHECK_EQ(EscFindTokenHandler(0x10, tbl), 0);
    CHECK_EQ(EscFindTokenHandler(0x12, tbl), 2);
    // 0x28 matches the sentinel directly (loop exits with index 3).
    CHECK_EQ(EscFindTokenHandler(0x28, tbl), 3);
    // 0x99 not present: scan reaches the 0x28 sentinel -> 39 (not-found).
    CHECK_EQ(EscFindTokenHandler(0x99, tbl), (int)kEscTokError);
}

// ---------------------------------------------------------------------------
// Field readers — record array at base, stride 224.
// ---------------------------------------------------------------------------
TEST(ScriptImport, EscReadStringFields) {
    // "knight.msh\0" — UtilStrChr truncates at the *last* '.', then copy stops
    // at the NUL. Expect "knight" in the field.
    unsigned char nm[] = "knight.msh";
    MemStream s{ nm, sizeof(nm), 0 };
    ScopedScriptStream guard{ s };
    std::vector<u8> rec(kEscRecordStride * 4, 0xCC);
    EscParseCtx ctx{ 1, 1, 0, rec.data() }; // index 1
    EscReadNameField(1, &ctx);
    u8* field = rec.data() + kEscRecordStride * 1 + 0;
    CHECK(std::strcmp((char*)field, "knight") == 0);
}

TEST(ScriptImport, EscReadMeshTextureOffsets) {
    std::vector<u8> rec(kEscRecordStride * 2, 0);
    EscParseCtx ctx{ 1, 0, 0, rec.data() };

    unsigned char mesh[] = "body.x";
    {
        MemStream s{ mesh, sizeof(mesh), 0 };
        ScopedScriptStream guard{ s };
        EscReadMeshField(1, &ctx);
    }
    CHECK(std::strcmp((char*)(rec.data() + 64), "body") == 0);

    unsigned char tex[] = "skin.tga";
    {
        MemStream s2{ tex, sizeof(tex), 0 };
        ScopedScriptStream guard{ s2 };
        EscReadTextureField(1, &ctx);
    }
    CHECK(std::strcmp((char*)(rec.data() + 128), "skin") == 0);
}

TEST(ScriptImport, EscReadByteFields) {
    unsigned char bytes[] = { 0xAB, 0xCD };
    MemStream s{ bytes, sizeof(bytes), 0 };
    ScopedScriptStream guard{ s };
    std::vector<u8> rec(kEscRecordStride, 0);
    EscParseCtx ctx{ 1, 0, 0, rec.data() };
    EscReadByteFieldA(1, &ctx);
    EscReadByteFieldB(1, &ctx);
    CHECK_EQ((int)rec[192], 0xAB);
    CHECK_EQ((int)rec[193], 0xCD);
}

// Golden vectors for flag-bit unpacking (computed with python3).
TEST(ScriptImport, EscReadFlagBitsField_golden) {
    struct Case { u8 in; u8 b194, b197, b198, b195; };
    Case cases[] = {
        {0x00, 0, 0, 0,  0},
        {0xFF, 1, 1, 15, 1},
        {0xA5, 1, 0, 9,  0},
        {0x3C, 0, 0, 15, 0},
        {0x80, 0, 0, 0,  0},
        {0x7E, 0, 1, 15, 1},
    };
    for (auto& c : cases) {
        unsigned char b[] = { c.in };
        MemStream s{ b, 1, 0 };
        ScopedScriptStream guard{ s };
        std::vector<u8> rec(kEscRecordStride, 0);
        EscParseCtx ctx{ 1, 0, 0, rec.data() };
        EscReadFlagBitsField(1, &ctx);
        CHECK_EQ((int)rec[194], (int)c.b194);
        CHECK_EQ((int)rec[197], (int)c.b197);
        CHECK_EQ((int)rec[198], (int)c.b198);
        CHECK_EQ((int)rec[195], (int)c.b195);
    }
}

TEST(ScriptImport, EscReadFlagBitsField2_golden) {
    struct Case { u8 in; u8 b196, b199; };
    Case cases[] = {
        {0x00, 0, 0},
        {0xFF, 1, 254},
        {0xA5, 1, 164},
        {0x3C, 0, 60},
        {0x80, 0, 128},
        {0x7E, 0, 126},
    };
    for (auto& c : cases) {
        unsigned char b[] = { c.in };
        MemStream s{ b, 1, 0 };
        ScopedScriptStream guard{ s };
        std::vector<u8> rec(kEscRecordStride, 0);
        EscParseCtx ctx{ 1, 0, 0, rec.data() };
        EscReadFlagBitsField2(1, &ctx);
        CHECK_EQ((int)rec[196], (int)c.b196);
        CHECK_EQ((int)rec[199], (int)c.b199);
    }
}

TEST(ScriptImport, EscIncrementCounter) {
    EscParseCtx ctx{ 1, 5, 0, nullptr };
    EscIncrementCounter(&ctx);
    CHECK_EQ(ctx.recordIndex, 6);
    EscIncrementCounter(&ctx);
    CHECK_EQ(ctx.recordIndex, 7);
}

// ---------------------------------------------------------------------------
// Context / command table scan helpers.
// ---------------------------------------------------------------------------
static std::vector<u8> MakeCtxTable() {
    std::vector<u8> t(kScriptContextStride * kScriptContextCount, 0);
    // init all handles to -1 (free)
    for (int i = 0; i < kScriptContextCount; ++i)
        *reinterpret_cast<i32*>(t.data() + i * kScriptContextStride + kScHandle) = -1;
    return t;
}

TEST(ScriptImport, FindByHandle) {
    auto t = MakeCtxTable();
    *reinterpret_cast<i32*>(t.data() + 3 * kScriptContextStride + kScHandle) = 42;
    CHECK(FindByHandle(t.data(), 42) == t.data() + 3 * kScriptContextStride);
    CHECK(FindByHandle(t.data(), 999) == nullptr);
    CHECK(FindByHandle(t.data(), -1) == nullptr);  // -1 always nullptr
}

TEST(ScriptImport, FindByName) {
    auto t = MakeCtxTable();
    std::strcpy((char*)(t.data() + 5 * kScriptContextStride), "Tavern");
    CHECK(FindByName(t.data(), "tavern") == t.data() + 5 * kScriptContextStride); // case-insensitive
    CHECK(FindByName(t.data(), "TAVERN") == t.data() + 5 * kScriptContextStride);
    CHECK(FindByName(t.data(), "nope") == nullptr);
}

TEST(ScriptImport, FindCommandByName) {
    std::vector<u8> t(kCommandStride * kCommandCapacity, 0);
    std::strcpy((char*)(t.data() + 7 * kCommandStride), "WalkTo");
    CHECK(FindCommandByName(t.data(), "WalkTo") == t.data() + 7 * kCommandStride);
    CHECK(FindCommandByName(t.data(), "missing") == nullptr);
}

// (CountActive / FreeFinished / SkipToSemicolon are owned by script_vm.cpp /
//  script_compiler.cpp and tested there — not duplicated here.)

TEST(ScriptImport, FindLogicalOperator) {
    const char* a = "x > 3 && y < 5";
    CHECK(FindLogicalOperator(a, a + std::strlen(a)) == a + 6);   // points at "&&"
    const char* b = "p || q";
    CHECK(FindLogicalOperator(b, b + std::strlen(b)) == b + 2);
    const char* c = "x > 3";   // no logical op
    CHECK(FindLogicalOperator(c, c + std::strlen(c)) == nullptr);
    const char* d = "&x";      // leading '&' -> bail
    CHECK(FindLogicalOperator(d, d + std::strlen(d)) == nullptr);
    const char* e = "a && b) || c"; // stops at ')' before the "||"
    CHECK(FindLogicalOperator(e, e + std::strlen(e)) == e + 2);   // finds first "&&"
}

TEST(ScriptImport, ConstantStubs) {
    CHECK_EQ(CmdReturnTrue(), 1);
    CHECK_EQ(CmdReturnFalse(), 0);
    CHECK_EQ(NullStub(), 0);
    i32 h = 5;
    ResetCurrentHandle(&h);
    CHECK_EQ(h, -1);
}
