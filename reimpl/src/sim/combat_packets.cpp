#include "sim/combat_packets.h"

#include <cstring>

namespace guild::sim {

// The originals form the id field as `*(record + 4)` — a 32-bit register/pointer
// add, which wraps in two's complement. Doing `id + 4` on a signed i32 in C++ is
// signed-overflow UB at the boundary (flagged by UBSAN). Compute the add in
// unsigned space so the result is the same well-defined wrap the binary produces.
static inline i32 Plus4(i32 id) {
    return static_cast<i32>(static_cast<u32>(id) + 4u);
}

// gilde.exe 0x495874 — VIBE_Command_RequestBuildOp80.
// Stack frame: v3[16] (header, opcode @+0), v4=a1 @+0x10, v5[44] @+0x14 (the
// staging copy), v6 @+0x40 (local-battle cut target). Then EnqueuePacket(v3),
// which recomputes len (ComputePacketSize(0x50)==68), and stamps cmdId/count.
i32 RequestBuildOp80(CommandQueue& q, i32 a1, const OrderStage& staging,
                     i32 localBattleCutTarget) {
    CommandPacket p{};
    p.opcode() = 80;                                // v3[0] = 80
    p.put32(0x10, static_cast<u32>(a1));            // v4 = a1
    std::memcpy(p.bytes + 0x14, staging.bytes, 44); // qmemcpy(v5, a2, 44)
    p.put32(0x40, static_cast<u32>(localBattleCutTarget)); // v6
    return q.EnqueuePacket(p);
}

// gilde.exe 0x488a4c — VIBE_Command_BuildAttackPacket.
// Faithful translation of the weapon-class switch + secondary gate. The slot copy
// (qmemcpy(v15, slot, 44)) starts the staging block from the zeroed allocated
// slot; the v9[28]/v9[12] pre-clears are therefore already satisfied.
i32 BuildAttackPacket(CommandQueue& q, const CombatOrderHandle& h,
                      i32 target, i32 attacker, i32 a5,
                      bool secondaryByteIn123,
                      const CombatOrderContext& ctx) {
    // if ( !a2 ) return -1;   (a2 == target)
    if (!target)
        return -1;

    // ObjectDef = VIBE_Combat_FindObjectDef(a2);
    i16 weaponClass = 0;
    u8  weaponSubclass = 0;
    bool hasDef = false;
    if (ctx.findObjectDef)
        hasDef = ctx.findObjectDef(target, weaponClass, weaponSubclass);
    (void)weaponSubclass;

    // if ( !a4 ) return -1;   (a4 == attacker)
    if (!attacker)
        return -1;

    // v8 = *(*(a4+388)+52); if ( !v8 ) return -1;  — the attacker's mesh origin
    // must exist. WorldToTileWithHeight(world, &v16, &v17): the projected tile X
    // lands in v16; failure returns -1.
    i32 tileX = 0, tileZ = 0;
    bool projected = false;
    if (ctx.worldToTile)
        projected = ctx.worldToTile(0.0f, 0.0f, 0.0f, tileX, tileZ);
    if (!projected)
        return -1;

    // v9 = FindOrAllocSlot(handle, a2); if ( !v9 ) return -1;
    bool haveSlot = ctx.findOrAllocSlot ? ctx.findOrAllocSlot(h.slotKey, target) : true;
    if (!haveSlot)
        return -1;

    // qmemcpy(v15, slot, 44): start from the zeroed slot.
    OrderStage v15{};

    // The weapon-class dispatch. The melee branch is taken when ObjectDef is null
    // OR the class is one of the melee-ish set OR class 0.
    const i16 c = weaponClass;
    const bool meleeBranch = !hasDef
        || c == 340 || c == 342 || c == 344 || c == 366 || c == 370 || c == 0;

    if (meleeBranch) {
        v15.set_targetId(Plus4(target));   // v15[0] = *(a2+4) -> here the id field
        v15.set_kind(kOrderPacketAttack);     // LOBYTE(v15[1]) = 2
        v15.set_field4(Plus4(attacker));   // v15[4] = *(v19+4)
    } else {
        switch (c) {
        case 350:
        case 352: {
            bool hasTarget = false, hasShots = true;
            if (ctx.findActiveTarget)
                ctx.findActiveTarget(target, hasTarget, hasShots);
            if (hasTarget && !hasShots) {
                // VIBE_Hud_SetStatusBannerText(dword_8C6F20); return -1;
                return -1;
            }
            v15.set_targetId(Plus4(target));
            v15.set_kind(kOrderPacketAttack);
            v15.set_field4(Plus4(attacker));
            v15.set_field5(tileX);          // v15[5] = v16
            v15.set_field6(a5);             // v15[6] = a5
            break;
        }
        case 372: {
            bool hasTarget = false, hasShots = true;
            if (ctx.findActiveTarget)
                ctx.findActiveTarget(target, hasTarget, hasShots);
            if (hasTarget && !hasShots) {
                // VIBE_Hud_SetStatusBannerText(dword_8C6F24); return -1;
                return -1;
            }
            v15.set_targetId(Plus4(target));
            v15.set_field4(0);              // v15[4] = 0
            v15.set_field5(tileX);          // v15[5] = v16
            v15.set_kind(kOrderPacketAttack);
            v15.set_field6(a5);             // v15[6] = a5
            break;
        }
        case 374: {
            bool hasTarget = false, hasShots = true;
            if (ctx.findActiveTarget)
                ctx.findActiveTarget(target, hasTarget, hasShots);
            if (hasTarget && !hasShots) {
                // VIBE_Hud_SetStatusBannerText(dword_8C6F28); return -1;
                return -1;
            }
            v15.set_targetId(Plus4(target));
            v15.set_kind(kOrderPacketAttack);
            v15.set_field5(tileX);          // v15[5] = v16
            v15.set_field4(0);              // v15[4] = 0
            v15.set_field6(a5);             // v15[6] = a5
            break;
        }
        default:
            // Class has a def but is none of the handled cases: the switch falls
            // through leaving v15 as the zeroed slot copy. Only the secondary gate
            // below applies (faithful to the original's no-default switch).
            break;
        }
    }

    // v10 = *(a2+452); if ( v10 == 1 || v10 == 2 || v10 == 3 ) LOBYTE(v15[7]) = 1;
    if (secondaryByteIn123)
        v15.set_secondary(1);

    // return VIBE_Command_RequestBuildOp80(*(v18+16), v15);
    return RequestBuildOp80(q, h.op80Owner, v15, -1);
}

// gilde.exe 0x488c8c — VIBE_Command_BuildMoveToPacket.
i32 BuildMoveToPacket(CommandQueue& q, const CombatOrderHandle& h,
                      i32 target, const float* bonePoint,
                      const CombatOrderContext& ctx) {
    // if ( !a3 ) return -1; if ( !a2 ) return -1; if ( !v11(handle) ) return -1;
    if (!bonePoint)
        return -1;
    if (!target)
        return -1;
    if (!h.slotKey && !h.op80Owner)
        return -1;

    // VIBE_Transform_PointThroughBoneChain(a3, a3+19, v8); -> world point v8.
    float wx = bonePoint[0], wy = bonePoint[1], wz = bonePoint[2];
    if (ctx.transformPoint)
        ctx.transformPoint(bonePoint, wx, wy, wz);

    // if ( !WorldToTileWithHeight(v4, v8, v9, &v10) ) return -1;  v9 = {tileX,tileZ}
    i32 tileX = 0, tileZ = 0;
    bool projected = ctx.worldToTile ? ctx.worldToTile(wx, wy, wz, tileX, tileZ) : false;
    if (!projected)
        return -1;

    // v6 = FindOrAllocSlot(v11, a2); if ( !v6 ) return -1;
    bool haveSlot = ctx.findOrAllocSlot ? ctx.findOrAllocSlot(h.slotKey, target) : true;
    if (!haveSlot)
        return -1;

    OrderStage v7{};
    v7.set_targetId(Plus4(target));   // v7[0] = *(a2+4)
    v7.set_kind(kOrderPacketMove);       // LOBYTE(v7[1]) = 3
    v7.set_field4(tileX);          // v7[4] = v9[0]
    v7.set_field5(tileZ);          // v7[5] = v9[1]
    return RequestBuildOp80(q, h.op80Owner, v7, -1);
}

// gilde.exe 0x488edc — VIBE_Command_BuildTilePacket.
i32 BuildTilePacket(CommandQueue& q, const CombatOrderHandle& h,
                    i32 target, const float* unitWorld,
                    const CombatOrderContext& ctx) {
    // if ( !WorldToTileWithHeight(map, unitWorld, &v10, &v12)
    //   || !FindSafestTileInRange(a2, v10, 8, v11, &v10, &v11) ) return -1;
    float wx = 0.0f, wy = 0.0f, wz = 0.0f;
    if (unitWorld) { wx = unitWorld[0]; wy = unitWorld[1]; wz = unitWorld[2]; }

    i32 v10 = 0;    // tileX scratch (input then output of FindSafestTileInRange)
    // v11 is the original's tileZ scratch. WorldToTileWithHeight here writes only
    // v10 (and the height float v12); v11 is NOT initialised before being passed
    // as FindSafestTileInRange's inZ arg in the binary. We model it as 0 (the
    // safest-tile search then offsets from inZ=0).
    i32 v11 = 0;
    i32 dummyZ = 0;
    bool projected = ctx.worldToTile ? ctx.worldToTile(wx, wy, wz, v10, dummyZ) : false;
    if (!projected)
        return -1;
    bool safe = ctx.findSafestTile
        ? ctx.findSafestTile(target, v10, 8, v11, v10, v11) : false;
    if (!safe)
        return -1;

    // v4 = FindOrAllocSlot(v13, a2);  (no null check in the original)
    if (ctx.findOrAllocSlot && !ctx.findOrAllocSlot(h.slotKey, target))
        return -1;

    OrderStage v6{};
    v6.set_targetId(Plus4(target));   // v6 = *(a2+4)
    v6.set_kind(kOrderPacketTile);       // v7 = 7
    v6.set_field4(v10);            // v8 = v10  (safest tile X)
    v6.set_field5(v11);            // v9 = v11  (safest tile Z)
    return RequestBuildOp80(q, h.op80Owner, v6, -1);
}

// gilde.exe 0x488fa4 — VIBE_Command_BuildSimplePacket.
i32 BuildSimplePacket(CommandQueue& q, const CombatOrderHandle& h,
                      i32 target, const CombatOrderContext& ctx) {
    // v4 = FindOrAllocSlot(a1, a2); v4[12]=0; qmemcpy(v6, v4, 0x2C);
    if (ctx.findOrAllocSlot && !ctx.findOrAllocSlot(h.slotKey, target))
        return -1;

    OrderStage v6{};
    v6.set_targetId(Plus4(target));   // v6[0] = *(a2+4)
    v6.set_kind(kOrderPacketSimple);     // LOBYTE(v6[1]) = 8
    return RequestBuildOp80(q, h.op80Owner, v6, -1);
}

} // namespace guild::sim
