#pragma once
// =============================================================================
// guild::play — UNIVERSE FRAME DRIVER (live-loop stand-up).
//
// The reconstructed engine per-frame spine and all its leaves exist and are
// individually tested; what was missing is DRIVING REAL GAME GEOMETRY through the
// engine frame orchestration. `RealFrameScene` (app/wiring.cpp) already drives the
// spine, but over a SYNTHETIC quad / hex-fan. This module closes that: it drives a
// REAL Objects.BIN mesh (the engine's object +460 geometry) through the genuine
// frame spine end-to-end:
//
//   render::RenderMainViewFrame  @0x5B6074  (gate + clear)
//     -> render::RenderUniverseFrame  @0x5B3DE8
//        -> render::BeginUniverseFrame @0x5B3900
//             clearRect hook            -> SurfaceColorFill (sky)
//             sceneWalk  hook           -> render::ProjectVerticesToScreen
//                                          + the REAL per-node dispatch
//                                          render::ProcessSceneNodeAppend (driven by
//                                          play::WalkSceneTree, InstallRealSceneBridge)
//                                          + render::RadixSortDrawList
//     -- then (caller-driven, like RealSubsystems::renderMainViewFrame) --
//   render::RasterizeMeshList   @0x5AEC88  -> software Surface
//
// The geometry is the REAL shipped .bgf decoded by play::RealMeshSource (the same
// real decode real_city_render uses) — no synthetic stand-in (rule 8). Headless:
// renders into an owned render::Surface; a guarded e2e dumps it to a BMP and asserts
// real multi-tri geometry was projected, dispatched, and rasterized.
//
// The render::FrameHooks are plain C function pointers (the engine passed its
// callbacks in registers), so — exactly like RealFrameScene — the driver threads
// itself through a process-global active pointer + free trampolines. One driver may
// render at a time (the engine had one set of global PolyLists too).
// =============================================================================
#include "guild/common/types.h"
#include "play/real_mesh_source.h"      // RealMeshSource (real .bgf -> MeshGeometry)
#include "play/real_texture_source.h"   // RealTextureSource / MaterialTextureTable
#include "play/terrain_render.h"        // FloorGround / GroundFrame (ground pass)
#include "render/geometry_types.h"      // MeshGeometry / DrawListEntry
#include "render/scene.h"               // DrawListBuffers
#include "render/texture.h"             // render::Texture (palettized texel record)

#include <string>
#include <vector>

namespace guild::render { struct Surface; }
namespace guild::shim { class IFileSystem; class IGraphicsDevice; }

namespace guild::play {

class UniverseFrameDriver {
public:
    struct Options {
        int fbW = 160, fbH = 120;
        u8  clearR = 0, clearG = 0, clearB = 64;   // dark-blue sky clear
        float margin = 0.12f;                       // fit inset (fraction of each FB dim)
        bool doubleSided = true;                    // objFlags530 0x40 (append both windings)
        // The view-camera euler (pitch,yaw,roll) the engine applies to orient an
        // object before the affine projection (view = MatrixFromEuler(euler) * (v -
        // center), exactly VIBE_Math_MatrixFromEuler @0x5cb1bc). Default = the game's
        // isometric tilt so a model is shown in recognizable 3D, not flat top-down.
        float viewEuler[3] = {-0.62f, 0.78f, 0.0f};
        // When set, project through the GENUINE universe-object PERSPECTIVE leaf
        // VIBE_Render_ProjectObjectVertices @0x5ac970 (1/z divide) over a synthesized
        // viewport, instead of the affine ProjectVerticesToScreen @0x5c5120 stand-in.
        bool perspective = false;

        // GROUND PASS (terrain-ground wave 4; default unbound == byte-identical
        // frames). When `ground` points at a valid play::FloorGround, the frame
        // spine takes the BeginUniverseFrame @0x5B3900 terrain arm (0x5b3a2f,
        // frame.cpp fs.hasTerrain) and draws the floor through the 0x5bf22c walk
        // BEFORE the object walk, under the camera below (the engine camera
        // model: eye + euler, view = Transpose(MatrixFromEuler(-rot))*(w-eye))
        // and the engine projection scalars (scale = fbW/2 at W/2,H/2).
        const FloorGround* ground = nullptr;
        float groundEye[3] = {0.0f, 0.0f, 0.0f};
        float groundRot[3] = {0.0f, 0.0f, 0.0f};
        float groundNearZ  = 10.0f;     // flt_13FC76C class near plane
        float groundFarZ   = 20000.0f;  // far plane (fog-far default)
    };

    struct Result {
        bool real = false;            // geometry resolved AND is real (>4 verts)
        int  meshVerts = 0;           // resolved mesh vertex count
        int  meshPolys = 0;           // resolved mesh polygon count
        int  appendedPolys = 0;       // entries the BeginUniverseFrame scene walk appended
        int  nodesDispatched = 0;     // nodes render::ProcessSceneNodeAppend dispatched
        int  rasterTris = 0;          // triangles render::RasterizeMeshList flushed
        int  nonClearPixels = 0;      // pixels != the sky clear colour
        // -- textured path --------------------------------------------------
        bool textured = false;        // the real Textures.BIN material bind ran this frame
        int  boundMaterials = 0;      // materials bound to a real render::Texture
        int  texturedPolys = 0;       // polys rasterized via RasterizeTexturedTriangleRgbz
        int  distinctColors = 0;      // distinct non-clear pixel colours (texture variety)
        // -- ground pass (0/false unless Options::ground was bound) ----------
        bool terrainDrawn      = false;
        int  terrainTiles      = 0;   // tiles with a nonzero LOD this frame
        int  terrainPolys      = 0;   // entries the 0x5bf22c walk appended
        int  terrainRasterTris = 0;   // polys the 0x5AEC88 flush iterated
    };

    ~UniverseFrameDriver();

    // Mount Resources/Textures.BIN so the loaded mesh's materials bind their REAL
    // per-material BMP (the textured raster path). Optional; without it meshes render
    // untextured (flat/shaded). Returns true on mount.
    bool InitTextures(shim::IFileSystem* fs,
                      const char* archivePath = "Resources/Textures.BIN");
    bool texturesMounted() const;

    // Resolve a REAL mesh by Objects.BIN member name through `src` (already mounted).
    // Returns true when geometry resolved (vertex + polygon counts > 0). The geometry
    // is owned/cached by `src`; this driver holds a borrowed pointer for the frame.
    // When textures are mounted, also builds the per-material render::Texture bind.
    bool LoadMember(RealMeshSource& src, const char* memberName);

    // Drive ONE engine frame over the loaded mesh into the owned Surface; returns the
    // per-frame counts. A no-op Result if no mesh is loaded.
    Result RenderFrame(const Options& opt);

    // Blit the rendered Surface into `device`'s backbuffer and present() it — the
    // present-layer swap (rule 3/4): headless with MemoryGraphicsDevice, ON-SCREEN
    // with VulkanGraphicsDevice+SdlVulkanPlatform. Returns true when present() ran.
    bool PresentToDevice(shim::IGraphicsDevice& device);

    render::Surface* surface() const { return fb_; }

    // ---- internal step bodies (public only so the C FrameHook trampolines + the
    // textured SpanFill can reach them; not part of the intended call surface) -----
    void doClear();
    int  doSceneWalk();
    void doFlush();
    void doRenderTerrain(char a2);   // the renderTerrain hook body (0x5b3a2f arm)

    // A material's bound texel record + 565 palette LUT (per matIndex). The textured
    // SpanFill reads these via the active-driver global.
    struct BoundTex {
        const render::Texture* tex = nullptr;  // texel record (or null = untextured)
        const u16*             palette = nullptr; // 256-entry 565 LUT
        // 24-bit (non-palettized) BMP stand-in bind (materials-wave4 handoff (b)):
        // set ONLY when render::Rgb24MaterialStandInEnabled() — the EXPLICIT,
        // default-OFF stand-in for the unreconstructed software palettizer
        // VIBE_Texture_PalettizeSurface @0x5da34c. Sampled by UFD_SpanTextured via
        // play::RasterTexturedTriangleAffine (the person-pass kernel).
        const render::DecodedBmp* rgb = nullptr;
    };
    const BoundTex* boundTexFor(i32 matIndex) const;
    void addTexturedPoly() { ++texturedPolysThisFrame_; }

private:
    render::MeshGeometry* geom_ = nullptr;   // borrowed (owned by the RealMeshSource)
    render::Surface*      fb_ = nullptr;
    Options               opt_{};

    std::vector<render::DrawListEntry> pool1_;   // db.base1 (final sorted draw list)
    std::vector<render::DrawListEntry> pool2_;   // db.base2 (radix ping-pong)
    std::vector<render::DrawListEntry> projScratch_;  // ProjectVerticesToScreen sink (discarded)
    render::DrawListBuffers db_{};

    // View-transformed geometry copy (the engine's per-object view transform output:
    // MatrixFromEuler(viewEuler) * (v - center)); projected in place each frame so the
    // cached source mesh is never mutated.
    std::vector<render::Vertex>  viewVerts_;
    std::vector<render::Polygon> viewPolys_;
    render::MeshGeometry         viewGeom_{};
    void buildViewGeometry();

    int lastNodesDispatched_ = 0;
    int lastRasterTris_ = 0;
    int texturedPolysThisFrame_ = 0;

    // -- ground pass state (terrain-ground wave 4) -----------------------------
    GroundFrame       groundFrame_;
    GroundRenderStats groundStats_{};
    bool              groundDrawn_ = false;

    // -- textured material bind (built by LoadMember from Textures.BIN) -----------
    RealTextureSource     tex_;
    std::vector<render::Texture> matTex_;        // per-material texel record
    std::vector<std::vector<u16>> matPalette_;   // per-material 565 palette LUT
    std::vector<BoundTex> boundTex_;             // indexed by material index
    int                   boundMaterials_ = 0;

    void buildTextureBind(RealMeshSource& src, const char* memberName);
};

} // namespace guild::play
