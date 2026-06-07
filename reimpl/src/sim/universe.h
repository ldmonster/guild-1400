#pragma once
// universe — the scene-slot ("Universe") manager of the Guild engine (gilde.exe).
//
// A "Universe" is a saved render-scene slot (city exterior / a building interior /
// the world map). The active slot owns the live render globals (object lists,
// camera, fog, heightmap); switching slots saves the active globals back into the
// outgoing slot's 984-byte record and loads the incoming slot's record into the
// live globals. There are up to 64 slots (byte_13ECEC8, stride 984).
//
// Translated functions:
//   VIBE_Universe_SwitchActiveSlot     0x5b4a24  (92 callers — the central swap)
//   VIBE_Universe_CreateDefaultCameras 0x5b5f48  (spawn the MegaCam + ortho cams)
//   VIBE_Universe_ResetCurrentSlot     0x5b44c4  (tear down the active slot)
//   VIBE_Universe_RestoreObjectStates  0x5b43f0  (scene-walk suspend restore)
//   VIBE_Universe_ClearActiveMeshes    0x5b2cd8  (scene-walk mesh clear)
//   VIBE_Universe_InitCameraNode       0x5b46dc  (seed a slot's camera node)
//
// The genuine render/scene leaves (object spawn, heightmap build, fog config,
// scene-graph traversal, DDraw flip) are forward-declared and routed through
// UniverseRenderHooks; the slot bookkeeping and the byte-exact save/restore of the
// per-slot record are translated 1:1 from the Hex-Rays reference of record.
//
// The active-slot id (dword_649D60) is owned by character_query.cpp
// (guild::sim::g_activeUniverseId) and reused here via extern — there is exactly
// one definition of that game global in the tree (ODR).
#include "guild/common/types.h"
#include <cstddef>

namespace guild::sim {

// ===========================================================================
// Universe record (gilde.exe byte_13ECEC8 @0x13ECEC8, stride 984 bytes/slot).
// The save/restore columns SwitchActiveSlot touches are recovered byte-for-byte
// from the `dword_13ECF48[246*slot]` (= record+0x80) column addressing. Offsets
// not used by the save/restore (the camera-node body at +128..+224, the camera
// matrix arrays at +224 and +296) are kept as raw byte spans so the struct stays
// exactly 984 bytes and the touched fields land at their true offsets.
// ===========================================================================
constexpr int kUniverseSlotCapacity = 64;     // a1 >= 64 -> reject
constexpr int kUniverseRecordStride = 984;    // 0x3D8

GUILD_PACKED_BEGIN
struct UniverseRecord {
    u8   head[128];          // +0x000 (+0)   camera-node link block (InitCameraNode body)
    u32  objListHead;        // +0x080 (+128) (col 0x13ECF48) object-list head (13FCF10)
    u32  objListTail;        // +0x084 (+132) (col 0x13ECF4C) object-list tail (13FD140)
    u32  activeCamera;       // +0x088 (+136) (col 0x13ECF50) active camera (649EFC)
    i32  camXform[6];        // +0x08C (+140) (cols 13ECF54..13ECF68) camera[19..21],[33..35]
    u32  renderNodeHead;     // +0x0A4 (+164) (col 0x13ECF6C) render-node head (1408438)
    u32  renderNodeTail;     // +0x0A8 (+168) (col 0x13ECF70) render-node tail (140874C)
    u32  floor;              // +0x0AC (+172) (col 0x13ECF74) floor/terrain (64A028)
    u8   gap0xB0[8];         // +0x0B0 (+176) unused (cols 13ECF78/7C)
    u32  sky;                // +0x0B8 (+184) (col 0x13ECF80) sky handle (64A7C8)
    float clipNear;          // +0x0BC (+188) (col 0x13ECF84) flt_64A074
    float fov0;              // +0x0C0 (+192) (col 0x13ECF88) flt_64A078
    float fov1;              // +0x0C4 (+196) (col 0x13ECF8C) flt_64A07C
    float ang0;              // +0x0C8 (+200) (col 0x13ECF90) flt_64A084
    float ang1;              // +0x0CC (+204) (col 0x13ECF94) flt_64A088
    float ang2;              // +0x0D0 (+208) (col 0x13ECF98) flt_64A08C
    u32  fogColor;           // +0x0D4 (+212) (col 0x13ECF9C) dword_649DD4
    float fogNear;           // +0x0D8 (+216) (col 0x13ECFA0) flt_13FC5FC
    float fogFar;            // +0x0DC (+220) (col 0x13ECFA4) flt_13FC5F8
    u8   camMatrixA[72];     // +0x0E0 (+224) camera matrix block (<->13FD170)
    u8   camMatrixB[672];    // +0x128 (+296) camera matrix block (<->13FD1B8)
    u32  field0x3C8;         // +0x3C8 (+968) reserved
    u32  nearPlane;          // +0x3CC (+972) (col 0x13ED294) dword_649D8C (InitCameraNode=4)
    u32  farPlane;           // +0x3D0 (+976) (col 0x13ED298) dword_649D90 (InitCameraNode=15)
    u8   field0x3D4;         // +0x3D4 (+980) cleared by ResetCurrentSlot (byte_13ED29C)
    u8   noReload;           // +0x3D5 (+981) reload guard (byte_13ED29D)
    u8   reloadFlags;        // +0x3D6 (+982) byte_13ED29E <- byte_649DD0
    u8   visGuard;           // +0x3D7 (+983) byte_13ED29F <- byte_649D70
} GUILD_PACKED;
GUILD_PACKED_END
static_assert(sizeof(UniverseRecord) == kUniverseRecordStride,
              "UniverseRecord must be 984 bytes");
static_assert(offsetof(UniverseRecord, objListHead)    == 128, "objListHead @+128");
static_assert(offsetof(UniverseRecord, activeCamera)   == 136, "activeCamera @+136");
static_assert(offsetof(UniverseRecord, renderNodeHead) == 164, "renderNodeHead @+164");
static_assert(offsetof(UniverseRecord, floor)          == 172, "floor @+172");
static_assert(offsetof(UniverseRecord, sky)            == 184, "sky @+184");
static_assert(offsetof(UniverseRecord, clipNear)       == 188, "clipNear @+188");
static_assert(offsetof(UniverseRecord, fogColor)       == 212, "fogColor @+212");
static_assert(offsetof(UniverseRecord, camMatrixA)     == 224, "camMatrixA @+224");
static_assert(offsetof(UniverseRecord, camMatrixB)     == 296, "camMatrixB @+296");
static_assert(offsetof(UniverseRecord, nearPlane)      == 972, "nearPlane @+972");
static_assert(offsetof(UniverseRecord, reloadFlags)    == 982, "reloadFlags @+982");

// ===========================================================================
// Live render-scene globals (the working set the active slot owns). These mirror
// the scattered 0x64A0xx / 0x13FCxx / 0x13FDxx globals SwitchActiveSlot saves &
// restores. Modeled as one struct so the swap is exercisable without a renderer.
// ===========================================================================
struct RenderGlobals {
    u32   objListHead;   // dword_13FCF10
    u32   objListTail;   // dword_13FD140
    u32   activeCamera;  // dword_649EFC
    i32   camXform[6];   // camera[19..21],[33..35]
    u32   renderNodeHead; // dword_1408438
    u32   renderNodeTail; // dword_140874C
    u32   floor;         // dword_64A028
    u32   sky;           // dword_64A7C8
    float clipNear;      // flt_64A074
    float fov0;          // flt_64A078
    float fov1;          // flt_64A07C
    float ang0;          // flt_64A084
    float ang1;          // flt_64A088
    float ang2;          // flt_64A08C
    u32   fogColor;      // dword_649DD4
    float fogNear;       // flt_13FC5FC
    float fogFar;        // flt_13FC5F8
    u32   nearPlane;     // dword_649D8C
    u32   farPlane;      // dword_649D90
    u8    reloadFlags;   // byte_649DD0
    u8    visGuard;      // byte_649D70
    float frustum;       // flt_64A070 (recomputed from fov + flt_62845C..)
};

// ===========================================================================
// Module globals (modeled as real objects; original bases in comments).
// ===========================================================================
extern UniverseRecord g_universeSlots[kUniverseSlotCapacity]; // byte_13ECEC8
extern RenderGlobals  g_render;                                // the 64A0xx/13FCxx set
extern UniverseRecord* g_activeUniverseRecord;                 // off_649D64

// Camera object handles spawned by CreateDefaultCameras (object ids/ptrs).
extern u32 g_megaCam;   // dword_649EFC
extern u32 g_camOben;   // dword_649EF0
extern u32 g_camVorne;  // dword_649EF4
extern u32 g_camSeite;  // dword_649EF8
extern u8  g_extraCameras; // byte_649D54 (spawn the 3 ortho cams when set)

// dword_649D60 (active slot id) is owned by character_query.cpp.
extern int g_activeUniverseId;

// Test/setup helper (not in the original): zero all slots + the live globals.
void ResetUniverse();

// ===========================================================================
// Render/scene leaves (forward-declared; routed through hooks).
// ===========================================================================
struct UniverseRenderHooks {
    // VIBE_Object_Spawn(type, name) -> camera object handle (CreateDefaultCameras /
    // InitCameraNode). Returns a nonzero opaque handle.
    u32  (*spawnObject)(int type, const char* name);
    // VIBE_Object_LinkIntoScene(handle): attach a spawned object to the scene.
    void (*linkObject)(u32 handle);
    // Heightmap (de)build leaves of SwitchActiveSlot (terrain present-vs-absent).
    void (*configureFog)(float nearV, float farV, u32 color);
    void (*buildTerrain)(u32 floor);
    void (*placeActiveCamera)(u32 camera, const i32* xform);
    // VIBE_SceneGraph_WalkAndInvoke / TraverseTree leaves (RestoreObjectStates,
    // ClearActiveMeshes): walk the active scene applying a per-object op.
    void (*walkRestoreStates)(UniverseRecord* active, void* arg, int flags, u8 mode);
    void (*walkClearMeshes)(UniverseRecord* active);
};
void SetUniverseRenderHooks(const UniverseRenderHooks* hooks);
const UniverseRenderHooks& GetUniverseRenderHooks();

// ===========================================================================
// Functions.
// ===========================================================================

// gilde.exe 0x5b4a24 — VIBE_Universe_SwitchActiveSlot (__usercall, eax=slot, dl=quiet).
// If `slot` >= 64 returns 0. If `slot` is already active returns 1 (when quiet) or
// runs only the present-frame tail. Otherwise: saves the live render globals into
// the active slot's record, loads the target slot's record into the live globals
// (initializing its camera node first time), sets off_649D64 / dword_649D60 to the
// target, re-places the active camera, and — unless `quiet` — (re)builds the
// terrain and reconfigures fog. Returns 1.
bool UniverseSwitchActiveSlot(int slot, bool quiet);

// gilde.exe 0x5b5f48 — VIBE_Universe_CreateDefaultCameras. Spawns the "MegaCam"
// perspective camera (g_megaCam) and, when g_extraCameras is set, the three
// orthographic cams "Oben"/"Vorne"/"Seite" (clearing flag bit 2 at +529 on each).
void UniverseCreateDefaultCameras();

// gilde.exe 0x5b46dc — VIBE_Universe_InitCameraNode(record). Seeds an empty slot's
// camera node (spawns a MegaCam, writes the default near/far = 4/15 at +972/+976
// and the default frustum constants). No-op if the node is already initialized
// (record+128 or record+164 nonzero). Returns true on init.
bool UniverseInitCameraNode(UniverseRecord* rec);

// gilde.exe 0x5b44c4 — VIBE_Universe_ResetCurrentSlot. Tears the active slot down:
// disposes its object lists (hook), clears the floor/sky/fog state to defaults,
// resets the camera arrays, respawns the default cameras, and zeroes the active
// slot's record fields (+0,+972=4,+976=15,+980/+981/+982). Returns the byte offset
// of the active slot's record (984*activeId).
unsigned UniverseResetCurrentSlot();

// gilde.exe 0x5b43f0 — VIBE_Universe_RestoreObjectStates(record, mode). Walks the
// active scene restoring per-object suspend state. Returns 0 if `record` is null,
// else 1.
bool UniverseRestoreObjectStates(UniverseRecord* rec, u8 mode);

// gilde.exe 0x5b2cd8 — VIBE_Universe_ClearActiveMeshes. Walks the active scene
// clearing per-object mesh data. Returns 1.
bool UniverseClearActiveMeshes();

} // namespace guild::sim
