#include "world/crime.h"

#include <cstring>

// Faithful 1:1 port of the VIBE_Straftat_* / VIBE_Beweis_* record ops.
// The crime table's parallel named globals (dword_11BC760 id, dword_11BC776
// perp, word_11BC77A wanted, dword_11BC785 provenState, ...) are unified into a
// CrimeRecord[512]; the original's raw "base + 45*i" pointer arithmetic becomes
// field access on g_crimeTable[i]. Evidence keeps the original two-array,
// stride-2 interleave so the slot math is identical.

namespace guild::world {

CrimeRecord g_crimeTable[kCrimeCount];
i32 g_evidenceOwner[kEvidenceDwords];
i32 g_evidenceCrimeId[kEvidenceDwords];

void CrimeAndEvidenceReset() {
    std::memset(g_crimeTable, 0, sizeof(g_crimeTable)); // provenState==0 -> free
    for (int i = 0; i < kEvidenceDwords; ++i) {
        g_evidenceOwner[i]   = -1;
        g_evidenceCrimeId[i] = -1;
    }
}

namespace {
struct CrimeTableInit {
    CrimeTableInit() { CrimeAndEvidenceReset(); }
} g_crimeTableInit;

void DefaultResolveNotify(int /*crimeIndex*/, i32 /*perpId*/) {}
ResolveNotifyFn g_resolveNotifyFn = &DefaultResolveNotify;

void DefaultRemoveGrid(i32 /*target*/, u8 /*location*/) {}
ResolveRemoveGridFn g_removeGridFn = &DefaultRemoveGrid;
} // namespace

void StraftatSetResolveNotifyFn(ResolveNotifyFn fn) {
    g_resolveNotifyFn = fn ? fn : &DefaultResolveNotify;
}

void StraftatSetResolveRemoveGridFn(ResolveRemoveGridFn fn) {
    g_removeGridFn = fn ? fn : &DefaultRemoveGrid;
}

// gilde.exe 0x4c3390 — VIBE_Straftat_FindFreeSlot.
int StraftatFindFreeSlot() {
    int result = -1;
    for (int i = 0; i < kCrimeCount; ++i) {
        if (g_crimeTable[i].provenState == 0) { // !*(&dword_11BC785 + 45*i)
            result = i;
            i = kCrimeCount; // original sets i=512 to break after first hit
        }
    }
    return result;
}

// gilde.exe 0x4c33bc — VIBE_Straftat_FindIndexById.
int StraftatFindIndexById(i32 id) {
    if (id == g_crimeTable[0].id) // a1 == dword_11BC760
        return 0;
    for (int i = 1; i < kCrimeCount; ++i) {
        if (id == g_crimeTable[i].id)
            return i;
    }
    return -1;
}

// gilde.exe 0x4c3874 — VIBE_Straftat_SetRecordState.
int StraftatSetRecordState(u32 state, u32 index) {
    if (index >= static_cast<u32>(kCrimeCount)) // a2 >= 0x200
        return -2;
    if (state < 2)                              // a1 < 2
        return -3;
    g_crimeTable[index].provenState = static_cast<i32>(state);
    return static_cast<int>(index);
}

// gilde.exe 0x4c3a48 — VIBE_Straftat_CountActiveByTarget.
int StraftatCountActiveByTarget(i32 personId) {
    int count = 0;
    for (int i = 0; i < kCrimeCount; ++i) {
        if (personId == g_crimeTable[i].perpetrator && g_crimeTable[i].provenState == 1)
            ++count;
    }
    return count;
}

// gilde.exe 0x4c354c — VIBE_Straftat_ResolveAndClear (record/evidence portion).
int StraftatResolveAndClear(i32 crimeId, int force) {
    // FindIndexById inlined (same scan as 0x4c33bc).
    int idx;
    if (crimeId == g_crimeTable[0].id) {
        idx = 0;
    } else {
        idx = -1;
        for (int i = 1; i < kCrimeCount; ++i) {
            if (crimeId == g_crimeTable[i].id) { idx = i; break; }
        }
    }
    if (idx == -1)
        return 3;

    CrimeRecord& rec = g_crimeTable[idx];

    // Decrement the wanted counter (clamped at 0).
    if (static_cast<i16>(rec.wanted) > 0)
        rec.wanted = static_cast<u16>(rec.wanted - 1);

    // Still wanted and not forced -> leave the record in place.
    if (rec.wanted && !force)
        return 2;

    // Not proven (and not a forced clear of a pending record) -> leave in place.
    i32 state = rec.provenState;
    if (state != 1 && (!force || !state))
        return 1;

    // Cleared path (0x4c3614..): the original resolves the perpetrator person
    // record (FindRecordById — gates the History notify below) and then calls
    // VIBE_City_RemoveCrimeFromGrid(target@+33, location@+28) UNCONDITIONALLY,
    // once, before removing the linked evidence. Surfaced via the grid hook.
    g_removeGridFn(rec.target, rec.location);

    // Remove all linked evidence pairs (those whose crimeId == id).
    for (int i = 0; i != kEvidenceDwords; i += 2) {
        if (crimeId == g_evidenceCrimeId[i]) {
            // The original notifies History when the evidence belongs to the
            // local player and a perpetrator exists; modeled via the hook.
            g_resolveNotifyFn(i * 4, rec.perpetrator);
            g_evidenceOwner[i]   = -1;
            g_evidenceCrimeId[i] = -1;
        }
    }

    // Clear the crime record (provenState, perpetrator, id).
    rec.provenState = 0;
    rec.perpetrator = -1;
    rec.id          = -1;
    return 0;
}

// ===========================================================================
// Evidence / Beweis.
// ===========================================================================

// gilde.exe 0x4c347c — VIBE_Beweis_FindOrAllocSlot.
int BeweisFindOrAllocSlot(i32 owner, i32 crimeId) {
    int freeIdx = -1;
    bool haveFree = false;
    int v = 0;
    while (v < kEvidenceCapacity) {
        if (!haveFree && g_evidenceOwner[2 * v] == -1 && g_evidenceCrimeId[2 * v] == -1) {
            haveFree = true;
            freeIdx = v;
        }
        if (owner == g_evidenceOwner[2 * v] && crimeId == g_evidenceCrimeId[2 * v])
            return -2; // pair already exists
        ++v;
    }
    return haveFree ? freeIdx : -1;
}

// gilde.exe 0x4c3338 — VIBE_Beweis_Add.
int BeweisAdd(i32 crimeId, i32 owner, int /*extra*/) {
    int slot = BeweisFindOrAllocSlot(owner, crimeId);
    if (slot <= -1)
        return 0;
    g_evidenceOwner[2 * slot]   = owner;   // dword_11C2160[2*slot] = owner
    g_evidenceCrimeId[2 * slot] = crimeId; // dword_11C2164[2*slot] = crimeId
    // The original logs gs_AddBeweis(...) via sprintf here; omitted (no effect).
    return 1;
}

// gilde.exe 0x4c3518 — VIBE_Beweis_ExistsForPair.
int BeweisExistsForPair(i32 owner, i32 crimeId) {
    for (int v = 0; v < kEvidenceDwords; v += 2) {
        if (owner == g_evidenceOwner[v] && crimeId == g_evidenceCrimeId[v])
            return 1;
    }
    return 0;
}

// gilde.exe 0x4c32ec — VIBE_Beweis_CollectByOwner.
int BeweisCollectByOwner(i32 key, i32* out, int outCapacity) {
    int count = 0;
    int recIdx = 0;
    int outBytes = 0;
    // Original bound: recIdx < 512 && outBytes < 128 (32 entries x 4 bytes).
    int maxBytes = 128;
    if (outCapacity * 4 < maxBytes)
        maxBytes = outCapacity * 4;
    do {
        // HARDENING (wave-12): the original always receives the engine's fixed
        // 32-entry (128-byte) buffer, so outBytes < maxBytes terminates before
        // `count` can reach the buffer end. With a degenerate caller buffer
        // (outCapacity <= 0, or < 32) the do/while writes out[count] before the
        // post-check and overruns (confirmed via ASAN). Guarding the write on
        // count < outCapacity is a no-op for every valid call (maxBytes ==
        // min(128, outCapacity*4) already stops the loop first) but stops the
        // overrun for a too-small buffer.
        if (key == g_crimeTable[recIdx].provenState && count < outCapacity) {
            out[count] = recIdx;
            ++count;
            outBytes += 4;
        }
        ++recIdx;
    } while (recIdx < kCrimeCount && outBytes < maxBytes);
    return count;
}

} // namespace guild::world
