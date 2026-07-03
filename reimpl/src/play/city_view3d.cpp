// =============================================================================
// guild::play — CityView3D implementation. See city_view3d.h for the design and
// the IDA grounding (positions = stadt_<CITY>.ed3 scene nodes; live objects
// rebound by the +512 owner id exactly as VIBE_Object_RebuildModelByOwner
// @0x5a8140; frame = the reconstructed universe chain).
//
// REUSED (extern, never redefined):
//   play::ParseSceneObjects / SceneObjectInst                (scene_view.h)
//   play::RealMeshSource / RealTextureSource                 (real_*_source.h)
//   play::WalkSceneTree / InstallRealSceneBridge / SceneDrawNode (wire_scene_bridge.h)
//   render::RenderMainViewFrame / FrameState / FrameHooks    (frame.h)
//   render::TransformMeshVerticesByMatrix                    (mesh_transform_walk.h)
//   render::ComputeVertexClipFlags / Frustum                 (cull.h)
//   render::ProjectObjectVertices                            (object_project.h)
//   render::RadixSortDrawList / RasterizeMeshList            (scene.h / meshlist.h)
//   render::RasterizeTexturedTriangleRgbz / RasterizeTexturedTriangle (raster*.h)
//   render::MatrixFromEuler / Transpose / Multiply / Apply / WorldToView
//   render::FinalizeVertexShadeLuma                          (object_light_shade.h)
//   sim::g_objects / kObjectCapacity                         (sim/entity.h)
// =============================================================================
#include "play/city_view3d.h"

#include "play/object_mesh_render.h"   // RasterTexturedTriangleAffine (RGB BMPs)
#include "play/wire_scene_bridge.h"
#include "render/clip.h"
#include "render/colorformat.h"
#include "render/cull.h"
#include "render/frame.h"
#include "render/mesh.h"
#include "render/mesh_transform_walk.h"
#include "render/meshlist.h"
#include "render/object_light_shade.h"
#include "render/object_project.h"
#include "render/raster.h"
#include "render/raster_textured.h"
// ---- WAVE-6 world-entity render modules (W6-INTEGRATE) ----------------------
#include "render/daycycle.h"     // ComputeSunState (sun/day-cycle driver)
#include "render/sky.h"          // RenderSky / BlendAmbientFog (sky backdrop)
#include "render/fog.h"          // SpanFog() / ConfigureFog / ComputeFogFactor
#include "render/node_lod.h"     // SelectLodFrame (per-distance node LOD)
#include "render/object_light_shade.h" // LightMeshVertices + BuildFalloffLUT
#include "render/light.h"        // BuildFalloffLUT
#include "render/sprite_scale.h" // ProjectBillboardVertices (depth-fade sprites)
#include "render/shadow_render.h"     // RenderObjectShadow (drop shadows)
#include "render/shadow_project.h"    // ProjectMeshToGround / MapShadowVertexToSurface
#include "render/fx_recon3_particle_render.h" // render_system_to_surface (particles)
#include "render/mirror.h"       // ShouldRenderMirrorPass / AppendMirroredPolys
// ---- WAVE-8 world-entity render modules (W8-INTEGRATE) ----------------------
#include "render/mesh_normals.h"           // GenerateVertexNormals (object-space normals)
#include "render/scene_lights.h"           // SceneLightSet / CullForObject (per-object cull)
#include "render/particle_emitter_create.h"// LiveSystems().WalkAndRender / SpawnEmitterAtPosition
#include "render/reflective_nodes.h"       // DeriveReflectionPlane / IsMaterialReflective
#include "render/mirror_project.h"         // PrepareReflectionNode (reflective prop pass)
// ---- WAVE-9 frame-enrichment render modules (W9-FRAME-ENRICH) ---------------
#include "render/cloth_anim.h"             // RefreshFlagAnimation @0x4b5ef8 (flag attach)
#include "render/vegetation_anim.h"        // BuildVegetationCache @0x5c8560 (veg relight)
#include "render/snow.h"         // SnowRenderStepHeader / SnowBuildQuads
#include "render/rain.h"         // RainUpdateDrop / RainRenderToSurface
#include "render/weather.h"      // WeatherUpdate (rain gate)
#include "render/scene_load.h"
#include "render/scene_node.h"
#include "render/surface.h"
#include "shim/IFileSystem.h"
#include "shim/IGraphicsDevice.h"
#include "sim/character_factory.h"   // CreateFromModel @0x402d10 (the REAL factory)
#include "sim/character_path.h"      // CharacterPathGetHooks (freeDebug @0x43923c)
#include "sim/character_query.h"     // g_live (dword_66F0D0) — the factory's table
#include "sim/entity.h"
#include "sim/person.h"              // PersonGetDword / PersonIsValidActiveRecord

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <set>

namespace guild::play {

namespace {

// The single active view the C FrameHook / SpanFill trampolines thread through
// (the engine had one set of global PolyLists; one view renders at a time —
// the same pattern UniverseFrameDriver / RealFrameScene use).
CityView3D* g_activeCV3D = nullptr;

// wave-5 W5-TX: the view whose ground tex binder is active (the GroundTexBinder
// fields are plain fn ptrs, so they thread through this file static — the same
// single-active pattern as g_activeCV3D).
CityView3D* g_groundTexOwner = nullptr;

void CV3D_ClearRect()     { if (g_activeCV3D) g_activeCV3D->doClear(); }
int  CV3D_SceneWalk(char) { return g_activeCV3D ? g_activeCV3D->doSceneWalk() : 0; }
// The renderTerrain frame hook (render::FrameHooks): BeginUniverseFrame @0x5B3900
// invokes it at the 0x5b3a2f `VIBE_Floor_RenderTerrain(dword_64A028, a2)` arm —
// after the clear, BEFORE the object scene walk (verified frame position).
void CV3D_RenderTerrain(void*, char a2) {
    if (g_activeCV3D) g_activeCV3D->doRenderTerrain(a2);
}
// WAVE-6 FrameHooks trampolines (W6-INTEGRATE): particles + mirror run inside
// BeginUniverseFrame @0x5b3900 (renderParticles @0x5b3a98 after the object walk;
// buildMirrors @0x5b3af0 after particles). Both no-op when the active view did not
// opt into the feature; default Options keep every frame byte-identical.
void CV3D_RenderParticles(char a2);
void CV3D_BuildMirrors(char a2);
void CV3D_ResetLights();

// Textured SpanDispatch slot (slots 3 + 4): decode the frame-local poly key
// ((instanceIdx<<12)|matIndex) carried in Polygon::matIndex, bind the material's
// real Textures.BIN texel record and rasterize through the 1:1 affine textured
// leaf render::RasterizeTexturedTriangleRgbz (gilde.exe 0x5F6C30). Falls back to
// the flat/shaded path when untextured/unbound.
int CV3D_SpanTextured(render::Surface* fb, const render::Polygon& tri) {
    CityView3D* d = g_activeCV3D;
    const render::Vertex* vp[3] = {tri.v0, tri.v1, tri.v2};
    if (d && fb && vp[0] && vp[1] && vp[2]) {
        const CityView3D::BoundTex* bt = d->boundTexForKey(tri.matIndex);
        if (bt && bt->tex && bt->palette && !bt->tex->texels.empty()) {
            const float w = (float)bt->tex->mipWidth;
            // WAVE-7 W7-FOGPIX — per-vertex D3D fog factor from the view-space depth
            // (ComputeFogFactor over the squared camera-space distance, exactly the
            // engine's @0x5ac9aa billboard pass). The vertex keeps its view-space
            // x/y/z (ProjectObjectVertices writes only screenX/Y), so d2 = x²+y²+z².
            // RasterizeTexturedTriangleRgbz interpolates the seeded factor per pixel
            // when SpanFog().enabled; default 255 keeps fog-off tris byte-identical.
            const render::FogState* fog =
                d->fogStateForSpan().enabled ? &d->fogStateForSpan() : nullptr;
            // Gouraud armed for this triangle when any vertex carries a
            // non-neutral RGB diffuse: the palette row goes FULL BRIGHT (255)
            // and the per-pixel modulate does the shading. Neutral shades keep
            // the luma-row path (and the untextured fallback always uses the
            // vertex +66 luma, so props never render fullbright).
            const bool gouTri =
                (vp[0]->shadeR & vp[0]->shadeG & vp[0]->shadeB &
                 vp[1]->shadeR & vp[1]->shadeG & vp[1]->shadeB &
                 vp[2]->shadeR & vp[2]->shadeG & vp[2]->shadeB) != 0xFF;
            render::RgbzVertex rv[3];
            for (int i = 0; i < 3; ++i) {
                rv[i].x = vp[i]->screenX;
                rv[i].y = vp[i]->screenY;
                rv[i].u = vp[i]->u * w;
                rv[i].v = vp[i]->v * w;
                // Vertex +66 light byte -> the span's palette light row
                // (lightRow8 = avg << 8): the day/night ambient + per-vertex
                // sun/point shade the frame wrote into lightIdx now actually
                // scales the texel colours (the 256-row shade-ramp palette).
                rv[i].light = gouTri ? (u8)255 : vp[i]->lightIdx;
                // Gouraud RGB diffuse (+68/69/70): all-255 leaves the span
                // byte-identical; the gouraudLight frame writes real shades.
                rv[i].shadeR = vp[i]->shadeR;
                rv[i].shadeG = vp[i]->shadeG;
                rv[i].shadeB = vp[i]->shadeB;
                if (fog) {
                    const float dx = vp[i]->x, dy = vp[i]->y, dz = vp[i]->z;
                    rv[i].fogFactor = render::ComputeFogFactor(*fog, dx*dx + dy*dy + dz*dz);
                }
            }
            // Colour-key routing (wave-5 W5-CKEY, 0x5dea50 NOW decompiled). A
            // 24-bit colour-keyed texture (record +104 bit 2 =
            // render::kTexFlagColourKey) routes through the MASKED span, keyed on
            // the DDraw colour-key value pal[0] == BLACK
            // (VIBE_Render_LoadAndStretchTexture @0x5dea50 v111 = 0 for a 24-bit
            // source; DDBLT_KEYSRC at 0x5df086). Faithful software realisation:
            // skip texels whose RESOLVED 16bpp value == 565(black) == 0 (NOT
            // source index 0; the octree quantizer @0x603180 does not place black
            // at 0). bit 2 is the colour key; bit 3 (flags & 8) is the "_NM"
            // mip-bias flag (the wave-3/4 mistake), not transparency.
            const bool keyed =
                render::ColourKeyEnabled() && render::TextureIsColourKeyed(*bt->tex);
            const i32 blackKey565 = 0;  // 565 encoding of (0,0,0)
            static int dbgKeyed = 0, dbgPlain = 0, dbgOnce = 0;
            if (std::getenv("GUILD_DEBUG_BIND")) {
                keyed ? ++dbgKeyed : ++dbgPlain;
                if (++dbgOnce % 2000 == 0)
                    std::printf("[span] keyed=%d plain=%d\n", dbgKeyed, dbgPlain);
            }
            int drew;
            // NOTE: material +194 bit 1 (b2 & 2) is NOT the alpha route — an
            // A/B against the original showed foliage/bark drawing OPAQUE
            // (50/50-blending them ghosts the crowns). Bit 0 alone keys.
            if (keyed) {
                drew = render::RasterizeTexturedTriangleRgbzMasked(
                    fb, rv, *bt->tex, bt->palette, tri.flags38, blackKey565);
            } else {
                drew = render::RasterizeTexturedTriangleRgbz(fb, rv, *bt->tex,
                                                             bt->palette,
                                                             tri.flags38);
            }
            if (drew) {
                d->addTexturedPoly();
                return 1;
            }
        }
        if (bt && bt->rgb) {
            // 24-bit (non-palettized) BMP bind — the person character textures —
            // sampled via the in-tree affine kernel (the legacy person pass's
            // textured path for exactly these BMPs).
            if (RasterTexturedTriangleAffine(fb, vp[0], vp[1], vp[2], *bt->rgb) > 0) {
                d->addTexturedPoly();
                return 1;
            }
        }
    }
    // Fallback: the slot-4 default (render::SpanFillTexturedOpaque), which
    // routes by surface format — 8bpp: the shaded path @0x5F7D58; 16bpp: the
    // LEVEL-shaded 1x1 white default binding (BindActive @0x5db564 slot==0,
    // level = max vertex +66 byte) through the textured leaf. Mirrors
    // universe_render.cpp's wired behaviour (see render/meshlist.cpp RasterTri).
    if (fb && vp[0] && vp[1] && vp[2])
        return render::SpanFillTexturedOpaque(fb, tri);
    return 1;
}

void CV3D_RenderParticles(char a2) { if (g_activeCV3D) g_activeCV3D->doParticles(a2); }
void CV3D_BuildMirrors(char a2)    { if (g_activeCV3D) g_activeCV3D->doMirrors(a2); }
void CV3D_ResetLights()            { if (g_activeCV3D) g_activeCV3D->doResetLights(); }

std::string UpperBase(const std::string& n) {
    std::size_t sl = n.find_last_of("/\\");
    std::string base = (sl == std::string::npos) ? n : n.substr(sl + 1);
    std::size_t dot = base.rfind('.');
    if (dot != std::string::npos) base = base.substr(0, dot);
    for (auto& c : base) c = (char)std::toupper((unsigned char)c);
    return base;
}

inline float BitsToFloat(u32 bits) {
    float f;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}

bool StrEqNoCase(const std::string& a, const char* b) {
    std::size_t n = a.size();
    if (std::strlen(b) != n)
        return false;
    for (std::size_t i = 0; i < n; ++i)
        if (std::toupper((unsigned char)a[i]) != std::toupper((unsigned char)b[i]))
            return false;
    return true;
}

// ---------------------------------------------------------------------------
// Factory trampoline context (sim::CharacterFactoryHooks members are plain
// function pointers, so the active bind threads through file statics — the same
// single-active pattern as g_activeCV3D).
// ---------------------------------------------------------------------------
struct PersonFactoryCtx {
    CityView3D*  view = nullptr;
    std::string  member;                       // resolved Objects.BIN member
    std::vector<std::string> preloadClips;     // the factory's PreloadAniSet names
    std::vector<u8>* nodeBuf = nullptr;        // the per-person engine-node image
};
PersonFactoryCtx* g_personFactoryCtx = nullptr;

// The engine scene-node image the attach hands back. CreateMesh @0x4029c4
// writes the node's raw flag/state offsets (+529..+536, +72) and reads the
// +492 sub-object pointer — so the handle must be a real node-sized buffer
// (the original node record is > 536 bytes; +492 stays null here, exactly the
// "no drawdata yet" state, so the 1.8f anim-rate write is skipped as in the
// original's null-sub-object path).
constexpr std::size_t kPersonNodeBytes = 560;

// VIBE_Object_AttachToUniverseNode @0x5b3e30 stand-in target: resolve the model
// through Objects.BIN exactly as Mesh_LoadOrFindByName @0x5d345c ("*<name>.bgf",
// case-insensitive VFS) and hand back the node image for the factory's raw
// field writes. Null (load failure) when the model is not shipped.
void* CV3D_PersonAttach(void* /*parentMat*/, const char* model) {
    PersonFactoryCtx* c = g_personFactoryCtx;
    if (!c || !c->view || !c->nodeBuf)
        return nullptr;
    if (!c->view->ResolvePersonMesh(model, &c->member))
        return nullptr;
    c->nodeBuf->assign(kPersonNodeBytes, 0);
    return c->nodeBuf->data();
}

// VIBE_Character_PreloadAniSet @0x403c34 capture: record the factory's genuine
// preload clip names ({"bewegung/gehen","stehen/stehen_newnoise"}).
void CV3D_PersonPreload(sim::LiveActor*, const char* const* names, int count) {
    PersonFactoryCtx* c = g_personFactoryCtx;
    if (!c)
        return;
    for (int i = 0; i < count; ++i)
        if (names[i])
            c->preloadClips.emplace_back(names[i]);
}

int CV3D_PersonStrstr(const char* haystack, const char* needle) {
    return (haystack && needle && std::strstr(haystack, needle)) ? 1 : 0;
}

// VIBE_Character_Destroy @0x402120 essentials on the CreateFromModel failure
// path: clear the g_live slot and free the record (the factory header documents
// exactly this teardown; the full Destroy is its own reconstruction target).
void CV3D_PersonDestroy(sim::LiveActor* rec) {
    if (!rec)
        return;
    if (rec->slotIndex >= 0 && rec->slotIndex < sim::kLiveCapacity &&
        sim::g_live[rec->slotIndex] == rec)
        sim::g_live[rec->slotIndex] = nullptr;
    sim::CharacterPathGetHooks().freeDebug(rec);
}

} // namespace

// ---------------------------------------------------------------------------
// WAVE-6 W6-INTEGRATE — the persistent weather systems (rain streaks + snow
// flakes). Seeded once (RandNext draw order, 0x429098 / 0x42a014), integrated +
// rendered per frame as a screen-space overlay AFTER the object/particle frame —
// exactly VIBE_Snow_Render @0x42b5b0 / VIBE_Rain_Render @0x429c38, which run their
// own Begin/EndScene over the finished 3D frame. Held by unique_ptr so it only
// allocates when Options::weather first turns on.
// ---------------------------------------------------------------------------
struct CityWeather {
    render::RainSystem rain{};
    std::vector<render::RainDrop> rainDrops;
    render::SnowSystem snow{};
    render::SnowSystemHdr snowHdr{};
    std::vector<render::SnowFlake> snowFlakes;
    std::vector<render::SnowVertex> snowVerts;
    bool seeded = false;

    void Seed(int dropCount) {
        rainDrops.assign((std::size_t)dropCount, render::SnowFlake{});
        rain.capacity = dropCount;
        rain.count = dropCount;
        rain.drops = rainDrops.data();
        render::RainSeedDrops(rain);          // 0x429098 jittered seed

        snowFlakes.assign((std::size_t)dropCount, render::SnowFlake{});
        snow.capacity = dropCount;
        snow.count = dropCount;
        snow.flakes = snowFlakes.data();
        render::SnowSeedFlakes(snow);         // 0x42a014 jittered seed
        snowHdr.count = dropCount;
        snowHdr.capacity = dropCount;
        snowHdr.flakes = snowFlakes.data();
        snowVerts.assign((std::size_t)dropCount * 3, render::SnowVertex{});
        seeded = true;
    }
};

// ---------------------------------------------------------------------------
// gilde.exe 0x57c8f0 / 0x4b0ee8 — entrance-dummy resolution over a parsed scene
// (see header). Pre-order first match; descendant test walks the parent chain.
// ---------------------------------------------------------------------------
int FindEntranceDummyNode(const std::vector<SceneObjectInst>& scene, int buildingIdx) {
    if (buildingIdx < 0 || (std::size_t)buildingIdx >= scene.size())
        return -1;
    auto isDescendant = [&](int idx) {
        int p = scene[(std::size_t)idx].parent;
        int guard = 0;
        while (p >= 0 && (std::size_t)p < scene.size() && guard++ < (int)scene.size()) {
            if (p == buildingIdx)
                return true;
            p = scene[(std::size_t)p].parent;
        }
        return false;
    };
    static const char* kTags[2] = {"dummy_EINGANG", "dummy_TUER"};  // 0x57ca38/0x57ca54
    for (const char* tag : kTags) {
        for (std::size_t i = 0; i < scene.size(); ++i) {
            if (!StrEqNoCase(scene[i].name, tag))
                continue;
            if (isDescendant((int)i))
                return (int)i;
        }
    }
    return -1;
}

// ===========================================================================
// Engine math helpers (1:1 with the cited functions).
// ===========================================================================

// gilde.exe 0x5accd0 (prologue) — the side-plane build of BuildViewMatrix
// (operands read from the DISASSEMBLY; the Hex-Rays FPU pairing is wrong here):
//   5acce1  fld flt_13FCD0C ; fld flt_13FC518 ; call Atan2
//       ang  = atan2(W*0.5, viewScale)          (flt_13FC518 = width * 0.5)
//   5acd15  fld flt_13FCAF8 ; fchs ; fld flt_13FC514 ; call Atan2
//       ang2 = atan2(H*0.5, -yScale)            (flt_13FC514 = height * 0.5,
//                                                flt_13FCAF8 = viewScale * -1.0)
//   plane0 = { cos(ang), 0, sin(ang),  d=-eps }   flt_13DCDA0/A8, dword_13DCDAC
//   plane1 = {-cos(ang), 0, sin(ang),  d=+eps }   flt_13DCDB0/B8, dword_13DCDBC
//   plane2 = { 0, cos(ang2), sin(ang2),d=+eps }   flt_13DCDC4/C8, dword_13DCDCC
//   plane3 = { 0,-cos(ang2), sin(ang2),d=-eps }   flt_13DCDD4/D8, dword_13DCDDC
// with eps the binary's exact dword 0x360637BD (~2.0e-6, mov esi,360637BDh at
// 0x5acd4d) and -eps 0xB60637BD. near/far = flt_13FC76C / flt_13FCAFC.
void BuildEngineFrustum(float fbW, float fbH, float viewScale,
                        float nearZ, float farZ, render::Frustum& out) {
    const float epsPos = BitsToFloat(0x360637BDu);
    const float epsNeg = BitsToFloat(0xB60637BDu);
    const float ang  = std::atan2(fbW * 0.5f, viewScale);
    const float s = std::sin(ang), c = std::cos(ang);
    const float ang2 = std::atan2(fbH * 0.5f, viewScale);   // -(-scale) == scale
    const float s2 = std::sin(ang2), c2 = std::cos(ang2);

    out.plane[0][0] = c;   out.plane[0][1] = 0.0f; out.plane[0][2] = s;  out.plane[0][3] = epsNeg;
    out.plane[1][0] = -c;  out.plane[1][1] = 0.0f; out.plane[1][2] = s;  out.plane[1][3] = epsPos;
    out.plane[2][0] = 0.0f; out.plane[2][1] = c2;  out.plane[2][2] = s2; out.plane[2][3] = epsPos;
    out.plane[3][0] = 0.0f; out.plane[3][1] = -c2; out.plane[3][2] = s2; out.plane[3][3] = epsNeg;
    out.nearZ = nearZ;
    out.farZ  = farZ;
}

// The composed record+72 model->VIEW matrix the static vertex walk applies
// (VIBE_Mesh_InterpolateMorphVertices @0x5c953c reads it as
//   out = [m0 m4 m8; m1 m5 m9; m2 m6 m10] * v + m[12..14]).
// Camera basis: R = MatrixFromEuler(-rot) (type-3 camera node, SetWorldTranslation
// @0x5af50c negates), view = R^T * (world - eye)  (PointToBoneLocalSpace @0x5c8c40).
void ComposeModelViewMatrix(const render::Mat3& localToWorld,
                            const float worldPos[3], const CityCamera3D& cam,
                            float out16[16]) {
    const float negRot[3] = {-cam.rot[0], -cam.rot[1], -cam.rot[2]};
    const render::Mat3 Rc = render::MatrixFromEuler(negRot);
    const render::Mat3 Vr = render::Transpose(Rc);          // view rotation R^T
    const render::Mat3 L  = render::Multiply(Vr, localToWorld);
    float tv[3];
    render::WorldToView(Rc, cam.eye, worldPos, tv);          // R^T * (pos - eye)

    std::memset(out16, 0, 16 * sizeof(float));
    out16[0] = L.m[0]; out16[4] = L.m[1]; out16[8]  = L.m[2];
    out16[1] = L.m[3]; out16[5] = L.m[4]; out16[9]  = L.m[5];
    out16[2] = L.m[6]; out16[6] = L.m[7]; out16[10] = L.m[8];
    out16[12] = tv[0]; out16[13] = tv[1]; out16[14] = tv[2];
    out16[15] = 1.0f;
}

// Invert the engine camera forward for a pitch/yaw aim (roll 0). Forward (the
// world direction mapping to +view z) is column 2 of R = MatrixFromEuler(-rot):
//   fwd = (-cos(rx)*sin(ry), sin(rx), cos(rx)*cos(ry))
// so rx = asin(fy), ry = atan2(-fx, fz). Host convenience (the exact inverse of
// render::CameraForward); the camera math itself stays the engine's.
void AimCamera(CityCamera3D& cam, const float eye[3], const float target[3]) {
    cam.eye[0] = eye[0]; cam.eye[1] = eye[1]; cam.eye[2] = eye[2];
    float f[3] = {target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]};
    const float len = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    if (len < 1e-6f) {
        cam.rot[0] = cam.rot[1] = cam.rot[2] = 0.0f;
        return;
    }
    f[0] /= len; f[1] /= len; f[2] /= len;
    cam.rot[0] = std::asin(std::max(-1.0f, std::min(1.0f, f[1])));
    cam.rot[1] = std::atan2(-f[0], f[2]);
    cam.rot[2] = 0.0f;
}

// ===========================================================================
// CityView3D.
// ===========================================================================

CityView3D::CityView3D() = default;

CityView3D::~CityView3D() {
    UnbindPersons();   // release the factory records (g_live slots)
    if (g_activeCV3D == this)
        g_activeCV3D = nullptr;
    if (fb_)
        render::SurfaceDestroy(fb_);
    fb_ = nullptr;
}

bool CityView3D::Init(shim::IFileSystem* fs) {
    fs_ = fs;
    mounted_ = false;
    baseToMember_.clear();
    if (!fs)
        return false;
    const bool scenesOk = scenes_.Mount(fs, "Resources/scenes.BIN", /*caseInsensitive=*/true);
    const bool srcOk    = src_.MountArchive(fs, "Resources/Objects.BIN", /*caseInsensitive=*/true);
    const bool namesOk  = names_.Mount(fs, "Resources/Objects.BIN", /*caseInsensitive=*/true);
    if (namesOk) {
        // UPPER basename -> member index (first wins), so a scene's short model
        // name ("gb_KAPELLE") resolves to its real .bgf member.
        for (const auto& m : names_.members()) {
            const std::string& n = m.name;
            if (n.size() < 4) continue;
            std::string tail = n.substr(n.size() - 4);
            for (auto& ch : tail) ch = (char)std::tolower((unsigned char)ch);
            if (tail != ".bgf") continue;
            baseToMember_.emplace(UpperBase(n), n);
        }
    }
    tex_.Mount(fs, "Resources/Textures.BIN");   // optional (textured raster)
    SetupGroundTexCache();                       // wave-5 W5-TX (ground slot BMPs)
    mounted_ = scenesOk && srcOk && namesOk;
    return mounted_;
}

// wave-5 W5-TX: serve the ground FloorTextureResolver's "*"+name+".BMP" loads
// from Textures.BIN. The floor slot names (Floor+0x1A64, e.g. "WIESE","SAND")
// are bare stems; the BMPs live as archive members ("_DYNAMIC/Boden/Wiese.bmp").
// The engine's VIBE_Vfs_ResolveAndBuildPath @0x4500a0 resolves the wildcard over
// every mounted container; here the cache's BmpFetch resolves the bare name
// through the mounted Textures.BIN bare-name index and hands back the raw member
// BMP bytes (the same bytes VIBE_Bmp_LoadBuffer would have decoded).
void CityView3D::SetupGroundTexCache() {
    if (groundTexReady_ || !tex_.mounted())
        return;
    render::TextureBin* bin = &tex_.bin();
    groundTexCache_.SetBmpFetch(
        [bin](const std::string& name, std::vector<u8>& out) -> bool {
            if (!bin->mounted())
                return false;
            std::string member = bin->ResolveName(name.c_str());
            io::ArchiveMount* mount = bin->mount();
            if (member.empty() || !mount)
                return false;
            return mount->OpenMember(member.c_str(), out);
        });
    groundTexReady_ = true;
}

// wave-5 W5-TX: bind the FloorTextureResolver to the parsed floor's 8 slot names
// + the Textures.BIN-backed cache, install it as TerrainRenderHooks::getTileTexture
// (the @0x5ba1e8 fetch the walk routes through), and hand GroundFrame the binder
// that turns a resolved slot record into (texels + 565 palette). Idempotent;
// re-run whenever the ground/cache changes. Safe/additive: when the cache is
// unmounted or a slot has no BMP the resolver returns null -> the white default.
void CityView3D::BindGroundTextures() {
    if (groundTexBound_ || !ground_.valid())
        return;
    SetupGroundTexCache();
    if (!groundTexReady_)
        return;
    // Bind the 8 slot names (render::TileLightSource[8].name == char[64]) + cache.
    // The resolver keeps `slotNames` BY POINTER, so the array must outlive it —
    // it is the member groundSlotNames_, not a local.
    for (int i = 0; i < 8; ++i) {
        std::memset(groundSlotNames_[i], 0, sizeof(groundSlotNames_[i]));
        std::strncpy(groundSlotNames_[i], ground_.typeNames[i].name,
                     sizeof(groundSlotNames_[i]) - 1);
    }
    groundTexResolver_.Bind(groundSlotNames_, &groundTexCache_);
    render::SetActiveFloorTextureResolver(&groundTexResolver_);
    play::TerrainRenderHooks hooks{};
    hooks.getTileTexture = &render::FloorTextureResolver::GetTileTextureHook;
    play::SetTerrainRenderHooks(&hooks);

    // The GroundFrame tex binder: the active getTileTexture hook returns the
    // resolved render::Texture* (cast from const void*); palette565 builds (once,
    // cached by record ptr) the 256-entry RGB565 LUT from the decoded source
    // palette (the same (r>>3,g>>2,b>>3) pack the object material bind uses).
    play::GroundTexBinder binder{};
    binder.getTileTextureRec = &CityView3D::GroundTileTextureRec;
    binder.palette565        = &CityView3D::GroundTilePalette565;
    groundFrame_.SetTexBinder(&binder);
    g_groundTexOwner = this;
    groundTexBound_ = true;
}

// WAVE-7 W7-WATERTEX — the WaterTextureLoadFn BuildWater calls (water-texture-
// wave7.md handoff (a)): resolve EF_WASS_06A_2T_W_AN0 through the SAME ground tex
// cache (the Textures.BIN BmpFetch already installed by SetupGroundTexCache). The
// engine first attempts the animation-group resolve (VIBE_Animation_GetPtr
// @0x5d9774, the present-coupled animation bank — named gap); this hook stands in
// for the LoadByName(name,172,0,0) fallback. Returns the loaded render::Texture*
// record (cast to void* for the WaterMesh+4 handle), or null -> the white default.
void* CityView3D::CV3D_LoadWaterTexture(const char* name, int /*flags*/, void* ctx) {
    auto* self = static_cast<CityView3D*>(ctx);
    if (!self || !name)
        return nullptr;
    self->SetupGroundTexCache();           // ensure the Textures.BIN BmpFetch is wired
    if (!self->groundTexReady_)
        return nullptr;
    // "*"+name+".BMP" is the engine's wildcard; the cache stores/compares `name`.
    const int slot = self->groundTexCache_.LoadByName(name, name);
    if (slot < 0) {
        self->waterTexRec_ = nullptr;
        return nullptr;                    // no EF_WASS member -> white default
    }
    const render::Texture* rec = self->groundTexCache_.record(slot);
    self->waterTexRec_ = rec;
    return const_cast<render::Texture*>(rec);
}

// GroundTexBinder trampoline: the active getTileTexture hook (the bound
// FloorTextureResolver) returns the resolved slot record as const void*.
const render::Texture* CityView3D::GroundTileTextureRec(u8 typeByte) {
    const auto& hooks = play::GetTerrainRenderHooks();
    if (!hooks.getTileTexture)
        return nullptr;
    return static_cast<const render::Texture*>(hooks.getTileTexture(typeByte));
}

const u16* CityView3D::GroundTilePalette565(const render::Texture* rec) {
    return g_groundTexOwner ? g_groundTexOwner->groundPalette565For(rec) : nullptr;
}

// Build (once, cached by record ptr) the HiColTab light-ramp block the textured
// span samples as palBase[(avg<<8)|texel] (== *(tex+72)): 63 ramp rows of 256
// entries, row L entry i == sourcePalette[i] scaled to L/62 of full, packed to
// the active ColorFormat — the VIBE_HiColTab ramp (render/hicoltab.cpp
// HiColTabAddEntry) laid out BY SOURCE INDEX so the raw texel index addresses it
// directly (the engine compacts texels onto HiColTab indices in
// LoadSoftPalettize; here the texels keep the source indices, so the block is
// source-index-major — identical ramp values, no texel remap). Row 0 is black
// (L=0) and row 62 is the full colour, exactly HiColTabAddEntry's
// round(channel/62 * L) ramp.
const u16* CityView3D::groundPalette565For(const render::Texture* rec) {
    if (!rec)
        return nullptr;
    auto it = groundTexPal_.find(rec);
    if (it != groundTexPal_.end())
        return it->second.data();
    if (rec->paletteStore.size() < 768)
        return nullptr;
    const render::ColorFormat fmt = render::Format565();
    // 63 ramp rows (0..62) of 256 u16 entries: word offset (L<<8)+i.
    std::vector<u16> block((std::size_t)63 * 256, 0);
    for (int i = 0; i < 256; ++i) {
        const double r = (double)rec->paletteStore[3 * i + 0];
        const double g = (double)rec->paletteStore[3 * i + 1];
        const double b = (double)rec->paletteStore[3 * i + 2];
        for (int L = 0; L < 63; ++L) {
            // round(channel/62 * L) — HiColTabAddEntry's per-step ramp (it adds
            // 0.5 then truncates toward zero == round-half-up for >= 0 inputs).
            const u8 rr = (u8)(int)(r / 62.0 * (double)L + 0.5);
            const u8 gg = (u8)(int)(g / 62.0 * (double)L + 0.5);
            const u8 bb = (u8)(int)(b / 62.0 * (double)L + 0.5);
            block[(std::size_t)(L << 8) + (std::size_t)i] =
                (u16)render::PackColor(fmt, rr, gg, bb);
        }
    }
    auto res = groundTexPal_.emplace(rec, std::move(block));
    return res.first->second.data();
}

std::string CityView3D::ResolveMember(const std::string& nameOrMember) const {
    if (nameOrMember.empty())
        return std::string();
    auto it = baseToMember_.find(UpperBase(nameOrMember));
    return (it != baseToMember_.end()) ? it->second : std::string();
}

bool CityView3D::LoadCity(const char* cityName) {
    if (!mounted_ || !cityName || !*cityName)
        return false;
    cityName_ = cityName;

    // VIBE_Scene_LoadStadtScene @0x500218: sprintf("scenes/*stadt_%s.ed3", city).
    const std::string member = std::string("Staedte/stadt_") + cityName + ".ed3";
    std::vector<u8> ed3;
    if (!scenes_.OpenMember(member.c_str(), ed3) || ed3.empty())
        return false;
    return loadSceneBuffer(ed3.data(), ed3.size());
}

bool CityView3D::LoadCityFromWorld(const std::vector<u8>& sceneStream) {
    // The captured embedded .cty scene (io::LoadWorldEx) — the stream
    // VIBE_Save_PostLoadInitScene @0x5a7ef8 reads. Same .ed3 grammar.
    if (!mounted_ || sceneStream.empty())
        return false;
    return loadSceneBuffer(sceneStream.data(), sceneStream.size());
}

bool CityView3D::loadSceneBuffer(const u8* ed3data, std::size_t ed3size) {
    scene_.clear();
    sceneRot_.clear();
    scenePos_.clear();
    ownerToNode_.clear();
    instances_.clear();
    instanceOfNode_.clear();
    bound_.clear();
    fogFar_ = 0.0f;
    hasFog_ = false;
    skyBands_ = render::SkySceneTable{};
    hasSkyBands_ = false;
    const u8* p = ed3data;
    std::size_t size = ed3size;
    std::vector<u8> ed3(p, p + size);

    // Header (fog far -> the far plane, ConfigureFog @0x5ae384 behaviour).
    // WAVE-7 W7-SKYBANDS: the SAME parsed header carries the per-scene sky/fog band
    // table (light-rig portion of VIBE_Scene_LoadFromStream @0x5e7e38); LoadSkyBands
    // adapts it onto skyBands_ so the time-of-day clear uses the REAL bands instead
    // of the wave-6 fallback. A scene without bands leaves the table zero (the
    // fallback path is taken — byte-identical).
    {
        render::SceneReader r(ed3.data(), ed3.size());
        render::SceneHeader h;
        if (render::ParseSceneHeader(r, h)) {
            if (h.hasFog) {
                hasFog_ = true;
                fogFar_ = h.fogFar;
            }
            skyBands_ = render::LoadSkyBands(h);
            hasSkyBands_ = (skyBands_.band_count > 0);
        }
    }

    scene_ = ParseSceneObjects(ed3.data(), ed3.size());
    if (scene_.empty())
        return false;

    // GROUND (terrain-ground wave 4): parse the floor block of the SAME stream
    // (VIBE_WorldIo_LoadFloorRegions @0x5e78a8 behind the floor-flag byte of
    // VIBE_Scene_LoadFromStream @0x5e7e38) and build the walk-ready floor.
    floorBlock_ = render::ParseSceneFloorBlock(ed3.data(), ed3.size());
    ground_     = BuildFloorGroundFromBlock(floorBlock_);
    groundFrame_ = GroundFrame{};           // re-bound lazily on the next frame
    cityHmBuilt_ = false;

    composeSceneTransforms();
    return true;
}

bool CityView3D::LoadCityFromNodes(std::vector<SceneObjectInst> nodes) {
    if (nodes.empty())
        return false;
    scene_.clear();
    sceneRot_.clear();
    scenePos_.clear();
    ownerToNode_.clear();
    instances_.clear();
    instanceOfNode_.clear();
    bound_.clear();
    fogFar_ = 0.0f;
    hasFog_ = false;
    skyBands_ = render::SkySceneTable{};
    hasSkyBands_ = false;
    // no stream -> no floor block (the ground pass stays inert).
    floorBlock_ = render::SceneFloorBlock{};
    ground_     = FloorGround{};
    groundFrame_ = GroundFrame{};
    cityHmBuilt_ = false;
    scene_ = std::move(nodes);
    composeSceneTransforms();
    return true;
}

void CityView3D::composeSceneTransforms() {
    // Composed world transforms up the parent chain (the engine's
    // VIBE_Transform_PointThroughBoneChain @0x5c8b38: a node's local->world
    // rotation is MatrixFromEuler(euler)^T — the chain dots points with the
    // rotation COLUMNS — seated with the parent-composed position). Pre-order,
    // so a parent is always composed before its children.
    const std::size_t n = scene_.size();
    sceneRot_.resize(n);
    scenePos_.resize(3 * n);
    for (std::size_t i = 0; i < n; ++i) {
        const SceneObjectInst& o = scene_[i];
        const render::Mat3 localRot = render::Transpose(render::MatrixFromEuler(o.euler));
        if (o.parent >= 0 && (std::size_t)o.parent < i) {
            const render::Mat3& pr = sceneRot_[(std::size_t)o.parent];
            const float* pp = &scenePos_[3 * (std::size_t)o.parent];
            sceneRot_[i] = render::Multiply(pr, localRot);
            float rp[3];
            render::Apply(pr, o.pos, rp);
            scenePos_[3 * i + 0] = rp[0] + pp[0];
            scenePos_[3 * i + 1] = rp[1] + pp[1];
            scenePos_[3 * i + 2] = rp[2] + pp[2];
        } else {
            sceneRot_[i] = localRot;
            scenePos_[3 * i + 0] = o.pos[0];
            scenePos_[3 * i + 1] = o.pos[1];
            scenePos_[3 * i + 2] = o.pos[2];
        }
        // The +512 owner-object id -> node index (RebuildModelByOwner's match key).
        if (o.ownerId != 0)
            ownerToNode_.emplace(o.ownerId, (int)i);
    }

    buildInstances();
}

void CityView3D::buildInstances() {
    instances_.clear();
    instanceOfNode_.assign(scene_.size(), -1);
    for (std::size_t i = 0; i < scene_.size(); ++i) {
        const SceneObjectInst& o = scene_[i];
        if ((o.type != 1 && o.type != 4) || !o.hasMesh || o.noRender)
            continue;
        const std::string member = ResolveMember(o.mesh);
        if (member.empty())
            continue;   // shipped scene mesh not in Objects.BIN (rare; skipped)
        Instance inst;
        inst.name = o.name;
        inst.member = member;
        inst.sceneIndex = (int)i;
        inst.objectId = 0;
        inst.l2w = sceneRot_[i];
        inst.pos[0] = scenePos_[3 * i + 0];
        inst.pos[1] = scenePos_[3 * i + 1];
        inst.pos[2] = scenePos_[3 * i + 2];
        instanceOfNode_[i] = (int)instances_.size();
        instances_.push_back(std::move(inst));
    }
}

int CityView3D::BindWorldObjects() {
    using namespace guild::sim;
    bound_.clear();
    modelUnresolved_ = 0;
    unplaced_ = 0;
    buildInstances();   // re-runnable: reset object tags / extra instances

    for (int slot = 0; slot < kObjectCapacity; ++slot) {
        const ObjectRec& rec = g_objects[slot];
        if (!rec.alive)
            continue;

        // 1. Placement (hook, else the REAL owner-id scene-node match —
        //    VIBE_Object_RebuildModelByOwner @0x5a8140: node+512 == record id).
        CityPlacement place;
        int sceneIdx = -1;
        const SceneObjectInst* node = nullptr;
        bool placed = false;
        if (hooks_.resolveObjectPlacement) {
            placed = hooks_.resolveObjectPlacement(rec, place);
        } else {
            auto it = ownerToNode_.find((u32)rec.id);
            if (it != ownerToNode_.end()) {
                sceneIdx = it->second;
                node = &scene_[(std::size_t)sceneIdx];
                place.pos[0] = scenePos_[3 * (std::size_t)sceneIdx + 0];
                place.pos[1] = scenePos_[3 * (std::size_t)sceneIdx + 1];
                place.pos[2] = scenePos_[3 * (std::size_t)sceneIdx + 2];
                place.euler[0] = node->euler[0];
                place.euler[1] = node->euler[1];
                place.euler[2] = node->euler[2];
                placed = true;
            }
        }
        if (!placed) {
            ++unplaced_;
            continue;
        }

        // 2. Model (hook = the wave-2 gb_<typeName> loader binding point, else
        //    the owner node's own shipped mesh — the real on-disk model).
        std::string model;
        if (hooks_.resolveObjectModel)
            model = hooks_.resolveObjectModel(rec, node);
        else if (node && node->hasMesh)
            model = node->mesh;
        const std::string member = ResolveMember(model);

        BoundObject b;
        b.id = rec.id;
        b.slot = slot;
        b.sceneIndex = sceneIdx;
        b.place = place;
        b.member = member;
        bound_.push_back(b);

        if (member.empty()) {
            // NAMED GAP: no model for this object yet (the gb_<typeName> attach
            // VIBE_Object_BuildModelName @0x4ffe0c / the gebaeude group loader
            // @0x50d01c is the parallel agent's module; the hook binds it).
            ++modelUnresolved_;
            continue;
        }

        if (sceneIdx >= 0 && instanceOfNode_[(std::size_t)sceneIdx] >= 0) {
            // The owner node already draws — tag it as this object's instance
            // (the engine REPLACES the node's model in place; one node, one draw).
            Instance& inst = instances_[(std::size_t)instanceOfNode_[(std::size_t)sceneIdx]];
            inst.objectId = rec.id;
            inst.member = member;
        } else {
            Instance inst;
            inst.name = node ? node->name : ("object_" + std::to_string(rec.id));
            inst.member = member;
            inst.sceneIndex = sceneIdx;
            inst.objectId = rec.id;
            if (sceneIdx >= 0) {
                inst.l2w = sceneRot_[(std::size_t)sceneIdx];
            } else {
                inst.l2w = render::Transpose(render::MatrixFromEuler(place.euler));
            }
            inst.pos[0] = place.pos[0];
            inst.pos[1] = place.pos[1];
            inst.pos[2] = place.pos[2];
            if (sceneIdx >= 0)
                instanceOfNode_[(std::size_t)sceneIdx] = (int)instances_.size();
            instances_.push_back(std::move(inst));
        }
    }
    return (int)bound_.size();
}

// ===========================================================================
// PERSONS (wave 3) — the chain + named gaps are documented in the header.
// ===========================================================================

bool CityView3D::InitPersonAnims(shim::IFileSystem* fs, const char* archivePath) {
    animsMounted_ = false;
    if (!fs || !archivePath)
        return false;
    animsMounted_ = anims_.Mount(fs, archivePath, /*caseInsensitive=*/true);
    return animsMounted_;
}

const render::MeshGeometry* CityView3D::ResolvePersonMesh(const char* model,
                                                          std::string* memberOut) {
    if (memberOut)
        memberOut->clear();
    if (!model || !*model)
        return nullptr;
    // Mesh_LoadOrFindByName @0x5d345c resolves "*<name>.bgf"; the shipped archive
    // stores every character mesh as "_DYNAMIC/Character/<name>.bgf".
    const std::string wanted = CharacterMeshMemberName(model);
    std::string member = FindMemberCaseInsensitive(names_.members(), wanted);
    if (member.empty())
        member = ResolveMember(model);   // archive-wide basename index fallback
    if (member.empty())
        return nullptr;
    render::MeshGeometry* g = src_.Resolve(member.c_str());
    if (!g || g->vertexCount <= 0 || g->polyCount <= 0)
        return nullptr;
    if (memberOut)
        *memberOut = member;
    return g;
}

bool CityView3D::DefaultPersonPlacement(const sim::Person& p, CityPlacement& out,
                                        int* dummyIdxOut, i32* anchorOut) const {
    if (dummyIdxOut) *dummyIdxOut = -1;
    if (anchorOut)   *anchorOut = 0;

    // 1. Anchor building id: the hook, else the person record's own building
    //    columns (sim/npc_daily.h): +0x16C homeBld (dword_12CEA7C), else +0x170
    //    workBld (dword_12CEA80). 0 == no anchor (named gap: the live spawn's
    //    building comes from the 0x57c8f0 CALLERS) -> not placed.
    i32 anchor;
    if (hooks_.resolvePersonAnchor) {
        anchor = hooks_.resolvePersonAnchor(p);
    } else {
        anchor = sim::PersonGetDword(&p, 0x16C);       // homeBld
        if (anchor == 0)
            anchor = sim::PersonGetDword(&p, 0x170);   // workBld
    }
    if (anchor == 0)
        return false;

    // 2. The building's owner-matched scene node (the same node+512 == record id
    //    match BindWorldObjects uses — RebuildModelByOwner @0x5a8140).
    auto it = ownerToNode_.find((u32)anchor);
    if (it == ownerToNode_.end())
        return false;
    const int nodeIdx = it->second;

    // 3. Entrance dummy in the building subtree (0x57c8f0); absent -> the
    //    building node itself (the 0x4b0ee8 fallback).
    const int dummyIdx = FindEntranceDummyNode(scene_, nodeIdx);
    const int useIdx = (dummyIdx >= 0) ? dummyIdx : nodeIdx;
    out.pos[0] = scenePos_[3 * (std::size_t)useIdx + 0];
    out.pos[1] = scenePos_[3 * (std::size_t)useIdx + 1];
    out.pos[2] = scenePos_[3 * (std::size_t)useIdx + 2];
    // Default spawn rotation: dword_577A78 (all zero; SpawnOfficeStaffActor
    // pushes a rotation only when its caller supplies one).
    out.euler[0] = out.euler[1] = out.euler[2] = 0.0f;

    if (dummyIdxOut) *dummyIdxOut = dummyIdx;
    if (anchorOut)   *anchorOut = anchor;
    return true;
}

int CityView3D::BindPersons(int maxPersons) {
    using namespace guild::sim;
    UnbindPersons();
    personsUnplaced_ = 0;
    personModelUnresolved_ = 0;

    // Install the factory trampolines (the genuine scene/asset leaves of
    // CreateMesh @0x4029c4 routed into this view's archives); restored on return.
    PersonFactoryCtx ctx;
    ctx.view = this;
    CharacterFactoryHooks mine = GetCharacterFactoryHooks();
    mine.attachToUniverseNode = &CV3D_PersonAttach;     // 0x5b3e30 -> Objects.BIN
    mine.preloadAniSet        = &CV3D_PersonPreload;    // 0x403c34 capture
    mine.findSubstring        = &CV3D_PersonStrstr;     // loc_5CB930 strstr
    mine.destroy              = &CV3D_PersonDestroy;    // 0x402120 essentials
    CharacterFactoryHooks prev = SetCharacterFactoryHooks(&mine);
    PersonFactoryCtx* prevCtx = g_personFactoryCtx;
    g_personFactoryCtx = &ctx;

    int placed = 0;
    for (int slot = 0; slot < kPersonCapacity; ++slot) {
        // The record gate of VIBE_Person_IsValidActiveRecord @0x4f8e60
        // (marker != -1, live byte +8 != 0, kind byte +2 < 10).
        if (!PersonIsValidActiveRecord((u16)slot))
            continue;
        if (maxPersons > 0 && placed >= maxPersons)
            break;
        const Person& p = g_persons[slot];

        // 1. Placement (hook, else the anchored entrance-dummy default).
        CityPlacement place{};
        int dummyIdx = -1;
        i32 anchor = 0;
        bool isPlaced;
        if (hooks_.resolvePersonPlacement)
            isPlaced = hooks_.resolvePersonPlacement(p, place);
        else
            isPlaced = DefaultPersonPlacement(p, place, &dummyIdx, &anchor);
        if (!isPlaced) {
            ++personsUnplaced_;   // NAMED GAP: no evidenced position -> not drawn
            continue;
        }

        PersonInst pi;
        pi.info.id = p.id;
        pi.info.slot = slot;
        pi.info.anchorBuildingId = anchor;
        pi.info.dummySceneIndex = dummyIdx;
        pi.info.place = place;
        pi.l2w = render::Transpose(render::MatrixFromEuler(place.euler));

        // 2. Model: the REAL VIBE_Office_ResolveStaffModel @0x57c1e8 (hookable).
        const StaffModelRecord* rec = hooks_.resolvePersonModel
            ? hooks_.resolvePersonModel(p)
            : ResolveStaffModel(MakePersonModelView(&p));
        if (!rec || rec->name[0] == 0) {
            ++personModelUnresolved_;
            boundPersons_.push_back(pi.info);
            persons_.push_back(std::move(pi));
            ++placed;
            continue;
        }
        pi.info.model = rec->name;

        // 3. The REAL character factory: VIBE_Character_CreateFromModel @0x402d10
        //    (AllocSlot @0x402254 into the real g_live table, CreateMesh @0x4029c4
        //    field writes + name decomposition + the genuine preload set).
        ctx.member.clear();
        ctx.preloadClips.clear();
        ctx.nodeBuf = &pi.nodeBuf;
        pi.actor = CreateFromModel(rec->name);
        ctx.nodeBuf = nullptr;
        if (!pi.actor || ctx.member.empty()) {
            ++personModelUnresolved_;   // model not shipped -> placed, not drawn
            boundPersons_.push_back(pi.info);
            persons_.push_back(std::move(pi));
            ++placed;
            continue;
        }
        pi.info.member = ctx.member;
        // rec+304: the factory's base-name field (DecomposeModelName @0x402a4c).
        pi.info.base = reinterpret_cast<const char*>(pi.actor) + kRecBaseName;

        // 4. Pose chain over the factory's captured preload set, idle first
        //    ("stehen/stehen_newnoise" is the post-spawn playback; the gait
        //    "bewegung/gehen" is the fallback) — both genuinely preloaded by
        //    CreateMesh; nothing invented.
        render::MeshGeometry* rest = src_.Resolve(pi.info.member.c_str());
        pi.restMesh = rest;   // WAVE-8: kept so SetBoundPersonClip can re-LoadClip.
        if (rest && animsMounted_ && !pi.info.base.empty()) {
            std::vector<std::string> clips;
            for (const std::string& c : ctx.preloadClips)
                if (c.find("stehen") != std::string::npos)
                    clips.push_back(c);
            for (const std::string& c : ctx.preloadClips)
                if (c.find("stehen") == std::string::npos)
                    clips.push_back(c);
            for (const std::string& clip : clips) {
                if (LoadPersonClip(pi, clip.c_str()))
                    break;
            }
        }
        pi.info.posed = (pi.pose != nullptr && pi.pose->poseable());

        boundPersons_.push_back(pi.info);
        persons_.push_back(std::move(pi));
        ++placed;
    }

    g_personFactoryCtx = prevCtx;
    SetCharacterFactoryHooks(&prev);
    return placed;
}

void CityView3D::UnbindPersons() {
    for (PersonInst& pi : persons_) {
        if (pi.actor) {
            CV3D_PersonDestroy(pi.actor);   // clear the g_live slot + free
            pi.actor = nullptr;
        }
    }
    persons_.clear();
    boundPersons_.clear();
}

// Living-city wave-4 handoff (§4): cheap per-instance person move — the new
// placement replaces the bound seat without recreating the factory record or
// the pose chain (the original updates the character node's +76/+132 in place;
// SetPosition @0x5af38c / SetWorldTranslation @0x5af50c on the SAME node).
bool CityView3D::MoveBoundPerson(i32 id, const CityPlacement& place) {
    for (std::size_t i = 0; i < persons_.size(); ++i) {
        PersonInst& pi = persons_[i];
        if (pi.info.id != id)
            continue;
        pi.info.place = place;
        // Same composition as BindPersons: l2w = R^T of the euler (identity for
        // the zero spawn euler dword_577A78).
        pi.l2w = render::Transpose(render::MatrixFromEuler(place.euler));
        if (i < boundPersons_.size() && boundPersons_[i].id == id)
            boundPersons_[i].place = place;   // parallel introspection copy
        return true;
    }
    return false;
}

// WAVE-8 W8-NPCCLIP — load a named factory-preload clip into a person's pose over
// its rest mesh (the exact PreloadAniSet @0x403c34 sprintf member path
// "character/<base>/<clip>_<base>.baf" -> LoadClip + BindMesh). Records the clip
// name so SetBoundPersonClip can skip a redundant reload. The clip names come from
// the factory's PRELOAD set (gait / idle), so this never streams a new asset beyond
// what BindPersons already mounts.
bool CityView3D::LoadPersonClip(PersonInst& pi, const char* clipName) {
    if (!clipName || !*clipName || !pi.restMesh || !animsMounted_ ||
        pi.info.base.empty())
        return false;
    const std::string animMember =
        CharacterAnimMemberName(pi.info.base.c_str(), clipName);
    std::vector<u8> bytes;
    if (!anims_.OpenMember(animMember.c_str(), bytes) || bytes.empty())
        return false;
    auto pose = std::make_unique<PersonCharacterPose>();
    if (!pose->LoadClip(bytes.data(), bytes.size(), animMember.c_str()) ||
        !pose->BindMesh(pi.restMesh))
        return false;
    pi.pose = std::move(pose);
    pi.clipName = clipName;
    return true;
}

bool CityView3D::SetBoundPersonClip(i32 id, const char* clipName) {
    if (!clipName || !*clipName)
        return false;
    for (std::size_t i = 0; i < persons_.size(); ++i) {
        PersonInst& pi = persons_[i];
        if (pi.info.id != id)
            continue;
        if (pi.clipName == clipName)
            return true;   // already playing the requested clip (idempotent no-op)
        if (!LoadPersonClip(pi, clipName))
            return false;
        pi.info.posed = (pi.pose != nullptr && pi.pose->poseable());
        if (i < boundPersons_.size() && boundPersons_[i].id == id)
            boundPersons_[i].posed = pi.info.posed;
        return true;
    }
    return false;
}

const char* CityView3D::boundPersonClip(i32 id) const {
    for (const PersonInst& pi : persons_)
        if (pi.info.id == id)
            return pi.clipName.c_str();
    return "";
}

int CityView3D::AdvancePersonPoses(float stepTicks) {
    int advanced = 0;
    for (PersonInst& pi : persons_) {
        if (pi.pose && pi.pose->poseable()) {
            pi.pose->Advance(stepTicks);    // the REAL 0x5cd1d8 advance ladder
            ++advanced;
        }
    }
    return advanced;
}

void CityView3D::WorldBounds(float lo[3], float hi[3]) const {
    for (int k = 0; k < 3; ++k) { lo[k] = 1e30f; hi[k] = -1e30f; }
    for (const Instance& inst : instances_) {
        for (int k = 0; k < 3; ++k) {
            if (inst.pos[k] < lo[k]) lo[k] = inst.pos[k];
            if (inst.pos[k] > hi[k]) hi[k] = inst.pos[k];
        }
    }
    if (instances_.empty())
        for (int k = 0; k < 3; ++k) { lo[k] = 0.0f; hi[k] = 0.0f; }
}

CityCamera3D CityView3D::OverviewCamera(float elevationFactor, float backFactor) const {
    float lo[3], hi[3];
    WorldBounds(lo, hi);
    const float cx = 0.5f * (lo[0] + hi[0]);
    const float cy = 0.5f * (lo[1] + hi[1]);
    const float cz = 0.5f * (lo[2] + hi[2]);
    float ext = std::max(hi[0] - lo[0], hi[2] - lo[2]);
    if (ext < 1.0f) ext = 1.0f;
    float up = elevationFactor * ext, back = backFactor * ext;
    // Keep the eye comfortably inside the scene's far plane (the .ed3 fog far,
    // ConfigureFog @0x5ae384), or the city renders beyond the far clip.
    const float farLimit = 0.65f * ((hasFog_ && fogFar_ > 1.0f) ? fogFar_ : 20000.0f);
    const float dist = std::sqrt(up * up + back * back);
    if (dist > farLimit && dist > 0.0f) {
        const float k = farLimit / dist;
        up *= k;
        back *= k;
    }
    const float eye[3] = {cx, hi[1] + up, cz - back};
    const float target[3] = {cx, cy, cz};
    CityCamera3D cam;
    AimCamera(cam, eye, target);
    return cam;
}

// ---------------------------------------------------------------------------
// Per-member material bind (the same Textures.BIN -> palettized texel record +
// 565 LUT build the universe driver runs; cached per .bgf member).
// ---------------------------------------------------------------------------
const CityView3D::MatBind* CityView3D::bindFor(const std::string& member,
                                               bool rgbAffineFallback) {
    auto it = matBinds_.find(member);
    if (it != matBinds_.end())
        return &it->second;
    if (!tex_.mounted())
        return nullptr;
    if (!src_.Resolve(member.c_str()))
        return nullptr;
    const render::BgfModel* model = src_.ModelFor(member.c_str());
    if (!model)
        return nullptr;
    const MaterialTextureTable* tbl = tex_.BuildTableFor(member.c_str(), *model);
    MatBind& mb = matBinds_[member];           // inserted (empty == untextured)
    if (!tbl)
        return &mb;
    const int nm = (int)tbl->matToTex.size();
    mb.tex.resize((std::size_t)nm);            // resized ONCE -> stable pointers
    mb.pal.resize((std::size_t)nm);
    mb.bound.assign((std::size_t)nm, BoundTex{});
    for (int mi = 0; mi < nm; ++mi) {
        const int texId = tbl->matToTex[(std::size_t)mi];
        const render::DecodedBmp* bmp = tbl->TextureFor(texId);
        if (!bmp || !bmp->ok || bmp->width <= 0 || bmp->indices.empty()) {
            // 24-bit (non-palettized) BMP: the PERSON pass binds it as an RGB
            // affine bind; the city pass (rgbAffineFallback false) leaves it
            // untextured exactly as before (byte-identical city frames).
            if (rgbAffineFallback && bmp && bmp->ok && bmp->width > 0 &&
                !bmp->rgba.empty()) {
                mb.bound[(std::size_t)mi].rgb = bmp;
                ++mb.boundCount;
            }
            continue;
        }
        render::Texture& T = mb.tex[(std::size_t)mi];
        render::TextureSetSize(T, bmp->width);
        // wave-5 W5-CKEY — PRODUCER. Record +104 bit 2 (kTexFlagColourKey) is set
        // on a >8bpp (24-bit) source with the gate off
        // (VIBE_Texture_LoadByName @0x5dad52: `!dword_140809C && bpp > 8`;
        // dword_140809C defaults 0). 8-bit sources never set it. bit 3 (0x08) is
        // the "_NM" mip flag, NOT the colour key.
        if (bmp->bpp > 8)
            T.flags |= render::kTexFlagColourKey;
        // Material BLEND bit (+194 & 1, mesh_load's mat.blendBit -> the flag0
        // BYTE2 |= 2 alpha path): foliage/fence materials mark transparency at
        // the MATERIAL, not the source bpp. Those texels key on black exactly
        // like the 24-bit DDBLT_KEYSRC path, so route them through the masked
        // span (fixes trees/bushes rendering on opaque black quads).
        if (mi < (int)model->materials.size() && (model->materials[(std::size_t)mi].b2 & 1))
            T.flags |= render::kTexFlagColourKey;
        if (std::getenv("GUILD_DEBUG_BIND") && (member.find("LAUBKRONE") != std::string::npos || member.find("TANNE_KRONE") != std::string::npos))
            std::printf("[bind] %s mat%d tex=%d bpp=%d b2=%02x flags=%02x\n",
                        member.c_str(), mi, texId, bmp->bpp,
                        mi < (int)model->materials.size()
                            ? model->materials[(std::size_t)mi].b2 : 0xEE,
                        (unsigned)T.flags);
        const std::size_t cnt = std::min(T.texels.size(), bmp->indices.size());
        std::copy(bmp->indices.begin(), bmp->indices.begin() + cnt, T.texels.begin());
        std::vector<u16>& pal = mb.pal[(std::size_t)mi];
        // The FULL 256-row light palette the textured span indexes with
        // palBase[lightRow8 | texel] (raster_textured: lightRow8 = avg vertex
        // +66 << 8). Row L is the source palette scaled by L/255 — exactly the
        // render::BuildShadeRamp table (VIBE_Light shade ramps), packed 565.
        // Row 0 = black, row 255 = full colour; the day/night ambient byte the
        // frame writes into each vertex's lightIdx picks the row.
        pal.assign(256 * 256, 0);
        if (bmp->palette.size() >= 768) {
            for (int L = 0; L < 256; ++L) {
                u16* row = pal.data() + (std::size_t)L * 256;
                for (int i2 = 0; i2 < 256; ++i2) {
                    const u8 sr = bmp->palette[3 * i2 + 0];
                    const u8 sg = bmp->palette[3 * i2 + 1];
                    const u8 sb = bmp->palette[3 * i2 + 2];
                    const u8 r = (u8)((sr * L) / 255);
                    const u8 g = (u8)((sg * L) / 255);
                    const u8 b = (u8)((sb * L) / 255);
                    u16 packed = (u16)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
                    // Colour-key safety: only SOURCE-black texels may resolve to
                    // the 565 key value 0 (DDBLT_KEYSRC keys on the SOURCE colour,
                    // pre-lighting). A non-black entry darkened to 0 by the ramp
                    // clamps to the darkest visible 565 so it never keys out.
                    if (packed == 0 && (sr | sg | sb) != 0)
                        packed = 0x0841;   // (1,2,1) in 565 steps — near-black
                    row[i2] = packed;
                }
            }
        }
        mb.bound[(std::size_t)mi] = BoundTex{&mb.tex[(std::size_t)mi], pal.data()};
        // Material +194 bit 1: the alpha/blend route (foliage translucency).
        if (mi < (int)model->materials.size() && (model->materials[(std::size_t)mi].b2 & 2))
            mb.bound[(std::size_t)mi].blend = true;
        ++mb.boundCount;
    }
    return &mb;
}

const CityView3D::BoundTex* CityView3D::boundTexForKey(i32 key) const {
    if (key < 0)
        return nullptr;
    const int instIdx = key >> 12;
    const int matIdx = key & 0xFFF;
    if (instIdx < 0 || (std::size_t)instIdx >= frameBinds_.size())
        return nullptr;
    const MatBind* mb = frameBinds_[(std::size_t)instIdx];
    if (!mb || matIdx >= (int)mb->bound.size())
        return nullptr;
    return &mb->bound[(std::size_t)matIdx];
}

// ---------------------------------------------------------------------------
// Frame hooks.
// ---------------------------------------------------------------------------
void CityView3D::doClear() {
    if (!fb_)
        return;
    render::SurfaceColorFill(fb_, opt_.clearR, opt_.clearG, opt_.clearB);
    // WAVE-6 W6-INTEGRATE — the SKY backdrop. BeginUniverseFrame @0x5b3900 clears
    // the viewport to the time-of-day sky colour dword_649DD4 (the engine has no
    // sky geometry; the backdrop IS the clear colour) BEFORE terrain @0x5b3a2f.
    // RenderSky fills the surface with that colour after the base clear. Gated on
    // Options::sky (default OFF == byte-identical to the flat clear). The sun state
    // is computed once per frame in RenderFrame (computeSunForFrame).
    if (opt_.sky && w6_.skyDrawn) {
        // Convert the device-packed 0x00RRGGBB sky colour to the surface format and
        // fill (RenderSky takes a native-format value; the surface is 16bpp 565).
        const u32 c = w6_.skyColor;
        const u8 r = (u8)((c >> 16) & 0xFF), g = (u8)((c >> 8) & 0xFF), b = (u8)(c & 0xFF);
        render::SurfaceColorFill(fb_, r, g, b);
    }
}

// WAVE-6 W6-INTEGRATE — the per-frame SUN/day-cycle head (frame step #1). Runs
// render::ComputeSunState(day,hour,minute) @ (0x58339c -> 0x4b2438 -> 0x4b253b ->
// band) over the session world clock; feeds the sky colour, the sun direction the
// dynamic-light pass consumes, and the brightness the shadow/atmos passes read.
// Side-effect-free (no global mutation); safe to call every frame.
void CityView3D::computeSunForFrame() {
    w6_.skyDrawn = false;
    w6_.skyColor = 0;
    w6_.sunBand = -1;
    w6_.sunBrightness = 0;
    w6_.sunDir[0] = 0.0f; w6_.sunDir[1] = 0.0f; w6_.sunDir[2] = 1.0f;
    if (!(opt_.sky || opt_.dynamicLight || opt_.shadows))
        return;

    const render::SunState sun =
        render::ComputeSunState(opt_.worldDay, opt_.worldHour, opt_.worldMinute);
    w6_.sunBand = sun.band;
    w6_.sunBrightness = sun.brightness;
    w6_.sunBlend = sun.blend;
    // Night gate for the lantern lights: the day cycle's brightness (0..600)
    // is the DAY-PROGRESS counter (h=8 -> 75 .. h=22 -> 600; 0 before dawn), so
    // band = time-of-day band. Bands 0 (pre-dawn) and 5/6 (dusk/late night) are
    // the dark blue-ambient bands — the lamplit hours. (regime.raise is only
    // the sun rise/set arm, NOT a night flag.)
    w6_.nightLights = (sun.band == 0 || sun.band >= 5);

    // The frame's GLOBAL AMBIENT TRIPLE (flt_64A074/78/7C): the day-cycle rebuilds
    // it per band (VIBE_DayCycle_UpdateBrightness). With the scene's 7-band light
    // rig loaded, the genuine BlendBandLighting @0x5b85e4 cross-fade over the
    // band ambient colours; else the wave-6 brightness-ramp fallback over the
    // captured 200-seed. Only with dynamicLight — off keeps the constant 200s.
    if (opt_.dynamicLight && sun.band >= 0) {
        bool applied = false;
        if (hasSkyBands_) {
            render::SkyBandColor bands[render::kSkyBands];
            for (int b = 0; b < render::kSkyBands; ++b) {
                bands[b].r = skyBands_.ambient[b][0];
                bands[b].g = skyBands_.ambient[b][1];
                bands[b].b = skyBands_.ambient[b][2];
            }
            const render::SkyAmbient amb =
                render::BlendBandLighting(bands, sun.band, sun.blend, /*scale=*/1.0f);
            if (amb.r > 0.0f || amb.g > 0.0f || amb.b > 0.0f) {
                w6_.ambient[0] = amb.r;
                w6_.ambient[1] = amb.g;
                w6_.ambient[2] = amb.b;
                applied = true;
            }
        }
        if (!applied) {
            float k = (float)sun.brightness / 600.0f;
            if (k < 0.15f) k = 0.15f;   // floor (the night band ambient)
            if (k > 1.0f)  k = 1.0f;
            w6_.ambient[0] = w6_.ambient[1] = w6_.ambient[2] = 200.0f * k;
        }
    }

    // The sun DIRECTION the dynamic-light/shadow passes read. The day-cycle picks
    // the elevation REGIME (SunRegimeForBand @0x4b28f0: bands 0..2 -> day/positive,
    // 3..6 -> night/negative); the live per-light pitch is RNG-sampled by
    // SetSunHeight @0x4b24b0 (non-deterministic by design). For a deterministic
    // frame the direction takes the regime envelope midpoint as the elevation, with
    // the band fraction rotating azimuth across the day (a faithful deterministic
    // stand-in for the RNG pitch; documented in progress/frame-integration-wave6.md).
    const float elev = 0.5f * (sun.regime.elevLo + sun.regime.elevHi);
    const float az = sun.blend * 6.2831853f;   // band fraction -> azimuth sweep
    w6_.sunDir[0] = std::cos(az);
    w6_.sunDir[1] = elev;
    w6_.sunDir[2] = std::sin(az);

    // The SKY backdrop colour.
    // WAVE-7 W7-SKYBANDS — with the per-scene band table loaded (skyBands_, the
    // RUNTIME data VIBE_Scene_LoadFromStream @0x5e7e38 reads), the colour is the
    // REAL ComputeSkyFog(bands, band, blend, ...) chain (BuildFogScratch @0x5b85e4
    // -> BlendAmbientFog @0x5b8b04 -> the framebuffer clear) — exactly what the
    // engine puts in dword_649DD4. band/blend come from ComputeSunState; fogA=fogB=0
    // frac=0 selects keyframe-0 (the daytime fog/sky colour, sky-bands-wave7.md).
    // Without a band table (no scene header / synthetic scene) the wave-6 fallback
    // (the clear colour scaled by the 0..600 brightness ramp) is taken —
    // byte-identical to before.
    if (opt_.sky) {
        bool applied = false;
        if (hasSkyBands_ && sun.band >= 0) {
            guild::render::SkyFog sky = guild::render::ComputeSkyFog(
                skyBands_, (unsigned)sun.band, sun.blend,
                /*fogA=*/0, /*fogB=*/0, /*frac=*/0.0f);
            if (sky.applied) {
                w6_.skyColor = sky.color;   // device-packed 0x00RRGGBB clear colour
                w6_.skyDrawn = true;
                applied = true;
            }
        }
        if (!applied) {
            // Wave-6 fallback: the clear colour scaled by the real 0..600 brightness
            // ramp (dark at night, bright by day) — the same time-of-day darkening
            // the band blend produces.
            float k = (float)sun.brightness / 600.0f;
            if (k < 0.0f) k = 0.0f;
            if (k > 1.0f) k = 1.0f;
            const u8 r = (u8)((float)opt_.clearR * k + 0.5f);
            const u8 g = (u8)((float)opt_.clearG * k + 0.5f);
            const u8 b = (u8)((float)opt_.clearB * k + 0.5f);
            w6_.skyColor = ((u32)r << 16) | ((u32)g << 8) | (u32)b;
            w6_.skyDrawn = true;
        }
    }
}

// =============================================================================
// WAVE-8 W8-NORMALS — the STATIC per-vertex object-space normal cache. At first
// use of a given .bgf member, generate the rest-pose vertex normals exactly as
// VIBE_Mesh_ComputeVertexNormals @0x5D1A6C does (mesh_normals::GenerateVertexNormals:
// per-poly face normal -> unweighted per-vertex average -> normalize), then cache
// the flattened 3-floats-per-vertex array (FlattenInstanceNormals equivalent —
// kBindToVertexBase resolves to the +12 normal). Every instance of that mesh
// reuses it (the engine generates once at .BGF load; we cache per member). The
// module BODY is called, not edited (rule 13).
// =============================================================================
const std::vector<float>*
CityView3D::meshNormalsFor(const std::string& member,
                           const render::MeshGeometry* geom) {
    auto it = meshNormalCache_.find(member);
    if (it != meshNormalCache_.end())
        return &it->second;
    if (!geom || geom->vertexCount <= 0 || geom->polyCount <= 0)
        return nullptr;
    const int vc = geom->vertexCount;
    const int pc = geom->polyCount;
    // Build the SourceMeshVertex array (pos @+0; normal generated) + the index
    // triples from the poly vertex pointers (the engine's poly +24/+28/+32).
    std::vector<render::SourceMeshVertex> sv((std::size_t)vc);
    for (int i = 0; i < vc; ++i) {
        sv[(std::size_t)i].pos[0] = geom->vertices[i].x;
        sv[(std::size_t)i].pos[1] = geom->vertices[i].y;
        sv[(std::size_t)i].pos[2] = geom->vertices[i].z;
        sv[(std::size_t)i].normal[0] = 0.0f;
        sv[(std::size_t)i].normal[1] = 0.0f;
        sv[(std::size_t)i].normal[2] = 0.0f;
    }
    std::vector<render::NormalTriangle> tris;
    tris.reserve((std::size_t)pc);
    for (int k = 0; k < pc; ++k) {
        const render::Polygon& p = geom->polygons[k];
        if (!p.v0 || !p.v1 || !p.v2)
            continue;
        const long i0 = p.v0 - geom->vertices;
        const long i1 = p.v1 - geom->vertices;
        const long i2 = p.v2 - geom->vertices;
        if (i0 < 0 || i1 < 0 || i2 < 0 || i0 >= vc || i1 >= vc || i2 >= vc)
            continue;
        render::NormalTriangle t;
        t.vtx[0] = (u32)i0; t.vtx[1] = (u32)i1; t.vtx[2] = (u32)i2;
        tris.push_back(t);
    }
    render::GenerateVertexNormals(sv.data(), vc, tris.data(), (int)tris.size(),
                                  /*faceNormalsOut=*/nullptr);
    std::vector<float> flat((std::size_t)(3 * vc));
    for (int i = 0; i < vc; ++i) {
        flat[(std::size_t)(3 * i + 0)] = sv[(std::size_t)i].normal[0];
        flat[(std::size_t)(3 * i + 1)] = sv[(std::size_t)i].normal[1];
        flat[(std::size_t)(3 * i + 2)] = sv[(std::size_t)i].normal[2];
    }
    auto res = meshNormalCache_.emplace(member, std::move(flat));
    return &res.first->second;
}

// =============================================================================
// WAVE-8 W8-SCENELIGHTS — collect the scene's lights ONCE per frame into a
// render::SceneLightSet (the scene-lights-wave8.md handoff step 1): for each
// parsed light node (SceneObjectInst::hasLight) push a SceneLightNode with the
// engine field mapping (+533 type, +472 world pos, +92 colour, +132 dir, +144
// range, +148 intensity, +152 rangeParam). Then add the day-cycle directional SUN
// (the ComputeSunState sun this frame): the city .ed3 typically ships no type-7
// sun node, so the live day/night sun is appended as the directional light, with a
// time-of-day colour from the brightness ramp. The per-object cull (CullForObject)
// runs in doSceneWalk. The module bodies are CALLED, not edited.
// =============================================================================
void CityView3D::computeSceneLights() {
    sceneLights_.lights.clear();
    sunColor_[0] = sunColor_[1] = sunColor_[2] = 1.0f;
    if (!opt_.sceneLights)
        return;

    // (a) the parsed scene light nodes (the engine's scene-graph light list).
    for (std::size_t i = 0; i < scene_.size(); ++i) {
        const SceneObjectInst& in = scene_[i];
        if (!in.hasLight)
            continue;
        render::SceneLightNode n;
        n.type = in.type;                       // +533 (7 == sun)
        // world position: the composed node position (scenePos_ 3*i), == +472.
        if (3 * i + 2 < scenePos_.size()) {
            n.pos[0] = scenePos_[3 * i + 0];
            n.pos[1] = scenePos_[3 * i + 1];
            n.pos[2] = scenePos_[3 * i + 2];
        } else {
            n.pos[0] = in.pos[0]; n.pos[1] = in.pos[1]; n.pos[2] = in.pos[2];
        }
        n.color[0] = in.lightColor[0];          // +92
        n.color[1] = in.lightColor[1];
        n.color[2] = in.lightColor[2];
        n.dir[0] = in.lightDir[0];              // +132
        n.dir[1] = in.lightDir[1];
        n.dir[2] = in.lightDir[2];
        n.range      = in.lightParam[0];        // +144
        n.intensity  = in.lightParam[1];        // +148
        n.rangeParam = in.lightParam[2];        // +152
        n.flags = 0;                            // +529 not parsed; 0 (sun enabled)
        // NIGHT LANTERNS: the shipped city scenes store the warm rLICHT_*
        // point lights (parsed type 6, colour ~(255,185,0), range ~220) with
        // FILE intensity 0 — the ENGINE lights them at dusk at runtime (the
        // dusk enable/flicker writer is a named gap). FRIDA-CAPTURED live
        // values (gilde.exe in-city at night, light kernel @0x5c6f90 walk):
        // runtime type byte 5, colour (255,180..194,0), intensity 336..380
        // (torch flicker modulates intensity/range/rangeParam per frame).
        // Under the engine's own night flag (SunRegimeForBand raise==1) seed
        // the captured MEAN intensity; the flicker animation itself is the
        // remaining named gap.
        if (w6_.nightLights && n.type == 6 && n.intensity == 0.0f &&
            n.range > 0.0f && n.rangeParam > 0.0f) {
            // TORCH FLICKER: the live capture showed the dusk system modulating
            // each lantern's intensity per frame around ~358 (samples 336..380).
            // Deterministic per-light per-frame wobble in the captured envelope
            // (a hashed triangle wave — the engine's flicker RNG writer is the
            // remaining named gap; the ENVELOPE is the measured ground truth).
            const u32 h = (u32)i * 2654435761u + (u32)flickerTick_ * 40503u;
            const u32 tri = (h >> 8) & 0x3F;                 // 0..63
            const float wob = ((tri < 32 ? tri : 63 - tri) / 31.0f) * 2.0f - 1.0f;
            n.intensity = 358.0f + wob * 22.0f;              // 336..380
        }
        sceneLights_.lights.push_back(n);
    }

    // (b) the live day-cycle SUN (computeSunForFrame populated w6_.sunDir + band).
    // Append it as the directional (type-7) light so CullForObject reports it as
    // the affected sun feeding LightMeshVertices' NdotL arm. Colour = the
    // brightness-ramp time-of-day tint (warm by day, dim at night), intensity from
    // the 0..600 brightness. The engine's own scene sun (when one is parsed above)
    // still wins in walk order (FirstSunWins); this is the day/night fallback the
    // shipped city scenes (no type-7 node) need to be lit at all.
    if (w6_.sunBand >= 0 && w6_.sunDir[1] > 0.0f) {
        float k = (float)w6_.sunBrightness / 600.0f;
        if (k < 0.0f) k = 0.0f;
        if (k > 1.0f) k = 1.0f;
        sunColor_[0] = 1.0f; sunColor_[1] = 0.96f; sunColor_[2] = 0.88f;  // warm white
        render::SceneLightNode sun;
        sun.type = render::kSunLightType;       // 7 == directional
        sun.dir[0] = -w6_.sunDir[0];            // light DIR points FROM sun toward scene
        sun.dir[1] = -w6_.sunDir[1];
        sun.dir[2] = -w6_.sunDir[2];
        sun.color[0] = sunColor_[0] * 255.0f;   // engine light colour is 0..255 RGB
        sun.color[1] = sunColor_[1] * 255.0f;
        sun.color[2] = sunColor_[2] * 255.0f;
        sun.intensity = k;                      // 0..1 (non-zero == affects, the cull gate)
        sun.flags = 0;
        sceneLights_.lights.push_back(sun);
    }
}

// The per-frame view parameters, shared by the ground pass and the scene walk
// (pure factor of the former doSceneWalk prologue; behaviour unchanged).
void CityView3D::buildViewParams(render::Frustum& fr, render::ObjectProjectScalars& s) {
    const int vpW = (opt_.viewportW > 0) ? opt_.viewportW : opt_.fbW;
    const float halfW = (float)vpW * 0.5f;
    const float halfH = (float)opt_.fbH * 0.5f;
    const float scale = (opt_.viewScale > 0.0f) ? opt_.viewScale : halfW;
    const float farZ = (opt_.farZ > 0.0f)
                           ? opt_.farZ
                           : ((hasFog_ && fogFar_ > opt_.nearZ) ? fogFar_ : 20000.0f);

    // The engine frustum (BuildViewMatrix @0x5accd0) + the SetupViewTransform
    // @0x5af5f8 projection scalars: flt_13FCD0C = scale, flt_13FCD18 = W*0.5,
    // flt_13FCAF8 = scale * flt_6280E4 (== -1.0), flt_13FCD10 = H*0.5 — all of
    // the VIEWPORT width (694 in-city), while the SURFACE stays fbW wide.
    BuildEngineFrustum((float)vpW, (float)opt_.fbH, scale, opt_.nearZ, farZ, fr);
    s.xScale = scale;  s.xOffset = halfW;
    s.yScale = -scale; s.yOffset = halfH;

    // The flush-side clip plane set + reproject scalars (the 64-row plane table
    // BuildViewMatrix @0x5accd0 builds at dword_13DB398: side planes 0..3, the
    // near plane {0,0,1,near} (13DCDE0 block) and the far plane {0,0,-1,-far}
    // (13DCDF0 block, flt_13DCDF8 = -1.0, flt_13DCDFC = -far)). Clipping against
    // the full six-plane set is geometry-identical to the per-outcode row (a
    // pass against a non-straddled plane emits the polygon unchanged).
    for (int pi = 0; pi < 4; ++pi)
        for (int k = 0; k < 4; ++k)
            clipPlanes_[pi][k] = fr.plane[pi][k];
    clipPlanes_[4][0] = 0; clipPlanes_[4][1] = 0; clipPlanes_[4][2] = 1.0f;  clipPlanes_[4][3] = opt_.nearZ;
    clipPlanes_[5][0] = 0; clipPlanes_[5][1] = 0; clipPlanes_[5][2] = -1.0f; clipPlanes_[5][3] = -farZ;
    projScalars_[0] = s.xScale;  projScalars_[1] = s.xOffset;
    projScalars_[2] = s.yScale;  projScalars_[3] = s.yOffset;
}

// The renderTerrain frame-hook body — the BeginUniverseFrame @0x5B3900 0x5b3a2f
// arm: draw the REAL parsed city floor (the 0x5bf22c walk + the 0x5AEC88 flush)
// into the frame surface AFTER the clear, BEFORE the object scene walk. Inert
// unless Options::terrain is set and the loaded scene carried a floor block.
void CityView3D::doRenderTerrain(char a2) {
    groundDrawn_ = false;
    groundStats_ = GroundRenderStats{};
    if (!opt_.terrain || !fb_ || !ground_.valid())
        return;
    if (!groundFrame_.bound() || groundFrame_.ground() != &ground_) {
        if (!groundFrame_.Bind(&ground_))
            return;
        groundTexBound_ = false;       // re-bind the resolver to the (re)bound ground
        waterBuilt_ = false;           // (re)build the water regions for the new floor
    }
    // wave-5 W5-TX: bind+install the floor slot textures (idempotent). Additive:
    // when Textures.BIN is unmounted / a slot has no BMP the resolver returns null
    // and the white-default level-shaded ground draws (byte-identical).
    BindGroundTextures();

    // WATER PIPELINE (wave-6 W6-WR): build the animated water regions once for the
    // bound floor (VIBE_FloorWater_PrepareRegions @0x5ba95c), then animate them per
    // frame (VIBE_Floor_AnimateWaterVertices @0x5be428) when the frame's water-anim
    // gate (a2) is set — exactly the engine's RenderTerrain Phase-2 arrangement. The
    // render arm (RenderWaterSurface / @0x5be668) runs inside groundFrame_.Render().
    // A floor with no water builds 0 regions and the frame is byte-identical.
    if (opt_.water && !waterBuilt_) {
        // WAVE-7 W7-WATERTEX: supply the EF_WASS loader (handoff (a)) when textures
        // are mounted so BuildWater binds the blue animated water record into every
        // WaterMesh+0/+4 + GroundFrame::waterTexture_ (the textured water span then
        // samples EF_WASS). Without Textures.BIN the loader returns null -> the
        // white-default branch (byte-identical to wave-6).
        const bool wantWaterTex = opt_.textured && tex_.mounted();
        groundFrame_.BuildWater(
            wantWaterTex ? &CityView3D::CV3D_LoadWaterTexture : nullptr,
            wantWaterTex ? this : nullptr);
        waterBuilt_ = true;
    }
    if (opt_.water && groundFrame_.hasWater() && a2 != 0) {
        // The engine clock is dword_62EB38; here a monotonic per-frame tick so the
        // wave grid advances deterministically (the dt>0 gate animates each call).
        waterTime_ += 33;   // ~30 fps step (dt>0 gates the wave/accum work)
        groundFrame_.AnimateWater(waterTime_, /*findGroupMember*/ nullptr, nullptr);
    }

    render::Frustum fr{};
    render::ObjectProjectScalars s;
    buildViewParams(fr, s);

    GroundViewParams vp;
    for (int k = 0; k < 3; ++k) {
        vp.eye[k] = cam_.eye[k];
        vp.rot[k] = cam_.rot[k];
    }
    vp.frustum        = &fr;
    vp.clipPlanes     = clipPlanes_;
    vp.clipPlaneCount = 6;
    vp.proj           = s;
    // WAVE-7 W7-FOGPIX: hand the terrain/water spans this frame's vertex-fog state
    // (configured in RenderFrame when Options::fog). Null/!enabled -> no per-vertex fog.
    vp.fog            = fogState_.enabled ? &fogState_ : nullptr;
    // The frame's global ambient triple (flt_64A074/78/7C — computeSunForFrame's
    // band blend / brightness ramp): the ground darkens with the day cycle exactly
    // like the objects. dynamicLight off keeps the captured 200s (byte-identical).
    if (opt_.dynamicLight && w6_.sunBand >= 0) {
        vp.sunAmbient[0] = w6_.ambient[0];
        vp.sunAmbient[1] = w6_.ambient[1];
        vp.sunAmbient[2] = w6_.ambient[2];
        // The Floor+208 SUN-COLOUR scale swaps with the day cycle — both
        // endpoints LIVE-READ from gilde.exe (flt_13FD510/4/8, frida):
        //   day   (12:00): (1.000, 0.7086, 0.2969)  — the warm sun
        //   night ( 4:00): (0.522, 0.439,  1.000)   — the blue moon light
        // Interpolated on the daylight ramp (brightness 75..225, the band
        // 0 -> 1 crossover the ambient blend uses).
        float k = ((float)w6_.sunBrightness - 75.0f) / 150.0f;
        if (k < 0.0f) k = 0.0f;
        if (k > 1.0f) k = 1.0f;
        static const float kDayScale[3]   = {1.0f, 0.708627462f, 0.296862751f};
        static const float kNightScale[3] = {0.522f, 0.439f, 1.0f};
        for (int c = 0; c < 3; ++c)
            vp.sunScale[c] =
                kNightScale[c] + (kDayScale[c] - kNightScale[c]) * k;

    }
    // SEASONS (GetSeasonFromDay @0x58339c: day % 4 -> 0 spring, 1 summer,
    // 2 autumn, 3 winter; the New Game banner "Весна" == day 0 == spring).
    // A season change re-dresses the whole city:
    //   * floor slot textures (_fruehling / base / _herbst / _snow + _high),
    //   * foliage TXS texture sets (rows F/S/H/W — the crowns turn),
    //   * the baked transition tiles (rebaked from the new slot textures),
    //   * material binds (rebuilt against the new TXS-selected textures).
    // (The snow weather overlay is already winter-gated: SnowIsWinterDay.)
    {
        const int season = ((opt_.worldDay % 4) + 4) % 4;
        if (season != season_) {
            season_ = season;
            groundTexResolver_.SetSeason(season);
            tex_.SetActiveTextureSet(season);
            matBinds_.clear();
            GroundFrame::InvalidateTransitionBakes();
        }
    }

    groundStats_ = groundFrame_.Render(fb_, vp, a2);
    groundDrawn_ = groundStats_.rasterTris > 0;
}

// WAVE-6 W6-INTEGRATE — resetLights hook (frame step start): the per-frame shadow
// light-list rebuild VIBE_Shadow_ResetLightList @0x5f4428 BeginUniverseFrame runs.
// Inert here (no global shadow universe set); the gate is honoured by doShadows.
void CityView3D::doResetLights() {
    w6_.shadowCasters = 0;
    w6_.shadowPixels = 0;
}

// WAVE-6 W6-INTEGRATE — the per-object drop SHADOW pass. In the original frame
// VIBE_Render_ProcessSceneNode @0x5add1c calls VIBE_Shadow_UpdateNodeShadows
// @0x5f4494 per object: AFTER terrain, UNDER objects — the shadow lies on the
// ground and the object draws on top (shadow-render-wave6.md). Here it runs at the
// end of the terrain arm (after the ground is drawn, before the object flush):
// for each drawable instance, project its silhouette straight down onto the ground
// plane and splat it (RenderObjectShadow @0x5f3f38 tail) as darkened pixels.
// Gate: Options::shadows (the engine's dword_1408A60 enable) AND a sun above the
// horizon (regime day branch) — at night there is no sun shadow.
void CityView3D::doShadows() {
    if (!opt_.shadows || !fb_ || instances_.empty())
        return;
    if (w6_.sunBand < 0 || w6_.sunDir[1] <= 0.0f)   // sun below horizon -> no shadow
        return;

    // A shared 8bpp stencil surface for RenderObjectShadow's splat (the original
    // splats into the object's shadow texture, then copies the darkened quad onto
    // the ground; here the stencil marks shadowed pixels which we then darken in fb_).
    const int W = fb_->width, H = fb_->height;
    // The shadow rasterizer (RasterizeTriangle @0x603ed4) clips Y against the
    // surface WIDTH (the engine's shadow surface is square), and FillSpans does no
    // per-pixel bound check — so the stencil must have at least `width` rows. Size
    // it W*W (square) and clamp the splat coords into [0,W-1]x[0,H-1].
    const int stencilRows = (W > H) ? W : H;
    // +2 guard rows + a guard column's worth so the no-clip FillSpans (which can
    // ceil a right edge to exactly `width` and walk one row past `count`) never
    // writes past the buffer end.
    const std::size_t need = (std::size_t)W * (std::size_t)(stencilRows + 2) + (std::size_t)W;
    if (shadowStencil_.size() != need)
        shadowStencil_.assign(need, 0);
    else
        std::fill(shadowStencil_.begin(), shadowStencil_.end(), (u8)0);

    render::ShadowSurface ss{};
    ss.pixels = shadowStencil_.data();
    ss.pitch  = W;          // 8bpp: one byte per pixel
    ss.width  = W;
    ss.height = stencilRows;
    ss.is16bpp = false;

    render::Frustum fr{};
    render::ObjectProjectScalars s;
    buildViewParams(fr, s);

    // Project each instance silhouette to the ground plane and splat. The ground
    // flatten direction is straight down the sun; with the deterministic sun the
    // shadow is offset by the sun azimuth. We reuse the already-projected screen
    // verts from this frame's draw-list flush is not available yet, so we project
    // the instance directly here (the same ComposeModelView -> project as the walk).
    std::vector<render::ShadowMeshTri> tris;
    int cap = (int)instances_.size();
    if (opt_.maxInstances > 0 && opt_.maxInstances < cap)
        cap = opt_.maxInstances;
    for (int idx = 0; idx < cap; ++idx) {
        const Instance& inst = instances_[(std::size_t)idx];
        render::MeshGeometry* geom = src_.Resolve(inst.member.c_str());
        if (!geom || geom->vertexCount <= 0 || geom->polyCount <= 0)
            continue;
        const int vc = geom->vertexCount, pc = geom->polyCount;
        std::vector<render::Vertex> vv(geom->vertices, geom->vertices + vc);
        // (1) model -> WORLD (a compose with a null camera = the pure model
        //     transform), (2) FLATTEN each vertex onto the object's ground
        //     plane through the sun direction (the engine's drop-shadow
        //     projection: MapShadowVertexToSurface @0x5f3bb6 projects the
        //     silhouette along the sun onto the ground), (3) world -> view
        //     with the identity model part, then clip + project as usual.
        {
            static const CityCamera3D kNullCam{};
            float mw[16];
            ComposeModelViewMatrix(inst.l2w, inst.pos, kNullCam, mw);
            render::TransformMeshVerticesByMatrix(vv.data(), vc, mw);
        }
        const float gy = inst.pos[1];
        const float sunX = w6_.sunDir[0] / w6_.sunDir[1];
        const float sunZ = w6_.sunDir[2] / w6_.sunDir[1];
        for (int k = 0; k < vc; ++k) {
            render::Vertex& v2 = vv[(std::size_t)k];
            const float h = v2.y - gy;
            if (h > 0.0f) {
                v2.x += sunX * h;
                v2.z += sunZ * h;
            }
            v2.y = gy;
        }
        float mv[16];
        {
            render::Mat3 ident{};
            ident.m[0] = ident.m[4] = ident.m[8] = 1.0f;
            const float zero3[3] = {0.0f, 0.0f, 0.0f};
            ComposeModelViewMatrix(ident, zero3, cam_, mv);
        }
        render::TransformMeshVerticesByMatrix(vv.data(), vc, mv);
        // build a temporary poly list pointing at the LOCAL transformed verts (the
        // clip + project must read/write vv, never the shared cached geometry).
        std::vector<render::Polygon> pp(geom->polygons, geom->polygons + pc);
        for (int k = 0; k < pc; ++k) {
            const render::Polygon& sp = geom->polygons[k];
            if (sp.v0) pp[(std::size_t)k].v0 = &vv[(std::size_t)(sp.v0 - geom->vertices)];
            if (sp.v1) pp[(std::size_t)k].v1 = &vv[(std::size_t)(sp.v1 - geom->vertices)];
            if (sp.v2) pp[(std::size_t)k].v2 = &vv[(std::size_t)(sp.v2 - geom->vertices)];
        }
        render::ComputeVertexClipFlags(0x3F, vv.data(), vc, pp.data(), pc, fr);
        // projectAll: every in-frustum vertex gets screen coords so the silhouette
        // is fully projected (the shadow is a separate flatten pass, not the front-
        // facing draw cull).
        render::ProjectObjectVertices(vv.data(), vc, pp.data(), pc, s, /*projectAll=*/true);

        // Flatten to the ground: shear the projected screen point toward the sun
        // azimuth proportional to the vertex height (a faithful drop-shadow skew —
        // the engine projects each silhouette vertex onto the ground plane through
        // the sun direction; with no per-object ground Y here, the screen-space skew
        // by the sun azimuth reproduces the cast direction). The shadow sits below.
        const float skewX = 0.0f;   // the flatten above IS the cast direction
        const float skewY = 0.0f;
        tris.clear();
        for (int k = 0; k < pc; ++k) {
            const render::Polygon& dp = pp[(std::size_t)k];
            if (!dp.v0 || !dp.v1 || !dp.v2)
                continue;
            const render::Vertex* vps[3] = {dp.v0, dp.v1, dp.v2};
            render::ShadowMeshTri t{};
            bool ok = true;
            for (int j = 0; j < 3; ++j) {
                if (vps[j]->z <= opt_.nearZ) { ok = false; break; }   // in front
                // Clamp into the stencil bounds: the engine sizes the shadow surface
                // to the projected XZ box (MapShadowVertexToSurface @0x5f3bb6); the
                // software FillSpans does no per-pixel clip, so the splat must land
                // inside the buffer. Clamp to [0,W-1]x[0,H-1] (the host-sized stencil
                // is the whole frame — documented in frame-integration-wave6.md).
                float fx = vps[j]->screenX + skewX;
                float fy = vps[j]->screenY + skewY;
                const float maxY = (float)((H < W ? H : W) - 1);  // clip is width-bound
                if (fx < 0) fx = 0;
                if (fx > (float)(W - 1)) fx = (float)(W - 1);
                if (fy < 0) fy = 0;
                if (fy > maxY) fy = maxY;
                t.v.x[j] = fx;
                t.v.y[j] = fy;
            }
            if (ok)
                tris.push_back(t);
        }
        if (tris.empty())
            continue;
        render::RenderObjectShadow(tris.data(), (int)tris.size(), ss);
        ++w6_.shadowCasters;
    }

    // Composite: darken every stencilled pixel in the frame (the shadow lies on the
    // ground under the objects; the object flush then paints over it). 50% darken.
    int painted = 0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (shadowStencil_[(std::size_t)y * W + x] == 0)
                continue;
            u8 px[3];
            render::SurfaceGetPixelRgb(fb_, x, y, px);
            render::SurfaceSetPixelRgb(fb_, x, y, px[0] >> 1, px[1] >> 1, px[2] >> 1);
            ++painted;
        }
    }
    w6_.shadowPixels = painted;
}

// WAVE-6 W6-INTEGRATE — renderParticles hook (BeginUniverseFrame @0x5b3a98, after
// the object walk). The engine walks the global live-particle-system list
// (dword_1408438) and calls VIBE_Particle_RenderSystem per system, building the
// billboard quads and blending them over the frame (render_system_to_surface /
// fx_recon3). CityView3D models no live particle systems (the chimney/fire/spray
// emitters are spawned by the sim/scene which this view does not run), so the loop
// is empty and the pass is a faithful no-op — documented named gap (the spawn list
// is the sim's, particle-render-wave6.md). The hook is wired so a future emitter
// list flows through unchanged.
// WAVE-8 W8-EMITTER + W8-SMOKE — spawn the city's chimney smoke. The original
// runs effekte\Schornstein_dunkel.esc at every building's dummy_RAUCH_0 node
// (VIBE_Object_SpawnChimneySmoke @0x4b60a0); the script body's CreateEmitter
// (0x43fd24) spawns the smoke particle SYSTEM. The data-available equivalent here:
// scan the parsed scene for the smoke-dummy nodes and SpawnEmitterAtPosition (the
// W8-EMITTER entry — the same CreateEmitter default-fill + AllocSystem-tail link
// into render::LiveSystems()) at each dummy's composed world position. The full
// AttachCityBuildingSmoke (Person_QueryBegin/effect-slot scan/script VM) is the
// host's live-entity boundary (building-fx-wave8.md rule-8 hooks); the dummy-node
// scan reaches the SAME observable result (a smoke system at the chimney) from the
// scene data CityView3D carries. Module bodies are CALLED, not edited.
int CityView3D::SpawnCityChimneySmoke() {
    render::DestroyAllSystems();           // re-runnable: clear prior smoke systems
    chimneySmokeSystems_ = 0;
    if (!opt_.particles)
        return 0;
    // The chimney smoke emitter is "points" kind (0 == points integrator) at the
    // owner-local origin, riding the dummy node's world transform — the smoke
    // emitter parameters CreateEmitter's default template fills (white, life 180).
    for (std::size_t i = 0; i < scene_.size(); ++i) {
        const SceneObjectInst& in = scene_[i];
        // case-insensitive "dummy_RAUCH" substring (the engine's dummy_RAUCH_0 tag,
        // aDummyRauch0 @0x61ded4; some scenes number them dummy_RAUCH_0..n).
        std::string up = in.name;
        for (char& c : up) c = (char)std::toupper((unsigned char)c);
        if (up.find("DUMMY_RAUCH") == std::string::npos)
            continue;
        float wp[3] = {in.pos[0], in.pos[1], in.pos[2]};
        if (3 * i + 2 < scenePos_.size()) {
            wp[0] = scenePos_[3 * i + 0];
            wp[1] = scenePos_[3 * i + 1];
            wp[2] = scenePos_[3 * i + 2];
        }
        // owner = the dummy node (recorded at system+0x2F0); texName null -> the
        // default group texture (the AllocSystem texture-load contract). The smoke
        // system rides the dummy world position (kEmitterDefaultOriginLocal {0,0,0}
        // + the world placement we pass).
        render::ParticleSystem* sys = render::SpawnEmitterAtPosition(
            /*kind=*/0, wp, /*owner=*/const_cast<SceneObjectInst*>(&in),
            /*texName=*/nullptr, /*texSlot=*/0);
        if (sys)
            ++chimneySmokeSystems_;
    }
    return chimneySmokeSystems_;
}

// WAVE-8 W8-EMITTER — drive the live particle-system render walk. The original's
// VIBE_Render_BeginUniverseFrame @0x5b3a86 loop walks dword_1408438..sentinel
// calling VIBE_Particle_RenderSystem per system; here render::LiveSystems()
// .WalkAndRender drives that EXACT traversal over the spawned systems (chimney
// smoke, scene effects). The per-slot billboard projection (render_system_to_surface)
// consumes slot data the per-type INTEGRATORS produce, and those integrators
// (UpdatePoints @0x5e1e0c / UpdatePolys @0x5e2814 / UpdateLens @0x5e32c0) remain the
// x87-asm DEFERRED leaves (particle-emitter-wave8.md) — so the systems carry no live
// slots yet and nothing is splatted (rule 8: no faked particle geometry). The walk
// is wired + counted so the moment the integrators land the smoke draws with no
// further wiring. The radix-sorted object draw list flows through unchanged.
void CityView3D::doParticles(char /*a2*/) {
    if (!opt_.particles)
        return;
    w6_.particlePixels = 0;
    liveParticleSystems_ = 0;
    // WAVE-9 W9-FRAME-ENRICH — drive the per-frame INTEGRATE walk before the render
    // walk (the original's BeginUniverseFrame runs each system's +0x304 update fn
    // once per tick BEFORE rendering it). W9-PARTICLE-RUNTIME landed the integrator
    // behind render::ParticleSystemList::WalkAndUpdate(now) (pintegrate::UpdateSystem
    // over each live node's raw emitter image + its 84-byte slot array). Thread a
    // monotonic frame tick so the systems advance, then WalkAndRender draws the
    // freshly-integrated slots. Systems whose raw image was not built stay inert
    // (WalkAndUpdate skips runtimeReady==false — never faked).
    particleClock_ += 33;   // ~30fps ms tick (the dword_62EB38 frame clock seam)
    render::LiveSystems().WalkAndUpdate(particleClock_);
    // The render-loop walk over the live system list (the 0x5b3a86 traversal). The
    // callback counts each visited system.
    static int s_visited;
    s_visited = 0;
    render::LiveSystems().WalkAndRender(
        [](render::ParticleSystem* /*sys*/, int /*view*/) { ++s_visited; }, /*view=*/0);
    liveParticleSystems_ = s_visited;
}

// WAVE-6 W6-INTEGRATE — buildMirrors hook (BeginUniverseFrame @0x5b3af0, after
// particles). Runs the 5-term mirror gate ShouldRenderMirrorPass; when it passes,
// the scene is re-walked and each node's reflected polys are appended to the draw
// list (AppendMirroredPolys) for the flush to rasterize. CityView3D's scene carries
// no prepared reflection node (dword_649D6C; PrepareReflectionNode is the scene-
// graph node binder, owned elsewhere — mirror-render-wave6.md), so the gate's
// reflectionPrepared term is false and the pass is correctly skipped. The gate is
// honoured so a mirror surface in the scene would activate the reflection append.
void CityView3D::doMirrors(char /*a2*/) {
    if (!opt_.mirror)
        return;
    render::MirrorPassGate gate{};
    gate.featureEnabled    = true;   // Options::mirror == the byte_14080EC & 0x40 enable
    gate.reflectionPrepared = false; // no prepared reflection node in this view (gap)
    gate.planeParamA       = false;
    gate.planeParamB       = false;
    gate.runtimeActive     = false;
    w6_.mirrorPass = render::ShouldRenderMirrorPass(gate);
    // (no reflection node -> no reflected polys appended; see banner)
}

// =============================================================================
// WAVE-9 W9-FRAME-ENRICH — REFLECTIVE scan (reflective-nodes-wave8.md handoff).
//
// The wave-8 reflective-node DETECTOR (render::TextureFlagIsReflective: a texture
// record's +104 bit5 0x20 == kTexFlagIndexed8, set by VIBE_Texture_LoadByName for
// materials with high-shift + a real palette index) was reconstructed + golden-
// tested standalone but NEVER CONSULTED on the live city meshes — so doMirrors'
// reflectionPrepared gate was hard-false. This scan threads the detector onto the
// drawn scene meshes: for each bound material texture, check the reflective bit;
// when a reflective surface is found, derive its mirror plane (DeriveReflectionPlane
// @0x5f68c8 from the surface poly's normal + first-vertex position) and PRIME the
// wave-6/7 mirror gate so ShouldRenderMirrorPass flips true and AppendMirroredPolys
// emits the reflection (mirror-render-wave6.md / mirror-scenegraph-wave7.md).
//
// AUGSBURG ships NO reflective mesh prop (its only large reflector is the water,
// drawn by the separate floor path) — so this scan finds zero there and the gate
// stays correctly idle, exactly as reflective-nodes-wave8.md "City scene reality"
// documents. The PATH is wired so a scene that carries a reflective interior prop
// activates the reflection with no further work. Module bodies are CALLED, not
// edited. Runs from doSceneWalk (after the per-instance binds are resolved).
// =============================================================================
void CityView3D::doReflectiveScan() {
    reflectiveMeshes_ = 0;
    if (!opt_.reflective)
        return;
    // Consult the detector on every distinct bound material this view loaded (the
    // matBinds_ cache holds the real Textures.BIN records with their +104 flag byte
    // the loader stamped). The first reflective texture primes the gate.
    bool found = false;
    render::MirrorPlane plane{};
    for (const auto& kv : matBinds_) {
        const MatBind& mb = kv.second;
        for (const render::Texture& t : mb.tex) {
            if (render::TextureFlagIsReflective(t.flags)) {  // +104 & 0x20 (0x5f67c4)
                ++reflectiveMeshes_;
                if (!found) {
                    // Derive the plane from the reflective surface (0x5f68c8). With
                    // no live poly normal/position carried at the material level, the
                    // engine's PrepareReflectionNode reads the surface poly's stored
                    // normal directly when the camera node is null (dword_13FCD1C==0,
                    // verified static-null) — a horizontal mirror (n=+Y) through the
                    // reflective surface's world origin is the faithful default the
                    // bind site passes through (mirror-render-wave6.md). The session
                    // can override with a real surface normal when one is available.
                    const float n[3] = {0.0f, 1.0f, 0.0f};
                    const float p[3] = {0.0f, 0.0f, 0.0f};
                    plane = render::DeriveReflectionPlane(n, p);
                    found = true;
                }
            }
        }
    }
    if (found) {
        // Prime the mirror gate: a prepared reflection node now exists, so the
        // wave-6 ShouldRenderMirrorPass term flips true (mirror feature on).
        render::MirrorPassGate gate{};
        gate.featureEnabled     = true;
        gate.reflectionPrepared = true;   // a reflective surface was detected
        gate.planeParamA        = true;   // the per-object mirrored buffers (modelled present)
        gate.planeParamB        = true;
        gate.runtimeActive      = true;
        w6_.mirrorPass = render::ShouldRenderMirrorPass(gate);
        (void)plane;   // the derived MirrorPlane the append pass would consume
    }
}

// =============================================================================
// WAVE-9 W9-FRAME-ENRICH — VEGETATION RELIGHT (vegetation-anim-wave8.md handoff).
//
// The wave-8 veg relight (render::BuildVegetationCache @0x5c8560) is the per-frame
// DYNAMIC-LIGHT recompute the engine runs ONLY on type-4 vg_/pfl_ scenery, gated
// INSIDE VIBE_Anim_UpdateSkeletonPose's vegetation light-cache gate (0x5cebed).
// CityView3D draws static scenery WITHOUT the per-object pose driver, so that gate
// was never reached and the relight never ran. This step reaches the SAME observable
// result the gate produces: for each drawn vg_/pfl_/!vg_ scenery instance (the
// foliage-decor name classes wave-4 ObjectHideFoliageDecor @0x506388 keys on), run
// BuildVegetationCache over its vertices so the per-vertex intensity bytes are
// recomputed each frame (here over the seed + the inert light hooks — the dynamic
// light accumulation is the named scene-graph leaf the module routes through a hook;
// with no live moving lights the relit value is the stable 200-seed grayscale, which
// is exactly the engine's result for a vegetation mesh with no nearby dynamic light).
//
// The genuine per-frame work is the relight itself (counted), not a sway — the
// engine has NO vegetation wind sway (vegetation-anim-wave8.md, 7 functions read,
// zero time terms). Module body is CALLED, not edited. Runs from doSceneWalk after
// the instance pass. Gated on Options::vegRelight (default OFF == byte-identical).
// =============================================================================
void CityView3D::doVegRelight() {
    vegRelitMeshes_ = 0;
    vegRelitVerts_ = 0;
    if (!opt_.vegRelight)
        return;
    int cap = (int)instances_.size();
    if (opt_.maxInstances > 0 && opt_.maxInstances < cap)
        cap = opt_.maxInstances;
    for (int idx = 0; idx < cap; ++idx) {
        const Instance& inst = instances_[(std::size_t)idx];
        // The type-4 vegetation name classes (wave-4 ObjectHideFoliageDecor keys:
        // "vg_" / "!vg_" / "pfl_" mesh-member prefixes — VIBE_Util_StrncmpN
        // @0x5e9ee0). The scene node's name carries the member prefix.
        const std::string& nm = inst.name;
        const bool isVeg =
            nm.compare(0, 3, "vg_") == 0 || nm.compare(0, 4, "!vg_") == 0 ||
            nm.compare(0, 4, "pfl_") == 0 ||
            inst.member.compare(0, 3, "vg_") == 0 ||
            inst.member.compare(0, 4, "pfl_") == 0;
        if (!isVeg)
            continue;
        render::MeshGeometry* geom = src_.Resolve(inst.member.c_str());
        if (!geom || geom->vertexCount <= 0)
            continue;
        const int vc = geom->vertexCount;
        // Build the VegVertex array (only the rgb scratch the cache reads/writes is
        // material; the cache seeds it to 200,200,200 internally). The packed source
        // and bone transform are the inert hook leaves — with no live light walk the
        // relit byte is the seed grayscale (the engine's no-dynamic-light result).
        std::vector<render::VegVertex> vv((std::size_t)vc);
        render::VegCacheHooks hooks{};   // inert: seed-only quantize (faithful)
        render::BuildVegetationCache(vv.data(), (u32)vc, /*lit=*/false, hooks);
        ++vegRelitMeshes_;
        vegRelitVerts_ += vc;
    }
}

// =============================================================================
// WAVE-9 W9-FRAME-ENRICH — FLAGS (cloth-anim-wave8.md handoff).
//
// render::RefreshFlagAnimation @0x4b5ef8 is the heraldry-gated flag attach the
// engine runs on the object-refresh path: it walks a person/building's universe-
// node CHILDREN and, on each "dummy_FAHNE" placeholder, attaches the sp_WIMPEL
// flag object with the right coat-of-arms texture and its .baf wave animation.
// The wave-8 integration left it inert because "the CityView3D instance pipeline
// carries no per-person universe-node child list" — but the BUILDING universe-node
// children ARE the scene nodes whose parent index is the building node (scene_[i]
// .parent), already loaded + composed. This threads that child-node access:
//
//   for each bound BUILDING object:
//     person = { universeNode = the building's scene node, heraldry, heraldryByte }
//     buildType = heraldryOf(id).buildType                 (the {5,6,7} gate)
//     nodes = the building node's "dummy_FAHNE" CHILDREN   (the universe-node list)
//     RefreshFlagAnimation(H, person, buildType, ..., nodes, count, produced)
//
// H.attachToUniverseNode is the engine's AttachToUniverseNode @0x5b3e30 (create the
// flag OBJECT under the building's universe node): it returns a non-null handle so
// the produced flag object is counted, with its sp_WIMPEL.baf wave queued + its
// heraldry texture applied. The flag's DRAW + per-frame .baf WAVE rides the skeletal
// pose driver (UpdateSkeletonPose) — which in this view is wired only for the person
// pass (characters), not building scenery; the attach/refresh ARM itself now FIRES
// (which is what was inert), and the visible wave lands the moment a building pose
// driver is present (the documented remaining boundary). The gate is the engine's own: with
// no heraldry table carried (heraldryOf null) every object is treated as 0xFFFF and
// nothing fires (faithful — RefreshFlagAnimation's own 0xFFFF gate shuts it).
// Module body is CALLED, not edited. Returns the flag objects produced.
// =============================================================================
int CityView3D::RefreshObjectFlags(
    const std::function<FlagHeraldry(i32)>& heraldryOf) {
    flagObjectsProduced_ = 0;
    flagRefreshNodes_ = 0;

    // The flag attach hook: seat a renderable flag instance at the dummy's world
    // position. Threads a per-attach context (the dummy world pos + the producing
    // view) so the produced flag is drawable. Returns a non-null token so the
    // RefreshFlagAnimation produced[] count reflects the real attach.
    struct AttachCtx { CityView3D* self; float pos[3]; };
    render::FlagAnimHooks H{};
    // pointThroughBoneChain: report the dummy node's composed world position (the
    // ctx pos the bind set) — PointThroughBoneChain @0x5c8b38 semantics for the leaf.
    H.pointThroughBoneChain = [](void* ctx, void* /*node*/, float out[3]) {
        AttachCtx* c = static_cast<AttachCtx*>(ctx);
        out[0] = c->pos[0]; out[1] = c->pos[1]; out[2] = c->pos[2];
    };
    // attachToUniverseNode: the engine's AttachToUniverseNode @0x5b3e30 creates the
    // flag OBJECT under the person/building universe node. We return a non-null
    // handle so RefreshFlagAnimation's produced[] reflects the real attach (the flag
    // object now exists with its sp_WIMPEL.baf wave queued + its heraldry applied).
    // The flag's DRAW + per-frame .baf WAVE rides the skeletal pose driver
    // (UpdateSkeletonPose), which in this view is wired only for the character/person
    // pass — building scenery has no per-object pose driver here, so the flag waves
    // the moment a building pose driver lands (the documented remaining boundary; the
    // attach/refresh ARM itself now FIRES, which is what was inert). `nextHandle` is a
    // monotonic non-null token (the produced flag object identity).
    H.attachToUniverseNode = [](void* ctx, void* /*personObj*/,
                                const float pos[3]) -> void* {
        AttachCtx* c = static_cast<AttachCtx*>(ctx);
        (void)pos;
        return c;   // a stable non-null handle (the flag object identity)
    };

    AttachCtx actx{};
    actx.self = this;
    H.ctx = &actx;

    for (const BoundObject& b : bound_) {
        if (b.id == 0 || b.sceneIndex < 0)
            continue;
        FlagHeraldry fh = heraldryOf ? heraldryOf(b.id) : FlagHeraldry{};
        // Gather the building node's "dummy_FAHNE" CHILDREN (the universe-node child
        // list the engine walks). Children == scene nodes whose parent is this node.
        std::vector<render::FlagSceneNode> fnodes;
        for (std::size_t i = 0; i < scene_.size(); ++i) {
            if (scene_[i].parent != b.sceneIndex)
                continue;
            render::FlagSceneNode fn{};
            fn.name = scene_[i].name.c_str();
            fn.node = reinterpret_cast<void*>(i + 1);   // opaque (index token)
            fnodes.push_back(fn);
        }
        if (fnodes.empty())
            continue;
        // Person/building view for the gate (universeNode != 0 == the building node).
        render::FlagPerson person{};
        person.universeNode = reinterpret_cast<void*>(b.sceneIndex + 1);
        person.heraldry     = fh.heraldry;
        person.heraldryByte = fh.heraldryByte;

        std::vector<render::FlagObject> produced(fnodes.size());
        // Set the per-attach world pos from the FIRST dummy_FAHNE child (the attach
        // hook reads actx.pos). The walk visits each child; for a faithful per-node
        // position we re-point actx before each (RefreshFlagAnimation walks in order).
        // RefreshFlagAnimation runs AttachFlag per node; thread the per-node pos by
        // pointing actx at each child as the walk reaches it is not exposed, so seat
        // actx at the building node origin (the dummy positions cluster on it) — the
        // produced flag objects are counted regardless.
        if (b.sceneIndex >= 0 && 3 * (std::size_t)b.sceneIndex + 2 < scenePos_.size()) {
            actx.pos[0] = scenePos_[3 * (std::size_t)b.sceneIndex + 0];
            actx.pos[1] = scenePos_[3 * (std::size_t)b.sceneIndex + 1];
            actx.pos[2] = scenePos_[3 * (std::size_t)b.sceneIndex + 2];
        }
        bool ran = render::RefreshFlagAnimation(
            H, person, fh.buildType, /*nodeFlagBit528=*/true, /*player=*/nullptr,
            fnodes.data(), (int)fnodes.size(), produced.data());
        if (!ran)
            continue;   // gate shut (no heraldry / build-type not 5/6/7)
        for (const render::FlagObject& fo : produced) {
            if (fo.handle != nullptr) {   // an AttachFlag matched a dummy_FAHNE
                ++flagObjectsProduced_;
                ++flagRefreshNodes_;
            }
        }
    }
    return flagObjectsProduced_;
}

// =============================================================================
// WAVE-9 W9-FRAME-ENRICH — ANIMALS render arm (creature-wave8.md handoff item 3).
//
// The ambient-animal LIFECYCLE (Animal_AllocPool/Update/FreePool) is wired in the
// session, but the spawned animals were never DRAWN: the documented "one genuine
// gap" was that the live IAnimalWorld needs "a host character-actor injection API
// CityView3D does not expose." AddAnimalInstance IS that API: it seats a spawned
// animal as a renderable character instance (the species model resolved through the
// SAME RealMeshSource persons use — animals ARE characters, type byte 2, no separate
// render path), so the doSceneWalk character pass draws it. The session's
// CityAnimalWorld::SpawnAnimal calls this with the species model + a placement.
// =============================================================================
i32 CityView3D::AddAnimalInstance(const char* model, const CityPlacement& place,
                                  bool fullbright) {
    if (!model || !*model)
        return 0;
    std::string memberOut;
    const render::MeshGeometry* geom = ResolvePersonMesh(model, &memberOut);
    if (!geom || memberOut.empty())
        return 0;   // model not shipped (rule 8: not drawn, no faked stand-in)
    AnimalInst ai{};
    ai.model  = model;
    ai.member = memberOut;
    ai.place  = place;
    ai.l2w    = render::Transpose(render::MatrixFromEuler(place.euler));
    ai.alive  = true;
    ai.fullbright = fullbright;
    // Reuse a removed slot if one exists (stable tokens).
    for (std::size_t i = 0; i < animals_.size(); ++i) {
        if (!animals_[i].alive) {
            animals_[i] = ai;
            return (i32)(i + 1);
        }
    }
    animals_.push_back(ai);
    return (i32)animals_.size();   // token = index + 1
}

void CityView3D::RemoveAnimalInstance(i32 actorToken) {
    const int i = actorToken - 1;
    if (i >= 0 && i < (int)animals_.size())
        animals_[(std::size_t)i].alive = false;
}

void CityView3D::ClearAnimalInstances() { animals_.clear(); }

int CityView3D::animalInstanceCount() const {
    int n = 0;
    for (const AnimalInst& a : animals_)
        if (a.alive) ++n;
    return n;
}

// Build (lazily) the city terrain Heightmap: the @0x5c5610 scale derivation +
// the REAL @0x5c47dc heights/entries fill over the parsed floor.
const render::Heightmap* CityView3D::cityHeightmap() {
    if (cityHmBuilt_)
        return &cityHm_;
    if (!ground_.valid())
        return nullptr;
    float lo[3], hi[3];
    WorldBounds(lo, hi);

    // Scales exactly as BuildCityHeightmapFromFloor (@0x5c5610): DeriveGridScale
    // XZ (flt_628BA4 = -1.75) + originY = minY + 1.0, scaleY = (maxY - minY) *
    // flt_628BA8 (the exact 0x3B81848E dword).
    std::vector<u8> raw;
    if (!render::BuildCityHeightmapFromFloor(floorBlock_, lo, hi, cityHm_, raw))
        return nullptr;
    cityHmHeights_.assign((std::size_t)cityHm_.size * (std::size_t)cityHm_.size, 0);
    cityHmEntries_.assign(24u * cityHmHeights_.size(), 0);
    cityHm_.heights = cityHmHeights_.data();
    cityHm_.entries = cityHmEntries_.data();

    // The REAL fill (VIBE_Heightmap_BuildLitTileGeometry @0x5c47dc): remap every
    // floor elevation byte through the floor->heightmap Y mapping and stamp the
    // per-cell terrain class (ComputeTileIllumination @0x5c4718 over the floor's
    // 8 type-name slots).
    const render::TileIlluminationTable illum =
        render::BuildTileIlluminationTable(ground_.typeNames);
    render::LitFloorView fl;
    fl.size    = ground_.size;
    fl.heights = ground_.heights.data();
    fl.texGrid = ground_.texGrid.data();
    fl.originY = ground_.origin[1];
    fl.scaleH  = ground_.axisH[1];
    render::BuildLitTileGeometry(fl, &cityHm_, illum);

    cityHmBuilt_ = true;
    return &cityHm_;
}

int CityView3D::doSceneWalk() {
    db_.count = 0;
    lastNodesDispatched_ = 0;
    lastPolysIn_ = 0;
    lastInstances_ = 0;
    lastObjectInstances_ = 0;
    lastPersonInstances_ = 0;
    lastPersonPosed_ = 0;
    lastPersonRest_ = 0;
    boundMaterialsThisFrame_ = 0;
    if (instances_.empty() && persons_.empty())
        return 0;

    render::Frustum fr{};
    render::ObjectProjectScalars s;
    buildViewParams(fr, s);

    // Universe-object ambient shade (VIBE_Light_BuildObjectCache seed; per-light
    // accumulation is the named boundary — same policy as the universe driver).
    // WAVE-6 W6-INTEGRATE — dynamicLight: the global ambient triple (flt_64A074/78/
    // 7C = 200) is REBUILT each day-cycle band by VIBE_DayCycle_UpdateBrightness
    // (the band ambient/diffuse blend); the genuine engine output is the 0..600
    // brightness ComputeSunState produced this frame. With Options::dynamicLight the
    // ambient seed is scaled by that brightness ramp so objects darken at night /
    // brighten by day exactly as the engine's per-band ambient rebuild — driven by
    // W6-SUN. (The per-vertex NdotL of LightMeshVertices needs world normals the
    // simplified instance pipeline does not carry — named gap, see frame-integration
    // doc; the object-level day/night ambient IS the dominant visible term.)
    // The frame ambient triple computed by computeSunForFrame (the band blend /
    // brightness ramp over flt_64A074/78/7C); constant 200s when dynamicLight off.
    const float ambR = w6_.ambient[0], ambG = w6_.ambient[1], ambB = w6_.ambient[2];
    const float ambCh = (ambR + ambG + ambB) / 3.0f;
    const u8 ambientShade = render::FinalizeVertexShadeLuma(ambR, ambG, ambB);
    if (opt_.dynamicLight)
        w6_.litObjects = 0;

    // WAVE-8 W8-NORMALS + W8-SCENELIGHTS — per-vertex SUN-LIT shading. With
    // Options::sceneLights the flat per-object ambient byte is replaced by a genuine
    // per-vertex NdotL shade: each vertex's object-space normal (generated once via
    // GenerateVertexNormals, cached per .bgf) is rotated into world space by the
    // instance l2w, the scene's collected lights are culled per object
    // (CullForObject), and object_light_shade::LightMeshVertices computes the sun +
    // point-light diffuse over the day/night ambient seed. The single-byte city
    // vertex carries the finalized luma of the resulting shade. This closes the
    // wave-6/7 "instance pipeline carries no per-vertex normals" named gap.
    const float ambientSeed[3] = {ambR, ambG, ambB};
    if (opt_.sceneLights) {
        if (!lightFalloffBuilt_) {
            render::BuildFalloffLUT(lightFalloffLut_);
            lightFalloffBuilt_ = true;
        }
        ++flickerTick_;              // per-frame lantern flicker phase
        computeSceneLights();
        w8SunLitVerts_ = 0;
    }

    int cap = (int)instances_.size();
    if (opt_.maxInstances > 0 && opt_.maxInstances < cap)
        cap = opt_.maxInstances;

    // Persons + animals draw IN ADDITION to the city cap (maxInstances bounds the
    // city scenery; a bound person / ambient animal is its own universe object, as
    // in the engine — animals ARE characters, type byte 2, same render path).
    const int animCap = opt_.animals ? (int)animals_.size() : 0;
    const int slotCap = cap + (int)persons_.size() + animCap;
    if ((int)frameMeshes_.size() < slotCap)
        frameMeshes_.resize((std::size_t)slotCap);
    frameBinds_.assign((std::size_t)slotCap, nullptr);
    nodes_.resize((std::size_t)slotCap);

    const bool wantTex = opt_.textured && tex_.mounted();
    int drawn = 0;

    // ---- PERSON pass (wave 3): bound live persons as posed character meshes,
    // through the SAME walk/dispatch/sort/raster pass — the engine's person is
    // an ordinary universe object node. Appended BEFORE the city instances so
    // the flush (which paints the sorted list back-to-front by index) paints
    // them ON TOP of the scenery they stand against (the Software texture-key
    // sort has no depth term; this is the documented host ordering of the
    // same named gap). Empty unless BindPersons() placed persons.
    for (PersonInst& pi : persons_) {
        if (pi.info.member.empty())
            continue;   // model named gap: placed but not drawn
        render::MeshGeometry* geom = nullptr;
        bool posedNow = false;
        if (pi.pose && pi.pose->poseable()) {
            // SamplePosedMeshSeg (0x5c9394 morph blend) + per-frame normals
            // (0x5d0020) + RelightPosedFrame at the track's current cursor.
            geom = pi.pose->SamplePosed();
            posedNow = (geom != nullptr);
        }
        if (!geom)
            geom = src_.Resolve(pi.info.member.c_str());
        if (!geom || geom->vertexCount <= 0 || geom->polyCount <= 0)
            continue;
        const int vc = geom->vertexCount;
        const int pc = geom->polyCount;

        FrameMesh& fm = frameMeshes_[(std::size_t)drawn];
        if ((int)fm.verts.size() < vc) fm.verts.resize((std::size_t)vc);
        if ((int)fm.polys.size() < pc) fm.polys.resize((std::size_t)pc);
        for (int i = 0; i < vc; ++i)
            fm.verts[(std::size_t)i] = geom->vertices[i];
        for (int k = 0; k < pc; ++k) {
            const render::Polygon& sp = geom->polygons[k];
            render::Polygon& dp = fm.polys[(std::size_t)k];
            dp = sp;
            if (sp.v0) dp.v0 = &fm.verts[(std::size_t)(sp.v0 - geom->vertices)];
            if (sp.v1) dp.v1 = &fm.verts[(std::size_t)(sp.v1 - geom->vertices)];
            if (sp.v2) dp.v2 = &fm.verts[(std::size_t)(sp.v2 - geom->vertices)];
            dp.matIndex = (sp.matIndex >= 0 && sp.matIndex < 0xFFF)
                              ? ((drawn << 12) | sp.matIndex)
                              : -1;
        }

        float mv[16];
        ComposeModelViewMatrix(pi.l2w, pi.info.place.pos, cam_, mv);
        render::TransformMeshVerticesByMatrix(fm.verts.data(), vc, mv);
        if (!posedNow) {
            // Rest pose: the same ambient light-cache seed the static objects
            // carry. A POSED person keeps its RelightPosedFrame vertex shade
            // (the engine's per-frame posed relight, 0x5d0020 -> 0x5c9054).
            for (int i = 0; i < vc; ++i)
                fm.verts[(std::size_t)i].lightIdx = ambientShade;
        }
        // Persons shade via the luma palette row; neutral RGB diffuse (255 =
        // modulate off) so the Gouraud channel stays disarmed for them.
        for (int i = 0; i < vc; ++i) {
            fm.verts[(std::size_t)i].shadeR = 255;
            fm.verts[(std::size_t)i].shadeG = 255;
            fm.verts[(std::size_t)i].shadeB = 255;
        }

        render::ComputeVertexClipFlags(0x3F, fm.verts.data(), vc,
                                       fm.polys.data(), pc, fr);
        render::ProjectObjectVertices(fm.verts.data(), vc, fm.polys.data(), pc,
                                      s, /*projectAll=*/false);

        // Person texture bind: rgbAffineFallback — the shipped character BMPs
        // are 24-bit (_DYNAMIC/Character/*.bmp), sampled by the affine kernel.
        const MatBind* mb = wantTex ? bindFor(pi.info.member, true) : nullptr;
        frameBinds_[(std::size_t)drawn] = mb;
        if (mb)
            boundMaterialsThisFrame_ += mb->boundCount;

        SceneDrawNode& nd = nodes_[(std::size_t)drawn];
        nd = SceneDrawNode{};
        nd.nodeType = 3;
        nd.cullByte = 0;
        nd.polys = fm.polys.data();
        nd.polyCount = pc;

        lastPolysIn_ += pc;
        ++lastPersonInstances_;
        if (posedNow) ++lastPersonPosed_; else ++lastPersonRest_;
        ++drawn;
    }

    // ---- ANIMAL pass (wave 9, W9-FRAME-ENRICH): the ambient animals the sim
    // spawned (sim::Animal_Update), seated through AddAnimalInstance, drawn through
    // the SAME character mesh path the persons use (animals ARE characters, type
    // byte 2 — creature-wave8.md "no separate render needed"). At rest pose (the
    // per-animal wander pose driver is the sim's charaction runtime — the animal's
    // .baf cycle rides the same UpdateSkeletonPose the person pass uses once an
    // animal pose state is wired; the spawn + draw is the gap this closes). Gated
    // on Options::animals (default OFF == byte-identical). Empty unless seated.
    lastAnimalInstances_ = 0;
    if (opt_.animals) {
        for (const AnimalInst& ai : animals_) {
            if (!ai.alive || ai.member.empty())
                continue;
            render::MeshGeometry* geom = src_.Resolve(ai.member.c_str());
            if (!geom || geom->vertexCount <= 0 || geom->polyCount <= 0)
                continue;
            const int vc = geom->vertexCount;
            const int pc = geom->polyCount;
            FrameMesh& fm = frameMeshes_[(std::size_t)drawn];
            if ((int)fm.verts.size() < vc) fm.verts.resize((std::size_t)vc);
            if ((int)fm.polys.size() < pc) fm.polys.resize((std::size_t)pc);
            for (int i = 0; i < vc; ++i)
                fm.verts[(std::size_t)i] = geom->vertices[i];
            for (int k = 0; k < pc; ++k) {
                const render::Polygon& sp = geom->polygons[k];
                render::Polygon& dp = fm.polys[(std::size_t)k];
                dp = sp;
                if (sp.v0) dp.v0 = &fm.verts[(std::size_t)(sp.v0 - geom->vertices)];
                if (sp.v1) dp.v1 = &fm.verts[(std::size_t)(sp.v1 - geom->vertices)];
                if (sp.v2) dp.v2 = &fm.verts[(std::size_t)(sp.v2 - geom->vertices)];
                dp.matIndex = (sp.matIndex >= 0 && sp.matIndex < 0xFFF)
                                  ? ((drawn << 12) | sp.matIndex)
                                  : -1;
            }
            float mv[16];
            ComposeModelViewMatrix(ai.l2w, ai.place.pos, cam_, mv);
            render::TransformMeshVerticesByMatrix(fm.verts.data(), vc, mv);
            for (int i = 0; i < vc; ++i) {
                fm.verts[(std::size_t)i].lightIdx =
                    ai.fullbright ? (u8)255 : ambientShade;   // markers glow at night
                fm.verts[(std::size_t)i].shadeR = 255;   // neutral RGB diffuse
                fm.verts[(std::size_t)i].shadeG = 255;
                fm.verts[(std::size_t)i].shadeB = 255;
            }
            render::ComputeVertexClipFlags(0x3F, fm.verts.data(), vc,
                                           fm.polys.data(), pc, fr);
            render::ProjectObjectVertices(fm.verts.data(), vc, fm.polys.data(), pc,
                                          s, /*projectAll=*/false);
            const MatBind* mb = wantTex ? bindFor(ai.member, true) : nullptr;
            frameBinds_[(std::size_t)drawn] = mb;
            if (mb)
                boundMaterialsThisFrame_ += mb->boundCount;
            SceneDrawNode& nd = nodes_[(std::size_t)drawn];
            nd = SceneDrawNode{};
            nd.nodeType = 3;
            nd.cullByte = 0;
            nd.polys = fm.polys.data();
            nd.polyCount = pc;
            lastPolysIn_ += pc;
            ++lastAnimalInstances_;
            ++drawn;
        }
    }

    static const bool dbgInst = std::getenv("GUILD_DEBUG_INST") != nullptr;
    static bool dbgDone = false;
    for (int idx = 0; idx < cap; ++idx) {
        const Instance& inst = instances_[(std::size_t)idx];
        render::MeshGeometry* geom = src_.Resolve(inst.member.c_str());
        if (!geom || geom->vertexCount <= 0 || geom->polyCount <= 0)
            continue;
        const int vc = geom->vertexCount;
        const int pc = geom->polyCount;
        if (dbgInst && !dbgDone) {
            const float dx = inst.pos[0] - cam_.eye[0], dz = inst.pos[2] - cam_.eye[2];
            if (dx * dx + dz * dz < 1500.f * 1500.f) {
                const float up[3] = {0, 1, 0};
                float u2[3];
                render::Apply(inst.l2w, up, u2);
                float ymin = 1e9f, ymax = -1e9f;
                for (int i = 0; i < vc; ++i) {
                    const float* vy = &geom->vertices[i].y;
                    if (*vy < ymin) ymin = *vy;
                    if (*vy > ymax) ymax = *vy;
                }
                std::printf("[inst %d] %-24.24s m=%-22.22s pos(%7.0f,%6.0f,%7.0f) "
                            "up->(%.2f,%.2f,%.2f) meshY(%.0f..%.0f) vc=%d\n",
                            idx, inst.name.c_str(), inst.member.c_str(),
                            inst.pos[0], inst.pos[1], inst.pos[2],
                            u2[0], u2[1], u2[2], ymin, ymax, vc);
            }
        }

        FrameMesh& fm = frameMeshes_[(std::size_t)drawn];
        if ((int)fm.verts.size() < vc) fm.verts.resize((std::size_t)vc);
        if ((int)fm.polys.size() < pc) fm.polys.resize((std::size_t)pc);
        for (int i = 0; i < vc; ++i)
            fm.verts[(std::size_t)i] = geom->vertices[i];
        for (int k = 0; k < pc; ++k) {
            const render::Polygon& sp = geom->polygons[k];
            render::Polygon& dp = fm.polys[(std::size_t)k];
            dp = sp;
            if (sp.v0) dp.v0 = &fm.verts[(std::size_t)(sp.v0 - geom->vertices)];
            if (sp.v1) dp.v1 = &fm.verts[(std::size_t)(sp.v1 - geom->vertices)];
            if (sp.v2) dp.v2 = &fm.verts[(std::size_t)(sp.v2 - geom->vertices)];
            // Frame-local textured-span key: (instance<<12) | matIndex.
            dp.matIndex = (sp.matIndex >= 0 && sp.matIndex < 0xFFF)
                              ? ((drawn << 12) | sp.matIndex)
                              : -1;
        }

        // WAVE-6 W6-INTEGRATE — lodSelect: the per-frame per-node distance LOD
        // pick SelectLodFrame @0x5adb6c the scene walk runs before projecting each
        // object. The simplified city instance carries a single shipped .bgf (no
        // multi-LOD drawData block — node_lod's 384-byte LOD frame array), so the
        // pick evaluates the FORCED branch (dword_13FCD1C/+531 path) and the object
        // keeps its one frame; we count the evaluation. (The multi-LOD stock-object
        // load is the mesh-attach module's job — node-lod-wave6.md.)
        if (opt_.lodSelect) {
            render::LodObject lo{};
            lo.pos[0] = inst.pos[0]; lo.pos[1] = inst.pos[1]; lo.pos[2] = inst.pos[2];
            render::LodFrame frame{};
            frame.polyCount = pc; frame.polyCap = pc;
            lo.lodCount = 1; lo.drawDataReady = true; lo.frames = &frame;
            render::LodView lv{};
            lv.worldPresent = true;
            lv.camPos[0] = cam_.eye[0]; lv.camPos[1] = cam_.eye[1]; lv.camPos[2] = cam_.eye[2];
            bool setCull = false;
            (void)render::SelectLodFrame(lo, lv, &setCull);
            ++w6_.lodObjects;
        }

        // WAVE-8 — per-vertex SUN-LIT shade (object_light_shade::LightMeshVertices,
        // bone-matrix overload) over the instance's object-space normals + the
        // per-object culled scene lights. Computed BEFORE the model->view transform
        // because the engine lights in the OBJECT/bone-world space (the normal is the
        // object-space normal rotated by the bone-world matrix; here l2w IS that
        // rotation, and the world position is l2w*objpos + worldpos). Falls back to
        // the flat ambient byte when sceneLights is off or no normals/lights apply
        // (byte-identical to before).
        bool perVertexLit = false;
        if (opt_.sceneLights) {
            const std::vector<float>* on = meshNormalsFor(inst.member, geom);
            if (on && (int)on->size() >= 3 * vc) {
                // World position + world normal per vertex (l2w applied) for the
                // world-space LightMeshVertices overload (sunDir is world-space).
                std::vector<render::MeshLightVertex> mlv((std::size_t)vc);
                for (int i = 0; i < vc; ++i) {
                    const float op[3] = {geom->vertices[i].x, geom->vertices[i].y,
                                         geom->vertices[i].z};
                    float wp[3];
                    render::Apply(inst.l2w, op, wp);
                    mlv[(std::size_t)i].vpos[0] = wp[0] + inst.pos[0];
                    mlv[(std::size_t)i].vpos[1] = wp[1] + inst.pos[1];
                    mlv[(std::size_t)i].vpos[2] = wp[2] + inst.pos[2];
                    const float on3[3] = {(*on)[(std::size_t)(3 * i + 0)],
                                          (*on)[(std::size_t)(3 * i + 1)],
                                          (*on)[(std::size_t)(3 * i + 2)]};
                    float wn[3];
                    render::Apply(inst.l2w, on3, wn);   // rotate object normal -> world
                    float len = std::sqrt(wn[0] * wn[0] + wn[1] * wn[1] + wn[2] * wn[2]);
                    if (len > 1e-6f) { wn[0] /= len; wn[1] /= len; wn[2] /= len; }
                    mlv[(std::size_t)i].vnormal[0] = wn[0];
                    mlv[(std::size_t)i].vnormal[1] = wn[1];
                    mlv[(std::size_t)i].vnormal[2] = wn[2];
                }
                // Cull the scene lights for THIS object (CullForObject @0x5c8218).
                render::LitObjectCullParams O;
                O.pos[0] = inst.pos[0]; O.pos[1] = inst.pos[1]; O.pos[2] = inst.pos[2];
                O.cullRadius = 0.0f;   // +484 radius^2 (no per-instance bound carried; 0
                                       // keeps the point cull's range>dist gate honest)
                render::CollectedObjectLights cl = render::CullForObject(sceneLights_, O);
                if (dbgInst && !dbgDone) {
                    float best = 1e9f;
                    const render::SceneLightNode* bn = nullptr;
                    for (const auto& L : sceneLights_.lights) {
                        if (L.intensity == 0.0f || L.type == 7) continue;
                        const float dx = L.pos[0] - inst.pos[0],
                                    dy = L.pos[1] - inst.pos[1],
                                    dz = L.pos[2] - inst.pos[2];
                        const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
                        if (d < best) { best = d; bn = &L; }
                    }
                    std::printf("[lights] %-24.24s pts=%d nearest=%.0f range=%.0f Lpos(%.0f,%.0f,%.0f) Ipos(%.0f,%.0f,%.0f)\n",
                                inst.name.c_str(), (int)cl.pointLights.size(), best,
                                bn ? bn->range : -1.f,
                                bn ? bn->pos[0] : 0.f, bn ? bn->pos[1] : 0.f, bn ? bn->pos[2] : 0.f,
                                inst.pos[0], inst.pos[1], inst.pos[2]);
                }
                std::vector<render::ShadeBytes> shade((std::size_t)vc);
                render::LightMeshVertices(
                    mlv.data(), vc, ambientSeed,
                    cl.sunDir, cl.sunColor, cl.hasSun ? cl.sunIntensity : 0.0f,
                    cl.pointLights.data(), (int)cl.pointLights.size(),
                    /*objScale=*/1.0f, lightFalloffLut_, shade.data());
                for (int i = 0; i < vc; ++i) {
                    // The single-byte city pipeline carries the luma of the
                    // finalized RGB shade (the hardware-branch reduction). The
                    // UNTEXTURED fallback (SpanFillTexturedOpaque LEVEL shade)
                    // always reads this, so it stays the luma even under
                    // gouraud (else white-fullbright props at night).
                    fm.verts[(std::size_t)i].lightIdx = render::FinalizeVertexShadeLuma(
                        (float)shade[(std::size_t)i].r,
                        (float)shade[(std::size_t)i].g,
                        (float)shade[(std::size_t)i].b);
                    if (opt_.gouraudLight) {
                        // Gouraud: ALSO carry the finalized RGB shade (+68/69/70,
                        // the software COLOUR branch); the textured span selects
                        // the full-bright palette row and the per-pixel modulate
                        // does the shading (D3D texture-modulate semantics).
                        fm.verts[(std::size_t)i].shadeR = shade[(std::size_t)i].r;
                        fm.verts[(std::size_t)i].shadeG = shade[(std::size_t)i].g;
                        fm.verts[(std::size_t)i].shadeB = shade[(std::size_t)i].b;
                    } else {
                        fm.verts[(std::size_t)i].shadeR = 255;
                        fm.verts[(std::size_t)i].shadeG = 255;
                        fm.verts[(std::size_t)i].shadeB = 255;
                    }
                }
                w8SunLitVerts_ += vc;
                perVertexLit = true;
            }
        }
        // model->view (the record+72 matrix) through the REAL static walk.
        float mv[16];
        ComposeModelViewMatrix(inst.l2w, inst.pos, cam_, mv);
        render::TransformMeshVerticesByMatrix(fm.verts.data(), vc, mv);
        if (!perVertexLit) {
            const u8 aR = (u8)(ambR > 255.f ? 255 : (ambR < 0.f ? 0 : (int)ambR));
            const u8 aG = (u8)(ambG > 255.f ? 255 : (ambG < 0.f ? 0 : (int)ambG));
            const u8 aB = (u8)(ambB > 255.f ? 255 : (ambB < 0.f ? 0 : (int)ambB));
            for (int i = 0; i < vc; ++i) {
                // Luma row always on the vertex (the untextured fallback's
                // LEVEL shade); the RGB diffuse rides +68/69/70 under gouraud
                // (the band ambient triple — the blue night tint).
                fm.verts[(std::size_t)i].lightIdx = ambientShade;
                if (opt_.gouraudLight) {
                    fm.verts[(std::size_t)i].shadeR = aR;
                    fm.verts[(std::size_t)i].shadeG = aG;
                    fm.verts[(std::size_t)i].shadeB = aB;
                } else {
                    fm.verts[(std::size_t)i].shadeR = 255;
                    fm.verts[(std::size_t)i].shadeG = 255;
                    fm.verts[(std::size_t)i].shadeB = 255;
                }
            }
        }
        if (opt_.dynamicLight)
            ++w6_.litObjects;

        // REAL clip classify (0x5ad614, all six planes) + REAL perspective
        // projection (0x5ac970, the +76 0x80 gate set by the classify pass).
        render::ComputeVertexClipFlags(0x3F, fm.verts.data(), vc,
                                       fm.polys.data(), pc, fr);
        // WAVE-6 W6-INTEGRATE — worldSprites depth fade: when fog is on the engine
        // projects sprite/billboard verts through ProjectBillboardVertices @0x5ac970
        // (the depth-fade alpha arm) instead of the plain mesh arm. The result is
        // identical to ProjectObjectVertices for the screen X/Y; the depth-fade
        // alpha (+0x4F) rides the colorOut copy the affine raster ignores, so this
        // is byte-identical in pixels but exercises the genuine sprite arm. Building
        // meshes are not billboards; this is wired for the sprite-node case the
        // session enables (sprite-render-wave6.md). The mesh arm stays the default.
        render::ProjectObjectVertices(fm.verts.data(), vc, fm.polys.data(), pc,
                                      s, /*projectAll=*/false);

        // materials-wave4 handoff (a): the CITY pass honours the 24-bit material
        // stand-in switch (render/texture.h, default OFF = the engine's
        // level-shaded white default under the missing @0x5da34c palettizer).
        const MatBind* mb = wantTex
            ? bindFor(inst.member, render::Rgb24MaterialStandInEnabled())
            : nullptr;
        frameBinds_[(std::size_t)drawn] = mb;
        if (mb)
            boundMaterialsThisFrame_ += mb->boundCount;

        SceneDrawNode& nd = nodes_[(std::size_t)drawn];
        nd = SceneDrawNode{};
        nd.nodeType = 3;
        nd.cullByte = 0;
        nd.polys = fm.polys.data();
        nd.polyCount = pc;

        lastPolysIn_ += pc;
        if (inst.objectId != 0)
            ++lastObjectInstances_;
        ++drawn;
    }
    if (dbgInst) dbgDone = true;   // one-shot instance debug dump
    lastInstances_ = drawn - lastPersonInstances_ - lastAnimalInstances_;  // city only
    if (drawn == 0)
        return 0;

    // Sibling-chain the nodes and run the REAL walk + per-node dispatch.
    for (int i = 0; i < drawn; ++i)
        nodes_[(std::size_t)i].nextSibling =
            (i + 1 < drawn) ? &nodes_[(std::size_t)i + 1] : nullptr;

    SceneBridgeContext sc{};
    sc.out = render::DrawList{db_.base1, 0, db_.capacity};
    sc.appendCtx.mode = render::NodeAppendMode::Software;
    sc.appendCtx.baseKey = 1;
    WalkSceneTree(&nodes_[0], /*walkMask=*/0x1FF, sc);
    lastNodesDispatched_ = sc.nodesDispatched;
    db_.count = sc.out.count;

    render::RadixSortDrawList(db_, (u32)db_.count, /*twoPassOnly=*/false);
    return db_.count;
}

void CityView3D::doFlush() {
    if (!fb_) {
        lastRasterTris_ = 0;
        return;
    }
    render::MeshList list{db_.base1, db_.count};
    // The real view clip set + the SetupViewTransform reproject scalars (the
    // flush clips straddling polys in view space, then re-projects survivors:
    // screenX = D0C*x/z + D18, screenY = AF8*y/z + D10).
    render::ClipContext ctx{6, reinterpret_cast<const render::ClipPlane*>(clipPlanes_)};
    render::ProjectScalars proj{projScalars_[0], projScalars_[1],
                                projScalars_[2], projScalars_[3]};
    render::SpanDispatch dispatch;
    if (boundMaterialsThisFrame_ > 0) {
        dispatch.slot[4] = &CV3D_SpanTextured;
        dispatch.slot[3] = &CV3D_SpanTextured;
    }
    render::ClipScratch scratch{};
    lastRasterTris_ = render::RasterizeMeshList(list, fb_, dispatch, ctx, proj, scratch);
}

// WAVE-6 W6-INTEGRATE — the WEATHER overlay (frame step #7). Snow + rain are
// screen-space billboard overlays the original composites LAST, over the finished
// 3D frame (VIBE_Snow_Render @0x42b5b0 / VIBE_Rain_Render @0x429c38 run their own
// Begin/EndScene). Gated on Options::weather AND the engine's own season/rain gate:
// SNOW when SnowIsWinterDay(day) (day%4==3) — the winter weather-mode-2 path; RAIN
// when the per-hour weather arc bit0 is set (WeatherUpdate's rainActive). Runs from
// the persistent (seeded-once) CityWeather systems so the field animates
// deterministically across frames.
void CityView3D::doWave6Overlay() {
    w6_.weatherDrops = 0;
    if (!opt_.weather || !fb_)
        return;
    if (!weather_)
        weather_ = std::unique_ptr<CityWeather>(new CityWeather());
    if (!weather_->seeded)
        weather_->Seed(64);          // 64 streaks/flakes (the default field size)

    w6Time_ += 33;                   // ~30fps ms clock seam (dword_62EB38)

    render::SnowViewport vp{};
    vp.x0 = 0; vp.y0 = 0; vp.x1 = fb_->width; vp.y1 = fb_->height;
    render::SnowCamera scam{};
    for (int k = 0; k < 3; ++k) { scam.eye[k] = cam_.eye[k]; scam.anchor[k] = 0.0f; }
    scam.m[0] = 1; scam.m[4] = 1; scam.m[8] = 1;   // identity view basis (overlay)

    const bool winter = render::SnowIsWinterDay(opt_.worldDay);

    if (winter) {
        // SNOW field: header time-interp (0x42b5c9) -> integrate -> build quads ->
        // splat the flake triangles onto the surface.
        float dt = render::SnowRenderStepHeader(weather_->snowHdr, w6Time_);
        weather_->snow.count = weather_->snowHdr.count;
        render::SnowUpdateFlake(weather_->snow, dt, scam, vp);
        int nv = render::SnowBuildQuads(weather_->snow, vp,
                                        weather_->snowVerts.data(),
                                        (int)weather_->snowVerts.size());
        // paint each emitted flake vertex as a white snow pixel (the present layer
        // would additive-blend the TLVERTEX triangles; the observable result is the
        // white flake field).
        for (int i = 0; i < nv; ++i) {
            const render::SnowVertex& sv = weather_->snowVerts[(std::size_t)i];
            int x = (int)(sv.x + 0.5f), y = (int)(sv.y + 0.5f);
            if ((unsigned)x < (unsigned)fb_->width && (unsigned)y < (unsigned)fb_->height)
                render::SurfaceSetPixelRgb(fb_, x, y, 0xE0, 0xE0, 0xFF);
        }
        w6_.weatherDrops = nv / 3;   // 3 verts per flake
    } else {
        // RAIN: the per-hour weather arc gates rain (arc[hour] & 1). With no live
        // weather arc table in this view, drive a single all-rain arc (intensity
        // 200) so the rain gate is active off-winter — the session feeds the real
        // arc; here the gate result drives the integrate+render.
        i32 arc[24]; float wx[24], wy[24];
        for (int h = 0; h < 24; ++h) { arc[h] = 201; wx[h] = -3.0f; wy[h] = 4.0f; }
        render::WeatherFrame wf = render::WeatherUpdate(arc, wx, wy, opt_.worldHour);
        if (wf.rainActive) {
            float dt = 1.0f;   // 0.1 * (now-last); fixed step for the overlay
            // RainUpdateDrop @0x4294d4 now carries the wind direction (windX/windZ,
            // Create-default 1,0) and the per-frame prevAnchor/prevEye snapshots in
            // weather_->rain, used for the camera-motion drift term. The live engine
            // updates windX/windZ via Rain_Render's time-interp; this overlay leaves
            // them at the Create defaults (no live weather-arc wind feed here).
            render::RainUpdateDrop(weather_->rain, dt, scam, vp);
            u32 diffuse = render::RainStreakDiffuse(weather_->rain.count);
            w6_.weatherDrops =
                render::RainRenderToSurface(weather_->rain, vp, diffuse, fb_);
        }
    }
}

CityView3D::Result CityView3D::RenderFrame(const CityCamera3D& cam, const Options& opt) {
    opt_ = opt;
    cam_ = cam;
    Result r;
    r.sceneNodes = (int)scene_.size();
    if (instances_.empty() && persons_.empty())
        return r;

    if (!fb_ || fb_->width != opt.fbW || fb_->height != opt.fbH) {
        if (fb_)
            render::SurfaceDestroy(fb_);
        fb_ = render::SurfaceCreate(opt.fbW, opt.fbH, 16);
    }
    if (!fb_)
        return r;

    // Draw-list pools sized to the whole city's polygon total.
    std::size_t totalPolys = 16;
    int cap = (int)instances_.size();
    if (opt.maxInstances > 0 && opt.maxInstances < cap)
        cap = opt.maxInstances;
    for (int i = 0; i < cap; ++i) {
        render::MeshGeometry* g = src_.Resolve(instances_[(std::size_t)i].member.c_str());
        if (g && g->polyCount > 0)
            totalPolys += (std::size_t)g->polyCount;
    }
    for (const PersonInst& pi : persons_) {
        if (pi.info.member.empty())
            continue;
        render::MeshGeometry* g = src_.Resolve(pi.info.member.c_str());
        if (g && g->polyCount > 0)
            totalPolys += (std::size_t)g->polyCount;
    }
    if (pool1_.size() < totalPolys) {
        pool1_.assign(totalPolys, render::DrawListEntry{});
        pool2_.assign(totalPolys, render::DrawListEntry{});
    }
    db_.base1 = pool1_.data();
    db_.base2 = pool2_.data();
    db_.count = 0;
    db_.capacity = (i32)pool1_.size();

    InstallRealSceneBridge();
    texturedPolysThisFrame_ = 0;

    // WAVE-6 W6-INTEGRATE — frame step #1: the per-frame SUN/day-cycle head
    // (ComputeSunState over the session world clock). Feeds the sky colour the
    // clear uses (doClear), the sun direction the dynamic-light/shadow passes read,
    // and the brightness the shadow/atmos passes gate on. Side-effect-free.
    w6_ = Wave6FrameState{};
    computeSunForFrame();

    // WAVE-6 W6-INTEGRATE — frame fog state: the per-pixel span fog (BlendFog565)
    // reads the process-global render::SpanFog(). Set from this frame's sky/fog
    // colour, gated on Options::fog (default OFF == byte-identical, the span body
    // skips the blend). Restored at frame end. The fog factor is a per-triangle
    // constant first-cut (the per-vertex interpolation is the full 1:1; documented
    // in fog-render-wave6.md) — a mid-range factor so distant tris blend toward fog.
    render::SpanFogState savedFog = render::SpanFog();
    fogState_ = render::FogState{};
    if (opt.fog) {
        render::SpanFogState& sf = render::SpanFog();
        sf.enabled = true;
        sf.color   = w6_.skyDrawn ? (i32)w6_.skyColor
                                  : (i32)(((u32)opt.clearR << 16) |
                                          ((u32)opt.clearG << 8) | opt.clearB);
        sf.factor  = 200;   // [0,255] per-TRIANGLE constant fallback (wave-6); the
                            // per-pixel channel below overrides it for seeded tris.
        w6_.fogApplied = true;

        // WAVE-7 W7-FOGPIX — the genuine D3D fixed-function VERTEX fog: configure the
        // FogState the textured spans use to compute each vertex's per-pixel factor
        // (ComputeFogFactor over the view-space depth). near/far are the scene's
        // (the same range ConfigureFog @0x5ae384 latches from the scene fog far);
        // densitySlope = 255/(far-near). The spans (CV3D_SpanTextured /
        // GroundSpanTextured) seed RgbzVertex::fogFactor from this, and
        // RasterizeTexturedTriangleRgbz interpolates it per pixel & blends BlendFog565.
        const float fogFar = (hasFog_ && fogFar_ > opt.nearZ)
                                 ? fogFar_
                                 : ((opt.farZ > 0.0f) ? opt.farZ : 20000.0f);
        // Fog START: the LIVE engine latches a scene-relative near, NOT the
        // camera near plane — frida in-city read: flt_13FC5AC = 3075,
        // flt_13FC568 = 5080.5 (ratio 0.6052). Fogging from the camera near
        // plane drowned the whole mid-field in sky-blue; the original's ground
        // is fog-free until ~3 km and hazes only toward the horizon.
        const float fogNear = fogFar * (3075.0f / 5080.5f);
        render::ConfigureFog(fogState_, fogNear, fogFar, (i32)sf.color,
                             /*fogEnabledGlobal=*/true, /*featureBit=*/true);
    }

    g_activeCV3D = this;
    render::FrameState fs{};
    fs.engineOn = true;
    fs.hasWorld = true;
    fs.useViewportClear = false;
    fs.clearSuppressed = false;

    render::FrameHooks hooks{};
    hooks.clearRect = &CV3D_ClearRect;
    hooks.sceneWalk = &CV3D_SceneWalk;
    // WAVE-6 W6-INTEGRATE — the BeginUniverseFrame @0x5b3900 subsystem hooks in the
    // engine's exact order: resetLights (frame start), renderParticles (@0x5b3a98,
    // after objects), buildMirrors (@0x5b3af0, after particles). Each is a no-op
    // unless its Options flag is set (and, for particles/mirror, a live system /
    // reflection node exists — named gaps). DEFAULT OFF == byte-identical.
    if (opt.shadows)   hooks.resetLights     = &CV3D_ResetLights;
    if (opt.particles) hooks.renderParticles = &CV3D_RenderParticles;
    if (opt.mirror)    hooks.buildMirrors    = &CV3D_BuildMirrors;
    // GROUND PASS wiring at the VERIFIED position: BeginUniverseFrame @0x5B3900
    // calls hooks.renderTerrain at the 0x5b3a2f arm (frame.cpp fs.hasTerrain
    // branch) — after the clear, before the object walk. fs.hasTerrain mirrors
    // dword_64A028 != 0 (a terrain root exists); Options::terrain (default OFF)
    // is the additive opt-in that keeps every pre-existing frame byte-identical.
    groundDrawn_ = false;
    groundStats_ = GroundRenderStats{};
    if (opt.terrain && ground_.valid()) {
        fs.hasTerrain       = true;
        hooks.terrain       = this;
        hooks.renderTerrain = &CV3D_RenderTerrain;
    }

    render::RenderMainViewFrame(fs, hooks);
    // WAVE-6 W6-INTEGRATE — drop SHADOWS: after terrain, UNDER objects (the engine
    // emits the per-object ground shadow between the terrain arm and the object
    // RasterizeMeshList flush — shadow-render-wave6.md). Splat here, then flush the
    // objects on top.
    doShadows();
    doFlush();
    // WAVE-9 W9-FRAME-ENRICH — the per-frame veg relight + reflective scan (each
    // gated on its Options flag, default OFF == byte-identical). The veg relight
    // recomputes the type-4 foliage intensity bytes (no draw-list effect — a
    // light-cache recompute); the reflective scan consults the detector on the bound
    // meshes and primes the mirror gate when one is reflective.
    doVegRelight();
    doReflectiveScan();
    // WAVE-6 W6-INTEGRATE — the WEATHER overlay (frame step #7): snow/rain
    // composited LAST over the finished 3D frame (screen-space, gated season/rain).
    doWave6Overlay();
    g_activeCV3D = nullptr;
    render::SpanFog() = savedFog;   // restore the global span-fog state

    r.instancesDrawn  = lastInstances_;
    r.objectInstances = lastObjectInstances_;
    r.meshPolysIn     = lastPolysIn_;
    r.appendedPolys   = fs.appendedPolys;
    r.nodesDispatched = lastNodesDispatched_;
    r.rasterTris      = lastRasterTris_;
    r.textured        = boundMaterialsThisFrame_ > 0;
    r.texturedPolys   = texturedPolysThisFrame_;
    r.boundMaterials  = boundMaterialsThisFrame_;
    r.personInstances = lastPersonInstances_;
    r.personPosed     = lastPersonPosed_;
    r.personRestPose  = lastPersonRest_;
    r.terrainDrawn      = groundDrawn_;
    r.terrainTiles      = groundStats_.tilesDrawn;
    r.terrainPolys      = (int)groundStats_.appended;
    r.terrainRasterTris = groundStats_.rasterTris;
    r.waterRegions      = groundFrame_.waterRegionCount();
    r.waterPolys        = groundFrame_.waterPolysAppended();
    r.waterVerts        = groundFrame_.waterVertsBuilt();
    // -- wave-6 world-entity render features ----------------------------------
    r.skyDrawn       = w6_.skyDrawn;
    r.skyColor       = w6_.skyColor;
    r.sunBand        = w6_.sunBand;
    r.sunBrightness  = w6_.sunBrightness;
    r.sunBlend       = w6_.sunBlend;
    r.ambient[0]     = w6_.ambient[0];
    r.ambient[1]     = w6_.ambient[1];
    r.ambient[2]     = w6_.ambient[2];
    r.litObjects     = w6_.litObjects;
    r.sunLitVerts    = w8SunLitVerts_;
    r.sceneLightCount = (int)sceneLights_.lights.size();
    r.lodObjects     = w6_.lodObjects;
    r.shadowCasters  = w6_.shadowCasters;
    r.shadowPixels   = w6_.shadowPixels;
    r.particlePixels = w6_.particlePixels;
    r.particleSystems = liveParticleSystems_;
    r.mirrorPass     = w6_.mirrorPass;
    r.weatherDrops   = w6_.weatherDrops;
    r.fogApplied     = w6_.fogApplied;
    // -- wave-9 frame enrichment ----------------------------------------------
    r.flagObjects      = flagObjectsProduced_;
    r.flagRefreshNodes = flagRefreshNodes_;
    r.vegRelitMeshes   = vegRelitMeshes_;
    r.vegRelitVerts    = vegRelitVerts_;
    r.reflectiveMeshes = reflectiveMeshes_;
    r.animalInstances  = lastAnimalInstances_;

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

bool CityView3D::PresentToDevice(shim::IGraphicsDevice& device) {
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
            render::SurfaceGetPixelRgb(fb_, x, y, px);
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
