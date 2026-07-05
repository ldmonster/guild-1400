#pragma once
// =============================================================================
// guild::render — FLAG / BANNER / PENNANT ("Wimpel") animation driver.
//
// MISSION CONTEXT (wave-8 W8-CLOTH brief):  the brief asked to reconstruct the
// engine's "cloth / flag wave" — the wind-waving of flags/banners/awnings/sails
// on buildings and market stalls — anticipating a per-vertex sine/cosine grid
// akin to the water wave grid (AnimateWaterWaveGrid @0x5be428).
//
// FINDING (documented precisely, NOT invented — rule 8):
//   Die Gilde does NOT animate flags/banners with a per-vertex cloth-wave grid.
//   The ONLY sine/cosine vertex-displacement grid in the binary is the WATER
//   surface (VIBE_Floor_AnimateWaterVertices @0x5be428 / AnimateWaterWaveGrid),
//   already reconstructed in render/floorwater + render/water_vertices.
//
//   Flags/banners/pennants are SKELETAL .baf bone-animation objects.  The wind
//   wave is baked into the bone animation file and replayed per frame by the
//   skeletal/object-anim pose driver VIBE_Anim_UpdateSkeletonPose @0x5cd1d8
//   (render/skeleton_pose_driver) — exactly the same per-frame animator that
//   drives characters and vegetation.  Evidence (addresses + asset names):
//     * VIBE_Character_AttachFlag        @0x4b5d98 loads the flag animation
//         VIBE_Character_LoadObjectAnimation(node, "sonstiges\\sp_WIMPEL.baf", 1)
//       onto every "dummy_FAHNE" placeholder node.
//     * VIBE_Building_LoadAndAlignGebaeudeModel @0x50d01c loads the building
//       flag mesh "%s*sp_BAU_WIMPEL.ogr" and animates it with the SAME
//       "sonstiges\\sp_WIMPEL.baf".
//     * The city-tower banners load "sonstiges\\wimpel_STADTTURM.baf"
//       (VIBE_Map_SpawnCityTowerMarker @0x52e2b7, ChooseCity @0x52ecf5, ...).
//   .baf == bone-animation file -> skeletal, not vertex-wave.  There is no
//   separate "AnimateClothWaveGrid".  So the genuine reconstructible piece in
//   this module's ownership is the FLAG-ATTACH / FLAG-REFRESH plumbing that
//   wires the animated flag onto a character/building each frame and selects
//   the per-player heraldry texture — i.e. the code that makes a flag exist,
//   wave, and show the right coat of arms.
//
// RECONSTRUCTED HERE 1:1 (the flag cluster around 0x4b5d98):
//   0x4b5d98  VIBE_Character_AttachFlag          (al = fn(name@eax, person@edx))
//   0x4b5e9c  VIBE_Character_ShowFlag            (al = fn(name@eax, player@edi))
//   0x4b5ef8  VIBE_Character_RefreshFlagAnimation(al = fn(obj@eax))
//   0x4b62c0  VIBE_Character_CollectFlagNodes    (al = fn(node@eax, acc@edx))
//
// The deterministic control logic + the heraldry texture-index math are faithful;
// the genuine engine leaves (scene-graph walk, attach-to-node, parent transform,
// texture-set apply, .baf load) are routed through an installable hooks struct
// with INERT defaults so the module links + tests headless.  No new tech (rule 6
// N/A).  The bone-orientation math reuses render/scene_transform MatrixFromEuler.
// =============================================================================
#include "guild/common/types.h"

#include <cstring>

namespace guild::render {

using namespace ::guild;  // u8/i16/u32/...

// --- shared constants --------------------------------------------------------

// gilde.exe 0x4b5dcf — the placeholder bone/node name AttachFlag matches against.
inline constexpr char kDummyFlagName[] = "dummy_FAHNE";
// gilde.exe 0x61deb0 — the animated-flag node/object substring ("sp_WIMPEL").
inline constexpr char kFlagNodeName[]  = "sp_WIMPEL";
// gilde.exe 0x4b5e64 — the flag bone-animation asset attached to the node.
inline constexpr char kFlagAnimFile[]  = "sonstiges\\sp_WIMPEL.baf";

// gilde.exe 0x4b5df2 — AttachFlag sets the attached flag's Euler Y to π
// (1078530011 == (float)0x40490FDB == 3.14159274), a 180° yaw so the pennant
// faces away from the bone; Euler X/Z stay 0.  v11/v13 (x,z)=0, v12 (y)=π.
inline constexpr float kFlagYawPi = 3.14159274101257324f;  // float(0x40490FDB)

// gilde.exe 0x4b5e6b — VIBE_Character_LoadObjectAnimation(node, file, 1) flag arg.
inline constexpr int kFlagAnimLoadMode = 1;

// --- heraldry texture-index math ---------------------------------------------
//
// The flag's coat-of-arms texture is chosen from the person's heraldry index
// (person record +39, a u16).  When that index != 0xFFFF the engine reads the
// person table word_12CE910 (stride 268 words == 536 bytes) at word index
// (268*heraldry + 42) — i.e. byte +84 of the heraldry record — takes its low
// byte, and forms the texture-set selector:
//   AttachFlag (0x4b5e3f):  texIndex = LOBYTE(word_12CE910[268*h + 42]) - 62
//   ShowFlag   (0x4b5ed5):  texIndex = LOBYTE(word_12CE910[268*h + 42]) - 62
// (Both use the same -62 bias; the sibling RefreshAllFlags @0x4b5fa4 building
//  path uses byte-61 on a different record cell — a different code path.)
inline constexpr u16 kNoHeraldry   = 0xFFFF;
inline constexpr int kHeraldryBias = 62;       // 0x4b5e44 sub 62 / 0x3E

// The +84 heraldry byte read from word_12CE910[268*h + 42] (low byte).  The
// person table is engine global state, so the resolved byte is supplied to the
// reconstruction rather than dereferenced here.
//   heraldryByte == LOBYTE(word_12CE910[268*heraldry + 42])
// gilde.exe 0x4b5e3f — the texture-set selector index the engine applies.
inline int FlagTextureSetIndex(u8 heraldryByte) {
    return static_cast<int>(heraldryByte) - kHeraldryBias;
}

// --- substring node-name predicate -------------------------------------------
//
// CollectFlagNodes (0x4b62ce) tests node names with the engine's strstr-equivalent
// VIBE_Util_StrStr @0x5cb930 (matches a NEEDLE anywhere inside the HAYSTACK).
// Reproduced faithfully (the original is a hand-rolled strstr).
//   gilde.exe 0x5cb930 — VIBE_Util_StrStr(haystack, needle) -> ptr or null.
inline bool NameContains(const char* haystack, const char* needle) {
    if (!haystack || !needle) return false;
    if (needle[0] == '\0') return true;          // 0x5cb93c: empty needle -> match
    return std::strstr(haystack, needle) != nullptr;
}

// =============================================================================
// Hooks — the genuine engine leaves the flag cluster calls.  Inert defaults in
// the .cpp so the driver links + tests headless.  Each hook cites its address.
// =============================================================================
struct FlagAnimHooks {
    void* ctx = nullptr;

    // 0x5cb8f0 — VIBE_Util_StrCmpNoCase(a,b): 0 == equal (case-insensitive).
    // Default: case-insensitive strcmp.
    int (*strCmpNoCase)(void* ctx, const char* a, const char* b) = nullptr;

    // 0x5c8b38 — VIBE_Transform_PointThroughBoneChain(node, node+76, out[3]):
    // world position of the node's local origin (the attach point).
    void (*pointThroughBoneChain)(void* ctx, void* node, float out[3]) = nullptr;

    // 0x5b3e30 — VIBE_Object_AttachToUniverseNode(personObj, pos[3], ?, &slot):
    // create/attach the flag object under the person's universe node; returns the
    // new object handle (0 on failure).  `personObj` == person record +97.
    void* (*attachToUniverseNode)(void* ctx, void* personObj, const float pos[3]) = nullptr;

    // 0x5b7e24 — VIBE_Object_ApplyParentTransform(obj@eax, pos@edx, mat@ebx):
    // transforms `pos` through the parent (TransformPointToParent) with the 3x3
    // from MatrixFromEuler, then SetPosition + SetWorldTranslation. The call site
    // @0x4b5e17 passes the SAME bone-chain pos fed to AttachToUniverseNode
    // (edx = &v10) and the matrix in ebx (still live from MatrixFromEuler).
    void (*applyParentTransform)(void* ctx, void* obj, const float pos[3],
                                 const float mat[9]) = nullptr;

    // 0x5b3f54 — VIBE_Object_SelectTextureSet(obj, obj+460 material, 1, texIndex,
    // player): select the heraldry coat-of-arms texture set on the flag object.
    void (*selectTextureSet)(void* ctx, void* obj, int texIndex, void* player) = nullptr;

    // 0x426488 — VIBE_Character_LoadObjectAnimation(obj, "sonstiges\\sp_WIMPEL.baf",
    // 1): load + start the skeletal flag wave animation on the flag object.
    void (*loadObjectAnimation)(void* ctx, void* obj, const char* file, int mode) = nullptr;
};

// =============================================================================
// Per-node / per-object record views.  We model ONLY the cells the flag cluster
// reads/writes; the wider record stays in the engine domain.  Offsets cited.
// =============================================================================

// The person record AttachFlag/ShowFlag walk (the `a2`/edx base, the person whose
// flag is being (re)built).  Person table stride is 536 bytes.
struct FlagPerson {
    void* universeNode = nullptr;  // +97  (dword): the person's universe object node
    u16   heraldry     = kNoHeraldry; // +39 (word): coat-of-arms index (0xFFFF == none)
    u8    heraldryByte = 0;        // LOBYTE(word_12CE910[268*heraldry + 42]) (+84 low byte)
};

// The flag OBJECT record AttachFlag produces / ShowFlag/RefreshFlagAnimation
// inspect.  Only the byte flags the cluster writes are modelled.
struct FlagObject {
    void* handle = nullptr;        // the attached object (attachToUniverseNode result)
    // gilde.exe 0x4b5e58 — *(obj+535) = 4 when a heraldry texture set was applied.
    u8    texSetState = 0;         // +535
    // gilde.exe 0x4b5e76 — *(obj+530) = (old & 0xB3) | 0x44  (set render flags).
    u8    renderFlags530 = 0;      // +530
    // gilde.exe 0x4b5e8a — *(obj+529) &= ~2  (clear bit 1).
    u8    renderFlags529 = 0;      // +529
    bool  animLoaded = false;      // LoadObjectAnimation issued
    int   texSetIndexApplied = -1; // the FlagTextureSetIndex used (-1 == none applied)
};

// =============================================================================
// gilde.exe 0x4b5d98 — VIBE_Character_AttachFlag(name, person).
//   Per-node callback (invoked by RefreshFlagAnimation via the scene-graph walk).
//   If `nodeName` != "dummy_FAHNE" -> no-op, return true (keep walking).  Else:
//     1. pos   = PointThroughBoneChain(node)                       (attach point)
//     2. obj   = AttachToUniverseNode(person.universeNode, pos)    (the flag obj)
//     3. R     = MatrixFromEuler({0, π, 0}); ApplyParentTransform(obj, R)  (180° yaw)
//     4. if person.heraldry != 0xFFFF:
//            SelectTextureSet(obj, FlagTextureSetIndex(person.heraldryByte), player)
//            obj.texSetState (+535) = 4
//     5. LoadObjectAnimation(obj, "sonstiges\\sp_WIMPEL.baf", 1)   (the wave!)
//     6. obj.renderFlags530 = (530 & 0xB3) | 0x44; obj.renderFlags529 &= ~2
//   Returns true (the engine always returns 1 -> keep walking).  `out` receives
//   the produced flag object (handle from the attach hook).
//   `node` is the opaque scene node passed to the position hook; `player` is the
//   render-slot/player passed to SelectTextureSet.
bool AttachFlag(FlagAnimHooks& H, const char* nodeName, void* node,
                const FlagPerson& person, void* player, FlagObject& out);

// gilde.exe 0x4b5e9c — VIBE_Character_ShowFlag(name, player).
//   Per-node callback: if `nodeName` != "sp_WIMPEL" OR person.heraldry == 0xFFFF
//   -> no-op, return true.  Else just re-apply the heraldry texture set on the
//   existing flag object (no re-attach, no re-animate):
//       SelectTextureSet(obj, FlagTextureSetIndex(person.heraldryByte), player)
//   Returns true (keep walking).  `obj` is the existing flag object (the node's
//   object handle == the `a1`/eax base in the original).
bool ShowFlag(FlagAnimHooks& H, const char* nodeName, FlagObject& obj,
              const FlagPerson& person, void* player);

// --- scene-graph walk driver -------------------------------------------------
//
// RefreshFlagAnimation (0x4b5ef8) walks the person's universe node with
// VIBE_SceneGraph_WalkAndInvoke @0x5ac738 invoking AttachFlag per node, gated by
// a build/type byte (byte_12CE912[536*heraldry]) in {5,6,7} and a node flag
// (*(node+528) & 1).  We expose the gate + a small node iterator abstraction so
// the deterministic gating is testable; the scene-graph traversal itself is the
// host's (it owns the live node tree).

// A flat scene node the walk visits (name + opaque node ptr + per-node player).
struct FlagSceneNode {
    const char* name = nullptr;  // node name fed to AttachFlag/ShowFlag
    void*       node = nullptr;  // opaque node ptr for PointThroughBoneChain
};

// gilde.exe 0x4b5ef8 — VIBE_Character_RefreshFlagAnimation(obj).
//   GATE (faithful): proceed only when
//     person.universeNode != 0  (0x4b5efe)
//     AND person.heraldry != 0xFFFF                                  (0x4b5f0d)
//     AND byte_12CE912[536*heraldry] in {5,6,7}  (== `buildType`)    (0x4b5f3b)
//     AND person.universeNode is present.
//   When the node-render-flag bit (*(node+528)&1) is SET it walks invoking
//   AttachFlag (0x4b5f99); otherwise it temporarily clears the node's +496 field,
//   walks, then restores it (0x4b5f57..0x4b5f7d) — a transient suppression of one
//   render field around the rebuild.  `nodeFlagBit528` models *(universeNode+528)&1.
//   For each visited node AttachFlag runs and any produced flag object is written
//   back through `produced` (size `count`).  Returns true when the refresh ran.
bool RefreshFlagAnimation(FlagAnimHooks& H, const FlagPerson& person, u8 buildType,
                          bool nodeFlagBit528, void* player,
                          FlagSceneNode* nodes, int nodeCount,
                          FlagObject* produced /* [nodeCount] */);

// gilde.exe 0x4b5f3b — the RefreshFlagAnimation build-type gate predicate,
// isolated for golden testing: true iff buildType in {5,6,7}.
inline bool FlagBuildTypeEnabled(u8 buildType) {
    return buildType == 5 || buildType == 6 || buildType == 7;
}

// =============================================================================
// gilde.exe 0x4b62c0 — VIBE_Character_CollectFlagNodes(node, acc).
//   Per-node callback used by a separate walk to GATHER the flag nodes (the ones
//   whose name contains "sp_WIMPEL") into a capped list.  Faithful:
//     if NameContains(nodeName, "sp_WIMPEL"):
//         acc[ ++count ] = node;            // acc[0] is the count; nodes follow
//     return count < 32;                    // stop the walk at 32 (0x4b62dd)
//   `acc` is the engine's accumulator: acc[0] == running count, acc[1..] == node
//   ptrs.  Returns true while there is still room (count < 32).
// =============================================================================
inline constexpr int kMaxFlagNodes = 32;  // gilde.exe 0x4b62db (cmp ..., 32)

// `count` is in/out (the acc[0] cell); `out` holds the gathered node ptrs.
bool CollectFlagNodes(const char* nodeName, void* node,
                      void** out, int outCapacity, int& count);

} // namespace guild::render
