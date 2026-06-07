#pragma once
// ===========================================================================
// illness.h — character disease / illness contraction + progression
// ===========================================================================
// MODULE: sim character health (namespace guild::sim).
//
// This is the disease/illness sub-system that the daily "RunSimKrankheiten"
// (= "run-sim-illnesses") handler drives. Every game day the handler walks a
// rotating window of the Person/NPC array and, for each disease CANDIDATE,
// rolls against an environmental risk threshold; on a hit it picks a random
// NEW disease from the per-building disease-state bitfield and applies it,
// charging the building's stock for the medical cost.
//
// The disease STATE for a building/person record is packed into a 32-bit
// bitfield at record+44 (8 sub-fields, one per disease group). A group with a
// zero sub-field is "free" (eligible to contract a new disease of that group).
// PickRandomDiseaseEvent rolls a random free group, draws a severity from the
// group's event table row, packs the severity back into the +44 bitfield, and
// returns the medical cost to debit.
//
// Translated functions:
//   VIBE_Character_IsDiseaseCandidate     0x4d762c   (eligibility predicate)
//   VIBE_Building_PickRandomDiseaseEvent  0x58aaf4   (pick + pack + cost)
//
// Reused leaves (NOT redefined here):
//   guild::sim::IsObjectForTurn / IsOwnerForTurn  (sim/character_state.cpp)
//   guild::crt::RandNext                          (crt/rand.cpp)  == VIBE_Util_RandNext
//   guild::util::ConvertX                         (util/coord.cpp) truncate-toward-zero
//
// Cross-cluster effect tails (command delta packet + building stock debit) are
// routed through IIllnessHooks so the decision/packing arithmetic stays
// byte-faithful and unit-testable in isolation.
#include "guild/common/types.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// Disease event table  (gilde.exe unk_647728, 10 rows x 12 bytes).
// Row layout: { i32 id; f32 costScale; i32 countMod }.
//   id        : the disease group index (0..9) — written into v22[8] by the
//               original but not used downstream (kept for fidelity).
//   costScale : gold-cost-per-unit multiplier for this group.
//   countMod  : modulus for the severity draw (RandNext() % countMod).
// PickRandomDiseaseEvent indexes this table by (chosenBit + 2), i.e. groups
// 0..7 of the +44 bitfield map to rows 2..9.
// ---------------------------------------------------------------------------
struct DiseaseEvent {
    i32   id;         // +0x00  group id (denorm in the binary; = row index)
    float costScale;  // +0x04  gold cost scale
    i32   countMod;   // +0x08  severity modulus
};
extern const DiseaseEvent kDiseaseEvents[10];  // unk_647728

// ---------------------------------------------------------------------------
// Person/building record view for the disease pass. The disease pass addresses
// the 536-byte Person record (word_12CE910) by raw byte offset; we model just
// the touched fields.
//   +0x00 marker  (0xFFFF == free slot)
//   +0x02 kind    (< 10 == "real" person; disease only afflicts people)
//   +0x04 id      (turn-stride key)
//   +0x08 active  (live-actor / present byte; 0 == not present)
//   +0x2C disease-state bitfield (record+44 .. +47), 8 packed disease groups
// ---------------------------------------------------------------------------
GUILD_PACKED_BEGIN
struct IllnessRec {
    i16 marker;        // +0x00  (0xFFFF == free)
    u8  kind;          // +0x02  kind/class byte (< 10 == person)
    u8  pad3;          // +0x03
    i32 id;            // +0x04  record id
    u8  active;        // +0x08  present / live byte
    u8  pad9[35];      // +0x09..+0x2B
    u32 diseaseState;  // +0x2C  (+44) packed disease-group bitfield
    u8  pad48[488];    // +0x30..+0x217
} GUILD_PACKED;
GUILD_PACKED_END
static_assert(sizeof(IllnessRec) == 536, "IllnessRec must alias the 536-byte Person record");

// Turn-ownership classification for the disease candidate test. The original
// calls VIBE_Character_IsOwnerForTurn / VIBE_Character_IsObjectForTurn, which
// already live in character_state.cpp over a TurnObject. The disease candidate
// test only needs the boolean result for THIS record, so the caller supplies it.
struct IllnessTurnView {
    bool ownerForTurn;   // VIBE_Character_IsOwnerForTurn(record)
    bool objectForTurn;  // VIBE_Character_IsObjectForTurn(record)
};

// gilde.exe 0x4d762c — VIBE_Character_IsDiseaseCandidate.
//   if (marker == 0xFFFF || !active || kind >= 10) return 0;
//   if (IsOwnerForTurn(rec)) return 1;
//   return IsObjectForTurn(rec);
// A free slot, an absent actor, or a non-person record is never a candidate;
// otherwise the record must be owned/objected by this peer this turn.
bool IllnessIsDiseaseCandidate(const IllnessRec* rec, const IllnessTurnView& turn);

// ---------------------------------------------------------------------------
// Result of a disease-event pick.
// ---------------------------------------------------------------------------
struct DiseasePick {
    bool  applied;      // false == no free disease group / no severity drawn
    int   chosenBit;    // the +44 bitfield sub-field index chosen (0..7), or -1
    int   group;        // chosenBit + 2 (the kDiseaseEvents row), or -1
    int   severity;     // RandNext() % countMod (0 == no disease drawn)
    int   cost;         // (int)(severity * costScale), truncated toward zero
    u32   newState;     // the +44 bitfield after packing the new severity
    char  eventByte;    // *a2: the chosen group (== group) or 0 if severity==0
};

// gilde.exe 0x58aaf4 — VIBE_Building_PickRandomDiseaseEvent.
// Computes which of the 8 disease groups packed into `diseaseState` (record+44)
// are currently FREE (sub-field == 0), picks one at random, draws a severity
// from that group's event-table row, packs the severity back into the bitfield,
// and computes the medical cost (severity * costScale, truncated). Returns the
// decision; the caller (or a hook) emits the state-delta packet and debits the
// building stock by `cost`.
//
// `diseaseState` is the current record+44 dword. `outEventByte` (== a2 in the
// original) receives the chosen group byte (or 0). The boolean return models the
// original's 0/1 (false == no group free / no severity).
DiseasePick IllnessPickRandomDiseaseEvent(u32 diseaseState);

// ---------------------------------------------------------------------------
// Bitfield helpers (the packing arithmetic of PickRandomDiseaseEvent, exposed
// for the unit golden vectors). The +44 dword packs 8 disease groups; group i's
// "free" test reads a specific sub-mask, and applying severity writes a value
// into the same sub-field. The masks/shifts below are recovered 1:1.
// ---------------------------------------------------------------------------

// Returns true if disease group `bit` (0..7) is currently FREE in `state`.
// Mirrors the 8 eligibility tests at 0x58ab17..0x58ab96.
bool IllnessGroupIsFree(u32 state, int bit);

// Packs `value` into disease group `group` (== bit+2, range 2..9) of `state`,
// returning the new state. Mirrors the switch at 0x58ac5d..0x58adb7.
u32 IllnessPackGroup(u32 state, int group, int value);

}  // namespace guild::sim
