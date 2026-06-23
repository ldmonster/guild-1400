#pragma once
// render/coord_transform_leaf.{h,cpp} — the tiny widget-coordinate add leaf
// (gilde.exe 0x5d8b00 — VIBE_Coord_Transform) plus its sibling resource-flush
// leaf (gilde.exe 0x5d9104 — VIBE_Resource_FlushAndFree).
//
// VIBE_Coord_Transform is NOT a float->int projection (despite the "Coord" name);
// it is a pointer/struct walk used pervasively by the widget scroll/slider/object
// layout code (xrefs: Widget_DrawScrollThumb/Bar 0x40ecb0/0x4121c4, Object_Update
// 0x40eea0, Slider_ComputeThumbPos 0x41069c, Entity_InteractionLogic 0x41078c,
// GameLogic_Objects 0x412fa0, Property_Get 0x4152cc, Window_LayoutScrollContent
// 0x41536c, Property_Set 0x4159dc, Animation_Apply 0x415b78, Object_RecomputeSize
// 0x41b164, Shape_LoadAndRegister 0x41f5e0, Slider_UpdateFromMouse 0x420a04).
//
// Disassembly (gilde.exe 0x5d8b00, __usercall eax=result, dx=a2):
//   test eax, eax
//   jnz  short loc_5D8B05
//   retn                              ; result == 0  -> return 0 unchanged
//   loc_5D8B05:
//   and  edx, 0FFFFh                  ; a2 is a 16-bit unsigned index
//   add  eax, [eax+edx*4+45h]         ; result += *(i32*)(result + 4*a2 + 0x45)
//   retn
//
// So: given a non-null base pointer `result` and a 16-bit index `a2`, it reads the
// dword field at byte offset 0x45 + 4*a2 inside the record `result` points at and
// adds it to the pointer, returning the advanced pointer. When `result` is 0 it
// returns 0 unchanged (the null guard). It is byte-faithfully a pointer-relative
// field add with a zero-passthrough.

#include "guild/common/types.h"

namespace guild::render {

using guild::i32;
using guild::u16;

// gilde.exe 0x5d8b00 — VIBE_Coord_Transform(result@eax, a2@dx).
//
// Reads the i32 at byte offset (0x45 + 4*(a2 & 0xFFFF)) from the record `base`
// addresses and returns base + that value, as an integer (the original works on a
// raw 32-bit pointer/integer in eax). When `base == 0` returns 0 unchanged.
//
// `mem` must point at a byte array such that `base` is an offset into it (the
// engine works on flat 32-bit pointers; here we take the base as an integer and the
// backing memory explicitly so the field read is testable without raw pointers).
// The field is read little-endian, matching the x86 `mov`/`add`.
i32 Coord_Transform(i32 base, u16 index, const unsigned char* mem);

// Raw-pointer overload matching the original exactly: `base` is treated as a real
// pointer; the field at base+0x45+4*index is added. Provided for the live engine
// wiring (the callers pass real record pointers). Null base -> 0.
i32 Coord_Transform_Ptr(i32 base, u16 index);

// The fixed field-base offset (0x45) and stride (4) the leaf bakes in.
inline constexpr i32 kCoordFieldBase   = 0x45;  // add eax, [eax+edx*4+45h]
inline constexpr i32 kCoordFieldStride = 4;     // edx*4

// ---------------------------------------------------------------------------
// gilde.exe 0x5d9104 — VIBE_Resource_FlushAndFree(a1@eax, a2@edx).
//
// Disassembly:
//   push ebx
//   mov  ebx, eax                     ; save a1
//   call VIBE_Resource_FreeEntryData  ; (eax=a1, edx=a2)  -> eax = entry result
//   mov  edx, eax                     ; preserve the FreeEntryData result in edx
//   mov  eax, ebx                     ; a1
//   call VIBE_File_FreeStream         ; (eax=a1)          -> clobbers eax
//   mov  eax, edx                     ; RETURN the FreeEntryData result, not stream
//   pop  ebx
//   retn
//
// It frees the resource entry's buffered data (Resource_FreeEntryData @0x5d91d4,
// passing the "also close the handle" flag a2) and then unlinks the file stream
// record (File_FreeStream @0x5fb574), RETURNING the FreeEntryData result. Note the
// return value is the FIRST call's result, captured in edx across the second call.
//
// Resource_FreeEntryData and File_FreeStream are themselves substantial functions
// rooted in the deep file/memory/Vfs subsystem (indirect off_64A910/14/1C dispatch,
// Memory_FreeBlock, Vfs temp-file close, the stream free-list at dword_1408760) and
// are reconstructed elsewhere; here we reproduce the wrapper's control flow + the
// edx-preserved return value 1:1, routing the two callees through a hook so the call
// ORDER and the return-value selection are faithful and testable.
struct ResourceFlushHooks {
    // VIBE_Resource_FreeEntryData @0x5d91d4 (eax=a1, edx=a2).
    i32 (*freeEntryData)(i32 a1, i32 a2) = nullptr;
    // VIBE_File_FreeStream @0x5fb574 (eax=a1).
    i32 (*freeStream)(i32 a1) = nullptr;
};

// gilde.exe 0x5d9104 — returns the Resource_FreeEntryData result (captured before
// File_FreeStream runs). Calls freeEntryData(a1,a2) then freeStream(a1), in order.
i32 Resource_FlushAndFree(i32 a1, i32 a2, const ResourceFlushHooks& h);

} // namespace guild::render
