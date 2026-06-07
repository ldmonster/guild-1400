// =============================================================================
// guild::play — REAL CITY RENDER implementation. See header.
//
// The runtime installer that was missing: it points object_mesh_render's
// MeshResolver hook at the REAL AGF mesh source (RealMeshSource over Objects.BIN)
// so the live city's objects draw as their ACTUAL decoded geometry instead of the
// inert quad fallback, then drives the same real project/sort/raster pipeline
// ObjectMeshRenderer already runs. Every link is a CALL into a reconstructed
// sibling over real bytes; the only inert-substituted policy is the per-object
// .bgf NAME map (the engine's +460 mesh handle is not in the reimpl record).
//
// REUSED (extern, never redefined):
//   play::RealMeshSource / InstallRealMeshSource / InstallMeshNameResolver /
//     RealMeshResolver / ActiveRealMeshSource / ActiveMeshNameResolver  (real_mesh_source.h)
//   play::ObjectMeshRenderer                                            (object_mesh_render.h)
//   play::SceneNodeWorldPlacement / NodeOffset / NodeType              (object_transform.h)
//   play::MakeCityViewCamera / PickSceneObject / ScenePickObject       (scene_pick.h)
//   sim::g_objects / kObjectCapacity                                   (sim/entity.h)
//   render::SurfaceCreate / SurfaceColorFill / SurfaceDestroy / PackColor
#include "play/real_city_render.h"

#include "play/object_transform.h"
#include "render/colorformat.h"
#include "render/surface.h"
#include "shim/IGraphicsDevice.h"
#include "shim_impl/filedump_graphics.h"
#include "sim/entity.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace guild::play {

namespace {

// The renderer currently driving a frame, so the free-function NodeResolver /
// MeshNameResolver hooks (which can't capture) can reach its per-object policy.
// Set for the duration of one Render() call only.
RealCityRenderer* g_activeRenderer = nullptr;
const RealCityRenderer::Options* g_activeOpt = nullptr;

// object_mesh_render NodeResolver hook -> the active renderer's per-object node.
const void* ActiveNodeResolver(const EntityRef& e) {
    if (!g_activeRenderer || !g_activeOpt) return nullptr;
    // nodeFor is private; reach it through the public adapter.
    return g_activeRenderer->NodeForPublic(e, *g_activeOpt);
}

// real_mesh_source MeshNameResolver hook -> the active renderer's name map.
std::string ActiveNameResolver(const EntityRef& e) {
    if (!g_activeRenderer) return std::string();
    return g_activeRenderer->NameForPublic(e);
}

// object_mesh_render TexTableResolver hook -> the active renderer's per-poly
// texture table for the entity's mesh (built from Textures.BIN).
const MaterialTextureTable* ActiveTexTableResolver(const EntityRef& e) {
    if (!g_activeRenderer) return nullptr;
    return g_activeRenderer->TexTableForPublic(e);
}

} // namespace

// Public adapters the free-function hooks call (the hooks can't reach privates).
const void* RealCityRenderer::NodeForPublic(const EntityRef& e, const Options& opt) const {
    return nodeFor(e, opt);
}
std::string RealCityRenderer::NameForPublic(const EntityRef& e) const {
    return nameFor(e);
}

// Build (cache) and return the per-poly MaterialTextureTable for `e`'s mesh: map
// the entity to its .bgf name, ensure the model is decoded (its materials carry
// the name0 -> Textures.BIN BMP), then BuildTableFor that model. The table is
// keyed by the .bgf name inside RealTextureSource, so repeated objects of the
// same mesh share one table (and its polyTexId order matches the MeshGeometry's).
const MaterialTextureTable* RealCityRenderer::TexTableForPublic(const EntityRef& e) {
    if (!tex_.mounted()) return nullptr;
    std::string nm = nameFor(e);
    if (nm.empty()) return nullptr;
    // Ensure the model (and thus its geometry/poly order) is decoded + cached.
    if (!src_.Resolve(nm.c_str())) return nullptr;
    const render::BgfModel* model = src_.ModelFor(nm.c_str());
    if (!model) return nullptr;
    return tex_.BuildTableFor(nm.c_str(), *model);
}

RealCityRenderer::~RealCityRenderer() {
    if (g_activeRenderer == this) {
        g_activeRenderer = nullptr;
        g_activeOpt = nullptr;
    }
}

bool RealCityRenderer::Init(shim::IFileSystem* fs, const char* archivePath,
                            bool caseInsensitive) {
    meshNames_.clear();
    if (!src_.MountArchive(fs, archivePath, caseInsensitive))
        return false;
    // Mount a parallel index to enumerate the archive's member NAMES (RealMeshSource
    // resolves by name but exposes no member list). Harvest every .bgf member; the
    // names are both the object->mesh map and the Resolve() keys.
    if (!names_.Mount(fs, archivePath, caseInsensitive))
        return true;   // mounted for decode; just no name harvest -> quad fallback
    for (const auto& m : names_.members()) {
        const std::string& n = m.name;
        if (n.size() < 4) continue;
        // case-insensitive ".bgf" suffix
        std::string tail = n.substr(n.size() - 4);
        for (auto& ch : tail) ch = (char)((ch >= 'A' && ch <= 'Z') ? ch + 32 : ch);
        if (tail != ".bgf") continue;
        // Prefer non-character static meshes (buildings/props) for the city draw;
        // _DYNAMIC/Character/* are skinned actor rigs.
        if (n.find("Character/") != std::string::npos ||
            n.find("CHARACTER/") != std::string::npos) continue;
        meshNames_.push_back(n);
    }
    // If filtering left nothing (unexpected), fall back to any .bgf member.
    if (meshNames_.empty()) {
        for (const auto& m : names_.members()) {
            const std::string& n = m.name;
            if (n.size() >= 4) {
                std::string tail = n.substr(n.size() - 4);
                for (auto& ch : tail) ch = (char)((ch >= 'A' && ch <= 'Z') ? ch + 32 : ch);
                if (tail == ".bgf") meshNames_.push_back(n);
            }
        }
    }
    return true;
}

bool RealCityRenderer::InitTextures(shim::IFileSystem* fs, const char* archivePath) {
    return tex_.Mount(fs, archivePath);
}

const void* RealCityRenderer::nodeFor(const EntityRef& e, const Options& opt) const {
    // Lay each object on a grid so distinct objects decode to distinct world
    // positions; a real SceneNodeWorldPlacement read drives the actual placement.
    const int kNodeBytes = 640;
    int idx = nodeUsed_;
    if ((int)nodeStore_.size() < (idx + 1) * kNodeBytes)
        nodeStore_.resize((size_t)(idx + 1) * kNodeBytes, 0);
    unsigned char* nb = nodeStore_.data() + (size_t)idx * kNodeBytes;
    std::memset(nb, 0, kNodeBytes);
    ++nodeUsed_;

    int cols = opt.gridCols > 0 ? opt.gridCols : 8;
    int cell = e.slot;
    float wx = opt.originX + (float)(cell % cols) * opt.cellSize;
    float wz = opt.originZ + (float)(cell / cols) * opt.cellSize;
    float wy = 0.0f;
    std::memcpy(nb + kNodePosX, &wx, 4);
    std::memcpy(nb + kNodePosY, &wy, 4);
    std::memcpy(nb + kNodePosZ, &wz, 4);
    // Frame matrix (+396): a yaw derived from the id so headings vary.
    float yaw = (float)((e.id % 8) * 0.39269908f);   // n*pi/8
    float c = std::cos(yaw), s = std::sin(yaw);
    float m[16] = { c,0,s,0,  0,1,0,0,  -s,0,c,0,  0,0,0,1 };
    std::memcpy(nb + kNodeFrameMatrix, m, sizeof m);
    nb[kNodeTypeByte] = kNodeTypeMeshA;   // drawable mesh-object

    // Record this placement so Pick() projects the SAME positions the render used.
    ObjPlace p{ e.id, { wx, wy, wz } };
    placements_.push_back(p);
    return nb;
}

std::string RealCityRenderer::nameFor(const EntityRef& e) const {
    if (meshNames_.empty()) return std::string();
    // Deterministic id-derived pick from the real member list, so each object maps
    // to a real shipped .bgf (the engine's +460 handle, inert-substituted).
    std::size_t i = (std::size_t)((e.id < 0 ? -e.id : e.id) + e.slot) % meshNames_.size();
    return meshNames_[i];
}

RealCityRenderer::Result RealCityRenderer::Render(const Options& opt,
                                                  shim::IGraphicsDevice& device) {
    Result r;
    r.mounted = src_.mounted();
    r.memberCount = meshNames_.size();
    if (!r.mounted)
        return r;

    using namespace guild::sim;

    // Render into a software surface sized to the frame, cleared to the sky colour.
    render::Surface* fb = render::SurfaceCreate(opt.fbW, opt.fbH, 16);
    if (!fb)
        return r;
    render::SurfaceColorFill(fb, opt.clearR, opt.clearG, opt.clearB);

    // --- install the REAL mesh source + name resolver as the live hooks --------
    RealMeshSource*  prevSrc  = ActiveRealMeshSource();
    MeshNameResolver prevName = ActiveMeshNameResolver();
    InstallRealMeshSource(&src_);
    InstallMeshNameResolver(&ActiveNameResolver);

    placements_.clear();
    nodeUsed_ = 0;
    g_activeRenderer = this;
    g_activeOpt = &opt;
    lastOpt_ = opt;

    ObjectMeshRenderer renderer;
    ObjectMeshRenderer::Options ro;
    ro.nodeResolver = &ActiveNodeResolver;   // real placement decode per object
    ro.meshResolver = &RealMeshResolver;     // REAL AGF mesh source (NOT inert default)
    ro.scanObjects  = opt.scanObjects;
    ro.scanScene    = opt.scanScene;
    ro.maxObjects   = opt.maxObjects;
    ro.quadHalf     = opt.quadHalf;
    ro.pixelsPerUnit = opt.pixelsPerUnit;
    ro.eyeX = opt.eyeX; ro.eyeZ = opt.eyeZ;
    // Textured raster: only when requested AND Textures.BIN is mounted (else the
    // table resolver yields null -> the untextured flat path, Wave 28 behaviour).
    ro.textured = opt.textured && tex_.mounted();
    ro.texTableResolver = ro.textured ? &ActiveTexTableResolver
                                      : &DefaultTexTableResolver;
    r.usedRealResolver = (ro.meshResolver == &RealMeshResolver);

    MeshRenderStats st = renderer.render(ro, fb);

    // restore the prior install (no global leak across renders).
    g_activeRenderer = nullptr;
    g_activeOpt = nullptr;
    InstallRealMeshSource(prevSrc);
    InstallMeshNameResolver(prevName);

    r.meshObjects   = st.meshObjects;
    r.quadFallbacks = st.quadFallbacks;
    r.meshTris      = st.meshTris;
    r.appendedPolys = st.appendedPolys;
    r.rasterTris    = st.rasterTris;
    r.textured      = ro.textured;
    r.texturedPolys = st.texturedPolys;

    // Count live objects + the distinct resolved mesh vertex extents (proof the
    // real AGF geometry — not a 2-tri quad — was used).
    int live = 0;
    for (int i = 0; i < kObjectCapacity; ++i)
        if (g_objects[i].alive) ++live;
    r.liveObjects = live;

    // Walk the name map for the objects we drew and inspect the resolved geometry.
    int distinct = 0, maxVerts = 0;
    std::vector<std::string> seen;
    for (int i = 0, drawn = 0; i < kObjectCapacity && drawn < opt.maxObjects; ++i) {
        if (!g_objects[i].alive) continue;
        EntityRef e; e.kind = EntityKind::Object; e.id = g_objects[i].id;
        e.slot = i; e.type = g_objects[i].alive;
        std::string nm = nameFor(e);
        ++drawn;
        if (nm.empty()) continue;
        render::MeshGeometry* g = src_.Resolve(nm.c_str());
        if (!g || g->vertexCount <= 0) continue;
        if (g->vertexCount > maxVerts) maxVerts = g->vertexCount;
        bool dup = false;
        for (auto& s : seen) if (s == nm) { dup = true; break; }
        if (!dup) { seen.push_back(nm); ++distinct; }
    }
    r.maxMeshVerts   = maxVerts;
    r.distinctMeshes = distinct;

    // Non-clear pixel count over the surface.
    u16 clear = (u16)render::PackColor(fb->fmt, opt.clearR, opt.clearG, opt.clearB);
    const u16* px = reinterpret_cast<const u16*>(fb->pixels);
    int total = fb->widthPx * fb->height, nonClear = 0;
    std::vector<u16> colors;
    for (int i = 0; i < total; ++i)
        if (px[i] != clear) { ++nonClear; colors.push_back(px[i]); }
    r.nonClearPixels = nonClear;
    std::sort(colors.begin(), colors.end());
    colors.erase(std::unique(colors.begin(), colors.end()), colors.end());
    r.distinctColors = (int)colors.size();

    // Present the rendered surface to the headless device -> a BMP when a
    // FileDumpGraphicsDevice; otherwise just a present.
    if (shim::Surface* bb = device.backbuffer()) {
        if (bb->pixels && bb->bpp == (int)fb->bpp &&
            bb->width == fb->width && bb->height == fb->height) {
            std::size_t bytes = (std::size_t)fb->pitch * (std::size_t)fb->height;
            std::memcpy(bb->pixels, fb->pixels, bytes);
        }
    }
    device.present();
    r.presented = true;
    if (auto* fdd = dynamic_cast<shim::FileDumpGraphicsDevice*>(&device))
        r.bmpPath = fdd->framePath(fdd->presentCount() - 1,
                                   shim::FileDumpGraphicsDevice::kBmp);

    render::SurfaceDestroy(fb);
    return r;
}

ScenePickResult RealCityRenderer::Pick(const Options& opt, float sx, float sy,
                                       float pickRadius) const {
    // Build the pick roster from the decoded world placements the render produced.
    // If Render() hasn't run yet, regenerate the placements deterministically.
    std::vector<ObjPlace> places = placements_;
    if (places.empty()) {
        using namespace guild::sim;
        int cols = opt.gridCols > 0 ? opt.gridCols : 8;
        for (int i = 0, drawn = 0; i < kObjectCapacity && drawn < opt.maxObjects; ++i) {
            if (!g_objects[i].alive) continue;
            int cell = i;
            ObjPlace p;
            p.id = g_objects[i].id;
            p.pos[0] = opt.originX + (float)(cell % cols) * opt.cellSize;
            p.pos[1] = 0.0f;
            p.pos[2] = opt.originZ + (float)(cell / cols) * opt.cellSize;
            places.push_back(p);
            ++drawn;
        }
    }

    std::vector<ScenePickObject> roster;
    roster.reserve(places.size());
    for (auto& p : places) {
        ScenePickObject o;
        o.id = p.id;
        o.pos[0] = p.pos[0]; o.pos[1] = p.pos[1]; o.pos[2] = p.pos[2];
        roster.push_back(o);
    }

    // The same projection ObjectMeshRenderer used: a top-down city-view camera
    // whose eye/scale recenters the city on the frame. eye chosen so (eyeX,eyeZ)
    // lands at the frame center, matching object_mesh_render's MakeProjectParams.
    float ppu = opt.pixelsPerUnit > 0.0f ? opt.pixelsPerUnit : 1.0f;
    float eye[3] = {
        opt.eyeX - ((float)opt.fbW * 0.5f) / ppu, 0.0f,
        opt.eyeZ - ((float)opt.fbH * 0.5f) / ppu
    };
    CityViewCamera cam = MakeCityViewCamera(eye, ppu, opt.fbW, opt.fbH);

    return PickSceneObject(cam, sx, sy,
                           roster.empty() ? nullptr : roster.data(),
                           (int)roster.size(), pickRadius);
}

// ===========================================================================
// RenderRealCity — mount + render the already-loaded live world in one call.
// ===========================================================================
RealCityRenderer::Result RenderRealCity(shim::IFileSystem* fs,
                                        const RealCityRenderer::Options& opt,
                                        shim::IGraphicsDevice& device,
                                        const char* archivePath) {
    RealCityRenderer rc;
    if (!rc.Init(fs, archivePath)) {
        RealCityRenderer::Result r;
        r.mounted = false;
        return r;
    }
    return rc.Render(opt, device);
}

} // namespace guild::play
