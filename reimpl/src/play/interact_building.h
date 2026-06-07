#pragma once
// Wave 25 PLAY P5 — the BUILDING-INTERACTION vertical slice: the second half of a
// world-view click. input_command.{h,cpp} bridges a click into a unit ORDER; this
// module bridges a click on a BUILDING object into its CONTACT/BUILDING DIALOG, and
// turns a menu choice there into a real building-state Command (namespace
// guild::play).
//
// GROUNDING (decompiled this wave — the live building click->dialog->command path):
//   * VIBE_Building_EnterAndDispatch @0x51defc is the building click handler. After
//     VIBE_Building_CheckEntryAllowed (already reconstructed, sim::Building6
//     CheckEntryAllowed @0x51dcd4) gates entry, it:
//        - resets the selection, loads the building interior scene, then
//        - reads the resolved scene-entity's TYPE WORD (`v31 = *v21`, the building
//          KIND code), and runs a big switch(kind) that dispatches to the matching
//          contact-menu run-loop:
//             19  -> VIBE_ContactMenu_GuildMasterActions
//             20  -> VIBE_ContactMenu_SabotageActions
//             21  -> VIBE_ContactMenu_ThreatLetterRhetoric
//             22  -> VIBE_WineCellar_RunContactLoop
//             24  -> VIBE_ContactMenu_Mistress
//             0x74(116) -> VIBE_ContactMenu_SmithProduction
//             133 -> VIBE_ContactMenu_CarpenterProduction
//             0x9B(155) -> VIBE_ContactMenu_StonemasonProduction
//             0xF7(247) -> VIBE_Location_ProductionContactLoop
//             0x114(276)-> VIBE_CityTreasury_RunContactLoop
//             ... (the full ladder is mirrored in BuildingDialogKindToActionGroup).
//          The default branch spins the plain RunFrameLoop (no contact menu).
//     The contact loops themselves (contact_loops.cpp / contact_menu.cpp, already
//     reconstructed) register status-text entries and, on a click, dispatch the
//     chosen entry to a game verb. Those verbs route mutating actions through the
//     deterministic Command channel (VIBE_Command_QueueRequest*).
//
//   * The state-mutating building command this slice EMITS+APPLIES is opcode 26 /
//     VIBE_Command_QueueRequestArgs26 @0x494848: a single-field DELTA on a building
//     record. Staging (153-byte packet): bytes[0]=26 (opcode), then three dwords at
//     +0x10: buildingId, fieldCode, value. ComputePacketSize(26)==28 (command.cpp).
//     The buildings module already uses it (building_stock.h: QueuePriceUpdate ->
//     QueueRequestArgs26(buildingId, field, floatBits), field 124=price/28=stock).
//
// This module mirrors EnterAndDispatch's kind->dialog dispatch as a small,
// deterministic FSM (BuildingDialogFsm), classifies a (kind, menu-item) pair into a
// concrete building Command (ClassifyBuildingAction — the golden), then resolves a
// world-view click via play::scene_pick (-> the REAL sim::GameObjectResolveEntityById),
// drives the FSM to the chosen action, BUILDS the opcode-26 packet, ENQUEUES it
// through the REAL sim::CommandQueue codec, and APPLIES it so the live building
// record (sim::BuildingRec over sim::g_objects) mutates.
//
// The GUI frame-loop / scene-interior / status-text-overlay leaves of EnterAndDispatch
// are not in the translated slice; they are routed through installable hooks with
// inert-by-default reconstructions defined in interact_building.cpp (the
// CutsceneMiscHooks / ObjLifeHooks / InputCommandApplyHooks pattern). The opcode-26
// APPLY handler is likewise supplied as a queue handler with an inert default that
// writes the field delta into the live building record.
//
// Additive: no edits to contact_loops.cpp / building*.cpp / wiring.cpp /
// input_command.cpp.
#include "guild/common/types.h"
#include "play/scene_pick.h"
#include "sim/command.h"

#include <functional>

namespace guild::play {

// ===========================================================================
// Building dialog ACTION GROUP — which contact-menu run-loop EnterAndDispatch's
// kind-switch selects for a building's KIND code. Each maps 1:1 to one switch arm.
// ===========================================================================
enum class BuildingActionGroup {
    kNone        = 0,   // default arm: plain frame loop, no contact menu
    kGuildMaster = 1,   // kind 19   -> ContactMenu_GuildMasterActions
    kSabotage    = 2,   // kind 20   -> ContactMenu_SabotageActions
    kThreat      = 3,   // kind 21   -> ContactMenu_ThreatLetterRhetoric
    kWineCellar  = 4,   // kind 22   -> WineCellar_RunContactLoop
    kMistress    = 5,   // kind 24   -> ContactMenu_Mistress
    kSmith       = 6,   // kind 116  -> ContactMenu_SmithProduction
    kCarpenter   = 7,   // kind 133  -> ContactMenu_CarpenterProduction
    kStonemason  = 8,   // kind 155  -> ContactMenu_StonemasonProduction
    kProduction  = 9,   // kind 247  -> Location_ProductionContactLoop
    kTreasury    = 10,  // kind 276  -> CityTreasury_RunContactLoop
};

// Mirror of EnterAndDispatch's switch(*v21): map a building's KIND code (the
// scene-entity type word) to the contact-menu action group it opens. Unmapped
// codes fall to the default (kNone) arm.
BuildingActionGroup BuildingDialogKindToActionGroup(int kind);

// ===========================================================================
// Building MENU ITEM — the entry the player clicks inside an opened contact menu.
// These name the production/storage/upgrade verbs the loops dispatch (contact_menu.h
// / contact_loops.h). Each that mutates building state maps to one building Command.
// ===========================================================================
enum class BuildingMenuItem {
    kNone        = 0,   // nothing chosen / a non-mutating verb (info/open-panel)
    kRaisePrice  = 1,   // raise the building's sale price (field 124 delta +)
    kLowerPrice  = 2,   // lower the building's sale price (field 124 delta -)
    kRestock     = 3,   // refill the building's stock     (field 28  delta +)
    kSell        = 4,   // sell down the building's stock   (field 28  delta -)
    kUpgrade     = 5,   // bump the building's upgrade level (field 89 delta +)
};

// ===========================================================================
// Building field codes the opcode-26 delta targets (the `field` arg of
// QueueRequestArgs26). Recovered from building_stock.h (124=price, 28=stock) plus
// the upgrade-level field (the +89 packed dword, building.h::GetUpgradeLevel).
// ===========================================================================
enum BuildingFieldCode : i32 {
    kFieldPrice   = 124,  // building sale price scalar
    kFieldStock   = 28,   // building fill/stock level
    kFieldUpgrade = 89,   // building upgrade-level packed dword
};

// The order opcode every enqueued building action rides (QueueRequestArgs26).
inline constexpr u8 kBuildingCmdOpcode = 26;     // 0x1A
// Staging payload offsets inside the 153-byte packet (the three dwords v5/v6/v7
// QueueRequestArgs26 writes at +0x10): buildingId, field, value.
inline constexpr u32 kCmdBuildingIdOff = 0x10;   // +16
inline constexpr u32 kCmdFieldOff      = 0x14;   // +20
inline constexpr u32 kCmdValueOff      = 0x18;   // +24

// ===========================================================================
// The classified building command (the golden: kind + menu item -> command).
// ===========================================================================
struct BuildingCommand {
    bool issued   = false;   // a mutating command results from this (kind, item)
    u8   opcode   = 0;       // wire opcode (kBuildingCmdOpcode == 26 when issued)
    i32  field    = 0;       // building field code (price/stock/upgrade)
    i32  delta    = 0;       // the signed value delta applied to that field
    BuildingActionGroup group = BuildingActionGroup::kNone;
};

// 1:1 classifier: given a building KIND code and a chosen menu ITEM, resolve the
// concrete building Command. A non-mutating item / a kind whose dialog does not
// offer the item yields {issued=false}. This is the unit golden.
BuildingCommand ClassifyBuildingAction(int kind, BuildingMenuItem item);

// ===========================================================================
// Building dialog FSM — the EnterAndDispatch lifecycle reduced to its observable
// states. CheckEntry gate -> Enter (interior load, hooked) -> ContactMenu (the
// kind-selected loop, hooked) -> Action (a menu item chosen) -> Closed.
// ===========================================================================
enum class BuildingDialogState {
    kClosed      = 0,   // not open
    kEntryDenied = 1,   // CheckEntryAllowed gate failed
    kEntered     = 2,   // interior loaded, contact menu opening
    kContactMenu = 3,   // contact menu open, awaiting a menu choice
    kAction      = 4,   // a menu item was chosen (the dispatch point)
};

// ===========================================================================
// Frame-loop / scene-interior / overlay leaves of EnterAndDispatch — not in the
// translated slice; installable hooks with inert-by-default reconstructions.
// ===========================================================================
struct BuildingDialogHooks {
    // VIBE_Building_CheckEntryAllowed gate (sim::Building6 CheckEntryAllowed is
    // reconstructed but pulls live globals; the FSM lets a test gate entry directly).
    // Default: entry allowed.
    std::function<bool(i32 buildingId, int kind)> checkEntryAllowed;
    // VIBE_Scene_EnterBuildingInterior + the per-frame RunFrameLoop the contact
    // menu spins. Default: inert (returns immediately, no frames pumped).
    std::function<void(i32 buildingId, int kind)> enterInterior;
    // The opcode-26 APPLY: write the field delta into the live building record.
    // Default writes (field, delta) into the resolved building's BuildingRec.
    std::function<void(i32 buildingId, i32 field, i32 delta)> applyFieldDelta;
};
void SetBuildingDialogHooks(const BuildingDialogHooks* hooks);

class BuildingDialogFsm {
public:
    BuildingDialogFsm() = default;

    BuildingDialogState state() const { return state_; }
    int  kind() const { return kind_; }
    BuildingActionGroup group() const { return group_; }

    // EnterAndDispatch's head: CheckEntryAllowed, then (if allowed) load the interior
    // and resolve the kind's contact-menu action group. Returns true if the dialog
    // opened (state -> kContactMenu), false if entry was denied (kEntryDenied).
    bool Open(i32 buildingId, int kind);

    // Choose a contact-menu item: advances kContactMenu -> kAction and records the
    // classified command. Returns the classified BuildingCommand (issued=false if the
    // dialog is not open or the item is non-mutating for this kind).
    BuildingCommand ChooseMenuItem(BuildingMenuItem item);

    // Close the dialog (EnterAndDispatch's tail: selection reset, slot restore).
    void Close();

    i32 buildingId() const { return buildingId_; }

private:
    BuildingDialogState  state_      = BuildingDialogState::kClosed;
    i32                  buildingId_ = 0;
    int                  kind_       = 0;
    BuildingActionGroup  group_      = BuildingActionGroup::kNone;
};

// ===========================================================================
// The result of one building click -> dialog -> command -> apply cycle.
// ===========================================================================
struct BuildingClickResult {
    bool                opened     = false;  // the dialog opened (entry allowed)
    int                 pickIndex  = -1;      // scene_pick index (-1 == empty space)
    i32                 pickId     = 0;       // resolved entity id
    int                 resolveKind= 0;       // GameObjectResolveEntityById kind
    int                 buildingKind = 0;     // the building's KIND code (dialog switch)
    BuildingActionGroup group      = BuildingActionGroup::kNone;
    BuildingDialogState finalState = BuildingDialogState::kClosed;
    BuildingCommand     command{};            // the classified command
    bool                enqueued   = false;   // a packet hit the send ring
    i32                 ringSlot   = -1;       // EnqueuePacket ring slot
    bool                applied    = false;    // FlushSendQueue+ExecCommands ran
};

// ===========================================================================
// The opcode-26 building-command APPLY handler — installs on the queue so
// FlushSendQueue+ExecCommands route an applied building command into the active
// applyFieldDelta hook (default: mutate the live building record). Idempotent; call
// once per queue after Init(). IssueBuildingClick assumes it is installed.
// ===========================================================================
void InstallBuildingCommandHandler(sim::CommandQueue& q);

// ===========================================================================
// The bridge: resolve a world-view click into a building, open its dialog, drive a
// menu choice, build+enqueue the matching Command via the REAL CommandQueue, and
// apply it so the building record mutates.
//
// `cam`/`cursor`(sx,sy)/`objects` drive the REAL scene pick. `kindOf` supplies the
// picked building's KIND code (the scene-entity type word the dialog switch reads;
// the reconstructed ObjectRec does not store it as a named field, so the caller
// resolves it — tests pass the seeded kind, the e2e derives it from the record).
// `item` is the menu choice. `q` is the live CommandQueue (handler installed).
//
// After the call: if the dialog opened and the item is mutating, the opcode-26 packet
// has been built, enqueued through the real codec, and — in standalone mode — flushed
// + executed so the apply handler mutated the building.
BuildingClickResult IssueBuildingClick(sim::CommandQueue& q,
                                       const CityViewCamera& cam, float sx, float sy,
                                       const ScenePickObject* objects, int count,
                                       float pickRadius,
                                       const std::function<int(i32 id)>& kindOf,
                                       BuildingMenuItem item);

} // namespace guild::play
