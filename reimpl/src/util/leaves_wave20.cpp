// leaves_wave20.cpp — see header for provenance and the boundary/handoff notes.
#include "util/leaves_wave20.h"
#include "crt/handle_recon.h"
#include <cstring>

namespace guild::util {

// gilde.exe 0x5d8f1c — VIBE_ShapeAnim_GetSlot: memset(table + 17*n, 0, 17).
//   5d8f20 shl eax,4 / 5d8f23 add eax,edx  -> eax = 17*n
//   5d8f25 mov ebx,11h (count=17) / 5d8f2f xor edx,edx (fill byte=0)
//   5d8f2a add eax, offset table          -> dst = table + 17*n
//   5d8f31 call Light_SetGrayColorThunk(0,17,dst)  == fill 17 bytes with 0
void ShapeAnimClearSlot(u8* table, int n) {
    std::memset(table + 17 * n, 0, 17);
}

// gilde.exe 0x5f8230 — tail-jump to VIBE_HandleTable_ClearEntry(slotIndex@edx).
void ExitHandlerThunk(guild::crt::HandleTable& table, int slotIndex) {
    table.ClearEntry(slotIndex);
}

} // namespace guild::util
