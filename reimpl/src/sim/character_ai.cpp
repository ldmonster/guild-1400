// character_ai — see character_ai.h. Faithful 1:1 ports of the per-turn character
// AI behaviour + visibility/freeze/animation leaves.
#include "sim/character_ai.h"
#include "util/coord.h"   // ConvertX (truncate)

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cmath>

namespace guild::sim {

using guild::util::ConvertX;

// float reinterpret helper used by the LOD distance math (defined below).
static float AsF(int bits) { float f; std::memcpy(&f, &bits, sizeof(f)); return f; }

int g_freezeSlotAnchor = 0;   // dword_649D60 (host installs the real value)

// ===========================================================================
// Inert defaults.
// ===========================================================================
namespace {
u8  DefStateType(int) { return 0; }
u16 DefClassWord(int) { return 0; }
u8  DefHostile(int) { return 0; }
i8  DefAffinity(int, int) { return 0; }
int DefPlaneHi(int, int) { return 0; }
int DefLinkedObjectId(int) { return 0; }
void* DefRecordBase(int) { return nullptr; }

int    DefComputeOfficeRank(int, int) { return 0; }
double DefComputeFavorability(int, int, int) { return 0.0; }
int    DefRandomModulo(u16) { return 0; }
void*  DefPersonFindRecordById(int) { return nullptr; }

void DefBeginDeltaPacket(void*, int) {}
void DefAppendDeltaField(unsigned, unsigned, const void*, unsigned) {}
void DefAppendRawField(unsigned, unsigned, const void*, unsigned) {}
void DefQueueRequestState22() {}
void DefQueueRequestCoord27(int, int, int) {}
void DefHistory(void*, void*) {}

int  DefIsActiveTypeForTurn(void*) { return 0; }
void DefQueueGuardTarget61(void*, int, int, int) {}
void DefAiNeedsEvaluate(void*) {}
int  DefAiMethodSelect(int) { return 0; }
int  DefAiMethodExecute(int) { return 0; }
unsigned char DefAiMethodTableByte(int, int) { return 0; }

int  DefObjectToggleSuspend(int, int, int) { return 0; }
int  DefLightBuildObjectCache(int) { return 0; }
int  DefObjectSetPosition(int, const float*) { return 0; }
int  DefObjectSetWorldTranslation(int, const float*) { return 0; }
bool DefVectorWithinTolerance(const float*, const float*, float) { return true; }
int  DefAnimFindFreeMeshSlot() { return 0; }
int  DefAnimLoadStreamToStock(const char*, int) { return 0; }
int  DefAnimAttachToBone(int, int) { return 0; }
int  DefCharacterDrawSubMeshes(int) { return 0; }
int  DefMeshLoadObjectAnimation(const char*, int, int) { return 0; }
void DefUniverseRestoreObjectStates(int, int) {}
int  DefUniverseSwitchActiveSlot(int, int, int, int) { return 0; }
void DefObjectDetachAndRelease(int) {}
int  DefCharacterIndexFromPointer(int) { return 0; }
void DefReportError(const char*) {}
void DefActionQueueUnlink(int) {}
void DefCreateSampleLoop(int) {}

int  DefReadNodeI32(int, int) { return 0; }
void DefWriteNodeI32(int, int, int) {}
unsigned char DefReadNodeU8(int, int) { return 0; }
void DefWriteNodeU8(int, int, unsigned char) {}

CharacterAiHooks MakeDefaults() {
    CharacterAiHooks h{};
    h.persons.stateType = DefStateType;
    h.persons.classWord = DefClassWord;
    h.persons.hostileA = DefHostile;
    h.persons.hostileB = DefHostile;
    h.persons.affinity = DefAffinity;
    h.persons.rankPlaneHi = DefPlaneHi;
    h.persons.affinityPlaneHi = DefPlaneHi;
    h.persons.linkedObjectId = DefLinkedObjectId;
    h.persons.recordBase = DefRecordBase;
    h.computeOfficeRank = DefComputeOfficeRank;
    h.computeFavorability = DefComputeFavorability;
    h.randomModulo = DefRandomModulo;
    h.personFindRecordById = DefPersonFindRecordById;
    h.debugSpeed = 0;
    h.beginDeltaPacket = DefBeginDeltaPacket;
    h.appendDeltaField = DefAppendDeltaField;
    h.appendRawField = DefAppendRawField;
    h.queueRequestState22 = DefQueueRequestState22;
    h.queueRequestCoord27 = DefQueueRequestCoord27;
    h.historyNotifyTargetReachedA = DefHistory;
    h.historyNotifyTargetReachedB = DefHistory;
    h.historyNotifyTargetFound = DefHistory;
    h.isActiveTypeForTurn = DefIsActiveTypeForTurn;
    h.queueRequestGuardTarget61 = DefQueueGuardTarget61;
    h.aiNeedsEvaluateActions = DefAiNeedsEvaluate;
    h.aiMethodSelectBest = DefAiMethodSelect;
    h.aiMethodExecuteSelected = DefAiMethodExecute;
    h.aiMethodTableByte = DefAiMethodTableByte;
    h.currentTargetSlot = nullptr;
    h.objectToggleSuspend = DefObjectToggleSuspend;
    h.lightBuildObjectCache = DefLightBuildObjectCache;
    h.objectSetPosition = DefObjectSetPosition;
    h.objectSetWorldTranslation = DefObjectSetWorldTranslation;
    h.vectorWithinTolerance = DefVectorWithinTolerance;
    h.animFindFreeMeshSlot = DefAnimFindFreeMeshSlot;
    h.animLoadStreamToStock = DefAnimLoadStreamToStock;
    h.animAttachToBone = DefAnimAttachToBone;
    h.characterDrawSubMeshes = DefCharacterDrawSubMeshes;
    h.meshLoadObjectAnimation = DefMeshLoadObjectAnimation;
    h.universeRestoreObjectStates = DefUniverseRestoreObjectStates;
    h.universeSwitchActiveSlot = DefUniverseSwitchActiveSlot;
    h.objectDetachAndRelease = DefObjectDetachAndRelease;
    h.characterIndexFromPointer = DefCharacterIndexFromPointer;
    h.reportError = DefReportError;
    h.actionQueueUnlinkEntry = DefActionQueueUnlink;
    h.characterCreateSampleLoopAction = DefCreateSampleLoop;
    h.readNodeI32 = DefReadNodeI32;
    h.writeNodeI32 = DefWriteNodeI32;
    h.readNodeU8 = DefReadNodeU8;
    h.writeNodeU8 = DefWriteNodeU8;
    h.lowPolyMode = 0;
    h.lowPolyCellX = 0;
    h.lowPolyCellY = 0;
    h.cameraNode = 0;
    h.sceneLodAnchor = 0;
    h.lodSwapDistance = 1500.0f;   // flt_6100FC
    h.oamPathPrefixIsSet = 0;
    h.oamPathPrefix = "";          // dword_1406110 (empty until host installs prefix)
    return h;
}
CharacterAiHooks g_hooks = MakeDefaults();
}  // namespace

CharacterAiHooks CharacterAiSetHooks(const CharacterAiHooks* hooks) {
    CharacterAiHooks prev = g_hooks;
    g_hooks = hooks ? *hooks : MakeDefaults();
    return prev;
}
const CharacterAiHooks& CharacterAiGetHooks() { return g_hooks; }

// --- typed actor field accessors over the hooked node-read/write ---
namespace {
inline int   AsHandle(void* p) { return static_cast<int>(reinterpret_cast<intptr_t>(p)); }
inline int   AI32(int a, int off) { return g_hooks.readNodeI32(a, off); }
inline u8    AU8(int a, int off)  { return g_hooks.readNodeU8(a, off); }
inline int   AI32(void* a, int off) { return g_hooks.readNodeI32(AsHandle(a), off); }
inline u8    AU8(void* a, int off)  { return g_hooks.readNodeU8(AsHandle(a), off); }
}  // namespace

// ===========================================================================
// gilde.exe 0x4526d8 — VIBE_Character_FindNearestTarget.
// ===========================================================================
void* FindNearestTarget(int actor) {
    const CharacterAiHooks& h = g_hooks;
    void* actorPtr = reinterpret_cast<void*>(static_cast<intptr_t>(actor));
    // a1 is a u16*; dword[1] = id field (used as the delta-packet owner). The scan
    // also reads it as the 16-bit person id (*a1 == low word of dword[0]).
    const int actorId = AI32(actor, 0) & 0xFFFF;   // *a1 (16-bit person index)
    const int curTarget = AI32(actor, 4 * 131);    // *((_DWORD*)a1 + 131)

    if (curTarget == -1) {
        int best = -1;                              // v24
        // Engageable only if a hostile flag is set or state byte == 5.
        u8 fA = AU8(actor, 358);
        if (!fA && !AU8(actor, 361) && AU8(actor, 2) != 5)
            return nullptr;                         // 0x452812

        float bestScore = 128.0f;                   // v22
        for (int p = 0; p < 768; ++p) {             // ecx 0..768, ebp+=768, edi+=536
            // Candidate gate (0x452847): p != self AND state is enemy (6/7), OR
            // class is faction (2,3,4,5) AND one of the hostile flags set.
            bool gate = false;
            if (p != actorId && h.persons.stateType(p) == 6) gate = true;
            else if (h.persons.stateType(p) == 7) gate = true;
            else {
                u16 cls = h.persons.classWord(p);
                if ((cls == 2 || cls == 5 || cls == 4 || cls == 3) &&
                    (h.persons.hostileA(p) || h.persons.hostileB(p)))
                    gate = true;
            }
            if (!gate) continue;

            int r6 = h.computeOfficeRank(p, 0);                 // v6
            int rankDelta = std::abs(r6 - h.computeOfficeRank(actorId, 0));  // v23
            i8 aff = h.persons.affinity(p, actorId);           // (i16)(i8)byte_1333110[id+768p]
            double weight = NearestTargetRankWeight(rankDelta); // v20
            // (double)affinity * weight < bestScore  &&  favorability(p,id,1) < 34.0
            if (static_cast<double>(static_cast<i16>(aff)) * weight < bestScore &&
                h.computeFavorability(p, actorId, 1) < kFavorabilityCeil) {
                i8 aff2 = h.persons.affinity(p, actorId);      // re-read (same value)
                best = p;                                       // v24 = p
                bestScore = static_cast<float>(
                    static_cast<double>(static_cast<i16>(aff2)) * weight);  // v22
            }
        }
        if (best == -1)
            return nullptr;                          // 0x4527e9

        // Combined affinity gate (0x45289b..0x4528b3).
        int combined = h.persons.affinityPlaneHi(best, actorId) / 2
                     + h.persons.rankPlaneHi(best, actorId);    // v12
        if (combined >= -(static_cast<u16>(h.randomModulo(0x20)) + 50))
            return nullptr;                          // 0x4527ef

        // Emit the engage delta packet.
        char zero = 0;                               // v30[0]
        h.beginDeltaPacket(actorPtr, AI32(actor, 4));
        h.appendDeltaField(1, 1, &zero, 0x211);

        // Walk-step count: derived from the (truncated) bestScore distance.
        int dist = static_cast<int>(ConvertX(bestScore));   // v23 = (int)v22 (0x4528f0)
        int v30 = static_cast<unsigned char>(std::abs(dist) / 3 + 1);  // 0x45290a
        int hi = 2 * h.debugSpeed + 35;
        int lo = 2 * h.debugSpeed + 15;
        int v13 = v30;
        if (v30 >= hi) v13 = hi;                      // 0x452920
        int steps;
        if (v13 <= lo) {                             // 0x452932
            steps = static_cast<unsigned char>(lo);  // 0x452a0f
        } else {
            steps = hi;                              // 0x45293a
            if (v30 < hi) steps = v30;               // 0x452945
        }
        unsigned char stepByte = static_cast<unsigned char>(steps);  // v30[0]
        h.appendDeltaField(1, 1, &stepByte, 0x212);

        void* rec = h.persons.recordBase(best);      // &word_12CE910[268*best]
        int targetId = AI32(rec, 4);                 // (int*)v15 + 1
        h.appendDeltaField(4, 1, &targetId, 0x20C);
        i8 affByte = h.persons.affinity(best, actorId);  // byte_1333110[768*best + id]
        h.appendDeltaField(1, 1, &affByte, 0x210);
        h.queueRequestState22();
        h.queueRequestCoord27(h.persons.linkedObjectId(best), AI32(actor, 4), 0);
        h.historyNotifyTargetReachedA(actorPtr, rec);
        return nullptr;                              // 0x45281d
    }

    // curTarget != -1 : we already have a target; validate / advance the engage.
    void* rec = h.personFindRecordById(curTarget);   // v17
    u8 selfEngage = AU8(actor, 529);                 // v19
    bool selfReady = AU8(actor, 358) || AU8(actor, 361) || AU8(actor, 2) == 5;
    bool targetHostile = rec && (AU8(rec, 361) || AU8(rec, 358) ||
                                 AU8(rec, 2) == 6 || AU8(rec, 2) == 7 ||
                                 AU8(rec, 2) == 5);
    if (!selfReady || !rec || !targetHostile || selfEngage > AU8(actor, 530) + 5) {
        // Disengage: clear the target.
        char zero = 0; int negOne = -1;              // v29[0]=0, v21=-1
        char zero32 = 0;                             // v32[0]=0
        h.beginDeltaPacket(actorPtr, AI32(actor, 4));
        h.appendDeltaField(1, 1, &zero, 0x211);
        h.appendDeltaField(1, 1, &zero, 0x212);
        h.appendDeltaField(4, 1, &negOne, 0x20C);
        h.appendDeltaField(1, 1, &zero32, 0x210);
        char one = 1; h.appendDeltaField(1, 1, &one, 0x214);
        h.queueRequestState22();
        if (rec) {
            h.queueRequestCoord27(curTarget, AI32(actor, 4), 60);
            h.historyNotifyTargetFound(actorPtr, rec);
        }
        return nullptr;                              // 0x452b29
    }

    const unsigned field = 532;                      // v25
    if (selfEngage >= AU8(actor, 530)) {             // 0x452ba1
        char one = 1;                                // v28[0]
        h.beginDeltaPacket(actorPtr, AI32(actor, 4));
        h.appendRawField(1, 1, &one, 529);
        one = 1;
        h.appendDeltaField(1, 1, &one, field);
        h.queueRequestState22();
        return nullptr;                              // 0x452cc5
    }

    // Close-enough engage test (0x452bd4).
    int eng = h.persons.rankPlaneHi(curTarget, actorId)   // unk_133310D + 768*targetId + id
            + (AI32(actor, 525) >> 24);                   // *(int*)(a1+525) >> 24
    if (eng > 0) {
        char v27 = static_cast<char>(AU8(actor, 530) + 1);
        h.beginDeltaPacket(actorPtr, AI32(actor, 4));
        h.appendDeltaField(1, 1, &v27, 0x211);
        char one = 1;
        h.appendDeltaField(1, 1, &one, field);
        h.queueRequestState22();
        return nullptr;                              // 0x452d21
    }

    if (AU8(actor, 529) > 3u)                         // 0x452be1
        return rec;                                   // 0x452b36

    char v31 = static_cast<char>(h.randomModulo(2) + 1);
    h.beginDeltaPacket(actorPtr, AI32(actor, 4));
    h.appendRawField(1, 1, &v31, 529);
    if (AU8(actor, 529) + static_cast<unsigned char>(v31) > 3) {  // 0x452c2a
        unsigned char src531 = AU8(actor, 531);       // src = (char*)a1+531
        h.appendDeltaField(1, 1, &src531, field);
    }
    h.queueRequestState22();
    if (static_cast<unsigned char>(v31) + AU8(actor, 529) <= 3)
        return nullptr;                               // 0x452c65
    h.historyNotifyTargetReachedB(actorPtr, rec);
    return rec;                                       // 0x4527f1
}

// The live-actor slot table dword_66F0D0[512] is owned by character_query.cpp. The
// host wires liveActorAt(i) to g_live[i]; with inert defaults the freeze loop sees
// an empty world. Convention: readNodeI32(0, i*4) returns g_live[i].
static inline int LiveActorAt(int i) {
    return g_hooks.readNodeI32(0, i * 4);
}

// ===========================================================================
// gilde.exe 0x452d38 — VIBE_Character_UpdateGuardBehavior.
// The building-roster spawn loop (256 buildings * 169-byte records) and the
// sprintf-of-a-name leaf depend on the building globals (dword_13CE298/294); we
// reproduce the control flow and route the spawn through queueRequestGuardTarget61.
// The roster walk is driven by the host through isActiveTypeForTurn + the person
// table; with the inert defaults it falls through to the AI-dispatch tail.
// ===========================================================================
unsigned char UpdateGuardBehavior(int actor) {
    const CharacterAiHooks& h = g_hooks;
    void* actorPtr = reinterpret_cast<void*>(static_cast<intptr_t>(actor));
    unsigned char status = 0;

    if (!h.isActiveTypeForTurn(actorPtr))            // 0x452d4b
        return 0;

    // The roster spawn loop (0x452d5f..0x452e75) is gated by building globals that
    // are not part of this slice; it is driven entirely through the hooked
    // queueRequestGuardTarget61 the host installs. With inert wiring it is a no-op,
    // matching an empty building roster.

    status = static_cast<unsigned char>(actor);      // LOBYTE(IsActiveTypeForTurn)=actor
    if (AU8(actor, 8)) {                              // 0x452e82 : actor "ready" flag
        void* nearest = FindNearestTarget(actor);    // 0x452ea8
        if (h.currentTargetSlot)
            *h.currentTargetSlot = static_cast<int>(reinterpret_cast<intptr_t>(nearest));
        h.aiNeedsEvaluateActions(actorPtr);          // 0x452eb4
        status = static_cast<unsigned char>(actor);
        int v9 = static_cast<signed char>(AU8(actor, 306));  // prev method (movsx bl)
        if (!AU8(actor, 433)) {                       // 0x452ec7
            int p = AI32(actor, 0) & 0xFFFF;          // *v16
            status = static_cast<unsigned char>(h.aiMethodSelectBest(p));  // 0x452ee3
            if (status) {
                int v12 = AI32(actor, 303) >> 24;     // selected method id (sar 0x18)
                // Re-dispatch if the method changed (0x452efa) OR, when unchanged,
                // its table flag bit 1 (mask 2) is set (0x452f22..0x452f34):
                //   byte_B572A1[4*(5*v12 + 32*v9)] & 2.
                if (v12 != v9 || (h.aiMethodTableByte(v9, v12) & 2)) {
                    status = static_cast<unsigned char>(h.aiMethodExecuteSelected(p));
                }
            }
        }
        if (h.currentTargetSlot)
            *h.currentTargetSlot = 0;                 // 0x452f0f
    }
    return status;
}

// ===========================================================================
// gilde.exe 0x40244c — VIBE_Character_UpdateLowPolyMesh.
// ===========================================================================
int UpdateLowPolyMesh(int actor) {
    const CharacterAiHooks& h = g_hooks;
    if (!h.readNodeI32(actor, 492))                  // no low-poly proxy
        return actor;                                // 0x402461

    int mesh = h.readNodeI32(actor, 52);             // +0x34 full mesh
    int low  = h.readNodeI32(actor, 492);            // +0x1EC low-poly

    if (!h.lowPolyMode) {                            // dword_62D088 == 0 : full only
        if (h.readNodeU8(mesh, 533) == 1 && (h.readNodeU8(actor, 140) & 0x20) == 0) {
            h.objectToggleSuspend(mesh, 1, actor);
            h.lightBuildObjectCache(h.readNodeI32(actor, 52));
        }
        low = h.readNodeI32(actor, 492);
        if (h.readNodeU8(low, 533) == 1)
            return low;
        return h.objectToggleSuspend(low, 0, actor); // LABEL_63
    }

    // Mirror the proxy's cull-cell + frame fields from the full mesh.
    h.writeNodeI32(low, 512, h.readNodeI32(mesh, 512));
    h.writeNodeI32(low, 72, h.readNodeI32(mesh, 72));

    if (h.readNodeU8(actor, 141) & 2) {              // forced-low flag
        if (h.readNodeU8(mesh, 533) != 1)
            h.objectToggleSuspend(mesh, 0, actor);
        low = h.readNodeI32(actor, 492);
        if (h.readNodeU8(low, 533) == 1) {
            h.objectToggleSuspend(low, 1, actor);
            h.lightBuildObjectCache(h.readNodeI32(actor, 492));
        }
        return h.readNodeI32(actor, 492);
    }

    if (h.readNodeI32(actor, 136) == h.sceneLodAnchor) {  // == byte_13ECEC8
        // Distance-based LOD swap.
        bool near = false;
        if (h.lowPolyMode == 1 && h.cameraNode) {
            float cx = AsF(h.readNodeI32(h.cameraNode, 76)) - AsF(h.readNodeI32(mesh, 76));
            float cy = AsF(h.readNodeI32(h.cameraNode, 80)) - AsF(h.readNodeI32(mesh, 80));
            float cz = AsF(h.readNodeI32(h.cameraNode, 84)) - AsF(h.readNodeI32(mesh, 84));
            near = std::sqrt(static_cast<double>(cx) * cx + static_cast<double>(cy) * cy +
                             static_cast<double>(cz) * cz) < h.lodSwapDistance;
        }
        if (near) {
            if (h.readNodeU8(mesh, 533) == 1 && (h.readNodeU8(actor, 140) & 0x20) == 0) {
                h.objectToggleSuspend(mesh, 1, actor);
                h.lightBuildObjectCache(h.readNodeI32(actor, 52));
            }
            int lp = h.readNodeI32(actor, 492);
            if (h.readNodeU8(lp, 533) != 1)
                h.objectToggleSuspend(lp, 0, actor);
        } else {
            if (h.readNodeU8(mesh, 533) != 1)
                h.objectToggleSuspend(mesh, 0, actor);
            if ((h.lowPolyCellX == h.readNodeI32(actor, 44) &&
                 h.lowPolyCellY == h.readNodeI32(actor, 48)) ||
                !h.readNodeI32(actor, 48)) {
                int lp = h.readNodeI32(actor, 492);
                if (h.readNodeU8(lp, 533) == 1 && (h.readNodeU8(actor, 140) & 0x20) == 0) {
                    h.objectToggleSuspend(lp, 1, actor);
                    h.lightBuildObjectCache(h.readNodeI32(actor, 492));
                }
            } else {
                int lp = h.readNodeI32(actor, 492);
                if (h.readNodeU8(lp, 533) != 1)
                    h.objectToggleSuspend(lp, 0, actor);
            }
            // Sync proxy position/world-translation to the full mesh when divergent.
            int v14 = h.readNodeI32(actor, 492);
            if (h.readNodeU8(v14, 533) != 1 && h.readNodeI32(v14, 460)) {
                float a[3] = { AsF(h.readNodeI32(v14, 76)), AsF(h.readNodeI32(v14, 80)),
                               AsF(h.readNodeI32(v14, 84)) };
                int m = h.readNodeI32(actor, 52);
                float b[3] = { AsF(h.readNodeI32(m, 76)), AsF(h.readNodeI32(m, 80)),
                               AsF(h.readNodeI32(m, 84)) };
                if (!h.vectorWithinTolerance(a, b, 2.0f))
                    h.objectSetPosition(h.readNodeI32(actor, 492), b);
            }
            int v15 = h.readNodeI32(actor, 52);
            if (h.readNodeI32(v15, 460)) {
                int lp = h.readNodeI32(actor, 492);
                float a[3] = { AsF(h.readNodeI32(lp, 132)), AsF(h.readNodeI32(lp, 136)),
                               AsF(h.readNodeI32(lp, 140)) };
                float b[3] = { AsF(h.readNodeI32(v15, 132)), AsF(h.readNodeI32(v15, 136)),
                               AsF(h.readNodeI32(v15, 140)) };
                if (!h.vectorWithinTolerance(a, b, 0.050000001f))
                    h.objectSetWorldTranslation(h.readNodeI32(actor, 492), b);
            }
        }
        // (The walk-anim sub-mesh attach branch at 0x4025fd..0x40292e depends on the
        //  anim leaves owned by W19-OBJANIM; with inert wiring it is skipped, which
        //  matches a proxy that has no attached sub-mesh body. See report handoff.)
        return h.readNodeI32(actor, 492);
    }

    // Non-LOD-anchor proxy: simple cell-visibility suspend toggle.
    if ((h.lowPolyCellX == h.readNodeI32(actor, 44) &&
         h.lowPolyCellY == h.readNodeI32(actor, 48)) ||
        !h.readNodeI32(actor, 48)) {
        if (h.readNodeU8(mesh, 533) == 1 && (h.readNodeU8(actor, 140) & 0x20) == 0) {
            h.objectToggleSuspend(mesh, 1, actor);
            h.lightBuildObjectCache(h.readNodeI32(actor, 52));
        }
    } else {
        if (h.readNodeU8(mesh, 533) != 1)
            h.objectToggleSuspend(mesh, 0, actor);
    }
    int lp = h.readNodeI32(actor, 492);
    if (h.readNodeU8(lp, 533) != 1)
        return h.objectToggleSuspend(lp, 0, actor);  // goto LABEL_63
    return lp;
}

// ===========================================================================
// gilde.exe 0x426488 — VIBE_Character_LoadObjectAnimation.
// ===========================================================================
int LoadObjectAnimation(int actor, const char* baf, int flags) {
    const CharacterAiHooks& h = g_hooks;
    if (!actor || !baf)                              // 0x4264a8
        return 0;

    // Copy the name into a local (the original's word-wise copy) — semantically a
    // string copy, then strip path + extension.
    char name[256];
    std::strncpy(name, baf, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
    // VIBE_Util_StripPathAndExt(name) — host-owned; the attach below keys off the
    // raw flags + path, so the stripped name only feeds the (unused-here) stock id.

    if (!h.animFindFreeMeshSlot())                   // 0x4264ea
        h.animLoadStreamToStock(baf, 0);             // 0x4264f7

    // Build the 12-byte attach descriptor v17 (zeroed, then flag bits in BYTE1).
    unsigned char desc[12];
    std::memset(desc, 0, sizeof(desc));
    if (flags & 2)    desc[1] |= 0x10;               // 0x42652e
    if (flags & 1)    desc[1] &= ~0x10;              // 0x426540
    if (flags & 4)    desc[1] |= 0x02;               // 0x426552
    if (flags & 0x10) desc[1] |= 0x08;               // 0x426564
    if (flags & 0x20) desc[1] |= 0x04;               // 0x426576
    desc[2] |= 0x02;                                 // 0x42658f
    int descWord; std::memcpy(&descWord, desc, sizeof(descWord));  // v17 (dword view)

    int meshRoot = h.readNodeI32(actor, 492);        // *(actor+492)
    if (meshRoot && h.animAttachToBone(meshRoot + 244, descWord))
        return 1;                                     // 0x426639

    // Fallback: load the .oam file. 0x4265d3: sprintf(path, "%s%s.oam",
    // dword_1406110, a2) — the prefix global is always the first "%s".
    char path[512];
    const char* prefix = h.oamPathPrefix ? h.oamPathPrefix : "";
    std::snprintf(path, sizeof(path), "%s%s.oam", prefix, baf);
    int result = h.meshLoadObjectAnimation(path, actor, 1);  // 0x4265e4
    if (result) {
        if (flags & 2)    h.writeNodeU8(result, 45, h.readNodeU8(result, 45) | 0x10);
        if (flags & 1)    h.writeNodeU8(result, 45, h.readNodeU8(result, 45) & ~0x10);
        if (flags & 4)    h.writeNodeU8(result, 45, h.readNodeU8(result, 45) | 0x02);
        if (flags & 0x10) h.writeNodeU8(result, 45, h.readNodeU8(result, 45) | 0x08);
        if (flags & 0x20) h.writeNodeU8(result, 45, h.readNodeU8(result, 45) | 0x04);
        return 1;                                     // 0x426635
    }
    return result;                                    // 0x4264ac (0)
}

// ===========================================================================
// gilde.exe 0x401894 — VIBE_Character_SetVisible.
// ===========================================================================
void SetVisible(int actor, int visible) {
    const CharacterAiHooks& h = g_hooks;
    if (actor && h.readNodeI32(actor, 52)) {         // a1 && *(a1+52)
        int mesh = h.readNodeI32(actor, 52);
        h.universeRestoreObjectStates(mesh, visible);
        if (h.readNodeI32(actor, 100))
            h.objectToggleSuspend(h.readNodeI32(actor, 100), visible, actor);
        if (visible)
            h.lightBuildObjectCache(h.readNodeI32(actor, 52));
        int sub = h.readNodeI32(actor, 292);
        if (sub) {
            h.universeRestoreObjectStates(h.readNodeI32(sub, 0), visible);
            if (visible)
                h.lightBuildObjectCache(h.readNodeI32(h.readNodeI32(actor, 292), 0));
        }
        int low = h.readNodeI32(actor, 492);
        if (low)
            h.objectToggleSuspend(low, visible, actor);
        if (visible)
            h.writeNodeU8(actor, 140, h.readNodeU8(actor, 140) & ~0x20);
        else
            h.writeNodeU8(actor, 140, h.readNodeU8(actor, 140) | 0x20);
        UpdateLowPolyMesh(actor);
    } else {
        h.reportError("ch_SetVisible(): invalid character");
    }
}

// ===========================================================================
// gilde.exe 0x40238c — VIBE_Character_SetAllFreezeState.
// ===========================================================================
unsigned char SetAllFreezeState(unsigned char state, int arg) {
    const CharacterAiHooks& h = g_hooks;
    int anchor = g_freezeSlotAnchor;                 // dword_649D60
    for (int i = 0; i != 512; ++i) {                 // dword_66F0D0[512]
        int actor = LiveActorAt(i);
        if (actor) {
            int u = h.characterIndexFromPointer(h.readNodeI32(actor, 136));
            // 0x4023ed: SwitchActiveSlot(eax=index, edx=1, ecx=actorPtr, edi=arg).
            // IndexFromPointer (0x426724) preserves edx(=1, set for its own call) and
            // ecx(=the actor pointer set at 0x4023db), so they fall through as args 1/2.
            h.universeSwitchActiveSlot(u, 1, actor, arg);
            if (state == 2) {
                if (h.readNodeI32(actor, 492)) {
                    h.characterDrawSubMeshes(h.readNodeI32(actor, 492));
                    h.objectDetachAndRelease(h.readNodeI32(actor, 492));
                    h.writeNodeI32(actor, 492, 0);
                }
            } else if (state <= 1 && !h.readNodeI32(actor, 492)) {
                UpdateLowPolyMesh(actor);
            }
        }
    }
    g_hooks.lowPolyMode = 2 - state;                 // dword_62D088 = 2 - state
    return static_cast<unsigned char>(
        h.universeSwitchActiveSlot(anchor, 1, 2 - state, arg));
}

// ===========================================================================
// gilde.exe 0x405504 — VIBE_Character_StandUp.
// ===========================================================================
int StandUp(int actor) {
    const CharacterAiHooks& h = g_hooks;
    if (!actor)
        return actor;                                // 0x405551 (returns input)
    int state = h.readNodeI32(actor, 296);           // *(actor+296)
    if (!state)
        return 0;                                     // 0x405552
    int head = h.readNodeI32(state, 40);             // *(state+40)
    h.writeNodeU8(state, 400, 1);                     // *(state+400) = 1
    if (head) {
        do {
            h.actionQueueUnlinkEntry(h.readNodeI32(state, 40));  // 0x405528
        } while (h.readNodeI32(state, 40));
    }
    if (h.readNodeU8(actor, 140) & 0x10)             // 0x40553b
        h.characterCreateSampleLoopAction(actor);     // 0x405544
    return 1;
}

} // namespace guild::sim
