// gilde.exe — guild::io  (MODULE: binary-IO codec — VIBE_Bio_* compound codecs)
//
// 1:1 reconstructions of the compound VIBE_Bio_* serializers (vectors, length-
// prefixed blocks, count+stride-prefixed arrays) plus three pure zlib/zip leaves.
// All stream I/O goes through the already-reconstructed VFS verbs (io/vfs).
//
// See bio_codec.h for the per-function original addresses and field layout.
#include "io/bio_codec.h"

#include <cstdlib>
#include <cstring>

namespace guild::io {

namespace {

// --- VIBE_Memory_AllocDebug @0x438f10 / VIBE_Memory_FreeDebug @0x43923c -----
// Inert default: the original tracks an 8-byte guard header; for the codec slice
// only the user payload matters, so plain malloc/free is byte-faithful for the
// data the readers hand back. Tests may install a tracking implementation.
void* DefaultAlloc(guild::u32 size, const char* /*tag*/) {
    return std::malloc(size ? size : 1);
}
void DefaultFree(void* p) {
    std::free(p);
}

BioCodecHooks g_hooks{};

inline void* DoAlloc(guild::u32 size, const char* tag) {
    return g_hooks.alloc ? g_hooks.alloc(size, tag) : DefaultAlloc(size, tag);
}

// Exact stream helpers (size 4 / count 1, mirroring the original calls).
inline bool WriteExact(VfsHandle* h, const void* src, guild::u32 n) {
    return VfsWriteStream(src, n, h, 1) == n;
}
inline bool ReadExact(VfsHandle* h, void* dst, guild::u32 n) {
    return VfsReadStream(dst, n, h, 1) == n;
}

} // namespace

void SetBioCodecHooks(const BioCodecHooks& h) { g_hooks = h; }

BioCodecHooks GetBioCodecHooks() {
    BioCodecHooks eff = g_hooks;
    if (!eff.alloc) eff.alloc = &DefaultAlloc;
    if (!eff.free)  eff.free  = &DefaultFree;
    return eff;
}

// --- fixed-vector serializers ---------------------------------------------

// gilde.exe 0x5dc980 — VIBE_Bio_ReadVec4. Four sequential 4-byte reads into
// a2[0..3]. The original calls VIBE_Vfs_ReadStream(a2 + 4*i, 4, a1, 1) per lane.
bool BioReadVec4(VfsHandle* h, guild::u32 dst[4]) {
    if (!dst) return false;
    bool ok = true;
    ok = ReadExact(h, &dst[0], 4) && ok;
    ok = ReadExact(h, &dst[1], 4) && ok;
    ok = ReadExact(h, &dst[2], 4) && ok;
    ok = ReadExact(h, &dst[3], 4) && ok;
    return ok;
}

// gilde.exe 0x5dc9dc — VIBE_Bio_WriteVec3. Each lane is staged through its own
// stack slot then written (size 4 / count 1).
bool BioWriteVec3(VfsHandle* h, const guild::u32 src[3]) {
    if (!src) return false;
    guild::u32 a = src[0]; bool ok = WriteExact(h, &a, 4);
    guild::u32 b = src[1]; ok = WriteExact(h, &b, 4) && ok;
    guild::u32 c = src[2]; ok = WriteExact(h, &c, 4) && ok;
    return ok;
}

// gilde.exe 0x5dca40 — VIBE_Bio_WriteVec4.
bool BioWriteVec4(VfsHandle* h, const guild::u32 src[4]) {
    if (!src) return false;
    guild::u32 a = src[0]; bool ok = WriteExact(h, &a, 4);
    guild::u32 b = src[1]; ok = WriteExact(h, &b, 4) && ok;
    guild::u32 c = src[2]; ok = WriteExact(h, &c, 4) && ok;
    guild::u32 d = src[3]; ok = WriteExact(h, &d, 4) && ok;
    return ok;
}

// --- length-prefixed block ------------------------------------------------

// gilde.exe 0x5dcae4 — VIBE_Bio_WriteBlock. Emits {length} (4 bytes) then the
// payload as VfsWriteStream(a3, a2 /*length*/, a1, 1) — i.e. one element of
// `length` bytes.
bool BioWriteBlock(VfsHandle* h, const void* data, guild::u32 length) {
    guild::u32 len = length;
    if (!WriteExact(h, &len, 4))
        return false;
    if (length == 0)
        return true;
    if (!data)
        return false;
    return WriteExact(h, data, length);
}

// gilde.exe 0x5dcb20 — VIBE_Bio_ReadBlockAlloc. Reads the u32 length, allocates,
// stores the pointer in *a2, reads the payload, returns the length.
guild::u32 BioReadBlockAlloc(VfsHandle* h, void** out, const char* allocTag) {
    guild::u32 length = 0;
    if (!ReadExact(h, &length, 4)) {
        if (out) *out = nullptr;
        return 0;
    }
    void* p = DoAlloc(length, allocTag);
    if (out) *out = p;
    if (length && p)
        ReadExact(h, p, length);   // original ignores the read return here
    return length;
}

// gilde.exe 0x5dcb68 — VIBE_Bio_ReadBlockQuick (fixed tag string).
guild::u32 BioReadBlockQuick(VfsHandle* h, void** out) {
    return BioReadBlockAlloc(h, out, "bio:bio_rd_block_quick");
}

// --- count+stride-prefixed array ------------------------------------------

// gilde.exe 0x5dcbb0 — VIBE_Bio_WriteArray. Writes {count}, {stride}, then
// VfsWriteStream(data, stride, h, count) — `count` elements of `stride` bytes.
bool BioWriteArray(VfsHandle* h, const void* data, guild::u32 stride, guild::u32 count) {
    guild::u32 c = count;
    if (!WriteExact(h, &c, 4))
        return false;
    guild::u32 s = stride;
    if (!WriteExact(h, &s, 4))
        return false;
    guild::u32 total = stride * count;
    if (total == 0)
        return true;
    if (!data)
        return false;
    return VfsWriteStream(data, stride, h, count) == total;
}

// gilde.exe 0x5dcc08 — VIBE_Bio_ReadArrayDebug. Reads {count}=v9, {stride}=v10.
// If the on-disk stride v10 == the expected element size a2, allocate v10*v9 bytes,
// read them (a2 bytes * v9 count) and return v9. Otherwise seek forward over the
// array (v10*v9), set *out = null, log, and return 0.
guild::u32 BioReadArrayDebug(VfsHandle* h, guild::u32 expectStride, const char* allocTag,
                             void** out) {
    guild::u32 count = 0, stride = 0;
    if (!ReadExact(h, &count, 4)) { if (out) *out = nullptr; return 0; }
    if (!ReadExact(h, &stride, 4)) { if (out) *out = nullptr; return 0; }
    if (expectStride == stride) {
        void* p = DoAlloc(stride * count, allocTag);
        if (out) *out = p;
        if (p && count)
            VfsReadStream(p, expectStride, h, count);   // a2 bytes * count
        return count;
    }
    // mismatch: skip the array body and report failure
    VfsSeek(h, static_cast<long>(stride * count), 1 /*SEEK_CUR*/);
    if (out) *out = nullptr;
    // original calls VIBE_ErrorLog_ReportMessage("bio_rd_array_debug: ... invalid array-sizes")
    return 0;
}

// gilde.exe 0x5dcca0 — VIBE_Bio_ReadArrayQuick (fixed tag string).
guild::u32 BioReadArrayQuick(VfsHandle* h, guild::u32 expectStride, void** out) {
    return BioReadArrayDebug(h, expectStride, "bio:bio_rd_array_quick", out);
}

// --- pure leaves ----------------------------------------------------------

// gilde.exe 0x5eb5f0 — VIBE_Zip_TellCurrentFile.
//   if ( a1 && (v1 = *(a1+124)) != 0 ) return *(v1+24); else return -102;
int ZipTellCurrentFile(const void* unzPtr) {
    if (!unzPtr)
        return kUnzParamError;
    const unsigned char* base = static_cast<const unsigned char*>(unzPtr);
    const void* fileInfo = *reinterpret_cast<void* const*>(base + 124);
    if (!fileInfo)
        return kUnzParamError;
    const unsigned char* fi = static_cast<const unsigned char*>(fileInfo);
    return *reinterpret_cast<const int*>(fi + 24);
}

// gilde.exe 0x5ffae0 — VIBE_Inflate_SyncPoint: return *a1 == 1.
bool InflateSyncPoint(const guild::u8* state) {
    return state && *state == 1;
}

// gilde.exe 0x5ffab0 — VIBE_Inflate_SetDictionary_ffab0.
//   v4 = a1[10];          (window write cursor)
//   memcpy(v4, a2, a3);
//   v6 = a3 + a1[10];
//   a1[13] = v6; a1[12] = v6;   (read / end cursors -> write + length)
//   return a3;
guild::u32 InflateSetWindowDictionary(void* window, const void* dict, guild::u32 length) {
    if (!window)
        return length;
    // The inflate_blocks_state is an array of dword slots; slot 10 is the window
    // write pointer (a void* on 32-bit). We model the slots as uintptr_t-sized.
    void** slots = reinterpret_cast<void**>(window);
    unsigned char* writePtr = static_cast<unsigned char*>(slots[10]);
    if (dict && length && writePtr)
        std::memcpy(writePtr, dict, length);
    unsigned char* endPtr = writePtr + length;
    slots[12] = endPtr;   // a1[12] (read cursor)
    slots[13] = endPtr;   // a1[13] (end cursor)
    return length;
}

} // namespace guild::io
