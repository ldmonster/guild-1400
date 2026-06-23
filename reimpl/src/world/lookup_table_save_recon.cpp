// gilde.exe — guild::world  (MODULE: fixed-stride id lookup table)
//
// 1:1 reconstruction of VIBE_Table_FindEntrySlotById @0x4bad28. Logic transcribed
// from the disassembly:
//   mov edx, eax            ; edx = id
//   mov ecx, [dword_11B73A8]; ecx = table[0]
//   xor eax, eax            ; result(dword index) = 0
//   cmp edx, ecx
//   jz  hit                 ; id == table[0] -> hit at result 0
//   loop: add eax,10Ch      ; result += 67
//         cmp eax,4300h
//         jge done          ; result >= 4288 -> miss
//         cmp edx, [dword_11B73A8 + eax]   ; (eax is a dword index here)
//         jnz loop
//   hit:  mov [dword_11B73A8 + eax], 0     ; clear the key dword
//   done: ret  (eax = result*4)
//
// Note `eax` indexes the dword array as `dword_11B73A8[eax]` in the decompile (a
// dword index), and the function returns `result * 4` as a byte offset.
#include "world/lookup_table_save_recon.h"

namespace guild::world {

guild::u32 TableFindEntrySlotById(guild::u32* table, guild::u32 id) {
    int result = 0;
    if (id == table[0]) {
        table[result] = 0;
    } else {
        for (;;) {
            result += kLut_StrideDwords;          // += 67
            if (result >= kLut_LimitDwords)       // >= 4288 -> miss
                break;
            if (id == table[result]) {
                table[result] = 0;
                break;
            }
        }
    }
    return static_cast<guild::u32>(result) * 4u;
}

} // namespace guild::world
