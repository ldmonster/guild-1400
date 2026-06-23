// =============================================================================
// trace_misc_recon.cpp — VIBE_Trace cluster (see header).
// Strict 1:1 translation of the Hex-Rays decompile.
// =============================================================================
#include "util/trace_misc_recon.h"

namespace guild::util {

// gilde.exe 0x437c00 — VIBE_Trace_GetEntryByIndex
//   _DWORD *__usercall f@<eax>(int a1@<eax>, unsigned int a2@<edx>)
//   {
//     if ( a2 >= *(_DWORD *)(a1 + 4) ) return 0;
//     result = (_DWORD *)(a1 + 16);
//     for ( i = 0; i < a2; result = (_DWORD *)((char *)result + *result + 4) )
//       ++i;
//     return result;
//   }
// Entry stride is `*entry + 4` (a length-prefixed record): the first u32 of an
// entry is the trailing-bytes count, advance past it plus its own 4-byte field.
const u32* Trace_GetEntryByIndex(const u8* table, u32 index) {
    // *(_DWORD *)(a1 + 4) == entry count.
    const u32 count = *reinterpret_cast<const u32*>(table + 4); /*0x437c06*/
    if (index >= count)                                          /*0x437c06*/
        return nullptr;                                          /*0x437c1e*/
    const u8* result = table + 16;                               /*0x437c08*/
    for (u32 i = 0; i < index; ++i) {                            /*0x437c0f*/
        const u32 len = *reinterpret_cast<const u32*>(result);
        result = result + len + 4;                               /*0x437c15*/
    }
    return reinterpret_cast<const u32*>(result);                 /*0x437c1d*/
}

// gilde.exe 0x437d9c — VIBE_Trace_IsAddressReadable
//   bool __usercall f@<al>(unsigned __int8 a1@<al>, int a2@<ecx>)
//   {
//     v5 = a2;                       // dead
//     VirtualQuery(f, &Buffer, 0x1C);
//     if ( a1 >= 0x10u ) {
//       if ( a1 > 0x10u ) {
//         if ( a1 >= 0x40u ) {
//           if ( a1 > 0x40u && a1 != 0x80 ) return 0;
//         } else if ( a1 != 32 ) return 0;
//       }
//       return 1;
//     }
//     if ( a1 < 2u || a1 > 2u && a1 != 4 ) return 0;
//     return Buffer.Protect == 2 || Buffer.Protect == 4;
//   }
bool Trace_IsAddressReadable(u8 protectClass, u32 pageProtect) {
    const u8 a1 = protectClass;
    if (a1 >= 0x10u) {                                   /*0x437dba*/
        if (a1 > 0x10u) {                                /*0x437dd5*/
            if (a1 >= 0x40u) {                           /*0x437dda*/
                if (a1 > 0x40u && a1 != 0x80)            /*0x437de5*/
                    return false;                        /*0x437de8*/
            } else if (a1 != 32) {                       /*0x437ddf*/
                return false;                            /*0x437dec*/
            }
        }
        return true;                                     /*0x437dd4*/
    }
    if (a1 < 2u || (a1 > 2u && a1 != 4))                 /*0x437df1*/
        return false;                                    /*0x437df1*/
    return pageProtect == 2 || pageProtect == 4;         /*0x437dd3*/
}

// gilde.exe 0x437c24 — VIBE_Trace_WriteSymbolLine — OMITTED here.
//   char __usercall f@<al>(int *a1@<eax>)
// This is the per-frame line emitter of the crash tracer.  Its body is a 1:1
// record-walk + collapse/skip state machine, but every observable side effect is
// a Win32 WriteFile() to the trace file handle held in a1[1] (it also calls
// VIBE_Trace_GetEntryByIndex above).  Emitting it faithfully requires the Win32
// file boundary (project rule 4 — Win32 -> SDL) AND the live tracer context
// struct, which are not part of this leftover cluster.  Translating only the
// control flow while faking the I/O would violate rule 8 (no cheap analogues),
// so it is deferred to the Trace/IFileSystem integration rather than stubbed
// here.  GetEntryByIndex (its pure callee) IS reconstructed above.

} // namespace guild::util
