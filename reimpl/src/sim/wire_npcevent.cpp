// See wire_npcevent.h. Glue only: binds the NpcEvent step bridges to the real
// reconstructed leaves (entity arrays, command builders, packet ACK table, the
// reaper full reconstructions). No module logic lives here.
#include "sim/wire_npcevent.h"

#include "sim/npcevent_steps.h"        // NpcEventHooks / SetNpcEventHooks
#include "sim/npcevent_steps2.h"       // NpcEventHooks2 / SetNpcEventHooks2
#include "sim/npcevent_reaper_full.h"  // ReaperApproachTarget / ...Move / ...CachePose / ...UpdateSound
#include "sim/entity.h"                // PersonFindRecordById, BuildingFindById, PersonQueryBegin
#include "sim/types.h"                 // Person, ObjectRec (record layouts / strides)
#include "sim/command_codec.h"         // QueueRequestArgs25, QueueRequest16, EnqueueObjectInteraction
#include "sim/command_builders.h"      // QueueRequestPair33/Single49/NamedObject53
#include "sim/command_builders2.h"     // QueueRequestQuad52, QueueRequestFlag55
#include "sim/command_builders3.h"     // RequestBuildOp87

#include <cstring>
#include <cstdint>
#include <vector>

namespace guild::sim {

namespace {

// ===========================================================================
// Shared real command queue the wired emit hooks build onto (the engine's
// global request ring; here a self-owned instance Init()'d once on first use,
// matching VIBE_Command_QueueInitAndSync). The reaper plague step / politician /
// dark-corner steps re-arm against packet handles, so packetStatus/packetSeq
// must read this same ring.
// ===========================================================================
CommandQueue& Queue() {
    static CommandQueue q;
    static bool inited = false;
    if (!inited) { q.Init(); inited = true; }
    return q;
}

// ===========================================================================
// Opaque-handle token registry (LP64 reconciliation). The 32-bit original kept a
// record pointer in eax; the hook prototypes carry it as an i32. We hand the step
// machine a stable small token and resolve it back here. Token 0 == "absent"
// (matches the original null-pointer convention every call site tests for).
// ===========================================================================
std::vector<void*>& TokenTable() {
    static std::vector<void*> t = []{ std::vector<void*> v; v.push_back(nullptr); return v; }();
    return t;
}
i32 TokenFor(void* p) {
    if (!p) return 0;
    auto& t = TokenTable();
    for (std::size_t i = 1; i < t.size(); ++i)
        if (t[i] == p) return static_cast<i32>(i);
    t.push_back(p);
    return static_cast<i32>(t.size() - 1);
}
void* PtrFor(i32 token) {
    auto& t = TokenTable();
    if (token <= 0 || static_cast<std::size_t>(token) >= t.size()) return nullptr;
    return t[token];
}

// Byte-faithful dword read off a record at a raw offset, bounds-guarded against
// the record's declared size (the original read unaligned dwords by offset).
template <typename Rec>
i32 ReadRecField(void* rec, int byteOffset) {
    if (!rec || byteOffset < 0 ||
        static_cast<std::size_t>(byteOffset) + sizeof(i32) > sizeof(Rec))
        return 0;
    i32 v;
    std::memcpy(&v, reinterpret_cast<const std::uint8_t*>(rec) + byteOffset, sizeof(i32));
    return v;
}

// ===========================================================================
// NpcEventHooks (steps) — real entity / person resolution.
// ===========================================================================

// VIBE_GameObject_ResolveEntityById @0x583b44 — the steps only read object id (+1)
// and owner word (+39) off the handle, and gate on the resolved entity being
// non-null; the object/building array (g_objects) is the faithful source for an
// object id. Returns a token (0 == absent).
i32 EvResolveEntity(i32 id) {
    return TokenFor(BuildingFindById(id));
}

// Field read off a resolved entity handle (ObjectRec). The +1000 "class eligible"
// probe used by the reaper target scan is OUTSIDE the record — out-of-range reads
// return 0 (faithful "no data" / the engine probe is an unreconstructed leaf).
i32 EvEntityField(i32 entityHandle, int byteOffset) {
    return ReadRecField<ObjectRec>(PtrFor(entityHandle), byteOffset);
}

// VIBE_Person_FindRecordById @0x58bc6c.
i32 EvFindPerson(i32 id) {
    return TokenFor(PersonFindRecordById(id));
}

// Field read off a Person handle (raw record offset).
i32 EvPersonField(i32 personHandle, int byteOffset) {
    return ReadRecField<Person>(PtrFor(personHandle), byteOffset);
}

// VIBE_Building_FindById @0x587b20.
i32 EvFindBuilding(i32 id) {
    return TokenFor(BuildingFindById(id));
}

// --- command emit leaves -> real builders on the shared queue ---------------
i32 EvQueueRequestPair33(i32 id, int v)            { return QueueRequestPair33(Queue(), id, v); }
i32 EvQueueRequestSingle49(i32 id)                 { return QueueRequestSingle49(Queue(), id); }
i32 EvQueueRequestArgs25(i32 id, int a, int b, int c, int d) {
    return QueueRequestArgs25(Queue(), id, a, b, c, d);
}
i32 EvQueueRequestQuad52(i32 a, i32 b, int c, int d) { return QueueRequestQuad52(Queue(), a, b, c, d); }
i32 EvQueueRequestNamedObject53(i32 a, i32 b, int c, int d, int e, const char* tag) {
    // op53 wire payload: a1=a, a2=b, obj-name string (c is the director "0" slot,
    // no string here), a4=d, a5(byte)=e, name=tag.
    return QueueRequestNamedObject53(Queue(), a, b, /*obj=*/nullptr, d,
                                     static_cast<i8>(e), tag);
}
i32 EvRequestBuildOp87(i32 id)                     { return RequestBuildOp87(Queue(), id); }
i32 EvQueueGestureFlag55(i32 id, int v) {
    // op55: a1=id, a2(byte)=v, name=null.
    return QueueRequestFlag55(Queue(), id, static_cast<i8>(v), /*name=*/nullptr);
}
i32 EvEnqueueObjectInteraction(int a, int b, int c, int d, int e, int f, int g, int h) {
    // opcode 11: a1(byte)=a, a2=b, a3(word)=c, a4=d, a5=e, a6/a7/a8(byte)=f/g/h.
    return EnqueueObjectInteraction(Queue(), static_cast<u8>(a), b,
                                    static_cast<i16>(c), d, e,
                                    static_cast<u8>(f), static_cast<u8>(g),
                                    static_cast<u8>(h));
}

// --- packet status / sequence (re-arm gating) -------------------------------
i32 EvPacketStatus(i32 handle) {
    return Queue().GetPacketStatusById(static_cast<u32>(handle));
}
i32 EvPacketSeq(i32 handle) {
    return Queue().GetPacketSeqById(static_cast<u32>(handle));
}

// ===========================================================================
// NpcEventHooks2 (steps2) — bindable subset.
// ===========================================================================

// VIBE_Person_QueryBegin @0x586c20 — the steps2 callers use QueryBegin(&clock,1,1,id)
// i.e. an id-match query; faithful to filter op 1 (id == value). Returns a token.
i32 Ev2QueryBegin(i32 id) {
    PersonFilter f{1, id};
    return TokenFor(PersonQueryBegin(&f, 1));
}
i32 Ev2FindPerson(i32 id)                          { return TokenFor(PersonFindRecordById(id)); }
i32 Ev2PersonField(i32 personHandle, int off)      { return ReadRecField<Person>(PtrFor(personHandle), off); }
i32 Ev2ObjectField(i32 objectHandle, int off)      { return ReadRecField<ObjectRec>(PtrFor(objectHandle), off); }
i32 Ev2QueueRequestSingle49(i32 id)                { return QueueRequestSingle49(Queue(), id); }
i32 Ev2QueueRequestNamedObject53(i32 a, i32 b, int c, int d, int e, const char* tag) {
    return QueueRequestNamedObject53(Queue(), a, b, /*obj=*/nullptr, d,
                                     static_cast<i8>(e), tag);
}
i32 Ev2QueueRequestArgs25(i32 id, int a, int b, int c, int d) {
    return QueueRequestArgs25(Queue(), id, a, b, c, d);
}
i32 Ev2QueueRequest16(i32 a, i32 b, int amount, int d) {
    return QueueRequest16(Queue(), a, b, amount, static_cast<u8>(d));
}
i32 Ev2PacketStatus(i32 handle) { return Queue().GetPacketStatusById(static_cast<u32>(handle)); }
i32 Ev2PacketSeq(i32 handle)    { return Queue().GetPacketSeqById(static_cast<u32>(handle)); }

} // namespace

void InstallRealNpcEventWiring() {
    Queue();  // ensure the shared ring exists before any emit/status read.

    static NpcEventHooks hooks{};   // process-lifetime table the global ptr references.

    // --- entity / person / building resolution ---
    hooks.resolveEntity = &EvResolveEntity;
    hooks.entityField   = &EvEntityField;
    hooks.findPerson    = &EvFindPerson;
    hooks.personField   = &EvPersonField;
    hooks.findBuilding  = &EvFindBuilding;
    // hooks.isNearDoor  : INERT (VIBE_Object_IsNearDoor — unreconstructed leaf).

    // --- command emit leaves ---
    hooks.queueRequestPair33        = &EvQueueRequestPair33;
    hooks.queueRequestSingle49      = &EvQueueRequestSingle49;
    hooks.queueRequestArgs25        = &EvQueueRequestArgs25;
    hooks.queueRequestQuad52        = &EvQueueRequestQuad52;
    hooks.queueRequestNamedObject53 = &EvQueueRequestNamedObject53;
    hooks.requestBuildOp87          = &EvRequestBuildOp87;
    // hooks.requestBuildOp77        : INERT (MeisterAi_RegisterApEvent leaf).
    hooks.enqueueObjectInteraction  = &EvEnqueueObjectInteraction;
    hooks.queueGestureFlag55        = &EvQueueGestureFlag55;

    // --- packet status / sequence ---
    hooks.packetStatus = &EvPacketStatus;
    hooks.packetSeq    = &EvPacketSeq;

    // hooks.loadDemandSnapshot : INERT (VIBE_Economy_LoadDemandSnapshot leaf).

    // --- Reaper render/transform/sound leaves (SUPERSEDES InstallRealReaperWiring) ---
    hooks.reaperApproach    = &ReaperApproachTarget;     // 0x4d8c34
    hooks.reaperMove        = &ReaperMoveTowardTarget;    // 0x4d8f74
    hooks.reaperCachePose   = &ReaperCacheTargetPose;     // 0x4d92a4
    hooks.reaperUpdateSound = &ReaperUpdateSoundPos;      // 0x4d9440
    // hooks.reaperDetach / nodeFieldGet / nodeFieldSet / cutscenePause /
    // cutsceneResume : INERT (render-node teardown / cutscene leaves — the FULL
    // reaper bodies route their own engine leaves through ReaperFullHooks).

    // hooks.eventPanelCreate / eventPanelDestroy / dialogResult : INERT
    // (master-exam / talent-up dialog GUI leaves — unreconstructed).

    SetNpcEventHooks(&hooks);
}

void InstallRealNpcEvent2Wiring() {
    Queue();

    static NpcEventHooks2 hooks{};

    hooks.queryBegin   = &Ev2QueryBegin;
    hooks.findPerson   = &Ev2FindPerson;
    hooks.personField  = &Ev2PersonField;
    hooks.objectField  = &Ev2ObjectField;
    // buildingUpgradeLevel / computeRoomWorth / sumCurrencyHeld /
    // patrolBrawlEligible : INERT (building-value / currency-sum / brawl-scan leaves).
    // changePlayerAction : INERT (VIBE_Character_ChangePlayerAction leaf).
    hooks.queueRequestSingle49      = &Ev2QueueRequestSingle49;
    hooks.queueRequestNamedObject53 = &Ev2QueueRequestNamedObject53;
    hooks.queueRequestArgs25        = &Ev2QueueRequestArgs25;
    hooks.queueRequest16            = &Ev2QueueRequest16;
    // queueRequest39 / applyTitleDelta : INERT (fight/gather packet draft, title
    // delta apply — drafted from host arrays / cross-cluster).
    hooks.packetStatus = &Ev2PacketStatus;
    hooks.packetSeq    = &Ev2PacketSeq;
    // cutsceneActive / sendEntityMessage / sendQuickjumpMessage / compareAwardTime /
    // eventPanelCreate / eventPanelDestroy / renderAwardText / playAwardVoice /
    // awardDialogResult / awardActivePlayerGate : INERT (cutscene-slot / messaging /
    // award-dialog GUI+voice leaves — unreconstructed).

    SetNpcEventHooks2(&hooks);
}

CommandQueue* NpcEventCommandQueue() { return &Queue(); }

} // namespace guild::sim
