#pragma once
// Combat / Duel record layouts for the Guild simulation (gilde.exe).
//
// MODULE: combat — namespace guild::sim. Field offsets recovered from the
// combat-resolution accessors (VIBE_Combat_* / VIBE_Duel_*). The combat "unit"
// is a 536-byte (0x218) record copied from a Person record (see
// VIBE_Combat_SpawnUnit @0x489430 qmemcpy ...,0x218) and then re-interpreted by
// the combat code through _DWORD/_WORD/float views. Only the fields the combat
// resolution actually reads/writes are named; the rest are raw padding.
//
// Combat unit array (gilde.exe word_B5A350 @0xB5A350):
//   stride 536 / 0x218, capacity 32 (17152 / 536). marker word @+0 (-1 == free).
//   id column dword_B5A354 @0xB5A354 (parallel, stride 134 dwords).
// (Mirror of the Person master-array idiom; see VIBE_Combat_FindUnitById
//  @0x486430 — linear scan, bound 17152 = 536 * 32.)
#include "guild/common/types.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// Combat unit record  (gilde.exe word_B5A350 @0xB5A350)
//   stride 536 / 0x218, 32 slots. Combat re-interprets the copied Person record:
//     +0   marker word (-1 == free); also low word == person/unit id (*v1)
//     +4   person/entity id (dword)   [used as id, == dword_B5A354[i] column]
//     +8   "alive" byte (0 == dead; set !=0 when spawned)  (a1[2] low byte)
//     +28  worth/value float  (unit[7]; loot/contribution scale)
//     +32  active-target scratch dword (unit[8]; set by ResolveMeleeHit delta)
//     +36  HIT POINTS (int, unit[9]) — damage subtracts from this
//     +364 team/faction id (dword, unit[91]; compared !=  -> enemy)
//     +388 character-actor ptr (dword, unit[97]; the live in-scene Character)
//     +400 base-move-speed float scratch (unit+416 written by SpawnUnit)
// ---------------------------------------------------------------------------
constexpr int kUnitStride   = 536;   // 0x218
constexpr int kUnitCapacity = 32;    // 17152 / 536

GUILD_PACKED_BEGIN
struct CombatUnit {
    i16 marker;          // +0x00  (-1 == free slot); low word also == unit id
    u8  pad2[2];         // +0x02
    i32 id;              // +0x04  unit / person id
    u8  alive;           // +0x08  alive byte (0 == dead)
    u8  pad9[19];        // +0x09..+0x1B
    float worth;         // +0x1C  (+28, unit[7]) worth/value scale float
    i32  targetScratch;  // +0x20  (+32, unit[8]) active-target scratch
    i32  hp;             // +0x24  (+36, unit[9]) HIT POINTS
    u8  pad40[324];      // +0x28..+0x16B
    i32 teamId;          // +0x16C (+364, unit[91]) team / faction id
    u8  pad368[20];      // +0x170..+0x183
    i32 actorPtr;        // +0x184 (+388, unit[97]) character-actor ptr
    u8  pad392[144];     // +0x188..+0x217  (392..535)
} GUILD_PACKED;
GUILD_PACKED_END
static_assert(sizeof(CombatUnit) == kUnitStride, "CombatUnit stride must be 536");

// ---------------------------------------------------------------------------
// Object definition (weapon / object-type descriptor) — the relevant combat
// fields read by VIBE_Combat_ResolveMeleeHit / PerformAttackAction. The full
// record lives in the object-type table (VIBE_Combat_FindObjectDef @0x485a54);
// combat reads only:
//     +0  type word (weapon-type id: 340/342/344/350/352/366/370/372/374 ...)
//     +7  base damage/speed byte (SpawnUnit: speed = def[7] * 0.0166667)
//     +88 weapon class byte (1/2 = ranged-ish target gate; FindActiveTarget)
// The weapon-type word values select the melee sound/animation AND gate the
// hit/death branch (see ResolveMeleeHit). We model just these three fields.
// ---------------------------------------------------------------------------
GUILD_PACKED_BEGIN
struct ObjectDef {
    i16 weaponType;      // +0x00  weapon-type word
    u8  pad2[5];         // +0x02
    u8  baseStat;        // +0x07  base damage/speed byte
    u8  pad8[80];        // +0x08..+0x57
    u8  weaponClass;     // +0x58  (+88) weapon class (1/2 special)
} GUILD_PACKED;
GUILD_PACKED_END
static_assert(sizeof(ObjectDef) == 89, "ObjectDef partial record must be 89 bytes");

// Weapon-type word constants observed in VIBE_Combat_ResolveMeleeHit /
// PerformAttackAction branch selection. (Asset/mesh ids in the object table.)
enum WeaponTypeId : i16 {
    kWpnFistGrab1   = 0x15C, // 348 — punch variant (sound dword_631220)
    kWpnFistGrab2   = 0x15E, // 350 — (CreateSoundAction ausweichen)
    kWpnDodge       = 0x160, // 352 — dodge-able strike
    kWpnSword       = 342,   // melee, parry sound branch
    kWpnSword2      = 344,   // melee, parry sound branch
    kWpnStab        = 340,   // stab (death branch)
    kWpnStab2       = 366,   // stab -> banner "x defeats y" (sets +8=4)
    kWpnStab3       = 370,   // stab variant
    kWpnDropBomb    = 372,   // special: drop-bomb action
    kWpnThrowBomb   = 374,   // special: throw-bomb action
};

// ---------------------------------------------------------------------------
// Duel state  (the pistol-duel mini-game).  Globals recovered from
// VIBE_Duel_ResolveShot @0x4a4b68 / VIBE_Duel_ProcessIntroChoice @0x4a4eb4.
//   dword_11B4E30 / dword_11B4E34  — the two combatant unit ptrs (slot A / B)
//   byte_6315DC / byte_6315DD      — accumulated hit-score for A / B (byte_6315DC
//                                    += damage when A's unit (==slot A) is hit)
//   byte_6315DE / byte_6315DF      — "rattled" flag (lost the taunt) for A / B
//                                    -> shot hit-chance * 0.66
//   byte_6315E0 / byte_6315E1      — "aim" flag (won the concentrate roll) for A/B
//                                    -> damage roll narrowed to 5..19 (else 15..44)
//   dword_6315C4                   — duel-over flag (set on fatal hit)
//   dword_11AA540[seq % count]     — shared deterministic RNG table (cross-peer)
//   dword_6315E8 / dword_6315E4    — RNG cursor / table length
// The shot uses a per-combatant production rating as the SKILL stat
// (VIBE_Building_EvalProductionRating(unit, 3) — forward-declared, mocked in test).
// ---------------------------------------------------------------------------
struct DuelState {
    // Combatant skill ratings (EvalProductionRating(unit,3) for each side).
    float skillA = 0.0f;     // attacker (slot A) shooting skill
    float skillB = 0.0f;     // defender (slot B) shooting skill
    // Per-side flags set by the intro choices (taunt / aim).
    bool  rattledA = false;  // byte_6315DE — A lost the taunt (hit chance * 0.66)
    bool  rattledB = false;  // byte_6315DF
    bool  aimA = false;      // byte_6315E0 — A won the concentrate (tight damage)
    bool  aimB = false;      // byte_6315E1
    // 367: object-slot "good pistol" active -> hit-chance * 1.5.
    bool  goodPistolA = false;
    bool  goodPistolB = false;
    // Accumulated damage score (byte_6315DC / byte_6315DD).
    u8    scoreA = 0;
    u8    scoreB = 0;
    bool  over = false;      // dword_6315C4
};

// ===========================================================================
// Battle state-machine layouts (recovered for the combat battle module:
// VIBE_Combat_RunBattleSetup @0x490014, RunBattleLoop @0x492c28,
// UpdateUnitOrders @0x491688, AssignUnitsToRoles @0x48bfb0, EvaluateBattleOutcome
// @0x48cf6c). These mirror the squad-state record (dword_631208 / dword_11AB000)
// and the per-unit 44-byte order slot the order-tick state machine walks.
// ===========================================================================

// ---------------------------------------------------------------------------
// Battle descriptor (the `a1` arg to RunBattleSetup / *the* squad-state record
// dword_631208). Recovered field offsets from RunBattleSetup/EvaluateBattleOutcome/
// ComputeUnitBalanceWeights:
//     +4   commander/owner person id (dword)        [== a1[4], the squad id]
//     +12  scenario/level id (dword_631204 = a1[12])
//     +20  query filter id (alt roster filter)
//     +48  active-slot count byte (*(a1+48), <=16)  [number of populated rosters]
//     +52  per-slot owner-person id column (dword[N], -1 == empty)
//     +124 mode flags byte (bit0=raid/conquer, bit1=attack, bit2=defend/mission)
//     +128 attacker roster: 16 person-ids (dword[16], -1 == empty)
//     +192 defender roster: 16 person-ids (dword[16], -1 == empty)
//     +256 production-object ids (dword[3], -1 == empty) — raid loot check
//     +268 person-query filter for QueryBegin
// The two sides' "owner person" records are dword_6311E8 (attacker / side A) and
// dword_6311E4 (defender / side B). We model the roster as plain id arrays.
// ---------------------------------------------------------------------------
constexpr int kRosterCapacity = 16;   // 64 bytes / 4

struct BattleDescriptor {
    i32 squadOwnerId = -1;             // +4   commander/owner person id
    i32 scenarioId   = 0;              // +12  level id (-> dword_631204)
    i32 queryFilter  = 0;              // +20
    u8  slotCount    = 0;              // +48  active-slot count
    u8  modeFlags    = 0;              // +124 bit0 raid, bit1 attack, bit2 defend
    i32 attackerOwnerId = -1;          // +52 side A owner (-> dword_6311E8)
    i32 defenderOwnerId = -1;          // +56 side B owner (-> dword_6311E4)
    i32 attackerRoster[kRosterCapacity]; // +128
    i32 defenderRoster[kRosterCapacity]; // +192
    i32 productionObjects[3] = {-1, -1, -1}; // +256

    BattleDescriptor() {
        for (int i = 0; i < kRosterCapacity; ++i) {
            attackerRoster[i] = -1;
            defenderRoster[i] = -1;
        }
    }
};

// Mode-flag bits on BattleDescriptor::modeFlags (*(a1+124)).
enum BattleModeFlag : u8 {
    kBattleRaid    = 0x01,  // bit0 — raid / object-conquer scenario
    kBattleAttack  = 0x02,  // bit1 — open attack
    kBattleDefend  = 0x04,  // bit2 — defend / mission
};

// ---------------------------------------------------------------------------
// Per-unit order slot (the 44-byte record the order-tick state machine walks:
// `v80 += 44` in VIBE_Combat_UpdateUnitOrders @0x491688). Each commander squad
// holds 16 of these starting at `commander+37 dwords` (= +148 bytes; v87+37).
// Recovered field offsets (byte offsets into the 44-byte slot):
//     +0   target/unit id (dword, -1 == empty slot)
//     +4   order-type/state byte (0 idle, 1 move, 2 attack, 3 march, 4 capture,
//          5 stand, 6 ware-collect, 7 escape, 8 standup) — the switch selector.
//          (==5 means the target id is a Person rather than a CombatUnit.)
//     +8   in-flight command/packet id (dword, -1 == none, 1 == ready)
//     +12  call/phase counter byte (v4[12]); also walk-issued flag
//     +16  target tile X (dword, *(v4+4)) — and order-specific args follow
//     +20  target tile Z (dword, *(v4+5))
//     +24  aux tile / arg (dword, *(v4+6))
//     +28  "moved" flag byte (v4[28])
//     +29  in-range/firing flag byte (v4[29] = v84)
//     +32  predicted-damage low byte / hit flag (*(v4+8) dword)
//     +34  ware-phase byte (v4[34])
//     +36  predicted-damage value (dword, *(v4+9))
// ---------------------------------------------------------------------------
constexpr int kOrderSlotStride   = 44;
constexpr int kOrderSlotsPerSquad = 16;

GUILD_PACKED_BEGIN
struct OrderSlot {
    i32 unitId;          // +0   target/unit id (-1 == empty)
    u8  state;           // +4   order-type / state byte (switch selector)
    u8  pad5[3];         // +5
    i32 packetId;        // +8   in-flight packet id (-1 none, 1 ready)
    u8  phase;           // +12  call/phase counter / walk-issued flag
    u8  pad13[3];        // +13
    i32 tileX;           // +16  target tile X (*(v4+4))
    i32 tileZ;           // +20  target tile Z (*(v4+5))
    i32 tileAux;         // +24  aux tile / arg (*(v4+6))
    u8  moved;           // +28  moved flag (v4[28])
    u8  firing;          // +29  in-range / firing flag (v4[29] = v84)
    u8  pad30[2];        // +30
    i32 hitFlag;         // +32  predicted-hit flag (*(v4+8) dword, bytes 32..35).
                         //      NOTE: the original also reads byte +34 (v4[34], the
                         //      ware-collection phase) which aliases this dword's
                         //      byte 2 — use WarePhase()/SetWarePhase() below.
    i32 predictedDamage; // +36  predicted-damage value (*(v4+9))
    u8  pad40[4];        // +40  trailing slot bytes (stride padding to 44)

    // Aliased accessor for the ware-phase byte (+34 == hitFlag byte 2).
    u8  WarePhase() const {
        return static_cast<u8>((static_cast<u32>(hitFlag) >> 16) & 0xFF);
    }
} GUILD_PACKED;
GUILD_PACKED_END
static_assert(sizeof(OrderSlot) == kOrderSlotStride, "OrderSlot must be 44 bytes");

// Order-state values (OrderSlot::state) — the switch(v4[4]) selector in
// VIBE_Combat_UpdateUnitOrders.
enum OrderState : u8 {
    kOrderIdle      = 0,
    kOrderMove      = 1,  // move to a tile (op78 path)
    kOrderAttack    = 2,  // attack: range gate + hit-chance roll + dmg predict
    kOrderMarch     = 3,  // labelled march to tile
    kOrderCapture   = 4,  // capture object via bone-chain probe
    kOrderStand     = 5,  // stand (target id is a Person id)
    kOrderWareCollect = 6,// ware-save collection phases
    kOrderEscape    = 7,  // flee to nearest free tile
    kOrderStandUp   = 8,  // stand-up
};

// Combat-role ids (the unit-role byte written at CombatUnit+452 by
// AssignUnitsToRoles; the switch(role) selector in BuildOrderForUnit /
// ScoreUnitForRole). 0..5.
enum CombatRole : u8 {
    kRoleAttack       = 0,  // engage nearest enemy / threatened tile
    kRoleConquerWare  = 1,  // grab a ware object
    kRoleMoveConquer  = 2,  // move to a conquer target
    kRoleHold         = 3,  // simple hold
    kRoleTile         = 4,  // tile order
    kRoleEscape       = 5,  // flee
};

// ---------------------------------------------------------------------------
// Bomb projectile records (the thrown/dropped-bomb tables in
// VIBE_Combat_UpdateThrownBombs @0x486ce4 / UpdateBombExplosions @0x4866b8):
//   thrown bombs:  dword_B5F910[handle], dword_B5F914[flags+45], dword_B5F918
//                  (tile X), dword_B5F91C (tile Z) — 16-byte stride, 32 slots.
//   dropped bombs: dword_B5F810[handle], dword_B5F814[spawn-tick] — 8-byte
//                  stride, 32 slots; detonates at spawnTick+350.
// We model just the gameplay-relevant fields (position + timing + damage band).
// ---------------------------------------------------------------------------
struct Bomb {
    bool  active = false;
    float x = 0.0f, y = 0.0f, z = 0.0f;  // world position of the blast centre
    u8    minDamage = 0;   // object-def body[3] (d3) — base-damage low
    u8    maxDamage = 0;   // object-def body[4] (d4) — base-damage high
    u32   spawnTick = 0;   // dropped-bomb fuse base (detonates spawnTick+350)
};

} // namespace guild::sim
