// guild::play — d3 scene object reader + perspective compositor. See header.
#include "play/scene_view.h"

#include "io/archive_mount.h"
#include "play/real_texture_source.h"
#include "render/bgf_loader.h"
#include "render/d3_projection.h"
#include "render/geometry_types.h"
#include "render/light.h"
#include "render/object_anim.h"
#include "render/scene_transform.h"
#include "render/scene_load.h"
#include "render/surface.h"
#include "render/types.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <map>
#include <vector>

namespace guild::play {
namespace {

using render::SceneReader;

// VIBE_Object_Spawn @0x5b054c: for kind>=5 the type byte is chosen from the name's
// first char ('r'->6, 's'->8, 'p'->7, else 5).
int TypeFromName(char c0) {
    const unsigned char v = (unsigned char)c0;
    if (v >= 0x72) {            // >= 'r'
        if (v <= 0x72) return 6;   // 'r'
        if (v == 0x73) return 8;   // 's'
        return 5;
    }
    if (v == 0x70) return 7;       // 'p'
    return 5;
}

// VIBE_Event_LoadEventBindings @0x5f4bc8: u32 count, then count*(string,string).
void SkipEventBindings(SceneReader& r) {
    const u32 n = r.ReadDword();
    for (u32 i = 0; i < n && !r.atEnd(); ++i) { r.ReadString(); r.ReadString(); }
}

// One object record (recursive) for tag 0x3A6C00BB-class scenes — the byte grammar
// of VIBE_WorldIo_ReadObject @0x5e67c8 for this version, body per type.
void ReadObject(SceneReader& r, u32 ver, std::vector<SceneObjectInst>& out, int parentIdx) {
    if (r.atEnd()) return;
    int myIdx = -1;
    const u8 present = r.ReadByte();
    if (present) {
        SceneObjectInst o;
        o.parent = parentIdx;
        o.name = r.ReadString();
        if (ver >= 0x3A6C00B2u) o.ownerId = r.ReadDword();   // +512 owner-object id
        if (ver >= 0x3A6C00ABu) r.ReadDword();   // +535
        const u32 kind = r.ReadDword();
        if (ver >= 0x3A6C00A6u) r.ReadByte();    // flag (rename path ignored here)
        const int type = ((int)kind < 5) ? (int)kind
                                          : TypeFromName(o.name.empty() ? 0 : o.name[0]);
        o.type = type;

        auto readVec = [&](float* d) { render::SceneVec3 v = r.ReadVec3();
                                       d[0] = v.x; d[1] = v.y; d[2] = v.z; };

        if (type == 0) {                          // light / sfx node
            r.ReadByte(); r.ReadDword();
            readVec(o.pos); readVec(o.rot); r.ReadVec3();
        } else if (type == 1 || type == 4) {      // MESH object
            r.ReadByte(); r.ReadByte();
            if (ver >= 0x3A6C000Du) {
                r.ReadByte();
                if (ver >= 0x3A6C00A6u) {
                    r.ReadByte(); r.ReadByte();
                    if (ver >= 0x3A6C00B4u) r.ReadByte();
                    if (ver >= 0x3A6C00B9u) r.ReadByte();
                }
            }
            r.ReadDword();                        // +532
            const u32 lodN = r.ReadDword();
            if ((int)lodN > 0) { o.mesh = r.ReadString(); o.hasMesh = true; }
            readVec(o.pos); readVec(o.euler);     // +76 pos, +132 rotation euler
            if (r.ReadByte()) { readVec(o.rot); r.ReadVec3(); }   // +92 rot, +144
            if (ver >= 0x3A6C00AFu)
                for (int i = 0; i < 10; ++i) { r.ReadVec3(); r.ReadVec3(); }
        } else if (type == 2 || type == 3) {      // dummy / locator
            readVec(o.pos); readVec(o.euler);      // +76 pos, +132 rotation euler
            if (r.ReadByte()) {                    // optional +92 / +144 block
                readVec(o.rot);                    // +92 (camera dummies: the EYE)
                readVec(o.camLook); o.hasCam = true; // +144 (camera look/translation)
            }
            if (ver >= 0x3A6C00AFu)
                for (int i = 0; i < 10; ++i) { r.ReadVec3(); r.ReadVec3(); }
        } else {                                  // 5..8 animated/light/particle
            r.ReadByte();
            if (ver >= 0x3A6C00A9u) r.ReadByte();
            // VIBE_WorldIo_ReadObject light branch (ver >= 0x3A6C00A8): 3 dwords
            // (+144 range, +148 intensity, +152 rangeParam), then colour (+92),
            // position (+76), direction (+132), then the keyframe block.
            u32 p0 = r.ReadDword(), p1 = r.ReadDword(), p2 = r.ReadDword();
            std::memcpy(&o.lightParam[0], &p0, 4);   // +144 range
            std::memcpy(&o.lightParam[1], &p1, 4);   // +148 intensity
            std::memcpy(&o.lightParam[2], &p2, 4);   // +152 rangeParam
            readVec(o.lightColor);    // +92  colour
            readVec(o.pos);           // +76  position
            readVec(o.lightDir);      // +132 direction (type-7 sun)
            o.hasLight = true;
            if (ver >= 0x3A6C00ACu) { r.ReadDword(); r.ReadDword(); }
            const int kf = (ver < 0x3A6C00BAu) ? 6 : 7;
            for (int i = 0; i < kf; ++i) {
                r.ReadDword(); r.ReadDword(); r.ReadDword();
                r.ReadVec3(); r.ReadVec3(); r.ReadVec3();
                if (ver >= 0x3A6C00ACu) { r.ReadDword(); r.ReadDword(); }
            }
        }
        out.push_back(std::move(o));
        myIdx = (int)out.size() - 1;
    }
    if (r.ReadByte()) ReadObject(r, ver, out, myIdx);     // child  -> parent = this node
    if (r.ReadByte()) ReadObject(r, ver, out, parentIdx); // sibling -> parent = this node's parent
    if (ver >= 0x3A6C00A7u) SkipEventBindings(r);
}

struct V3 { float x, y, z; };
inline V3 sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 cross(V3 a, V3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 nrm(V3 a) { float l = std::sqrt(dot(a, a)); return l > 1e-6f ? V3{a.x / l, a.y / l, a.z / l} : a; }

// basename(uppercased, no path/ext) -> archive member path, so a scene's short
// model name resolves to a real .bgf member. First member with a given base wins.
std::map<std::string, std::string> BuildBaseToMember(io::ArchiveMount& objectsBin) {
    std::map<std::string, std::string> byBase;
    for (const auto& m : objectsBin.members()) {
        std::string n = m.name;
        std::size_t sl = n.find_last_of("/\\");
        std::string base = (sl == std::string::npos) ? n : n.substr(sl + 1);
        std::size_t dot = base.rfind('.');
        if (dot != std::string::npos) base = base.substr(0, dot);
        for (auto& c : base) c = (char)std::toupper((unsigned char)c);
        byBase.emplace(base, m.name);
    }
    return byBase;
}

// A scene object's WORLD transform: rotation (Mat3) + position, with the parent chain
// composed (VIBE_Transform_PointThroughBoneChain @0x5c8b38). An object's local->world
// rotation is M^T where M = MatrixFromEuler(euler +132) (the engine dots points with R's
// columns); world = parentWorld o (T(pos) o M^T). Objects are in pre-order so a parent
// is always computed before its children.
struct WorldXf { render::Mat3 rot; V3 pos; };

std::vector<WorldXf> ComputeWorldXforms(const std::vector<SceneObjectInst>& objs) {
    std::vector<WorldXf> w(objs.size());
    for (std::size_t i = 0; i < objs.size(); ++i) {
        const SceneObjectInst& o = objs[i];
        const float e[3] = {o.euler[0], o.euler[1], o.euler[2]};
        const render::Mat3 localRot = render::Transpose(render::MatrixFromEuler(e));
        const float lp[3] = {o.pos[0], o.pos[1], o.pos[2]};
        if (o.parent >= 0 && o.parent < (int)i) {
            const WorldXf& p = w[(std::size_t)o.parent];
            w[i].rot = render::Multiply(p.rot, localRot);
            float rp[3]; render::Apply(p.rot, lp, rp);
            w[i].pos = {rp[0] + p.pos.x, rp[1] + p.pos.y, rp[2] + p.pos.z};
        } else {
            w[i].rot = localRot;
            w[i].pos = {lp[0], lp[1], lp[2]};
        }
    }
    return w;
}

// Transform a local mesh point to world via the object's composed world xform + the
// host-side uniform `scale` (markers): world = wx.rot * (scale*local) + wx.pos.
inline V3 WorldPoint(const WorldXf& wx, float scale, float x, float y, float z) {
    const float lv[3] = {x * scale, y * scale, z * scale};
    float rv[3]; render::Apply(wx.rot, lv, rv);
    return {rv[0] + wx.pos.x, rv[1] + wx.pos.y, rv[2] + wx.pos.z};
}

inline void PutPixel(render::Surface* s, int x, int y, int r, int g, int b) {
    if (s->bpp == 32) {
        auto* row = reinterpret_cast<std::uint32_t*>(
            static_cast<std::uint8_t*>(s->pixels) + (std::size_t)y * s->pitch);
        row[x] = 0xFF000000u | ((std::uint32_t)r << 16) | ((std::uint32_t)g << 8) | (std::uint32_t)b;
    } else {
        auto* row = reinterpret_cast<std::uint16_t*>(
            static_cast<std::uint8_t*>(s->pixels) + (std::size_t)y * s->pitch);
        row[x] = (std::uint16_t)((((r >> 3) & 31) << 11) | (((g >> 2) & 63) << 5) | ((b >> 3) & 31));
    }
}

inline void ReadPixel(const render::Surface* s, int x, int y, int& r, int& g, int& b) {
    if (s->bpp == 32) {
        auto* row = reinterpret_cast<const std::uint32_t*>(
            static_cast<const std::uint8_t*>(s->pixels) + (std::size_t)y * s->pitch);
        const std::uint32_t p = row[x];
        r = (p >> 16) & 0xFF; g = (p >> 8) & 0xFF; b = p & 0xFF;
    } else {
        auto* row = reinterpret_cast<const std::uint16_t*>(
            static_cast<const std::uint8_t*>(s->pixels) + (std::size_t)y * s->pitch);
        const std::uint16_t p = row[x];
        r = ((p >> 11) & 31) << 3; g = ((p >> 5) & 63) << 2; b = (p & 31) << 3;
    }
}

} // namespace

std::vector<SceneObjectInst> ParseSceneObjects(const u8* ed3, std::size_t size, u32* versionOut) {
    std::vector<SceneObjectInst> out;
    if (!ed3 || size < 8) return out;
    SceneReader r(ed3, size);
    render::SceneHeader h;
    if (!render::ParseSceneHeader(r, h)) return out;
    if (versionOut) *versionOut = h.tag;
    const u32 objCount = r.ReadDword();
    for (u32 i = 0; i < objCount && !r.atEnd(); ++i) ReadObject(r, h.tag, out, -1);
    return out;
}

render::SkyAmbient ComputeSceneAmbient(const u8* ed3, std::size_t size,
                                       unsigned band, float frac, float scale, bool* ok) {
    render::SkyAmbient s{0, 0, 0, 0};
    if (ok) *ok = false;
    if (!ed3 || size < 8) return s;
    SceneReader r(ed3, size);
    render::SceneHeader h;
    if (!render::ParseSceneHeader(r, h)) return s;
    if (h.lights.size() < render::kSkyBands) return s;     // no full 7-band rig
    // Each band's ambient colour is SceneLight::pos (flt_13FD1B8/BC/C0).
    render::SkyBandColor bands[render::kSkyBands];
    for (int i = 0; i < render::kSkyBands; ++i)
        bands[i] = {h.lights[i].pos.x, h.lights[i].pos.y, h.lights[i].pos.z};
    if (ok) *ok = true;
    return render::BlendBandLighting(bands, static_cast<int>(band), frac, scale);
}

bool CameraFromDummy(const std::vector<SceneObjectInst>& objs, const char* dummyName,
                     PerspCamera& cam, float fovY) {
    if (!dummyName) return false;
    const SceneObjectInst* d = nullptr;
    for (const auto& o : objs) if (o.name == dummyName) { d = &o; break; }
    if (!d) return false;
    // VIBE_Camera_SetToDummy @0x43fbbc: camera position <- dummy +92 (parsed into
    // `rot`), camera world-translation/look <- dummy +144 (`camLook`). The camera
    // looks from the +92 eye toward the +144 point (up = +Y).
    cam.eye[0] = d->rot[0]; cam.eye[1] = d->rot[1]; cam.eye[2] = d->rot[2];
    cam.target[0] = d->camLook[0]; cam.target[1] = d->camLook[1]; cam.target[2] = d->camLook[2];
    cam.up[0] = 0; cam.up[1] = 1; cam.up[2] = 0;
    cam.fovY = fovY;
    // The real ChooseCity scene (VIBE_Menu_RunChooseCity @0x52e6d8) drives the
    // engine projection (VIBE_Render_SetProjectionTransform @0x5de3e4): fixed
    // ~90 deg horizontal FOV, vertical scaled by aspect. Use it here too (rule 3).
    cam.engineProjection = true;
    return true;
}

bool BuildSceneCamera(const u8* ed3, std::size_t size,
                      const std::vector<SceneObjectInst>& objs, const char* dummyName,
                      PerspCamera& cam, float fovY) {
    if (!dummyName || !ed3 || size < 8) return false;
    // Read the MegaCam orientation (camPos -> camTarget) from the scene header.
    SceneReader r(ed3, size);
    render::SceneHeader h;
    if (!render::ParseSceneHeader(r, h) || !h.hasCamTarget) return false;
    const SceneObjectInst* d = nullptr;
    for (const auto& o : objs) if (o.name == dummyName) { d = &o; break; }
    if (!d) return false;

    (void)h;
    // EXACT camera (VIBE_Camera_SetToDummy @0x43fbbc + SetWorldTranslation @0x5af50c):
    //   eye  = cam[+76]  <- dummy+92  (the `rot` field)
    //   R    = MatrixFromEuler(-(cam[+132]))  with cam[+132] <- dummy+144 (`camLook`)
    // i.e. the camera euler is the NEGATED +144 (in RADIANS), and forward = column 2
    // of that rotation. For dummy_A2 that is a ~180deg-Y + downward look at the map.
    cam.eye[0] = d->rot[0]; cam.eye[1] = d->rot[1]; cam.eye[2] = d->rot[2];
    const float camEuler[3] = {-d->camLook[0], -d->camLook[1], -d->camLook[2]};
    render::Mat3 R = render::MatrixFromEuler(camEuler);
    float fwd3[3]; render::CameraForward(R, fwd3);
    V3 fwd = nrm({fwd3[0], fwd3[1], fwd3[2]});
    cam.target[0] = cam.eye[0] + fwd.x;
    cam.target[1] = cam.eye[1] + fwd.y;
    cam.target[2] = cam.eye[2] + fwd.z;
    cam.up[0] = 0; cam.up[1] = 1; cam.up[2] = 0;
    cam.fovY = fovY;
    cam.engineProjection = true;
    return true;
}

bool CameraFlightAt(const std::vector<SceneObjectInst>& objs,
                    const std::vector<std::string>& dummyNames,
                    float totalTicks, float tick, PerspCamera& cam, float fovY) {
    if (dummyNames.size() < 2) return false;
    // The flight animates two tracks through the waypoints, exactly the fields
    // VIBE_Camera_SetToDummy / the flight setup @0x428a84 read: the camera EYE
    // (dummy +92 = `rot`) and the +144 EULER track (`camLook`). At each sample the
    // camera euler is -(interpolated +144) and forward = column 2 of its rotation
    // matrix (matching the static BuildSceneCamera).
    std::vector<render::ObjAnimFrame> frames;
    frames.reserve(dummyNames.size());
    const float segDur = totalTicks / (float)(dummyNames.size() - 1);
    for (const auto& nm : dummyNames) {
        const SceneObjectInst* d = nullptr;
        for (const auto& o : objs) if (o.name == nm) { d = &o; break; }
        if (!d) return false;
        render::ObjAnimFrame f;
        f.dur = segDur;
        f.pos[0] = d->rot[0];     f.pos[1] = d->rot[1];     f.pos[2] = d->rot[2];      // eye (+92)
        f.rot[0] = d->camLook[0]; f.rot[1] = d->camLook[1]; f.rot[2] = d->camLook[2];  // +144 euler
        frames.push_back(f);
    }
    render::BuildFrameTangents(frames);
    float eye[3], euler144[3];
    render::SampleObjectAnim(frames, tick, eye, euler144);

    const float camEuler[3] = {-euler144[0], -euler144[1], -euler144[2]};
    render::Mat3 R = render::MatrixFromEuler(camEuler);
    float fwd[3]; render::CameraForward(R, fwd);
    cam.eye[0] = eye[0]; cam.eye[1] = eye[1]; cam.eye[2] = eye[2];
    cam.target[0] = eye[0] + fwd[0]; cam.target[1] = eye[1] + fwd[1]; cam.target[2] = eye[2] + fwd[2];
    cam.up[0] = 0; cam.up[1] = 1; cam.up[2] = 0;
    cam.fovY = fovY;
    cam.engineProjection = true;
    return true;
}

int PlaceMeshAtMarkers(std::vector<SceneObjectInst>& objs, const char* markerSubstr,
                       const char* meshBasename, float scale) {
    if (!markerSubstr || !meshBasename) return 0;
    std::string needle = markerSubstr;
    for (auto& c : needle) c = (char)std::toupper((unsigned char)c);
    // Snapshot the markers first (we append while iterating the original range).
    std::vector<SceneObjectInst> towers;
    for (const auto& o : objs) {
        if (!(o.type == 2 || o.type == 3)) continue;   // a locator/dummy
        std::string nm = o.name;
        for (auto& c : nm) c = (char)std::toupper((unsigned char)c);
        if (nm.find(needle) == std::string::npos) continue;
        SceneObjectInst t;
        t.name = std::string("tower:") + o.name;
        t.type = 4;
        t.mesh = meshBasename;
        t.hasMesh = true;
        t.scale = scale;
        t.pos[0] = o.pos[0]; t.pos[1] = o.pos[1]; t.pos[2] = o.pos[2];
        towers.push_back(std::move(t));
    }
    const int n = (int)towers.size();
    for (auto& t : towers) objs.push_back(std::move(t));
    return n;
}

bool IsCityMarkerName(const std::string& name) {
    // VIBE_Util_StrncmpN(picked, "stadt_", 6) == 0 — case-insensitive prefix.
    auto starts = [&](const char* p) {
        std::size_t i = 0;
        for (; p[i]; ++i) {
            if (i >= name.size()) return false;
            if (std::tolower((unsigned char)name[i]) != std::tolower((unsigned char)p[i]))
                return false;
        }
        return true;
    };
    // Real markers are "stadt_<CITY>"; the host PlaceMeshAtMarkers names towers
    // "tower:dummy_<CITY>" — accept both as selectable cities.
    return starts("stadt_") || starts("tower:dummy_");
}

// gilde.exe 0x5b5a38 — VIBE_Pick_FindNearestObjectAt (host reconstruction).
// See header. Projects each mesh object's view-space bounding-sphere centre and
// keeps the nearest object whose projected disc contains the cursor.
int PickNearestObject(const std::vector<SceneObjectInst>& objs,
                      io::ArchiveMount& objectsBin, const PerspCamera& cam,
                      int fbW, int fbH, float cursorX, float cursorY) {
    if (fbW <= 0 || fbH <= 0) return -1;

    // Camera basis (matches RenderSceneObjects): view space x=right,y=up,z=fwd.
    const V3 eye{cam.eye[0], cam.eye[1], cam.eye[2]};
    const V3 fwd = nrm(sub({cam.target[0], cam.target[1], cam.target[2]}, eye));
    const V3 right = nrm(cross({cam.up[0], cam.up[1], cam.up[2]}, fwd));
    const V3 up = cross(fwd, right);

    // flt_13FCD0C (a7) is the projection half-width scale; with the engine's clip
    // window (dvClipWidth=2 over fbW px) that is fbW/2. flt_13FCAF8 == -a7 (Y is
    // flipped). Screen centre == flt_13FCD18/flt_13FCD10 == (fbW/2, fbH/2).
    const float a7 = (float)fbW * 0.5f;
    const float cx = (float)fbW * 0.5f;
    const float cy = (float)fbH * 0.5f;

    std::map<std::string, std::string> byBase = BuildBaseToMember(objectsBin);
    const std::vector<WorldXf> wxf = ComputeWorldXforms(objs);

    int   best = -1;
    float bestD2 = 1.0e10f;            // v12 = 1.0e10 in FindNearestObjectAt

    for (std::size_t i = 0; i < objs.size(); ++i) {
        const SceneObjectInst& o = objs[i];
        if (!(o.type == 1 || o.type == 4) || !o.hasMesh || o.noPick) continue;
        const float osc = (o.scale > 0.0f) ? o.scale : 1.0f;

        std::string key = o.mesh;
        for (auto& c : key) c = (char)std::toupper((unsigned char)c);
        auto it = byBase.find(key);
        if (it == byBase.end()) continue;

        // Local AABB of the model (cached by member path).
        std::vector<u8> bytes;
        if (!objectsBin.OpenMember(it->second.c_str(), bytes) || bytes.empty()) continue;
        render::BgfModel model;
        if (!render::LoadFastChunk(bytes.data(), bytes.size(), model) || model.polyCount == 0) continue;
        render::BgfGeometry geo;
        if (!render::BuildGeometry(model, geo)) continue;
        render::MeshGeometry* g = geo.View();
        if (!g || !g->vertices || g->vertexCount <= 0) continue;

        V3 mn{1e30f, 1e30f, 1e30f}, mx{-1e30f, -1e30f, -1e30f};
        for (int v = 0; v < g->vertexCount; ++v) {
            const render::Vertex& vv = g->vertices[v];
            mn = {std::min(mn.x, vv.x), std::min(mn.y, vv.y), std::min(mn.z, vv.z)};
            mx = {std::max(mx.x, vv.x), std::max(mx.y, vv.y), std::max(mx.z, vv.z)};
        }

        // ComputeBoundingRadius @0x5b2cf8: transform the 8 bbox corners into camera
        // space, average them (x0.125 == flt_6282F4) for the centre, radius = max
        // corner distance from that centre.
        V3 vc[8];
        V3 vsum{0, 0, 0};
        const float xs[2] = {mn.x, mx.x}, ys[2] = {mn.y, mx.y}, zs[2] = {mn.z, mx.z};
        int k = 0;
        for (int ix = 0; ix < 2; ++ix)
            for (int iy = 0; iy < 2; ++iy)
                for (int iz = 0; iz < 2; ++iz) {
                    V3 w = WorldPoint(wxf[i], osc, xs[ix], ys[iy], zs[iz]);
                    V3 p = sub(w, eye);
                    V3 v{dot(p, right), dot(p, up), dot(p, fwd)};
                    vc[k++] = v;
                    vsum = {vsum.x + v.x, vsum.y + v.y, vsum.z + v.z};
                }
        const V3 centre{vsum.x * 0.125f, vsum.y * 0.125f, vsum.z * 0.125f};
        float r2 = 0.0f;
        for (int j = 0; j < 8; ++j) {
            V3 d = sub(vc[j], centre);
            r2 = std::max(r2, dot(d, d));
        }
        const float radius = std::sqrt(r2);

        if (centre.z <= 1e-4f) continue;          // behind the camera (engine: 1/viewZ)
        const float inv = 1.0f / centre.z;
        const float screenX = cx + a7 * centre.x * inv;   // a7*vx/vz + centre
        const float screenY = cy - a7 * centre.y * inv;   // flt_13FCAF8 = -a7
        const float projR = a7 * radius * inv;            // projected radius (px)

        const float dx = screenX - cursorX, dy = screenY - cursorY;
        const float d2 = dx * dx + dy * dy;               // v7 = pixel distance^2
        if (d2 < bestD2 && std::sqrt(d2) < projR) {        // nearer AND within disc
            bestD2 = d2;
            best = (int)i;
        }
    }
    return best;
}

SceneViewStats RenderSceneObjects(const std::vector<SceneObjectInst>& objs,
                                  io::ArchiveMount& objectsBin, const PerspCamera& cam,
                                  render::Surface* fb, const PerspRenderOptions& opt,
                                  RealTextureSource* textures) {
    SceneViewStats st{};
    if (!fb || !fb->pixels) return st;
    const int W = fb->widthPx ? fb->widthPx : fb->width;
    const int H = fb->height;
    if (W <= 0 || H <= 0) return st;
    if (opt.clearFirst) render::SurfaceColorFill(fb, opt.clearR, opt.clearG, opt.clearB);

    // basename(upper) -> archive member path, so a scene's short model name resolves
    // to a real .bgf member ("ob_KARTENTISCH" -> "_DYNAMIC/.../ob_KARTENTISCH.bgf").
    std::map<std::string, std::string> byBase = BuildBaseToMember(objectsBin);

    const V3 eye{cam.eye[0], cam.eye[1], cam.eye[2]};
    const V3 fwd = nrm(sub({cam.target[0], cam.target[1], cam.target[2]}, eye));
    const V3 right = nrm(cross({cam.up[0], cam.up[1], cam.up[2]}, fwd));
    const V3 up = cross(fwd, right);
    const float fproj = 1.0f / std::tan(cam.fovY * 0.5f);
    const float aspect = (float)W / (float)H;
    const V3 light = nrm({opt.lightDir[0], opt.lightDir[1], opt.lightDir[2]});
    // Engine-exact projection (rule 3) when the camera requests it (ChooseCity).
    const render::D3Projection proj =
        render::MakeProjection((float)W, (float)H, cam.nearZ, cam.farZ);

    std::vector<float> zbuf((std::size_t)W * H, 1e30f);

    // One projected, shaded triangle ready to rasterise. Opaque tris raster inline;
    // transparent (blend != 0) tris are DEFERRED and flushed after all opaque + are
    // depth-tested but never write depth (gilde.exe 0x5e0358 VIBE_Render_SetBlendMode).
    struct TriRaster {
        float SX[3], SY[3], SZ[3], area;
        int minx, maxx, miny, maxy;
        std::array<float, 3> VC[3];
        int flatR, flatG, flatB; float shade;
        bool textured; const MaterialTextureTable* tbl; std::size_t pi;
        float uz[3], vz[3], iz[3];
        int blend; float opacity;
    };
    std::vector<TriRaster> deferred;
    auto rasterTri = [&](const TriRaster& T) -> bool {
        const bool transparent = (T.blend != render::kBlendOpaque);
        bool wrote = false;
        for (int y = T.miny; y <= T.maxy; ++y) {
            for (int x = T.minx; x <= T.maxx; ++x) {
                const float px = x + 0.5f, py = y + 0.5f;
                float w0 = ((T.SX[1] - px) * (T.SY[2] - py) - (T.SX[2] - px) * (T.SY[1] - py)) / T.area;
                float w1 = ((T.SX[2] - px) * (T.SY[0] - py) - (T.SX[0] - px) * (T.SY[2] - py)) / T.area;
                float w2 = 1.0f - w0 - w1;
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                const float z = w0 * T.SZ[0] + w1 * T.SZ[1] + w2 * T.SZ[2];
                const std::size_t idx = (std::size_t)y * W + x;
                const float zlimit = transparent ? zbuf[idx] : (zbuf[idx] - z * render::kCoplanarSlack);
                if (z >= zlimit) continue;
                int cr = T.flatR, cg = T.flatG, cb = T.flatB;
                float tintR = 1.0f, tintG = 1.0f, tintB = 1.0f;
                if (opt.bakedLighting) {
                    tintR = (w0 * T.VC[0][0] + w1 * T.VC[1][0] + w2 * T.VC[2][0]) / 255.0f;
                    tintG = (w0 * T.VC[0][1] + w1 * T.VC[1][1] + w2 * T.VC[2][1]) / 255.0f;
                    tintB = (w0 * T.VC[0][2] + w1 * T.VC[1][2] + w2 * T.VC[2][2]) / 255.0f;
                }
                if (T.textured) {
                    const float izp = w0 * T.iz[0] + w1 * T.iz[1] + w2 * T.iz[2];
                    if (izp > 1e-9f) {
                        const float u = (w0 * T.uz[0] + w1 * T.uz[1] + w2 * T.uz[2]) / izp;
                        const float v = (w0 * T.vz[0] + w1 * T.vz[1] + w2 * T.vz[2]) / izp;
                        TexSample s = textures->SamplePoly(*T.tbl, T.pi, u, v);
                        if (s.ok) {
                            if (opt.bakedLighting) {
                                cr = std::min(255, (int)(tintR * s.r));
                                cg = std::min(255, (int)(tintG * s.g));
                                cb = std::min(255, (int)(tintB * s.b));
                            } else {
                                cr = std::min(255, (int)(T.shade * s.r));
                                cg = std::min(255, (int)(T.shade * s.g));
                                cb = std::min(255, (int)(T.shade * s.b));
                            }
                        }
                    }
                } else if (opt.bakedLighting) {
                    cr = std::min(255, (int)(tintR * 255.0f));
                    cg = std::min(255, (int)(tintG * 255.0f));
                    cb = std::min(255, (int)(tintB * 255.0f));
                }
                if (transparent) {
                    int dr, dg, db; ReadPixel(fb, x, y, dr, dg, db);
                    if (T.blend == render::kBlendAdditive) {
                        cr = dr + (int)(cr * T.opacity);
                        cg = dg + (int)(cg * T.opacity);
                        cb = db + (int)(cb * T.opacity);
                    } else {  // kBlendAlpha
                        cr = (int)(dr * (1.0f - T.opacity) + cr * T.opacity);
                        cg = (int)(dg * (1.0f - T.opacity) + cg * T.opacity);
                        cb = (int)(db * (1.0f - T.opacity) + cb * T.opacity);
                    }
                    if (cr > 255) cr = 255; if (cg > 255) cg = 255; if (cb > 255) cb = 255;
                }
                if (!transparent) zbuf[idx] = z;
                PutPixel(fb, x, y, cr, cg, cb);
                ++st.pixelsWritten;
                wrote = true;
            }
        }
        return wrote;
    };

    // Parent-composed world transforms for every object (rule 1: the scene is a tree).
    const std::vector<WorldXf> wxf = ComputeWorldXforms(objs);

    // Baked-lighting setup (rule 3/8): the falloff LUT + the scene's light nodes,
    // split into point lights (type != 7) and the directional "sun" (type 7).
    struct PtLight { float pos[3], color[3], range, intensity, rangeParam; };
    struct DirLight { float dir[3], color[3], intensity; };
    std::vector<PtLight> ptLights;
    std::vector<DirLight> dirLights;
    std::vector<float> falloffLut;
    if (opt.bakedLighting) {
        falloffLut.resize(1024);
        render::BuildFalloffLUT(falloffLut.data());
        for (std::size_t i = 0; i < objs.size(); ++i) {
            const SceneObjectInst& L = objs[i];
            if (!L.hasLight) continue;
            const V3 lp = WorldPoint(wxf[i], 1.0f, 0, 0, 0);  // light world position
            if (L.type == 7) {
                DirLight d;
                d.dir[0] = L.lightDir[0]; d.dir[1] = L.lightDir[1]; d.dir[2] = L.lightDir[2];
                d.color[0] = L.lightColor[0]; d.color[1] = L.lightColor[1]; d.color[2] = L.lightColor[2];
                d.intensity = L.lightParam[1];
                dirLights.push_back(d);
            } else {
                PtLight p;
                p.pos[0] = lp.x; p.pos[1] = lp.y; p.pos[2] = lp.z;
                p.color[0] = L.lightColor[0]; p.color[1] = L.lightColor[1]; p.color[2] = L.lightColor[2];
                p.range = L.lightParam[0]; p.intensity = L.lightParam[1]; p.rangeParam = L.lightParam[2];
                ptLights.push_back(p);
            }
        }
    }

    for (std::size_t oi = 0; oi < objs.size(); ++oi) {
        const SceneObjectInst& o = objs[oi];
        if (!(o.type == 1 || o.type == 4) || !o.hasMesh || o.noRender) continue;
        std::string key = o.mesh;
        for (auto& c : key) c = (char)std::toupper((unsigned char)c);
        auto it = byBase.find(key);
        if (it == byBase.end()) { ++st.meshesMissing; continue; }

        std::vector<u8> bytes;
        if (!objectsBin.OpenMember(it->second.c_str(), bytes) || bytes.empty()) { ++st.meshesMissing; continue; }
        render::BgfModel model;
        if (!render::LoadFastChunk(bytes.data(), bytes.size(), model) || model.polyCount == 0) { ++st.meshesMissing; continue; }
        render::BgfGeometry geo;
        if (!render::BuildGeometry(model, geo)) { ++st.meshesMissing; continue; }
        render::MeshGeometry* g = geo.View();
        if (!g) continue;

        // Resolve per-polygon textures (Textures.BIN) when a source is supplied and
        // BuildGeometry kept the polygon order 1:1 with the model (so poly pi maps
        // to model.polygons[pi]'s material + UVs).
        const MaterialTextureTable* tbl =
            (textures && g->polyCount == model.polyCount)
                ? textures->BuildTableFor(it->second.c_str(), model)
                : nullptr;

        // World transform: the object's parent-composed world xform (euler +132 ->
        // MatrixFromEuler, chained up the parents) + the host marker scale.
        const float sc = (o.scale > 0.0f) ? o.scale : 1.0f;
        const WorldXf& wx = wxf[oi];
        auto world = [&](float x, float y, float z) -> V3 { return WorldPoint(wx, sc, x, y, z); };

        // Per-object baked vertex lighting (VIBE_Light_BuildObjectCache @0x5c8218):
        // world-space vertex positions + smoothed per-vertex normals, then per vertex
        // accum = ambient + each light's contribution, reduced (hue-preserving clamp,
        // ComputeColorShade @0x5c8218 colour branch) to a 0..255 vertex colour the
        // raster Gouraud-interpolates to modulate the surface.
        std::vector<V3> wpos, wnrm;
        std::vector<std::array<float, 3>> vcol;
        if (opt.bakedLighting) {
            const int nv = g->vertexCount;
            wpos.resize(nv); wnrm.assign(nv, V3{0, 0, 0});
            for (int v = 0; v < nv; ++v)
                wpos[v] = world(g->vertices[v].x, g->vertices[v].y, g->vertices[v].z);
            for (int pi = 0; pi < g->polyCount; ++pi) {
                const render::Polygon& p = g->polygons[pi];
                if (!p.v0 || !p.v1 || !p.v2) continue;
                const int i0 = (int)(p.v0 - g->vertices), i1 = (int)(p.v1 - g->vertices),
                          i2 = (int)(p.v2 - g->vertices);
                if (i0 < 0 || i1 < 0 || i2 < 0 || i0 >= nv || i1 >= nv || i2 >= nv) continue;
                V3 fn = cross(sub(wpos[i1], wpos[i0]), sub(wpos[i2], wpos[i0]));
                for (int t : {i0, i1, i2}) { wnrm[t].x += fn.x; wnrm[t].y += fn.y; wnrm[t].z += fn.z; }
            }
            vcol.resize(nv);
            for (int v = 0; v < nv; ++v) {
                wnrm[v] = nrm(wnrm[v]);
                float acc[3] = {opt.ambientRGB[0], opt.ambientRGB[1], opt.ambientRGB[2]};
                const float vp[3] = {wpos[v].x, wpos[v].y, wpos[v].z};
                const float vn[3] = {wnrm[v].x, wnrm[v].y, wnrm[v].z};
                for (const auto& L : ptLights)
                    render::AccumulatePointLight(vp, vn, L.pos, L.color, L.range, L.intensity,
                                                 L.rangeParam, sc, falloffLut.data(), acc);
                for (const auto& L : dirLights)
                    render::AccumulateDirectionalLight(vn, L.dir, L.color, L.intensity, sc,
                                                       falloffLut.data(), acc);
                // hue-preserving clamp: scale by 255/max when the brightest channel >255.
                float m = std::max({acc[0], acc[1], acc[2]});
                if (m > 255.0f) { const float s = 255.0f / m; acc[0] *= s; acc[1] *= s; acc[2] *= s; }
                vcol[v] = {acc[0], acc[1], acc[2]};
            }
        }

        bool drewMesh = false;
        for (int pi = 0; pi < g->polyCount; ++pi) {
            const render::Polygon& p = g->polygons[pi];
            if (!p.v0 || !p.v1 || !p.v2) continue;
            const V3 a = world(p.v0->x, p.v0->y, p.v0->z);
            const V3 b = world(p.v1->x, p.v1->y, p.v1->z);
            const V3 c = world(p.v2->x, p.v2->y, p.v2->z);
            const V3 n = nrm(cross(sub(b, a), sub(c, a)));
            const float shade = opt.ambient + (1.0f - opt.ambient) * std::fabs(dot(n, light));
            // Baked per-vertex colours for Gouraud (the three poly corners).
            std::array<float, 3> VC[3] = {{{0, 0, 0}}, {{0, 0, 0}}, {{0, 0, 0}}};
            if (opt.bakedLighting && !vcol.empty()) {
                const int bi[3] = {(int)(p.v0 - g->vertices), (int)(p.v1 - g->vertices),
                                   (int)(p.v2 - g->vertices)};
                for (int t = 0; t < 3; ++t)
                    if (bi[t] >= 0 && bi[t] < (int)vcol.size()) VC[t] = vcol[bi[t]];
            }
            const V3 P[3] = {a, b, c};
            float SX[3], SY[3], SZ[3];
            for (int i = 0; i < 3; ++i) {
                V3 pp = sub(P[i], eye);
                V3 vc{dot(pp, right), dot(pp, up), dot(pp, fwd)};
                if (vc.z < cam.nearZ) vc.z = cam.nearZ;
                if (cam.engineProjection) {
                    render::ProjectedPoint pj =
                        render::ProjectViewPoint(proj, vc.x, vc.y, vc.z);
                    SX[i] = pj.sx; SY[i] = pj.sy; SZ[i] = vc.z;
                } else {
                    SX[i] = (vc.x * fproj / aspect / vc.z * 0.5f + 0.5f) * W;
                    SY[i] = (0.5f - vc.y * fproj / vc.z * 0.5f) * H;
                    SZ[i] = vc.z;
                }
            }
            const float area = (SX[1] - SX[0]) * (SY[2] - SY[0]) - (SX[2] - SX[0]) * (SY[1] - SY[0]);
            if (std::fabs(area) < 1e-4f) continue;
            if ((opt.backfaceCull == 1 && area < 0.0f) ||
                (opt.backfaceCull == 2 && area > 0.0f)) continue;
            int minx = std::max(0, (int)std::floor(std::min({SX[0], SX[1], SX[2]})));
            int maxx = std::min(W - 1, (int)std::ceil(std::max({SX[0], SX[1], SX[2]})));
            int miny = std::max(0, (int)std::floor(std::min({SY[0], SY[1], SY[2]})));
            int maxy = std::min(H - 1, (int)std::ceil(std::max({SY[0], SY[1], SY[2]})));
            const int flatR = std::min(255, (int)(shade * opt.baseR));
            const int flatG = std::min(255, (int)(shade * opt.baseG));
            const int flatB = std::min(255, (int)(shade * opt.baseB));

            // Per-corner UV (uv0[k]=U, uv1[k]=V) + perspective-correct setup when this
            // polygon has a resolved texture.
            const bool textured = tbl && tbl->PolyTexId((std::size_t)pi) >= 0;
            float uz[3] = {0, 0, 0}, vz[3] = {0, 0, 0}, iz[3] = {0, 0, 0};
            if (textured) {
                const render::BgfPolygon& mp = model.polygons[(std::size_t)pi];
                for (int k = 0; k < 3; ++k) {
                    iz[k] = 1.0f / SZ[k];
                    uz[k] = mp.uv0[k] * iz[k];
                    vz[k] = mp.uv1[k] * iz[k];
                }
            }

            // Material transparency (gilde.exe 0x5F87B8 byte0=mode/byte1=opacity;
            // 0x5e0358 SetBlendMode): mode 2 = additive light shaft / flame glow.
            int blend = render::kBlendOpaque; float opacity = 1.0f;
            {
                const int mi = model.polygons[(std::size_t)pi].matIndex;
                if (mi >= 0 && (std::size_t)mi < model.materials.size()) {
                    const render::BgfMaterial& mat = model.materials[(std::size_t)mi];
                    if (mat.flag == 2) { blend = render::kBlendAdditive; opacity = mat.b1 / 255.0f; }
                    else if (mat.flag == 1) { blend = render::kBlendAlpha; opacity = mat.b1 / 255.0f; }
                }
            }

            TriRaster T;
            for (int k = 0; k < 3; ++k) { T.SX[k]=SX[k]; T.SY[k]=SY[k]; T.SZ[k]=SZ[k];
                                          T.VC[k]=VC[k]; T.uz[k]=uz[k]; T.vz[k]=vz[k]; T.iz[k]=iz[k]; }
            T.area=area; T.minx=minx; T.maxx=maxx; T.miny=miny; T.maxy=maxy;
            T.flatR=flatR; T.flatG=flatG; T.flatB=flatB; T.shade=shade;
            T.textured=textured; T.tbl=tbl; T.pi=(std::size_t)pi;
            T.blend=blend; T.opacity=opacity;

            if (blend == render::kBlendOpaque) {
                if (rasterTri(T)) { ++st.trisDrawn; drewMesh = true; }
            } else {
                deferred.push_back(T);   // flushed after all opaque geometry
            }
        }
        if (drewMesh) ++st.meshesDrawn;
    }

    // Flush deferred transparent (additive/alpha) triangles last (over the opaque image).
    for (const TriRaster& T : deferred)
        if (rasterTri(T)) ++st.trisDrawn;
    return st;
}

void SampleTowerGlide(const float from[3], const float to[3], float frac, float out[3]) {
    if (frac <= 0.0f) { out[0] = from[0]; out[1] = from[1]; out[2] = from[2]; return; }
    if (frac >= 1.0f) { out[0] = to[0];   out[1] = to[1];   out[2] = to[2];   return; }
    // The engine's 2-keyframe object animation (VIBE_Anim_CreateObjectAnim): start
    // transform -> target. BuildFrameTangents leaves the endpoint tangents zero (no
    // interior frames), so SampleObjectAnim's Hermite is the smoothstep ease-in-out.
    std::vector<render::ObjAnimFrame> frames(2);
    const float kDur = 1.0f;
    frames[0].dur = kDur;
    frames[0].pos[0] = from[0]; frames[0].pos[1] = from[1]; frames[0].pos[2] = from[2];
    frames[1].dur = kDur;
    frames[1].pos[0] = to[0];   frames[1].pos[1] = to[1];   frames[1].pos[2] = to[2];
    render::BuildFrameTangents(frames);
    float rot[3];
    render::SampleObjectAnim(frames, frac * kDur, out, rot);
}

void UpdateSceneDrawListCamera(render::Scene3DDrawList& dl, const PerspCamera& cam,
                               int W, int H) {
    const V3 eye{cam.eye[0], cam.eye[1], cam.eye[2]};
    const V3 fwd = nrm(sub({cam.target[0], cam.target[1], cam.target[2]}, eye));
    const V3 right = nrm(cross({cam.up[0], cam.up[1], cam.up[2]}, fwd));
    const V3 up = cross(fwd, right);
    const float fproj = 1.0f / std::tan(cam.fovY * 0.5f);
    const float aspect = (H != 0) ? (float)W / (float)H : 1.0f;
    const render::D3Projection proj =
        render::MakeProjection((float)W, (float)H, cam.nearZ, cam.farZ);
    dl.cam.eye[0] = eye.x; dl.cam.eye[1] = eye.y; dl.cam.eye[2] = eye.z;
    dl.cam.right[0] = right.x; dl.cam.right[1] = right.y; dl.cam.right[2] = right.z;
    dl.cam.up[0] = up.x; dl.cam.up[1] = up.y; dl.cam.up[2] = up.z;
    dl.cam.fwd[0] = fwd.x; dl.cam.fwd[1] = fwd.y; dl.cam.fwd[2] = fwd.z;
    dl.cam.nearZ = cam.nearZ; dl.cam.farZ = cam.farZ; dl.cam.q = proj.q;
    dl.cam.clipX = proj.clipX; dl.cam.clipWidth = proj.clipWidth;
    dl.cam.clipY = proj.clipY; dl.cam.clipHeight = proj.clipHeight;
    dl.cam.originX = proj.originX; dl.cam.originY = proj.originY;
    dl.cam.width = proj.width; dl.cam.height = proj.height;
    dl.cam.engineProjection = cam.engineProjection;
    dl.cam.fproj = fproj; dl.cam.aspect = aspect;
}

render::Scene3DDrawList BuildSceneDrawList(const std::vector<SceneObjectInst>& objs,
                                           io::ArchiveMount& objectsBin,
                                           const PerspCamera& cam, int W, int H,
                                           const PerspRenderOptions& opt,
                                           RealTextureSource* textures) {
    render::Scene3DDrawList dl;
    dl.width = W; dl.height = H;
    dl.backfaceCull = opt.backfaceCull;
    dl.clearFirst = opt.clearFirst;
    dl.clearR = opt.clearR; dl.clearG = opt.clearG; dl.clearB = opt.clearB;
    dl.bilinear = opt.bilinear;
    UpdateSceneDrawListCamera(dl, cam, W, H);
    const V3 light = nrm({opt.lightDir[0], opt.lightDir[1], opt.lightDir[2]});
    if (W <= 0 || H <= 0) return dl;

    std::map<std::string, std::string> byBase = BuildBaseToMember(objectsBin);
    const std::vector<WorldXf> wxf = ComputeWorldXforms(objs);

    // Baked-lighting setup (mirrors RenderSceneObjects).
    struct PtLight { float pos[3], color[3], range, intensity, rangeParam; };
    struct DirLight { float dir[3], color[3], intensity; };
    std::vector<PtLight> ptLights; std::vector<DirLight> dirLights;
    std::vector<float> falloffLut;
    if (opt.bakedLighting) {
        falloffLut.resize(1024);
        render::BuildFalloffLUT(falloffLut.data());
        for (std::size_t i = 0; i < objs.size(); ++i) {
            const SceneObjectInst& L = objs[i];
            if (!L.hasLight) continue;
            const V3 lp = WorldPoint(wxf[i], 1.0f, 0, 0, 0);
            if (L.type == 7) {
                DirLight d; d.dir[0]=L.lightDir[0]; d.dir[1]=L.lightDir[1]; d.dir[2]=L.lightDir[2];
                d.color[0]=L.lightColor[0]; d.color[1]=L.lightColor[1]; d.color[2]=L.lightColor[2];
                d.intensity=L.lightParam[1]; dirLights.push_back(d);
            } else {
                PtLight p; p.pos[0]=lp.x; p.pos[1]=lp.y; p.pos[2]=lp.z;
                p.color[0]=L.lightColor[0]; p.color[1]=L.lightColor[1]; p.color[2]=L.lightColor[2];
                p.range=L.lightParam[0]; p.intensity=L.lightParam[1]; p.rangeParam=L.lightParam[2];
                ptLights.push_back(p);
            }
        }
    }

    // Global texture dedup (by decoded-BMP pointer; the TextureBin caches them).
    std::map<const render::DecodedBmp*, int> texIndex;
    auto getTex = [&](const render::DecodedBmp* bmp) -> int {
        auto it = texIndex.find(bmp);
        if (it != texIndex.end()) return it->second;
        render::SceneDrawTexture t; t.w = bmp->width; t.h = bmp->height;
        t.argb.resize((std::size_t)t.w * t.h);
        // Same representation rule as play::SampleTexel (the per-poly oracle):
        // when the decoded BMP carries quantized indices + palette (24-bit
        // sources soft-palettized at the resolve layer — the original's
        // VIBE_Texture_LoadSoftPalettize @0x5da34c chain), the engine surface
        // content is index -> palette; alpha stays from the rgba expansion.
        const bool pal = bmp->indices.size() >= t.argb.size() &&
                         bmp->palette.size() >= 768;
        for (std::size_t i = 0; i < t.argb.size(); ++i) {
            u8 r, g, b;
            const u8 a = bmp->rgba[i * 4 + 3];
            if (pal) {
                const std::size_t p = (std::size_t)bmp->indices[i] * 3;
                r = bmp->palette[p + 0]; g = bmp->palette[p + 1];
                b = bmp->palette[p + 2];
            } else {
                r = bmp->rgba[i * 4 + 0]; g = bmp->rgba[i * 4 + 1];
                b = bmp->rgba[i * 4 + 2];
            }
            t.argb[i] = ((std::uint32_t)a << 24) | ((std::uint32_t)r << 16) |
                        ((std::uint32_t)g << 8) | (std::uint32_t)b;
        }
        if (opt.bilinear) render::BuildSceneTextureMips(t);   // mip chain for minification AA
        const int id = (int)dl.textures.size();
        dl.textures.push_back(std::move(t)); texIndex[bmp] = id; return id;
    };

    int curTex = -2;   // sentinel so the first poly always opens a batch
    int curBlend = -1; float curOpacity = -1.0f;
    auto emitTri = [&](int texId, const V3 P[3], const std::array<float, 3> col[3],
                       const float U[3], const float Vv[3],
                       int blend = render::kBlendOpaque, float opacity = 1.0f) {
        if (texId != curTex || blend != curBlend || opacity != curOpacity || dl.batches.empty()) {
            render::SceneDrawBatch b; b.firstVertex = (int)dl.verts.size();
            b.vertexCount = 0; b.texId = texId; b.blend = blend; b.opacity = opacity;
            dl.batches.push_back(b); curTex = texId; curBlend = blend; curOpacity = opacity;
        }
        for (int k = 0; k < 3; ++k) {
            render::SceneDrawVertex v;
            v.pos[0] = P[k].x; v.pos[1] = P[k].y; v.pos[2] = P[k].z;
            v.color[0] = col[k][0]; v.color[1] = col[k][1]; v.color[2] = col[k][2];
            v.uv[0] = U[k]; v.uv[1] = Vv[k];
            dl.verts.push_back(v);
        }
        dl.batches.back().vertexCount += 3;
    };

    // gilde.exe 0x5e0358 — transparent (blend != 0) triangles are drawn AFTER all
    // opaque + shadow geometry (depth-tested, no depth write). Defer them here, keyed
    // by (texId, blend, opacity), and flush at the end.
    struct DeferredTri { int texId, blend; float opacity;
                         V3 P[3]; std::array<float,3> col[3]; float U[3], Vv[3]; };
    std::vector<DeferredTri> transparentTris;

    // Projected contact-shadow accumulation (drawn last, as one texId==-2 batch that
    // darkens the receiver). Each caster prop is flattened onto its own base plane
    // along `sdir`; the room shell (walls/ceiling/floor/shelf) only receives.
    std::vector<render::SceneDrawVertex> shadowVerts;
    const V3 sdir = nrm({opt.shadowDir[0], opt.shadowDir[1], opt.shadowDir[2]});
    auto receiverOnly = [](const std::string& nm) {
        std::string u; for (char c : nm) u += (char)std::toupper((unsigned char)c);
        return u.find("WAND") != std::string::npos || u.find("DECKE") != std::string::npos ||
               u.find("RAUM") != std::string::npos || u.find("REGAL") != std::string::npos ||
               u.find("LICHTKEGEL") != std::string::npos;
    };

    for (std::size_t oi = 0; oi < objs.size(); ++oi) {
        const SceneObjectInst& o = objs[oi];
        if (!(o.type == 1 || o.type == 4) || !o.hasMesh || o.noRender) continue;
        std::string key = o.mesh;
        for (auto& c : key) c = (char)std::toupper((unsigned char)c);
        auto it = byBase.find(key);
        if (it == byBase.end()) continue;
        std::vector<u8> bytes;
        if (!objectsBin.OpenMember(it->second.c_str(), bytes) || bytes.empty()) continue;
        render::BgfModel model;
        if (!render::LoadFastChunk(bytes.data(), bytes.size(), model) || model.polyCount == 0) continue;
        render::BgfGeometry geo;
        if (!render::BuildGeometry(model, geo)) continue;
        render::MeshGeometry* g = geo.View();
        if (!g) continue;

        const MaterialTextureTable* tbl =
            (textures && g->polyCount == model.polyCount)
                ? textures->BuildTableFor(it->second.c_str(), model) : nullptr;

        const float sc = (o.scale > 0.0f) ? o.scale : 1.0f;
        const WorldXf& wx = wxf[oi];
        auto world = [&](float x, float y, float z) -> V3 { return WorldPoint(wx, sc, x, y, z); };

        // Shadow caster? Compute the base (resting) plane = the object's lowest world y.
        const bool caster = opt.shadows && sdir.y < -1e-3f && !receiverOnly(o.name);
        float planeY = 0.0f;
        if (caster) {
            float baseY = 1e30f;
            for (int v = 0; v < g->vertexCount; ++v) {
                float wy = world(g->vertices[v].x, g->vertices[v].y, g->vertices[v].z).y;
                if (wy < baseY) baseY = wy;
            }
            planeY = baseY + 0.6f;   // lift just above the surface so it wins the depth test
        }
        const std::array<float, 3> shCol = {opt.shadowAlpha, opt.shadowAlpha, opt.shadowAlpha};

        // Per-object baked vertex colours (same as RenderSceneObjects).
        std::vector<std::array<float, 3>> vcol;
        if (opt.bakedLighting) {
            const int nv = g->vertexCount;
            std::vector<V3> wpos(nv), wnrm(nv, V3{0, 0, 0});
            for (int v = 0; v < nv; ++v)
                wpos[v] = world(g->vertices[v].x, g->vertices[v].y, g->vertices[v].z);
            for (int pi = 0; pi < g->polyCount; ++pi) {
                const render::Polygon& p = g->polygons[pi];
                if (!p.v0 || !p.v1 || !p.v2) continue;
                const int i0=(int)(p.v0-g->vertices), i1=(int)(p.v1-g->vertices), i2=(int)(p.v2-g->vertices);
                if (i0<0||i1<0||i2<0||i0>=nv||i1>=nv||i2>=nv) continue;
                V3 fn = cross(sub(wpos[i1], wpos[i0]), sub(wpos[i2], wpos[i0]));
                for (int t : {i0, i1, i2}) { wnrm[t].x+=fn.x; wnrm[t].y+=fn.y; wnrm[t].z+=fn.z; }
            }
            vcol.resize(nv);
            for (int v = 0; v < nv; ++v) {
                wnrm[v] = nrm(wnrm[v]);
                float acc[3] = {opt.ambientRGB[0], opt.ambientRGB[1], opt.ambientRGB[2]};
                const float vp[3]={wpos[v].x,wpos[v].y,wpos[v].z};
                const float vn[3]={wnrm[v].x,wnrm[v].y,wnrm[v].z};
                for (const auto& L : ptLights)
                    render::AccumulatePointLight(vp, vn, L.pos, L.color, L.range, L.intensity, L.rangeParam, sc, falloffLut.data(), acc);
                for (const auto& L : dirLights)
                    render::AccumulateDirectionalLight(vn, L.dir, L.color, L.intensity, sc, falloffLut.data(), acc);
                float m = std::max({acc[0], acc[1], acc[2]});
                if (m > 255.0f) { const float s = 255.0f/m; acc[0]*=s; acc[1]*=s; acc[2]*=s; }
                vcol[v] = {acc[0], acc[1], acc[2]};
            }
        }

        for (int pi = 0; pi < g->polyCount; ++pi) {
            const render::Polygon& p = g->polygons[pi];
            if (!p.v0 || !p.v1 || !p.v2) continue;
            const V3 a = world(p.v0->x, p.v0->y, p.v0->z);
            const V3 b = world(p.v1->x, p.v1->y, p.v1->z);
            const V3 c = world(p.v2->x, p.v2->y, p.v2->z);
            const V3 P[3] = {a, b, c};
            const V3 n = nrm(cross(sub(b, a), sub(c, a)));
            const float shade = opt.ambient + (1.0f - opt.ambient) * std::fabs(dot(n, light));

            // Resolve texture (global id).
            int texId = -1; float U[3] = {0,0,0}, Vv[3] = {0,0,0};
            if (tbl && tbl->PolyTexId((std::size_t)pi) >= 0) {
                const render::DecodedBmp* bmp = tbl->TextureFor(tbl->PolyTexId((std::size_t)pi));
                if (bmp && bmp->ok && bmp->width > 0 && !bmp->rgba.empty()) {
                    texId = getTex(bmp);
                    const render::BgfPolygon& mp = model.polygons[(std::size_t)pi];
                    for (int k = 0; k < 3; ++k) { U[k] = mp.uv0[k]; Vv[k] = mp.uv1[k]; }
                }
            }

            // Material transparency (gilde.exe 0x5F87B8 material byte0=mode, byte1=opacity;
            // 0x5e0358 SetBlendMode). mode 2 = additive glow (window light shaft / flame).
            int blend = render::kBlendOpaque; float opacity = 1.0f;
            {
                const int mi = model.polygons[(std::size_t)pi].matIndex;
                if (mi >= 0 && (std::size_t)mi < model.materials.size()) {
                    const render::BgfMaterial& mat = model.materials[(std::size_t)mi];
                    if (mat.flag == 2) { blend = render::kBlendAdditive; opacity = mat.b1 / 255.0f; }
                    else if (mat.flag == 1) { blend = render::kBlendAlpha; opacity = mat.b1 / 255.0f; }
                }
            }

            // Per-vertex colour modulator (folds baked/flat × textured/untextured).
            std::array<float, 3> col[3];
            if (opt.bakedLighting && !vcol.empty()) {
                const int bi[3] = {(int)(p.v0-g->vertices), (int)(p.v1-g->vertices), (int)(p.v2-g->vertices)};
                for (int k = 0; k < 3; ++k) {
                    const std::array<float,3>& vc =
                        (bi[k] >= 0 && bi[k] < (int)vcol.size()) ? vcol[bi[k]] : std::array<float,3>{0,0,0};
                    col[k] = {vc[0]/255.0f, vc[1]/255.0f, vc[2]/255.0f};
                }
            } else if (texId >= 0) {
                for (int k = 0; k < 3; ++k) col[k] = {shade, shade, shade};   // flat textured: grey shade
            } else {
                const std::array<float,3> fc = {shade*opt.baseR/255.0f, shade*opt.baseG/255.0f, shade*opt.baseB/255.0f};
                for (int k = 0; k < 3; ++k) col[k] = fc;                       // flat untextured: shade*base
            }
            if (blend == render::kBlendOpaque) {
                emitTri(texId, P, col, U, Vv);
            } else {
                DeferredTri d; d.texId = texId; d.blend = blend; d.opacity = opacity;
                for (int k = 0; k < 3; ++k) { d.P[k] = P[k]; d.col[k] = col[k];
                                              d.U[k] = U[k]; d.Vv[k] = Vv[k]; }
                transparentTris.push_back(d);
            }

            // Flatten this triangle onto the base plane along the light dir -> shadow.
            // Transparent (additive) polys never cast a shadow.
            if (caster && blend == render::kBlendOpaque) {
                const float zero[3] = {0, 0, 0};
                (void)zero;
                for (int k = 0; k < 3; ++k) {
                    const float t = (planeY - P[k].y) / sdir.y;   // sdir.y < 0
                    render::SceneDrawVertex sv;
                    sv.pos[0] = P[k].x + sdir.x * t;
                    sv.pos[1] = planeY;
                    sv.pos[2] = P[k].z + sdir.z * t;
                    sv.color[0] = shCol[0]; sv.color[1] = shCol[1]; sv.color[2] = shCol[2];
                    sv.uv[0] = 0; sv.uv[1] = 0;
                    shadowVerts.push_back(sv);
                }
            }
        }
    }

    // Append the shadow geometry as one darkening batch (texId == -2), drawn after all
    // opaque geometry so it dims the receivers it lands on.
    if (!shadowVerts.empty()) {
        render::SceneDrawBatch b;
        b.firstVertex = (int)dl.verts.size();
        b.vertexCount = (int)shadowVerts.size();
        b.texId = render::kSceneShadowBatch;
        dl.verts.insert(dl.verts.end(), shadowVerts.begin(), shadowVerts.end());
        dl.batches.push_back(b);
    }

    // Flush deferred transparent geometry LAST (after opaque + shadows), as additive/
    // alpha batches (gilde.exe 0x5e0358: depth-tested, no depth write). curTex/curBlend
    // are reset so the first transparent tri always opens its own batch.
    curTex = -2; curBlend = -1; curOpacity = -1.0f;
    for (const DeferredTri& d : transparentTris)
        emitTri(d.texId, d.P, d.col, d.U, d.Vv, d.blend, d.opacity);

    return dl;
}

} // namespace guild::play
