#pragma once
// =============================================================================
// guild::play — d3 SCENE (.ed3) object reader + perspective scene compositor.
//
// scene_load.{h,cpp} reconstructs the .ed3 header + the object-list *framing*
// (VIBE_WorldIo_ReadObject @0x5e67c8) but delegates each object BODY to a hook.
// This module supplies that body for the shipped scene-format version
// (tag 0x3A6C00BB, e.g. Menu/ChooseCity.ed3): it reads each object's kind, mesh
// name (the .bgf LOD), world position (+76) and rotation (+92), recursing into the
// child/sibling tree and skipping the per-object event bindings
// (VIBE_Event_LoadEventBindings @0x5f4bc8) — exactly the byte grammar the engine
// reads. `VIBE_Object_Spawn @0x5b054c`'s kind->type rule (kind<5 => type==kind;
// kind>=5 => type from the name's first char) selects which body shape to read.
//
// It then composites the parsed mesh objects through a perspective camera (the
// play::scene_persp_render path) into a software surface — the host-side renderer
// for the menu's 3D screens (the engine's perspective d3 raster is unreconstructed;
// geometry, transforms and camera are real, the rasteriser is a faithful-equivalent).
// =============================================================================
#include "guild/common/types.h"
#include "play/scene_persp_render.h"
#include "render/scene_drawlist.h"  // render::Scene3DDrawList (backend-neutral 3D list)
#include "render/sky.h"          // render::SkyAmbient / BlendBandLighting

#include <string>
#include <vector>

namespace guild::render { struct Surface; }
namespace guild::io { class ArchiveMount; }

namespace guild::play {

class RealTextureSource;

// One parsed scene object. `type` is the VIBE_Object_Spawn type byte (+533):
// 0=light/sfx node, 1/4=mesh object, 2/3=dummy/locator (e.g. dummy_<CITY>),
// 5..8=animated/light/particle node.
struct SceneObjectInst {
    std::string name;
    int         type = -1;
    std::string mesh;            // model basename (no path/ext); empty if none
    float       pos[3] = {0, 0, 0};   // object +76 position
    float       euler[3] = {0, 0, 0}; // object +132 — the rotation euler (radians) the
                                      // engine feeds MatrixFromEuler -> the +396 world
                                      // rotation matrix (SetWorldTranslation @0x5af50c)
    float       rot[3] = {0, 0, 0};   // object +92 (mesh: aux; camera dummy: the eye)
    float       scale  = 1.0f;        // uniform model scale (host-side; e.g. enlarge
                                      // a city-marker tower so it reads on the map)
    u32         ownerId = 0;          // object +512 (scene ver >= 0x3A6C00B2): the OWNER
                                      // OBJECT ID. After a .cty load the engine rebinds
                                      // each live world object onto its city scene node by
                                      // this id: VIBE_Save_PostLoadInitScene @0x5a7ef8 ->
                                      // TraverseTree(VIBE_Object_RebuildModelByOwner
                                      // @0x5a8140) matches node+512 == objectRec id (+1)
                                      // and seats record+97 = node / node+512 = record.
                                      // 0 == no owner link.
    bool        hasMesh = false;
    bool        noRender = false; // host-side: pickable but not drawn — the invisible
                                  // per-city "stadt_<CITY>" POINT markers (the engine's
                                  // VIBE_Map_SpawnCityPointMarker pick targets) carry a
                                  // mesh only for their pick radius; only the single
                                  // moving sp_STADTTURM tower is actually rendered.
    bool        noPick = false;   // host-side: drawn but NOT a pick target — the single
                                  // moving sp_STADTTURM tower (the engine pick only
                                  // accepts "stadt_<CITY>" markers, not the tower).
    int         parent  = -1;    // index of the parent node in the flat list (-1 = root);
                                 // the scene is a tree (child/sibling) and an object's
                                 // world transform composes its parents' (VIBE_Transform_
                                 // PointThroughBoneChain). -1 until ComputeWorldTransforms.
    // For light nodes (type 5..8) — fields recovered from VIBE_WorldIo_ReadObject
    // @0x5e67c8's light branch (the runtime object-record offsets):
    //   lightColor = +92  (RGB),  lightDir = +132 (direction, type-7/sun),
    //   lightParam = {+144 range, +148 intensity, +152 rangeParam}.
    // hasLight marks a parsed light node.
    bool        hasLight = false;
    float       lightColor[3] = {0, 0, 0};   // +92 (v5[23..25] in ApplyToCachedVertices)
    float       lightDir[3]   = {0, 0, 0};   // +132 (sun direction for type 7)
    float       lightParam[3] = {0, 0, 0};   // +144 range, +148 intensity, +152 rangeParam
    // For camera dummies (dummy_A*/B*/C*), VIBE_Camera_SetToDummy @0x43fbbc takes
    // the camera EYE from the object's +92 field (parsed into `rot` for type 2/3)
    // and the look/world-translation from +144 (here). hasCam marks it present.
    bool        hasCam = false;
    float       camLook[3] = {0, 0, 0};      // +144 (camera world-translation / look)
};

// Parse every object out of an in-memory .ed3 buffer (header + recursive object
// tree). `versionOut` (optional) receives the scene tag. Returns the flattened
// object list (tree pre-order). Empty on a header/parse failure.
std::vector<SceneObjectInst> ParseSceneObjects(const u8* ed3, std::size_t size,
                                               u32* versionOut = nullptr);

// Parse the .ed3's 7-band light rig header and apply the band the menu uses
// (VIBE_Menu_RunChooseCity drives BlendBandLighting(0, 0, 1.0) — band 0), giving
// the scene's active ambient light (the existing render::BlendBandLighting,
// gilde.exe 0x5b85e4, fed the rig's per-band ambient colour SceneLight::pos).
// `ok` (optional) reports whether a full 7-band rig was present. Returns the
// zero ambient if the header has no rig.
render::SkyAmbient ComputeSceneAmbient(const u8* ed3, std::size_t size,
                                       unsigned band = 0, float frac = 0.0f,
                                       float scale = 1.0f, bool* ok = nullptr);

// Append a mesh instance (`meshBasename`, e.g. "sp_STADTTURM") at every scene
// locator/dummy whose name contains `markerSubstr` (case-insensitive) — the host-
// side analogue of VIBE_Map_SpawnCityPointMarker placing a city tower at each
// dummy_<CITY> marker. New instances get type 4 (mesh object) at the marker's pos.
// Returns the number of instances appended.
int PlaceMeshAtMarkers(std::vector<SceneObjectInst>& objs, const char* markerSubstr,
                       const char* meshBasename, float scale = 1.0f);

// Build a perspective camera seated at the scene locator named `dummyName` (e.g.
// "dummy_A1", a CameraFlightEnhanced waypoint), aimed by that locator's euler
// rotation (forward = Rz*Ry*Rx applied to +X — the engine's SetCameraToDummy
// orientation). Returns false if the locator isn't present. `fovY` in radians.
bool CameraFromDummy(const std::vector<SceneObjectInst>& objs, const char* dummyName,
                     PerspCamera& cam, float fovY = 0.9f);

// Build the engine-faithful scene camera at a cutscene dummy. Reconstructs the
// world->view of VIBE_Transform_PointToBoneLocalSpace @0x5c8c40:
//   view = R_cam * (world - cam[+76] - cam[+120])
// where for the cutscene camera (VIBE_Camera_SetToDummy @0x43fbbc):
//   cam[+76]  <- dummy +92  (the eye; parsed into `rot`)
//   cam[+120] <- dummy +144 (the world-translation; `camLook`)  -> eye = +92 + +144
//   R_cam     = the scene MegaCam orientation (camPos->camTarget look-at), which
//               SetToDummy/the flight DO NOT change — only the eye moves.
// `ed3`/`size` is the scene buffer (to read the MegaCam camPos/camTarget header).
// Returns false if the dummy or a full header is absent.
bool BuildSceneCamera(const u8* ed3, std::size_t size,
                      const std::vector<SceneObjectInst>& objs, const char* dummyName,
                      PerspCamera& cam, float fovY = 0.9f);

// The real CameraFlightEnhanced motion (A_Stadtwahl.esc = CameraFlightEnhanced(1500,
// "dummy_A1","dummy_A2",...)): interpolate the camera position + orientation through
// the named dummy waypoints over `totalTicks`, sampling at `tick` via the engine's
// Catmull-Rom tangent + Hermite object-anim (render::object_anim — the reconstruction
// of VIBE_Anim_CreateObjectAnim/ComputeFrameTangents and the UpdateSkeletonPose object
// path). Seats `cam` at the sampled waypoint, aimed by the sampled euler (the same
// forward = Rz*Ry*Rx*+X as CameraFromDummy). Returns false if a waypoint is missing.
bool CameraFlightAt(const std::vector<SceneObjectInst>& objs,
                    const std::vector<std::string>& dummyNames,
                    float totalTicks, float tick, PerspCamera& cam, float fovY = 0.9f);

struct SceneViewStats {
    int meshesDrawn   = 0;   // mesh objects whose .bgf resolved + drew
    int meshesMissing = 0;   // mesh objects whose .bgf was not found in the mount
    int trisDrawn     = 0;
    int pixelsWritten = 0;
};

// gilde.exe 0x5b5a38 — VIBE_Pick_FindNearestObjectAt: pick the scene mesh object
// nearest the cursor. Faithful host reconstruction of the engine's screen-space
// bounding-sphere pick (TestObjectAtPoint @0x5b5938 + ComputeBoundingRadius
// @0x5b2cf8): each object's view-space bbox CENTRE is projected to the screen
// (scale `a7 = fbW*0.5` == flt_13FCD0C, centre fbW/2,fbH/2 == flt_13FCD18/10) and
// the cursor must fall within the projected radius; the nearest such object wins.
// `cursorX/Y` are device pixels. Returns the index into `objs`, or -1 if none.
// (Lights/dummies are skipped, as the engine's mask/type filter does.)
int PickNearestObject(const std::vector<SceneObjectInst>& objs,
                      io::ArchiveMount& objectsBin, const PerspCamera& cam,
                      int fbW, int fbH, float cursorX, float cursorY);

// The "stadt_" city-marker filter VIBE_Menu_RunChooseCity @0x52e6d8 applies to a
// pick result (VIBE_Util_StrncmpN(picked, "stadt_", 6)). Returns true when the
// object's name marks it a selectable city. The host marker spawner names towers
// "stadt_<CITY>"; "tower:dummy_<CITY>" (PlaceMeshAtMarkers) also matches a city.
bool IsCityMarkerName(const std::string& name);

// Composite the parsed MESH objects (type 1/4 with a mesh) through `cam` into `fb`,
// resolving each object's model by basename from a mounted Objects.BIN (the .bgf is
// decoded via the fast-chunk loader, world-placed by pos+rot, z-buffered). Lights/
// dummies are skipped. Clears `fb` first per opt.clearFirst.
SceneViewStats RenderSceneObjects(const std::vector<SceneObjectInst>& objs,
                                  io::ArchiveMount& objectsBin, const PerspCamera& cam,
                                  render::Surface* fb, const PerspRenderOptions& opt = {},
                                  RealTextureSource* textures = nullptr);

// Run the engine's exact transform & lighting (the same world transform, baked
// vertex lighting, camera basis and projection RenderSceneObjects uses) and emit a
// backend-neutral render::Scene3DDrawList for `W`x`H`, instead of rasterising. The
// CPU reference (render::RasterizeDrawList) reproduces RenderSceneObjects' image;
// the Vulkan backend (IGraphicsDevice::renderScene3D) draws the SAME list on the
// GPU (rule 3: only the rasteriser's GPU API is swapped). Triangles are grouped
// into texture-runs in draw order so depth ties break identically on both paths.
render::Scene3DDrawList BuildSceneDrawList(const std::vector<SceneObjectInst>& objs,
                                           io::ArchiveMount& objectsBin,
                                           const PerspCamera& cam, int W, int H,
                                           const PerspRenderOptions& opt = {},
                                           RealTextureSource* textures = nullptr);

// Sample the tower's smooth slide from `from` to `to` at `frac` in [0,1] — the 1:1
// motion VIBE_Menu_RunChooseCity gives the sp_STADTTURM tower when a city is picked:
// VIBE_Anim_CreateObjectAnim @0x5cef14 builds a 2-keyframe object animation
// (current transform -> target), so render::BuildFrameTangents leaves the endpoint
// tangents zero and the Hermite reduces to an ease-in-out (smoothstep) glide — NOT a
// teleport. Reconstructed via the same render::object_anim machinery as the camera
// flight. Writes the interpolated world position into `out`.
void SampleTowerGlide(const float from[3], const float to[3], float frac, float out[3]);

// Recompute ONLY the camera basis + projection params of an existing draw list for
// a new camera (the geometry/textures/`geometryId` are untouched). Lets a per-frame
// loop build the static scene's draw list once and just re-aim it each frame — the
// expensive mesh decode + texture build + lighting bake happen once, not per frame.
void UpdateSceneDrawListCamera(render::Scene3DDrawList& dl, const PerspCamera& cam,
                               int W, int H);

} // namespace guild::play
