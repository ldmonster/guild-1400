#pragma once
// ===========================================================================
// Meister / Guild-Master per-frame AI decision calculators — 1:1 reconstruction
// (gilde.exe). These are the master-NPC "brain" branches dispatched once per
// AI-controllable Meister per evaluation tick from VIBE_Ai_EvaluateMeister
// (0x4533a8, dispatch core in src/ai/aiplayer.cpp ClassifyMeisterRoutine).
//
// Reconstructed top-level calculators (this module):
//   0x457440 VIBE_Ai_CalcMeisterDiebe        — thief/criminal master AI
//   0x454f50 VIBE_Ai_CalcMeisterFarming      — farm/estate master AI
//   0x455cd8 VIBE_Ai_CalcMeisterWache        — guard master AI (patrol/escort)
//   0x4588d0 VIBE_Ai_CalcMeisterAmbush       — ambush master AI
//   0x4569a8 VIBE_Ai_CalcAngriff             — attack/spy decision (shared)
//   0x45e350 VIBE_MeisterAi_EquipStaffWeapon — arm idle staff with stocked weapons
// + the shared MeisterAi sub-planner cluster (HireStaff / TrainStaff / FlagIdleStaff
//   / AssignWorkstations / CollectStorageItems / GatherRequiredItems /
//   ReserveWorkstationItems / CheckWorkstationCapacity / CollectTransporters /
//   TradeManageStorage / the FULL bodies of FillAiSlots / RenovateBuilding /
//   FindFreeStaffSlot whose pure decision cores already live in
//   src/sim/meister_mgmt_recon.{h,cpp} and are REUSED here).
//
// DATA MODEL (rule 1, byte-faithful): the originals address their records by raw
// byte offset. We operate on the live record arrays from sim/entity.h
// (g_persons / g_personIds / g_objects / scene nodes) through raw little-endian
// byte accessors (see ai_meister.cpp anon-namespace rd*/wr* helpers) so every
// field touch matches the decompile exactly.
//
// BOUNDARIES surfaced as data (rule 8, NOT faked):
//   * the lockstep command queue: every Calc that "acts" builds a 248-byte command
//     record and would hand it to VIBE_Command_QueueRequestSlotReset28 /
//     EnqueueCmd15 / QueueRequest17. We capture those into a per-call MeisterCmdSink
//     (the packets the original would queue); the live bridge drains the sink into
//     the real CommandQueue. This mirrors the established sibling pattern in
//     src/sim/aiaction_dispatch_ai_recon2.h (RequestPacket).
//   * the 8x8 city-tile danger grid (word_12349A0/A2/byte_12349A4): a genuine
//     engine input table; modeled as g_cityTileGrid below (zero-init), populated by
//     the live engine. The patrol/ambush scorers read it verbatim.
//   * the MeisterAi work-order / stock SCRATCH tables (0xB5xxxx): shared scratch
//     globals the Production/Farming planners fill and consume within one tick.
//   * VIBE_Crt_Sprintf_0 debug-log calls have NO sim side effect and are dropped
//     (the original logs to a scratch buffer + dev console).
// ===========================================================================
#include <cstdint>
#include <vector>

#include "guild/common/types.h"
#include "sim/types.h"
#include "sim/entity.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// Meister routine selector (mirrors src/ai/aiplayer.h MeisterRoutine; we accept
// the same enum by value via the dispatch shim DispatchMeisterCalc below). The
// enum itself lives in ai/aiplayer.h — included by the .cpp, not here, to avoid
// a sim->ai header dependency in this public header.
// ---------------------------------------------------------------------------

// ===========================================================================
// City-tile danger grid — word_12349A0 (A), word_12349A2 (B), byte_12349A4 (valid)
// An 8x8 grid of 24-byte tile records (192 bytes/row, 24 bytes/col). The danger
// score of a tile = A + B*0.125. VIBE_Coord_WorldToCityTile maps a world pos to
// (row,col). This is a live-engine input table (genuine boundary), zero-init.
//   byte index of tile (row,col): 192*row + 24*col
//   word A   at +0 of the tile, word B at +2, valid byte at +4.
// ===========================================================================
constexpr int kCityTileRows   = 8;
constexpr int kCityTileCols   = 8;
constexpr int kCityTileStride = 24;                 // bytes per tile/col
constexpr int kCityTileRowStride = kCityTileCols * kCityTileStride; // 192
constexpr int kCityTileBytes  = kCityTileRows * kCityTileRowStride; // 1536
extern u8 g_cityTileGrid[kCityTileBytes];           // word_12349A0 base

// ===========================================================================
// MeisterAi shared scratch tables (0xB5xxxx). The Production/Farming planners
// fill these per-tick from the building's work orders + stock, then consume them.
// Modeled as flat byte arrays at the exact strides the decompile uses.
//   dword_B56FDC : work-order count           (int)
//   dword_B56FE0 : stock-row count            (int; <<6 == byte bound of stock tbl)
//   dword_B56464 : work-order table, 88-byte stride (kMaxWorkOrders entries)
//   dword_B5444E : stock table, 64-byte stride
//   word_B564BC  : per-work-order flag word   (44-byte stride within B564xx block)
// The B564xx columns (A4/A8/84/88, BC) are all sub-columns of the 88-byte order
// record; we expose the order block as one byte array addressed by the same
// raw offsets the decompile uses (base 0xB56464; column symbol - 0xB56464 == off).
// ===========================================================================
constexpr int kMaxWorkOrders = 64;     // generous; original cap is small per bldg
constexpr int kWorkOrderStride = 88;
constexpr int kStockRows = 256;
constexpr int kStockStride = 64;
extern int g_workOrderCount;           // dword_B56FDC
extern int g_stockRowCount;            // dword_B56FE0
extern u8  g_workOrderTable[kMaxWorkOrders * kWorkOrderStride]; // base 0xB56464
extern u8  g_stockTable[kStockRows * kStockStride];             // base 0xB5444E
// dword_B53950.. : the "selected meister" snapshot globals (debug/HUD mirror).
extern i32 g_aiSelMeisterBuilding;     // dword_B53950 (selected building rec ptr)
extern i32 g_aiSelMeisterPerson;       // dword_B53958
extern i32 g_aiSelMeisterCount;        // dword_B53964 (idle/managed staff count)
extern i32 g_aiSelMeisterBuildingRec;  // dword_B53954
extern i32 g_aiSelOrderCount;          // dword_B5396C

// ===========================================================================
// Captured command sink (the lockstep-command boundary, surfaced as data).
// Each Calc that acts builds one of these and the live bridge queues it. Field
// names map to the 248-byte stack command record the decompile fills via
// VIBE_Light_SetGrayColorThunk(0,248,&buf) + field writes.
// ===========================================================================
struct MeisterCommand {
    // VIBE_Light_SetGrayColorThunk seeds a 248-byte header; the planners then set:
    u8   cmdType   = 0;     // +4 byte: 6 hire, 9 farm-harvest, 60 burgle, 63/64 spy,
                            //          67 patrol, 73 attack, 98 ambush, 100 escort, 2 transport...
    i32  actorId   = 0;     // dword_12CE914[...] — the acting actor/meister id
    i32  buildingId= 0;     // *(buildingRec+1) — owning building id
    u64  timePacked= 0;     // qword_13CE852 (game-time stamp)
    i32  timeExtra = 0;     // unk_13CE85A
    u16  timeTail  = 0;     // unk_13CE85E
    u8   mode      = 0;     // v44/v66/v99 flag byte (1 / 2)
    i32  targetId  = 0;     // target building/person id
    i32  srcId     = 0;     // source building id (transport/escort)
    i32  extra0    = 0;     // *(bldgRec+1) duplicate / storable id
    i32  extra1    = 0;     // storable object id / aux
    // For the action/spy/attack/patrol commands the planner appends a worker-id
    // list terminated by -1 (the v43.. fill). Captured verbatim.
    std::vector<i32> workerIds;
    // For EnqueueCmd15/QueueRequest17 (buy/sell) the planner emits a cash/item op:
    bool buyItem   = false; // QueueRequest17 sell/buy path was taken
    i32  buyItemType = 0;
    i32  buyAmount = 0;
    i32  buyPrice  = 0;     // EnqueueCmd15 amount
};

struct MeisterCmdSink {
    std::vector<MeisterCommand> emitted;
    void push(const MeisterCommand& c) { emitted.push_back(c); }
};

// The active sink for the current Calc invocation. The live engine bridge sets
// this before dispatch and drains it after; tests inspect it directly. When null,
// the planners run all their decision logic but skip the capture (still faithful —
// the command queue is the boundary). Defined in ai_meister.cpp.
extern MeisterCmdSink* g_meisterCmdSink;

// ===========================================================================
// Leaf hooks (rule 8): the genuinely-external engine leaves these AI brains call.
// They are modeled in src/ with DIVERGENT typed APIs (Person*/BuildingRec*/GridEnv
// /GoodTable, virtual hooks, injected fn-ptrs) and several need engine context not
// reachable from a raw-record AI tick. Per the established sibling pattern
// (world/event5.h, sim/npcaction10.h), we surface them as a function-pointer table
// the live bridge wires to the real reimpl leaves. They are NEVER faked: when a hook
// is null the planner takes the same path the original would on a null/zero return,
// and tests inject deterministic stand-ins to exercise the decision logic.
//
// All record parameters are raw record bases (u8*) so the call sites match the
// decompile's raw-offset addressing exactly.
// ===========================================================================
struct MeisterAiLeaves {
    // --- scene / entity queries (raw-base wrappers over sim/entity.h) ----------
    // The orig QueryFind(rootSceneId, ...filters...) — returns an item/scene node
    // base (u8*) or null; IterNext continues. `rootId` is the building's +93 scene
    // root id. `filters` is the flat varargs list (op[,val]...), `nfilters` ops.
    u8* (*queryFind)(i32 rootId, const int* filters, int nfilters) = nullptr;
    u8* (*queryIterNext)() = nullptr;
    // ResolveEntityById(&outPerson/&outScene, id) — returns nonzero on hit and
    // writes the resolved record base into *out (person/scene as the orig wants).
    int (*resolveEntityById)(i32* outA, i32* outB, i32 id, int mode) = nullptr;
    u8* (*buildingFindById)(i32 id) = nullptr;                 // 0x587b20
    u8* (*personQueryBegin)(i32 a, int op0, int op1) = nullptr; // 0x586c20
    u8* (*personIterNext)() = nullptr;                         // 0x586a6c

    // --- handler-list probe (He) ----------------------------------------------
    // FindFirstHandlerByFilter(a,b,filterCode,mode,key) -> handler base or null;
    // *(handler+0xAC i.e. dword[43]) holds the matched building/target id.
    u8* (*heFindFirst)(int a, int b, int filterCode, int mode, i32 key) = nullptr;
    u8* (*heFindNext)() = nullptr;

    // --- money / valuation reads ----------------------------------------------
    i32 (*sumCurrencyHeld)(u8* personRec) = nullptr;          // 0x59152c
    i32 (*computeAssetWorth)(u8* objRec, int includeFlag) = nullptr; // 0x591328
    i32 (*relationMatrix)(u16 ownerWord, u16 otherWord) = nullptr;   // 0x5942fc
    double (*computeWageByCategory)(u8* personArrBase, int typeByte, int arg) = nullptr; // 0x594d70

    // --- building reads --------------------------------------------------------
    int (*securityLevel)(u8* objRec) = nullptr;               // 0x5902ac
    double (*marketPrice)(i16 itemId, u8 pct) = nullptr;      // 0x58f3d0
    int (*mapTypeToCategory)(u8 typeByte) = nullptr;          // 0x5878b0
    int (*isProductionType)(u8* objRec) = nullptr;            // 0x587f80
    u8* (*findStorableObject)(u8* bldgRec) = nullptr;         // 0x5877ac
    u8* (*findActiveWorkSlot)(i32 itemId) = nullptr;          // 0x586904
    i32 (*inventoryFreeCapacity)(u8* slot, int typeHi, int off, int big) = nullptr; // 0x5924a8
    void (*syncProfessionState)(i32* personPtr, u8* typeBase) = nullptr; // 0x58a154

    // --- coords / weapons / avatar / office -----------------------------------
    int (*worldToCityTile)(const float* pos3, int* row, int* col) = nullptr; // 0x577e04
    u8* (*avatarLookupById)(i16 id) = nullptr;                // 0x4859b0
    int (*isWeaponSlotCompatible)(u8* meisterRec, i16 weaponId) = nullptr; // 0x550298
    void (*changePlayerAction)(i32 bldgRec, int z, u8* handler, u16 ownerWord) = nullptr; // 0x4b09c8
    int (*ensureBuildingAvatar)(i32* bldgRecPtr) = nullptr;   // 0x505134
    u8* (*amtFindOfficeTypeRecord)(int itemHi) = nullptr;     // 0x56e850
    int (*amtFindFreePlacement)(i32 a, int b, u8 c, int d) = nullptr; // 0x56e974
    int (*charActionIsAnimalTargetBusy)(u8* objRec) = nullptr;// 0x4dc590

    // --- farming / production extras (folded in from the farming planner) ------
    i32 (*inventoryGetEffectiveStock)(u8* typeRec, u8* itemRec) = nullptr;  // 0x5923fc
    void (*queueRequest20)(i32 bldgId, i16 itemHi) = nullptr;               // 0x4946f4
    i32 (*sumWorkstationByCategory)(u8* bldgRec, u8 cat, int flag) = nullptr; // 0x5904fc
    void (*computeWorkstationOutput)(u8* woRow) = nullptr;                  // 0x45b948
    void (*collectProductionSlots)(u8* objRec, i32* outBuf) = nullptr;      // 0x5922d4
    u8* (*objectFindById)(i32 id) = nullptr;                               // 0x583a70
    i32 (*queueRequestMixed44)(i32 bldgId, i8 col, i16 kind, i8 row,
                               i8 d, i32 e) = nullptr;                      // 0x494cf0
    // --- renovation / trade extras --------------------------------------------
    i32 (*buildingGetUpgradeLevel)(u8* bldgRec) = nullptr;                 // 0x58fc84
    i32 (*buildingValueComputeRoomWorth)(u8* bldgRec) = nullptr;           // 0x59116c
    i32 (*inventoryGetSlotCapacity)(u8* item) = nullptr;                   // 0x592474
    double (*lookupCachedMarketPrice)(i32 item, i32 currency) = nullptr;   // 0x58f6b8
};

// The active leaf table for the current Calc invocation (set by the bridge / test).
// Null hooks behave as documented above. Defined in ai_meister.cpp.
extern const MeisterAiLeaves* g_meisterLeaves;

// Current game time (mirror of qword_13CE852). The bridge sets this each tick; the
// planners read hour (.hour == WORD2), minute (.minute == +6), day (.day == +0).
extern GameTime g_meisterGameTime;
// unk_13CE85A / unk_13CE85E — the two trailing time-stamp fields the command record
// carries verbatim (set alongside g_meisterGameTime).
extern i32 g_meisterTimeExtra;   // unk_13CE85A
extern u16 g_meisterTimeTail;    // unk_13CE85E

// ===========================================================================
// Top-level calculators. `meisterRec` is the Meister's 536-byte person record
// (g_persons[i] base) — the original `a1`/`v82`. They mutate the record's
// per-tick flag bytes (+436/+456) and current-target field (+448) and push
// MeisterCommands into g_meisterCmdSink.
// ===========================================================================

// 0x4569a8 — attack/spy decision. `attackBudget` is the orig `a2` (caller's
// remaining-action budget / staff count). Returns the orig int result (1 = a
// decision was reached and the caller should stop, 0 = no target / fall through).
int CalcAngriff(u8* meisterRec, int attackBudget);

// 0x457440 — thief master.
void CalcMeisterDiebe(u8* meisterRec);
// 0x454f50 — farming master. Returns the orig int (TradeManageStorage result).
int  CalcMeisterFarming(u8* meisterRec);
// 0x455cd8 — guard master. Returns the orig int.
int  CalcMeisterWache(u8* meisterRec);
// 0x4588d0 — ambush master.
void CalcMeisterAmbush(u8* meisterRec);

// 0x45e350 — equip idle staff with a stocked weapon. `arg` is the orig a2.
int  EquipStaffWeapon(u8* meisterRec, int arg);

// ===========================================================================
// Shared MeisterAi sub-planner FULL bodies (the loop drivers around the pure
// cores in meister_mgmt_recon.h). Exposed for direct golden testing.
// ===========================================================================
void MeisterCollectStorageItems(u8* meisterRec);                 // 0x45a62c
void MeisterGatherRequiredItems(u8* meisterRec, i32 neededId);   // 0x45c10c
void MeisterHireStaff(u8* meisterRec);                           // 0x45c670
void MeisterTrainStaff(SceneNode* bldgNode, u8* meisterRec, int cap); // 0x45d2ac
void MeisterFlagIdleStaff(u8* meisterRec);                       // 0x45df7c
void MeisterAssignWorkstations(u8* meisterRec, SceneNode* node); // 0x4599f0
void MeisterReserveWorkstationItems(u8* meisterRec, u8* order);  // 0x45bd68
bool MeisterCheckWorkstationCapacity(u8* meisterRec, u8* order, int mode); // 0x45ba84
int  MeisterCollectTransporters(u8* meisterRec);                 // 0x45e71c
int  MeisterTradeManageStorage(u8* meisterRec);                  // 0x45f1e4
// FULL FillAiSlots / RenovateBuilding / FindFreeStaffSlot loop drivers (the pure
// cores AiSlotDeficit / RenovateRoomBudget / FreeStaffSlotCount are in
// meister_mgmt_recon.h and called from these).
void MeisterFillAiSlots(SceneNode* bldgNode, u8* meisterRec, int cap);  // 0x45cfac
void MeisterRenovateBuilding(u8* meisterRec);                           // 0x45c9ac
int  MeisterFindFreeStaffSlot(u8* meisterRec);                         // 0x45e12c
// Work-order pass drivers (full bodies around the pure cores CancelTaskFlagMask/
// TaskMatchesOrder/DispatchOrderCount/IdleWorkerShouldQueue in meister_mgmt_recon.h).
void MeisterCancelMatchingTasks(u8* meisterRec, int filterType);       // 0x45d4c4
// 0x45d618 VIBE_MeisterAi_DispatchOrders — the original a1 is a stack struct mixing
// pointers (record bases) and ints. We model it as a typed struct so the pointer
// fields stay full-width on 64-bit hosts (the orig packed them as 32-bit dwords).
//   args[0]=mode, args[1]=meisterRec, args[2..4]=order rows, args[5]=divisor,
//   args[6]=total, args[7]=passIndex, args[8]=already.
struct MeisterDispatchArgs {
    int mode      = 0;       // a1[0] command-type mode
    u8* meisterRec= nullptr; // a1[1]
    u8* orderRow  = nullptr; // a1[2]
    u8* orderRow3 = nullptr; // a1[3]
    u8* orderRow4 = nullptr; // a1[4]
    int divisor   = 0;       // a1[5]
    int total     = 0;       // a1[6]
    int passIndex = 0;       // a1[7]
    int already   = 0;       // a1[8]
};
void MeisterDispatchOrders(MeisterDispatchArgs* args);                 // 0x45d618
void MeisterAssignIdleWorkers();                                       // 0x45379c

// ===========================================================================
// Dispatch shim (rule 13). Called by the EvaluateMeister bridge with the routine
// chosen by ClassifyMeisterRoutine (ai/aiplayer.h). Defined in ai_meister.cpp;
// switches the routine onto the matching Calc above. `routine` is the integer
// value of guild::ai::MeisterRoutine.
// ===========================================================================
void DispatchMeisterCalc(int routine, u8* meisterRec, int attackBudget);

// Test/setup helper: clear all scratch + sink globals.
void ResetMeisterAiScratch();

} // namespace guild::sim
