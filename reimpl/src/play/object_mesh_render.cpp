// =============================================================================
// guild::play — REAL OBJECT MESH RENDER implementation. See header.
//
// The flow per frame mirrors the engine's per-object geometry stage:
//   1. for each live entity, decode its WorldPlacement (NodeResolver) and resolve
//      its model-space mesh (MeshResolver);
//   2. seat the mesh in the world via the composed +72 world matrix (yaw + pos)
//      exactly as VIBE_Mesh_InterpolateMorphVertices transforms vertices;
//   3. project the world-seated mesh through the REAL render::ProjectVerticesToScreen
//      into the shared draw list (the BeginUniverseFrame per-object append walk);
//   4. radix-sort the appended list back-to-front (render::RadixSortDrawList);
//   5. flush the whole sorted list once through render::RasterizeMeshList.
// Entities without a resolvable mesh fall back to a single world-seated quad.
// =============================================================================
#include "play/object_mesh_render.h"

#include "play/real_texture_source.h"
#include "render/colorformat.h"
#include "render/raster.h"
#include "render/surface.h"
#include "sim/entity.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace guild::play {

// ---------------------------------------------------------------------------
// World-seat matrix + vertex transform.
// ---------------------------------------------------------------------------

void ComposeWorldMatrix(const WorldPlacement& wp, float m[16]) {
    // Column-major 4x4. Yaw about +Y exactly as VIBE_Math_MatrixFromEuler seats it
    // for a ground object (pitch=roll=0): M[0]=cos(yaw), M[8]=-sin(yaw), and the
    // matching M[2]=sin(yaw), M[10]=cos(yaw) so the basis is a proper rotation.
    // (object_transform recovered M[0]/M[8]; the +Y rotation's other two terms are
    // the standard heading rotation, M[2]=sin, M[10]=cos.) Translation in col 4.
    const float c = std::cos(wp.yaw);
    const float s = std::sin(wp.yaw);
    m[0]  = c;    m[1]  = 0.0f; m[2]  = s;    m[3]  = 0.0f;  // col 0 (x basis)
    m[4]  = 0.0f; m[5]  = 1.0f; m[6]  = 0.0f; m[7]  = 0.0f;  // col 1 (y basis)
    m[8]  = -s;   m[9]  = 0.0f; m[10] = c;    m[11] = 0.0f;  // col 2 (z basis)
    m[12] = wp.x; m[13] = wp.y; m[14] = wp.z; m[15] = 1.0f;  // col 3 (translation)
}

void TransformVertices(const render::MeshGeometry& src, const float m[16],
                       std::vector<render::Vertex>& dst) {
    const int n = src.vertexCount;
    dst.assign(n < 0 ? 0 : (size_t)n, render::Vertex{});
    for (int i = 0; i < n; ++i) {
        const render::Vertex& v = src.vertices[i];
        render::Vertex& o = dst[(size_t)i];
        o = v;  // carry UV / light / flag fields through unchanged
        // The exact +72 world-matrix apply from VIBE_Mesh_InterpolateMorphVertices.
        o.x = v.x * m[0] + v.y * m[4] + v.z * m[8]  + m[12];
        o.y = v.x * m[1] + v.y * m[5] + v.z * m[9]  + m[13];
        o.z = v.x * m[2] + v.y * m[6] + v.z * m[10] + m[14];
    }
}

render::MeshGeometry* WorldMesh::View() {
    geom.vertices    = vertices.data();
    geom.polygons    = polygons.data();
    geom.polyCount   = (i32)polygons.size();
    geom.polyCap     = (i32)polygons.size();
    geom.vertexCount = (i32)vertices.size();
    return &geom;
}

bool TransformMeshGeometry(const render::MeshGeometry& src, const WorldPlacement& wp,
                           WorldMesh& out) {
    out.vertices.clear();
    out.polygons.clear();
    if (!src.vertices || !src.polygons || src.vertexCount <= 0 || src.polyCount <= 0)
        return false;

    float m[16];
    ComposeWorldMatrix(wp, m);
    TransformVertices(src, m, out.vertices);

    // Rebind each polygon's vertex pointers into the transformed array (same index
    // resolution, preserving winding + UVs + flags). Drop any poly that references
    // an out-of-range vertex (defensive; a well-formed BGF never does).
    out.polygons.reserve((size_t)src.polyCount);
    const render::Vertex* base = src.vertices;
    const size_t nv = out.vertices.size();
    for (int i = 0; i < src.polyCount; ++i) {
        const render::Polygon& sp = src.polygons[i];
        // Index of each src vertex pointer within the src vertex array.
        long i0 = sp.v0 ? (sp.v0 - base) : -1;
        long i1 = sp.v1 ? (sp.v1 - base) : -1;
        long i2 = sp.v2 ? (sp.v2 - base) : -1;
        if (i0 < 0 || i1 < 0 || i2 < 0 ||
            (size_t)i0 >= nv || (size_t)i1 >= nv || (size_t)i2 >= nv)
            continue;
        render::Polygon p = sp;  // copy uv/flags
        p.v0 = &out.vertices[(size_t)i0];
        p.v1 = &out.vertices[(size_t)i1];
        p.v2 = &out.vertices[(size_t)i2];
        // Clear the project-stage scratch flags so each frame re-evaluates cull.
        p.flags36 = 0;
        out.polygons.push_back(p);
    }
    out.View();
    return !out.polygons.empty();
}

// ---------------------------------------------------------------------------
// Inert default mesh resolver.
// ---------------------------------------------------------------------------
const render::MeshGeometry* DefaultMeshResolver(const EntityRef&) {
    return nullptr;   // no mesh bound -> quad fallback
}

// ---------------------------------------------------------------------------
// Inert default texture-table resolver.
// ---------------------------------------------------------------------------
const MaterialTextureTable* DefaultTexTableResolver(const EntityRef&) {
    return nullptr;   // no table -> object renders untextured
}

// ---------------------------------------------------------------------------
// Affine textured triangle. The engine's RGBZ span path (raster_textured.h:
// VIBE_Raster_RasterizeMirrorTriangle 0x5F6C30 + FillSpanLoop 0x5F6B34)
// interpolates U/V LINEARLY in screen space (16.16 fixed point, no per-pixel
// 1/z) — i.e. AFFINE mapping. We reproduce that affine interpolation in float
// (bit-for-bit identical mapping decisions are not required for the portable
// surface; what matters is the per-pixel texel is the affine-interpolated UV of
// the triangle). Coverage uses the standard signed-area barycentric test (a
// top-left fill is not modelled — the portable Surface is exercised by the
// stats/pixel oracles, not the on-disk frame byte layout).
// ---------------------------------------------------------------------------
int RasterTexturedTriangleAffine(render::Surface* fb, const render::Vertex* a,
                                 const render::Vertex* b, const render::Vertex* c,
                                 const render::DecodedBmp& bmp) {
    if (!fb || !fb->pixels || !a || !b || !c) return 0;
    if (!bmp.ok || bmp.width <= 0 || bmp.height <= 0 || bmp.rgba.empty()) return 0;

    // Projected screen positions live in Vertex::screenX/screenY (+16/+20).
    const float ax = a->screenX, ay = a->screenY;
    const float bx = b->screenX, by = b->screenY;
    const float cx = c->screenX, cy = c->screenY;

    // Signed area (x2 of the triangle). Degenerate -> nothing to fill.
    const float area = (bx - ax) * (cy - ay) - (cx - ax) * (by - ay);
    if (std::fabs(area) < 1e-6f) return 0;
    const float invArea = 1.0f / area;

    // Integer bounding box clipped to the surface clip rect.
    int minX = (int)std::floor(std::min(ax, std::min(bx, cx)));
    int maxX = (int)std::ceil (std::max(ax, std::max(bx, cx)));
    int minY = (int)std::floor(std::min(ay, std::min(by, cy)));
    int maxY = (int)std::ceil (std::max(ay, std::max(by, cy)));
    const int cx0 = fb->clipX0, cy0 = fb->clipY0;
    const int cx1 = fb->clipX1 > 0 ? fb->clipX1 : fb->width;
    const int cy1 = fb->clipY1 > 0 ? fb->clipY1 : fb->height;
    if (minX < cx0) minX = cx0;
    if (maxX > cx1) maxX = cx1;
    if (minY < cy0) minY = cy0;
    if (maxY > cy1) maxY = cy1;
    if (minX >= maxX || minY >= maxY) return 0;

    int written = 0;
    for (int y = minY; y < maxY; ++y) {
        const float py = (float)y + 0.5f;
        for (int x = minX; x < maxX; ++x) {
            const float px = (float)x + 0.5f;
            // Barycentric weights (w0+w1+w2 == 1).
            float w0 = ((bx - px) * (cy - py) - (cx - px) * (by - py)) * invArea;
            float w1 = ((cx - px) * (ay - py) - (ax - px) * (cy - py)) * invArea;
            float w2 = 1.0f - w0 - w1;
            // Inside test (allow either winding via the sign of `area`).
            if (area > 0.0f) { if (w0 < 0 || w1 < 0 || w2 < 0) continue; }
            else             { if (w0 > 0 || w1 > 0 || w2 > 0) continue; }
            // AFFINE U/V: linear blend of the three corner texel coords.
            float u = w0 * a->u + w1 * b->u + w2 * c->u;
            float v = w0 * a->v + w1 * b->v + w2 * c->v;
            TexSample s = SampleTexel(bmp, u, v, /*texelSpace=*/false);
            if (!s.ok) continue;
            render::SurfaceSetPixelRgb(fb, x, y, s.r, s.g, s.b);
            ++written;
        }
    }
    return written;
}

// ---------------------------------------------------------------------------
// ObjectMeshRenderer.
// ---------------------------------------------------------------------------

namespace {

// Frame-scoped texture binding for the textured SpanDispatch slots. A SpanFillFn
// can't capture, so the renderer publishes its per-poly texture map + a textured-
// pixel counter here for the duration of one RasterizeMeshList flush, then clears
// it. (The engine likewise bound the active texture via globals before the flush.)
const std::unordered_map<const render::Polygon*, const render::DecodedBmp*>*
    g_framePolyTex = nullptr;
int g_frameTexturedPolys = 0;

// Textured opaque/blend slot: look up this polygon's bound texture; if present,
// sample it AFFINELY per pixel; otherwise fall back to the engine's flat/shaded
// triangle (RasterTexturedTriangle), so a mixed mesh (some textured, some not)
// renders correctly.
int SpanFillTexturedSample(render::Surface* fb, const render::Polygon& tri) {
    if (!fb || !tri.v0 || !tri.v1 || !tri.v2) return 1;
    const render::DecodedBmp* bmp = nullptr;
    if (g_framePolyTex) {
        auto it = g_framePolyTex->find(&tri);
        if (it != g_framePolyTex->end()) bmp = it->second;
    }
    if (bmp) {
        RasterTexturedTriangleAffine(fb, tri.v0, tri.v1, tri.v2, *bmp);
        ++g_frameTexturedPolys;
        return 1;
    }
    // Untextured poly -> the slot-4 default (render::SpanFillTexturedOpaque),
    // which routes by surface format: 8bpp -> the shaded path @0x5F7D58
    // (unchanged), 16bpp -> the 1x1 white default binding through the textured
    // leaf (VIBE_Texture_BindActive @0x5db564 slot==0 semantics; meshlist.cpp).
    // Calling RasterizeTexturedTriangle directly here wrote 8-bit shade bytes
    // into the 16bpp framebuffer (the wave-3 "pink polygon" artifact).
    return render::SpanFillTexturedOpaque(fb, tri);
}

// A SpanDispatch whose opaque (4) + blend (3) slots route through the textured
// sampler. Slots 0..2,5,6 keep the NullStub default (the flush only indexes 3/4).
render::SpanDispatch MakeTexturedDispatch() {
    render::SpanDispatch d;   // NullStub defaults + flat slots 3/4
    d.slot[4] = &SpanFillTexturedSample;
    d.slot[3] = &SpanFillTexturedSample;
    return d;
}

// Build the projection params that map world (x,z) -> framebuffer pixels and
// world y -> the light/shade term, recentering (eyeX,eyeZ) on the frame center.
//   screenX = (x - eye0)*invDepth1 + bias  with invDepth1 = ppu, bias = 0,
//             eye0 chosen so x==eyeX lands at fbW/2.
render::ProjectParams MakeProjectParams(int fbW, int fbH, float ppu,
                                        float eyeX, float eyeZ) {
    if (ppu <= 0.0f) ppu = 1.0f;
    render::ProjectParams pp{};
    // eye0 so that (eyeX - eye0)*ppu == fbW/2  -> eye0 = eyeX - (fbW/2)/ppu.
    pp.eye[0] = eyeX - ((float)fbW * 0.5f) / ppu;
    pp.eye[2] = eyeZ - ((float)fbH * 0.5f) / ppu;
    pp.eye[1] = 0.0f;
    pp.invDepth[0] = pp.invDepth[1] = pp.invDepth[2] = ppu;  // invDepth[1] scales X
    pp.biasX  = 0.0f;
    pp.scaleX = ppu;            // screenY = bias + (z - eye2)*scaleX
    pp.scaleY = 0.0f;           // flat lighting term (lightIdx clamps to >=1)
    pp.lightCap = 254.0f;
    pp.screenW  = (float)((fbW > fbH) ? fbW : fbH);  // on-screen clip extent
    return pp;
}

} // namespace

int ObjectMeshRenderer::projectMesh(render::MeshGeometry* geom,
                                    const render::ProjectParams& pp) {
    if (!geom) return 0;
    int before = db_.count;
    render::DrawList sink = db_.AppendSink();
    // LIGHT-CACHE SHADE (Options::cacheShade): a lit universe OBJECT's vertex
    // shade is the VIBE_Light_BuildObjectCache @0x5c8218 byte the resolver's
    // geometry carries; the Y-depth term ProjectVerticesToScreen writes is the
    // CHARACTER shading. Snapshot the cache shade and re-stamp it after the
    // projection (the same engine truth universe_render applies by overwriting
    // the projected lightIdx with the cache shade).
    if (cacheShade_ && geom->vertices && geom->vertexCount > 0) {
        cacheShadeSave_.resize((size_t)geom->vertexCount);
        for (int i = 0; i < geom->vertexCount; ++i)
            cacheShadeSave_[(size_t)i] = geom->vertices[i].lightIdx;
    }
    // 0x40 (double-sided) so a freshly seated mesh's winding always appends; the
    // on-screen [0,screenW) clip in ProjectVerticesToScreen still culls off-frame.
    render::ProjectVerticesToScreen(geom, pp, /*objFlags530=*/0x40,
                                    /*viewCull42=*/0, &sink);
    if (cacheShade_ && geom->vertices && geom->vertexCount > 0)
        for (int i = 0; i < geom->vertexCount; ++i)
            geom->vertices[i].lightIdx = cacheShadeSave_[(size_t)i];
    db_.count = sink.count;
    return db_.count - before;
}

int ObjectMeshRenderer::projectQuad(const WorldPlacement& wp, const Options& opt,
                                    const render::ProjectParams& pp,
                                    render::Surface* /*fb*/) {
    // A flat world-seated quad (two tris) centered on the placement, in the X/Z
    // ground plane, with half-extent opt.quadHalf in world units. Seated via the
    // same world matrix so a fallback object still lands at its real position.
    float m[16];
    ComposeWorldMatrix(wp, m);

    worldMeshes_.emplace_back();
    WorldMesh& wm = worldMeshes_.back();
    wm.vertices.assign(4, render::Vertex{});
    const float h = opt.quadHalf;
    struct { float x, z, u, v; } corner[4] = {
        {-h, -h, 0.0f, 0.0f}, { h, -h, 1.0f, 0.0f},
        {-h,  h, 0.0f, 1.0f}, { h,  h, 1.0f, 1.0f},
    };
    for (int i = 0; i < 4; ++i) {
        render::Vertex& o = wm.vertices[(size_t)i];
        float lx = corner[i].x, ly = 0.0f, lz = corner[i].z;
        o.x = lx * m[0] + ly * m[4] + lz * m[8]  + m[12];
        o.y = lx * m[1] + ly * m[5] + lz * m[9]  + m[13];
        o.z = lx * m[2] + ly * m[6] + lz * m[10] + m[14];
        o.u = corner[i].u; o.v = corner[i].v;
        o.lightIdx = 180;
        o.clipFlags = 0;
    }
    wm.polygons.assign(2, render::Polygon{});
    wm.polygons[0].v0 = &wm.vertices[0];
    wm.polygons[0].v1 = &wm.vertices[1];
    wm.polygons[0].v2 = &wm.vertices[2];
    wm.polygons[1].v0 = &wm.vertices[1];
    wm.polygons[1].v1 = &wm.vertices[3];
    wm.polygons[1].v2 = &wm.vertices[2];
    wm.View();
    return projectMesh(&wm.geom, pp);
}

MeshRenderStats ObjectMeshRenderer::render(const Options& opt, render::Surface* fb) {
    using namespace guild::sim;
    stats_ = MeshRenderStats{};
    cacheShade_ = opt.cacheShade;
    if (!fb) return stats_;

    const int fbW = fb->width;
    const int fbH = fb->height;

    // Reset the per-frame draw-list buffers + world-mesh storage.
    if ((int)pool1_.size() < kMaxPolys) { pool1_.resize(kMaxPolys); pool2_.resize(kMaxPolys); }
    db_.base1 = pool1_.data();
    db_.base2 = pool2_.data();
    db_.count = 0;
    db_.capacity = (i32)pool1_.size();
    worldMeshes_.clear();
    worldMeshes_.reserve((size_t)opt.maxObjects + 4);
    polyTex_.clear();

    render::ProjectParams pp =
        MakeProjectParams(fbW, fbH, opt.pixelsPerUnit, opt.eyeX, opt.eyeZ);

    int drawn = 0;

    // Place one entity: decode its world placement, resolve its mesh, seat + project
    // (mesh) or fall back to a quad. Returns true if anything was emitted.
    // `cap` is the draw-budget bound: opt.maxObjects for the object/scene scans,
    // opt.maxObjects + opt.maxPersons for the person scan (separate budgets).
    auto place = [&](const EntityRef& ref, int cap) -> bool {
        if (drawn >= cap) return false;

        const void* node = opt.nodeResolver ? opt.nodeResolver(ref) : nullptr;
        WorldPlacement wp = node ? ObjectWorldPlacement(nullptr, node)
                                 : WorldPlacement{ /*x*/0, 0, 0, 0, /*visible*/true };
        if (node && !wp.visible)
            return false;   // engine cull (light/empty/off) -> skip entirely

        const render::MeshGeometry* mesh =
            opt.meshResolver ? opt.meshResolver(ref) : nullptr;

        if (mesh) {
            worldMeshes_.emplace_back();
            WorldMesh& wm = worldMeshes_.back();
            if (TransformMeshGeometry(*mesh, wp, wm)) {
                stats_.meshTris += (int)wm.polygons.size();
                // TEXTURE BINDING: resolve this entity's per-poly MaterialTextureTable
                // and record each WorldMesh polygon's DecodedBmp into polyTex_.
                // TransformMeshGeometry preserves source poly order (it only drops
                // out-of-range polys, which a well-formed BGF never has), so WorldMesh
                // poly i maps to source poly i -> table->polyTexId[i].
                if (opt.textured && opt.texTableResolver &&
                    wm.polygons.size() == (size_t)mesh->polyCount) {
                    const MaterialTextureTable* tbl = opt.texTableResolver(ref);
                    if (tbl) {
                        for (size_t pi = 0; pi < wm.polygons.size(); ++pi) {
                            const render::DecodedBmp* b =
                                tbl->TextureFor(tbl->PolyTexId(pi));
                            if (b) polyTex_[&wm.polygons[pi]] = b;
                        }
                    }
                }
                stats_.appendedPolys += projectMesh(wm.View(), pp);
                ++stats_.meshObjects;
                ++drawn;
                return true;
            }
            worldMeshes_.pop_back();  // empty geometry -> fall through to quad
        }

        stats_.appendedPolys += projectQuad(wp, opt, pp, fb);
        ++stats_.quadFallbacks;
        ++drawn;
        return true;
    };

    // 1. Scene-node tree (DFS via childPtr, parent before children), the engine
    //    WalkAndInvoke draw order — mirrors world_render's WalkSceneNodes.
    if (opt.scanScene) {
        int count = g_sceneNodeCount;
        if (count < 0 || count > kSceneNodeCapacity) count = 0;
        static bool visited[kSceneNodeCapacity];
        for (int i = 0; i < kSceneNodeCapacity; ++i) visited[i] = false;
        static int stack[kSceneNodeCapacity + 8];
        for (int seed = 0; seed < count; ++seed) {
            if (visited[seed] || g_sceneNodes[seed].type == 0) continue;
            int sp = 0; stack[sp++] = seed;
            while (sp > 0) {
                int n = stack[--sp];
                if (n < 0 || n >= count || visited[n]) continue;
                visited[n] = true;
                if (g_sceneNodes[n].type == 0) continue;
                EntityRef ref;
                ref.kind = EntityKind::Scene;
                ref.id   = g_sceneNodes[n].id;
                ref.slot = n;
                ref.type = g_sceneNodes[n].type;
                place(ref, opt.maxObjects);
                int c = g_sceneNodes[n].childPtr;
                if (c >= 0 && c < count && !visited[c]) stack[sp++] = c;
            }
        }
    }

    // 2. Alive objects (linear g_objects scan).
    if (opt.scanObjects) {
        for (int i = 0; i < kObjectCapacity && drawn < opt.maxObjects; ++i) {
            if (g_objects[i].alive == 0) continue;
            EntityRef ref;
            ref.kind = EntityKind::Object;
            ref.id   = g_objects[i].id;
            ref.slot = i;
            ref.type = g_objects[i].alive;
            place(ref, opt.maxObjects);
        }
    }

    // 2b. Live persons (linear g_persons scan) — OPT-IN (scanPersons, default
    //     false -> behaviour unchanged). A row is a live person when its marker
    //     word != -1 (free slot) and its kind byte (+2) is a "real person"
    //     (< 10 — the record gate of VIBE_Person_IsValidActiveRecord @0x4f8e60).
    //     Persons go through the SAME place() (node resolver -> world seat ->
    //     project) and land in the SAME draw list, so the radix sort + raster
    //     flush below covers objects and persons in one pass, as the engine's
    //     single per-frame draw list does.
    if (opt.scanPersons) {
        int personsDrawn = 0;
        for (int i = 0; i < kPersonCapacity && personsDrawn < opt.maxPersons; ++i) {
            const Person& p = g_persons[i];
            if (p.marker == -1) continue;
            if (p.kind >= 10) continue;
            EntityRef ref;
            ref.kind = EntityKind::Person;
            ref.id   = p.id;
            ref.slot = i;
            ref.type = p.kind;
            int meshBefore = stats_.meshObjects;
            if (place(ref, opt.maxObjects + opt.maxPersons)) {
                ++personsDrawn;
                if (stats_.meshObjects > meshBefore) ++stats_.personMeshes;
                else                                 ++stats_.personQuads;
            }
        }
    }

    // 3. Sort the appended draw list back-to-front, then flush it ONCE through the
    //    real rasterizer (the present-lock bracket flush in the engine).
    render::RadixSortDrawList(db_, (u32)db_.count, /*twoPassOnly=*/false);
    stats_.appendedPolys = db_.count;

    render::MeshList list{db_.base1, db_.count};
    render::ClipContext ctx{0, nullptr};                 // direct path (no clip planes)
    render::ProjectScalars proj{1.0f, 0.0f, 1.0f, 0.0f}; // identity reproject

    if (opt.textured && !polyTex_.empty()) {
        // Bind the per-poly texture map for the textured span slots, swap in the
        // textured dispatch, flush, then restore (no global leak between frames).
        render::SpanDispatch texDisp = MakeTexturedDispatch();
        g_framePolyTex = &polyTex_;
        g_frameTexturedPolys = 0;
        stats_.rasterTris =
            render::RasterizeMeshList(list, fb, texDisp, ctx, proj, scratch_);
        stats_.texturedPolys = g_frameTexturedPolys;
        g_framePolyTex = nullptr;
        g_frameTexturedPolys = 0;
    } else {
        // Legacy untextured path: the default flat/shaded dispatch (unchanged).
        stats_.rasterTris =
            render::RasterizeMeshList(list, fb, dispatch_, ctx, proj, scratch_);
    }

    (void)drawn;
    return stats_;
}

} // namespace guild::play
