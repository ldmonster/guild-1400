// session_camera.cpp — SessionCamera: the thin session adapter over the REAL
// reconstructed camera cluster (VIBE_Camera_Update @0x4b4c68 and everything it
// dispatches). See session_camera.h for the mapping table and wiring status.
#include "session_camera.h"

#include <cstring>

namespace guild::play {

using guild::i32;

// ---------------------------------------------------------------------------
// The 0x4b365c hook: run the reconstructed pan core (play::CameraUpdatePan)
// over the staged held-direction/frame-time, then commit the pan-core eye into
// the camera scene node — the role VIBE_Object_SetPosition plays inside the
// original UpdatePan. Returns nonzero iff the camera panned (the original's
// eax result that the dispatcher stores into dword_631628).
// ---------------------------------------------------------------------------
i32 SessionCamera::UpdatePanThunk(void* user) {
    SessionCamera* self = static_cast<SessionCamera*>(user);
    // VIBE_Camera_UpdatePan entry gate @0x4b3685/0x4b3692: when the camera is
    // detached (dword_62D4E8 || dword_62D4E4) the original jumps straight to
    // the loc_4B3B7D tail — return 0 without touching the velocity
    // accumulators. Re-attach happens through Camera_EdgeScroll's commit.
    if (self->in2.altMoveMode || self->in2.disableMove)
        return 0;
    PanStep stp = CameraUpdatePan(self->pan, self->panDir_, self->dtMs_);
    // Commit eye X/Z (the pan core moves only the ground-plane axes; Y is
    // owned by the zoom/movement path through AnchorToTerrain).
    self->obj.posX = self->pan.eye[0];
    self->obj.posZ = self->pan.eye[2];
    return stp.moved ? 1 : 0;
}

// ---------------------------------------------------------------------------
// The 0x427468 hook: the REAL VIBE_Terrain_AverageAreaHeight, reconstructed as
// render::AverageAreaHeight (heightmap.cpp), sampling the bound Heightmap at
// the camera node position (the world triple the call sites pass in eax).
// a1/a2 are the original's dead leftover registers (ignored, like the callee).
// ---------------------------------------------------------------------------
render::f32 SessionCamera::TerrainThunk(guild::i32 /*a1*/, guild::i32 /*a2*/,
                                        const render::f32* world, void* user) {
    return (render::f32)render::AverageAreaHeight(
        static_cast<const render::Heightmap*>(user), world);
}

void SessionCamera::applyTerrainBinding_() {
    if (terrain_) {
        h1_.terrainHeight = &SessionCamera::TerrainThunk;
        h1_.terrainUser   = const_cast<render::Heightmap*>(terrain_);
        uin.byte6316D8    = 1;   // byte_6316D8: terrain clamp gate ON
    } else {
        render::CameraHooks d = render::Camera_DefaultHooks();
        h1_.terrainHeight = d.terrainHeight;   // inert 0
        h1_.terrainUser   = nullptr;
        uin.byte6316D8    = 0;
    }
}

void SessionCamera::BindTerrain(const render::Heightmap* hm) {
    terrain_ = hm;
    applyTerrainBinding_();
}

void SessionCamera::Init(float eyeX, float eyeZ, int fbW, int fbH) {
    fbW_ = fbW;
    fbH_ = fbH;

    h1_ = render::Camera_DefaultHooks();
    h2_ = render::Camera2_DefaultHooks();
    uh_ = render::CameraUpdate_DefaultHooks();
    uh_.updatePan     = &SessionCamera::UpdatePanThunk;
    uh_.updatePanUser = this;
    applyTerrainBinding_();          // re-apply a BindTerrain done before Init

    // dword_69FFBC packed screen extents: HIWORD = width (right edge becomes
    // width-8 in the dispatcher's box init), LOWORD = height (bottom edge).
    uin.d69FFBC = (fbW << 16) | (fbH & 0xFFFF);

    // Run the dispatcher once BEFORE the camera node exists — the original's
    // boot order (VIBE_Scene_RunMainFrameLoop calls Camera_Update every frame;
    // the first calls hit the !dword_13FCD1C early path, which initializes the
    // screen-edge box dword_62D0C4..D4 and the cursor mirrors).
    obj = render::CameraObject{};
    obj.present = false;
    in2.g_13FCD1C_present = 0;
    render::Camera_Update(obj, cs, st2, in2, in1, us, uin, h1_, h2_, uh_,
                          /*thisXLeftover*/ 0);

    // Materialize the camera node (the original's scene loader creates
    // dword_13FCD1C, then the camera is anchored to the terrain at the spawn).
    obj.present = true;
    in2.g_13FCD1C_present = 1;
    obj.posX = eyeX;
    obj.posZ = eyeZ;
    // Identity basis (camera node world matrix at +396; cols [0..2][3..5][6..8]).
    std::memset(obj.matrix, 0, sizeof(obj.matrix));
    obj.matrix[0] = 1.0f; obj.matrix[4] = 1.0f; obj.matrix[8] = 1.0f;
    obj.matrix[10] = 1.0f;

    // REAL anchor at zoom fraction 0: Camera_AnchorToTerrain @0x4b2900 sets
    // posY = terrain + baseHeight + span*0 (= 450 headless) and the pitch
    // worldX = baseAngle, plus the pos/world history mirrors.
    float zero = 0.0f;
    i32 zeroBits;
    std::memcpy(&zeroBits, &zero, sizeof(i32));
    render::Camera_AnchorToTerrain(obj, cs, h1_, /*x*/ 0, /*z*/ 0, zeroBits);

    // World-pose mirror (the scene graph's local->world propagation for the
    // parent-less camera node): +76 -> +92, +132 -> +144. EdgeScroll's center
    // commit and the 3D view's pose() read the world fields.
    obj.wposX = obj.posX;   obj.wposY = obj.posY;   obj.wposZ = obj.posZ;
    obj.wrotX = obj.worldX; obj.wrotY = obj.worldY; obj.wrotZ = obj.worldZ;

    // The pan core mirrors the node state it pans (eye = node +76, zoom =
    // flt_6316DC, screen size for the edge box).
    float eye[3] = {obj.posX, obj.posY, obj.posZ};
    pan = MakeCameraControl(eye, /*yaw*/ 0.0f, /*zoom*/ cs.zoomT, fbW, fbH);
}

void SessionCamera::Frame(const shim::MouseState& ms, bool keyLeft,
                          bool keyRight, bool keyUp, bool keyDown,
                          float wheelDelta, float dtMs) {
    // --- 1. input snapshot -> the original's input-global model -------------
    // The original input layer keeps the 672170/672174 "view" mirrors equal to
    // the 67220E/672210 cursor pair (see VIBE_Coord_ConvertY @0x40da48, which
    // writes 672174 and 672210 together).
    in2.viewShift = ms.x;            // SHIWORD(dword_672170)
    in2.d672174   = ms.y;            // dword_672174 >> 16
    in2.d67220E   = ms.x;            // (int)unk_67220E >> 16
    in2.d672210   = ms.y;            // dword_672210 >> 16
    in1.d672170   = ms.x;
    in1.d672174   = ms.y;
    in1.d67220E   = ms.x;
    in1.d672210   = ms.y;
    in2.d672238 = ms.right ? 1 : 0;  // dword_672238: drag active
    in2.d672220 = ms.left ? 1 : 0;   // dword_672220: pan/tilt button
    in1.d672238 = in2.d672238;
    in1.d672220 = in2.d672220;
    in2.d1233564 = scrollSpeed;      // dword_1233564: [Game] scroll_speed

    // Wheel notches -> the dword_672254 accumulator. The wheel-zoom branch of
    // UpdateMovement consumes the (672254 - 672250) delta; 672250 is advanced
    // to the handled count after the frame (the original input layer's role).
    wheelFrac_ += wheelDelta;
    const i32 steps = (i32)wheelFrac_;   // whole notches (trunc toward zero)
    wheelFrac_ -= (float)steps;
    in2.d672254 += steps;
    in1.d672254 = in2.d672254;
    in1.d672250 = in2.d672250;

    // --- 2. stage the pan-core inputs for the updatePan hook ----------------
    panDir_ = PanInput{};
    if (keyLeft)  panDir_.dirX = -1;
    if (keyRight) panDir_.dirX = +1;
    if (keyUp)    panDir_.dirZ = -1;
    if (keyDown)  panDir_.dirZ = +1;
    if (panDir_.dirX == 0 && panDir_.dirZ == 0)
        panDir_ = ResolveEdgeScroll(ms.x, ms.y, fbW_, fbH_, edgeMargin);
    dtMs_ = dtMs;
    // UpdatePan scales pan speed by the engine zoom fraction (flt_6316DC) and
    // starts from the node's current eye.
    pan.zoom   = cs.zoomT;
    pan.eye[0] = obj.posX;
    pan.eye[1] = obj.posY;
    pan.eye[2] = obj.posZ;

    // --- 3. the REAL per-frame dispatcher -----------------------------------
    render::Camera_Update(obj, cs, st2, in2, in1, us, uin, h1_, h2_, uh_,
                          /*thisXLeftover*/ 0);

    // World-pose mirror (the scene graph's local->world propagation for the
    // parent-less camera node): +76 -> +92, +132 -> +144. EdgeScroll's center
    // commit (below) and the 3D view's pose() read the world fields.
    obj.wposX = obj.posX;   obj.wposY = obj.posY;   obj.wposZ = obj.posZ;
    obj.wrotX = obj.worldX; obj.wrotY = obj.worldY; obj.wrotZ = obj.worldZ;

    // --- 4. post-frame input-layer bookkeeping ------------------------------
    // Consume the wheel pair (672250 catches up to 672254).
    in2.d672250 = in2.d672254;
    // The REAL re-attach: VIBE_Camera_EdgeScroll @0x4b2c34 (reconstructed in
    // render/camera_edge_scroll.*). Its live callers are the combat frame
    // loops (0x48c60f / 0x48c679 / 0x4905e9) and the city HUD click router
    // VIBE_Hud_HandleMouseClick @0x4bc4b1; the adapter stands in for that
    // per-frame cadence. UpdateMovement's drag-release path (0x4b4ae3) sets
    // dword_62D4E4 = 1 (camera detached); EdgeScroll's commit clears
    // dword_62D4E4/dword_62D4E8 at 0x4b2d95 when the cursor steps the edge
    // state back to center (a boundary row / opposite edge) — until then the
    // camera stays detached, exactly like the original.
    render::Camera_EdgeScroll(obj, cs, st2, in2, h2_);
}

} // namespace guild::play
