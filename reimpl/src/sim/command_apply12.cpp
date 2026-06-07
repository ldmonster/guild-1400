#include "sim/command_apply12.h"

#include <cstring>

namespace guild::sim {

// ---------------------------------------------------------------------------
// The 2-byte-stride label copy (shared by both label builders).
// ---------------------------------------------------------------------------
//
// Original loop (BuildLabeledMoveCommand @0x488db6, identical in Conquer):
//   v8 = dst;
//   do {
//     v9   = *src; *v8 = *src;      // copy byte 0 of the pair
//     if (!v9) break;               // first byte NUL -> stop (1 byte written)
//     v10  = src[1]; src += 2;
//     v8[1] = v10; v8 += 2;         // copy byte 1, advance both by 2
//   } while (v10);                  // second byte NUL -> stop (pair written)
// We bound the writes by dstCap (the recovered slots leave 0x10 bytes for the
// label before the next field, so the real text is always short; the cap just
// keeps a malformed unterminated source from running off the buffer).
u32 CopyStrideLabel(u8* dst, u32 dstCap, const char* src) {
    if (!dst || !src || dstCap == 0)
        return 0;
    const u8* s = reinterpret_cast<const u8*>(src);
    u32 w = 0;
    for (;;) {
        u8 b0 = s[0];
        if (w >= dstCap) break;
        dst[w++] = b0;                 // *v8 = *src
        if (!b0)                       // if (!v9) break;
            break;
        u8 b1 = s[1];
        if (w >= dstCap) break;
        dst[w++] = b1;                 // v8[1] = v10
        s += 2;                        // src += 2
        if (!b1)                       // while (v10)
            break;
    }
    return w;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x488d4c — VIBE_Command_BuildLabeledMoveCommand.
// ---------------------------------------------------------------------------
i32 BuildLabeledMoveCommand(const CombatOrderHandle& h, i32 target,
                            const float* bonePoint, const char* label,
                            OrderStage& slot, const CombatOrderContext& ctx) {
    // VIBE_Combat_FindObjectDef(a2) is called for its side effect only (the result
    // is discarded here); we forward it to keep the call sequence faithful.
    if (ctx.findObjectDef) {
        i16 wc = 0; u8 ws = 0;
        ctx.findObjectDef(target, wc, ws);
    }

    // VIBE_Transform_PointThroughBoneChain(a3, a3+19, v12) -> world point.
    float wx = 0.0f, wy = 0.0f, wz = 0.0f;
    if (bonePoint) { wx = bonePoint[0]; wy = bonePoint[1]; wz = bonePoint[2]; }
    if (ctx.transformPoint)
        ctx.transformPoint(bonePoint, wx, wy, wz);

    // if ( !WorldToTileWithHeight(map, v12, v16, &v15) ) return 0;
    //   v16[0] -> tile X (slot+0x20), &v15 (4th out) -> slot+0x24.
    i32 tileX = 0, tileZ = 0;
    bool projected = ctx.worldToTile ? ctx.worldToTile(wx, wy, wz, tileX, tileZ) : false;
    if (!projected)
        return 0;

    // v7 = FindOrAllocSlot(handle, a2). The freshly-alloc'd slot is zero-cleared;
    // `slot` models that. findOrAllocSlot reports availability (the original does
    // not null-check here, but a roster-full slot would be 0 -> we honour that).
    bool haveSlot = ctx.findOrAllocSlot ? ctx.findOrAllocSlot(h.slotKey, target) : true;
    if (!haveSlot)
        return 0;

    slot = OrderStage{};                       // zero-cleared alloc'd slot
    slot.bytes[kSlotPreClear] = 0;             // *(v7+12) = 0  (already 0)
    CopyStrideLabel(slot.bytes + kSlotLabel,
                    kSlotTileX - kSlotLabel,   // 0x10 bytes before tile X
                    label);
    slot.put32(kSlotTargetId, target + 4);     // *v7      = *(a2+4)
    slot.bytes[kSlotKind] = kOrderKindLabeledMove; // *(v7+4) = 4
    slot.put32(kSlotTileX, tileX);             // *(v7+32) = v16[0]
    slot.put32(kSlotTileZ, tileZ);             // *(v7+36) = v14
    return 1;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x488df4 — VIBE_Command_BuildConquerCommand.
// ---------------------------------------------------------------------------
i32 BuildConquerCommand(CommandQueue& q, const CombatOrderHandle& h, i32 target,
                        const float* bonePoint, const char* label,
                        i32 targetBusyFlag, const CombatOrderContext& ctx) {
    if (ctx.findObjectDef) {
        i16 wc = 0; u8 ws = 0;
        ctx.findObjectDef(target, wc, ws);
    }

    float wx = 0.0f, wy = 0.0f, wz = 0.0f;
    if (bonePoint) { wx = bonePoint[0]; wy = bonePoint[1]; wz = bonePoint[2]; }
    if (ctx.transformPoint)
        ctx.transformPoint(bonePoint, wx, wy, wz);

    // if ( !WorldToTileWithHeight(...) || *(a2+0x1AC) ) return -1;
    i32 tileX = 0, tileZ = 0;
    bool projected = ctx.worldToTile ? ctx.worldToTile(wx, wy, wz, tileX, tileZ) : false;
    if (!projected || targetBusyFlag)
        return -1;

    bool haveSlot = ctx.findOrAllocSlot ? ctx.findOrAllocSlot(h.slotKey, target) : true;
    if (!haveSlot)
        return -1;

    // qmemcpy(&v12, slot, 44): start the staging block from the zeroed slot.
    OrderStage stage{};
    stage.bytes[kSlotPreClear] = 0;            // v6[12] = 0  (already 0)
    stage.put32(kSlotTargetId, target + 4);    // v12 = *(a2+4)
    stage.bytes[kSlotKind] = kOrderKindConquer; // v13 = 6
    CopyStrideLabel(stage.bytes + kSlotLabel,
                    kConquerTileXByte - kSlotLabel,
                    label);
    // The tile lands as two SINGLE BYTES (LOBYTE of the WorldToTile outs).
    stage.bytes[kConquerTileXByte] = static_cast<u8>(tileX); // v14[16] = v16
    stage.bytes[kConquerTileZByte] = static_cast<u8>(tileZ); // v14[17] = v17[0]
    return RequestBuildOp80(q, h.op80Owner, stage, -1);
}

// ---------------------------------------------------------------------------
// gilde.exe 0x488ff0 — VIBE_Command_IssueOnObject (the cursor-click router).
// ---------------------------------------------------------------------------
namespace {
const IssueOnObjectHooks* g_issueHooks = nullptr;

// String prefix/equality leaves the original used (VIBE_Util_StrCmp @0x5d3f10
// returns 0 when equal; VIBE_Util_StrncmpN @0x5e9ee0 is strncmp). We provide tiny
// local equivalents for the inert default path; the integration test forwards the
// prefix test into the REAL reconstructed util::StrncmpN sibling instead.
int LocalStrCmp(const char* a, const char* b) {
    if (!a || !b) return a == b ? 0 : 1;
    return std::strcmp(a, b);
}
int LocalStrncmp(const char* a, const char* b, int n) {
    if (!a || !b) return a == b ? 0 : 1;
    return std::strncmp(a, b, static_cast<std::size_t>(n));
}
} // namespace

void SetIssueOnObjectHooks(const IssueOnObjectHooks* hooks) {
    g_issueHooks = hooks;
}

char IssueOnObject(CommandQueue& q, const CombatOrderHandle& h, i32 target,
                   const CombatOrderContext& ctx, OrderStage& outSlot) {
    const IssueOnObjectHooks* hk = g_issueHooks;
    char result = 0;   // v18 = 0

    // if ( !dword_67221C ) return 0;   (the cursor-order armed latch)
    bool armed = (hk && hk->orderArmed) ? hk->orderArmed() : false;
    if (!armed)
        return result;

    // if ( dword_631720 ) { ...object/label branch... } else { ...ground... }
    i32 picked = (hk && hk->pickedObject) ? hk->pickedObject() : 0;
    if (picked) {
        // if ( *(picked+535) == 2 )   -> the picked object is a battle UNIT.
        bool isUnit = (hk && hk->pickedIsUnit) ? hk->pickedIsUnit(picked) : false;
        if (isUnit) {
            i32 owner = (hk && hk->pickedUnitOwner) ? hk->pickedUnitOwner(picked) : 0;
            // ( *(a2+364) != *(owner+364) || byte_671D96 )
            //   && BuildAttackPacket(...) != -1  -> 1
            bool allowed = (hk && hk->attackAllowed) ? hk->attackAllowed(target, owner) : false;
            if (allowed) {
                // The original passes secondary=(*(a2+452) in {1,2,3}); with no
                // game-state hook for it we leave it false (inert), matching the
                // common path. a5 (attack param) is 0 as in the binary.
                i32 r = BuildAttackPacket(q, h, target, owner, /*a5=*/0,
                                          /*secondaryByteIn123=*/false, ctx);
                if (r != -1)
                    return 1;
            }
            return result;
        }

        // Treat dword_631720 as an order-label STRING. The two prefix tests use the
        // strncmp leaf, forwarded to the real util::StrncmpN by the live wiring (and
        // the integration test); inert default falls back to the libc-equivalent.
        const char* label = (hk && hk->pickedLabel) ? hk->pickedLabel() : nullptr;
        auto strncmpN = [hk](const char* a, const char* b, int n) -> int {
            if (hk && hk->labelStrncmp) return hk->labelStrncmp(a, b, n);
            return LocalStrncmp(a, b, n);
        };
        // if ( VIBE_Util_StrCmp("sp_ESCAPE", label) )  -> label != "sp_ESCAPE"
        if (LocalStrCmp("sp_ESCAPE", label) != 0) {
            // if ( StrncmpN(label, "sp_CONQUER", 10) )  -> not a conquer order
            if (strncmpN(label, "sp_CONQUER", 10) != 0) {
                // if ( !StrncmpN(label, "WARE", 4) )    -> a WARE conquer order
                if (strncmpN(label, "WARE", 4) == 0) {
                    // BuildConquerCommand(handle, a2, label); v18 = 1; return 1.
                    // The label doubles as the bone-point in the original (the
                    // dword is reinterpreted both ways); we pass no bone point —
                    // ctx.transformPoint/worldToTile carry the geometry.
                    BuildConquerCommand(q, h, target, /*bonePoint=*/nullptr,
                                        label, /*targetBusyFlag=*/0, ctx);
                    return 1;
                }
                return result;   // label is neither escape, conquer, nor ware
            }
            // sp_CONQUER prefix -> labelled move (kind 4).
            i32 r = BuildLabeledMoveCommand(h, target, /*bonePoint=*/nullptr,
                                            label, outSlot, ctx);
            result = static_cast<char>(r);   // v18 = return of the builder
            return result;
        } else {
            // label == "sp_ESCAPE" -> a plain MOVE-to packet (kind 3).
            i32 r = BuildMoveToPacket(q, h, target, /*bonePoint=*/nullptr, ctx);
            result = static_cast<char>(r);
            return result;
        }
    } else {
        // No object under the cursor: raycast the cursor to a ground tile (two out
        // params: var_1C and var_18) and, on hit, build a MOVE order DIRECTLY into
        // the slot. Staging (base var_48): +0x00 target id, +0x04 kind=1, +0x10
        // first raycast out, +0x14 second raycast out, then enqueue via Op80.
        i32 gx = 0, gz = 0;
        bool hit = (hk && hk->raycastGround) ? hk->raycastGround(gx, gz) : false;
        if (hit) {
            bool haveSlot = ctx.findOrAllocSlot ? ctx.findOrAllocSlot(h.slotKey, target) : true;
            if (haveSlot) {
                outSlot = OrderStage{};
                outSlot.bytes[kSlotPreClear] = 0;          // v9[12] = 0
                outSlot.put32(kSlotTargetId, target + 4);  // var_48 = *(a2+4)
                outSlot.bytes[kSlotKind] = kOrderKindGroundMove; // var_44 = 1 (dl)
                outSlot.set_field4(gx);                    // var_38 = var_1C
                outSlot.set_field5(gz);                    // var_34 = var_18
                RequestBuildOp80(q, h.op80Owner, outSlot, -1);
                result = 1;                                // var_10 = 1
            }
        } else {
            // VIBE_Hud_SetStatusBannerText(dword_8C6F2C): "can't move there".
            if (hk && hk->rejectBanner)
                hk->rejectBanner();
        }
        return result;
    }
}

} // namespace guild::sim
