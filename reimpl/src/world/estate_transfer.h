#pragma once
// guild::world — estate_transfer: 1:1 reconstruction of the estate/ownership
// transfer mutation from gilde.exe.
//
//   gilde.exe 0x58c4a8 — VIBE_Person_TransferEstateOwnership
//     (__usercall: ax=ret, eax=fromId@a1, edx=toRec@a2, edi=mode@a3)
//
// This is the "person dies / abdicates and their whole estate passes to another
// person" mutation. It is invoked from the command/apply path (the estate-
// transfer command, modeled in command_apply2.h as EstateTransferFn @0x58c4a8)
// and from the office privilege actions. It performs, in order (verbatim from the
// decompile):
//
//   1. Resolve the FROM person record by id (return -1 if not found) and run the
//      criminal-record init pass over it (VIBE_StraftatTable_FindAndInit).
//   2. Walk the 256-slot building array (dword_13CE298, stride 169): for every
//      live building of category 2 (a residence/estate) whose owner word (+39)
//      equals the FROM person's marker, re-parent it under the focus record
//      (dword_6498E4).  [VIBE_Building_SetObjectParent]
//   3. Snapshot the FROM person's total wealth (VIBE_Person_ComputeTotalWealth).
//   4. Resolve the TO person record by pointer-id (return -2 if not found).
//   5. Refresh the FROM person's entity handlers and cancel both persons' pending
//      character actions.
//   6. Copy a 0x218-byte working image of the TO record, splice in selected FROM
//      fields (marker/kind/byte13/relation+0x170), reconcile the office-rank /
//      family-head byte depending on building-group rank, and a second 0x218-byte
//      working image of the FROM record with the TO marker spliced in.
//   7. Commit both working images back into the person array (word_12CE910), relink
//      the per-person object lists (dword_12CEA88), re-point every relation slot in
//      the whole 0x300x8 relation matrix (dword_12CE96C) that referenced FROM/TO,
//      move the FROM person's "category 9" inventory objects to the TO container,
//      stamp the wealth/jail bookkeeping (dword_12CEAD8/AB8/AA4), and finally
//      remove+clean the spliced building record.
//   8. Return the FROM person's slot index (v41 = (u16)fromMarker).
//
// Because the heavy leaves (building re-parent, object-list relink, inventory
// move, character-action cancel, building remove) live in other modules, they are
// routed through an installable EstateTransferHooks struct with inert defaults so
// the mutation is observable headless and golden-pinnable on synthetic states.
// The DETERMINISTIC record splices + the relation-matrix re-point + the building
// scan are reconstructed in full here against the real Person/Object arrays.

#include "guild/common/types.h"
#include "sim/types.h"

namespace guild::world {

// ---------------------------------------------------------------------------
// Cross-module leaf hooks (return small codes / perform side effects elsewhere).
// Inert defaults make every leaf a no-op so the pure mutation can be tested.
// ---------------------------------------------------------------------------
struct EstateTransferHooks {
    // VIBE_StraftatTable_FindAndInit @0x5384a0 — criminal-record init over a record.
    void (*straftatFindAndInit)(guild::sim::Person* fromRec);
    // VIBE_Building_MapTypeToCategory @0x5878b0 — building type byte -> category.
    int  (*buildingMapTypeToCategory)(guild::u8 typeByte);
    // VIBE_Building_SetObjectParent @0x58820c — re-parent building under focus.
    void (*buildingSetObjectParent)(guild::sim::ObjectRec* bld, guild::u16 focusMarker,
                                    guild::u16 focusId, int mode);
    // VIBE_Person_ComputeTotalWealth @0x591f7c — FROM person's snapshot wealth.
    int  (*computeTotalWealth)(guild::u16 fromMarker, const guild::sim::Person* fromRec);
    // VIBE_He_RefreshEntityHandlers @0x4c6e0c — refresh FROM person's handlers.
    void (*heRefreshEntityHandlers)(guild::sim::Person* fromRec);
    // VIBE_CharAction_CancelEntityActions @0x4dc074 — cancel pending actions.
    void (*charActionCancel)(guild::sim::Person* rec);
    // VIBE_Office_ReleaseCharacterHoldings @0x47ed68 — drop FROM person's offices.
    void (*officeReleaseHoldings)(guild::sim::Person* fromRec);
    // VIBE_BuildingType_GroupFromCode @0x58a4c8 — building group from code byte.
    int  (*buildingTypeGroupFromCode)(guild::u8 code);
    // VIBE_BuildingType_ComputeRankWithinGroup @0x58a560 — rank within group.
    int  (*buildingTypeRankWithinGroup)(guild::u8 code);
    // VIBE_Building_GetCategoryForObject @0x589d24 — category byte for a marker.
    guild::u8 (*buildingGetCategoryForObject)(guild::i16 marker);
    // VIBE_GameObject_AddObjektToParent @0x5862a4 — move an inventory object.
    void (*gameObjectAddToParent)(int container, guild::u16 proto, int amount,
                                  guild::sim::Person* toRec);
    // VIBE_Building_RemoveAndCleanup @0x5894b0 — remove the spliced building.
    void (*buildingRemoveAndCleanup)(guild::i16 marker, bool dropFlag);

    void* ctx;
};
void EstateTransferSetHooks(const EstateTransferHooks& h);
void EstateTransferResetHooks();
const EstateTransferHooks& EstateTransferGetHooks();

// gilde.exe 0x58c4a8 — VIBE_Person_TransferEstateOwnership.
//   fromId : the id of the person whose estate is being transferred (eax).
//   toRec  : the id of the receiving person (edx; the original passes a "record
//            pointer cast to int" that is really used as an id in FindRecordById).
//   mode   : the re-parent / removal mode flag (edi, forwarded to leaves).
// Returns:
//   -1  if the FROM person id resolves to no record;
//   -2  if the TO person id resolves to no record;
//   else the FROM person's slot index (u16 of the FROM marker) on success.
int PersonTransferEstateOwnership(guild::i32 fromId, guild::i32 toRec, int mode);

} // namespace guild::world
