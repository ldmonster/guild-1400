#pragma once
// =============================================================================
// guild::play — SESSION PERSONS IN THE 3D CITY VIEW (wave 3, agent W3-B).
//
// ONE clean entry point for the session (src/play/sdl_session.cpp) to put the
// live sim::g_persons into the real-3D city view (play::CityView3D) exactly the
// way the original does it — persons are character OBJECTS in the same
// universe/scene graph the buildings render through:
//
//   VIBE_Character_SpawnAtBuildingEntrance @0x57c8f0   [DISASM-VERIFIED 2026-06-11]
//     -> the dummy TAG is a BRANCH, not a fallback order. 0x57c8f0(bld@edx,
//        pos*@ecx, objId@ebx, personId@eax, rot* stack):
//          objId != -1 (object resolved via GameObject_QueryFind @0x5857fc):
//            tag = PickWaitAnimation(obj) @0x406344, else "dummy_EINGANG"
//            (@0x57ca38); FindByHandle SEARCH ROOT = NULL (@0x57c9a3 push 0).
//          no object (the building-anchored path THIS module models):
//            tag = "dummy_TUER" ONLY (@0x57ca47); search root =
//            [building+0x61] — the building's scene node subtree.
//        The search itself lives in SpawnOfficeStaffActor @0x57c744:
//        FindByHandle @0x5b7be4(root, 256, tag) -> PointThroughBoneChain
//        @0x5c8b38 over node +76 (@0x57c7d4). NO-DUMMY fallback (@0x57c8d9):
//        the caller-passed position (a4) else the all-zero dword_577A68 —
//        NOT the building node itself (that fallback belongs to
//        IsNearDoorAlt @0x4b0ee8, a different function).
//     -> model: VIBE_Office_ResolveStaffModel @0x57c1e8 (1:1, person_render.*)
//     -> create: VIBE_Character_CreateFromModel @0x402d10 (1:1, the character
//        factory; mesh attach = AttachToUniverseNode @0x5b3e30 ->
//        Mesh_LoadOrFindByName @0x5d345c over Objects.BIN)
//     -> pose: the factory preload set ({"bewegung/gehen",
//        "stehen/stehen_newnoise"}, PreloadAniSet @0x403c34) advanced per frame
//        through VIBE_Anim_UpdateSkeletonPose @0x5cd1d8 (1:1 pose driver).
//
// =============================================================================
// THE SESSION CALL CONTRACT (the orchestrator stitches this into sdl_session):
//
//   // boot — AFTER view.Init(&fs) + view.LoadCityFromWorld(sceneBlob)
//   //        + view.BindWorldObjects(), i.e. once the city is in the view:
//   play::SessionPersons3DOptions po;            // defaults are fine
//   po.anchorProvider = ...;                     // OPTIONAL: person -> building
//                                                // id (else the record's own
//                                                // +0x16C/+0x170 columns gate)
//   play::SessionPersons3DStatus st =
//       play::WireSessionPersons3D(view, &fs, po);
//
//   // per frame — BEFORE view.RenderFrame(cam, opt):
//   play::UpdateSessionPersons3D(view, po.animStepPerFrame);
//
//   // whenever the sim creates/destroys persons or anchors change
//   // (e.g. after the new-game commit / a day-tick that moves people):
//   play::RebindSessionPersons3D(view, po);
//
//   // teardown (optional; the view's destructor also releases everything):
//   play::UnwireSessionPersons3D(view);
//
// RenderFrame() then reports Result::personInstances / personPosed /
// personRestPose, and view.boundPersons() carries the per-person roster
// (id, slot, anchor building, world seat, model, member, posed).
//
// NAMED GAPS (rule 8 — never faked):
//  * person -> building ANCHOR: the original's spawn receives the building from
//    its callers (VIBE_NpcAction_DailyRoutineStep @0x4e7e88 and the command/
//    combat clusters). Default = the person record's own building columns the
//    daily director reads (sim/npc_daily.h): +0x16C homeBld (dword_12CEA7C),
//    else +0x170 workBld (dword_12CEA80). A person with neither (and no
//    anchorProvider result) is NOT placed — counted, never seated at an
//    invented position.
//  * live WALKING positions: the wire_npc_movement bridge keeps a moving
//    entity's current TILE at record +0x6C/+0x70 (state +0x74); converting it
//    to world space needs the terrain heightmap (render::TileToWorld @0x5c65d4)
//    which CityView3D does not own. A session with the heightmap wired can
//    supply `placementOverride` from GetEntityMovePos + TileToWorld.
//  * clip SELECTION (which animation a person plays right now) is the
//    NpcAction/charaction runtime; the pose plays the factory preload set.
//
// VERIFICATION HANDOFF (for the city_view3d owner — placement fix that cannot
// land here): CityView3D::DefaultPersonPlacement prefers "dummy_EINGANG" over
// "dummy_TUER" inside the anchor building's subtree. The binary's
// building-anchored spawn path searches the building subtree for
// "dummy_TUER" ONLY (0x57ca47..0x57ca54: root = [bld+0x61], tag fixed);
// "dummy_EINGANG" is the OBJECT-path tag searched from a NULL root. In the
// shipped city scenes the building subtrees carry no dummy_EINGANG so the
// result is identical today, but if a host attaches `gb_` model subtrees
// (which do carry dummy_EINGANG) the preference diverges. Fix:
// FindEntranceDummyNode should match TUER only when anchoring by building.
// Also: the engine's no-dummy fallback is the spawn position arg / zero —
// CityView3D's "building node itself" fallback is the IsNearDoorAlt
// (@0x4b0ee8) point, faithful for the daily director's DESTINATION but not
// for the spawn seat; keep it documented as the host stand-in.
// =============================================================================
#include "play/city_view3d.h"

#include <functional>

namespace guild::render { struct Heightmap; }
namespace guild::shim { class IFileSystem; }
namespace guild::sim { struct Person; }

namespace guild::play {

struct SessionPersons3DOptions {
    // Cap on bound persons (0 == every live person).
    int maxPersons = 0;
    // Mount Resources/animations.BIN for the pose chain (rest pose without it).
    bool        mountAnims = true;
    const char* animsArchive = "Resources/animations.BIN";
    // Pose ticks folded per UpdateSessionPersons3D call (the pose driver's
    // host-rate seam; the track +96 rate source is the documented gap).
    float animStepPerFrame = 8.0f;
    // OPTIONAL hooks (installed into the view's person slots when set):
    // person -> anchor building id (0 == none). Replaces the +0x16C/+0x170 read.
    std::function<i32(const sim::Person&)> anchorProvider;
    // person -> full world placement (true == placed). Replaces the
    // entrance-dummy default entirely.
    std::function<bool(const sim::Person&, CityPlacement&)> placementOverride;
    // person -> staff-model record. Replaces the 0x57c1e8 default resolver.
    std::function<const StaffModelRecord*(const sim::Person&)> modelOverride;
};

struct SessionPersons3DStatus {
    bool animsMounted    = false;  // animations.BIN available for the pose chain
    int  bound           = 0;      // persons placed (BindPersons return)
    int  posed           = 0;      // ... with a clip bound through the pose chain
    int  unplaced        = 0;      // live persons with no anchor (named gap)
    int  modelUnresolved = 0;      // placed persons whose model is not shipped
};

// Wire the view's person pass: mount the anim archive (optional), install the
// option hooks into the view's person slots (the building/object hooks the
// session already installed are PRESERVED), and bind the live persons.
SessionPersons3DStatus WireSessionPersons3D(CityView3D& view,
                                            shim::IFileSystem* fs,
                                            const SessionPersons3DOptions& opt = {});

// Per-frame: advance every bound person's pose through the REAL driver
// (UpdateSkeletonPose @0x5cd1d8). Call before view.RenderFrame(). Returns the
// number of poses advanced (0 == nothing bound/poseable; safe to call always).
int UpdateSessionPersons3D(CityView3D& view, float stepTicks);

// Re-run the person bind against the CURRENT sim state (after person creation/
// destruction or anchor changes). Hooks installed by Wire stay installed.
SessionPersons3DStatus RebindSessionPersons3D(CityView3D& view,
                                              const SessionPersons3DOptions& opt = {});

// Release the bound persons and clear the person hook slots (building/object
// hooks preserved). The view renders exactly as before wiring.
void UnwireSessionPersons3D(CityView3D& view);

// ===========================================================================
// LIVE WALKING POSITIONS (wave 4 — the header's documented placement handoff).
//
// A moving entity's CURRENT TILE lives in its record pad (+0x6C/+0x70, state
// +0x74 — the wire_npc_movement bridge, the WalkStep @0x4093b0 waypoint
// advance). tile -> world is render::TileToWorld @0x5c65d4 over the session's
// REAL terrain heightmap. These helpers implement the `placementOverride`
// chain the wave-3 header promised:
//
//   InstallSessionPersonsMovePlacement(view, opt, hm):
//     1. CAPTURES the current bound persons' world seats (the genuine
//        entrance-dummy defaults the view itself computed at BindPersons —
//        SpawnAtBuildingEntrance @0x57c8f0 semantics), then
//     2. installs opt.placementOverride + the view hook: a person with a live
//        movement tile is placed at TileToWorld(curTile) over `hm` (real
//        ground height via the heights grid); every other person keeps its
//        CAPTURED entrance-dummy seat (no behavior change). A person with
//        neither stays unplaced — never an invented position (rule 8).
//
//   UpdateSessionPersons3DPositions(view, opt):
//     The cheap per-frame check: compares each bound person's live movement
//     tile against its bound placement and, ONLY when at least one tile
//     changed, re-runs the person bind so the next frame draws the person at
//     its new position. Frames where nothing moved cost a roster scan and
//     nothing else (no rebind). Returns the number of persons whose position
//     changed (0 == nothing to do).
//
// NAMED GAPS (rule 8):
//  * the rebind-on-move is the documented stand-in for a per-instance move:
//    CityView3D exposes no "move bound person" mutator (the bound placement
//    is captured at BindPersons), so a position change re-runs the bind —
//    correct but it recreates the factory records and restarts the pose
//    phase. The precise handoff (CityView3D::MoveBoundPerson) is written in
//    progress/living-city-wave4.md for the view's owner.
//  * clip selection for MOVING persons (the gait "bewegung/gehen" vs the idle
//    "stehen/stehen_newnoise") is the NpcAction/charaction runtime (the
//    already-documented clip-selection gap): the bind plays the factory
//    preload set with the idle first, movement state notwithstanding.
// ===========================================================================

// Capture the current bound seats + install the movement-aware placement
// override into `opt` AND the view's person hook slot. Returns the number of
// seats captured. Call AFTER WireSessionPersons3D (the default bind).
int InstallSessionPersonsMovePlacement(CityView3D& view,
                                       SessionPersons3DOptions& opt,
                                       const guild::render::Heightmap* hm);

// Per-frame: rebind ONLY when a bound person's live movement tile moved away
// from its bound placement. Returns the number of persons that changed
// position this frame (a rebind ran iff > 0).
int UpdateSessionPersons3DPositions(CityView3D& view,
                                    const SessionPersons3DOptions& opt);

// Re-capture the static seats from the CURRENT bound roster (call after a
// rebind triggered by sim changes — e.g. quickload — so newly bound persons'
// entrance-dummy seats become the captured fallbacks too).
int RecaptureSessionPersonsSeats(CityView3D& view);

// Rebind after a SIM RELOAD (e.g. quickload): re-runs the DEFAULT
// entrance-dummy bind first (so a NEW person's genuine seat is computed and
// captured — the movement override would otherwise leave it unplaced), then
// re-installs the movement placement and rebinds with it. Requires
// InstallSessionPersonsMovePlacement to have run (else equals Rebind).
SessionPersons3DStatus RebindSessionPersonsAfterSimChange(
    CityView3D& view, SessionPersons3DOptions& opt);

// Clear the captured-seat store + the movement placement state (test/teardown
// helper; the override installed into a view's hooks is cleared by
// UnwireSessionPersons3D as before).
void ClearSessionPersonsMovePlacement();

} // namespace guild::play
