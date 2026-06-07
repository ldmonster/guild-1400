#pragma once
// Per-tick WALK-ON-PATH movement integration for the Guild simulation (gilde.exe).
//
// This is the float-heavy character movement the first charaction agent stubbed.
// It is the per-frame action step that advances a character along a precomputed
// tile waypoint list, snapping to terrain height (heightmap) and interpolating the
// facing toward each segment heading. Three engine entry points implement it:
//
//   VIBE_Command_Dispatcher           0x40a4d4  (MISNAMED — the original monolithic
//                                                walk-on-path executor "ch_WalkOnPath";
//                                                NOT the command router)
//   VIBE_CharAction_WalkUpdate        0x40a0b8  (refactored top-level action step)
//   VIBE_CharAction_WalkStep          0x4093b0  (refactored waypoint-advance step)
//   VIBE_CharAction_WalkOnPathRotation 0x409b2c (refactored heading interpolation)
//
// The 0x40a4d4 monolith and the 0x4093b0+0x409b2c split share IDENTICAL movement
// math (same constants, same control flow); they are two generations of the same
// routine. We translate the split form (it isolates the three concerns cleanly)
// and verify both share the same per-tick numbers.
//
// Render / animation / sound / mesh leaves are out of this module's scope. The
// movement state the integration reads/writes lives in three records the renderer
// owns; we model exactly those fields (offset comments below) as POD structs and
// route the few genuine render/anim side effects (attach walk anim, free buffer,
// play footstep) through a hook the host installs. The per-tick integration —
// waypoint advance, distance/speed, rotation interpolation, height snapping,
// arrival — is translated 1:1.
#include "guild/common/types.h"

namespace guild::render { struct Heightmap; }

namespace guild::sim {

// ===========================================================================
// Avatar / mesh world-transform record (the "v110" float* object in the
// decompilation; Character +20 -> avatar, avatar[13] -> 3D object, the object's
// +52 mesh holds the world transform). The walk integration reads/writes:
//   [1]   (byte) avatar flag byte (bit0 indoors, bit3 mounted/cart)
//   [4]   (byte) move-flag byte   (bit3 fast/cart movement)
//   +76,+80,+84  (float) current world position X,Y,Z  (== floats [19],[20],[21])
//   +132,+136,+140 (float) "set translation" X,Y,Z (heading angle is at +136)
//   +172  (dword) indoor/floor handle (nonzero -> indoor speed scale)
// In the engine +136 doubles as the heading (yaw) for the rotation interpolation;
// SetWorldTranslationXYZ(obj, x@+132, yaw@+136, z@+140) writes it back.
// ===========================================================================
struct WalkAvatar {
    u8    flag1;        // [1]    bit0 indoors, bit3 mounted/cart
    u8    moveFlags;    // [4]    bit3 fast/cart
    float posX;         // +76    world X            (mesh float[19])
    float posY;         // +80    world Y (height)   (mesh float[20])
    float posZ;         // +84    world Z            (mesh float[21])
    float heading;      // +136   facing yaw (radians, [0,2*pi))
    int   indoorFloor;  // +172   indoor floor handle (0 == outdoors)
};

// ===========================================================================
// Walk movement-animation node (the avatar[28] "active animation handle"). The
// integration drives the renderer through these fields:
//   +76,+80,+84  (float) target world position X,Y,Z (segment endpoint)
//   +92   (float) segment travel duration (ticks-to-target scalar)
//   +96   (float) playback speed (set from base move speed * terrain factor)
//   +109  (byte)  anim state flags (bit5 0x20 = "anim done / reached")
//   +110  (byte)  move flags (bit1 0x02 active, bit2 0x04 enable, bit3 0x08 stop)
// ===========================================================================
struct WalkAnim {
    float targetX;   // +76
    float targetY;   // +80
    float targetZ;   // +84
    float duration;  // +92
    float speed;     // +96
    u8    flags109;  // +109  bit5 (0x20) reached/done
    u8    flags110;  // +110  bit1 active / bit2 enable / bit3 stop
};

// ===========================================================================
// Character walk-state fields (offsets into the 32-bit Character record). The
// action node `a1` in the engine IS the Character base; these are the fields the
// walk integration owns. Recovered from WalkStep/WalkOnPathRotation/Dispatcher.
//   +12   (dword) active-path flag (0 == no path -> finish)
//   +240  (dword) waypoint capacity (256)
//   +244  (byte*) waypoint buffer: 2 bytes/tile (col,row), 0x200 alloc
//   +248  (dword) current waypoint index
//   +252  (dword) -1 sentinel
//   +260  (dword) "morph movement initialised" gate (0 until anim attached)
//   +264  (float) pending turn angle (signed; <0 means turning, ==0 done)
//   +268  (float) current segment length (world units)
//   +292,+296,+300 (float) current target world X,Y,Z
//   +340  (dword) morph/transition stamp (-1 none)
//   +400  (byte)  abort flag
// ===========================================================================
struct WalkState {
    int   activePath;   // +12
    int   waypointCap;  // +240
    u8*   waypoints;    // +244  (col,row) pairs
    int   waypointIdx;  // +248
    int   sentinel;     // +252
    int   morphInit;    // +260
    float turnAngle;    // +264
    float segLength;    // +268
    float targetX;      // +292
    float targetY;      // +296
    float targetZ;      // +300
    int   morphStamp;   // +340
    u8    abort;        // +400

    WalkAvatar* avatar;        // Character +20 -> avatar
    WalkAnim*   anim;          // avatar[28] -> active walk anim (null until init)
    const render::Heightmap* mesh;  // avatar mesh's heightmap (terrain sampler)
};

// ===========================================================================
// Per-tick frame clock. The integration scales rotation/movement by the elapsed
// game-ticks (dword_62D008 - dword_62D004). These globals are set by
// Character_Update bracketing the frame; we expose them so a test can drive the
// integration tick-by-tick deterministically.
//   dword_62D008 @0x62D008  frame end-tick   (dword_62EB38 latch)
//   dword_62D004 @0x62D004  frame start-tick
// ===========================================================================
extern int g_tickEnd;    // dword_62D008
extern int g_tickStart;  // dword_62D004

// ---------------------------------------------------------------------------
// Hook for the render/anim/sound leaves the walk steps touch but that are out of
// this module's scope. A test installs a recording/inert mock.
// ---------------------------------------------------------------------------
struct WalkHooks {
    // Attach the walk movement animation (VIBE_Character_AttachAni "bewegung/gehen"
    // or "bewegung/karren_ziehen"); returns the WalkAnim* on success or null.
    WalkAnim* (*attachWalkAnim)(WalkState* st, int cart);
    // Query the terrain-type byte of the tile under the avatar and snap the
    // avatar height to it (VIBE_Character_QueryTileAhead). Returns the type byte.
    int (*queryTileAhead)(WalkState* st);
    // Play a footstep sound for the given terrain type (no-op in tests).
    void (*footstep)(WalkState* st, int tileType);
    // Detach the walk anim on completion (VIBE_Character_DetachMorphAni).
    void (*detachAnim)(WalkState* st);
};
void SetWalkHooks(const WalkHooks* hooks);
const WalkHooks& GetWalkHooks();

// ===========================================================================
// Rotation interpolation toward the segment heading (the core of
// VIBE_CharAction_WalkOnPathRotation 0x409b2c and the rotation block of
// VIBE_Command_Dispatcher 0x40a4d4).
// ---------------------------------------------------------------------------
// Given the signed angle `delta` (radians, target - current; the engine's +264
// turn angle, recomputed via Math_AngleToTargetSigned each tick) and the current
// heading `cur`, advance the heading by one tick's worth of turn and return it.
//
// Step size = elapsedTicks * 0.9090909... (per-tick base, dbl_61089C), * 0.4 if
// the cart/fast move flag (avatar.moveFlags bit3) is set; then scaled by how far
// from aligned we are (|delta| buckets at pi/2, pi/3, pi/6 give factors 0.2, 0.14,
// 0.04*1.5, 0.04). If |delta| > 2*pi/3 (and not mounted) the cap is 0.2. The
// heading is fmod'd into [0,2*pi); if we would overshoot the target it snaps to
// the target and `*done` is set (turn complete -> +264 cleared to 0).
//
// `elapsedTicks` = g_tickEnd - g_tickStart. `mounted` = avatar.flag1 bit3.
// Returns the new heading; if `done` non-null, *done = 1 when the turn completed.
float WalkRotateTowardHeading(float cur, float delta, int elapsedTicks,
                              bool fastMove, bool mounted, int* done);

// gilde.exe 0x409b2c — VIBE_CharAction_WalkOnPathRotation. Recomputes the signed
// angle to the current target (+292..300) via the avatar mesh, then interpolates
// the avatar heading toward it for this tick (WalkRotateTowardHeading), writing it
// back through the world transform. When +264 is exactly 0 (aligned) it instead
// checks the threshold: if the avatar has overshot its segment endpoint by >1.4x
// the segment length it flags the anim "missed threshold" (bit3 stop). The signed
// recompute is provided by the caller-installed angle oracle (avatar.heading is
// the live yaw; the geometric angle-to-target is computed here from positions).
void WalkOnPathRotation(WalkState* st);

// ===========================================================================
// Waypoint advance + per-segment speed (the core of VIBE_CharAction_WalkStep
// 0x4093b0 and the advance block of VIBE_Command_Dispatcher 0x40a4d4).
// ===========================================================================
// gilde.exe 0x4093b0 — VIBE_CharAction_WalkStep. Advances +248 to the next
// waypoint; samples its world position from the heightmap (height snapping);
// recomputes the signed turn angle and the segment length |targetWorld - avatarPos|;
// writes the segment target + duration into the walk anim; sets the anim active
// flags. Returns 1 to continue, 0 when the path is exhausted (caller finishes).
// `frameStep` is the avatar mesh tile scale (a2+16 in the engine == 1/scaleX),
// used in the arrival-segment speed clamp.
int WalkStep(WalkState* st, float frameStep);

// Computes the per-segment travel `duration` written to anim+92 for the waypoint
// at index `idx` (WalkStep's v42 logic): 20 (40 mounted) normally; 8 (24 mounted)
// on the final segment; * 1.2 (dbl_610814) when indoors. Pure; exposed for test.
float WalkSegmentDuration(int idx, int waypointCap, bool mounted, bool indoors);

// Computes the anim playback speed written to anim+96 (the speed-ramp block at the
// end of WalkUpdate / Dispatcher): baseSpeed * terrainFactor * tileFactor * ramp,
// where terrainFactor depends on the tile type ahead (6/11 == stairs/water uses a
// different multiplier). Pure; exposed for test.
//   baseSpeed = avatar base move speed; ramp in [0,1]; tileFactor 0.7 (or 1.0
//   indoor-floor); the four multipliers are flt_6108F0..FC = {2.2,2.5,1.7,1.9}.
float WalkAnimSpeed(float baseSpeed, float ramp, float tileFactor,
                    int tileType, bool mounted);

// ===========================================================================
// Top-level walk action step (VIBE_CharAction_WalkUpdate 0x40a0b8): drives one
// tick of the walk — height snapping (queryTileAhead), the speed ramp toward 1.0,
// WalkStep, then WalkOnPathRotation, then the anim speed update. This is the
// per-frame entry the action queue calls. Returns 1 while walking, 0 on finish.
// ===========================================================================
int WalkUpdate(WalkState* st, float frameStep);

} // namespace guild::sim
