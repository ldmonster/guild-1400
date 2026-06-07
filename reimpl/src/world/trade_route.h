#pragma once
// Trade-route management — the trade-transport panel FSM + route record rules
// (gilde.exe). This is the *logic core* extracted from the big trade-route panel
// state machine; the deeply GUI-coupled rendering sub-states (window/slot/drag
// layout, sprite/animation, text rendering) are DEFERRED and listed in the
// module report. What is recovered here byte-for-byte:
//
//   * The trade-route / cart "handler" record layout (the per-cart object the
//     dispatcher reads at object+82..+90, +28*4, +40, +101, +1) and the
//     route-assignment time/cost rule.
//   * VIBE_TradeTransport_AssignRoute       0x53f404 — assign caravan route
//     (snapshot arrival time = GameTime + 1 day; charge the cart cost; emit the
//     per-route command).
//   * VIBE_TradeTransport_ResetCartHandler  0x53f5c0 — clear a cart's pending
//     route (find the cart handler by id, stamp the current time, emit reset cmd).
//   * VIBE_TradeTransport_FindOwnerChain    0x53f610 — resolve whether a target
//     building is reachable on the owner chain (the route-target predicate).
//   * VIBE_TradeTransport_PanelDispatcher   0x54014c — the panel FSM, reduced to
//     its STATE-MACHINE skeleton: the open-mode (1/2/4 + flag-8 route-edit) entry
//     branch and the per-frame button dispatch (add-goods / assign-cart /
//     buy-cart / sell-carts / confirm). The widget-layout body is deferred.
//
// All mutations (money transfer, per-good buy/sell, cart-cost charge) go through
// a settable command hook so the rules are unit-testable without the lockstep
// network queue or the GUI. The originals commit via VIBE_Command_QueueRequest16
// /_17/_20/_SlotReset28/_Entity29.
#include <cstddef>
#include <vector>

#include "guild/common/types.h"

namespace guild::world {

// ===========================================================================
// Trade-cart "handler" record  (gilde.exe — the object the dispatcher and
// AssignRoute read; it is an entry of the He (handler/entity) table queried by
// VIBE_He_FindFirstHandlerByFilter(2,3,goodId,0,17) / (1,0,15)). Only the
// route-relevant fields are recovered; offsets are byte offsets into the record
// as the originals address it (e.g. `*(_QWORD*)(i+82)`, `*((_DWORD*)i+28)`).
// ===========================================================================
GUILD_PACKED_BEGIN
struct TradeCartRecord {
    u8  pad0[40];          // +0x00..+0x27  (other cart state, not route-relevant)
    u8  transportMode;     // +0x28 (+40)   transport mode byte (None/Slow/Med/Fast)
    u8  pad41[41];         // +0x29..+0x51  reserved
    // The dispatcher writes the arrival-time record here from the global game
    // clock snapshot (qword_13CE852 / unk_13CE85A / unk_13CE85E):
    u64 arriveTimeLo;      // +0x52 (+82)   *(_QWORD*)(i+82) = clock day/hour/min
    u32 arriveTimeMid;     // +0x5A (+90)   *(_DWORD*)(i+90)
    u16 arriveTimeHi;      // +0x5E (+94)   *((_WORD*)i+47)  (ends at +96)
    // *((_DWORD*)i+28) == byte +112: route slot marker set to -1 on (re)assign.
    u8  pad60[16];         // +0x60..+0x6F  reserved up to +112
    i32 routeSlot;         // +0x70 (+112)  *((_DWORD*)i+28) (-1 == cleared)
} GUILD_PACKED;
GUILD_PACKED_END
static_assert(offsetof(TradeCartRecord, transportMode) == 40, "mode @+40");
static_assert(offsetof(TradeCartRecord, arriveTimeLo) == 82, "arrive @+82");
static_assert(offsetof(TradeCartRecord, routeSlot) == 112, "routeSlot @+112");

// Transport mode (mirrors world/tradetransport.h TransportMode; redeclared here
// so this module is independent of that file). Mode 0 = no transport selected.
enum class RouteMode { None = 0, Slow = 1, Medium = 2, Fast = 3 };

// Cart-cost per-good rates by mode (gilde.exe flt_626A7C/78/74; clamp 32k..256k;
// fee = trunc(clamped * rate); cost = trunc(fee + 0.5)). Recovered byte-exact.
constexpr float  kRouteRateSlow   = 0.05f;   // flt_626A7C (0x3D4CCCCD)
constexpr float  kRouteRateMedium = 0.1f;    // flt_626A78 (0x3DCCCCCD)
constexpr float  kRouteRateFast   = 0.15f;   // flt_626A74 (0x3E19999A)
constexpr double kRouteCostBase   = 0.5;     // dbl_626A84 (0x3FE0000000000000)
constexpr i32    kRouteValueFloor = 32000;
constexpr i32    kRouteValueCeil  = 256000;
constexpr int    kRouteTravelDays = 1;       // GameTime_Advance(.., 0, 1, 0)

// gilde.exe 0x592220 — VIBE_TradeTransport_ComputeCartCost (re-derived here so
// AssignRoute is self-contained; identical formula to world/tradetransport.h).
i32 RouteComputeCartCost(i32 goodValue, RouteMode mode);

// ===========================================================================
// Command hook (lockstep). The originals emit money/route commands through
// VIBE_Command_QueueRequest16 (transfer), _17 (per-good move), _20 (per-good
// give), _SlotReset28 (route packet), _Entity29 (reset packet). We collapse them
// to one recorded "command" so a test can assert the exact sequence emitted.
// ===========================================================================
enum class RouteCmd {
    RouteAssign,   // QueueRequestSlotReset28 (the route packet, +1 day arrival)
    ChargeCost,    // QueueRequest16(-1, ownerAccount, cost, currency)
    ResetCart,     // QueueRequestEntity29(-1, cart)
    GoodBuy,       // QueueRequest17(-1, account, qty, goodId, ...) (player buys)
    GoodGive,      // QueueRequest20(account, goodId)              (player gives)
};
struct RouteCommand {
    RouteCmd kind;
    i32 payer = 0;
    i32 recipient = 0;
    i32 amount = 0;   // cost / quantity
    i32 goodId = 0;
    int currency = 0;
};
using RouteCmdHook = void (*)(const RouteCommand& cmd, void* ctx);
void RouteSetCmdHook(RouteCmdHook hook, void* ctx);
void RouteEmit(const RouteCommand& cmd);

// ===========================================================================
// gilde.exe 0x53f404 — VIBE_TradeTransport_AssignRoute.
//   (1) For every cart handler matching the route good, stamp arrival-time =
//       current clock and clear its route slot (-1).
//   (2) Snapshot clock, advance +1 day -> arrival time; emit the route packet
//       (RouteAssign) for the cart.
//   (3) If the cart has a transport mode (+40 != 0): collect its cargo value,
//       cost = ComputeCartCost(value, mode), and charge it (ChargeCost from the
//       treasury sink -1 to the owner account).
// Returns the charged cost (0 when no transport mode). `cargoValue` is the value
// VIBE_Inventory_CollectProductionSlots would have produced from the cart.
struct RouteAssignResult {
    i32  cost = 0;
    int  travelDays = kRouteTravelDays;
    bool charged = false;
};
RouteAssignResult RouteAssign(i32 cargoValue, RouteMode mode, i32 ownerAccount,
                              i32 cart, bool commit);

// gilde.exe 0x53f5c0 — VIBE_TradeTransport_ResetCartHandler.
//   Find the cart handler whose id (+43*4 == +172) equals the target cart id,
//   stamp arrival-time = current clock, emit the reset packet (ResetCart). On
//   success returns true. (We model the "find by id" as a direct match.)
bool RouteResetCart(i32 cart);

// ===========================================================================
// The panel FSM (gilde.exe 0x54014c — VIBE_TradeTransport_PanelDispatcher).
//
// Open mode: the three thunks pass dl = 1 / 2 / 4 into HIBYTE(v369). Bit-8 of
// the same byte (a2 & 8) selects the route-EDIT variant (uses the active route
// context dword_63174C). The dispatcher then queries the panel's source object
// type by mode (market id 255 / contor 475 / contor 476) to seed the slots.
// ===========================================================================
enum class PanelMode {
    Market = 1,   // HIBYTE bit0 (v369 & 0x1000000): local market (own production)
    Import = 2,   // contor import  (HIBYTE == 2)  -> contor object id 475
    Export = 4,   // contor export  (HIBYTE == 4)  -> contor object id 476
};
struct PanelOpen {
    PanelMode mode = PanelMode::Market;
    bool routeEdit = false;   // (a2 & 8): the route-edit variant
};
// gilde.exe 0x54011c/2c/3c — the three open thunks decode (a1, dl) into PanelOpen.
PanelOpen RouteDecodeOpen(u8 modeByte);

// The per-frame button dispatch. The dispatcher's frame loop reads the clicked
// widget id (dword_75BF38 / dword_62D22C). These are the route-management
// transitions recovered from the loop body (LABEL_244..LABEL_251); the rest of
// the loop (slot layout / drag / rendering) is deferred.
enum class PanelButton {
    None = 0,
    AddGoods,     // dword_62D22C == v320: "buy transport cart" / add-goods slot
    AssignCart,   // dword_62D22C == v318: assign loaded carts to a route target
    SellCarts,    // dword_62D22C == v364: confirm-sell the selected carts
    Courier,      // dword_62D22C == v319: courier (compute fee, enqueue)
    Confirm,      // dword_75BF38 == 1228: confirm the goods buy/sell (per-good)
    Cancel,       // dword_75BF38 == 1155 / dword_672230: close the panel
};

// One state the FSM can be in / one observable outcome of a transition.
enum class PanelState {
    Open,         // panel open, idle (frame loop running)
    GoodsAdded,   // a goods slot was added/confirmed (per-good commands emitted)
    CartAssigned, // a route target was chosen and carts assigned (AssignRoute ran)
    CartsSold,    // sell-carts confirmed
    Closed,       // panel closed (Cancel)
};

// Drives one FSM step. `routeTargetValid` mirrors the dispatcher's
// VIBE_TradeTransport_FindOwnerChain check that gates AssignCart (the route
// target must be reachable / non-self). Returns the resulting state. On
// AssignCart with a valid target, the supplied cargo lines are each routed via
// RouteAssign; on Confirm, the supplied per-good lines are emitted as Good
// commands. Pure transition logic — no GUI.
struct GoodLine {
    i32 goodId = -1;     // -1 == empty slot (skipped)
    i32 quantity = 0;    // >0 player buys (GoodBuy); <0 player gives (GoodGive)
    i32 account = 0;     // counterparty account for the move
};
struct PanelStep {
    PanelButton button = PanelButton::None;
    bool routeTargetValid = false;   // FindOwnerChain result for AssignCart
    i32  ownerAccount = 0;           // dword_12CE914[..] cart owner account
    RouteMode mode = RouteMode::None;
    int  currency = 0;
    std::vector<GoodLine> goods;     // per-good lines (Confirm)
    std::vector<i32> cargoValues;    // per-cart cargo values (AssignCart)
    i32  cart = 0;                   // the cart being acted on
};
PanelState RoutePanelStep(PanelState cur, const PanelStep& step);

// gilde.exe 0x53f610 — VIBE_TradeTransport_FindOwnerChain (route-target
// predicate). Walks the owner chain of the candidate target building: returns
// true if the target is within tolerance on the chain OR a chain ancestor's id
// equals the source building id. Modeled with explicit id links so it is
// testable: `chain` is the candidate's owner-id chain (root last); a target is
// reachable when `sourceId` appears in the chain or `targetWithinTolerance`.
bool RouteFindOwnerChain(i32 sourceId, const std::vector<i32>& chain,
                         bool targetWithinTolerance);

} // namespace guild::world
