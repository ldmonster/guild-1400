#pragma once
// =============================================================================
// guild::play — SESSION CAMERA (the in-city camera, driven by the REAL
// reconstructed camera cluster).
//
// SessionCamera is the session-facing adapter over the 1:1 reconstructed
// camera state machine of gilde.exe. It owns the reconstructed state
// (CameraObject == the original's dword_13FCD1C scene node; CameraState /
// Camera2State / Camera2Input / CameraUpdateState == the process globals) and
// each frame calls the REAL per-frame dispatcher:
//
//   render::Camera_Update              @0x4b4c68  (camera_update_recon)
//     -> CameraUpdateHooks::updatePan  @0x4b365c  -> play::CameraUpdatePan
//        (the reconstructed edge/key pan core, camera_controls — keyboard
//        arrows / screen-edge scroll, velocity ramp/decay, zoom-scaled speed)
//     -> render::Camera_UpdateMovement @0x4b41a8  (camera_recon2 — the
//        drag-latch -> pan / wheel-zoom / rotate state machine; the wheel
//        branch ends in Camera_AnchorToTerrain @0x4b2900 which recomputes the
//        eye height + pitch from the zoom fraction)
//     -> render::Camera_ClampToTerrainHeight @0x4b2a0c (gated off by default:
//        byte_6316D8 == 0; terrain hook is inert headless)
//
// The adapter is a THIN BRIDGE: it only translates SDL-shim input into the
// original's input-global model (Camera2Input/CameraInput) and reads the
// resulting camera object back out. All motion math is the reconstructed code.
//
// INPUT MAPPING (shim -> original input globals):
//   mouse x/y      -> unk_67220E>>16 / dword_672210>>16 (+ the 672170/672174
//                     view mirrors — the original input layer keeps them equal)
//   right button   -> dword_672238 (drag active)
//   left  button   -> dword_672220 (pan/tilt button; left+right = tilt-zoom
//                     pan branch, right alone = rotate/orbit branch)
//   wheel notches  -> dword_672254 accumulator (dword_672250 = consumed count,
//                     advanced after the frame — the wheel-zoom branch fires on
//                     the 672254 != 672250 delta exactly like the original)
//   arrows / edge  -> the updatePan hook resolves them with the REAL edge
//                     decision core (play::ResolveEdgeScroll) and runs the REAL
//                     pan core (play::CameraUpdatePan @0x4b365c)
//   scrollSpeed    -> dword_1233564 (the INI [Game] scroll_speed option read at
//                     0x56bcd3 in VIBE_Config_ReadGfxAndSoundSettings @0x56b834;
//                     the UpdateMovement rotate branch scales by it * 6/99)
//
// RENDER CONSUMPTION: eyeX()/eyeZ() are the camera object's +76/+84 position;
// zoom() is flt_6316DC (the engine zoom fraction, [0,1]); pixelsPerUnit() maps
// it exactly as the city frame compositor does (city_frame.cpp:
// opt.pixelsPerUnit = 1.0f + camera.zoom), so the result plugs straight into
// RealCityRenderer::Options { eyeX, eyeZ, pixelsPerUnit }.
//
// Wiring status: session integration lands in sdl_session.cpp wave 2 (this
// module + tests + doc are the wave-1 deliverable; sdl_session.cpp is owned by
// another agent this wave).
// =============================================================================
#include "guild/common/types.h"
#include "shim/IPlatform.h"             // shim::MouseState
#include "render/camera_update_recon.h" // Camera_Update + state models
#include "render/camera_edge_scroll.h"  // Camera_EdgeScroll @0x4b2c34 (re-attach)
#include "render/heightmap.h"           // Heightmap + AverageAreaHeight @0x427468
#include "camera_controls.h"            // CameraControl / CameraUpdatePan core

namespace guild::play {

// ---------------------------------------------------------------------------
// CameraPose — the full camera orientation for the real-3D city view, the
// engine way (field offsets verified in IDA, see session_camera.h notes):
//   eye  = camera node +76/+80/+84 (VIBE_Object_SetPosition @0x5af38c writes
//          dword lanes [19..21]); identical to the WORLD translation
//          +92/+96/+100 for the parent-less city camera node (the values
//          EdgeScroll's center commit @0x4b2d0b and the ChooseCity MegaCam
//          read) — the adapter mirrors them every frame.
//   rot  = the node rotation euler +132/+136/+140 (written by
//          VIBE_Object_SetWorldTranslation @0x5af50c); identical to the WORLD
//          rotation euler +144/+148/+152 for the parent-less node. The engine
//          view basis is VIBE_Math_MatrixFromEuler(-rot) -> node+396 (camera
//          nodes carry type byte *(node+533) == 3, which negates the euler
//          before the matrix build — see 0x5af552..0x5af58e).
//   rotX = pitch (AnchorToTerrain @0x4b2900: baseAngle + span * zoom),
//   rotY = yaw   (UpdateMovement pan branch @0x4b43f1 / RotateView @0x4b30a2),
//   rotZ = roll  (never written by the camera movers; stays 0).
// ---------------------------------------------------------------------------
struct CameraPose {
    float eyeX, eyeY, eyeZ;
    float rotX, rotY, rotZ;
};

struct SessionCamera {
    // Build the camera at (eyeX, eyeZ) for an fbW x fbH framebuffer. Runs the
    // dispatcher once BEFORE the camera object exists (the original boot
    // order), so the early path of VIBE_Camera_Update initializes the screen
    // edge box (dword_62D0C4..D4) 1:1; then materializes the camera node and
    // anchors it with the REAL Camera_AnchorToTerrain @0x4b2900 at zoom 0.
    void Init(float eyeX, float eyeZ, int fbW, int fbH);

    // Advance one frame through the REAL dispatcher. dtMs is milliseconds
    // since the previous frame (0 => the pan core's "no previous frame"
    // default factor). wheelDelta is in wheel notches (+ = zoom in).
    void Frame(const shim::MouseState& ms, bool keyLeft, bool keyRight,
               bool keyUp, bool keyDown, float wheelDelta, float dtMs);

    // Renderer-facing accessors (RealCityRenderer::Options consumes these).
    float eyeX() const { return obj.posX; }            // node +76
    float eyeZ() const { return obj.posZ; }            // node +84
    float zoom() const { return cs.zoomT; }            // flt_6316DC, [0,1]
    float pixelsPerUnit() const { return 1.0f + cs.zoomT; } // city_frame map

    // Full pose for the real-3D city view (see CameraPose above): eye = node
    // +76/+80/+84, rot = the node rotation euler +132/+136/+140. The consumer
    // builds the engine view basis as MatrixFromEuler(-rot) (0x5af50c).
    CameraPose pose() const {
        return CameraPose{obj.posX, obj.posY, obj.posZ,
                          obj.worldX, obj.worldY, obj.worldZ};
    }

    // Bind the REAL terrain sampler: VIBE_Terrain_AverageAreaHeight @0x427468
    // (reconstructed as render::AverageAreaHeight, heightmap.h) over the
    // loaded city heightmap, and enable the per-frame terrain-follow gate
    // byte_6316D8 so the dispatcher runs Camera_ClampToTerrainHeight @0x4b2a0c
    // each frame (the eye Y eases toward terrain + baseHeight + span*zoom).
    // The wheel-zoom AnchorToTerrain @0x4b2900 path samples the same hook.
    // Pass nullptr to unbind (inert terrain, gate off). Survives Init (call in
    // either order).
    void BindTerrain(const render::Heightmap* hm);

    // --- options -----------------------------------------------------------
    // dword_1233564 — INI [Game] scroll_speed (options slider, range 0..99;
    // the rotate branch scales mouse-drag motion by scrollSpeed * 6.0 / 99).
    // 50 = slider midpoint; the original default comes from the INI reader's
    // caller (not a binary constant), so it is a configurable knob here.
    int scrollSpeed = 50;
    // Edge-scroll band in pixels. The original's edge box uses width-8 for the
    // right edge (see the box init in Camera_Update's early path).
    int edgeMargin = 8;

    // --- the reconstructed state (exposed for tests / advanced wiring) ------
    render::CameraObject      obj;   // dword_13FCD1C camera scene node
    render::CameraState       cs;    // flt_6316xx / dword_62D4xx globals
    render::Camera2State      st2;   // dword_631Exx / 62D0xx / 11BCxxx globals
    render::Camera2Input      in2;   // dword_672xxx input snapshot
    render::CameraInput       in1;   // first-pass mirror of the same inputs
    render::CameraUpdateState us;    // dword_631628
    render::CameraUpdateInput uin;   // dispatcher gates + packed screen size
    CameraControl             pan;   // UpdatePan @0x4b365c core state
                                     // (velocity accumulators 631DF0/631DF8)

private:
    render::CameraHooks       h1_;
    render::Camera2Hooks      h2_;
    render::CameraUpdateHooks uh_;
    PanInput panDir_;                // staged held-direction for the pan hook
    float    dtMs_ = 0.0f;           // staged frame time for the pan hook
    float    wheelFrac_ = 0.0f;      // sub-notch wheel remainder
    int      fbW_ = 0, fbH_ = 0;
    const render::Heightmap* terrain_ = nullptr;     // BindTerrain target

    static guild::i32 UpdatePanThunk(void* user);    // the 0x4b365c hook
    // the 0x427468 hook bound to render::AverageAreaHeight over `terrainUser`
    static render::f32 TerrainThunk(guild::i32 a1, guild::i32 a2,
                                    const render::f32* world, void* user);
    void applyTerrainBinding_();     // (re)wire h1_/uin after Init or Bind
};

} // namespace guild::play
