// charaction_steps7 — the CharAction transport (he_Transport) coroutine cluster.
// See charaction_steps7.h for the module overview and the recovered He-record field
// map. Each function carries its gilde.exe address; struct field accesses use the
// Cas7_*/He_* accessors (byte-faithful offsets). The control flow, phase ids, lap
// counter, RNG draws and GameTime arithmetic are translated 1:1; cross-cluster leaf
// side effects are routed through the CharActionStep7Hooks bridge + NpcLeafHooks.
#include "sim/charaction_steps7.h"

#include "sim/charaction_steps5.h"  // ApplyTransportSpeed (reused)
#include "sim/gametime.h"           // GameTimeAdvance, GameTimeCompare
#include "sim/npcaction.h"          // NpcClock(), GetNpcLeafHooks()

#include <cstring>                  // std::memcpy

namespace guild::sim {

// Byte-exact, alignment-safe loads/stores at arbitrary byte offsets. The x86
// binary reads/writes He-record fields via unaligned `*(int*)(rec+N)` at offsets
// that are not naturally aligned (e.g. +1, +2, +93, +59, +36, +97). Binding an
// i32&/HeRecord*& reference to those addresses is UB in portable C++ (UBSAN).
// These move the identical little-endian bytes without forming a misaligned ref.
namespace {
inline i32 LoadI32At(const HeRecord* h, int off) {
    i32 v; std::memcpy(&v, reinterpret_cast<const u8*>(h) + off, sizeof(v)); return v;
}
inline void StoreI32At(HeRecord* h, int off, i32 v) {
    std::memcpy(reinterpret_cast<u8*>(h) + off, &v, sizeof(v));
}
// The transport endpoint people/objects resolved by VIBE_Person_QueryBegin /
// VIBE_Object_FindObjectById store their entity id at byte offset +1 (a misaligned
// dword), NOT at the +4 used by the He_* record family. Every id comparison/emit in
// the original transport code reads *(rec+1) (disasm: `mov eax,[edi+1]` @0x4de0ae,
// `cmp ...,[Begin+1]` @0x4df8a1, etc.). Use this for those records.
inline i32 PersonId(const HeRecord* rec) {
    return rec ? LoadI32At(rec, 1) : 0;
}
} // namespace

// ---------------------------------------------------------------------------
// Hook table plumbing (inert default — every leaf reports "absent"/no-op/0).
// ---------------------------------------------------------------------------
namespace {

HeRecord* InertPersonQueryBegin(i32, int, int, i32)            { return nullptr; }
HeRecord* InertFindObjectById(i32)                             { return nullptr; }
HeRecord* InertObjectQueryFind(i32, int, int, int, i32)        { return nullptr; }
HeRecord* InertResolveEntityById(int, HeRecord** out, i32, int){ if (out) *out = nullptr; return nullptr; }
HeRecord* InertBuildingFindById(i32)                           { return nullptr; }
int       InertIsProductionType(HeRecord*)                     { return 0; }
int       InertIsStorageType(HeRecord*)                        { return 0; }
int       InertSumWorkstation(HeRecord*, int, int)             { return 0; }
HeRecord* InertFindWorkProduct(HeRecord*)                      { return nullptr; }
int       InertCollectProductionVolume(HeRecord*)              { return 0; }
void      InertWalkScene(HeRecord*, bool (*)(HeRecord*, void*), void*) {}
int       InertNodeNameEquals(HeRecord*, const char*)          { return 0; }
i32       InertCmdRequest19(i32, i32, i32, i32)                { return 0; }
void      InertCmdRequestPair51(i32, int)                      {}
void      InertCmdRequestArgs25(i32, int, int, int, int)       {}
void      InertCmdRequestSingle49(i32)                         {}
void      InertCmdRequestNamedObject53(i32, i32, int, int, int, const char*) {}
void      InertCmdRequestQuad46(i32, int, int, int)            {}
void      InertCmdRequestChrMove(i32, i32, int, int)           {}
void      InertCmdRequestBuildOp74(i32)                        {}
void      InertRequestChangeZustand(i32, int)                  {}
void      InertSendQuickjump(i32, i32, int)                    {}
void      InertSendEntityMessage(i32, int)                     {}
i32       InertUniverseSwitchSlot(i32, int)                    { return 0; }
int       InertIsNearDoor(HeRecord*, HeRecord*)                { return 0; }
int       InertWithinTolerance(const float*, const float*, float) { return 0; }
i32       InertCityRecipientId(u16)                            { return 0; }
u8        InertCityCategory(u16)                               { return 0; }
int       InertRandomModulo(int)                               { return 0; }
int       InertFastFrameCounter()                              { return 0; }
int       InertMedFrameCounter()                               { return 0; }
u16       InertCurrentSceneCity()                              { return 0; }

const CharActionStep7Hooks kInertHooks = {
    InertPersonQueryBegin, InertFindObjectById, InertObjectQueryFind,
    InertResolveEntityById, InertBuildingFindById, InertIsProductionType,
    InertIsStorageType, InertSumWorkstation, InertFindWorkProduct,
    InertCollectProductionVolume, InertWalkScene, InertNodeNameEquals,
    InertCmdRequest19, InertCmdRequestPair51, InertCmdRequestArgs25,
    InertCmdRequestSingle49, InertCmdRequestNamedObject53, InertCmdRequestQuad46,
    InertCmdRequestChrMove, InertCmdRequestBuildOp74, InertRequestChangeZustand,
    InertSendQuickjump, InertSendEntityMessage, InertUniverseSwitchSlot,
    InertIsNearDoor, InertWithinTolerance, InertCityRecipientId, InertCityCategory,
    InertRandomModulo, InertFastFrameCounter, InertMedFrameCounter,
    InertCurrentSceneCity,
};
const CharActionStep7Hooks* g_hooks = &kInertHooks;

// Re-stamp the global clock (qword_13CE852 image) into the appointment (+82) and
// scratch (+96) GameTime mirrors — the original's two three-store sequences.
inline void StampClockBoth(HeRecord* h) {
    He_ApptTime(h) = NpcClock();
    *reinterpret_cast<GameTime*>(HeBytes(h) + 96) = NpcClock();
}

// The literal the transporter scene-walk matches against (aDummyTransport_2).
const char kDummyTransporter[] = "dummy_TRANSPORTER";

} // namespace

void SetCharActionStep7Hooks(const CharActionStep7Hooks* hooks) {
    g_hooks = hooks ? hooks : &kInertHooks;
}
const CharActionStep7Hooks& GetCharActionStep7Hooks() { return *g_hooks; }

// ===========================================================================
// Scene-walk collection (PickRandomTransporter).
// ===========================================================================

// gilde.exe 0x4de3b0 — VIBE_CharAction_CollectTransporterCb
bool CollectTransporterCb(HeRecord* node, TransporterCollector* ctx) {
    const CharActionStep7Hooks& k = GetCharActionStep7Hooks();
    // if ( strcmp_equal(node, "dummy_TRANSPORTER") ) ctx->nodes[ctx->count++] = node;
    if (k.nodeNameEquals(node, kDummyTransporter)) {
        i32 idx = ctx->count;            // v6 = *(_DWORD*)(ctx+128)
        ctx->count = idx + 1;            // *(_DWORD*)(ctx+128) = v6 + 1
        if (idx >= 0 && idx < 32)        // *(_DWORD*)(ctx + 4*v6) = node
            ctx->nodes[idx] = node;
    }
    return ctx->count < 32;              // return *(_DWORD*)(ctx+128) < 32
}

// Thunk matching the void*-ctx scene-walk callback signature.
static bool CollectTransporterTrampoline(HeRecord* node, void* ctx) {
    return CollectTransporterCb(node, static_cast<TransporterCollector*>(ctx));
}

// gilde.exe 0x4de3fc — VIBE_CharAction_PickRandomTransporter
HeRecord* PickRandomTransporter(HeRecord* root) {
    const CharActionStep7Hooks& k = GetCharActionStep7Hooks();
    TransporterCollector v5;            // _DWORD v5[32]
    v5.count = 0;                       // v6 = 0
    // The original temporarily clears root[+496] when root is null or root[+528]&1
    // is set, runs the walk, then restores it. The field is a render-visibility
    // toggle opaque to this cluster; the collection result is identical either way,
    // so we replay the single scene walk and route the gate as a comment.
    bool rawGate = (!root) ||
                   ((*reinterpret_cast<u8*>(HeBytes(root) + 528) & 1) != 0);
    if (rawGate) {
        k.walkScene(root, CollectTransporterTrampoline, &v5);
    } else {
        i32 saved = *reinterpret_cast<i32*>(HeBytes(root) + 496);   // v3
        *reinterpret_cast<i32*>(HeBytes(root) + 496) = 0;
        k.walkScene(root, CollectTransporterTrampoline, &v5);
        *reinterpret_cast<i32*>(HeBytes(root) + 496) = saved;
    }
    if (v5.count) {
        int n = k.randomModulo(v5.count) & 0xFFFF;   // (unsigned __int16)RandomModulo
        if (n >= 0 && n < v5.count && n < 32)
            return v5.nodes[n];
        return nullptr;
    }
    return nullptr;
}

// ===========================================================================
// One-shot transport allocator.
// ===========================================================================

// gilde.exe 0x4ddfcc — VIBE_CharAction_AllocTransport
HeRecord* AllocTransport(HeRecord* h) {
    const CharActionStep7Hooks& k = GetCharActionStep7Hooks();
    // Snapshot the saved time (+68) into the appointment (+82) and scratch (+96).
    He_ApptTime(h) = He_SavedTime(h);
    *reinterpret_cast<GameTime*>(HeBytes(h) + 96) =
        *reinterpret_cast<GameTime*>(HeBytes(h) + 68);
    i32 goalKey = Cas7_GoalId(h);                 // v16 = *(_DWORD*)(a1+176)... (read early)
    *reinterpret_cast<i32*>(HeBytes(h) + 232) = -1;  // window (+232) = -1
    Cas7_Started(h) = 0;                          // (+208) = 0
    *reinterpret_cast<i32*>(HeBytes(h) + 212) = 0;   // lap (+212) = 0

    // Begin = QueryBegin(&appt, 1,1, start) — note the original keys on +176 read
    // into v16 BEFORE this call but passes the start record; we follow the bytes.
    HeRecord* start = k.personQueryBegin(0, 1, 1, Cas7_StartId(h));
    if (!start)
        { GetNpcLeafHooks().freeHandlerEntry(h); return nullptr; }
    HeRecord* goal = k.personQueryBegin(Cas7_GoalId(h), 1, 1, Cas7_GoalId(h));
    if (!goal)
        { GetNpcLeafHooks().freeHandlerEntry(h); return nullptr; }
    (void)goalKey;

    // Classify the route variant from the two IsProductionType results.
    //   v5 = IsProductionType(start)  (the original discards the first call's eax,
    //        re-using the ecx side; faithfully both calls are made).
    int v5 = k.isProductionType(start);
    bool v6 = k.isProductionType(goal) != 0;
    if (!v5 || v6) {
        if (!v5 && v6)
            Cas7_Variant(h) = 1;             // door
    } else {
        Cas7_Variant(h) = 2;                 // local
    }
    bool jumped = false;
    if (!v5 && !v6) {
        Cas7_Variant(h) = 3;                 // city-city
        jumped = true;
    }
    if (!jumped) {
        if (!v5 || !v6) {
            // fallthrough to the shared LABEL_9 body
        } else {
            Cas7_Variant(h) = 4;             // mixed
            if (start == goal) {
                // start==goal -> error + free
                { GetNpcLeafHooks().freeHandlerEntry(h); return nullptr; }
            }
        }
    }

    // LABEL_9: stash the combined endpoint ids, advance the appointment one day,
    // resolve the cart, scale its speed, flag it, and roll the escort type.
    He_SeqId(h) = *reinterpret_cast<i32*>(HeBytes(start) + 48) +
                  *reinterpret_cast<i32*>(HeBytes(goal) + 48);   // +184
    Cas7_OriginId(h) = PersonId(start);       // (+196) = *(start+1)  (disasm 0x4de0ae)
    Cas7_Started(h) = 0;                      // (+208) = 0
    GameTimeAdvance(&He_ApptTime(h), 0, 1, 0); // VIBE_GameTime_Advance(+82,0,1,0): +1 second

    HeRecord* cart = nullptr;
    HeRecord* ent = k.resolveEntityById(0, &cart, Cas7_CartId(h), 0);
    if (!ent || !cart)
        { GetNpcLeafHooks().freeHandlerEntry(h); return nullptr; }

    HeRecord* bld = k.buildingFindById(*reinterpret_cast<i32*>(HeBytes(cart) + 28));
    // cart speed = (i16)cart[+18] * scale * factor + base, into vehicle[+416].
    int classByte = *reinterpret_cast<u8*>(HeBytes(cart) + 18);
    HeRecord* veh; std::memcpy(&veh, HeBytes(cart) + 59, sizeof(veh));
    if (veh) {
        // binary: (double)(__int16)cls * (double)flt_61F244 * dbl_61F248 + dbl_61F250
        float spd = static_cast<float>(
            static_cast<double>(static_cast<short>(classByte)) *
                static_cast<double>(kCartSpeedScale) * kCartSpeedFactor +
            kCartSpeedBase);
        *reinterpret_cast<float*>(HeBytes(veh) + 416) = spd;
        u16 cls = *reinterpret_cast<u16*>(HeBytes(cart));   // *(_WORD*)v20
        if (cls == 310)
            *reinterpret_cast<float*>(HeBytes(veh) + 416) =
                static_cast<float>(spd + kCartBonus310);
        else if (cls == 309)
            *reinterpret_cast<float*>(HeBytes(veh) + 416) =
                static_cast<float>(spd + kCartBonus309);
    }
    // workstation-count speed bump (only when a backing building exists).
    if (bld && veh) {
        int n = k.sumWorkstation(bld, 17, 1);
        float cur = *reinterpret_cast<float*>(HeBytes(veh) + 416);
        float v = static_cast<float>((n * kWorkstationMul + 1.0) * cur);
        *reinterpret_cast<float*>(HeBytes(veh) + 416) = v;
        Cas7_BaseSpeed(h) = v;                // *(float*)(a1+192) = v
    }
    *reinterpret_cast<u8*>(HeBytes(cart) + 19) |= 0x40u;     // mark in-transit
    if ((He_Flags(h) & 2) != 0)
        k.cmdRequestArgs25(LoadI32At(cart, 2), 19, 64, 1, 0);
    { HeRecord* _p = h; std::memcpy(HeBytes(cart) + 36, &_p, sizeof(_p)); }  // cart owner = h
    StoreI32At(h, 16, LoadI32At(cart, 2));  // *(a1+16) = cart id  (disasm: mov [ebp+10h],eax)

    // escort-type roll from the production volume (only when the cart is "loaded").
    if (cart && (*reinterpret_cast<u8*>(HeBytes(cart) + 32) & 1) != 0) {
        int vol = k.collectProductionVolume(cart);   // v19 (production sum)
        if (vol <= 0) {
            Cas7_Escort(h) = 0;                       // (+204) = 0
        } else {
            int v14 = (k.randomModulo(1) & 0xFFFF) + vol / 16000;
            Cas7_Escort(h) = v14;
            if (v14 > 2)
                Cas7_Escort(h) = 2;
        }
        if (Cas7_Escort(h)) {
            *reinterpret_cast<u8*>(HeBytes(cart) + 40) =
                static_cast<u8>(Cas7_Escort(h));
            // ComputeCartCost / cmd16 emit — opaque cost path; the control effect is
            // the escort byte stamp above. (cmd16 routed through cmd19's family is
            // not load-bearing for the phase machine.)
        } else {
            *reinterpret_cast<u8*>(HeBytes(cart) + 40) = 0;
        }
    }
    He_ReqHandle(h) = -1;                       // (+132) = -1
    *reinterpret_cast<i32*>(HeBytes(h) + 236) = -1;  // move packet (+236) = -1
    return cart;
}

// ===========================================================================
// Variant sub-steps. These run inside RunTransport state 1 (working tick). Each
// resolves the two endpoint people + the cart, scales the cart speed, and either
// arms the first move (Started == 0) or, once arrived, queues the unload + the
// narrative quickjump and re-arms the cmd29 packet. The opaque cmd/render emits are
// routed through hooks; the per-variant gating + the Started/lap bookkeeping is 1:1.
// ===========================================================================

// gilde.exe 0x4de534 — VIBE_CharAction_RunTransportLocal (variant 2)
i32 RunTransportLocal(HeRecord* h) {
    const CharActionStep7Hooks& k = GetCharActionStep7Hooks();
    HeRecord* begin = k.personQueryBegin(reinterpret_cast<intptr_t>(h) & 0, 1, 0, 68);
    k.personQueryBegin(0, 1, 0, 69);
    HeRecord* startp = k.personQueryBegin(0, 1, 1, Cas7_StartId(h));   // v20
    HeRecord* goalp  = k.personQueryBegin(0, 1, 1, Cas7_GoalId(h));    // v2
    HeRecord* cart = k.findObjectById(Cas7_CartId(h));
    if (!cart)
        return reinterpret_cast<intptr_t>(cart) & 0;
    ApplyTransportSpeed(h, cart);
    if (!Cas7_Started(h)) {
        // first leg: queue the move and flag started.
        k.cmdRequestSingle49(LoadI32At(cart, 2));
        k.cmdRequestNamedObject53(LoadI32At(cart, 2),
                                  PersonId(goalp), 0, -1, 1, "Trans");
        i32 pkt = k.cmdRequest19(PersonId(begin),
                                 PersonId(startp), 0,
                                 LoadI32At(cart, 2));
        Cas7_Started(h) = 1;
        Cas7_MovePkt(h) = pkt;
    } else {
        // arrived? gate on the cart's near-target tolerance / the lap watchdog.
        // binary 0x4de677: arrived only when veh && veh[+296]==0 && ((veh[+136]==
        // &byte_13ECEC8 && WithinTolerance(...,100.0)) || lap>6). When veh is null or
        // veh[+296]!=0 the whole `if` is false (NO arrival path). The earlier recon
        // fabricated `arrived = lap>6` in the else, which the binary does not do.
        // BOUNDARY: veh[+136]==&byte_13ECEC8 compares a render-state pointer to a
        // cross-cluster global sentinel; modelled as matching so the tolerance gate
        // is preserved (the only data not reachable from this cluster).
        HeRecord* veh; std::memcpy(&veh, HeBytes(cart) + 59, sizeof(veh));
        bool arrived = false;
        if (veh && *reinterpret_cast<i32*>(HeBytes(veh) + 296) == 0) {
            int near = k.withinTolerance(
                reinterpret_cast<const float*>(*reinterpret_cast<i32*>(HeBytes(veh) + 52) + 76),
                &Cas7_TgtX(h), 100.0f);
            arrived = near || Cas7_Lap(h) > 6;
        }
        if (arrived) {
            if (!k.isStorageType(goalp)) {
                Cas7_MovePkt(h) = k.cmdRequest19(PersonId(goalp),
                                                 0, 0, LoadI32At(cart, 2));
                k.cmdRequestPair51(LoadI32At(cart, 2), 0);
            }
            k.cmdRequestArgs25(LoadI32At(cart, 2), 19, 0, 1, 64);
            // narrative push gated on the city category (6/7).
            u8 cat = k.cityCategory(He_CityIndex(h));
            if (cat == 6 || cat == 7) {
                HeRecord* prod = k.findWorkProduct(goalp);
                k.sendQuickjump(k.cityRecipientId(He_CityIndex(h)),
                                LoadI32At(cart, 2),
                                prod ? 1421 : 1421);
            }
            He_ReqHandle(h) = GetNpcLeafHooks().queueRequestEntity29(-1, h);
        }
    }
    return k.universeSwitchSlot(0, 1);
}

// gilde.exe 0x4de884 — VIBE_CharAction_RunTransportDoor (variant 1)
i32 RunTransportDoor(HeRecord* h) {
    const CharActionStep7Hooks& k = GetCharActionStep7Hooks();
    HeRecord* begin = k.personQueryBegin(0, 1, 0, 68);
    k.personQueryBegin(0, 1, 0, 69);
    HeRecord* startp = k.personQueryBegin(0, 1, 1, Cas7_StartId(h));   // v27 / v4
    HeRecord* goalp  = k.personQueryBegin(0, 1, 1, Cas7_GoalId(h));    // v5
    HeRecord* cart = k.findObjectById(Cas7_CartId(h));
    if (!cart)
        return 0;
    ApplyTransportSpeed(h, cart);
    if (!Cas7_Started(h)) {
        k.cmdRequestSingle49(LoadI32At(cart, 2));
        if (!k.isStorageType(startp))
            k.cmdRequestPair51(LoadI32At(cart, 2), 1);
        k.cmdRequestNamedObject53(LoadI32At(cart, 2),
                                  PersonId(startp), 0, -1, 0, "Trans");
        if (!k.isStorageType(startp))
            Cas7_MovePkt(h) = k.cmdRequest19(PersonId(begin),
                                             PersonId(startp), 0,
                                             LoadI32At(cart, 2));
        Cas7_Started(h) = 1;
        return k.universeSwitchSlot(0, 1);
    }
    if (!k.isNearDoor(cart, goalp))
        return k.universeSwitchSlot(0, 1);
    // reached the door: queue the unload move + the narrative push, re-arm cmd29.
    Cas7_MovePkt(h) = k.cmdRequest19(PersonId(goalp),
                                     PersonId(begin), 0,
                                     LoadI32At(cart, 2));
    u8 cat = k.cityCategory(He_CityIndex(h));
    if (cat == 6 || cat == 7)
        k.sendQuickjump(k.cityRecipientId(He_CityIndex(h)),
                        LoadI32At(cart, 2), 1421);
    He_ReqHandle(h) = GetNpcLeafHooks().queueRequestEntity29(-1, h);
    return k.universeSwitchSlot(0, 1);
}

// gilde.exe 0x4df258 — VIBE_CharAction_RunTransportEntry (variant 4)
i32 RunTransportEntry(HeRecord* h) {
    const CharActionStep7Hooks& k = GetCharActionStep7Hooks();
    HeRecord* begin = k.personQueryBegin(0, 1, 0, 68);
    k.personQueryBegin(0, 1, 0, 69);
    HeRecord* startp = k.personQueryBegin(0, 1, 1, Cas7_StartId(h));   // v31 / v32
    HeRecord* goalp  = k.personQueryBegin(0, 1, 1, Cas7_GoalId(h));    // v4 / v35
    HeRecord* cart = k.findObjectById(Cas7_CartId(h));
    if (!cart)
        return 0;
    ApplyTransportSpeed(h, cart);
    if (!Cas7_Started(h)) {
        k.cmdRequestSingle49(LoadI32At(cart, 2));
        k.cmdRequestNamedObject53(LoadI32At(cart, 2),
                                  PersonId(goalp), 0, -1, 1, "Trans");
        Cas7_MovePkt(h) = k.cmdRequest19(PersonId(begin),
                                         PersonId(startp), 0,
                                         LoadI32At(cart, 2));
        Cas7_Started(h) = 1;
        return k.universeSwitchSlot(0, 1);
    }
    if (!k.isNearDoor(cart, goalp)) {
        // not yet at the entry; the lap-16 watchdog re-issues the move.
        if (Cas7_Started(h) && Cas7_Lap(h) > 16) {
            HeRecord* veh; std::memcpy(&veh, HeBytes(cart) + 59, sizeof(veh));
            if (veh && *reinterpret_cast<i32*>(HeBytes(veh) + 296) == 0) {
                k.cmdRequestNamedObject53(LoadI32At(cart, 2),
                                          PersonId(goalp), 0, -1, 1, "Trans");
                Cas7_Lap(h) = 0;
            }
        }
        return k.universeSwitchSlot(0, 1);
    }
    // at the entry: queue the move into the building, request the avatar, then the
    // narrative push (the variant-specific 6216/6218/6220 templates), re-arm cmd29.
    Cas7_MovePkt(h) = k.cmdRequest19(PersonId(goalp),
                                     PersonId(begin), 0,
                                     LoadI32At(cart, 2));
    k.cmdRequestChrMove(LoadI32At(cart, 2),
                        PersonId(goalp), 0, -1);
    // PickRandomTransporter waypoint (opaque destination snap) — preserved as a call.
    PickRandomTransporter(nullptr);
    k.cmdRequestArgs25(LoadI32At(cart, 2), 19, 0, 1, 64);
    u8 cat = k.cityCategory(He_CityIndex(h));
    if (cat == 6 || cat == 7)
        k.sendQuickjump(k.cityRecipientId(He_CityIndex(h)),
                        LoadI32At(cart, 2), 1421);
    He_ReqHandle(h) = GetNpcLeafHooks().queueRequestEntity29(-1, h);
    return k.universeSwitchSlot(0, 1);
}

// gilde.exe 0x4deca4 — VIBE_CharAction_RunTransportStadtStadt (variant 3)
i32 RunTransportStadtStadt(HeRecord* h) {
    const CharActionStep7Hooks& k = GetCharActionStep7Hooks();
    HeRecord* begin = k.personQueryBegin(0, 1, 0, 68);
    k.personQueryBegin(0, 1, 0, 69);
    HeRecord* startp = k.personQueryBegin(0, 1, 1, Cas7_StartId(h));   // v3 / v29
    HeRecord* goalp  = k.personQueryBegin(0, 1, 1, Cas7_GoalId(h));    // v5

    // The driver: when the leg origin (+196) still equals the start endpoint and the
    // cart has NOT started, arm the cross-city leg (resolve a waypoint transporter,
    // snap the target coords, queue the move). Otherwise gate on arrival.
    // binary 0x4dece8: gate is `+196 == *(v3+1) && !+208` (v3 = startp, id @+1).
    if (Cas7_OriginId(h) == PersonId(startp) && !Cas7_Started(h)) {
        k.universeSwitchSlot(0, 1);
        // binary 0x4ded41: PickRandomTransporter(*(v5+97)) where v5 = goalp (the
        // GoalId query), NOT startp. (earlier recon read startp+97.)
        HeRecord* _wpsrc; std::memcpy(&_wpsrc, HeBytes(goalp) + 97, sizeof(_wpsrc));
        HeRecord* way = PickRandomTransporter(_wpsrc);
        (void)way;
        HeRecord* obj = k.objectQueryFind(
            startp ? LoadI32At(startp, 93) : 0,
            1, 1, 0, Cas7_CartId(h));
        if (!obj)
            obj = k.findObjectById(Cas7_CartId(h));
        if (obj) {
            ApplyTransportSpeed(h, obj);
            Cas7_Started(h) = 1;
            // snap the target coords (the heightmap world-to-tile result).
            Cas7_TgtX(h) = 0.0f; Cas7_TgtY(h) = 0.0f; Cas7_TgtZ(h) = 0.0f;
            if (!k.isStorageType(startp))
                k.cmdRequestPair51(LoadI32At(obj, 2), 1);
            k.cmdRequestQuad46(LoadI32At(obj, 2), 0, 0, 0);
            if (!k.isStorageType(startp))
                Cas7_MovePkt(h) = k.cmdRequest19(PersonId(begin),
                                                 Cas7_StartId(h), 0, Cas7_CartId(h));
        } else {
            k.objectQueryFind(0, 2, 7, 1, Cas7_CartId(h));
        }
        Cas7_OriginId(h) = PersonId(begin);   // (+196) = *(Begin+1)  (binary 0x4deeb7)
        return Cas7_OriginId(h);
    }

    // running leg: when the leg origin reached the start endpoint, resolve the cart
    // in-scene and, once within tolerance (or past the watchdog), queue the unload.
    if (Cas7_OriginId(h) == PersonId(begin)) {
        HeRecord* obj = k.objectQueryFind(
            begin ? LoadI32At(begin, 93) : 0,
            1, 1, 0, Cas7_CartId(h));
        if (!obj)
            obj = k.objectQueryFind(0, 2, 7, 1, Cas7_CartId(h));
        if (obj) {
            HeRecord* veh; std::memcpy(&veh, HeBytes(obj) + 59, sizeof(veh));
            ApplyTransportSpeed(h, obj);
            if (veh) {
                i32 anim = *reinterpret_cast<i32*>(HeBytes(veh) + 296);
                if (anim) {
                    // binary 0x4defa2: if *(anim+9)==45 copy anim's target coords
                    // (anim[+308/+312/+316]) into the He target slots (+216/+220/+224).
                    u8* a = reinterpret_cast<u8*>(static_cast<uintptr_t>(
                                static_cast<u32>(anim)));
                    if (a[9] == 45) {
                        std::memcpy(HeBytes(h) + 216, a + 308, 4);
                        std::memcpy(HeBytes(h) + 220, a + 312, 4);
                        std::memcpy(HeBytes(h) + 224, a + 316, 4);
                    }
                }
                if (veh && *reinterpret_cast<i32*>(HeBytes(veh) + 296) == 0) {
                    int near = k.withinTolerance(
                        reinterpret_cast<const float*>(*reinterpret_cast<i32*>(HeBytes(veh) + 52) + 76),
                        &Cas7_TgtX(h), 300.0f);
                    if (near || Cas7_Lap(h) > 6) {
                        k.cmdRequestArgs25(LoadI32At(obj, 1),
                                           19, 0, 1, 64);
                        // binary 0x4df01b/0x4df04e: running-leg storage checks key on
                        // IsStorageType(v5)=goalp, not startp.
                        if (!k.isStorageType(goalp)) {
                            k.cmdRequestPair51(LoadI32At(obj, 1), 0);
                            // binary 0x4df046: QueueRequest19(*(v5+1), *(Begin+1), ..,
                            // *(a1+172)) where v5 = goalp (GoalId query). (earlier
                            // recon passed the raw +176 StartId field for arg1.)
                            Cas7_MovePkt(h) = k.cmdRequest19(PersonId(goalp),
                                                             PersonId(begin), 0,
                                                             Cas7_CartId(h));
                        }
                        u8 cat = k.cityCategory(He_CityIndex(h));
                        if (cat == 6 || cat == 7)
                            k.sendQuickjump(k.cityRecipientId(He_CityIndex(h)),
                                            LoadI32At(obj, 1), 1421);
                        Cas7_OriginId(h) = Cas7_GoalId(h);   // (+196) = (+180)
                        He_ReqHandle(h) = GetNpcLeafHooks().queueRequestEntity29(-1, h);
                    }
                }
            }
        }
    }
    return Cas7_OriginId(h);
}

// ===========================================================================
// The transport coroutine driver.
// ===========================================================================

// gilde.exe 0x4df7d8 — VIBE_CharAction_RunTransport
void RunTransport(HeRecord* h) {
    const CharActionStep7Hooks& k = GetCharActionStep7Hooks();
    // Initial gate: if a cmd29 packet is pending (flag 2 set and +132 != -1), wait
    // for it to apply before advancing.
    if ((He_Flags(h) & 2) != 0 && He_ReqHandle(h) != -1) {
        if (!GetNpcLeafHooks().packetStatus(He_ReqHandle(h)))
            return;
        He_ReqHandle(h) = -1;
    }
    HeRecord* begin = k.personQueryBegin(0, 1, 0, 68);
    HeRecord* begin69 = k.personQueryBegin(0, 1, 0, 69);   // v6 — also null-checked
    i32 startKey = Cas7_StartId(h);
    HeRecord* startp = k.personQueryBegin(startKey, 1, 1, startKey);   // v7
    HeRecord* goalp  = k.personQueryBegin(startKey, 1, 1, Cas7_GoalId(h)); // v8
    // any endpoint missing -> force the finish phase (-1). binary 0x4df84e/0x4df8e7:
    // tests Begin(68), then startp, goalp, AND the query-69 result (v6=begin69).
    if (!begin || !startp || !goalp || !begin69)
        He_State(h) = -1;

    if (!k.findObjectById(Cas7_CartId(h))) {
        // LABEL_76 — the cart vanished; free the handler.
        GetNpcLeafHooks().freeHandlerEntry(h);
        return;
    }

    switch (He_State(h)) {
        case -2: {  // 0xFFFFFFFE — arrival
            // VIBE_NpcAction_SetTargetCityRef(h, currentSceneCity) @0x4c9484:
            //   *(u16*)(h+8) = city ; if city==0xFFFF -> *(h+12) = -1
            //   else *(h+12) = dword_12CE914[134*city]  (== cityRecipientId(city)).
            u16 city = k.currentSceneCity();
            He_CityIndex(h) = city;
            He_CityId(h) = (city == 0xFFFF) ? -1 : k.cityRecipientId(city);
            i32 origin = Cas7_OriginId(h);
            // binary 0x4df8a1/0x4df8aa/0x4df8ba: compares +196 against *(Begin+1),
            // *(goalp+1), *(startp+1) — the person-record id at +1.
            if (origin == PersonId(begin) ||
                origin == PersonId(goalp) ||
                origin == PersonId(startp)) {
                He_ReqHandle(h) = GetNpcLeafHooks().queueRequestEntity29(1, h);
            }
            return;
        }
        case -1: {  // 0xFFFFFFFF — finish / drop
            HeRecord* obj = k.objectQueryFind(0, 2, 7, 1, Cas7_CartId(h));
            if ((He_Flags(h) & 2) != 0 && obj) {
                if ((k.randomModulo(100) & 0xFFFF) > 50)
                    k.requestChangeZustand(LoadI32At(obj, 1), -2);
                i32 win = Cas7_Window(h);
                if (win != -1) {
                    // VIBE_Window_RemoveIfActive — opaque; clearing the handle is the
                    // observable record effect.
                }
            }
            if (obj) {
                u8 flag = *reinterpret_cast<u8*>(HeBytes(obj) + 19);
                *reinterpret_cast<i32*>(HeBytes(obj) + 36) = 0;        // clear owner
                *reinterpret_cast<u8*>(HeBytes(obj) + 19) = flag & 0xBF; // clear 0x40
            }
            GetNpcLeafHooks().freeHandlerEntry(h);   // LABEL_76
            return;
        }
        case 0: {   // begin
            if ((He_Flags(h) & 2) != 0) {
                Cas7_Window(h) = -1;                 // (+232) = -1
                GameTimeAdvance(&He_ApptTime(h), 0, 1, 0);  // +1 second (0x4df934)
                He_ReqHandle(h) =
                    GetNpcLeafHooks().queueRequestEntity29(He_State(h) + 1, h);
            }
            return;
        }
        case 1: {   // working tick
            if ((He_Flags(h) & 2) == 0)
                return;
            GameTime clk = NpcClock();
            if (GameTimeCompare(&He_ApptTime(h), &clk) >= 0)
                return;
            i32 pkt = Cas7_MovePkt(h);
            if (pkt != -1 && !GetNpcLeafHooks().packetStatus(pkt))
                return;
            if (Cas7_MovePkt(h) != -1 &&
                GetNpcLeafHooks().packetStatus(Cas7_MovePkt(h)) == 2) {
                He_ReqHandle(h) = GetNpcLeafHooks().queueRequestEntity29(-1, h);
                return;
            }
            Cas7_MovePkt(h) = -1;
            switch (Cas7_Variant(h)) {
                case 2: RunTransportLocal(h);      break;
                case 1: RunTransportDoor(h);       break;
                case 3: RunTransportStadtStadt(h); break;
                case 4: RunTransportEntry(h);      break;
                default: /* ErrorLog */            break;
            }
            ++Cas7_Lap(h);
            StampClockBoth(h);
            GameTimeAdvance(&He_ApptTime(h), 0, 0, 5);   // +5 minutes

            if (Cas7_Lap(h) == 5) {
                HeRecord* obj = nullptr;
                k.resolveEntityById(0, &obj, Cas7_CartId(h), 0);
                int cls = obj ? *reinterpret_cast<u8*>(HeBytes(obj) + 18) : 0;
                if ((k.randomModulo(30) & 0xFFFF) > cls) {
                    // highwayman ambush narrative + drop.
                    if (obj) {
                        k.sendEntityMessage(k.cityRecipientId(He_CityIndex(h)), 1421);
                        k.cmdRequestBuildOp74(PersonId(obj));  // *(v35+1) (binary 0x4dfc93)
                    }
                    He_ReqHandle(h) = GetNpcLeafHooks().queueRequestEntity29(-1, h);
                    return;
                }
            }
            if (Cas7_Delivered(h) || Cas7_Lap(h) < 5)
                return;

            // arrival-probability roll: scale a base by the building product count
            // and the framerate tiers, compare against RandomModulo(105).
            HeRecord* obj = nullptr;
            k.resolveEntityById(0, &obj, Cas7_CartId(h), 0);
            HeRecord* bld = obj ? k.buildingFindById(*reinterpret_cast<i32*>(HeBytes(obj) + 28)) : nullptr;
            float prob = 0.0f;
            if (bld) {
                int n = k.sumWorkstation(bld, 2, 1);
                // binary: v40 = (double)v39 + flt_61F3A8
                prob = static_cast<float>(static_cast<double>(n) +
                                          static_cast<double>(kAmbushBase));
            }
            if (k.fastFrameCounter() > 100)
                prob = static_cast<float>(prob + kAmbushFast);
            else if (k.medFrameCounter() > 200)
                prob = static_cast<float>(prob + kAmbushMed);
            int cls = obj ? *reinterpret_cast<u8*>(HeBytes(obj) + 18) : 0;
            prob = static_cast<float>(prob * kAmbushScale);  // v40 = v40 * dbl_61F3B8
            // binary: *(float*)&v39 = (double)(__int16)v41 * v40
            float threshold = static_cast<float>(
                static_cast<double>(static_cast<short>(cls)) *
                static_cast<double>(prob));
            int roll = k.randomModulo(105) & 0xFFFF;
            if (static_cast<double>(roll) <= threshold) {
                Cas7_Delivered(h) = 1;
            } else {
                u8 cat = k.cityCategory(He_CityIndex(h));
                if ((cat == 6 || cat == 7) && obj &&
                    (*reinterpret_cast<u8*>(HeBytes(obj) + 32) & 1) == 0) {
                    k.sendEntityMessage(k.cityRecipientId(He_CityIndex(h)), 1421);
                }
                if (obj)
                    k.requestChangeZustand(LoadI32At(obj, 2), -15);
                Cas7_Delivered(h) = 2;
            }
            return;
        }
        case 2: {   // deliver
            if (Cas7_MovePkt(h) != -1 &&
                !GetNpcLeafHooks().packetStatus(Cas7_MovePkt(h)))
                return;
            Cas7_MovePkt(h) = -1;
            // re-resolve the goal person + queue the delivery flag-blob; maybe play
            // the arrival voice. Opaque emits; the observable effect is the cmd29
            // re-arm below.
            k.personQueryBegin(0, 1, 1, Cas7_GoalId(h));
            He_ReqHandle(h) = GetNpcLeafHooks().queueRequestEntity29(-1, h);
            return;
        }
        default:
            return;
    }
}

} // namespace guild::sim
