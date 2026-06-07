#include "test.h"

// Integration: drive the script_import2 command body VIBE_Script_CmdKillLocalScripts
// (0x43c790) against a REAL reconstructed sibling — VIBE_Script_FindByHandle
// (script_import.cpp 0x442174) — no mock context-table lookup.
//
// CmdKillLocalScripts walks the 128-slot context table (2584-byte stride) and,
// for every runnable sibling context owned by the same scene as the current
// context (but not the current handle itself), calls the host's per-context
// "finish" callback. The live engine's finish path re-resolves the doomed
// context by handle through the same table. Here the finish callback forwards
// into the genuine FindByHandle: each context the kill-loop selects must resolve
// back — through the real sibling — to exactly that slot, and only those
// contexts the original's gate selects (handle != -1, runnable, same owner, not
// the current handle) must be visited. That wires the kill enumeration against
// the real table-lookup sibling end to end.
//
// script_import2's cross-module character/action leaves (ScriptCmdHooks) have no
// reconstructed sibling and remain on their inert defaults; this test targets the
// one command body whose collaborator IS reconstructed.
#include "sim/script_import2.h"
#include "sim/script_import.h"   // real FindByHandle (0x442174)
#include "sim/script_vm.h"

#include <cstring>
#include <set>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

constexpr int kTableBytes = kScriptContextStride * kScriptContextCount; // 330752

// Configure one context slot's gate fields.
void SetSlot(u8* base, int idx, i32 handle, i32 owner, u8 runFlags) {
    u8* s = base + static_cast<std::size_t>(kScriptContextStride) * idx;
    *reinterpret_cast<i32*>(s + kScHandle)  = handle;
    *reinterpret_cast<i32*>(s + kScOwner)   = owner;
    *(s + kScRunFlags)                      = runFlags;
}

// --- the finish callback: forward into the REAL FindByHandle sibling ----
u8*               g_tableBase = nullptr;
std::set<i32>     g_finishedHandles;       // handles the kill-loop selected
bool              g_allResolvedToSelf = true;

void FinishViaRealLookup(u8* ctx) {
    i32 handle = *reinterpret_cast<i32*>(ctx + kScHandle);
    g_finishedHandles.insert(handle);
    // REAL sibling: the doomed context must resolve, by its handle, back to the
    // very slot the kill-loop handed us.
    u8* resolved = FindByHandle(g_tableBase, handle);   // 0x442174
    if (resolved != ctx)
        g_allResolvedToSelf = false;
}

} // namespace

// Three runnable siblings share owner 100 with the current context (handle 5,
// owner 100). Slot 1 (handle 10) and slot 4 (handle 40) qualify; slot 2 is a
// different owner; slot 3 is non-runnable; slot 5 has handle -1 (free). The
// current context (handle 5) must NOT finish itself. Every finished handle must
// resolve through the real FindByHandle back to its own slot.
TEST(ScriptImport2Itest, KillLocalScriptsSelectsSiblingsResolvedByRealLookup) {
    std::vector<u8> table(kTableBytes, 0);
    u8* base = table.data();
    // Initialize all handles to -1 (free) first.
    for (int i = 0; i < kScriptContextCount; ++i)
        SetSlot(base, i, -1, 0, 0);

    SetSlot(base, 0, /*handle=*/5,  /*owner=*/100, kRunFlagRunnable);  // current
    SetSlot(base, 1, /*handle=*/10, /*owner=*/100, kRunFlagRunnable);  // sibling
    SetSlot(base, 2, /*handle=*/20, /*owner=*/999, kRunFlagRunnable);  // other owner
    SetSlot(base, 3, /*handle=*/30, /*owner=*/100, 0);                 // not runnable
    SetSlot(base, 4, /*handle=*/40, /*owner=*/100, kRunFlagRunnable);  // sibling

    // The current context is slot 0.
    ScriptEngineState& E = ScriptEngine();
    E.currentCtx = base + 0;

    g_tableBase = base;
    g_finishedHandles.clear();
    g_allResolvedToSelf = true;

    i32 rc = CmdKillLocalScripts(base, &FinishViaRealLookup);
    CHECK_EQ(rc, 0);   // the original always returns 0

    // Exactly the two qualifying siblings (10, 40) were finished; the current
    // context (5), the other-owner (20), the non-runnable (30) and the free
    // slots (-1) were not.
    CHECK_EQ(static_cast<int>(g_finishedHandles.size()), 2);
    CHECK(g_finishedHandles.count(10) == 1);
    CHECK(g_finishedHandles.count(40) == 1);
    CHECK(g_finishedHandles.count(5)  == 0);   // never finishes itself
    CHECK(g_finishedHandles.count(20) == 0);
    CHECK(g_finishedHandles.count(30) == 0);
    // Every finished context resolved through the REAL FindByHandle to its slot.
    CHECK(g_allResolvedToSelf);

    // Cross-check the real sibling directly: the selected handles resolve, the
    // skipped/free ones behave per FindByHandle's contract.
    CHECK(FindByHandle(base, 10) == base + 1 * kScriptContextStride);
    CHECK(FindByHandle(base, 40) == base + 4 * kScriptContextStride);
    CHECK(FindByHandle(base, -1) == nullptr);   // -1 is the "no handle" sentinel
    CHECK(FindByHandle(base, 7777) == nullptr); // absent handle => not found

    E.currentCtx = nullptr;
}

// When no sibling shares the current context's owner, the real lookup is never
// asked and nothing is finished — the kill-loop's owner gate (driven by the same
// table the real sibling reads) holds.
TEST(ScriptImport2Itest, KillLocalScriptsNoSiblingsFinishesNothing) {
    std::vector<u8> table(kTableBytes, 0);
    u8* base = table.data();
    for (int i = 0; i < kScriptContextCount; ++i)
        SetSlot(base, i, -1, 0, 0);

    SetSlot(base, 0, /*handle=*/1, /*owner=*/100, kRunFlagRunnable);  // current
    SetSlot(base, 1, /*handle=*/2, /*owner=*/200, kRunFlagRunnable);  // other owner
    SetSlot(base, 2, /*handle=*/3, /*owner=*/300, kRunFlagRunnable);  // other owner

    ScriptEngineState& E = ScriptEngine();
    E.currentCtx = base + 0;

    g_tableBase = base;
    g_finishedHandles.clear();
    g_allResolvedToSelf = true;

    i32 rc = CmdKillLocalScripts(base, &FinishViaRealLookup);
    CHECK_EQ(rc, 0);
    CHECK_EQ(static_cast<int>(g_finishedHandles.size()), 0);
    CHECK(g_allResolvedToSelf);   // vacuously true; no lookups performed

    // The other-owner contexts still resolve by the real sibling — they just
    // weren't selected for finishing.
    CHECK(FindByHandle(base, 2) == base + 1 * kScriptContextStride);
    CHECK(FindByHandle(base, 3) == base + 2 * kScriptContextStride);

    E.currentCtx = nullptr;
}
