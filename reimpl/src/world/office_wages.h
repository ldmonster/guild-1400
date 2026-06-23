#pragma once
// gilde.exe 0x57b480 — VIBE_Amt_ComputeOfficeWages.
//
// The per-office wage computation: for a building/office slot it resolves the
// two office-holder seats (the two title bytes at +0xA76 / +0xA79 of the office
// record block), looks up each title's *rank* via VIBE_Office_GetDefinition,
// and pays each non-zero seat a wage of
//
//     wage = trunc( (double)rank * 100.0f * 32.0f * lawRate )
//
// where `lawRate` is a single-precision field pulled out of law record 14
// (VIBE_Gesetz_GetRecord(14, ...)). The two products (100.0f and 32.0f) are the
// binary's flt_6258AC / flt_6258B0; the whole chain promotes to double for the
// multiply, then VIBE_Coord_ConvertX (frndint, round-toward-zero) + an integer
// cast truncates the result toward zero.
//
// This module reconstructs that arithmetic 1:1. The original additionally reads
// live office/building arrays and commits each wage + an occasional random
// "office kind 3" bonus through the lockstep command queue
// (VIBE_Command_QueueRequest16 / VIBE_Command_RequestBuildOp90). Those live-table
// reads and command emissions are surfaced through an input struct + a settable
// command sink so the deterministic money math stays standalone-testable; the
// engine wires the real sink (see office_wages.cpp for the handoff note).
//
// NOTE: `world/amt.{h,cpp}` carries an *abstracted* `AmtComputeOfficeWages`
// (no provenance, no seat-gate, no bonus branch). This module is the faithful
// 0x57b480 reconstruction; the two coexist (different symbols, no ODR clash).
#include "guild/common/types.h"

namespace guild::world {

// ===========================================================================
// Recovered single-precision tuning constants (get_bytes; little-endian).
//   flt_6258AC @0x6258AC = 00 00 C8 42 = 100.0f   (rank scale)
//   flt_6258B0 @0x6258B0 = 00 00 00 42 =  32.0f   (base scale)
// (These are the same two literals world/amt.h names kWageRankScale /
// kWageBaseScale; redeclared here so the module is self-contained.)
// ===========================================================================
constexpr float kOfficeWageRankScale = 100.0f; // flt_6258AC (0x42C80000)
constexpr float kOfficeWageBaseScale = 32.0f;  // flt_6258B0 (0x42000000)

// ---------------------------------------------------------------------------
// VIBE_Coord_ConvertX 0x5c6b08: fldcw round-toward-zero, frndint, restore. The
// canonical pattern `v = x; ConvertX(); (int)v` is truncation of x toward zero.
// ---------------------------------------------------------------------------
i32 OfficeWageTrunc(double x);

// ---------------------------------------------------------------------------
// The wage of a single seat: trunc(rank * 100.0f * 32.0f * lawRate).
//   gilde.exe 0x57b55c / 0x57b598:
//     v9 = (double)HIBYTE(v19) * flt_6258AC * flt_6258B0 * v20;
//   (rank == HIBYTE of office-definition word; v20 == lawRate float.)
// ---------------------------------------------------------------------------
i32 OfficeWageForSeat(int rank, float lawRate);

// ===========================================================================
// Inputs the original reads from the live office/building arrays for one office
// record (index a1; the arrays have 536-byte stride). Field provenance:
//   officeId         byte_12CEA76[536*a1]  (seat A title id; *(a3+0))
//   deputyId         byte_12CEA79[536*a1]  (seat B title id; *(a3+1))
//   personId         word_12CE910[268*a1]  (== -1 when the office is vacant)
//   stateByte        byte_12CE912[536*a1]  (must be < 10; == 3 enables the bonus)
//   enabledByte      byte_12CEA918[536*a1] (must be non-zero)
//   rankA / rankB    HIBYTE(office-def word) from VIBE_Office_GetDefinition
//                    of officeId / deputyId respectively
//   bonusByte        BYTE2(office-def word) of the deputy's definition (seat B);
//                    drives the kind-3 random bonus
//   lawRate          law record 14, float field (VIBE_Gesetz_GetRecord(14))
//   officeObjectId   dword_12CE914[134*a1]  (target for the wage command)
// ===========================================================================
struct OfficeWageInput {
    int   officeId   = 0;   // seat A title (byte_12CEA76)
    int   deputyId   = 0;   // seat B title (byte_12CEA79)
    i32   personId   = -1;  // word_12CE910 (-1 == vacant office -> no wage)
    int   stateByte  = 0;   // byte_12CE912 (>=10 -> skip; ==3 -> bonus)
    bool  enabled    = true;// byte_12CEA918 != 0
    int   rankA      = 0;   // HIBYTE(office-def) of officeId (seat A)
    int   rankB      = 0;   // HIBYTE(office-def) of deputyId (seat B)
    int   bonusByte  = 0;   // BYTE2(office-def) of deputyId (kind-3 bonus)
    float lawRate    = 0.f; // law(14) float field
    i32   officeObjectId = -1; // dword_12CE914 (command target)
    // Whether the original's `a2` (the "commit/dispatch" flag) is set — when 0
    // the function computes the seat wages and returns without queueing.
    bool  dispatch   = false;
};

// Per-office result. wageA / wageB mirror *(a3+4) / *(a3+8) the original writes.
struct OfficeWageResult {
    i32  wageA = 0;   // seat A wage  (*(a3+4); officeId rank)
    i32  wageB = 0;   // seat B wage  (*(a3+8); deputyId rank)
    bool valid = false; // matches the original's v21 (1 once a seat is computed)
    // Bonus (only when stateByte == 3 and dispatch && person resolved): the
    // original draws RandomModulo(bonusByte + 2), then queues
    //   QueueRequest16(officeObject, -1, 6400 * (roll + bonusByte), 0)
    //   RequestBuildOp90(officeObject, bonusByte)
    bool bonusEmitted = false;
    i32  bonusAmount  = 0; // 6400 * (roll + bonusByte)
    int  bonusOp      = 0; // bonusByte (RequestBuildOp90 arg)
};

// ---------------------------------------------------------------------------
// Command sink (lockstep). The original commits each wage and the bonus through
// VIBE_Command_QueueRequest16 / VIBE_Command_RequestBuildOp90 and bumps the
// pending-command counter dword_641DA4. We model the two emissions as a settable
// sink (default: no-op) so the arithmetic is testable; the engine installs the
// real command-queue forwarder. `requestBuild` is op-90 (RequestBuildOp90).
// ---------------------------------------------------------------------------
struct OfficeWageCommandSink {
    void (*queueRequest16)(i32 target, i32 a2, i32 amount, i32 a4) = nullptr;
    void (*requestBuild)(i32 target, i32 arg) = nullptr;
};
void SetOfficeWageCommandSink(const OfficeWageCommandSink& sink);

// ---------------------------------------------------------------------------
// RNG hook for the kind-3 bonus draw (VIBE_Math_RandomModulo). Defaults to the
// shared LCG modulo (guild::util::RandomModulo) so a seeded run is deterministic.
// ---------------------------------------------------------------------------
using OfficeWageRandFn = int (*)(u16 n);
void SetOfficeWageRandFn(OfficeWageRandFn fn);

// ===========================================================================
// gilde.exe 0x57b480 — VIBE_Amt_ComputeOfficeWages (__usercall: ax=officeIndex,
// edx=a2 dispatch-flag, ebx=out record). Reconstructs the full control flow:
//   1. write seat title ids into the out record, zero the two wages;
//   2. early-out (valid=false) when the office is vacant (personId==-1),
//      saturated (stateByte>=10), disabled (!enabled), or has no titles at all;
//   3. fetch lawRate from law record 14;
//   4. compute seat-B wage (deputyId rank) and seat-A wage (officeId rank);
//   5. if a2==0, return (valid=true) without dispatching;
//   6. otherwise (the original resolves the office's person + a type-277 object,
//      then) queue each non-zero wage; and when stateByte==3, draw the random
//      bonus and queue it + the build-op.
// The person/object resolution that gates dispatch is the caller's
// responsibility (input.dispatch already folds it); see the .cpp handoff note.
// ===========================================================================
OfficeWageResult ComputeOfficeWages(const OfficeWageInput& in);

} // namespace guild::world
