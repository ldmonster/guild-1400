// See wire_selinput.h. Binds the two wireable command_apply9 selection/check
// hook fields to their real reconstructed cross-module leaves. Glue only.
//
// Both adapters operate over the SAME shared real entity arrays that entity.cpp
// owns: VIBE_Util_ParseInt is a pure string->int leaf, and VIBE_Person_QueryBegin
// scans the real g_objects pool (stride 169 / 256 slots). The returned record is
// an ObjectRec (169 bytes), so reading its byte at +90 is in-bounds and
// byte-faithful — exactly the read VIBE_Command_CheckObjectFlagClear performs.
#include "sim/wire_selinput.h"

#include "sim/command_apply9.h"  // SelectionHooks / CheckHooks / Set*/Get*
#include "sim/entity.h"          // PersonQueryBegin / PersonFilter / ObjectRec
#include "world/city.h"          // UtilParseInt (VIBE_Util_ParseInt @0x5dc070)

namespace guild::sim {

namespace {

// =========================================================================
// SelectionHooks.parseInt -> VIBE_Util_ParseInt @0x5dc070.
// QueueSetSelectedFlag / QueueRevealAllPersons parse a leading signed decimal
// from the "-<int>" console token. The real leaf lives in world/city.cpp.
// =========================================================================
i32 WsiParseInt(const char* s) {
    return guild::world::UtilParseInt(s);
}

// =========================================================================
// CheckHooks.personQueryBeginFlag90 -> VIBE_Person_QueryBegin @0x586c20.
// gilde.exe 0x496124 (CheckObjectFlagClear): Begin = QueryBegin(a2, 1, 1, key);
// return !Begin || (Begin[90] & 2) == 0. The original varargs call QueryBegin(
// a2, argc=1, op=1, val=key) resolves the first record whose id (+4) == key. We
// replay that as a single {op:1, value:key} filter against the real g_objects
// pool, surface "found" via the return, and write the record's byte +90 out.
// =========================================================================
int WsiPersonQueryBeginFlag90(int /*a2*/, int key, u8* outFlagByte90) {
    PersonFilter f{ /*op=*/1, /*value=*/key };
    ObjectRec* rec = PersonQueryBegin(&f, 1);
    if (!rec) return 0;
    if (outFlagByte90)
        *outFlagByte90 = reinterpret_cast<const u8*>(rec)[90];
    return 1;
}

// --- process-lifetime wired hook tables (the global hook ptr references these) ---
SelectionHooks g_sel{};
CheckHooks     g_check{};

} // namespace

void InstallRealSelInputWiring() {
    // --- SelectionHooks (command_apply9.h) -----------------------------------
    // Seed from the module's inert defaults (non-null stubs), then bind only the
    // one field with a clean reconstructed target.
    g_sel = GetSelectionHooks();
    g_sel.parseInt = &WsiParseInt;
    // selectedPersonRecord (reads the process-global selection table
    // *(sel+8*idx+4)) / worldActiveCount (word_12CE910 active-count global) /
    // revealableSlot (the 768-slot person tables word_12CE910/byte_12CE918/
    // byte_12CE912/byte_12CEA76/dword_12CE914) / revealFlagByte (byte_6477A1):
    // process-global table reads with no standalone callable reconstructed leaf
    // -> inert (default stubs retained).
    SetSelectionHooks(&g_sel);

    // --- CheckHooks (command_apply9.h) ---------------------------------------
    g_check = GetCheckHooks();
    g_check.personQueryBeginFlag90 = &WsiPersonQueryBeginFlag90;
    // officeCanRunFor (VIBE_Office_CanRunForOffice) / officePrereqMet
    // (VIBE_Office_CheckPrerequisitesMet): not reconstructed in the translated
    // slice -> inert (default "not eligible / not met").
    SetCheckHooks(&g_check);
}

} // namespace guild::sim
