#pragma once
// =============================================================================
// trace_misc_recon — small leftover VIBE_Trace cluster (crash/symbol tracer).
//
// gilde.exe addresses:
//   0x437c00  VIBE_Trace_GetEntryByIndex   (__usercall eax = (eax,edx))
//   0x437c24  VIBE_Trace_WriteSymbolLine   (__usercall al  = (eax))
//   0x437d9c  VIBE_Trace_IsAddressReadable (__usercall al  = (al,ecx))
//
// These are the debug stack-walk/symbol-dump helpers that emit one line of a
// crash trace.  The pointer/index walk math is 1:1.  The two OS-coupled pieces
// (WriteFile via the Win32 file handle in WriteSymbolLine, and the VirtualQuery
// page-protection probe in IsAddressReadable) are routed through inert function
// hooks declared here so the portable build links with NO Win32 dependency
// (project rule 4 — Win32 -> SDL; here the tracer needs no real backend in the
// headless build, so the hooks default to inert behavior).
// =============================================================================
#include "guild/common/types.h"

namespace guild::util {

// ---- Symbol table layout (in the original, the table is a flat blob) --------
// gilde.exe — the "module" record consulted by GetEntryByIndex:
//   +0x00  flags / unused
//   +0x04  u32 entryCount
//   +0x08  ...
//   +0x10  first variable-length entry:
//            +0x00 u32 nameLen     (line walks `ptr + *ptr + 4`)
//            +0x04 bytes[nameLen]  name characters
// GetEntryByIndex returns the address of the index-th entry, or null.
//
// gilde.exe 0x437c00 — VIBE_Trace_GetEntryByIndex.
const u32* Trace_GetEntryByIndex(const u8* table, u32 index);

// gilde.exe 0x437d9c — VIBE_Trace_IsAddressReadable.
// Classifies a page-protection constant (a1 = al).  The original always calls
//   VirtualQuery(&VIBE_Trace_IsAddressReadable, &mbi, 0x1C)
// — i.e. it probes the protection of *its own* code page — and in the low tier
// (protectClass < 0x10) returns mbi.Protect==2 (READONLY) || mbi.Protect==4
// (READWRITE).  The `ecx` argument is written to a local and never read (dead).
// `pageProtect` supplies that probed protection (the VirtualQuery result) so the
// classification stays 1:1; the OS probe itself is the Win32 boundary.
//   PAGE_* low-tier accepted values: 2 == PAGE_READONLY, 4 == PAGE_READWRITE.
bool Trace_IsAddressReadable(u8 protectClass, u32 pageProtect);

} // namespace guild::util
