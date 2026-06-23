#pragma once
// =============================================================================
// guild::play — the NEW-GAME COMMIT: write the player's chosen identity into the
// live world entity arrays the way the original does after the world is loaded.
//
//   gilde.exe 0x5336f0 — VIBE_Command_EnqueueInheritanceTransfer
//     The function VIBE_GameLogic_InitOrLoadSession @0x533a54 calls right after
//     the new game's "<city>.cty" world load (call site 0x533e03; the
//     inherit-dynasty variant at 0x533b1b). Gated on BYTE2(dword_122F4A0) (the
//     0x12 byte the RunChoosePlayer wappen commit writes at 0x52d4af), it:
//       1. enqueues the opcode-12 "create player" command
//          (VIBE_Command_EnqueueTradeRequest @0x494548) carrying the whole
//          identity block: professionVariant = SHIBYTE(dword_122F4A0),
//          16 (owner word), wappen id dword_122F4A4, gender byte_122F4A8,
//          faith byte_122F4A9, first name String @0x122F4AA, family name
//          byte_122F4CA — applied by VIBE_Command_HandleCreatePersonB @0x496714
//          which allocates the kind-6 player Person (sim::g_persons);
//       2. creates the two parents via two opcode-11 commands
//          (VIBE_Command_EnqueueObjectInteraction @0x4944f0: kind 9, profession
//          word = RandomModulo(4)+32, gender 1 then 0) — applied by
//          VIBE_Command_HandleCreatePersonA @0x496614;
//       3. stamps the family fields onto the three records (offsets in the
//          536-byte Person record): father +0x40 = family name; both parents
//          +0x50 (family word) / +0x54 (wappen) copied from the player,
//          +0x68 = player id, +0x30 = a random first name from the female
//          (dword_8C4320, 112 entries) / male (dword_8C400C, 191 entries)
//          tables, +0x5C = the spouse's id; player +0x60 = father id,
//          +0x64 = mother id, +0x1F0 = the avatar model name unk_122F4F5,
//          +0x18C = dword_122F528 (1555 on the ChooseProfession path,
//          0x52da1a), +0x80..+0x84 = the five talent bytes byte_122F4F0;
//       4. queues six reciprocal relationship commands
//          (VIBE_Command_QueueRequestCoord27 @0x494878, delta 127) between
//          player/father/mother, and one opcode-15 purse grant per parent
//          (VIBE_Command_EnqueueCmd15 @0x494604,
//          amount = 32*RandomModulo(0x200) + 16000, rate byte_6477A1);
//       5. registers the mission slot (VIBE_Mission_SlotRegister @0x53872c,
//          owner = player id, type = LOBYTE(dword_122F4EC)) when the mission
//          byte 0x63C8F4 is >= 0 (static image: 0xFE, i.e. skipped).
//
//   gilde.exe 0x533b9d..0x533f8f — the caller's adjacent new-single-player
//     block: the world clock seed (GameTime_Set 06:00 -> QueueRequestFlagBlob32
//     flag 3) and the per-player START GOLD pass — for every alive Person
//     (byte +8 != 0) of kind 6/7: EnqueueCmd15(person, -1,
//     MoneyMultiplyByRate(base, byte_6477A1), byte_6477A1) with
//     base = cheat(dword_63C7B4) ? 75000 : 1250 - 250*difficulty(dword_63C744).
//     The difficulty is the ChooseCharacterIntroVariant pick (byte_12335BA).
//
// This module reconstructs that commit against the reimplementation's REAL
// machinery: the real packet builders (sim::EnqueueTradeRequest /
// EnqueueObjectInteraction / QueueRequestCoord27 / EnqueueCmd15 /
// QueueRequestFlagBlob32), the real lockstep queue (sim::CommandQueue,
// standalone flush == the original's MarkSyncRangeStart/End +
// CheckSyncRangeAcked wait), the real apply handlers (sim::ExCreatePersonA/B)
// and the real record arrays (sim::g_persons). The talents derivation
// reproduces the RunChooseHistory tail (0x52d9ef..0x52da0e): the first five
// bytes of VIBE_Building_LookupTypeRecordA(professionVariant).
//
// NAMED GAPS (routed through NewGameApplyHooks with inert defaults — rule 8):
//   * VIBE_Office_ResolveStaffModel @0x57c1e8 (the parents' staff-model /
//     avatar resolve) — render/avatar subsystem; default no-op.
//   * the first-name string tables dword_8C4320 (female, 112) / dword_8C400C
//     (male, 191), loaded from text resources by 0x530e50 — not reconstructed;
//     default returns "" (the parents keep the CreateAndSpawn-leaf name). The
//     RandomModulo draws themselves ARE made, preserving the RNG stream.
//   * VIBE_Person_CreateAndSpawn @0x58da70 internals (stats/avatar/family RNG) —
//     the existing person_create.h hook (default backend allocates the slot,
//     stamps marker/kind/id/owner/alive(100)/gender).
//   * VIBE_Object_ResetState @0x538400 at function entry clears the CALLER'S
//     4608-byte scratch (Light_SetGrayColorThunk(0,4608)) — no record effect;
//     not performed.
//
// CLOSED FORMER GAP: the opcode-27 (relationship) apply handler IS reconstructed
// — it is sim::ExComputeObjectCoords (gilde.exe 0x49818C, jump-table slot 27 ==
// opcode 0x1B, command_apply6 batch 6), resolving its persons in the LIVE
// sim::g_persons array. Register batch 6 on the queue (RegisterApplyHandlers6,
// done by app::wiring) and the six mode-0/delta-127 packets genuinely saturate
// the primary 768x768 relation grid for all six directed player/father/mother
// pairs. See progress/newgame-apply.md (closure 2026-06-11) and
// progress/command-apply-relation.md.
// =============================================================================
#include "guild/common/types.h"
#include "gui/newgame_setup.h"   // gui::NewGameParams (the collected block)

#include <string>
#include <vector>

namespace guild::sim { class CommandQueue; }

namespace guild::play {

// ---------------------------------------------------------------------------
// Host hooks for the unreconstructed leaves (inert defaults; tests install
// spies / fake name tables).
// ---------------------------------------------------------------------------
struct NewGameApplyHooks {
    virtual ~NewGameApplyHooks() = default;

    // VIBE_Office_ResolveStaffModel @0x57c1e8 — called once per parent with the
    // record's marker word (ax = *(WORD*)record, 0x533869/0x5338be). Resolves
    // the staff-model/avatar row for the new NPC. Inert default: no-op.
    virtual void ResolveStaffModel(u16 markerWord) { (void)markerWord; }

    // The parent first-name tables: dword_8C4320 (female, RandomModulo(0x70))
    // and dword_8C400C (male, RandomModulo(0xBF)) — fixed-index slices of the
    // global localized-text array dword_8C36B0, loaded as ordinary text resources
    // (Text_C_Personen.res) by VIBE_Text_LoadTextFile @0x44dba0; see
    // sim/name_tables.h. `index` is the exact RandomModulo draw the original makes.
    // When the name tables have been loaded (sim::NameTables_Load*), this returns
    // the real localized name; otherwise it falls back to "" (no spawn-name
    // overwrite), preserving the inert behavior when no asset is present.
    virtual const char* ParentFirstName(bool female, int index);
};
// Install hooks (null restores the inert defaults). Returns the previous set.
NewGameApplyHooks* NewGameApply_SetHooks(NewGameApplyHooks* hooks);

// ---------------------------------------------------------------------------
// gilde.exe 0x52d9ef..0x52da0e (VIBE_Menu_RunChooseHistory profession tail) —
// the five talent bytes byte_122F4F0[0..4] a committed profession produces:
//   LookupTypeRecordA(SHIBYTE(dword_122F4A0), &rec);  out[k] = rec bytes[0..4]
// (for eax = 1..5: byte_122F4F0[eax-1] = [esp+eax-1], the record's first five
// bytes). Variants outside 0..75 fall back to record 0 (all zero), exactly as
// VIBE_Building_LookupTypeRecordA does.
void NewGameProfessionTalents(int professionVariant, u8 out[5]);

// ---------------------------------------------------------------------------
// The scattered engine globals the commit reads beside the parameter block.
// Defaults are the cold-image statics; the session host passes live values.
// ---------------------------------------------------------------------------
struct NewGameApplyInputs {
    u8  rateByte = 0;            // byte_6477A1 — city money-rate byte (cmd15 arg)
    u8  parentProfCtxA = 0;      // signed byte @0x122F4A1 (1st opcode-11 a6);
                                 //   0x00 on the standard path (the wappen commit
                                 //   writes word 0x1200 at 0x122F4A1)
    u8  parentProfCtxB = 0;      // signed byte @0x122F4A0 (2nd opcode-11 a6);
                                 //   LOBYTE(dword_122F4A0) = 0 (0x52d4a9)
    std::string avatarModelName; // unk_122F4F5 -> player +0x1F0. Set only by the
                                 //   dynasty preview scene (0x52bb1a); "" on the
                                 //   automatic-ancestors path (static image 0)
    i32 portraitId = gui::kStartCommandValue; // dword_122F528 -> player +0x18C
                                 //   (1555 on the ChooseProfession path 0x52da1a;
                                 //   modelTable+1468 on the dynasty path 0x52bb46)
    u8  missionId = 0;           // LOBYTE(dword_122F4EC) (RunChooseHistoryDialog
                                 //   result; 0 static) — Mission_SlotRegister type
    u8  missionModeByte = 0xFE;  // byte_63C8F4 — the mission-mode byte the gate
                                 //   reads as SIGNED (0x533a15: > -1 -> register).
                                 //   Cold image: 0xFE (-2, skipped). The same
                                 //   byte backs world::g_missionSlotMode (the
                                 //   SlotRegister single-slot mode) — keep them
                                 //   in sync when the mission system is live.
    const u8* talentsOverride = nullptr; // byte_122F4F0[0..4]: when null they are
                                 //   derived from p.professionVariant (the
                                 //   profession path); the dynasty Talent screen
                                 //   (0x52b463) would supply its own five bytes
    bool cheatStartGold = false; // dword_63C7B4 — start purse forced to 75000
    bool seedStartGold = true;   // run the 0x533b9d clock-seed + 0x533f40 gold
                                 //   pass (the caller's new-single-player block)
};

// ---------------------------------------------------------------------------
// Observable outcome (ids resolve into sim::g_persons via PersonFindRecordById).
// ---------------------------------------------------------------------------
struct NewGameApplyResult {
    bool applied = false;        // the BYTE2(dword_122F4A0) gate passed (p.started)
    bool createFailed = false;   // a person create rejected (array full / no load)
    i32  playerId = -1;          // the kind-6 player person (opcode 12)
    i32  motherId = -1;          // 1st opcode-11 parent (gender flag 1, female)
    i32  fatherId = -1;          // 2nd opcode-11 parent (gender flag 0, male)
    u8   talents[5] = {0, 0, 0, 0, 0};  // the byte_122F4F0 image written +0x80..
    i32  pursefather = 0;        // the father's cmd15 amount (32*rand+16000)
    i32  purseMother = 0;        // the mother's cmd15 amount
    i32  startGoldBase = 0;      // 75000 / 1250-250*difficulty (0 if pass skipped)
    std::vector<i32> goldSeededIds; // person ids the start-gold pass funded
    int  missionSlot = -1;       // Mission_SlotRegister result (-1 = not taken)
};

// ---------------------------------------------------------------------------
// gilde.exe 0x5336f0 (+ the 0x533b9d/0x533f40 caller block) — the new-game
// commit. `p` is the collected parameter block the menu chain produced
// (NativeMenuResult.params); `difficultyVariant` is the ChooseCharacterIntro-
// Variant pick (byte_12335BA / dword_63C744, p.difficulty in the chain).
// `q` is the session's lockstep command queue: the function registers the
// person-create apply handlers (RegisterApplyHandlers5) on it, enqueues the
// REAL packets and drives the standalone flush/exec (the original's sync-range
// barrier). Call it AFTER the world (.cty) is loaded, exactly where the
// original calls it (0x533e03).
// ---------------------------------------------------------------------------
NewGameApplyResult ApplyNewGameParams(const gui::NewGameParams& p,
                                      int difficultyVariant,
                                      sim::CommandQueue& q,
                                      const NewGameApplyInputs& in = {});

} // namespace guild::play
