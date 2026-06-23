#pragma once
// =============================================================================
// guild::play — LIVE PERSON -> CHARACTER MODEL resolution + posed-character
// support for the city session render (the person pass of RealCityRenderer).
//
// HOW THE ORIGINAL PLACES A PERSON IN THE 3D CITY (recovered via IDA MCP)
// ---------------------------------------------------------------------------
//   VIBE_Character_SpawnAtBuildingEntrance @0x57c8f0
//     -> person = VIBE_Person_FindRecordById(id)           (the 536-stride table)
//     -> if (*((DWORD*)person + 97)) return                (+388: live char ptr)
//     -> position = the building's "dummy_EINGANG"/"dummy_TUER" scene dummy
//        (VIBE_Object_FindByHandle + VIBE_Transform_PointThroughBoneChain)
//     -> VIBE_Character_SpawnOfficeStaffActor @0x57c744:
//          model = VIBE_Office_ResolveStaffModel(personRow) @0x57c1e8
//          char  = VIBE_Character_CreateFromModel(model->name) @0x402d10
//          *((DWORD*)person + 97) = char                   (+388 char backref)
//          sprintf(*(char+52)/*node*/, "sp_%i", person->id)
//          *(node+512) = person; *(node+72) = *personKindWord | 0x3000000
//          strcpy(person + 496, model->name)               (+0x1F0 stored name)
//          VIBE_Character_ApplyHeadVariant @0x57c548 / ResolveHeadBone @0x57c5d4
//          VIBE_Character_PreloadAniSet(char, 1, "bewegung/gehen") @0x403c34
//
// THE MODEL RESOLVER (VIBE_Office_ResolveStaffModel @0x57c1e8) — reconstructed
// 1:1 here. It reads these PERSON record columns (base word_12CE910, 536-stride):
//   +0x02  kind byte           (byte_12CE912;  17 == reaper/spy -> random spion)
//   +0x09  gender byte         (LOBYTE(dword_12CE919); 0 male / 1 female)
//   +0x0A  adulthood word      ((u16*)((char*)&dword_12CE919)+1)
//   +0x20  adulthood threshold (flt_12CE930; word >= float == adult)
//   +0x164 office byte         (byte_12CEA74; office-model table key)
//   +0x165 profession byte     (byte_12CEA75; profession-model table key)
//   +0x18C texture-set dword   (dword_12CEA9C; -1 == none)
//   +0x1F0 stored model name   (byte_12CEB00; written back on spawn)
// and resolves against SEVEN static 40-byte-record tables (code dword + name[32]
// + 4 texture-variant bytes), all recovered byte-exact via get_bytes:
//   0x63DA78 child boy            0x63DAA0 child girl
//   0x63DAC8 male professions[27] 0x63DF00 female professions[27]
//   0x63E338 male offices[76]     0x63EF18 female offices[76]
//   0x6405E8 reaper/spy[3] (kind 17, random of 3)
//
// THE CHARACTER MESH + ANIMATION ASSETS
//   mesh: the model name loads through Object_AttachToUniverseNode @0x5b3e30 ->
//         Mesh_LoadOrFindByName @0x5d345c ("*<name>.bgf" via the VFS); in the
//         shipped Resources/Objects.BIN the members are
//         "_DYNAMIC/Character/<name>.bgf" (case-insensitive VFS match).
//   anim: VIBE_Character_PreloadAniSet @0x403c34 formats
//         "character/%s/%s_%s.baf" (base, clip, base) where `base` comes from the
//         factory's name decomposition (sim::DecomposeModelName @0x402a4c:
//         "dieb_MANN2" -> base "MANN2"); the factory preloads the gait
//         "bewegung/gehen" and the idle set "stehen/stehen_newnoise".
//
// THE POSED-CHARACTER CHAIN (all real reconstructions, reused):
//   render::LoadAnimation        (VIBE_ModelIo_LoadBinaryAnimation @0x5e450c)
//   render::UpdateSkeletonPose   (VIBE_Anim_UpdateSkeletonPose    @0x5cd1d8)
//   render::SamplePosedMeshSeg   (VIBE_Anim_ComputeMorphWeights   @0x5c9394 +
//                                 VIBE_Math_VectorLerp             @0x5ca2fc)
//   render::CalculateClipNormals (VIBE_Anim_CalculateAnimNormals  @0x5d0020)
//   render::RelightPosedFrame    (0x5d0020 -> VIBE_Mesh_ComputeVertexLighting
//                                 @0x5c9054 glue)
// PersonCharacterPose drives ONE person's idle clip through the REAL pose driver
// (a one-layer SkeletonPoseState whose track/header view is filled from the
// loaded .baf) and samples the posed model-space mesh for the draw pass.
//
// NAMED GAPS (rule 8 — substituted, not faked; see progress doc):
//   * WORLD POSITION: the live spawn position is the building-entrance dummy node
//     (dummy_EINGANG/dummy_TUER) of the person's building in the live universe
//     scene; the portable reimpl has no live universe scene, so RealCityRenderer
//     seats persons on a deterministic grid (the SAME documented substitution its
//     object pass uses for the missing +460/+76 engine state).
//   * VIBE_Character_ApplyHeadVariant @0x57c548 / ResolveHeadBone @0x57c5d4 (the
//     per-person head morph/texture variant) are not reconstructed.
//   * VIBE_Object_SelectTextureSet @0x5b3f54 (the 4 texture-variant bytes ->
//     surface swap) is not reconstructed; the variant bytes are carried in the
//     resolved record for it.
//   * The walk/AI animation SELECTION (which clip a live person plays right now)
//     is the NpcAction/charaction runtime; this pass plays the factory-preloaded
//     idle ("stehen/stehen_newnoise", gait fallback "bewegung/gehen") only.
// =============================================================================
#include "guild/common/types.h"
#include "io/archive_mount.h"
#include "render/agf_anim.h"
#include "render/anim_normals.h"
#include "render/geometry_types.h"
#include "render/skeleton_pose.h"        // render::VertexSource (posedSources_)
#include "render/skeleton_pose_driver.h"

#include <array>
#include <string>
#include <vector>

namespace guild::sim { struct Person; }

namespace guild::play {

// ---------------------------------------------------------------------------
// One 40-byte staff-model record (the engine layout at 0x63DA78..0x640660):
//   +0x00 i32 code (office/profession key; 0 == terminator/default entry)
//   +0x04 char name[32] (the character model name, e.g. "dieb_MANN2")
//   +0x24 u8 texVariants[4] (VIBE_Object_SelectTextureSet variant bytes)
// ---------------------------------------------------------------------------
struct StaffModelRecord {
    i32  code;
    char name[32];
    u8   texVariants[4];
};
static_assert(sizeof(StaffModelRecord) == 40, "engine 40-byte record");

// The recovered tables (bytes via get_bytes; addresses in the .cpp).
const StaffModelRecord& StaffChildBoyModel();          // 0x63DA78
const StaffModelRecord& StaffChildGirlModel();         // 0x63DAA0
const StaffModelRecord* StaffMaleProfessionTable();    // 0x63DAC8 (27 records)
const StaffModelRecord* StaffFemaleProfessionTable();  // 0x63DF00 (27 records)
const StaffModelRecord* StaffMaleOfficeTable();        // 0x63E338 (76 records)
const StaffModelRecord* StaffFemaleOfficeTable();      // 0x63EF18 (76 records)
const StaffModelRecord* StaffReaperTable();            // 0x6405E8 (3 records)

// ---------------------------------------------------------------------------
// The resolver's view of one person row (the exact columns 0x57c1e8 reads).
// MakePersonModelView fills it from a live sim::Person by raw byte offset.
// ---------------------------------------------------------------------------
struct PersonModelView {
    u8          kind = 0;            // +0x02
    u8          gender = 0;          // +0x09 (0 male / 1 female)
    u16         adultWord = 0;       // +0x0A
    float       adultThreshold = 0;  // +0x20
    i8          office = 0;          // +0x164 (compared sign-extended)
    i8          profession = 0;      // +0x165 (compared sign-extended)
    i32         texSet = -1;         // +0x18C
    const char* storedName = nullptr;// +0x1F0 (null/"" == none)
};
PersonModelView MakePersonModelView(const sim::Person* rec);

// Injected random for the kind-17 path (default: util::RandomModulo @0x58b89c).
using StaffRandModulo = int (*)(u16 n);

// gilde.exe 0x57c1e8 — VIBE_Office_ResolveStaffModel (1:1).
// Returns the resolved 40-byte record (a static table entry, or — on the
// stored-name path — one of the engine's 8 rotating scratch records at
// 0x1234610 whose name is the person's +0x1F0 string and whose texVariants are
// all (texSet + 68)). Returns null only for gender outside {0,1} on the
// office/profession/default paths (where the original would dereference a null
// table base — i.e. only for data the original could not survive either).
const StaffModelRecord* ResolveStaffModel(const PersonModelView& v,
                                          StaffRandModulo rnd = nullptr);

// Reset the rotating-scratch state (dword_641FE8 + the 8 records) — test helper.
void ResetStaffModelScratch();

// ---------------------------------------------------------------------------
// Asset-name mapping (the engine's load paths for a resolved model name).
// ---------------------------------------------------------------------------

// Objects.BIN member for a character model name: "_DYNAMIC/Character/<name>.bgf"
// (Mesh_LoadOrFindByName resolves "*<name>.bgf" through the case-insensitive VFS;
// this is where the shipped archive stores every character mesh).
std::string CharacterMeshMemberName(const char* modelName);

// animations.BIN member for (base, clip): "character/<base>/<clip>_<base>.baf"
// — the exact VIBE_Character_PreloadAniSet @0x403c34 sprintf format.
std::string CharacterAnimMemberName(const char* baseName, const char* clipName);

// Case-insensitive member-name lookup over an ArchiveMount member list. Returns
// the EXACT stored member name (for exact-keyed caches) or "" when absent.
std::string FindMemberCaseInsensitive(const std::vector<io::ArchiveMember>& members,
                                      const std::string& wanted);

// ---------------------------------------------------------------------------
// PersonCharacterPose — one person's idle-clip playback through the REAL pose
// driver, sampled into a posed model-space MeshGeometry for the draw pass.
// ---------------------------------------------------------------------------
class PersonCharacterPose {
public:
    PersonCharacterPose() = default;

    // Parse a .baf byte stream (already extracted from animations.BIN) into the
    // clip and build the one-layer SkeletonPoseState over it (forward loop, the
    // factory's anim-set playback mode). Returns false on a malformed stream.
    bool LoadClip(const u8* data, std::size_t size, const char* name);

    // Bind the rest mesh (the decoded character .bgf geometry). Captures the
    // topology (vertex-index triples), the per-vertex UV/flag pass-through, and
    // runs the load-time per-frame normal pass (CalculateClipNormals ==
    // VIBE_Anim_CalculateAnimNormals @0x5d0020) when the clip's morph vertex
    // count matches the mesh. Returns true when posed rendering is possible
    // (clip + mesh bound, counts match).
    bool BindMesh(const render::MeshGeometry* rest);

    bool clipLoaded() const { return clip_.valid; }
    bool poseable()   const { return poseable_; }
    int  fromFrame()  const { return layer_.tracks[0].fromFrame; }
    int  toFrame()    const { return layer_.tracks[0].toFrame; }

    // Advance the pose through the REAL driver (UpdateSkeletonPose @0x5cd1d8):
    // folds `stepTicks` into the track's fractional phase (the host-supplied
    // per-tick rate — the documented +96 rate gap) and runs one driver tick at
    // an internally-advancing time. No-op when not poseable.
    void Advance(float stepTicks);

    // Sample the posed model-space mesh at the track's current (from,to,phase)
    // cursor — SamplePosedMeshSeg (the 0x5c9394 morph-weight blend) — apply the
    // frame's recomputed normals + relight (RelightPosedFrame), and return the
    // posed geometry view (owned; stable until the next Sample/Advance).
    // Returns null when not poseable.
    render::MeshGeometry* SamplePosed();

private:
    render::AnimClip clip_;
    bool             poseable_ = false;

    // The one-layer pose-driver state over the loaded clip.
    render::SkeletonPoseState::Layer layer_{};
    render::SkeletonPoseState        st_{};
    std::vector<i32>                 durations_;
    u32                              timeCursor_ = 0;

    // Rest-mesh capture.
    const render::MeshGeometry*          rest_ = nullptr;
    std::vector<std::array<int, 3>>      triangles_;
    std::vector<std::vector<float>>      frameNormals_;   // per clip frame
    std::vector<render::AnimFrameBounds> frameBounds_;
    std::vector<u8>                      litFlags_;       // vertex +77 bytes

    // Posed output storage (model space).
    std::vector<render::Vertex>       posedVerts_;
    std::vector<render::Polygon>      posedPolys_;
    std::vector<render::VertexSource> posedSources_;
    render::MeshGeometry              posedGeom_{};
};

} // namespace guild::play
