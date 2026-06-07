#pragma once
// gilde.exe — Combat batch-4 LEAVES: the untranslated combat-mode rules the
// earlier batches left as presentation/driver glue — floating-point target
// distance, the escape-roll, the floating "damage number" subsystem, the
// unit attack-vs-move decision, the shot/bomb input dispatch, the pursuit-order
// roll, the target-entity spawn/event dispatch, and the death/blood-pool spawn
// y-offset math (namespace guild::sim, MODULE combat, prefix VIBE_Combat_*).
//
// SCOPE. Each original addresses static globals and issues cross-module
// GameObject / Command / Audio / Object / Transform / Heightmap / Voice / Text
// calls; this module reproduces the SAME control flow + arithmetic with the side
// effects routed through CombatSlots4Hooks (inert defaults = no-op) and the table
// state passed in by value, so the logic is testable with no OS / render /
// animation / network coupling.
//
//   * DistanceToTargetXZ     (0x4864a0) — sqrt(dx*dx + 0 + dz*dz), single-precision.
//   * TriggerEscapeAction    (0x485c0c) — RandomModulo(10) < cowardice -> flee.
//   * SpawnDamageNumber      (0x487300) — allocate a free damage-number slot + ttl.
//   * UpdateDamageNumbers    (0x48736c) — per-record on-screen update / ttl decay.
//   * EvalUnitAttackMove     (0x491324) — attack-in-range vs. close-distance vs. flee.
//   * ProcessShotAndBomb     (0x4bfb68) — shot/melee/bomb input dispatch decision.
//   * UpdatePursuitTargets   (0x48c400) — clear stale pursuit + per-unit attack roll.
//   * ResolveTargetEntityRef (0x57ea8c) — event-building pick vs. person spawn dispatch.
//   * SpawnDeathBloodPool    (0x48c708) / SpawnBloodPool (0x4a4944) — blood-pool y math.
//
// x87 NOTE. The originals compute the distance/scale rolls in the x87 FPU at 80-bit
// extended precision and store back through 32-bit `float` slots (`*(float*)&v7`).
// We mirror that: the squared terms accumulate in `double` (the widest the visible
// operations need) but each intermediate that the original spills to a `float`
// local is truncated through `float` so a host double-rounding cannot leak in. The
// blood-pool y term is `(double)rand * (float)scaleA * (double)scaleB` written back
// to a float — we reproduce the exact promotion order.
#include "guild/common/types.h"

#include <vector>

namespace guild::sim {

// ===========================================================================
// Recovered table geometry / constants
// ===========================================================================
//   dword_B5F6C0.. — the damage-number table: kDmgNumCount records, stride 5 dwords
//     +0 age/ttl (dword_B5F6D0, a float), +1 value (dword_B5F6D4, int + the "live"
//     flag: nonzero == slot in use), +2 owner unit (dword_B5F6DC), +3 (unused here),
//     +4 kind (dword_B5F6E0: 0 plain, 1 "[-n]", 2 "(-n)"). The widget handle lives in
//     a parallel cell (dword_B5F6D8) the update leaf manages.
constexpr int kDmgNum4Count  = 16;   // 80 / 5  (loop bound `v0 >= 80` step 5)

// TriggerEscapeAction roll modulus (RandomModulo(10) < cowardice). 0x485c5b.
constexpr int kEscapeRollMod = 10;

// EvalUnitAttackMove: object-def type ids that mean "no usable weapon mesh" so the
// unit just closes distance instead of attacking (0x49140f switch: 0/340/342/344/
// 366/370/374). 0 == null def.
// UpdatePursuitTargets: the attack-vs-flee gate (0x48c51f):
//   if outputRatio*K >= 20  OR  RandomModulo(100) <= 30  -> attack the nearest enemy.
constexpr int kPursuitAttackThreshold = 20;   // (int)(outputRatio*K) >= 20
constexpr int kPursuitRollMod         = 100;  // RandomModulo(100)
constexpr int kPursuitRollCutoff      = 30;   // roll <= 30

// ===========================================================================
// Leaf hooks — the cross-module side effects these bodies make. Tests install a
// recording mock; the inert default (all null) makes every leaf a no-op.
// ===========================================================================
struct CombatSlots4Hooks {
    // --- damage-number widget management (UpdateDamageNumbers) -------------------
    // Project the owning unit to screen; returns true + writes x/y if on screen.
    bool (*objectScreenBounds)(const void* unit, int* outX, int* outY) = nullptr;
    int  (*createTextLabel)(int x, int y, const char* text) = nullptr;   // -> widget
    void (*widgetLayoutBounds)(int x, int y, int widget) = nullptr;      // reposition
    void (*objectSetVisible)(int widget, int visible) = nullptr;
    void (*widgetDestroyByType)(int widget) = nullptr;

    // --- escape action (TriggerEscapeAction) ------------------------------------
    void (*resetObjectHighlights)(const void* unit) = nullptr;
    void (*queueEscapeDelta)(const void* unit, i8 flagValue) = nullptr;  // the +453 write
    void (*sendFleeMessage)(const void* unit) = nullptr;                 // the formatted shout

    // --- pursuit / attack-move command coupling ---------------------------------
    void (*issueAttackOrder)(const void* unit, const void* target) = nullptr;
    void (*issueMoveOrder)(const void* unit, int tileX, int tileZ) = nullptr;

    // --- blood-pool spawn (SpawnDeathBloodPool / SpawnBloodPool) ----------------
    // Attach the blood-pool mesh at the unit node with the computed y offset, then
    // load its animation. Returns the new object handle (0 == failed).
    int (*spawnBloodPoolMesh)(const void* unit, float yOffset) = nullptr;
};

void SetCombatSlots4Hooks(const CombatSlots4Hooks* hooks);
const CombatSlots4Hooks& GetCombatSlots4Hooks();

// ===========================================================================
// DistanceToTargetXZ  (gilde.exe 0x4864a0)
// ===========================================================================
// dx = target.x - self.x ; dz = target.z - self.z ; return sqrt(dx*dx + 0 + dz*dz).
// The original reads self at unit+388 -> +52 -> +76/+84 and target at target+76/+84;
// we take the resolved world coordinates. Returns single-precision-faithful distance
// (squared terms truncate through float as the original spills v3/v4 to float slots).
float DistanceToTargetXZ(float selfX, float selfZ, float targetX, float targetZ);

// ===========================================================================
// TriggerEscapeAction  (gilde.exe 0x485c0c)
// ===========================================================================
// Only acts when the unit's +459 flags have bit 0x20 set ("can flee"). Rolls
// RandomModulo(10); if the roll is LESS than the unit's cowardice byte (+453) the
// unit flees: highlights are cleared, the escape delta (flag = -cowardice) is queued,
// and — if the unit is at a target building of class 6/7 — a flee shout is sent.
// Otherwise it just queues the "stop" delta (flag = 1). Returns true == fled.
struct EscapeInput {
    bool canFlee = false;       // (unit+459 & 0x20) != 0
    u8   cowardice = 0;         // unit+453 byte
    bool atBuilding = false;    // unit+91 dword resolves to a building (v6 && +39 != 0xFFFF)
    u8   buildingClass = 0;     // word_12CE910[...][1] of that building (6/7 -> shout)
    bool hasObjectDef = false;  // FindObjectDef(unit) != null && *def != 0
};
bool TriggerEscapeAction(const EscapeInput& in, const void* unit);

// ===========================================================================
// Damage-number subsystem  (gilde.exe 0x487300 / 0x48736c)
// ===========================================================================
struct DmgNumberRecord {
    float ttl   = 0.0f;   // +0 dword_B5F6D0 (float age; <= 0 -> expire)
    i32   value = 0;      // +1 dword_B5F6D4 (the "-n" amount; the slot's "live" flag
                          //   is `value != 0` — a 0 value marks the slot free)
    const void* owner = nullptr; // +2 dword_B5F6DC (the unit the number floats over)
    i32   kind  = 0;      // +4 dword_B5F6E0 (0 plain, 1 "[-n]", 2 "(-n)")
    i32   widget = -1;    // dword_B5F6D8 (text-label handle; -1 == none)
};

// gilde.exe 0x487300 — VIBE_Combat_SpawnDamageNumber.
// Finds the first free slot (value == 0), scanning index 0 then 5,10,...,75. If a
// free slot exists it stores owner, kind, seeds the float ttl cell (dword_B5F6D0) to
// 64.0f (the immediate 1115684864 == 0x42800000 == 64.0f), and stores the VALUE cell
// (dword_B5F6D4) as `(int)((double)amount / (unitScale * 0.01f))` — the
// `v6 = (double)result / (*(float*)(a1+28) * dbl_61B1C4)` computation, dbl_61B1C4 ==
// 0.01. Returns the chosen slot index (== (int)v6 in the original's eax, but we
// return the SLOT so the caller can address it), or -1 if the table is full.
//   `unitScale` is *(float*)(unit+28). `amount` is the incoming a2 (edx) value.
constexpr float kDmgNumInitialTtl = 64.0f;   // 1115684864 == 0x42800000
constexpr float kDmgNumScale      = 0.01f;   // dbl_61B1C4 (the unit-scale divisor)
int SpawnDamageNumber(std::vector<DmgNumberRecord>& table, const void* owner,
                      int amount, int kind, float unitScale);

// gilde.exe 0x48736c — VIBE_Combat_UpdateDamageNumbers.
// For each LIVE slot (value != 0): project the owner to screen. If off-screen, hide
// the widget. If on-screen, create the label (when widget == -1, formatting the value
// per `kind`) or reposition it, show it, then decay ttl by dbl_61B1DC (a negative
// per-frame step). When ttl <= 0 the widget is destroyed and the slot freed
// (value = 0, owner = null, widget = -1). Returns the number of slots that EXPIRED
// this call. The per-frame ttl step is supplied as `ttlStep` (the original constant
// dbl_61B1DC, expected negative).
int UpdateDamageNumbers(std::vector<DmgNumberRecord>& table, double ttlStep);

// gilde.exe 0x48736c (inner sprintf branches) — format the floating "-n" label.
//   kind 1 -> "[-n]" (bracket chars 92/93) ; kind 2 -> "(-n)" (chars 94/95) ; else "-n".
// `value` is the stored magnitude; the text always shows a leading minus.
void FormatDamageText(char* out, unsigned long cap, int value, int kind);

// ===========================================================================
// EvalUnitAttackMove  (gilde.exe 0x491324)
// ===========================================================================
// The per-unit "what order to issue" decision. The deterministic decision (the part
// not buried in command packet plumbing) is:
//   * If the unit's weapon-class (objDef+88) is 1 or 2 (a thrown/ranged weapon) and
//     it has an active target whose hp (+0x20) <= 0  -> kHoldNoTarget (return -1).
//   * If the unit has NO usable weapon def (def == null OR def type in the
//     {0,340,342,344,366,370,374} set): if it ALSO has no target or the target is out
//     of weapon range (distance > def.range)  -> kMoveToTarget (close distance);
//     else fall through.
//   * If the unit is already busy (unit +388 -> +296 != 0)  -> kHoldBusy (return -1).
//   * Otherwise  -> kAttack (issue the attack packet).
enum class AttackMoveDecision { kHoldNoTarget, kMoveToTarget, kHoldBusy, kAttack };

struct AttackMoveInput {
    bool hasObjectDef   = false;  // FindObjectDef(self) != null
    i16  objDefType     = 0;      // *(u16*)def  (the def's type id)
    i8   weaponClass    = 0;      // def+88 (1/2 == ranged/thrown)
    bool hasActiveTarget = false; // FindActiveTarget(self) != null
    i32  activeTargetHp  = 0;     // target+0x20 ( *((int*)t+8) )
    bool hasTarget      = false;  // a usable move/attack target exists
    float distanceToTarget = 0.0f; // DistanceToTargetXZ(self, target)
    float weaponRange   = 0.0f;   // def+12 (v24+12) range threshold
    bool selfBusy       = false;  // self +388 -> +296 != 0
};
AttackMoveDecision EvalUnitAttackMove(const AttackMoveInput& in);

// Returns true if `objDefType` is in the "no usable weapon mesh" set (the 0x49140f
// switch: 0, 340, 342, 344, 366, 370, 374).
bool IsNoWeaponDefType(i16 objDefType);

// ===========================================================================
// ProcessShotAndBomb  (gilde.exe 0x4bfb68)
// ===========================================================================
// The shot/bomb input dispatch run each frame after UpdateBombExplosions. With the
// "combat input armed" flag (byte_671D96) clear it does nothing. When armed it sets a
// status word (33) and dispatches based on two request flags:
//   * shotRequested (dword_672228): play the shot sound, apply a pending melee hit
//     (if dword_631734), clear the pending-hit slot, then if bombRequested also place
//     the bomb (raycast from cursor -> tile -> spawn) and clear the mouse mask.
//   * else if bombRequested (dword_672230): just place the bomb + clear the mask.
//   * else: nothing further.
// Returns the dispatch path taken so the side-effect order is assertable.
enum class ShotBombPath { kDisarmed, kNone, kShotOnly, kShotThenBomb, kBombOnly };

struct ShotBombInput {
    bool armed         = false;   // byte_671D96 != 0
    bool shotRequested = false;   // dword_672228 != 0
    bool bombRequested = false;   // dword_672230 != 0
    bool hasPendingHit = false;   // dword_631734 != 0
};

// Outcome side-effect flags (so tests can assert without a full hooks mock).
struct ShotBombOutcome {
    ShotBombPath path = ShotBombPath::kDisarmed;
    bool playedShotSound = false;
    bool appliedMeleeHit = false;
    bool placedBomb = false;
    bool clearedMouseMask = false;
    int  statusWord = 0;          // word_62D310 (33 when armed, else untouched -> 0)
};
ShotBombOutcome ProcessShotAndBomb(const ShotBombInput& in);

// ===========================================================================
// UpdatePursuitTargets  (gilde.exe 0x48c400)  — per-unit attack/flee roll
// ===========================================================================
// The original first clears stale pursuit records (state 2 whose target unit is dead)
// then, for each commanded unit, decides between "press the attack" and "flee to the
// nearest escape tile":
//   attack  if  (int)(outputRatio * K) >= 20  OR  RandomModulo(100) <= 30
//   else    flee.
// We model the per-unit decision as a pure function (the loop / command coupling is
// the caller's). Returns true == attack, false == flee.
bool PursuitPressesAttack(int scaledOutputRatio, int roll100);

// gilde.exe 0x48c400 (head) — the stale-pursuit clear predicate. A pursuit record in
// state 2 is cleared (state -> 0, target -> -1) when its target unit is dead
// (targetAlive == false). States 3 and 4 are kept; state 2 with a live target is kept.
// `duelMode` (dword_6315A4) must be > 1 for the per-record clear to run at all.
bool PursuitRecordShouldClear(int duelMode, u8 state, bool targetAlive);

// ===========================================================================
// ResolveTargetEntityRef  (gilde.exe 0x57ea8c)
// ===========================================================================
// Dispatches by the `flags` byte (a1@al):
//   bit0 set  -> pick a random event building (PickRandomEventBuildings(seed)); if it
//                returned a valid id (!= 0xFFFF) AND bit1 set, stamp two owner bytes
//                (byte_12CEA75 = a5, byte_12CEA74 = a2) into that entity's record.
//   bit0 clear-> spawn a new person (CreateAndSpawn(...)) and use its id.
//   id == 0xFFFF -> return null.
//   Otherwise the entity record is &word_12CE910[268 * id]; if bit2 set AND a
//   building-context pointer (a6) is non-null AND ResolveTargetObjekt(a6) succeeds,
//   spawn a character at the building entrance. Return the entity record.
// We return the chosen entity id (-1 == null) and the side-effect flags taken.
struct ResolveTargetInput {
    u8   flags = 0;            // a1@al
    u8   owner = 0;            // a2@cl  (byte_12CEA74)
    u16  seed = 0;             // a3@bx  (PickRandomEventBuildings seed / spawn arg)
    u8   personType = 0;       // a4@dl  (CreateAndSpawn type)
    u8   ownerB = 0;           // a5     (byte_12CEA75)
    bool hasBuildingCtx = false; // a6 != 0
    // resolved by hooks/caller:
    int  pickedEventId = -1;   // PickRandomEventBuildings result (-1 == 0xFFFF)
    int  spawnedPersonId = -1; // CreateAndSpawn result (-1 == 0xFFFF)
    bool resolveObjektOk = false; // ResolveTargetObjekt(a6) != 0
};
struct ResolveTargetOutcome {
    int  entityId = -1;        // -1 == returned null
    bool stampedOwnerBytes = false; // wrote byte_12CEA74/75
    bool spawnedAtEntrance = false; // SpawnAtBuildingEntrance called
};
ResolveTargetOutcome ResolveTargetEntityRef(const ResolveTargetInput& in);

// ===========================================================================
// Blood-pool spawn y-offset  (gilde.exe 0x48c708 / 0x4a4944)
// ===========================================================================
// Both spawn a "ub_Blutlache" mesh at the dying unit's node. The only arithmetic is
// the y component of the placement vector: starting from a 3-int template, the y slot
// is overwritten with  (double)RandInt(360) * scaleA * scaleB  truncated through the
// float slot it is stored into (`*(float*)&v7`). SpawnDeathBloodPool uses the table at
// dword_484964 + (flt_61B930, dbl_61B934); SpawnBloodPool uses dword_49D7D8 +
// (flt_61CD60, dbl_61CD64). We expose the shared y math; the values differ only by the
// constants the caller supplies.
//   randRoll = RandInt(360)  (0..359). scaleA is a float constant, scaleB a double.
float BloodPoolYOffset(u32 randRoll, float scaleA, double scaleB);

// Recovered blood-pool constants (the originals' immediate FP literals).
//   flt_61B930 / dbl_61B934  (SpawnDeathBloodPool) ; flt_61CD60 / dbl_61CD64
//   (SpawnBloodPool). Decompilation shows the same expression shape for both; the
//   exact literals are recovered from the .rdata below.
extern const float  kDeathBloodScaleA;   // flt_61B930
extern const double kDeathBloodScaleB;   // dbl_61B934
extern const float  kBloodScaleA;        // flt_61CD60
extern const double kBloodScaleB;        // dbl_61CD64

} // namespace guild::sim
