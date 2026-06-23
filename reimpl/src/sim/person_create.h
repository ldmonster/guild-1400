#pragma once
#include "guild/common/types.h"
#include "sim/types.h"

// gilde.exe — Person create leaf (namespace guild::sim).
//
// VIBE_Person_CreateAndSpawn @0x58da70 allocates a fresh Person record in the
// person array (word_12CE910, stride 536, 768 slots), assigns it an id from the
// global allocator (dword_649890), and initialises ~150 fields: kind/marker,
// player-mode, family/relation links derived from two parent records, randomised
// stat/need vectors (heavy RNG + many float tables flt_5828xx / flt_6268xx), name
// selection from string tables, avatar slot allocation, and head-bone resolution.
//
// The original is ~0x900 bytes, the largest function in the create cluster, and is
// entangled with the RNG, avatar/render subsystem, ~30 float-constant globals and
// several string tables that are absent from the cold IDB. A bit-exact 1:1 port is
// out of scope for the command-apply module (it would require the full RNG +
// avatar + render leaves). The DEFERRED list in the module report records it.
//
// The command-apply person-create handlers (opcodes 0x0B / 0x0C) call
// CreateAndSpawn as a LEAF. They observe only:
//   * the returned slot index (word; 0xFFFF on failure),
//   * the new record's id at dword_12CE914[134*idx] (== g_persons[idx].id), and
//   * the record base &word_12CE910[268*idx] (== &g_persons[idx]),
// then write a few of their own fields (name, family fields, owner/relation ids).
// So we model the leaf as a mockable hook with a faithful default backend that
// allocates a real g_persons slot, stamps marker/kind/id/owner, and returns the
// slot index — enough for the apply handlers + tests to round-trip. Tests can spy.

namespace guild::sim {

// Argument bundle for VIBE_Person_CreateAndSpawn (recovered __userpurge register
// order: al=kind, edx=parentAId, ecx=ownerWord, ebx=parentBId, then stack
// args queryRec/a6/a7/a8). The apply handlers fill these from the packet payload.
struct PersonSpawnArgs {
    u8  kind;        // al    record kind / player-mode byte (obj +2)
    i32 parentAId;   // edx   first parent person id (a2)
    u16 ownerWord;   // ecx   owner index word (a3)  -> record word +5 (+10)
    i32 parentBId;   // ebx   second parent person id (a4)
    // a5 — the parent BUILDING record. The cmd-0x0B applier resolves it with
    // Person_QueryBegin(1,1,id) @0x4966e5, whose cursor is the 169-stride
    // OBJECT/BUILDING array dword_13CE298 (verified @0x586cb9): the ObjectRec*
    // (null = none). The default backend reproduces the 0x58dc8c
    // building-column writes from it (the daily-director populater).
    const void* queryRec = nullptr;
    u8  a6;          // a6    spawn-context byte (record +356)
    u8  a7;          // a7    spawn-context byte (record +357)
    u8  a8;          // a8    gender/seed byte (record +9)
};

// VIBE_Person_CreateAndSpawn @0x58da70 — create a person record. Returns the new
// slot index (0..767), or 0xFFFF if the array is full / not loaded.
using PersonSpawnFn = u16 (*)(const PersonSpawnArgs& args);
void SetPersonSpawnHook(PersonSpawnFn fn);

// Direct entry (calls the installed hook). Apply handlers use this.
u16 Person_CreateAndSpawn(const PersonSpawnArgs& args);

// The default backend's next-id allocator (dword_649890; shared concept with the
// building create allocator in the binary, modelled separately here). Reset by tests.
extern i32 g_personNextId; // dword_649890

// gilde.exe dword_647724 @0x647724 — the live-person counter the function
// post-decrements when reusing a kind-15 (dead) slot and post-increments after
// stamping the slot (`0x58db07` / `0x58dbe5`). Surfaced for tests.
extern i32 g_personLiveCount; // dword_647724

// -----------------------------------------------------------------------------
// Named hooks for the genuinely-missing-data LEAVES of 0x58da70 (rule 8). The
// default backend reconstructs every in-tree field write + the EXACT RandNext
// draw sequence (so the new-game RNG stream stays in lockstep); these slots are
// the leaves whose backing data/subsystem is NOT in this segment's tree:
//
//   * Avatar slot alloc — VIBE_Avatar_AllocSlot @0x484598 (kind-16/menu-dummy
//     path only; never taken by the new-game commit). Returns a runtime avatar
//     record pointer; default null => the avatar branch is skipped exactly as
//     the original does when no avatar slot is free.
//   * First-name string tables — dword_8C400C (male, 191) / dword_8C4320
//     (female, 112) / dword_8C4508 (dynasty-female, 149), runtime POINTER tables
//     filled from localized text resources by 0x530e50; all-zero in the static
//     image. Default returns "" — the RandNext index draw is still consumed so
//     the stream matches; the apply layer / ParentFirstName hook supplies names.
//   * Family record — RECONSTRUCTED (wave-18, sim/family_record.{h,cpp}). The
//     family table word_13C3110 (16 x 164 bytes) + VIBE_Person_GetFamilyRecord
//     @0x58c408 + the inline allocator @0x58ecf3 are now in-tree; the default
//     backend stamps the real record (+128 = -1.0f, word[0] = family word). The
//     allocFamilyRecord hook below is retained for spies/tests but is no longer
//     consulted by the default path.
//   * Head-bone resolve — VIBE_Character_ResolveHeadBone @0x57c5d4 (scene-graph
//     leaf). Default no-op.
struct PersonCreateHooks {
    // Returns a pointer to a 218-byte avatar record (v81 in the decompile) or
    // null. The default returns null (no avatar slot).
    const void* (*allocAvatarSlot)() = nullptr;
    // Family-record allocation: returns true and fills a 132-byte record image
    // through `outRecord` (FamilyRecord[+128]=-1082130432f, FamilyRecord[0]=word)
    // when a record is available; false otherwise. The default returns false.
    bool (*allocFamilyRecord)(int familyWord, void* outRecord) = nullptr;
    // Copies up to 15 chars of the selected first name into `dst`
    // (StrNCopyPad-padded to 16). `gender` 0 == male table, 1 == female table,
    // 2 == dynasty-female (8C4508). `index` is the already-drawn table index.
    void (*firstName)(char* dst, int gender, int index) = nullptr;
    // Scene-graph head-bone resolve (no record-observable effect here).
    void (*resolveHeadBone)(void* personRec) = nullptr;
};
void SetPersonCreateHooks(const PersonCreateHooks& h);
const PersonCreateHooks& GetPersonCreateHooks();

void ResetPersonCreate(); // test helper (not in orig)

} // namespace guild::sim
