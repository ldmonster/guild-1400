#include "sim/dragslot.h"

// Faithful 1:1 port of the carried-item drag-slot stacking table from
// gilde.exe (dword_75B9F0 / dword_75B9F4, 6 entries of 3 dwords each). The
// originals walk the table by raw x86 index registers and return those indices
// verbatim; we preserve the exact return conventions (see dragslot.h).
//
// The on-disk table is interleaved as a flat dword block. Each mutator scans
// the `key` column (stride 3 dwords) with the original's loop shape — the scan
// pointer steps by 3 dword indices == one record — so the located record index
// and the returned register value match the binary bit-for-bit.

namespace guild::sim {

// ---------------------------------------------------------------------------
// The canonical runtime table (gilde.exe dword_75B9F0). Single definition; the
// inline overloads in the header and the GUI route through it.
// ---------------------------------------------------------------------------
DragSlotTable& DragSlotGlobalTable() {
    static DragSlotTable g_table;
    return g_table;
}

// ===========================================================================
// VIBE_DragSlot_ResetTable  0x41f9dc
//   for (result = 0; result != 18; ...) {
//       result += 3;
//       dword_75B9E4[result] = -1;   // == key column F0 (E4 + 12)
//       dword_75B9E8[result] = 0;    // == accum column F4 (E8 + 12)
//   }
//   return result * 4;               // 18*4 == 72
// The loop writes records 0..5 (dword indices 3,6,..,18 through the E4/E8
// aliases) — i.e. each slot's key=-1, accum=0.
// ===========================================================================
int DragSlotResetTable(DragSlotTable& t) {
    int result = 0;
    do {
        result += 3;
        // dword_75B9E4[result] / dword_75B9E8[result]; the E4-alias index
        // `result` lands on key/accum of slot (result/3 - 1).
        int slot = result / 3 - 1;
        t.slots[slot].key   = -1;
        t.slots[slot].accum = 0;
    } while (result != 18);
    return result * 4;  // 72
}

// ===========================================================================
// VIBE_DragSlot_CountUsed  0x41fa00
//   for (i = 0; i != 18; i += 3) if (dword_75B9F0[i] != -1) ++v0;
//   return v0;
// ===========================================================================
int DragSlotCountUsed(const DragSlotTable& t) {
    int used = 0;
    for (int i = 0; i != 18; i += 3) {
        if (t.slots[i / 3].key != -1)
            ++used;
    }
    return used;
}

// ===========================================================================
// VIBE_DragSlot_AddItem  0x41f880  (eax=key, edx=qty)
//   v2 = key;
//   if (qty) {
//       // locate slot whose key == v2 (result == #increments == slot index)
//       result = 0; v4 = 0;
//       if (v2 != dword_75B9F0[0])
//           do { v4 += 3; ++result; } while (v4 < 18 && v2 != dword_75B9F0[v4]);
//       if (result == 6) {            // not found -> first free (key == -1)
//           result = 0; v5 = 0;
//           if (dword_75B9F0[0] != -1)
//               do { v5 += 3; ++result; } while (v5 < 18 && dword_75B9F0[v5] != -1);
//       }
//       if (result != 6) {            // have a slot
//           result *= 3;              // dword index
//           v6 = dword_75B9F4[result];
//           dword_75B9F0[result] = v2;
//           dword_75B9F4[result] = qty + v6;   // ACCUMULATE
//       }
//   }
//   return result;                    // dword index (3*slot), or 6 full / key on no-op
// ===========================================================================
int DragSlotAddItem(DragSlotTable& t, i32 key, i32 qty) {
    int result = key;  // v2 = result; on the qty==0 no-op path this is returned.
    if (qty) {
        // --- locate matching key ---
        result = 0;
        if (key != t.slots[0].key) {
            int v4 = 0;
            do {
                v4 += 3;
                ++result;
            } while (v4 < 18 && key != t.slots[v4 / 3].key);
        }
        // --- not found: locate first free slot ---
        if (result == 6) {
            result = 0;
            if (t.slots[0].key != -1) {
                int v5 = 0;
                do {
                    v5 += 3;
                    ++result;
                } while (v5 < 18 && t.slots[v5 / 3].key != -1);
            }
        }
        // --- write (accumulate) ---
        if (result != 6) {
            result *= 3;  // dword index
            int slot = result / 3;
            int prev = t.slots[slot].accum;
            t.slots[slot].key   = key;
            t.slots[slot].accum = qty + prev;
        }
    }
    return result;
}

// ===========================================================================
// VIBE_DragSlot_RemoveItem  0x41f900  (eax=key, edx=qty)
//   v4 = 0; result = 0;
//   if (key != dword_75B9F0[0])
//       do { result += 3; ++v4; } while (result < 18 && key != dword_75B9F0[result]);
//   if (v4 != 6) {                    // found
//       result = 3 * v4;
//       v6 = dword_75B9F4[3*v4] - qty;
//       dword_75B9F4[3*v4] = v6;
//       if (!v6) dword_75B9F0[3*v4] = -1;   // free when total hits 0
//   }
//   return result * 4;                // byte offset 12*slot (0 when absent)
// ===========================================================================
int DragSlotRemoveItem(DragSlotTable& t, i32 key, i32 qty) {
    int v4 = 0;
    int result = 0;
    if (key != t.slots[0].key) {
        do {
            result += 3;
            ++v4;
        } while (result < 18 && key != t.slots[result / 3].key);
    }
    if (v4 != 6) {
        result = 3 * v4;  // dword index
        int slot = v4;
        int v6 = t.slots[slot].accum - qty;
        t.slots[slot].accum = v6;
        if (!v6)
            t.slots[slot].key = -1;
    }
    return result * 4;  // byte offset
}

// ===========================================================================
// VIBE_DragSlot_StoreItem  0x41f95c  (eax=key, edx=qty)
//   v4 = 0;   // slot index (low dword); HIDWORD == byte offset (12*slot)
//   if (key != dword_75B9F0[0])
//       do { off += 12; ++v4; } while (off < 72 && key != *(F0 + off));
//   if (v4 == 6) {                    // not found -> first free
//       v4 = 0;
//       if (dword_75B9F0[0] != -1)
//           do { off += 12; ++v4; } while (off < 72 && *(F0 + off) != -1);
//   }
//   if (v4 != 6) {                    // have a slot
//       off = 12 * v4;
//       *(F0 + off) = key;
//       *(F4 + off) = qty;            // OVERWRITE
//       if (!qty) *(F0 + off) = -1;   // qty 0 -> free
//   }
//   return v4;                        // slot index (0..6)
// ===========================================================================
int DragSlotStoreItem(DragSlotTable& t, i32 key, i32 qty) {
    int v4 = 0;  // slot index
    if (key != t.slots[0].key) {
        int off = 0;
        do {
            off += 12;
            ++v4;
        } while (off < 72 && key != t.slots[off / 12].key);
    }
    if (v4 == 6) {  // not found -> first free
        v4 = 0;
        if (t.slots[0].key != -1) {
            int off = 0;
            do {
                off += 12;
                ++v4;
            } while (off < 72 && t.slots[off / 12].key != -1);
        }
    }
    if (v4 != 6) {
        t.slots[v4].key   = key;
        t.slots[v4].accum = qty;  // OVERWRITE (not accumulate)
        if (!qty)
            t.slots[v4].key = -1;
    }
    return v4;
}

}  // namespace guild::sim
