#pragma once
// ===========================================================================
// dragslot.{h,cpp} — the carried-item DRAG-SLOT stacking table (gilde.exe)
// ===========================================================================
// MODULE: the player/character carried-item grid (namespace guild::sim).
//
// When the player drags goods between an inventory/stall grid and the carry
// bar, the binary accumulates the dragged (prototype, quantity) pairs into a
// fixed 6-entry "drag-slot" table. Each entry is a 3-dword (12-byte) record in
// the BSS block based at dword_75B9F0:
//
//      slot[i] @ dword_75B9F0 + 12*i :
//        +0  key   (dword)  item prototype id        (-1 == free slot)
//        +4  accum (dword)  accumulated quantity
//        +8  gfx   (dword)  icon/widget rider (set elsewhere; NOT touched here)
//
//   word_75B9F4 == dword_75B9F0 + 4 is the `accum` column; the ResetTable
//   variant addresses the same two columns one record earlier through the
//   dword_75B9E4 / dword_75B9E8 aliases (E4+12 == F0), confirming the 6-slot,
//   12-byte-stride geometry.
//
// The four mutators are pure table walks (the icon/widget side is the GUI's
// DragSlotSink edge, modeled separately in gui/trade_item_panel). They stack by
// key: AddItem ACCUMULATES into a matching-or-free slot; StoreItem OVERWRITES;
// RemoveItem subtracts and frees the slot when the running total hits zero.
//
// FIDELITY NOTE — the originals return the raw x86 index registers:
//   * AddItem    returns the DWORD index (3*slot, or 6 when full / on qty==0
//                no-op it returns the untouched key register).
//   * RemoveItem returns the BYTE offset (3*slot*4 == 12*slot).
//   * StoreItem  returns the SLOT index (0..6; 6 == table full).
// We preserve these exact return values; callers compare against 6 / use the
// offset to index sibling widget tables.
//
// Translated functions:
//   VIBE_DragSlot_AddItem      0x41f880
//   VIBE_DragSlot_RemoveItem   0x41f900
//   VIBE_DragSlot_StoreItem    0x41f95c
//   VIBE_DragSlot_ResetTable   0x41f9dc
//   VIBE_DragSlot_CountUsed    0x41fa00
#include "guild/common/types.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// One drag-slot record (the 3-dword / 12-byte original blob, named). `gfx` is
// the icon rider the GUI sets; the stacking mutators never read or write it.
// ---------------------------------------------------------------------------
struct DragSlot {
    i32 key   = -1;   // +0  item prototype id (-1 == free)
    i32 accum = 0;    // +4  accumulated quantity
    i32 gfx   = 0;    // +8  icon/widget rider (untouched by the mutators)
};

// The 6-entry carried-item drag table (BSS block at dword_75B9F0). Owned here;
// the GUI reaches it through this canonical instance.
inline constexpr int kDragSlotCount = 6;     // table at F0, 18 dwords = 6*3

struct DragSlotTable {
    DragSlot slots[kDragSlotCount];
};

// The canonical runtime table (gilde.exe dword_75B9F0). One definition.
DragSlotTable& DragSlotGlobalTable();

// gilde.exe 0x41f9dc — VIBE_DragSlot_ResetTable. Clears every slot to
// key=-1, accum=0 (the gfx rider is left as-is, matching the original which
// only writes the two columns). Returns 72 (the original's result*4 with
// result==18 at loop exit).
int DragSlotResetTable(DragSlotTable& t);

// gilde.exe 0x41fa00 — VIBE_DragSlot_CountUsed. Number of occupied slots
// (key != -1).
int DragSlotCountUsed(const DragSlotTable& t);

// gilde.exe 0x41f880 — VIBE_DragSlot_AddItem (eax=key, edx=qty). ACCUMULATE
// `qty` into the slot whose key == `key` (or the first free slot if absent).
//   * qty == 0      -> no-op; returns `key` (the original returns the EAX it
//                      entered with, untouched).
//   * table full    -> no write; returns 6.
//   * otherwise     -> slot.accum += qty; slot.key = key;
//                      returns the DWORD index 3*slot.
int DragSlotAddItem(DragSlotTable& t, i32 key, i32 qty);

// gilde.exe 0x41f900 — VIBE_DragSlot_RemoveItem (eax=key, edx=qty). Subtract
// `qty` from the slot whose key == `key`; free the slot (key=-1) when its
// running total reaches exactly 0. No-op if `key` is absent. Returns the BYTE
// offset (3*slot*4 == 12*slot) of the located slot. When `key` is absent the
// linear scan runs the full table and returns 72 (the past-the-end index*4),
// except when slot 0 already holds `key`, where it returns 0.
int DragSlotRemoveItem(DragSlotTable& t, i32 key, i32 qty);

// gilde.exe 0x41f95c — VIBE_DragSlot_StoreItem (eax=key, edx=qty). OVERWRITE
// the slot whose key == `key` (or the first free slot) with (key, accum=qty).
// If qty == 0 the slot is freed (key=-1). Returns the SLOT index (0..6; 6 ==
// table full).
int DragSlotStoreItem(DragSlotTable& t, i32 key, i32 qty);

// ---------------------------------------------------------------------------
// Convenience overloads operating on the canonical global table (the form the
// binary's call sites use — they pass no table, the globals are implicit).
// ---------------------------------------------------------------------------
inline int DragSlotResetTable()                    { return DragSlotResetTable(DragSlotGlobalTable()); }
inline int DragSlotCountUsed()                     { return DragSlotCountUsed(DragSlotGlobalTable()); }
inline int DragSlotAddItem(i32 key, i32 qty)       { return DragSlotAddItem(DragSlotGlobalTable(), key, qty); }
inline int DragSlotRemoveItem(i32 key, i32 qty)    { return DragSlotRemoveItem(DragSlotGlobalTable(), key, qty); }
inline int DragSlotStoreItem(i32 key, i32 qty)     { return DragSlotStoreItem(DragSlotGlobalTable(), key, qty); }

}  // namespace guild::sim
