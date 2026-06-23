#pragma once
// charaction_steps7 — batch 7 of the CharAction step / state-machine LEAVES of the
// Guild simulation (gilde.exe, VIBE_CharAction_* family). This batch is the
// "transport" (he_Transport) cluster: the trade-cart escort coroutine the master
// NPC runs to move goods between two buildings/cities. The earlier batches
// (charaction_steps2..6) translated the surrounding step machines and the small
// transport-speed scaler leaf (ApplyTransportSpeed @0x4de4b0, charaction_steps5);
// this batch fills in the remaining untranslated transport functions:
//
//   * AllocTransport (0x4ddfcc) — the one-shot allocator: snapshot the saved clock
//     into the appointment (+82) and scratch (+96) slots, resolve the two endpoint
//     buildings (the start +176 and goal +180), classify the route into one of four
//     variants (+200: 1 door / 2 local / 3 city-city / 4 mixed) by the two
//     IsProductionType results, advance the appointment one day, resolve the cart
//     object (+172), set the cart speed, flag it, roll the escort type (+204) from
//     the production volume, and arm the entity packet (+132).
//   * RunTransport (0x4df7d8) — the coroutine driver: a phase machine on +112
//     (states -2,-1,0,1,2) that, in the working state 1, dispatches to one of the
//     four per-variant sub-steps (RunTransportLocal/Door/StadtStadt/Entry), counts
//     the lap (+212), re-stamps the clock and re-arms a +5-minute wake, and at lap 5
//     rolls the highwayman-ambush check (RandomModulo) and the arrival check.
//   * RunTransportLocal (0x4de534) — variant 2: same-building / market move.
//   * RunTransportDoor (0x4de884) — variant 1: deliver to a building door.
//   * RunTransportEntry (0x4df258) — variant 4: enter the destination building.
//   * RunTransportStadtStadt (0x4deca4) — variant 3: cross-city move (PickRandom-
//     Transporter waypoint, coordinate resolve, tolerance gate).
//   * PickRandomTransporter (0x4de3fc) — gather up to 32 "dummy_TRANSPORTER" scene
//     nodes via a scene-graph walk and return a uniformly-chosen one.
//   * CollectTransporterCb (0x4de3b0) — the scene-walk callback that appends a
//     matching node to the collection buffer (cap 32).
//
// The state-machine control flow, the phase ids, the lap counter, the RNG draws,
// the GameTime arithmetic and the exact He-record field offsets are translated 1:1.
// The cross-cluster leaf side effects (person/object/building resolves, the cmd
// queue emits, the formatted-message renders + quickjump/entity sends, the
// scene-graph walk, the universe slot switch, the framerate-tier counters and the
// recipient/category tables) are routed through the CharActionStep7Hooks bridge
// below plus the shared NpcLeafHooks (npcaction.h). Installing a null hook table
// restores the inert default (every resolve reports absent, every emit/render is a
// no-op, every query/counter returns 0). NpcClock()/GameTimeAdvance/GameTimeCompare
// and ApplyTransportSpeed (charaction_steps5) are reused.
//
// Addresses are absolute, imagebase 0x400000.
#include "guild/common/types.h"
#include "sim/he.h"

namespace guild::sim {

// ===========================================================================
// Transport He-record fields (byte-faithful offsets beyond the shared he.h map).
//   +0xAC (+172)  : cart object id (dword)               — He_Counter172 reuse.
//   +0xB0 (+176)  : start building/person id (dword).
//   +0xB4 (+180)  : goal  building/person id (dword).
//   +0xC4 (+196)  : current-leg origin building id (dword).
//   +0xC8 (+200)  : route variant (1 door / 2 local / 3 city-city / 4 mixed).
//   +0xCC (+204)  : escort type (0..2) rolled from the production volume.
//   +0xD0 (+208)  : "started" byte (0 -> first leg arms the move; 1 -> running).
//   +0xD4 (+212)  : lap counter (incremented each working tick).
//   +0xD8 (+216)  : target world X (float) ; +0xDC (+220) Y ; +0xE0 (+224) Z.
//   +0xE8 (+232)  : transport-window handle (dword, -1 == none).
//   +0xEC (+236)  : in-flight move packet handle (dword, -1 == none).
//   +0xC0 (+192)  : base walk speed (float) — also written by the escort path.
//   +0xF1 (+241)  : "ambushed/delivered" byte (0 none / 1 ambushed / 2 robbed).
// ===========================================================================
inline i32&   Cas7_CartId(HeRecord* h)   { return *reinterpret_cast<i32*>(HeBytes(h) + 172); }
inline i32&   Cas7_StartId(HeRecord* h)  { return *reinterpret_cast<i32*>(HeBytes(h) + 176); }
inline i32&   Cas7_GoalId(HeRecord* h)   { return *reinterpret_cast<i32*>(HeBytes(h) + 180); }
inline i32&   Cas7_OriginId(HeRecord* h) { return *reinterpret_cast<i32*>(HeBytes(h) + 196); }
inline i32&   Cas7_Variant(HeRecord* h)  { return *reinterpret_cast<i32*>(HeBytes(h) + 200); }
inline i32&   Cas7_Escort(HeRecord* h)   { return *reinterpret_cast<i32*>(HeBytes(h) + 204); }
inline u8&    Cas7_Started(HeRecord* h)  { return *reinterpret_cast<u8*>(HeBytes(h) + 208); }
inline i32&   Cas7_Lap(HeRecord* h)      { return *reinterpret_cast<i32*>(HeBytes(h) + 212); }
inline float& Cas7_TgtX(HeRecord* h)     { return *reinterpret_cast<float*>(HeBytes(h) + 216); }
inline float& Cas7_TgtY(HeRecord* h)     { return *reinterpret_cast<float*>(HeBytes(h) + 220); }
inline float& Cas7_TgtZ(HeRecord* h)     { return *reinterpret_cast<float*>(HeBytes(h) + 224); }
inline i32&   Cas7_Window(HeRecord* h)   { return *reinterpret_cast<i32*>(HeBytes(h) + 232); }
inline i32&   Cas7_MovePkt(HeRecord* h)  { return *reinterpret_cast<i32*>(HeBytes(h) + 236); }
inline float& Cas7_BaseSpeed(HeRecord* h){ return *reinterpret_cast<float*>(HeBytes(h) + 192); }
inline u8&    Cas7_Delivered(HeRecord* h){ return *reinterpret_cast<u8*>(HeBytes(h) + 241); }

// The two scratch GameTime mirrors AllocTransport stamps the clock into:
//   +0x52 (+82) appointment  (He_ApptTime) ; +0x60 (+96) scratch (He_Scratch).
// (he.h already exposes both.)

// ===========================================================================
// Recovered .rdata float constants (gilde.exe) used by the escort-type roll and
// the cart-speed scaler. The values flow into opaque speed/cost fields and are not
// asserted by the golden control-flow tests; they are named + addressed for record.
//   flt_61F244 / dbl_61F248 / dbl_61F250 — cart base-speed terms (AllocTransport).
//   dbl_61F258 / dbl_61F260              — object-class speed bonuses (309 / 310).
//   dbl_61F268                           — workstation-count speed multiplier.
//   flt_61F3A8 / flt_61F3AC / flt_61F3B0 — ambush-probability framerate terms.
//   dbl_61F3B8                           — ambush-probability scale.
// ===========================================================================
// Byte-verified via get_bytes (gilde.exe .rdata):
//   flt_61F244 = 0x3c23d70a = 0.01f ; dbl_61F248 = 0x3fe0..   = 0.5
//   dbl_61F250 = 0x3fecccc.. = 0.9  ; dbl_61F258 = 0x3fd3333. = 0.3
//   dbl_61F260 = 0xbfd9999. = -0.4  ; dbl_61F268 = 0x3f847ae. = 0.01
//   flt_61F3A8 = 0x42c80000 = 100.0f; flt_61F3AC = 0xc1200000 = -10.0f
//   flt_61F3B0 = 0xc1700000 = -15.0f; dbl_61F3B8 = 0x3f847ae. = 0.01
// float-origin constants are stored as float so promotion to double in the
// mixed expressions matches the binary's load of a 4-byte .rdata float bit-for-bit.
constexpr float  kCartSpeedScale  = 0.01f; // flt_61F244 (float)
constexpr double kCartSpeedFactor = 0.5;   // dbl_61F248
constexpr double kCartSpeedBase   = 0.9;   // dbl_61F250
constexpr double kCartBonus309    = 0.3;   // dbl_61F258  (cls==309 path)
constexpr double kCartBonus310    = -0.4;  // dbl_61F260  (cls==310 path)
constexpr double kWorkstationMul  = 0.01;  // dbl_61F268
constexpr float  kAmbushBase      = 100.0f;// flt_61F3A8 (float)
constexpr float  kAmbushMed       = -10.0f;// flt_61F3AC (float)
constexpr float  kAmbushFast      = -15.0f;// flt_61F3B0 (float)
constexpr double kAmbushScale     = 0.01;  // dbl_61F3B8

// ===========================================================================
// CharActionStep7 cross-cluster leaves. A null member installs an inert default.
// The shared NpcLeafHooks (npcaction.h) still supplies queueRequestEntity29 /
// freeHandlerEntry / packetStatus; this struct adds the resolves, the cmd queue
// emits, the render/send bridge, the scene-graph walk, the universe slot switch and
// the framerate-tier counters. Return-value semantics match the original leaves.
// ===========================================================================
struct CharActionStep7Hooks {
    // --- resolves / queries ---------------------------------------------
    // VIBE_Person_QueryBegin(self, a, b, key) — resolve a person/building record by
    // a query key; null == absent.
    HeRecord* (*personQueryBegin)(i32 self, int a, int b, i32 key);
    // VIBE_Object_FindObjectById(id) — resolve a scene/cart object record; null ==
    // absent.
    HeRecord* (*findObjectById)(i32 id);
    // VIBE_GameObject_QueryFind(scene, a, b, c, key) — find a world object; null ==
    // none.
    HeRecord* (*objectQueryFind)(i32 scene, int a, int b, int c, i32 key);
    // VIBE_GameObject_ResolveEntityById(scene, &out, id, flag) — resolve an entity
    // pointer into *out; returns the entity (or null).
    HeRecord* (*resolveEntityById)(int scene, HeRecord** out, i32 id, int flag);
    // VIBE_Building_FindById(id) — building record (or null).
    HeRecord* (*buildingFindById)(i32 id);
    // VIBE_Building_IsProductionType(rec) — nonzero if the building produces goods.
    int (*isProductionType)(HeRecord* rec);
    // VIBE_Building_IsStorageType(rec) — nonzero for a storage/Lager building.
    int (*isStorageType)(HeRecord* rec);
    // VIBE_Building_SumWorkstationByCategory(rec, cat, flag) — count matching slots.
    int (*sumWorkstation)(HeRecord* rec, int cat, int flag);
    // VIBE_Building_FindWorkProductObject(rec) — the building's product object (or
    // null).
    HeRecord* (*findWorkProduct)(HeRecord* rec);
    // VIBE_Inventory_CollectProductionSlots(obj, &outBuf) — sum production volume
    // into outBuf[+176>>2] (we expose the summed volume directly as the return).
    int (*collectProductionVolume)(HeRecord* obj);

    // --- scene-graph walk (PickRandomTransporter) ------------------------
    // Invoke `cb(node, ctx)` for each scene node under `root`; the callback returns
    // false to stop. Mirrors VIBE_SceneGraph_WalkAndInvoke(off_649D64, root, cb,
    // cap, ctx). The inert default visits nothing.
    void (*walkScene)(HeRecord* root, bool (*cb)(HeRecord* node, void* ctx),
                      void* ctx);
    // strcmp-equal of a scene node's name against the literal (loc_5CB930 thunk in
    // CollectTransporterCb). Nonzero == match.
    int (*nodeNameEquals)(HeRecord* node, const char* name);

    // --- emits / effects -------------------------------------------------
    // The cmd-queue emits. All opaque; return the handle where the original stores
    // one (cmd19 -> +236), else 0.
    i32 (*cmdRequest19)(i32 a, i32 b, i32 c, i32 d);       // QueueRequest19
    void (*cmdRequestPair51)(i32 a, int b);                 // QueueRequestPair51
    void (*cmdRequestArgs25)(i32 a, int b, int c, int d, int e);
    void (*cmdRequestSingle49)(i32 a);
    void (*cmdRequestNamedObject53)(i32 a, i32 b, int c, int d, int e, const char* n);
    void (*cmdRequestQuad46)(i32 a, int b, int c, int d);
    void (*cmdRequestChrMove)(i32 a, i32 b, int c, int d);  // RequestChrMoveToUniverse
    void (*cmdRequestBuildOp74)(i32 a);
    void (*requestChangeZustand)(i32 a, int delta);         // Object_RequestChangeZustand
    // VIBE_He_SendQuickjumpMessage / SendEntityMessage — delivered narrative pushes.
    void (*sendQuickjump)(i32 recipient, i32 from, int textId);
    void (*sendEntityMessage)(i32 recipient, int textId);
    // VIBE_Universe_SwitchActiveSlot(slot, on, ...) — opaque active-slot toggle.
    i32 (*universeSwitchSlot)(i32 slot, int on);
    // VIBE_Object_IsNearDoorAlt(obj, person) — nonzero if the cart reached the door.
    int (*isNearDoor)(HeRecord* obj, HeRecord* person);
    // VIBE_Math_VectorWithinTolerance(a, b, tol) — nonzero if |a-b| <= tol.
    int (*withinTolerance)(const float* a, const float* b, float tol);

    // --- tables / counters ----------------------------------------------
    // dword_12CE914[134*cityIndex] — the city's recipient entity id.
    i32 (*cityRecipientId)(u16 cityIndex);
    // byte_12CE912[536*cityIndex] — the city category byte (6/7 == player-relevant).
    u8 (*cityCategory)(u16 cityIndex);
    // VIBE_Math_RandomModulo(n) — uniform draw in [0,n). The inert default returns 0
    // (no ambush, first transporter chosen).
    int (*randomModulo)(int n);
    // *dword_11BC1CC ( > 100 -> ambush fast term ) / *dword_11BC1C8 ( > 200 -> med ).
    int (*fastFrameCounter)();
    int (*medFrameCounter)();
    // dword_6498E4 — the "current scene city index" word RunTransport stamps into
    // the target-city ref in state -2.
    u16 (*currentSceneCity)();
};
void SetCharActionStep7Hooks(const CharActionStep7Hooks* hooks);
const CharActionStep7Hooks& GetCharActionStep7Hooks();

// ===========================================================================
// Translated leaf / step functions.
// ===========================================================================

// gilde.exe 0x4de3b0 — VIBE_CharAction_CollectTransporterCb(node@eax, ctx@edx).
//   Scene-walk callback: if the node's name equals "dummy_TRANSPORTER", append the
//   node pointer to ctx's collection array (ctx[+0]..; count at ctx[+128]). Returns
//   true while the count is below 32 (keep walking).
struct TransporterCollector {
    HeRecord* nodes[32];   // ctx[+0 .. +124]
    i32       count;       // ctx[+128]
};
bool CollectTransporterCb(HeRecord* node, TransporterCollector* ctx);

// gilde.exe 0x4de3fc — VIBE_CharAction_PickRandomTransporter(root@eax, ctxKey@ecx).
//   Walk the scene under `root`, collecting up to 32 "dummy_TRANSPORTER" nodes, then
//   return a uniformly-random one (or null when none were found). (The original
//   temporarily clears root[+496] when root[+528]&1 is clear; we mirror the gate.)
HeRecord* PickRandomTransporter(HeRecord* root);

// gilde.exe 0x4ddfcc — VIBE_CharAction_AllocTransport(h@eax).
//   One-shot transport allocator. Returns the resolved cart entity on success, or
//   the free-handler result when an endpoint resolve fails / the endpoints coincide.
HeRecord* AllocTransport(HeRecord* h);

// gilde.exe 0x4de534 — VIBE_CharAction_RunTransportLocal(h@eax). Variant 2.
i32 RunTransportLocal(HeRecord* h);

// gilde.exe 0x4de884 — VIBE_CharAction_RunTransportDoor(h@eax). Variant 1.
i32 RunTransportDoor(HeRecord* h);

// gilde.exe 0x4df258 — VIBE_CharAction_RunTransportEntry(h@eax). Variant 4.
i32 RunTransportEntry(HeRecord* h);

// gilde.exe 0x4deca4 — VIBE_CharAction_RunTransportStadtStadt(h@eax). Variant 3.
i32 RunTransportStadtStadt(HeRecord* h);

// gilde.exe 0x4df7d8 — VIBE_CharAction_RunTransport(h@eax).
//   The transport coroutine driver. Phase machine on the state dword (+112):
//     -2 : arrival — stamp the target-city ref, and if the leg origin (+196) is the
//          start/goal/cart endpoint, re-arm the cmd29 packet; return.
//     -1 : finish — resolve the cart object, maybe drop it (RandomModulo>50), clear
//          the transport window (+232), clear the cart's owner + flag, then free.
//      0 : begin — advance the appointment +1 second, arm a state-1 cmd29; return.
//      1 : work  — gate on the appointment time and the move packet; dispatch by
//          variant (+200) to the matching sub-step; bump the lap (+212); re-stamp
//          the clock into +96/+82 and re-arm a +5-minute wake; at lap 5 roll the
//          highwayman ambush (RandomModulo(30) vs object class) and, once past lap 5
//          for a non-ambushed cart, the arrival probability (RandomModulo(105)).
//      2 : deliver — gate on the move packet, re-resolve the goal person, queue the
//          delivery (cmd flag-blob 32), maybe play the arrival voice, re-arm cmd29.
//   Returns nothing (void); drives the record + cmd queue in place.
void RunTransport(HeRecord* h);

} // namespace guild::sim
