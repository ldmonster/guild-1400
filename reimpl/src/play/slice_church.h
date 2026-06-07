#pragma once
// Wave 27 P5 — THE CHURCH / RELIGION (donation) VERTICAL SLICE
// (namespace guild::play).
//
// A faithful click -> dialog/HUD -> REAL command -> sim effect -> render slice
// for the church/donation system, mirroring play::slice_market /
// play::slice_council for the command/economy substrate. Where slice_market
// drives a player TRADE (opcode 17) and slice_council a POLITICS candidacy
// (opcode 68), this drives the CHURCH DONATION command path:
//
//   click "Spenden" (donate) inside the church donation dialog
//     -> VIBE_Location_ChurchDonationDialog (gilde.exe 0x521674) classifies the
//        church object (a QueryFind(+93, kind 2/6) currency target), computes the
//        wealth-scaled donation amount, and on the "donate" button EMITS the
//        donation command VIBE_Command_EnqueueCmd15 (OPCODE 15 / 0x0F,
//        gilde.exe 0x494604) packing { recipientChurchId, donorAccountId,
//        amount, currencyType } ; it also fans out reputation deltas via
//        VIBE_Command_QueueRequestCoord27 (opcode 27) to the other players.
//     -> the lockstep apply of opcode 15 is VIBE_Command_ExRemapObjectPair
//        (gilde.exe 0x496978, the jump-table[15] target): it REMOVES `amount` of
//        currency-proto from the donor account (src, +0x14) and ADDS it to the
//        church (dst, +0x10) via the two object-stock leaves
//        VIBE_GameObject_RemoveObjektAmount (0x5863b4) /
//        VIBE_GameObject_AddObjektToParent (0x5862a4).
//
// The packet BUILDER (EnqueueCmd15 0x494604), the packet TRANSPORT/dispatch
// (the REAL sim::CommandQueue codec: EnqueuePacket/FlushSendQueue/ExecCommands),
// and the opcode-15 APPLY handler (sim::ExRemapObjectPair, command_apply2.cpp)
// are all already reconstructed 1:1. This module is ADDITIVE GLUE that wires
// them into one classify -> build-packet -> enqueue -> apply -> run-a-day loop and
// proves the donation evolved the (folded) world deterministically. It defines NO
// new world state and edits no existing .cpp.
//
// The donor/church "wealth" the donation moves is stored in the object record's
// money pad dword (object+77 — the SAME folded field slice_market credits trade
// proceeds into), so HashFullWorld observes the transfer. The two object-stock
// leaves are routed through sim::SetRemoveObjektHook / sim::SetAddObjektHook
// (the engine's OWN deferred hooks) to that folded field; the INERT DEFAULT
// (defined in slice_church.cpp) performs exactly the +amount / -amount the real
// RemoveObjektAmount/AddObjektToParent net to on a currency stack.
#include <cstdint>
#include <string>

#include "guild/common/types.h"
#include "shim/IFileSystem.h"
#include "sim/command.h"   // CommandPacket / CommandQueue / AckEntry

namespace guild::play {

// ===========================================================================
// ChurchInteraction — one scripted church donation interaction, classified the
// way VIBE_Location_ChurchDonationDialog would classify the player's click on
// the "Spenden" button.
// ===========================================================================
enum class ChurchAction : int {
    kNone   = 0,
    kDonate = 1,   // "Spenden": donate currency to the church (opcode 15)
};

struct ChurchInteraction {
    ChurchAction action = ChurchAction::kDonate;
    i32 churchId      = 0;   // recipient church object id   (EnqueueCmd15 a1 -> +0x10)
    i32 donorAccount  = 0;   // donor currency-account id     (EnqueueCmd15 a2 -> +0x14)
    i32 amount        = 0;   // donation amount               (EnqueueCmd15 a3 -> +0x1D)
    u8  currencyType  = 0;   // currency/player type byte      (EnqueueCmd15 a4 -> +0x1C)
};

// ===========================================================================
// The opcode-15 donation command + its byte-exact wire layout (the staged
// 153-byte packet VIBE_Command_EnqueueCmd15 @0x494604 lays out, then enqueues
// via VIBE_Command_EnqueuePacket). Recovered stack-temp offsets:
//   [0x00]      opcode = 15            (v5[0] = 0Fh)
//   [0x10]      var_8C  = a1 (EAX)     recipient church id   (dword)
//   [0x14]      var_88  = a2 (EDX)     donor account id      (dword)
//   [0x1C]      var_80  = a4 (BL)      currency type         (byte)
//   [0x1D]      var_7F  = a3 (ECX)     donation amount       (dword)
// The apply (ExRemapObjectPair) reads +0x14 as src (Remove), +0x10 as dst (Add),
// +0x1C as the proto/currency-type byte, +0x1D as the moved amount.
// ===========================================================================
inline constexpr u8  kDonationOpcode  = 15;     // 0x0F — EnqueueCmd15 / jump-table[15]
inline constexpr u32 kDonationDstOff  = 0x10;   // recipient church (a1)
inline constexpr u32 kDonationSrcOff  = 0x14;   // donor account   (a2)
inline constexpr u32 kDonationTypeOff = 0x1C;   // currency type    (a4, byte)
inline constexpr u32 kDonationAmtOff  = 0x1D;   // amount           (a3, dword)

// Object-record money field the donation moves (folded by HashFullWorld). Same
// dword slice_market credits trade proceeds into (ExComputeSellableAmount +77).
inline constexpr u32 kChurchMoneyOff  = 77;

struct ChurchCommand {
    bool issued    = false;   // a valid donation (kDonate + amount > 0)
    u8   opcode    = 0;       // kDonationOpcode (15) when issued
    i32  churchId  = 0;       // dst (a1)
    i32  donorId   = 0;       // src (a2)
    i32  amount    = 0;       // a3
    u8   currency  = 0;       // a4

    // The staged on-wire image, exactly as EnqueueCmd15 lays it out.
    sim::CommandPacket Encode() const;
};

// 1:1 classifier: ChurchInteraction -> ChurchCommand. A non-donation (no action /
// amount <= 0) yields {issued=false} (the dialog only enqueues on a positive
// donate). Mirrors the dialog's gate (CheckResourceAmount on the chosen amount).
ChurchCommand ClassifyChurchInteraction(const ChurchInteraction& it);

// ===========================================================================
// Wiring helpers.
// ===========================================================================
// Install the REAL opcode-15 apply handler (sim::ExRemapObjectPair) on `q`, and
// point the engine's object-stock leaves at the folded money-field mutator
// (the inert default; tests may install richer backends via the sim hooks).
// Idempotent per queue.
void InstallChurchCommandHandler(sim::CommandQueue& q);

// Read / seat the folded money field (object+77) of a live object (0 if absent).
i64  ReadObjectMoney(i32 objectId);
// Seat a live object (alive!=0, id) into g_objects with `money` in its money
// field; returns the slot index, or -1 if the array is full.
int  SeatChurchObject(i32 objectId, i32 money);

// ===========================================================================
// Slice result.
// ===========================================================================
struct ChurchSliceResult {
    bool loaded = false;
    std::uint32_t personCount = 0;
    std::uint32_t objectCount = 0;

    // --- the classified click -> command ---
    ChurchCommand command{};
    bool enqueued = false;     // a packet hit the send ring
    i32  ringSlot = -1;        // EnqueuePacket ring slot
    bool applied  = false;     // FlushSendQueue + ExecCommands ran the apply

    // --- the real folded money fields, before/after the donation ---
    i64 churchMoneyBefore = 0;
    i64 churchMoneyAfter  = 0;   // == before + amount on apply
    i64 donorMoneyBefore  = 0;
    i64 donorMoneyAfter   = 0;   // == before - amount on apply

    // --- the game-day ---
    int daySteps = 0;            // composed day steps replayed

    // --- determinism oracle (the three whole-world hashes, fold = loop order) ---
    std::uint64_t hashAfterLoad    = 0;
    std::uint64_t hashAfterCommand = 0;
    std::uint64_t hashAfterDay     = 0;

    bool commandChangedWorld() const { return hashAfterLoad != hashAfterCommand; }
    bool worldChanged()        const { return hashAfterLoad != hashAfterDay; }
    bool donationMoved() const {
        return applied && churchMoneyAfter != churchMoneyBefore &&
               donorMoneyAfter != donorMoneyBefore;
    }
    bool ok() const {
        return loaded && command.issued && applied && donationMoved() &&
               commandChangedWorld() && worldChanged();
    }
};

// ===========================================================================
// RunChurchSlice — the whole donation loop over a REAL city.
//   1. mount real assets + io::LoadWorld the city into the live arrays,
//   2. ZeroWorldGlobals (blank tables) + a deterministic baseline so the
//      whole-world hash is a pure function of (city + seed),
//   3. SEAT a donor object + a church object into g_objects matching `it`,
//   4. HashFullWorld -> hashAfterLoad,
//   5. ClassifyChurchInteraction -> Encode opcode-15 packet -> enqueue through
//      the REAL sim::CommandQueue -> flush + exec -> sim::ExRemapObjectPair moves
//      the currency donor->church (folded money fields) -> HashFullWorld ->
//      hashAfterCommand,
//   6. RunGameDay (the real per-day cascade, seeded) -> HashFullWorld -> hashAfterDay,
//   7. assert the donation moved + the world evolved deterministically.
//
// Headless + GUARDED: gated on the city asset being present (caller supplies fs).
// Reproduces a byte-identical result on every call with the same arguments.
ChurchSliceResult RunChurchSlice(shim::IFileSystem* fs, const std::string& gameDir,
                                 const std::string& cityName,
                                 const ChurchInteraction& it,
                                 std::uint32_t seed);

// ===========================================================================
// SyntheticChurchStep — the abstract donation-slice step sequence, exposed so the
// unit test can drive the SAME sequencing over a SYNTHETIC live world (no assets):
//   kSeed (seat donor+church) -> kCommand (apply donation) -> kDay (run a day).
// ===========================================================================
enum class ChurchStep { kSeed = 0, kCommand = 1, kDay = 2 };

struct ChurchStepHash {
    ChurchStep    step;
    std::uint64_t hashAfter = 0;
    bool          mutated   = false;   // hashAfter != previous step's hashAfter
};

// Drive the donation-slice step sequence on a SYNTHETIC live world (a donor +
// church seeded into g_objects matching `it`). Measures HashFullWorld before/after
// each step. `out` receives one ChurchStepHash per step in loop order; returns the
// step count (3). Fills `outChurchBefore`/`outChurchAfter` with the church money
// field around the command (may be null).
int RunChurchStepsSynthetic(std::uint32_t seed, const ChurchInteraction& it,
                            ChurchStepHash* out, int cap,
                            i64* outChurchBefore, i64* outChurchAfter);

} // namespace guild::play
