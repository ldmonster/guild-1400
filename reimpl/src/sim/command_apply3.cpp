#include "sim/command_apply3.h"

#include "sim/command_apply.h" // g_lastObjectId (dword_631288)
#include "sim/entity.h"

#include <cstring>

namespace guild::sim {

// ===========================================================================
// Module-global engine tables (the originals' file globals) + modeled state.
// ===========================================================================

namespace {

CutsceneTable   g_cutsceneTable; // dword_11AE6B0 (96 slots, stride 276)
CombatUnitField g_combatField;   // word_B5A350 (32 slots, stride 536)

char g_chatBuffer[kChatBufferBytes]; // byte_11B6BA0
i32  g_localPlayerId = -1;       // dword_12CE914[134 * word_63CC5C]
bool g_standalone    = true;     // dword_764CE0 == -1

CharActionLog g_charLog;

// --- default leaf backends --------------------------------------------------

// A tiny modeled actor set: the test "spawns" sp ids; FindByPredicate resolves
// those to a stable non-null sentinel (the sp id biased so 0 stays non-null).
i32 g_charFindAccept = -1;       // when >=0, accept exactly this sp id; -1 = any
void* DefaultCharFind(i32 spId) {
    if (g_charFindAccept >= 0 && spId != g_charFindAccept)
        return nullptr;
    if (spId < 0)
        return nullptr;
    // stable non-null handle derived from the id (never null for id>=0)
    return reinterpret_cast<void*>(static_cast<uintptr_t>(spId) + 0x10000);
}

i32 g_personCreateNext = 0x5000; // synthetic new-person id source
i32 DefaultPersonCreate(u8 /*kind*/, u8 /*a*/, u8 /*b*/) {
    return g_personCreateNext++;
}

int DefaultObjectFind(i32 id) {
    return BuildingFindById(id) ? 1 : 0;
}

int DefaultTargetBusy(const CommandPacket& /*pkt*/) { return 0; } // free

// The deferred combat-order slot tables: model a small ring keyed by (squad,
// unit) so a test can read back the stored 0x2C bytes.
constexpr int kCombatSlotSlots = 16;
struct CombatOrderSlot { bool used; i32 squad; i32 unit; u8 rec[0x2C]; };
CombatOrderSlot g_combatSlots[kCombatSlotSlots];
int DefaultCombatSlotStore(i32 squad, i32 unit, const u8* record, bool /*update*/) {
    // find existing (squad,unit) or first free
    int free = -1;
    for (int i = 0; i < kCombatSlotSlots; ++i) {
        if (g_combatSlots[i].used && g_combatSlots[i].squad == squad &&
            g_combatSlots[i].unit == unit) {
            std::memcpy(g_combatSlots[i].rec, record, 0x2C);
            return 1;
        }
        if (free < 0 && !g_combatSlots[i].used) free = i;
    }
    if (free < 0) return 0;
    g_combatSlots[free].used  = true;
    g_combatSlots[free].squad = squad;
    g_combatSlots[free].unit  = unit;
    std::memcpy(g_combatSlots[free].rec, record, 0x2C);
    return 1;
}

CharFindFn        g_charFind   = &DefaultCharFind;
PersonCreateFn    g_personCreate = &DefaultPersonCreate;
ObjectFindFn      g_objectFind = &DefaultObjectFind;
TargetBusyFn      g_targetBusy = &DefaultTargetBusy;
CombatSlotStoreFn g_combatStore = &DefaultCombatSlotStore;

// --- shared ack helpers (mirror the +0/+1/+6 stamping in the originals) ------
inline void AckBegin(AckEntry* ack) {
    if (ack) { ack->status = 2; ack->slot = 0; ack->seq = 0; }
}
inline void AckOk(AckEntry* ack) { if (ack) ack->status = 1; }
inline void AckStamp(AckEntry* ack, u8 status, u8 slot, i32 seq) {
    if (ack) { ack->status = status; ack->slot = slot; ack->seq = seq; }
}

// "sp_%i" predicate: the original sprintf's the dword at +16 into "sp_<id>" and
// looks the live actor up by name. We feed the raw id to the find hook.
inline void* FindActor(const CommandPacket& pkt) {
    return g_charFind(static_cast<i32>(pkt.get32(0x10)));
}

inline void NoteAction(i32 actorSp, i32 type, i32 arg) {
    g_charLog.lastActorSp = actorSp;
    g_charLog.lastActionType = type;
    g_charLog.lastArg = arg;
}

} // namespace

// ===========================================================================
// Accessors / hook setters.
// ===========================================================================

CutsceneTable&   Apply3_Cutscenes() { return g_cutsceneTable; }
CombatUnitField& Apply3_Combat()    { return g_combatField; }

const char* Apply3_ChatBuffer() { return g_chatBuffer; }
void        Apply3_ClearChat()  { g_chatBuffer[0] = '\0'; }

void Apply3_SetLocalPlayerId(i32 id) { g_localPlayerId = id; }
i32  Apply3_LocalPlayerId()          { return g_localPlayerId; }
void Apply3_SetStandalone(bool v)    { g_standalone = v; }

const CharActionLog& Apply3_CharLog() { return g_charLog; }
void Apply3_ResetCharLog() {
    std::memset(&g_charLog, 0, sizeof(g_charLog));
    g_charLog.lastActorSp = -1;
    g_charLog.lastActionType = -1;
    g_charLog.lastArg = 0;
    g_charLog.lastOrderKind = -1;
}

void SetCharFindHook(CharFindFn fn)           { g_charFind = fn ? fn : &DefaultCharFind; }
void SetPersonCreateHook(PersonCreateFn fn)   { g_personCreate = fn ? fn : &DefaultPersonCreate; }
void SetObjectFindHook(ObjectFindFn fn)       { g_objectFind = fn ? fn : &DefaultObjectFind; }
void SetTargetBusyHook(TargetBusyFn fn)       { g_targetBusy = fn ? fn : &DefaultTargetBusy; }
void SetCombatSlotStoreHook(CombatSlotStoreFn fn) { g_combatStore = fn ? fn : &DefaultCombatSlotStore; }

void ResetApply3State() {
    g_cutsceneTable.Clear();
    g_combatField.Clear();
    std::memset(g_chatBuffer, 0, sizeof(g_chatBuffer));
    std::memset(g_combatSlots, 0, sizeof(g_combatSlots));
    g_localPlayerId = -1;
    g_standalone = true;
    g_charFindAccept = -1;
    g_personCreateNext = 0x5000;
    Apply3_ResetCharLog();
    g_charFind = &DefaultCharFind;
    g_personCreate = &DefaultPersonCreate;
    g_objectFind = &DefaultObjectFind;
    g_targetBusy = &DefaultTargetBusy;
    g_combatStore = &DefaultCombatSlotStore;
}

// ===========================================================================
// Cutscene handlers (operate on g_cutsceneTable).
// ===========================================================================

// gilde.exe 0x4993A4 — VIBE_Command_ExAllocCutsceneWithId (opcode 0x27).
int ExAllocCutsceneWithId(CommandPacket& pkt, AckEntry* ack) {
    // The original copies a 276-byte template (unk_1077B60). In networked mode it
    // seeds the active-count global from payload +16; we model the active count
    // implicitly via AllocSlot. The new slot id IS the payload +16 (the original
    // sets dword_649894[0] from it before AllocSlot reads the id source).
    i32 newId = static_cast<i32>(pkt.get32(0x10));
    CutsceneSlot tmpl;
    std::memset(&tmpl, 0, sizeof(tmpl));
    tmpl.id = -1;
    // The live engine's alloc template (unk_1077B60) carries a set alive gate
    // (byte +48) so the slot is immediately resolvable by FindSlotById; the cold
    // IDB shows it zeroed (template is initialized at runtime). We seed the gate
    // here so the alloc -> add-participant -> ready flow round-trips. Participants
    // append starting at partIds[partCount].
    tmpl.partCount = 1; // alive gate (byte +48)
    CutsceneSlot* s = g_cutsceneTable.AllocSlot(tmpl, newId);
    if (!s)
        return 1;
    // v3[30] == dword index 30 == byte +120 <- payload +20.
    u32 v = pkt.get32(0x14);
    std::memcpy(reinterpret_cast<u8*>(s) + 120, &v, 4);
    AckStamp(ack, 1, 9, static_cast<i32>(reinterpret_cast<uintptr_t>(s)));
    return 0;
}

// gilde.exe 0x49947C — VIBE_Command_ExAllocCutscene (opcode 0x28).
int ExAllocCutscene(CommandPacket& /*pkt*/, AckEntry* ack) {
    CutsceneSlot tmpl;
    std::memset(&tmpl, 0, sizeof(tmpl));
    tmpl.id = -1;
    tmpl.partCount = 1; // alive gate (see ExAllocCutsceneWithId note)
    // The template carries no explicit new id here; AllocSlot defaults id from
    // the template (-1 stays free in the original until the caller fills it). We
    // mirror "alloc succeeds -> stamp ack" and "full -> reject".
    CutsceneSlot* s = g_cutsceneTable.AllocSlot(tmpl, -1);
    if (!s)
        return 1;
    AckStamp(ack, 1, 9, static_cast<i32>(reinterpret_cast<uintptr_t>(s)));
    return 0;
}

// gilde.exe 0x4994E8 — VIBE_Command_ExSetCutsceneState (opcode 0x29).
int ExSetCutsceneState(CommandPacket& pkt, AckEntry* ack) {
    i32 id = static_cast<i32>(pkt.get32(0x10));
    if (!g_cutsceneTable.FindById(id))
        return 1;
    if (!g_cutsceneTable.AddParticipant(id, static_cast<i32>(pkt.get32(0x14))))
        return 1;
    AckOk(ack);
    return 0;
}

// gilde.exe 0x499520 — VIBE_Command_ExClearCutsceneState (opcode 0x2A).
int ExClearCutsceneState(CommandPacket& pkt, AckEntry* ack) {
    i32 id = static_cast<i32>(pkt.get32(0x10));
    if (!g_cutsceneTable.FindById(id))
        return 1;
    // The original RemoveParticipant takes the slot id only (context-resolved
    // participant). Our slot-targeted model removes the participant equal to the
    // same id token; a missing participant is a no-op (the original returns 1 on
    // a found slot regardless). Faithful for the apply path's success/ack.
    g_cutsceneTable.RemoveParticipant(id, static_cast<i32>(pkt.get32(0x14)));
    AckOk(ack);
    return 0;
}

// gilde.exe 0x49CFC0 — VIBE_Command_ExCutsceneReady (opcode 0x59).
int ExCutsceneReady(CommandPacket& pkt, AckEntry* ack) {
    if (g_targetBusy(pkt)) {
        if (ack) ack->status = 2; // *v4 = 2 (busy reject)
        return 1;
    }
    if (ack) ack->status = 2;
    i32 slotId = static_cast<i32>(pkt.get32(0x10));
    g_cutsceneTable.FindById(slotId); // probe (return value unused by orig)

    // gilde.exe 0x49d011-0x49d029: edx walks from (a1+0x10) to (a1+0x50) stepping
    // +4 (16 iterations); each iteration reads the person id at [edx+4], i.e. the
    // 16 dwords at payload +0x14, +0x18, ... +0x50. The slot id written to each
    // found record's +520 is [esi] == *(a1+0x10). The kind==6/7 branches only add
    // an int3 (debug trap) before the SAME write, so the net effect for every
    // resolved person is record[+520] = slotId (regardless of prior value/kind).
    for (int i = 0; i < 16; ++i) {
        i32 personId = static_cast<i32>(pkt.get32(0x14 + 4 * i));
        Person* p = PersonFindRecordById(personId);
        if (!p)
            continue;
        u8* base = reinterpret_cast<u8*>(p);
        std::memcpy(base + kPfCutsceneId, &slotId, 4);
    }
    AckStamp(ack, 1, 0, 0);
    return 0;
}

// gilde.exe 0x49D3F0 — VIBE_Command_ExSetObjectField (opcode 0x5F).
int ExSetObjectField(CommandPacket& pkt, AckEntry* ack) {
    AckBegin(ack); // +0=2
    CutsceneSlot* s = g_cutsceneTable.FindById(static_cast<i32>(pkt.get32(0x10)));
    if (!s)
        return 1;
    // SlotById[3] == dword index 3 == byte +12 (the master field) <- payload +20.
    s->master = static_cast<i32>(pkt.get32(0x14));
    AckOk(ack);
    return 0;
}

// ===========================================================================
// Character / walk / sound handlers (delegating via the char-action log).
// ===========================================================================

// gilde.exe 0x4998E4 — VIBE_Command_ExCharPlaySample (opcode 0x2E).
int ExCharPlaySample(CommandPacket& pkt, AckEntry* ack) {
    void* actor = FindActor(pkt);
    if (!actor)
        return 0; // gilde.exe 0x499916: `return result` (the null find handle == 0)
    // InsertActionVararg(actor | 0x2D<<32, +20, +24, +28) — action type 0x2D (45).
    NoteAction(static_cast<i32>(pkt.get32(0x10)), 0x2D,
               static_cast<i32>(pkt.get32(0x14)));
    AckStamp(ack, 2, 0, 0);
    return 0;
}

// gilde.exe 0x499950 — VIBE_Command_ExChrWalkToDummy (opcode 0x2F).
int ExChrWalkToDummy(CommandPacket& pkt, AckEntry* ack) {
    void* actor = FindActor(pkt);
    if (!actor)
        return 1;
    // The body resolves a target object/building, switches universe and queues a
    // walk action (type 0x39 / 57). The render/path/universe leaves are deferred;
    // we record the queued action and stamp the ack the success path writes.
    NoteAction(static_cast<i32>(pkt.get32(0x10)), 0x39,
               static_cast<i32>(pkt.get32(0x14)));
    AckStamp(ack, 0, 0, 0); // *(a2)=v15 (uninitialized-on-success in orig); model 0
    AckOk(ack);             // success path ultimately leaves it applied
    return 0;
}

// gilde.exe 0x499B28 — VIBE_Command_ExCharUseObject (opcode 0x30).
int ExCharUseObject(CommandPacket& pkt, AckEntry* ack) {
    void* actor = FindActor(pkt);
    if (!actor)
        return 1;
    // The original queues type 0x38 (56) frame actions around a type 0x33 (51)
    // use action. The person/scene resolves are deferred; record the chain.
    NoteAction(static_cast<i32>(pkt.get32(0x10)), 0x33,
               static_cast<i32>(pkt.get32(0x18)));
    AckStamp(ack, 2, 0, 0); // v33 = 2
    return 0;
}

// gilde.exe 0x499DC4 — VIBE_Command_ExCharStandUp (opcode 0x31).
int ExCharStandUp(CommandPacket& pkt, AckEntry* ack) {
    void* actor = FindActor(pkt);
    if (!actor)
        return 2;
    ++g_charLog.standUpCount;
    NoteAction(static_cast<i32>(pkt.get32(0x10)), -2 /*standup*/, 0);
    AckStamp(ack, 2, 0, 0);
    return 0;
}

// gilde.exe 0x499E1C — VIBE_Command_ExCharSetVisibility (opcode 0x32).
int ExCharSetVisibility(CommandPacket& pkt, AckEntry* ack) {
    void* actor = FindActor(pkt);
    if (!actor)
        return 1;
    ++g_charLog.visibilityCount;
    NoteAction(static_cast<i32>(pkt.get32(0x10)), -3 /*visibility*/, 0);
    AckStamp(ack, 2, 0, 0);
    return 0;
}

// gilde.exe 0x499E84 — VIBE_Command_ExCharQueueAction (opcode 0x33).
int ExCharQueueAction(CommandPacket& pkt, AckEntry* ack) {
    void* actor = FindActor(pkt);
    if (!actor)
        return 1;
    // InsertActionVararg(actor | 0x38<<32, *(payload+20)) — action type 0x38 (56).
    NoteAction(static_cast<i32>(pkt.get32(0x10)), 0x38,
               static_cast<i32>(pkt.get32(0x14)));
    AckStamp(ack, 2, 0, 0);
    return 0;
}

// gilde.exe 0x499EEC — VIBE_Command_ExCharSpawnAtEntrance (opcode 0x34).
int ExCharSpawnAtEntrance(CommandPacket& pkt, AckEntry* ack) {
    // SpawnAtBuildingEntrance(building +20, 0, entry +24, 0) -> actor handle or 0.
    // The spawn is a deferred render/character leaf; model success via object find
    // on the building id so a missing building fails (as the original does).
    i32 building = static_cast<i32>(pkt.get32(0x14));
    if (!g_objectFind(building))
        return 1;
    ++g_charLog.spawnCount;
    NoteAction(building, -4 /*spawn-at-entrance*/, static_cast<i32>(pkt.get32(0x18)));
    AckStamp(ack, 2, 7, building); // +6 = actor (modeled as the building id)
    return 0;
}

// gilde.exe 0x499F24 — VIBE_Command_ExChrGotoBuilding (opcode 0x35).
int ExChrGotoBuilding(CommandPacket& pkt, AckEntry* ack) {
    void* actor = FindActor(pkt);
    if (!actor)
        return 1;
    // The body is a ~1.5KB path/room/transport state machine over the render and
    // heightmap leaves; we record that the walk-to-building command ran on this
    // actor and stamp the success ack the tail writes.
    NoteAction(static_cast<i32>(pkt.get32(0x10)), -5 /*goto-building*/,
               static_cast<i32>(pkt.get32(0x14)));
    AckStamp(ack, 1, 7, static_cast<i32>(pkt.get32(0x10)));
    return 0;
}

// gilde.exe 0x49AC6C — VIBE_Command_ExCharUseGate (opcode 0x36).
int ExCharUseGate(CommandPacket& pkt, AckEntry* ack) {
    void* actor = FindActor(pkt);
    if (!actor)
        return 1;
    NoteAction(static_cast<i32>(pkt.get32(0x10)), -6 /*use-gate*/,
               static_cast<i32>(pkt.get32(0x14)));
    AckStamp(ack, 2, 0, 0); // v23 = 2
    return 0;
}

// gilde.exe 0x49ADF4 — VIBE_Command_ExCharPlaySound (opcode 0x37).
int ExCharPlaySound(CommandPacket& pkt, AckEntry* ack) {
    void* actor = FindActor(pkt);
    if (!actor)
        return 2;
    ++g_charLog.soundCount;
    // CreateSoundAction(actor, _, *(byte)(payload+68)) — sound id at byte +0x44.
    NoteAction(static_cast<i32>(pkt.get32(0x10)), -7 /*sound*/, pkt.bytes[0x44]);
    AckStamp(ack, 2, 0, 0);
    return 0;
}

// gilde.exe 0x49B8D8 — VIBE_Command_ExCharApplyInteraction (opcode 0x3E).
int ExCharApplyInteraction(CommandPacket& pkt, AckEntry* ack) {
    Person* p = PersonFindRecordById(static_cast<i32>(pkt.get32(0x10)));
    if (!p)
        return 1; // also returns 1 if the person has no avatar (+97); deferred
    // The original swaps the avatar's mesh via the render bridge (deferred). We
    // record the interaction and stamp the success ack.
    ++g_charLog.interactionCount;
    NoteAction(p->id, -8 /*interaction*/, 0);
    // gilde.exe 0x49bba0: success stamps *(a2)=v52==2, *(a2+1)=1, *(a2+6)=record.
    // The status stays 2 (NOT 1) on the success path.
    AckStamp(ack, 2, 1, p->id);
    return 0;
}

// gilde.exe 0x49BD3C — VIBE_Command_ExTriggerCharacterAction (opcode 0x43).
int ExTriggerCharacterAction(CommandPacket& pkt, AckEntry* ack) {
    Person* p = PersonFindRecordById(static_cast<i32>(pkt.get32(0x10)));
    if (p) {
        // RecordById[92] (dword index 92 == byte +368) is the avatar ptr; when
        // nonzero, AttachStorageRooms runs (deferred). Record the trigger.
        ++g_charLog.triggerCount;
        NoteAction(p->id, -9 /*trigger*/, 0);
    }
    AckStamp(ack, 1, 0, 0); // *(v4)=1 unconditionally on non-null ack
    return 0;
}

// gilde.exe 0x49BE74 — VIBE_Command_ExApplyCharacterUpdate (opcode 0x48).
int ExApplyCharacterUpdate(CommandPacket& pkt, AckEntry* ack) {
    Person* p = PersonFindRecordById(static_cast<i32>(pkt.get32(0x10)));
    if (!p)
        return 1;
    // Building_UpdateOccupantCategory(marker, HIBYTE(*(payload+17))) +
    // Character_ResolveHeadBone — both deferred render/building leaves. The
    // category arg is byte (payload+20). Record it.
    ++g_charLog.charUpdateCount;
    NoteAction(p->id, -10 /*char-update*/, pkt.bytes[0x14]);
    AckStamp(ack, 1, 0, 0);
    return 0;
}

// gilde.exe 0x49BEC4 — VIBE_Command_ExSpawnAndPlaceCharacter (opcode 0x49).
int ExSpawnAndPlaceCharacter(CommandPacket& pkt, AckEntry* ack) {
    AckStamp(ack, 2, 1, 0); // entry: +0=2,+1=1,+6=0
    // Optional parent lookup at payload +23 (with -2/-3/-4 remap). A non-(-1)
    // parent that fails to resolve rejects.
    i32 parent = static_cast<i32>(pkt.get32(0x17));
    if (parent != -1) {
        switch (parent) {
            case -2: parent = g_lastObjectId; break;
            case -3: parent = g_lastSceneId;  break;
            case -4: parent = g_lastTradeId;  break;
            default: break;
        }
        pkt.put32(0x17, static_cast<u32>(parent));
        if (!BuildingFindById(parent) && !PersonFindRecordById(parent))
            return 1;
    }
    // Create the person (kind = HIBYTE(*(payload+17)) == byte +20).
    u8 kind = pkt.bytes[0x14];
    i32 newId = g_personCreate(kind, pkt.bytes[0x1C + 3], pkt.bytes[0x1D + 3]);
    if (newId < 0)
        return 1;
    g_lastObjectId = newId; // dword_631288 <- new person id
    ++g_charLog.spawnCount;
    NoteAction(newId, -11 /*spawn-person*/, kind);
    AckStamp(ack, 1, 1, newId); // +0=1,+1=1,+6=record(id)
    return 0;
}

// gilde.exe 0x49C094 — VIBE_Command_ExDeselectObject (opcode 0x4A).
int ExDeselectObject(CommandPacket& pkt, AckEntry* ack) {
    if (!g_objectFind(static_cast<i32>(pkt.get32(0x10))))
        return 1;
    // Building_DetachAndDestroyOccupant — deferred building leaf. Record it.
    ++g_charLog.deselectCount;
    NoteAction(static_cast<i32>(pkt.get32(0x10)), -12 /*deselect*/, 0);
    AckStamp(ack, 1, 0, 0);
    return 0;
}

// ===========================================================================
// Select / chat.
// ===========================================================================

// gilde.exe 0x49C0C0 — VIBE_Command_ExAppendChatLine (opcode 0x4B).
int ExAppendChatLine(CommandPacket& pkt, AckEntry* ack) {
    // The original throttles the buffer (clears once >5 "$A" markers accumulate)
    // — that is a display heuristic; we keep the buffer and the targeting gate.
    // Targeting: any of the 8 ids at payload +16..+44 equals the local player id?
    bool local = false;
    for (int i = 0; i < 8; ++i) {
        if (static_cast<i32>(pkt.get32(0x10 + 4 * i)) == g_localPlayerId) {
            local = true;
            break;
        }
    }
    // The original appends if (a2 != 0) OR the message targets the local player.
    if (!ack && !local)
        return 0;
    // Append the NUL-terminated text at payload +0x30 (byte +48) to the buffer.
    const char* src = reinterpret_cast<const char*>(pkt.bytes + 0x30);
    // bound the source to the payload region [0x30, kPacketStride)
    size_t maxSrc = kPacketStride - 0x30;
    size_t srcLen = 0;
    while (srcLen < maxSrc && src[srcLen] != '\0') ++srcLen;

    size_t curLen = std::strlen(g_chatBuffer);
    size_t room = (curLen < kChatBufferBytes - 1) ? (kChatBufferBytes - 1 - curLen) : 0;
    size_t n = (srcLen < room) ? srcLen : room;
    std::memcpy(g_chatBuffer + curLen, src, n);
    g_chatBuffer[curLen + n] = '\0';
    AckStamp(ack, 1, 0, 0);
    return 0;
}

// ===========================================================================
// Combat handlers.
// ===========================================================================

// gilde.exe 0x49B65C — VIBE_Command_ExEquipCombatObject (opcode 0x3D).
int ExEquipCombatObject(CommandPacket& pkt, AckEntry* ack) {
    // The original resolves the owner building (Person_QueryBegin on +16),
    // resolves/creates a combat unit, copies profession/equipment bytes onto it
    // and assigns guard targets — all deep combat + render + person leaves. We
    // gate on the owner id existing (the original's first failure), record the
    // equip, and stamp the success ack.
    // gilde.exe 0x49b6a0: PersonQueryBegin(...,*(a1+16)); `if (!Begin) return 1;`
    // The owner-not-found path returns 1 and DOES NOT touch the ack (the ack(2,1,0)
    // stamp belongs only to the later target-resolve-fail path, which returns 2 —
    // that path lives behind the deferred VIBE_Combat_ResolveTargetEntityRef leaf).
    if (!BuildingFindById(static_cast<i32>(pkt.get32(0x10))) &&
        !PersonFindRecordById(static_cast<i32>(pkt.get32(0x10)))) {
        return 1;
    }
    ++g_charLog.equipCount;
    NoteAction(static_cast<i32>(pkt.get32(0x10)), -13 /*equip*/, pkt.bytes[0x1D]);
    AckStamp(ack, 1, 1, static_cast<i32>(pkt.get32(0x10))); // +0=1,+1=1,+6=unit
    return 0;
}

// Shared body for the two combat-slot store handlers (0x50/0x51). The packet's
// owner id (+4) keys the squad; the unit id lives at +20 (0x50) or +5-word
// (0x51, dword +20). Both copy a 0x2C record from payload +20. Gated on the
// packet being for the local battle (cutscene id column == local player's).
static int StoreCombatSlot(CommandPacket& pkt, AckEntry* ack, bool update) {
    // gilde.exe 0x49c777: `mov eax,[ecx+40h]` -> the local-battle gate id is at
    // payload +0x40 (a1[16], a1 is _DWORD*), compared to dword_12CEB18[134*local].
    // (Modeled via the local-player id.)
    if (static_cast<i32>(pkt.get32(0x40)) != g_localPlayerId)
        return 1;
    // gilde.exe 0x49c78f: `mov edi,[ecx+10h]` -> the battle/squad ring key is the
    // dword at payload +0x10 (a1[4]), NOT cmd_id (+4).
    i32 squad = static_cast<i32>(pkt.get32(0x10));        // a1[4] (+0x10)
    i32 unit  = static_cast<i32>(pkt.get32(0x14));        // a1[5] (+0x14)
    const u8* rec = pkt.bytes + 0x14;                      // payload +0x14, 0x2C bytes
    g_combatStore(squad, unit, rec, update);
    AckStamp(ack, 1, 8, 0);
    return 0;
}

// gilde.exe 0x49C754 — VIBE_Command_ExStoreCombatSlot (opcode 0x50).
int ExStoreCombatSlot(CommandPacket& pkt, AckEntry* ack) {
    return StoreCombatSlot(pkt, ack, /*update=*/false);
}

// gilde.exe 0x49C824 — VIBE_Command_ExUpdateCombatSlot (opcode 0x51).
int ExUpdateCombatSlot(CommandPacket& pkt, AckEntry* ack) {
    return StoreCombatSlot(pkt, ack, /*update=*/true);
}

// gilde.exe 0x49C8EC — VIBE_Command_ExApplyCombatDamage (opcode 0x52).
int ExApplyCombatDamage(CommandPacket& pkt, AckEntry* ack) {
    CombatUnit* u = g_combatField.FindUnitById(static_cast<i32>(pkt.get32(0x10)));
    if (u) {
        // SpawnDamageNumber (presentation, deferred); hp -= payload+20; flag=+24.
        i32 dmg = static_cast<i32>(pkt.get32(0x14));
        u->hp -= dmg;
        u->alive = pkt.bytes[0x18];
    }
    AckStamp(ack, 1, 8, 0);
    return 0;
}

// gilde.exe 0x49CDA0 — VIBE_Command_ExDispatchUnitOrder (opcode 0x55).
int ExDispatchUnitOrder(CommandPacket& pkt, AckEntry* ack) {
    // local-battle gate (dword_6315C0 != 0 && *dword_6315C0 == payload+16). We
    // model the active-battle id as the local player id.
    if (static_cast<i32>(pkt.get32(0x10)) != g_localPlayerId)
        return 1;
    // unit id at payload +53 (a1+53); order code at byte +20 (a1+20 of the order
    // sub-record == packet byte 0x14? No: v6 is the order base; the byte is at
    // v6+20). The order record begins at payload +53; the dispatch switch reads
    // the order code byte at +0x14 of the packet payload region. We read the
    // order code at byte +0x14.
    u8 order = pkt.bytes[0x14];
    CombatUnit* u = g_combatField.FindUnitById(static_cast<i32>(pkt.get32(0x35)));
    bool alive = u && u->alive;
    switch (order) {
        case 1: /* celebrate */            if (alive) g_charLog.lastOrderKind = 1; break;
        case 2: /* attack */               if (alive) g_charLog.lastOrderKind = 2; break;
        case 3: /* conquer flag */         if (alive) g_charLog.lastOrderKind = 3; break;
        case 4: /* pick up from ground */  if (alive) g_charLog.lastOrderKind = 4; break;
        case 5: /* pickup ground item */   if (alive) g_charLog.lastOrderKind = 5; break;
        case 6: /* stand up */             if (alive) g_charLog.lastOrderKind = 6; break;
        case 7: /* unit select sound */    g_charLog.lastOrderKind = 7; break;
        default: break;
    }
    AckStamp(ack, 1, 8, 0);
    return 0;
}

// ===========================================================================
// Dispatch wiring.
// ===========================================================================

namespace {
#define APPLY3_ADAPTER(NAME) \
    void NAME##_adapter(CommandQueue&, CommandPacket& pkt, AckEntry* ack) { NAME(pkt, ack); }
APPLY3_ADAPTER(ExAllocCutsceneWithId)
APPLY3_ADAPTER(ExAllocCutscene)
APPLY3_ADAPTER(ExSetCutsceneState)
APPLY3_ADAPTER(ExClearCutsceneState)
APPLY3_ADAPTER(ExCutsceneReady)
APPLY3_ADAPTER(ExSetObjectField)
APPLY3_ADAPTER(ExCharPlaySample)
APPLY3_ADAPTER(ExChrWalkToDummy)
APPLY3_ADAPTER(ExCharUseObject)
APPLY3_ADAPTER(ExCharStandUp)
APPLY3_ADAPTER(ExCharSetVisibility)
APPLY3_ADAPTER(ExCharQueueAction)
APPLY3_ADAPTER(ExCharSpawnAtEntrance)
APPLY3_ADAPTER(ExChrGotoBuilding)
APPLY3_ADAPTER(ExCharUseGate)
APPLY3_ADAPTER(ExCharPlaySound)
APPLY3_ADAPTER(ExCharApplyInteraction)
APPLY3_ADAPTER(ExTriggerCharacterAction)
APPLY3_ADAPTER(ExApplyCharacterUpdate)
APPLY3_ADAPTER(ExSpawnAndPlaceCharacter)
APPLY3_ADAPTER(ExDeselectObject)
APPLY3_ADAPTER(ExAppendChatLine)
APPLY3_ADAPTER(ExEquipCombatObject)
APPLY3_ADAPTER(ExStoreCombatSlot)
APPLY3_ADAPTER(ExUpdateCombatSlot)
APPLY3_ADAPTER(ExApplyCombatDamage)
APPLY3_ADAPTER(ExDispatchUnitOrder)
#undef APPLY3_ADAPTER
} // namespace

void RegisterApplyHandlers3(CommandQueue& q) {
    q.set_handler(kOp3AllocCutsceneWithId,    &ExAllocCutsceneWithId_adapter);
    q.set_handler(kOp3AllocCutscene,          &ExAllocCutscene_adapter);
    q.set_handler(kOp3SetCutsceneState,       &ExSetCutsceneState_adapter);
    q.set_handler(kOp3ClearCutsceneState,     &ExClearCutsceneState_adapter);
    q.set_handler(kOp3CutsceneReady,          &ExCutsceneReady_adapter);
    q.set_handler(kOp3SetObjectField,         &ExSetObjectField_adapter);
    q.set_handler(kOp3CharPlaySample,         &ExCharPlaySample_adapter);
    q.set_handler(kOp3ChrWalkToDummy,         &ExChrWalkToDummy_adapter);
    q.set_handler(kOp3CharUseObject,          &ExCharUseObject_adapter);
    q.set_handler(kOp3CharStandUp,            &ExCharStandUp_adapter);
    q.set_handler(kOp3CharSetVisibility,      &ExCharSetVisibility_adapter);
    q.set_handler(kOp3CharQueueAction,        &ExCharQueueAction_adapter);
    q.set_handler(kOp3CharSpawnAtEntrance,    &ExCharSpawnAtEntrance_adapter);
    q.set_handler(kOp3ChrGotoBuilding,        &ExChrGotoBuilding_adapter);
    q.set_handler(kOp3CharUseGate,            &ExCharUseGate_adapter);
    q.set_handler(kOp3CharPlaySound,          &ExCharPlaySound_adapter);
    q.set_handler(kOp3CharApplyInteraction,   &ExCharApplyInteraction_adapter);
    q.set_handler(kOp3TriggerCharacterAction, &ExTriggerCharacterAction_adapter);
    q.set_handler(kOp3ApplyCharacterUpdate,   &ExApplyCharacterUpdate_adapter);
    q.set_handler(kOp3SpawnAndPlaceCharacter, &ExSpawnAndPlaceCharacter_adapter);
    q.set_handler(kOp3DeselectObject,         &ExDeselectObject_adapter);
    q.set_handler(kOp3AppendChatLine,         &ExAppendChatLine_adapter);
    q.set_handler(kOp3EquipCombatObject,      &ExEquipCombatObject_adapter);
    q.set_handler(kOp3StoreCombatSlot,        &ExStoreCombatSlot_adapter);
    q.set_handler(kOp3UpdateCombatSlot,       &ExUpdateCombatSlot_adapter);
    q.set_handler(kOp3ApplyCombatDamage,      &ExApplyCombatDamage_adapter);
    q.set_handler(kOp3DispatchUnitOrder,      &ExDispatchUnitOrder_adapter);
}

int ApplyPacket3(CommandPacket& pkt, AckEntry* ack) {
    switch (pkt.opcode()) {
        case kOp3AllocCutsceneWithId:    return ExAllocCutsceneWithId(pkt, ack);
        case kOp3AllocCutscene:          return ExAllocCutscene(pkt, ack);
        case kOp3SetCutsceneState:       return ExSetCutsceneState(pkt, ack);
        case kOp3ClearCutsceneState:     return ExClearCutsceneState(pkt, ack);
        case kOp3CutsceneReady:          return ExCutsceneReady(pkt, ack);
        case kOp3SetObjectField:         return ExSetObjectField(pkt, ack);
        case kOp3CharPlaySample:         return ExCharPlaySample(pkt, ack);
        case kOp3ChrWalkToDummy:         return ExChrWalkToDummy(pkt, ack);
        case kOp3CharUseObject:          return ExCharUseObject(pkt, ack);
        case kOp3CharStandUp:            return ExCharStandUp(pkt, ack);
        case kOp3CharSetVisibility:      return ExCharSetVisibility(pkt, ack);
        case kOp3CharQueueAction:        return ExCharQueueAction(pkt, ack);
        case kOp3CharSpawnAtEntrance:    return ExCharSpawnAtEntrance(pkt, ack);
        case kOp3ChrGotoBuilding:        return ExChrGotoBuilding(pkt, ack);
        case kOp3CharUseGate:            return ExCharUseGate(pkt, ack);
        case kOp3CharPlaySound:          return ExCharPlaySound(pkt, ack);
        case kOp3CharApplyInteraction:   return ExCharApplyInteraction(pkt, ack);
        case kOp3TriggerCharacterAction: return ExTriggerCharacterAction(pkt, ack);
        case kOp3ApplyCharacterUpdate:   return ExApplyCharacterUpdate(pkt, ack);
        case kOp3SpawnAndPlaceCharacter: return ExSpawnAndPlaceCharacter(pkt, ack);
        case kOp3DeselectObject:         return ExDeselectObject(pkt, ack);
        case kOp3AppendChatLine:         return ExAppendChatLine(pkt, ack);
        case kOp3EquipCombatObject:      return ExEquipCombatObject(pkt, ack);
        case kOp3StoreCombatSlot:        return ExStoreCombatSlot(pkt, ack);
        case kOp3UpdateCombatSlot:       return ExUpdateCombatSlot(pkt, ack);
        case kOp3ApplyCombatDamage:      return ExApplyCombatDamage(pkt, ack);
        case kOp3DispatchUnitOrder:      return ExDispatchUnitOrder(pkt, ack);
        default:                         return -1; // unknown/guarded: no change
    }
}

} // namespace guild::sim
