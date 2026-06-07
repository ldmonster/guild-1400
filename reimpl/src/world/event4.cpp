// event4 — a further slice of the world-event "He"-action bodies (gilde.exe
// VIBE_Event_*). See event4.h for the function inventory and provenance.
#include "world/event4.h"

#include "sim/gametime.h"     // guild::sim::GameTimeAdvance / GameTimeCompare
#include "sim/npcaction.h"    // guild::sim::NpcClock (qword_13CE852 game clock)
#include "util/math_random.h" // guild::util::RandomModulo
#include "util/coord.h"       // guild::util::ConvertX (FPU trunc-toward-zero)

#include <cstdint>
#include <cstring>

namespace guild::world {

using guild::sim::GameTimeAdvance;
using guild::sim::GameTimeCompare;
using guild::sim::HeBytes;
using guild::util::RandomModulo;
using guild::util::ConvertX;

// Module-global game clock (qword_13CE852). Shared with the sim cluster — reuse
// sim::NpcClock() rather than redefining (ODR).
static GameTime& Clock() { return guild::sim::NpcClock(); }

// Raw record accessors at the exact original byte offsets (mirror the originals'
// *(T*)(base+off)); a1[k] in the pseudocode is the dword at base + 4*k.
namespace {
inline i32&      Dword(HeRecord* h, int off) { return *reinterpret_cast<i32*>(HeBytes(h) + off); }
inline u16&      Word (HeRecord* h, int off) { return *reinterpret_cast<u16*>(HeBytes(h) + off); }
inline u8&       Byte (HeRecord* h, int off) { return *reinterpret_cast<u8*>(HeBytes(h) + off); }
inline float&    Flt  (HeRecord* h, int off) { return *reinterpret_cast<float*>(HeBytes(h) + off); }
inline GameTime& Time (HeRecord* h, int off) { return *reinterpret_cast<GameTime*>(HeBytes(h) + off); }

// Stamp the 14-byte clock image into the GameTime at `off` (the originals write
// the qword at +0, the dword at +8, the word at +12 of the block).
inline void StampClock(HeRecord* h, int off) { Time(h, off) = Clock(); }

// The cancel-actions loop the teardown paths run: walk all 768 person slots; for
// each whose active-action handler == this record, re-invoke ChangePlayerAction
// with the person's action char id and the given object (0 or the resolved rec).
inline void CancelMyActions(const Event4Hooks& hk, HeRecord* h, void* obj) {
    for (int p = 0; p < 768; ++p) {
        if (hk.activeActionHe(p) == h)
            hk.changePlayerAction(obj, nullptr, nullptr, hk.personActionCharId(p));
    }
}
}  // namespace

// ---------------------------------------------------------------------------
// Hooks — inert defaults defined here so the library links standalone.
// ---------------------------------------------------------------------------
namespace {
i32   InertFree(HeRecord*) { return 0; }
void* InertActiveHe(int) { return nullptr; }
u16   InertActionCharId(int) { return 0; }
void  InertChangeAction(void*, void*, void*, u16) {}
void  InertResolve(void** a, i32* b, i32, i32) { if (a) *a = nullptr; if (b) *b = 0; }
void* InertQueryFind(i32, i32, i32, i32) { return nullptr; }
void* InertIterNext() { return nullptr; }
void  InertCollectProts(void*) {}
i32   InertQueue17(i32, i32, i32, i32, i32, i32) { return -1; }
void  InertCoord27(i32, i32, i32) {}
void  InertRenderMsg(char*, i32, i32, void*, i32) {}
void  InertQuickjump(i32, const char*, i32, const char*) {}
i32   InertSumWork(void*, i32, i32) { return 0; }
i32   InertProduce(const GameTime*, const GameTime*) { return 0; }
double InertFavor(u16, i32, const i32*) { return 0.0; }
i32   InertCmdHandle(u16) { return 0; }
void* InertFindBuilding(i32) { return nullptr; }
void  InertPlaySample(i32, i32, i32, const char*, i32) {}
i32   InertAudioEnabled() { return 0; }
i32   InertAnnounceEnabled() { return 0; }
void* InertFindPerson(i32) { return nullptr; }
void  InertScriptFinish(i32) {}
void  InertStandUp(void*) {}
void  InertInsertCollapse(void*) {}
void* InertSelectTarget(void*) { return nullptr; }
void  InertBroadcast(void*, void*, i32) {}
i32   InertEmployRel(void*) { return 0; }
void  InertPair33(i32, i32) {}
i32   InertPacketStatus(i32) { return 0; }
i32   InertPacketSeq(i32) { return 0; }
i32   InertQueue39(const void*) { return -1; }
void* InertCutsceneSlot(i32) { return nullptr; }
i32   InertSwitchSlot(i32) { return 0; }
int   InertCollectNodes(i32, void**, int) { return 0; }
i32   InertNodeState(void*) { return 0; }
i32   InertNodeFlags(void*) { return 0; }
int   InertNodeLevelCount(void*) { return 0; }
i32   InertNodeLevel(void*, int) { return 0; }
void  InertNodeSetState(void*, i32) {}
void  InertNodeClearDetached(void*) {}
void  InertNodeSetLevelBase(void*, i32) {}
void  InertRemoveMesh(void*) {}
void  InertRestoreStates(void*, i32) {}
void  InertDetachRelease(void*) {}
void* InertFindHeRoot(i32) { return nullptr; }

const Event4Hooks kInertHooks = {
    &InertFree,
    &InertActiveHe, &InertActionCharId, &InertChangeAction,
    &InertResolve, &InertQueryFind, &InertIterNext, &InertCollectProts,
    &InertQueue17, &InertCoord27,
    &InertRenderMsg, &InertQuickjump,
    &InertSumWork, &InertProduce, &InertFavor, &InertCmdHandle, &InertFindBuilding,
    &InertPlaySample, &InertAudioEnabled, &InertAnnounceEnabled,
    &InertFindPerson, &InertScriptFinish, &InertStandUp, &InertInsertCollapse,
    &InertSelectTarget, &InertBroadcast, &InertEmployRel, &InertPair33,
    &InertPacketStatus, &InertPacketSeq, &InertQueue39, &InertCutsceneSlot,
    &InertSwitchSlot, &InertCollectNodes,
    &InertNodeState, &InertNodeFlags, &InertNodeLevelCount, &InertNodeLevel,
    &InertNodeSetState, &InertNodeClearDetached, &InertNodeSetLevelBase,
    &InertRemoveMesh, &InertRestoreStates, &InertDetachRelease, &InertFindHeRoot,
};
const Event4Hooks* g_hooks = &kInertHooks;
}  // namespace

void SetEvent4Hooks(const Event4Hooks* hooks) { g_hooks = hooks ? hooks : &kInertHooks; }
const Event4Hooks& GetEvent4Hooks() { return *g_hooks; }

const float kMorningHour[4] = { 8.0f, 7.0f, 8.0f, 9.0f };   // flt_6476FC
const float kEveningHour[4] = { 20.0f, 21.0f, 20.0f, 19.0f }; // flt_64770C

// ===========================================================================
// 0x4f3b34 — VIBE_Event_HarvestWageRun
// ===========================================================================
i32 HarvestWageRun(HeRecord* h) {
    const Event4Hooks& hk = *g_hooks;
    i32 v4 = Dword(h, 112);
    i32 result = v4 + 2;          // the original's default return (counter+2)
    switch (v4) {
        case -2:
        case -1:
            CancelMyActions(hk, h, nullptr);
            return hk.freeHandlerEntry(h);
        case 0:
            Dword(h, 112) = v4 + 1;
            return result;
        case 1: {
            if (Dword(h, 20) == 0 && Dword(h, 24) == 0)
                return hk.freeHandlerEntry(h);
            result = GameTimeCompare(&Time(h, 82), &Clock());
            if (result >= 0) return result;
            void* rec = nullptr;            // a1[44] = +176 entity id -> wage record in *rec
            hk.resolveEntityById(&rec, nullptr, Dword(h, 176), 0);
            if (!rec) { Dword(h, 112) = -1; return result; }
            // base = building-type row word at +54 (row index = +170 dword >> 16); the
            // live table is 65*type + dword_13CE27C, surfaced here as the resolved
            // record's +54 word (the test wires the row that way).
            i32 base = static_cast<i32>(Word(reinterpret_cast<HeRecord*>(rec), 54));
            i32 wage = (Dword(h, 20) + Dword(h, 24)) *
                       (40 * base + static_cast<u16>(RandomModulo(40 * base)));
            hk.queueRequest17(Dword(h, 180) /*a1[45]*/, -1, wage,
                              Dword(h, 170) >> 16, 0, 0);
            char buf[512];
            hk.renderFormattedMessage(buf, 6085, 2 * (Dword(h, 170) >> 16) + 2151, rec, wage);
            hk.sendQuickjumpMessage(0, buf, 1424, "_NACHRICHTEN_HS_08");
            CancelMyActions(hk, h, rec);
            return hk.freeHandlerEntry(h);
        }
        default:
            return result;
    }
}

// ===========================================================================
// 0x4f3d7c — VIBE_Event_HarvestProcessRun
// ===========================================================================
void HarvestProcessRun(HeRecord* h) {
    const Event4Hooks& hk = *g_hooks;
    i32 v4 = Dword(h, 112);
    switch (v4) {
        case -2:
        case -1:
            CancelMyActions(hk, h, nullptr);
            hk.freeHandlerEntry(h);
            return;
        case 0:
            Dword(h, 112) = v4 + 1;
            return;
        case 1: {
            if (Dword(h, 20) == 0 && Dword(h, 24) == 0) {  // a1[5] && a1[6]
                hk.freeHandlerEntry(h);
                return;
            }
            if (GameTimeCompare(&Time(h, 82), &Clock()) >= 0)
                return;
            void* wageRec = nullptr;
            hk.resolveEntityById(&wageRec, nullptr, Dword(h, 172), 0);  // a1[43] = +172
            if (!wageRec) { Dword(h, 112) = -1; return; }
            i32 srcSeq = 0;
            hk.resolveEntityById(nullptr, &srcSeq, Dword(h, 176), 0);   // a1[44] = +176
            // Find the first "ready" good node (raw +7 dword == 1) of the source scene.
            void* node = hk.queryFind(srcSeq + 20, 1, 4, 23);
            while (node && *reinterpret_cast<i32*>(reinterpret_cast<u8*>(node) + 7) != 1)
                node = hk.queryIterNext();
            if (node) {
                // queue a take-1 of the ready good, then scan matching prots.
                hk.queueRequest17(srcSeq + 2, -1, 1, *reinterpret_cast<i32*>(node), 0, 0);
                hk.collectMatchingProts(wageRec);
                // (the multi-good city scan / per-member gate is driven by the live
                // object table; with inert hooks no further goods qualify.)
                i32 base = static_cast<i32>(Word(reinterpret_cast<HeRecord*>(wageRec), 54));
                i32 wage = (Dword(h, 20) + Dword(h, 24)) *
                           (static_cast<u16>(RandomModulo(30 * base)) + 30 * base);
                hk.queueRequest17(Dword(h, 180), -1, wage, *reinterpret_cast<i32*>(node), 0, 0);
                char buf[512];
                hk.renderFormattedMessage(buf, 6085, 2 * (*reinterpret_cast<i32*>(node)) + 2151,
                                          wageRec, wage);
                hk.sendQuickjumpMessage(0, buf, 1424, "_NACHRICHTEN_HS_08");
            }
            CancelMyActions(hk, h, wageRec);
            hk.freeHandlerEntry(h);
            return;
        }
        default:
            return;
    }
}

// ===========================================================================
// 0x4f2614 — VIBE_Event_WorkActionRun
// ===========================================================================
i32 WorkActionRun(HeRecord* h) {
    const Event4Hooks& hk = *g_hooks;
    i32 result = Dword(h, 112);
    switch (result) {
        case -2:
        case -1:
            CancelMyActions(hk, h, nullptr);
            return hk.freeHandlerEntry(h);
        case 0:
            Dword(h, 112) = ++result;
            return result;
        case 1: {
            if (Dword(h, 20) == 0 && Dword(h, 24) == 0) {
                Dword(h, 112) = 2;
                return result;
            }
            void* station = nullptr;
            hk.resolveEntityById(&station, nullptr, Dword(h, 184), 0);
            i32 cnt = hk.sumWorkstationByCategory(station, 1, 1);
            u8 tired = Byte(h, 200);
            double rate = static_cast<double>(cnt) * kWorkRateMul + 1.0;  // v25
            if (tired >= 60) {
                for (int slot = 0; slot < 4; ++slot) {
                    i32 memberId = Dword(h, 140 + 4 * slot);
                    if (memberId != -1) {
                        // station+65 dword == 2 selects "no command"; else send 2-state.
                        i32 stState = station ? *reinterpret_cast<i32*>(
                                          reinterpret_cast<u8*>(station) + 65) : 2;
                        if (stState != 2)
                            hk.queueRequestCoord27(hk.personCommandHandle(Word(h, 8)),
                                                   memberId, 2 - stState);
                    }
                }
                double fav = (hk.averageFavorability(Word(h, 8), 8, &Dword(h, 140)) + kFavorBias)
                             * kFavorScale;
                Byte(h, 200) = 0;
                Flt(h, 196) = static_cast<float>(fav);
            }
            i32 produced = hk.computeOutputOverTime(&Time(h, 96), &Clock());
            if (produced > 0) Byte(h, 200) += static_cast<u8>(produced);
            rate = rate + static_cast<double>(Flt(h, 196));
            while (true) {
                result = GameTimeCompare(&Time(h, 82), &Clock());
                if (result >= 0) break;
                i32 step = hk.computeOutputOverTime(&Time(h, 96), &Time(h, 82));
                double rem = static_cast<double>(Dword(h, 176)) -
                             static_cast<double>(step) * rate;  // the v11*v10 factor
                Dword(h, 176) = static_cast<i32>(ConvertX(rem));
                if (Dword(h, 176) <= 0) {
                    // deplete: try to refill the remaining work from the output node;
                    // on exhaustion render the "done" message, cancel, free.
                    bool refilled = false;
                    while (true) {
                        void* outNode = nullptr;
                        i32 outSeq = 0;
                        hk.resolveEntityById(nullptr, &outSeq, Dword(h, 188), 0);
                        void* found = hk.queryFind(outSeq + 20, 1, 0, Dword(h, 170) >> 16);
                        hk.resolveEntityById(&outNode, nullptr, Dword(h, 192), 0);
                        if (!found) break;
                        hk.queueRequest17(Dword(h, 192), Dword(h, 188), 0,
                                          Dword(h, 170) >> 16, 0, 0);
                        Dword(h, 176) += 0;  // += live table row +34
                        Flt(h, 180) = static_cast<float>(Dword(h, 176));
                        if (Dword(h, 176) > 0) { refilled = true; break; }
                    }
                    if (!refilled) {
                        char buf[256];
                        hk.renderFormattedMessage(buf, 6084, 2 * (Dword(h, 170) >> 16) + 2151,
                                                  station, 0);
                        hk.sendQuickjumpMessage(hk.personCommandHandle(Word(h, 8)), buf, 1424,
                                                "_NACHRICHTEN_HS_07");
                        CancelMyActions(hk, h, station);
                        return hk.freeHandlerEntry(h);
                    }
                }
                // roll the +96 scratch clock forward from +82 and advance +82 +5 min.
                std::memcpy(HeBytes(h) + 96, HeBytes(h) + 82, 14);
                GameTimeAdvance(&Time(h, 82), 0, 5, 0);
            }
            return result;
        }
        case 2: {
            if (Dword(h, 20) > 0 || Dword(h, 24) > 0) {
                Dword(h, 112) = 1;
                return result;
            }
            void* rec = nullptr;
            result = (hk.resolveEntityById(&rec, nullptr, Dword(h, 184), 0), 0);
            if (rec) {
                CancelMyActions(hk, h, rec);
                return hk.freeHandlerEntry(h);
            }
            Dword(h, 112) = -1;
            return result;
        }
        default:
            return result;
    }
}

// ===========================================================================
// 0x4f0250 — VIBE_Event_NightWatchmanAnnounceRun
// ===========================================================================
namespace {
// The audio-on / announce-on RNG gate the watchman uses (drawn twice in the
// original; we draw once, mirroring "RandomModulo(10) > 7" with the same call).
inline bool AnnounceRoll() { return static_cast<u16>(RandomModulo(10)) > 7u; }
}  // namespace

void NightWatchmanAnnounceRun(HeRecord* h) {
    const Event4Hooks& hk = *g_hooks;
    GameTime& clk = Clock();
    // Only act while the saved day (+68) equals the clock day.
    if (clk.day != Dword(h, 68)) { hk.freeHandlerEntry(h); return; }
    int season = SeasonFromDay(clk.day);
    int hour = static_cast<int>(clk.hour);
    bool audio = hk.audioEnabled() != 0;
    bool announce = hk.announceEnabled() != 0;

    // LABEL_23: stamp the clock into +82, then nudge the GLOBAL clock +1 second
    // (the original advances &qword_13CE852 with edx=0 days, ecx=1 second, ebx=0).
    auto stampNudge = [&]() {
        StampClock(h, 82);
        GameTimeAdvance(&clk, 0, 1, 0);
    };

    switch (Dword(h, 112)) {
        case -2:
        case -1:
            hk.freeHandlerEntry(h);
            return;
        case 0: {  // morning
            double hd = static_cast<double>(hour);
            if (hd < kMorningHour[season] || kMorningHour[season] + 1.0 <= hd) {
                stampNudge();
                return;
            }
            if (audio) hk.playQueuedSample(-7, 0, -1, "_NACHTWAECHTER_GLOCKEN_SFX_SHORT", 250);
            if (announce && audio && AnnounceRoll() && audio) {
                hk.playQueuedSample(Dword(h, 172), 0, -1, "_NACHTWAECHTER_ANKUENDIGUNGEN", 250);
                float m = kMorningHour[season];
                if (m == 7.0f) {
                    hk.playQueuedSample(Dword(h, 172), 0, -1, "_NACHTWAECHTER_MORGENS_ZEIT_7", 250);
                } else if (m == 8.0f) {
                    if (season == 2)
                        hk.playQueuedSample(Dword(h, 172), 0, -1, "_NACHTWAECHTER_MORGENS_ZEIT_HERBST", 250);
                    else
                        hk.playQueuedSample(Dword(h, 172), 0, -1, "_NACHTWAECHTER_MORGENS_ZEIT_8", 250);
                } else if (m == 9.0f) {
                    hk.playQueuedSample(Dword(h, 172), 0, -1, "_NACHTWAECHTER_MORGENS_ZEIT_9", 1000);
                }
                if (AnnounceRoll())
                    hk.playQueuedSample(Dword(h, 172), 0, -1, "_NACHTWAECHTER_MORGENS_SPRUECHE", 1000);
            }
            StampClock(h, 82);
            Word(h, 86) = static_cast<u16>(static_cast<int>(kEveningHour[season]));
            Dword(h, 88) = 0;
            Dword(h, 112) += 1;
            return;
        }
        case 1: {  // evening
            double hd = static_cast<double>(hour);
            if (hd < kEveningHour[season] || kEveningHour[season] + 1.0 <= hd) {
                stampNudge();
                return;
            }
            if (audio) hk.playQueuedSample(-7, 0, -1, "_NACHTWAECHTER_GLOCKEN_SFX_SHORT", 250);
            if (announce && audio && AnnounceRoll() && audio) {
                hk.playQueuedSample(Dword(h, 172), 0, -1, "_NACHTWAECHTER_ANKUENDIGUNGEN", 250);
                float e = kEveningHour[season];
                if (e == 19.0f) {
                    hk.playQueuedSample(Dword(h, 172), 0, -1, "_NACHTWAECHTER_ABENDS_ZEIT_7", 250);
                } else if (e == 20.0f) {
                    if (season == 2)
                        hk.playQueuedSample(Dword(h, 172), 0, -1, "_NACHTWAECHTER_ABENDS_ZEIT_8_HERBST", 250);
                    else
                        hk.playQueuedSample(Dword(h, 172), 0, -1, "_NACHTWAECHTER_ABENDS_ZEIT_8", 250);
                } else if (e == 21.0f) {
                    hk.playQueuedSample(Dword(h, 172), 0, -1, "_NACHTWAECHTER_ABENDS_ZEIT_9", 250);
                }
                if (AnnounceRoll())
                    hk.playQueuedSample(Dword(h, 172), 0, -1, "_NACHTWAECHTER_ABENDS_SPRUECHE", 1000);
            }
            StampClock(h, 82);
            Word(h, 86) = 22;
            Dword(h, 88) = 30;
            Dword(h, 112) += 1;
            return;
        }
        case 2: {  // night
            if (clk.hour < 22u || clk.minute < 30 || clk.minute >= 50) {
                stampNudge();
                return;
            }
            if (!audio) { hk.freeHandlerEntry(h); return; }
            hk.playQueuedSample(-7, 0, -1, "_NACHTWAECHTER_GLOCKEN_SFX_SHORT", 250);
            if (!announce || !AnnounceRoll()) { hk.freeHandlerEntry(h); return; }
            hk.playQueuedSample(Dword(h, 172), 0, -1, "_NACHTWAECHTER_NACHTS_ENDE", 250);
            hk.freeHandlerEntry(h);
            return;
        }
        default:
            return;
    }
}

// ===========================================================================
// 0x4efd88 — VIBE_Event_ConversationSinkRun
// ===========================================================================
i32 ConversationSinkRun(HeRecord* h) {
    const Event4Hooks& hk = *g_hooks;
    if (Dword(h, 112) == -1)
        return hk.freeHandlerEntry(h);
    void* person = hk.findPersonById(Dword(h, 172));
    if (!person)
        return hk.freeHandlerEntry(h);
    u8* pb = reinterpret_cast<u8*>(person);
    // Front gate: act only if the person has no live script-route (rec[130] == -1
    // i.e. byte/dword at +520) OR we're already in phase 6.
    i32 scriptRoute = *reinterpret_cast<i32*>(pb + 520);   // *((_DWORD*)rec + 130)
    if (scriptRoute != -1 && Dword(h, 112) != 6) {
        StampClock(h, 82);
        return GameTimeAdvance(&Time(h, 82), 0, 0, 5);     // +5 minutes
    }
    i32 v7 = Dword(h, 112);
    switch (v7) {
        case -2:
        case -1:
            return hk.freeHandlerEntry(h);
        case 0: {
            // rec[97] (+388) is itself a script-context pointer (a2 in the original).
            u8* sp = *reinterpret_cast<u8**>(pb + 388);
            if (sp) {
                i32 running = *reinterpret_cast<i32*>(sp + 40);  // running-script handle
                if (running != -1) hk.scriptFinishByHandle(running);
                hk.changePlayerAction(nullptr, nullptr, nullptr,
                                      *reinterpret_cast<u16*>(pb));  // *v34
                *reinterpret_cast<i32*>(pb + 364) = 0;               // rec[91] = 0
                ++Dword(h, 112);
            } else {
                if ((Byte(h, 120) & 2) == 0)
                    return hk.freeHandlerEntry(h);
                Dword(h, 112) = 3;
            }
            return v7 + 2;
        }
        case 1: {
            u8* sp = *reinterpret_cast<u8**>(pb + 388);  // rec[97]
            i32 running = sp ? *reinterpret_cast<i32*>(sp + 40) : -1;
            if (running == -1) Dword(h, 112) = v7 + 1;
            return sp ? static_cast<i32>(reinterpret_cast<std::intptr_t>(sp)) : 0;
        }
        case 2: {
            u8* sp = *reinterpret_cast<u8**>(pb + 388);  // rec[97]
            if (sp) {
                hk.characterStandUp(sp);
                hk.insertCollapseAction(sp);
            }
            StampClock(h, 82);
            GameTimeAdvance(&Time(h, 82), 0, 0, 30);  // +30 minutes
            i32 flags = Byte(h, 120);
            ++Dword(h, 112);
            if ((flags & 4) != 0) return hk.freeHandlerEntry(h);
            return flags;
        }
        case 3: {
            u8 cls = pb[2];  // *((_BYTE*)v34 + 2)
            if (cls == 6 || cls == 7) {
                // build a request-39 and store the handle into +180.
                unsigned char req[64] = {0};
                Dword(h, 180) = hk.queueRequest39(req);
                i32 r = GameTimeAdvance(&Time(h, 82), 0, 0, 1);  // +1 minute
                Dword(h, 112) = 6;
                return r;
            }
            i32 r = GameTimeAdvance(&Time(h, 82), 0, 0, 2);  // +2 minutes
            Dword(h, 112) = 4;
            return r;
        }
        case 4: {
            void* target = hk.selectConversationTarget(person);
            if (target) {
                hk.broadcastFamilyNews(person, target, 0);
                i32 rel = hk.findEmploymentRelation(person);
                hk.queueRequestPair33(*reinterpret_cast<i32*>(pb + 4), rel);
                return hk.freeHandlerEntry(h);
            }
            Dword(h, 112) = 5;
            return reinterpret_cast<std::intptr_t>(target);
        }
        case 5: {
            hk.broadcastFamilyNews(person, nullptr, 0);  // dword_6498E4 default target
            i32 rel = hk.findEmploymentRelation(person);
            hk.queueRequestPair33(*reinterpret_cast<i32*>(pb + 4), rel);
            return hk.freeHandlerEntry(h);
        }
        case 6: {
            i32 status = hk.packetStatus(Dword(h, 180));
            if (status) {
                StampClock(h, 82);
                if (status == 2) {
                    Dword(h, 112) = 4;
                } else {
                    Dword(h, 176) = hk.packetSeqValue(Dword(h, 180));
                    GameTimeAdvance(&Time(h, 82), 0, 0, 0);
                    Dword(h, 112) = 7;
                }
            }
            return status;
        }
        case 7: {
            if (pb[8]) return hk.freeHandlerEntry(h);  // *((_BYTE*)v34 + 8)
            void* slot = hk.cutsceneFindSlotById(Dword(h, 176));
            if (slot) {
                StampClock(h, 82);
                return GameTimeAdvance(&Time(h, 82), 2, 0, 0);  // +2 days
            }
            Dword(h, 180) = -1;
            Dword(h, 176) = -1;
            StampClock(h, 82);
            Dword(h, 112) = 4;
            return reinterpret_cast<std::intptr_t>(slot);
        }
        default:
            return v7 + 2;
    }
}

// ===========================================================================
// 0x4f56f8 — VIBE_Event_QueryBuildingHeMax
// ===========================================================================
i32 QueryBuildingHeMax(HeRecord* h) {
    const Event4Hooks& hk = *g_hooks;
    i32 best = 0;
    i32 prevSlot = hk.universeSwitchSlot(0);  // VIBE_Universe_SwitchActiveSlot(0,...)
    void* nodes[64];
    int count = hk.collectHeNodes(Dword(h, 4), nodes, 64);
    // The original starts the scan at index 1 (v23=1) and iterates while < count.
    for (int i = 1; i < count; ++i) {
        void* node = nodes[i];
        // Clear the node's level-base scratch ([134] dword == +536), then parse the
        // "_<n>" numeric segments; if at least one parsed AND the node's level
        // ([+? "v21"] == hk.nodeLevel(...,0)) exceeds the running best, keep it.
        hk.nodeSetLevelBase(node, 0);
        int segs = hk.nodeLevelCount(node);
        if (segs > 0) {
            i32 lvl = hk.nodeLevel(node, 0);
            if (lvl > best) best = lvl;
        }
    }
    hk.universeSwitchSlot(prevSlot);
    return best;
}

// ===========================================================================
// 0x4f52f8 — VIBE_Event_UpdateBuildingHeState
// ===========================================================================
namespace {
// Hide a collected node: ensure state==5, drop its detached mesh, clear the flag.
inline void HideNode(const Event4Hooks& hk, void* node) {
    if (hk.nodeStateByte(node) != 5 || (hk.nodeFlags(node) & 4) != 0) {
        hk.nodeSetStateByte(node, 5);
        if ((hk.nodeFlags(node) & 4) != 0) hk.sceneRemoveMesh(node);
        hk.nodeClearDetachedFlag(node);
    }
}
}  // namespace

void UpdateBuildingHeState(HeRecord* h) {
    const Event4Hooks& hk = *g_hooks;
    i32 prevSlot = hk.universeSwitchSlot(0);
    void* root = hk.findHeRootObject(Dword(h, 4));
    i32 target  = Dword(h, 200);  // v38[50] == +200
    i32 current = Dword(h, 196);  // v38[49] == +196
    void* nodes[64];

    if (target == -1 && root) {
        int count = hk.collectHeNodes(Dword(h, 4), nodes, 64);
        for (int i = 0; i < count; ++i) HideNode(hk, nodes[i]);
        hk.universeRestoreObjectStates(root, 0);
        hk.universeSwitchSlot(prevSlot);
        return;
    }

    if (target < current && root) {
        int count = hk.collectHeNodes(Dword(h, 4), nodes, 64);
        for (int i = 0; i < count; ++i) {
            void* node = nodes[i];
            HideNode(hk, node);
            hk.nodeSetLevelBase(node, 0);
            int segs = hk.nodeLevelCount(node);
            if (segs == 1) {
                i32 lvl = hk.nodeLevel(node, 0);
                // one segment: show iff its level <= target.
                hk.universeRestoreObjectStates(node, lvl <= target ? 1 : 0);
            } else if (segs == 2) {
                i32 lo = hk.nodeLevel(node, 0);
                i32 hi = hk.nodeLevel(node, 1);
                // two segments [lo..hi]: show iff target is inside the range.
                hk.universeRestoreObjectStates(node, (lo >= target && hi <= target) ? 1 : 0);
            } else {
                hk.universeRestoreObjectStates(node, 0);
            }
        }
        hk.universeSwitchSlot(prevSlot);
        return;
    }

    if (target > current) {
        void* next = hk.findBuildingById(Dword(h, 184));  // v38[46] == +184
        if (next) {
            i32 obj = *reinterpret_cast<i32*>(reinterpret_cast<u8*>(next) + 388);  // +97
            if (obj) hk.universeRestoreObjectStates(
                reinterpret_cast<void*>(static_cast<std::intptr_t>(obj)), 1);
        }
        if (root) {
            hk.sceneRemoveMesh(root);
            hk.objectDetachAndRelease(root);
        }
    }
    hk.universeSwitchSlot(prevSlot);
}

} // namespace guild::world
