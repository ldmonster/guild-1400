#pragma once
// character_recon4_flags — 1:1 reconstruction of two Character lifecycle
// *control-logic* routines whose bookkeeping (slot iteration, state-byte
// transitions, texture-set selection conditions, delta-packet gating) is
// self-contained and faithfully translatable, while the genuine engine leaves
// they call are routed through inert hooks.
//
// Reconstructed here:
//   VIBE_Character_RefreshAllFlags 0x4b5fa4  (__usercall al = refresh(eax,ecx))
//   VIBE_Character_SyncTurnState   0x531e60  (__usercall sync(eax))
//
// PERSON TABLE (word_12CE910 @0x12CE910): an array of person/character records,
// stride 268 16-bit words == 536 bytes.  RefreshAllFlags indexes it by player
// index a1 (&word_12CE910[268*a1]); SyncTurnState reads its state byte at
// +2 (byte_12CE912) and field +372 (dword_12CEA7C/byte_12CEA74 are the same
// record region at fixed sub-offsets).  We model only the record cells these
// two functions actually touch; the wider record stays in the engine domain.
//
// No third-party tech is introduced (rule 6 N/A).
#include "guild/common/types.h"

namespace guild::sim {

using f32 = float;

// Person record stride (word_12CE910): 268 words == 536 bytes.
constexpr int kPersonRecordWords = 268;
constexpr int kPersonRecordBytes = 536;

// --- RefreshAllFlags ---------------------------------------------------------
// Inert hooks for the scene-graph / iterator leaves RefreshAllFlags drives.  The
// faithful control structure (active-slot save/restore, the two iterator loops,
// the texture-set predicate) lives in the reconstruction; effects are stubbed.
struct RefreshFlagsHooks {
    void* ctx = nullptr;
    // VIBE_Universe_SwitchActiveSlot 0x5b4a24 — set the active render slot.
    void (*switchActiveSlot)(void* ctx, int slot, int a2, int a3, int player) = nullptr;
    // VIBE_Person_QueryBegin 0x586c20 / VIBE_Person_IterNext 0x586a6c — iterate
    // the player's persons (filter 1/4).  begin() returns the first record or
    // null; next() returns the next or null.  The pointer is opaque here.
    void* (*personQueryBegin)(void* ctx, void* rec, int a2, int a3, int player) = nullptr;
    void* (*personIterNext)(void* ctx) = nullptr;
    // person record +97 (dword): the person's character/object handle (0==none).
    void* (*personObjPtr)(void* ctx, void* person) = nullptr;
    // VIBE_SceneGraph_WalkAndInvoke 0x5ac738 — walk the scene graph for the
    // person's object, invoking VIBE_Character_ShowFlag per node.
    void (*walkShowFlag)(void* ctx, void* obj, void* person) = nullptr;
    // VIBE_GameObject_QueryFind 0x5857fc / VIBE_GameObject_IterNext 0x58529c —
    // iterate the player's flag-bearing game objects (3/7/3 .. 4/29).
    void* (*gameObjectQueryFind)(void* ctx, int player) = nullptr;
    void* (*gameObjectIterNext)(void* ctx) = nullptr;
    // gameobject +59 (dword): the object's character handle (0==none).
    void* (*gameObjectCharHandle)(void* ctx, void* gobj) = nullptr;
    // VIBE_Character_IndexFromPointer 0x426724 — map a universe ptr to a slot.
    int (*indexFromPointer)(void* ctx, int universePtr) = nullptr;
    // character +136 (dword): current universe ptr (fed to indexFromPointer).
    int (*charUniversePtr)(void* ctx, void* charHandle) = nullptr;
    // VIBE_Object_SelectTextureSet 0x5b3f54 — apply a texture set to the
    // character's transport mesh (character +292 -> mesh, +460 -> material).
    void (*selectTextureSet)(void* ctx, void* charHandle, int slot, int texIndex, int player) = nullptr;
    // The active-slot value saved/restored around the refresh (dword_649D60).
    int savedActiveSlot = 0;
};

// Per-record view RefreshAllFlags inspects for the texture-set predicate.  These
// alias the person table at &word_12CE910[268*a1] (the building/transport record
// the player is iterating).  The original reads:
//   *((_DWORD*)v3 + 21)  -> +84 dword : building id (skip if == 1341)
//   *((_BYTE*)v3 + 2)    -> +2  byte  : type (texture set only for 5/6/7)
//   *((_BYTE*)v3 + 84)   -> +84 byte  : raw texture byte (texIndex = byte - 61)
struct RefreshRecordView {
    bool present = false;     // v3 != null
    u32 buildingId = 0;       // +84 dword
    u8  type = 0;             // +2  byte
    u8  textureByte = 0;      // +84 byte  (texIndex = textureByte - 61)
};

// gilde.exe 0x4b5fa4 — VIBE_Character_RefreshAllFlags(player a1, ecx a2)
// Save the active slot, switch to the player, walk persons -> ShowFlag, then
// iterate flag-bearing game objects applying the building texture set under the
// type/id predicate, and finally restore the active slot.  `view` carries the
// record cells used by the texture predicate (the engine reads them off
// &word_12CE910[268*player]).
void RefreshAllFlags(RefreshFlagsHooks& H, int player, const RefreshRecordView& view);

// --- SyncTurnState -----------------------------------------------------------
// SyncTurnState reconciles a person/building's turn state with the command
// stream.  The control logic is a state-byte switch with two delta-packet
// emission branches and a production-gauge / build-op branch.  The genuine
// command-queue and gauge leaves are inert hooks; the state predicates and the
// field bookkeeping (the +372 packet field, the +93 refcount bump/restore) are
// faithful.
struct TurnStateHooks {
    void* ctx = nullptr;
    // VIBE_Command_BeginDeltaPacket 0x493a94 — open a delta packet for a record.
    void (*beginDeltaPacket)(void* ctx, void* rec, int id) = nullptr;
    // VIBE_Command_AppendRawField 0x493c14 — append a raw field (size,count,p,off)
    void (*appendRawField)(void* ctx, unsigned size, unsigned count, const void* p, int off) = nullptr;
    // VIBE_Command_QueueRequestState22 0x494750 — flush the state-22 request.
    void (*queueRequestState22)(void* ctx) = nullptr;
    // VIBE_BuildingType_ComputeRankWithinGroup 0x58a560 — rank (1..) of a type.
    int (*computeRank)(void* ctx, int typeByte) = nullptr;
    // VIBE_He_FindFirstHandlerByFilter 0x4c63f8 — first matching handler or null.
    void* (*findFirstHandler)(void* ctx, int a, int b, int c, int d, int e) = nullptr;
    // VIBE_Building_DrawProductionGauge 0x589d78 — returns the gauge fraction.
    f32 (*drawProductionGauge)(void* ctx, int player, void* scratch) = nullptr;
    // VIBE_Command_QueueRequestSlotReset28 0x4948c8 — emit a slot-reset request.
    void (*queueRequestSlotReset28)(void* ctx, void* req, int packed) = nullptr;
    // VIBE_Command_RequestBuildOp90_Thunk 0x594c8c — emit build-op 90.
    void (*requestBuildOp90)(void* ctx, int a, int recId) = nullptr;
};

// A faithful view of the SyncTurnState inputs read off &word_12CE910[268*a1].
struct TurnStateView {
    u8  stateByte = 0;        // byte_12CE912[536*a1] (+2 of the record)
    u8  prevStateByte = 0;    // byte_12CE912[536*a1] re-read (HIBYTE branch uses same cell)
    int data372 = 0;          // dword_12CEA7C[...] (record +372): nonzero forces emit
    int recId = 0;            // record +1 word pair (id, the packet target)
    u8  buildingFlag = 0;     // byte_12CEA74[536*a1]: building-type sync active
    int rankInputByte = 0;    // HIBYTE(... +134*a1) fed to ComputeRankWithinGroup
    int ledger = 0;           // dword_12CEAA4[...] (>=0 gates the build-op branch)
    int textByte = 0;         // record +353 high byte (sprintf arg)
};

// gilde.exe 0x531e60 — VIBE_Character_SyncTurnState(player a1)
// Reproduces the state-byte switch:
//   state in {6,7} or >=10 -> if prev in {6,7}: emit a state-22 delta packet.
//   else -> if data372 || state in {1,2,4,5}: emit a state-22 delta packet; then
//           if buildingFlag: compute rank, query a handler, bump+restore +93,
//           sample the production gauge, and (when no handler, rank in 1..5,
//           gauge>=1, ledger>=0) emit a slot-reset + build-op-90.
// Effects route through TurnStateHooks; the predicates/refcount are faithful.
void SyncTurnState(TurnStateHooks& H, const TurnStateView& v);

} // namespace guild::sim
