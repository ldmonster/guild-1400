#include "render/coord_transform_leaf.h"

#include <cstring>

namespace guild::render {

// gilde.exe 0x5d8b00 — VIBE_Coord_Transform.
//   if (base == 0) return 0;
//   return base + *(i32*)(base + 0x45 + 4*(index & 0xFFFF));
i32 Coord_Transform(i32 base, u16 index, const unsigned char* mem) {
    if (base == 0)                  // test eax,eax / jz retn
        return 0;
    // and edx, 0FFFFh — index is already u16, so the mask is implicit.
    const i32 off = base + kCoordFieldBase + kCoordFieldStride * static_cast<i32>(index);
    i32 field;                       // [eax+edx*4+45h] (little-endian dword)
    std::memcpy(&field, mem + off, sizeof(field));
    return base + field;             // add eax, [...]
}

// Raw-pointer variant: `base` is a real flat pointer value.
i32 Coord_Transform_Ptr(i32 base, u16 index) {
    if (base == 0)
        return 0;
    const auto* p = reinterpret_cast<const unsigned char*>(
        static_cast<std::uintptr_t>(static_cast<std::uint32_t>(base)));
    const std::ptrdiff_t off = kCoordFieldBase + kCoordFieldStride * static_cast<i32>(index);
    i32 field;
    std::memcpy(&field, p + off, sizeof(field));
    return base + field;
}

// gilde.exe 0x5d9104 — VIBE_Resource_FlushAndFree.
//   r = Resource_FreeEntryData(a1, a2);   // result kept in edx across next call
//   File_FreeStream(a1);                  // clobbers eax
//   return r;                             // mov eax, edx
i32 Resource_FlushAndFree(i32 a1, i32 a2, const ResourceFlushHooks& h) {
    i32 r = h.freeEntryData ? h.freeEntryData(a1, a2) : 0;  // call + mov edx,eax
    if (h.freeStream) h.freeStream(a1);                     // call (eax clobbered)
    return r;                                                // mov eax,edx
}

} // namespace guild::render
