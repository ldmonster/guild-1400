#pragma once
#include "guild/common/types.h"

// Minimal output-buffer cursor used by the formatted-output core in gilde.exe.
//   guild::util::BufferPutChar — gilde.exe 0x5f8540 — VIBE_Buffer_PutChar
//
// The original struct (passed in eax) is the sprintf "PUTC" sink: a write cursor
// plus a running character count. Only the two fields touched by PutChar are
// modelled here; the intervening dwords are padding/unused by this routine.
namespace guild::util {

struct OutputBuffer {
    char* cursor; // +0x00  write position, post-incremented per char
    u32   pad[3]; // +0x04 .. +0x0F  (unused by PutChar)
    u32   count;  // +0x10  total characters written
};

// 0x5f8540 — VIBE_Buffer_PutChar  (__usercall: buf@eax, ch@dl).
//   *buf->cursor++ = ch;  ++buf->count;  return buf;
OutputBuffer* BufferPutChar(OutputBuffer* buf, char ch);

} // namespace guild::util
