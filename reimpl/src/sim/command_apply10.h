#pragma once
#include "guild/common/types.h"

// command_apply10 — the Resolve*/Check*/Sync-predicate target-search family of
// VIBE_Command_* (namespace guild::sim). Every name/address below was checked,
// both by address (UPPERCASE + split forms) AND by bare name, case-insensitively
// against command_apply..command_apply9, command_codec, command_builders*,
// command_pending, command_inherit, command_receive and command_unit_orders;
// none of the functions defined here is defined there.
//
// This file covers (largest genuinely-untranslated members of the family):
//   * the target-validation Check* predicates that drive command acceptance —
//       CheckSourceTargetReachable (0x495cf8), CheckParamRefsValid (0x495ef8),
//       CheckOfficeSlotByTag (0x49623c), CheckPersonHasOfficeTag (0x496174),
//       CheckTargetOwnership (0x496080), CheckTargetCooldown (0x495c64),
//       CheckTargetNotInUse (0x4963ec);
//   * the pure ACK-table scan CheckSyncRangeAcked (0x493a34) — fully golden;
//   * two console-driven person resolvers — ResolveTargetGuard (0x4f8fac),
//       ResolveTargetOfficial (0x4f90e0) and the iterator-driven
//       ResolveTargetBestThief (0x4fa178).
//
// Cross-module entity-resolve / query leaves (VIBE_GameObject_ResolveEntityById,
// VIBE_GameObject_QueryFind, VIBE_Person_QueryBegin/IterNext,
// VIBE_Person_FindRecordById, VIBE_GameObject_CountAtLocation,
// VIBE_Inventory_Compute*, VIBE_BuildingValue_ComputeRoomWorth, the global
// person/selection tables word_12CE910 et al., the special-target sentinel
// globals dword_631288/8C/90) are NOT reconstructed, so they are routed through
// an installable hooks struct with inert default implementations defined in this
// library .cpp (the CutsceneMiscHooks / CheckHooks pattern). Tests install spies.
// Already-reconstructed callees are not duplicated.

namespace guild::sim {

// ---------------------------------------------------------------------------
// Resolved-entity triple. The original VIBE_GameObject_ResolveEntityById writes
// three out-pointers (and returns nonzero on success):
//   *immediate  — a "person" record base, or 0
//   *parent     — a parent/container record base, or 0
//   *object[0]  — the resolved game-object base (the io[4] slot also carries the
//                 caller's command record on entry; out[0] is the object).
// The selection rule used by every Check* below: if immediate, use it; else if
// parent, use it; else use object. We carry the three as opaque host pointers.
// ---------------------------------------------------------------------------
struct ResolvedEntity {
    void* immediate = nullptr; // a8/v8 in the originals (person record)
    void* parent    = nullptr; // a7/v7 (parent/container record)
    void* object    = nullptr; // io[0]/v9[0] (game object record)
};

// ---------------------------------------------------------------------------
// Cross-module hooks. All default implementations are inert (resolve nothing /
// find nothing); installing a hook set lets a test drive any path.
// ---------------------------------------------------------------------------
struct ApplyTargetHooks {
    // VIBE_GameObject_ResolveEntityById(id, caller): resolve id into a triple.
    // Returns nonzero on success (entity exists). Default: returns 0.
    int (*resolveEntityById)(i32 id, void* caller, ResolvedEntity* out);

    // The three special-target sentinels. The originals replace a target id of
    // -2/-3/-4 with dword_631288/dword_63128C/dword_631290 respectively before
    // resolving. The hook returns the current value; default 0.
    i32 (*specialTarget)(i32 sentinel); // sentinel in {-2,-3,-4}

    // VIBE_GameObject_QueryFind(key, a2, a3, a4 [, a5]) -> record base or null.
    // (CheckSourceTargetReachable uses the 4-arg form; the office queries use a
    // 5-arg form with a4=0,a5=300/301.) Default: returns nullptr.
    void* (*queryFind)(i32 key, i32 a2, i32 a3, i32 a4, i32 a5);

    // VIBE_GameObject_IterNext() -> next record in an open query, or null.
    void* (*queryIterNext)();

    // VIBE_Person_QueryBegin(a2, a3, a4, key) -> person record base or null.
    void* (*personQueryBegin)(i32 a2, i32 a3, i32 a4, i32 key);

    // VIBE_Person_IterNext() -> next person in an open person query, or null.
    void* (*personIterNext)();

    // VIBE_Person_FindRecordById(id) -> person record base or null.
    void* (*personFindRecordById)(i32 id);

    // VIBE_GameObject_CountAtLocation(loc) -> count (signed). Default: 0.
    i32 (*countAtLocation)(i32 loc);

    // VIBE_Inventory_ComputeFreeCapacity(obj, kind, obj2, need) -> capacity.
    i32 (*invFreeCapacity)(void* obj, i32 kind, void* obj2, i32 need);
    // VIBE_Inventory_ComputeCarryCapacity(obj, kind, need) -> capacity.
    i32 (*invCarryCapacity)(void* obj, i32 kind, i32 need);
    // VIBE_GameObject_ResolveOwnerOrParentB(obj, parent) -> record base or null.
    void* (*resolveOwnerOrParentB)(void* obj, void* parent);

    // word_12CE910[268*i] base + helpers for the console resolvers. We model the
    // 768-slot selection table as a single host callback that, given a slot
    // index 0..767, reports whether the slot is a person of the requested
    // profession class and (on match) hands back the record base. `cls` is the
    // profession-tag byte the loop is searching for (15 = guard, 26/21 = official).
    // Returns nonzero on match. `outRecord` receives the word_12CE910 record base.
    int (*selectionMatch)(int slot, int cls, void** outRecord);

    // VIBE_BuildingValue_ComputeRoomWorth(record, kind, table) — best-thief score.
    i32 (*roomWorth)(void* record, i32 kind, void* table);

    // *(record + 4) read as a 32-bit entity id (used to write back the resolved
    // id into the command's target slot). Default: 0.
    i32 (*recordEntityId)(void* record);
};
void SetApplyTargetHooks(const ApplyTargetHooks* hooks);
const ApplyTargetHooks& GetApplyTargetHooks();

// Resolve a raw target id, replacing the -2/-3/-4 sentinels first (the shared
// prologue of the Check* predicates). Returns the (possibly substituted) id.
i32 SubstituteSpecialTarget(i32 id);

// ---------------------------------------------------------------------------
// Check* predicates. Each takes the command record base `cmd` (a u8*; the
// originals index it by byte offset) plus the caller token `a2` ResolveEntityById
// threads through. All return the original's BOOL/int faithfully.
// ---------------------------------------------------------------------------

// gilde.exe 0x495cf8 — VIBE_Command_CheckSourceTargetReachable(cmd, a2).
// cmd is a _DWORD* (a1[k] = *(u32*)(cmd + 4*k)). Validates the "to" slot a1[5]
// and the "from" slot a1[4], each resolved via ResolveEntityById, against a set
// of capacity / count gates. Returns 1 (reject / unreachable) when ANY gate
// fails or an entity vanishes, else 0 (reachable).
int CheckSourceTargetReachable(u8* cmd, void* a2);

// gilde.exe 0x496080 — VIBE_Command_CheckTargetOwnership(cmd, a2). Resolves
// *(cmd+16); if the resolved base + *(cmd+20) lands exactly on object+228 AND
// *(cmd+30) < 0 AND object[458] < 0 it is rejected (returns 0). Otherwise 1.
int CheckTargetOwnership(u8* cmd, void* a2);

// gilde.exe 0x495c64 — VIBE_Command_CheckTargetCooldown(cmd, a2). For target
// slot *(cmd+20) (skipped when -1): resolves it and, if
// CountAtLocation(selected.field) - *(cmd+29) >= 0, rejects (returns 0). Else 1.
int CheckTargetCooldown(u8* cmd, void* a2);

// gilde.exe 0x4963ec — VIBE_Command_CheckTargetNotInUse(cmd). Scans up to 16
// person-id entries starting at *(cmd+16); returns 1 the moment a found person
// with state byte 6/7 is bound to a DIFFERENT object (its +130 dword != -1 and
// != *(cmd+16)); 0 if none conflicts (or the first id is -1).
int CheckTargetNotInUse(u8* cmd);

// gilde.exe 0x496174 — VIBE_Command_CheckPersonHasOfficeTag(cmd, a2). Branches
// on the 4-byte tag *(cmd+16): 'rdpm'(1668048242) and ' adm'(1651865888) office
// checks via QueryBegin->QueryFind(...,300). Returns 0 on a tag mismatch in the
// office record's +7 entries, else 1.
int CheckPersonHasOfficeTag(u8* cmd, void* a2);

// gilde.exe 0x49623c — VIBE_Command_CheckOfficeSlotByTag(cmd, a2). Three tag
// branches ('nmab'/'vmba'/'rdpm', queryFind ...,301). Returns 0 when the office
// slot table lacks a free/-1 slot for the requested tag, else 1.
int CheckOfficeSlotByTag(u8* cmd, void* a2);

// gilde.exe 0x495ef8 — VIBE_Command_CheckParamRefsValid(cmd). Resolves *(cmd+16)
// then walks the *(cmd+20) field descriptors (each: width byte, count byte, u16
// offset) starting at cmd+21. Rejects (returns 1) if a referenced field aliases
// the resolved object's reserved slots (+2 / +46 person link with a bad owner).
// Returns 0 if all field refs are valid.
int CheckParamRefsValid(u8* cmd);

// gilde.exe 0x493a34 — VIBE_Command_CheckSyncRangeAcked(start, end, ackTable).
// Pure scan of the ACK/status table between sequence numbers [start, end):
//   if start == end: return 1 (range empty / fully acked).
//   if start  > end: return 1.
//   walk seq from start: status = ackTable[10 * (seq & 0x7FFF)];
//     status 0  -> return 0 (still pending);
//     status 2  -> result = -1 (negatively acked, keep scanning);
//     reaching end -> return result (1 by default, -1 if any nak seen).
// The original reads file globals dword_11AA484 (start) / dword_11AA47C (end) /
// byte_B5FB60 (table); we pass them in so the scan is testable in isolation.
i32 CheckSyncRangeAcked(u32 start, u32 end, const u8* ackTable);

// ---------------------------------------------------------------------------
// Console-driven person resolvers. `mode` is the original's al argument:
//   mode 0 — resolve a fresh target, write its id back into the caller slot;
//   mode 1 — re-validate the already-bound target at slot `slotIdx`;
//   mode 2 — (Scoped variants) reject; these two ignore it.
// `slotTable`/`slotIdx` model the caller's 8-byte-stride target array (the
// originals read *(a2 + 8*a4 + 4)); we pass the table base + slot index. `out`
// receives the rendered message id (RenderFormattedMessage's third arg, the
// resolved record's word[0]); the originals format into a5. Returns 1 on a
// resolved/validated target, 0 otherwise.
// ---------------------------------------------------------------------------

// gilde.exe 0x4f8fac — VIBE_Command_ResolveTargetGuard. cls byte == 15.
int ResolveTargetGuard(int mode, i32* slotTable, int slotIdx, u16* outRendered);

// gilde.exe 0x4f90e0 — VIBE_Command_ResolveTargetOfficial. cls byte ∈ {26,21}.
int ResolveTargetOfficial(int mode, i32* slotTable, int slotIdx, u16* outRendered);

// gilde.exe 0x4fa178 — VIBE_Command_ResolveTargetBestThief. Iterates the person
// query (profession 22) scoring each by roomWorth; keeps the best. mode 2 -> 0,
// mode 1 -> resolve a bound id, else iterate. Returns 1 on a chosen record.
int ResolveTargetBestThief(int mode, i32* slotTable, int slotIdx, void** outRecord);

} // namespace guild::sim
