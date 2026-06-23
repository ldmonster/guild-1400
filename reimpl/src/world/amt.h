#pragma once
// The Amt (public-office) per-turn economic passes — the simulation heartbeat.
//
// Per turn, VIBE_GameTick_BeginPlayerRound (gilde.exe 0x533188) drives the Amt
// passes in a fixed order. This module reconstructs the *rules core* of those
// passes — the arithmetic + the per-turn cycle order — with all mutations routed
// through a command hook (the originals commit through VIBE_Command_QueueRequest16
// and the lockstep network queue; here that is a forward-declared, settable hook
// so the logic stays standalone-testable). The GUI/office-panel and net-sync
// plumbing is deferred (see report); only the deterministic economic rules are
// recovered byte-for-byte.
//
// Translated functions (rules core):
//   VIBE_Tax_CollectGuildIncome      0x57abd8   (formula)
//   VIBE_Tax_CollectBuildingIncome   0x57b0e4   (formula)
//   VIBE_Tax_CollectPropertyIncome   0x57ad28   (formula)
//   VIBE_Tax_CollectStaffIncome      0x57af64   (formula)
//   VIBE_Tax_CollectOfficeAllTaxes   0x57b214   (dispatch order)
//   VIBE_Amt_ComputeOfficeWages      0x57b480   (wage formula)
//   VIBE_Amt_ProcessAllOfficeWages   0x57b6bc   (wage cycle)
//   VIBE_Amt_RunBuildingTaxPass      0x57b9ac   (tax pass cycle)
//   VIBE_Amt_ProcessLoanRepayments   0x57b304   (loan threshold)
//   VIBE_Amt_RunProductionPass       0x57d448   (production cycle)
//   VIBE_Amt_RunGoodsDistributionPass 0x57dd84  (distribution math)
//   VIBE_Amt_PickRandomEventBuildings 0x57e2a4  (event probability table)
//   the per-turn pass order from VIBE_GameTick_BeginPlayerRound 0x533188
#include <cstddef>

#include "guild/common/types.h"

namespace guild::world {

// ===========================================================================
// Recovered single-precision tuning constants (from get_bytes; see comments).
// ===========================================================================
// All five tax-rate scales are the same 0.01f single literal (0x3C23D70A).
constexpr float kTaxRateScale     = 0.0099999998f; // flt_625884/8, 6258A0/A4/A8
// Wage formula: officeRank * 100.0f * 32.0f * lawRate.
constexpr float kWageRankScale    = 100.0f;        // flt_6258AC (0x42C80000)
constexpr float kWageBaseScale    = 32.0f;         // flt_6258B0 (0x42000000)
// Goods-distribution active-building threshold (a double): 652.8.
constexpr double kGoodsDistribThreshold = 652.8;   // dbl_62598C (0x4084666666666666)
// Cart-cost per-good rates by transport mode (clamped good-value * rate + 0.5).
constexpr float kCartRateMode1 = 0.05f;            // flt_626A7C (0x3D4CCCCD)
constexpr float kCartRateMode2 = 0.1f;             // flt_626A78 (0x3DCCCCCD)
constexpr float kCartRateMode3 = 0.15f;            // flt_626A74 (0x3E19999A)
constexpr double kCartCostBase = 0.5;              // dbl_626A84 (0x3FE0000000000000)
constexpr int kCartValueFloor  = 32000;            // clamp lower bound
constexpr int kCartValueCeil   = 256000;           // clamp upper bound
// Event-building selection probability table (dword_577A84, 6 dwords). Indexed
// by the count of occupied production rooms (3..7); a d256+1 roll must be < the
// table entry for the building to qualify.
constexpr i32 kEventProbTable[6] = {96, 75, 40, 23, 9, 0};

// ===========================================================================
// MUTATION / SIM hooks (command lockstep). The real passes commit every money
// move through VIBE_Command_QueueRequest16(payer, recipient, amount, currency)
// and read live Person/Building tables. We surface the minimal set as settable
// hooks so the deterministic arithmetic is testable in isolation. Defaults:
// the transfer hook records nothing (returns false), the rate hook is identity.
// ===========================================================================

// A money transfer enqueued through the command queue (payer -> recipient).
// payer/recipient are object/account ids (-1 == "the bank/treasury sink" as the
// originals pass -1 for the city/office side). Returns true if "committed".
using AmtTransferHook = bool (*)(i32 payer, i32 recipient, i32 amount, int currency);
void AmtSetTransferHook(AmtTransferHook hook);
bool AmtCommitTransfer(i32 payer, i32 recipient, i32 amount, int currency);

// gilde.exe 0x58f19c — VIBE_Money_MultiplyByRate(amount, currencyId):
//   amount * dword_649A88[dword_13CD6F2[189*currencyId] >> 16]
// The multiplier comes from a per-city currency table; in isolation we model it
// as a settable scalar (default 1). The loan-repayment pass uses this to scale
// the per-turn interest base.
using AmtRateHook = i32 (*)(i32 amount, u8 currencyId);
void AmtSetRateHook(AmtRateHook hook);
i32 AmtMoneyMultiplyByRate(i32 amount, u8 currencyId);

// ===========================================================================
// Truncation helper (VIBE_Coord_ConvertX 0x5c6b08 = round-toward-zero). Every
//   v = x; VIBE_Coord_ConvertX(); (int)v   is just trunc(x).
// ===========================================================================
i32 AmtTrunc(double x);

// ===========================================================================
// Tax formulas (one per income kind). Each is:
//   amount = trunc( (float)rate * 0.01f * (float)account ), clamped at 0,
// and is only "collected" when account > 0 and the relevant finance-law book
// slot is not already full (>= 6 active laws). `kind` selects the city
// accumulator that bit-2 of `flags` adds into (parity with the originals'
// distinct dword_1235xxx totals).
// ===========================================================================
enum class TaxKind { Trade, Guild, Building, Property, Staff };

// Pure amount (shared by all five; matches each original's float chain).
i32 TaxComputeIncome(i32 rate, i32 account);

struct TaxLine {
    TaxKind kind;
    i32     amount = 0;       // computed tax (>= 0)
    bool    collected = false; // account > 0 && law slot not full
    bool    committed = false; // a transfer was enqueued (flags bit0)
};

// gilde.exe 0x57abd8 / 0x57b0e4 / 0x57ad28 / 0x57af64 — the four office-tax
// kinds beyond Trade. `activeFinanceLaws` is the count of active laws in the
// kind's finance-law book slot (0..6). `payer`/`recipient` are the account ids
// the transfer would move money between (flags bit0). `cityAccumulator`
// receives the amount when flags bit1 is set. Returns 1 if a line was produced
// (account>0 within an open law slot), else 0 (law slot full / account<=0).
int TaxCollectIncome(TaxKind kind, i32 rate, int activeFinanceLaws, i32 account,
                     i32 payer, i32 recipient, int flags, TaxLine* out,
                     i32* cityAccumulator);

// Aggregated per-office collection (gilde.exe 0x57b214 dispatch order):
//   guild OR trade (depending on whether the office building is the "guild"
//   kind), then building, then property (only if propertyValue > 0), then
//   staff. Returns the summed collected amount. The booleans mirror the
//   original's per-call gates.
struct OfficeTaxInput {
    bool isGuildOffice = false; // VIBE_Amt_IsOfficeBuildingValid()
    i32  tradeRate = 0;         // Gesetz(8) threshold
    i32  guildRate = 0;         // Gesetz(9)
    i32  buildingRate = 0;      // Gesetz(10)
    i32  propertyRate = 0;      // Gesetz(12)
    i32  staffRate = 0;         // Gesetz(11)
    int  activeFinanceLaws = 0; // shared finance-law book fill count
    i32  account = 0;           // dword_12CEAAC[player] (trade/guild/building base)
    i32  propertyValue = 0;     // dword_12CEAB8[player]
    i32  staffValue = 0;        // dword_12CEAB0+12CEAB4 (sum)
};
i32 TaxCollectOfficeAllTaxes(const OfficeTaxInput& in, i32 payer, i32 recipient,
                             int flags);

// ===========================================================================
// Wages (gilde.exe 0x57b480 VIBE_Amt_ComputeOfficeWages).
//   wage = trunc( officeRank * 100.0f * 32.0f * lawRate )
// computed once per office seat held in a building. The original computes two
// seats (the two office-holder bytes at +A76/+A79) and pays each.
// ===========================================================================
i32 AmtComputeWage(int officeRank, float lawRate);

struct WagePair {
    i32 wageA = 0; // seat at byte_12CEA76 (rank from Office_GetDefinition)
    i32 wageB = 0; // seat at byte_12CEA79
    bool valid = false;
};
// `rankA`/`rankB` are the per-seat office ranks (Office_GetDefinition HIBYTE);
// a seat with rank 0 / no holder yields 0. lawRate is Gesetz(14) field. Returns
// the pair; if `commit`, enqueues each non-zero wage as a transfer from the
// treasury (payer=-1) to the office account.
WagePair AmtComputeOfficeWages(int rankA, int rankB, float lawRate,
                               i32 officeAccount, bool commit);

// ===========================================================================
// Loan repayments (gilde.exe 0x57b304).
//   base = VIBE_Money_MultiplyByRate(4 - lawSlot + 10, currency);  perTurn = base
//   overdraftLimit = 2 * base
// A building with negative held currency whose |debt| exceeds the overdraft
// limit (and which has no lender) is foreclosed; otherwise the per-turn
// interest `base` is charged. This recovers the threshold arithmetic.
// ===========================================================================
struct LoanDecision {
    i32  perTurnInterest = 0; // base charged when the holder is in debt
    i32  overdraftLimit = 0;  // 2 * base
    bool foreclose = false;   // debt beyond limit, no lender AND no creditor record
    bool charge = false;      // holder owes -> charge interest this turn
    bool dunLender = false;   // 0x57b41b: lender present -> -10 relation penalty
};
// noCreditor models dword_12CE96C[i]==-1 (the "no creditor on record" marker);
// the binary foreclosure gate requires it in addition to !hasLender.
LoanDecision AmtEvaluateLoan(int lawSlot, u8 currency, i32 heldCurrency,
                             bool hasLender, bool noCreditor = true);

// ===========================================================================
// Goods distribution helpers (gilde.exe 0x57dd84). The pass counts "active"
// production buildings, then spawns replacement businesses to top the city up
// toward a target of 40 when supply is low. This recovers the spawn-count math.
//   if activeCount <= threshold(650.8):
//     if active < 30:  spawn (40 - active) / 2
//     elif active < 40: spawn rand(2)+1            (1 or 2)
//     else:             spawn 0
// ===========================================================================
int AmtGoodsDistributionSpawnCount(int activeCount, bool belowThreshold,
                                   int rand2);

// ===========================================================================
// Event-building qualification (gilde.exe 0x57e2a4). A candidate building with
// `occupiedRooms` occupied production rooms (3..7 -> table index 0..4) qualifies
// when a d256+1 roll is strictly less than kEventProbTable[occupiedRooms-3].
// ===========================================================================
bool AmtEventBuildingQualifies(int occupiedRooms, int d256Plus1Roll);

// ===========================================================================
// The per-turn Amt cycle (order from VIBE_GameTick_BeginPlayerRound 0x533188):
//   1. RunProductionPass        (recompute production, then goods distribution)
//   2. UpdateOfficeProsperity
//   3. RunBuildingTaxPass(flags=3)   then ProcessLoanRepayments
//   4. ProcessAllOfficeWages    then UpdateOffices
// This enum documents the order; AmtRunTurnCycle invokes the supplied callbacks
// in exactly this sequence (the engine wires the real passes; tests wire mocks
// to assert the order + observe state).
// ===========================================================================
enum class AmtPass {
    Production = 0,
    Prosperity,
    BuildingTax,
    LoanRepayments,
    OfficeWages,
    UpdateOffices,
    Count
};

// Records the order in which passes ran (for the e2e cycle test). `cb` is
// invoked once per pass in heartbeat order; it returns void. The driver fills
// `outOrder` (length AmtPass::Count) with the pass sequence actually run.
using AmtPassFn = void (*)(AmtPass pass, void* ctx);
void AmtRunTurnCycle(AmtPassFn cb, void* ctx, AmtPass* outOrder /*[Count]*/);

} // namespace guild::world
