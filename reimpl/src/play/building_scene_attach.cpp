// building_scene_attach.cpp — see header for the module charter and the
// placement-fields finding. Every function carries its gilde.exe address.
#include "play/building_scene_attach.h"

#include "crt/printf.h"               // crt::Sprintf            @0x5cba00
#include "io/archive_mount.h"         // io::ArchiveMount        (Groups.BIN)
#include "io/vfs_recon5_worldio.h"    // io::LoadObjectGroup     @0x5e84f4
#include "render/camera_recon.h"      // render::CameraObject/State/Input/Hooks
#include "render/camera_recon2.h"     // render::Camera2State/Input/Hooks
#include "render/camera_update_recon.h" // render::Camera_Update @0x4b4c68
#include "sim/building.h"             // sim::g_buildingTypes (dword_13CE294)
#include "sim/building3.h"            // Building3_LookupTypeName @0x50c738
#include "sim/building5.h"            // Building_FilterBlockedBauplatze @0x50c8ec,
                                      // Building_FindNearestPlotByDistance @0x50cf24
#include "sim/entity.h"               // sim::g_objects (*0x13CE298, stride 169)
#include "sim/object_lifecycle7.h"    // sim::ObjectBuildModelName @0x4ffe0c (REUSED)
#include "sim/object_lifecycle9.h"    // sim::ObjectRebuildModelByOwner @0x5a8140 (see note)
#include "util/math.h"                // util::VectorAngleBetween @0x5ca334
#include "util/math_random.h"         // util::RandomModulo       @0x58b89c
#include "util/matrix.h"              // util::MatrixFromEuler    @0x5cb1bc
#include "util/string_ops.h"          // util::StrToUpper         @0x5e9f50
#include "util/transform.h"           // util::PointThroughBoneChain @0x5c8b38,
                                      // util::RotateVectorByHierarchy @0x5c8990

#include <cmath>
#include <cstring>

namespace guild::play {

// ---------------------------------------------------------------------------
// Recovered constants (get_bytes at the listed addresses).
// ---------------------------------------------------------------------------
const char kGbPrefix[4]         = "gb_";                    // unk_621458
const char kObPrefix[4]         = "ob_";                    // aObS_0 tail @0x620be4
const char kOgrPatternBase[]    = "%sgebaeude/*%s.ogr";     // 0x621474
const char kOgrPatternVariant[] = "%sgebaeude/*%s_%c.ogr";  // 0x62145c
const char kWimpelPattern[]     = "%s*sp_BAU_WIMPEL.ogr";   // 0x6214e0
const char kWimpelAnim[]        = "sonstiges\\sp_WIMPEL.baf"; // 0x6214f8
const float kAxisZ[3]           = {0.0f, 0.0f, 1.0f};       // flt_5CA2B0

namespace {

// Node byte offsets (single source of truth documented in play/object_transform.h
// and sim/object_lifecycle3.h; repeated here as locals for the raw writes).
constexpr int kOffPos      = 76;    // +76  world position (node[19..21])
constexpr int kOffAux92    = 92;
constexpr int kOffField104 = 104;
constexpr int kOffEuler    = 132;   // +132 euler -> matrix +396
constexpr int kOffAux144   = 144;
constexpr int kOffMatrix   = 396;
constexpr int kOffDraw460  = 460;   // ChangeTransparency edx source
constexpr int kOffSibling  = 496;
constexpr int kOffParent   = 504;
constexpr int kOffOwner    = 512;   // saved owner id / record link
constexpr int kOffFlags528 = 528;
constexpr int kOffFlags529 = 529;
constexpr int kOffFlags530 = 530;
constexpr int kOffLod532   = 532;
constexpr int kOffType533  = 533;   // spawn type byte
constexpr int kOffClass535 = 535;   // object class byte
constexpr int kOffField536 = 536;

inline sim::SceneNode3* N(void* p) { return static_cast<sim::SceneNode3*>(p); }

inline u32 F2B(float f) { u32 b; std::memcpy(&b, &f, 4); return b; }

inline i32 LoadI32(const u8* p) { i32 v; std::memcpy(&v, p, 4); return v; }
inline void StoreI32(u8* p, i32 v) { std::memcpy(p, &v, 4); }

// VIBE_Light_SetGrayColorThunk @0x5c6af0 with value 0: a dword-splat memset —
// both 0x50d01c uses pass value 0 (clear the wimpel/plot frames). Pure; inline.
inline void ZeroFill(void* dst, std::size_t bytes) { std::memset(dst, 0, bytes); }

} // namespace

// ===========================================================================
// Hook defaults.
// ===========================================================================

// 0x5af38c — default routes to the REAL sim::ObjectSetPosition (the +76 writes;
// the dirty/shadow scene walks are lc3-hook-gated and inert unless wired).
void BuildingSceneHooks::ObjectSetPosition(void* node, const float pos[3]) {
    if (node) sim::ObjectSetPosition(N(node), pos);
}

// 0x5af50c — VIBE_Object_SetWorldTranslation, 1:1 body over SceneNode3 with the
// REAL util::MatrixFromEuler @0x5cb1bc (sim/object_lifecycle3's twin routes the
// matrix through a process-global hook; this default keeps the matrix build
// in-module so the attach path needs no global hook install).
void BuildingSceneHooks::ObjectSetWorldTranslation(void* nodeP, const float e[3]) {
    if (!nodeP) return;
    sim::SceneNode3* node = N(nodeP);
    const u8 nodeType = node->b(kOffType533);          // v4 = *(a1+533)
    node->f(kOffEuler + 0) = e[0];
    node->f(kOffEuler + 4) = e[1];
    node->f(kOffEuler + 8) = e[2];
    if (nodeType == 3) {                               // camera: negate angles
        float neg[3] = {-node->f(kOffEuler + 0), -node->f(kOffEuler + 4),
                        -node->f(kOffEuler + 8)};
        util::MatrixFromEuler(neg, &node->f(kOffMatrix));
        return;
    }
    util::MatrixFromEuler(&node->f(kOffEuler), &node->f(kOffMatrix));
}

// 0x4b4c68 — default routes to the REAL render::Camera_Update over an owned
// inert default environment: with no camera object bound (dword_13FCD1C null
// path modeled by CameraObject's defaults) it takes the early path and returns
// 0 (camera idle), which is exactly the original's no-camera behavior. Hosts
// with a live camera override this hook.
i32 BuildingSceneHooks::CameraUpdate() {
    static render::CameraObject  obj;
    static render::CameraState   cs;
    static render::Camera2State  st2;
    static render::Camera2Input  in2;
    static render::CameraInput   in1;
    static render::CameraUpdateState us;
    static render::CameraUpdateInput uin;
    static render::CameraHooks   h1;
    static render::Camera2Hooks  h2;
    static render::CameraUpdateHooks uh = render::CameraUpdate_DefaultHooks();
    return render::Camera_Update(obj, cs, st2, in2, in1, us, uin, h1, h2, uh, 0);
}

// 0x438da8 — default sink (hosts route to config/errorlog).
void BuildingSceneHooks::ErrorLogReportMessage(const char* msg) {
    lastError = msg ? msg : "";
}

// ===========================================================================
// Building-type table accessors (dword_13CE294, 589-stride).
// ===========================================================================
u8 BuildingTypeKindDefault(u8 type) {
    if (!sim::g_buildingTypesLoaded || type >= sim::kBuildingTypeCapacity) return 0;
    return *(reinterpret_cast<const u8*>(&sim::g_buildingTypes[type]) + 0);
}
const char* BuildingTypeNameDefault(u8 type) {
    if (!sim::g_buildingTypesLoaded || type >= sim::kBuildingTypeCapacity) return "";
    return reinterpret_cast<const char*>(&sim::g_buildingTypes[type]) + 1;
}
i32 BuildingTypeField579Default(u8 type) {
    if (!sim::g_buildingTypesLoaded || type >= sim::kBuildingTypeCapacity) return 0;
    return LoadI32(reinterpret_cast<const u8*>(&sim::g_buildingTypes[type]) + 579);
}

namespace {
inline u8 EnvTypeKind(const BuildingSceneEnv& e, u8 t) {
    return e.typeKind ? e.typeKind(t) : BuildingTypeKindDefault(t);
}
inline const char* EnvTypeName(const BuildingSceneEnv& e, u8 t) {
    return e.typeName ? e.typeName(t) : BuildingTypeNameDefault(t);
}
inline i32 EnvTypeField579(const BuildingSceneEnv& e, u8 t) {
    return e.typeField579 ? e.typeField579(t) : BuildingTypeField579Default(t);
}
} // namespace

// ===========================================================================
// gilde.exe 0x5e67c8 — VIBE_WorldIo_ReadObject (kind ladder + spawn type rule).
// ===========================================================================
u8 SpawnKindForVersion(i32 kindRaw, u32 version) {
    if (version >= kVerSuspendByte)                 // ver >= 0x3A6C00A6
        return static_cast<u8>(kindRaw);            // v15 = (low byte of) n
    switch (static_cast<i8>(kindRaw)) {             // the old-version ladder
        case 0:  return 0;
        case 1:  return 1;
        case 2:  return 3;
        case 3:
        case 4:  return 4;
        case 5:  return 5;
        case 6:  return 6;
        case 7:  return 7;
        case 8:  return 8;
        default: return 2;
    }
}

// VIBE_Object_Spawn @0x5b054c type-byte rule (kind>=5 -> from name[0]):
// 'r'(0x72)->6, 's'(0x73)->8, >'s'->5, 'p'(0x70)->7, else 5 (the exact ladder
// at 0x5b0584..0x5b0631). NOTE the original then applies a global override at
// LABEL_7 (0x5b05a9): `if (byte_649D54 && type==6) type=5` — byte_649D54 is the
// "no rotating-billboard" engine global; not an input to this pure helper, so it
// is NOT modeled here (BOUNDARY: caller is in the type==6 + flag-clear regime).
u8 SpawnTypeForKind(u8 kind, const char* name) {
    if (kind < 5) return kind;
    const unsigned char c = name && name[0] ? (unsigned char)name[0] : 0;
    if (c >= 0x72) {                 // >= 'r'
        if (c <= 0x72) return 6;     // 'r'
        if (c == 0x73) return 8;     // 's'
        return 5;
    }
    if (c == 0x70) return 7;         // 'p'
    return 5;
}

// ===========================================================================
// gilde.exe 0x5e67c8 — VIBE_WorldIo_ReadObject (full body, recursive framing).
// Each stream read is annotated with the runtime node offset the original
// writes ("->" = destination). Returns this record's node index or -1.
// ===========================================================================
namespace {

// VIBE_Event_LoadEventBindings @0x5f4bc8 stream framing: u32 count, then
// count x (string,string). (Engine binding setup is host state — skipped.)
void SkipEventBindings(render::SceneReader& r) {
    const u32 n = r.ReadDword();
    for (u32 i = 0; i < n && !r.atEnd(); ++i) { r.ReadString(); r.ReadString(); }
}

void ReadVec3Into(render::SceneReader& r, float d[3]) {
    render::SceneVec3 v = r.ReadVec3();
    d[0] = v.x; d[1] = v.y; d[2] = v.z;
}

} // namespace

int ReadCityObjectRecord(render::SceneReader& r, u32 version,
                         std::vector<CityWorldNode>& out, int parent) {
    if (version < render::kSceneVerTooOld)   // a4 < 0x3A6C000B -> LABEL_2 (nothing)
        return -1;
    if (r.atEnd()) return -1;

    CityWorldNode node;
    node.parent = parent;
    bool placeholder = false;                // v122

    const u8 present = r.ReadByte();         // v123 presence byte
    if (present) {
        node.name = r.ReadString();                       // -> node+0
        if (version >= kVerOwnerId)                       // >= 0x3A6C00B2
            node.ownerId = r.ReadDword();                 // v117 -> node+512
        if (version >= kVerClassByte)                     // >= 0x3A6C00AB
            node.classOvr = r.ReadDword();                // v116 -> node+535
        node.kindRaw = static_cast<i32>(r.ReadDword());   // n
        if (version >= kVerSuspendByte) {                 // >= 0x3A6C00A6
            node.suspend = r.ReadByte();                  // v121
            // '!'-prefixed name with suspend==0: strip the '!' (0x5e6c96).
            if (!node.name.empty() && node.name[0] == '!' && node.suspend == 0)
                node.name.erase(0, 1);
        }
        const u8 kind = SpawnKindForVersion(node.kindRaw, version);
        node.spawnType = SpawnTypeForKind(kind, node.name.c_str()); // -> +533

        switch (node.spawnType) {
            case 0: {                                     // light/sfx point node
                r.ReadByte();                             // bit0 -> +529 (ver<A0 forces 1)
                r.ReadDword();                            // -> node+104
                ReadVec3Into(r, node.pos);                // -> node+76
                ReadVec3Into(r, node.aux92);              // -> node+92
                ReadVec3Into(r, node.aux144);             // -> node+144
                break;                                    // -> LABEL_31
            }
            case 1:
            case 4: {                                     // MESH object
                r.ReadByte();                             // bit7 -> +528
                r.ReadByte();                             // bit5 -> +528
                if (version >= 0x3A6C000Du) {
                    r.ReadByte();                         // bit2 -> +529
                    if (version >= kVerSuspendByte) {     // >= 0x3A6C00A6
                        r.ReadByte();                     // bits2..3 -> +530 (<=B8 remap)
                        r.ReadByte();                     // bit4 -> +530
                        if (version >= 0x3A6C00B4u) r.ReadByte(); // bit5 -> +530
                        if (version >= 0x3A6C00B9u) r.ReadByte(); // bit6 -> +530
                    }
                }
                node.lod532 = static_cast<u8>(r.ReadDword());  // -> node+532
                const i32 meshCount = static_cast<i32>(r.ReadDword());
                if (version >= kVerSingleMeshName) {      // >= 0x3A6C00A4
                    if (meshCount > 0) {
                        node.meshName = r.ReadString();   // Mesh_LoadOrFindByName
                        node.hasMesh = true;              // + AttachStockObjectLods
                    }                                     //   (slot 0; miss -> the
                } else {                                  //    "Missing LOD" log)
                    // Older multi-LOD form: count x (string -> slot i*384+244),
                    // then (>=A3) shadow flag byte + optional "_s" string.
                    for (i32 i = 0; i < meshCount && !r.atEnd(); ++i) {
                        std::string nm = r.ReadString();
                        if (i == 0) { node.meshName = nm; node.hasMesh = true; }
                    }
                    if (version >= 0x3A6C00A3u) {
                        if (r.ReadByte()) r.ReadString(); // "_s" shadow LOD name
                    }
                    // (< A3: the "_s" name is derived from draw-data state, no
                    //  stream bytes — engine-state path, nothing to consume.)
                }
                // LABEL_88 tail (shared with dummies):
                ReadVec3Into(r, node.pos);                // @0x5e6f87 -> node+76
                ReadVec3Into(r, node.euler);              // @0x5e6f99 -> node+132
                if (r.ReadByte()) {                       // bit0 -> +529
                    node.hasAux = true;
                    ReadVec3Into(r, node.aux92);          // -> node+92
                    ReadVec3Into(r, node.aux144);         // -> node+144
                }
                if (version >= kVerAnchors) {             // >= 0x3A6C00AF
                    for (int i = 0; i < 10; ++i) {        // @0x5e701d: ecx 0..9
                        r.ReadVec3();                     // -> node+156+24i
                        r.ReadVec3();                     // -> node+168+24i
                    }
                }
                break;
            }
            case 2:
            case 3: {                                     // dummy/locator: LABEL_88
                ReadVec3Into(r, node.pos);                // -> node+76
                ReadVec3Into(r, node.euler);              // -> node+132
                if (r.ReadByte()) {
                    node.hasAux = true;
                    ReadVec3Into(r, node.aux92);          // -> node+92 (camera eye)
                    ReadVec3Into(r, node.aux144);         // -> node+144 (cam look)
                }
                if (version >= kVerAnchors) {
                    for (int i = 0; i < 10; ++i) { r.ReadVec3(); r.ReadVec3(); }
                }
                break;
            }
            case 5: case 6: case 7: case 8: {             // LIGHT node
                node.isLight = true;
                r.ReadByte();                             // bit5 -> +528
                if (version >= 0x3A6C00A8u) {
                    const int lights = (version >= render::kVerLight7) ? 7 : 6;
                    if (version >= 0x3A6C00A9u) r.ReadByte();   // bit4 -> +529
                    u32 p0 = r.ReadDword();               // -> node+144 (range)
                    u32 p1 = r.ReadDword();               // -> node+148 (intensity)
                    u32 p2 = r.ReadDword();               // -> node+152
                    std::memcpy(&node.lightParam[0], &p0, 4);
                    std::memcpy(&node.lightParam[1], &p1, 4);
                    std::memcpy(&node.lightParam[2], &p2, 4);
                    ReadVec3Into(r, node.aux92);          // -> node+92 (colour)
                    ReadVec3Into(r, node.pos);            // -> node+76 (position)
                    ReadVec3Into(r, node.euler);          // -> node+132 (direction)
                    if (version >= 0x3A6C00ACu) { r.ReadDword(); r.ReadDword(); }
                    for (int i = 0; i < lights; ++i) {    // 56-byte keyframe recs
                        r.ReadDword(); r.ReadDword(); r.ReadDword(); // +40/+36/+44
                        r.ReadVec3(); r.ReadVec3(); r.ReadVec3();    // +24/+0/+12
                        if (version >= 0x3A6C00ACu) { r.ReadDword(); r.ReadDword(); }
                    }
                } else {                                  // pre-A8 light form
                    u32 p0 = r.ReadDword();               // -> +132 then moved +144
                    u32 p1 = r.ReadDword();               // -> +136 then moved +148
                    u32 p2 = 0x3F800000u;                 // 1.0f default (<0x3A6C000C)
                    if (version >= 0x3A6C000Cu) p2 = r.ReadDword(); // -> +140 -> +152
                    std::memcpy(&node.lightParam[0], &p0, 4);
                    std::memcpy(&node.lightParam[1], &p1, 4);
                    std::memcpy(&node.lightParam[2], &p2, 4);
                    ReadVec3Into(r, node.aux92);          // -> node+92
                    ReadVec3Into(r, node.pos);            // -> node+76
                    if (version >= 0x3A6C00A1u) {
                        const int lights = (version >= render::kVerLight7) ? 7
                                         : (version >= render::kVerLight6) ? 6 : 4;
                        for (int i = 0; i < lights; ++i) {
                            r.ReadDword(); r.ReadDword(); r.ReadDword();
                            r.ReadVec3(); r.ReadVec3();
                        }
                    }
                }
                break;                                    // -> LABEL_31
            }
            default:
                // "Unkown Object-Type in rd_object()!" (ErrorLog) -> LABEL_31.
                break;
        }
        // LABEL_31 @0x5e6a78: SetWorldTranslation(node, node+132) then
        // SetPosition(node, node+76) — recorded in pos/euler; the live setters
        // run when AttachCityBuildingScene instantiates the SceneNode3.
        // suspend!=0: ToggleSuspendStateNamed + "!" name prefix (0x5e6ab8).
        if (node.suspend && !node.name.empty() && node.name[0] != '!')
            node.name.insert(node.name.begin(), '!');
    } else {
        // Absent record: Object_Spawn(1, "STRANGEFUCK") placeholder (v122 = 1).
        node.present = false;
        node.name = "STRANGEFUCK";
        node.spawnType = 1;
        placeholder = true;
    }

    const int myIdx = static_cast<int>(out.size());
    out.push_back(node);

    // Child / sibling framing (recursive, gated by one byte each).
    const u8 hasChild = r.ReadByte();
    if (hasChild)
        ReadCityObjectRecord(r, version, out, myIdx);     // SetParent(v6, child)
    const u8 hasSibling = r.ReadByte();
    if (hasSibling)
        ReadCityObjectRecord(r, version, out, parent);    // LinkAsSibling(v6, sib)
    if (!hasChild && !hasSibling && placeholder) {
        // Empty leaf placeholder: Memory_FreeDebug(v6) — the node is dropped.
        // (Only safe to pop when no nested record appended after us.)
        if (myIdx == static_cast<int>(out.size()) - 1) out.pop_back();
        else out[myIdx].present = false;
        if (version >= render::kVerEventBindings) SkipEventBindings(r);
        return -1;
    }
    if (version >= render::kVerEventBindings)             // >= 0x3A6C00A7
        SkipEventBindings(r);                             // Event_LoadEventBindings
    return myIdx;
}

// ===========================================================================
// gilde.exe 0x5e7e38 (object-list portion) — ParseCityWorld.
// ===========================================================================
CityWorld ParseCityWorld(const u8* ed3, std::size_t size) {
    CityWorld w;
    render::SceneReader r(ed3, size);
    if (!render::ParseSceneHeader(r, w.header)) return w;
    const u32 ver = w.header.tag;
    const u32 objCount = r.ReadDword();
    for (u32 i = 0; i < objCount && !r.atEnd(); ++i)
        ReadCityObjectRecord(r, ver, w.nodes, -1);
    w.ok = true;
    return w;
}

// ===========================================================================
// gilde.exe 0x5e84f4 — ParseObjectGroupOgr (REUSES io::LoadObjectGroup framing).
// ===========================================================================
namespace {
struct OgrCtx {
    render::SceneReader* r;
    std::vector<CityWorldNode>* out;
};
u32 OgrReadDword(void* stream, bool* ok, void*) {
    auto* r = static_cast<render::SceneReader*>(stream);
    if (ok) *ok = !r->atEnd();
    return r->ReadDword();
}
i32 OgrReadObject(void* stream, i32 /*parent*/, u32 version, i32 /*prevRoot*/,
                  void* ctxP) {
    auto* ctx = static_cast<OgrCtx*>(ctxP);
    const int idx = ReadCityObjectRecord(*static_cast<render::SceneReader*>(stream),
                                         version, *ctx->out, -1);
    return idx >= 0 ? idx + 1 : 0;     // 0 == "no object" token
}
} // namespace

int ParseObjectGroupOgr(const u8* ogr, std::size_t size,
                        std::vector<CityWorldNode>& out) {
    render::SceneReader r(ogr, size);
    OgrCtx ctx{&r, &out};
    io::ObjectGroupHooks h;
    h.readDword  = &OgrReadDword;
    h.readObject = &OgrReadObject;
    h.ctx = &ctx;
    const i32 root = io::LoadObjectGroup(&r, 0, h);
    return root > 0 ? root - 1 : -1;
}

// ===========================================================================
// gilde.exe 0x50cec0 — VIBE_Building_ComputePlacementHeight (eax=out, edx=plot).
// ===========================================================================
void BuildingComputePlacementHeight(float out[3], const u8* plotNode,
                                    BuildingSceneHooks& h) {
    const i32 saved = h.UniverseActiveSlot();             // edi = dword_649D60
    h.UniverseSwitchActiveSlot(0, 1);                     // SwitchActiveSlot(0,1)
    float tmp[3] = {0, 0, 0};
    out[0] = 0; out[1] = 0; out[2] = 0;
    // RotateVectorByHierarchy(plot, +Z, tmp) @0x5c8990 — rotate the +Z axis by
    // the plot node's hierarchy rotation.
    util::RotateVectorByHierarchy(
        const_cast<float*>(reinterpret_cast<const float*>(plotNode)), kAxisZ, tmp);
    // out[1] = VectorAngleBetween(+Z, rotated) @0x5ca334 — the plot's yaw.
    out[1] = static_cast<float>(
        util::VectorAngleBetween(const_cast<float*>(kAxisZ), tmp));
    h.UniverseSwitchActiveSlot(saved, 1);                 // restore slot
}

// ===========================================================================
// gilde.exe 0x50cfd0 — VIBE_Building_AlignMeshToTerrain (eax=node, edx=pos).
// ===========================================================================
void BuildingAlignMeshToTerrain(void* node, const float pos[3],
                                BuildingSceneHooks& h) {
    h.ObjectSetPosition(node, pos);
    h.CollisionResolveMeshAgainstTerrain(node, 1, 0);
    float w[3] = {0, 0, 0};
    util::PointThroughBoneChain(
        reinterpret_cast<float*>(node),
        reinterpret_cast<const float*>(static_cast<u8*>(node) + kOffPos), w);
    if (w[1] < pos[1])                       // terrain pushed it below pos.y
        h.ObjectSetPosition(node, pos);
}

// ===========================================================================
// gilde.exe 0x4ffe0c — VIBE_Object_BuildModelName (eax=node, edx=rec, bl=mode).
// REUSES the existing body reconstruction sim::ObjectBuildModelName
// (src/sim/object_lifecycle7.cpp @0x4ffe0c) — this adapter extracts the record
// fields the lc7 twin takes pre-resolved, trampolines its process-global hooks
// (ParseNameAndBind @0x4ffb40 / Universe_RestoreObjectStates @0x5b43f0) onto
// the BuildingSceneHooks, and performs the two LINK writes the twin leaves to
// its caller:  *(rec+97) = node (0x4ffe93)  and  *(node+512) = rec (0x4ffe4e).
// Links are stored as INDICES (nodeToken / objSlot) — the repo-wide
// pointer-as-index model (see sim/types.h).
// ===========================================================================
namespace {
BuildingSceneEnv* g_activeEnv = nullptr;     // scoped during the delegate calls

int Lc7ParseNameAndBindTramp(sim::SceneNode7* node) {
    return g_activeEnv ? g_activeEnv->hooks->ObjectParseNameAndBind(node) : 1;
}
void Lc7UniverseRestoreTramp(sim::SceneNode7* node, unsigned flag) {
    if (g_activeEnv)
        g_activeEnv->hooks->UniverseRestoreObjectStates(node,
                                                        static_cast<u8>(flag));
}
} // namespace

i32 ObjectBuildModelName(sim::SceneNode3* node, u8* rec, u8 mode, i32 objSlot,
                         i32 nodeToken, BuildingSceneEnv& env) {
    static_assert(sizeof(sim::SceneNode3) == sizeof(sim::SceneNode7),
                  "node blob models must agree (raw[0x21C])");
    BuildingSceneEnv* prev = g_activeEnv;
    g_activeEnv = &env;
    sim::ObjLife7Hooks h7;                    // scoped install (single-threaded
    h7.parseNameAndBind = &Lc7ParseNameAndBindTramp;       // engine globals)
    h7.universeRestoreObjectStates = &Lc7UniverseRestoreTramp;
    sim::ObjLife7SetHooks(h7);
    if (mode == 2)
        StoreI32(rec + 97, nodeToken);        // *(rec+97) = node link (0x4ffe93)
    // mode 1's "ob_%s" name comes from the 65-stride scene-type table
    // (dword_13CE27C) — NOT reconstructed; "" (named gap, see report).
    const char* tname = (mode == 2) ? EnvTypeName(env, rec[0]) : "";
    const i32 result = sim::ObjectBuildModelName(
        reinterpret_cast<sim::SceneNode7*>(node), rec[0], rec[90],
        node->b(kOffType533), rec[0] == 10 ? 1 : 0, mode, tname);
    node->d(kOffOwner) = objSlot;             // *(node+512) = rec (0x4ffe4e)
    sim::ObjLife7ResetHooks();
    g_activeEnv = prev;
    return result;
}

// ===========================================================================
// gilde.exe 0x5a8140 — VIBE_Object_RebuildModelByOwner (eax=node).
// NOTE on reuse: sim::ObjectRebuildModelByOwner (object_lifecycle9.cpp) caches
// *(node+512) BEFORE the row loop; the ORIGINAL re-reads it EVERY iteration —
// observable because BuildModelName overwrites +512 with the record link on
// the first match (so later rows compare against the link, not the saved id).
// This loop keeps the original's re-read semantics 1:1 and routes each bind
// through the lc7-reusing ObjectBuildModelName above.
// ===========================================================================
namespace {
template <typename OnBind>
i32 RebuildModelByOwnerImpl(sim::SceneNode3* node, i32 nodeToken,
                            BuildingSceneEnv& env, OnBind&& onBind) {
    u8* base = reinterpret_cast<u8*>(sim::g_objects);
    for (i32 i = 0; i != 43264; i += 169) {               // 256 * 169 bytes
        u8* rec = base + i;                               // v3 = i + *0x13CE298
        if (rec[0] && node->d(kOffOwner) == LoadI32(rec + 1)) {   // re-read +512
            const i32 slot = i / 169;
            onBind(slot, rec);
            ObjectBuildModelName(node, rec, 2, slot, nodeToken, env);
        }
    }
    return 1;                                             // 0x5a817f
}
} // namespace

i32 ObjectRebuildModelByOwner(sim::SceneNode3* node, i32 nodeToken,
                              BuildingSceneEnv& env) {
    return RebuildModelByOwnerImpl(node, nodeToken, env, [](i32, u8*) {});
}

// ===========================================================================
// The 0x50d01c LABEL_9/LABEL_15 .ogr variant probe (shared with 0x49c19c).
// ===========================================================================
i32 ResolveGebaeudeOgrVariants(const char* upperGbName, BuildingSceneEnv& env,
                               char paths[][kOgrPathSlot], i32 cap) {
    BuildingSceneHooks& h = *env.hooks;
    char pattern[260];
    char resolved[260];
    float counter = 0.0f;                                 // v121 (float)
    i32 count = 0;                                        // v119
    crt::Sprintf(pattern, kOgrPatternBase, env.pathPrefix, upperGbName);
    for (;;) {
        if (h.VfsResolveAndBuildPath(pattern, resolved)) {
            if (count < cap) {                            // v69 slot (96 bytes)
                std::strncpy(paths[count], pattern, kOgrPathSlot - 1);
                paths[count][kOgrPathSlot - 1] = 0;
            }
            ++count;
        } else if ((F2B(counter) & 0x7FFFFFFFu) != 0) {
            break;            // miss after the variants started -> LABEL_26
        }
        // LABEL_15:
        counter += 1.0f;
        if (static_cast<i32>(F2B(counter)) >= 0x43000000) // >= 128.0f
            break;
        if ((F2B(counter) & 0x7FFFFFFFu) == 0) {          // (dead after +=1.0)
            crt::Sprintf(pattern, kOgrPatternBase, env.pathPrefix, upperGbName);
            continue;
        }
        // suffix char: (u8)(int)(counter + 64.0) — Coord_ConvertX truncation.
        const u8 c = static_cast<u8>(static_cast<i32>(counter + kVariantCharBase));
        crt::Sprintf(pattern, kOgrPatternVariant, env.pathPrefix, upperGbName, c);
    }
    return count;
}

// ===========================================================================
// gilde.exe 0x50d01c — VIBE_Building_LoadAndAlignGebaeudeModel.
// ===========================================================================
const u8* BuildingLoadAndAlignGebaeudeModel(i8 buildingType, char* outPath,
                                            BuildingSceneEnv& env) {
    BuildingSceneHooks& h = *env.hooks;

    // --- locals (stack frame of the original) ------------------------------
    static thread_local char paths[kMaxOgrVariants][kOgrPathSlot]; // v69[12288]
    char text1k[1024];                       // v70 (price dialog text)
    void* wimpels[256];                      // v71 (sp_BAU_WIMPEL groups)
    const u8* plots[256];                    // v72 (Bauplatz node frame)
    char text1kB[1024];                      // v73 (duplicate-building dialog)
    char wimpelPath[260];                    // v74
    char nameBuf[258];                       // v76/v77 ("gb_" + TYPE, upper)
    char pattern[260];                       // v78
    char typeNameBuf[64];                    // v79 (plot type name)
    float dragMat[16];                       // v80
    float dragLocal[3] = {0, 0, 0};          // v81..v83
    float dragWorld[3] = {0, 0, 0};          // v84..v86
    float aabbMin[3] = {0, 0, 0};            // v87/v88 block
    float aabbMax[3] = {0, 0, 0};            // v92/v93 block
    float worldPos[3] = {0, 0, 0};           // v89..v91
    float placeEuler[3] = {0, 0, 0};         // v102..v104
    float anchorEuler[3] = {0, 0, 0};        // v98 block
    float eulerArg[3] = {0, 0, 0};           // v99..v101
    i32 screenBounds[4] = {0, 0, 0, 0};      // v94 block

    const u8* chosenPlot = nullptr;          // v109 (the return value)
    void* preview = nullptr;                 // v120 (the dragged building group)
    i32 exitFlag = 0;                        // v117 (never set nonzero — kept 1:1)
    i32 pathCount = 0;                       // v119
    void* nearWimpel = nullptr;              // v115
    const u8* nearestPlot = nullptr;         // v118 (NOTE: first holds the wimpel
                                             //  pointer — the original aliases it;
                                             //  bug-faithful, forces a first seat)
    float bestDist = 1000000000.0f;          // v113 (0x4E6E6B28)
    const i32 hmIdx = env.universeHeightmapIdx;  // v114 = *((u32*)off_649D64+44)
    i32 firstIter = 1;                       // v112
    i32 firstFocus = 1;                      // v108
    // v116 colour state: the ORIGINAL leaves this stack slot UNINITIALIZED (no
    // prologue write; first read @0x50d758). 0 ("no tint yet") is observably
    // identical for any garbage value not in {1,2}.
    i32 colorState = 0;
    i32 randIdx = 0;                         // v110
    float counterF = 0.0f;                   // v121

    const u8 btype = static_cast<u8>(buildingType);

    // --- duplicate-building gate (type kind == 2, e.g. church) @0x50d0bd ---
    if (EnvTypeKind(env, btype) == 2) {
        const u8* it = h.PersonQueryBegin(2, 4, env.cityIndex, 5, 2);
        while (it && *it != btype)
            it = h.PersonIterNext();
        if (it) {
            h.TextRenderFormattedMessage(text1kB, 5125, 14 * btype + 1078, 0, 0);
            if (!h.DialogShowMessageBox(text1kB, 1))
                return nullptr;                            // declined -> 0
        }
    }

    // --- frames + name ------------------------------------------------------
    ZeroFill(plots, sizeof(plots));          // SetGrayColorThunk(0,1024,v72)
    ZeroFill(wimpels, sizeof(wimpels));      // SetGrayColorThunk(0,1024,v71)
    std::strcpy(nameBuf, kGbPrefix);                       // "gb_"
    std::strcat(nameBuf, EnvTypeName(env, btype));         // + table name (+1)
    counterF = 0.0f;
    util::StrToUpper(nameBuf + 2);           // StrToUpper(v77) — from index 2

    // --- the .ogr variant probe (LABEL_9 / LABEL_15) ------------------------
    pathCount = ResolveGebaeudeOgrVariants(nameBuf, env, paths, kMaxOgrVariants);

    if (!pathCount) {                                     // LABEL_26 miss path
        crt::Sprintf(pattern, kOgrPatternBase, env.pathPrefix, nameBuf);
        crt::Sprintf(nameBuf,
                     "lc_GebaeudeAusrichten(): Could not find any 3D-Object-Group "
                     "fitting this building...",
                     pattern);
        h.ErrorLogReportMessage(nameBuf);
        return nullptr;
    }

    randIdx = static_cast<u16>(util::RandomModulo(static_cast<u16>(pathCount)));
    if (!sim::Building3_LookupTypeName(btype, typeNameBuf))  // @0x50c738
        return nullptr;                                    // jz loc_50D2F7

    // --- collect free Bauplätze + plant the wimpels @0x50d370..0x50d546 -----
    // KNOWN DIVERGENCE (root out of this module): the binary passes v79
    // (typeNameBuf, from LookupTypeName @0x50d359) as the walk's wanted-name —
    // CollectFreeBauplatzCandidate @0x50c7b0 matches plot names against it and
    // only falls back to the "bk_" prefix when the name is NULL. The reimpl's
    // sim::Building_FilterBlockedBauplatze (building5.cpp) takes i32 and drops
    // the name (g_bauplatzWant hardcoded nullptr), so every plot is collected
    // instead of only the type-matching ones. Blocked on the sim/building5 API
    // (out-of-chunk); see progress/harden/play_00.md.
    const i32 plotCount = sim::Building_FilterBlockedBauplatze(0, plots, 256);
    placeEuler[0] = placeEuler[1] = placeEuler[2] = 0;
    env.bauplatzGlobal = plots;                            // dword_123343C = v72
    if (plotCount > 0) {
        for (i32 i = 0; i < plotCount; ++i) {
            const u8* plot = plots[i];
            if (!plot) continue;
            // worldPos = plot's world position (frame walk over plot+76).
            util::PointThroughBoneChain(
                const_cast<float*>(reinterpret_cast<const float*>(plot)),
                reinterpret_cast<const float*>(plot + kOffPos), worldPos);
            crt::Sprintf(wimpelPath, kWimpelPattern, env.pathPrefix);
            void* grp = h.SceneLoadObjectGroup(wimpelPath);
            wimpels[i] = grp;
            h.MeshApplyTransformRecursive(grp);
            h.ObjectSetPosition(grp, worldPos);
            h.ObjectChangeTransparency(grp, kTranspWimpel);    // 65664, ecx=128
            h.MeshSetVertexColors(grp, 192, 128, 160);         // edx/ecx/ebx
            const float dx = worldPos[0] - env.viewerPos[0];   // -(13FCD1C+76)
            const float dy = worldPos[1] - env.viewerPos[1];
            const float dz = worldPos[2] - env.viewerPos[2];
            if (std::sqrt(dx * dx + dy * dy + dz * dz) < bestDist) {
                nearWimpel = grp;                              // v115
                nearestPlot = reinterpret_cast<const u8*>(grp);// v118 (aliased!)
                bestDist =
                    static_cast<float>(std::sqrt(dx * dx + dy * dy + dz * dz));
            }
            h.CharacterLoadObjectAnimation(grp, kWimpelAnim, 1);
        }
    }

    if (!nearWimpel) {                                     // @0x50d6d3
        h.DialogShowMessageBox(env.textNoPlot, 0);         // dword_8C86BC
        return nullptr;
    }

    h.LightRefreshAllObjects(1);
    h.InputClearMouseButtonsByMask(2);
    env.keyChar = 50;                                      // byte_67225C = '2'

    // --- the interactive frame loop @0x50d576..0x50dc7c ---------------------
    do {
        if (firstIter) {
            counterF = 0.0f;
            if (preview) {
                sim::SceneNode3* p = N(preview);
                worldPos[0] = p->f(kOffPos + 0);           // v89 = v120[19]
                worldPos[1] = p->f(kOffPos + 4);
                worldPos[2] = p->f(kOffPos + 8);
                placeEuler[0] = p->f(kOffEuler + 0);       // v102 = v120[33]
                placeEuler[1] = p->f(kOffEuler + 4);
                placeEuler[2] = p->f(kOffEuler + 8);
                h.ObjectDetachAndRelease(preview);
            }
            const u16 pick =
                static_cast<u16>(util::RandomModulo(static_cast<u16>(pathCount)));
            std::strcpy(pattern, paths[pick]);             // v78 <- v69[96*pick]
            preview = h.SceneLoadObjectGroup(pattern);
            if (!preview) {
                crt::Sprintf(nameBuf,
                             "lc_GebaeudeAusrichten(): Could not load "
                             "3D-Object-Group '%s'...",
                             pattern);
                h.ErrorLogReportMessage(nameBuf);
                for (i32 i = 0; i < plotCount; ++i)
                    if (wimpels[i]) h.ObjectDetachAndRelease(wimpels[i]);
                return nullptr;
            }
            h.ObjectSetPosition(preview, worldPos);
            h.CollisionResolveMeshAgainstTerrain(preview, 1, 0);
            nearestPlot = sim::Building_FindNearestPlotByDistance(worldPos, plots);
            if (nearestPlot) {
                BuildingComputePlacementHeight(placeEuler, nearestPlot, h);
                h.ObjectSetWorldTranslation(preview, placeEuler);
                h.CollisionResolveMeshAgainstTerrain(preview, 1, 0);
                h.HudSetStatusBannerText(" ");             // asc_621550
                if (colorState != 1) {
                    h.MeshSetVertexColors(preview, 64, 64, 192);
                    colorState = 1;
                }
            } else {
                h.HudSetStatusBannerText(env.textBlocked); // dword_8C86C0
                if (colorState != 2) {
                    h.MeshSetVertexColors(preview, 192, 64, 64);
                    colorState = 2;
                }
            }
            // zero the group transform, take its AABB, then seat it @0x50d787:
            sim::SceneNode3* p = N(preview);
            p->f(kOffEuler + 0) = 0; p->f(kOffEuler + 4) = 0; p->f(kOffEuler + 8) = 0;
            p->f(kOffPos + 0) = 0;   p->f(kOffPos + 4) = 0;   p->f(kOffPos + 8) = 0;
            h.MeshComputeWorldAabb(preview, aabbMin, aabbMax);
            dragLocal[0] = 0.0f;                                       // v81
            dragLocal[1] = static_cast<float>((aabbMax[1] - aabbMin[1]) * kAabbHalf);
            dragLocal[2] = aabbMax[2] + kDragAnchorZ;                  // v83
            p->f(kOffPos + 0) = worldPos[0];               // v120[19..21]
            p->f(kOffPos + 4) = worldPos[1];
            p->f(kOffPos + 8) = worldPos[2];
            p->f(kOffEuler + 0) = placeEuler[0];           // v120[33..35]
            p->f(kOffEuler + 4) = placeEuler[1];
            p->f(kOffEuler + 8) = placeEuler[2];
            firstIter = 0;
        }

        // '2' key / rotate keys: focus camera on the active plot @0x50d863.
        if (env.keyChar == 50 ||
            (env.rotateHeld && (env.activeKey == env.keyRotateA ||
                                env.activeKey == env.keyRotateB))) {
            void* panel = h.MapViewPanelDispatcher(8, env.textPanel);
            if (panel) {
                h.CameraZoomOut(panel);
                util::PointThroughBoneChain(
                    reinterpret_cast<float*>(panel),
                    reinterpret_cast<const float*>(static_cast<u8*>(panel) +
                                                   kOffPos),
                    worldPos);
                BuildingAlignMeshToTerrain(preview, worldPos, h);
            } else if (firstFocus) {
                env.redrawFlag = 1;                        // dword_631614
            }
            firstFocus = 0;
        }

        // nearest plot to the preview's CURRENT position @0x50d8c2.
        i32 placeOk;
        {
            sim::SceneNode3* p = N(preview);
            const u8* plot2 = sim::Building_FindNearestPlotByDistance(
                &p->f(kOffPos), plots);
            if (plot2) {
                if (plot2 != nearestPlot) {
                    nearestPlot = plot2;
                    util::PointThroughBoneChain(
                        const_cast<float*>(reinterpret_cast<const float*>(plot2)),
                        reinterpret_cast<const float*>(plot2 + kOffPos), worldPos);
                    h.ObjectSetPosition(preview, worldPos);
                    BuildingComputePlacementHeight(anchorEuler, plot2, h);
                    h.ObjectSetWorldTranslation(preview, anchorEuler);
                }
                placeOk = 1;
                h.HudSetStatusBannerText(" ");
                if (colorState != 1) {
                    colorState = 1;
                    h.MeshSetVertexColors(preview, 64, 64, 192);
                }
            } else {
                nearestPlot = nullptr;
                placeOk = 0;
                h.HudSetStatusBannerText(env.textBlocked);
                if (colorState != 2) {
                    h.MeshSetVertexColors(preview, 192, 64, 64);
                    colorState = 2;
                }
            }
        }

        if (env.middleClick)                               // dword_672230
            env.redrawFlag = 1;

        // transparency pulse + colour refresh while counterF < 90.0 @0x50d981.
        if (preview && static_cast<i32>(F2B(counterF)) <
                           static_cast<i32>(F2B(kPulseReset))) {
            sim::SceneNode3* p = N(preview);
            p->b(kOffFlags530) &= static_cast<u8>(~2u);
            h.ObjectChangeTransparency(preview, kTranspPreview);  // 65757
            p->b(kOffFlags530) |= 2u;
            if (placeOk && colorState != 1) {
                h.MeshSetVertexColors(preview, 64, 64, 192);
                colorState = 1;
            } else if (!placeOk && colorState != 2) {
                h.MeshSetVertexColors(preview, 192, 64, 64);
                colorState = 2;
            }
            counterF = kPulseReset;                        // 90.0
        }

        // cursor drag (only when the camera is idle) @0x50da2e.
        if (!h.CameraUpdate() && !exitFlag) {
            i32 tileA = 0, tileB = 0;                      // var_5C / var_58
            if (h.HeightmapRaycastFromCursor(hmIdx, env.mouseX16 >> 16,
                                             env.mouseY16 >> 16, &tileA, &tileB) &&
                h.HeightmapTileToWorld(hmIdx, tileA, tileB, worldPos)) {
                h.ObjectSetPosition(preview, worldPos);
                h.CollisionResolveMeshAgainstTerrain(preview, 1, 0);
            }
            h.LightBuildObjectCache(preview);
            sim::SceneNode3* p = N(preview);
            eulerArg[0] = p->f(kOffEuler + 0);             // v99  = v120[33]
            eulerArg[1] = p->f(kOffEuler + 4) + kDragYawPi;// v100 += pi
            eulerArg[2] = p->f(kOffEuler + 8);             // v101 = v120[35]
            util::MatrixFromEuler(eulerArg, dragMat);      // @0x5cb1bc
            dragWorld[0] = dragLocal[0] * dragMat[0] + dragLocal[1] * dragMat[4] +
                           dragLocal[2] * dragMat[8];
            dragWorld[1] = dragLocal[0] * dragMat[1] + dragLocal[1] * dragMat[5] +
                           dragLocal[2] * dragMat[9];
            dragWorld[2] = dragLocal[0] * dragMat[2] + dragLocal[1] * dragMat[6] +
                           dragLocal[2] * dragMat[10];
            h.ObjectComputeScreenBounds(preview, screenBounds);
        }

        // left click on a valid plot: the price dialog @0x50dba8.
        if (env.leftClick && placeOk) {
            const i32 price = h.BuildingComputeSalePrice(env.cityIndex, btype);
            h.TextRenderFormattedMessage(text1k, 5122, price, 14 * btype + 1078,
                                         EnvTypeField579(env, btype)); // +0x243
            if (h.DialogShowMessageBox(text1k, 1))
                chosenPlot = nearestPlot;                  // v109 = v118
            else
                chosenPlot = nullptr;
            env.redrawFlag = 1;
        }

        if (exitFlag && !env.mouseHeld672220 && !env.escFlag)  // @0x50dc48
            exitFlag = 0;
    } while (h.GameLogicRunFrameLoop(kFrameLoopId));       // (eax=self, edx)

    // --- exit paths @0x50dc82 ------------------------------------------------
    if (chosenPlot) {
        std::strcpy(outPath, paths[randIdx]);              // v69[96 * v110] (!)
    } else {
        h.ObjectDetachAndRelease(preview);
        std::strcpy(outPath, "");                          // byte_621554
        preview = nullptr;
    }
    for (i32 i = 0; i < plotCount; ++i)
        if (wimpels[i]) h.ObjectDetachAndRelease(wimpels[i]);
    if (preview) h.ObjectDetachAndRelease(preview);
    return chosenPlot;                                     // v109
}

// ===========================================================================
// WAVE-2 ENTRY — AttachCityBuildingScene.
// ===========================================================================
CityAttachResult AttachCityBuildingScene(const u8* ed3, std::size_t size,
                                         BuildingSceneEnv& env) {
    CityAttachResult R;
    BuildingSceneHooks& h = *env.hooks;

    CityWorld w = ParseCityWorld(ed3, size);
    if (!w.ok) return R;
    R.parsedNodes = static_cast<int>(w.nodes.size());

    // 1. Spawn one engine-layout node per record and seat it with the REAL
    //    setters — the LABEL_31 order of 0x5e67c8: SetWorldTranslation(+132)
    //    then SetPosition(+76).
    R.liveNodes.reserve(w.nodes.size());
    for (const CityWorldNode& src : w.nodes) {
        auto node = std::make_unique<sim::SceneNode3>();
        std::strncpy(reinterpret_cast<char*>(node->raw), src.name.c_str(), 63);
        node->raw[63] = 0;
        node->b(kOffType533)  = src.spawnType;             // VIBE_Object_Spawn
        node->b(kOffClass535) = static_cast<u8>(src.classOvr); // v116 -> +535
        node->d(kOffOwner)    = static_cast<i32>(src.ownerId); // v117 -> +512
        node->b(kOffLod532)   = src.lod532;
        h.ObjectSetWorldTranslation(node.get(), src.euler); // @0x5af50c
        h.ObjectSetPosition(node.get(), src.pos);           // @0x5af38c
        if (src.hasMesh) ++R.meshNodes;
        R.liveNodeNames.push_back(src.name);
        R.liveNodes.push_back(std::move(node));
    }
    R.liveNodeCount = static_cast<int>(R.liveNodes.size());

    // 2. The PostLoadInitScene traversal: RebuildModelByOwner over every node
    //    (gilde.exe 0x5a7f8f: TraverseTree(off_649D64, 0, RebuildModelByOwner, 448)).
    for (std::size_t i = 0; i < R.liveNodes.size(); ++i) {
        sim::SceneNode3* node = R.liveNodes[i].get();
        const u32 savedId = static_cast<u32>(node->d(kOffOwner));
        if (!savedId) continue;                            // no owner record
        bool bound = false;
        AttachedCityObject a;
        RebuildModelByOwnerImpl(node, static_cast<i32>(i), env,
                                [&](i32 slot, u8* rec) {
                                    if (bound) return;     // first match recorded
                                    bound = true;
                                    a.objSlot = slot;
                                    a.typeId = rec[0];
                                });
        if (!bound) continue;
        a.nodeIndex = static_cast<int>(i);
        a.ownerId = savedId;
        a.gbName = reinterpret_cast<const char*>(node->raw);
        a.pos[0] = node->f(kOffPos + 0);
        a.pos[1] = node->f(kOffPos + 4);
        a.pos[2] = node->f(kOffPos + 8);
        a.euler[0] = node->f(kOffEuler + 0);
        a.euler[1] = node->f(kOffEuler + 4);
        a.euler[2] = node->f(kOffEuler + 8);

        // 3. Resolve the gebaeude model: "gb_" + TYPE name, uppercased from
        //    index 2 (0x50d01c's StrToUpper(v77)). When the script-import type
        //    table is not loaded (name empty), the node's PERSISTED name — the
        //    gb_* string the engine's own BuildModelName wrote at save time —
        //    is the same datum; used as the documented portable fallback.
        char nameBuf[258];
        const char* tname = EnvTypeName(env, a.typeId);
        if (tname && tname[0]) {
            std::strcpy(nameBuf, kGbPrefix);
            std::strcat(nameBuf, tname);
        } else if (!w.nodes[i].name.empty() &&
                   w.nodes[i].name.compare(0, 3, kGbPrefix) == 0) {
            std::strncpy(nameBuf, w.nodes[i].name.c_str(), sizeof(nameBuf) - 1);
            nameBuf[sizeof(nameBuf) - 1] = 0;
            a.gbName = nameBuf;                            // keep the real name
        } else {
            R.attached.push_back(a);                       // no resolvable name
            continue;
        }
        util::StrToUpper(nameBuf + 2);

        static thread_local char vpaths[kMaxOgrVariants][kOgrPathSlot];
        const i32 hits = ResolveGebaeudeOgrVariants(nameBuf, env, vpaths,
                                                    kMaxOgrVariants);
        if (hits > 0) {
            a.ogrPattern = vpaths[0];
            char resolved[260];
            if (h.VfsResolveAndBuildPath(vpaths[0], resolved)) {
                a.ogrMember = resolved;
                // Parse the .ogr (0x5e84f4 framing) for the mesh (.bgf) names.
                if (auto* gar = dynamic_cast<GroupsArchiveResolver*>(&h)) {
                    std::vector<u8> bytes;
                    if (gar->LoadMember(resolved, bytes)) {
                        std::vector<CityWorldNode> grp;
                        ParseObjectGroupOgr(bytes.data(), bytes.size(), grp);
                        for (const CityWorldNode& g : grp)
                            if (g.hasMesh) a.meshNames.push_back(g.meshName);
                    }
                }
            }
        }
        R.attached.push_back(a);
    }
    R.ok = true;
    return R;
}

// ===========================================================================
// GroupsArchiveResolver — the real Groups.BIN-backed VFS wildcard resolve.
// Pattern grammar (VIBE_Vfs_ResolveAndBuildPath over the mounted tree):
//   "<prefix>gebaeude/*NAME.ogr" — '*' spans the sub-directory part. Matched
//   case-insensitively against the archive member list (the VFS mounts member
//   paths verbatim; byte_62EB84 case-insensitive mode).
// ===========================================================================
namespace {
void LowerInto(std::string& s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
}
} // namespace

i32 GroupsArchiveResolver::VfsResolveAndBuildPath(const char* pattern,
                                                  char* out256) {
    if (!groups_ || !pattern) return 0;
    std::string pat(pattern);
    LowerInto(pat);
    const std::size_t star = pat.find('*');
    std::string pre = (star == std::string::npos) ? pat : pat.substr(0, star);
    std::string suf = (star == std::string::npos) ? "" : pat.substr(star + 1);
    for (const auto& m : groups_->members()) {
        std::string name = m.name;
        LowerInto(name);
        if (name.size() < pre.size() + suf.size()) continue;
        if (name.compare(0, pre.size(), pre) != 0) continue;
        if (!suf.empty() &&
            name.compare(name.size() - suf.size(), suf.size(), suf) != 0)
            continue;
        if (star == std::string::npos && name != pat) continue;
        std::strncpy(out256, m.name.c_str(), 255);
        out256[255] = 0;
        return 1;
    }
    return 0;
}

bool GroupsArchiveResolver::LoadMember(const char* member, std::vector<u8>& out) {
    return groups_ && groups_->OpenMember(member, out);
}

} // namespace guild::play
