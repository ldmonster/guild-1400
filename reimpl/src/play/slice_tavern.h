#pragma once
// Wave 27 PLAY P5 — TAVERN / SOCIAL vertical slice (namespace guild::play).
//
// A faithful click -> dialog/HUD -> REAL command -> sim effect -> render slice for
// the TAVERN (Wirtshaus) social system, mirroring slice_personnel.cpp /
// slice_council.cpp / playable_slice.cpp. Where playable_slice issues a CONQUER
// order on an OBJECT record and slice_personnel a HIRE on a PERSON record, this
// module drives the TAVERN social/recruit command path on the live person arrays.
//
// REAL command path (decompiled this wave):
//   The player clicks the "Dunkle Ecke" (Dark Corner) of a tavern, browses the
//   shady characters present, and BUYs one (recruits a thug / hires a brawler).
//   VIBE_Location_TavernDarkCornerBuy (gilde.exe 0x5186b4), on a confirmed click
//   over a present character, stages a 5-dword command record and EMITS it:
//
//       v25[0] = 0x62757920;  // "buy " tag (little-endian 'b','u','y',' ')
//       v25[1] = dword_12CE914[134*word_63CC5C];   // PLAYER (current char) person id
//       v25[2] = *(_DWORD*)(v35 + 1);              // tavern/location id (building)
//       v25[3] = v27[v5];                          // the selected dark-corner NPC id
//       v25[4] = v26[v5];                          // the price (resource amount)
//       VIBE_Command_EnqueueBuildingActionStart("buy dunkle ecke");  // 0x49441c
//       VIBE_Command_RequestBuildOp83(v25);        // 0x495954  <-- OPCODE 83
//       VIBE_Command_QueueRequestPair36(npcId, playerId);            // 0x494b98 (op 36)
//       VIBE_Command_QueueRequest16(sellerId, playerId, price, 0);   // 0x494630 (op 16)
//       VIBE_Command_EnqueueBuildingActionEnd();                     // 0x4944xx
//
//   The eligibility gate is VIBE_Dialog_CheckResourceAmount(price, byte_6477A1):
//   the player must be able to afford the price.
//
// GROUNDING (byte-exact wire layout, decompiled this wave):
//   * VIBE_Command_RequestBuildOp83 @0x495954 — packet:
//        byte[0]   = 83 (opcode)
//        dword@+16 = a1[0] ("buy " tag)
//        dword@+20 = a1[1] (player person id)
//        dword@+24 = a1[2] (tavern/location id)
//        dword@+28 = a1[3] (target NPC id)
//        dword@+32 = a1[4] (price)
//     then VIBE_Command_EnqueuePacket (0x49388c) copies 0x99 bytes into the ring.
//   * VIBE_Command_QueueRequest16 @0x494630 — the price-TRANSFER command:
//        byte[0]=16, dword@+16=a1 (payer), dword@+20=a2 (payee), byte@+28=a4,
//        dword@+29=a3 (amount).
//   * VIBE_Command_QueueRequestPair36 @0x494b98 — the BINDING command:
//        byte[0]=36, dword@+16=a1 (npc id), dword@+20=a2 (player id).
//
// The opcode-83 ("buy dunkle ecke") APPLY path is the lockstep dispatch consuming
// the enqueued packet (VIBE_Command_Dispatcher @0x40a4d4 routes a DECODED
// interaction struct; the opcode->handler routing glue is not reconstructed in the
// translated slice). So the apply is supplied as an installable hook with a faithful
// INERT-by-default reconstruction in slice_tavern.cpp (the slice_personnel /
// CutsceneMiscHooks pattern): the default BINDS the recruited NPC to the player
// (relation slot 0 +0x5C and superior +0x60 == player id — exactly what the op-36
// Pair36 command records) and DEBITS the price from the player's cash word (+0x0A)
// — exactly what the op-16 QueueRequest16 transfer records.
//
// REAL reconstructed siblings WIRED (called directly, never redefined):
//   sim::PersonFindRecordById              (entity.h)
//   sim::PersonGetDword/SetDword/GetWord/SetWord/GetByte/SetByte  (person.h)
//   sim::PersonFindEmploymentRelation      (person_personnel2.h)
//   play::RunEconomyTurn / SeedEconomyTurnState           (turn_economy.h)
//   play::HashFullWorld / SnapshotFullWorld               (world_digest.h)
//   crt::Srand                                            (crt/rand.h)
// INERT hook (defined here, faithful default): the op-83 dark-corner-buy apply.
//
// Additive: no edits to playable_slice.cpp / slice_personnel.cpp / input_command.cpp.
#include <cstdint>
#include <functional>

#include "guild/common/types.h"

namespace guild::sim { struct Person; }

namespace guild::play {

// ===========================================================================
// The tavern interaction the slice replays. A click in the tavern window resolves
// to one of these against a target character present in the tavern.
// ===========================================================================
enum class TavernAction {
    kNone        = 0,   // nothing issued
    kRecruitThug = 1,   // "buy dunkle ecke": recruit a shady NPC (op 83)
};

// The TAG dword the op-83 "buy" command carries (v25[0] in DarkCornerBuy).
// 0x62757920 == 'b','u','y',' ' (little-endian) == "buy ".
constexpr u32 kTavernBuyTag = 0x62757920u;

// ===========================================================================
// TavernPacket — the 5-dword command record VIBE_Location_TavernDarkCornerBuy
// stages and hands to VIBE_Command_RequestBuildOp83 (byte-exact, see header doc).
// ===========================================================================
struct TavernPacket {
    u8   opcode    = 0;            // 83 on a built recruit command, else 0
    u32  tag       = kTavernBuyTag;// "buy " tag dword (a1[0])
    i32  playerId  = 0;            // a1[1]: current-player person id (the buyer)
    i32  locationId = 0;          // a1[2]: tavern/location (building) id
    i32  targetId  = 0;            // a1[3]: the recruited dark-corner NPC id
    i32  price     = 0;            // a1[4]: the resource amount paid
    bool built     = false;

    // The on-wire packet image RequestBuildOp83 lays out (byte[0]=83, then the five
    // dwords at +16). 36 bytes is enough to cover the staged payload for asserts.
    void encode(u8 out[36]) const;
};

// gilde.exe 0x5186b4/0x495954 — classify + BUILD the dark-corner-buy command packet.
// Returns a built packet (opcode 83) for a kRecruitThug interaction whose price the
// buyer can afford (CheckResourceAmount gate, modeled as price <= buyer cash); an
// unbuilt (opcode 0) packet otherwise.
struct TavernClick {
    TavernAction action     = TavernAction::kRecruitThug;
    i32          playerId    = 0;   // the clicking guild-master's person id (buyer)
    i32          targetId    = 0;   // the dark-corner NPC being recruited
    i32          locationId  = 0;   // the tavern/location (building) id
    i32          price       = 0;   // the offered price (resource amount)
};

TavernPacket BuildTavernPacket(const TavernClick& click);

// ===========================================================================
// Person-record offsets the apply hook reads/writes (byte offsets into the record).
// Same fields the real binding/transfer commands touch.
// ===========================================================================
enum TavernOffset : int {
    kTvRelationOff = 0x5C,  // relation slot 0 (employer/master id); -1 == unbound
    kTvSuperiorOff = 0x60,  // superior/office link id; -1 == unbound
    kTvCashOff     = 0x0A,  // cash-on-hand word
};

// ===========================================================================
// TavernApplyHooks — the op-83 dark-corner-buy apply, not reconstructed in the
// translated slice; installable hook with a faithful inert default in the .cpp.
// ===========================================================================
struct TavernApplyHooks {
    // Apply a decoded RECRUIT: bind the target NPC's relation slot 0 (+0x5C) and
    // superior (+0x60) to the buyer (the op-36 Pair36 binding) and debit `price`
    // from the buyer's cash word (+0x0A) (the op-16 transfer). Default does that.
    std::function<void(i32 playerId, i32 targetId, int price)> applyRecruit;
};
// Install the active apply hooks (nullptr restores the inert default).
void SetTavernApplyHooks(const TavernApplyHooks* hooks);

// Zero EVERY live world table HashFullWorld folds (same set as slice_personnel /
// playable_slice) so a run's hashes are a pure function of (loaded city + seed),
// reproducible across reruns in one process. Call BEFORE io::LoadWorld / seeding.
void TavernZeroWorldGlobals();

// ===========================================================================
// The resolved outcome of one tavern interaction.
// ===========================================================================
struct TavernOrder {
    bool        issued    = false;   // the interaction built + enqueued a command
    u8          opcode     = 0;       // 83 on a built recruit
    i32         playerId    = 0;
    i32         targetId    = 0;
    int         price       = 0;

    bool        applied      = false; // the apply hook ran
    // --- before/after readbacks of the folded person fields ---
    i32 targetRelationBefore = 0;     // target +0x5C before apply
    i32 targetRelationAfter  = 0;     // target +0x5C after apply (== playerId on ok)
    i32 targetSuperiorAfter  = 0;     // target +0x60 after apply (== playerId on ok)
    int buyerCashBefore      = 0;     // buyer cash word before apply
    int buyerCashAfter       = 0;     // ... after apply (== before - price)
    int employmentAfter      = 0;     // PersonFindEmploymentRelation(target): 0 == bound
};

// ===========================================================================
// IssueTavernClick — resolve a tavern interaction into a REAL command, gate it via
// the affordability rule, and apply it so the live person records mutate. Operates
// on the live g_persons array. Returns the outcome.
// ===========================================================================
TavernOrder IssueTavernClick(const TavernClick& click);

// ===========================================================================
// The full tavern slice over a SYNTHETIC live world.
// ===========================================================================
struct TavernSliceResult {
    bool seeded = false;
    TavernOrder order{};

    // --- the determinism oracle (three world hashes, fold order = loop order) ---
    std::uint64_t hashAfterSeed    = 0;  // HashFullWorld() right after seeding
    std::uint64_t hashAfterCommand = 0;  // ... after the tavern command applied
    std::uint64_t hashAfterDay     = 0;  // ... after one game-day

    int economyPasses = 0;

    bool commandChangedWorld() const { return hashAfterSeed != hashAfterCommand; }
    bool worldChanged() const { return hashAfterSeed != hashAfterDay; }
    bool ok() const {
        return seeded && order.issued && order.applied && commandChangedWorld();
    }
};

// Drive the whole tavern slice on a synthetic live world: seed `persons` people
// (a guild-master @ slot 0 buyer + dark-corner NPCs), issue `click`, run one
// economy day. Deterministic in (seed, econSeed, persons, click). Used by the unit
// test. Fills order before/after readbacks.
TavernSliceResult RunTavernSliceSynthetic(std::uint32_t seed, int persons,
                                          const TavernClick& click,
                                          std::uint32_t econSeed);

// ===========================================================================
// Step sequencing (mirrors slice_personnel's PersonnelStep), so the unit test can
// assert each step's pre/post HashFullWorld() behaves as the loop requires.
// ===========================================================================
enum class TavernStep {
    kSeed    = 0,   // world populated (synthetic persons)
    kCommand = 1,   // apply the tavern command (mutates a person record)
    kDay     = 2,   // run the economy day (mutates economy + RNG)
};
struct TavernStepHash {
    TavernStep    step;
    std::uint64_t hashAfter = 0;
    bool          mutated   = false;
};
// Measure the three steps' hashes on a synthetic world. Returns the step count (3).
int RunTavernStepsSynthetic(std::uint32_t seed, int persons,
                            const TavernClick& click, std::uint32_t econSeed,
                            TavernStepHash* out, int cap);

} // namespace guild::play
