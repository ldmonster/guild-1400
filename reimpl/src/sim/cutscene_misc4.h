#pragma once
// ===========================================================================
// cutscene_misc4.{h,cpp} — the three remaining "big" VIBE_Cutscene_* bodies
// that the misc/misc2/misc3 slices left untranslated (gilde.exe, namespace
// guild::sim).
// ===========================================================================
//
// Wave 14 slice. The misc3 header comment over-promised; these three were NOT
// actually translated there (no bare-name definition exists for any of them):
//
//   * VIBE_Cutscene_RunParticipants     (gilde.exe 0x4aac78, 2152 bytes) — the
//     per-cutscene participant run driver. A multi-mode orchestration over the
//     slot's participant id array (slot.partIds, +52..) keyed off the world
//     flag word (word_63C740) and a per-type "run mode" byte (byte_11AE5CC).
//     Long sequence of cross-module side effects (net wait, command staging,
//     frame pump, dialog) wrapped around a small set of DETERMINISTIC scanners.
//   * VIBE_Cutscene_SalonFadeTransition (gilde.exe 0x4a9d04,  747 bytes) — the
//     salon scene fade/transition. Decides whether the requested (entity,season)
//     already matches the cached current scene (dword_631744 / dword_631748) —
//     in which case it just runs the fade-out — or whether a new scene must be
//     loaded, dispatching on building production-type via two packed global
//     arrays (dword_122DD5D / dword_122DDAC).
//   * VIBE_Cutscene_LeaseWindow         (gilde.exe 0x4a9868,  510 bytes) — the
//     lease (Pacht) offer window. Scans the lessor's offer table for the lessee
//     participant, clamps the asking rent against the lessee's funds, and (if
//     affordable) pumps the rent-slider window until the player accepts, then
//     reads back the negotiated rent.
//
// Each is translated 1:1 from Hex-Rays pseudocode: the deterministic decision
// kernels are exposed as standalone testable free functions, and the full
// control flow is driven over an installable hook surface (CutsceneMisc4Hooks,
// inert defaults defined in cutscene_misc4.cpp). We REUSE CutsceneSlot /
// CutsceneRng / the GetRandSeed/SetRandSeed seed-snapshot from sim/cutscene.h,
// and the Cutscene3State replay/frame globals from sim/cutscene_misc3.h.
//
// RECOVERED TABLES / CONSTANTS
//   * word_63C740 — world flag word. RunParticipants tests bit 2 (0x4, "local
//     player is the cutscene master / report path") and bit 3 (0x8, "render-only
//     participant pass"). LeaseWindow's accept button id is dword_75BF38 == 1210.
//   * byte_11AE5CC[20 * cutType] — the per-cutscene-type RunParticipants mode:
//     1 == the interactive master-driven branch (the big choice loop), 0 == the
//     non-master local replay branch, any other value == skip (no participant
//     run). cutType == slot.type-derived index (slot[+5]>>24 in the original).
//   * The participant scanners walk slot.partIds[0..partCount): a slot id of -1
//     marks an empty participant cell. "find prior valid" walks BACKWARD from a
//     start index to the last non-empty cell (or 0 if none) — used to pick the
//     anchor participant the camera/dialog returns to.
//   * Person record +2 "kind" byte: 6 (== bride/groom / wedding-class) and 7 are
//     the two classes that RunParticipants special-cases (kind 6 -> type-A voice
//     table dword_11AE5C4; kind 7 -> skip; else -> type-B table dword_11AE5C8).
//   * VIBE_Math_ClampValueRange (0x552734, util::ClampValueRange): LeaseWindow
//     clamps the rent with lo == base rent, value == lessee funds, mul == 32.
//   * dword_75BF38 lease accept button == 1210.
// ---------------------------------------------------------------------------
#include "guild/common/types.h"
#include "sim/cutscene.h"        // CutsceneSlot, CutsceneRng, kMaxParticipants
#include "sim/cutscene_misc3.h"  // Cutscene3State / Cutscene3()

namespace guild::sim {

// ---------------------------------------------------------------------------
// Recovered constants.
// ---------------------------------------------------------------------------
// word_63C740 flag bits read by RunParticipants.
constexpr u16 kWorldFlagMasterReport = 0x4;  // bit 2 — local player is master
constexpr u16 kWorldFlagRenderPass   = 0x8;  // bit 3 — render-only participant pass

// byte_11AE5CC[20*cutType] run-mode selector values.
enum CutsceneRunMode {
    kRunModeNonMaster   = 0,  // local replay branch (the trailing scan loop)
    kRunModeMaster      = 1,  // interactive master-driven choice loop
    // any other value -> skip (RunParticipants returns after Init/banner).
};

// Person record +2 "kind" byte values special-cased by RunParticipants.
constexpr u8 kPersonKindWeddingA = 6;  // -> dword_11AE5C4 (type-A voice table)
constexpr u8 kPersonKindWeddingB = 7;  // -> skipped entirely

// LeaseWindow accept button id (dword_75BF38) and clamp multiplier.
constexpr i32 kLeaseAcceptButtonId = 1210;
constexpr int kLeaseClampMul       = 32;

// SalonFadeTransition fade-out colour-fill gate: dword_6315C0[+38] & 0x20.
constexpr u8 kSalonSurfacePresentBit = 0x20;

// ---------------------------------------------------------------------------
// Leaf hooks — every cross-module side effect these bodies make. Tests install
// a recording mock; the inert default (all null) makes every leaf a no-op, the
// frame pump returns 0 ("stop") and the packet-status poll returns "done", so
// every loop terminates deterministically with no host state.
// ---------------------------------------------------------------------------
struct CutsceneMisc4Hooks {
    // --- the frame pump (GameLogic_RunFrameLoop) -------------------------------
    // Returns nonzero to keep looping, 0 to stop. RunParticipants/LeaseWindow
    // pass dword_631598 (with byte1|0x80 on the dialog-pump variants).
    int (*pumpFrame)(i32 flags, i32 a, void* payload) = nullptr;

    // --- person ----------------------------------------------------------------
    void* (*personFind)(i32 id) = nullptr;          // Person_FindRecordById
    u8    (*personKind)(void* person) = nullptr;    // record +2
    i32   (*personEntityId)(void* person) = nullptr;// record +4 (the id sent in cut-info)
    int   (*personSumCurrency)(void* person) = nullptr; // SumCurrencyHeld (LeaseWindow funds)

    // --- net / command (RunParticipants) ---------------------------------------
    void  (*queueFlagBlob)(int kind, void* blob) = nullptr;   // Command_QueueRequestFlagBlob32
    void  (*netRunWaitLoop)(void* arg) = nullptr;             // Net_RunWaitLoop
    void  (*stagePendingBlock)(unsigned cmd, void* blob) = nullptr; // Command_StagePendingBlock
    u32   (*requestSendCutInfo)(i32 entityId, i32 cutId) = nullptr; // Command_RequestSendCutInfo
    int   (*packetStatus)(u32 packetId) = nullptr;            // Command_GetPacketStatusById
    void  (*refreshGuildState)() = nullptr;                   // Amt_RefreshGuildState

    // --- ui / dialog -----------------------------------------------------------
    void  (*setStatusBanner)(const char* text) = nullptr;     // Hud_SetStatusBannerText
    void  (*initParticipantTable)(const CutsceneSlot* slot) = nullptr; // Cutscene_InitParticipantTable
    void  (*showParticipantDialog)(const CutsceneSlot* slot) = nullptr; // Cutscene_ShowParticipantDialog
    int   (*allParticipantsDone)(const CutsceneSlot* slot) = nullptr;   // Cutscene_AllParticipantsDone
    void  (*setLightGray)(int a, int b) = nullptr;            // Light_SetGrayColorThunk

    // --- the per-type voice/animation callback tables --------------------------
    // dword_11AE5C4[20*cutType] (kind-6 / weddingA) and dword_11AE5C8[20*cutType]
    // (the others). The original calls them as `((void(*)())tbl[..])()`; we route
    // both through a single hook that receives which table + the cutType.
    void  (*runTypeCallback)(int table, int cutType) = nullptr; // 4 -> 5C4, 8 -> 5C8

    // --- LeaseWindow engine leaves ---------------------------------------------
    // The rent-slider window pump: returns the negotiated rent when the player
    // accepts (button 1210), or `defaultRent` if cancelled. Inert default returns
    // defaultRent (no negotiation). This stands in for the whole Widget_AddSlider
    // + Form pump + Object_GetDataPtr/Money_MultiplyByRate readback.
    i32   (*leaseRunRentSlider)(i32 defaultRent, i32 funds) = nullptr;

    // --- SalonFadeTransition engine leaves -------------------------------------
    void  (*salonFadeOut)() = nullptr;     // the Surface_ColorFill + frame-pump cross-fade
    void  (*salonLoadScene)(int kind, i32 entityId, i16 season) = nullptr; // Scene_Load*+SwitchSlot
    void  (*salonReregisterFade)() = nullptr; // Fade_Unregister + Fade_Register(BLACK)
};

void SetCutsceneMisc4Hooks(const CutsceneMisc4Hooks* hooks);
const CutsceneMisc4Hooks& GetCutsceneMisc4Hooks();

// ===========================================================================
// DETERMINISTIC KERNELS (the testable cores of the three bodies).
// ===========================================================================

// gilde.exe 0x4aad57 / 0x4ab159 / 0x4aaecd — the forward participant scan that
// the original repeats in every branch: walk slot.partIds[0..partCount) and
// return the index of the first cell whose id equals `wantId`, or -1 if none.
// (RunParticipants uses it with wantId == slot.master to find the master's own
// participant cell.) An id of -1 means "empty cell" and never matches a real id.
int CutsceneFindParticipantById(const CutsceneSlot* slot, i32 wantId);

// gilde.exe 0x4ab2d0 / 0x4ab387 — the "find prior valid participant" backward
// scan: starting at `startIndex - 1`, walk DOWN to the first cell whose id is
// not -1; if none is found (or startIndex-1 <= 0) the anchor is 0. Returns the
// anchor participant index. Verbatim quirk: the loop guard is `index <= 0`
// (so index 0 is treated as "give up, anchor 0") — index 0's own emptiness is
// never tested here, matching the binary.
int CutsceneFindPriorValidParticipant(const CutsceneSlot* slot, int startIndex);

// gilde.exe 0x4aae8e — the RunParticipants run-mode selector: returns
// byte_11AE5CC[20*cutType]. We model the recovered semantic (the only values the
// table holds for valid cut types are 0 and 1) via the caller-supplied raw byte:
// 1 -> master branch, 0 -> non-master branch, other -> skip. This kernel simply
// classifies the raw mode byte into a CutsceneRunMode-or-skip decision.
//   returns: 1 (master), 0 (non-master), -1 (skip / no participant run).
int CutsceneClassifyRunMode(u8 rawModeByte);

// gilde.exe 0x4aadbb / 0x4aafab — the per-participant voice-table selector keyed
// off the person's +2 "kind" byte:
//   kind == 6  -> use table A (dword_11AE5C4), value 4 (the runTypeCallback arg)
//   kind == 7  -> skip entirely (return 0)
//   otherwise  -> use table B (dword_11AE5C8), value 8
// Returns 4, 8, or 0 (skip).
int CutsceneParticipantCallbackTable(u8 personKind);

// gilde.exe 0x4a9d12 / 0x4a9d1e..4a9d31 — the SalonFadeTransition cache-match
// decision. The current scene is cached in `cachedEntity` (dword_631744 OR
// dword_631748) with its season packed in cached[+39]>>16 (we pass it already
// extracted as `cachedSeason`). The transition is a pure fade (no scene reload)
// when the requested (entity, season) equals a cached scene. `replayGate` is
// dword_6315BC: when nonzero the whole call is a no-op.
//   returns: 0 -> replay-gated no-op
//            1 -> matches cached scene (fade only, no reload)
//            2 -> no match (must load a new scene)
int CutsceneSalonClassifyTransition(i32 replayGate,
                                    i32 reqEntity, i16 reqSeason,
                                    i32 cachedEntity1, i16 cachedSeason1,
                                    i32 cachedEntity2, i16 cachedSeason2);

// gilde.exe 0x4a9dfa..4a9f9f — the SalonFadeTransition scene-kind dispatch for
// the "load new scene" path. A "production-type" building (Building_IsProduction)
// loads a Gebaeude (building-interior) scene keyed off a packed byte from
// dword_122DD5D; a non-production building loads an Objekt scene keyed off
// dword_122DDAC. We expose the recovered branch decision:
//   isProductionType == true  -> kind 0 (Gebaeude scene)
//   isProductionType == false -> kind 1 (Objekt scene), UNLESS the packed
//       dispatch byte == -2 (0xFE) which means "no scene for this object" (skip).
// `packedDispatchByte` is the high byte the original sign-extends:
//   for production: (dword_122DD5D[obj]>>24); for objekt: (dword_122DDAC[obj+1]>>24).
//   returns: 0 (load Gebaeude), 1 (load Objekt), -1 (skip — packed byte == -2).
int CutsceneSalonSceneKind(bool isProductionType, int packedDispatchByte);

// gilde.exe 0x4a9885..4a98a3 — the LeaseWindow offer-table scan: walk the lessor
// offer list `offerLessees[0..count)` for the entry whose lessee id equals
// `lesseeId`; the matched slot is valid only if its parallel `offerValid[index]`
// is not -1. Returns the matched index, or -1 if not found / invalid.
int CutsceneLeaseFindOffer(const i32* offerLessees, const i32* offerValid,
                           int count, i32 lesseeId);

// gilde.exe 0x4a98e2..4a9916 — the LeaseWindow affordability gate. The asking
// rent is clamped against the lessee's funds via ClampValueRange(lo=baseRent,
// value=funds, mul=32); the window is only shown (slider pumped) when the
// lessee can afford at least the base rent, i.e. funds >= baseRent. Returns
// true when the rent-slider window should be presented.
bool CutsceneLeaseCanAfford(int funds, i32 baseRent);

// ===========================================================================
// FULL FLOW DRIVERS (1:1 control flow over the hook surface).
// ===========================================================================

// gilde.exe 0x4aac78 — VIBE_Cutscene_RunParticipants(slot@<eax>, a2@<edx>).
// Snapshots the cutscene RNG seed, inits the participant table, runs the mode
// path selected by (word_63C740, byte_11AE5CC[..]), then restores the seed and
// increments the duel/report mode (Cutscene3().duelMode). `worldFlags` is
// word_63C740; `runModeByte` is byte_11AE5CC[20*cutType]; `cutType` is the
// per-type index (slot[+5]>>24). The frame pump / dialog / command staging all
// route through the hooks. Returns the (restored) RNG seed, matching the tail
// `return VIBE_Cutscene_SetRandSeed(RandSeed)`.
i32 CutsceneRunParticipants(CutsceneRng& rng, const CutsceneSlot* slot,
                            u16 worldFlags, u8 runModeByte, int cutType);

// gilde.exe 0x4a9d04 — VIBE_Cutscene_SalonFadeTransition(entity@<eax>,
// season@<dx>, scratch@<edi>). Classifies the transition; on a cache match (or
// after loading the new scene) runs the fade-out and re-registers the BLACK
// fade if one is active. Returns the classification (0 gated / 1 fade / 2 load).
int CutsceneSalonFadeTransition(i32 reqEntity, i16 reqSeason,
                                i32 cachedEntity1, i16 cachedSeason1,
                                i32 cachedEntity2, i16 cachedSeason2,
                                bool isProductionType, int packedDispatchByte);

// gilde.exe 0x4a9868 — VIBE_Cutscene_LeaseWindow(self@<eax>, offers@<edx>).
// Finds the lessee in the offer table, computes affordability, and (if
// affordable) pumps the rent-slider window via leaseRunRentSlider, latching the
// negotiated rent. `funds` is the lessee's SumCurrencyHeld; `baseRent` is the
// offer's asking rent (v3[8]). `outRent` receives the negotiated rent (the base
// rent if not negotiated). Returns: 0 == not shown / not affordable, 1 == shown
// and accepted (negotiated), matching the +38 "accepted" flag (a1[38]).
int CutsceneLeaseWindow(const i32* offerLessees, const i32* offerValid,
                        int offerCount, i32 lesseeId, int funds, i32 baseRent,
                        i32* outRent);

}  // namespace guild::sim
