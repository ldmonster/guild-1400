#include "io/vfs.h"

#include "compress/gzip.h"
#include "compress/inflate.h"
#include "compress/crc.h"

#include <cstdint>
#include <cstring>
#include <map>
#include <new>
#include <vector>

namespace guild::io {

using guild::compress::Gunzip;
using guild::compress::InflateRaw;

// --- module-global state (mirrors the binary's globals) --------------------
namespace {
guild::shim::IFileSystem* g_fs = nullptr;   // bound at VfsInit
bool g_caseInsensitive = false;             // byte_62EB84
int  g_openCount = 0;                        // dword_62EB88

// The binary stored a 32-bit backend pointer in handle slot +0x100. On a 64-bit
// host we keep the byte layout identical and stash the live backend object in a
// registry keyed by a non-zero id placed in that slot.
guild::u32 g_nextBackendId = 1;
std::map<guild::u32, void*> g_backends;

guild::u32 RegisterBackend(void* p) {
    guild::u32 id = g_nextBackendId++;
    if (g_nextBackendId == 0) g_nextBackendId = 1;   // never hand out 0
    g_backends[id] = p;
    return id;
}
void* LookupBackend(guild::u32 id) {
    if (id == 0) return nullptr;
    auto it = g_backends.find(id);
    return it == g_backends.end() ? nullptr : it->second;
}
void UnregisterBackend(guild::u32 id) {
    g_backends.erase(id);
}

// dword_62EB70 / dword_62EB74 — gzip magic probed inside zip members / buffers.
constexpr guild::u8 kGzMagic0 = 0x1f;
constexpr guild::u8 kGzMagic1 = 0x8b;

// .BIN extension table @0x44E8F0, 6-byte stride, highest index first.
// aBin5[6*idx] selects ".BIN5".."".BIN" as idx goes 0..6.
const char* const kBinExtensions[] = {
    ".BIN5", ".BIN4", ".BIN3", ".BIN2", ".BIN1", ".BIN0", ".BIN",
};
constexpr int kBinExtCount = 7;

// ---------------------------------------------------------------------------
// A decompressed/loose backing buffer for read streams. For loose binary files
// we still read the whole file in (the original buffered the FILE*; serving from
// memory is behaviour-identical for the read/seek/tell verbs the VFS exposes).
struct ReadBuffer {
    std::vector<guild::u8> data;
    std::size_t            pos = 0;
};

ReadBuffer* AsRead(VfsHandle* h) {
    return reinterpret_cast<ReadBuffer*>(LookupBackend(h->backendStream));
}

// A loose write target backed by a shim::IFile.
struct WriteTarget {
    LooseFile* file = nullptr;
};

// An in-memory write cursor (memory streams). Mirrors writeDst/writeRemaining,
// but the destination pointer is 64-bit so it lives in the backend registry.
struct MemWrite {
    guild::u8* dst = nullptr;
    guild::u32 remaining = 0;
};

// Read every byte of a host file into `out`. Returns false on open failure.
bool SlurpFile(const char* path, std::vector<guild::u8>& out) {
    if (!g_fs)
        return false;
    guild::shim::IFile* f = g_fs->open(path, "rb");
    if (!f)
        return false;
    std::int64_t sz = f->size();
    if (sz < 0) sz = 0;
    out.resize(static_cast<std::size_t>(sz));
    std::size_t got = 0;
    if (sz > 0)
        got = f->read(out.data(), static_cast<std::size_t>(sz));
    out.resize(got);
    g_fs->close(f);
    return true;
}

// ---------------------------------------------------------------------------
// PKZIP local-file decode. Faithful to the on-disk PKZIP layout the original's
// VIBE_Zip_* readers consume; the deflate payload is handed to InflateRaw (the
// already-done guild::compress core). Finds the first member whose name matches
// `member` (case-insensitive, '\\' and '/' equivalent) or, if member==nullptr,
// the first member in the archive.
constexpr guild::u32 kLocalSig = 0x04034b50;  // "PK\3\4"

bool NameMatches(const char* a, std::size_t alen, const char* b) {
    std::size_t blen = std::strlen(b);
    if (alen != blen)
        return false;
    for (std::size_t i = 0; i < alen; ++i) {
        char ca = a[i], cb = b[i];
        if (ca == '\\') ca = '/';
        if (cb == '\\') cb = '/';
        if (ca >= 'a' && ca <= 'z') ca = static_cast<char>(ca - 32);
        if (cb >= 'a' && cb <= 'z') cb = static_cast<char>(cb - 32);
        if (ca != cb)
            return false;
    }
    return true;
}

guild::u16 Rd16(const guild::u8* p) {
    return static_cast<guild::u16>(p[0] | (p[1] << 8));
}
guild::u32 Rd32(const guild::u8* p) {
    return static_cast<guild::u32>(p[0]) | (static_cast<guild::u32>(p[1]) << 8) |
           (static_cast<guild::u32>(p[2]) << 16) | (static_cast<guild::u32>(p[3]) << 24);
}

// Walk the local-file headers (sufficient for the game's flat .BIN archives) and
// extract one member into `out`. Returns true on success.
bool UnzipMember(const std::vector<guild::u8>& zip, const char* member,
                 std::vector<guild::u8>& out) {
    std::size_t i = 0;
    while (i + 30 <= zip.size()) {
        const guild::u8* h = zip.data() + i;
        if (Rd32(h) != kLocalSig)
            break;                                   // hit central dir / EOCD
        guild::u16 method   = Rd16(h + 8);
        guild::u32 compSize  = Rd32(h + 18);
        guild::u32 uncompSize = Rd32(h + 22);
        guild::u16 nameLen   = Rd16(h + 26);
        guild::u16 extraLen  = Rd16(h + 28);
        std::size_t nameOff  = i + 30;
        std::size_t dataOff  = nameOff + nameLen + extraLen;
        if (dataOff + compSize > zip.size())
            return false;
        const char* name = reinterpret_cast<const char*>(zip.data() + nameOff);

        bool hit = (member == nullptr) || NameMatches(name, nameLen, member);
        if (hit) {
            const guild::u8* payload = zip.data() + dataOff;
            if (method == 0) {                       // stored
                out.assign(payload, payload + compSize);
                return true;
            }
            if (method == 8) {                       // deflate
                out.clear();
                out.reserve(uncompSize);
                return InflateRaw(payload, compSize, out);
            }
            return false;                            // unsupported method
        }
        i = dataOff + compSize;
    }
    return false;
}

// Split "stem.BINk" path: locate the .BIN[0-5] extension. Returns the matching
// extension index (0..6 into kBinExtensions) or -1 if the path has no .BIN ext.
int BinExtensionIndex(const char* path) {
    std::size_t len = std::strlen(path);
    for (int idx = 0; idx < kBinExtCount; ++idx) {
        const char* ext = kBinExtensions[idx];
        std::size_t el = std::strlen(ext);
        if (len < el)
            continue;
        const char* tail = path + (len - el);
        bool eq = true;
        for (std::size_t j = 0; j < el; ++j) {
            char a = tail[j], b = ext[j];
            if (a >= 'a' && a <= 'z') a = static_cast<char>(a - 32);
            if (a != b) { eq = false; break; }
        }
        if (eq)
            return idx;
    }
    return -1;
}

// Allocate + zero a 320-byte handle and copy the path into the name field.
VfsHandle* AllocHandle(const char* path) {
    VfsHandle* h = new (std::nothrow) VfsHandle();
    if (!h)
        return nullptr;
    std::memset(h, 0, sizeof(VfsHandle));
    if (path) {
        std::strncpy(h->name, path, sizeof(h->name) - 1);
        h->name[sizeof(h->name) - 1] = '\0';
    }
    return h;
}

// Build a read-backed handle from an already-decompressed buffer.
VfsHandle* MakeReadHandle(const char* path, std::vector<guild::u8>&& bytes,
                          guild::u8 extraFlags) {
    VfsHandle* h = AllocHandle(path);
    if (!h)
        return nullptr;
    ReadBuffer* rb = new (std::nothrow) ReadBuffer();
    if (!rb) { delete h; return nullptr; }
    rb->data = std::move(bytes);
    h->backendStream = RegisterBackend(rb);
    h->flags = static_cast<guild::u8>(kVfsRead | extraFlags);
    ++g_openCount;
    return h;
}

} // namespace

// ---------------------------------------------------------------------------
// gilde.exe 0x451f98 — VIBE_Vfs_Init  (__usercall, al = (root@eax, caseFlag@dl)).
// The original also scans the root directory tree (VIBE_Vfs_ScanDirectory) and
// records the working dir; here we bind the host filesystem the VFS reads through.
bool VfsInit(guild::shim::IFileSystem* fs, bool caseInsensitive) {
    g_fs = fs;
    g_caseInsensitive = caseInsensitive;
    g_openCount = 0;                                 // dword_62EB88 = 0
    return fs != nullptr;
}

// gilde.exe 0x452004 — VIBE_Vfs_Shutdown.
void VfsShutdown() {
    g_fs = nullptr;
}

int VfsOpenCount() {
    return g_openCount;
}

// ---------------------------------------------------------------------------
// Mode-string parsing, matching the char scan at the top of VIBE_Vfs_OpenFile.
//   'R'/'B' -> read, 'W' -> write, 'T' -> text (binary=0), 'N' -> no gzip,
//   'Z' -> request gzip framing. Returns flags seeded with read/binary/gzip.
struct ParsedMode {
    bool read   = true;   // v5  (default read; cleared by 'W')
    bool binary = true;   // v76 (cleared by 'T')
    bool gzip   = true;   // v77 (cleared by 'N')
};

static ParsedMode ParseMode(const char* mode) {
    ParsedMode m;
    if (!mode)
        return m;
    for (const char* p = mode; *p; ++p) {
        char c = *p;
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 32);          // VIBE_Util_CharToUpper
        switch (c) {
            case 'R': m.read = true;  break;        // 0x52
            case 'B': m.binary = true; break;       // 0x42
            case 'T': m.binary = false; break;      // 0x54
            case 'W': m.read = false; break;        // 0x57
            case 'N': m.gzip = false; break;        // 0x4E
            case 'Z': m.gzip = true; break;         // 0x5A
            default: break;
        }
    }
    return m;
}

// gilde.exe 0x450bc8 — VIBE_Vfs_OpenFile.
VfsHandle* VfsOpenFile(const char* path, const char* mode) {
    if (!path || !*path || !mode || !g_fs)
        return nullptr;

    ParsedMode m = ParseMode(mode);

    // --- write path --------------------------------------------------------
    if (!m.read) {
        LooseFile* lf = FileOpen(g_fs, path, m.binary ? "wb" : "wt");
        if (!lf)
            return nullptr;
        VfsHandle* h = AllocHandle(path);
        if (!h) { FileClose(lf); return nullptr; }
        WriteTarget* wt = new (std::nothrow) WriteTarget{lf};
        if (!wt) { FileClose(lf); delete h; return nullptr; }
        h->backendStream = RegisterBackend(wt);
        // flags: read=0, binary->bit1, gzip->bit3 (write framing handled later)
        guild::u8 f = 0;
        if (m.binary) f |= kVfsBinary;
        // gzip-on-write would set kVfsGzip; transparent gz write is not exercised
        // by this slice (memory-stream deflate path covers the codec) so leave it
        // to the loose writer.
        h->flags = f;
        ++g_openCount;
        return h;
    }

    // --- read path: zip member? -------------------------------------------
    int binIdx = BinExtensionIndex(path);
    if (binIdx >= 0) {
        std::vector<guild::u8> archive;
        if (!SlurpFile(path, archive))
            return nullptr;
        std::vector<guild::u8> member;
        // The .BIN file IS the archive; open its single/first member.
        if (!UnzipMember(archive, nullptr, member))
            return nullptr;
        return MakeReadHandle(path, std::move(member),
                              static_cast<guild::u8>(kVfsZipMember | kVfsGzInZip));
    }

    // --- read path: loose file, maybe gzip-framed --------------------------
    std::vector<guild::u8> raw;
    if (!SlurpFile(path, raw))
        return nullptr;

    bool isGz = m.gzip && raw.size() >= 2 &&
                raw[0] == kGzMagic0 && raw[1] == kGzMagic1;
    if (isGz) {
        std::vector<guild::u8> plain;
        if (Gunzip(raw.data(), raw.size(), plain))
            return MakeReadHandle(path, std::move(plain), kVfsGzip);
        // fall through: treat as loose on framing failure (original logs + retries)
    }
    return MakeReadHandle(path, std::move(raw), 0);
}

// gilde.exe 0x451b30 — VIBE_Vfs_OpenMemoryStream.
VfsHandle* VfsOpenMemoryStream(guild::u8* buffer, guild::u32 length, const char* mode) {
    if (!buffer || !length)
        return nullptr;

    ParsedMode m = ParseMode(mode);

    if (m.read) {
        std::vector<guild::u8> bytes;
        bool gz = m.gzip && length >= 2 &&
                  buffer[0] == kGzMagic0 && buffer[1] == kGzMagic1;
        if (gz) {
            std::vector<guild::u8> plain;
            if (!Gunzip(buffer, length, plain))
                return nullptr;
            bytes = std::move(plain);
        } else if (!m.binary) {
            // raw memory; copy through
            bytes.assign(buffer, buffer + length);
        } else {
            bytes.assign(buffer, buffer + length);
        }
        VfsHandle* h = MakeReadHandle(nullptr, std::move(bytes), kVfsMemory);
        return h;
    }

    // write-to-memory: the 64-bit destination lives in the backend registry; the
    // handle's writeRemaining/writeTotal slots track room/total exactly as the
    // original 32-bit fields did.
    VfsHandle* h = AllocHandle(nullptr);
    if (!h)
        return nullptr;
    MemWrite* mw = new (std::nothrow) MemWrite{buffer, length};
    if (!mw) { delete h; return nullptr; }
    h->backendStream = RegisterBackend(mw);
    h->writeRemaining = length;
    h->writeTotal = 0;
    h->flags = kVfsMemory;
    if (m.binary) h->flags |= kVfsBinary;
    ++g_openCount;
    return h;
}

// gilde.exe 0x4514ac — VIBE_Vfs_ReadStream.
guild::u32 VfsReadStream(void* dst, guild::u32 size, VfsHandle* h, guild::u32 count) {
    constexpr guild::u32 kErr = 0xFFFFFFFFu;
    if (!h || (h->flags & kVfsRead) == 0)
        return kErr;
    if (!h->backendStream && (h->flags & kVfsMemory) == 0)
        return kErr;

    guild::u32 want = count * size;                  // v27 = a4 * a2
    if (want == 0)
        return 0;
    if (!dst)
        return kErr;

    ReadBuffer* rb = AsRead(h);
    std::size_t avail = rb->data.size() - rb->pos;
    if (static_cast<std::size_t>(want) > avail)
        want = static_cast<guild::u32>(avail);       // clamp at EOF
    if (want == 0)
        return 0;
    std::memcpy(dst, rb->data.data() + rb->pos, want);
    rb->pos += want;
    return want;
}

// gilde.exe 0x4517a8 — VIBE_Vfs_WriteStream.
guild::u32 VfsWriteStream(const void* src, guild::u32 size, VfsHandle* h, guild::u32 count) {
    constexpr guild::u32 kErr = 0xFFFFFFFFu;
    if (!h || (h->flags & kVfsRead) != 0)
        return kErr;

    guild::u32 want = count * size;                  // v21 = a4 * a2
    if ((h->flags & kVfsMemory) != 0 && want > h->writeRemaining)
        want = h->writeRemaining;
    if (want == 0)
        return 0;
    if (!src)
        return kErr;

    if ((h->flags & kVfsMemory) != 0) {
        MemWrite* mw = reinterpret_cast<MemWrite*>(LookupBackend(h->backendStream));
        if (!mw)
            return kErr;
        std::memcpy(mw->dst, src, want);
        mw->dst += want;
        mw->remaining -= want;
        h->writeRemaining = mw->remaining;
        h->writeTotal += want;
        return want;
    }

    WriteTarget* wt = reinterpret_cast<WriteTarget*>(LookupBackend(h->backendStream));
    if (!wt || !wt->file || !wt->file->file)
        return kErr;
    std::size_t n = wt->file->file->write(src, want);
    h->writeTotal += static_cast<guild::u32>(n);
    return static_cast<guild::u32>(n);
}

// gilde.exe 0x4518f0 — VIBE_Vfs_Seek. Seeking is read-only in the original.
int VfsSeek(VfsHandle* h, long offset, guild::u32 whence) {
    if (!h)
        return -1;
    if ((h->flags & kVfsRead) == 0)
        return -1;                                   // "Seeking only supported in read-only files"
    ReadBuffer* rb = AsRead(h);
    if (!rb)
        return -1;
    long base;
    switch (whence) {
        case 0: base = 0; break;                              // SEEK_SET
        case 1: base = static_cast<long>(rb->pos); break;     // SEEK_CUR
        case 2: base = static_cast<long>(rb->data.size()); break; // SEEK_END
        default: return -1;
    }
    long target = base + offset;
    if (target < 0 || static_cast<std::size_t>(target) > rb->data.size())
        return -1;
    rb->pos = static_cast<std::size_t>(target);
    return 0;
}

// gilde.exe 0x451aa4 — VIBE_Vfs_Tell.
long VfsTell(VfsHandle* h) {
    if (!h)
        return -1;
    if ((h->flags & kVfsMemory) != 0 && (h->flags & kVfsRead) == 0)
        return static_cast<long>(h->writeTotal);     // memory write: bytes written
    if ((h->flags & kVfsRead) != 0) {
        ReadBuffer* rb = AsRead(h);
        if (!rb)
            return -1;
        return static_cast<long>(rb->pos);
    }
    return -1;
}

// gilde.exe 0x451354 — VIBE_Vfs_CloseStream.
int VfsCloseStream(VfsHandle* h) {
    if (!h)
        return 0;

    int written = 0;
    void* backend = LookupBackend(h->backendStream);
    if ((h->flags & kVfsRead) != 0) {
        delete reinterpret_cast<ReadBuffer*>(backend);
    } else {
        written = static_cast<int>(h->writeTotal);
        if ((h->flags & kVfsMemory) != 0) {
            delete reinterpret_cast<MemWrite*>(backend);
        } else {
            WriteTarget* wt = reinterpret_cast<WriteTarget*>(backend);
            if (wt) {
                FileClose(wt->file);
                delete wt;
            }
        }
    }
    UnregisterBackend(h->backendStream);
    --g_openCount;
    delete h;
    return written;
}

} // namespace guild::io
