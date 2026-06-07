// character_path — see character_path.h. Faithful 1:1 ports of the untranslated
// VIBE_Character_* path / physics / AI leaf functions.
#include "sim/character_path.h"
#include "sim/character_query.h"   // LiveActor, g_live, kLiveCapacity

#include <cstdlib>
#include <cstring>
#include <cmath>

namespace guild::sim {

// ===========================================================================
// Inert default hooks (defined in THIS library .cpp so any src/ reference to a
// callee resolves in the unified build). Tests install their own.
// ===========================================================================
namespace {
void* DefAllocDebug(unsigned size, const char*) { return std::malloc(size); }
void  DefFreeDebug(void* p) { std::free(p); }
void  DefClearRecord(int value, int count, void* base) {
    if (base) std::memset(base, value, static_cast<unsigned>(count));
}
void  DefReportError(const char*) {}

int  DefHeFindFirst(int, int, int) { return 0; }
int  DefHeFindNext() { return 0; }

int    DefRandomModulo(u16) { return 0; }
int    DefComputeOfficeRank(u16, int) { return 0; }
double DefComputeFavorability(int, int, int) { return 0.0; }
bool   DefVectorWithinTolerance(const float*, const float*, float) { return false; }

void DefBeginDeltaPacket(void*, int) {}
void DefAppendDeltaField(unsigned, unsigned, const void*, unsigned) {}
void DefAppendRawField(unsigned, unsigned, const void*, unsigned) {}
void DefQueueRequestState22() {}
void DefQueueRequestCoord27(int, int, int) {}
void DefHistoryA(void*, void*) {}
void DefHistoryB(void*, void*) {}
void DefHistoryFound(void*, void*) {}
void* DefPersonFindRecordById(int) { return nullptr; }

int  DefWorldToTile(int, const float*, int*, float*) { return 0; }
void DefFindNearestWalkable(int, int col, int* oc, int row, int* orow) {
    if (oc) *oc = col;
    if (orow) *orow = row;
}
int  DefTileToWorld(int, int, float*, int) { return 0; }
void DefTraceLineOfSight(int, const float*, int fromRow, const float*, int toRow,
                         int* oc, int* orow, int, int) {
    if (oc) *oc = fromRow;
    if (orow) *orow = toRow;
}
u16  DefBuildWaypointList(int*, int, int, int, int) { return 0xFFFF; }
int  DefResamplePolyline(const int*, void*, int) { return 0; }
int  DefResolveMesh(int) { return 0; }

CharacterPathHooks MakeDefaults() {
    CharacterPathHooks h{};
    h.allocDebug = DefAllocDebug;
    h.freeDebug = DefFreeDebug;
    h.clearRecord = DefClearRecord;
    h.reportError = DefReportError;
    h.heFindFirst = DefHeFindFirst;
    h.heFindNext = DefHeFindNext;
    h.heActiveTally = 0;
    h.heTurnMultiplier = 0;
    h.heTurnByte = 0;
    h.randomModulo = DefRandomModulo;
    h.computeOfficeRank = DefComputeOfficeRank;
    h.computeFavorability = DefComputeFavorability;
    h.vectorWithinTolerance = DefVectorWithinTolerance;
    h.beginDeltaPacket = DefBeginDeltaPacket;
    h.appendDeltaField = DefAppendDeltaField;
    h.appendRawField = DefAppendRawField;
    h.queueRequestState22 = DefQueueRequestState22;
    h.queueRequestCoord27 = DefQueueRequestCoord27;
    h.historyNotifyTargetReachedA = DefHistoryA;
    h.historyNotifyTargetReachedB = DefHistoryB;
    h.historyNotifyTargetFound = DefHistoryFound;
    h.personFindRecordById = DefPersonFindRecordById;
    h.worldToTileWithHeight = DefWorldToTile;
    h.findNearestWalkableTile = DefFindNearestWalkable;
    h.tileToWorld = DefTileToWorld;
    h.traceLineOfSight = DefTraceLineOfSight;
    h.buildWaypointList = DefBuildWaypointList;
    h.resamplePolyline = DefResamplePolyline;
    h.resolveMesh = DefResolveMesh;
    return h;
}

CharacterPathHooks g_hooks = MakeDefaults();
}  // namespace

CharacterPathHooks CharacterPathSetHooks(const CharacterPathHooks* hooks) {
    CharacterPathHooks prev = g_hooks;
    g_hooks = hooks ? *hooks : MakeDefaults();
    return prev;
}
const CharacterPathHooks& CharacterPathGetHooks() { return g_hooks; }

// ===========================================================================
// Slot-table leaves. The original treats a record as a raw 0x204-byte block whose
// dword[0] holds its slot index; we reuse the real g_live[512] table and the
// LiveActor view (LiveActor.slotIndex aliases record[0]).
// ===========================================================================

// gilde.exe 0x402254 — VIBE_Character_AllocSlot.
LiveActor* AllocSlot() {
    void* rec = g_hooks.allocDebug(kCharRecordSize, "Ch_CreateCharacter");
    g_hooks.clearRecord(0, 516, rec);                       // 0x402272
    // Scan g_live for the first free (null) slot. The original's loop reads
    // dword_66F0D4[v4] (slot v4+1) while v4 counts from 0 and stops on the first
    // null OR at 512; the net effect is "first index whose slot is null".
    int idx = 0;
    if (g_live[0]) {                                        // 0x402281
        while (idx < kLiveCapacity && g_live[idx]) ++idx;   // 0x402288..0x402296
    }
    if (idx >= kLiveCapacity) {                             // 0x40229e
        g_hooks.freeDebug(rec);                             // 0x4022b2
        g_hooks.reportError("Ch_CreateTooMany");            // 0x4022bc
        return nullptr;                                     // 0x4022c1
    }
    g_live[idx] = static_cast<LiveActor*>(rec);             // 0x4022a0
    g_live[idx]->slotIndex = idx;                           // 0x4022a9
    return g_live[idx];                                     // 0x4022a7
}

// gilde.exe 0x4022c8 — VIBE_Character_AllocSlotAtIndex.
LiveActor* AllocSlotAtIndex(int index) {
    void* rec = g_hooks.allocDebug(kCharRecordSize, "Ch_CreateCharacter"); // 0x4022dd
    g_hooks.clearRecord(0, 516, rec);                       // 0x4022e6
    if (g_live[index])                                      // 0x4022f2
        return nullptr;                                     // 0x4022fd (record leaked, as original)
    g_live[index] = static_cast<LiveActor*>(rec);           // 0x402304
    g_live[index]->slotIndex = index;                       // 0x40230a
    return g_live[index];                                   // 0x402302
}

// gilde.exe 0x406e68 — VIBE_Character_FindNearbyWide.
int FindNearbyWide(LiveActor* self, LiveActor** outArray) {
    int count = 0;                                          // v3
    int out = 0;                                            // v5 (byte stride 4)
    int i = 0;                                              // v4
    do {
        LiveActor* a = g_live[i];                           // 0x406e7b
        if (a && a != self) {                               // 0x406e83 / 0x406e87
            // a1[34]==v7[34] (universe ptr +136), a1[11]==v7[11] (universeId +44),
            // and the mesh cull gate (+533) must not both be open.
            // *(_BYTE*)(v7[13]+533) != 1  ||  (v10=v7[123]) != 0 && *(_BYTE*)(v10+533) != 1
            //   v7[13] = mesh (+52), v7[123] = lowPoly (+492). Cull byte at +533.
            bool meshOk = false;
            if (a->mesh && a->mesh->cullGate != 1) {        // 0x406f0c
                meshOk = true;
            } else if (a->lowPoly && a->lowPoly->cullGate != 1) {
                meshOk = true;
            }
            if (a->universe == self->universe &&            // 0x406f0c
                a->universeId == self->universeId &&
                meshOk) {
                if (self->mesh && a->mesh &&
                    g_hooks.vectorWithinTolerance(self->mesh->pos, a->mesh->pos,
                                                  kNearbyWideTol)) {  // 0x406ec4
                    ++count;                                // 0x406ed2
                    outArray[out] = a;                      // 0x406ede
                    ++out;                                  // 0x406edb (v5 += 4)
                }
            }
        }
        ++i;                                                // 0x406ee0
    } while (i < kLiveCapacity && out < kNearbyWideMax);    // 0x406eee
    return count;                                           // 0x406ef2
}

// gilde.exe 0x4073f0 — VIBE_Character_CreateMapNode.
MapNode* CreateMapNode(int payload) {
    MapNode* node = static_cast<MapNode*>(g_hooks.allocDebug(0x14, "Ch_Map")); // 0x4073fb
    node->payload = payload;                                // 0x407404 (*((_DWORD*)result+4))
    return node;                                            // 0x407408
}

// ===========================================================================
// Per-turn AI leaves.
// ===========================================================================

// gilde.exe 0x4525fc — VIBE_Character_CountActiveByTurn.
int CountActiveByTurn() {
    if (g_hooks.heFindFirst(1, 0, 11)) {                    // 0x452605
        while (g_hooks.heFindNext())                        // 0x45261b
            ;
    }
    // return v0 + dword_62EB98 * (u8)byte_63CC1D — v0 is the tally edx left by the
    // He walk (surfaced through heActiveTally).
    return g_hooks.heActiveTally +
           g_hooks.heTurnMultiplier * static_cast<unsigned char>(g_hooks.heTurnByte); // 0x452638
}

// flt chain at 0x45277e: (10.0 - rankDelta*0.5) * 0.1.
float NearestTargetRankWeight(int rankDelta) {
    return (kRankCurveBase - static_cast<double>(rankDelta) * kRankDeltaScale) *
           kRankCurveScale;
}

// 0x452917..0x452947: start = |dist|/3 + 1, then clamp into
// [2*debugSpeed+15, 2*debugSpeed+35], returned as an unsigned byte.
int NearestTargetWalkSteps(int dist, int debugSpeed) {
    int hi = 2 * debugSpeed + 35;
    int lo = 2 * debugSpeed + 15;
    int v = static_cast<unsigned char>(std::abs(dist) / 3 + 1);  // v30[0]
    if (v >= hi) v = hi;                                          // 0x452920
    int out;
    if (v <= lo) {                                               // 0x452932
        out = static_cast<unsigned char>(lo);                    // 0x452a0f
    } else {
        out = hi;                                                // 0x45293a
        if (v < hi) out = v;                                     // 0x452945
    }
    return static_cast<unsigned char>(out);                      // 0x452947 stored as byte
}

// ===========================================================================
// Path tile clamp shared by the two path builders.
//   coord < 1            -> 1
//   coord > gridDim - 2  -> gridDim - 2
// ===========================================================================
int ClampTileCoord(int coord, int gridDim) {
    if (coord >= 1) {                                       // 0x408865 / 0x408dae
        if (gridDim - 2 < coord)                            // 0x4089eb / 0x408ecc
            coord = gridDim - 2;
    } else {
        coord = 1;                                          // 0x40886b / 0x408db4
    }
    return coord;
}

// ===========================================================================
// gilde.exe 0x4062c0 — VIBE_Character_WaitSlotCallback.
// slotRec is the int-view of the a2 record: slotRec[0..31] is the queue, slotRec
// dword at byte +128 (== slotRec[32]) is the queue count.
// ===========================================================================
bool WaitSlotCallback(int actorId, int* slotRec, const unsigned char* waitTemplate,
                      int (*slotCallback)(int entry, const unsigned char* rec)) {
    unsigned char buf[960];
    std::memcpy(buf, waitTemplate, 0x3C0u);                 // 0x4062db
    int v4 = 0;                                             // 0x4062df
    // while ( !buf[v4] || !callback(v4, &buf[v4]) )
    while (!buf[v4] || !slotCallback(v4, &buf[v4])) {       // 0x406315
        v4 += 64;                                           // 0x4062e9
        if (v4 >= 960)                                      // 0x4062f2
            return slotRec[32] < 32;                        // 0x406307 (*(a2+128) < 32)
    }
    int count = slotRec[32];                                // v6 = *(a2+128)  0x406317
    slotRec[32] = count + 1;                                // 0x406325
    slotRec[count] = actorId;                               // *(4*v6 + a2) = a1  0x40632b
    return slotRec[32] < 32;                                // 0x4062e1
}

// ===========================================================================
// gilde.exe 0x406344 — VIBE_Character_PickWaitAnimation.
// ===========================================================================
int PickWaitAnimation(int (*collect)(int* out, int cap), unsigned (*randNext)()) {
    int buf[32];                                            // v5[32]
    std::memset(buf, 0, sizeof(buf));                       // SetGrayColorThunk(0,132,v5)
    int count = collect(buf, 256);                          // WalkAndInvoke -> v6
    if (!count)                                             // 0x40637c
        return 0;                                           // 0x40639e
    if (!static_cast<unsigned short>(count))                // 0x40638a  (!(_WORD)v6)
        return buf[0];                                      // 0x406391
    unsigned r = randNext();                                // 0x4063a2
    // v5[(u16)((int)v3 % v4)] — v4 is (u16)count; index is the unsigned-16 result.
    unsigned short denom = static_cast<unsigned short>(count);
    unsigned short idx = static_cast<unsigned short>(static_cast<int>(r) % denom);
    return buf[idx];                                        // 0x40639c
}

// ===========================================================================
// gilde.exe 0x4087f4 / 0x408c4c shared path-endpoint resolution. Probe both world
// points to tiles, clamp, then build the waypoint list. Mirrors 0x408855..0x408935.
// ===========================================================================
u16 ResolvePathEndpoints(int grid, int gridDim, const float* startWorld,
                         const float* goalWorld, int mode,
                         int* startTile, int* goalTile, int* outLen) {
    float h = 0.0f;
    int startCol = 0, goalCol = 0;
    // !WorldToTile(start) || !WorldToTile(goal) -> fail (0xFFFF).  0x408855
    if (!g_hooks.worldToTileWithHeight(grid, startWorld, &startCol, &h) ||
        !g_hooks.worldToTileWithHeight(grid, goalWorld, &goalCol, &h)) {
        if (startTile) *startTile = startCol;
        if (goalTile)  *goalTile = goalCol;
        return 0xFFFF;
    }
    startCol = ClampTileCoord(startCol, gridDim);           // 0x408865
    goalCol  = ClampTileCoord(goalCol, gridDim);            // 0x408879
    if (startTile) *startTile = startCol;
    if (goalTile)  *goalTile = goalCol;
    return g_hooks.buildWaypointList(outLen, grid, startCol, goalCol, mode); // 0x408935
}

} // namespace guild::sim
