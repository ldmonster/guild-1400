#pragma once
// charaction_motion — the FULL 1:1 translation of the walk-on-path motion
// executor monolith and its supporting motion primitives for the Guild
// simulation (gilde.exe). This is the faithful byte-offset port that complements
// the abstracted, hook-only walk integration in charaction_walk.{h,cpp}; here the
// engine's exact control flow, field offsets and float constants are reproduced.
//
// Translated functions:
//   VIBE_Character_StepMotionQueue            0x4041e8  (motion-queue advance)
//   VIBE_Math_AngleToTargetSigned             0x5b6d1c  (signed yaw-to-target)
//   VIBE_Object_SetWorldTranslationXYZ        0x5af5cc  (xyz -> object transform)
//   VIBE_Character_QueryTileAhead             0x4066bc  (terrain sample + h-snap)
//   VIBE_Command_Dispatcher                   0x40a4d4  (MISNAMED — the monolithic
//                                                       walk-on-path executor
//                                                       "ch_WalkOnPath")
//
// The motion executor reads/writes a handful of records the renderer owns. Rather
// than abstract them behind callbacks (as charaction_walk does), this module
// models them as offset-documented POD records and reproduces the engine math
// exactly. The genuine render/anim/sound LEAVES (attach anim, free mesh slot,
// play footstep, draw submeshes, scene-graph dirty walk) are out of this module's
// scope and routed through the MotionHooks table the host installs; tests install
// a recording/inert mock. Every numeric path — waypoint advance, distance/speed,
// rotation interpolation, height snapping, the "missed threshold" arrival check —
// is translated 1:1 from the Hex-Rays reference of record.
#include "guild/common/types.h"

#include "sim/charaction_walk.h"  // g_tickStart / g_tickEnd (frame brackets, shared)

namespace guild::render { struct Heightmap; }

namespace guild::sim {

// ===========================================================================
// Recovered IEEE-754 movement constants (decoded byte-for-byte via get_bytes;
// the monolith reads the @0x61093x..0x6109Cx copies, QueryTileAhead the @0x6106Cx
// copies, AngleToTargetSigned the @0x6286F8/0x628700 copies).
// ===========================================================================
extern const float  kRampStep;        // flt_610938 = 0.0099999998   (speed ramp +)
extern const double kIndoorDurScale;  // dbl_61093C = 3.0            (indoor seg dur)
extern const double kMissedThresh;    // dbl_610944 = 1.2            (overshoot ratio)
extern const double kTurnHi;          // dbl_61094C = 0.04           (aligned + thresh)
extern const double kTurnLo;          // dbl_610954 = -0.04          (aligned - thresh)
extern const double kTurnPerTick;     // dbl_61095C = 0.9090909...   (10/11 per tick)
extern const float  kCartTurn;        // flt_610964 = 0.40000001     (cart turn scale)
extern const double kRunThresh;       // dbl_61096C = 2.0943951      (2*pi/3)
extern const double kBucketHalfPi;    // dbl_610974 = 1.5707963      (pi/2)
extern const double kBucketPi3;       // dbl_61097C = 1.0471976      (pi/3)
extern const double kBucketPi6;       // dbl_610984 = 0.52359878     (pi/6)
extern const double kFac200;          // dbl_61098C = 2.0            (pi/6..pi/3 mult)
extern const double kFac014;          // dbl_610994 = 0.14
extern const double kFac020;          // dbl_61099C = 0.2
extern const double kFac032;          // dbl_6109A4 = 0.32           (run cap)
extern const double kTwoPi;           // dbl_6109AC = 6.2831853
extern const float  kSpeedMulFlat;    // flt_6109B4 = 2.2            (walk, flat)
extern const float  kSpeedMulStairs;  // flt_6109B8 = 2.5            (walk, tile 6/11)
extern const float  kSpeedMulCartFl;  // flt_6109BC = 1.7           (cart, flat)
extern const float  kSpeedMulCartSt;  // flt_6109C0 = 1.9            (cart, tile 6/11)
extern const float  kIndoorOne;       // dword_62D07C = 1.0          (indoor base factor)

// QueryTileAhead constants:
extern const double kStairStepGate;   // dbl_6106CC = -5.0           (height-delta gate)
extern const double kIndoorLift;      // dbl_6106D4 = 3.0            (outdoor->indoor lift)
extern const float  kHeightLerp;      // flt_6106DC = 0.25           (height smoothing)

// AngleToTargetSigned constants:
extern const double kDotFloor;        // dbl_6286F8 = -1.0           (acos clamp floor)
extern const double kHalfPiRot;       // dbl_628700 = 1.5707963      (pi/2 sign rotate)

// ===========================================================================
// Avatar / mesh world-transform record (the "v110" float* object in the engine;
// Character +20 -> avatar). The executor accesses it as a `float[]` / `int[]` at
// these dword indices ([k] == byte +4*k):
//   [1]   (byte) avatar flag byte  (bit0 indoors, bit3 mounted/cart)
//   [4]   (byte) move-flag byte    (bit3 fast/cart movement)
//   [11]  (int)  universe id       (compared to dword_62D080)
//   [13]  (int)  3D object ptr (the mesh whose +512 holds the indoor-floor flag)
//   [28]  (int)  active walk-anim handle  (the WalkAnim; 0 == none)
//   [31]  (int)  morph slot id (nonzero -> release morph mesh on (re)attach)
//   [32]  (float) morph blend (reset to 0 when (re)initialising)
//   [33],[34],[35] (float) the object world transform X, YAW, Z (SetWorldTranslationXYZ)
//   [34]  (int)  parent universe ptr (IndexFromPointer arg)
//   [73]  (int)  cart flag (nonzero -> "karren_ziehen" anim)
//   [104] (float) base move speed
//   [105] (float) speed ramp (0 -> 1)
// We mirror the executor-touched fields; render-only tail fields are opaque.
// ===========================================================================
// The mesh object (avatar+52) carries BOTH the world POSITION (dword indices
// [19],[20],[21] == bytes +76,+80,+84) and the world TRANSFORM (dword indices
// [33],[34],[35] == bytes +132,+136,+140; [34]/+136 is the heading/yaw). The walk
// math samples the position from [19..21] and writes the heading via
// SetWorldTranslationXYZ([33],[34],[35]). QueryTileAhead height-snaps [20] (posY)
// and calls SetPosition([19..21]). We carry both on one record (the renderer keeps
// them on the same object); for the determinism-relevant motion they co-vary.
struct MotionAvatar {
    u8    flag1;        // [1]    bit0 indoors, bit3 mounted/cart
    u8    moveFlags;    // [4]    bit3 fast/cart
    int   universeId;   // [11]   universe id   (== dword_62D080 -> in active universe)
    void* object3d;     // [13]   3D object ptr (object[+512] == indoor floor present)
    int   object3dIndoorFloor;  // object3d[+512]: nonzero -> indoor floor present
    float posX;         // [19] / +76   object world position X
    float posY;         // [20] / +80   object world position Y (height; snapped)
    float posZ;         // [21] / +84   object world position Z
    struct MotionAnim* anim;    // [28]  active walk-anim handle (null until attached)
    int   morphSlot;    // [31]   morph slot id (nonzero -> release on reattach)
    float morphBlend;   // [32]   morph blend  (reset to 0)
    float transX;       // [33] / +132  object world transform X
    float transYaw;     // [34] / +136  object world heading (yaw)
    float transZ;       // [35] / +140  object world transform Z
    void* parentUniverse;       // [34 as ptr] parent universe (IndexFromPointer arg)
    int   cart;         // [73]   cart flag
    float baseSpeed;    // [104]  base move speed
    float ramp;         // [105]  speed ramp (0..1)
};

// ===========================================================================
// Walk movement-animation node (avatar[28]). Field offsets (bytes into the node):
//   +76,+80,+84  (float) target world position X,Y,Z (segment endpoint)
//   +92   (float) segment travel duration (ticks-to-target scalar)
//   +96   (float) playback speed
//   +109  (byte)  anim state flags (bit5 0x20 = "reached/done")
//   +110  (byte)  move flags (bit1 0x02 active, bit2 0x04 enable, bit3 0x08 stop)
// ===========================================================================
struct MotionAnim {
    float targetX;   // +76
    float targetY;   // +80
    float targetZ;   // +84
    float duration;  // +92
    float speed;     // +96
    u8    flags109;  // +109  bit5 (0x20) reached/done
    u8    flags110;  // +110  bit1 active / bit2 enable / bit3 stop
};

// ===========================================================================
// The walk-action character record (the action-node `a1` base, which in the
// engine IS the Character record carrying the walk scratch). Field offsets into
// the 32-bit Character record (recovered from the Dispatcher monolith):
//   +12   (dword) active-path flag (0 == no path -> (re)build/attach)
//   +20   (ptr)   avatar (MotionAvatar)
//   +40   (ptr)   the action node (node+9 == action type; 56 == FinishSetVisible)
//   +44   (dword) universe id (== avatar[11] mirror, compared to dword_62D080)
//   +48,+52,+56  start tile column/row + start mesh handle (build phase)
//   +136  (ptr)   universe/scene record (parent universe; IndexFromPointer)
//   +140  (byte)  flag byte A (bit3 0x08 dirty-mesh)
//   +240  (dword) waypoint capacity (256)
//   +244  (byte*) waypoint buffer: 2 bytes/tile (col,row), 0x200 alloc
//   +248  (dword) current waypoint index
//   +252  (dword) -1 sentinel
//   +256  (byte)  last footstep terrain cell (avoid re-trigger)
//   +260  (dword) "morph movement initialised" gate (0 until anim attached)
//   +264  (float) pending turn angle (signed; <0 turning, ==0 aligned, >0 turning)
//   +268  (float) current segment length (world units)
//   +292,+296,+300 (float) current target world X,Y,Z
//   +340  (dword) morph/transition stamp (-1 none; +200 cooldown vs dword_62D008)
//   +400  (byte)  abort flag
// The waypoint buffer is the host-owned 512-byte block; we model it inline.
// ===========================================================================
constexpr int kWaypointAlloc = 0x200;   // 512 bytes == 256 (col,row) pairs

struct MotionCharacter {
    int   activePath;    // +12
    MotionAvatar* avatar;// +20
    struct { u8 actionType; bool present; } node; // +40 (only +9 type read here)
    int   universeId;    // +44
    int   startTileX;    // +48
    int   startTileY;    // +52  (passed as height byte in the engine's start-snap)
    int   startMesh;     // +56  (start mesh handle; unused by the math)
    void* universe;      // +136 (parent universe record; ->+172 indoor floor handle)
    int   universeIndoorFloor;  // universe[+172]: indoor floor handle (0 == outdoors)
    u8    flagsA;        // +140
    int   waypointCap;   // +240
    u8*   waypoints;     // +244 (col,row) pairs (null == none)
    int   waypointIdx;   // +248
    int   sentinel;      // +252
    u8    lastFootstep;  // +256
    int   morphInit;     // +260
    float turnAngle;     // +264
    float segLength;     // +268
    float targetX;       // +292
    float targetY;       // +296
    float targetZ;       // +300
    int   morphStamp;    // +340
    u8    abort;         // +400

    const render::Heightmap* mesh;  // ResolveMesh(avatar) result (terrain sampler)
    u8    waypointBuf[kWaypointAlloc];  // host-owned +244 backing store
};

// ===========================================================================
// Per-frame game-tick brackets (dword_62D004/62D008) and the throttle globals the
// monolith reads. Character_Update brackets the frame with the clock; the walk
// executor scales rotation by (dword_62D008 - dword_62D004) and gates the morph
// cooldown / footstep throttle off these.
//   dword_62D004 @0x62D004  frame start-tick   (shared with charaction_walk.h)
//   dword_62D008 @0x62D008  frame end-tick     (shared with charaction_walk.h)
//   dword_62D074 @0x62D074  footstep-enabled flag
//   dword_62D078 @0x62D078  footsteps-played-this-frame counter (cap 3)
//   dword_62D080 @0x62D080  active-universe id (== avatar[11] -> audible)
//   byte_62D011  @0x62D011  "scene changed" flag (set dirty-mesh on teardown)
// The frame-tick brackets g_tickStart/g_tickEnd are declared by charaction_walk.h
// (the two walk forms share them); we reuse those rather than redeclare. The
// footstep/active-universe/scene globals are module-local (prefixed to avoid the
// differently-typed g_activeUniverse in character_query.h).
// ===========================================================================
extern int g_motionFootstepEnabled; // dword_62D074
extern int g_motionFootstepCount;   // dword_62D078
extern int g_motionActiveUniverse;  // dword_62D080
extern u8  g_motionSceneChanged;    // byte_62D011

// ===========================================================================
// Render / anim / sound leaves the monolith calls (out of this module's scope).
// Inert defaults: attach returns a fresh MotionAnim (so the path can be walked),
// the mesh/sound calls are no-ops. A test installs a recording mock.
// ===========================================================================
struct MotionHooks {
    // VIBE_Character_AttachAni(avatar, name, loop): attach the walk movement anim;
    // returns the MotionAnim* stored into avatar[28], or null on failure.
    MotionAnim* (*attachAni)(MotionCharacter* ch, const char* name, int loop);
    // VIBE_Character_DetachMorphAni / morph teardown (avatar[28] cleared by caller).
    void (*detachMorphAni)(MotionCharacter* ch);
    // VIBE_Character_DrawSubMeshes(avatar[13]) + TouchMeshFrames: refresh meshes.
    void (*drawSubMeshes)(MotionCharacter* ch);
    // VIBE_Memory_FreeDebug(ch+244): release the waypoint buffer (host owns it; we
    // just null ch->waypoints, but tests can observe the free).
    void (*freeWaypoints)(MotionCharacter* ch);
    // VIBE_Sound_PlaySample + Sound3d_PlayOneShot for the footstep terrain sample.
    // `terrainType` is the tile-ahead cell; the engine builds "Normal_s"/"Erde_s"…
    // Returns nonzero if a sample actually played (the engine bumps the counter).
    int (*footstep)(MotionCharacter* ch, int terrainType, int indoors);
};
void SetMotionHooks(const MotionHooks* hooks);
const MotionHooks& GetMotionHooks();

// ===========================================================================
// VIBE_Object_SetWorldTranslationXYZ  (gilde.exe 0x5af5cc).
//   (__userpurge al=(obj@eax, x, yaw, z)). Packs (x,yaw,z) into a vec3 and calls
//   VIBE_Object_SetWorldTranslation, which writes obj+132/136/140 (the object's
//   world transform: X at +132, heading/yaw at +136, Z at +140) and rebuilds the
//   euler matrix. We translate the field write (the scene-graph dirty walk + euler
//   matrix are render leaves modeled by writing the avatar transform fields).
// ===========================================================================
void ObjectSetWorldTranslationXYZ(MotionAvatar* obj, float x, float yaw, float z);

// ===========================================================================
// VIBE_Math_AngleToTargetSigned  (gilde.exe 0x5b6d1c).
//   (__usercall st0=(mesh@eax, target@edx)). The engine transforms a forward
//   probe point and the mesh pivot through the avatar's bone chain to derive the
//   avatar's facing direction `facing` (a planar XZ unit vector) and pivot
//   `pivot`; the target direction `toTarget = normalize(target - pivot)`. The
//   signed angle is acos(clamp(dot(facing,toTarget),-1,1)), negated when the
//   target lies to one side (cross-product test: facing rotated +90 deg dotted
//   with toTarget > 0 -> flip sign).
//
// The bone-chain transform is render-coupled; the geometric core (the part that
// is determinism-relevant and testable) takes the two already-resolved world
// vectors `facing` (the avatar forward, need NOT be unit) and `pivot` (avatar
// position), plus the `target` world point, and returns the signed angle exactly
// as the engine computes it (normalize, dot, clamp, acos, cross-sign).
// `facing`/`toTarget` are XZ-planar in the engine (their Y is zeroed: v15=0,
// v24=0), which we reproduce.
float AngleToTargetSigned(const float facing[3], const float pivot[3],
                          const float target[3]);

// ===========================================================================
// VIBE_Character_StepMotionQueue  (gilde.exe 0x4041e8).
//   (__usercall eax=(node@eax)). The action-node motion-queue advance. node+20 is
//   the avatar; the avatar's motion handle lives at avatar+112 (a MotionAnim). On
//   the first poll (no handle and node+12 path flag clear) it attaches a motion
//   anim and returns the node's packed result (node+13 >> 24). Once the handle is
//   present and reports "reached" (anim+109 bit5) — or the abort flag (node+400)
//   is set — it prunes the attachment, clears the handle and returns -1 (done).
//   Otherwise it returns the packed result while the anim is still playing.
//
// Modeled record: a small MotionQueueNode mirroring the byte offsets the function
// reads. The bone-delta / prune render leaves are routed through MotionHooks-style
// callbacks on the node; the control flow + return codes are translated 1:1.
// ===========================================================================
struct MotionQueueNode {
    // node+20 -> avatar; avatar+112 -> MotionAnim handle; avatar+140 bit1 gates the
    // prune; node+12 path flag; node+13>>24 packed result; node+6 hi-byte motion id;
    // node+400 abort.
    MotionAnim** motionHandle;  // &avatar[112] (the slot AttachMotion fills)
    u8           motionId;      // HIBYTE(node+6): motion id passed to AttachMotion
    int          pathFlag;      // node+12
    i8           packedResult;  // SHIBYTE(node+13): node+13 >> 24 (signed)
    u8           abort;         // node+400
    u8           avatarFlag140; // avatar+140 (bit1 0x02 gates the prune)
    // Leaf: VIBE_Character_AttachMotion(avatar, motionId) -> MotionAnim* (or null).
    MotionAnim* (*attachMotion)(MotionQueueNode* n);
    // Leaf: prune the expired attachment (Anim_ComputeBoneDelta + Prune); no-op model.
    void (*pruneAttachment)(MotionQueueNode* n);
};

// Returns the engine's motion-queue result: the packed node result (>=0) while
// the anim plays / on first attach, or -1 when the queue is idle / reached / aborted.
int StepMotionQueue(MotionQueueNode* node);

// ===========================================================================
// VIBE_Character_QueryTileAhead  (gilde.exe 0x4066bc).
//   (__usercall eax=(ch@eax, meshArg@edi)). Resolves the avatar mesh, samples the
//   terrain type byte of the tile under the avatar (WorldToTileWithHeight ->
//   entries[24*idx] type byte), height-snaps the avatar's Y toward the sampled
//   terrain height (lerp by flt_6106DC == 0.25), and returns the terrain type.
//   When an indoor floor is present it picks the floor tile height and blends; a
//   non-indoor avatar gets a +3.0 lift. Returns the terrain-type byte (0 if the
//   mesh/sample resolve fails).
//
// `ch->mesh` is the resolved heightmap; the avatar world position is read from the
// avatar transform. The indoor-floor pick + SetPosition scene-graph leaf are
// modeled (we write the avatar transY/Z back as the engine writes +80 then calls
// SetPosition). Returns the terrain type byte.
// ===========================================================================
int QueryTileAhead(MotionCharacter* ch);

// ===========================================================================
// VIBE_Command_Dispatcher  (gilde.exe 0x40a4d4) — the FULL walk-on-path executor
// monolith ("ch_WalkOnPath"). One per-tick step of a character walking a tile
// waypoint list. Phases (translated 1:1 from the Hex-Rays reference):
//   1. morph cooldown release (ch+340 stamp vs frame end-tick + 200).
//   2. build phase (path flag ch+12 == 0): tear down any morph, alloc/zero the
//      waypoint buffer, snap the start target, clear the morph-init gate; or unlink
//      if the character is no longer in its owning universe.
//   3. morph-init phase (ch+260 == 0): attach the "bewegung/gehen" /
//      "karren_ziehen" anim, seed the first segment + anim active flags.
//   4. abort (ch+400): teardown + unlink.
//   5. per-tick advance: QueryTileAhead (height-snap + terrain), footstep, speed
//      ramp, the rotation interpolation toward the segment heading (with the
//      "missed threshold" overshoot check), waypoint advance (with the gap-skip
//      scan), and the anim playback-speed update from the tile ahead.
// Render/anim/sound leaves route through MotionHooks; all numeric paths are 1:1.
// ===========================================================================
void WalkOnPathStep(MotionCharacter* ch);

} // namespace guild::sim
