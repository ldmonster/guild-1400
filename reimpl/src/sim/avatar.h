#pragma once
// Avatar registry for the Guild simulation (gilde.exe). MODULE: Avatar
// (namespace guild::sim).
//
// The avatar registry (gilde.exe dword_630E4A @0x630E4A) is a fixed table of 10
// entries, 92 bytes (23 dwords) each. Each entry's first dword packs the owning
// scene-entity id in its high 16 bits (0 == free entry); the 90-byte payload at
// +2 holds the avatar's appearance/animation slots. Avatars are resolved by
// scene-entity id (LookupById) or found/allocated for a person from the scene
// entities the person owns (FindOrAllocForPerson).
//
// The appearance-randomising allocator (VIBE_Avatar_AllocSlot @0x484598, a
// separate 248-byte/32-slot pool with RNG-scaled float columns) and the
// save/load serialisers (VIBE_Avatar_Save/Load) are render/IO-coupled and are
// LISTED AS DEFERRED (see report).
//
// Translated functions:
//   VIBE_Avatar_LookupById          0x4859b0
//   VIBE_Avatar_FindOrAllocForPerson 0x4859e0
#include "guild/common/types.h"
#include "sim/types.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// Avatar registry table (gilde.exe dword_630E4A, 92-byte/23-dword stride, 10
// entries). Entry layout used by the lookups:
//   dword[0] : high 16 bits == owning scene-entity id (0 == free entry).
//   bytes +2..+91 : avatar payload (appearance/anim; opaque to the lookups).
// ---------------------------------------------------------------------------
constexpr int kAvatarEntryStride = 92;   // 23 dwords
constexpr int kAvatarCapacity    = 10;   // 230 dwords / 23

GUILD_PACKED_BEGIN
struct AvatarEntry {
    u16 ownerLo;   // +0x00  low  16 bits of dword[0] (unused by lookups)
    u16 ownerId;   // +0x02  high 16 bits of dword[0] == scene-entity id
    u8  payload[88]; // +0x04..+0x5B  appearance / animation slots
} GUILD_PACKED;
GUILD_PACKED_END
static_assert(sizeof(AvatarEntry) == kAvatarEntryStride, "AvatarEntry stride 92");

// gilde.exe dword_630E4A @0x630E4A — the avatar registry table.
extern AvatarEntry g_avatars[kAvatarCapacity];
void ResetAvatars();  // test/setup helper

// gilde.exe 0x4859b0 — VIBE_Avatar_LookupById  (__usercall, ax=(id@ax)).
// Linear scan for the entry whose owner id matches `id`. The original returns a
// pointer to the entry payload (&dword_630E4A[idx] + 2); we return the entry
// (its payload begins at &entry.ownerId). Returns nullptr if not found.
//   NB: the original scans by id == dword>>16 WITHOUT a "free entry" guard, so
//   id 0 finds the first free entry — faithfully reproduced.
AvatarEntry* Avatar_LookupById(u16 id);

// gilde.exe 0x4859e0 — VIBE_Avatar_FindOrAllocForPerson (__usercall, eax=person
// record ptr). The original queries the scene entities the person owns
// (GameObject_QueryFind(person+376, type 5)) and returns the first matching
// avatar; if none match, returns the first free entry; nullptr if the table is
// full. Here `ownedEntityIds`/`count` is the resolved owned-entity id list (the
// QueryFind result) so the registry logic is testable without the scene tree.
AvatarEntry* Avatar_FindOrAllocForPerson(const u16* ownedEntityIds, int count);

}  // namespace guild::sim
