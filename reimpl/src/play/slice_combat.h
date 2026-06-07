#pragma once
// Wave 27 P5 — COMBAT / INTRIGUE (attack, sabotage, theft, crime) VERTICAL SLICE
// (namespace guild::play).
//
// A faithful click -> classify -> REAL command packet -> apply (folded record/table
// mutation) -> run one game-day -> prove deterministic evolution slice for the
// HOSTILE-ACTION system, mirroring play::playable_slice / slice_council. Two real
// command paths drive it, depending on what the player clicked:
//
//  (A) ATTACK an enemy UNIT (combat):
//        VIBE_Command_BuildAttackPacket (gilde.exe 0x488a4c) classifies the
//        attacker's weapon object-def, validates target/heightmap, and EMITS an
//        order through the shared sink VIBE_Command_RequestBuildOp80 (OPCODE 80,
//        gilde.exe 0x495874) with order-KIND byte = 2 (kOrderPacketAttack,
//        LOBYTE(v15[1])). Both are already reconstructed 1:1 in src/sim
//        (combat_packets.cpp). The op-80 apply writes the order (kind 2, dest tile)
//        into the picked target's record pad (folded by HashFullWorld via g_objects).
//
//  (B) COMMIT a CRIME / INTRIGUE against a person (sabotage / theft / arson /
//        poison — the "Straftat" subsystem):
//        VIBE_Command_ExAddStraftat (gilde.exe 0x498f44) is the lockstep crime-apply
//        command: it grabs a free crime slot (VIBE_Straftat_FindFreeSlot 0x4c3390),
//        copies the 45-byte crime record into g_crimeTable (dword_11BC760), stamps a
//        fresh crime id, and (the broader original also adds evidence + city-grid +
//        history). The crime record carries { id, perpetrator (taeter, +22), victim
//        (opfer, +18), location (+28), target (+33), wanted, provenState (+37) }.
//        g_crimeTable IS a HashFullWorld-folded table (world_digest region [25]).
//
// This module is ADDITIVE GLUE wiring the already-reconstructed siblings into one
// classify -> build-packet -> apply -> run-a-day loop and proving the combat/crime
// state evolved deterministically. It defines NO new world state and edits no
// existing .cpp; the only inert-default hook is the op-80 ATTACK apply dispatch (so
// the build links standalone), whose default writes the order into the target's
// record pad. The CRIME-apply (ExAddStraftat) and the attack PACKET build are pure
// calls into reconstructed siblings.
//
// Folded combat/crime tables HashFullWorld covers (world_digest.cpp regions):
//   [25] g_crimeTable   [26] g_relationMatrix   (+ g_objects for the attack order pad)
#include <cstdint>
#include <string>

#include "guild/common/types.h"
#include "shim/IFileSystem.h"
#include "sim/combat_packets.h"   // CombatOrderHandle / CombatOrderContext / Build*

namespace guild::play {

// ===========================================================================
// CombatInteraction — one scripted hostile interaction, classified the way
// VIBE_Command_IssueOnObject / the context-action menu would classify a click on a
// rival person or unit.
// ===========================================================================
enum class HostileAction : int {
    kNone     = 0,
    kAttack   = 1,  // attack an enemy UNIT  -> opcode 80, order-kind 2
    kCrime    = 2,  // commit a crime/intrigue against a person -> ExAddStraftat
};

// The crime subtype the player picked from the intrigue menu (ExAddStraftat is
// generic over the crime-type byte; these are the gameplay-named variants).
enum class CrimeType : u8 {
    kSabotage = 1,
    kTheft    = 2,
    kArson    = 3,
    kPoison   = 4,
};

struct CombatInteraction {
    HostileAction action = HostileAction::kAttack;

    // --- attack-path inputs (BuildAttackPacket) ---
    i32 attackerId = 0;   // the clicking player's unit (a4 / v19, *(attacker+4))
    i32 targetId   = 0;   // the picked enemy unit (a2, *(target+4))
    i32 attackTileX = 0;  // a3 scratch tileX fed onward
    i32 attackParam = 0;  // a5, the attack parameter that lands in v15[6]

    // --- crime-path inputs (ExAddStraftat) ---
    i32 perpetratorId = 0;     // taeter (crime record +22)
    i32 victimId      = 0;     // opfer  (crime record +18)
    u8  crimeLocation = 0;     // location byte (crime record +28)
    CrimeType crimeType = CrimeType::kSabotage;  // crime-type byte
};

// ===========================================================================
// CombatPacket — the wire image of the ATTACK order. BuildAttackPacket stages a
// 44-byte block then RequestBuildOp80 wraps it in an opcode-80 packet. The fields
// the attack builder writes into the staging block:
//   stg[0..3]  = *(target+4)        (target id, the stamped id)
//   stg[4]     = 2                  (LOBYTE(v15[1]) == order-kind = attack)
//   stg[16..19]= *(attacker+4)      (attacker id, v15[4])
//   stg[7th dword secondary flag]   set when *(target+452) in {1,2,3}
// ===========================================================================
struct CombatPacket {
    u8  opcode    = 0;    // 80 on a built attack order, else 0
    u8  orderKind = 0;    // 2 (kOrderPacketAttack) on success
    i32 target    = -1;
    i32 attacker  = -1;
    i32 ringSlot  = -1;   // ring slot RequestBuildOp80 assigned (>=0 on enqueue)
    bool built    = false;
};

// gilde.exe 0x488a4c — VIBE_Command_BuildAttackPacket (the packet BUILD), driven
// through the reconstructed sim::BuildAttackPacket over a standalone CommandQueue.
// Builds + enqueues the opcode-80 attack order for `it`; returns the built packet.
// (Returns an unbuilt packet when `it` is not a kAttack, or BuildAttackPacket's
// early-outs fire.)
CombatPacket BuildCombatPacket(const CombatInteraction& it);

// ===========================================================================
// Op-80 ATTACK apply dispatch hook (the lockstep handler — same pad-write apply
// playable_slice / input_command use). The live engine routes the enqueued opcode-80
// packet to its handler; the dispatch glue is not separately reconstructed here, so
// it is an installable hook. The INERT DEFAULT writes (kind, destX, destZ) into the
// picked target object's record pad @ +0x60/+0x64/+0x68 (the observable "unit took
// the attack order" mutation HashFullWorld folds via g_objects).
struct CombatApplyHooks {
    // Resolve the target id to its g_objects slot index (models the live resolver).
    // Required for the default apply path. Returns -1 if not found.
    int (*resolveSlot)(i32 targetId, void* ctx) = nullptr;
    void* ctx = nullptr;
};
void SetCombatApplyHooks(const CombatApplyHooks* hooks);  // nullptr -> inert default

// Apply the built attack packet through the (hooked) op-80 handler. Returns 1 when
// the order was written into the target's record pad, 0 on any gate failure.
int ApplyCombatPacket(const CombatPacket& pkt, const CombatInteraction& it);

// ===========================================================================
// gilde.exe 0x498f44 — VIBE_Command_ExAddStraftat (the CRIME apply). Reconstructed
// here as a faithful port of the record-write core: grab a free crime slot
// (world::StraftatFindFreeSlot), copy the staged 45-byte crime record into
// g_crimeTable[slot], stamp the crime id, and bump the perpetrator/victim relation.
// The original's side cascade (evidence add, city-grid placement, history notify) is
// driven by handler-filter scans that have no headless analogue; we model the
// core folded-table mutation (g_crimeTable + g_relationMatrix) and SAY SO.
// Returns the crime slot index (>=0) on success, -1 if the table is full / invalid.
int ApplyCrimeCommand(const CombatInteraction& it, i32 crimeId);

// ===========================================================================
// Slice result.
// ===========================================================================
struct CombatSliceResult {
    bool loaded = false;
    std::uint32_t personCount = 0;
    std::uint32_t objectCount = 0;

    HostileAction action = HostileAction::kNone;

    // --- the classified click -> command (ATTACK path) ---
    bool attackBuilt    = false;   // BuildAttackPacket produced an opcode-80 packet
    int  attackOpcode   = 0;       // 80 on success
    u8   attackOrderKind = 0;      // 2 on success
    i32  attackRingSlot = -1;
    bool attackApplied  = false;   // op-80 apply wrote the order pad

    // --- the ATTACK order pad, before/after, on the target's record ---
    int  targetSlot     = -1;      // g_objects slot the order was written into
    i32  padKindBefore  = 0;       // *(rec+0x60) pre-command
    i32  padKindAfter   = 0;       // ... post-command (== orderKind on apply)

    // --- the classified click -> command (CRIME path) ---
    int  crimeSlot      = -1;      // g_crimeTable index the crime was written into
    i32  crimeId        = 0;       // the stamped crime id
    i32  crimePerp      = 0;       // perpetrator written (+22)
    i32  crimeVictim    = 0;       // victim written (+18)
    int  relationBefore = 0;       // g_relationMatrix[perp][victim] pre-command
    int  relationAfter  = 0;       // ... post-command (relation soured)

    // --- the game-day ---
    int  daySteps = 0;             // composed day steps replayed

    // --- determinism oracle (the combat/crime-aware whole-world hashes) ---
    std::uint64_t hashAfterLoad    = 0;
    std::uint64_t hashAfterCommand = 0;
    std::uint64_t hashAfterDay     = 0;

    // The command alone moved a folded combat/crime field -> the world hash moved.
    bool commandChangedWorld() const { return hashAfterLoad != hashAfterCommand; }
    // The whole slice (command + day) evolved the world.
    bool worldChanged() const { return hashAfterLoad != hashAfterDay; }
    // The hostile action genuinely mutated its folded state.
    bool combatChanged() const {
        if (action == HostileAction::kAttack)
            return attackApplied && padKindAfter != padKindBefore;
        if (action == HostileAction::kCrime)
            return crimeSlot >= 0 && relationAfter != relationBefore;
        return false;
    }
    bool ok() const { return loaded && combatChanged() &&
                             commandChangedWorld() && worldChanged(); }
};

// ===========================================================================
// RunCombatSlice — the whole hostile-action loop over a REAL city.
//   1. mount real assets + io::LoadWorld the city into the live arrays,
//   2. ZeroWorldGlobals (blank combat/crime tables) + seed a deterministic baseline,
//   3. HashFullWorld -> hashAfterLoad,
//   4. classify + BUILD the real command (attack opcode-80 OR crime ExAddStraftat)
//      -> APPLY -> the folded g_objects pad / g_crimeTable + g_relationMatrix mutate
//      -> HashFullWorld -> hashAfterCommand,
//   5. RunGameDay (the real per-day cascade, seeded) -> HashFullWorld -> hashAfterDay,
//   6. assert the combat/crime field changed + the world evolved deterministically.
//
// Headless + GUARDED on the city asset (caller supplies fs). Byte-identical on
// every call with the same arguments.
CombatSliceResult RunCombatSlice(shim::IFileSystem* fs, const std::string& gameDir,
                                 const std::string& cityName,
                                 const CombatInteraction& it,
                                 std::uint32_t seed);

// ===========================================================================
// SyntheticCombatStep — the abstract combat-slice step sequence, exposed so the unit
// test can drive the SAME sequencing over a SYNTHETIC live world (no assets):
//   kSeed (place the attacker/target or perp/victim) -> kCommand -> kDay.
// ===========================================================================
enum class CombatStep { kSeed = 0, kCommand = 1, kDay = 2 };

struct CombatStepHash {
    CombatStep    step;
    std::uint64_t hashAfter = 0;
    bool          mutated   = false;   // hashAfter != previous step's hashAfter
};

// Drive the combat-slice step sequence on a SYNTHETIC live world. Measures
// HashFullWorld before/after each step. `out` receives one CombatStepHash per step
// in loop order; returns the step count (3). Fills `outBefore`/`outAfter` with the
// real mutated field around the command (pad-kind for attack, relation for crime;
// may be null).
int RunCombatStepsSynthetic(std::uint32_t seed, const CombatInteraction& it,
                            CombatStepHash* out, int cap,
                            i32* outBefore, i32* outAfter);

} // namespace guild::play
