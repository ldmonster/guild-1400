#pragma once
// character_query — the owner / universe COLLECTION and COUNT data-rules of the
// Character cluster (gilde.exe). These walk the live-actor array dword_66F0D0
// (512 ptrs @0x66F0D0) and the Person array word_12CE910 (768 records @0x12CE910)
// and bucket / count / collect actors by owning universe, transport state, mesh
// id and proximity. They are pure data/iteration code: the only render/anim leaves
// are routed through a hook (mesh cull-gate read, SetVisible, heightmap tile probe,
// rng), so the collection logic is testable in isolation. Faithful 1:1 ports.
//
// Translated functions (this TU):
//   VIBE_Character_CountActiveUniverse   0x401a9c  (action-node-pool live count)
//   VIBE_Character_CountByOwner          0x401ad4
//   VIBE_Character_CountByOwnerInRange   0x401b40
//   VIBE_Character_CountWithTransport    0x401bd8
//   VIBE_Character_CollectByOwner        0x4b99ac
//   VIBE_Character_CollectNearbyAtTile   0x401c3c  (proximity + repulsion vector)
//   VIBE_Character_IndexFromPointer      0x426724  (universe ptr -> slot index)
//   VIBE_Character_FindFreeSlot          0x4266f4  (free universe slot scan)
//   VIBE_Character_FindByPredicate       0x402314  (find by mesh-name)
//   VIBE_Character_FindByMesh            0x402360  (find by mesh handle)
#include "guild/common/types.h"
#include "sim/types.h"   // LiveActorFlag (kLa*) + the live-actor/universe offset map

namespace guild::sim {

// ===========================================================================
// Live scene-actor (reconstruction record). Named-field view of the 0x204-byte
// ch_t in dword_66F0D0 (see sim/types.h for the byte-offset map this mirrors).
// Only the fields the query/count/collect data-rules touch are modeled; the
// render/anim tail is opaque (mesh / transport handles are MeshHandle*).
// ===========================================================================
struct Universe;   // forward (the +136 universe record)

// Mesh handle view — the query pass reads the cull gate (+533) and world pos
// (+76..+84) of a mesh. The renderer owns the full handle; we model the queried
// fields so the iteration logic is exercisable without a renderer.
struct MeshHandle {
    u8    cullGate;   // +533 (1 == culled/"open" — excluded by the owner counts)
    float pos[3];     // +76,+80,+84 world X,Y,Z
};

struct LiveActor {
    int         slotIndex;     // +0    array index (AllocSlot writes it back)
    int         universeId;    // +44   home-universe id (-1 == wild/no owner)
    int         groupId;       // +48   sub-universe / group id
    MeshHandle* mesh;          // +52   mesh handle (+533 cull, +76 world pos)
    float       targetX;       // +84   redraw target world X (+88 packed, +92 Z)
    float       targetPackedY; // +88   set to 0 on retarget
    float       targetZ;       // +92
    Universe*   universe;      // +136  owning universe record
    u8          flagsA;        // +140  flag byte A (kLa* in sim/types.h)
    MeshHandle* transport;     // +292  transport / secondary mesh (nonzero == has)
    void*       action;        // +296  action-queue head (node+9 == type byte)
    u8          actionType;    // mirror of action node +9 (45 == talk/walk busy)
    MeshHandle* lowPoly;       // +492  low-poly mesh (+533 cull gate)
};

// ===========================================================================
// Universe / scene record (reconstruction record; byte_13ECEC8, stride 984).
// ===========================================================================
struct Universe {
    int id;                 // the universe id (== its slot index)
    u8  noReload;           // +981 reload-skip guard
    u8  flags;              // +982 inflate/log flags
    void* meshHandle;       // +176 collision-grid / heightmap handle (ResolveMesh)
    int   assetHandle;      // +180 source asset for Heightmap_Create
};

// ===========================================================================
// Global arrays (modeled as real arrays; original bases in comments).
// ===========================================================================
constexpr int kLiveCapacity     = 512;   // dword_66F0D0
constexpr int kUniverseSlotCount = 64;   // byte_13ECEC8 / 984

extern LiveActor* g_live[kLiveCapacity];     // dword_66F0D0 @0x66F0D0
extern Universe   g_universes[kUniverseSlotCount]; // byte_13ECEC8 @0x13ECEC8

// Active-scene globals: off_649D64 (active universe ptr), dword_649D60 (active id).
extern Universe* g_activeUniverse;  // off_649D64 @0x649D64
extern int       g_activeUniverseId; // dword_649D60 @0x649D60

// Test/setup helper (not in the original): clears the live array + universes.
void ResetCharacterQuery();

// ===========================================================================
// Query hook — the few render/anim/heightmap leaves the collect pass calls.
// ===========================================================================
struct CharQueryHooks {
    // VIBE_Character_SetVisible(actor, visible). CollectByOwner hides overflow.
    void (*setVisible)(LiveActor* a, int visible);
    // VIBE_Heightmap_WorldToTileWithHeight: probe the tile at `world` on the
    // actor's universe heightmap; fills col,row; returns nonzero if on a tile.
    // CollectNearbyAtTile uses it to validate the repulsion retarget tile.
    int  (*worldToTile)(LiveActor* a, const float world[3], int* col, int* row);
    // Terrain code at (col,row) — CollectNearbyAtTile rejects 0 and 13 (blocked).
    u8   (*terrainAt)(LiveActor* a, int col, int row);
};
void SetCharQueryHooks(const CharQueryHooks* hooks);
const CharQueryHooks& GetCharQueryHooks();

// ===========================================================================
// Counts.
// ===========================================================================

// gilde.exe 0x401a9c — VIBE_Character_CountActiveUniverse. Counts occupied
// action-node-pool slots (dword_62CEFC, 1280 nodes of stride 404): a node is live
// when its step-fn slot (node+0) is nonzero. (Despite the name this counts live
// action nodes, not universes — the loop bound 517120 == 404*1280.) `poolStep0`
// supplies the node-step-fn predicate per slot.
int CountActiveUniverse(const int* poolStep0, int nodeCount);

// gilde.exe 0x401ad4 — VIBE_Character_CountByOwner (eax=ownerUniverse, edx=anyMesh).
// Counts live actors whose universe pointer is &g_universes[ownerUniverse]
// (ownerUniverse==-1 matches any), and whose mesh cull gate (+533) is not 1 unless
// `anyMesh` is set. Returns the count.
int CountByOwner(int ownerUniverse, int anyMesh);

// gilde.exe 0x401b40 — VIBE_Character_CountByOwnerInRange. As CountByOwner, plus a
// VectorWithinTolerance(mesh+76, center, tol) box test (`tol` == a4). Returns the
// count of in-range matching actors.
int CountByOwnerInRange(int ownerUniverse, int anyMesh, const float center[3],
                        float tol);

// gilde.exe 0x401bd8 — VIBE_Character_CountWithTransport. Counts live actors that
// have a redraw target (+292 != 0 in the engine; here transport != null) in the
// requested universe (ownerUniverse==-1 or == active id), optionally also gated by
// the mesh cull gate / action head. Returns the count.
int CountWithTransport(int ownerUniverse, int anyMesh);

// ===========================================================================
// Collection.
// ===========================================================================

// gilde.exe 0x4b99ac — VIBE_Character_CollectByOwner (eax=ownerKeyId). Walks the
// Person array (768): for each Person with a live actor (person[+388]) whose home
// universe id (actor+44) equals the owner key, appends the Person* to a result
// buffer (dword_11BB69C). Beyond the 9th match it hides the overflow actors via
// SetVisible. When ownerKeyId is 0 it collects the "wild" actors (actor+44 == -1).
// Caps at 31 matches / 768 scanned. Returns the match count.
//   `personLiveActor[i]`  == person[i] live-actor ptr (Person+388 column).
//   `outPersons`/`maxOut` == the result buffer.
// NOTE (1:1): the binary indexes the result buffer with a PRE-incremented counter
// (dword_11BB69C[++v6]), so the k-th match (1-based) is stored at outPersons[k];
// outPersons[0] is the reserved slot and is left untouched. Provide a buffer of
// at least 32 entries to hold the full 31-match cap.
int CollectByOwner(int ownerKeyId, LiveActor* const* personLiveActor,
                   int personCount, LiveActor** outPersons, int maxOut);

// gilde.exe 0x401c3c — VIBE_Character_CollectNearbyAtTile. The idle "make room"
// behaviour: gathers up to 15 nearby live actors in the same universe+group whose
// mesh is visible and within radius (50, or 100 if already talking), then for the
// nearest neighbours computes a normalized repulsion vector (scaled by 2.0 then
// 3.0), sets the actor's redraw target (+84..+92), marks the redraw flag (+140|1),
// and validates the destination tile via the heightmap hook (clearing the flag if
// the tile is blocked: terrain 0 or 13). Returns the number of neighbours found.
// The proximity math + repulsion are translated 1:1; the heightmap/terrain leaves
// go through the hook. `rng` supplies VIBE_Util_RandNext() (deterministic in test).
int CollectNearbyAtTile(LiveActor* self, unsigned int (*rng)());

// ===========================================================================
// Index / free-slot / find helpers.
// ===========================================================================

// gilde.exe 0x426724 — VIBE_Character_IndexFromPointer. Maps a universe pointer to
// its slot index ((ptr-base)/984); returns -1 (0xFFFFFFFF) if >= 64.
int IndexFromUniverse(const Universe* u);

// gilde.exe 0x4266f4 — VIBE_Character_FindFreeSlot. Returns the first universe slot
// whose two lead bytes (+0,+1) are both clear, or -1 if all 64 are occupied.
int FindFreeSlot();

// gilde.exe 0x402314 — VIBE_Character_FindByPredicate. Returns the first live actor
// whose mesh-name (+52 read as a string) case-insensitively equals `name`, else
// null. `actorMeshName(a)` supplies the actor's mesh name (the +52 string).
LiveActor* FindByPredicate(const char* name,
                           const char* (*actorMeshName)(LiveActor*));

// gilde.exe 0x402360 — VIBE_Character_FindByMesh. Returns the first live actor
// whose mesh handle (+52) equals `mesh`, else null.
LiveActor* FindByMesh(const MeshHandle* mesh);

} // namespace guild::sim
