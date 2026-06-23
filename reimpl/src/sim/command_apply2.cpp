#include "sim/command_apply2.h"

#include "sim/command_apply.h" // g_lastObjectId / g_lastSceneId / g_lastTradeId
#include "sim/entity.h"

#include <cstring>

namespace guild::sim {

// ===========================================================================
// Modeled leaf tables (stand in for the deferred render/scene/He/office/slot
// subsystems). Only the bytes the apply path touches are modeled; the default
// hooks operate on these so the handlers round-trip deterministically.
// ===========================================================================

namespace {

constexpr int kHeTableSlots    = 32;
constexpr int kSlotTableSlots  = 32;
constexpr int kTradeTableSlots = 64;

struct HeSlot     { bool used; i32 id; HeRecord rec; };
struct SlotSlot   { bool used; u8 type; i16 proto; BuildingSlot rec; };
struct TradeSlot  { bool used; i32 owner; i32 partner; TradeNode rec; };

HeSlot    g_heTable[kHeTableSlots];
SlotSlot  g_slotTable[kSlotTableSlots];
TradeSlot g_tradeTable[kTradeTableSlots];

// --- default leaf backends --------------------------------------------------

HeRecord* DefaultHeFind(i32 id) {
    for (int i = 0; i < kHeTableSlots; ++i)
        if (g_heTable[i].used && g_heTable[i].id == id)
            return &g_heTable[i].rec;
    return nullptr;
}

BuildingSlot* DefaultSlotFind(u8 typeByte, i16 proto) {
    for (int i = 0; i < kSlotTableSlots; ++i)
        if (g_slotTable[i].used && g_slotTable[i].type == typeByte && g_slotTable[i].proto == proto)
            return &g_slotTable[i].rec;
    return nullptr;
}

// Find-or-create a type-202 trade node under `ownerBuildingId` matching
// `partnerId`. Models VIBE_GameObject_QueryFind (find) + VIBE_GameObject_AddObjekt
// (create). The owner building must exist (the original resolves it first).
TradeNode* DefaultTradeNodeFindOrAdd(i32 ownerBuildingId, i32 partnerId) {
    if (!BuildingFindById(ownerBuildingId))
        return nullptr;
    // find existing node for this owner with a matching partner id (node+42).
    for (int i = 0; i < kTradeTableSlots; ++i) {
        if (g_tradeTable[i].used && g_tradeTable[i].owner == ownerBuildingId) {
            i32 nodePartner;
            std::memcpy(&nodePartner, g_tradeTable[i].rec.bytes + 42, 4);
            if (nodePartner == partnerId)
                return &g_tradeTable[i].rec;
        }
    }
    // allocate a fresh node; set g_lastTradeId to its (synthetic) node id.
    for (int i = 0; i < kTradeTableSlots; ++i) {
        if (!g_tradeTable[i].used) {
            g_tradeTable[i].used    = true;
            g_tradeTable[i].owner   = ownerBuildingId;
            g_tradeTable[i].partner = partnerId;
            std::memset(g_tradeTable[i].rec.bytes, 0, sizeof(TradeNode));
            g_lastTradeId = 0x4000 + i; // dword_631290 <- new node id
            return &g_tradeTable[i].rec;
        }
    }
    return nullptr;
}

i32  DefaultEstateTransfer(i32 /*fromId*/, const i32* /*spec*/) { return 0; } // index 0
i32  DefaultOfficeOp(const i32* /*spec*/) { return 0; }                        // success
i32  DefaultLawApply() { return 0; }                                            // success
int  DefaultObjStock(i32 /*c*/, i32 /*p*/, i32 /*amt*/) { return 1; }          // success
int  DefaultBuildingFree(i32 /*id*/) { return 0; }                              // 0 == ok
void DefaultBuildingRemove(i32 /*id*/) {}
int  DefaultSelectionToggle(i32 /*c*/, i32 /*s*/) { return 0; }                 // success

HeFindFn            g_heFind         = &DefaultHeFind;
SlotFindFn          g_slotFind       = &DefaultSlotFind;
TradeNodeFindOrAddFn g_tradeFind     = &DefaultTradeNodeFindOrAdd;
EstateTransferFn    g_estateTransfer = &DefaultEstateTransfer;
OfficeOpFn          g_officeAssign   = &DefaultOfficeOp;
OfficeOpFn          g_officeTransfer = &DefaultOfficeOp;
OfficeOpFn          g_officeSwap     = &DefaultOfficeOp;
LawApplyFn          g_lawApply       = &DefaultLawApply;
ObjStockOpFn        g_removeObjekt   = &DefaultObjStock;
ObjStockOpFn        g_addObjekt      = &DefaultObjStock;
ObjStockOpFn        g_decObjekt      = &DefaultObjStock;
BuildingFreeFn      g_buildingFree   = &DefaultBuildingFree;
BuildingRemoveFn    g_buildingRemove = &DefaultBuildingRemove;
SelectionToggleFn   g_selectToggle   = &DefaultSelectionToggle;

// --- shared ack helpers (mirror the +0/+1/+6 stamping in the originals) ------
inline void AckBegin(AckEntry* ack) {
    if (ack) { ack->status = 2; ack->slot = 0; ack->seq = 0; }
}
inline void AckOk(AckEntry* ack) { if (ack) ack->status = 1; }

// Remap the dword id at packet offset `off` through the -2/-3/-4 last-created
// tokens (dword_631288/63128C/631290 == g_lastObjectId/Scene/Trade) and write
// the resolved id back into the packet (the originals do this in place).
i32 RemapIdAt(CommandPacket& pkt, u32 off) {
    i32 id = static_cast<i32>(pkt.get32(off));
    switch (id) {
        case -2: id = g_lastObjectId; break;
        case -3: id = g_lastSceneId;  break;
        case -4: id = g_lastTradeId;  break;
        default: break;
    }
    pkt.put32(off, static_cast<u32>(id));
    return id;
}

} // namespace

// --- hook setters -----------------------------------------------------------
void SetHeFindHook(HeFindFn fn)               { g_heFind = fn ? fn : &DefaultHeFind; }
void SetSlotFindHook(SlotFindFn fn)           { g_slotFind = fn ? fn : &DefaultSlotFind; }
void SetTradeNodeHook(TradeNodeFindOrAddFn fn){ g_tradeFind = fn ? fn : &DefaultTradeNodeFindOrAdd; }
void SetEstateTransferHook(EstateTransferFn fn){ g_estateTransfer = fn ? fn : &DefaultEstateTransfer; }
void SetOfficeAssignHook(OfficeOpFn fn)       { g_officeAssign = fn ? fn : &DefaultOfficeOp; }
void SetOfficeTransferHook(OfficeOpFn fn)     { g_officeTransfer = fn ? fn : &DefaultOfficeOp; }
void SetOfficeSwapHook(OfficeOpFn fn)         { g_officeSwap = fn ? fn : &DefaultOfficeOp; }
void SetLawApplyHook(LawApplyFn fn)           { g_lawApply = fn ? fn : &DefaultLawApply; }
void SetRemoveObjektHook(ObjStockOpFn fn)     { g_removeObjekt = fn ? fn : &DefaultObjStock; }
void SetAddObjektHook(ObjStockOpFn fn)        { g_addObjekt = fn ? fn : &DefaultObjStock; }
void SetDecrementObjektHook(ObjStockOpFn fn)  { g_decObjekt = fn ? fn : &DefaultObjStock; }
void SetBuildingFreeHook(BuildingFreeFn fn)   { g_buildingFree = fn ? fn : &DefaultBuildingFree; }
void SetBuildingRemoveHook(BuildingRemoveFn fn){ g_buildingRemove = fn ? fn : &DefaultBuildingRemove; }
void SetSelectionToggleHook(SelectionToggleFn fn){ g_selectToggle = fn ? fn : &DefaultSelectionToggle; }

void ResetApply2State() {
    std::memset(g_heTable, 0, sizeof(g_heTable));
    std::memset(g_slotTable, 0, sizeof(g_slotTable));
    std::memset(g_tradeTable, 0, sizeof(g_tradeTable));
    g_heFind = &DefaultHeFind;
    g_slotFind = &DefaultSlotFind;
    g_tradeFind = &DefaultTradeNodeFindOrAdd;
    g_estateTransfer = &DefaultEstateTransfer;
    g_officeAssign = &DefaultOfficeOp;
    g_officeTransfer = &DefaultOfficeOp;
    g_officeSwap = &DefaultOfficeOp;
    g_lawApply = &DefaultLawApply;
    g_removeObjekt = &DefaultObjStock;
    g_addObjekt = &DefaultObjStock;
    g_decObjekt = &DefaultObjStock;
    g_buildingFree = &DefaultBuildingFree;
    g_buildingRemove = &DefaultBuildingRemove;
    g_selectToggle = &DefaultSelectionToggle;
}

// Test/seed access to the modeled leaf tables (declared in the header).
HeRecord* Apply2_SeedHe(i32 id) {
    for (int i = 0; i < kHeTableSlots; ++i) {
        if (!g_heTable[i].used) {
            g_heTable[i].used = true;
            g_heTable[i].id = id;
            std::memset(g_heTable[i].rec.bytes, 0, sizeof(HeRecord));
            return &g_heTable[i].rec;
        }
    }
    return nullptr;
}
BuildingSlot* Apply2_SeedSlot(u8 type, i16 proto) {
    for (int i = 0; i < kSlotTableSlots; ++i) {
        if (!g_slotTable[i].used) {
            g_slotTable[i].used = true;
            g_slotTable[i].type = type;
            g_slotTable[i].proto = proto;
            std::memset(g_slotTable[i].rec.bytes, 0, sizeof(BuildingSlot));
            return &g_slotTable[i].rec;
        }
    }
    return nullptr;
}
TradeNode* Apply2_FindTradeNode(i32 owner, i32 partner) {
    for (int i = 0; i < kTradeTableSlots; ++i) {
        if (g_tradeTable[i].used && g_tradeTable[i].owner == owner) {
            i32 p; std::memcpy(&p, g_tradeTable[i].rec.bytes + 42, 4);
            if (p == partner) return &g_tradeTable[i].rec;
        }
    }
    return nullptr;
}

// ===========================================================================
// Object-pair / remap handlers (0x0D..0x10).
// ===========================================================================

// gilde.exe 0x496888 — VIBE_Command_ExBindObjectProto (opcode 0x0D).
int ExBindObjectProto(CommandPacket& pkt, AckEntry* ack) {
    i32 fromId = static_cast<i32>(pkt.get32(0x10));      // *(a1+16)
    // *(int**)(a1+20) is a pointer to a spec block; we pass the payload slice.
    const i32* spec = reinterpret_cast<const i32*>(pkt.bytes + 0x14);
    i32 idx = g_estateTransfer(fromId, spec);            // LOWORD result
    if (ack) {
        ack->status = 1;            // *(_BYTE*)v4 = 1 (unconditional on non-null)
        ack->slot   = (idx >= 0) ? 1 : 0;
        ack->seq    = (idx >= 0) ? idx : 0; // +6: &word_12CE910[268*idx] -> model as idx
    }
    return 0;
}

// gilde.exe 0x4968D4 — VIBE_Command_ExRemapAndValidateObject (opcode 0x0E).
int ExRemapAndValidateObject(CommandPacket& pkt, AckEntry* ack) {
    // gilde.exe 0x4968e0: `xor eax,eax; mov ax,[ecx+10h]` => eax is the ZERO-
    // extended u16 id (0..0xFFFF). The remap compares the FULL 32-bit eax:
    //   cmp eax,0xFFFFFFFE  -> only matches when raw == 0xFFFE (a valid u16)
    //   cmp eax,0xFFFFFFFD / 0xFFFFFFFC  -> never match (eax <= 0xFFFF) => DEAD
    // So ONLY the -2 (0xFFFE) case remaps; -3/-4 are dead code in the binary.
    // The remap takes LOWORD(dword_631288) and stores `ax` back at +0x10.
    u16 raw = pkt.get16(0x10);
    u16 id = raw;
    if (raw == 0xFFFE)
        id = static_cast<u16>(g_lastObjectId); // mov ax, dword_631288 (low word)
    pkt.put16(0x10, id);                         // mov [ecx+10h], ax
    i32 lookup = static_cast<i32>(id);           // and ecx,0FFFFh (zero-extended)

    ObjectRec* obj = nullptr;
    SceneNode* scene = nullptr;
    Person* person = nullptr;
    if (!GameObjectResolveEntityById(&obj, &scene, lookup, &person))
        return 1;
    if (obj) {
        if (g_buildingFree(obj->id))   // nonzero == failure in this leaf path
            return 1;
        AckOk(ack);
        return 0;
    }
    if (scene)
        return 1;
    // person column: RemoveAndCleanup(*v9, 1) — v9 is the person id ptr.
    if (person)
        g_buildingRemove(person->id);
    AckOk(ack);
    return 0;
}

// gilde.exe 0x496978 — VIBE_Command_ExRemapObjectPair (opcode 0x0F).
int ExRemapObjectPair(CommandPacket& pkt, AckEntry* ack) {
    // proto = HIWORD(dword_13CD6F2[189 * byte(+0x1C)]). We don't have that table;
    // the proto resolution is a deferred leaf — model it via the raw type byte as
    // the proto key (the hooks receive it; tests verify the moved amount).
    u8  protoKey = pkt.bytes[0x1C];                     // *(a1+28)
    i32 amount;  std::memcpy(&amount, pkt.bytes + 0x1D, 4); // *(a1+29)

    i32 src = static_cast<i32>(pkt.get32(0x14));        // *(a1+20)
    if (src != -1) {
        i32 resolved = RemapIdAt(pkt, 0x14);
        if (!g_removeObjekt(resolved, protoKey, amount))
            return 1;
    }
    i32 dst = static_cast<i32>(pkt.get32(0x10));        // *(a1+16)
    if (dst != -1) {
        i32 resolved = RemapIdAt(pkt, 0x10);
        if (!g_addObjekt(resolved, protoKey, amount))
            return 1;
    }
    AckOk(ack);
    return 0;
}

// gilde.exe 0x496A90 — VIBE_Command_ExUpdateObjectPair (opcode 0x10).
int ExUpdateObjectPair(CommandPacket& pkt, AckEntry* ack) {
    u8  protoKey = pkt.bytes[0x1C];
    i32 amount;  std::memcpy(&amount, pkt.bytes + 0x1D, 4);

    i32 src = static_cast<i32>(pkt.get32(0x14));
    if (src != -1) {
        i32 resolved = RemapIdAt(pkt, 0x14);
        g_decObjekt(resolved, protoKey, amount); // void; no short-circuit
    }
    i32 dst = static_cast<i32>(pkt.get32(0x10));
    if (dst != -1) {
        i32 resolved = RemapIdAt(pkt, 0x10);
        g_addObjekt(resolved, protoKey, amount);
    }
    AckOk(ack);
    return 0;
}

// ===========================================================================
// He record (0x1D).
// ===========================================================================

// gilde.exe 0x498890 — VIBE_Command_ExSetHE (opcode 0x1D).
int ExSetHE(CommandPacket& pkt, AckEntry* ack) {
    if (ack) { ack->status = 2; ack->slot = 4; ack->seq = 0; }
    HeRecord* he = g_heFind(static_cast<i32>(pkt.get32(0x10))); // filter on +16
    if (!he)
        return 1;
    u8* r = he->bytes;
    const u8* p = pkt.bytes;
    auto wr32 = [&](int dstOff, u32 srcOff) { u32 v = pkt.get32(srcOff); std::memcpy(r + dstOff, &v, 4); };
    auto wr16 = [&](int dstOff, u32 srcOff) { u16 v = pkt.get16(srcOff); std::memcpy(r + dstOff, &v, 2); };
    wr32(68,  20); // [+68]  <- payload +20
    wr32(72,  24); // [+72]  <- +24
    wr32(76,  28); // [+76]  <- +28
    wr16(80,  32); // [+80]  <- +32 (word)
    wr32(82,  34); // [+82]  <- +34
    wr32(86,  38); // [+86]  <- +38
    wr32(90,  42); // [+90]  <- +42
    wr16(94,  46); // [+94]  <- +46 (word)
    wr32(96,  48); // [+96]  <- +48
    wr32(100, 52); // [+100] <- +52
    wr32(104, 56); // [+104] <- +56
    wr16(108, 60); // [+108] <- +60 (word)
    // [+112] <- (*(int*)(payload+59)) >> 24  (arithmetic shift on a signed dword).
    i32 v59; std::memcpy(&v59, p + 59, 4);
    i32 hi = v59 >> 24;
    std::memcpy(r + 112, &hi, 4);

    if (ack) { ack->status = 1; ack->slot = 4; ack->seq = 0; }
    return 0;
}

// ===========================================================================
// Selection (0x2B).
// ===========================================================================

// gilde.exe 0x499558 — VIBE_Command_ExSelectObject (opcode 0x2B).
int ExSelectObject(CommandPacket& pkt, AckEntry* ack) {
    if (ack) ack->status = 2;
    ObjectRec* obj = nullptr;
    SceneNode* scene = nullptr;
    Person* person = nullptr;
    GameObjectResolveEntityById(&obj, &scene, static_cast<i32>(pkt.get32(0x10)), &person);
    i32 subId = static_cast<i32>(pkt.get32(0x18)); // a1[6]

    if (!obj) {
        // No object record. Original probes person, then scene.
        if (person) {
            int r = g_selectToggle(person->id, subId);
            if (r) return r;
            AckOk(ack);
            return 0;
        }
        if (!scene)
            return -1;
        int r = g_selectToggle(scene->id, subId);
        if (r) return r;
        AckOk(ack);
        return 0;
    }
    // Object present.
    i32 wantSub = static_cast<i32>(pkt.get32(0x14)); // a1[5]
    if (wantSub == -1) {
        int r = g_selectToggle(obj->id, subId);
        if (r) return r;
        AckOk(ack);
        return 0;
    }
    // The original re-queries a child scene node here (QueryFind ... a1[5]); the
    // tree query is a deferred leaf, so we model "sub-object found" via the same
    // selection-toggle leaf keyed on the object's id and the requested sub.
    int r = g_selectToggle(obj->id, subId);
    if (r) return r;
    AckOk(ack);
    return 0;
}

// ===========================================================================
// Building slot state (0x3F).
// ===========================================================================

// gilde.exe 0x49BBF8 — VIBE_Command_ExSetObjectState (opcode 0x3F).
int ExSetObjectState(CommandPacket& pkt, AckEntry* ack) {
    u8  typeByte = pkt.bytes[0x10];                       // *(a1+16)
    // proto = HIWORD(*(dword*)(a1+15)) == bytes[17]|bytes[18]<<8.
    i16 proto = static_cast<i16>(pkt.bytes[0x11] | (pkt.bytes[0x12] << 8));
    BuildingSlot* slot = g_slotFind(typeByte, proto);
    if (!slot)
        return 1;
    std::memcpy(slot->bytes, pkt.bytes + 0x11, 0x80);     // qmemcpy(slot, a1+17, 0x80)
    if (ack) { ack->status = 1; ack->slot = 0; ack->seq = 0; } // +0=1,+1=0,+6=0
    return 0;
}

// ===========================================================================
// Office / law delegators (0x44/0x45/0x46/0x5C).
// ===========================================================================

static inline void AckResult(AckEntry* ack, int ret) {
    if (ack) { ack->slot = 0; ack->seq = 0; ack->status = static_cast<u8>((ret == 0) + 1); }
}

// gilde.exe 0x49BD74 — VIBE_Command_ExAssignOffice (opcode 0x44).
int ExAssignOffice(CommandPacket& pkt, AckEntry* ack) {
    i32 ret = g_officeAssign(reinterpret_cast<const i32*>(pkt.bytes + 0x10));
    AckResult(ack, ret);
    return (ret == 0);
}

// gilde.exe 0x49BDB0 — VIBE_Command_ExTransferOffice (opcode 0x45).
int ExTransferOffice(CommandPacket& pkt, AckEntry* ack) {
    i32 ret = g_officeTransfer(reinterpret_cast<const i32*>(pkt.bytes + 0x10));
    AckResult(ack, ret);
    return (ret == 0);
}

// gilde.exe 0x49BDEC — VIBE_Command_ExReleaseOffice (opcode 0x46).
int ExReleaseOffice(CommandPacket& /*pkt*/, AckEntry* ack) {
    i32 ret = g_lawApply();
    AckResult(ack, ret);
    return 0; // the original always returns 0
}

// gilde.exe 0x49D1BC — VIBE_Command_ExSwapOfficeHolders (opcode 0x5C).
int ExSwapOfficeHolders(CommandPacket& pkt, AckEntry* ack) {
    if (ack) { ack->status = 2; ack->slot = 0; ack->seq = 0; }
    i32 ret = g_officeSwap(reinterpret_cast<const i32*>(pkt.bytes + 0x10));
    // gilde.exe 0x49d1dd: `test eax,eax; jnz loc_49D1ED` -> `mov [edx],1` runs ONLY
    // when ret != 0 (SwapHolders returned nonzero). Return value is (ret==0).
    if (ack && ret != 0) ack->status = 1; // *v3 = 1 only when ret != 0 (and ack set)
    return (ret == 0);
}

// ===========================================================================
// Trade-route entry upsert (0x5E).
// ===========================================================================

// gilde.exe 0x49D2E8 — VIBE_Command_ExUpsertTradeEntry (opcode 0x5E).
int ExUpsertTradeEntry(CommandPacket& pkt, AckEntry* ack) {
    AckBegin(ack); // +0=2, +1=0, +6=0
    // Both endpoints must exist.
    if (!BuildingFindById(static_cast<i32>(pkt.get32(0x26)))) // *(a1+38) partner
        return 1;
    i32 ownerId = static_cast<i32>(pkt.get32(0x14));          // *(a1+20) owner
    if (!BuildingFindById(ownerId))
        return 1;

    i32 partnerId = static_cast<i32>(pkt.get32(0x26));
    TradeNode* node = g_tradeFind(ownerId, partnerId);
    if (!node)
        return 1;

    u8* n = node->bytes;
    u32 f0 = pkt.get32(0x18); std::memcpy(n + 28, &f0, 4); // [+28] <- +24
    u32 f1 = pkt.get32(0x1C); std::memcpy(n + 32, &f1, 4); // [+32] <- +28
    u32 f2 = pkt.get32(0x20); std::memcpy(n + 36, &f2, 4); // [+36] <- +32
    u16 f3 = pkt.get16(0x24); std::memcpy(n + 40, &f3, 2); // [+40] <- +36 (word)
    u32 f4 = pkt.get32(0x26); std::memcpy(n + 42, &f4, 4); // [+42] <- +38 (partner key)
    u32 f5 = pkt.get32(0x2E); std::memcpy(n + 50, &f5, 4); // [+50] <- +46
    n[54] = pkt.bytes[0x32];                                // [+54] <- +50 (byte)
    // [+55]: (payload +51 byte) + node[+55], clamp to <=100.
    u8 pct = static_cast<u8>(pkt.bytes[0x33] + n[55]);
    n[55] = (pct <= 100) ? pct : 100;

    AckOk(ack);
    return 0;
}

// ===========================================================================
// Dispatch wiring.
// ===========================================================================

namespace {
#define APPLY2_ADAPTER(NAME) \
    void NAME##_adapter(CommandQueue&, CommandPacket& pkt, AckEntry* ack) { NAME(pkt, ack); }
APPLY2_ADAPTER(ExBindObjectProto)
APPLY2_ADAPTER(ExRemapAndValidateObject)
APPLY2_ADAPTER(ExRemapObjectPair)
APPLY2_ADAPTER(ExUpdateObjectPair)
APPLY2_ADAPTER(ExSetHE)
APPLY2_ADAPTER(ExSelectObject)
APPLY2_ADAPTER(ExSetObjectState)
APPLY2_ADAPTER(ExAssignOffice)
APPLY2_ADAPTER(ExTransferOffice)
APPLY2_ADAPTER(ExReleaseOffice)
APPLY2_ADAPTER(ExSwapOfficeHolders)
APPLY2_ADAPTER(ExUpsertTradeEntry)
#undef APPLY2_ADAPTER
} // namespace

void RegisterApplyHandlers2(CommandQueue& q) {
    q.set_handler(kOp2BindObjectProto,     &ExBindObjectProto_adapter);
    q.set_handler(kOp2RemapValidateObject, &ExRemapAndValidateObject_adapter);
    q.set_handler(kOp2RemapObjectPair,     &ExRemapObjectPair_adapter);
    q.set_handler(kOp2UpdateObjectPair,    &ExUpdateObjectPair_adapter);
    q.set_handler(kOp2SetHE,               &ExSetHE_adapter);
    q.set_handler(kOp2SelectObject,        &ExSelectObject_adapter);
    q.set_handler(kOp2SetObjectState,      &ExSetObjectState_adapter);
    q.set_handler(kOp2AssignOffice,        &ExAssignOffice_adapter);
    q.set_handler(kOp2TransferOffice,      &ExTransferOffice_adapter);
    q.set_handler(kOp2ReleaseOffice,       &ExReleaseOffice_adapter);
    q.set_handler(kOp2SwapOfficeHolders,   &ExSwapOfficeHolders_adapter);
    q.set_handler(kOp2UpsertTradeEntry,    &ExUpsertTradeEntry_adapter);
}

int ApplyPacket2(CommandPacket& pkt, AckEntry* ack) {
    switch (pkt.opcode()) {
        case kOp2BindObjectProto:     return ExBindObjectProto(pkt, ack);
        case kOp2RemapValidateObject: return ExRemapAndValidateObject(pkt, ack);
        case kOp2RemapObjectPair:     return ExRemapObjectPair(pkt, ack);
        case kOp2UpdateObjectPair:    return ExUpdateObjectPair(pkt, ack);
        case kOp2SetHE:               return ExSetHE(pkt, ack);
        case kOp2SelectObject:        return ExSelectObject(pkt, ack);
        case kOp2SetObjectState:      return ExSetObjectState(pkt, ack);
        case kOp2AssignOffice:        return ExAssignOffice(pkt, ack);
        case kOp2TransferOffice:      return ExTransferOffice(pkt, ack);
        case kOp2ReleaseOffice:       return ExReleaseOffice(pkt, ack);
        case kOp2SwapOfficeHolders:   return ExSwapOfficeHolders(pkt, ack);
        case kOp2UpsertTradeEntry:    return ExUpsertTradeEntry(pkt, ack);
        default:                      return -1; // unknown/guarded: no state change
    }
}

} // namespace guild::sim
