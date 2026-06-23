#pragma once
// =============================================================================
// guild::play — REAL CITY RENDER (PLAYABLE_PLAN P2 CAPSTONE / Milestone M2).
//
// Wave 27 built two halves of "draw the city with its REAL geometry":
//   * src/render/agf_loader.* decodes the shipped "BGF\0" AGF meshes out of
//     Resources/Objects.BIN (VIBE_Mesh_LoadBgfFile @0x5d2348 -> the AGF/script
//     fallback VIBE_ModelIo_ReadChunkTag @0x5e44b4),
//   * src/play/real_mesh_source.* wraps that decode in an installable
//     MeshResolver (RealMeshSource + RealMeshResolver) — but NOTHING installs it
//     at runtime, so src/play/object_mesh_render.* still hits its inert
//     DefaultMeshResolver and every object falls back to a flat quad.
//
// This module CLOSES THAT LOOP. Given a mounted real game dir and a loaded world
// (sim::g_objects populated by io::LoadWorld), it:
//   (a) initializes the REAL mesh source over Resources/Objects.BIN (mounts the
//       PKZIP, lazily decodes+caches AGF meshes per member),
//   (b) installs that source as the live object_mesh_render MeshResolver (via the
//       PUBLIC play::InstallRealMeshSource / RealMeshResolver hooks — no owned
//       file is edited) and drives ObjectMeshRenderer over the live entity array
//       so each object draws as its ACTUAL multi-tri geometry (NOT a 2-tri quad),
//       seated at its decoded world placement, into a headless device's surface
//       (MemoryGraphicsDevice / FileDumpGraphicsDevice -> a BMP),
//   (c) supports a screen->world pick that selects the object under a click, by
//       reusing play::scene_pick (the real BeginUniverseFrame camera + projection).
//
// THE ENTITY -> MESH NAME MAP (the wiring this module owns)
// ---------------------------------------------------------------------------
// The portable ObjectRec only carries id+type; the engine's object +0x34/+460
// mesh handle is not in the reimpl record, so the live game's "which .bgf is this
// object" link is the one genuinely-missing datum. We supply a deterministic name
// resolver that maps each live object to a REAL .bgf member of the mounted
// Objects.BIN (chosen from the archive's own member list, so the name ALWAYS
// resolves to real shipped geometry). That is the single inert-substituted policy;
// everything downstream (decode, geometry build, world-seat, project, raster) is
// the real reconstructed path over real bytes. A later agent that recovers the
// per-object +460 handle installs a real name resolver WITHOUT editing this file.
//
// Everything here is additive (new file). It calls only PUBLIC sibling APIs:
// RealMeshSource / InstallRealMeshSource / RealMeshResolver (real_mesh_source.h),
// ObjectMeshRenderer (object_mesh_render.h), SceneNodeWorldPlacement
// (object_transform.h), the scene_pick camera/pick (scene_pick.h). No wiring.cpp /
// hook-table / owned-module edits.
// =============================================================================
#include "guild/common/types.h"
#include "io/archive_mount.h"
#include "play/object_mesh_render.h"
#include "play/person_render.h"
#include "play/real_mesh_source.h"
#include "play/real_texture_source.h"
#include "play/scene_pick.h"
#include "render/light_atmos.h"   // LightAtmosObject / EnsureNodeLit (atmosRelight)
#include "shim/IFileSystem.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace guild::shim { class IGraphicsDevice; }
namespace guild::render { struct Surface; }

namespace guild::play {

// ---------------------------------------------------------------------------
// RealCityRenderer — owns the real mesh source + the per-frame node/name policy
// so the whole "draw the live city with real meshes" loop is one object.
// ---------------------------------------------------------------------------
class RealCityRenderer {
public:
    struct Options {
        int   fbW = 160, fbH = 120;
        u8    clearR = 0, clearG = 0, clearB = 64;   // dark-blue sky clear
        int   maxObjects   = 32;
        float pixelsPerUnit = 0.18f;                 // world units -> pixels
        float eyeX = 0.0f, eyeZ = 0.0f;              // city center -> frame center
        float quadHalf = 18.0f;                      // fallback quad half-extent
        bool  scanObjects = true;
        bool  scanScene   = false;                   // AUGSBURG populates g_objects
        // City spread for the per-object synthetic node placement (the +76 world
        // pos the real SceneNodeWorldPlacement decodes). Cells laid on a grid so
        // distinct objects land at distinct decoded city positions.
        int   gridCols    = 8;
        float cellSize    = 80.0f;
        float originX     = -280.0f, originZ = -200.0f;
        // -- TEXTURED render (default OFF) ----------------------------------
        // When `textured` is set AND a RealTextureSource is installed (InitTextures
        // mounted Resources/Textures.BIN), each object's mesh is sampled from its
        // real per-material Textures.BIN BMP (affine UV, object_mesh_render's
        // textured path). OFF -> the Wave 28 untextured (flat/shaded) frame.
        bool  textured    = false;
        // -- PERSON pass (default OFF -> every existing behaviour unchanged) --
        // When `scanPersons` is set, live sim::g_persons rows (marker != -1,
        // kind < 10) are resolved to their REAL character model through the 1:1
        // VIBE_Office_ResolveStaffModel @0x57c1e8 reconstruction (person_render.h),
        // loaded from Objects.BIN ("_DYNAMIC/Character/<model>.bgf"), posed
        // through the REAL pose driver (UpdateSkeletonPose @0x5cd1d8 over the
        // factory-preloaded idle clip, + per-frame CalculateAnimNormals @0x5d0020
        // + RelightPosedFrame) and drawn in the SAME sorted draw/raster pass as
        // the objects. Persons are seated on their own deterministic grid (the
        // live spawn position is the building-entrance dummy node — the same
        // documented engine-state substitution the object grid is; rule 8).
        bool  scanPersons     = false;
        int   maxPersons      = 16;
        int   personGridCols  = 8;
        float personCellSize  = 26.0f;
        float personOriginX   = -260.0f, personOriginZ = -260.0f;
        // Pose ticks folded into each person's track phase THIS render (the
        // host-supplied per-tick rate of the pose driver; 0 == hold the cursor).
        float personAnimStep  = 0.0f;
        // -- ATMOS LIGHTING (default OFF -> every existing behaviour unchanged) --
        // When set, each resolved mesh frame is run through the reconstructed
        // day/night lighting-table rebuild before the draw: a LightAtmosObject
        // is registered per mesh (the scene-graph membership of
        // VIBE_Light_RefreshAllObjects @0x5c886c) and relit on resolve through
        // the exact VIBE_Render_ProcessSceneNode on-screen trigger arm
        // (render::LightAtmosEnsureNodeLit -> VIBE_Light_BuildObjectCache
        // @0x5c8218). The rebuilt Vertex::lightIdx is what the flat/shaded
        // raster path interpolates into the frame, so the SessionAtmos
        // brightness (applied via play::ApplyAtmosLightingFrame) changes the
        // rendered pixels — the original's day/night application point.
        bool  atmosRelight    = false;
    };

    struct Result {
        bool   mounted        = false;  // Objects.BIN mounted
        std::size_t memberCount = 0;    // .bgf members indexed in the archive
        int    liveObjects    = 0;      // alive g_objects scanned
        int    meshObjects    = 0;      // objects drawn as a RESOLVED real mesh
        int    quadFallbacks  = 0;      // objects that fell back to a quad
        int    meshTris       = 0;      // polygons sourced from resolved meshes
        int    appendedPolys  = 0;      // draw-list entries projected+sorted
        int    rasterTris     = 0;      // triangles RasterizeMeshList flushed
        int    maxMeshVerts   = 0;      // largest resolved mesh vertex count (>4 == real)
        int    distinctMeshes = 0;      // distinct .bgf names that resolved to geometry
        int    nonClearPixels = 0;      // pixels != the sky-clear colour
        bool   presented      = false;  // the device present() succeeded
        std::string bmpPath;            // dumped BMP (when a FileDumpGraphicsDevice)
        bool   usedRealResolver = false;// the RealMeshResolver was the installed hook
        bool   textured       = false;  // the textured raster path was used
        int    texturedPolys  = 0;      // poly triangles bound to a real texture
        int    distinctColors = 0;      // distinct non-clear pixel colours
        // -- person pass (all 0 unless Options::scanPersons) ------------------
        int    livePersons    = 0;      // live person rows scanned (marker/kind gate)
        int    personMeshes   = 0;      // persons drawn as their RESOLVED character mesh
        int    personPosed    = 0;      // ... of which drawn as a POSED (anim-chain) mesh
        int    personRestPose = 0;      // ... drawn at the static rest pose (named gap)
        int    personQuads    = 0;      // persons that fell back to a quad (no mesh)
        bool   animsMounted   = false;  // animations.BIN was available for the pose chain
        // -- atmos lighting pass (all 0 unless Options::atmosRelight) ----------
        int    relitMeshes    = 0;      // BuildObjectCache rebuilds this render
        u32    lightSerial    = 0;      // render::LightAtmos().rebuildSerial seen
    };

    RealCityRenderer() = default;
    ~RealCityRenderer();

    // (a) Mount Resources/Objects.BIN through `fs` and ready the AGF mesh source.
    // `archivePath` defaults to the shipped Objects.BIN. Returns true on mount.
    bool Init(shim::IFileSystem* fs,
              const char* archivePath = "Resources/Objects.BIN",
              bool caseInsensitive = false);

    bool mounted() const { return src_.mounted(); }
    RealMeshSource& source() { return src_; }

    // Mount Resources/Textures.BIN so Render(opt.textured=true) samples each
    // object's real per-material BMP. Returns true on mount. Optional; without it
    // a textured render falls back to untextured (no table -> flat path).
    bool InitTextures(shim::IFileSystem* fs,
                      const char* archivePath = "Resources/Textures.BIN");
    bool texturesMounted() const { return tex_.mounted(); }
    RealTextureSource& textures() { return tex_; }

    // Mount Resources/animations.BIN so the person pass can pose characters
    // through the real anim chain (.baf idle clips). Optional: without it (or
    // when a person's clip member is absent) persons draw at the static rest
    // pose (Result::personRestPose counts them; named gap). Returns true on mount.
    bool InitPersonAnims(shim::IFileSystem* fs,
                         const char* archivePath = "Resources/animations.BIN");
    bool personAnimsMounted() const { return animsMounted_; }

    // Public adapter the free-function TexTableResolver trampoline calls: build
    // (cache) and return the per-poly MaterialTextureTable for `e`'s mesh.
    const MaterialTextureTable* TexTableForPublic(const EntityRef& e);

    // The real .bgf member names this renderer maps objects onto (filled by Init
    // from the mount's member list). Empty until Init mounts an archive.
    const std::vector<std::string>& meshNames() const { return meshNames_; }

    // (b) Install the REAL mesh source as object_mesh_render's MeshResolver, build
    // a per-object node placement, and render ONE frame of the LIVE world (the
    // populated sim::g_objects) as real meshes into `device` (already init()'d to
    // opt.fbW x opt.fbH x 16bpp). Returns the per-frame counts. Restores the
    // previously-installed mesh source/name-resolver on return (no global leak).
    Result Render(const Options& opt, shim::IGraphicsDevice& device);

    // (c) Pick the live object under screen (sx, sy): build the scene-pick roster
    // from the SAME decoded world placements the render used, project through the
    // real city-view camera, and return the nearest object within `pickRadius`.
    // Reuses play::scene_pick (PickSceneObject). A miss returns {index=-1,id=0}.
    ScenePickResult Pick(const Options& opt, float sx, float sy,
                         float pickRadius = 24.0f) const;

    // ------------------------------------------------------------------------
    // PERSON ROSTER — one entry per person the last Render(scanPersons=true)
    // resolved+drew: the person id/slot, the resolved model + archive member,
    // the world seat, the screen-projected centre (the SAME city-view camera
    // Pick uses) and a pick radius from the drawn mesh's projected extent. The
    // session's pick/selection layer consumes this to include persons.
    // ------------------------------------------------------------------------
    struct PersonRosterEntry {
        i32         id   = 0;          // sim::Person::id
        int         slot = -1;         // g_persons slot
        std::string model;             // resolved character model ("dieb_MANN2")
        std::string member;            // archive member (.bgf) actually loaded
        float       world[3] = {0, 0, 0};
        float       screenX = 0.0f, screenY = 0.0f;
        bool        onScreen = false;
        float       radius = 0.0f;     // pick radius, px (projected mesh extent)
        bool        posed = false;     // drawn through the live anim chain
    };
    const std::vector<PersonRosterEntry>& personRoster() const { return roster_; }

    // Pick the person under screen (sx, sy) from the LAST rendered roster
    // (projected through the same camera the render used). Each person's own
    // projected radius widens the hit test; `extraRadius` pads it. A miss
    // returns {index=-1,id=0}; index is the roster index on a hit.
    ScenePickResult PickPerson(const Options& opt, float sx, float sy,
                               float extraRadius = 4.0f) const;

    // Public adapters the free-function object_mesh_render hooks invoke (a free
    // function can't capture `this`, so the active-renderer trampoline calls these).
    const void* NodeForPublic(const EntityRef& e, const Options& opt) const;
    std::string NameForPublic(const EntityRef& e) const;
    // Person-aware mesh adapter: posed geometry for a prepared person, else the
    // static rest mesh, else null (-> quad fallback). Objects route to the
    // ordinary RealMeshResolver.
    const render::MeshGeometry* MeshForPersonPublic(const EntityRef& e);
    // Atmos-relight adapter (Options::atmosRelight): register `g`'s
    // LightAtmosObject and run the on-screen ProcessSceneNode rebuild arm over
    // it (render::LightAtmosEnsureNodeLit). Returns `g` (relit in place).
    const render::MeshGeometry* RelightForPublic(const render::MeshGeometry* g);

private:
    RealMeshSource           src_;
    RealTextureSource        tex_;   // Resources/Textures.BIN (optional, textured path)
    // A parallel mount used ONLY to enumerate the archive's member NAMES (the
    // RealMeshSource decodes by name but does not expose its member list); the
    // names harvested here are the object->mesh map and the Resolve() keys.
    io::ArchiveMount         names_;
    std::vector<std::string> meshNames_;   // real .bgf members, object -> name map

    // Per-object decoded world placement (filled by Render, read by Pick).
    struct ObjPlace { i32 id; float pos[3]; };
    mutable std::vector<ObjPlace> placements_;
    mutable Options               lastOpt_{};

    // Build the synthetic engine node bytes for object slot `slot`/id `id` seated
    // at its grid world position (the +76 pos / +396 yaw / +533 type the real
    // SceneNodeWorldPlacement decodes). Returns a pointer into node storage.
    const void* nodeFor(const EntityRef& e, const Options& opt) const;
    std::string nameFor(const EntityRef& e) const;

    // Node-buffer storage reused across the frame's resolves.
    mutable std::vector<unsigned char> nodeStore_;
    mutable int                        nodeUsed_ = 0;

    // ---- PERSON pass state -------------------------------------------------
    // Per-slot prepared person: the resolved model/member, the grid seat, the
    // pose state (idle clip through the real driver) and this frame's sampled
    // posed geometry. Built by PreparePersons() at the top of a scanPersons
    // Render(); read by the resolver adapters during the draw.
    struct PersonDraw {
        i32         id = 0;
        int         slot = -1;
        int         gridIdx = 0;
        std::string model;             // resolved staff-model name
        std::string member;            // exact Objects.BIN member (.bgf)
        std::string base;              // decomposed base ("MANN2") for anim paths
        float       world[3] = {0, 0, 0};
        std::unique_ptr<PersonCharacterPose> pose;
        render::MeshGeometry* framePosed = nullptr;  // this frame's posed view
        bool        posed = false;     // framePosed valid this frame
    };
    io::ArchiveMount               anims_;          // Resources/animations.BIN
    bool                           animsMounted_ = false;
    std::map<int, PersonDraw>      personDraws_;    // keyed by g_persons slot
    std::vector<PersonRosterEntry> roster_;
    mutable Options                lastPersonOpt_{};

    // ---- ATMOS LIGHTING pass state (Options::atmosRelight) ----------------
    // One LightAtmosObject per distinct mesh frame this renderer has drawn —
    // the registered scene membership the rebuild walks. Unregistered in the
    // destructor (no global leak).
    std::map<const render::MeshGeometry*,
             std::unique_ptr<render::LightAtmosObject>> lightObjs_;
    int relitThisFrame_ = 0;   // BuildObjectCache runs in the current Render()
    int frameCounter_   = 0;   // the dword_62EB38 stand-in stamped into rebuilds

    // Scan live persons, resolve models/meshes/clips, advance + sample poses,
    // seat the grid, and build the roster. Returns the live-person count.
    int PreparePersons(const Options& opt);
    const void* personNodeFor(const EntityRef& e, const Options& opt) const;
};

// ---------------------------------------------------------------------------
// Free-function convenience entry (mirrors RunPlayableSlice's shape): mount, load
// is the caller's job; this renders the already-loaded live world to `device`.
// ---------------------------------------------------------------------------
RealCityRenderer::Result RenderRealCity(shim::IFileSystem* fs,
                                        const RealCityRenderer::Options& opt,
                                        shim::IGraphicsDevice& device,
                                        const char* archivePath = "Resources/Objects.BIN");

} // namespace guild::play
