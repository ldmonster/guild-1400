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
    i32 queryRec;    // a5    QueryBegin building record ptr token (0 if none)
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

void ResetPersonCreate(); // test helper (not in orig)

} // namespace guild::sim
