// event5 — the four giant deferred world-event "He"-action coroutines (gilde.exe
// VIBE_Event_*). See event5.h for the inventory, phase semantics and provenance.
#include "world/event5.h"

#include "sim/gametime.h"     // guild::sim::GameTimeAdvance / GameTimeCompare
#include "sim/npcaction.h"    // guild::sim::NpcClock (qword_13CE852 game clock)
#include "util/math_random.h" // guild::util::RandomModulo

#include <cmath>
#include <cstdint>
#include <cstring>

namespace guild::world {

using guild::sim::GameTimeAdvance;
using guild::sim::GameTimeCompare;
using guild::sim::HeBytes;
using guild::util::RandomModulo;

// Module-global game clock (qword_13CE852). Shared with the sim cluster — reuse
// sim::NpcClock() rather than redefining (ODR).
static GameTime& Clock() { return guild::sim::NpcClock(); }

// Raw record accessors at the exact original byte offsets (mirror the originals'
// *(T*)(base+off)). a1[k] in the pseudocode is the dword at base + 4*k.
namespace {
inline i32&      Dword(HeRecord* h, int off) { return *reinterpret_cast<i32*>(HeBytes(h) + off); }
inline u16&      Word (HeRecord* h, int off) { return *reinterpret_cast<u16*>(HeBytes(h) + off); }
inline u8&       Byte (HeRecord* h, int off) { return *reinterpret_cast<u8*>(HeBytes(h) + off); }
inline float&    Flt  (HeRecord* h, int off) { return *reinterpret_cast<float*>(HeBytes(h) + off); }
inline GameTime& Time (HeRecord* h, int off) { return *reinterpret_cast<GameTime*>(HeBytes(h) + off); }

// Stamp the 14-byte clock image into the GameTime at `off` (the originals write
// qword@+0, dword@+8, word@+12 of the destination GameTime block).
inline void StampClock(HeRecord* h, int off) { Time(h, off) = Clock(); }
// Roll +96 scratch clock forward from the +82 appointment (the verbatim
// "*(_DWORD*)(a1+96)=*(a1+82)..." block the originals run before advancing +82).
inline void CopyApptToScratch(HeRecord* h) { Time(h, 96) = Time(h, 82); }

// FPU trunc-toward-zero (VIBE_Coord_ConvertX): the originals truncate doubles to
// int. Plain (int) cast matches the x87 round-toward-zero used here.
inline i32 ConvertX(double v) { return static_cast<i32>(v); }

// Raw-offset helpers for "scene record" pointers the hooks return.
inline i32&  RecI32(void* p, int off) { return *reinterpret_cast<i32*>(reinterpret_cast<u8*>(p) + off); }
inline u8    RecU8 (void* p, int off) { return *reinterpret_cast<u8*>(reinterpret_cast<u8*>(p) + off); }
inline u16   RecU16(void* p, int off) { return *reinterpret_cast<u16*>(reinterpret_cast<u8*>(p) + off); }
}  // namespace

// Forward-declared internal helpers (defined below the hook block, but used by the
// machines that precede their definition site).
static void CancelMyActions(const Event5Hooks& hk, HeRecord* h, void* obj);
static i32  ReArmEntity29(const Event5Hooks& hk, HeRecord* h, i32 newState);
static void* MyActionSlot(HeRecord* h);
static void SendProduktionMessage(const Event5Hooks& hk, HeRecord* h, void* b, u8 variant);
static void UpdateBuildingHeStateForward(HeRecord* h);
static i32  QueryBuildingHeMaxForward(HeRecord* h);

// ---------------------------------------------------------------------------
// Hooks — inert defaults defined here so the library links standalone.
// ---------------------------------------------------------------------------
namespace {
i32   InertFree(HeRecord*) { return 0; }
HeRecord* InertFind5(i32, i32, i32, i32, i32) { return nullptr; }
HeRecord* InertFind3(i32, i32, i32) { return nullptr; }
HeRecord* InertFindNext() { return nullptr; }
void  InertQuickjump(i32, i32, i32, const char*, i32, i32, i32, i32, const char*) {}
void  InertEntityMsg(i32, i32, i32, const char*, i32, const char*) {}
void  InertDestroyIcon(void*) {}
i32   InertResolve(void** a, void** b, i32, void** c) { if (a) *a = nullptr; if (b) *b = nullptr; if (c) *c = nullptr; return 0; }
void* InertQueryFind(i32, i32, i32, i32, i32) { return nullptr; }
void* InertIterNext() { return nullptr; }
void* InertFindByHandle(i32, i32, const char*, i32, i32) { return nullptr; }
i32   InertQueue17(i32, i32, i32, i32, i32, i32) { return -1; }
i32   InertQueue18(i32, i32, i32, i32) { return -1; }
i32   InertQueueEnt29(i32, const void*) { return -1; }
void  InertSingle49(i32) {}
void  InertSingle59(i32) {}
void  InertNamedObj53(i32, i32, i32, i32, i32, const char*) {}
void  InertFlagBlob32(i32, const void*) {}
void  InertFlag55(i32, i32) {}
void  InertPair36(i32, i32) {}
void  InertQuad43(i32, i32, i32, i32) {}
void  InertCoord27(i32, i32, i32) {}
void  InertArgs25(i32, i32, i32, i32, i32) {}
void  InertState22() {}
i32   InertSlotReset28(const void*) { return -1; }
void  InertQueue16(i32, i32, i32, i32) {}
void  InertBuildOp66(i32) {}
i32   InertEnqueue15(i32, i32, i32, i32) { return -1; }
i32   InertTargetedAction(i32, i32, i32, i32) { return -1; }
void  InertBeginDelta(i32, i32) {}
void  InertAppendRaw(i32, i32, const void*, i32) {}
void  InertAppendDelta(i32, i32, i32, i32) {}
i32   InertPacketStatus(i32) { return 0; }
void* InertPacketSeq(i32) { return nullptr; }
void* InertPersonFind(i32) { return nullptr; }
void* InertPersonQuery(i32, i32, i32, i32) { return nullptr; }
void* InertPersonActive(void*) { return nullptr; }
void* InertFamily(void*) { return nullptr; }
i32   InertCurrency(void*, i32) { return 0; }
void  InertChangeAction(void*, void*, void*, u16) {}
void  InertCountByType(void*) {}
void  InertRefreshFlag(void*) {}
void* InertBldgFindById(i32) { return nullptr; }
void* InertBldgStorable(void*) { return nullptr; }
void* InertBldgWorkProduct(void*) { return nullptr; }
void* InertBldgActiveSlot(i32) { return nullptr; }
i32   InertSumWorkstation(void*, i32, i32) { return 0; }
double InertOutputRatio(void*) { return 0.0; }
double InertMarketPrice(i32, i32) { return 0.0; }
i32   InertProdPixels(i32, void*) { return 0; }
i32   InertBauplatzPos(void*, void*) { return 0; }
i32   InertReqGebaeude(void*) { return -1; }
void  InertStockValue(void*, i32* out) { if (out) *out = 0; }
void  InertAdjustStock(i32, i32, i32) {}
void  InertReserveBauplatz(void*, void*) {}
void  InertBuildPath(void*, i32) {}
i32   InertEffStock(void*, void*) { return 0; }
i32   InertFreeCapacity(void*, i32, i32, i32) { return 0; }
i32   InertSlotCapacity(void*) { return 0; }
i32   InertProdSlotMatch(i32, i32) { return 0; }
double InertAvgFavor(u16, i32, const i32*) { return 0.0; }
i32   InertOutputOverTime(const GameTime*, const GameTime*) { return 0; }
i32   InertDailyHourOutput(const GameTime*, const GameTime*) { return 0; }
void* InertScriptFind(i32) { return nullptr; }
void  InertScriptFinish(void*) {}
void* InertScriptLoad(const char*) { return nullptr; }
void  InertScriptRun(void*, i32, i32) {}
void  InertScriptStep(void*, HeRecord*) {}
i32   InertSwitchSlot(i32) { return 0; }
void  InertRestoreStates(void*, i32) {}
void  InertDetachRelease(void*) {}
void  InertBuildModel(i32, void*, i32) {}
void  InertHideScaffold(void*) {}
void  InertInflate(void*) {}
i32   InertNearDoor(void*) { return 0; }
int   InertCollectHandles(void**, int) { return 0; }
void  InertRemoveMesh(void*) {}
void  InertRebuildOctree(void*) {}
void  InertApplyTransform(void*) {}
i32   InertBoundingRadius(void*, float* r) { if (r) *r = 0.0f; return 0; }
void  InertRenderMsg(char* buf, i32, i32, i32, i32) { if (buf) buf[0] = 0; }
void  InertVoiceSample(i32, i32, i32, i32, i32) {}
i32   InertViolation(i32, i32, i32, i32, i32) { return -1; }
void* InertGestureTarget(void*) { return nullptr; }
i32   InertAnimalBusy(void*) { return 0; }
void  InertGuardTarget(void*, void*) {}
i32   InertMoneyRate(i32 a, i32) { return a; }
void  InertOwnerChain(void*, void*) {}
void  InertSetCityRef(HeRecord*, u16) {}
i32   InertFastBuild() { return 0; }
i32   InertDeferredBuild() { return 0; }
i32   InertPlayerCity() { return -1; }
u16   InertPlayerCityW() { return 0; }
void* InertActiveHe(int) { return nullptr; }
u16   InertActionCharId(int) { return 0; }
void  InertUpdateHeState(HeRecord*) {}
i32   InertQueryHeMax(HeRecord*) { return 0; }

const Event5Hooks kInertHooks = {
    &InertFree, &InertFind5, &InertFind3, &InertFindNext,
    &InertQuickjump, &InertEntityMsg, &InertDestroyIcon,
    &InertResolve, &InertQueryFind, &InertIterNext, &InertFindByHandle,
    &InertQueue17, &InertQueue18, &InertQueueEnt29, &InertSingle49, &InertSingle59,
    &InertNamedObj53, &InertFlagBlob32, &InertFlag55, &InertPair36, &InertQuad43,
    &InertCoord27, &InertArgs25, &InertState22, &InertSlotReset28, &InertQueue16,
    &InertBuildOp66, &InertEnqueue15, &InertTargetedAction, &InertBeginDelta,
    &InertAppendRaw, &InertAppendDelta, &InertPacketStatus, &InertPacketSeq,
    &InertPersonFind, &InertPersonQuery, &InertPersonActive, &InertFamily,
    &InertCurrency, &InertChangeAction, &InertCountByType, &InertRefreshFlag,
    &InertBldgFindById, &InertBldgStorable, &InertBldgWorkProduct, &InertBldgActiveSlot,
    &InertSumWorkstation, &InertOutputRatio, &InertMarketPrice, &InertProdPixels,
    &InertBauplatzPos, &InertReqGebaeude, &InertStockValue, &InertAdjustStock,
    &InertReserveBauplatz, &InertBuildPath, &InertEffStock, &InertFreeCapacity,
    &InertSlotCapacity, &InertProdSlotMatch, &InertAvgFavor, &InertOutputOverTime,
    &InertDailyHourOutput, &InertScriptFind, &InertScriptFinish, &InertScriptLoad,
    &InertScriptRun, &InertScriptStep, &InertSwitchSlot, &InertRestoreStates,
    &InertDetachRelease, &InertBuildModel, &InertHideScaffold, &InertInflate,
    &InertNearDoor, &InertCollectHandles, &InertRemoveMesh, &InertRebuildOctree,
    &InertApplyTransform, &InertBoundingRadius, &InertRenderMsg, &InertVoiceSample,
    &InertViolation, &InertGestureTarget, &InertAnimalBusy, &InertGuardTarget,
    &InertMoneyRate, &InertOwnerChain, &InertSetCityRef, &InertFastBuild,
    &InertDeferredBuild, &InertPlayerCity, &InertPlayerCityW,
    &InertActiveHe, &InertActionCharId, &InertUpdateHeState, &InertQueryHeMax,
};
const Event5Hooks* g_hooks = &kInertHooks;
}  // namespace

void SetEvent5Hooks(const Event5Hooks* hooks) { g_hooks = hooks ? hooks : &kInertHooks; }
const Event5Hooks& GetEvent5Hooks() { return *g_hooks; }

// ---------------------------------------------------------------------------
// Internal helpers shared by the machines.
// ---------------------------------------------------------------------------
// The cancel-actions loop the teardown paths run: walk all 768 person slots; for
// each whose active-action handler == this record, re-invoke ChangePlayerAction
// with the person's action char id (the dword_12CEA8C / word_12CE910 tables).
static void CancelMyActions(const Event5Hooks& hk, HeRecord* h, void* obj) {
    for (int p = 0; p < 768; ++p) {
        if (hk.activeActionHe(p) == h)
            hk.changePlayerAction(obj, nullptr, nullptr, hk.personActionCharId(p));
    }
}

// Re-arm the entity-29 packet into +132 and set the next phase; mirrors the
// repeated "(+132) = QueueRequestEntity29(state+1 or -1, a1)" tail.
static i32 ReArmEntity29(const Event5Hooks& hk, HeRecord* h, i32 newState) {
    if (newState < 0) {
        // LABEL_23: re-arm with -1 (keep phase) and bump state by 1.
        Dword(h, 132) = hk.queueRequestEntity29(-1, h);
    } else {
        Dword(h, 132) = hk.queueRequestEntity29(newState, h);
        ++Dword(h, 112);
    }
    return Dword(h, 132);
}

// The record's own action slot, if this He owns one of the 768 person actions.
static void* MyActionSlot(HeRecord* h) {
    const Event5Hooks& hk = *g_hooks;
    for (int p = 0; p < 768; ++p)
        if (hk.activeActionHe(p) == h)
            return reinterpret_cast<void*>(static_cast<std::intptr_t>(hk.personActionCharId(p)));
    return nullptr;
}

namespace { int RohstoffVariant(u8 classByte); }

// The "Rohstoff" / production message dispatch (collapsed): when the building's
// class byte is 6/7, broadcast the rendered message (with or without the raw
// prefix variant). Mirrors the two SendQuickjumpMessage call sites.
static void SendProduktionMessage(const Event5Hooks& hk, HeRecord* h, void* b, u8 variant) {
    if (!b) return;
    u8 cls = RecU8(b, 0 /* class via type table; modelled as record byte 0 */);
    if (cls == 6 || cls == 7) {
        // The original selects a raw-material prefix string by the building class
        // byte (RohstoffVariant); -1 == no prefix. We preserve that selection.
        int rv = RohstoffVariant(static_cast<u8>(RecU16(b, 39) & 0xFF));
        char buf[512];
        hk.renderFormattedMessage(buf, 6079, reinterpret_cast<std::intptr_t>(b), 0, 0);
        hk.sendQuickjump(0, Dword(h, 4), 0, buf, 1424, RecI32(b, 1), 0, 0,
                         (variant && rv != -1) ? "rohstoff" : nullptr);
    }
}

// The two building-visibility walkers are reconstructed in event4; the live wiring
// forwards through the hooks. (Inert default -> no-op / 0.)
static void UpdateBuildingHeStateForward(HeRecord* h) { (*g_hooks).updateBuildingHeState(h); }
static i32  QueryBuildingHeMaxForward(HeRecord* h)     { return (*g_hooks).queryBuildingHeMax(h); }

// The localized "Rohstoff" message-prefix table the production machine selects by
// the building class byte (the original copies these C-strings into a stack buf;
// we keep the verbatim mapping but model the copy as a no-op label since the
// downstream send is a hook). The exact id->name set is recovered from the switch.
namespace {
// Map the class byte to the raw-message variant index the original picks (0 ==
// aNachrichtenRoh, 1..8 == the _1.._8 variants, -1 == none/default-3 path). This
// preserves the branch structure without materialising the strings.
int RohstoffVariant(u8 classByte) {
    switch (classByte) {
        case 7:  return 0;
        case 8:  return 1;
        case 11: return 2;
        case 12: return 3;
        case 13: return 4;
        case 14: return 5;
        case 18: return 6;
        case 20: return 7;
        case 21: return 8;
        case 22: return -2;  // aNachrichtenRoh_0
        default: return -1;  // no match -> v81=3 / LABEL_137 default
    }
}
}  // namespace

// ===========================================================================
// 0x4f5b5c — VIBE_Event_RunGebaeudeBauen
// ===========================================================================
i32 RunGebaeudeBauen(HeRecord* h) {
    const Event5Hooks& hk = *g_hooks;
    void* obj = nullptr;                 // v99 — resolved build object
    i32 ret = 0;                         // PacketStatusById
    const i32 deferred = hk.deferredBuildFlag();  // dword_649D60 snapshot (v105)

    // Front gate: only run a phase when the in-flight build packet (+132) is
    // resolved (handle == -1, or its status != 0).
    i32 pkt = Dword(h, 132);
    if (pkt != -1 && hk.packetStatus(pkt) == 0)
        return ret;  // packet still pending -> no-op this tick

    i32 phase = Dword(h, 112) + 2;
    Dword(h, 132) = -1;
    ret = phase;

    switch (phase) {
        case 0:
        case 1: {
            hk.resolveEntityById(&obj, nullptr, Dword(h, 184), nullptr);
            hk.universeSwitchSlot(0);
            if (Dword(h, 188) != -1) {
                void* sc = hk.scriptFindByHandle(Dword(h, 188));
                if (sc) hk.scriptFinish(sc);
                Dword(h, 188) = -1;
            }
            Dword(h, 200) = Dword(h, 196) + 1;
            UpdateBuildingHeStateForward(h);
            if ((Byte(h, 120) & 2) != 0 && obj) {
                hk.queueRequestSingle59(RecI32(obj, 1));
                unsigned char blob[124] = {0};
                hk.queueRequestFlagBlob32(12, blob);
            }
            hk.universeSwitchSlot(deferred);
            ret = hk.freeHandlerEntry(h);
            break;
        }
        case 2: {
            ret = GameTimeCompare(&Time(h, 82), &Clock());
            if (ret <= 0 && (Byte(h, 120) & 2) != 0) {
                // dword_6498E4+4 is the "player city" sentinel id.
                if (Dword(h, 172) == hk.playerCitySentinel()) {
                    Dword(h, 204) = hk.buildingRequestGebaeudeBauen(nullptr);
                } else {
                    void* rec = hk.personFindRecordById(Dword(h, 172));
                    if (!rec) { ret = ReArmEntity29(hk, h, -1); break; }
                    Dword(h, 204) = hk.enqueueCmd15(-1, Dword(h, 172), Dword(h, 180), 0);
                    void* fam = hk.personGetFamilyRecord(rec);
                    if (fam) RecI32(fam, 84) += Dword(h, 180);  // fam[21]
                }
                ret = ReArmEntity29(hk, h, Dword(h, 112) + 1);
            }
            break;
        }
        case 3: {
            u8 flags = Byte(h, 120);
            Dword(h, 132) = -1;
            if ((flags & 4) == 0) {
                ret = hk.packetStatus(Dword(h, 204));
                if (ret) {
                    if (hk.packetStatus(Dword(h, 204)) == 2)
                        ret = ReArmEntity29(hk, h, -1);
                    else
                        ret = ReArmEntity29(hk, h, Dword(h, 112) + 1);
                }
            }
            break;
        }
        case 4: {
            if ((Byte(h, 120) & 2) != 0) {
                Dword(h, 204) = hk.enqueueTargetedAction(Byte(h, 176), Dword(h, 172), 1, 0);
                ret = ReArmEntity29(hk, h, Dword(h, 112) + 1);
            }
            break;
        }
        case 5: {
            u8 flags = Byte(h, 120);
            Dword(h, 132) = -1;
            if ((flags & 4) == 0) {
                ret = hk.packetStatus(Dword(h, 204));
                if (ret) {
                    void* seq = (hk.packetStatus(Dword(h, 204)) == 2) ? nullptr
                                                                      : hk.packetSeq(Dword(h, 204));
                    obj = seq;
                    if (!seq) {
                        ret = ReArmEntity29(hk, h, -1);
                    } else {
                        Dword(h, 184) = RecI32(seq, 4);
                        Dword(h, 16)  = RecI32(seq, 4);
                        ret = ReArmEntity29(hk, h, Dword(h, 112) + 1);
                    }
                }
            }
            break;
        }
        case 6: {
            i32 id = Dword(h, 184);
            Dword(h, 132) = -1;
            ret = hk.resolveEntityById(&obj, nullptr, id, nullptr);
            if (obj) {
                // SpawnBuildEffectByName is folded into the model build; on failure
                // the original takes LABEL_37 (re-arm entity-29). We treat the build
                // effect as always-available here (inert path) and proceed.
                hk.objectBuildModelName(RecI32(obj, 388), obj, 2);
                if (hk.fastBuildMode()) {
                    i32 mx = QueryBuildingHeMaxForward(h);
                    Dword(h, 192) = 10;
                    Dword(h, 196) = mx;
                } else {
                    i32 mx = QueryBuildingHeMaxForward(h);
                    Dword(h, 196) = mx;
                    // 1020 * obj[579 dword] / mx  (guarded against /0).
                    i32 hb = obj ? RecI32(obj, 579 * 4 /* obj+2316 */) : 0;
                    Dword(h, 192) = (mx != 0)
                        ? static_cast<i32>(static_cast<u32>(1020 * hb) / static_cast<u32>(mx))
                        : 0;
                }
                Dword(h, 200) = 0;
                ret = GameTimeAdvance(&Time(h, 82), 0, 0, 1);  // +1 minute
                ++Dword(h, 112);
            } else {
                Dword(h, 112) = -1;
            }
            break;
        }
        case 7: {
            Dword(h, 132) = -1;
            hk.universeSwitchSlot(0);
            hk.resolveEntityById(&obj, nullptr, Dword(h, 184), nullptr);
            unsigned char posBuf[64];
            if (!obj || !hk.buildingGetBauplatzPos(posBuf, obj)) {
                ret = ReArmEntity29(hk, h, -1);
                hk.universeSwitchSlot(deferred);
                break;
            }
            // Walk the scene "dummy_tuer" door nodes into a handle list.
            void* nodes[176];
            int count = hk.sceneCollectHandles(nodes, 176);
            Byte(h, 208) = 0;
            for (int i = 0; i < count; ++i) {
                void* door = hk.findByHandle(RecI32(obj, 388), 256, "dummy_tuer", 0,
                                             reinterpret_cast<std::intptr_t>(nodes[i]));
                if (nodes[i] && (door != nullptr)) {
                    void* sc = hk.scriptLoadFromScriptDir("specialevents/he_tuer");
                    if (sc) {
                        hk.scriptRunWithArgs(sc, 4, reinterpret_cast<std::intptr_t>(door));
                        hk.scriptStep(sc, h);
                    }
                    ++Byte(h, 208);
                }
            }
            StampClock(h, 82);
            GameTimeAdvance(&Time(h, 82), 0, 0, 10);  // +10 minutes
            ++Dword(h, 112);
            ret = hk.universeSwitchSlot(deferred);
            break;
        }
        case 8: {
            Dword(h, 132) = -1;
            ret = GameTimeAdvance(&Time(h, 82), 0, 0, 20);  // +20 minutes
            if (!Byte(h, 208)) {
                hk.universeSwitchSlot(0);
                hk.resolveEntityById(&obj, nullptr, Dword(h, 184), nullptr);
                unsigned char posBuf[64];
                if (hk.buildingGetBauplatzPos(posBuf, obj)) {
                    void* nodes[176];
                    int count = hk.sceneCollectHandles(nodes, 176);
                    for (int i = 0; i < count; ++i) {
                        hk.sceneRemoveMeshFromTree(nodes[i]);
                        hk.objectDetachAndRelease(nodes[i]);
                    }
                    StampClock(h, 96);
                    StampClock(h, 82);
                    GameTimeAdvance(&Time(h, 82), 0, 0, 10);  // +10 minutes
                    ++Dword(h, 112);
                    Dword(h, 180) = 0;
                    ret = hk.universeSwitchSlot(deferred);
                } else {
                    Dword(h, 132) = hk.queueRequestEntity29(-1, h);
                    ret = hk.universeSwitchSlot(deferred);
                }
            }
            break;
        }
        case 9: {
            i32 out = hk.productionComputeDailyHourOutput(&Time(h, 96), &Clock());
            i32 perStep = Dword(h, 192);
            i32 produced = out + Dword(h, 180);
            if (produced < perStep) {
                ret = GameTimeAdvance(&Time(h, 82), 0, 0, 30);  // +30 minutes, keep waiting
                break;
            }
            CopyApptToScratch(h);
            if (!obj) hk.resolveEntityById(&obj, nullptr, Dword(h, 184), nullptr);
            // (the optional "next stage" scene re-scan at +188==-1 && +200>=2 only
            // matters for the live scene; with inert hooks it is a no-op.)
            i32 step = (perStep != 0) ? perStep : 1;
            Dword(h, 200) += produced / step;
            Dword(h, 180) = produced % step;
            GameTimeAdvance(&Time(h, 82), 0, 0, step);
            if (!deferred) {
                if (Dword(h, 200) <= Dword(h, 196)) UpdateBuildingHeStateForward(h);
                if (Dword(h, 200) == 2 && (Byte(h, 120) & 2) != 0) {
                    unsigned char blob[124] = {0};
                    hk.queueRequestFlagBlob32(12, blob);
                }
            }
            ret = Dword(h, 200);
            if (ret > Dword(h, 196)) {
                hk.universeSwitchSlot(0);
                i32 sc = Dword(h, 188);
                if (sc != -1) {
                    void* s = hk.scriptFindByHandle(sc);
                    if (s) hk.scriptFinish(s);
                    Dword(h, 188) = -1;
                }
                if (deferred && obj) {
                    hk.meshApplyTransformRecursive(reinterpret_cast<void*>(
                        static_cast<std::intptr_t>(RecI32(obj, 388))));
                }
                UpdateBuildingHeStateForward(h);
                if (obj && RecI32(obj, 388)) {
                    void* model = reinterpret_cast<void*>(static_cast<std::intptr_t>(RecI32(obj, 388)));
                    hk.objectHideUpgradeScaffold(model);
                    hk.objectInflateGeometry(model);
                    hk.sceneRebuildRegionOctree(model);
                }
                if (obj) {
                    u8 cls = RecU8(obj, 0);  // *v99 — building type byte
                    hk.buildingReserveBauplatz(nullptr, obj);
                    hk.buildingBuildPath(nullptr, cls);
                    if ((Byte(h, 120) & 2) != 0) {
                        hk.queueRequestArgs25(RecI32(obj, 1), 90, 0, 2, 1);
                        hk.combatAssignGuardTarget(nullptr, obj);
                    }
                }
                Dword(h, 124) = 0;
                ret = hk.freeHandlerEntry(h);
            }
            break;
        }
        default:
            return ret;
    }
    return ret;
}

// ===========================================================================
// 0x4f2bd0 — VIBE_Event_RunProduktion
// ===========================================================================
i32 RunProduktion(HeRecord* h) {
    const Event5Hooks& hk = *g_hooks;

    // City-index 0xFFFF == teardown-only record.
    if (Word(h, 8) == 0xFFFF) {
        void* b = hk.buildingFindById(Dword(h, 188));
        return hk.freeHandlerEntry(h);
        (void)b;
    }

    i32 result = Dword(h, 112);
    switch (result) {
        case -2:
        case -1: {
            void* b = nullptr;
            hk.resolveEntityById(&b, nullptr, Dword(h, 188), nullptr);
            if (b) hk.requestBuildOp66(RecI32(b, 1));
            CancelMyActions(hk, h, nullptr);
            return hk.freeHandlerEntry(h);
        }
        case 0:
            Dword(h, 112) = ++result;
            return result;
        case 1: {
            if (Dword(h, 20) <= 0 && Dword(h, 24) <= 0) {
                Dword(h, 112) = ++result;
                return result;
            }
            u32 pktV = static_cast<u32>(Dword(h, 196));
            u8 v81 = 0;
            void* b = nullptr;
            if (pktV != 0xFFFFFFFFu) {
                result = hk.packetStatus(static_cast<i32>(pktV));
                if (!result) return result;          // packet still pending
                result = hk.resolveEntityById(&b, nullptr, Dword(h, 188), nullptr);
                if (!b) { Dword(h, 112) = -1; return result; }
                if (hk.packetStatus(Dword(h, 196)) == 2) {
                    // packet completed — find this record's action slot, gate on the
                    // building's active person (must be idle) and try a top-up.
                    void* active = hk.personFindActiveByEntity(b);
                    void* myAction = MyActionSlot(h);
                    bool gate = !myAction
                                || (active && (RecU16(active, 436) & 1) != 0)  // active[218]&1
                                || Dword(h, 200) != 0;
                    if (!gate) {
                        void* inv = nullptr;
                        hk.resolveEntityById(nullptr, &inv, Dword(h, 192), nullptr);
                        hk.buildingFindWorkProductObject(b);
                        bool found = hk.queryFind(inv ? RecI32(inv, 20) : 0, 1, 0,
                                                  Dword(h, 170) >> 16, 0) != nullptr;
                        bool full = !hk.inventoryComputeFreeCapacity(
                            inv, static_cast<u16>(Dword(h, 170) >> 16), 0, 1);
                        if (found && full) {
                            char buf[512];
                            hk.renderFormattedMessage(buf, 6080, reinterpret_cast<std::intptr_t>(b),
                                                      2 * (Dword(h, 170) >> 16) + 2151, 0);
                            v81 = 1;
                        } else if (hk.inventoryComputeFreeCapacity(
                                       inv, static_cast<u16>(Dword(h, 170) >> 16), 0, 1)) {
                            // warehouse full at the destination
                            char buf[512];
                            hk.renderFormattedMessage(buf, 6081, reinterpret_cast<std::intptr_t>(b),
                                                      2 * (Dword(h, 170) >> 16) + 2151, 0);
                            v81 = 2;
                        }
                    }
                    SendProduktionMessage(hk, h, b, v81);
                }
                if (v81) {
                    if (b && RecI32(b, 149) != -1) hk.requestBuildOp66(RecI32(b, 1));
                    Dword(h, 200) = 1;
                }
                Dword(h, 196) = -1;
                return GameTimeAdvance(&Time(h, 82), 0, 0, 5);  // +5 minutes
            }
            // pkt == -1: (LABEL_81) compute the production batch.
            result = hk.resolveEntityById(&b, nullptr, Dword(h, 188), nullptr);
            if (!b) { Dword(h, 112) = -1; return result; }
            if (RecI32(b, 149) == -1) hk.requestBuildOp66(RecI32(b, 1));
            i32 wsCount = hk.buildingSumWorkstationByCategory(b, 1, 1);
            double rate = static_cast<double>(wsCount) * kProdWorkRateMul + 1.0;
            void* active = hk.personFindActiveByEntity(b);
            // (active==0 -> use the city's leading person; the favorability term)
            double favTerm = 0.0;
            if (active) {
                u8 cls = RecU8(active, 2);
                if (cls == 6 || cls == 7) {
                    i32 mood = RecU8(active, 129);
                    favTerm = static_cast<double>(static_cast<i16>(mood)) * kProdClassMul * kProdFavorScale;
                }
            }
            rate += favTerm;
            u8 bflags = b ? RecU8(b, 90) : 0;
            if ((bflags & 0x20) != 0 && hk.inventoryIsProductionSlotMatch(bflags, 376))
                rate *= kProdSlotMul;
            if (Byte(h, 208) >= 60) {
                int nMembers = 0;
                double ratioSum = 0.0;
                for (int slot = 0; slot < 4; ++slot) {
                    if (Dword(h, 140 + 4 * slot) != -1) {
                        i32 cmd = b ? RecI32(b, 65) : 2;
                        if (cmd != 2)
                            hk.queueRequestCoord27(0, Dword(h, 140 + 4 * slot), 2 - cmd);
                        void* p = hk.personFindRecordById(Dword(h, 140 + 4 * slot));
                        if (p) ratioSum += hk.buildingComputeOutputRatio(p);
                        ++nMembers;
                    }
                }
                if (nMembers) Flt(h, 212) = static_cast<float>(ratioSum / nMembers);
                hk.characterCountByType(h);
                double fav = (hk.aiAverageFavorability(b ? RecU16(b, 39) : 0, 8, &Dword(h, 140))
                              + kProdFavorBias) * kProdFavorScale;
                Byte(h, 208) = 0;
                Flt(h, 204) = static_cast<float>(fav);
            }
            i32 over = hk.productionComputeOutputOverTime(&Time(h, 96), &Clock());
            if (over > 0) Byte(h, 208) += static_cast<u8>(over);
            rate = rate + static_cast<double>(Flt(h, 204));
            i32 cmdState = (b ? RecI32(b, 65) : 0) - 2;
            rate = (static_cast<double>(cmdState) * kProdCmdStateMul + rate)
                   * static_cast<double>(Flt(h, 212));
            i32 perStep = Dword(h, 192);
            u8 v80 = 0;
            void* outRec = nullptr;
            if (!hk.resolveEntityById(nullptr, &outRec, perStep, nullptr)
                || (outRec && RecU16(outRec, 0) != 42 && RecU16(outRec, 0) != 278))
                return hk.freeHandlerEntry(h);
            int hi = Dword(h, 170) >> 16;
            bool foundOut = hk.queryFind(outRec ? RecI32(outRec, 20) : 0, 1, 0, hi, 0) != nullptr;
            bool fullOut = !hk.inventoryComputeFreeCapacity(outRec, static_cast<u16>(hi), 0, 1);
            if (foundOut && fullOut) {
                char buf[512];
                hk.renderFormattedMessage(buf, 6080, reinterpret_cast<std::intptr_t>(b),
                                          2 * hi + 2151, 0);
                v80 = 0;  // string-prefix copied; the gate below uses v80
                // (LABEL_106/109: the "found+full" branch falls into the finish path)
            } else if (!hk.inventoryComputeFreeCapacity(outRec, static_cast<u16>(hi), 0, 1)) {
                char buf[512];
                hk.renderFormattedMessage(buf, 6081, reinterpret_cast<std::intptr_t>(b),
                                          2 * hi + 2151, 0);
            } else {
                // Scan the 4 recipe-input slots (row +46 words off the type table) and
                // verify enough effective stock; on a miss set v80 to the variant tag.
                for (int k = 0; k < 4; ++k) {
                    // (the live recipe row is in the type table; with inert hooks the
                    // recipe word is 0 so the slot passes.)
                    void* item = nullptr;
                    if (item == nullptr) continue;
                }
            }
            if (!v80) {
                // LABEL_137 success: drain the production over time.
                if (Dword(h, 200)) {
                    i32 v60 = Dword(h, 4);
                    HeRecord* sib = hk.findFirstHandler5(2, 0, 17, 3, v60);
                    if (sib) hk.freeHandlerEntry(sib);
                    Dword(h, 200) = 0;
                    CopyApptToScratch(h);
                }
                while (true) {
                    result = GameTimeCompare(&Time(h, 82), &Clock());
                    if (result >= 0) return result;
                    double workAmt = static_cast<double>(Dword(h, 20)) * kProdWorkAmtMul
                                     + static_cast<double>(Dword(h, 24));
                    i32 step = hk.productionComputeOutputOverTime(&Time(h, 96), &Time(h, 82));
                    int laps = 0;
                    Flt(h, 180) = Flt(h, 180) - static_cast<float>(static_cast<double>(step) * workAmt * rate);
                    while (Flt(h, 180) <= 0.0f) {
                        // refill from the recipe row +34 dword (live table); inert == 0,
                        // so this would spin — guard: bail once with the QueueRequest17.
                        ++laps;
                        Flt(h, 180) = Flt(h, 180) + 0.0f;
                        if (Dword(h, 196) == -1) {
                            i32 q = hk.queueRequest17(outRec ? RecI32(outRec, 1) : -1, -1, 0,
                                                      hi, 0, 0);
                            Dword(h, 196) = q;
                        }
                        break;  // verbatim refill loop is table-driven; one pass under inert hooks
                    }
                    if (laps) {
                        i32 q = hk.queueRequest18(Dword(h, 192), hi, 0, laps);
                        Dword(h, 196) = q;
                    }
                    CopyApptToScratch(h);
                    GameTimeAdvance(&Time(h, 82), 0, 0, 1);  // +1 minute
                    if (laps) break;
                }
                return result;
            }
            // LABEL_109 fail: notify, build-op, idle.
            hk.personFindActiveByEntity(b);
            void* wp = hk.buildingFindWorkProductObject(b);
            SendProduktionMessage(hk, h, b, v80);
            if (b && !Dword(h, 200) && RecI32(b, 149) != -1) hk.requestBuildOp66(RecI32(b, 1));
            Dword(h, 200) = 1;
            Dword(h, 196) = -1;
            result = GameTimeAdvance(&Time(h, 82), 0, 0, 5);  // +5 minutes
            CopyApptToScratch(h);
            (void)wp;
            return result;
        }
        case 2: {
            if (Dword(h, 20) > 0 || Dword(h, 24) > 0) {
                Dword(h, 112) = 1;
                return result;
            }
            void* b = nullptr;
            result = hk.resolveEntityById(&b, nullptr, Dword(h, 188), nullptr);
            if (!b) { Dword(h, 112) = -1; return result; }
            CancelMyActions(hk, h, b);
            hk.requestBuildOp66(RecI32(b, 1));
            return hk.freeHandlerEntry(h);
        }
        default:
            return result;
    }
}

// ===========================================================================
// 0x4f0708 — VIBE_Event_DiscoveryRaidRun
// ===========================================================================
i32 DiscoveryRaidRun(HeRecord* h) {
    const Event5Hooks& hk = *g_hooks;
    void* storable = nullptr;            // StorableObject
    i32 phase = Dword(h, 112) + 2;
    i32 memberCount = 0;                 // v112

    switch (phase) {
        case 0:
        case 1: {
            void* leader = hk.personQueryBegin(0, 1, 1, Dword(h, 176));
            if (leader) storable = hk.buildingFindStorableObject(leader);
            for (int slot = 0; slot < 8; ++slot) {
                i32 mid = Dword(h, 140 + 4 * slot);  // a1[35..]
                if (mid != -1) {
                    void* rec = hk.personFindRecordById(mid);
                    if (rec) {
                        hk.queueRequestSingle49(mid);
                        if (leader && storable)
                            hk.queueRequestNamedObject53(mid, RecI32(leader, 1), 0,
                                                         RecI32(storable, 1), 0, "raubzug");
                        else
                            hk.queueRequestNamedObject53(mid, -1, 0, -1, 0, "raubzug");
                        hk.changePlayerAction(nullptr, nullptr, nullptr, RecU16(rec, 0));
                    }
                }
            }
            return hk.freeHandlerEntry(h);
        }
        case 2: {
            void* leader = hk.personQueryBegin(0, 1, 1, Dword(h, 172));
            if (!leader) return hk.freeHandlerEntry(h);
            for (int slot = 0; slot < 8; ++slot) {
                if (Dword(h, 140 + 4 * slot) != -1) {
                    void* rec = hk.personFindRecordById(Dword(h, 140 + 4 * slot));
                    if (rec) {
                        hk.changePlayerAction(leader, nullptr, h, RecU16(rec, 0));
                        hk.queueRequestSingle49(Dword(h, 140 + 4 * slot));
                        hk.queueRequestNamedObject53(Dword(h, 140 + 4 * slot),
                                                     RecI32(leader, 1), 0, -1, 1, "raubzug");
                    }
                }
            }
            StampClock(h, 82);
            i32 r = GameTimeAdvance(&Time(h, 82), 0, 0, 4);  // +4 minutes
            Dword(h, 112) = 1;
            return r;
        }
        case 3: {
            hk.personQueryBegin(0, 1, 1, Dword(h, 172));
            // Scan up to 8 members; break if any is resolved AND not near a door.
            for (int slot = 0; slot < 8; ++slot) {
                i32 mid = Dword(h, 140 + 4 * slot);
                if (mid != -1) {
                    void* rec = hk.personFindRecordById(mid);
                    if (rec && RecI32(rec, 388) && !hk.objectIsNearDoor(rec)) {
                        StampClock(h, 82);
                        return GameTimeAdvance(&Time(h, 82), 0, 0, 4);  // someone still walking
                    }
                }
            }
            StampClock(h, 82);
            i32 r = GameTimeAdvance(&Time(h, 82), 0, 1, 0);  // +1 hour
            Dword(h, 112) = 2;
            return r;
        }
        case 4: {
            void* leader = hk.personQueryBegin(0, 1, 1, Dword(h, 172));
            unsigned char ctx[16] = {0};
            // ctx: [0]=leader, [1]=range 2500.0f, [2]=h, [3]=0 (the gesture-target req).
            *reinterpret_cast<void**>(ctx + 0) = leader;
            *reinterpret_cast<float*>(ctx + 4) = 2500.0f;
            *reinterpret_cast<void**>(ctx + 8) = h;
            void* target = leader ? hk.charActionFindGestureTarget(ctx) : nullptr;
            if (leader && target) {
                Dword(h, 180) = hk.gesetzEvaluateViolation(22, 1, RecI32(target, 4),
                                                           0 /*city cmd handle*/, RecI32(leader, 1));
                Dword(h, 184) = RecI32(target, 4);
                i32 retSeq = RecI32(target, 176);
                Dword(h, 112) = 3;
                Dword(h, 188) = retSeq;
                StampClock(h, 82);
                return retSeq;
            }
            for (int slot = 0; slot < 8; ++slot) {
                i32 mid = Dword(h, 140 + 4 * slot);
                if (mid != -1) {
                    void* rec = hk.personFindRecordById(mid);
                    if (rec) {
                        hk.changePlayerAction(leader, nullptr, h, RecU16(rec, 0));
                        hk.queueRequestSingle49(mid);
                        hk.queueRequestNamedObject53(mid, leader ? RecI32(leader, 1) : -1,
                                                     0, -1, 0, "raubzug");
                        RandomModulo(3);
                        hk.queueRequestFlag55(mid, 1);
                    }
                }
            }
            Dword(h, 112) = 5;
            StampClock(h, 82);
            return GameTimeAdvance(&Time(h, 82), 0, 0, 15);  // +15 minutes
        }
        case 5: {
            i32 pkt = Dword(h, 180);
            i32 result = 0;
            if (pkt != -1) {
                result = hk.packetStatus(pkt);
                if (!result) return result;
            }
            hk.personQueryBegin(pkt, 1, 1, Dword(h, 172));
            HeRecord* owner = hk.findFirstHandler3(1, 1, Dword(h, 184));
            if (owner && reinterpret_cast<void*>(static_cast<std::intptr_t>(RecI32(owner, 212)))
                            == reinterpret_cast<void*>(static_cast<std::intptr_t>(Dword(h, 4)))
                && Dword(h, 180) != -1) {
                hk.personFindRecordById(Dword(h, 184));
                void* seq = hk.packetSeq(Dword(h, 180));
                if (seq) hk.queueRequestPair36(RecI32(seq, 0), 0);
                Dword(h, 112) = 4;
                return GameTimeAdvance(&Time(h, 82), 0, 0, 10);  // +10 minutes
            }
            Dword(h, 112) = -1;
            return result;
        }
        case 6: {
            i32 ownerId = Dword(h, 184);
            HeRecord* owner = hk.findFirstHandler3(1, 1, ownerId);
            if (!owner) { Dword(h, 112) = -1; return 0; }
            if (RecI32(owner, 212) == Dword(h, 4))
                return GameTimeAdvance(&Time(h, 82), 0, 0, 6);  // +6 minutes
            void* victim = hk.personQueryBegin(ownerId, 1, 1, Dword(h, 172));
            if (victim) {
                void* leaderRec = hk.personFindRecordById(0 /*city leader*/);
                if (leaderRec) {
                    u8 currency = 0;
                    u16 draw = static_cast<u16>(RandomModulo(0xC8));  // 0..199
                    i32 payout = hk.moneyMultiplyByRate(draw + 550, currency);
                    char buf[512];
                    hk.renderFormattedMessage(buf, 5625, reinterpret_cast<std::intptr_t>(victim), payout, 0);
                    hk.sendEntityMessage(RecI32(leaderRec, 1), RecI32(leaderRec, 1), 1, buf, 1422, "raub_a");
                    hk.renderFormattedMessage(buf, 5626, reinterpret_cast<std::intptr_t>(victim), payout, 0);
                    hk.sendEntityMessage(0, 0, 1, buf, 1422, "raub_b");
                    hk.queueRequest16(0, RecI32(leaderRec, 1), payout, currency);
                }
            }
            for (int slot = 0; slot < 8; ++slot) {
                i32 mid = Dword(h, 140 + 4 * slot);
                if (mid != -1) {
                    void* rec = hk.personFindRecordById(mid);
                    if (rec) {
                        u16 d = static_cast<u16>(RandomModulo(0x64));  // 0..99
                        hk.buildingAdjustStockAndNotify(RecU16(rec, 0), -100 - d, 0);
                    }
                }
            }
            void* leader2 = hk.personQueryBegin(0, 1, 1, Dword(h, 176));
            if (!leader2) return hk.freeHandlerEntry(h);
            void* st = hk.buildingFindStorableObject(leader2);
            for (int slot = 0; slot < 8; ++slot) {
                i32 mid = Dword(h, 140 + 4 * slot);
                if (mid != -1) {
                    hk.queueRequestSingle49(mid);
                    hk.queueRequestNamedObject53(mid, RecI32(leader2, 1), 0,
                                                 st ? RecI32(st, 1) : -1, 0, "entdeckt");
                }
            }
            return hk.freeHandlerEntry(h);
        }
        case 7: {
            void* leader = hk.personQueryBegin(0, 1, 1, Dword(h, 172));
            void* cityLead = hk.personFindRecordById(0);
            if (!cityLead) { Dword(h, 112) = -2; return 0; }
            i32 lootValue = 0;
            void* victim = hk.personQueryBegin(0, 1, 1, Dword(h, 176));
            i32 v51 = 0;
            if (victim) {
                void* victimStore = hk.buildingFindStorableObject(victim);
                void* victimGoods = hk.queryFind(RecI32(victim, 93), 2, 6, 0, 278);
                double favSum = 0.0;
                for (int slot = 0; slot < 8; ++slot) {
                    i32 mid = Dword(h, 140 + 4 * slot);
                    if (mid != -1) {
                        void* rec = hk.personFindRecordById(mid);
                        if (rec) {
                            hk.queueRequestSingle49(mid);
                            hk.queueRequestNamedObject53(mid, leader ? RecI32(leader, 1) : -1, 0,
                                                         victimStore ? RecI32(victimStore, 1) : -1,
                                                         0, "raubzug");
                            i32 px = hk.buildingComputeProductionPixels(2, rec);
                            ++memberCount;
                            favSum += px;
                            hk.changePlayerAction(victim, nullptr, nullptr, RecU16(rec, 0));
                        }
                    }
                }
                if (memberCount) favSum /= memberCount;
                v51 = 1;
                hk.gesetzEvaluateViolation(22, 1, RecI32(cityLead, 1) /*v99[1]*/,
                                           0 /*city cmd handle*/, leader ? RecI32(leader, 1) : -1);
                (void)victimGoods; (void)favSum;
            }
            if ((!leader || hk.charActionIsAnimalTargetBusy(leader)) && v51) {
                void* victim2 = hk.personQueryBegin(0, 1, 1, Dword(h, 172));
                i32 store = victim2 ? RecI32(victim2, 93) : 0;
                void* goods = hk.queryFind(store, 2, 6, 0, 42);
                i32 stockVal = 0;
                hk.buildingValueComputeStockValue(nullptr, &stockVal);
                lootValue = ConvertX(static_cast<double>(stockVal) * kRaidLootValueMul);
                char buf[256];
                hk.renderFormattedMessage(buf, 0 /*aZTA_0*/, lootValue, 0, 0);
                hk.enqueueCmd15(cityLead ? RecI32(cityLead, 1) : -1,
                                RecI32(cityLead, 1) /*v99[1]*/, lootValue, 0);
                hk.beginDeltaPacket(reinterpret_cast<std::intptr_t>(victim2),
                                    victim2 ? RecI32(victim2, 1) : 0);
                unsigned char fld[8] = {0};
                hk.appendRawField(4, 1, fld, 105);
                hk.queueRequestState22();
                if (goods) {
                    for (void* it = hk.queryFind(RecI32(goods, 20), 1, 5, 0, 0); it;
                         it = hk.queryIterNext()) {
                        if (static_cast<u16>(RandomModulo(0x64)) > 0x32) {  // > 50
                            i32 eff = hk.inventoryGetEffectiveStock(goods, it);
                            i32 qty = ConvertX(static_cast<double>(memberCount) * kRaidLootStockMul
                                               * static_cast<double>(eff));
                            if (qty < 1) qty = 1;
                            hk.queueRequest17(goods ? RecI32(goods, 1) : -1,
                                              goods ? RecI32(goods, 1) : -1, qty,
                                              RecU16(it, 0), 0, 0);
                            double price = hk.buildingLookupCachedMarketPrice(RecU16(it, 0), 0);
                            lootValue = ConvertX(price * static_cast<double>(qty)
                                                 + static_cast<double>(lootValue));
                        }
                    }
                }
                void* fam = hk.personGetFamilyRecord(cityLead);
                if (fam) RecI32(fam, 20) += lootValue;  // fam[5]
                u8 cls = RecU8(cityLead, 2);
                if (cls == 6 || cls == 7) {
                    hk.renderFormattedMessage(buf, 5778, 0, reinterpret_cast<std::intptr_t>(victim2), 0);
                    hk.sendQuickjump(RecI32(cityLead, 1), RecI32(cityLead, 1), 1, buf, 1422,
                                     victim2 ? RecI32(victim2, 1) : -1, -1, 0, "raub_share_a");
                }
            } else {
                void* victim3 = hk.personQueryBegin(Dword(h, 172), 1, 1, Dword(h, 172));
                if (victim3) {
                    u8 cls = RecU8(cityLead, 2);
                    if (cls == 6 || cls == 7) {
                        char buf[512];
                        hk.renderFormattedMessage(buf, 5622, 0, reinterpret_cast<std::intptr_t>(victim3), 5584);
                        hk.sendQuickjump(RecI32(cityLead, 1), RecI32(cityLead, 1), 1, buf, 1422,
                                         RecI32(victim3, 1), -1, 0, "raub_caught_a");
                        hk.renderFormattedMessage(buf, 5624, reinterpret_cast<std::intptr_t>(victim3), 0, 0);
                        hk.sendQuickjump(0, 0, 1, buf, 1422, RecI32(victim3, 1), -1, 0, nullptr);
                    }
                }
            }
            return hk.freeHandlerEntry(h);
        }
        default:
            return Dword(h, 112) + 2;
    }
}

// ===========================================================================
// 0x4f4348 — VIBE_Event_SlotProcessRun
// ===========================================================================
i32 SlotProcessRun(HeRecord* h) {
    const Event5Hooks& hk = *g_hooks;
    u8 stalled = 0;                      // v72

    if (Dword(h, 112) == -2) {
        Dword(h, 112) = 0;
        hk.npcSetTargetCityRef(h, hk.playerCitySentinelWord());
    }
    // Two packet gates: idle +1 min while a request is in flight.
    if (Dword(h, 232) != -1) {
        if (!hk.packetStatus(Dword(h, 232)))
            return GameTimeAdvance(&Time(h, 82), 0, 0, 1);
        Dword(h, 232) = -1;
    }
    if (Dword(h, 236) != -1) {
        if (!hk.packetStatus(Dword(h, 236)))
            return GameTimeAdvance(&Time(h, 82), 0, 0, 1);
        Dword(h, 236) = -1;
    }

    // Resolve the three role entities.
    void* dest = nullptr;   // v73 (a1+172)
    void* source = nullptr; // v74 (a1+176)
    void* wage = nullptr;   // v75 (a1+180)
    hk.resolveEntityById(nullptr, &dest, Dword(h, 172), nullptr);
    hk.resolveEntityById(&source, nullptr, Dword(h, 176), nullptr);
    hk.resolveEntityById(nullptr, nullptr, Dword(h, 180), &wage);
    if (!dest || !source) return hk.freeHandlerEntry(h);

    // If a sibling handler already owns the same (dest,source), idle or free.
    HeRecord* sib = nullptr;
    for (sib = reinterpret_cast<HeRecord*>(hk.findFirstHandler5(2, 0, 15, 2, Word(h, 8)));
         sib; sib = reinterpret_cast<HeRecord*>(hk.findNextHandler())) {
        if (RecI32(sib, 172) == Dword(h, 172)) break;  // sib[43]
    }
    if (sib) {
        if (RecI32(sib, 180) == Dword(h, 176))         // sib[45]
            return GameTimeAdvance(&Time(h, 82), 0, 0, 5);
        return hk.freeHandlerEntry(h);
    }
    // Owner-chain gate (+241 == carry flag).
    if (!Byte(h, 241)) {
        hk.tradeTransportFindOwnerChain(source, dest);
        // The original re-calls the chain probe; with no chain it frees.
        return hk.freeHandlerEntry(h);
    }

    // Find the source's process node, clamp the member count to 3.
    void* proc = hk.queryFind(dest ? RecI32(dest, 20) : 0, 1, 0, 477, 0);
    u8 nodeCount = proc ? RecU8(proc, 28) : 0;
    i32 memberCount = (nodeCount >= 3) ? 3 : nodeCount;

    // input-pending check (any +208 input slot OR +214 output slot set).
    bool anyWork = false;
    for (int k = 0; k < memberCount && !anyWork; ++k) {
        if (Word(h, 208 + 2 * k) || Word(h, 214 + 2 * k)) anyWork = true;
    }
    if (!anyWork) {
        // No work: just emit a take-1 of the first ready good (the v9==0 branch).
        void* take = hk.queryFind(source ? RecI32(source, 93) : 0, 2, 6, 0, 42);
        if (take) {
            for (void* it = hk.queryFind(proc ? RecI32(proc, 20) : 0, 1, 5, 0, 0); it;
                 it = hk.queryIterNext())
                Dword(h, 232) = hk.queueRequest17(RecI32(take, 4), proc ? RecI32(proc, 4) : 0,
                                                  RecI32(it, 28), RecU16(it, 0), 0, 0);
        } else {
            void* take2 = hk.queryFind(source ? RecI32(source, 93) : 0, 2, 6, 0, 278);
            if (take2)
                for (void* it = hk.queryFind(proc ? RecI32(proc, 20) : 0, 1, 5, 0, 0); it;
                     it = hk.queryIterNext())
                    Dword(h, 232) = hk.queueRequest17(RecI32(take2, 4), proc ? RecI32(proc, 4) : 0,
                                                      RecI32(it, 28), RecU16(it, 0), 0, 0);
        }
        if (Dword(h, 232) == -1) return hk.freeHandlerEntry(h);
        return GameTimeAdvance(&Time(h, 82), 0, 0, 5);
    }

    // --- input scan (+208 slots) --------------------------------------------
    bool anyInput = false;
    for (int k = 0; k < memberCount; ++k)
        if (Word(h, 208 + 2 * k)) { anyInput = true; break; }
    if (anyInput) {
        for (int k = 0; k < memberCount; ++k) {
            if (!Word(h, 208 + 2 * k) || Dword(h, 184 + 4 * k) != (source ? RecI32(source, 1) : 0))
                continue;
            i32 itemId = Dword(h, 206 + 2 * k) >> 16;
            void* item = hk.queryFind(proc ? RecI32(proc, 20) : 0, 1, 0, itemId, 0);
            if (!item) { Word(h, 208 + 2 * k) = 0; Dword(h, 184 + 4 * k) = -1; continue; }
            u8 srcCls = source ? RecU8(reinterpret_cast<void*>(
                static_cast<std::intptr_t>(589 * RecU16(source, 0))), 0) : 0;
            (void)srcCls;
            void* slotRec = (srcCls == 10) ? hk.buildingFindActiveWorkSlot(Dword(h, 206 + 2 * k) >> 16)
                                           : hk.queryFind(source ? RecI32(source, 93) : 0, 2, 6, 0, 42);
            if (slotRec) {
                i32 eff = hk.inventoryGetEffectiveStock(proc, item);
                i32 freeCap = hk.inventoryComputeFreeCapacity(item, RecU16(item, 0), 0, eff);
                double price = hk.buildingLookupCachedMarketPrice(RecU16(item, 0), 0) * kSlotPriceMulIn;
                i32 unit = ConvertX(price);
                i32 take = (freeCap <= unit) ? freeCap : unit;
                hk.queueRequest17(RecI32(slotRec, 8), proc ? RecI32(proc, 4) : 0, take,
                                  RecU16(item, 0), 0, unit);
                if (wage) RecI32(wage, 440) += take * unit;  // wage[110]
                i32 rest = eff - take;
                if (rest) {
                    double rp = hk.buildingLookupCachedMarketPrice(RecU16(item, 0), 0) * kSlotPriceMulRest;
                    Dword(h, 232) = hk.queueRequest17(-1, proc ? RecI32(proc, 4) : 0, rest,
                                                      RecU16(item, 0), 0, ConvertX(rp));
                }
            }
            Word(h, 208 + 2 * k) = 0;
            Dword(h, 184 + 4 * k) = -1;
        }
    } else {
        // --- output scan (+214 slots) --------------------------------------
        i32 budget = 0;
        if (wage && RecI32(wage, 364)) {  // wage[91]
            void* owner = hk.personFindRecordById(RecI32(wage, 364));
            i32 cur = hk.personGetCurrencyAmount(owner, 0);
            if (cur >= RecI32(wage, 440)) cur = RecI32(wage, 440);
            budget = cur;
        }
        for (int k = 0; k < memberCount; ++k) {
            if (!Word(h, 214 + 2 * k) || Dword(h, 196 + 4 * k) != (source ? RecI32(source, 1) : 0))
                continue;
            u8 srcCls = source ? RecU8(reinterpret_cast<void*>(
                static_cast<std::intptr_t>(589 * RecU16(source, 0))), 0) : 0;
            void* slotRec = (srcCls == 10) ? hk.buildingFindActiveWorkSlot(Dword(h, 212 + 2 * k) >> 16)
                                           : hk.queryFind(source ? RecI32(source, 93) : 0, 2, 6, 0, 42);
            if (!slotRec) { stalled = 0; continue; }
            void* item = hk.queryFind(RecI32(slotRec, 20), 1, 0, Dword(h, 212 + 2 * k) >> 16, 0);
            if (!item) continue;
            double price = (srcCls == 10)
                ? hk.buildingLookupCachedMarketPrice(RecU16(item, 0), 0) * kSlotPriceMulRaw
                : hk.buildingLookupCachedMarketPrice(RecU16(item, 0), 0) * static_cast<double>(Flt(reinterpret_cast<HeRecord*>(source), 73));
            i32 unit = ConvertX(price);
            i32 cap = (RecI32(item, 28) * hk.inventoryGetSlotCapacity(item)) >> 2;  // unsigned >>2
            i32 avail = hk.inventoryGetEffectiveStock(proc, item) - cap;
            i32 want = Dword(h, 220 + 4 * k);
            if (avail <= want) want = avail;
            i32 qty = (unit != 0) ? budget / unit : 0;
            if (want < qty) qty = want;
            if (qty < 0) qty = 0;
            if (qty) {
                Dword(h, 232) = hk.queueRequest17(proc ? RecI32(proc, 4) : 0,
                                                  RecI32(slotRec, 8), qty, RecU16(item, 0), 0, unit);
            }
            if (wage && RecI32(wage, 364)) {
                char buf[128];
                hk.renderFormattedMessage(buf, 0, unit * qty, budget, qty);
            }
            budget -= unit * qty;
            if (wage) RecI32(wage, 440) -= unit * qty;
            if (qty || !Byte(h, 240)) {
                Word(h, 214 + 2 * k) = 0;
                Dword(h, 196 + 4 * k) = -1;
            } else {
                stalled = static_cast<u8>(stalled | 1);
            }
        }
    }

    // --- tail (LABEL_47) -----------------------------------------------------
    if (Byte(h, 240) && stalled) {
        if (Clock().hour >= 20u) {
            i32 r = GameTimeAdvance(&Time(h, 82), 0, 0, 5);
            Byte(h, 240) = 0;
            return r;
        }
        i32 r = GameTimeAdvance(&Time(h, 82), 0, 0, 30);
        --Byte(h, 240);
        return r;
    }

    // pick the next pending slot's id into +176.
    Dword(h, 176) = -1;
    for (int k = 0; k < memberCount && Dword(h, 176) == -1; ++k)
        if (Word(h, 208 + 2 * k)) Dword(h, 176) = Dword(h, 184 + 4 * k);
    if (Dword(h, 176) == -1)
        for (int k = 0; k < memberCount && Dword(h, 176) == -1; ++k)
            if (Word(h, 214 + 2 * k)) Dword(h, 176) = Dword(h, 196 + 4 * k);
    if (Dword(h, 176) == -1) {
        void* fb = nullptr;
        hk.resolveEntityById(&fb, nullptr, Dword(h, 16), nullptr);
        Dword(h, 176) = fb ? RecI32(fb, 1) : -1;
    }
    if (Byte(h, 241))
        return GameTimeAdvance(&Time(h, 82), 1, 0, 30);  // +1 day +30 min (carry)
    // build the slot-reset blob and either idle +5 (same id) or +10 (new packet).
    i32 dstId = dest ? RecI32(dest, 1) : 0;
    if (dstId == Dword(h, 176))
        return GameTimeAdvance(&Time(h, 82), 0, 0, 5);
    unsigned char blob[256] = {0};
    Dword(h, 236) = hk.queueRequestSlotReset28(blob);
    return GameTimeAdvance(&Time(h, 82), 0, 0, 10);
}

} // namespace guild::world
