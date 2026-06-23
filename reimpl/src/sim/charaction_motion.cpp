// charaction_motion — full 1:1 translation of the walk-on-path executor monolith
// and its motion primitives. The Hex-Rays pseudocode of VIBE_Command_Dispatcher
// (0x40a4d4) is the reference of record; render/anim/sound leaves are routed
// through MotionHooks. All float constants are decoded byte-for-byte (see header).
#include "sim/charaction_motion.h"

#include "render/heightmap.h"

#include <cmath>
#include <cstring>

namespace guild::sim {

// --- recovered constants (decoded byte-for-byte; see header notes) ----------
const float  kRampStep       = 0.009999999776482582f; // flt_610938
const double kIndoorDurScale = 3.0;                    // dbl_61093C
const double kMissedThresh   = 1.2;                    // dbl_610944
const double kTurnHi         = 0.04;                   // dbl_61094C
const double kTurnLo         = -0.04;                  // dbl_610954
const double kTurnPerTick    = 0.9090909090909092;     // dbl_61095C
const float  kCartTurn       = 0.4000000059604645f;    // flt_610964
const double kRunThresh      = 2.094395102393333;      // dbl_61096C  (2*pi/3)
const double kBucketHalfPi   = 1.570796326795;         // dbl_610974  (pi/2)
const double kBucketPi3      = 1.047197551196667;      // dbl_61097C  (pi/3)
const double kBucketPi6      = 0.5235987755983333;     // dbl_610984  (pi/6)
const double kFac200         = 2.0;                    // dbl_61098C
const double kFac014         = 0.14;                   // dbl_610994
const double kFac020         = 0.2;                    // dbl_61099C
const double kFac032         = 0.32;                   // dbl_6109A4
const double kTwoPi          = 6.28318530718;          // dbl_6109AC
const float  kSpeedMulFlat   = 2.200000047683716f;     // flt_6109B4
const float  kSpeedMulStairs = 2.5f;                   // flt_6109B8
const float  kSpeedMulCartFl = 1.7000000476837158f;    // flt_6109BC
const float  kSpeedMulCartSt = 1.899999976158142f;     // flt_6109C0
const float  kIndoorOne      = 1.0f;                   // dword_62D07C
const double kStairStepGate  = -5.0;                   // dbl_6106CC
const double kIndoorLift      = 3.0;                   // dbl_6106D4
const float  kHeightLerp     = 0.25f;                  // flt_6106DC
const double kDotFloor       = -1.0;                   // dbl_6286F8
const double kHalfPiRot      = 1.570796326795;         // dbl_628700  (pi/2)

// --- throttle globals (g_tickStart/g_tickEnd live in charaction_walk.cpp) ----
int g_motionFootstepEnabled = 0;   // dword_62D074
int g_motionFootstepCount   = 0;   // dword_62D078
int g_motionActiveUniverse  = 0;   // dword_62D080
u8  g_motionSceneChanged    = 0;   // byte_62D011

// ---------------------------------------------------------------------------
// Hook table (inert default: attach yields a fresh anim so paths can be walked).
// ---------------------------------------------------------------------------
namespace {
MotionAnim* InertAttach(MotionCharacter* ch, const char*, int) {
    static MotionAnim s_anim;   // single inert anim shared by the default hook
    (void)ch;
    s_anim = MotionAnim{};
    return &s_anim;
}
void InertDetach(MotionCharacter*)             {}
void InertDraw(MotionCharacter*)               {}
void InertFreeWp(MotionCharacter* ch)          { ch->waypoints = nullptr; }
int  InertFootstep(MotionCharacter*, int, int) { return 0; }
const MotionHooks kInert = { &InertAttach, &InertDetach, &InertDraw,
                             &InertFreeWp, &InertFootstep };
const MotionHooks* g_hooks = &kInert;
} // namespace

void SetMotionHooks(const MotionHooks* hooks) { g_hooks = hooks ? hooks : &kInert; }
const MotionHooks& GetMotionHooks() { return *g_hooks; }

// ===========================================================================
// VIBE_Object_SetWorldTranslationXYZ  (0x5af5cc).
//   v5 = {x, yaw, z}; SetWorldTranslation(obj, v5).
// SetWorldTranslation (0x5af50c) writes obj+132 = x, +136 = yaw, +140 = z and
// rebuilds the euler matrix (a render leaf). We map +136 (heading) -> transYaw
// and +132/+140 -> the object's X/Z. The scene-graph dirty walk + matrix rebuild
// are render leaves; the observable field writes are reproduced.
// ===========================================================================
void ObjectSetWorldTranslationXYZ(MotionAvatar* obj, float x, float yaw, float z) {
    if (!obj) return;
    obj->transX   = x;     // +132
    obj->transYaw = yaw;   // +136 (heading)
    obj->transZ   = z;     // +140
}

// ===========================================================================
// VIBE_Math_AngleToTargetSigned  (0x5b6d1c) — geometric core.
//   facing = normalize(forwardProbe - pivot)  (Y zeroed)
//   toTarget = normalize(target - pivot)       (Y zeroed)
//   dot = facing . toTarget
//   c = dot >= -1 ? (dot < 1 ? dot : 1) : -1      (the dbl_6286F8 / 1.0 clamp)
//   ang = acos(c)
//   // sign: rotate facing by +90deg in XZ (sin/cos of pi/2 => (fz,-fx)); if that
//   //       dotted with toTarget > 0, flip the sign bit of `ang`.
// The engine's v14/v15/v16 is `facing` (forward - pivot) with v15 (Y) zeroed;
// v23/v24/v25 is `toTarget` (target - pivot) with v24 (Y) zeroed.
// ===========================================================================
float AngleToTargetSigned(const float facing[3], const float pivot[3],
                          const float target[3]) {
    // v14 = facing - pivot? In the engine `facing` is already (probe - pivot).
    // Our caller passes `facing` as the avatar forward vector and `pivot` as the
    // avatar position; we form toTarget = target - pivot here, matching the engine
    // (v23 = *a2 - v17 where v17 is the transformed pivot).
    float f[3]  = { facing[0], 0.0f, facing[2] };          // v15 (Y) := 0
    float t[3]  = { target[0] - pivot[0], 0.0f, target[2] - pivot[2] }; // v24 := 0

    // VIBE_Math_VectorNormalize(&v23); VIBE_Math_VectorNormalize(&v14);
    auto norm = [](float v[3]) {
        float len = (float)std::sqrt((double)v[0] * v[0] + (double)v[1] * v[1] +
                                     (double)v[2] * v[2]);
        if ((( *reinterpret_cast<u32*>(&len)) & 0x7FFFFFFFu) != 0) {
            float inv = 1.0f / len;
            v[0] *= inv; v[1] *= inv; v[2] *= inv;
        } else {
            v[0] = v[1] = v[2] = 0.0f;
        }
    };
    norm(t);   // VectorNormalize(&v23)
    norm(f);   // VectorNormalize(&v14)

    // v6 = dot(facing, toTarget) = v23*v14 + v24*v15 + v25*v16
    float dot = t[0] * f[0] + t[1] * f[1] + t[2] * f[2];

    // c clamp: if ( v6 < -1.0 || (int)v6 < 1.0f-bits ) { c = v6>=-1 ? v6 : -1 } else c = 1
    float c;
    if ((double)dot < kDotFloor || *reinterpret_cast<i32*>(&dot) < 0x3F800000) {
        c = ((double)dot >= kDotFloor) ? dot : -1.0f;
    } else {
        c = 1.0f;
    }
    float ang = (float)std::acos((double)c);   // AcosGuarded(c)

    // sign: rotate facing by pi/2 in XZ. sin(pi/2)=1, cos(pi/2)=0:
    //   v20 = cos*f.x + f.z*sin = f.z
    //   v22 = f.z*cos + sin*(-f.x) = -f.x
    double s = std::sin(kHalfPiRot);
    double co = std::cos(kHalfPiRot);
    float rotX = (float)(co * f[0] + f[2] * s);   // v20
    float rotZ = (float)(f[2] * co + s * -f[0]);  // v22
    // v21 = v15 (== 0); cross-test: v20*v23 + v21*v24 + v22*v25 > 0 -> flip sign.
    if (rotX * t[0] + 0.0f * t[1] + rotZ * t[2] > 0.0f) {
        u32 bits = *reinterpret_cast<u32*>(&ang);
        bits ^= 0x80000000u;                     // HIBYTE(v12) ^= 0x80
        ang = *reinterpret_cast<float*>(&bits);
    }
    return ang;
}

// ===========================================================================
// VIBE_Character_StepMotionQueue  (0x4041e8).
//   v3 = avatar (node+20); handle = avatar+112.
//   if ( !handle && !node+12 )                     // no motion + no path
//     handle = AttachMotion(avatar, motionId); return node+13>>24;
//   if ( handle && (handle+109 & 0x20) )           // reached
//     { prune; handle = 0; return -1; }
//   if ( handle && node+400 )                      // abort
//     { prune; handle = 0; return -1; }
//   if ( handle ) return node+13>>24;              // still playing
//   return -1;
// ===========================================================================
int StepMotionQueue(MotionQueueNode* node) {
    MotionAnim* handle = *node->motionHandle;     // v5/v3+112
    if (!handle && !node->pathFlag) {
        // First poll, no motion + no path: attach a fresh motion anim.
        *node->motionHandle = node->attachMotion ? node->attachMotion(node) : nullptr;
        return node->packedResult;                // *(node+13) >> 24
    }
    if (handle && (handle->flags109 & 0x20) != 0) {           // reached
        if (node->pruneAttachment && (node->avatarFlag140 & 2) == 0)
            node->pruneAttachment(node);
        *node->motionHandle = 0;
        return -1;
    }
    if (handle && node->abort) {                              // aborted
        if (node->pruneAttachment && (node->avatarFlag140 & 2) == 0)
            node->pruneAttachment(node);
        *node->motionHandle = 0;
        return -1;
    }
    if (*node->motionHandle)
        return node->packedResult;                // *(node+13) >> 24
    return -1;
}

// ===========================================================================
// VIBE_Character_QueryTileAhead  (0x4066bc).
//   mesh = ResolveMesh(avatar);  obj = avatar+52 (transform);
//   world = { obj[19], obj[20], obj[21] };   // avatar position
//   if ( !WorldToTileWithHeight(mesh, world, &tile, &h) ) return 0;
//   idx = mesh[8]*tileY + tileX;             // mesh[8] == grid size
//   type = mesh[9][24*idx];                  // entries type byte
//   if ( (!type || type==13) && !universe[+172] ) return type;
//   floor = universe[+172];
//   if ( floor ) { PickTileAtPoint(floor, obj+76, 1, &tileY, &floorH);
//                  if ( !type || type==13 || floorH - h >= -5.0 ) h = floorH; }
//   if ( !IndexFromPointer(universe) ) h += 3.0;        // outdoor lift
//   obj[80] = (h - obj[80]) * 0.25 + obj[80];           // height lerp (+80 == Y)
//   SetPosition(obj, obj+76);
//   return type;
// ===========================================================================
int QueryTileAhead(MotionCharacter* ch) {
    if (!ch->mesh || !ch->avatar) return 0;       // ResolveMesh null -> 0
    MotionAvatar* av = ch->avatar;
    const render::Heightmap* hm = ch->mesh;

    // v12 = { obj[19], obj[20], obj[21] } -> the object world POSITION.
    float world[3] = { av->posX, av->posY, av->posZ };

    int tileX = 0, tileY = 0;
    float h = 0.0f;
    if (!render::WorldToTileWithHeight(hm, world, &tileX, &tileY, &h))
        return 0;

    int idx  = hm->size * tileY + tileX;          // mesh[8]*v14 + v16[0]
    int type = (int)(i8)hm->entries[24 * idx];    // *(char*)(mesh[9] + 24*idx)

    int floor = ch->universeIndoorFloor;          // universe[+172]
    if ((type == 0 || type == 13) && floor == 0)
        return (i8)hm->entries[24 * idx];

    if (floor) {
        // VIBE_Floor_PickTileAtPoint(floor, obj+76, 1, &tileY, &floorH): pick the
        // indoor floor tile at the avatar's world point. Render-coupled; modeled
        // as "floor height == current sampled height" unless the host overrides.
        float floorH = h;     // PickTileAtPoint result (modeled)
        if (type == 0 || type == 13 || (double)(floorH - h) >= kStairStepGate)
            h = floorH;
    }

    // if ( !IndexFromPointer(universe) ) h += 3.0;  (outdoor -> lift)
    // IndexFromPointer returns the universe slot index; 0 means the primary/no
    // indoor universe -> apply the outdoor lift.
    if (ch->universeIndoorFloor == 0)
        h = (float)((double)h + kIndoorLift);

    // *(obj+80) = (h - obj[80]) * 0.25 + obj[80]  -> the world-Y height lerp.
    av->posY = (float)((double)(h - av->posY) * kHeightLerp + av->posY);
    // SetPosition(obj, obj+76): writes obj[19..21] from the +76 vector (posX/Y/Z).
    return type;
}

// ===========================================================================
// The rotation interpolation block (shared by the two arms of the monolith). It
// advances `cur` (the object heading, v54/v68 [34]) toward `target = cur + delta`
// where `delta` is the freshly recomputed signed angle to the segment target
// (+264). Returns the new heading; *done set when the turn completed (+264 -> 0).
//
// Faithful to the monolith's branch (the "<0" arm subtracts the step; the ">0"
// arm adds it; the aligned arm snaps and clears +264):
//   if ( delta > 0.04 || delta < -0.04 ) {
//     step = (tickEnd-tickStart) * 0.9090909; if (fast) step *= 0.4;
//     if ( |delta| <= pi/3 (kRunThresh) ... ) bucket; else step *= 0.32 (run cap);
//     cur -/+= s; cur = fmod(cur,2pi); if (cur<0) cur += 2pi;
//     if ( cur over/under target ) cur = target;       // (arm-specific compare)
//   } else { cur = target; +264 = 0; }
// The bucketing: |delta|<=pi/2 ? (|delta|<=pi/3 ? (|delta|<=pi/6 ? *0.04 :
//   *0.04*2.0) : *0.14) : *0.2.  NB the monolith's gate is
//   `fabs(delta) <= 2*pi/3 (kRunThresh) || mounted` for the bucket path, else *0.32.
// ===========================================================================
namespace {
// add==true: the ">0" additive arm (advance heading by +s; overshoot when cur>target).
// add==false: the "<0" subtractive arm (advance heading by -s; overshoot when cur<target).
double RotateStep(double cur, double delta, int elapsed, bool fast, bool mounted,
                  bool add, bool* done) {
    *done = false;
    double target = cur + delta;
    if (delta > kTurnHi || delta < kTurnLo) {
        double step = (double)elapsed * kTurnPerTick;     // v105/v106
        if (fast)
            step *= kCartTurn;                            // flt_610964
        double s;
        // gate: fabs(delta) <= 2*pi/3 (kRunThresh) || mounted -> bucket; else run cap.
        if (std::fabs(delta) <= kRunThresh || mounted) {
            double mag = std::fabs(delta);                // v87/v89
            if (mag <= kBucketHalfPi) {
                if (mag <= kBucketPi3) {
                    if (mag <= kBucketPi6)
                        s = step * kTurnHi;               // * 0.04
                    else
                        s = step * kTurnHi * kFac200;     // * 0.04 * 2.0
                } else {
                    s = step * kFac014;                   // * 0.14
                }
            } else {
                s = step * kFac020;                       // * 0.2
            }
        } else {
            s = step * kFac032;                           // * 0.32 (run cap)
        }
        double nv = add ? (cur + s) : (cur - s);
        nv = std::fmod(nv, kTwoPi);                       // VIBE_Math_Fmod
        if (nv < 0.0)
            nv += kTwoPi;
        if (add) {
            // ">0" arm: v109 = cur+s; snap when nv > target.
            if (nv > target) { nv = target; }
        } else {
            // "<0" arm: v108 = cur-s; snap when nv < target.
            if (nv < target) { nv = target; }
        }
        return nv;
    }
    // aligned: snap, finish.
    *done = true;
    return target;
}
// The avatar forward vector (XZ) for the angle recompute: the engine derives it
// from the bone chain; the planar facing of heading `yaw` is (sin yaw, 0, cos yaw).
void AvatarForward(const MotionAvatar* av, float out[3]) {
    out[0] = (float)std::sin((double)av->transYaw);
    out[1] = 0.0f;
    out[2] = (float)std::cos((double)av->transYaw);
}

// LABEL_111 + LABEL_116: the per-segment rotation interpolation toward the
// current target heading, the "missed threshold" overshoot check (aligned arm),
// and the anim playback-speed update from the tile ahead. Shared by the in-segment
// arm and the `goto LABEL_111` after a fresh waypoint is loaded.
void RunRotationAndSpeed(MotionCharacter* ch, int tileAhead) {
    MotionAvatar* av = ch->avatar;

    if (ch->turnAngle < 0.0f) {
        // "<0" arm: recompute the signed angle, subtract the step.
        float facing[3]; AvatarForward(av, facing);
        float pivot[3]  = { av->posX, av->posY, av->posZ };
        float target[3] = { ch->targetX, ch->targetY, ch->targetZ };
        float delta = AngleToTargetSigned(facing, pivot, target);
        ch->turnAngle = delta;
        bool done = false;
        bool fast = (av->moveFlags & 8) != 0;
        bool mounted = (av->flag1 & 8) != 0;
        int elapsed = g_tickEnd - g_tickStart;
        double nv = RotateStep(av->transYaw, delta, elapsed, fast, mounted,
                               /*add=*/false, &done);
        if (done) ch->turnAngle = 0.0f;
        ObjectSetWorldTranslationXYZ(av, av->transX, (float)nv, av->transZ);
    } else if (ch->turnAngle > 0.0f) {
        // ">0" arm: recompute, add the step.
        float facing[3]; AvatarForward(av, facing);
        float pivot[3]  = { av->posX, av->posY, av->posZ };
        float target[3] = { ch->targetX, ch->targetY, ch->targetZ };
        float delta = AngleToTargetSigned(facing, pivot, target);
        ch->turnAngle = delta;
        bool done = false;
        bool fast = (av->moveFlags & 8) != 0;
        bool mounted = (av->flag1 & 8) != 0;
        int elapsed = g_tickEnd - g_tickStart;
        double nv = RotateStep(av->transYaw, delta, elapsed, fast, mounted,
                               /*add=*/true, &done);
        if (done) ch->turnAngle = 0.0f;
        ObjectSetWorldTranslationXYZ(av, av->transX, (float)nv, av->transZ);
    } else {
        // turnAngle == 0 (aligned): the "missed threshold" overshoot check.
        // dist(avatarPos, segment target) > segLength * 1.2 -> missed.
        float dx = av->posX - ch->targetX;
        float dy = av->posY - ch->targetY;
        float dz = av->posZ - ch->targetZ;
        double dist = std::sqrt((double)dx * dx + (double)dy * dy +
                                (double)dz * dz);
        if (dist > (double)ch->segLength * kMissedThresh) {
            // final segment: ApplyVisibilityState(avatar, 0) (render leaf).
            if (av->anim)
                av->anim->flags110 |= 8u;   // bit3 stop ("missed threshold")
            // engine logs "ch_WalkOnPath(): Have missed my treshhold..."
        }
    }

    // LABEL_116: anim playback-speed update from the tile ahead.
    if (tileAhead && tileAhead != 13 && av->anim) {
        float tileFactor = 0.69999999f;       // v104 default 0.7
        if (av->universeId == -1 && av->object3dIndoorFloor) {
            // indoor floor: cart -> 1.0, walk -> 1.0 + 0.2 == 1.2
            tileFactor = av->cart ? kIndoorOne
                                  : (float)((double)kIndoorOne + kFac020);
        }
        float mul;
        if ((av->flag1 & 8) != 0) {           // mounted/cart
            mul = (tileAhead == 11 || tileAhead == 6) ? kSpeedMulCartSt
                                                      : kSpeedMulCartFl;
        } else {
            mul = (tileAhead == 11 || tileAhead == 6) ? kSpeedMulStairs
                                                      : kSpeedMulFlat;
        }
        av->anim->speed = av->baseSpeed * mul * tileFactor * av->ramp;
    }
}
} // namespace

// ===========================================================================
// VIBE_Command_Dispatcher  (0x40a4d4) — the full walk-on-path executor.
// ===========================================================================
void WalkOnPathStep(MotionCharacter* ch) {
    MotionAvatar* av = ch->avatar;
    if (!av) return;

    // v100 = ResolveMesh(avatar); v110 = avatar.
    const render::Heightmap* mesh = ch->mesh;   // ResolveMesh(avatar)

    // --- phase 1: morph cooldown release (ch+340) -----------------------------
    int morphStamp = ch->morphStamp;            // *(a1+340)
    if (morphStamp != -1) {
        if (morphStamp + 200 > g_tickEnd)       // not yet elapsed -> wait
            return;
        if (av->anim)                           // v5 = avatar[28]; if (v5) v5[96]=1.0
            av->anim->speed = 1.0f;             // *(v5+96) = 1065353216 (1.0f)
        ch->morphStamp = -1;
    }

    // --- phase 2: build phase (path flag ch+12 == 0) --------------------------
    int pathFlag = ch->activePath;              // v6 = *(a1+12)
    if (!pathFlag) {
        // Tear down any active morph slot (avatar[31]) — release morph mesh.
        if (av->morphSlot) {
            GetMotionHooks().detachMorphAni(ch); // morph_%i prune + ReleaseMeshData
            // *(v+124) = 0 (secondary anim handle) — modeled by detach.
        }
        GetMotionHooks().drawSubMeshes(ch);     // DrawSubMeshes(avatar[13])
        av->morphBlend = 0.0f;                  // v110[32] = 0
        av->morphSlot  = 0;                     // v11[31] = 0
        av->anim       = nullptr;               // v11[28] = 0
        av->ramp       = 1.0f;                  // v11[105] = 1.0
        ch->flagsA &= (u8)~8u;                  // avatar+140 &= ~8 (dirty-mesh)
        ch->waypoints = nullptr;                // *(a1+244) = 0

        // The owning-universe check: IndexFromPointer(avatar parent) == ch+56.
        // In the engine: if (IndexFromPointer(v110[34]) == *(a1+56)) build, else unlink.
        // We model "still in owning universe" as universe present + start mesh match.
        int startMeshHandle = ch->startMesh;    // *(a1+56)
        int parentSlot = ch->universe ? 0 : -1; // IndexFromPointer(avatar[34]) (modeled)
        if (parentSlot == startMeshHandle || (startMeshHandle == 0 && ch->universe)) {
            // Alloc + zero the waypoint buffer (ch_t:waypoints, 0x200 bytes).
            std::memset(ch->waypointBuf, 0, sizeof(ch->waypointBuf));
            ch->waypoints   = ch->waypointBuf;  // *(a1+244)
            ch->waypointCap = 256;              // *(a1+240)
            ch->waypointIdx = 0;                // *(a1+248)
            ch->sentinel    = -1;               // *(a1+252)
            // Snap the start target from the first waypoint (col,row) then from
            // the (startTileX, startTileY) seed (the engine calls TileToWorld twice;
            // the second overwrites with the explicit start tile + height byte).
            float w[3] = {0, 0, 0};
            if (mesh) {
                render::TileToWorld(mesh, ch->waypoints[0], ch->waypoints[1], w);
                render::TileToWorld(mesh, ch->startTileX, ch->startTileY, w);
            }
            ch->targetX = w[0];                 // *(a1+292)
            ch->targetY = w[1];                 // *(a1+296)
            ch->targetZ = w[2];                 // *(a1+300)
            ch->morphInit = 0;                  // *(a1+260) = 0
        } else {
            GetMotionHooks().freeWaypoints(ch);
            ch->activePath = 0;
            // VIBE_ActionQueue_UnlinkEntry(a1): the host frees the action node.
        }
        return;
    }

    // --- phase 3: morph-init (ch+260 == 0 and anim ready/absent) --------------
    if (!ch->morphInit &&
        ((av->anim && (av->anim->flags109 & 0x20) != 0) || !av->anim)) {
        av->morphBlend = 0.0f;                  // v110[32] = 0
        if (av->morphSlot) {
            GetMotionHooks().detachMorphAni(ch);
        }
        GetMotionHooks().drawSubMeshes(ch);     // DrawSubMeshes + (cond) TouchMeshFrames
        const char* animName = av->cart ? "bewegung/karren_ziehen" : "bewegung/gehen";
        MotionAnim* a = GetMotionHooks().attachAni(ch, animName, 1);
        av->anim = a;
        if (!a) {
            // attach failed -> set dirty-mesh, free waypoints, unlink.
            if (g_motionSceneChanged)
                ch->flagsA |= 8u;
            GetMotionHooks().freeWaypoints(ch);
            ch->activePath = 0;
            return;                             // (engine: goto LABEL_31 -> unlink)
        }
        a->flags109 &= (u8)~0x10u;              // *(anim+109) &= ~0x10
        a->flags109 = (u8)((a->flags109 & 0x3F) | 0x40); // &= 0x3F then |= 0x40
        a->targetX = ch->targetX;               // anim+76 = a1+292
        a->targetY = ch->targetY;               // anim+80 = a1+296
        a->targetZ = ch->targetZ;               // anim+84 = a1+300
        // anim+92: 40.0 (mounted) else 20.0  (1109393408 / 1101004800)
        a->duration = ((av->flag1 & 8) != 0) ? 40.0f : 20.0f;
        a->flags110 |= 2u;                      // active
        a->flags110 &= (u8)~8u;                 // clear stop
        a->flags110 |= 4u;                      // enable
        a->flags109 &= (u8)~0x20u;              // clear reached
        ch->morphInit = 1;                      // *(a1+260) = 1
    }

    // --- phase 4: abort (ch+400) ----------------------------------------------
    if (ch->abort) {
        // "%s_%s" anim name build + ConvertBackslashToSlash + prune (render leaf).
        if (av->anim) {
            // VIBE_Anim_PruneExpiredAttachments (render leaf): no observable state
            // here beyond clearing the handle below.
        }
        av->anim = nullptr;                     // v110[28] = 0
        QueryTileAhead(ch);                     // height-snap one last time
        if (g_motionSceneChanged)
            ch->flagsA |= 8u;
        GetMotionHooks().freeWaypoints(ch);
        ch->activePath = 0;
        return;                                 // unlink
    }

    // morph not yet initialised + no anim -> teardown + unlink.
    if (!ch->morphInit && !av->anim) {
        if (g_motionSceneChanged)
            ch->flagsA |= 8u;
        GetMotionHooks().freeWaypoints(ch);
        ch->activePath = 0;
        return;                                 // LABEL_31 -> unlink
    }
    // morph not initialised but anim present -> wait a tick.
    if (!ch->morphInit && av->anim)
        return;

    // --- per-tick advance ------------------------------------------------------
    int tileAhead = QueryTileAhead(ch);         // height-snap + terrain type

    // Footstep: only while audible (in the active universe), throttled to 3/frame.
    if (av->anim && g_motionFootstepEnabled) {
        if (av->universeId == g_motionActiveUniverse) {        // in active universe
            // type the footstep on a cell transition into a hard surface type.
            int cell = tileAhead;
            if ((u8)ch->lastFootstep != (u8)cell &&
                (cell == 4 || cell == 12 || cell == 18 || cell == 24)) {
                if ((av->flag1 & 1) == 0 || g_motionFootstepCount >= 3) {
                    ch->lastFootstep = (u8)cell;
                } else {
                    int indoors = (av->object3dIndoorFloor != 0) ? 0 : 1;
                    if (GetMotionHooks().footstep(ch, tileAhead, indoors))
                        ++g_motionFootstepCount;
                    ch->lastFootstep = (u8)cell;
                }
            }
        }
    }

    // LABEL_77: speed ramp toward 1.0.
    if (av->ramp < 1.0f)
        av->ramp = av->ramp + kRampStep;        // v110[105] += flt_610938

    // The rotation / segment-advance gate: *(a1+248) != 0 means a segment is active.
    // When a segment is active and its anim isn't in the "stop" state, run the
    // rotation interpolation + speed update (LABEL_111/LABEL_116) and return.
    if (ch->waypointIdx != 0) {
        MotionAnim* a = av->anim;
        if (!a || (a->flags110 & 8) == 0) {
            RunRotationAndSpeed(ch, tileAhead);
            return;
        }
    }

    // --- waypoint advance ------------------------------------------------------
    // Gap-skip scan: if the next slot is empty (0,0) and we have room, scan up to
    // 32 entries ahead for a non-empty slot; if found, snap +248 back one before it
    // and re-flag the anim; else advance normally.
    int next = ch->waypointIdx + 1;
    {
        // NB: the bound (next < waypointCap) is tested BEFORE dereferencing w[],
        // so we never read past the 512-byte waypoint buffer when waypointIdx is at
        // the last slot (next == cap). On valid in-range input every term is still
        // evaluated, so this is behaviour-identical to the original's gap-skip gate.
        u8* w = ch->waypoints + 2 * next;
        if (ch->waypoints && next < ch->waypointCap && w[0] == 0 && w[1] == 0) {
            int j = ch->waypointIdx + 1;
            int cap = (ch->waypointIdx + 33 <= 256) ? (ch->waypointIdx + 33) : 256;
            int found = next;
            for (; j < cap; ++j) {
                u8* s = ch->waypoints + 2 * j;
                if (s[0] || s[1]) {
                    found = j;
                    ch->waypointIdx = j - 1;
                    break;
                }
            }
            if (j != found && j >= cap) {
                // scanned the whole window with no hit -> stall the anim a tick.
                if (av->anim) {
                    av->anim->flags110 &= (u8)~2u;   // clear active
                    av->anim->flags110 |= 8u;        // set stop
                    av->anim->flags109 |= 0x20u;     // set reached
                }
                return;
            }
        }
    }

    int idx = ch->waypointIdx + 1;
    ch->waypointIdx = idx;                        // *(a1+248) = v48
    if (idx < ch->waypointCap) {
        // Sample the next waypoint world position (height snap).
        int tileX = ch->waypoints[2 * idx];
        int tileY = ch->waypoints[2 * idx + 1];
        float w[3] = {0, 0, 0};
        if (mesh)
            render::TileToWorld(mesh, tileX, tileY, w);
        ch->targetX = w[0];
        ch->targetY = w[1];
        ch->targetZ = w[2];

        // Recompute the signed turn angle (+264) and segment length (+268).
        {
            float facing[3]; AvatarForward(av, facing);
            float pivot[3]  = { av->posX, av->posY, av->posZ };
            float target[3] = { ch->targetX, ch->targetY, ch->targetZ };
            ch->turnAngle = AngleToTargetSigned(facing, pivot, target);
        }
        float sdx = av->posX - ch->targetX;       // (avatar+52)+76 - a1+292
        float sdy = av->posY - ch->targetY;       // +80 - a1+296
        float sdz = av->posZ - ch->targetZ;       // +84 - a1+300
        ch->segLength = (float)std::sqrt((double)sdx * sdx + (double)sdy * sdy +
                                         (double)sdz * sdz);

        if (av->anim) {
            // v103 segment duration: normal 20/40 (mounted); final 8/24 (mounted).
            float v103;
            if (ch->waypointCap - 1 > ch->waypointIdx || ch->waypointCap <= 1)
                v103 = ((av->flag1 & 8) != 0) ? 40.0f : 20.0f;
            else
                v103 = ((av->flag1 & 8) != 0) ? 24.0f : 8.0f;
            if (ch->universeIndoorFloor)              // indoor -> * 3.0
                v103 = (float)((double)v103 * kIndoorDurScale);
            av->anim->targetX  = ch->targetX;
            av->anim->targetY  = ch->targetY;
            av->anim->targetZ  = ch->targetZ;
            av->anim->duration = v103;
            av->anim->flags110 |= 2u;                 // active
            av->anim->flags110 &= (u8)~8u;            // clear stop
            av->anim->flags109 &= (u8)~0x20u;         // clear reached
        }
        // goto LABEL_111: the engine immediately runs the rotation + speed block
        // with the freshly loaded segment (it does NOT re-run the whole function).
        RunRotationAndSpeed(ch, tileAhead);
        return;
    }

    // Path exhausted: detach the morph anim, free waypoints, unlink.
    GetMotionHooks().detachMorphAni(ch);          // DetachMorphAni(avatar)
    if (g_motionSceneChanged)
        ch->flagsA |= 8u;
    GetMotionHooks().freeWaypoints(ch);
    ch->activePath = 0;
}

} // namespace guild::sim
