#pragma once
// ===========================================================================
// hud_recon_selaction — gilde.exe 0x54e7b4  VIBE_Hud_BuildSelectedObjectAction
// ===========================================================================
// In-game HUD "act on the selected object" dispatcher. When the player has a HUD
// object selected (dword_631724 holds the active selection list) this resolves the
// object's amt slot, and EITHER:
//   * (free-act path) iterates the live entity array byte_12CEA98 (411648 bytes,
//     stride 536) and, for every active entity, queues a per-entity craft/favor
//     command (VIBE_Command_QueueRequestSlotReset28) and plays a voice comment, OR
//   * (info path) sets the HUD tooltip text — either a generic "no info" string
//     (slot rank<=1) or a sprintf'd resource-name string keyed by the slot's
//     (field+8 >> 16) index into the resource-name table dword_8C584C (stride 2).
//
// PURE LOGIC reconstructed 1:1 here:
//   * the GATE predicate that selects free-act vs info-tooltip vs nothing, and
//   * the info-path branch (rank<=1 -> generic; else -> resource-name index), and
//   * the entity-array stride iteration (count of active entities).
// COUPLED leaves (the amt slot table, the live entity bytes, the command queue,
// the voice bank, the tooltip widget, the sprintf) are expressed as inert-default
// hooks so the predicate/iteration math is exercised without faking engine state.
//
// All additive: new file, namespace guild::play, no edits to existing units.

#include "guild/common/types.h"

namespace guild {
namespace play {

using namespace guild;

// ---------------------------------------------------------------------------
// Coupled-leaf inputs (recovered from the decompile's field offsets).
// ---------------------------------------------------------------------------
struct SelActionSlot {
    bool valid;       // SlotByObjectId != 0  (VIBE_Amt_FindSlotByObjectId result)
    i8   rank;        // *(char*)(slot+13)   — gates info text and free-act path
    i32  field0;      // *(_DWORD*)slot      — v18 (entity owner id)
    i32  field8;      // *(int*)(slot+8)     — high 16 bits index the resource name
};

struct SelActionInput {
    bool selectionActive;   // dword_631724 != 0
    bool buildModeActive;   // dword_6317B0  (free-act gate)
    bool buildOverride;     // dword_63C7B8  (forces free-act even when rank<=1)
    bool entityArrayReady;  // dword_67221C  (free-act gate)
    SelActionSlot slot;     // resolved amt slot
};

// The original's entity array: byte_12CEA98, 411648 bytes, stride 536.
inline constexpr int kSelActionEntityArrayBytes = 411648;
inline constexpr int kSelActionEntityStride     = 536;
inline constexpr int kSelActionEntityCount =
    kSelActionEntityArrayBytes / kSelActionEntityStride; // 768

// Result classification of the dispatch.
enum class SelActionKind {
    kNothing,        // no slot, or nothing to do
    kFreeAct,        // iterate entities, queue per-entity commands + voice
    kTooltipGeneric, // rank<=1: VIBE_Widget_SetTooltipText(dword_8C38E0)
    kTooltipNamed,   // rank>1 : sprintf resource-name then SetTooltipText
};

struct SelActionResult {
    SelActionKind kind = SelActionKind::kNothing;
    int  resourceNameIndex = -1; // dword_8C584C index = (slot.field8 >> 16); stride 2
    int  freeActEntityVisits = 0; // # of active entities the free-act loop touched
};

// gilde.exe 0x54e7b4 — pure dispatch decision.
// `entityActive(i)` reports whether byte_12CEA98[i*536] is non-zero (the loop tests
// byte_12CEA98[i] where i steps by 536, i.e. the first byte of each 536-byte record).
SelActionResult Hud_BuildSelectedObjectAction(
    const SelActionInput& in,
    bool (*entityActive)(int entityIndex) = nullptr);

// Resource-name index helper (exposed for tests): high 16 bits of slot.field8.
inline int SelAction_ResourceNameIndex(i32 field8) {
    return field8 >> 16; // *(int*)(slot+8) >> 16 ; table stride is 2 dwords
}

// ---------------------------------------------------------------------------
// Free-act command hook (coupled leaf). Each active entity in the original queues
// VIBE_Command_QueueRequestSlotReset28 with the selected object's owner + a craft
// favor payload, then VIBE_Voice_PlayCraftFavorComment. We surface a hook so a wired
// build can route the real command; the default is inert.
// ---------------------------------------------------------------------------
using SelActionFreeActHook = void (*)(int entityIndex, i32 ownerId, void* ctx);
void Hud_SetSelActionFreeActHook(SelActionFreeActHook hook, void* ctx);

// Tooltip hook (coupled leaf). kind+text routed to VIBE_Widget_SetTooltipText.
using SelActionTooltipHook = void (*)(SelActionKind kind, int resourceNameIndex,
                                      void* ctx);
void Hud_SetSelActionTooltipHook(SelActionTooltipHook hook, void* ctx);

} // namespace play
} // namespace guild
