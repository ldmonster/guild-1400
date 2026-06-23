#pragma once
// ai_recon5_decisions.h — reconstructed AI decision logic from gilde.exe.
//
// This cluster reconstructs the *genuine decision math / control flow* of four
// AI routines, with the outbound couplings (person/object record lookups,
// money-table conversions, command-queue emission, RNG) factored out behind an
// inert-default hooks struct so the pure logic is testable in isolation.
//
// Reconstructed (1:1) here:
//   gilde.exe 0x46670c — VIBE_AiCardGame_PlaceBet            (bet curve + RNG seat)
//   gilde.exe 0x4675b8 — VIBE_AiMethod_SelectConversationTarget (candidate scan/pick)
//   gilde.exe 0x47a524 — VIBE_AiObject_CountInventoryMatch   (recipe/inventory scan)
//   gilde.exe 0x47d568 — VIBE_AiPlayer_ExecThreaten          (rating+rand gate)
//
// NOT reconstructed:
//   gilde.exe 0x469204 — VIBE_AiMethodStack_Contains : already present as
//     guild::ai::MethodStack::Contains (src/ai/methodstack.cpp). Skipped (ODR).
//   gilde.exe 0x47a2b4 — VIBE_AiAction_LoadBuildingGraphic : pure VFS path-build
//     (sprintf "%sgebaeude/*%s.ogr" + VIBE_Vfs_ResolveAndBuildPath fallback loop)
//     plus a graphic-load command emission; carries no AI decision logic of its
//     own beyond the type guard. Omitted per task rule 3 (pure path-build).
//
// Recovered float constants (get_bytes):
//   dbl_61A188 = 10000.0   dbl_61A190 = 0.1
//   flt_61A198 = 0.25      flt_61A19C = 0.1
//   flt_61A1A0 = -0.2      flt_61A1A4 = 0.11   flt_61A1A8 = 0.2
//   flt_62675C = 0x38000100 (~1/32768, RNG scale)        // VIBE_Math_RandomFloatScaled
//   flt_6266F4 = 0.6  flt_6266F8 = 0.4  flt_6266FC = 0.5  // ComputeRatingCurveA
//
#include "guild/common/types.h"

namespace guild::sim {

using guild::u8;
using guild::u16;
using guild::u32;
using guild::i16;
using guild::i32;
using f32 = float;

// ---------------------------------------------------------------------------
// Outbound couplings, modeled as inert-default hooks. Defaults make every leaf
// behave like "no data / RNG returns 0" so callers see deterministic results;
// tests inject deterministic stand-ins.
// ---------------------------------------------------------------------------
struct AiRecon5Hooks {
    // VIBE_Util_RandNext (0x5cb8bc) — raw CRT-style RNG, returns a 15-bit-ish int.
    // RandomFloatScaled (0x58b910) = (double)randNext() * flt_62675C.
    // RandomModulo (0x58b89c) = n ? randNext() % n : 0.
    i32 (*randNext)() = nullptr;

    // VIBE_Person_ComputeTotalWealth (0x591f7c) — total wealth of a person id.
    i32 (*personTotalWealth)(i32 personId) = nullptr;
    // VIBE_Money_ConvertToDisplayCoord (0x58f14c) — wealth -> display units.
    i32 (*moneyToDisplay)(i32 wealth, u8 mode) = nullptr;
    // VIBE_Money_MultiplyByRate (0x58f19c) — value * rate-table[mode].
    i32 (*moneyMulByRate)(i32 value, u8 mode) = nullptr;
    // VIBE_MeisterAi_SelectMoodColor (0x4664d8) — picks a mood-color word.
    i16 (*selectMoodColor)() = nullptr;

    // VIBE_Building_ComputeRatingCurveA (0x58a6e8) — perceived-strength oracle
    // used by ExecThreaten. Returns a [~0..1] rating for (selfBuilding,target).
    f32 (*ratingCurveA)() = nullptr;
};

extern const AiRecon5Hooks kInertAiRecon5Hooks;

// Float scale used by RandomFloatScaled (flt_62675C reinterpreted).
double RandomFloatScaled(const AiRecon5Hooks& hk);   // randNext() * scale
i32    RandomModulo(const AiRecon5Hooks& hk, u16 n);  // n ? randNext()%n : 0

// ---------------------------------------------------------------------------
// 0x46670c — VIBE_AiCardGame_PlaceBet
//
// Decides/commits the AI's wager at the start of a tavern "17" round.
// Inputs (recovered from __usercall a1@eax person ptr, a2@bl bet-multiplier):
//   personId       — the human seat's person id (orig a1; must be != 0)
//   personTypeByte — *(a1+2); must equal 6 (a seat occupied by the human)
//   betMult        — a2; must be 1, 2 or 4 (Hold/Raise/Knock multiplier)
//   familyAgg      — *((float*)FamilyRecord+22): an aggression/confidence float
//                    that biases which seat gets the strong hand and is itself
//                    nudged by the outcome (FamilyRecord from 0x58c408).
// Output: a CardBet describing the committed stake and seat assignment, plus
// the updated familyAgg. ok==false means the guard rejected (returns 0).
//
// Bet curve (1:1):
//   wealth = moneyToDisplay(personTotalWealth(personId), byte_6477A1==0);
//   w      = (wealth >= 10000.0) ? (double)wealth : 10000.0;
//   raw    = w * 0.1 / (log10(w) + 1.0) * (betMult * 0.25);
//   stakeA = moneyMulByRate((int)raw, rate);    // a1[0]
//   stakeB = (int)((double)stakeA * 0.1);        // a1[4]  (raise increment)
// Seat assignment (1:1): if RandomFloatScaled() <= familyAgg the human (a1) is
//   placed at seat B (+40) and the strong mood word at A; else human at seat A
//   (+12). familyAgg is then nudged by +/-0.2/-0.2 +/- rand*0.11.
struct CardBet {
    bool ok        = false;
    i32  stakeA    = 0;     // committed stake (state +0)
    i32  stakeB    = 0;     // raise increment (state +4)
    i16  moodColor = 0;     // selectMoodColor() result placed at the strong seat
    int  humanSeat = 0;     // 0 == seat A (+12), 1 == seat B (+40)
    f32  familyAgg = 0.0f;  // updated aggression/confidence value
};

// familyAggIn is *((float*)FamilyRecord+22) read before the call.
CardBet AiCardGame_PlaceBet(const AiRecon5Hooks& hk,
                            i32 personId, u8 personTypeByte, u8 betMult,
                            f32 familyAggIn);

// ---------------------------------------------------------------------------
// 0x47d568 — VIBE_AiPlayer_ExecThreaten
//
// Executes a "threaten" intrigue action. Genuine decision: only when the actor
// command byte (*a1) == 7 and the target person record resolves does the action
// run; whether the *slot-reset* sub-command is queued is gated by
//   ratingCurveA() + RandomFloatScaled() >= 1.0.
// The command emission itself is a coupling; here we report the decision.
//   result==0  -> rejected (cmd byte != 7, or target record missing)
//   result==1  -> action executed; doSlotReset tells whether the >=1.0 branch hit
struct ThreatenDecision {
    int  result      = 0;     // 0 reject, 1 executed
    bool doSlotReset = false; // the rating+rand >= 1.0 branch
};

// cmdByte = *a1; targetResolved = (FindRecordById(*(a1+1)) != 0).
ThreatenDecision AiPlayer_ExecThreaten(const AiRecon5Hooks& hk,
                                       u8 cmdByte, bool targetResolved);

// ---------------------------------------------------------------------------
// 0x4675b8 — VIBE_AiMethod_SelectConversationTarget
//
// Scans a person's 5-slot social ring (+12..+28, ids at slot+92), collects the
// "talkable" candidates (record present, alive byte +8 set, relation word [5]
// > 0x0B), counts them (v19) and the eligible subset (v5). Picks one at random
// among the eligible. Then a personType (+2) switch:
//   typeByte == 5  -> "leave" path: returns the random pick if any; else if no
//      candidates returns 0; else issues a batch of relation-clear commands
//      (coupling) and returns 0.
//   otherwise      -> returns self record if it's talkable (alive, type in
//      (1,10), and flag [229]&4 clear); else the random pick if its type is in
//      (1,10); else 0.
// The genuine logic reconstructed here is the candidate gathering + random pick
// + type gating, expressed over an injected slot view.
//
// A candidate slot, as the scan sees a resolved record:
struct ConvCandidate {
    bool present     = false; // FindRecordById(slotId) != 0
    u8   aliveByte   = 0;     // *(rec+8)
    u16  relation    = 0;     // (u16)rec[5]
    u8   typeByte    = 0;     // *(rec+2)
    i32  recordId    = 0;     // identity returned to the caller (rec ptr surrogate)
};

struct ConvSelfRecord {
    bool present   = false;   // FindRecordById(*(a1+92)) != 0
    u8   aliveByte = 0;       // *(rec+8)
    u8   typeByte  = 0;       // *(rec+2)
    u16  flag229   = 0;       // rec[229]
    i32  recordId  = 0;
};

struct ConvResult {
    bool        haveTarget   = false;
    i32         targetId     = 0;
    // For typeByte==5 callers: relation-clear commands would be emitted; we
    // report the count of slots that matched (coupling left to the caller).
    int         relClearCount = 0;
};

// slots: up to 5 ring entries (a1+12..+28). personTypeByte = *(a1+2).
// self: the actor's own record (FindRecordById(*(a1+92))).
ConvResult AiMethod_SelectConversationTarget(const AiRecon5Hooks& hk,
                                             const ConvCandidate* slots,
                                             int slotCount,
                                             u8 personTypeByte,
                                             const ConvSelfRecord& self);

// ---------------------------------------------------------------------------
// 0x47a524 — VIBE_AiObject_CountInventoryMatch
//
// Walks a recipe/ingredient list (count at +34, 2-byte entries from +35) and
// for each ingredient resolves whether the actor can supply it from inventory
// or by purchase, stopping at the first ingredient that is *not* satisfiable
// (the "missing" one). On a miss it writes that ingredient's class word to *a1
// and the supplying object id (or -1) to *a2 and returns 1; if all are present
// it returns 0.
//
// The per-ingredient availability checks (VIBE_GameObject_QueryFind iterators)
// are couplings; the genuine logic is the loop structure, the high-bit mask
// (HIBYTE &= ~0x80 on each class word), the category gate (slot byte from the
// item-class table: 33 -> always satisfiable; 2 or 6 -> needs lookup; else ->
// direct "need" check), and the count/threshold (+33) comparison.
//
// Injected per-ingredient view (one entry per recipe slot, in order):
struct InvIngredient {
    u16  classWord;     // *(v3+35), top bit masked off by the routine
    u8   itemCategory;  // *(65*classWord + item-class table) : 33 / 2 / 6 / other
    // results the QueryFind couplings would produce, supplied by caller/test:
    int  ownedCount;    // how many already owned (the v8 counter, !=253 entries)
    bool foundInStock;  // first owned match found (sets v6/v18/v20)
    bool foundForSale;  // fallback "buy" match found
    i32  supplyObjId;   // id of the supplying object when found (else -1)
    // LABEL_36 "need" gate (QueryFind type 0 on the ingredient class): true when
    // the actor already covers this ingredient directly. extraGateA/B model the
    // optional secondary-ingredient classes at (i+163)/(i+165): when nonzero the
    // gate also requires that class to be coverable.
    bool needCovered;   // QueryFind(class) != 0  (already have it)
    u16  extraGateA;    // *(i+163): 0 == no extra gate
    bool extraGateAOk;  // QueryFind(extraGateA) != 0
    u16  extraGateB;    // *(i+165): 0 == no extra gate
    bool extraGateBOk;  // QueryFind(extraGateB) != 0
};

struct InvMatch {
    int  matched   = 0;   // 1 == a missing ingredient was found; 0 == all present
    u16  classWord = 0;   // *a1 : the missing ingredient class
    i32  objId     = -1;  // *a2 : supplying object id, or -1
};

// threshold = *(recipe+33) (the "need" count). recipeCount = *(recipe+34).
InvMatch AiObject_CountInventoryMatch(const InvIngredient* ings, int recipeCount,
                                      u8 threshold);

} // namespace guild::sim
