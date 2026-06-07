#include "util/buffer.h"

namespace guild::util {

// gilde.exe 0x5f8540 — VIBE_Buffer_PutChar
//   v3 = (*result)++;  *v3 = a2;  ++result[4];  return result;
OutputBuffer* BufferPutChar(OutputBuffer* buf, char ch) {
    char* dst = buf->cursor;
    ++buf->cursor;
    *dst = ch;
    ++buf->count;
    return buf;
}

} // namespace guild::util
