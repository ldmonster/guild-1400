// gilde.exe 0x5dc0d0 — VIBE_Vfs_WriteBuffered  (CRT `fwrite`, recovered 1:1).
//
// See savestate_decompress.h for the full wave-20 provenance note explaining why
// the three brief'd addresses are (a) two scene-update funcs handed off to other
// agents and (b) this one genuine CRT-fwrite leaf, reconstructed here.
//
// The control flow mirrors the disassembly at 0x5dc0d0 exactly:
//   - the WRITE-flag gate + EINVAL/ error-bit early-out,
//   - the zero-length early-out (returns 0, before the divide),
//   - the lazy buffer allocation,
//   - the binary block-or-buffer loop with 512-byte-aligned direct writes,
//   - the text per-byte PutcBuffered (CR/LF) loop,
//   - the "error => byte count forced to 0" rule,
//   - the final `total / size` record-count return.
//
// It operates on the BufferedFile record reconstructed in file_buffered.{h,cpp}:
// the original's +0x00 buffer-cursor pointer is here (buffer + cursorOff); +0x04
// remaining/pending; +0x14 bufSize; the +0x0C/+0x0D mode/dirty/error bits are
// carried by `flags` (kFileWrite/kFileError/kFileDirty/kFileBinary) with the
// binary/text split also cached in `f->binary`.

#include "io/savestate_decompress.h"

#include "shim/IFileSystem.h"

#include <cstring>
#include <new>

namespace guild::io {

namespace {

// +0x0D secondary-flags bit 0x04 = line/unbuffered stream (forces a flush after
// every buffered chunk / putc). Documented in file_buffered.h (+0x0D 0x04
// unbuffered); kept local to this CRT-fwrite slice.
constexpr guild::u8 kFileUnbuffered = 0x04;

// The original allocates the FILE buffer lazily the first time data flows
// (VIBE_File_AllocReadBuffer @0x5fb820); replicate that here so a freshly opened
// write stream gets its 4096-byte buffer on first write.
constexpr guild::u32 kFwriteDefaultBufSize = 4096;

bool EnsureBuffer(BufferedFile* f) {
    if (f->buffer)
        return true;
    if (!f->bufSize)
        f->bufSize = kFwriteDefaultBufSize;
    f->buffer = new (std::nothrow) guild::u8[f->bufSize];
    return f->buffer != nullptr;
}

// VIBE_File_FlushBuffer @0x5fb620 — commit the pending write buffer to the
// backing IFile. Sets the error bit and returns -1 on a short/failed write,
// matching the original's `*(+0x0C) |= 0x20`.
int FwriteFlush(BufferedFile* f) {
    if ((f->flags & kFileDirty) && f->remaining && f->buffer && f->file) {
        std::size_t n = f->file->write(f->buffer, f->remaining);
        if (n != f->remaining) {
            f->flags |= kFileError;
            f->remaining = 0;
            f->cursorOff = 0;
            f->flags &= ~static_cast<guild::u32>(kFileDirty);
            return -1;
        }
    }
    f->remaining = 0;
    f->cursorOff = 0;
    f->flags &= ~static_cast<guild::u32>(kFileDirty);
    return 0;
}

// VIBE_Crt_PutcBuffered @0x5fcf40 — emit one byte into the write buffer; in text
// mode a '\n' (0x0A) is preceded by a '\r' (0x0D). Returns the byte on success,
// -1 on a flush failure. Mirrors the original's 0x40-binary check that gates the
// CR injection.
int FwritePutc(BufferedFile* f, guild::u8 ch) {
    if (!EnsureBuffer(f))
        return -1;

    // Text mode: inject CR ahead of LF (0x5fcfb9..0x5fcfe1 in the original).
    if (!f->binary && ch == 0x0A) {
        f->flags |= kFileDirty;
        f->buffer[f->cursorOff++] = 0x0D;
        ++f->remaining;
        if (f->remaining == f->bufSize) {
            if (FwriteFlush(f) != 0)
                return -1;
        }
    }

    f->flags |= kFileDirty;
    f->buffer[f->cursorOff++] = ch;
    ++f->remaining;
    if (f->remaining == f->bufSize) {
        if (FwriteFlush(f) != 0)
            return -1;
    }
    return ch;
}

} // namespace

// gilde.exe 0x5dc0d0 — VIBE_Vfs_WriteBuffered.
std::size_t VfsWriteBuffered(const void* src, std::size_t size,
                             std::size_t count, BufferedFile* f) {
    // 0x5dc0e5..0x5dc10a: require the WRITE flag; otherwise EINVAL + error bit.
    if (!f || (f->flags & kFileWrite) == 0) {
        if (f)
            f->flags |= kFileError;
        return 0;
    }
    if (!src)
        return 0;

    // 0x5dc10f..0x5dc127: total = size*count; zero-length short-circuits before
    // any buffer work (and before the trailing /size divide).
    std::size_t total = size * count;
    if (total == 0)
        return 0;

    // 0x5dc12b..0x5dc133: bind the buffer if the stream has none yet.
    if (!EnsureBuffer(f))
        return 0;

    std::size_t written = 0;

    if (f->binary) {
        // ---- BINARY path (0x5dc15d..0x5dc22f) -----------------------------
        std::size_t left = total;
        const guild::u8* in = static_cast<const guild::u8*>(src);
        while (left) {
            std::size_t chunk;
            // 0x5dc15d: if the buffer holds pending bytes OR the remaining
            // request is smaller than the whole buffer, buffer it; else write a
            // 512-aligned block straight through.
            if (f->remaining != 0 || left < f->bufSize) {
                guild::u32 room = f->bufSize - f->remaining;   // 0x5dc1b1
                chunk = room;
                if (chunk > left)                               // 0x5dc1b5
                    chunk = left;
                std::memcpy(f->buffer + f->cursorOff, in, chunk); // 0x5dc1cd
                f->cursorOff += static_cast<guild::u32>(chunk);   // *a3 += v13
                f->remaining += static_cast<guild::u32>(chunk);   // 0x5dc1e3
                f->flags |= kFileDirty;                            // |0x10
                // 0x5dc1f7..0x5dc1fe: flush when the buffer fills up OR the
                // stream is line/unbuffered (+0x0D & 4). The original tests
                // `bh = (flags2 | 0x10); cmp remaining,bufSize / jz; test bh,4`
                // i.e. `remaining == bufSize || (flags2 & 4)`.
                if (f->remaining == f->bufSize ||
                    (f->flags2 & kFileUnbuffered) != 0) {
                    if (FwriteFlush(f) != 0)
                        break;
                }
            } else {
                // 0x5dc170: direct write, 512-byte aligned; if that rounds to 0
                // write the whole remaining request.
                std::size_t blk = left & ~static_cast<std::size_t>(0x1FF);
                if (blk == 0)
                    blk = left;
                std::size_t n = f->file ? f->file->write(in, blk) : 0;
                if (n == 0) {                                   // 0x5dc18e / ENOSPC
                    f->flags |= kFileError;
                    break;
                }
                chunk = n;
            }
            in += chunk;                                         // 0x5dc207
            written += chunk;                                    // 0x5dc21b
            left -= chunk;                                       // 0x5dc221
            // 0x5dc22f: stop on completion or a sticky error.
            if (f->flags & kFileError)
                break;
        }
    } else {
        // ---- TEXT path (0x5dc23a..0x5dc2a5) -------------------------------
        const guild::u8* in = static_cast<const guild::u8*>(src);
        for (std::size_t i = 0; i < total; ++i) {
            if (FwritePutc(f, in[i]) < 0)                       // 0x5dc26d
                break;
            ++written;                                          // 0x5dc280
            if (f->flags & kFileError)                          // 0x5dc279 (0x30)
                break;
        }
    }

    // 0x5dc2ae..0x5dc2b2: on a sticky error the reported count is forced to 0.
    if (f->flags & kFileError)
        written = 0;

    // 0x5dc121: return whole-record count.
    return written / size;
}

} // namespace guild::io
