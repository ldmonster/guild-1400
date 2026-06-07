#include "sim/command_apply.h"

#include "sim/entity.h"

#include <cstring>

namespace guild::sim {

// ===========================================================================
// Global apply state (the binary's file globals).
// ===========================================================================

i32 g_lastObjectId = -1; // dword_631288
i32 g_lastSceneId  = -1; // dword_63128C
i32 g_lastTradeId  = -1; // dword_631290

i32 g_idPairA[kIdPairSlots]; // dword_11C2160 column A
i32 g_idPairB[kIdPairSlots]; // dword_11C2164 column B

void ResetIdPairTable() {
    for (int i = 0; i < kIdPairSlots; ++i) {
        g_idPairA[i] = -1;
        g_idPairB[i] = -1;
    }
}

// ===========================================================================
// Mockable leaf hooks (deferred render/cutscene/AI/Office/Beweis leaves).
// ===========================================================================

namespace {

// Default Beweis allocator: faithful enough find-or-append into the id-pair
// table (the real VIBE_Beweis_FindOrAllocSlot @0x4c347c does the same scan with
// extra evidence-record bookkeeping that is out of this module's scope).
int DefaultBeweisAlloc(i32 /*kind*/, i32 id) {
    int firstFree = -1;
    for (int i = 0; i < kIdPairSlots; ++i) {
        if (g_idPairB[i] == id)
            return i;
        if (firstFree < 0 && g_idPairA[i] == -1 && g_idPairB[i] == -1)
            firstFree = i;
    }
    return firstFree;
}

int DefaultTurnControl() { return 1; }          // accept
void DefaultCityAck() {}                          // no-op

BeweisAllocFn g_beweisAlloc = &DefaultBeweisAlloc;
TurnControlFn g_turnControl = &DefaultTurnControl;
CityAckFn     g_cityAck     = &DefaultCityAck;

// --- ack helpers (mirror the repeated +0/+1/+6 stamps in the originals) -----
inline void AckBegin(AckEntry* ack) {
    if (ack) { ack->status = 2; ack->slot = 0; ack->seq = 0; }
}
inline void AckOk(AckEntry* ack) {
    if (ack) ack->status = 1;
}

// Resolve the +0x10 entity id, applying the -2/-3/-4 last-created remap tokens,
// and write the resolved id back into the packet (the originals do this so a
// downstream group command sees the concrete id). Mirrors the switch in the
// field-patch handlers.
i32 RemapEntityId(CommandPacket& pkt) {
    i32 id = static_cast<i32>(pkt.get32(kAfEntityId));
    switch (id) {
        case -2: id = g_lastObjectId; break;
        case -3: id = g_lastSceneId;  break;
        case -4: id = g_lastTradeId;  break;
        default: break;
    }
    pkt.put32(kAfEntityId, static_cast<u32>(id));
    return id;
}

} // namespace

void SetBeweisAllocHook(BeweisAllocFn fn) { g_beweisAlloc = fn ? fn : &DefaultBeweisAlloc; }
void SetTurnControlHook(TurnControlFn fn) { g_turnControl = fn ? fn : &DefaultTurnControl; }
void SetCityAckHook(CityAckFn fn)         { g_cityAck     = fn ? fn : &DefaultCityAck; }

// ===========================================================================
// Field-patch handlers — receive side of the delta encoder.
// ===========================================================================

// Resolve an entity by id across the three arrays, returning its byte base, and
// the priority order the given handler uses. order: 0 == {object,scene,person},
// 1 == {person,scene,object}. Returns null on miss.
static u8* ResolveForPatch(i32 id, int order) {
    ObjectRec* obj = nullptr;
    SceneNode* scene = nullptr;
    Person* person = nullptr;
    if (!GameObjectResolveEntityById(&obj, &scene, id, &person))
        return nullptr;
    if (order == 0) {
        if (obj)    return reinterpret_cast<u8*>(obj);
        if (scene)  return reinterpret_cast<u8*>(scene);
        return reinterpret_cast<u8*>(person);
    } else {
        if (person) return reinterpret_cast<u8*>(person);
        if (scene)  return reinterpret_cast<u8*>(scene);
        return reinterpret_cast<u8*>(obj);
    }
}

// gilde.exe 0x497c18 — VIBE_Command_ExPatchObjectFieldsAdd (opcode 0x16).
int ExPatchObjectFieldsAdd(CommandPacket& pkt, AckEntry* /*ack*/) {
    i32 id = RemapEntityId(pkt);
    u8* base = ResolveForPatch(id, /*order=*/0); // {object, scene, person}
    if (!base)
        return 1;

    const u8* p = pkt.bytes + kAfRecords;     // a1 + 21
    u8 nFields = pkt.bytes[kAfFieldCount];    // *(a1 + 20)
    for (u8 f = 0; f < nFields; ++f) {
        u8  width  = p[0];
        u8  count  = p[1];
        u16 off    = static_cast<u16>(p[2] | (p[3] << 8));
        const u8* vals = p + 4;
        u8* dst = base + off;
        if (width == 1) {
            for (u32 i = 0; i < count; ++i)
                dst[i] = static_cast<u8>(dst[i] + vals[i]);
        } else if (width == 2) {
            for (u32 i = 0; i < count; ++i) {
                u16 ov = static_cast<u16>(dst[2 * i] | (dst[2 * i + 1] << 8));
                u16 dv = static_cast<u16>(vals[2 * i] | (vals[2 * i + 1] << 8));
                u16 nv = static_cast<u16>(ov + dv);
                dst[2 * i]     = static_cast<u8>(nv);
                dst[2 * i + 1] = static_cast<u8>(nv >> 8);
            }
        } else if (width == 4) {
            for (u32 i = 0; i < count; ++i) {
                u32 ov = static_cast<u32>(dst[4 * i]) | (static_cast<u32>(dst[4 * i + 1]) << 8)
                       | (static_cast<u32>(dst[4 * i + 2]) << 16) | (static_cast<u32>(dst[4 * i + 3]) << 24);
                u32 dv = static_cast<u32>(vals[4 * i]) | (static_cast<u32>(vals[4 * i + 1]) << 8)
                       | (static_cast<u32>(vals[4 * i + 2]) << 16) | (static_cast<u32>(vals[4 * i + 3]) << 24);
                u32 nv = ov + dv;
                dst[4 * i]     = static_cast<u8>(nv);
                dst[4 * i + 1] = static_cast<u8>(nv >> 8);
                dst[4 * i + 2] = static_cast<u8>(nv >> 16);
                dst[4 * i + 3] = static_cast<u8>(nv >> 24);
            }
        }
        // The original advances by stride*count+4 for every record, even when
        // the width is unrecognized (it just skips the body) — match that.
        p += static_cast<u32>(width) * count + 4;
    }
    // ack stamped by the dispatcher adapter; original sets *a2 = 1 here.
    return 0;
}

// gilde.exe 0x497da4 — VIBE_Command_ExWriteObjectFields (opcode 0x17).
int ExWriteObjectFields(CommandPacket& pkt, AckEntry* /*ack*/) {
    i32 id = RemapEntityId(pkt);
    u8* base = ResolveForPatch(id, /*order=*/0); // {object, scene, person}
    if (!base)
        return 1;

    const u8* p = pkt.bytes + kAfRecords;
    u8 nFields = pkt.bytes[kAfFieldCount];
    for (u8 f = 0; f < nFields; ++f) {
        u8  width  = p[0];
        u8  count  = p[1];
        u16 off    = static_cast<u16>(p[2] | (p[3] << 8));
        const u8* vals = p + 4;
        u8* dst = base + off;
        // Absolute write (memcpy-equivalent for widths 1/2/4).
        if (width == 1 || width == 2 || width == 4)
            std::memcpy(dst, vals, static_cast<u32>(width) * count);
        p += static_cast<u32>(width) * count + 4;
    }
    return 0;
}

// gilde.exe 0x497ed0 — VIBE_Command_ExApplyNeedDeltas (opcode 0x18). Person.
int ExApplyNeedDeltas(CommandPacket& pkt, AckEntry* /*ack*/) {
    i32 id = RemapEntityId(pkt);
    // The original passes (0, 0, id, &person): person column only.
    Person* person = nullptr;
    if (!GameObjectResolveEntityById(nullptr, nullptr, id, &person) || !person)
        return 1;
    u8* base = reinterpret_cast<u8*>(person);

    const u8* p = pkt.bytes + kAfRecords;      // a1 + 21
    u8 nEntries = pkt.bytes[kAfFieldCount];    // *(a1 + 20)
    for (u8 e = 0; e < nEntries; ++e) {
        u8 statId = p[0];
        float add;
        std::memcpy(&add, p + 1, 4);
        p += 5;

        // entity + 144 + 12*statId  (decompiler: &v14[6*statId+72], v14 __int16*).
        float* slot = reinterpret_cast<float*>(base + 144 + 12 * statId);
        float v = *slot + add;
        // clamp to [0, 1000]
        if (v < 0.0f || v < 1000.0f) {
            if (v < 0.0f) v = 0.0f;
        } else {
            v = 1000.0f;
        }
        *slot = v;
    }
    return 0;
}

// gilde.exe 0x498024 — VIBE_Command_ExPatchObjectBitfield (opcode 0x19).
int ExPatchObjectBitfield(CommandPacket& pkt, AckEntry* /*ack*/) {
    i32 id = RemapEntityId(pkt);
    u8* base = ResolveForPatch(id, /*order=*/0); // {object, scene, person}
    if (!base)
        return 1;

    u16 off   = static_cast<u16>(pkt.get32(0x14)); // a1[5]
    u32 width = pkt.get32(0x18);                    // a1[6]
    u8* dst = base + off;
    if (width == 1) {
        u8 setBits  = pkt.bytes[0x1C]; // a1+28
        u8 maskBits = pkt.bytes[0x20]; // a1+32
        u8 cur = static_cast<u8>(~maskBits & *dst);
        *dst = static_cast<u8>(setBits | cur);
    } else if (width == 2) {
        u16 setBits  = static_cast<u16>(pkt.get16(0x1C)); // (_WORD*)a1+14
        u16 maskBits = static_cast<u16>(pkt.get16(0x20)); // (_WORD*)a1+16
        u16 v = static_cast<u16>(dst[0] | (dst[1] << 8));
        u16 cur = static_cast<u16>(~maskBits & v);
        u16 nv = static_cast<u16>(setBits | cur);
        dst[0] = static_cast<u8>(nv);
        dst[1] = static_cast<u8>(nv >> 8);
    } else if (width == 4) {
        u32 setBits  = pkt.get32(0x1C); // a1[7]
        u32 maskBits = pkt.get32(0x20); // a1[8]
        u32 v = static_cast<u32>(dst[0]) | (static_cast<u32>(dst[1]) << 8)
              | (static_cast<u32>(dst[2]) << 16) | (static_cast<u32>(dst[3]) << 24);
        u32 cur = ~maskBits & v;
        u32 nv = setBits | cur;
        dst[0] = static_cast<u8>(nv);
        dst[1] = static_cast<u8>(nv >> 8);
        dst[2] = static_cast<u8>(nv >> 16);
        dst[3] = static_cast<u8>(nv >> 24);
    }
    return 0;
}

// gilde.exe 0x498104 — VIBE_Command_ExAddObjectFloatField (opcode 0x1A).
int ExAddObjectFloatField(CommandPacket& pkt, AckEntry* /*ack*/) {
    i32 id = RemapEntityId(pkt);
    u8* base = ResolveForPatch(id, /*order=*/1); // {person, scene, object}
    if (!base)
        return 1;

    u32 off = pkt.get32(0x14);          // a1 + 20
    float add;
    std::memcpy(&add, pkt.bytes + 0x18, 4); // a1 + 24
    float cur;
    std::memcpy(&cur, base + off, 4);
    cur += add;
    std::memcpy(base + off, &cur, 4);
    return 0;
}

// ===========================================================================
// Id-pair (evidence) table handlers.
// ===========================================================================

// gilde.exe 0x4991ec — VIBE_Command_ExRegisterIdPair (opcode 0x24).
int ExRegisterIdPair(CommandPacket& pkt, AckEntry* ack) {
    i32 id   = static_cast<i32>(pkt.get32(0x10)); // *(a1+16)
    i32 kind = static_cast<i32>(pkt.get32(0x14)); // *(a1+20)
    int slot = g_beweisAlloc(kind, id);
    if (slot > -1) {
        g_idPairA[slot] = kind; // dword_11C2160[2*slot]
        g_idPairB[slot] = id;   // dword_11C2164[2*slot]
    }
    AckOk(ack); // original sets *a2 = 1 regardless of slot result
    return 0;
}

// gilde.exe 0x499230 — VIBE_Command_ExUnregisterIdPair (opcode 0x25).
int ExUnregisterIdPair(CommandPacket& pkt, AckEntry* ack) {
    i32 a = static_cast<i32>(pkt.get32(0x14)); // *(a1+20) matches column A
    i32 b = static_cast<i32>(pkt.get32(0x10)); // *(a1+16) matches column B
    for (int i = 0; i < kIdPairSlots; ++i) {
        if (g_idPairA[i] == a && g_idPairB[i] == b) {
            g_idPairA[i] = -1;
            g_idPairB[i] = -1;
            AckOk(ack);
            return 0;
        }
    }
    return 1; // not found
}

// ===========================================================================
// Person scalar mutation handlers.
// ===========================================================================

// gilde.exe 0x49d080 — VIBE_Command_ExAdjustObjectCounter (opcode 0x5A).
int ExAdjustObjectCounter(CommandPacket& pkt, AckEntry* ack) {
    AckBegin(ack);
    Person* person = PersonFindRecordById(static_cast<i32>(pkt.get32(0x10)));
    if (!person)
        return 1;
    u8* base = reinterpret_cast<u8*>(person);
    i32 cur;
    std::memcpy(&cur, base + 0x194, 4);
    i32 v = cur + static_cast<i32>(pkt.get32(0x14)); // + *(a1+20)
    if (v > 49)      v = 50;
    else if (v < 0)  v = 0;
    std::memcpy(base + 0x194, &v, 4);
    AckOk(ack);
    return 0;
}

// gilde.exe 0x49d0e4 — VIBE_Command_ExAdjustCharacterReputation (opcode 0x5B).
int ExAdjustCharacterReputation(CommandPacket& pkt, AckEntry* ack) {
    AckBegin(ack);
    Person* person = PersonFindRecordById(static_cast<i32>(pkt.get32(0x10)));
    if (!person)
        return 1;
    u8* base = reinterpret_cast<u8*>(person);
    // v8 = a1[5] + (u8)person[+0x1B1]. The binary clamps with:
    //   if (v8 > 254) LOBYTE(v8) = -1;  else if (v8 < 0) LOBYTE(v8) = 0;
    // i.e. > 254 stores 255 (0xFF), < 0 stores 0, else the value. A trailing
    // `(u8)v8 ? v8 : 0` idiom is a no-op for the stored byte.
    int v = static_cast<int>(pkt.get32(0x14)) + base[0x1B1];
    u8 stored;
    if (v > 254)      stored = 0xFF;
    else if (v < 0)   stored = 0;
    else              stored = static_cast<u8>(v);
    base[0x1B1] = stored;
    // HUD refresh + Office holdings clear are deferred leaves (see report); they
    // do not mutate this record. Skipped here.
    AckOk(ack);
    return 0;
}

// gilde.exe 0x49d1fc — VIBE_Command_ExSetObjectFillLevel (opcode 0x5D).
int ExSetObjectFillLevel(CommandPacket& pkt, AckEntry* ack) {
    AckBegin(ack);
    Person* person = PersonFindRecordById(static_cast<i32>(pkt.get32(0x10)));
    if (!person)
        return 1;
    u8* base = reinterpret_cast<u8*>(person);
    u32 index = pkt.get32(0x14); // *(v3+20)
    if (index > 4)
        return 1;
    // v6 = person[0x80 + index] + *(a1+24); clamp to [0,252] in float, store byte.
    int v6 = base[0x80 + index] + static_cast<int>(pkt.get32(0x18));
    float fv;
    if (static_cast<double>(v6) < 252.0 && static_cast<double>(v6) <= 0.0)
        fv = 0.0f;
    else if (static_cast<double>(v6) >= 252.0)
        fv = 252.0f;
    else
        fv = static_cast<float>(v6);
    base[0x80 + index] = static_cast<u8>(static_cast<int>(fv));
    AckOk(ack);
    return 0;
}

// ===========================================================================
// Delegating handlers (self-contained modulo their deferred leaf).
// ===========================================================================

// gilde.exe 0x49cec0 — VIBE_Command_ExEndTurn (opcode 0x56).
int ExEndTurn(CommandPacket& /*pkt*/, AckEntry* ack) {
    AckBegin(ack);
    if (!g_turnControl()) {
        // The original JUMPOUTs to a shared tail that leaves status==2 (pending).
        return 0;
    }
    AckOk(ack);
    return 0;
}

// gilde.exe 0x49bcd0 — VIBE_Command_ExAck (opcode 0x41).
int ExAck(CommandPacket& /*pkt*/, AckEntry* ack) {
    g_cityAck();
    AckOk(ack);
    return 0;
}

// ===========================================================================
// Dispatch wiring.
// ===========================================================================

namespace {
// Thin adapters dropping the CommandQueue& the queue passes to a Handler.
#define APPLY_ADAPTER(NAME) \
    void NAME##_adapter(CommandQueue&, CommandPacket& pkt, AckEntry* ack) { NAME(pkt, ack); }
APPLY_ADAPTER(ExRegisterIdPair)
APPLY_ADAPTER(ExUnregisterIdPair)
APPLY_ADAPTER(ExAdjustObjectCounter)
APPLY_ADAPTER(ExAdjustCharacterReputation)
APPLY_ADAPTER(ExSetObjectFillLevel)
APPLY_ADAPTER(ExEndTurn)
APPLY_ADAPTER(ExAck)
#undef APPLY_ADAPTER

// For the field-patch family the original sets *ack = 1 on success at the very
// end (the adapters above swallow that since the free functions take the ack but
// those four do not stamp it). Provide a success-stamping wrapper for them.
void StampOk(CommandQueue&, CommandPacket& pkt, AckEntry* ack,
             int (*fn)(CommandPacket&, AckEntry*)) {
    int r = fn(pkt, ack);
    if (r == 0) { if (ack) ack->status = 1; }
}
void ExPatchObjectFieldsAdd_ok(CommandQueue& q, CommandPacket& p, AckEntry* a) { StampOk(q, p, a, &ExPatchObjectFieldsAdd); }
void ExWriteObjectFields_ok(CommandQueue& q, CommandPacket& p, AckEntry* a)    { StampOk(q, p, a, &ExWriteObjectFields); }
void ExApplyNeedDeltas_ok(CommandQueue& q, CommandPacket& p, AckEntry* a)      { StampOk(q, p, a, &ExApplyNeedDeltas); }
void ExPatchObjectBitfield_ok(CommandQueue& q, CommandPacket& p, AckEntry* a)  { StampOk(q, p, a, &ExPatchObjectBitfield); }
void ExAddObjectFloatField_ok(CommandQueue& q, CommandPacket& p, AckEntry* a)  { StampOk(q, p, a, &ExAddObjectFloatField); }
} // namespace

void RegisterApplyHandlers(CommandQueue& q) {
    q.set_handler(kOpPatchObjectFieldsAdd,      &ExPatchObjectFieldsAdd_ok);
    q.set_handler(kOpWriteObjectFields,         &ExWriteObjectFields_ok);
    q.set_handler(kOpApplyNeedDeltas,           &ExApplyNeedDeltas_ok);
    q.set_handler(kOpPatchObjectBitfield,       &ExPatchObjectBitfield_ok);
    q.set_handler(kOpAddObjectFloatField,       &ExAddObjectFloatField_ok);
    q.set_handler(kOpRegisterIdPair,            &ExRegisterIdPair_adapter);
    q.set_handler(kOpUnregisterIdPair,          &ExUnregisterIdPair_adapter);
    q.set_handler(kOpAdjustObjectCounter,       &ExAdjustObjectCounter_adapter);
    q.set_handler(kOpAdjustCharacterReputation, &ExAdjustCharacterReputation_adapter);
    q.set_handler(kOpSetObjectFillLevel,        &ExSetObjectFillLevel_adapter);
    q.set_handler(kOpEndTurn,                   &ExEndTurn_adapter);
    q.set_handler(kOpAck,                       &ExAck_adapter);
}

int ApplyPacket(CommandPacket& pkt, AckEntry* ack) {
    switch (pkt.opcode()) {
        case kOpPatchObjectFieldsAdd: { int r = ExPatchObjectFieldsAdd(pkt, ack); if (r == 0 && ack) ack->status = 1; return r; }
        case kOpWriteObjectFields:    { int r = ExWriteObjectFields(pkt, ack);    if (r == 0 && ack) ack->status = 1; return r; }
        case kOpApplyNeedDeltas:      { int r = ExApplyNeedDeltas(pkt, ack);      if (r == 0 && ack) ack->status = 1; return r; }
        case kOpPatchObjectBitfield:  { int r = ExPatchObjectBitfield(pkt, ack);  if (r == 0 && ack) ack->status = 1; return r; }
        case kOpAddObjectFloatField:  { int r = ExAddObjectFloatField(pkt, ack);  if (r == 0 && ack) ack->status = 1; return r; }
        case kOpRegisterIdPair:       return ExRegisterIdPair(pkt, ack);
        case kOpUnregisterIdPair:     return ExUnregisterIdPair(pkt, ack);
        case kOpAdjustObjectCounter:  return ExAdjustObjectCounter(pkt, ack);
        case kOpAdjustCharacterReputation: return ExAdjustCharacterReputation(pkt, ack);
        case kOpSetObjectFillLevel:   return ExSetObjectFillLevel(pkt, ack);
        case kOpEndTurn:              return ExEndTurn(pkt, ack);
        case kOpAck:                  return ExAck(pkt, ack);
        default:                      return -1; // unknown/guarded: no state change
    }
}

} // namespace guild::sim
