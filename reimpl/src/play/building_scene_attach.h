#pragma once
// =============================================================================
// guild::play — BUILDING SCENE ATTACH (city objects -> real 3D scene presence).
//
// Reconstructs, 1:1 from gilde.exe, the chain by which loaded city (.cty) world
// objects obtain their REAL 3D scene presence — world POSITION + MODEL attached
// to the scene graph — plus the interactive building-placement loop:
//
//   0x50d01c  VIBE_Building_LoadAndAlignGebaeudeModel  ("lc_GebaeudeAusrichten")
//   0x50cec0  VIBE_Building_ComputePlacementHeight     (plot yaw -> placement euler)
//   0x50cfd0  VIBE_Building_AlignMeshToTerrain         (seat + terrain clamp)
//   0x4ffe0c  VIBE_Object_BuildModelName               ("gb_%s"/"ob_%s" bind)
//   0x5a8140  VIBE_Object_RebuildModelByOwner          (.cty record -> scene node)
//   0x5e67c8  VIBE_WorldIo_ReadObject                  (full body: the field->offset
//             map of the scene-file object record, incl. the +512 owner id the
//             play::ParseSceneObjects view drops)
//
// =============================================================================
// THE PLACEMENT-FIELDS FINDING (the key question, with IDA evidence)
// =============================================================================
// WHERE do the original's city objects get their world transform?
//
// 1. The city's 3D world is a SCENE STREAM in the .ed3 grammar (tag 0x3A6C00BB
//    for the shipped cities). For a loaded city it is EMBEDDED IN THE .cty
//    ITSELF: VIBE_Save_LoadGameFile @0x5a7604 reads the save tables, then
//    VIBE_Save_PostLoadInitScene @0x5a7ef8 hands the STILL-OPEN save stream to
//    VIBE_Scene_LoadFromStream @0x5e7e38 (edx = stream), which reads one
//    VIBE_WorldIo_ReadObject @0x5e67c8 record per scene object. Verified on
//    AUGSBURG.cty: gunzipped offset 0x1b9c2 carries the 0x3A6C00BB scene whose
//    56 owner-tagged nodes match the 55+1 alive g_objects records (ids 1, 14,
//    27, 34, 41, ...). io::LoadWorldEx captures exactly this blob. The
//    Resources/scenes.BIN member "Staedte/stadt_<CITY>.ed3" is the SAME scene
//    in template form — identical node names/positions but ALL owner ids 0
//    (it seeds new scenarios; RebuildModelByOwner finds nothing to bind there).
//
// 2. Record fields -> runtime scene-node offsets (recovered byte-for-byte from
//    the 0x5e67c8 disasm; "node" is the 540-byte render node VIBE_Object_Spawn
//    @0x5b054c allocates — modeled portably by sim::SceneNode3):
//        presence  : u8                    (0 -> "STRANGEFUCK" placeholder node)
//        name      : cstr                  -> node+0   (name buffer)
//        ownerId   : u32  (ver>=0x3A6C00B2)-> node+512 (the .cty OBJECT RECORD ID)
//        classOvr  : u32  (ver>=0x3A6C00AB)-> node+535 (object class byte)
//        kind      : u32                   -> spawn kind (maps to type byte +533)
//        suspend   : u8   (ver>=0x3A6C00A6)
//      mesh body (type 1/4), after the version-gated flag bytes (+528/529/530):
//        lod532    : u32                   -> node+532 (low byte)
//        meshCount : u32 ; meshName : cstr -> VIBE_Mesh_LoadOrFindByName @0x5d345c
//                                             + VIBE_Mesh_AttachStockObjectLods
//                                             @0x5d1824 (slot 0 of drawdata +492)
//        POSITION  : vec3 (12 raw LE floats) -> node+76  (file @0x5e6f87)
//        ROTATION  : vec3 (euler, radians)   -> node+132 (file @0x5e6f99)
//        auxFlag   : u8 -> bit0 of node+529; if set: vec3 -> node+92, vec3 -> node+144
//        anchors   : (ver>=0x3A6C00AF) 10 x { vec3 -> node+156+24i, vec3 -> node+168+24i }
//      then for EVERY record type (LABEL_31 @0x5e6a78):
//        VIBE_Object_SetWorldTranslation @0x5af50c (node, node+132)
//            -> writes euler to +132/136/140 and builds the 16-float rotation
//               frame matrix at node+396 via VIBE_Math_MatrixFromEuler @0x5cb1bc
//        VIBE_Object_SetPosition @0x5af38c (node, node+76)
//            -> writes the WORLD POSITION dwords node[19..21] == +76/+80/+84
//    So: scene-node +76 = world position, +132 = euler, +396 = rotation matrix —
//    all sourced from the per-object vec3 fields of the .ed3 object record.
//
// 3. The SIM RECORDS link to those nodes BY ID: VIBE_Save_PostLoadInitScene
//    runs VIBE_SceneGraph_TraverseTree(off_649D64, 0, VIBE_Object_RebuildModelByOwner,
//    448): for every scene node, 0x5a8140 scans the 169-stride object/building
//    array (*0x13CE298 == sim::g_objects, 256 slots, 43264 bytes):
//        if (rec.alive /*type byte +0*/ && *(u32*)(node+512) == *(u32*)(rec+1))
//            VIBE_Object_BuildModelName(node, rec, 2);
//    0x4ffe0c mode 2 then renames the node "gb_<typeName>" (type-table name at
//    dword_13CE294 + 589*type + 1), sets node class +535 = 3 (type 10 -> 0,
//    +536 = 0), stores the scene-node pointer into rec+97 and the record pointer
//    into node+512, and VIBE_Object_ParseNameAndBind @0x4ffb40 resolves the
//    "gb_*" model. The MODEL itself is the object group
//    "<prefix>gebaeude/*<TYPENAME>.ogr" (+ variants "_A".."): the same VFS
//    wildcard loop 0x50d01c and VIBE_Command_ExCreateGebaeude @0x49c19c run; the
//    .ogr (Resources/Groups.BIN, magic 0x3A6C00xx via VIBE_Scene_LoadObjectGroup
//    @0x5e84f4) contains WorldIo records whose meshName fields are the .bgf
//    members of Resources/Objects.BIN.
//
// 4. NEW buildings placed in play take their transform from a BAUPLATZ plot
//    node: 0x50d01c computes world pos = PointThroughBoneChain(plot, plot+76)
//    @0x5c8b38 and euler = (0, angle((0,0,1), plotRotation*(0,0,1)), 0) via
//    0x50cec0, then SetPosition/SetWorldTranslation on the preview node; the
//    chosen plot + .ogr path feed VIBE_Command_ExCreateGebaeude @0x49c19c which
//    repeats SetPosition(+25 vec3 of the command)/SetWorldTranslation(+41)/
//    AlignMeshToTerrain and binds via BuildModelName.
//
// REUSED reconstructions (NOT redefined here):
//   sim::SceneNode3 / ObjectSetPosition            (sim/object_lifecycle3, 0x5af38c)
//   sim::Building_FilterBlockedBauplatze @0x50c8ec, Building_FindNearestPlotByDistance
//        @0x50cf24                                  (sim/building5)
//   sim::Building3_LookupTypeName @0x50c738         (sim/building3)
//   util::PointThroughBoneChain @0x5c8b38, RotateVectorByHierarchy @0x5c8990,
//   util::MatrixFromEuler @0x5cb1bc, VectorAngleBetween @0x5ca334,
//   util::RandomModulo @0x58b89c, StrToUpper @0x5e9f50, crt::Sprintf @0x5cba00
//   io::LoadObjectGroup @0x5e84f4                   (io/vfs_recon5_worldio)
//   play::ParseSceneObjects                          (play/scene_view; the .ed3 list)
//   render::Camera_Update @0x4b4c68                  (render/camera_update_recon;
//        the default cameraUpdate hook routes there over an inert default env)
//
// NOT reconstructed (named gaps; routed through BuildingSceneHooks, inert
// defaults — see progress/building-scene-attach.md):
//   0x4ffb40 VIBE_Object_ParseNameAndBind, 0x5e84f4's live VFS open (the byte
//   source is injected), 0x429070 Mesh_ApplyTransformRecursive*, 0x5b2710
//   Object_ChangeTransparency, 0x428928 Mesh_SetVertexColors, 0x426488
//   Character_LoadObjectAnimation, 0x5c886c/0x5c8218 Light_*, 0x40dca8
//   Input_ClearMouseButtonsByMask, 0x5b4258 Object_DetachAndRelease, 0x427b60
//   Collision_ResolveMeshAgainstTerrain, 0x4bcdcc Hud_SetStatusBannerText,
//   0x5441d0 MapView_PanelDispatcher, 0x4b5974 Camera_ZoomOut, 0x5c67b8/0x5c65d4
//   Heightmap_*, 0x5b5c70 Object_ComputeScreenBounds, 0x4284f4
//   Mesh_ComputeWorldAabb, 0x591480 Building_ComputeSalePrice, 0x586c20/0x586a6c
//   Person_QueryBegin/IterNext, 0x4ad6f0 Dialog_ShowMessageBox, 0x59f99c
//   Text_RenderFormattedMessage, 0x4c09a0 GameLogic_RunFrameLoop, 0x5b43f0
//   Universe_RestoreObjectStates, 0x5b4a24 Universe_SwitchActiveSlot, 0x4500a0's
//   live VFS tree (the resolver is injected; BindGroupsArchiveResolver supplies
//   the real Groups.BIN-backed resolve).
//   (* some have real reconstructions elsewhere but over disjoint state models;
//      hosts wire them through these hooks — rule 13 wiring lives with the host.)
// =============================================================================
#include "guild/common/types.h"
#include "render/scene_load.h"        // render::SceneReader / ParseSceneHeader
#include "sim/object_lifecycle3.h"    // sim::SceneNode3 (+76/+132/+396 layout)
#include "sim/types.h"                // sim::ObjectRec (169-stride)

#include <memory>
#include <string>
#include <vector>

namespace guild::io { class ArchiveMount; }

namespace guild::play {

// ---------------------------------------------------------------------------
// Recovered constants (get_bytes / disasm of 0x50d01c & friends).
// ---------------------------------------------------------------------------
extern const char kGbPrefix[4];                   // unk_621458       "gb_"
extern const char kObPrefix[4];                   // aObS_0 base      "ob_"
extern const char kOgrPatternBase[];              // 0x621474 "%sgebaeude/*%s.ogr"
extern const char kOgrPatternVariant[];           // 0x62145c "%sgebaeude/*%s_%c.ogr"
extern const char kWimpelPattern[];               // 0x6214e0 "%s*sp_BAU_WIMPEL.ogr"
extern const char kWimpelAnim[];                  // 0x6214f8 "sonstiges\\sp_WIMPEL.baf"
constexpr float  kVariantCharBase   = 64.0f;      // flt_621560 (1 -> 'A')
constexpr float  kVariantCounterCap = 128.0f;     // cmp bits 0x43000000 @0x50d215
constexpr double kAabbHalf          = 0.5;        // dbl_621558
constexpr float  kDragAnchorZ       = 400.0f;     // flt_621564
constexpr float  kDragYawPi         = 3.14159265358979f; // flt_621568 (pi)
constexpr float  kPulseReset        = 90.0f;      // 0x42B40000 @0x50d98f
constexpr u32    kTranspWimpel      = 65664;      // 0x010080  @0x50d42a
constexpr u32    kTranspPreview     = 65757;      // 0x0100DD  @0x50d9a2
constexpr int    kMaxOgrVariants    = 128;        // v69[12288]/96 path slots
constexpr int    kOgrPathSlot       = 96;         // 96-byte path slots
constexpr i32    kFrameLoopId       = 415687;     // 0x657C7 @0x50dc6b
constexpr u32    kVerOwnerId        = 0x3A6C00B2; // +512 owner dword present
constexpr u32    kVerClassByte      = 0x3A6C00AB; // +535 class dword present
constexpr u32    kVerSuspendByte    = 0x3A6C00A6; // suspend byte present
constexpr u32    kVerAnchors        = 0x3A6C00AF; // 10 anchor vec3-pairs present
constexpr u32    kVerSingleMeshName = 0x3A6C00A4; // one mesh-name string form
// flt_5CA2B0 — the +Z axis the placement-height rotation measures against.
extern const float kAxisZ[3];

// ---------------------------------------------------------------------------
// CityWorldNode — one parsed scene-file object record (the full 0x5e67c8 body;
// each field is annotated with the runtime node offset it lands in).
// ---------------------------------------------------------------------------
struct CityWorldNode {
    bool        present = true;     // leading presence byte (0 -> placeholder)
    std::string name;               // -> node+0
    u32         ownerId  = 0;       // -> node+512 (matches sim::g_objects rec id +1)
    u32         classOvr = 0;       // -> node+535
    i32         kindRaw  = 0;       // spawn kind dword
    u8          suspend  = 0;       // ver>=0x3A6C00A6 byte (33=='!' name strip flag)
    u8          spawnType = 0;      // VIBE_Object_Spawn type byte -> node+533
    u8          lod532   = 0;       // mesh branch first dword -> node+532
    float       pos[3]   = {0,0,0}; // -> node+76/+80/+84 (world position)
    float       euler[3] = {0,0,0}; // -> node+132/136/140 -> matrix +396
    bool        hasAux   = false;   // mesh/dummy branch flag byte (bit0 of +529)
    float       aux92[3]  = {0,0,0};// -> node+92
    float       aux144[3] = {0,0,0};// -> node+144
    std::string meshName;           // Mesh_LoadOrFindByName arg (the .bgf base name)
    bool        hasMesh  = false;
    bool        isLight  = false;   // types 5..8 body
    float       lightParam[3] = {0,0,0};  // -> +144/+148/+152
    int         parent   = -1;      // flattened pre-order parent index
    int         objIndex = -1;      // host link: bound sim::g_objects slot (-1 none)
};

// gilde.exe 0x5e67c8 (kind ladder + VIBE_Object_Spawn @0x5b054c type rule):
// ver >= 0x3A6C00A6 takes the kind LOW BYTE verbatim; older files map
// 0,1,2,3/4,5..8,* -> 0,1,3,4,5..8,2. Spawn: kind<5 -> type==kind; kind>=5 ->
// type from name[0] ('r'->6,'s'->8,'p'->7, else 5).
u8 SpawnKindForVersion(i32 kindRaw, u32 version);
u8 SpawnTypeForKind(u8 kind, const char* name);

// gilde.exe 0x5e67c8 — read ONE object record (full body, recursive child/
// sibling framing) appending pre-order into `out`. Returns the node index of
// this record (-1 for an absent/empty-released node, exactly the original's
// free-on-empty path). `version` is the scene/group tag.
int ReadCityObjectRecord(render::SceneReader& r, u32 version,
                         std::vector<CityWorldNode>& out, int parent);

// The fully parsed city world: header (render::ParseSceneHeader) + the flat
// pre-order object list (.ed3 object-list portion of Scene_LoadFromStream).
struct CityWorld {
    render::SceneHeader        header;
    std::vector<CityWorldNode> nodes;
    bool ok = false;
};
CityWorld ParseCityWorld(const u8* ed3, std::size_t size);

// gilde.exe 0x5e84f4 framing (REUSES io::LoadObjectGroup) — parse an .ogr object
// group (Groups.BIN member bytes) into nodes. Returns root index or -1.
int ParseObjectGroupOgr(const u8* ogr, std::size_t size,
                        std::vector<CityWorldNode>& out);

// ---------------------------------------------------------------------------
// BuildingSceneHooks — engine-coupled leaves of the 0x50d01c family. Inert
// defaults (each named with its gilde.exe address); tests/hosts override.
// Node handles are sim::SceneNode3* in this reconstruction (void* at the rim
// to mirror the original's untyped registers).
// ---------------------------------------------------------------------------
struct BuildingSceneHooks {
    virtual ~BuildingSceneHooks() = default;

    // 0x4500a0 VIBE_Vfs_ResolveAndBuildPath(pattern, root, out, root) — resolve a
    // (wildcard) VFS path; nonzero on hit, writes the resolved path into out[256].
    // Inert: 0 (not found). BindGroupsArchiveResolver installs the real
    // Groups.BIN-backed resolve.
    virtual i32 VfsResolveAndBuildPath(const char* pattern, char* out256) {
        (void)pattern; (void)out256; return 0;
    }
    // 0x5e84f4 VIBE_Scene_LoadObjectGroup(path, 0, _, parent=0) — load an .ogr
    // group, return the root scene node. Inert: nullptr.
    virtual void* SceneLoadObjectGroup(const char* path) { (void)path; return nullptr; }
    // 0x429070 VIBE_Mesh_ApplyTransformRecursive(group). Inert: no-op.
    virtual void MeshApplyTransformRecursive(void* group) { (void)group; }
    // 0x5af38c VIBE_Object_SetPosition(node, pos) — default: the REAL
    // sim::ObjectSetPosition (+76 writes).
    virtual void ObjectSetPosition(void* node, const float pos[3]);
    // 0x5af50c VIBE_Object_SetWorldTranslation(node, euler) — default: the 1:1
    // body over SceneNode3 (+132 writes + util::MatrixFromEuler -> +396; type 3
    // negates the angles).
    virtual void ObjectSetWorldTranslation(void* node, const float euler[3]);
    // 0x5b2710 VIBE_Object_ChangeTransparency(node, *(node+460), packed, ecx-leftover).
    // Inert: no-op.
    virtual void ObjectChangeTransparency(void* node, u32 packed) {
        (void)node; (void)packed;
    }
    // 0x428928 VIBE_Mesh_SetVertexColors(node, edx, ecx, ebx). Inert: no-op.
    virtual void MeshSetVertexColors(void* node, u8 a, u8 b, u8 c) {
        (void)node; (void)a; (void)b; (void)c;
    }
    // 0x426488 VIBE_Character_LoadObjectAnimation(node, "sonstiges\\sp_WIMPEL.baf", 1).
    virtual void CharacterLoadObjectAnimation(void* node, const char* baf, i32 mode) {
        (void)node; (void)baf; (void)mode;
    }
    // 0x5c886c VIBE_Light_RefreshAllObjects(1). Inert: no-op.
    virtual void LightRefreshAllObjects(u32 mode) { (void)mode; }
    // 0x5c8218 VIBE_Light_BuildObjectCache(node). Inert: no-op.
    virtual void LightBuildObjectCache(void* node) { (void)node; }
    // 0x40dca8 VIBE_Input_ClearMouseButtonsByMask(2). Inert: no-op.
    virtual void InputClearMouseButtonsByMask(i32 mask) { (void)mask; }
    // 0x5b4258 VIBE_Object_DetachAndRelease(node). Inert: no-op.
    virtual void ObjectDetachAndRelease(void* node) { (void)node; }
    // 0x427b60 VIBE_Collision_ResolveMeshAgainstTerrain(node, 1, _, 0). Inert: no-op.
    virtual void CollisionResolveMeshAgainstTerrain(void* node, i32 mode, i32 flag) {
        (void)node; (void)mode; (void)flag;
    }
    // 0x4bcdcc VIBE_Hud_SetStatusBannerText(text). Inert: no-op.
    virtual void HudSetStatusBannerText(const char* text) { (void)text; }
    // 0x5441d0 VIBE_MapView_PanelDispatcher(8, 0, 0, text, 0) — returns the panel
    // focus node (the camera target). Inert: nullptr.
    virtual void* MapViewPanelDispatcher(i32 op, const char* text) {
        (void)op; (void)text; return nullptr;
    }
    // 0x4b5974 VIBE_Camera_ZoomOut(node). Inert: no-op.
    virtual void CameraZoomOut(void* node) { (void)node; }
    // 0x4b4c68 VIBE_Camera_Update() — default routes to the REAL
    // render::Camera_Update over an owned inert default env (returns 0 via the
    // no-camera early path). Hosts wire the live camera env.
    virtual i32 CameraUpdate();
    // 0x5c67b8 VIBE_Heightmap_RaycastFromCursor(hmIdx, mx, my, &outB; ecx=&outA).
    // Inert: false.
    virtual bool HeightmapRaycastFromCursor(i32 hmIdx, i32 mx, i32 my,
                                            i32* outA, i32* outB) {
        (void)hmIdx; (void)mx; (void)my; (void)outA; (void)outB; return false;
    }
    // 0x5c65d4 VIBE_Heightmap_TileToWorld(hmIdx, a, b, outPos). Inert: false.
    virtual bool HeightmapTileToWorld(i32 hmIdx, i32 a, i32 b, float outPos[3]) {
        (void)hmIdx; (void)a; (void)b; (void)outPos; return false;
    }
    // 0x5b5c70 VIBE_Object_ComputeScreenBounds(node, out4). Inert: no-op.
    virtual void ObjectComputeScreenBounds(void* node, i32 out4[4]) {
        (void)node; (void)out4;
    }
    // 0x4284f4 VIBE_Mesh_ComputeWorldAabb(outMin, outMax, node) — outMin/outMax
    // are the (x,y,z) extents the drag anchor uses. Inert: zeros.
    virtual void MeshComputeWorldAabb(void* node, float outMin[3], float outMax[3]) {
        (void)node;
        for (int i = 0; i < 3; ++i) { outMin[i] = 0; outMax[i] = 0; }
    }
    // 0x591480 VIBE_Building_ComputeSalePrice(cityPersonRec, btype). Inert: 0.
    virtual i32 BuildingComputeSalePrice(u16 cityIndex, i32 btype) {
        (void)cityIndex; (void)btype; return 0;
    }
    // 0x586c20 / 0x586a6c VIBE_Person_QueryBegin(2,4,city,5,2) / IterNext — the
    // duplicate-building scan; iterates 169-stride object records. Inert: null.
    virtual const u8* PersonQueryBegin(i32 a, i32 b, u16 city, i32 c, i32 d) {
        (void)a; (void)b; (void)city; (void)c; (void)d; return nullptr;
    }
    virtual const u8* PersonIterNext() { return nullptr; }
    // 0x4ad6f0 VIBE_Dialog_ShowMessageBox(text, mode) — nonzero == confirmed.
    // Inert: 0 (declined).
    virtual i32 DialogShowMessageBox(const char* text, i32 mode) {
        (void)text; (void)mode; return 0;
    }
    // 0x59f99c VIBE_Text_RenderFormattedMessage(buf, msgId, args...). Inert: "".
    virtual void TextRenderFormattedMessage(char* buf, i32 msgId,
                                            i32 a0, i32 a1, i32 a2) {
        (void)msgId; (void)a0; (void)a1; (void)a2; if (buf) buf[0] = 0;
    }
    // 0x4c09a0 VIBE_GameLogic_RunFrameLoop(eax=self, edx=415687) — nonzero ==
    // keep looping. Inert: 0 (single pass).
    virtual i32 GameLogicRunFrameLoop(i32 loopId) { (void)loopId; return 0; }
    // 0x438da8 VIBE_ErrorLog_ReportMessage(msg). Default: record into lastError
    // (hosts route to config/errorlog).
    virtual void ErrorLogReportMessage(const char* msg);
    // 0x5b43f0 VIBE_Universe_RestoreObjectStates(node, 1) (BuildModelName tail).
    virtual i32 UniverseRestoreObjectStates(void* node, u8 flag) {
        (void)node; (void)flag; return 0;
    }
    // 0x5b4a24 VIBE_Universe_SwitchActiveSlot(slot, mode) + the dword_649D60
    // active-slot read (ComputePlacementHeight wraps the rotation in a slot
    // switch). Inert: slot 0, no-op switch.
    virtual i32  UniverseActiveSlot() { return 0; }
    virtual void UniverseSwitchActiveSlot(i32 slot, i32 mode) { (void)slot; (void)mode; }
    // 0x4ffb40 VIBE_Object_ParseNameAndBind(node) — the gb_/ob_ name -> model
    // re-bind (BuildModelName tail). Inert: 1.
    virtual i32 ObjectParseNameAndBind(void* node) { (void)node; return 1; }

    std::string lastError;   // ErrorLogReportMessage default sink
};

// ---------------------------------------------------------------------------
// BuildingSceneEnv — the process globals 0x50d01c reads/writes, modeled as
// fields (address per field), plus the hooks pointer.
// ---------------------------------------------------------------------------
struct BuildingSceneEnv {
    BuildingSceneHooks* hooks = nullptr;       // required

    // -- input globals --
    u8  keyChar = 0;          // byte_67225C  last key char (set to 50=='2' on entry)
    i32 leftClick = 0;        // dword_67221C
    i32 mouseHeld672220 = 0;  // dword_672220
    i32 rotateHeld = 0;       // dword_672228
    i32 middleClick = 0;      // dword_672230
    i32 escFlag = 0;          // dword_672234
    i32 mouseX16 = 0;         // unk_67220E   (16.16 fixed; >>16 used)
    i32 mouseY16 = 0;         // dword_672210 (16.16 fixed; >>16 used)
    i32 activeKey = 0;        // dword_62D22C
    i32 keyRotateA = 0;       // dword_6316F8
    i32 keyRotateB = 0;       // dword_6316FC
    i32 redrawFlag = 0;       // dword_631614 (written 1)

    // -- world / scene globals --
    u16 cityIndex = 0;        // word_63CC5C
    i32 universeHeightmapIdx = 0;  // *((dword*)off_649D64 + 44)  (+0xB0)
    const char* pathPrefix = "";   // byte_122F098 (resource path prefix)
    float viewerPos[3] = {0, 0, 0};// dword_13FCD1C node +76..+84 (camera/viewer)
    const u8** bauplatzGlobal = nullptr; // dword_123343C mirror (write-only)

    // -- localized text pointers --
    const char* textPanel   = ""; // dword_8C86AC (MapView panel arg)
    const char* textNoPlot  = ""; // dword_8C86BC ("no free plot" message box)
    const char* textBlocked = ""; // dword_8C86C0 (status banner: blocked)

    // -- building-type table (dword_13CE294, 589-stride) accessors. Defaults
    //    (null fns) read the live sim::g_buildingTypes: kind byte +0, NAME at +1,
    //    sale-text dword +579 (0x243). --
    u8          (*typeKind)(u8 type)   = nullptr;
    const char* (*typeName)(u8 type)   = nullptr;
    i32         (*typeField579)(u8 type) = nullptr;
};

// Resolved defaults for the table accessors (read sim::g_buildingTypes).
u8          BuildingTypeKindDefault(u8 type);
const char* BuildingTypeNameDefault(u8 type);
i32         BuildingTypeField579Default(u8 type);

// ---------------------------------------------------------------------------
// The reconstructed functions.
// ---------------------------------------------------------------------------

// gilde.exe 0x50cec0 — VIBE_Building_ComputePlacementHeight (eax=out, edx=plot).
// out = (0, VectorAngleBetween(+Z, plotHierarchyRotation * +Z), 0) — the euler
// triple SetWorldTranslation seats the building with; wrapped in an active-slot
// switch (slot 0 around the rotation, restore after).
void BuildingComputePlacementHeight(float out[3], const u8* plotNode,
                                    BuildingSceneHooks& h);

// gilde.exe 0x50cfd0 — VIBE_Building_AlignMeshToTerrain (eax=node, edx=pos).
// SetPosition(node,pos); ResolveMeshAgainstTerrain(node,1,0); if the terrain
// pushed the node's world Y below pos.y, SetPosition(node,pos) again.
void BuildingAlignMeshToTerrain(void* node, const float pos[3],
                                BuildingSceneHooks& h);

// gilde.exe 0x4ffe0c — VIBE_Object_BuildModelName (eax=node, edx=rec, bl=mode).
// mode 2: sprintf(node.name, "gb_%s", typeName(rec[0])); rec+97 <- node link;
//   type 10 -> +535=0,+536=0; else +535=3 and (spawnType==1 && !(rec[90]&1)) ->
//   UniverseRestoreObjectStates(node,1).
// mode 1: sprintf(node.name, "ob_%s", objTypeName(*(u16*)rec)); +535=4.
// tail: ParseNameAndBind(node); node+512 <- rec.
// Portable link model: node+512 stores the g_objects SLOT index (objSlot) and
// rec+97 stores the caller-supplied nodeToken (pointers are 64-bit here; the
// repo-wide "index instead of raw pointer" rule, see sim/types.h).
i32 ObjectBuildModelName(sim::SceneNode3* node, u8* rec, u8 mode, i32 objSlot,
                         i32 nodeToken, BuildingSceneEnv& env);

// gilde.exe 0x5a8140 — VIBE_Object_RebuildModelByOwner (eax=node). Scans the
// full 169-stride array (43264 bytes / 256 slots) of sim::g_objects; binds via
// ObjectBuildModelName(node, rec, 2) on every alive record whose id (+1 dword)
// equals *(node+512). Returns 1. `nodeToken` is the value written into rec+97.
i32 ObjectRebuildModelByOwner(sim::SceneNode3* node, i32 nodeToken,
                              BuildingSceneEnv& env);

// The 0x50d01c / 0x49c19c shared .ogr variant-resolve loop, 1:1 (the LABEL_9 /
// LABEL_15 path probe of 0x50d01c): probe "%sgebaeude/*%s.ogr" then
// "%sgebaeude/*%s_%c.ogr" with c = (u8)(int)(counter + 64.0f) for counter
// 1,2,... stopping at the first missing variant or counter == 128.0. Each HIT
// stores the PATTERN (not the resolved path) into paths[i] (96-byte slots).
// Returns the hit count.
i32 ResolveGebaeudeOgrVariants(const char* upperGbName, BuildingSceneEnv& env,
                               char paths[][kOgrPathSlot], i32 cap);

// gilde.exe 0x50d01c — VIBE_Building_LoadAndAlignGebaeudeModel
//   (__usercall eax = fn(al=buildingType, edx=outPath, [sil dead])).
// The interactive "lc_GebaeudeAusrichten" placement loop: resolve the type's
// .ogr variants, plant sp_BAU_WIMPEL flags on every free Bauplatz, preview the
// building on the nearest plot (position from PointThroughBoneChain(plot+76),
// euler from ComputePlacementHeight), drive the frame loop until confirm /
// cancel. Returns the chosen plot node (or null); writes the chosen .ogr
// wildcard pattern (paths[firstRandomPick]) — or "" on cancel — into outPath.
const u8* BuildingLoadAndAlignGebaeudeModel(i8 buildingType, char* outPath,
                                            BuildingSceneEnv& env);

// ---------------------------------------------------------------------------
// WAVE-2 ENTRY API — real-positioned scene nodes for the loaded city.
// ---------------------------------------------------------------------------
// Given the live loaded world (after io::LoadWorldEx populated sim::g_objects
// AND captured the .cty's embedded scene blob — the wave-2 input; the
// stadt_<CITY>.ed3 template parses identically but binds no owners), produce
// REAL engine-layout scene nodes
// (sim::SceneNode3: +76 world pos, +132 euler, +396 matrix via the REAL
// setters) for every scene object, then run the 1:1 owner-id link
// (ObjectRebuildModelByOwner) and the gebaeude .ogr resolve for every bound
// building record. This is the path that replaces the wave-1 synthetic grid.
// ---------------------------------------------------------------------------
struct AttachedCityObject {
    int         nodeIndex = -1;     // index into CityAttachResult::liveNodes
    int         objSlot   = -1;     // sim::g_objects slot bound by owner id
    u8          typeId    = 0;      // rec type byte (+0)
    u32         ownerId   = 0;      // the matching id (rec +1 == .ed3 node +512)
    std::string gbName;             // node name after BuildModelName ("gb_*")
    std::string ogrPattern;         // resolved variant pattern (0x50d01c loop)
    std::string ogrMember;          // archive member the pattern resolved to
    std::vector<std::string> meshNames; // .bgf base names parsed from the .ogr
    float pos[3]   = {0, 0, 0};     // node +76 (REAL world position)
    float euler[3] = {0, 0, 0};     // node +132
};

struct CityAttachResult {
    bool ok = false;
    int  parsedNodes   = 0;   // .ed3 object records parsed
    int  meshNodes     = 0;   // static nodes carrying a mesh name
    int  liveNodeCount = 0;
    std::vector<std::unique_ptr<sim::SceneNode3>> liveNodes; // engine-layout nodes
    std::vector<std::string> liveNodeNames;                  // node +0 names
    std::vector<AttachedCityObject> attached;                // owner-bound buildings
};

// Parse + spawn + place + link + resolve. `env.hooks` supplies the VFS resolve
// (BindGroupsArchiveResolver for the real Groups.BIN). `ogrLoader` (optional)
// returns member bytes so each building's .ogr is parsed for its mesh names.
CityAttachResult AttachCityBuildingScene(const u8* ed3, std::size_t size,
                                         BuildingSceneEnv& env);

// ---------------------------------------------------------------------------
// Host adapter: the real archive-backed VFS resolve over Resources/Groups.BIN.
// Implements VfsResolveAndBuildPath for "gebaeude/*NAME.ogr" patterns (the VFS
// wildcard '*' spans the sub-directory part, e.g. "Gebaeude/ad_Bauernhof/") and
// SceneLoadObjectGroup is left inert (the attach path parses .ogr bytes
// directly). Also exposes the member loader the attach uses for mesh lists.
// ---------------------------------------------------------------------------
class GroupsArchiveResolver : public BuildingSceneHooks {
public:
    explicit GroupsArchiveResolver(io::ArchiveMount* groups) : groups_(groups) {}
    i32 VfsResolveAndBuildPath(const char* pattern, char* out256) override;
    // Load the member a pattern resolved to (out of VfsResolveAndBuildPath).
    bool LoadMember(const char* member, std::vector<u8>& out);
private:
    io::ArchiveMount* groups_;
};

} // namespace guild::play
