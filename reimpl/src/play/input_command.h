#pragma once
// Wave 25 PLAY P1/P5 — the real INPUT -> COMMAND -> sim-apply bridge: the
// interactivity that turns the render+turn shell into a *game* (namespace
// guild::play).
//
// input_router.{h,cpp} already latches the platform mouse/keys and drives the GUI
// click core (windows / widgets / HUD). That covers UI chrome. This module covers
// the OTHER half of a click: a click in the WORLD VIEW that issues a unit ORDER.
//
// GROUNDING (decompiled this wave — the live click->order path):
//   * VIBE_Command_DispatchSelectedUnits @0x4891c8 is the per-frame order-issue
//     pump. When the cursor-order ARMED latch (dword_67221C) is set, it walks the
//     32-slot selected-unit roster (dword_11BB6A0[]) and, for each commandable
//     unit, calls VIBE_Command_IssueOnObject(handle, unit).
//   * VIBE_Command_IssueOnObject @0x488ff0 (reconstructed in command_apply12.cpp)
//     is the classifier+builder. It reads the picked object/label (dword_631720)
//     and the cursor's selected order-mode and dispatches:
//        picked is a battle UNIT (*(picked+535)==2) + attack-allowed -> ATTACK (kind 2)
//        label == "sp_ESCAPE"        -> MOVE          (kind 3, BuildMoveToPacket)
//        label prefix "sp_CONQUER"   -> LABELLED MOVE (kind 4, direct slot, NO enqueue)
//        label prefix "WARE"         -> CONQUER       (kind 6, BuildConquerCommand)
//        no object under cursor       -> GROUND MOVE   (kind 1, cursor raycast)
//     EVERY enqueued order rides one wire opcode — RequestBuildOp80's opcode 80;
//     the classification above is the order-KIND byte at staging +4 (kSlotKind).
//
// This module mirrors that classifier EXACTLY (ClassifyOrder), resolves the picked
// object/tile from a world-view click via play::scene_pick (PickAndResolveSceneEntity
// -> the REAL sim::GameObjectResolveEntityById), drives the REAL builders through
// sim::IssueOnObject (which threads the REAL combat_packets builders + the REAL
// CommandQueue codec), and then APPLIES the queued packet so the live sim mutates:
//   IssueOnObject -> EnqueuePacket -> FlushSendQueue (standalone: apply locally)
//                 -> ExecCommands  -> opcode-80 handler -> target order slot written.
//
// The opcode-80 apply handler is not reconstructed in the binary's translated slice,
// so it is supplied as an installable hook with an INERT-by-default reconstruction
// (defined in input_command.cpp, the CutsceneMiscHooks / ObjLifeHooks pattern):
// the default writes the order-kind + destination tile into the resolved target's
// live entity record, the observable mutation a real order apply produces. Tests
// install their own apply hook or use the default.
//
// Additive: no edits to input_router.cpp / wiring.cpp / command_apply12.cpp.
#include "guild/common/types.h"
#include "play/scene_pick.h"
#include "sim/command.h"
#include "sim/combat_packets.h"     // OrderStage / CombatOrderContext / CombatOrderHandle
#include "sim/command_apply12.h"    // IssueOnObject / IssueOnObjectHooks

#include <functional>

namespace guild::play {

// ===========================================================================
// Cursor order-mode — the player's currently-selected order tool (the in-game
// cursor-mode latch the toolbar sets, which IssueOnObject reads as the selected
// order-label dword_631720 / armed latch dword_67221C). Each maps to one of the
// classifier branches.
// ===========================================================================
enum class CursorMode {
    kDisarmed   = 0,   // dword_67221C == 0: no order issued (router returns 0)
    kAttackMove = 1,   // armed, default: attack a unit / ground-move on empty space
    kMove       = 2,   // "sp_ESCAPE" selected -> plain MOVE order (kind 3)
    kLabeled    = 3,   // "sp_CONQUER" label   -> labelled move    (kind 4)
    kConquer    = 4,   // "WARE" label         -> conquer/ware     (kind 6)
};

// The order-KIND byte the classifier resolves (staging +4 / kSlotKind). This is
// the "opcode" the golden test asserts; the WIRE opcode of every *enqueued* order
// is always 80 (RequestBuildOp80), with this kind inside the 44-byte staging block.
enum OrderKind : u8 {
    kKindNone     = 0,   // nothing issued (disarmed / not allowed)
    kKindGround   = 1,   // cursor-raycast ground move (no object picked)
    kKindAttack   = 2,   // attack a battle unit
    kKindMove     = 3,   // sp_ESCAPE plain move
    kKindLabeled  = 4,   // sp_CONQUER labelled move (NOT enqueued; direct slot)
    kKindConquer  = 6,   // WARE conquer order
};

// The picked object's CLASS, as the classifier distinguishes it. Mirrors the two
// readings IssueOnObject makes of dword_631720: a battle unit (*(picked+535)==2)
// vs. an order-label string vs. nothing under the cursor.
enum class PickedClass {
    kNone   = 0,   // no object under the cursor (the ground-raycast branch)
    kUnit   = 1,   // a battle unit (the attack branch)
    kObject = 2,   // a non-unit object/building (treated as an order-label string)
};

// ===========================================================================
// Classification (the golden: object class + cursor mode -> order kind).
// ===========================================================================
//
// 1:1 with IssueOnObject's branch ladder. `attackAllowed` models the attack-gate
// (different team OR friendly-fire override); only consulted for a UNIT pick. The
// returned kind is the order-KIND byte; kKindNone means "no order issued".
OrderKind ClassifyOrder(PickedClass picked, CursorMode mode, bool attackAllowed);

// ===========================================================================
// The resolved intent of one world-view click.
// ===========================================================================
struct WorldOrder {
    bool        issued     = false;     // an order was issued (IssueOnObject != 0)
    OrderKind   kind       = kKindNone; // the classified order kind (staging +4)
    int         pickIndex  = -1;        // scene_pick index (-1 == empty space)
    i32         pickId     = 0;         // resolved entity id (0 == none)
    int         resolveKind= 0;         // GameObjectResolveEntityById kind (1/2/3/0)
    i32         ringSlot    = -1;       // EnqueuePacket ring slot (enqueued orders)
    bool        enqueued    = false;    // a packet hit the send ring (kinds 1/2/3/6)
    sim::OrderStage slot{};             // the order staging the builder produced
};

// ===========================================================================
// Opcode-80 ORDER APPLY hook — the mutation a queued order produces when applied.
// Not reconstructed in the translated slice; supplied as an installable hook with
// an inert-by-default reconstruction defined in input_command.cpp.
// ===========================================================================
struct InputCommandApplyHooks {
    // Apply one decoded opcode-80 order to the live world. `targetId` is the order
    // target's entity id (staging +0 minus 4 — the original stores *(target+4)),
    // `kind` the order-kind byte (staging +4), `destX`/`destZ` the destination tile
    // (staging +0x20/+0x24 for a slot order, or the conquer single-byte tile). The
    // default writes (kind, destX, destZ) into the resolved target's live entity
    // record (ObjectRec/Person), the observable "the unit took the order" mutation.
    std::function<void(i32 targetId, u8 kind, i32 destX, i32 destZ)> applyOrder;
};

// Install the active apply hook (nullptr restores the inert default). Mirrors
// SetIssueOnObjectHooks / SetCommandApply8Hooks.
void SetInputCommandApplyHooks(const InputCommandApplyHooks* hooks);

// Offsets into the target entity record the DEFAULT apply hook writes (chosen in
// the records' TODO pad regions; documented so tests read them back). The order
// "lands" on the unit exactly like the binary's order-slot write would be observable.
enum AppliedOrderOffset : u32 {
    kAppliedKindOff  = 0x60,  // order-kind byte
    kAppliedDestXOff = 0x64,  // destination tile X (dword)
    kAppliedDestZOff = 0x68,  // destination tile Z (dword)
};

// ===========================================================================
// The bridge: resolve a world-view click into a unit order, build+enqueue it via
// the REAL builders, and apply it so the sim mutates.
// ===========================================================================
//
// `cam` + `cursor` (sx,sy) + the live `objects` roster drive scene_pick. `mode`
// is the player's selected order tool. `q` is the live CommandQueue. `h`/`ctx`
// are forwarded to IssueOnObject's builders. `units`/`unitCount` is the selected-
// unit roster DispatchSelectedUnits would iterate (we issue the order for each
// selected unit, mirroring the per-unit loop). `attackAllowed` gates the unit
// attack branch.
//
// Returns the resolved order. After the call: the matching packet (if any) has
// been built via the real builders, enqueued through the real CommandQueue, and —
// in standalone mode — flushed + executed so the apply hook has mutated the world.
WorldOrder IssueWorldClick(sim::CommandQueue& q,
                           const CityViewCamera& cam, float sx, float sy,
                           const ScenePickObject* objects, int count,
                           float pickRadius, CursorMode mode,
                           bool attackAllowed,
                           const sim::CombatOrderHandle& h,
                           const sim::CombatOrderContext& ctx);

// Install the opcode-80 apply handler on `q` (so FlushSendQueue+ExecCommands route
// applied orders into SetInputCommandApplyHooks's apply hook). Call once per queue
// after Init(); IssueWorldClick assumes it is installed. Idempotent.
void InstallOrderApplyHandler(sim::CommandQueue& q);

} // namespace guild::play
