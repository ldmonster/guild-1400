#include "sim/gamelogic_recon5_turns.h"

#include <cstring>

namespace guild::sim {

// ===========================================================================
// Hooks plumbing (inert defaults).
// ===========================================================================
namespace {

i32  DefDiffMinutes(const TurnClock&, const TurnClock&) { return 0; }
i32  DefTotalPlayerCount() { return 0; }
bool DefSlotOccupied(int) { return false; }
void DefRefillTavernStock(int) {}
void DefUpdateGuardBehavior(int) {}
bool DefGuardSuppressed() { return false; }
bool DefOwnedGate(int) { return false; }
i32  DefRecordBalance(int) { return 0; }
void DefEmitBalanceDelta(int, i32) {}

const Recon5TurnHooks kDefaults = {
    &DefDiffMinutes, &DefTotalPlayerCount, &DefSlotOccupied, &DefRefillTavernStock,
    &DefUpdateGuardBehavior, &DefGuardSuppressed, &DefOwnedGate, &DefRecordBalance,
    &DefEmitBalanceDelta,
};

Recon5TurnHooks g_hooks = kDefaults;

} // namespace

void SetRecon5TurnHooks(const Recon5TurnHooks* h) {
    if (!h) { g_hooks = kDefaults; return; }
    g_hooks = *h;
    if (!g_hooks.diffMinutes)         g_hooks.diffMinutes         = kDefaults.diffMinutes;
    if (!g_hooks.totalPlayerCount)    g_hooks.totalPlayerCount    = kDefaults.totalPlayerCount;
    if (!g_hooks.slotOccupied)        g_hooks.slotOccupied        = kDefaults.slotOccupied;
    if (!g_hooks.refillTavernStock)   g_hooks.refillTavernStock   = kDefaults.refillTavernStock;
    if (!g_hooks.updateGuardBehavior) g_hooks.updateGuardBehavior = kDefaults.updateGuardBehavior;
    if (!g_hooks.guardSuppressed)     g_hooks.guardSuppressed     = kDefaults.guardSuppressed;
    if (!g_hooks.ownedGate)           g_hooks.ownedGate           = kDefaults.ownedGate;
    if (!g_hooks.recordBalance)       g_hooks.recordBalance       = kDefaults.recordBalance;
    if (!g_hooks.emitBalanceDelta)    g_hooks.emitBalanceDelta    = kDefaults.emitBalanceDelta;
}
const Recon5TurnHooks& GetRecon5TurnHooks() { return g_hooks; }

TurnWindowState& Recon5TurnWindow() {
    static TurnWindowState s; // dword_B56450 / B56454.. / B56464
    return s;
}

// ===========================================================================
// Exact image float constants.
//   flt_6191C0 = 0x414ccccd = 12.8f   (minutes -> per-tick scale)
//   flt_6191C4 = 0x3d800000 = 0.0625f (player-count -> fractional band)
//   flt_6191C8 = 0x3f000000 = 0.5f
//   flt_6191CC = 0xbf800000 = -1.0f
//   flt_6191D0 = 0x3f63d70a = 0.89f   (balance retention factor)
//   1.0f bits  = 0x3f800000 = 1065353216 (the fracAccum carry threshold)
// ===========================================================================
namespace {
constexpr f32 kMinScale     = 12.8f;   // flt_6191C0
constexpr f32 kCountBand    = 0.0625f; // flt_6191C4
constexpr f32 kCountBias    = 0.5f;    // flt_6191C8
constexpr f32 kFracCarry    = -1.0f;   // flt_6191CC
constexpr f32 kBalanceKeep  = 0.89f;   // flt_6191D0
constexpr i32 kOneFloatBits = 1065353216; // 0x3f800000

inline i32 floatBits(f32 f) { i32 b; std::memcpy(&b, &f, 4); return b; }
inline f32 bitsFloat(i32 b) { f32 f; std::memcpy(&f, &b, 4); return f; }
} // namespace

// ===========================================================================
// gilde.exe 0x452f38 — VIBE_GameLogic_UpdatePlayerTurns
// ===========================================================================
void GameLogic_UpdatePlayerTurns(const TurnClock& current) {
    TurnWindowState& W = Recon5TurnWindow();

    // v9/v10/v11 = snapshot of the incoming clock image (used for the diff and as
    // the value committed back into dword_B56454..).
    TurnClock snap = current;

    // v15 = DiffMinutes(committed, snapshot).
    i32 minutes = g_hooks.diffMinutes(W.committed, snap);
    // v0 = (double)minutes * 12.8f ; v14 = v0 ; v13 = (int)v0.
    double v0 = (double)minutes * (double)kMinScale;
    f32 v14 = (f32)v0;
    i32 v13 = (i32)v0;

    // v1 = (player count). flt_62EB90 = (double)v1 ; dword_62EB94 =
    //   v1*0.0625f + 0.5f. (Engine globals, not state we own; computed for parity
    //   but the only behavioral use of v1 is the gates below.)
    i32 v1 = g_hooks.totalPlayerCount();
    (void)((double)v1);                                  // flt_62EB90
    (void)((f32)((double)v1 * (double)kCountBand + (double)kCountBias)); // dword_62EB94

    // Gate: v1 > 0 && WORD2(clock) in [6, 0x16].
    if (!(v1 > 0 && snap.word2 <= 0x16u && snap.word2 >= 6u))
        return;

    if (snap.lo > W.committed.lo) {
        // Clock moved backwards relative to committed -> recommit, no processing.
        W.committed = snap;
        return;
    }

    // else branch.
    if (v1 > 60) {
        v13 = 768;                                       // process the whole table
        // LOWORD(qword_B56458) = qword_B56458 + 1 — a rolling counter we don't
        // model as observable state here (it lives in the committed packed word).
    } else {
        // v15 = v13 (unused after). Commit clock.
        W.committed = snap;
        // v14 = v14 - (double)v13 ; fracAccum += v14.
        v14 = (f32)((double)v14 - (double)v13);
        f32 frac = bitsFloat(W.fracAccumBits);
        frac = frac + v14;
        W.fracAccumBits = floatBits(frac);
        // if (dword_B56464[0] >= 1065353216) { ++v13; fracAccum += -1.0f; }
        // NOTE: the original compares the *float bit image* as a signed int to the
        // bit pattern of 1.0f. Preserved exactly.
        if (W.fracAccumBits >= kOneFloatBits) {
            ++v13;
            frac = bitsFloat(W.fracAccumBits) + kFracCarry;
            W.fracAccumBits = floatBits(frac);
        }
    }

    // v3 = v13 ; sprintf debug ; v4 = dword_B56450 (start cursor).
    int v3 = v13;
    int v4 = W.startCursor;

    if (v3) {
        do {
            if (g_hooks.slotOccupied(v4)) {              // word_12CE910[268*v4]!=-1
                g_hooks.refillTavernStock(v4);
                if (snap.word2 > 6u && snap.word2 < 0x16u && !g_hooks.guardSuppressed())
                    g_hooks.updateGuardBehavior(v4);
                if (g_hooks.ownedGate(v4)) {
                    i32 bal = g_hooks.recordBalance(v4);  // *(record+36)
                    // v6 = (double)bal * 0.89f ; v12 = (int)v6 ; v12 = v12 - bal.
                    double v6 = (double)bal * (double)kBalanceKeep;
                    i32 delta = (i32)v6;
                    delta = delta - bal;
                    g_hooks.emitBalanceDelta(v4, delta);
                }
            }
            --v13;
            v4 = (v4 + 1) % 768;
        } while (v13);
    }

    W.startCursor = v4;                                  // dword_B56450 = v4
    if (v4 == 768) {                                     // (unreachable: %768 caps
        W.startCursor = 0;                               //  at 767, but faithful)
        W.fracAccumBits = 0;
    }
}

// ===========================================================================
// gilde.exe 0x4b94d8 — VIBE_Selection_ClearAll
// do { result += 536; byte_12CE880[result] = 0; } while (result != 411648);
// The first write is at offset 536, the last at 411648 (== 536*768). i.e. it
// clears slots 1..768 of the alive array (offsets 536,1072,...,411648). The
// original's quirk: it never writes offset 0, and writes one past slot 767.
// Reproduced exactly.
// ===========================================================================
int Selection_ClearAll(void (*clearAliveByte)(int byteOffset), SelectionGlobals& g) {
    int result = 0;
    g.g11BC270 = 0;
    g.g631740  = 0;
    do {
        result += 536;
        if (clearAliveByte) clearAliveByte(result); // byte_12CE880[result] = 0
    } while (result != 411648);
    g.g6317B0 = 0;
    return result;
}

// ===========================================================================
// gilde.exe 0x5383f4 — VIBE_DebugFlag_SetReload
// ===========================================================================
void DebugFlag_SetReload(i32& reloadFlag) {
    reloadFlag = 1; // dword_63CD44 = 1
}

} // namespace guild::sim
