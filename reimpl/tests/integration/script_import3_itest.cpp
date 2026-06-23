#include "test.h"
// Integration: the command-table registration layer (script_import3) wired
// against its REAL reconstructed sibling, VIBE_Script_FindCommandByName
// (script_import.cpp, gilde.exe 0x445b70).  ImportCommand calls FindCommandByName
// internally for its duplicate check; here we additionally drive the SAME real
// lookup against the table the Register* functions built and assert the
// cross-module flow agrees: every name we register is found, at the slot offset
// FindCommandByName reports, with the argc/kind ImportCommand wrote.
//
// FindCommandByName delegates to the real host::UtilStrCmp byte comparison the
// script_import slice owns, so this exercises the genuine live wiring (registrar
// -> table -> name lookup) end to end, not a mock.
#include "sim/script_import3.h"
#include "sim/script_import.h"   // real FindCommandByName (0x445b70)
#include "sim/script_vm.h"
#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {
// Command names are stored contiguously (the copy loop is an unrolled strcpy).
std::string ReadName(const u8* p) {
    return std::string(reinterpret_cast<const char*>(p));
}
} // namespace

// Register the core commands, then confirm the real FindCommandByName resolves
// every registered name to a record whose +0 name round-trips.
TEST(ScriptImport3Itest, RegisterThenFindRoundTrip) {
    ResetCommands();
    CHECK_EQ(RegisterCommands(), 1);

    const char* names[] = {
        "ecmd_Dummy", "Print", "PrintInt", "PrintFloat", "rnd", "GetTime",
        "RunScript", "RunScriptInt", "RunScriptString", "StopScript", "Sleep",
        "FindScript", "KillLocalScripts", "CallUserFunction",
        "CallUserFunctionExtended",
    };
    for (const char* n : names) {
        u8* rec = FindCommandByName(Commands().bytes, n);   // REAL sibling
        CHECK(rec != nullptr);
        if (rec) CHECK_EQ(ReadName(rec), std::string(n));
    }
    // A name that was never registered must NOT be found.
    CHECK(FindCommandByName(Commands().bytes, "NoSuchCommand") == nullptr);
    ResetCommands();
}

// The slot offset FindCommandByName returns must match the registration order
// (ImportCommand fills slots front-to-back; the real lookup walks the same
// 52-byte stride).
TEST(ScriptImport3Itest, FindReportsRegistrationSlot) {
    ResetCommands();
    static char f0, f1, f2;
    CHECK_EQ(ImportCommand("Alpha", &f0, 1, 0), 1);
    CHECK_EQ(ImportCommand("Beta",  &f1, 5, 0), 1);
    CHECK_EQ(ImportCommand("Gamma", &f2, 1, 0), 1);

    u8* base = Commands().bytes;
    u8* alpha = FindCommandByName(base, "Alpha");
    u8* beta  = FindCommandByName(base, "Beta");
    u8* gamma = FindCommandByName(base, "Gamma");
    CHECK(alpha && beta && gamma);
    if (alpha && beta && gamma) {
        CHECK_EQ(static_cast<long>(alpha - base), 0L);
        CHECK_EQ(static_cast<long>(beta  - base), static_cast<long>(kCommandStride));
        CHECK_EQ(static_cast<long>(gamma - base), static_cast<long>(2 * kCommandStride));
        // fn pointers and kinds survived the round trip through the real lookup.
        CHECK_EQ(CommandFn(beta), static_cast<ScriptCmdFn>(&f1));
        CHECK_EQ(beta[48], static_cast<u8>(5));
    }
    ResetCommands();
}

// The duplicate-rejection in ImportCommand is driven by the real
// FindCommandByName: re-registering an existing name returns 0 and leaves the
// original record's fn pointer untouched.
TEST(ScriptImport3Itest, DuplicateRejectionViaRealLookup) {
    ResetCommands();
    static char first, second;
    CHECK_EQ(ImportCommand("Sleep", &first, 5, 0), 1);
    CHECK_EQ(ImportCommand("Sleep", &second, 1, 0), 0);   // real lookup finds dup
    u8* rec = FindCommandByName(Commands().bytes, "Sleep");
    CHECK(rec != nullptr);
    if (rec) {
        CHECK_EQ(CommandFn(rec), static_cast<ScriptCmdFn>(&first));
        CHECK_EQ(rec[48], static_cast<u8>(5));            // original kind kept
    }
    ResetCommands();
}
