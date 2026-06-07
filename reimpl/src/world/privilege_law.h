#pragma once
// Privilege / Gesetz / Office law-flow remainder (gilde.exe, imagebase 0x400000).
//
// This module recovers the still-untranslated *deterministic* members of the
// VIBE_Office_* candidate-presentation / candidacy-commit family plus the role-
// template lookup. The functions below are 1:1 ports of the Hex-Rays pseudocode;
// the side-effecting leaves they call (HUD widget builders, the lockstep command
// queue, the RNG, the form/window driver) have no fully reconstructed sibling, so
// — per the build model (one static lib from all of src/**, one exe per test) —
// they are routed through an installable hooks struct whose inert defaults live in
// this library .cpp. Tests install their own hooks; the integration test forwards
// the rank-requirement hook into the REAL reconstructed
// guild::world::OfficeGetRankRequirements (office.cpp).
//
// Functions reconstructed here (addr / size / VIBE name):
//   0x49db90  87  VIBE_Office_CollectActorsByOwner
//   0x47fcf0 265  VIBE_Office_ConfirmCandidacyDialog
//   0x47fc24 202  VIBE_Office_RenderRequirementText
//   0x555eb4 173  VIBE_Office_PrepareCandidatePage
//   0x556108 305  VIBE_Office_ShowCandidateCardListVariantA
//   0x55623c 305  VIBE_Office_ShowCandidateCardListVariantB
//   0x556370 313  VIBE_Office_ShowCandidateCardListVariantC

#include "guild/common/types.h"

namespace guild::world {

// ===========================================================================
// Recovered constant tables / layout strides
// ===========================================================================

// The candidate-card array each ShowCandidateCardListVariant* fills: the original
// advances the cursor by 14 u16 (28 bytes) per candidate. Within an entry:
//   [-10] (i16) screen x      (filled by the layout math)
//   [-9]  (i16) screen y
//   [2]   (i16) widget/object id (read back to enable the card)
// Modeled below as CandidateCard so tests can assert the deterministic layout.
constexpr int kCandidateCardStrideU16 = 14;

// Per-candidate vertical layout (recovered verbatim):
//   y starts at 40, advances by 112 per card; the loop runs while
//   cursor(112*i) < 112*count, i.e. exactly `count` cards.
constexpr int kCandidateCardY0     = 40;
constexpr int kCandidateCardPitch  = 112;

// CollectActorsByOwner (0x49db90) scans the 768-entry session-actor table
// (word_12CE910, stride 268 u16 = 536 bytes). For each entry the owner pointer
// lives at dword offset +97 of the actor, and the building/owner identity it is
// compared against lives at byte offset +136 of that owner. The scan collects up
// to 31 matching actor indices.
constexpr int kActorTableCount   = 768;
constexpr int kActorStrideU16    = 268;
constexpr int kActorOwnerDwordOff = 97;   // *( (DWORD*)actor + 97 )
constexpr int kActorMaxMatches   = 31;

// ===========================================================================
// Data views
// ===========================================================================

// A single candidate card slot (28 bytes / 14 u16). Only the three fields the
// originals touch are named; the remainder mirrors the stride so a flat array of
// these matches the binary's pointer arithmetic exactly.
struct CandidateCard {
    i16 x;          // [-10] after the +=14 advance -> slot+8 bytes
    i16 y;          // [-9]
    i16 objectId;   // [2]  widget id read back to enable the card
    i16 fill[11];   // pad to 14 u16
};
static_assert(sizeof(CandidateCard) == kCandidateCardStrideU16 * 2,
              "CandidateCard must be 28 bytes");

// Result of ConfirmCandidacyDialog / a candidacy commit attempt.
struct CandidacyCommit {
    bool eligible;   // GetRankRequirements returned non-zero AND record state ok
    i32  ageRoll;    // v6 + RandomModulo(v7+1-v6)  (the chosen age)
    u8   moneyRoll;  // v8 + RandomModulo(v9+1-v8)  (the chosen money/standing)
    u8   officeType; // the office type applied for
};

// ===========================================================================
// Hooks (installable; inert defaults defined in privilege_law.cpp)
// ===========================================================================
// Every cross-module leaf with no fully reconstructed target is routed here so
// the deterministic control flow / arithmetic stays exact and 1:1 while the
// side effects remain observable & inert-by-default. The integration test
// forwards `rankReq` into the REAL OfficeGetRankRequirements sibling.
struct PrivilegeLawHooks {
    // VIBE_Office_GetRankRequirements(rank, out24) -> 1/0. Fills a 24-byte block;
    // ConfirmCandidacyDialog reads age range [v6,v7] at dwords +0/+? and money
    // range at +? — we surface the four scalars the callers actually consume.
    // Default: returns 0 (no requirements -> ineligible).
    int (*rankReq)(u8 rank, i16* ageMin, i16* ageMax, u8* moneyMin,
                   u8* moneyMax) = nullptr;

    // VIBE_Math_RandomModulo(n) -> uniform in [0,n). Default deterministic 0.
    u16 (*randMod)(u32 n) = nullptr;

    // VIBE_BuildingType_ComputeVariantIndex(a,b) -> variant byte. Used by
    // RenderRequirementText. Default returns a (passthrough, deterministic).
    u8 (*computeVariant)(int a, int b) = nullptr;

    // The lockstep command commit for a confirmed candidacy: the original packs a
    // delta packet (BeginDeltaPacket + 3 AppendDeltaField + QueueRequestState22)
    // and a QueueRequest16. We surface the meaningful payload. Default: inert.
    void (*queueCandidacy)(i32 personId, i32 ageValue, u8 moneyValue,
                           u8 officeType) = nullptr;

    // VIBE_Command_EnqueueObjectInteraction(op, ...) from RenderRequirementText.
    // Default: inert, returns 0.
    int (*enqueueObjInteraction)(int op, int target, u8 variant,
                                 int extra) = nullptr;

    // VIBE_Office_ApplyForCandidacy(rec, type) tail call. Default: inert returns 1.
    int (*applyForCandidacy)(i32 personId, u8 officeType) = nullptr;

    // HUD page scaffolding (PrepareCandidatePage / card builders):
    //   pageWidth(windowKey) == (dword_67EB84[238*win]+2) >> 16  (the page width
    //   the layout math divides). Default: returns a fixed 640 so layout is
    //   deterministic.
    int (*pageWidth)(int windowKey) = nullptr;
    //   contentWidth() == dword_62D204[+117762] >> 16. Default 0.
    int (*contentWidth)() = nullptr;
    // VIBE_Hud_AddCenteredLabel / BuildPersonCard / Object_SetEnabled — observable.
    void (*addCenteredLabel)(int variantTag, int x, int w) = nullptr;
    int  (*buildPersonCard)(int x, i16 y, int kind, CandidateCard* slot) = nullptr;
    void (*setEnabled)(i16 objectId, int on) = nullptr;
    // Form/window driver (PrepareCandidatePage). Default inert.
    void (*selectWindow)(int formId, int windowId) = nullptr;
    void (*removeChildren)(int windowKey) = nullptr;
    void (*createScrollButtons)(int windowKey) = nullptr;
};

void PrivilegeLawSetHooks(const PrivilegeLawHooks& hooks);
const PrivilegeLawHooks& PrivilegeLawGetHooks();
void PrivilegeLawResetHooks(); // restore inert defaults

// ===========================================================================
// Reconstructed functions
// ===========================================================================

// gilde.exe 0x47fcf0 — VIBE_Office_ConfirmCandidacyDialog (eax=rec, dl=type).
// `recOffice2`==19 gate, then GetRankRequirements; on success rolls age & money
// in their recovered ranges (via randMod) and emits the candidacy delta packet +
// QueueRequest16, then tail-calls ApplyForCandidacy. Returns 1 on commit, 0 on
// gate/requirement failure. The rolls/payload are returned via `out`.
int OfficeConfirmCandidacyDialog(i32 personId, u8 officeType, u8 recOffice2,
                                 CandidacyCommit* out);

// gilde.exe 0x47fc24 — VIBE_Office_RenderRequirementText (al=rank).
// GetRankRequirements gate; flips a sticky parity byte `*parity` (init 2 -> seeded
// by randMod(2)); rolls a variant from randMod(12)+1 and randMod(ageRange), runs
// it through computeVariant, rolls an id offset, then enqueues an object
// interaction (op 19). Returns the interaction result, or -1 when no requirements.
// `parity` models the recovered byte_62EE34 sticky state (caller owns it).
int OfficeRenderRequirementText(u8 rank, u8* parity);

// gilde.exe 0x555eb4 — VIBE_Office_PrepareCandidatePage (eax=form, edx=win, ebx=alt).
// Selects the primary window, clears its children, and (when alt != -1) sets up
// the alternate window with scroll buttons. Returns 0 normally, leaving the
// active window on the primary. (When alt == -1 the original returns an
// uninitialized ecx; we faithfully return 0 in that branch as well — the only
// observable effect is the window scaffolding via hooks.)
int OfficePrepareCandidatePage(int formId, int windowId, int altWindowId);

// gilde.exe 0x556108 / 0x55623c / 0x556370 —
// VIBE_Office_ShowCandidateCardListVariant{A,B,C}.
// All three are byte-identical except the centered-label variant tag (8C8168 /
// 8C816C / 8C8170 -> 0/1/2 here). For `count` candidates they:
//   * PrepareCandidatePage(form, win, alt),
//   * add the centered header label,
//   * lay out `count` cards (x = (pageWidth - contentWidth)/2, y = 40 + 112*i)
//     into `cards[i]` and BuildPersonCard each,
//   * enable each card's widget object.
// Returns 1 when count>0, else 0 (no work). `cards` must hold >= count entries.
int OfficeShowCandidateCardList(int variant, int formId, int windowId,
                                int altWindowId, int count, CandidateCard* cards);

// gilde.exe 0x49db90 — VIBE_Office_CollectActorsByOwner.
// Scans the 768-entry actor table; for each actor whose owner pointer is non-null
// and whose owner identity matches `ownerKey`, records the actor's index. Stops at
// 31 matches or the end of the table. Writes matched indices into out[] (1-based
// write order exactly as the original's pre-increment) and returns the count.
// `ownerOf(index)` returns the owner-identity key for actor `index` (0 == none).
int OfficeCollectActorsByOwner(i32 ownerKey,
                               i32 (*ownerOf)(int actorIndex),
                               i32* out, int outCapacity);

} // namespace guild::world
