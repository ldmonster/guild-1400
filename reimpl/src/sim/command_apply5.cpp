#include "sim/command_apply5.h"

#include "sim/command_apply.h"   // shared g_last* remap tokens
#include "sim/building_create.h" // Building_CreateGebaeude leaf
#include "sim/person_create.h"   // Person_CreateAndSpawn leaf
#include "sim/family_record.h"   // Person_GetFamilyRecord (word_13C3110)
#include "sim/entity.h"          // g_persons / g_objects / PersonFindRecordById
#include "crt/rand.h"            // crt::RandNext (0x5cb8bc LCG) — HandleCreatePersonA draw

#include <cstring>
#include <cstdint>
#include <cstddef>

namespace guild::sim {

// ===========================================================================
// Owned engine globals (the binary's file globals).
// ===========================================================================
u8       g_sysGlobalFlags  = 0;   // byte_63CC28
i32      g_sysLoadFlag     = 0;   // dword_11AA480
GameTime g_sysGameTime     = {};  // qword_13CE852
u8       g_sysByte63CC1D   = 0;   // byte_63CC1D
i32      g_sysDword764CF4   = 0;  // dword_764CF4
i32      g_sysActivePlayer  = -1; // dword_63CC24 (-1 == free)
u8       g_sysByte63C8F4    = 0;  // byte_63C8F4
i32      g_sysDword122F49C   = 0; // dword_122F49C
i32      g_sysDword63CC30    = 0; // dword_63CC30
i32      g_sysDword63CC70    = 0; // dword_63CC70
u8       g_sysName63CC74[64] = {};// byte_63CC74
i32      g_sysDword63127C     = 0;// dword_63127C
i32      g_sysCutTable50[8]  = {-1,-1,-1,-1,-1,-1,-1,-1}; // dword_13CEC50
i32      g_sysCutTable54[8]  = {};// dword_13CEC54
i32      g_sysDword631294     = 0;// dword_631294
i16      g_currentPlayer      = -1;// word_63CC5C

namespace {
bool      g_standalone5 = true; // dword_764CE0 == -1
CreateLog g_log{};
} // namespace

void Apply5_SetStandalone(bool v) { g_standalone5 = v; }
bool Apply5_Standalone() { return g_standalone5; }
const CreateLog& Apply5_CreateLog() { return g_log; }

// ===========================================================================
// Modeled leaf tables + hooks.
// ===========================================================================
namespace {

// --- office slot table (VIBE_Amt_* office cluster) --------------------------
OfficeSlot g_officeTable[kOfficeSlots];

OfficeSlot* DefaultOfficeAssign(i32 /*personId*/, i32 officeType, i32 /*coord*/, i32 /*mode*/) {
    for (auto& s : g_officeTable) {
        if (!s.used) {
            s.used = true; s.type = officeType; s.holderId = -1; s.extra = 0;
            return &s;
        }
    }
    return nullptr;
}

// --- chat-buffer table (talk action node) ----------------------------------
constexpr int kChatSlots   = 8;
constexpr int kChatBufBytes = 64;
struct ChatSlot {
    bool used;
    i32  actorId;
    i32  capacity;
    u8   buffer[kChatBufBytes];
    ChatTarget view;
};
ChatSlot g_chatTable[kChatSlots];

const ChatTarget* DefaultChatFind(i32 actorId) {
    for (auto& s : g_chatTable) {
        if (s.used && s.actorId == actorId) {
            s.view.capacity = &s.capacity;
            s.view.buffer = s.buffer;
            s.view.bufferLen = kChatBufBytes;
            return &s.view;
        }
    }
    return nullptr;
}

// --- render / building lifecycle leaves -------------------------------------
void DefaultPlaceMesh(int /*op*/, u8* rec) {
    ++g_log.placeMeshCount;
    if (rec) { i32 id; std::memcpy(&id, rec + 1, 4); g_log.lastBuildingId = id; }
}
void DefaultInitWorkerCaps(u8* /*rec*/) {}
void DefaultBuildingFree(u8* rec) {
    // VIBE_Building_FreeAndUnlink: clear the record's alive byte so it frees.
    if (rec) rec[0] = 0;
}
void DefaultDetachOccupant(SceneNode* /*node*/) { ++g_log.detachOccupantCount; }
void DefaultRemoveByProt(i32 /*container*/, i16 /*proto*/) { ++g_log.removeByProtCount; }
void DefaultSysMessageLeaf(int caseType) {
    ++g_log.sysMessageLeafCount; g_log.lastSysLeafCase = caseType;
}

OfficeAssignFn      g_officeHook  = &DefaultOfficeAssign;
ChatFindFn          g_chatHook    = &DefaultChatFind;
BuildingPlaceMeshFn g_meshHook    = &DefaultPlaceMesh;
InitWorkerCapsFn    g_capsHook    = &DefaultInitWorkerCaps;
BuildingFreeFn5      g_freeHook    = &DefaultBuildingFree;
DetachOccupantFn    g_detachHook  = &DefaultDetachOccupant;
RemoveByProtFn      g_removeHook   = &DefaultRemoveByProt;
SysMessageLeafFn    g_sysLeafHook  = &DefaultSysMessageLeaf;

// --- ack helper -------------------------------------------------------------
inline void AckSet(AckEntry* ack, u8 status, u8 slot, i32 seq) {
    if (ack) { ack->status = status; ack->slot = slot; ack->seq = seq; }
}

// Resolve the -2/-3/-4 last-created remap tokens.
i32 Remap(i32 id) {
    switch (id) {
        case -2: return g_lastObjectId;
        case -3: return g_lastSceneId;
        case -4: return g_lastTradeId;
        default: return id;
    }
}

// Little-endian raw reads at a byte offset into the packet.
inline i32 RdI32(const CommandPacket& p, u32 off) { return static_cast<i32>(p.get32(off)); }
inline u16 RdU16(const CommandPacket& p, u32 off) { return p.get16(off); }
inline u8  RdU8 (const CommandPacket& p, u32 off) { return p.bytes[off]; }
inline u8  HiByte(const CommandPacket& p, u32 dwordOff) { return static_cast<u8>(p.get32(dwordOff) >> 24); }
inline i16 HiWord(const CommandPacket& p, u32 dwordOff) { return static_cast<i16>(p.get32(dwordOff) >> 16); }

// VIBE_Util_StrNCopyPad @0x5d9360 — copy up to n source chars, stopping early at
// the first NUL (which is NOT emitted), then zero-pad the remainder of the n-byte
// field. A full-width (n-char, no NUL) source fills all n bytes WITHOUT a
// terminator. (A raw memcpy would copy trailing bytes past an early NUL and a
// hardcoded rec[n-1]=0 would clobber a full-width name — both are wrong.)
inline void StrNCopyPad(u8* dst, const u8* src, u32 n) {
    u32 i = 0;
    for (; i < n && src[i] != 0; ++i) dst[i] = src[i];
    for (; i < n; ++i) dst[i] = 0;
}

} // namespace

OfficeSlot* Apply5_OfficeSlot(int i) {
    if (i < 0 || i >= kOfficeSlots) return nullptr;
    return &g_officeTable[i];
}
bool Apply5_SeedChatTarget(i32 actorId, int capacity) {
    for (auto& s : g_chatTable) {
        if (!s.used) {
            s.used = true; s.actorId = actorId; s.capacity = capacity;
            std::memset(s.buffer, 0, sizeof(s.buffer));
            return true;
        }
    }
    return false;
}

void SetOfficeAssignHook(OfficeAssignFn fn)       { g_officeHook = fn ? fn : &DefaultOfficeAssign; }
void SetChatFindHook(ChatFindFn fn)               { g_chatHook = fn ? fn : &DefaultChatFind; }
void SetBuildingPlaceMeshHook(BuildingPlaceMeshFn fn){ g_meshHook = fn ? fn : &DefaultPlaceMesh; }
void SetInitWorkerCapsHook(InitWorkerCapsFn fn)   { g_capsHook = fn ? fn : &DefaultInitWorkerCaps; }
void SetBuildingFreeHook(BuildingFreeFn5 fn)       { g_freeHook = fn ? fn : &DefaultBuildingFree; }
void SetDetachOccupantHook(DetachOccupantFn fn)   { g_detachHook = fn ? fn : &DefaultDetachOccupant; }
void SetRemoveByProtHook(RemoveByProtFn fn)       { g_removeHook = fn ? fn : &DefaultRemoveByProt; }
void SetSysMessageLeafHook(SysMessageLeafFn fn)   { g_sysLeafHook = fn ? fn : &DefaultSysMessageLeaf; }

void ResetApply5State() {
    g_sysGlobalFlags = 0; g_sysLoadFlag = 0; g_sysGameTime = GameTime{};
    g_sysByte63CC1D = 0; g_sysDword764CF4 = 0; g_sysActivePlayer = -1;
    g_sysByte63C8F4 = 0; g_sysDword122F49C = 0; g_sysDword63CC30 = 0;
    g_sysDword63CC70 = 0; std::memset(g_sysName63CC74, 0, sizeof(g_sysName63CC74));
    g_sysDword63127C = 0;
    for (int i = 0; i < 8; ++i) { g_sysCutTable50[i] = -1; g_sysCutTable54[i] = 0; }
    g_sysDword631294 = 0; g_currentPlayer = -1;
    g_standalone5 = true; g_log = CreateLog{};
    for (auto& s : g_officeTable) { s.used = false; s.holderId = -1; s.type = 0; s.extra = 0; }
    for (auto& s : g_chatTable) { s.used = false; s.actorId = 0; s.capacity = 0; }
    g_officeHook = &DefaultOfficeAssign; g_chatHook = &DefaultChatFind;
    g_meshHook = &DefaultPlaceMesh; g_capsHook = &DefaultInitWorkerCaps;
    g_freeHook = &DefaultBuildingFree; g_detachHook = &DefaultDetachOccupant;
    g_removeHook = &DefaultRemoveByProt; g_sysLeafHook = &DefaultSysMessageLeaf;
}

// ===========================================================================
// Person-array helpers (the originals index parallel columns by slot index).
// ===========================================================================
namespace {
// dword_12CE914[134*idx] — the parallel id column == g_persons[idx].id.
i32 PersonIdAt(int idx) {
    if (idx < 0 || idx >= kPersonCapacity) return 0;
    return g_personIds[idx];
}
// &word_12CE910[268*idx] — the record base.
u8* PersonRecAt(int idx) {
    if (idx < 0 || idx >= kPersonCapacity) return nullptr;
    return reinterpret_cast<u8*>(&g_persons[idx]);
}
// Linear scan of the parallel id column for the owner index (HandleSpawnObject).
// Original loop: skip slots whose marker word == -1 (free) or whose id column
// mismatches; return the first alive slot whose id matches. 0xFFFF if exhausted.
u16 FindOwnerIndexById(i32 id) {
    for (int i = 0; i < kPersonCapacity; ++i) {
        if (g_persons[i].marker != -1 && g_personIds[i] == id)
            return static_cast<u16>(i);
    }
    return 0xFFFF;
}
} // namespace

// ===========================================================================
// 0x00 / 0x03 — state gate.
// ===========================================================================
// gilde.exe 0x49644C — VIBE_Command_HandleStateGate.
int ExHandleStateGate(CommandPacket& pkt, AckEntry* ack) {
    u8 op = pkt.bytes[kFOpcode];
    if (op != 0 && op != 5 && op != 6 && op != 7)
        return 1;
    if (ack) ack->status = 1;
    return 0;
}

// ===========================================================================
// 0x0A — create building directly.
// ===========================================================================
// gilde.exe 0x496520 — VIBE_Command_HandleSpawnObject.
int ExCreateBuildingDirect(CommandPacket& pkt, AckEntry* ack) {
    if (!g_standalone5)
        g_buildingNextId = RdI32(pkt, 16); // dword_649890 = *(a1+16)

    i32 ownerId = RdI32(pkt, 22);
    u16 ownerIdx;
    if (ownerId == -1) {
        ownerIdx = 0xFFFF;
    } else {
        i32 resolved = Remap(ownerId);
        pkt.put32(22, static_cast<u32>(resolved));   // *(a1+22) = resolved
        ownerIdx = FindOwnerIndexById(resolved);
        if (ownerIdx == 0xFFFF) return 1;
    }

    u8* geb = Building_CreateGebaeude(RdU8(pkt, 20), ownerIdx);
    if (!geb) return 1;
    ++g_log.createBuildingCount;

    i32 newId; std::memcpy(&newId, geb + 1, 4);
    g_lastSceneId = newId;                            // dword_63128C
    g_log.lastBuildingId = newId;

    // *((WORD*)Geb + 45) = *(a1+26)  -> word at building +90.
    u16 w = RdU16(pkt, 26);
    std::memcpy(geb + 90, &w, 2);

    // if (*(a1+28)) qmemcpy(Geb+101, a1+32, 0x30).
    if (RdI32(pkt, 28) != 0)
        std::memcpy(geb + 101, pkt.bytes + 32, 0x30);

    // 0x496588 — *(a2+6) = Gebaeude (the new building record token).
    if (ack) { ack->status = 1; ack->slot = 2;
               ack->seq = static_cast<i32>(reinterpret_cast<std::intptr_t>(geb) & 0xFFFFFFFF); }
    return 0;
}

// ===========================================================================
// 0x0B — create person (from a parent record).
// ===========================================================================
// gilde.exe 0x496614 — VIBE_Command_HandleCreatePersonA.
int ExCreatePersonA(CommandPacket& pkt, AckEntry* ack) {
    // 0x49661c — branch on dword_764CE0 (standalone == -1).
    if (g_standalone5) {
        // 0x4966bc — standalone path: *(a1+38) = VIBE_Util_RandNext(); the LCG
        // advance leaves edx == the new state, which the unconditional RandSeed
        // thunk below writes straight back (a no-op), so the NET effect is exactly
        // ONE RandNext draw — kept so the RNG stream Person_CreateAndSpawn consumes
        // stays in sync with the binary.
        i32 r = crt::RandNext();
        pkt.put32(38, static_cast<u32>(r));
    } else {
        g_personNextId = RdI32(pkt, 16); // 0x49662c — dword_649890 = *(a1+16)
        // 0x496634 — VIBE_Math_RandomSeed_Thunk(): RandSeed(edx) where edx == the
        // ack pointer (an indeterminate address). BOUNDARY: the original seeds the
        // RNG with a non-reproducible host pointer here; we leave the RNG state
        // untouched in the non-standalone path (cannot faithfully reproduce edx).
    }

    // v3 == packet base. Optionally resolve the parent BUILDING record by
    // id(+31): the original's Person_QueryBegin(1,1,id) @0x4966e5 iterates the
    // 169-stride OBJECT/BUILDING array dword_13CE298 (cursor seed @0x586cb9),
    // i.e. BuildingFindById semantics — NOT the person array. (Verified
    // 2026-06-11; the earlier person-array resolve was a misread.) Miss => 1.
    const void* parentRecToken = nullptr;
    i32 v4 = RdI32(pkt, 31);
    if (v4 != -1) {
        i32 resolved = Remap(v4);
        pkt.put32(31, static_cast<u32>(resolved));
        ObjectRec* parentBld = BuildingFindById(resolved);
        if (!parentBld) return 1;
        parentRecToken = parentBld;
    }

    PersonSpawnArgs args{};
    args.kind      = HiByte(pkt, 17);             // HIBYTE(*(v3+17))
    args.parentAId = RdI32(pkt, 21);              // *(v3+21)
    args.ownerWord = RdU16(pkt, 29);              // *(WORD)(v3+29)
    args.parentBId = RdI32(pkt, 25);              // *(v3+25)
    args.queryRec  = parentRecToken;
    args.a6        = HiByte(pkt, 32);             // HIBYTE(*(v3+32))
    args.a7        = HiByte(pkt, 33);             // HIBYTE(*(v3+33))
    args.a8        = HiByte(pkt, 34);             // HIBYTE(*(v3+34))

    u16 idx = Person_CreateAndSpawn(args);
    if (idx == 0xFFFF) return 1;
    ++g_log.createPersonCount;

    g_lastObjectId = PersonIdAt(idx);             // dword_631288 = dword_12CE914[134*v6]
    g_log.lastPersonId = g_lastObjectId;

    AckSet(ack, 1, 1, reinterpret_cast<std::intptr_t>(PersonRecAt(idx)) & 0xFFFFFFFF);
    return 0;
}

// ===========================================================================
// 0x0C — create person (spouse) with family fields.
// ===========================================================================
// gilde.exe 0x496714 — VIBE_Command_HandleCreatePersonB.
int ExCreatePersonB(CommandPacket& pkt, AckEntry* ack) {
    // 0x496727 — standalone path mirrors HandleCreatePersonA: *(a1+69) =
    // RandNext() then the (no-op) RandSeed thunk => exactly ONE RandNext draw,
    // kept so the RNG stream Person_CreateAndSpawn consumes stays in sync.
    if (g_standalone5) {
        i32 r = crt::RandNext();
        pkt.put32(69, static_cast<u32>(r));
    } else {
        g_personNextId = RdI32(pkt, 16); // 0x496730 — dword_649890 = *(a1+16)
        // 0x496738 — RandSeed(edx == ack ptr): BOUNDARY (indeterminate host
        // pointer; not reproducible) — RNG state left untouched here.
    }

    PersonSpawnArgs args{};
    args.kind      = static_cast<u8>((ack == nullptr ? 1 : 0) + 6); // (a2==0)+6
    args.parentAId = RdI32(pkt, 20);              // *(a1+20)
    args.ownerWord = RdU16(pkt, 28);              // *(WORD)(a1+28)
    args.parentBId = RdI32(pkt, 24);              // *(a1+24)
    args.queryRec  = 0;
    args.a6        = HiByte(pkt, 27);             // HIBYTE(*(a1+27))
    args.a7        = 0;
    args.a8        = HiByte(pkt, 32);             // HIBYTE(*(a1+32))

    u16 idx = Person_CreateAndSpawn(args);
    if (idx == 0xFFFF) return 1;
    ++g_log.createPersonCount;

    g_lastObjectId = PersonIdAt(idx);             // dword_631288
    g_log.lastPersonId = g_lastObjectId;

    u8* rec = PersonRecAt(idx);
    // 0x4967b6 — StrNCopyPad(record+48, a1+37, 16): copy up to 16 chars (stop at
    // NUL, zero-pad the rest; a 16-char name leaves no terminator).
    StrNCopyPad(rec + 48, pkt.bytes + 37, 16);

    // 0x4967bf..0x49681c — Person_GetFamilyRecord(rec) block. RECONSTRUCTED
    // (wave-18, sim/family_record): the CreateAndSpawn leaf's family allocation
    // (kind 6/7/5) stamps +0x50 = dword_647720|0x8000, so the sign byte +81 < 0
    // and the accessor now returns the real family record over word_13C3110.
    //   word[0]  = rec word +0x50 (word_12CE960[v5/2]);
    //   StrNCopyPad(familyRec+2, packet+53, 16) — the dynasty name;
    //   StrNCopyPad(rec+64, packet+53, 16)       — person's own family-name field;
    //   the +0x60-linked spouse's +64 gets the same name (FindRecordById);
    //   familyRec[+128] = -1.0f.
    if (u8* fam = Person_GetFamilyRecord(rec)) {
        i16 famWord; std::memcpy(&famWord, rec + 0x50, 2);   // rec +0x50 word
        const char* name = reinterpret_cast<const char*>(pkt.bytes + 53);
        FamilyRecord_StampName(fam, famWord, name);          // +0/+2/+128
        // 0x4967f6 — StrNCopyPad(record+64, a1+53, 16): person's own family-name.
        StrNCopyPad(rec + 64, pkt.bytes + 53, 16);
        // 0x496801 — spouse (record +0x60 dword id): StrNCopyPad its +64 field too,
        // when the spouse record resolves.
        i32 spouseId; std::memcpy(&spouseId, rec + 0x60, 4);
        if (Person* sp = PersonFindRecordById(spouseId)) {
            StrNCopyPad(reinterpret_cast<u8*>(sp) + 64, pkt.bytes + 53, 16);
        }
    }

    // 0x49683c — dword_12CE964[134*idx] = *(a1+31): the wappen id (1342+i)
    // into record dword +0x54.
    std::memcpy(rec + 0x54, pkt.bytes + 31, 4);
    // 0x496845 — HIBYTE(dword_12CE919[...]) = *(a1+36): the faith byte into
    // record byte +12 (dword at +9, high byte).
    rec[12] = pkt.bytes[36];

    AckSet(ack, 1, 1, reinterpret_cast<std::intptr_t>(rec) & 0xFFFFFFFF);
    return 0;
}

// ===========================================================================
// 0x13 — move a scene node between owners' child lists.
// ===========================================================================
namespace {
// Resolve an id to the child-list head pointer slot the original computes:
//   if object: rec+93 ; else if scene: node+20 ; else if person: rec+188.
// In the reimpl scene nodes are index-linked; we operate on the SceneNode tree via
// entity.cpp's index links. The list head for an owner is its childPtr field.
// We model the move using scene-node indices (entityPtr/childPtr as -1 sentinels).
// Returns a byte pointer to the 32-bit list-head slot (accessed via memcpy to stay
// alignment-safe over the packed records). Null if the id does not resolve.
u8* ResolveListHead(i32 id) {
    ObjectRec* obj = nullptr; SceneNode* scn = nullptr; Person* per = nullptr;
    int r = GameObjectResolveEntityById(&obj, &scn, id, &per);
    if (r == 0) return nullptr;
    if (obj) return reinterpret_cast<u8*>(obj) + 93;
    // 0x497951 — scene head slot is node+20 (entityPtr), NOT childPtr(+63);
    // the per-node next links are at +63 but the list HEAD lives at +20.
    if (scn) return reinterpret_cast<u8*>(scn) + offsetof(SceneNode, entityPtr);
    if (per) return reinterpret_cast<u8*>(per) + 188;
    return nullptr;
}
inline i32 LoadHead(const u8* p) { i32 v; std::memcpy(&v, p, 4); return v; }
inline void StoreHead(u8* p, i32 v) { std::memcpy(p, &v, 4); }
} // namespace

// gilde.exe 0x49790C — VIBE_Command_ExMoveObjectBetweenLists.
int ExMoveObjectBetweenLists(CommandPacket& pkt, AckEntry* ack) {
    // src owner (+0x14), dst owner (+0x10), moved-node id (+0x18); each remapped.
    i32 srcId = Remap(RdI32(pkt, 20)); pkt.put32(20, static_cast<u32>(srcId));
    u8* srcHead = ResolveListHead(srcId);
    if (!srcHead) return 1;

    i32 dstId = Remap(RdI32(pkt, 16)); pkt.put32(16, static_cast<u32>(dstId));
    u8* dstHead = ResolveListHead(dstId);
    if (!dstHead) return 1;

    i32 nodeId = Remap(RdI32(pkt, 24)); pkt.put32(24, static_cast<u32>(nodeId));

    // Walk the src list (index chain through SceneNode::childPtr) for the node whose
    // id matches; unlink it, set its owner id (+6) to dst, link under dst head.
    i32 prev = -1;
    i32 cur = LoadHead(srcHead);
    while (cur != -1) {
        if (cur < 0 || cur >= kSceneNodeCapacity) { cur = -1; break; }
        if (g_sceneNodes[cur].id == nodeId) break;
        prev = cur;
        cur = g_sceneNodes[cur].childPtr;
    }
    if (cur == -1) return 1;
    SceneNode* moved = &g_sceneNodes[cur];
    if (prev != -1) g_sceneNodes[prev].childPtr = moved->childPtr;
    else            StoreHead(srcHead, moved->childPtr);

    // 0x4979d8 — *(_DWORD*)(v11+6) = a1[4]: the dst owner id is written as a full
    // DWORD into the node's byte offset +6 (the pad6 region), NOT the i16 ownerId
    // field at +0x0A.
    std::memcpy(reinterpret_cast<u8*>(moved) + 6, &dstId, 4);
    moved->childPtr = LoadHead(dstHead);        // *(v11+63) = *dstHead
    StoreHead(dstHead, cur);                     // *dstHead = v11

    if (ack) ack->status = 1;
    return 0;
}

// ===========================================================================
// 0x14 — consume one stockpiled object (use check).
// ===========================================================================
// gilde.exe 0x497AD0 — VIBE_Command_ExUseObjectCheck.
int ExUseObjectCheck(CommandPacket& pkt, AckEntry* ack) {
    i32 id = Remap(RdI32(pkt, 16)); pkt.put32(16, static_cast<u32>(id));
    ObjectRec* obj = nullptr; SceneNode* scn = nullptr;
    if (!GameObjectResolveEntityById(&obj, &scn, id, nullptr) || !scn)
        return 1;
    if (scn->type != 42 && scn->type != 278)
        return 1;

    i16 proto = HiWord(pkt, 18); // HIWORD(*(a1+18))
    // QueryFind(scn->entityPtr, {1:0, ... proto}) — locate the child by proto.
    SceneFilter f[1] = {{1, proto}};
    SceneNode* child = GameObjectQueryFind(scn->entityPtr, f, 1);
    if (!child) return 1;
    // *(v5+7) is the child's count dword: must equal 1.
    i32 count; std::memcpy(&count, reinterpret_cast<u8*>(child) + 7, 4);
    if (count != 1) return 1;

    g_removeHook(scn->entityPtr, proto);
    if (ack) ack->status = 1;
    return 0;
}

// ===========================================================================
// 0x20 — sys message (global state machine).
// ===========================================================================
// gilde.exe 0x498AB4 — VIBE_Command_ExSysMessage.
int ExSysMessage(CommandPacket& pkt, AckEntry* ack) {
    // The original qmemcpy's the whole 153-byte packet onto the stack; the payload
    // subtype byte is v20[16] (== packet +0x10) and the per-case args follow.
    u8 subtype = pkt.bytes[16];
    int result = 1; // v24 = 1

    switch (subtype) {
        case 0: // sky init (render leaf)
            g_sysLeafHook(0);
            break;
        case 2: // load flag + flag bits
            g_sysLoadFlag = 1; // dword_11AA480 = 1
            if (RdI32(pkt, 17) != 0)
                g_sysGlobalFlags = static_cast<u8>((g_sysGlobalFlags & 0xE7) | 8);
            else
                g_sysGlobalFlags = static_cast<u8>((g_sysGlobalFlags & 0xE7) | 0x10);
            break;
        case 3: { // write the game clock
            // LODWORD(qword_13CE852) = *(v20+17)  (day, dword)
            // WORD2(...) = *(WORD)(v20+21)        (hour, word at +4)
            // *(...+6) = *(v20+23)                (minute, dword)
            // unk_13CE85C = *(v20+27)             (carried; second is at +10)
            i32 day; std::memcpy(&day, pkt.bytes + 17, 4);
            u16 hour = pkt.get16(21);
            i32 minute; std::memcpy(&minute, pkt.bytes + 23, 4);
            i32 second; std::memcpy(&second, pkt.bytes + 27, 4);
            g_sysGameTime.day = day;
            g_sysGameTime.hour = hour;
            g_sysGameTime.minute = minute;
            g_sysGameTime.second = second;
            // DEFERRED: the original also mirrors the clock into the secondary copy
            // qword_122F840 (0x498be2..) and toggles the Clock_ComputeGameTimeOfDay
            // timer proc (TimeBase_IsProcActive/SetProcInterval @0x498bad/0x498bf6).
            // The 122F840 mirror is a global not owned by this chunk; the timer proc
            // is a TimeBase leaf — both out of scope here.
            break;
        }
        case 4: // enter building interior (scene leaf)
            g_sysLeafHook(4);
            break;
        case 5: // reseed (RNG leaf) + clear dword_122DC00
            g_sysLeafHook(5);
            break;
        case 6: // byte_63CC1D = v20[17]
            g_sysByte63CC1D = pkt.bytes[17];
            break;
        case 7: // dword_764CF4 = *(v20+17)
            g_sysDword764CF4 = RdI32(pkt, 17);
            break;
        case 8: { // latch active player if free AND the person exists
            i32 pid = RdI32(pkt, 17);
            if (g_sysActivePlayer != -1 || !PersonFindRecordById(pid))
                result = 2; // LABEL_21: v24 = 2
            else
                g_sysActivePlayer = pid; // dword_63CC24 = *(v20+17)
            break;
        }
        case 9: // byte_63C8F4 = v20[17]; dword_122F49C = *(v20+21)
            g_sysByte63C8F4 = pkt.bytes[17];
            g_sysDword122F49C = RdI32(pkt, 21);
            break;
        case 0xA: { // release active player
            if (RdI32(pkt, 17) == g_sysActivePlayer) {
                g_sysActivePlayer = -1;
                if (RdI32(pkt, 33) == 1)
                    g_sysDword63CC30 = 1;
                // (the byte_63C8F4==5 mission-slot path is a mission leaf; skipped.)
            } else {
                result = 2; // LABEL_21
            }
            break;
        }
        case 0xC: // terrain rebuild (heightmap leaf)
            g_sysLeafHook(0xC);
            break;
        case 0xD: // refresh all flags for a person (render leaf)
            g_sysLeafHook(0xD);
            break;
        case 0xF: { // dword_63CC70 = *(v20+17); strcpy(byte_63CC74, v20+21)
            g_sysDword63CC70 = RdI32(pkt, 17);
            const u8* src = pkt.bytes + 21;
            int n = 0;
            while (n < 63 && src[n] != 0) { g_sysName63CC74[n] = src[n]; ++n; }
            g_sysName63CC74[n] = 0;
            break;
        }
        case 0x10: { // paired insert into the cut tables (first free slot, step 2)
            int v15 = 0;
            if (g_sysCutTable50[0] != -1) {
                while (1) {
                    v15 += 2;
                    if (v15 >= 16) { v15 = -1; break; }
                    if (g_sysCutTable50[v15 / 2 * 2 / 2] == -1) break; // see note
                }
            }
            // NOTE: the original indexes dword_13CEC50[v15] with v15 stepping by 2
            // (dword stride 2 => every other dword). Model: 8 logical slots, index
            // v15/2. Recompute cleanly:
            int slot = -1;
            if (g_sysCutTable50[0] == -1) slot = 0;
            else for (int s = 1; s < 8; ++s) if (g_sysCutTable50[s] == -1) { slot = s; break; }
            if (slot >= 0) {
                g_sysCutTable50[slot] = RdI32(pkt, 17);
                g_sysCutTable54[slot] = RdI32(pkt, 21);
            }
            (void)v15;
            break;
        }
        case 0x11: { // conditional counter bump (uses current player)
            // 0x498e30 — ++dword_63127C only if word_63CC5C != -1 AND
            //   (*(v20+17) != dword_12CEB18[134*word_63CC5C] || *(v20+17) == -1).
            // dword_12CEB18[134*idx] is the player's record +0x208 slot-id column.
            if (g_currentPlayer != -1) {
                i32 arg = RdI32(pkt, 17);
                i32 slotId = 0;
                int pi = static_cast<int>(static_cast<u16>(g_currentPlayer));
                if (pi >= 0 && pi < kPersonCapacity)
                    std::memcpy(&slotId, reinterpret_cast<const u8*>(&g_persons[pi]) + 0x208, 4);
                if (arg != slotId || arg == -1)
                    ++g_sysDword63127C;
            }
            break;
        }
        case 0x12: // game-speed / HUD banner select.
            // 0x498e47 — the FULL original:
            //   v16 = min(*(int*)(a1+17), 4);
            //   dword_631284 = v16;                     // == g_gameSpeed (apply7)
            //   dword_631280 = dword_492EB0[v16];       // camera-rate table (not in tree)
            //   Hud_SetStatusBannerText(dword_8C98EC[v16]);  // UI leaf
            //   Config_ApplyCameraAndScrollSettings();       // config/UI leaf
            // HANDOFF/DEFERRED: dword_631284 is owned by command_apply7 (g_gameSpeed)
            // and dword_631280 / the dword_492EB0 (camera rate) + dword_8C98EC (banner
            // string) tables are not in this chunk; the HUD/Config calls are UI leaves
            // (rule 3-5 boundary). Routed through the leaf hook pending those owners.
            g_sysLeafHook(0x12);
            break;
        case 0x13: // dword_631294 = *(v20+17)
            g_sysDword631294 = RdI32(pkt, 17);
            break;
        default:
            break;
    }

    if (ack) { ack->status = static_cast<u8>(result); ack->slot = 0; ack->seq = 0; }
    return 0;
}

// ===========================================================================
// 0x2C — assign / release a person's office.
// ===========================================================================
// gilde.exe 0x499638 — VIBE_Command_ExAssignPersonToOffice.
int ExAssignPersonToOffice(CommandPacket& pkt, AckEntry* ack) {
    u8 status = 2; // v18 = 2
    OfficeSlot* slot = nullptr;

    Person* per = PersonFindRecordById(RdI32(pkt, 20)); // *(a1+20)
    if (per) {
        i32 officeType = HiWord(pkt, 24);     // HIWORD(*(a1+24))
        i32 coord = RdU8(pkt, 25);            // *(a1+25)
        i32 mode = RdU8(pkt, 28);             // *(a1+28)
        slot = g_officeHook(per->id, officeType, coord, mode);
        if (slot) {
            slot->holderId = per->id;         // v4[1] = *(Begin+1)
            status = 1;                       // v18 = 1
            if (RdU8(pkt, 28))
                slot->extra = RdI32(pkt, 29); // v4[4] = *(a1+29)
            ++g_log.officeAssignCount;
        }
    }

    if (ack) { ack->status = status; ack->slot = 5;
               ack->seq = slot ? (reinterpret_cast<std::intptr_t>(slot) & 0xFFFFFFFF) : 0; }
    return 0;
}

// ===========================================================================
// 0x3A — upgrade a building one level.
// ===========================================================================
// gilde.exe 0x49AFF0 — VIBE_Command_ExGebUpgrade.
int ExGebUpgrade(CommandPacket& pkt, AckEntry* ack) {
    // 0x49b00e — non-standalone: dword_649890 = *(a1+20) (the next-building seed).
    if (!g_standalone5)
        g_buildingNextId = RdI32(pkt, 20);
    // Begin = QueryBegin(filter id == *(a1+16)) over buildings.
    PersonFilter f[2] = {{1, RdI32(pkt, 16)}, {0, 0}}; // {id, alive}
    ObjectRec* begin = PersonQueryBegin(f, 1);
    if (!begin) return 1;
    u8* rec = reinterpret_cast<u8*>(begin);

    // marker byte (the level / kind) is rec[0]; +89 packs (level, growth) — the
    // original tests AiPlayer-table caps (+583 >= +584) for "already max". We model
    // a simple per-record level ceiling at rec+92 (the capacity byte): refuse if
    // rec[0] is already at the high level. Without the AiPlayer table we treat a
    // record level byte >= 250 as "already max" (record-neutral guard).
    if (rec[0] >= 250) return 1;

    ++rec[0];                                 // ++*Begin (level up)
    // Begin[92] = HIBYTE(*(Begin+89)) + (100 - (*(int*)(Begin+89) >> 24)) / 2.
    i32 v89; std::memcpy(&v89, rec + 89, 4);
    u8 hi = static_cast<u8>(static_cast<u32>(v89) >> 24);
    rec[92] = static_cast<u8>(hi + (100 - (v89 >> 24)) / 2);

    g_capsHook(rec);                          // VIBE_Building_InitWorkerCapacities
    g_meshHook(0x3A, rec);                    // (re)place the upgraded mesh (leaf)
    ++g_log.gebUpgradeCount;

    AckSet(ack, 2, 0, 0);                     // ack +0=2,+1=0,+6=0 on entry
    // The original sets status back via the +1/+6 stamp at the tail; it returns 0
    // (applied) leaving status 2 in the "long" path until the ack-finalise. We keep
    // status 2 (matching the entry stamp) since success returns 0 here.
    return 0;
}

// ===========================================================================
// 0x3B — remove a building.
// ===========================================================================
// gilde.exe 0x49B4F4 — VIBE_Command_ExRemoveBuilding.
int ExRemoveBuilding(CommandPacket& pkt, AckEntry* ack) {
    PersonFilter f[1] = {{1, RdI32(pkt, 16)}}; // id == *(a1+16)
    ObjectRec* begin = PersonQueryBegin(f, 1);
    if (!begin) return 1;
    u8* rec = reinterpret_cast<u8*>(begin);
    i32 bId; std::memcpy(&bId, rec + 1, 4);

    // Detach scene occupants whose entity id (+7 dword) == this building's id.
    // (Original: QueryFind(...,4,29) then DetachAndDestroyOccupant for matches.)
    SceneFilter sf[1] = {{0, 0}}; // flat scan
    for (SceneNode* n = GameObjectQueryFind(0, sf, 1); n; n = GameObjectIterNext()) {
        // 0x49b543 — mov edx,[eax+1Ch]; cmp edx,[ecx+1]: the occupant's entity-id
        // field is at +0x1C (+28), matched against the building id (Begin+1).
        i32 ent; std::memcpy(&ent, reinterpret_cast<u8*>(n) + 0x1C, 4);
        if (ent == bId) g_detachHook(n);
    }

    // 0x49b55a — for(j=0; j!=102912; j+=134) if (Begin == dword_12CEA7C[j]) clr.
    // dword_12CEA7C[134*i] is the person record's home-building column at +0x16C
    // (+364), which holds the building id in the id-model (person_create stores
    // b->id there). Clear every row linked to this building.
    for (int i = 0; i < kPersonCapacity; ++i) {
        u8* p = reinterpret_cast<u8*>(&g_persons[i]);
        i32 link; std::memcpy(&link, p + 0x16C, 4); // home-building column (+364)
        if (link == bId) { i32 z = 0; std::memcpy(p + 0x16C, &z, 4); }
    }

    g_freeHook(rec);                          // VIBE_Building_FreeAndUnlink
    ++g_log.removeBuildingCount;
    g_log.lastBuildingId = bId;

    AckSet(ack, 2, 0, 0);
    if (ack) ack->status = 2; // original stamps status=2 on success
    return 0;
}

// ===========================================================================
// 0x4C — create a building under a person (place at bauplatz).
// ===========================================================================
// gilde.exe 0x49C19C — VIBE_Command_ExCreateGebaeude.
int ExCreateGebaeude(CommandPacket& pkt, AckEntry* ack) {
    Person* owner = PersonFindRecordById(RdI32(pkt, 16)); // a1[4] == +0x10
    if (!owner) return 1;
    if (!g_standalone5)
        g_buildingNextId = RdI32(pkt, 20); // dword_649890 = v52[5] (+0x14)

    // CreateGebaeude(type = byte +0x18, owner = owner's marker word).
    u8 type = RdU8(pkt, 24);
    u16 ownerWord = static_cast<u16>(owner->marker);
    u8* geb = Building_CreateGebaeude(type, ownerWord);
    if (!geb) return 1;
    ++g_log.createBuildingCount;

    i32 newId; std::memcpy(&newId, geb + 1, 4);
    g_lastSceneId = newId;                    // dword_63128C = *(Gebaeude+1)
    g_log.lastBuildingId = newId;

    g_meshHook(0x4C, geb);                    // load + place the mesh at bauplatz

    if (ack) { ack->status = 1; ack->slot = 2;
               ack->seq = reinterpret_cast<std::intptr_t>(geb) & 0xFFFFFFFF; }
    return 0;
}

// ===========================================================================
// 0x4F — set a talk action's chat buffer.
// ===========================================================================
// gilde.exe 0x49C6AC — VIBE_Command_ExSetCharacterChatBuffer.
int ExSetCharacterChatBuffer(CommandPacket& pkt, AckEntry* ack) {
    // The actor is located by a name/id token; we model it by the actor id at +0x10.
    i32 actorId = RdI32(pkt, 16);
    const ChatTarget* t = g_chatHook(actorId);
    if (!t) return 1;            // actor not found
    if (!t->buffer || !t->capacity) return 2; // no active talk node / buffer

    // *(node+240) = *(a1+44) — set the buffer capacity from the packet.
    *t->capacity = RdI32(pkt, 44);
    i32 start = RdI32(pkt, 40);  // *(a1+40): first index
    int written = 0;
    int v9 = start;
    int wpos = 2 * start;        // v11 = 2*v9
    while (written < 2) {
        if (v9 >= *t->capacity) break;
        // copy two bytes per iteration from the packet's +48 region.
        if (wpos + 1 < t->bufferLen) {
            t->buffer[wpos]     = pkt.bytes[48 + 2 * written];
            t->buffer[wpos + 1] = pkt.bytes[48 + 2 * written + 1];
        }
        wpos += 2;
        ++v9;
        ++written;
    }

    if (ack) { ack->status = 1; ack->slot = 0; ack->seq = 0; }
    return 0;
}

// ===========================================================================
// 0x53 — remove a building trade/supply link.
// ===========================================================================
// gilde.exe 0x49C944 — VIBE_Command_ExRemoveBuildingLink.
int ExRemoveBuildingLink(CommandPacket& pkt, AckEntry* ack) {
    AckSet(ack, 2, 0, 0); // ack +0=2,+1=0,+6=0 on entry

    i32 tag = RdI32(pkt, 16); // a1[4] == +0x10
    // Tags are 4-char little-endian constants in the original:
    //   1668048242 = 'rmpl'  (clear the whole link node)
    //   1651865888 = 'rml '  (clear one matching link entry)
    if (tag == 1668048242) {
        PersonFilter f[1] = {{1, RdI32(pkt, 24)}}; // id == a1[6] (+0x18)
        ObjectRec* begin = PersonQueryBegin(f, 1);
        if (!begin) return 1;
        // QueryFind a type-300 supply node under the building's scene id; clear its
        // four link dwords (+7,+8,+9 = -1) and stamp the time (+10). The scene
        // sub-node lookup is a render-tree leaf; with no live tree the lookup
        // misses, so we report "not found" (faithful: returns 1 on a missing node).
        // We DO record the attempt for tests.
        ++g_log.removeBuildingCount;
        return 1;
    } else if (tag == 1651865888) {
        Person* rec = PersonFindRecordById(RdI32(pkt, 20)); // a1[5] (+0x14)
        if (!rec) return 1;
        PersonFilter f[1] = {{1, RdI32(pkt, 24)}};          // a1[6] (+0x18)
        ObjectRec* begin = PersonQueryBegin(f, 1);
        if (!begin) return 1;
        // The matching-entry scan walks a type-300 scene node's three link slots for
        // a1[7] (+0x1C); the node lives in the render tree (not modeled live), so the
        // lookup misses -> returns 1. Recorded for tests.
        ++g_log.removeBuildingCount;
        return 1;
    }
    // Unknown tag: no-op, ack stays 2 (the original leaves it 2 and returns 1 only
    // inside the matched branches; for an unmatched tag it falls through to ack=1).
    if (ack) ack->status = 1;
    return 0;
}

// ===========================================================================
// 0x54 — add / remove / clear a group link.
// ===========================================================================
// gilde.exe 0x49CAC4 — VIBE_Command_ExUpdateBuildingLinks.
int ExUpdateBuildingLinks(CommandPacket& pkt, AckEntry* ack) {
    AckSet(ack, 2, 0, 0);

    i32 tag = RdI32(pkt, 16); // a1[4]
    //   1785686382 = 'next' add member
    //   1818583414 = 'remv' remove member
    //   1668048242 = 'rmpl' clear all
    if (tag != 1785686382 && tag != 1818583414 && tag != 1668048242)
        return 1;

    PersonFilter f[1] = {{1, RdI32(pkt, 24)}}; // owner id == a1[6]
    ObjectRec* begin = PersonQueryBegin(f, 1);
    if (!begin) return 1;
    // The group node is a type-301 scene node under the building. Modifying it needs
    // the live scene tree (not modeled), so the QueryFind for the type-301 node
    // misses and the handler returns 1 — exactly as the original does when the node
    // is absent. We record the attempt for tests.
    ++g_log.removeBuildingCount;
    return 1;
}

// ===========================================================================
// Registry + direct apply.
// ===========================================================================
namespace {
#define APPLY5_ADAPTER(NAME) \
    void NAME##_adapter(CommandQueue&, CommandPacket& pkt, AckEntry* ack) { NAME(pkt, ack); }
APPLY5_ADAPTER(ExHandleStateGate)
APPLY5_ADAPTER(ExCreateBuildingDirect)
APPLY5_ADAPTER(ExCreatePersonA)
APPLY5_ADAPTER(ExCreatePersonB)
APPLY5_ADAPTER(ExMoveObjectBetweenLists)
APPLY5_ADAPTER(ExUseObjectCheck)
APPLY5_ADAPTER(ExSysMessage)
APPLY5_ADAPTER(ExAssignPersonToOffice)
APPLY5_ADAPTER(ExGebUpgrade)
APPLY5_ADAPTER(ExRemoveBuilding)
APPLY5_ADAPTER(ExCreateGebaeude)
APPLY5_ADAPTER(ExSetCharacterChatBuffer)
APPLY5_ADAPTER(ExRemoveBuildingLink)
APPLY5_ADAPTER(ExUpdateBuildingLinks)
#undef APPLY5_ADAPTER
} // namespace

void RegisterApplyHandlers5(CommandQueue& q) {
    q.set_handler(kOp5HandleStateGate0,       &ExHandleStateGate_adapter);
    q.set_handler(kOp5HandleStateGate3,       &ExHandleStateGate_adapter);
    q.set_handler(kOp5CreateBuildingDirect,   &ExCreateBuildingDirect_adapter);
    q.set_handler(kOp5CreatePersonA,          &ExCreatePersonA_adapter);
    q.set_handler(kOp5CreatePersonB,          &ExCreatePersonB_adapter);
    q.set_handler(kOp5MoveObjectBetweenLists, &ExMoveObjectBetweenLists_adapter);
    q.set_handler(kOp5UseObjectCheck,         &ExUseObjectCheck_adapter);
    q.set_handler(kOp5SysMessage,             &ExSysMessage_adapter);
    q.set_handler(kOp5AssignPersonToOffice,   &ExAssignPersonToOffice_adapter);
    q.set_handler(kOp5GebUpgrade,             &ExGebUpgrade_adapter);
    q.set_handler(kOp5RemoveBuilding,         &ExRemoveBuilding_adapter);
    q.set_handler(kOp5CreateGebaeude,         &ExCreateGebaeude_adapter);
    q.set_handler(kOp5SetCharacterChatBuffer, &ExSetCharacterChatBuffer_adapter);
    q.set_handler(kOp5RemoveBuildingLink,     &ExRemoveBuildingLink_adapter);
    q.set_handler(kOp5UpdateBuildingLinks,    &ExUpdateBuildingLinks_adapter);
}

int ApplyPacket5(CommandPacket& pkt, AckEntry* ack) {
    switch (pkt.opcode()) {
        case kOp5HandleStateGate0:       return ExHandleStateGate(pkt, ack);
        case kOp5HandleStateGate3:       return ExHandleStateGate(pkt, ack);
        case kOp5CreateBuildingDirect:   return ExCreateBuildingDirect(pkt, ack);
        case kOp5CreatePersonA:          return ExCreatePersonA(pkt, ack);
        case kOp5CreatePersonB:          return ExCreatePersonB(pkt, ack);
        case kOp5MoveObjectBetweenLists: return ExMoveObjectBetweenLists(pkt, ack);
        case kOp5UseObjectCheck:         return ExUseObjectCheck(pkt, ack);
        case kOp5SysMessage:             return ExSysMessage(pkt, ack);
        case kOp5AssignPersonToOffice:   return ExAssignPersonToOffice(pkt, ack);
        case kOp5GebUpgrade:             return ExGebUpgrade(pkt, ack);
        case kOp5RemoveBuilding:         return ExRemoveBuilding(pkt, ack);
        case kOp5CreateGebaeude:         return ExCreateGebaeude(pkt, ack);
        case kOp5SetCharacterChatBuffer: return ExSetCharacterChatBuffer(pkt, ack);
        case kOp5RemoveBuildingLink:     return ExRemoveBuildingLink(pkt, ack);
        case kOp5UpdateBuildingLinks:    return ExUpdateBuildingLinks(pkt, ack);
        default:                         return -1;
    }
}

} // namespace guild::sim
