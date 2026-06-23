#include "test.h"
// Unit coverage for the command-table registration slice (script_import3):
//   ImportCommand / AddEventToken record layout, the Register* sequences, and
//   CmdCreateCharacterAtDummy's control flow.  Golden record offsets are taken
//   from the gilde.exe writes (52-byte command stride, 48-byte token stride).
#include "sim/script_import3.h"
#include "sim/script_import.h"   // FindCommandByName (verification)
#include "sim/script_vm.h"
#include <cstring>
#include <cstdio>
#include <string>

using namespace guild;
using namespace guild::sim;

namespace {
// The name is stored as a contiguous NUL-terminated byte string (the original's
// 2-bytes-per-iteration copy loop is an unrolled strcpy, not a wide copy).
std::string ReadName(const u8* p) {
    return std::string(reinterpret_cast<const char*>(p));
}
} // namespace

// ---- ImportCommand: first-slot insertion + full record layout ----
TEST(ScriptImport3, ImportCommandLayout) {
    ResetCommands();
    static char fnTok;   // unique identity token (never dereferenced)
    const u8 types[] = {1, 6};
    CHECK_EQ(ImportCommand("Sleep", &fnTok, 5, 2, types), 1);

    const u8* base = Commands().bytes;
    // Slot 0: name at +0, argc +32, types +36.., fn +44, kind +48.
    CHECK_EQ(ReadName(base), std::string("Sleep"));
    CHECK_EQ(*reinterpret_cast<const i32*>(base + 32), 2);
    CHECK_EQ(base[36], static_cast<u8>(1));
    CHECK_EQ(base[37], static_cast<u8>(6));
    CHECK_EQ(CommandFn(base), static_cast<ScriptCmdFn>(&fnTok));
    CHECK_EQ(base[48], static_cast<u8>(5));
}

// ---- ImportCommand: rejects names longer than 31, and duplicates ----
TEST(ScriptImport3, ImportCommandRejects) {
    ResetCommands();
    static char a, b;
    std::string tooLong(40, 'x');   // > 0x1F
    CHECK_EQ(ImportCommand(tooLong.c_str(), &a, 1, 0), 0);
    // No record written.
    CHECK_EQ(Commands().bytes[0], static_cast<u8>(0));

    CHECK_EQ(ImportCommand("Dup", &a, 1, 0), 1);
    CHECK_EQ(ImportCommand("Dup", &b, 1, 0), 0);   // duplicate name rejected
    // The original record's fn is untouched (still &a).
    u8* rec = FindCommandByName(Commands().bytes, "Dup");
    CHECK(rec != nullptr);
    if (rec) CHECK_EQ(CommandFn(rec), static_cast<ScriptCmdFn>(&a));
}

// ---- ImportCommand: sequential slots use 52-byte stride ----
TEST(ScriptImport3, ImportCommandStride) {
    ResetCommands();
    static char f0, f1, f2;
    CHECK_EQ(ImportCommand("Cmd0", &f0, 1, 0), 1);
    CHECK_EQ(ImportCommand("Cmd1", &f1, 1, 0), 1);
    CHECK_EQ(ImportCommand("Cmd2", &f2, 1, 0), 1);
    const u8* base = Commands().bytes;
    CHECK_EQ(ReadName(base + 0 * kCommandStride), std::string("Cmd0"));
    CHECK_EQ(ReadName(base + 1 * kCommandStride), std::string("Cmd1"));
    CHECK_EQ(ReadName(base + 2 * kCommandStride), std::string("Cmd2"));
    CHECK_EQ(CommandFn(base + 2 * kCommandStride), static_cast<ScriptCmdFn>(&f2));
}

// ---- AddEventToken: 48-byte record layout + growth ----
TEST(ScriptImport3, AddEventTokenLayout) {
    ResetEventTokens();
    ScriptHandle h = AddEventToken("SND_W2", 0x1234, 0x05);
    CHECK(h != 0);
    const u8* rec = reinterpret_cast<const u8*>(h);
    if (rec) {
        CHECK_EQ(rec[0], static_cast<u8>(0x05));                       // low type nibble
        CHECK_EQ(ReadName(rec + 1), std::string("SND_W2"));            // name at +1
        CHECK_EQ(*reinterpret_cast<const i32*>(rec + 36), 1);           // marker
        CHECK_EQ(*reinterpret_cast<const i32*>(rec + 40), 0);
        CHECK_EQ(*reinterpret_cast<const i32*>(rec + 44), 0x1234);      // handler
    }
    CHECK_EQ(EventTokens().count, 1u);
    // Capacity grew by one chunk (768 bytes) on the first insert.
    CHECK_EQ(EventTokens().capBytes, static_cast<u32>(kEventTokenGrowBytes));
    ResetEventTokens();
}

// ---- AddEventToken: single-chunk fill, stable record addrs ----
// gilde.exe 0x441148-0x44115b grows when `count + 48 > capBytes` (verified by
// disasm: `mov eax,count; add eax,30h; cmp eax,capBytes; ja grow`).  The grow
// chunk is 768 bytes = 16 records, so the FIRST insert grows once (0+48>0) and
// the table then holds 16 records (offsets 0..15*48, the 16th ending at 768)
// before the condition fires again — which only happens at count > 720.  The
// original therefore overflows its own buffer past 16 tokens; the game never
// registers that many in one table (RegisterSoundCommands adds 8).  This test
// fills exactly one chunk and confirms cap stays at one grow-chunk.
TEST(ScriptImport3, AddEventTokenGrowth) {
    ResetEventTokens();
    // Insert 16 records: fills one 768-byte chunk; no second growth (count+48
    // never exceeds 768 until count > 720).
    for (int i = 0; i < 16; ++i) {
        char nm[8];
        std::snprintf(nm, sizeof nm, "T%d", i);
        AddEventToken(nm, i, static_cast<u8>(i & 0xF));
    }
    CHECK_EQ(EventTokens().count, 16u);
    CHECK_EQ(EventTokens().capBytes, static_cast<u32>(kEventTokenGrowBytes)); // 768, one chunk
    // Records are contiguous at the 48-byte stride; spot-check the 16th.
    const u8* base = EventTokens().base;
    if (base) {
        const u8* r16 = base + 15 * kEventTokenStride;
        CHECK_EQ(*reinterpret_cast<const i32*>(r16 + 44), 15);
        CHECK_EQ(ReadName(r16 + 1), std::string("T15"));
    }
    ResetEventTokens();
}

// ---- RegisterCommands: registers the 15 core commands in order ----
TEST(ScriptImport3, RegisterCommandsCore) {
    ResetCommands();
    CHECK_EQ(RegisterCommands(), 1);
    // Spot-check a few of the well-known names and their argc/kind.
    u8* sleep = FindCommandByName(Commands().bytes, "Sleep");
    CHECK(sleep != nullptr);
    if (sleep) {
        CHECK_EQ(*reinterpret_cast<i32*>(sleep + 32), 1);   // argc 1
        CHECK_EQ(sleep[36], static_cast<u8>(1));            // int arg
        CHECK_EQ(sleep[48], static_cast<u8>(5));            // statement kind
    }
    u8* rnd = FindCommandByName(Commands().bytes, "rnd");    // 0x43c8be name = off_61688C = "rnd"
    CHECK(rnd != nullptr);
    if (rnd) CHECK_EQ(rnd[48], static_cast<u8>(1));          // function kind
    u8* dummy = FindCommandByName(Commands().bytes, "ecmd_Dummy");
    CHECK(dummy != nullptr);
    if (dummy) CHECK_EQ(*reinterpret_cast<i32*>(dummy + 32), 0);  // argc 0
    // CallUserFunctionExtended is the last and takes 3 int args.
    u8* cufe = FindCommandByName(Commands().bytes, "CallUserFunctionExtended");
    CHECK(cufe != nullptr);
    if (cufe) {
        CHECK_EQ(*reinterpret_cast<i32*>(cufe + 32), 3);
        CHECK_EQ(cufe[38], static_cast<u8>(1));
    }
    ResetCommands();
}

// ---- RegisterSoundCommands: 9 commands + 8 SND_* event tokens ----
TEST(ScriptImport3, RegisterSoundCommands) {
    ResetCommands();
    ResetEventTokens();
    CHECK_EQ(RegisterSoundCommands(), 1);
    CHECK(FindCommandByName(Commands().bytes, "PlaySample3D") != nullptr);
    u8* p3d = FindCommandByName(Commands().bytes, "PlaySample3D");
    if (p3d) CHECK_EQ(*reinterpret_cast<i32*>(p3d + 32), 4);   // argc 4
    CHECK(FindCommandByName(Commands().bytes, "SpeechQueued") != nullptr);
    // 8 SND_* tokens appended.
    CHECK_EQ(EventTokens().count, 8u);
    ResetCommands();
    ResetEventTokens();
}

// ---- RegisterObjectCommands: large table, distinct-arg spot checks ----
TEST(ScriptImport3, RegisterObjectCommands) {
    ResetCommands();
    CHECK_EQ(RegisterObjectCommands(), 1);
    u8* co = FindCommandByName(Commands().bytes, "CreateObject");
    CHECK(co != nullptr);
    if (co) {
        CHECK_EQ(*reinterpret_cast<i32*>(co + 32), 4);
        CHECK_EQ(co[36], static_cast<u8>(7));   // float
        CHECK_EQ(co[39], static_cast<u8>(6));   // string
    }
    u8* mo = FindCommandByName(Commands().bytes, "MoveObject");
    CHECK(mo != nullptr);
    if (mo) {
        CHECK_EQ(*reinterpret_cast<i32*>(mo + 32), 5);
        CHECK_EQ(mo[48], static_cast<u8>(5));   // statement kind
    }
    u8* ce = FindCommandByName(Commands().bytes, "CreateEmitter");
    CHECK(ce != nullptr);
    if (ce) CHECK_EQ(*reinterpret_cast<i32*>(ce + 32), 7);
    u8* last = FindCommandByName(Commands().bytes, "SelectAllTextureSets");
    CHECK(last != nullptr);
    if (last) {
        CHECK_EQ(last[36], static_cast<u8>(6));
        CHECK_EQ(last[37], static_cast<u8>(1));
    }
    ResetCommands();
}

// ---- CmdCreateCharacterAtDummy: invalid-dummy path returns 0 ----
TEST(ScriptImport3, CmdCreateCharAtDummyInvalid) {
    SetScriptCmdHooks(nullptr);
    SetScriptImport3Hooks(nullptr);
    ScriptHandle dummy = 0;
    char nameBuf[] = "x\0";   // not used on the invalid path
    char* name = nameBuf;
    CHECK_EQ(CmdCreateCharacterAtDummy(&dummy, &name), 0);
}

// ---- CmdCreateCharacterAtDummy: load-failure path returns the failed handle ----
TEST(ScriptImport3, CmdCreateCharAtDummyLoadFail) {
    // Default hooks: createFromModel returns 0 (load failed). A nonzero dummy
    // takes the create path, which fails -> returns 0.
    SetScriptImport3Hooks(nullptr);
    static u8 dummyObj[128] = {};
    ScriptHandle dummy = reinterpret_cast<ScriptHandle>(dummyObj);
    char nameBuf[] = "Bob";
    char* name = nameBuf;
    CHECK_EQ(CmdCreateCharacterAtDummy(&dummy, &name), 0);
}
