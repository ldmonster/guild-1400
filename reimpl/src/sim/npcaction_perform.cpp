#include "sim/npcaction_perform.h"

#include <cstdint>

namespace guild::sim {

// ===========================================================================
// Hook plumbing (mirrors npcaction5.cpp / npcevent_steps2.cpp).
// ===========================================================================
static const NpcActionPerformHooks kInertPerformHooks{};
static const NpcActionPerformHooks* g_performHooks = &kInertPerformHooks;
void SetNpcActionPerformHooks(const NpcActionPerformHooks* hooks) {
    g_performHooks = hooks ? hooks : &kInertPerformHooks;
}
const NpcActionPerformHooks& GetNpcActionPerformHooks() { return *g_performHooks; }

// ---------------------------------------------------------------------------
// Byte-faithful raw-offset accessors on the action/context record. The originals
// address it by explicit offset off a register base; we keep that exactly.
// ---------------------------------------------------------------------------
static inline u8  CtxByte(const u8* p, int off) { return p[off]; }
static inline i32 CtxDword(const u8* p, int off) {
    return *reinterpret_cast<const i32*>(p + off);
}

// ===========================================================================
// 0x470ea4 — VIBE_NpcAction_PerformSpionage.
//   (__usercall: ecx=actor, edx=ctx, eax=live-actor record.)
//   Gate: ctx type byte (+0) must be 7. Build the op28 slot-reset packet:
//     actionId = 24, objectId = *(live+4), seq = -1, byte = 2,
//     ownerId   = (live+368 ? *(*(live+368)+1) : -1),
//     fieldA = *(ctx+1)?  no — *((dword*)ctx + 1) == ctx+4 ; fieldB = ctx+16.
//   The original reads v15 = *((DWORD*)a2+1) (ctx+4) and v16 = *((DWORD*)a2+4)
//   (ctx+16). Wrap in EnqueueBuildingActionStart("spionage")/End. Returns 37.
// ===========================================================================
int NpcActionPerform_Spionage(const void* /*actor*/, const u8* ctx, const u8* live) {
    const auto& hk = GetNpcActionPerformHooks();
    if (CtxByte(ctx, 0) != 7)
        return 0;

    // ownerId: v5 = *(live+368) is a sub-record pointer; the original then reads
    //   v6 = (v5 ? *(_DWORD*)(v5 + 1) : -1)   (a +1-byte unaligned dword read).
    i32 ownerSub = CtxDword(live, 368);
    i32 ownerId = -1;
    if (ownerSub) {
        ownerId = *reinterpret_cast<const i32*>(
            reinterpret_cast<const u8*>(static_cast<std::intptr_t>(ownerSub)) + 1);
    }

    i32 objectId = CtxDword(live, 4);   // v11 = *(_DWORD*)(a3 + 4)
    i32 fieldA   = CtxDword(ctx, 4);   // *((DWORD*)ctx + 1)
    i32 fieldB   = CtxDword(ctx, 16);  // *((DWORD*)ctx + 4)

    if (hk.enqueueBuildingActionStart) hk.enqueueBuildingActionStart("spionage");
    if (hk.queueRequestSlotReset28)
        hk.queueRequestSlotReset28(/*actionId*/ 24, objectId, /*seq*/ -1, ownerId,
                                   fieldA, fieldB);
    if (hk.enqueueBuildingActionEnd) hk.enqueueBuildingActionEnd();
    return 37;
}

// ===========================================================================
// 0x471840 — VIBE_NpcAction_PerformEnterBuilding.
//   threaten gate -> 40, else the interaction reject stub.
// ===========================================================================
int NpcActionPerform_EnterBuilding() {
    const auto& hk = GetNpcActionPerformHooks();
    if (hk.aiExecThreaten && hk.aiExecThreaten())
        return 40;
    return hk.interactionEvalRejectStub ? hk.interactionEvalRejectStub() : 0;
}

// ===========================================================================
// 0x471cb4 — VIBE_NpcAction_PerformUseBack(ctx@eax).
//   slander gate; if rejected return 0, else op25(*(ctx+4),484,512,4,0) -> 42.
// ===========================================================================
int NpcActionPerform_UseBack(const u8* ctx) {
    const auto& hk = GetNpcActionPerformHooks();
    if (!(hk.aiExecSlander && hk.aiExecSlander(CtxDword(ctx, 0))))
        return 0;
    if (hk.queueRequestArgs25)
        hk.queueRequestArgs25(CtxDword(ctx, 4), 484, 512, 4, 0);
    return 42;
}

// ===========================================================================
// 0x471dfc — VIBE_NpcAction_PerformOpenDoorLarge.  slander-label gate -> 43.
// ===========================================================================
int NpcActionPerform_OpenDoorLarge() {
    const auto& hk = GetNpcActionPerformHooks();
    if (hk.aiFormatSlanderLabel && hk.aiFormatSlanderLabel())
        return 43;
    return hk.interactionEvalRejectStub ? hk.interactionEvalRejectStub() : 0;
}

// ===========================================================================
// 0x471f24 — VIBE_NpcAction_PerformOpenDoorSmall.  slander-label gate -> 44.
// ===========================================================================
int NpcActionPerform_OpenDoorSmall() {
    const auto& hk = GetNpcActionPerformHooks();
    if (hk.aiFormatSlanderLabel && hk.aiFormatSlanderLabel())
        return 44;
    return hk.interactionEvalRejectStub ? hk.interactionEvalRejectStub() : 0;
}

// ===========================================================================
// 0x4737cc — VIBE_NpcAction_PerformShopTransaction(a@edx, b@ebx).
//   (*a==4 && *b==1) -> spy-mission gate -> 50/0.
//   (*a==19 && *b==4) -> dark-corner gate -> 50; else 0.
// ===========================================================================
int NpcActionPerform_ShopTransaction(const u8* a, const u8* b) {
    const auto& hk = GetNpcActionPerformHooks();
    if (CtxByte(a, 0) == 4 && CtxByte(b, 0) == 1) {
        if (hk.aiQueueSpyMission && hk.aiQueueSpyMission())
            return 50;
        return 0;
    }
    if (CtxByte(a, 0) != 19 || CtxByte(b, 0) != 4 ||
        !(hk.aiBuyDarkCorner && hk.aiBuyDarkCorner()))
        return 0;
    return 50;
}

// ===========================================================================
// 0x4742ec — VIBE_NpcAction_PerformDrinkTavern(actor@eax, ctx@edx, sub@ebx).
//   tavern-graphic gate; on accept emit op25/op72/op90 and return 52.
//   op72's byte arg is HIBYTE(*(_DWORD*)(ctx+13)) == byte ctx+16.
// ===========================================================================
int NpcActionPerform_DrinkTavern(const u8* actor, const u8* ctx, i32 sub) {
    const auto& hk = GetNpcActionPerformHooks();
    if (!(hk.aiLoadBuildingGraphic &&
          hk.aiLoadBuildingGraphic(CtxDword(actor, 4), ctx, sub)))
        return 0;
    i32 actorId = CtxDword(actor, 4);
    if (hk.queueRequestArgs25)
        hk.queueRequestArgs25(actorId, 456, /*&unk_1000000*/ 0x1000000, 4, 0);
    if (hk.requestBuildOp72)
        hk.requestBuildOp72(actorId, CtxByte(ctx, 16));
    if (hk.requestBuildOp90)
        hk.requestBuildOp90(8, actorId);
    return 52;
}

// ===========================================================================
// 0x474c18 — VIBE_NpcAction_PerformVerdict(actor@ecx, ctx@edx, target@eax).
//   Gate ctx type == 21. packet = {kind 3, id *(ctx+4), field *(ctx+16)};
//   Amt_BuildGuildOfficeMenu(target, &packet) -> 54.
// ===========================================================================
int NpcActionPerform_Verdict(const void* /*actor*/, const u8* ctx, i32 target) {
    const auto& hk = GetNpcActionPerformHooks();
    if (CtxByte(ctx, 0) != 21)
        return 0;
    if (hk.amtBuildGuildOfficeMenu)
        hk.amtBuildGuildOfficeMenu(target, /*kind*/ 3, CtxDword(ctx, 4),
                                   CtxDword(ctx, 16));
    return 54;
}

// ===========================================================================
// 0x474da0 — VIBE_NpcAction_PerformArrest(actor@ecx, ctx@edx, target@eax).
//   Gate ctx type == 4. packet = {kind 2, id *(ctx+4), field *(ctx+16)};
//   Amt_BuildElectionDialog(target, &packet) -> 55.
// ===========================================================================
int NpcActionPerform_Arrest(const void* /*actor*/, const u8* ctx, i32 target) {
    const auto& hk = GetNpcActionPerformHooks();
    if (CtxByte(ctx, 0) != 4)
        return 0;
    if (hk.amtBuildElectionDialog)
        hk.amtBuildElectionDialog(target, /*kind*/ 2, CtxDword(ctx, 4),
                                  CtxDword(ctx, 16));
    return 55;
}

// ===========================================================================
// 0x4756c4 — VIBE_NpcAction_PerformPickupCarry(actor@ecx, ctx@edx, target@eax).
//   Gate ctx type == 7 or 22. packet = {kind = byte *(ctx+16), id *(ctx+4),
//   seq 0}; Amt_BuildOfficeActionDialog(target, &packet) -> 56.
// ===========================================================================
int NpcActionPerform_PickupCarry(const void* /*actor*/, const u8* ctx, i32 target) {
    const auto& hk = GetNpcActionPerformHooks();
    if (CtxByte(ctx, 0) != 7 && CtxByte(ctx, 0) != 22)
        return 0;
    if (hk.amtBuildOfficeActionDialog)
        hk.amtBuildOfficeActionDialog(target, /*kind*/ CtxByte(ctx, 16),
                                      CtxDword(ctx, 4), /*seq*/ 0);
    return 56;
}

// ===========================================================================
// Registration.
// ===========================================================================
namespace {
struct Binding { int address; const void* fn; };
const Binding kBindings[] = {
    { 0x470ea4, reinterpret_cast<const void*>(&NpcActionPerform_Spionage) },
    { 0x471840, reinterpret_cast<const void*>(&NpcActionPerform_EnterBuilding) },
    { 0x471cb4, reinterpret_cast<const void*>(&NpcActionPerform_UseBack) },
    { 0x471dfc, reinterpret_cast<const void*>(&NpcActionPerform_OpenDoorLarge) },
    { 0x471f24, reinterpret_cast<const void*>(&NpcActionPerform_OpenDoorSmall) },
    { 0x4737cc, reinterpret_cast<const void*>(&NpcActionPerform_ShopTransaction) },
    { 0x4742ec, reinterpret_cast<const void*>(&NpcActionPerform_DrinkTavern) },
    { 0x474c18, reinterpret_cast<const void*>(&NpcActionPerform_Verdict) },
    { 0x474da0, reinterpret_cast<const void*>(&NpcActionPerform_Arrest) },
    { 0x4756c4, reinterpret_cast<const void*>(&NpcActionPerform_PickupCarry) },
};
} // namespace

int RegisterNpcActionsPerform() {
    return static_cast<int>(sizeof(kBindings) / sizeof(kBindings[0]));
}

const void* NpcActionPerform_TableEntry(int address) {
    for (const auto& b : kBindings)
        if (b.address == address)
            return b.fn;
    return nullptr;
}

} // namespace guild::sim
