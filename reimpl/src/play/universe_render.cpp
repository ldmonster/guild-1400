// =============================================================================
// guild::play — UNIVERSE FRAME DRIVER. See universe_render.h.
// =============================================================================
#include "play/universe_render.h"

#include "play/city_view3d.h"           // BuildEngineFrustum (0x5accd0 prologue)
#include "play/object_mesh_render.h"    // RasterTexturedTriangleAffine (24-bit BMP stand-in)
#include "play/wire_scene_bridge.h"     // SceneDrawNode, WalkSceneTree, InstallRealSceneBridge
#include "render/frame.h"               // FrameState, FrameHooks, RenderMainViewFrame
#include "render/mesh.h"                // ProjectVerticesToScreen, ProjectParams, DrawList
#include "render/meshlist.h"            // RasterizeMeshList, MeshList, SpanDispatch, ProjectScalars
#include "render/scene_node.h"          // NodeAppendMode / NodeAppendContext
#include "render/clip.h"                // ClipContext, ClipScratch
#include "render/surface.h"             // SurfaceCreate/Destroy/ColorFill/GetPixelRgb
#include "render/raster.h"              // RasterVertex, RasterizeTexturedTriangle (flat fallback)
#include "render/raster_textured.h"     // RgbzVertex, RasterizeTexturedTriangleRgbz (the 1:1 textured tri)
#include "render/texture.h"             // TextureSetSize
#include "render/scene_transform.h"     // MatrixFromEuler (0x5cb1bc), Apply (the real view rotation)
#include "render/mesh_transform_walk.h" // TransformMeshVerticesByMatrix (0x5c953c static walk)
#include "render/object_project.h"      // ProjectObjectVertices (0x5ac970 perspective project+cull)
#include "render/object_light_shade.h"  // FinalizeVertexShadeLuma (BuildObjectCache ambient shade)
#include "render/light_atmos.h"         // LightAtmos() — flt_64A074/78/7C live ambient
#include "shim/IGraphicsDevice.h"

#include <algorithm>
#include <cstddef>
#include <set>

namespace guild::play {

namespace {
// The single active driver the C FrameHook / SpanFill trampolines thread through
// (the engine had one set of global PolyLists; one driver renders at a time).
UniverseFrameDriver* g_activeUFD = nullptr;

void UFD_ClearRect()        { if (g_activeUFD) g_activeUFD->doClear(); }
int  UFD_SceneWalk(char)    { return g_activeUFD ? g_activeUFD->doSceneWalk() : 0; }
// The renderTerrain frame hook — BeginUniverseFrame @0x5B3900 invokes it at the
// 0x5b3a2f `VIBE_Floor_RenderTerrain(dword_64A028, a2)` arm (after the clear,
// before the object walk). Inert unless Options::ground is bound.
void UFD_RenderTerrain(void*, char a2) {
    if (g_activeUFD) g_activeUFD->doRenderTerrain(a2);
}

// The textured SpanDispatch slot (slots 3 + 4): bind the polygon's material to its
// real render::Texture and rasterize through the 1:1 affine textured-triangle leaf
// render::RasterizeTexturedTriangleRgbz (gilde.exe 0x5F6C30). A texture record
// carrying the colour-key trigger (flags & 8 — the UploadToSurface @0x5db234
// `v9 = (rec->flags & 8) ? 0 : noTransparency` arm, texture_upload.cpp) routes
// through the MASKED span body (FillSpanTexturedMasked @0x5F721A: source palette
// index 0 transparent) — the software sibling of the keyed upload + the
// BeginScene @0x5e010c alpha test. The flag SOURCE for shipped textures is a
// named gap (progress/terrain-colorkey-wave3.md); decoded records default to 0
// (plain span), so this wiring is behaviour-neutral until that bit is recovered.
//
// UNTEXTURED / UNBOUND material fallback: the engine's live span binding never
// goes out-of-format — a record with no texels binds the 1x1 "white" default
// (VIBE_Texture_BindActive @0x5db564 / ResetBinding @0x5db5f0; texture_upload.h)
// and the poly renders through the SAME 16bpp textured span. The previous
// fallback here (the 8bpp shaded path render::RasterizeTexturedTriangle
// @0x5F7D58) wrote shade BYTES into the 16bpp framebuffer — the 0xC8C8 "pink
// polygon" artifact pinned by tests/e2e/render_fidelity_w3a_e2e_test.cpp.
int UFD_SpanTextured(render::Surface* fb, const render::Polygon& tri) {
    UniverseFrameDriver* d = g_activeUFD;
    const render::Vertex* vp[3] = {tri.v0, tri.v1, tri.v2};
    if (!fb || !vp[0] || !vp[1] || !vp[2])
        return 1;
    const UniverseFrameDriver::BoundTex* bt =
        d ? d->boundTexFor(tri.matIndex) : nullptr;
    if (bt && bt->tex && bt->palette && !bt->tex->texels.empty()) {
        const float w = (float)bt->tex->mipWidth;
        render::RgbzVertex rv[3];
        for (int i = 0; i < 3; ++i) {
            rv[i].x = vp[i]->screenX;
            rv[i].y = vp[i]->screenY;
            rv[i].u = vp[i]->u * w;      // normalized UV -> texel coords (wrapped by texelMask)
            rv[i].v = vp[i]->v * w;
        }
        // Colour-key routing (wave-5 W5-CKEY, 0x5dea50 NOW decompiled). A 24-bit
        // colour-keyed texture (record +104 bit 2 = render::kTexFlagColourKey,
        // the producer in buildTextureBind) routes through the MASKED span. The
        // engine's transparency for it is the DDraw colour-key on pal[0] == BLACK
        // (VIBE_Render_LoadAndStretchTexture @0x5dea50: v111 = pal[0] = 0 for a
        // 24-bit source whose palette buffer is memset-0; DDBLT_KEYSRC at
        // 0x5df086). The faithful software realisation keys on the RESOLVED 16bpp
        // value == 565(black) == 0 (NOT source index 0 — the octree quantizer
        // @0x603180 does not place black at index 0). NOTE bit 2 is the colour
        // key; bit 3 (flags & 8) is the "_NM" mip-bias flag (the wave-3/4
        // mistake), not transparency.
        const bool keyed =
            render::ColourKeyEnabled() && render::TextureIsColourKeyed(*bt->tex);
        const i32 blackKey565 = 0;  // 565 encoding of (0,0,0)
        const int drew = keyed
            ? render::RasterizeTexturedTriangleRgbzMasked(fb, rv, *bt->tex, bt->palette,
                                                          /*polyFlags38=*/0, blackKey565)
            : render::RasterizeTexturedTriangleRgbz(fb, rv, *bt->tex, bt->palette);
        if (drew) {
            if (d) d->addTexturedPoly();
            return 1;
        }
    }
    // 24-bit (non-palettized) BMP stand-in bind (materials-wave4 handoff (b)):
    // only ever set when render::Rgb24MaterialStandInEnabled() (default OFF).
    if (bt && bt->rgb &&
        play::RasterTexturedTriangleAffine(fb, vp[0], vp[1], vp[2], *bt->rgb) > 0) {
        if (d) d->addTexturedPoly();
        return 1;
    }
    // Fallback: the slot-4 default (render::SpanFillTexturedOpaque), which
    // routes by surface format — 8bpp: the shaded path @0x5F7D58; 16bpp: the
    // LEVEL-shaded 1x1 white default binding (BindActive @0x5db564 slot==0,
    // level = max vertex +66 byte) through the textured leaf. See
    // render/meshlist.cpp RasterTri.
    return render::SpanFillTexturedOpaque(fb, tri);
}
} // namespace

UniverseFrameDriver::~UniverseFrameDriver() {
    if (fb_)
        render::SurfaceDestroy(fb_);
    fb_ = nullptr;
}

bool UniverseFrameDriver::InitTextures(shim::IFileSystem* fs, const char* archivePath) {
    return tex_.Mount(fs, archivePath);
}
bool UniverseFrameDriver::texturesMounted() const { return tex_.mounted(); }

const UniverseFrameDriver::BoundTex*
UniverseFrameDriver::boundTexFor(i32 matIndex) const {
    if (matIndex < 0 || (std::size_t)matIndex >= boundTex_.size())
        return nullptr;
    return &boundTex_[(std::size_t)matIndex];
}

// Build the per-material render::Texture bind from the mounted Textures.BIN for the
// member's BgfModel (the real material->BMP resolution real_city_render uses).
void UniverseFrameDriver::buildTextureBind(RealMeshSource& src, const char* memberName) {
    matTex_.clear();
    matPalette_.clear();
    boundTex_.clear();
    boundMaterials_ = 0;
    if (!tex_.mounted())
        return;
    const render::BgfModel* model = src.ModelFor(memberName);
    if (!model)
        return;
    const MaterialTextureTable* tbl = tex_.BuildTableFor(memberName, *model);
    if (!tbl)
        return;

    const int nm = (int)tbl->matToTex.size();
    matTex_.resize((std::size_t)nm);          // resized ONCE -> &matTex_[mi] stable below
    matPalette_.resize((std::size_t)nm);
    boundTex_.assign((std::size_t)nm, BoundTex{});

    for (int mi = 0; mi < nm; ++mi) {
        const int texId = tbl->matToTex[(std::size_t)mi];
        const render::DecodedBmp* bmp = tbl->TextureFor(texId);
        if (!bmp || !bmp->ok || bmp->width <= 0 || bmp->indices.empty()) {
            // 24-bit (non-palettized) BMP: under the default-OFF stand-in switch
            // (render/texture.h; the @0x5da34c palettizer named gap) bind the RGB
            // pixels for the affine kernel; default = stay unbound (the engine's
            // level-shaded white default).
            if (render::Rgb24MaterialStandInEnabled() && bmp && bmp->ok &&
                bmp->width > 0 && !bmp->rgba.empty()) {
                boundTex_[(std::size_t)mi].rgb = bmp;
                ++boundMaterials_;
            }
            continue;
        }

        // The palettized texel record (TextureSetSize allocates w*w, sets mask/shift).
        render::Texture& T = matTex_[(std::size_t)mi];
        render::TextureSetSize(T, bmp->width);

        // wave-5 W5-CKEY — PRODUCER. The original sets record +104 bit 2
        // (kTexFlagColourKey) on a fresh load when `!dword_140809C && bpp > 8`
        // (VIBE_Texture_LoadByName @0x5dad52). The gate dword_140809C defaults
        // to 0 (get_global_value), so EVERY >8bpp (24-bit) source is keyed; an
        // 8-bit source never sets the bit. (bit 3 / 0x08 is the "_NM" mip-bias
        // flag, NOT the colour key — the wave-3/4 code used it in error.) The
        // colour-key VALUE is pal[0] == black for 24-bit (LoadAndStretchTexture
        // @0x5dea50 v111 = 0); see the FillSpanTexturedMasked banner.
        if (bmp->bpp > 8)
            T.flags |= render::kTexFlagColourKey;
        const std::size_t n = std::min(T.texels.size(), bmp->indices.size());
        std::copy(bmp->indices.begin(), bmp->indices.begin() + n, T.texels.begin());

        // The 565 palette LUT from the BMP's source palette (256*3 RGB triples).
        std::vector<u16>& pal = matPalette_[(std::size_t)mi];
        pal.assign(256, 0);
        if (bmp->palette.size() >= 768) {
            for (int i = 0; i < 256; ++i) {
                const u8 r = bmp->palette[3 * i + 0];
                const u8 g = bmp->palette[3 * i + 1];
                const u8 b = bmp->palette[3 * i + 2];
                pal[(std::size_t)i] = (u16)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
            }
        }
        boundTex_[(std::size_t)mi] = BoundTex{&matTex_[(std::size_t)mi], pal.data()};
        ++boundMaterials_;
    }
}

bool UniverseFrameDriver::LoadMember(RealMeshSource& src, const char* memberName) {
    geom_ = src.Resolve(memberName);
    if (!geom_ || geom_->vertexCount <= 0 || geom_->polyCount <= 0)
        return false;
    buildTextureBind(src, memberName);   // no-op when Textures.BIN not mounted
    return true;
}

// clearRect hook — sky fill (the engine cleared the back buffer each frame).
void UniverseFrameDriver::doClear() {
    if (fb_)
        render::SurfaceColorFill(fb_, opt_.clearR, opt_.clearG, opt_.clearB);
}

// renderTerrain hook body — draw the bound FloorGround through the @0x5bf22c
// walk + @0x5AEC88 flush (play::GroundFrame) under the engine camera/projection
// the Options carry (terrain-ground wave 4; see play/terrain_render.h).
void UniverseFrameDriver::doRenderTerrain(char a2) {
    groundDrawn_ = false;
    groundStats_ = GroundRenderStats{};
    if (!fb_ || !opt_.ground || !opt_.ground->valid())
        return;
    if (!groundFrame_.bound() || groundFrame_.ground() != opt_.ground) {
        if (!groundFrame_.Bind(opt_.ground))
            return;
    }

    // The engine frustum + SetupViewTransform scalars over this driver's FB
    // (the same parameterisation CityView3D::buildViewParams uses: scale = W/2
    // at offsets W/2, H/2; yScale negated).
    const float halfW = (float)opt_.fbW * 0.5f;
    const float halfH = (float)opt_.fbH * 0.5f;
    render::Frustum fr{};
    BuildEngineFrustum((float)opt_.fbW, (float)opt_.fbH, halfW,
                       opt_.groundNearZ, opt_.groundFarZ, fr);
    float planes[6][4];
    for (int pi = 0; pi < 4; ++pi)
        for (int k = 0; k < 4; ++k)
            planes[pi][k] = fr.plane[pi][k];
    planes[4][0] = 0; planes[4][1] = 0; planes[4][2] = 1.0f;  planes[4][3] = opt_.groundNearZ;
    planes[5][0] = 0; planes[5][1] = 0; planes[5][2] = -1.0f; planes[5][3] = -opt_.groundFarZ;

    GroundViewParams vp;
    for (int k = 0; k < 3; ++k) {
        vp.eye[k] = opt_.groundEye[k];
        vp.rot[k] = opt_.groundRot[k];
    }
    vp.frustum        = &fr;
    vp.clipPlanes     = planes;
    vp.clipPlaneCount = 6;
    vp.proj.xScale = halfW;  vp.proj.xOffset = halfW;
    vp.proj.yScale = -halfW; vp.proj.yOffset = halfH;

    groundStats_ = groundFrame_.Render(fb_, vp, a2);
    groundDrawn_ = groundStats_.rasterTris > 0;
}

// Build the view-transformed geometry copy: rotate every source vertex about the
// mesh centre by R = MatrixFromEuler(viewEuler) — the engine's per-object view
// transform (ProcessSceneNode applies the +72 world/view matrix via
// InterpolateMorphVertices before projection). Polygons are re-pointed into the
// copy, carrying UV + matIndex. The copy is projected/mutated each frame so the
// cached source mesh stays pristine.
void UniverseFrameDriver::buildViewGeometry() {
    const int vc = geom_->vertexCount;
    const int pc = geom_->polyCount;
    if ((int)viewVerts_.size() != vc) viewVerts_.assign((std::size_t)vc, render::Vertex{});
    if ((int)viewPolys_.size() != pc) viewPolys_.assign((std::size_t)pc, render::Polygon{});

    // Mesh centre (bounding-box midpoint) so the rotation pivots in place.
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (int i = 0; i < vc; ++i) {
        const render::Vertex& v = geom_->vertices[i];
        const float pos[3] = {v.x, v.y, v.z};
        for (int k = 0; k < 3; ++k) { if (pos[k] < lo[k]) lo[k] = pos[k]; if (pos[k] > hi[k]) hi[k] = pos[k]; }
    }
    const float ctr[3] = {0.5f * (lo[0] + hi[0]), 0.5f * (lo[1] + hi[1]), 0.5f * (lo[2] + hi[2])};

    // Copy the source verts (so the cached mesh stays pristine).
    for (int i = 0; i < vc; ++i)
        viewVerts_[(std::size_t)i] = geom_->vertices[(std::size_t)i];

    // Build the object's 16-float column-major WORLD matrix (the engine's drawData+72):
    // a rotation R = MatrixFromEuler(viewEuler) about the mesh centre, i.e.
    //   view = R*(v - centre) = R*v - R*centre  ->  translation = -R*centre.
    // Then run the REAL static vertex-transform walk of VIBE_Mesh_InterpolateMorphVertices
    // (@0x5c953c) over the copy — the exact transform ProcessSceneNode applies.
    const render::Mat3 R = render::MatrixFromEuler(opt_.viewEuler);
    float Rc[3];
    render::Apply(R, ctr, Rc);                    // R * centre
    float world[16] = {0.0f};
    world[0] = R.m[0]; world[4] = R.m[1]; world[8]  = R.m[2]; world[12] = -Rc[0];
    world[1] = R.m[3]; world[5] = R.m[4]; world[9]  = R.m[5]; world[13] = -Rc[1];
    world[2] = R.m[6]; world[6] = R.m[7]; world[10] = R.m[8]; world[14] = -Rc[2];
    world[15] = 1.0f;
    render::TransformMeshVerticesByMatrix(viewVerts_.data(), vc, world);

    for (int k = 0; k < pc; ++k) {
        const render::Polygon& sp = geom_->polygons[(std::size_t)k];
        render::Polygon& dp = viewPolys_[(std::size_t)k];
        dp = sp;                                  // carry uv/flags/matIndex
        // Re-point the 3 vertex ptrs into the transformed copy by source index.
        if (sp.v0) dp.v0 = &viewVerts_[(std::size_t)(sp.v0 - geom_->vertices)];
        if (sp.v1) dp.v1 = &viewVerts_[(std::size_t)(sp.v1 - geom_->vertices)];
        if (sp.v2) dp.v2 = &viewVerts_[(std::size_t)(sp.v2 - geom_->vertices)];
    }
    viewGeom_.vertices    = viewVerts_.data();
    viewGeom_.polygons    = viewPolys_.data();
    viewGeom_.polyCount   = pc;
    viewGeom_.polyCap     = pc;
    viewGeom_.vertexCount = vc;
}

// sceneWalk hook — BeginUniverseFrame's project/cull/append walk over the real mesh.
int UniverseFrameDriver::doSceneWalk() {
    if (!geom_)
        return 0;
    db_.count = 0;

    // Orient the object through the real view-rotation leaf, then project the copy.
    buildViewGeometry();
    render::MeshGeometry* g = &viewGeom_;

    // Transformed bounds (view space, mesh centred at origin).
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (int i = 0; i < g->vertexCount; ++i) {
        const render::Vertex& v = g->vertices[i];
        const float p[3] = {v.x, v.y, v.z};
        for (int k = 0; k < 3; ++k) { if (p[k] < lo[k]) lo[k] = p[k]; if (p[k] > hi[k]) hi[k] = p[k]; }
    }
    const float mx = opt_.margin * (float)opt_.fbW;
    const float mz = opt_.margin * (float)opt_.fbH;

    if (opt_.perspective) {
        // GENUINE universe projection: push the centred mesh to positive view-z (a
        // synthesized camera distance so the whole model is in front of the eye), then
        // run the real perspective leaf VIBE_Render_ProjectObjectVertices @0x5ac970.
        const float depth = hi[2] - lo[2];
        const float halfX = 0.5f * (hi[0] - lo[0] > 1e-6f ? hi[0] - lo[0] : 1.0f);
        const float halfY = 0.5f * (hi[1] - lo[1] > 1e-6f ? hi[1] - lo[1] : 1.0f);
        const float camDist = (depth > 1e-6f ? depth : 1.0f) * 1.5f + (halfX > halfY ? halfX : halfY) * 2.0f;
        for (int i = 0; i < g->vertexCount; ++i) {
            g->vertices[i].z += camDist;       // view-z forward (all z > 0)
            g->vertices[i].clipFlags = 0x80;   // mark visible (the +76 gate)
        }
        for (int k = 0; k < g->polyCount; ++k)
            g->polygons[k].flags36 = (u8)(g->polygons[k].flags36 | 0x80);  // front-candidate
        // Map x/halfX and y/halfY at the centre depth (camDist) into the inset FB.
        render::ObjectProjectScalars s;
        s.xScale  = ((float)opt_.fbW - 2.0f * mx) * camDist / (2.0f * halfX);
        s.xOffset = 0.5f * (float)opt_.fbW;
        s.yScale  = ((float)opt_.fbH - 2.0f * mz) * camDist / (2.0f * halfY);
        s.yOffset = 0.5f * (float)opt_.fbH;
        render::ProjectObjectVertices(g->vertices, g->vertexCount, g->polygons, g->polyCount,
                                      s, /*projectAll=*/true);
    } else {
        // Affine model->screen stand-in (VIBE_Mesh_ProjectVerticesToScreen @0x5c5120).
        const float sx = (hi[0] > lo[0]) ? ((float)opt_.fbW - 2.0f * mx) / (hi[0] - lo[0]) : 1.0f;
        const float sz = (hi[2] > lo[2]) ? ((float)opt_.fbH - 2.0f * mz) / (hi[2] - lo[2]) : 1.0f;
        render::ProjectParams pp{};
        pp.eye[0] = lo[0]; pp.eye[1] = 0.0f; pp.eye[2] = lo[2];
        pp.invDepth[0] = 1.0f; pp.invDepth[1] = sx; pp.invDepth[2] = 1.0f;
        pp.biasX  = mx;
        pp.scaleY = 8.0f;
        pp.scaleX = sz;
        pp.lightCap = 254.0f;
        pp.screenW  = (float)opt_.fbW;
        render::DrawList projSink{projScratch_.data(), 0, (i32)projScratch_.size()};
        render::ProjectVerticesToScreen(g, pp,
                                        /*objFlags530=*/opt_.doubleSided ? 0x40 : 0,
                                        /*viewCull42=*/0, &projSink);
    }

    // Object light cache shade (VIBE_Light_BuildObjectCache @0x5c8218). A UNIVERSE
    // object's per-vertex shade comes from the light cache, NOT the character Y-depth
    // term ProjectVerticesToScreen just wrote. The cache SEEDS each vertex from the
    // LIVE global ambient triple flt_64A074/78/7C (render::LightAtmos() — the values
    // VIBE_SkyColor_BlendBandLighting @0x5b85e4 stores each day-cycle rebuild; static
    // image 200/200/200) and finalizes to the luma byte — the engine's no-affecting-
    // lights object shade, now following the day/night brightness chain. (The
    // per-light accumulation + its runtime shade-ramp LUT are the named boundary,
    // rule 8.) Overwrite the projected lightIdx.
    const render::LightAtmosGlobals& la = render::LightAtmos();
    const u8 ambientShade =
        render::FinalizeVertexShadeLuma(la.ambientR, la.ambientG, la.ambientB);
    for (int i = 0; i < g->vertexCount; ++i)
        g->vertices[i].lightIdx = ambientShade;

    SceneDrawNode node{};
    node.nodeType  = 3;
    node.cullByte  = 0;
    node.polys     = g->polygons;
    node.polyCount = g->polyCount;

    SceneBridgeContext sc{};
    sc.out = render::DrawList{db_.base1, 0, db_.capacity};
    sc.appendCtx.mode    = render::NodeAppendMode::Software;
    sc.appendCtx.baseKey = 1;
    WalkSceneTree(&node, /*walkMask=*/0x1FF, sc);
    lastNodesDispatched_ = sc.nodesDispatched;
    db_.count = sc.out.count;

    render::RadixSortDrawList(db_, (u32)db_.count, /*twoPassOnly=*/false);
    return db_.count;
}

// The rasterize step (caller-driven after the spine). With materials bound the
// textured SpanDispatch slots sample the real Textures.BIN texels.
void UniverseFrameDriver::doFlush() {
    if (!fb_) {
        lastRasterTris_ = 0;
        return;
    }
    render::MeshList list{db_.base1, db_.count};
    render::ClipContext  ctx{0, nullptr};
    render::ProjectScalars proj{1.0f, 0.0f, 1.0f, 0.0f};
    render::SpanDispatch dispatch;                 // engine defaults (slot 4 opaque, 3 blend)
    if (boundMaterials_ > 0) {
        dispatch.slot[4] = &UFD_SpanTextured;      // opaque textured (key>>24 == 4)
        dispatch.slot[3] = &UFD_SpanTextured;      // translucent textured (key>>24 == 3)
    }
    render::ClipScratch  scratch{};
    lastRasterTris_ = render::RasterizeMeshList(list, fb_, dispatch, ctx, proj, scratch);
}

UniverseFrameDriver::Result UniverseFrameDriver::RenderFrame(const Options& opt) {
    opt_ = opt;
    Result r;
    if (!geom_ || geom_->vertexCount <= 0 || geom_->polyCount <= 0)
        return r;

    if (!fb_)
        fb_ = render::SurfaceCreate(opt.fbW, opt.fbH, 16);
    if (!fb_)
        return r;

    const std::size_t cap = (std::size_t)geom_->polyCount * 2 + 16;
    if (pool1_.size() < cap) {
        pool1_.assign(cap, render::DrawListEntry{});
        pool2_.assign(cap, render::DrawListEntry{});
        projScratch_.assign(cap, render::DrawListEntry{});
    }
    db_.base1 = pool1_.data();
    db_.base2 = pool2_.data();
    db_.count = 0;
    db_.capacity = (i32)cap;

    InstallRealSceneBridge();
    texturedPolysThisFrame_ = 0;

    g_activeUFD = this;
    render::FrameState fs{};
    fs.engineOn = true;
    fs.hasWorld = true;
    fs.useViewportClear = false;
    fs.clearSuppressed  = false;

    render::FrameHooks hooks{};
    hooks.clearRect = &UFD_ClearRect;
    hooks.sceneWalk = &UFD_SceneWalk;
    // GROUND PASS wiring at the verified position (BeginUniverseFrame @0x5B3900,
    // the 0x5b3a2f arm): only when a FloorGround is bound (default unbound, so
    // every pre-existing frame stays byte-identical).
    groundDrawn_ = false;
    groundStats_ = GroundRenderStats{};
    if (opt.ground && opt.ground->valid()) {
        fs.hasTerrain       = true;       // dword_64A028 != 0
        hooks.terrain       = this;
        hooks.renderTerrain = &UFD_RenderTerrain;
    }

    render::RenderMainViewFrame(fs, hooks);
    doFlush();
    g_activeUFD = nullptr;

    r.meshVerts       = geom_->vertexCount;
    r.meshPolys       = geom_->polyCount;
    r.real            = geom_->vertexCount > 4;
    r.appendedPolys   = fs.appendedPolys;
    r.nodesDispatched = lastNodesDispatched_;
    r.rasterTris      = lastRasterTris_;
    r.textured        = boundMaterials_ > 0;
    r.boundMaterials  = boundMaterials_;
    r.texturedPolys   = texturedPolysThisFrame_;
    r.terrainDrawn      = groundDrawn_;
    r.terrainTiles      = groundStats_.tilesDrawn;
    r.terrainPolys      = (int)groundStats_.appended;
    r.terrainRasterTris = groundStats_.rasterTris;

    std::set<u32> colors;
    int nonClear = 0;
    for (int y = 0; y < opt.fbH; ++y) {
        for (int x = 0; x < opt.fbW; ++x) {
            u8 px[3];
            render::SurfaceGetPixelRgb(fb_, x, y, px);
            if (px[0] != opt.clearR || px[1] != opt.clearG || px[2] != opt.clearB) {
                ++nonClear;
                colors.insert(((u32)px[0] << 16) | ((u32)px[1] << 8) | px[2]);
            }
        }
    }
    r.nonClearPixels = nonClear;
    r.distinctColors = (int)colors.size();
    return r;
}

bool UniverseFrameDriver::PresentToDevice(shim::IGraphicsDevice& device) {
    if (!fb_)
        return false;
    shim::Surface* bb = device.backbuffer();
    if (!bb || !bb->pixels)
        return false;

    const int w = std::min(opt_.fbW, bb->width);
    const int h = std::min(opt_.fbH, bb->height);
    u8* base = static_cast<u8*>(bb->pixels);
    for (int y = 0; y < h; ++y) {
        u8* row = base + (std::size_t)y * (std::size_t)bb->pitch;
        for (int x = 0; x < w; ++x) {
            u8 px[3];
            render::SurfaceGetPixelRgb(fb_, x, y, px);    // R,G,B from the 565 frame
            if (bb->bpp == 16) {
                u16 c = (u16)(((px[0] & 0xF8) << 8) | ((px[1] & 0xFC) << 3) | (px[2] >> 3));
                reinterpret_cast<u16*>(row)[x] = c;
            } else if (bb->bpp == 32) {
                u32 c = (0xFFu << 24) | ((u32)px[0] << 16) | ((u32)px[1] << 8) | px[2];
                reinterpret_cast<u32*>(row)[x] = c;
            } else if (bb->bpp == 24) {
                row[x * 3 + 0] = px[2]; row[x * 3 + 1] = px[1]; row[x * 3 + 2] = px[0];
            }
        }
    }
    device.present();
    return true;
}

} // namespace guild::play
