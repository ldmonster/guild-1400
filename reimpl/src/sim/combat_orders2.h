#pragma once
// gilde.exe — Combat ORDER-DRIVER + battle-UI layout RULES (namespace guild::sim).
// MODULE: combat (prefix VIBE_Combat_*). This file translates the remaining BIG
// combat drivers the earlier combat agents left undone, confirmed untranslated by
// DEFINITION (the earlier files reference these only in prose comments):
//
//   * VIBE_Combat_UpdateUnitOrders     @0x491688 — the per-slot order-tick DRIVER
//     (the deferred ~0x159d-byte command-packet state machine). combat_battle.cpp
//     (EvaluateAttack/TickOrderSlot) and combat_orders.cpp (TickNonAttackOrder)
//     translated the per-state RULE math at an abstracted altitude; what was never
//     reconstructed is the TOP-LEVEL driver: the prelude gate (dword_6311F0), the
//     16-slot iteration over commander+37dwords (stride 44), the per-slot unit
//     resolution (state==5 -> Person, else CombatUnit), the dead-unit PRUNE that
//     resets the slot to (id=-1,state=0) and bails, and the exact switch(state)
//     1..8 that sequences the RequestBuildOp78/80/81/85 packet emissions. We model
//     the scene/path/heightmap/command leaves through installable hooks and surface
//     the emitted command sequence so the 8-case dispatch is testable 1:1.
//   * VIBE_Combat_CreateOrderSlotWindows @0x48857c — the order-slot HUD builder
//     RULE: reset the 6 parallel slot-handle tables (stride 8, 6 entries, all -1),
//     then for each populated roster entry that passes the selection-highlight gate
//     (faction-match OR global highlight) allocate a slot row; iterate while
//     roster<31 AND slotCol<48. (Widget creation is the leaf; the table reset, the
//     gate, and the advance bookkeeping are the rule.)
//   * VIBE_Combat_BuildDummyTargetUnits @0x48a9cc — the FERN/NAHK target-naming
//     RULE: per roster unit, classify by weapon object-def class word
//     (372/374/350/352 -> FERN long-range, else NAHK melee), emit
//     "dummy_<tag>_FERN_%02i" / "dummy_<tag>_NAHK_%02i" with two independent
//     1-based counters; 16 slots.
//   * VIBE_Combat_BuildObjectiveIcons  @0x48d518 — objective-icon GRID layout RULE:
//     x = 120*(idx%5)+baseX, y = 96*(idx/5)+baseY; per-faction filter (mode 0 all,
//     1 own only, 2 enemy only); accumulate total = sum(price*count).
//   * VIBE_Combat_BuildUnitRosterPanel @0x48d7f8 — roster-panel COLUMN layout RULE:
//     count populated slots -> colW = (screenW-48)/(count+1); per slot
//     x = colW + colW*(i%cols)+baseX, y = 100*(i/cols)+baseY; health-bar ratio.
//   * VIBE_Combat_BuildHudWindow       @0x4906a8 — HUD slider-centering RULE:
//     sliderX = (screenW-400)/2.
//
// Determinism: the order-driver's attack hit-chance roll reuses the CRT LCG via
// the installed randomModulo hook (the integration test wires it to the REAL
// guild::sim::Math_RandomModulo over crt::RandNext); every other leaf is inert.
#include "guild/common/types.h"
#include "sim/combat_types.h"

#include <vector>

namespace guild::sim {

// ===========================================================================
// UpdateUnitOrders driver — the per-slot command-packet state machine.
// ===========================================================================
//
// The original walks 16 OrderSlot records and, per non-dead slot, runs a
// switch(slot.state) that GATES on the in-flight packet, evaluates the unit's
// world position via heightmap/path leaves, and emits the next command. We surface
// the emission as an ordered list of OrderCommandKind and route the scene leaves
// through OrderDriverHooks so the dispatch is exercisable without the engine.

// The wire opcode an order-tick case emits (the RequestBuildOp* call). Matches the
// recovered opcodes; surfaced for testing + sequencing assertions.
enum class OrderEmit : u8 {
    Op78Path,   // VIBE_Command_RequestBuildOp78DualStr — walk to a (free) tile
    Op80Sync,   // VIBE_Command_RequestBuildOp80        — order-phase complete
    Op81Ware,   // VIBE_Command_RequestBuildOp81        — ware-save sync (state 6)
    Op85Anim,   // VIBE_Command_RequestBuildOp85Unit    — set unit anim/formation
};

// The scene/path/command leaves the order driver needs. Every default is INERT
// (deterministic): findFreeTile fails, onTargetTile fails, the unit is never busy,
// the unit/def always resolve, randomModulo returns 0. Tests install recording /
// real implementations. Mirrors the established CombatDriversHooks pattern.
struct OrderDriverHooks {
    // VIBE_Command_GetPacketStatusById(id): nonzero once the in-flight packet has
    // been applied/acked (the gate that lets a slot advance). The original also
    // treats packetId==1 as "ready" before calling this; the driver handles the
    // ==1 short-circuit itself. Default: 0 (never acked) — but the driver still
    // advances a slot whose packetId is the ready sentinel 1.
    int (*packetStatus)(i32 packetId) = nullptr;

    // VIBE_Combat_FindUnitById / FindObjectDef resolution. `unitAlive` mirrors
    // *(unit+8) (0 == dead/no-actor). `defClass` mirrors *(WORD*)ObjectDef (the
    // weapon/role class word, e.g. 372/374). `hasDef` mirrors ObjectDef != null.
    // Default: alive, no def (class 0) — the dead-unit prune therefore does NOT
    // fire by default (alive), letting the switch run.
    struct UnitInfo { bool alive = true; bool hasDef = false; i16 defClass = 0; };
    UnitInfo (*resolveUnit)(i32 unitId, u8 state) = nullptr;

    // VIBE_Math_VectorWithinTolerance(unitOrigin, tileWorld, tol) — is the acting
    // unit standing on its target tile within `tol` world units? Default: false.
    bool (*onTargetTile)(const OrderSlot& slot, float tol) = nullptr;

    // VIBE_Path_FindNearestFreeTile(inX,inZ,&outZ,&outX) — a reachable free tile
    // near the goal; writes it back. Default: fails (no free tile).
    bool (*findFreeTile)(i32 inX, i32 inZ, i32& outX, i32& outZ) = nullptr;

    // *(actor+296) != 0 — a walk/anim is still playing -> don't re-issue. Default:
    // not busy.
    bool (*unitBusy)(i32 unitId) = nullptr;

    // VIBE_Math_RandomModulo(n) — the attack hit-chance roll (state 2). Default 0.
    int (*randomModulo)(u16 n) = nullptr;

    // Command emission sink. Called once per RequestBuildOp* the driver issues, in
    // emission order. Default: no-op (the driver still mutates the slot 1:1).
    void (*emit)(int slotIndex, OrderEmit op) = nullptr;
};

void SetOrderDriverHooks(const OrderDriverHooks* hooks);
const OrderDriverHooks& GetOrderDriverHooks();

// gilde.exe 0x491688 — VIBE_Combat_UpdateUnitOrders (the driver).
// Walks `slots` (up to 16; the original's commander+37dwords stride-44 array),
// per non-empty slot resolves the unit, applies the dead-unit prune, and runs the
// switch(state) packet state machine, mutating each slot 1:1 (packetId reset to
// -1 once acked, phase byte set, tile fields updated by findFreeTile) and emitting
// the Op78/80/81/85 sequence via the hook. Returns the number of slots that did
// work this tick. When `globalHalt` (dword_6311F0) is set the driver no-ops.
//
// The 8-case shape (preserved verbatim from the decompile):
//   1 move:   gate; if phase set & onTile(30) -> LABEL_202 (Op85 anim6 + Op80);
//             else if findFreeTile -> phase=1, Op85 anim, Op78 path.
//   2 attack: the range/hit/damage RULE (delegated to EvaluateAttack altitude),
//             then Op85 anim2 + Op80 sync on completion (the firing branch).
//   3 march:  gate; if !phase & !onTile(50) & findFreeTile -> Op85+Op78; then
//             onTile(50) -> Op85 anim1 + Op80.
//   4 capture: gate; bone-probe + onTile(49.5) branches -> Op85 anim{3,6}/Op80.
//   5 stand:  gate -> Op85 anim7 + Op80.
//   6 ware:   gate; warePhase 0 -> Op85 anim6 + Op78; 1 -> Op81 + Op78;
//             2 -> onTile(50) -> Op85 anim5 + Op80.
//   7 escape: gate; phase set & onTile(30) -> LABEL_202; else findFreeTile ->
//             phase=1, Op85 anim, Op78.
//   8 standup: gate; !phase -> phase=1, Op85 anim6.
int UpdateUnitOrders(std::vector<OrderSlot>& slots, bool globalHalt = false);

// The LABEL_202 "objective reached" terminal emission (Op85 anim mode 6 + Op80
// sync), surfaced for reuse/testing. Returns the slot's new packetId (-1 reset
// then the Op80 ring slot; here the abstracted emission is reported, packetId
// reset to -1). Several cases jump here.
void EmitObjectiveReached(OrderSlot& slot, int slotIndex);

// Per-state VectorWithinTolerance thresholds (verbatim from the cases).
constexpr float kTolMove    = 30.0f;  // states 1 (move) / 7 (escape)
constexpr float kTolMarch   = 50.0f;  // state 3 (march) / state 6 phase-2
constexpr float kTolCapture = 49.5f;  // state 4 (capture)

// ===========================================================================
// BuildDummyTargetUnits — FERN/NAHK target-naming RULE.
// ===========================================================================

// Whether a unit's weapon object-def class word denotes a long-range ("FERN")
// target rather than a melee ("NAHK") one. gilde.exe 0x48aa45 test:
//   class == 372 || 374 || 350 || 352  -> FERN ; else NAHK.
bool IsFernTargetClass(i16 defClass);

// One emitted dummy-target name + its assigned 1-based index.
struct DummyTargetName {
    bool   isFern = false;  // FERN (long-range) vs NAHK (melee)
    int    index  = 0;      // the 1-based per-category counter value used
    char   name[64] = {};   // "dummy_<tag>_FERN_%02i" / "dummy_<tag>_NAHK_%02i"
};

// gilde.exe 0x48a9cc — VIBE_Combat_BuildDummyTargetUnits (the naming RULE).
// For each populated roster slot (id != -1, up to 16), classify by `defClass`
// (FERN vs NAHK) and emit the formatted dummy name with the next per-category
// counter (FERN and NAHK each start at 1 and advance independently). `tag` is the
// scenario tag the original sprintf's in (a2). `defClassOf(rosterId)` returns the
// unit's weapon-class word (the FindObjectDef leaf); a roster entry with no unit
// (resolves to <0) is skipped. Returns the names in roster order.
std::vector<DummyTargetName> BuildDummyTargetNames(const std::vector<i32>& roster,
                                                   const char* tag,
                                                   const std::vector<i16>& defClassOf);

// ===========================================================================
// CreateOrderSlotWindows — slot-table reset + highlight-eligibility RULE.
// ===========================================================================

// The 6 parallel slot-handle tables CreateOrderSlotWindows clears (stride 8, 6
// entries, all set to -1). We model them as a 6x6 flat reset target.
constexpr int kOrderSlotTableEntries = 6;   // v0: 8,16,..,48 -> 6 strides

// Reset all 6 slot-handle tables' first 6 stride-8 entries to -1 (the prelude of
// CreateOrderSlotWindows). `tables` is treated as 6 separate arrays of >=48 i32.
void ResetOrderSlotTables(i32* tables[6]);

// gilde.exe 0x48857c — VIBE_Combat_CreateOrderSlotWindows (the slot-row RULE).
// Walks up to 31 roster commanders; a commander whose primary order target
// (+364) is the player faction owner OR when the global highlight (dword_63C7B8)
// is set passes the gate and claims the next slot column (advancing by 8). Stops
// when roster index reaches 31 OR the slot column reaches 48. Returns the number
// of slot rows created. `factionMatch[i]` mirrors the +364 == player-owner test
// per commander; `populated[i]` mirrors dword_11BB6A0[i] != 0; `globalHighlight`
// is dword_63C7B8.
int CountOrderSlotRows(const std::vector<bool>& populated,
                       const std::vector<bool>& factionMatch,
                       bool globalHighlight);

// ===========================================================================
// BuildObjectiveIcons — objective-icon GRID layout + price total RULE.
// ===========================================================================

// One placed objective icon (the layout the original computes per qualifying
// objective in the 5-wide grid).
struct ObjectiveIcon {
    int x = 0;       // 120*(idx%5) + baseX
    int y = 0;       // 96*(idx/5)  + baseY
    int labelX = 0;  // x - 16
    int labelY = 0;  // y - 15  (and the second label at y + 52)
    i32 count = 0;   // *(obj+7) — the objective's quantity
    double value = 0.0; // marketPrice(obj) * count — added to the running total
};

// One enumerated objective: its faction owner, quantity, and unit market price.
struct ObjectiveEntry {
    i32    factionOwner = 0; // *(obj+5) — compared to the player faction (mode 1/2)
    i32    count = 0;        // *(obj+7)
    double marketPrice = 0.0; // LookupCachedMarketPrice(*obj) leaf result
};

// gilde.exe 0x48d518 — VIBE_Combat_BuildObjectiveIcons (the grid/total RULE).
// `entries` are the enumerated objectives (the GameObject_QueryFind/IterNext
// leaf results). `mode` is the faction filter (v25): 0 = all, 1 = only objectives
// whose owner == `playerFaction`, 2 = only objectives whose owner != playerFaction.
// `baseX`/`baseY` are the panel origin (a1/a2). Lays out the qualifying objectives
// in a 5-wide grid and accumulates the total value. Returns the placed icons; the
// final running total is written to `outTotal` (the v22 accumulator, truncated to
// int per the original's `LODWORD(v22) = (int)v18` each step).
std::vector<ObjectiveIcon> BuildObjectiveIcons(const std::vector<ObjectiveEntry>& entries,
                                               int mode, i32 playerFaction,
                                               int baseX, int baseY, i32& outTotal);

// ===========================================================================
// BuildUnitRosterPanel — roster-panel COLUMN layout RULE.
// ===========================================================================

// One placed roster cell.
struct RosterCell {
    int x = 0;          // colW + colW*(i % cols) + baseX
    int y = 0;          // 100*(i / cols) + baseY
    int healthBarX = 0; // x   (the ComputeProductionPixels row origin)
    int healthBarY = 0; // y + 60
    int labelX = 0;     // x - 36
    int labelY = 0;     // y - 17
    bool dead = false;  // *(unit+8) == 0 -> a tilt-by-(-meshAngle) extra sprite
};

// gilde.exe 0x48d7f8 — VIBE_Combat_BuildUnitRosterPanel (the column layout RULE).
// `roster` is the 16-wide unit-id array (-1 == empty). `cols` is the column count
// the original derives (`v10`, the modulus used in v7%v10 / v7/v10 — the recovered
// grid width). `screenW` is the panel width source ((dword_62D298+6)>>16). `baseX`/
// `baseY` are the origin (a2/a3). `alive[i]` mirrors *(unit+8) for slot i. Lays out
// the populated cells (16 slots) into columns spaced colW = (screenW-48)/(count+1).
// Returns the placed cells; writes the derived colW to `outColW`.
std::vector<RosterCell> BuildUnitRosterPanel(const std::vector<i32>& roster,
                                             int cols, int screenW,
                                             int baseX, int baseY,
                                             const std::vector<bool>& alive,
                                             int& outColW);

// ===========================================================================
// BuildHudWindow — HUD slider-centering RULE.
// ===========================================================================

// gilde.exe 0x490761 — the slider X-centering expression inside BuildHudWindow:
//   sliderX = ((screenW) - 400) / 2.   (`screenW` == (dword_62D298+6)>>16.)
// The rest of BuildHudWindow is window/widget creation leaves; this is the one
// deterministic geometry rule worth surfacing.
int HudSliderCenterX(int screenW);
constexpr int kHudSliderWidth = 400;

} // namespace guild::sim
