#include "sim/command_apply4.h"

#include "sim/command_apply.h" // shared g_last* remap tokens
#include "sim/entity.h"

#include <cstring>
#include <cstdint>

namespace guild::sim {

// ===========================================================================
// Module-global engine state (the binary's file globals).
// ===========================================================================

u8 g_globalFlags = 0; // byte_63CC28

u8  g_loadBuf[kLoadBufCapacity];
i32 g_loadBufSize   = 0; // dword_11AA490
i32 g_loadBufCursor = 0; // dword_11AA464

u8  g_straftatTable[kStraftatSlots * kStraftatStride];
i32 g_straftatCounter = 0; // dword_632240

namespace {
CutInfoSlot g_cutInfo[kCutInfoSlots];
bool        g_standalone4 = true; // dword_764CE0 == -1
CharLifeLog g_charLog{};
} // namespace

u8* Apply4_StraftatSlot(int i) {
    if (i < 0 || i >= kStraftatSlots) return nullptr;
    return g_straftatTable + i * kStraftatStride;
}
CutInfoSlot* Apply4_CutInfoSlot(int i) {
    if (i < 0 || i >= kCutInfoSlots) return nullptr;
    return &g_cutInfo[i];
}
void Apply4_SetStandalone(bool v) { g_standalone4 = v; }
bool Apply4_Standalone() { return g_standalone4; }
const CharLifeLog& Apply4_CharLog() { return g_charLog; }

// ===========================================================================
// Modeled leaf tables + hooks.
// ===========================================================================
namespace {

// --- AddObjekt modeled pool (VIBE_GameObject_AddObjekt @0x585af4) ------------
// A flat object record large enough for the transform region the handlers write
// (through byte +58). We keep a small pool plus the (container, proto) key so a
// test can recover the record a create produced.
constexpr int kAddPoolSlots  = 64;
constexpr int kAddRecordBytes = 64;
struct AddSlot {
    bool used;
    i32  container;
    i32  proto;
    u8   bytes[kAddRecordBytes];
};
AddSlot g_addPool[kAddPoolSlots];
i32     g_addNextId = 0x40000000; // synthetic object ids (distinct from real)

u8* DefaultAddObjekt(i32 container, i32 proto, i32 /*amount*/) {
    for (auto& s : g_addPool) {
        if (!s.used) {
            s.used = true;
            s.container = container;
            s.proto = proto;
            std::memset(s.bytes, 0, sizeof(s.bytes));
            // id at dword +2 (the originals read obj+2 as the new object id).
            i32 id = g_addNextId++;
            std::memcpy(s.bytes + 2, &id, 4);
            return s.bytes;
        }
    }
    return nullptr; // pool exhausted
}

// --- transform slot modeled table (VIBE_Building_FindSlotByProt @0x5851fc) ---
constexpr int kSlotPool = 32;
struct TSlot {
    bool used;
    u8   type;
    i16  proto;
    i32  data[16]; // ints [4][5][6], floats [8][9][11][14]
};
TSlot g_slotPool[kSlotPool];

i32* DefaultSlotFind(u8 type, i16 proto) {
    for (auto& s : g_slotPool)
        if (s.used && s.type == type && s.proto == proto)
            return s.data;
    return nullptr;
}

// --- Straftat leaves --------------------------------------------------------
int DefaultStraftatFree() {
    for (int i = 0; i < kStraftatSlots; ++i) {
        i32 id;
        std::memcpy(&id, g_straftatTable + i * kStraftatStride, 4);
        if (id == 0) return i;
    }
    return -1;
}
int DefaultStraftatResolve(i32 /*id*/, i32 /*kind*/) { return 0; } // applied
int DefaultStraftatUpdate(i32, i32, i32, i32) { return 1; }        // 1 updated

// --- person / character lifecycle leaves ------------------------------------
void DefaultChangePlayerAction(i32 personId, int /*kind*/) {
    ++g_charLog.changeActionCount;
    g_charLog.lastChangeActionId = personId;
}
void DefaultScriptFinish(i32 handle) {
    ++g_charLog.scriptFinishCount;
    g_charLog.lastScriptHandle = handle;
}
void DefaultBuildingRemove(i32 /*marker*/, i32 /*arg*/) { ++g_charLog.buildingRemoveCount; }
void DefaultSetObjectParent(i32, i32, i32) { ++g_charLog.setParentCount; }
void DefaultChimneySmoke(i32 /*marker*/) { ++g_charLog.chimneyCount; }
int  DefaultIsProduction(i32 /*marker*/) { return 0; }
void DefaultCharDestroy(i32 /*charPtr*/) { ++g_charLog.charDestroyCount; }
int  DefaultCharFindAndQueue(i32 actorId, i32 tag, i32 /*a0*/, i32 /*a1*/) {
    ++g_charLog.queueActionCount;
    g_charLog.lastActorId = actorId;
    g_charLog.lastActionTag = tag;
    return 1; // actor found
}

// --- cutscene-clear modeled slot table (0x58) --------------------------------
constexpr int kCutscenePool   = 16;
constexpr int kCutsceneMaxPart = 8;
struct CutScSlot {
    bool used;
    i32  id;
    int  participants;
    i32  ids[kCutsceneMaxPart];
};
CutScSlot g_cutscenePool[kCutscenePool];
CutsceneClearSlot g_cutsceneView; // scratch for the find hook return

const CutsceneClearSlot* DefaultCutsceneFind(i32 id) {
    for (auto& s : g_cutscenePool) {
        if (s.used && s.id == id) {
            g_cutsceneView.id = s.id;
            g_cutsceneView.participants = s.participants;
            g_cutsceneView.ids = s.ids;
            return &g_cutsceneView;
        }
    }
    return nullptr;
}
void DefaultCutsceneRemove(i32 id) {
    for (auto& s : g_cutscenePool)
        if (s.used && s.id == id) { s.used = false; return; }
}

// installed hooks
AddObjektFn          g_addObjekt   = &DefaultAddObjekt;
SlotFindFn4          g_slotFind    = &DefaultSlotFind;
StraftatFreeFn       g_stFree      = &DefaultStraftatFree;
StraftatResolveFn    g_stResolve   = &DefaultStraftatResolve;
StraftatUpdateFn     g_stUpdate    = &DefaultStraftatUpdate;
ChangePlayerActionFn g_changeAct   = &DefaultChangePlayerAction;
ScriptFinishFn       g_scriptFin   = &DefaultScriptFinish;
BuildingRemoveFn4    g_bldRemove   = &DefaultBuildingRemove;
SetObjectParentFn    g_setParent   = &DefaultSetObjectParent;
ChimneySmokeFn       g_chimney     = &DefaultChimneySmoke;
IsProductionFn       g_isProd      = &DefaultIsProduction;
CharDestroyFn        g_charDestroy = &DefaultCharDestroy;
CharFindAndQueueFn   g_charQueue   = &DefaultCharFindAndQueue;
CutsceneFindFn       g_cutFind     = &DefaultCutsceneFind;
CutsceneRemoveFn     g_cutRemove   = &DefaultCutsceneRemove;

// --- ack helpers (mirror the repeated +0/+1/+6 stamps in the originals) -----
inline void AckSet(AckEntry* ack, u8 status, u8 slot, i32 seq) {
    if (ack) { ack->status = status; ack->slot = slot; ack->seq = seq; }
}

// Resolve the +0x10 entity id with the -2/-3/-4 last-created remap tokens.
i32 Remap(i32 id) {
    switch (id) {
        case -2: return g_lastObjectId;
        case -3: return g_lastSceneId;
        case -4: return g_lastTradeId;
        default: return id;
    }
}

} // namespace

u8*  Apply4_FindAddedObject(i32 container, i32 proto) {
    for (auto& s : g_addPool)
        if (s.used && s.container == container && s.proto == proto)
            return s.bytes;
    return nullptr;
}
i32* Apply4_SeedTransformSlot(u8 type, i16 proto) {
    for (auto& s : g_slotPool) {
        if (!s.used) {
            s.used = true; s.type = type; s.proto = proto;
            std::memset(s.data, 0, sizeof(s.data));
            return s.data;
        }
    }
    return nullptr;
}
bool Apply4_SeedCutsceneSlot(i32 id, const i32* ids, int count) {
    if (count > kCutsceneMaxPart) count = kCutsceneMaxPart;
    for (auto& s : g_cutscenePool) {
        if (!s.used) {
            s.used = true; s.id = id; s.participants = count;
            for (int i = 0; i < count; ++i) s.ids[i] = ids[i];
            return true;
        }
    }
    return false;
}

void SetAddObjektHook(AddObjektFn fn)             { g_addObjekt = fn ? fn : &DefaultAddObjekt; }
void SetSlotFindHook4(SlotFindFn4 fn)             { g_slotFind = fn ? fn : &DefaultSlotFind; }
void SetStraftatFreeHook(StraftatFreeFn fn)       { g_stFree = fn ? fn : &DefaultStraftatFree; }
void SetStraftatResolveHook(StraftatResolveFn fn) { g_stResolve = fn ? fn : &DefaultStraftatResolve; }
void SetStraftatUpdateHook(StraftatUpdateFn fn)   { g_stUpdate = fn ? fn : &DefaultStraftatUpdate; }
void SetChangePlayerActionHook(ChangePlayerActionFn fn) { g_changeAct = fn ? fn : &DefaultChangePlayerAction; }
void SetScriptFinishHook(ScriptFinishFn fn)       { g_scriptFin = fn ? fn : &DefaultScriptFinish; }
void SetBuildingRemoveHook4(BuildingRemoveFn4 fn) { g_bldRemove = fn ? fn : &DefaultBuildingRemove; }
void SetSetObjectParentHook(SetObjectParentFn fn) { g_setParent = fn ? fn : &DefaultSetObjectParent; }
void SetChimneySmokeHook(ChimneySmokeFn fn)       { g_chimney = fn ? fn : &DefaultChimneySmoke; }
void SetIsProductionHook(IsProductionFn fn)       { g_isProd = fn ? fn : &DefaultIsProduction; }
void SetCharDestroyHook(CharDestroyFn fn)         { g_charDestroy = fn ? fn : &DefaultCharDestroy; }
void SetCharFindAndQueueHook(CharFindAndQueueFn fn) { g_charQueue = fn ? fn : &DefaultCharFindAndQueue; }
void SetCutsceneFindHook(CutsceneFindFn fn)       { g_cutFind = fn ? fn : &DefaultCutsceneFind; }
void SetCutsceneRemoveHook(CutsceneRemoveFn fn)   { g_cutRemove = fn ? fn : &DefaultCutsceneRemove; }

void ResetApply4State() {
    g_globalFlags = 0;
    std::memset(g_loadBuf, 0, sizeof(g_loadBuf));
    g_loadBufSize = 0;
    g_loadBufCursor = 0;
    std::memset(g_straftatTable, 0, sizeof(g_straftatTable));
    g_straftatCounter = 0;
    for (auto& s : g_cutInfo) { s.playerId = -1; s.ready = 0; s.valid = 0; s.time = 0; }
    g_standalone4 = true;
    g_charLog = CharLifeLog{};
    for (auto& s : g_addPool) s.used = false;
    g_addNextId = 0x40000000;
    for (auto& s : g_slotPool) s.used = false;
    for (auto& s : g_cutscenePool) s.used = false;
    g_addObjekt = &DefaultAddObjekt; g_slotFind = &DefaultSlotFind;
    g_stFree = &DefaultStraftatFree; g_stResolve = &DefaultStraftatResolve;
    g_stUpdate = &DefaultStraftatUpdate; g_changeAct = &DefaultChangePlayerAction;
    g_scriptFin = &DefaultScriptFinish; g_bldRemove = &DefaultBuildingRemove;
    g_setParent = &DefaultSetObjectParent; g_chimney = &DefaultChimneySmoke;
    g_isProd = &DefaultIsProduction; g_charDestroy = &DefaultCharDestroy;
    g_charQueue = &DefaultCharFindAndQueue; g_cutFind = &DefaultCutsceneFind;
    g_cutRemove = &DefaultCutsceneRemove;
}

// ===========================================================================
// Little-endian helpers (the handlers read raw payload bytes by byte offset).
// ===========================================================================
namespace {
inline i32 RdI32(const CommandPacket& p, u32 off) { return static_cast<i32>(p.get32(off)); }
inline u8  RdU8 (const CommandPacket& p, u32 off) { return p.bytes[off]; }
inline i16 RdProtoHi(const CommandPacket& p, u32 dwordOff) {
    // proto = HIWORD(dword @ dwordOff)  (== (dword >> 16) & 0xFFFF, signed word)
    return static_cast<i16>(p.get32(dwordOff) >> 16);
}
} // namespace

// ===========================================================================
// Trivial / framing-ack handlers.
// ===========================================================================

// gilde.exe 0x496474 — opcode 0x01.
int ExHandleAck(CommandPacket& /*pkt*/, AckEntry* ack) {
    if (ack) ack->status = 1;
    return 0;
}

// gilde.exe 0x496484 — opcode 0x02. Literal `return 1` (no ack touch).
int ExHandleNoop(CommandPacket& /*pkt*/, AckEntry* /*ack*/) {
    return 1;
}

// gilde.exe 0x49648C — opcode 0x04. byte_63CC28 |= *(a1+16); ack=1.
int ExSetGlobalFlag(CommandPacket& pkt, AckEntry* ack) {
    g_globalFlags = static_cast<u8>(g_globalFlags | pkt.bytes[kFPayload]);
    if (ack) ack->status = 1;
    return 0;
}

// gilde.exe 0x4964A4 — opcode 0x08. Alloc cm_LoadBuf of size *(a1+16); cursor=0.
int ExLoadBufAlloc(CommandPacket& pkt, AckEntry* ack) {
    g_loadBufSize = RdI32(pkt, kFPayload); // dword_11AA490
    // The original AllocDebug's a buffer of that size; we model a fixed pool and
    // clamp. The cursor (dword_11AA464) is reset to 0.
    g_loadBufCursor = 0;                    // dword_11AA464 = 0
    if (ack) ack->status = 1;
    return 0;
}

// gilde.exe 0x4964D8 — opcode 0x09. qmemcpy(buf+cursor, a1+16, 0x80); cursor+=128.
int ExLoadBufAppend(CommandPacket& pkt, AckEntry* ack) {
    if (g_loadBufCursor >= 0 && g_loadBufCursor + 128 <= kLoadBufCapacity)
        std::memcpy(g_loadBuf + g_loadBufCursor, pkt.bytes + kFPayload, 0x80);
    g_loadBufCursor += 128;
    if (ack) ack->status = 1;
    return 0;
}

// gilde.exe 0x498A4C — opcode 0x1F. Hud-mode probe (no-op here); ack=1.
int ExAckStub(CommandPacket& /*pkt*/, AckEntry* ack) {
    if (ack) ack->status = 1;
    return 0;
}

// ===========================================================================
// Object create / transform / effect.
// ===========================================================================

// gilde.exe 0x497B8C — opcode 0x15.
int ExSetObjectTransform(CommandPacket& pkt, AckEntry* ack) {
    i32 id = Remap(RdI32(pkt, 0x10));
    pkt.put32(0x10, static_cast<u32>(id));
    i16 proto = RdProtoHi(pkt, 0x12); // HIWORD(*(a1+18))
    u8* obj = g_addObjekt(id, proto, 1);
    if (!obj)
        return 1;
    // dword_631290 = *(obj+2)
    i32 objId;
    std::memcpy(&objId, obj + 2, 4);
    g_lastTradeId = objId;
    // qmemcpy(obj+28, a1+22, 0x1C)
    std::memcpy(obj + 28, pkt.bytes + 22, 0x1C);
    // v7 = a1+22; v7 += 28 => a1+50: word -> obj+56, byte -> obj+58
    std::memcpy(obj + 56, pkt.bytes + 50, 2);
    obj[58] = pkt.bytes[52];
    AckSet(ack, 1, 3, 0);
    return 0;
}

// gilde.exe 0x499854 — opcode 0x2D.
int ExSpawnEffectObject(CommandPacket& pkt, AckEntry* ack) {
    i32 id = Remap(RdI32(pkt, 0x14)); // *(a1+20)
    pkt.put32(0x14, static_cast<u32>(id));
    u8* obj = g_addObjekt(id, 437, 1);
    if (!obj)
        return 1;
    obj[18] = 64;
    i32 objId;
    std::memcpy(&objId, obj + 2, 4);
    // *(DWORD*)(obj+28) = *(DWORD*)(a1+24)
    std::memcpy(obj + 28, pkt.bytes + 24, 4);
    // *(WORD*)(obj+32) = *(WORD*)(a1+28)
    std::memcpy(obj + 32, pkt.bytes + 28, 2);
    // *(BYTE*)(obj+34) = *(BYTE*)(a1+30), then immediately cleared to 0
    obj[34] = 0;
    g_lastTradeId = objId;
    AckSet(ack, 1, 3, 0);
    return 0;
}

// gilde.exe 0x49BC50 — opcode 0x40.
int ExAdjustObjectTransform(CommandPacket& pkt, AckEntry* ack) {
    u8  type  = RdU8(pkt, 0x10);
    i16 proto = RdProtoHi(pkt, 0x0F); // HIWORD(*(a1+15))
    i32* slot = g_slotFind(type, proto);
    if (!slot)
        return 1;
    // int deltas at packet +0x13/+0x17/+0x1B -> slot ints [4][5][6]
    slot[4] += RdI32(pkt, 0x13);
    slot[5] += RdI32(pkt, 0x17);
    slot[6] += RdI32(pkt, 0x1B);
    // float deltas at +0x1F/+0x23/+0x27/+0x2B -> slot floats [8][9][11][14]
    auto addFloat = [&](int idx, u32 off) {
        float cur, d;
        std::memcpy(&cur, &slot[idx], 4);
        std::memcpy(&d, pkt.bytes + off, 4);
        cur += d;
        std::memcpy(&slot[idx], &cur, 4);
    };
    addFloat(8,  0x1F);
    addFloat(9,  0x23);
    addFloat(11, 0x27);
    addFloat(14, 0x2B);
    if (ack) ack->status = 1;
    return 0;
}

// ===========================================================================
// Straftat (crime) table.
// ===========================================================================

// gilde.exe 0x498F44 — opcode 0x22.
int ExAddStraftat(CommandPacket& pkt, AckEntry* ack) {
    if (g_standalone4)
        ++g_straftatCounter;             // ++dword_632240
    else
        g_straftatCounter = RdI32(pkt, 0x10); // = *(a1+16)

    int slotIdx = g_stFree();
    if (slotIdx == -1)
        return -1;

    u8* slot = g_straftatTable + slotIdx * kStraftatStride;
    // qmemcpy(slot, a1+16, 0x2D)
    std::memcpy(slot, pkt.bytes + 0x10, 0x2D);
    // *(int*)slot = dword_632240  (overwrite the copied id with the allocator's)
    std::memcpy(slot, &g_straftatCounter, 4);

    // (He/Beweis evidence loop is hooked-out; the deterministic record write is
    // the slot copy above. Real game runs VIBE_He_FindFirstHandlerByFilter +
    // VIBE_Beweis_Add over the matching handlers — deferred leaves.)

    AckSet(ack, 1, 6, 0);
    if (ack) ack->seq = reinterpret_cast<intptr_t>(slot) & 0xFFFFFFFF; // +6 = slot ptr (low)
    return 0;
}

// gilde.exe 0x4991B0 — opcode 0x23.
int ExDispatchStatusResult(CommandPacket& pkt, AckEntry* ack) {
    int r = g_stResolve(RdI32(pkt, 0x10), RdI32(pkt, 0x14));
    switch (r) {
        case 1: return 1;
        case 2: return 2;
        case 3: return 3;
        default:
            if (ack) ack->status = 1;
            return 0;
    }
}

// gilde.exe 0x49B610 — opcode 0x3C.
int ExQueryObjectStatus(CommandPacket& pkt, AckEntry* ack) {
    int n = g_stUpdate(RdI32(pkt, 0x10), RdI32(pkt, 0x14), RdI32(pkt, 0x1C), RdI32(pkt, 0x18));
    u8 status = (n > 0) ? 1 : 2;
    AckSet(ack, status, 0, 0);
    return 0;
}

// ===========================================================================
// Cut-info slot table.
// ===========================================================================

// gilde.exe 0x499288 — opcode 0x26.
int ExSendCutInfo(CommandPacket& pkt, AckEntry* ack) {
    i32 player = RdI32(pkt, 0x10);
    int found = -1;
    for (int i = 0; i < kCutInfoSlots; ++i) {
        if (g_cutInfo[i].playerId == player) { found = i; break; }
    }
    if (found < 0)
        return 1; // player has no slot -> all 16 exhausted (orig: v3 >= 16)

    // LABEL_4 effect: the time column is cleared on the matching slot. (The
    // original guards the 0x35C blob copy on the dword_6315C0 cut-target match;
    // we model that gate as "matching" so the deterministic column write occurs.)
    g_cutInfo[found].time = 0; // dword_11AB00C[slot] = 0
    AckSet(ack, 1, 8, 0);
    if (ack) ack->seq = found; // +6 -> &slot (we expose the index for tests)
    return 0;
}

// ===========================================================================
// Person / character lifecycle.
// ===========================================================================
namespace {
// Read the person's "character/building ptr token" at dword index 97 (byte +388).
i32 PersonCharPtr(const Person* p) {
    i32 v;
    std::memcpy(&v, reinterpret_cast<const u8*>(p) + 388, 4);
    return v;
}
void PersonSetCharPtr(Person* p, i32 v) {
    std::memcpy(reinterpret_cast<u8*>(p) + 388, &v, 4);
}
} // namespace

// gilde.exe 0x498EAC — opcode 0x21.
int ExRemovePersonAndScript(CommandPacket& pkt, AckEntry* ack) {
    if (ack) ack->status = 2;
    Person* p = PersonFindRecordById(RdI32(pkt, 0x10));
    if (!p) {
        // original: leaves ack at 2, returns 0.
        return 0;
    }
    g_changeAct(p->id, /*kind=*/0);             // ChangePlayerAction(0,0,0,marker)
    i32 charPtr = PersonCharPtr(p);             // record[97]
    if (charPtr != 0)
        g_scriptFin(charPtr);                   // FindByHandle(charPtr+40)->Finish
    g_bldRemove(p->marker, RdI32(pkt, 0x14));   // RemoveAndCleanup(marker, *(a1+20))
    if (ack) ack->status = 1;
    return 0;
}

// gilde.exe 0x49AE60 — opcode 0x38.
int ExSetObjectParent(CommandPacket& pkt, AckEntry* ack) {
    PersonFilter f{1, RdI32(pkt, 0x10)}; // op 1 == id match (owner object)
    ObjectRec* owner = PersonQueryBegin(&f, 1);
    if (!owner)
        return 1;
    i32 childId = RdI32(pkt, 0x18);
    Person* child = PersonFindRecordById(childId);
    if (!child && childId != -1)
        return 1;
    i32 parentId = RdI32(pkt, 0x14);
    Person* newParent = PersonFindRecordById(parentId);
    if (!newParent && parentId != -1)
        return 2;
    g_setParent(owner->id,
                newParent ? newParent->marker : -1,
                child ? child->marker : -1);
    // ack +0 = 2, +1 = 0, +6 = 0  (the original leaves status at 2)
    AckSet(ack, 2, 0, 0);
    return 0;
}

// gilde.exe 0x49BCF0 — opcode 0x42.
int ExActivateObject(CommandPacket& pkt, AckEntry* ack) {
    PersonFilter f{1, RdI32(pkt, 0x10)};
    ObjectRec* obj = PersonQueryBegin(&f, 1);
    if (obj) {
        i32 bld;
        std::memcpy(&bld, reinterpret_cast<u8*>(obj) + 97, 4); // *(obj+97)
        if (bld != 0 && !g_isProd(obj->id))
            g_chimney(obj->id);
    }
    if (ack) ack->status = 1;
    return 0;
}

// gilde.exe 0x49BE14 — opcode 0x47.
int ExChangePlayerHead(CommandPacket& pkt, AckEntry* ack) {
    Person* p = PersonFindRecordById(RdI32(pkt, 0x10));
    // original logs but would deref null; we guard.
    if (p && p->marker != -1)
        g_changeAct(p->id, /*kind=head model*/ RdI32(pkt, 0x14));
    if (ack) ack->status = 1;
    return 0;
}

// gilde.exe 0x49C580 — opcode 0x4D.
int ExStopCharacterScript(CommandPacket& pkt, AckEntry* ack) {
    Person* p = PersonFindRecordById(RdI32(pkt, 0x10));
    if (!p)
        return 0; // original jumps to the shared no-target tail (return 0)
    i32 charPtr = PersonCharPtr(p);
    if (charPtr != 0)
        g_scriptFin(charPtr);
    if (ack) ack->status = 1;
    return 0;
}

// gilde.exe 0x49C5CC — opcode 0x4E.
int ExEnqueueCharacterAction(CommandPacket& pkt, AckEntry* ack) {
    i32 actorId = RdI32(pkt, 0x10);
    // tag: the original uses 0x2D when a secondary actor (byte +16 flag) is
    // present, else 0x3A. We model the flag as packet byte +0x14 != 0.
    i32 tag = pkt.bytes[0x14] ? 0x2D : 0x3A;
    int found = g_charQueue(actorId, tag, RdI32(pkt, 0x28), RdI32(pkt, 0x2C));
    if (!found)
        return 1;
    if (ack) ack->status = 1;
    return 0;
}

// gilde.exe 0x49CEEC — opcode 0x57.
int ExDestroyCharacter(CommandPacket& pkt, AckEntry* ack) {
    if (ack) ack->status = 2;
    Person* p = PersonFindRecordById(RdI32(pkt, 0x10));
    if (!p)
        return 1;
    i32 charPtr = PersonCharPtr(p);
    if (charPtr == 0)
        return 1;
    g_charDestroy(charPtr);
    PersonSetCharPtr(p, 0); // *(record+388) = 0
    if (ack) ack->status = 1;
    return 0;
}

// gilde.exe 0x49CF40 — opcode 0x58.
int ExClearObjectOccupants(CommandPacket& pkt, AckEntry* ack) {
    i32 slotId = RdI32(pkt, 0x10);
    const CutsceneClearSlot* slot = g_cutFind(slotId);
    if (!slot)
        return 1;
    for (int i = 0; i < slot->participants; ++i) {
        i32 pid = slot->ids[i];
        if (pid == -1)
            continue;
        Person* p = PersonFindRecordById(pid);
        if (!p)
            continue;
        i32 csId;
        std::memcpy(&csId, reinterpret_cast<u8*>(p) + 520, 4); // record[130]
        if (csId == slot->id) {
            i32 minus1 = -1;
            std::memcpy(reinterpret_cast<u8*>(p) + 520, &minus1, 4);
        }
    }
    g_cutRemove(slotId);
    if (ack) ack->status = 1;
    return 0;
}

// ===========================================================================
// Registry + direct apply.
// ===========================================================================
namespace {
#define APPLY4_ADAPTER(NAME) \
    void NAME##_adapter(CommandQueue&, CommandPacket& pkt, AckEntry* ack) { NAME(pkt, ack); }
APPLY4_ADAPTER(ExHandleAck)
APPLY4_ADAPTER(ExHandleNoop)
APPLY4_ADAPTER(ExSetGlobalFlag)
APPLY4_ADAPTER(ExLoadBufAlloc)
APPLY4_ADAPTER(ExLoadBufAppend)
APPLY4_ADAPTER(ExAckStub)
APPLY4_ADAPTER(ExSetObjectTransform)
APPLY4_ADAPTER(ExSpawnEffectObject)
APPLY4_ADAPTER(ExAdjustObjectTransform)
APPLY4_ADAPTER(ExAddStraftat)
APPLY4_ADAPTER(ExDispatchStatusResult)
APPLY4_ADAPTER(ExQueryObjectStatus)
APPLY4_ADAPTER(ExSendCutInfo)
APPLY4_ADAPTER(ExRemovePersonAndScript)
APPLY4_ADAPTER(ExSetObjectParent)
APPLY4_ADAPTER(ExActivateObject)
APPLY4_ADAPTER(ExChangePlayerHead)
APPLY4_ADAPTER(ExStopCharacterScript)
APPLY4_ADAPTER(ExEnqueueCharacterAction)
APPLY4_ADAPTER(ExDestroyCharacter)
APPLY4_ADAPTER(ExClearObjectOccupants)
#undef APPLY4_ADAPTER
} // namespace

void RegisterApplyHandlers4(CommandQueue& q) {
    q.set_handler(kOp4HandleAck,             &ExHandleAck_adapter);
    q.set_handler(kOp4HandleNoop,            &ExHandleNoop_adapter);
    q.set_handler(kOp4SetGlobalFlag,         &ExSetGlobalFlag_adapter);
    q.set_handler(kOp4LoadBufAlloc,          &ExLoadBufAlloc_adapter);
    q.set_handler(kOp4LoadBufAppend,         &ExLoadBufAppend_adapter);
    q.set_handler(kOp4AckStub,               &ExAckStub_adapter);
    q.set_handler(kOp4SetObjectTransform,    &ExSetObjectTransform_adapter);
    q.set_handler(kOp4SpawnEffectObject,     &ExSpawnEffectObject_adapter);
    q.set_handler(kOp4AdjustObjectTransform, &ExAdjustObjectTransform_adapter);
    q.set_handler(kOp4AddStraftat,           &ExAddStraftat_adapter);
    q.set_handler(kOp4DispatchStatusResult,  &ExDispatchStatusResult_adapter);
    q.set_handler(kOp4QueryObjectStatus,     &ExQueryObjectStatus_adapter);
    q.set_handler(kOp4SendCutInfo,           &ExSendCutInfo_adapter);
    q.set_handler(kOp4RemovePersonAndScript, &ExRemovePersonAndScript_adapter);
    q.set_handler(kOp4SetObjectParent,       &ExSetObjectParent_adapter);
    q.set_handler(kOp4ActivateObject,        &ExActivateObject_adapter);
    q.set_handler(kOp4ChangePlayerHead,      &ExChangePlayerHead_adapter);
    q.set_handler(kOp4StopCharacterScript,   &ExStopCharacterScript_adapter);
    q.set_handler(kOp4EnqueueCharacterAction,&ExEnqueueCharacterAction_adapter);
    q.set_handler(kOp4DestroyCharacter,      &ExDestroyCharacter_adapter);
    q.set_handler(kOp4ClearObjectOccupants,  &ExClearObjectOccupants_adapter);
}

int ApplyPacket4(CommandPacket& pkt, AckEntry* ack) {
    switch (pkt.opcode()) {
        case kOp4HandleAck:             return ExHandleAck(pkt, ack);
        case kOp4HandleNoop:            return ExHandleNoop(pkt, ack);
        case kOp4SetGlobalFlag:         return ExSetGlobalFlag(pkt, ack);
        case kOp4LoadBufAlloc:          return ExLoadBufAlloc(pkt, ack);
        case kOp4LoadBufAppend:         return ExLoadBufAppend(pkt, ack);
        case kOp4AckStub:               return ExAckStub(pkt, ack);
        case kOp4SetObjectTransform:    return ExSetObjectTransform(pkt, ack);
        case kOp4SpawnEffectObject:     return ExSpawnEffectObject(pkt, ack);
        case kOp4AdjustObjectTransform: return ExAdjustObjectTransform(pkt, ack);
        case kOp4AddStraftat:           return ExAddStraftat(pkt, ack);
        case kOp4DispatchStatusResult:  return ExDispatchStatusResult(pkt, ack);
        case kOp4QueryObjectStatus:     return ExQueryObjectStatus(pkt, ack);
        case kOp4SendCutInfo:           return ExSendCutInfo(pkt, ack);
        case kOp4RemovePersonAndScript: return ExRemovePersonAndScript(pkt, ack);
        case kOp4SetObjectParent:       return ExSetObjectParent(pkt, ack);
        case kOp4ActivateObject:        return ExActivateObject(pkt, ack);
        case kOp4ChangePlayerHead:      return ExChangePlayerHead(pkt, ack);
        case kOp4StopCharacterScript:   return ExStopCharacterScript(pkt, ack);
        case kOp4EnqueueCharacterAction:return ExEnqueueCharacterAction(pkt, ack);
        case kOp4DestroyCharacter:      return ExDestroyCharacter(pkt, ack);
        case kOp4ClearObjectOccupants:  return ExClearObjectOccupants(pkt, ack);
        default:                        return -1;
    }
}

} // namespace guild::sim
