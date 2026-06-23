#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::render — VIBE_Camera_* reconstruction cluster (camera_recon).
//
// 1:1 translations of the camera-control functions of gilde.exe. These drive
// the single global camera scene object (the original's dword_13FCD1C, a NIF/
// scene node with a packed float layout) in response to mouse/keyboard input
// state and the terrain. All arithmetic, constants, integer wraparound and
// control flow follow the Hex-Rays reference exactly.
//
// The genuinely-coupled leaves the originals call (terrain height sampling,
// mesh height-range, the object position/world-translation setters, the vector
// math helpers, the coordinate truncators, the person-record lookup, the
// scene-graph walk, the 3D-sound listener) are NOT in this cluster. They are
// routed through CameraHooks with INERT defaults, so the pure control flow and
// arithmetic are reconstructed faithfully and are golden-testable in isolation.
//
// The originals read/write a single global camera object via raw byte offsets
// off dword_13FCD1C. We model that object as CameraObject with the SAME byte
// offsets (documented per field). The process-state globals (dword_631Exx,
// flt_6316xx, dword_62D4xx, dword_11BCxxx ...) become CameraState; the input
// state globals (dword_672xxx, unk_67220E) become CameraInput.
//
// Reconstructed functions (verified untranslated across src/ by addr + name):
//   0x43f528  VIBE_Camera_CmdCameraFlight        (script cmd: kick camera flight)
//   0x43f5dc  VIBE_Camera_CmdCameraFlightTimed   (script cmd: timed camera flight)
//   0x4b2900  VIBE_Camera_AnchorToTerrain        (snap height to terrain + lerp)
//   0x4b2a0c  VIBE_Camera_ClampToTerrainHeight   (clamp Y toward terrain, eased)
//   0x4b300c  VIBE_Camera_RotateView             (mouse-drag orbit/rotate)
//   0x4b41a8  VIBE_Camera_UpdateMovement         (per-frame pan/zoom/rotate state)
//   0x4b5250  VIBE_Camera_ZoomReset              (zoom-to-object reset)
//   0x4b562c  VIBE_Camera_OrientToTarget         (point camera + set 3D listener)
//   0x4b5974  VIBE_Camera_ZoomOut                (zoom out from object)
//   0x4c20ec  VIBE_Camera_ComputeZoomScale       (wealth-driven zoom distance)
//   0x5e967c  VIBE_Camera_UpdateTrackTargetFromMouse (track/orbit from mouse delta)
// =============================================================================

namespace guild::render {

// types.h carries the integer typedefs; the original's floats are 32/64-bit
// IEEE, so we alias them locally (the shared header defines no f32/f64).
using f32 = float;
using f64 = double;

// ---------------------------------------------------------------------------
// CameraObject — the global camera scene node (original dword_13FCD1C).
// Field byte offsets match the original raw accesses exactly. The original
// treats it as a flat blob; only the offsets touched by the camera code are
// modeled. `present` mirrors "dword_13FCD1C != 0" (the object exists).
// ---------------------------------------------------------------------------
struct CameraObject {
    bool present = false;
    // position vector (local translation): +76 / +80 / +84
    f32 posX = 0.0f;   // +76
    f32 posY = 0.0f;   // +80
    f32 posZ = 0.0f;   // +84
    // +120 / +124 / +128 — secondary position (used when dword_62D4E8 set)
    f32 pos2X = 0.0f;  // +120
    f32 pos2Y = 0.0f;  // +124
    f32 pos2Z = 0.0f;  // +128
    // world translation / orientation triple: +132 / +136 / +140
    // NOTE on semantics (verified at VIBE_Object_SetWorldTranslation @0x5af50c):
    // this triple is the node's LOCAL ROTATION EULER. The setter writes it and
    // rebuilds the basis at +396 via VIBE_Math_MatrixFromEuler(-euler) when the
    // node type byte *(node+533) == 3 (the camera node's type). The historical
    // field names worldX/Y/Z are kept for source stability.
    f32 worldX = 0.0f; // +132 (rotation euler X = pitch)
    f32 worldY = 0.0f; // +136 (rotation euler Y = yaw)
    f32 worldZ = 0.0f; // +140 (rotation euler Z = roll)
    // world-space pose — the scene-graph propagated copies of the local pose,
    // read by VIBE_Camera_EdgeScroll's center commit (0x4b2d0b: +92/+0x90) and
    // the ChooseCity MegaCam (eye = node+92, basis = MatrixFromEuler(-(node+144))).
    // For the PARENT-LESS city camera node world == local; the session adapter
    // mirrors +76->+92 and +132->+144 each frame.
    f32 wposX = 0.0f;  // +92  world translation X
    f32 wposY = 0.0f;  // +96  world translation Y
    f32 wposZ = 0.0f;  // +100 world translation Z
    f32 wrotX = 0.0f;  // +144 world rotation euler X
    f32 wrotY = 0.0f;  // +148 world rotation euler Y
    f32 wrotZ = 0.0f;  // +152 world rotation euler Z
    // the four screen-edge view blocks (pos[3] + rot[3] each) committed by
    // VIBE_Camera_EdgeScroll @0x4b2c34 and probed by
    // VIBE_Camera_UpdateCombatScroll @0x487b2c (see app/combat_scroll.h):
    f32 edgeTop[6]    = {0}; // +180..+200 (0xB4) — top-edge view
    f32 edgeBottom[6] = {0}; // +204..+224 (0xCC) — bottom-edge view
    f32 edgeLeft[6]   = {0}; // +228..+248 (0xE4) — left-edge view
    f32 edgeRight[6]  = {0}; // +252..+272 (0xFC) — right-edge view
    // world matrix at +396, 16 floats (3x3 used as basis cols)
    f32 matrix[16] = {0};
    // the original also reads packed _DWORD lanes [19..21]=+76.., [33..35]=+132..
    // those alias posX.. and worldX.. respectively; we expose them via helpers.
};

// ---------------------------------------------------------------------------
// CameraState — the mutable process globals the camera code touches.
// ---------------------------------------------------------------------------
struct CameraState {
    // flight / interpolation height params (originals are read-only consts here):
    //   flt_6316B4=450, flt_6316B8=-0.471238911, flt_6316BC=1600, flt_6316C0=-1.082104
    f32 baseHeight  = 450.0f;        // flt_6316B4
    f32 baseAngle   = -0.471238911151886f; // flt_6316B8
    f32 spanHeight  = 1600.0f;       // flt_6316BC
    f32 spanAngle   = -1.0821040868759155f; // flt_6316C0
    f32 zoomT       = 0.0f;          // flt_6316DC (also LODWORD)
    i32 zoomTBits   = 0;             // dword_6316E0 (alias of zoomT2 below as float)
    i32 minZoomDist = 50;            // dword_6316C8

    // gate / mode flags
    i32 disableMove = 0;             // dword_62D4E4
    i32 altMoveMode = 0;             // dword_62D4E8
    i32 globalDisabled = 0;          // dword_649D60 (AnchorToTerrain early-out)

    // clamp result + scratch
    i32 clampResultBits = 0;         // dword_631DDC

    // rotate-view persistence
    i32 rotPrevX = 0;                // dword_631DE4
    i32 rotPrevY = 0;                // dword_631DE8
    i32 rotActive = 0;               // dword_631DEC

    // mirror copies (dword_11BC2D4..2EC) of pos/world (history/save buffers)
    i32 hist_2D4 = 0, hist_2D8 = 0, hist_2DC = 0;  // pos copy
    i32 hist_2E4 = 0, hist_2E8 = 0, hist_2EC = 0;  // world copy

    // the dword_631610==dword_631618 && (!dword_631744 || dword_631748) gate
    i32 g_631610 = 0, g_631618 = 0, g_631744 = 0, g_631748 = 0;
};

// ---------------------------------------------------------------------------
// CameraInput — mouse/keyboard input snapshot globals.
// The originals read packed fixed-point screen deltas via (x>>16) on
// dword_672170/4/210, byte buttons, etc. Modeled raw.
// ---------------------------------------------------------------------------
struct CameraInput {
    i32 d672170 = 0;   // SHIWORD(dword_672170) = (d672170+2-byte view) >> 16
    i32 d672174 = 0;   // dword_672174 >> 16
    i32 d67220E = 0;   // unk_67220E >> 16
    i32 d672210 = 0;   // dword_672210 >> 16
    i32 d672220 = 0;   // dword_672220 (rotate-button held)
    i32 d672230 = 0;   // dword_672230
    i32 d672234 = 0;   // dword_672234
    i32 d672238 = 0;   // dword_672238 (drag active)
    i32 d67221C = 0;   // dword_67221C
    i32 d672250 = 0;   // dword_672250
    i32 d672254 = 0;   // dword_672254
    u8  byte671D6F = 0; // byte_671D6F (invert axis)
};

// ---------------------------------------------------------------------------
// CameraHooks — genuine cross-module leaves, INERT by default.
// ---------------------------------------------------------------------------
struct CameraHooks {
    // 0x427468 VIBE_Terrain_AverageAreaHeight(a1@ecx, a2@edx, world@eax) -> st0.
    // a1/a2 are DEAD leftover registers (the callee stores then never reads
    // them); the REAL argument is world = the camera node position triple
    // (node+76) — verified at both camera call sites (0x4b292a inside
    // AnchorToTerrain, 0x4b2a2b inside ClampToTerrainHeight). The reconstructed
    // body is render::AverageAreaHeight (heightmap.h, 8x8 box average over the
    // loaded heightmap); `terrainUser` carries the Heightmap*. Inert: 0.
    f32 (*terrainHeight)(i32 a1, i32 a2, const f32* world, void* user) = nullptr;
    void* terrainUser = nullptr;
    // 0x5af38c VIBE_Object_SetPosition(obj, float[3]) — sets +76/+80/+84.
    void (*setPosition)(CameraObject* obj, const f32* v) = nullptr;
    // 0x5af50c VIBE_Object_SetWorldTranslation(obj, float[3]) — sets +132/+136/+140.
    void (*setWorldTranslation)(CameraObject* obj, const f32* v) = nullptr;
    // 0x5d3fb2 VIBE_Math_Fmod(x, m) -> fmod. Inert default: real fmodf.
    f32 (*fmodHook)(f32 x, f32 m) = nullptr;
    // 0x5cabf0 VIBE_Math_MatrixCopy(srcMatPtr, dst[16]) — copies the 16-float basis.
    void (*matrixCopy)(const f32* src, f32* dst) = nullptr;
    // 0x5caa4c VIBE_Math_VectorWithinTolerance(a[3], b[3], tol) -> bool.
    bool (*withinTolerance)(const f32* a, const f32* b, f32 tol) = nullptr;
};

// Default-construct an inert hooks set whose terrainHeight=0, setters no-op.
CameraHooks Camera_DefaultHooks();

// ---------------------------------------------------------------------------
// Function declarations. CameraObject/CameraState/CameraInput are passed by
// reference to model the original globals; CameraHooks carries the leaves.
// ---------------------------------------------------------------------------

// gilde.exe 0x43f528 — VIBE_Camera_CmdCameraFlight (script command shim)
// __userpurge eax=a1(ptr), edx=a2(ptr). Returns 0/1. `slotObj`/`callbackMatches`
// model the dword_62E8CC dispatch-slot self-check; drawLabels models
// VIBE_Render_DrawTextLabels3D(a1[0]/17, 6, a2[0]); reportError logs failure.
struct CmdCameraFlightCtx {
    bool slotPresent = false;       // dword_62E8CC != 0
    bool slotIsSelf = false;        // *(slot+44) == this command
    bool movingFlags = false;       // dword_62D4E4 || dword_62D4E8
    bool scriptFlag1 = false;       // *(dword_62E8A8+2564) == 1
    // captured side effects:
    bool wroteSlot = false;         // wrote *(dword_62E8A8+2528) = this command
    i32  drawArg = 0;               // arg passed as a1[0]/divisor
    bool drawCalled = false;
    bool drawOk = true;             // result of VIBE_Render_DrawTextLabels3D
    bool reportedError = false;     // VIBE_Script_ReportError fired
};
i32 Camera_CmdCameraFlight(i32 a1_val, CmdCameraFlightCtx& ctx);
i32 Camera_CmdCameraFlightTimed(i32 a1_val, CmdCameraFlightCtx& ctx);

// gilde.exe 0x4b2900 — VIBE_Camera_AnchorToTerrain (a1=x, a2=z, a3=zoomT bits)
void Camera_AnchorToTerrain(CameraObject& obj, CameraState& st, const CameraHooks& h,
                            i32 a1, i32 a2, i32 a3);

// gilde.exe 0x4b2a0c — VIBE_Camera_ClampToTerrainHeight (this=x sample base)
void Camera_ClampToTerrainHeight(CameraObject& obj, CameraState& st, const CameraInput& in,
                                 const CameraHooks& h, i32 thisX);

// gilde.exe 0x4c20ec — VIBE_Camera_ComputeZoomScale
// Pure wealth->zoom-distance math. `area` models VIBE_Amt_FindNextActiveBuilding's
// out param (the &v14 divisor); `wealth` models VIBE_Person_ComputeTotalWealth.
// `truncMax` models the dword compared against the truncated result.
struct ComputeZoomScaleCtx {
    i32 area = 1;       // v14 (divisor; original divides by it)
    i32 wealth = 0;     // VIBE_Person_ComputeTotalWealth(...) (same each call here)
    i32 truncMax = 0;   // the v10 bound (>= result -> return 0)
};
i32 Camera_ComputeZoomScale(const ComputeZoomScaleCtx& ctx);

// gilde.exe 0x4b300c — VIBE_Camera_RotateView. Pure rotate math is in
// Camera_RotateView_PanRotated below (the orbit branch core); the full
// stateful dispatch is documented in the .cpp.

} // namespace guild::render
