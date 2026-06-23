#pragma once
#include "guild/common/types.h"

// gamelogic_recon5_turns — the per-tick player-turn update loop plus two small
// state setters (namespace guild::sim).
//
//   0x452f38 VIBE_GameLogic_UpdatePlayerTurns — once per game tick this advances
//            a rolling "player window": it diffs the current game clock against
//            the last-committed clock to get elapsed minutes, derives a per-tick
//            player-process count, walks that many person slots (768-slot wrap),
//            refilling tavern stock / updating guard AI and emitting a balance
//            delta command per owned person, then advances the start cursor.
//
//   0x4b94d8 VIBE_Selection_ClearAll — clears the 768-slot selection alive-byte
//            array (byte_12CE880, 536-byte stride) and three selection globals.
//
//   0x5383f4 VIBE_DebugFlag_SetReload — sets the reload debug flag (dword_63CD44).
//
// The person table, command queue, guard AI, tavern restock and clock diff are
// live-state leaves -> routed through an installable hooks struct with inert
// defaults. The minute-diff scaling, the per-tick count derivation (including the
// 0.0625*count+0.5 fractional accumulator with the float-bit >= 1.0 carry), the
// >60-minute clamp to 768, the wrap-around slot walk, and the cursor advance ARE
// reconstructed 1:1, including the exact float op-order.

namespace guild::sim {

using f32 = float;

// 14-byte game-clock image (qword + dword + word), matching unk_13CE852 and the
// committed mirror at dword_B56454. We model it as the same layout the original
// copies (v9/v10/v11) and the comparison reads: a low dword that drives the
// diff, a packed middle, and WORD2(clock) (the month/period byte at +4) gating.
struct TurnClock {
    i32 lo    = 0;   // qword_13CE852 low dword (dword_B56454)  — diff anchor
    i32 mid0  = 0;   // qword_13CE852 high dword
    i32 mid1  = 0;   // unk_13CE85A
    i16 word6 = 0;   // unk_13CE85E
    // WORD2 of the full qword (bits 32..47): the period/month byte the gates read.
    u16 word2 = 0;
};

struct Recon5TurnHooks {
    // VIBE_GameTime_DiffMinutes(committedClock, currentClock) -> elapsed minutes.
    i32  (*diffMinutes)(const TurnClock& committed, const TurnClock& current) = nullptr;
    // dword_63C744 -> player count (cast to double, used for the count band).
    i32  (*totalPlayerCount)() = nullptr;
    // word_12CE910[268*slot] != -1 (slot occupied / valid person record).
    bool (*slotOccupied)(int slot) = nullptr;
    // VIBE_MeisterAi_RefillTavernStock(&record).
    void (*refillTavernStock)(int slot) = nullptr;
    // VIBE_Character_UpdateGuardBehavior(&record).
    void (*updateGuardBehavior)(int slot) = nullptr;
    // word_63C740 & 0x80 — guard-update suppression flag (true == suppressed).
    bool (*guardSuppressed)() = nullptr;
    // The owned-person gate: (dword_764CE0==-1 || record.kind==6 ||
    //   record.id % byte_63CC1D == dword_764CF4) && record.balance(+36) != 0.
    // We expose the composite predicate plus the balance value, since the record
    // fields are live leaves. Returns true if a delta command should be emitted.
    bool (*ownedGate)(int slot) = nullptr;
    // record balance field *(record+36).
    i32  (*recordBalance)(int slot) = nullptr;
    // Emit the balance-delta command: BeginDeltaPacket / AppendRawField(delta,36) /
    // QueueRequestState22. `delta` is the computed (int)(balance*0.89) - balance.
    void (*emitBalanceDelta)(int slot, i32 delta) = nullptr;
};

void SetRecon5TurnHooks(const Recon5TurnHooks* h);
const Recon5TurnHooks& GetRecon5TurnHooks();

// Persistent rolling-window state (the dword_B56450/B56454/B56458/B56464 globals).
// Exposed so tests can seed/observe; UpdatePlayerTurns mutates it in place.
struct TurnWindowState {
    int       startCursor = 0;   // dword_B56450 — next-player start slot
    TurnClock committed;          // dword_B56454.. — last committed clock
    i32       fracAccumBits = 0;  // dword_B56464[0] — float accumulator (bit image)
};
TurnWindowState& Recon5TurnWindow();

// gilde.exe 0x452f38 — VIBE_GameLogic_UpdatePlayerTurns
void GameLogic_UpdatePlayerTurns(const TurnClock& current);

// gilde.exe 0x4b94d8 — VIBE_Selection_ClearAll
// Clears the 768-slot selection alive array + globals. `aliveBytes` is the
// 536-stride byte array base (byte_12CE880); selGlobals receive 0.
struct SelectionGlobals {
    i32 g11BC270 = 0;  // dword_11BC270
    i32 g631740  = 0;  // dword_631740
    i32 g6317B0  = 0;  // dword_6317B0
};
// We clear the 768 alive bytes via callback (the array is live state) and zero
// the three globals. Returns the final loop accumulator (411648), matching the
// original's `return result`.
int Selection_ClearAll(void (*clearAliveByte)(int byteOffset), SelectionGlobals& g);

// gilde.exe 0x5383f4 — VIBE_DebugFlag_SetReload
void DebugFlag_SetReload(i32& reloadFlag); // reloadFlag = dword_63CD44

} // namespace guild::sim
