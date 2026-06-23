#pragma once
// gilde.exe 0x4869dc — VIBE_Object_SpawnThrownBomb.
//
// Spawns a thrown "ob_BOMBE" projectile: finds a free slot in the 32-entry bomb
// table, attaches the bomb scene node at the (lifted) spawn position, computes the
// ballistic velocity toward the target tile, fills the 352-byte object anim/physics
// descriptor (velocity, target delta, PI-rotation slots, height), and starts the
// object animation. Returns a pointer to the slot's node cell (or null when the
// table is full).
//
// The engine leaves it reaches — VIBE_Object_AttachToUniverseNode @0x5b3e30,
// VIBE_Heightmap_TileToWorld @0x5c65d4, VIBE_Anim_CreateObjectAnim @0x5cef14,
// VIBE_Coord_ConvertX @0x5c6b08 — are reconstructed elsewhere but touch live
// scene/anim/coord state; they route through an inert-default hooks struct so the
// PURE spawn/slot/velocity/descriptor logic is reconstructed 1:1 and headless-testable.
#include "guild/common/types.h"

namespace guild::sim {

using f32 = float;

// One bomb-table slot: the four interleaved arrays dword_B5F910/914/918/91C, one
// per active thrown bomb (32 slots). node==0 means the slot is free.
struct BombSlot {
    i32 node = 0;   // dword_B5F910[k] — attached scene node (0 = free)
    i32 anim = 0;   // dword_B5F914[k] — object-anim handle
    i32 tile = 0;   // dword_B5F918[k] — target tile (a2)
    i32 arg  = 0;   // dword_B5F91C[k] — caller arg (a3)
};

// Spawn coords the caller passes (a1): x/y floats + a packed z dword.
struct BombSpawn {
    f32 x = 0.0f;   // *(float*)a1
    f32 y = 0.0f;   // *(float*)(a1+4)
    i32 z = 0;      // *(int*)(a1+8)
};

// Injected engine leaves (inert defaults -> headless reconstruction of the math).
struct ThrowBombHooks {
    // VIBE_Object_AttachToUniverseNode(0, &pos[3], "ob_BOMBE", colorParam[3]) -> node id.
    i32 (*attachNode)(const f32 pos[3], const i32 colorParam[3]) = nullptr;
    // node+529 |= 4 (mark flag). Default: no-op.
    void (*markNodeFlag)(i32 node) = nullptr;
    // VIBE_Heightmap_TileToWorld(map, tile, out[3], arg) -> target world pos.
    void (*tileToWorld)(i32 tile, i32 arg, f32 out[3]) = nullptr;
    // VIBE_Coord_ConvertX() -> the int stamped at descriptor[67]. Default 0.
    i32 (*coordConvertX)() = nullptr;
    // VIBE_Anim_CreateObjectAnim(node, descSmall, ?, 4, slot*4) -> anim handle.
    i32 (*createObjectAnim)(i32 node, const void* animParams, int slotByteIndex) = nullptr;
    // The heightmap handle (off_649D64[44]) — opaque; passed back to tileToWorld.
    i32 mapHandle = 0;
};

void SetThrowBombHooks(const ThrowBombHooks* hooks);
const ThrowBombHooks& GetThrowBombHooks();

// The 32-slot bomb table (g_bombTable). Exposed for the spawn routine + tests.
constexpr int kBombSlotCount = 32;
extern BombSlot g_bombTable[kBombSlotCount];
void ResetBombTable();

// gilde.exe 0x4869dc — spawn the bomb. `spawn` is the a1 coords, `targetTile`=a2,
// `arg`=a3. Returns the slot index used (0..31), or -1 when the table is full
// (the original returns &dword_B5F910[slot] / null; the index is the portable form).
// `outDescriptor`, when non-null, receives the 88-dword (352-byte) anim descriptor
// the routine built (for verification / handoff to the real anim system).
int SpawnThrownBomb(const BombSpawn& spawn, i32 targetTile, i32 arg,
                    i32* outDescriptor88 = nullptr);

} // namespace guild::sim
