#pragma once
// Shared in-memory byte-stream test helper for the .esc script-import parser
// (src/sim/script_import.cpp).
//
// The parser's only cross-module leaves are byte/string stream readers, which
// the library routes through an installable ScriptImportHooks struct (inert by
// default). Tests need to feed those readers a deterministic in-memory stream
// WITHOUT defining any src/-referenced symbol — every test file is its own
// executable, so a bare `g_stream` global owned by one test TU is invisible to
// the others and to the library.
//
// This header is fully self-contained (header-only, `inline` state) so each
// test executable gets its own copy. A test installs the hooks and points them
// at a MemStream via ScopedScriptStream; the opaque `int` stream handle the
// readers receive is ignored (a single current stream is addressed).
#include "sim/script_import.h"
#include <cstddef>
#include <cstring>

namespace guild::test {

struct MemStream {
    const unsigned char* data;
    std::size_t          len;
    std::size_t          pos;
};

// The current stream the installed reader hooks draw from. Test-owned; defined
// inline so every (separately-linked) test executable has exactly one.
inline MemStream*& CurrentStream() {
    static MemStream* s = nullptr;
    return s;
}

// --- reader hooks (mirror VIBE_Vfs_ReadStream / VIBE_Bio_ReadByte / ReadString)
inline guild::u32 ReadStream(guild::u8* buf, guild::u32 size, int /*stream*/, int count) {
    MemStream* s = CurrentStream();
    if (!s) return 0;
    guild::u32 total = size * (guild::u32)count;
    if (s->pos + total > s->len) return 0;
    std::memcpy(buf, s->data + s->pos, total);
    s->pos += total;
    return total;
}
inline guild::u32 ReadByte(int stream, guild::u8* out) {
    return ReadStream(out, 1, stream, 1);
}
inline guild::u32 ReadString(int stream, guild::u8* out) {
    // gilde.exe 0x5dc86c: read bytes until a NUL terminator (inclusive).
    guild::u32 result = 0;
    guild::u8 b;
    do {
        if (!ReadStream(&b, 1, stream, 1)) { *out = 0; return result; }
        *out++ = b;
        result = b;
    } while (b);
    return result;
}

inline const guild::sim::ScriptImportHooks& Hooks() {
    static const guild::sim::ScriptImportHooks h = [] {
        guild::sim::ScriptImportHooks hk;
        hk.readStream = &ReadStream;
        hk.readByte   = &ReadByte;
        hk.readString = &ReadString;
        return hk;
    }();
    return h;
}

// RAII: install the reader hooks and point them at `ms` for this scope; restore
// the inert library defaults (and clear the current stream) on exit.
struct ScopedScriptStream {
    explicit ScopedScriptStream(MemStream& ms) {
        CurrentStream() = &ms;
        guild::sim::SetScriptImportHooks(&Hooks());
    }
    ~ScopedScriptStream() {
        guild::sim::SetScriptImportHooks(nullptr);
        CurrentStream() = nullptr;
    }
    ScopedScriptStream(const ScopedScriptStream&) = delete;
    ScopedScriptStream& operator=(const ScopedScriptStream&) = delete;
};

} // namespace guild::test
