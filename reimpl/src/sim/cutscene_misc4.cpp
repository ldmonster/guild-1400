// ===========================================================================
// cutscene_misc4.cpp — RunParticipants / SalonFadeTransition / LeaseWindow.
// 1:1 translations of the three big VIBE_Cutscene_* bodies the earlier slices
// left untranslated. See cutscene_misc4.h for the recovered-constants map.
// ===========================================================================
#include "sim/cutscene_misc4.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// Hook surface — inert defaults (all leaves no-op; the pumps terminate).
// ---------------------------------------------------------------------------
namespace {
// pumpFrame default: 0 == "stop the loop".
// packetStatus default: 1 == "packet already complete" (so the wait loops exit).
int DefaultPacketStatus(u32) { return 1; }

const CutsceneMisc4Hooks kInertHooks = []{
    CutsceneMisc4Hooks h{};
    h.packetStatus = &DefaultPacketStatus;
    return h;
}();

const CutsceneMisc4Hooks* g_hooks = &kInertHooks;
}  // namespace

void SetCutsceneMisc4Hooks(const CutsceneMisc4Hooks* hooks) {
    g_hooks = hooks ? hooks : &kInertHooks;
}
const CutsceneMisc4Hooks& GetCutsceneMisc4Hooks() { return *g_hooks; }

// ---------------------------------------------------------------------------
// Deterministic kernels.
// ---------------------------------------------------------------------------
// 0x4aad57 etc — forward scan of slot.partIds for `wantId`.
int CutsceneFindParticipantById(const CutsceneSlot* slot, i32 wantId) {
    if (!slot) return -1;
    int count = static_cast<int>(slot->partCount);
    if (count > kMaxParticipants) count = kMaxParticipants;
    for (int i = 0; i < count; ++i) {
        if (slot->partIds[i] == wantId)
            return i;
    }
    return -1;
}

// 0x4ab2d0 / 0x4ab387 — backward "find prior valid participant" scan.
//   v32 = startIndex - 1; if (v32 <= 0) anchor 0; else walk down past empty
//   (-1) cells; if it reaches index 0 the anchor is 0.
int CutsceneFindPriorValidParticipant(const CutsceneSlot* slot, int startIndex) {
    if (!slot) return 0;
    int idx = startIndex - 1;
    if (idx <= 0)
        return 0;
    int count = static_cast<int>(slot->partCount);
    while (idx < count && idx >= 0 && slot->partIds[idx] == -1) {
        --idx;
        if (idx <= 0)
            return 0;
    }
    return idx;
}

// 0x4aae8e — classify byte_11AE5CC[20*cutType].
int CutsceneClassifyRunMode(u8 rawModeByte) {
    if (rawModeByte == kRunModeMaster) return 1;
    if (rawModeByte == kRunModeNonMaster) return 0;
    return -1;  // any other value -> skip the participant run
}

// 0x4aadbb / 0x4aafab — per-participant voice-table from the +2 kind byte.
int CutsceneParticipantCallbackTable(u8 personKind) {
    if (personKind == kPersonKindWeddingA) return 4;  // dword_11AE5C4
    if (personKind == kPersonKindWeddingB) return 0;  // skipped
    return 8;                                          // dword_11AE5C8
}

// 0x4a9d12.. — salon cache-match transition classifier.
int CutsceneSalonClassifyTransition(i32 replayGate,
                                    i32 reqEntity, i16 reqSeason,
                                    i32 cachedEntity1, i16 cachedSeason1,
                                    i32 cachedEntity2, i16 cachedSeason2) {
    if (replayGate)
        return 0;  // dword_6315BC nonzero -> early return, no-op
    // Matches the FIRST cached scene (dword_631744) ...
    if (reqEntity == cachedEntity1 && reqSeason == cachedSeason1)
        return 1;
    // ... or the SECOND cached scene (dword_631748).
    if (reqEntity == cachedEntity2 && reqSeason == cachedSeason2)
        return 1;
    return 2;  // no match -> load a new scene
}

// 0x4a9dfa.. — scene-kind dispatch for the load path.
int CutsceneSalonSceneKind(bool isProductionType, int packedDispatchByte) {
    if (isProductionType)
        return 0;  // Gebaeude (building interior) scene
    // Non-production: the objekt dispatch byte == -2 means "no scene".
    if (packedDispatchByte == -2)
        return -1;
    return 1;      // Objekt scene
}

// 0x4a9885.. — lease offer-table scan.
int CutsceneLeaseFindOffer(const i32* offerLessees, const i32* offerValid,
                           int count, i32 lesseeId) {
    if (!offerLessees || !offerValid) return -1;
    int i = 0;
    for (; i < count; ++i) {
        if (offerLessees[i] == lesseeId)
            break;
    }
    if (i < count && offerValid[i] != -1)
        return i;
    return -1;
}

// 0x4a98e2.. — affordability gate. ClampValueRange(lo=baseRent, value=funds,
// mul=32) succeeds only for lo>=1; the window is shown when funds >= baseRent.
bool CutsceneLeaseCanAfford(int funds, i32 baseRent) {
    if (baseRent < 1)
        return false;  // ClampValueRange returns 0 (failure) for lo<1
    return funds >= baseRent;
}

// ---------------------------------------------------------------------------
// Full flow drivers.
// ---------------------------------------------------------------------------
// 0x4aac78 — VIBE_Cutscene_RunParticipants.
i32 CutsceneRunParticipants(CutsceneRng& rng, const CutsceneSlot* slot,
                            u16 worldFlags, u8 runModeByte, int cutType) {
    const CutsceneMisc4Hooks& h = GetCutsceneMisc4Hooks();
    Cutscene3State& st = Cutscene3();

    // RandSeed = VIBE_Cutscene_GetRandSeed(); InitParticipantTable(slot);
    i32 savedSeed = rng.GetSeed();
    if (h.initParticipantTable) h.initParticipantTable(slot);

    if (!slot) {  // guard: the original always has a live slot
        rng.SetSeed(savedSeed);
        return savedSeed;
    }

    // if (word_63C740 & 4) { master-report preamble (net wait) }
    if (worldFlags & kWorldFlagMasterReport) {
        // The "i am master" cut-info broadcast happens only when not replaying.
        if (!st.replayGate) {
            if (h.setLightGray) h.setLightGray(0, 124);
            // VIBE_Command_QueueRequestFlagBlob32(17, &blob)
            if (h.queueFlagBlob) h.queueFlagBlob(17, nullptr);
        }
        if (!st.replayGate) {
            if (h.netRunWaitLoop) h.netRunWaitLoop(nullptr);
        }
    }

    if (h.setStatusBanner) h.setStatusBanner("");  // byte_61C3E4

    int count = static_cast<int>(slot->partCount);
    if (count > kMaxParticipants) count = kMaxParticipants;

    // --- render-only pass: word_63C740 & 8 ----------------------------------
    if (worldFlags & kWorldFlagRenderPass) {
        for (int i = 0; i < count; ++i) {
            i32 pid = slot->partIds[i];
            if (pid == -1)
                continue;
            void* rec = h.personFind ? h.personFind(pid) : nullptr;
            if (!rec)
                continue;
            u8 kind = h.personKind ? h.personKind(rec) : 0;
            int table = CutsceneParticipantCallbackTable(kind);
            if (kind == kPersonKindWeddingA) {
                if (h.runTypeCallback) h.runTypeCallback(4, cutType);
            } else if (table == 8) {
                if (h.runTypeCallback) h.runTypeCallback(8, cutType);
            }
            // Form_SelectWindow + Text_RenderRichString (per-participant line).
            if (h.showParticipantDialog) h.showParticipantDialog(slot);
        }
        ++st.duelMode;
        rng.SetSeed(savedSeed);
        return savedSeed;
    }

    if (!(worldFlags & kWorldFlagMasterReport)) {
        ++st.duelMode;
        rng.SetSeed(savedSeed);
        return savedSeed;
    }

    // --- master-report path: dispatch on byte_11AE5CC[20*cutType] ------------
    int mode = CutsceneClassifyRunMode(runModeByte);
    if (mode == 1) {
        // Interactive master-driven choice loop. The original first locates the
        // master's own participant cell, then anchors the camera on the prior
        // valid participant, and pumps the dialog until all are done.
        int masterIdx = CutsceneFindParticipantById(slot, slot->master);
        if (masterIdx != -1)
            (void)CutsceneFindPriorValidParticipant(slot, masterIdx);
        // do { ShowParticipantDialog; pump } while (!master finished) — the
        // pump returns 0 by default (terminates immediately and faithfully).
        for (;;) {
            int done = h.allParticipantsDone ? h.allParticipantsDone(slot) : 1;
            if (done) {
                if (h.showParticipantDialog) h.showParticipantDialog(slot);
                if (h.pumpFrame) h.pumpFrame(st.frameFlags, masterIdx, nullptr);
                break;
            }
            if (h.showParticipantDialog) h.showParticipantDialog(slot);
            int keep = h.pumpFrame ? h.pumpFrame(st.frameFlags, masterIdx, nullptr) : 0;
            if (!keep)
                break;
        }
    } else if (mode == 0) {
        // Non-master local replay: stage + send each participant's cut-info,
        // wait for the packet, then pump until all participants are done.
        for (int i = 0; i < count; ++i) {
            i32 pid = slot->partIds[i];
            void* rec = h.personFind ? h.personFind(pid) : nullptr;
            if (!rec && pid != -1)
                continue;
            if (rec) {
                u8 kind = h.personKind ? h.personKind(rec) : 0;
                if (kind != kPersonKindWeddingA && kind != kPersonKindWeddingB) {
                    if (h.runTypeCallback) h.runTypeCallback(8, cutType);
                    if (h.stagePendingBlock) h.stagePendingBlock(0x35C, nullptr);
                    i32 ent = h.personEntityId ? h.personEntityId(rec) : pid;
                    u32 pkt = h.requestSendCutInfo
                                  ? h.requestSendCutInfo(ent, slot->id)
                                  : 0;
                    while (h.packetStatus && !h.packetStatus(pkt)) {
                        if (h.refreshGuildState) h.refreshGuildState();
                    }
                    if (h.showParticipantDialog) h.showParticipantDialog(slot);
                    if (h.pumpFrame) h.pumpFrame(st.frameFlags, kind, nullptr);
                }
            }
        }
        // trailing: pump until all done.
        while (h.allParticipantsDone && !h.allParticipantsDone(slot)) {
            if (h.showParticipantDialog) h.showParticipantDialog(slot);
            if (h.setStatusBanner) h.setStatusBanner("");  // dword_8C99B0
            if (h.pumpFrame) h.pumpFrame(st.frameFlags, 0, nullptr);
            else break;
        }
    }
    // mode == -1 -> skip (fall straight through to the tail).

    // LABEL_92: ++dword_6315A4; return VIBE_Cutscene_SetRandSeed(RandSeed);
    ++st.duelMode;
    rng.SetSeed(savedSeed);
    return savedSeed;
}

// 0x4a9d04 — VIBE_Cutscene_SalonFadeTransition.
int CutsceneSalonFadeTransition(i32 reqEntity, i16 reqSeason,
                                i32 cachedEntity1, i16 cachedSeason1,
                                i32 cachedEntity2, i16 cachedSeason2,
                                bool isProductionType, int packedDispatchByte) {
    const CutsceneMisc4Hooks& h = GetCutsceneMisc4Hooks();
    Cutscene3State& st = Cutscene3();

    int cls = CutsceneSalonClassifyTransition(st.replayGate, reqEntity, reqSeason,
                                              cachedEntity1, cachedSeason1,
                                              cachedEntity2, cachedSeason2);
    if (cls == 0)
        return 0;  // replay-gated no-op

    if (cls == 2) {
        // Load a new scene: dispatch on production type, then SwitchActiveSlot.
        int kind = CutsceneSalonSceneKind(isProductionType, packedDispatchByte);
        if (kind != -1) {
            if (h.salonLoadScene) h.salonLoadScene(kind, reqEntity, reqSeason);
        }
        // EnterBuildingInterior, then fall into the LABEL_17 fade-out path.
    }

    // Fade-out (LABEL_17 / loc_4A9D37): gated on the active surface bit
    // (dword_6315C0[+38] & 0x20) — the host owns that surface; the colour-fill
    // cross-fade is one engine leaf.
    if (h.salonFadeOut) h.salonFadeOut();

    // If a fade is registered (dword_6315D4, host-owned) the original does
    // Fade_Unregister + Fade_Register(BLACK). We route both through one hook;
    // the inert default never re-registers.
    if (h.salonReregisterFade) h.salonReregisterFade();
    return cls;
}

// 0x4a9868 — VIBE_Cutscene_LeaseWindow.
int CutsceneLeaseWindow(const i32* offerLessees, const i32* offerValid,
                        int offerCount, i32 lesseeId, int funds, i32 baseRent,
                        i32* outRent) {
    const CutsceneMisc4Hooks& h = GetCutsceneMisc4Hooks();

    // a1[38] = 0 (not accepted yet); a1[37] default == base rent.
    if (outRent) *outRent = baseRent;

    int idx = CutsceneLeaseFindOffer(offerLessees, offerValid, offerCount, lesseeId);
    if (idx == -1)
        return 0;  // result < count && v3[result] != -1 failed

    // The lessee must exist as a person record (Person_FindRecordById).
    void* rec = h.personFind ? h.personFind(lesseeId) : nullptr;
    if (!rec)
        return 0;

    int liquid = h.personSumCurrency ? h.personSumCurrency(rec) : funds;
    if (!CutsceneLeaseCanAfford(liquid, baseRent))
        return 0;  // v6 >= v5 failed -> window not shown

    // Pump the rent-slider window. The hook returns the negotiated rent on
    // accept (button 1210) or the base rent on cancel; default returns base.
    i32 negotiated = h.leaseRunRentSlider
                         ? h.leaseRunRentSlider(baseRent, liquid)
                         : baseRent;
    if (outRent) *outRent = negotiated;

    // a1[38] = 1 only when the player accepted (negotiated differs / accept).
    return negotiated != baseRent ? 1 : (h.leaseRunRentSlider ? 1 : 0);
}

}  // namespace guild::sim
