#pragma once
#include "guild/common/types.h"

// gamelogic_recon5_resolve_stat — the two stat-rendering ResolveTarget handlers
// (namespace guild::sim). Siblings of the command_recon4_resolve family; they are
// dispatched from the same history/console target-resolution table, but unlike
// the pick/scan resolvers they compute a *numeric stat* (a wealth / quantity
// scaled by a parsed-percent factor) and render it.
//
//   0x4fa290 VIBE_Command_ResolveTargetSelectedStat  — confirm a previously
//            chosen person id (params[index]), reject unless it is an active
//            person record, then ComputeTotalWealth and scale by
//            ParseInt(name-tail) * 0.01 * wealth, convert to a display coord and
//            render. kind must be 2; index must not be 8.
//
//   0x4fa3bc VIBE_Command_ResolveTargetBuildingStat  — find the building object
//            tied to the current person (QueryByGoodType -> QueryFind), iterate
//            its child objects for the one whose category byte == 9, take its
//            quantity field, then scale by ParseInt(name-tail) * 0.01 * quantity
//            and render. No confirm/kind gating (this is a __thiscall variant).
//
// The leaves (FindRecordById, the 768-slot person selection table, the building
// object query/iterator graph, ComputeTotalWealth, ParseInt/StripNameTokens, the
// coord/money display conversion, RenderFormattedMessage) live in other modules
// and in live game state, so they are routed through an installable hooks struct
// with inert defaults. The GATING, the exact float op-order
// (`(double)parsed * 0.01f * (double)stat`), the truncation to int, and the
// child-object scan/back-write ARE reconstructed 1:1.

namespace guild::sim {

using f32 = float;

// ---------------------------------------------------------------------------
// SelectedStat record view (the +0 word / +8 alive / +2 kind fields of the
// 536-byte person selection record, identical to command_recon4_resolve's
// Recon4Record but kept local to avoid cross-module coupling).
// ---------------------------------------------------------------------------
struct Recon5StatHooks {
    // SelectedStat path -----------------------------------------------------
    // VIBE_Person_FindRecordById(id) -> opaque record base (or null). The byte at
    // +0 of the record is the table slot index (word_12CE910 selector).
    void* (*findRecordById)(i32 id) = nullptr;
    // word_12CE910[268*slot]: the record's type word (-1 == empty). Indexed by the
    // slot returned in *RecordById.
    i16  (*tableTypeWord)(u16 slot) = nullptr;
    // byte_12CE918[536*slot]: alive flag. byte_12CE912[536*slot]: kind byte.
    u8   (*tableAlive)(u16 slot) = nullptr;
    u8   (*tableKind)(u16 slot) = nullptr;
    // The slot index stored in *RecordById (first word of the FindRecordById
    // record). Default 0xFFFF -> treated as empty.
    u16  (*recordSlot)(void* rec) = nullptr;
    // VIBE_Person_ComputeTotalWealth(slot, scratch) -> wealth score.
    i32  (*computeTotalWealth)(u16 slot) = nullptr;

    // BuildingStat path -----------------------------------------------------
    // VIBE_Person_QueryByGoodType(1, name) -> opaque person object (or null).
    void* (*queryByGoodType)(const char* name) = nullptr;
    // *(person+93): the object id used as the QueryFind anchor.
    i32  (*personAnchorId)(void* person) = nullptr;
    // VIBE_GameObject_QueryFind(anchorId, 2, 6, ?, 277) -> opaque building (null).
    void* (*queryFindBuilding)(i32 anchorId) = nullptr;
    // *(building+20): child-iteration anchor id.
    i32  (*buildingChildAnchor)(void* building) = nullptr;
    // Child object iterator over (childAnchor, 1, 5): returns child handles; the
    // host yields `iterCount` children indexed 0..iterCount-1, then null.
    // For each child we read its category byte and quantity:
    //   category == *(dword_13CE27C + 65 * childWord)  (== 9 selects it)
    //   quantity == *(child + 7 words) == *((i32*)child + 7)   (v17 = *(i+7))
    int  (*buildingChildCount)(void* building) = nullptr;
    u8   (*childCategory)(void* building, int childIndex) = nullptr;
    i32  (*childQuantity)(void* building, int childIndex) = nullptr;

    // Shared leaves ---------------------------------------------------------
    // StripNameTokens folds '%' codes into the scratch fmt and reports how many
    // bytes were consumed; ParseInt parses the name tail. We collapse to one hook
    // returning the parsed integer applied to the stat.
    i32  (*parseStatPercent)(const char* name) = nullptr;
    // VIBE_Money_ConvertToDisplayCoord(value, byte_6477A1) — the display coord
    // conversion. byte_6477A1 is a global render flag (image value 0). We pass the
    // truncated stat value and the flag; the host returns the rendered coord.
    i32  (*convertToDisplayCoord)(i32 value, u8 flag) = nullptr;
    // VIBE_Text_RenderFormattedMessage(out, fmt, coord, flag). We record the
    // truncated stat value (the load-bearing computed quantity); out/fmt opaque.
    void (*renderMessage)(char* out, const char* fmt, i32 coord, i32 flag) = nullptr;
};

void SetRecon5StatHooks(const Recon5StatHooks* h);
const Recon5StatHooks& GetRecon5StatHooks();

// gilde.exe 0x4fa290 — VIBE_Command_ResolveTargetSelectedStat
// (kind a1@al, params a2@edx, name a3@ecx, index a4@ebx, out a5)
int ResolveTargetSelectedStat(u8 kind, u8* params, const char* name, int index, char* out);

// gilde.exe 0x4fa3bc — VIBE_Command_ResolveTargetBuildingStat
// (__thiscall: this == name, a2 == out). Returns 1 on success, 0/err otherwise.
int ResolveTargetBuildingStat(const char* name, char* out);

} // namespace guild::sim
