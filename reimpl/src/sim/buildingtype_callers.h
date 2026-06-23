#pragma once
// ===========================================================================
// buildingtype_callers.{h,cpp} — the four REAL CALLERS of the buildingtype_recon
// cluster (gilde.exe), translated 1:1.  MODULE: buildings (namespace guild::sim).
// ===========================================================================
//
// This unit closes the "Wiring PENDING on unreconstructed callers" item of
// progress/buildingtype-bauplatz-recon.md: each of the four entry points of
// src/sim/buildingtype_recon.* is now invoked from its real, reconstructed
// caller exactly where the binary calls it:
//
//   VIBE_Scene_SyncMeisterBuildings  0x504ce0 -> VIBE_Building_RegisterNames
//                                                (0x504a54, full flow below; its
//                                                pick core is Building_PickName)
//   VIBE_Building_RegisterNames      0x504a54 -> Building_PickName (recon) +
//                                                Util_StrCmp name-clash scan
//   VIBE_Building_CreateGebaeude     0x586fb8 -> Building_AllocStorageRoom
//                                                (0x588988, recon) in the
//                                                room-slot loop @0x58733d
//   VIBE_Command_ExSellObjekt        0x496b90 -> Building_AllocStorageRoom
//                                                (@0x4974af) + Building4_Remove-
//                                                StorageRoom (@0x49739b) via the
//                                                two storage-node phases below
//   VIBE_Bauplatz_MapAllToSupermap   0x577464 -> Bauplatz_MapOneToSupermap
//                                                (0x5774b8, recon) per free plot
//
// REAL reconstructed siblings routed (NOT mocked):
//   UniverseSwitchActiveSlot (universe.cpp 0x5b4a24), Building_RecalcAll-
//   Production (building_production.cpp 0x583c3c), PersonSyncMasterShopObjects
//   (person_personnel2.cpp 0x594c94), PersonQueryBegin/IterNext + GameObject-
//   QueryFind/IterNext (entity.cpp 0x586c20/0x586a6c/0x5857fc/0x58529c),
//   BuildingTypeDefAt (building.cpp, dword_13CE294 + 589*type), g_objects /
//   g_persons (entity.cpp, dword_13CE298 / word_12CE910), g_buildingNextId
//   (building_create.cpp, dword_649890), Building_PickUniqueName + Building_-
//   ApplyTypeDefaults (building_create2.cpp), Building4_EnsureDefaultObjects +
//   Building4_RemoveStorageRoom (building4.cpp 0x586df8/0x588ce4), Building_-
//   FilterBlockedBauplatze (building5.cpp 0x50c8ec), Building_MapKindToCategory
//   + Building_IsProductionKind (building_type.cpp 0x5878b0/0x587f80),
//   util::RandomModulo (0x58b89c), render::BroadcastGrayDword (0x5c6af0),
//   BuildingArrays().objectTypeBase (building2.cpp, dword_13CE27C stride 65),
//   g_lastTradeId (command_apply.h, dword_631290).
//
// HOOKED leaves (genuinely unreconstructed engine subsystems — rule 8: named,
// inert defaults, never faked):
//   VIBE_Building_ComputeSlotStats     0x583c74  (per-city stats columns)
//   VIBE_GameObject_AddObjekt          0x585af4  (scene-graph object creation)
//   VIBE_GameObject_RemoveByProt       0x5859b4  (routed via Building4Hooks)
//   VIBE_Building_FindStorableObject   0x5877ac  (building4 core is hook-domain;
//                                                the word read needs live nodes)
//   VIBE_Building_InitWorkerCapacities 0x586ed8  (building4 core needs the two
//                                                resolved worker node records)
//   byte_13CD6A0 city table / byte_620EFC + dword_8C4788/8C4790 name tables /
//   qword_13CE852 game clock           (disk/runtime-loaded data -> providers)
#include <cstring>

#include "guild/common/types.h"
#include "sim/types.h"
#include "sim/building_production.h"   // PackedTime (qword_13CE852 image)
#include "sim/trade_sell.h"            // SellResolve + SellStoragePhase base

namespace guild::sim {

// ===========================================================================
// Module-state mirrors of binary globals this cluster owns.
// ===========================================================================
// word_63C740 — the scene/session flag word tested by SyncMeisterBuildings
// (bit 0x40 == "headless/quick sync", bit 4 == "skip name registration").
// NOTE: building6.cpp mirrors BIT 7 of the same word separately
// (g_freeEntryEverywhere); unification is tracked in the progress file.
void SetSceneSyncFlagsWord(u16 w);
u16  SceneSyncFlagsWord();

// dword_6498E8 / dword_6498EC[0] / dword_6498EC[1..] — the meister anchors and
// the master-shop record list filled by Scene_SyncMeisterBuildings.
Person* MeisterAnchorA();         // dword_6498E8   (kind 12, sub-state 0)
Person* MeisterAnchorB();         // dword_6498EC[0] (kind 12, sub-state 1)
Person* MeisterShopAt(int i);     // dword_6498EC[1 + i] (kind 11 records)
int     MeisterShopCount();

// byte_6498D7 / byte_6498D8 — the CreateGebaeude room-slot latch (set only in
// the slot-word==0xFFFF branch @0x587498, which is unreachable behind the outer
// !=0xFFFF guard; translated verbatim, see .cpp).
u8 LastGebProtLatch();
u8 LastGebSlotLatch();

// ===========================================================================
// Hook surface.  Every default is defined out-of-line in the .cpp: defaults
// route to the REAL reconstructions listed above; the genuinely unreconstructed
// leaves are inert.
// ===========================================================================
struct BuildingCallerHooks {
    virtual ~BuildingCallerHooks() = default;

    // ---- Scene_SyncMeisterBuildings (0x504ce0) leaves ----------------------
    // VIBE_Universe_SwitchActiveSlot(0, 0, ..) @0x504cf7.
    // Default: REAL UniverseSwitchActiveSlot(0, /*quiet=*/false).
    virtual void SwitchActiveSlot();
    // VIBE_Building_RecalcAllProduction() @0x504db6.  The 1:1 reconstruction
    // takes the lifted clock (nowDay/nowMinute); the original reads globals.
    // Default: REAL Building_RecalcAllProduction(nowDay, nowMinute).
    i32 nowDay = 0;
    i32 nowMinute = 0;
    virtual void RecalcAllProduction();
    // byte_13CD6A0[756*slot] != 0 — "city slot is live".  Inert: false.
    virtual bool CityAlive(int slot);
    // VIBE_Building_ComputeSlotStats(slot) @0x583c74 — UNRECONSTRUCTED leaf.
    virtual void ComputeSlotStats(int slot);
    // VIBE_Person_SyncMasterShopObjects(anchorA@esi) @0x504db5 (tail call).
    // Default: REAL PersonSyncMasterShopObjects; the original passes the record
    // POINTER as the opaque slot arg — the reimpl port of that token is the
    // record's index in g_persons (0 when null).
    virtual void SyncMasterShopObjects(Person* anchorA);

    // ---- Building_RegisterNames (0x504a54) iteration + name tables ---------
    // VIBE_Person_QueryBegin(node, 1, 6) / VIBE_Person_IterNext — despite the
    // name these iterate the OBJECT/BUILDING array (see entity.h).  Default:
    // REAL entity query with the match-any filter (op 6), exactly (.., 1, 6).
    virtual ObjectRec* MeisterQueryBegin();
    virtual ObjectRec* MeisterIterNext();
    // byte_620EFC — the default shop-name template (runtime/language-loaded;
    // all-zero in the cold image).  Inert: "".
    virtual const char* DefaultShopName();
    // dword_8C4790[14*type + i], i in 0..11 — per-type candidate name table.
    // Inert: nullptr (no candidate).
    virtual const char* NameTemplate(u8 type, int i);
    // dword_8C4788[14*type] — the per-type PRIMARY building name.  Inert: nullptr.
    virtual const char* PrimaryName(u8 type);
    // unk_62661C — CreateGebaeude's fallback name (cold image: "").  Inert: "".
    virtual const char* DefaultBuildingName();

    // ---- Building_CreateGebaeude (0x586fb8) leaves --------------------------
    // VIBE_GameObject_AddObjekt(parentId, proto, count, parentNode) @0x585af4 —
    // UNRECONSTRUCTED scene-graph leaf.  Inert: nullptr.
    virtual u8* AddObjekt(i32 parentId, i16 proto, i32 count, void* parentNode);
    // The 0x58736e step: QueryFind(*(rec+93), 1, 4, 2) then IterNext past
    // type-253 nodes; returns the surviving node's type word, or -1 for none.
    // Default: REAL entity GameObjectQueryFind/IterNext (typedef-byte filter 2).
    virtual i32 QueryStorageNodeTypeWord(u8* rec);
    // VIBE_Building_FindStorableObject(rec) @0x5877ac — the original stores the
    // FOUND NODE's type word into rec+41.  building4's core resolves over hook
    // handles (no node memory to read the word from), so the word itself stays a
    // hook.  Inert: -1 (none found).
    virtual i32 StorableObjectTypeWord(u8* rec);
    // VIBE_Building_InitWorkerCapacities(rec) @0x586ed8 — building4 owns the 1:1
    // core (Building4_InitWorkerCapacities) but it needs the two RESOLVED worker
    // node records (proto 42 / 278), which only a live scene query provides.
    // Inert: no-op.
    virtual void InitWorkerCapacities(u8* rec);
    // qword_13CE852 — the live game clock image (ApplyTypeDefaults' `now`).
    // Inert: zeroed PackedTime.
    virtual PackedTime Now();

    // ---- Command_ExSellObjekt (0x496b90) storage-phase leaves ---------------
    // VIBE_Building_RemoveStorageRoom(srcBuilding, proto, destOwner) @0x49739b.
    // Default: REAL Building4_RemoveStorageRoom (its child-list/slot handles are
    // Building4Hooks-domain tokens; 0 is the inert handle).
    virtual i32 RemoveStorageRoom(u8* srcBuildingRec, i16 proto,
                                  const u8* destOwnerRec);
    // VIBE_GameObject_RemoveByProt(childList, proto, destOwner) @0x49738b.
    // Default: routed through the REAL Building4Hooks surface
    // (GameObjectRemoveByProt; inert hooks make it a no-op).
    virtual i32 RemoveByProt(u8* srcChildList, i16 proto,
                             const u8* destOwnerRec);
};
void SetBuildingCallerHooks(BuildingCallerHooks* hooks);
BuildingCallerHooks* GetBuildingCallerHooks();

// ===========================================================================
// Shared helper — the 169-stride object-array name-clash scan used verbatim by
// BOTH RegisterNames (0x504b65) and CreateGebaeude (0x5871b4):
//   for (k = 0; k < 43264; k += 169) { if (base[k] && !StrCmp(name, base+k+5))
//        break; ++n; }   taken <=> n < 256
// Scans the REAL g_objects array (dword_13CE298).
// ===========================================================================
bool Building_IsNameTaken169(const char* name);

// ===========================================================================
// gilde.exe 0x504a54 — VIBE_Building_RegisterNames (full flow).
// Pass 1: stamp the default template (byte_620EFC) into the name field (+5) of
// every queried record whose type-table kind byte != 28.  Pass 2: re-stamp the
// default, build the 12 candidates from dword_8C4790[14*type+i], then run the
// recon pick core Building_PickName (taken-scan / len>=0x20 filter / uniform
// RandomModulo pick) and store the survivor into the record's +5 name.
// (The chosen-too-long ErrorLog branch @0x504cc2 is dead: survivors are already
// length-filtered at collection; Building_PickName mirrors the recheck.)
// ===========================================================================
void Building_RegisterNames();

// ===========================================================================
// gilde.exe 0x504ce0 — VIBE_Scene_SyncMeisterBuildings (full flow).
//   * zero the anchor globals; SwitchActiveSlot.
//   * if !(flags & 0x40): RecalcAllProduction + the 4-slot ComputeSlotStats
//     do/while over the live city table.
//   * scan g_persons (stride 536, 768 records): kind byte(+2)==12 picks the two
//     anchors by sub-state byte(+12) 0/1 — LAST-match-wins until both found
//     (the binary overwrites; it only stops early once v5==3).  Second scan
//     collects every kind==11 record into the shop list (slots [1..]).
//   * publish anchors; if !(flags & 0x40) && !(flags & 4): Building_RegisterNames.
//   * tail: SyncMasterShopObjects(anchorA).
// NOTE: cutscene_misc5's SceneClassifyMeisterRecords kernel implements the same
// scan (overwrite-until-both-found, fixed in fixups wave 2 — see
// progress/fixups-wave2.md); the scan here remains the verbatim record-pointer
// translation over the live g_persons array.
// ===========================================================================
void Scene_SyncMeisterBuildings();

// ===========================================================================
// gilde.exe 0x586fb8 — VIBE_Building_CreateGebaeude (full flow).
//   guard dword_13CE294; FindFreeSlot (1:1 g_objects scan); stamp the fixed
//   scalar field block (all 20 stores recovered from the disasm); grey-fill the
//   16-byte light block @+153 (|=1) and zero +101..148; pick the name (primary
//   or fallback, then the candidate de-dup + RandNext%count via the REAL
//   Building_PickUniqueName); walk the type record's +35 room-slot words —
//   hi-bit (0x8000) slots of object-type 2/6 go through the REAL
//   Building_AllocStorageRoom (0x588988), other hi-bit slots through AddObjekt
//   while no 2/6 slot was seen; resolve the storage node word (+41); apply the
//   per-type defaults via the REAL Building_ApplyTypeDefaults; the prot 20/22
//   LABEL_71 AddObjekt(437) + field stamp; tail InitWorkerCapacities /
//   MapKindToCategory / Building4_EnsureDefaultObjects.
// Returns the new record base, or nullptr (table unloaded / array full).
// ===========================================================================
u8* Building_CreateGebaeudeFlow(u8 prot, u16 ownerWord);

// ===========================================================================
// gilde.exe 0x496b90 — VIBE_Command_ExSellObjekt: the two STORAGE-NODE phases
// (the pieces trade_sell.cpp had deferred).  Both run inside the commit path of
// TradeSellObjektResolve once WireBuildingCallers() installs them.
// Source phase (0x496ec2..0x496f47 head): decrement the source stock node's
//   count (+14); when it drops <= 0 remove the node — object-type 2/6 through
//   RemoveStorageRoom (needs the resolved source building, else reject), other
//   types through RemoveByProt.  Returns false on the original's `return 1`.
// Dest phase (0x496f4b..0x4974ce + LABEL_58): when no dest stock node exists —
//   object-type 2/6 allocates through the REAL Building_AllocStorageRoom
//   (reject when absent/failed), other types AddObjekt (+ the v57 count bump
//   and the kind-23/37 x proto-42/278 x owner-kind-6/7 "+28 = 4" stamp).  An
//   existing node gains qty at +14.  Latches *(node+2) into g_lastTradeId
//   (dword_631290) and r.lastDestNodeId.
// ===========================================================================
bool Sell_DepleteSourceStockNode(SellResolve& r, i32 qty);
bool Sell_EnsureDestStorageNode(SellResolve& r, i32 qty);

// The trade_sell phase adapter (installed by WireBuildingCallers).
struct SellStoragePhase1to1 : SellStoragePhase {
    bool SourcePhase(SellResolve& r, i32 qty) override;
    bool DestPhase(SellResolve& r, i32 qty) override;
};

// ===========================================================================
// gilde.exe 0x577464 — VIBE_Bauplatz_MapAllToSupermap.
//   zero a 256-pointer frame (the grey-0 fill thunk == zero fill); collect the
//   free plots via the REAL Building_FilterBlockedBauplatze (0x50c8ec); for
//   every surviving (non-null) plot NODE call the REAL Bauplatz_MapOneToSupermap
//   with the node pointer as the plot name (scene nodes begin with their name).
// Returns the last MapOne result, or the filter count when no plot mapped
// (exactly the binary's eax residue).  Frame reads are safety-clamped to the
// 256-entry frame (the binary would read past its stack frame beyond that).
// ===========================================================================
int Bauplatz_MapAllToSupermap(i32 mapCtx);

// ===========================================================================
// Wiring (rule 13).  Installs:
//   * Building_CreateGebaeudeFlow as the Building_CreateGebaeude backend
//     (building_create.h hook) -> live callers VIBE_Command_HandleSpawnObject
//     0x496520 + VIBE_Command_ExCreateGebaeude 0x49c19c (command_apply5.cpp).
//   * SellStoragePhase1to1 as trade_sell's storage phase -> live caller
//     VIBE_Command_ExSellObjekt 0x496b90 (command_apply6.cpp, via
//     VIBE_Command_ExecCommands 0x494088).
// 0x533a54's NewGameSyncScene step sink is WIRED: app::InitOrLoadSession
// (src/app/session_init.cpp, SessionMode::NewSingle branch) dispatches the REAL
// Scene_SyncMeisterBuildings() at the NewGameSyncScene step. Remaining parents
// are documented in the progress file (0x4fffd4 via the wholesale ObjLife8Hooks
// struct; 0x503f78/0x50442c net spines; 0x576ac4 unreconstructed).
// ===========================================================================
void WireBuildingCallers();

}  // namespace guild::sim
