// gilde.exe 0x5384d0 — VIBE_GesetzTable_FindByKey
//
// Pure table-scan accessor over the shared 24-byte descriptor table. The table
// storage (g_eventTable / g_eventTableCount) lives in event.cpp; we reuse it via
// extern declarations rather than redefining it (ODR).

#include "gesetztable_law_recon.h"

#include "event.h"  // EventDesc, g_eventTable, g_eventTableCount

namespace guild::world {

// gilde.exe 0x5384d0
//
// Original (Hex-Rays):
//   if ( dword_5383F0 <= 0 ) return 0;                 // empty table
//   v1 = 0;                                            // byte offset into image
//   while ( byte_63CD4C[v1] != a1 ) {                  // compare value byte (+4)
//     v1 += 24;                                        // advance one record
//     if ( v1 >= 24 * dword_5383F0 ) return 0;         // off the end
//   }
//   return &byte_63CD4C[v1];                           // ptr to matching value byte
//
// byte_63CD4C == &g_eventTable[0].value (record base + 4). Walking v1 by 24 visits
// g_eventTable[v1/24].value. We reproduce the integer index walk exactly.
u8* GesetzTableFindByKey(u8 key) {
    if (g_eventTableCount <= 0)           // dword_5383F0 <= 0
        return nullptr;
    int i = 0;                            // record index (v1 / 24)
    while (g_eventTable[i].value != key) {
        ++i;                              // v1 += 24
        if (i >= g_eventTableCount)       // v1 >= 24 * dword_5383F0
            return nullptr;
    }
    return &g_eventTable[i].value;        // &byte_63CD4C[v1]
}

int GesetzTableFindIndexByKey(u8 key) {
    if (g_eventTableCount <= 0)
        return -1;
    int i = 0;
    while (g_eventTable[i].value != key) {
        ++i;
        if (i >= g_eventTableCount)
            return -1;
    }
    return i;
}

} // namespace guild::world
