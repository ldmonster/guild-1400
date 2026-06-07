#pragma once
// Wave 27 P5 — THE COURT / COUNCIL / OFFICE (politics) VERTICAL SLICE
// (namespace guild::play).
//
// A faithful click -> dialog/HUD -> REAL command -> sim effect -> render slice for
// the office/council (politics) system, mirroring play::playable_slice for the
// command/economy substrate. Where playable_slice drives a unit ORDER (opcode 80)
// onto an object record, this drives the POLITICS command path:
//
//   click "Amtsbewerbung" (apply-for-office) on a vacant council seat
//     -> VIBE_Office_ApplyForCandidacy (gilde.exe 0x47e1b8) classifies the seat,
//        scans the holder table for an open slot of the requested office type, and
//        EMITS the candidacy command VIBE_Command_RequestBuildOp68 (OPCODE 68,
//        gilde.exe 0x495454) packing { applicant person id, holderA, holderB,
//        officeType }
//     -> the lockstep apply of opcode 68 is VIBE_Office_AssignToCandidate
//        (gilde.exe 0x47e4e0): it marks the applicant's +360 candidacy field and
//        BUMPS the matched holder slot's rank counter
//        (g_officeHolders[slot].rank, a HashFullWorld-folded politics field).
//
// All three of those are already reconstructed 1:1 in src/world (office_assign.cpp,
// office.cpp). This module is ADDITIVE GLUE that wires them into one
// classify -> build-packet -> apply -> run-a-day loop and proves the politics state
// evolved deterministically. It defines NO new world state and edits no existing
// .cpp; the only inert-default hook is the candidacy-packet APPLY dispatch (so the
// build links standalone), whose default runs the real OfficeAssignToCandidate.
//
// Folded politics tables HashFullWorld covers (world_digest.cpp regions):
//   [20] g_lawTable  [24] g_officeHolders  [25] g_crimeTable  [26] g_relationMatrix
// This slice mutates region [24] g_officeHolders (the holder-slot rank) directly via
// the real command path; RunGameDay then evolves the broader world on top.
#include <cstdint>
#include <string>

#include "guild/common/types.h"
#include "shim/IFileSystem.h"
#include "world/law_types.h"      // OfficeHolder layout
#include "world/office_assign.h"  // OfficePersonRec (the candidacy apply record)

namespace guild::play {

// ===========================================================================
// CouncilInteraction — one scripted court/council/office interaction, classified
// the way VIBE_Office_ApplyForCandidacy / the council cutscene would classify a
// player's click on a council seat.
// ===========================================================================
enum class CouncilAction : int {
    kNone          = 0,
    kApplyCandidacy = 1,  // "Amtsbewerbung": stand for a vacant office (opcode 68)
};

struct CouncilInteraction {
    CouncilAction action = CouncilAction::kApplyCandidacy;
    i32 applicantId = 0;  // the clicking character's person id (packed into op68)
    u8  holderKey   = 0;  // the targeted holder-slot character-id key (op68 holderA)
    u8  officeType  = 0;  // the office type the seat is for (op68 officeType)
};

// ===========================================================================
// CouncilPacket — the wire packet VIBE_Command_RequestBuildOp68 builds (0x495454).
// Byte layout, exactly as the original packs it:
//   [0]    opcode = 68
//   [1..4] applicant person id (the v17 = *(person+4) dword, little-endian)
//   [5]    holderA (v18)
//   [6]    holderB (v19; 0xFF == none)
//   [7]    officeType (v20)
// ===========================================================================
struct CouncilPacket {
    u8  opcode    = 0;   // 68 on a valid candidacy command, else 0
    i32 applicant = -1;  // packed applicant id
    u8  holderA   = 0xFF;
    u8  holderB   = 0xFF;
    u8  officeType = 0;
    bool built    = false;

    // The 8-byte on-wire image (as RequestBuildOp68 lays it out), for byte asserts.
    void encode(u8 out[8]) const;
};

// gilde.exe 0x495454 — VIBE_Command_RequestBuildOp68 (the packet BUILD).
// Classify + build the candidacy command packet for `it`. Returns a built packet
// (opcode 68) when `it` is a kApplyCandidacy with a valid office type; an unbuilt
// (opcode 0) packet otherwise.
CouncilPacket BuildCouncilPacket(const CouncilInteraction& it);

// ===========================================================================
// Candidacy-apply dispatch hook (the lockstep op-68 handler).
// ===========================================================================
// The live engine routes the enqueued opcode-68 packet to its handler which calls
// VIBE_Office_AssignToCandidate (0x47e4e0). The handler dispatch glue itself is not
// reconstructed, so it is an installable hook; the INERT DEFAULT runs the REAL
// world::OfficeAssignToCandidate over a caller-supplied person store, mutating
// g_officeHolders[slot].rank + the applicant's +360 field. `pkt` is the built op-68
// packet; `store` resolves the applicant person id. Returns the AssignToCandidate
// result (1 == applied).
struct CouncilApplyHooks {
    // Resolve a person id to its office-mutation record (models
    // VIBE_Person_FindRecordById; the office-rank/candidacy fields the original
    // reads/writes). Required for the default apply path.
    world::OfficePersonRec* (*find)(i32 personId, void* ctx) = nullptr;
    void* ctx = nullptr;
};
void SetCouncilApplyHooks(const CouncilApplyHooks* hooks);  // nullptr -> inert default

// Apply the built candidacy packet through the (hooked) op-68 handler. Returns the
// real OfficeAssignToCandidate result (1 == the candidacy was seated, slot rank
// bumped); 0 on any gate failure or unbuilt packet.
int ApplyCouncilPacket(const CouncilPacket& pkt);

// ===========================================================================
// Slice result.
// ===========================================================================
struct CouncilSliceResult {
    bool loaded = false;
    std::uint32_t personCount = 0;
    std::uint32_t objectCount = 0;

    // --- the classified click -> command ---
    bool commandBuilt   = false;   // BuildCouncilPacket produced an opcode-68 packet
    int  commandOpcode  = 0;       // 68 on success
    i32  commandApplicant = -1;
    u8   commandOfficeType = 0;
    bool commandApplied = false;   // OfficeAssignToCandidate returned 1

    // --- the real folded politics field, before/after the command ---
    int  slotIndex      = -1;      // g_officeHolders index the command mutated
    i32  rankBefore     = 0;       // g_officeHolders[slot].rank pre-command
    i32  rankAfter      = 0;       // ... post-command (== rankBefore + 1 on apply)
    u8   candidacyBefore = 0;      // applicant +360 pre-command
    u8   candidacyAfter  = 0;      // ... post-command (== officeType on apply)

    // --- the game-day ---
    int  daySteps = 0;             // composed day steps replayed

    // --- determinism oracle (the politics-aware whole-world hashes) ---
    std::uint64_t hashAfterLoad    = 0;
    std::uint64_t hashAfterCommand = 0;
    std::uint64_t hashAfterDay     = 0;

    // The command alone moved a folded politics field -> the world hash moved.
    bool commandChangedWorld() const { return hashAfterLoad != hashAfterCommand; }
    // The whole slice (command + day) evolved the world.
    bool worldChanged() const { return hashAfterLoad != hashAfterDay; }
    // The politics field genuinely changed (rank bumped or candidacy seated).
    bool politicsChanged() const {
        return commandApplied && (rankAfter != rankBefore ||
                                  candidacyAfter != candidacyBefore);
    }
    bool ok() const { return loaded && commandApplied && politicsChanged() &&
                             commandChangedWorld() && worldChanged(); }
};

// ===========================================================================
// RunCouncilSlice — the whole politics loop over a REAL city.
//   1. mount real assets + io::LoadWorld the city into the live arrays,
//   2. ZeroWorldGlobals (blank politics tables) + seed a deterministic baseline so
//      the whole-world hash is a pure function of (city + seed),
//   3. SEED one vacant council seat into g_officeHolders matching `it`,
//   4. HashFullWorld -> hashAfterLoad,
//   5. BuildCouncilPacket(it) (opcode 68) -> ApplyCouncilPacket (the real
//      OfficeAssignToCandidate) -> the folded g_officeHolders rank bumps ->
//      HashFullWorld -> hashAfterCommand,
//   6. RunGameDay (the real per-day cascade, seeded) -> HashFullWorld -> hashAfterDay,
//   7. assert the politics field changed + the world evolved deterministically.
//
// Headless + GUARDED: gated on the city asset being present (caller supplies fs).
// Reproduces a byte-identical result on every call with the same arguments.
CouncilSliceResult RunCouncilSlice(shim::IFileSystem* fs, const std::string& gameDir,
                                   const std::string& cityName,
                                   const CouncilInteraction& it,
                                   std::uint32_t seed);

// ===========================================================================
// SyntheticCouncilStep — the abstract politics-slice step sequence, exposed so the
// unit test can drive the SAME sequencing over a SYNTHETIC live world (no assets):
//   kSeed (seat a vacant office) -> kCommand (apply candidacy) -> kDay (run a day).
// ===========================================================================
enum class CouncilStep { kSeed = 0, kCommand = 1, kDay = 2 };

struct CouncilStepHash {
    CouncilStep   step;
    std::uint64_t hashAfter = 0;
    bool          mutated   = false;   // hashAfter != previous step's hashAfter
};

// Drive the politics-slice step sequence on a SYNTHETIC live world (a vacant seat
// seeded into g_officeHolders matching `it`). Measures HashFullWorld before/after
// each step. `out` receives one CouncilStepHash per step in loop order; returns the
// step count (3). Also fills `outRankBefore`/`outRankAfter` with the real
// g_officeHolders rank field around the command (may be null).
int RunCouncilStepsSynthetic(std::uint32_t seed, const CouncilInteraction& it,
                             CouncilStepHash* out, int cap,
                             i32* outRankBefore, i32* outRankAfter);

} // namespace guild::play
