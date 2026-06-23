// See wire_scene_fx.h. Binds the reconstructed pure-logic scene/fx leaves into
// the globally-installable hook seams that were otherwise inert at runtime.
#include "render/wire_scene_fx.h"

#include "render/fxrecon_particle_mirror_shadow.h"  // Set{BuildBasis,MatrixToEuler,SetWorldTranslation}Hook
#include "render/modelio_recon.h"                    // SetModelIoBioHooks / ModelIoBioHooks
#include "render/render_recon_objlist.h"             // ObjListHooks().floorReloadTextures (0x5b9ef4 SetGammaTable edge)
#include "render/floor_reload.h"                     // guild::render::ReloadTextures (0x5bd2d8) / FloorTileAccess

#include "util/matrix.h"           // guild::util::BuildBasisFromAngle / MatrixToEuler
#include "sim/object_lifecycle3.h" // guild::sim::ObjectSetWorldTranslation / SceneNode3
#include "io/worldio.h"            // guild::io::BioReadByte / BioReadDword
#include "io/file.h"               // guild::io::VfsHandle

namespace guild::render {
namespace {

// ---------------------------------------------------------------------------
// Thin adapters bridging the reconstructed-leaf prototypes to the hook signatures.
// Each is shape-identical to the original callee (same operation, same arg order);
// only the opaque pointer / return-int widening differs across the hook seam.
// ---------------------------------------------------------------------------

// fxrecon SetWorldTranslationHook : u8 (*)(void* objField, const float euler[3]).
// 0x5af50c — VIBE_Object_SetWorldTranslation(node, angles). The reconstruction
// takes a typed SceneNode3*; the original al-return char widens to u8.
u8 WorldTranslationAdapter(void* objField, const float euler[3]) {
    return static_cast<u8>(guild::sim::ObjectSetWorldTranslation(
        reinterpret_cast<guild::sim::SceneNode3*>(objField), euler));
}

// modelio ModelIoBioHooks.readByte : int (*)(void* stream, u8* dst), returns bytes
// read. 0x5dc850 — VIBE_Bio_ReadByte. The reconstruction returns bool (full read);
// the thunks consume it as the original byte-count return (1 on success, 0 fail).
int BioReadByteAdapter(void* stream, guild::u8* dst) {
    return guild::io::BioReadByte(reinterpret_cast<guild::io::VfsHandle*>(stream), dst)
               ? 1 : 0;
}

// modelio ModelIoBioHooks.readDword : int (*)(void* stream, u32* dst).
// 0x5dc894 — VIBE_Bio_ReadDword. Returns 4 on a full read (the original's count), 0 fail.
int BioReadDwordAdapter(void* stream, guild::u32* dst) {
    return guild::io::BioReadDword(reinterpret_cast<guild::io::VfsHandle*>(stream), dst)
               ? 4 : 0;
}

// ObjListHooks().floorReloadTextures : int (*)(u32 floorHandle).
// 0x5bd2d8 — VIBE_Floor_ReloadTextures. In the original, SetGammaTable
// (0x5b9ef4) calls Floor_ReloadTextures(dword_64A028) and, for each non-zero
// world-floor record dword_13ECF74[i], Floor_ReloadTextures(rec) — where each
// argument IS the 32-bit floor-record pointer. The reconstructed body lives in
// floor_reload.cpp as ReloadTextures(void* floor, const FloorTileAccess&); this
// adapter restores the original's pointer-as-handle call and routes the floor's
// tile-field accessors through FloorTileAccess. Under the headless build no
// floor records / tile surfaces exist, so the accessor table is inert: the
// 8 slots x 3 mips iterate but every tile load is skipped (null surface) — the
// faithful behaviour for a SetGammaTable with no live floor (returns 0). A real
// floor + texture backend supplies the live FloorTileAccess + BMP loader.
int FloorReloadTexturesAdapter(guild::u32 floorHandle) {
    FloorTileAccess acc{};   // no live floor-tile fields headless: inert iteration.
    return ReloadTextures(reinterpret_cast<void*>(floorHandle), acc);
}

} // namespace

void InstallRealSceneFxWiring() {
    // --- fxrecon pure-math / transform leaves -------------------------------
    // BuildBasisFromAngle and MatrixToEuler match the hook prototypes directly
    // (void(const float*,float,float*) and void(float*)); pass them straight in.
    guild::render::fxrecon::SetBuildBasisHook(&guild::util::BuildBasisFromAngle);   // 0x5ca544
    guild::render::fxrecon::SetMatrixToEulerHook(&guild::util::MatrixToEuler);      // 0x5cb2cc
    guild::render::fxrecon::SetSetWorldTranslationHook(&WorldTranslationAdapter);   // 0x5af50c

    // --- modelio Bio reader thunks (reconstructed VFS binary primitives) ----
    ModelIoBioHooks bio{};
    bio.readByte  = &BioReadByteAdapter;   // 0x5dc850
    bio.readDword = &BioReadDwordAdapter;  // 0x5dc894
    SetModelIoBioHooks(bio);

    // --- SetGammaTable -> Floor_ReloadTextures edge (0x5b9ef4 -> 0x5bd2d8) ---
    // Connect the reconstructed gamma/quality-change reload sweep to the
    // reconstructed per-floor texture reload along the original's exact call
    // edge. (activeFloor/forEachFloorRecord remain inert, so under headless
    // wiring SetGammaTable never produces a live floor handle; the edge is wired
    // and exercised directly via the hook in tests.)
    ObjListHooks().floorReloadTextures = &FloorReloadTexturesAdapter;   // 0x5bd2d8

    // All other scene/fx leaves are GPU (rule 3) / platform I/O (rule 4) /
    // allocator (rule 6) / unverified-prototype — left at their inert defaults.
}

} // namespace guild::render
