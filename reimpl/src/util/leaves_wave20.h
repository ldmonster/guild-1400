#pragma once
// leaves_wave20.{h,cpp} — small entry-reachable leaves recovered in wave 20.
//
// These are the genuinely-unrepresented (or divergently-modelled) leaves from the
// wave-18 coverage audit that have a clean faithful home here. The larger sibling
// leaves are already reconstructed elsewhere and are only referenced/documented:
//   VIBE_Building_EmptyCallbackStub @0x4f73ec -> sim/building5.cpp (no-op, DONE)
//   VIBE_Util_NullStub             @0x5f8224 -> sim/lasttail_recon.cpp (retn, DONE)
//   VIBE_HandleTable_ClearEntry    @0x60447c -> crt/handle_recon.cpp (DONE)
//   VIBE_Util_GetRandStatePtr      @0x5cb8b0 -> crt/rand.cpp RandStatePtr (DONE)
//   VIBE_Util_RandSeed             @0x5cb8e0 -> crt/rand.cpp RandSeed (added wave 20)
//   VIBE_Util_NullThunk            @0x5f821c -> CRT per-thread TLS getter (reads
//       lpTlsValue); the single-state RNG/errno model bypasses it, so it is a CRT-TLS
//       boundary, not reconstructed (see crt/rand.h PLAN §8). Documented, not faked.

#include "guild/common/types.h"

namespace guild::crt { class HandleTable; }

namespace guild::util {

// gilde.exe 0x5d8f1c — VIBE_ShapeAnim_GetSlot. Despite the audit name, the disasm
// shows this CLEARS slot `n`: it computes table + 17*n and calls
// VIBE_Light_SetGrayColorThunk(0, 17, ptr), i.e. memset(ptr, 0, 17). (The accessor
// modelled at this same address in render/shape_recon_cluster.cpp returns the slot
// pointer instead — a divergent interpretation; this is the faithful clear. See the
// wave-20 progress doc for the flagged handoff.)
//   table: base of the 17-byte-per-slot array (dword_1406420 in the original).
void ShapeAnimClearSlot(u8* table, int n);

// gilde.exe 0x5f8230 — VIBE_Util_ExitHandlerThunk. A bare `jmp` to
// VIBE_HandleTable_ClearEntry (0x60447c); the slot index travels in edx. Forwards
// to the reconstructed crt::HandleTable::ClearEntry. Called from
// VIBE_Resource_FreeEntryData (0x5d9282). The original targets the module-global
// handle table; since the reimpl instantiates HandleTable per caller, the table is
// passed by reference here (faithful: it is exactly `table.ClearEntry(slotIndex)`).
void ExitHandlerThunk(guild::crt::HandleTable& table, int slotIndex);

} // namespace guild::util
