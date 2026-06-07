#pragma once
// gilde.exe — Combat COMMAND-ISSUING front-ends: the Build*Packet family that the
// player's combat UI / order glue calls to emit a unit's next order onto the
// lockstep command channel (namespace guild::sim). MODULE: command (the original
// symbols are VIBE_Command_Build*Packet @0x488a4c–0x488fe7) but they live in the
// combat order path, so they sit alongside the other combat sim translations.
//
// SCOPE. Four front-ends + their shared packet sink:
//   * VIBE_Command_BuildAttackPacket  @0x488a4c — issue an ATTACK order against a
//     target unit. Looks up the attacker's weapon object-def, validates the
//     target's mesh/active-target, world->tile-projects the target, then assembles
//     an opcode-2 order payload whose exact field set depends on the weapon class
//     (340/342/.. melee-ish vs 350/352 vs 372 vs 374 ranged variants) and a
//     "secondary" gate byte (person+452 in {1,2,3}). Several variants early-out
//     with a HUD banner when the existing ranged target has no shots left.
//   * VIBE_Command_BuildMoveToPacket  @0x488c8c — issue a MOVE order (payload
//     kind 3). Transforms a bone-chain point to world space, world->tile-projects
//     it, and emits the destination tile.
//   * VIBE_Command_BuildTilePacket    @0x488edc — issue a TILE order (payload
//     kind 7). Projects the unit's own mesh origin to a tile, then picks the
//     SAFEST reachable tile within range 8 (threat-field search) as the goal.
//   * VIBE_Command_BuildSimplePacket  @0x488fa4 — issue a bare order (payload
//     kind 8) carrying only the unit id.
//   * VIBE_Command_RequestBuildOp80   @0x495874 — the shared sink: wrap a 44-byte
//     order-staging block in an opcode-0x50 (80) command packet, stamp the
//     local-battle cut-target at +0x40, and enqueue it. (Translated faithfully —
//     it is self-contained apart from the CommandQueue it enqueues into.)
//
// CROSS-CLUSTER LEAVES (heightmap / transform / gameobject-iter / slot-alloc) are
// routed through CombatOrderContext, mirroring the established OrderWorldContext /
// ThreatField pattern: the RULE (which fields land on the wire, the validation
// gates, the early-outs) is translated 1:1; the scene queries are callbacks.
//
// Determinism: the assembled payload bytes are the wire format; golden-vector
// tested against the recovered field offsets + ComputePacketSize(0x50)==68.
#include "guild/common/types.h"
#include "sim/command.h"

#include <functional>

namespace guild::sim {

// ---------------------------------------------------------------------------
// The 44-byte order-staging block.
// ---------------------------------------------------------------------------
//
// In the originals this is the `_DWORD v15[11]` / `v6[16](0x2C)` stack temp that
// FindOrAllocSlot's 44-byte slot is qmemcpy'd into, then selected dwords are
// overwritten before RequestBuildOp80 copies the whole 44 bytes to packet +0x14.
// The slot pre-clears (v9[28]=0, v9[12]=0 / v6[12]=0) zero specific bytes of the
// COPIED slot; since FindOrAllocSlot zero-inits a freshly allocated slot, a fresh
// staging block here is all-zero and those pre-clears are already satisfied. We
// expose it as a flat 44-byte buffer with the recovered field accessors.
struct OrderStage {
    u8 bytes[44] = {};   // dword indices 0..10

    void put32(u32 off, i32 v) {
        bytes[off]     = static_cast<u8>(v);
        bytes[off + 1] = static_cast<u8>(v >> 8);
        bytes[off + 2] = static_cast<u8>(v >> 16);
        bytes[off + 3] = static_cast<u8>(v >> 24);
    }
    i32 get32(u32 off) const {
        return static_cast<i32>(static_cast<u32>(bytes[off])
             | (static_cast<u32>(bytes[off + 1]) << 8)
             | (static_cast<u32>(bytes[off + 2]) << 16)
             | (static_cast<u32>(bytes[off + 3]) << 24));
    }
    // Field accessors (dword index -> byte offset). v15[1] is written via
    // LOBYTE(v15[1]) = kind in the originals, i.e. only byte +4 changes; we mirror
    // that (the slot copy supplies the other 3 bytes of dword 1).
    void set_targetId(i32 v) { put32(0,  v); }   // v15[0]  = *(target+4)
    void set_kind(u8 v)      { bytes[4] = v; }   // LOBYTE(v15[1]) = order kind
    void set_field4(i32 v)   { put32(16, v); }   // v15[4]
    void set_field5(i32 v)   { put32(20, v); }   // v15[5]  = tileX
    void set_field6(i32 v)   { put32(24, v); }   // v15[6]  = a5 (attack param)
    void set_secondary(u8 v) { bytes[28] = v; }  // LOBYTE(v15[7]) = secondary flag
};

// ---------------------------------------------------------------------------
// Cross-cluster scene queries (the heightmap/transform/gameobject leaves).
// ---------------------------------------------------------------------------
//
// Each callback corresponds to one VIBE_* leaf the front-ends call. The default-
// constructed context queries nothing (every lookup "fails"), so tests wire only
// the callbacks a given case exercises.
struct CombatOrderContext {
    // VIBE_Combat_FindObjectDef(attacker) @0x485a54 — the attacker's attached
    // weapon object-def record. Returns the weapon CLASS word (*ObjectDef, e.g.
    // 340/350/372/374), or a sentinel. `hasDef`=false models ObjectDef==null.
    // (-1 here means "no def"; the rule treats !ObjectDef the same as class 0.)
    std::function<bool(i32 attacker, i16& weaponClass, u8& weaponSubclass)> findObjectDef;

    // VIBE_Combat_FindActiveTarget(attacker) @0x485b1c — the attacker's current
    // live ranged target. Returns hasTarget + whether it still has shots left
    // (*(target+32) > 0, i.e. the `[8] <= 0` death/empty gate). When hasTarget &&
    // !hasShots, the variant cases early-out with a HUD banner.
    std::function<void(i32 attacker, bool& hasTarget, bool& hasShots)> findActiveTarget;

    // VIBE_Heightmap_WorldToTileWithHeight @0x5c6644 — project a world point to a
    // map tile. Returns success + the tile X (the originals keep tile X in v16 /
    // v9[0] and feed it onward; v9[1]/Z is used by Move). Writes tileX (and tileZ
    // for Move). Default: fails.
    std::function<bool(float worldX, float worldY, float worldZ,
                       i32& tileX, i32& tileZ)> worldToTile;

    // VIBE_Combat_FindSafestTileInRange(unit, tileX, 8, tileZ,&tileX,&tileZ)
    // @0x48b01c — threat-field search for the safest reachable tile near a goal.
    // Returns success and the chosen tile. Default: fails.
    std::function<bool(i32 unit, i32 inX, i32 range, i32 inZ,
                       i32& outX, i32& outZ)> findSafestTile;

    // VIBE_Transform_PointThroughBoneChain @0x5c8b38 — transform a bone-relative
    // point to world space (Move's destination). Writes worldX/Y/Z. Default:
    // identity (passes the input through).
    std::function<void(const float* point, float& wx, float& wy, float& wz)> transformPoint;

    // VIBE_Command_FindOrAllocSlot(handle, target) @0x485f7c — find/alloc the
    // 44-byte order slot for this (handle,target) pair. Returns whether a slot was
    // available (the originals early-out -1 when the 16-slot roster is full).
    // The freshly-allocated slot is zero, so the staging block starts zeroed; this
    // callback only reports availability. Default: a slot is always available.
    std::function<bool(i32 handle, i32 target)> findOrAllocSlot;
};

// The handle the front-ends thread to RequestBuildOp80 (the original passes
// *(handle+16) — the command queue / owner token — as Op80's a1, and *(handle+0)
// as the FindOrAllocSlot key). We bundle the two values the rule actually reads.
struct CombatOrderHandle {
    i32 slotKey   = 0;   // *handle  (FindOrAllocSlot's `a1`)
    i32 op80Owner = 0;   // *(handle+16)  (RequestBuildOp80's `a1`, lands at +0x10)
};

// gilde.exe 0x495874 — VIBE_Command_RequestBuildOp80  (eax=a1, edx=staging).
// Wrap the 44-byte staging block in an opcode-0x50 packet and enqueue it.
//   v3[0]=80; v4(a1)@+0x10; qmemcpy(v5@+0x14, staging, 44);
//   v6@+0x40 = dword_6315C0 ? *dword_6315C0 : -1;   // local-battle cut target
//   return EnqueuePacket(v3).
// `localBattleCutTarget` models *dword_6315C0 (the active local-battle target);
// pass -1 (the default) for "no local battle" (dword_6315C0 == 0).
i32 RequestBuildOp80(CommandQueue& q, i32 a1, const OrderStage& staging,
                     i32 localBattleCutTarget = -1);

// ---------------------------------------------------------------------------
// The four order front-ends.
// ---------------------------------------------------------------------------
//
// Each returns the ring slot index RequestBuildOp80/EnqueuePacket assigned, or -1
// for any of the original early-outs (null args, lookup failure, no slot, or the
// ranged-target "no shots" banner gate).

// gilde.exe 0x488a4c — VIBE_Command_BuildAttackPacket
//   (eax=handleKey, edx=target, ecx=a3, ebx=attacker, +stack a5).
// `target` (a2), `attacker` (a4 / v19) are the two person records; `a3` (v16, the
// initial tileX scratch) and `a5` (the attack parameter that lands in v15[6]) are
// passed through. `secondaryByteIn123` mirrors *(target+452) in {1,2,3} (sets
// the secondary flag). Returns the ring slot or -1.
i32 BuildAttackPacket(CommandQueue& q, const CombatOrderHandle& h,
                      i32 target, i32 attacker, i32 a5,
                      bool secondaryByteIn123,
                      const CombatOrderContext& ctx);

// gilde.exe 0x488c8c — VIBE_Command_BuildMoveToPacket (eax=handleKey, edx=target,
// ebx=bonePoint). Transforms `bonePoint` through the bone chain, world->tile-
// projects it, and emits a MOVE order (kind 3) carrying the destination tile.
// `bonePoint` is the 19-float bone-relative point the original passes (a3, a3+19).
i32 BuildMoveToPacket(CommandQueue& q, const CombatOrderHandle& h,
                      i32 target, const float* bonePoint,
                      const CombatOrderContext& ctx);

// gilde.exe 0x488edc — VIBE_Command_BuildTilePacket (eax=handleKey, edx=target).
// Projects the unit's own mesh origin (target->meshPtr+76) to a tile, finds the
// safest reachable tile within range 8, and emits a TILE order (kind 7).
// `unitWorld` is the unit's mesh-origin world XYZ (the originals read
// *(*(target+388)+52)+76). Returns the ring slot or -1.
i32 BuildTilePacket(CommandQueue& q, const CombatOrderHandle& h,
                    i32 target, const float* unitWorld,
                    const CombatOrderContext& ctx);

// gilde.exe 0x488fa4 — VIBE_Command_BuildSimplePacket (eax=handleKey, edx=target).
// Emits a bare order (kind 8) carrying only the target id. The original does NOT
// null-check the slot, so a full roster faults in the binary; we mirror the early
// -1 only when findOrAllocSlot reports no slot (the safe reconstruction).
i32 BuildSimplePacket(CommandQueue& q, const CombatOrderHandle& h,
                      i32 target, const CombatOrderContext& ctx);

// Order-packet "kind" byte (LOBYTE(stage[1])) recovered from the builders. This
// is the on-WIRE payload kind, distinct from the order-SLOT state enum OrderState
// in combat_types.h (which numbers move=1 etc.); the names are kept separate to
// avoid colliding with that enum in the shared guild::sim namespace.
enum OrderPacketKind : u8 {
    kOrderPacketAttack = 2,   // LOBYTE(v15[1]) for all attack variants
    kOrderPacketMove   = 3,   // BuildMoveToPacket
    kOrderPacketTile   = 7,   // BuildTilePacket
    kOrderPacketSimple = 8,   // BuildSimplePacket
};

} // namespace guild::sim
