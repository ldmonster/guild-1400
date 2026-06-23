#pragma once
// gilde.exe — guild::render  (MODULE: model/AGF chunk-I/O record helpers +
// resource-handle management)
//
// 1:1 reconstruction of a cluster of small leaf functions that the big binary
// model loaders (VIBE_Model_FastChunkIo @0x5f9558 — DEFERRED, 0x1011 bytes;
// VIBE_Mesh_LoadBgfFile @0x5d2348 — DEFERRED) and the resource/file cache use.
//
// All of these operate on the ORIGINAL 32-bit in-memory model record (the block
// that VIBE_Model_FastChunkIo fills), which the IDB never gave a named UDT — the
// decompile accesses it as raw *(type*)(base + off). To stay verifiably 1:1 we
// translate them against a raw `u8* base` plus the exact byte offsets the binary
// uses; every offset is annotated with its original `a1 + N` form. The record
// layout (field offsets) is documented in modelio_recon.cpp.
//
// Reconstructed here:
//   0x5f8eb0  VIBE_Model_ComputeNormals      — per-face + per-vertex normals
//   0x5f8f98  VIBE_Model_ComputeBounds       — radius + AABB (+ 8 corner verts)
//   0x5f9430  VIBE_Model_ComputeChunkSize    — serialized chunk byte size
//   0x5e3fac  VIBE_ModelIo_ReadFloatThunk    — Bio read 1 byte -> mat[+200]
//   0x5e3fd4  VIBE_ModelIo_ReadFloatThunk2   — Bio read 1 byte -> mat[+201]
//   0x5e3ffc  VIBE_ModelIo_ReadDwordThunk    — Bio read dword  -> mat[+204]
//   0x5e4024  VIBE_ModelIo_ReadDwordThunk2   — Bio read dword  -> mat[+208]
//   0x5e404c  VIBE_ModelIo_ReadDwordThunk3   — Bio read dword  -> mat[+212]
//   0x5e4074  VIBE_ModelIo_ReadDwordThunk4   — Bio read dword  -> mat[+216]
//   0x5e409c  VIBE_ModelIo_ReadDwordThunk5   — Bio read dword  -> mat[+220]
//   0x40decc  VIBE_Resource_EvictOldestEntry — LRU eviction of a cache entry
//   0x40df94  VIBE_Resource_FindFreeSlot     — first free 740-byte handle slot
//   0x5d91d4  VIBE_Resource_FreeEntryData    — flush/close/free a stream entry
//
// File I/O (the Bio readers, the eviction allocator, the stream flush/close in
// FreeEntryData) crosses the platform boundary (rule 4), so it is routed through
// installable hook structs whose inert defaults live in modelio_recon.cpp. Tests
// install their own hooks to exercise the pure record/offset logic.
#include "guild/common/types.h"

namespace guild::render {

// ===========================================================================
//  Model record geometry helpers
// ===========================================================================
// The original model record (`a1`) interleaves 4-byte pointer and i32 slots
// (a1[16]=verts ptr, a1[17]=vert count, a1[18]=faces ptr, a1[19]=face count,
// a1+468=radius), which cannot be stored as raw host pointers on a 64-bit build
// (the slots would alias). Following the codebase convention (geometry_types.h
// models the same on-disk records as typed C++ structs), the geometry helpers
// take a typed view of just the fields they touch; the original byte offsets are
// preserved in the field comments and in the vertex/face stride constants below.
//
// Vertex record: 24-byte stride (6 floats). xyz @+0/+4/+8, normal @+12/+16/+20.
struct ModelVertex {
    float x, y, z;     // +0/+4/+8   model-space position
    float nx, ny, nz;  // +12/+16/+20 vertex normal (written by ComputeNormals/Bounds corners use xyz only)
};
// Face record: 56-byte stride. The fields these helpers touch:
struct ModelFace {
    u8   _pad0[24];      // +0..+23
    i32  v0, v1, v2;     // +24/+28/+32  vertex indices
    u8   _pad24[8];      // +36..+43
    float nx, ny, nz;    // +44/+48/+52  face normal (written by ComputeNormals)
};
static_assert(sizeof(ModelVertex) == 24, "vertex stride 24");
static_assert(sizeof(ModelFace) == 56,   "face stride 56");

// Typed view of the model record fields touched by the geometry helpers.
//   verts/vertCount  mirror a1[16]/a1[17] (== a1+64/a1+68 in ComputeBounds).
//   faces/faceCount  mirror a1[18]/a1[19].
//   radius           mirrors *(float*)(a1+468).
// ComputeBounds writes 8 corner verts at verts[vertCount .. vertCount+7], so the
// `verts` buffer must hold at least vertCount + 8 entries.
struct ModelRecord {
    ModelVertex* verts = nullptr;
    i32          vertCount = 0;
    ModelFace*   faces = nullptr;
    i32          faceCount = 0;
    float        radius = 0.0f;
};

// VIBE_Model_ComputeNormals @0x5f8eb0 — for each face, compute its triangle
// normal (TriangleNormal of its 3 verts) and store it into the face record
// (nx/ny/nz); then for each vertex, accumulate the normals of every face that
// references it, normalize, and store into the vertex (nx/ny/nz).
void ModelComputeNormals(ModelRecord& rec);

// VIBE_Model_ComputeBounds @0x5f8f98 — pass 1 computes the bounding radius
// (max |v| over all verts) into rec.radius. Pass 2 computes the AABB
// (min/max xyz) and writes the 8 bounding-box corner vertices into the 8 vertex
// slots immediately AFTER the real vertices (index vertCount .. vertCount+7).
void ModelComputeBounds(ModelRecord& rec);

// VIBE_Model_ComputeChunkSize @0x5f9430 — compute the serialized byte size of a
// model chunk. `rec` is the model record (eax / a1, a 32-bit-word view), `aux`
// is the auxiliary block (edx / a2) whose +16 holds a 224-byte-strided material
// table and +52 a count of 88-byte-strided sub-records at +56.
//   rec[17] = normVtxCnt, rec[19] = faceCnt, rec[120] = materialCount.
// Returns the total byte size.
guild::i32 ModelComputeChunkSize(const u8* rec, const u8* aux);

// ===========================================================================
//  Model material-field Bio read thunks  (0x5e3fac .. 0x5e409c)
// ===========================================================================
// Each reads one field of the material at index ctx[+8] within the material
// table ctx[+16] (224-byte stride). `ctx` is the parser context (edx / a2);
// `stream` is the opaque VFS stream handle (eax / a1).
//
// Bio reader hook — mirrors VIBE_Bio_ReadByte @0x5dc850 / VIBE_Bio_ReadDword
// @0x5dc894 (already reconstructed as guild::io::BioReadByte/BioReadDword). The
// originals return the byte count read; the hook returns that count.
struct ModelIoBioHooks {
    // read 1 byte from `stream` into *dst; return bytes read (1 on success).
    int (*readByte)(void* stream, u8* dst) = nullptr;
    // read 4 bytes from `stream` into *dst (little-endian dword); return bytes read.
    int (*readDword)(void* stream, u32* dst) = nullptr;
};
void SetModelIoBioHooks(const ModelIoBioHooks& h);
ModelIoBioHooks GetModelIoBioHooks();

// The parser context fields the thunks read:
//   ctx + 8   (i32)  material index
//   ctx + 16  (ptr)  material table base
// Material table stride is 224 bytes; the per-field byte offsets are +200/+201
// (bytes) and +204/+208/+212/+216/+220 (dwords).
int ModelIoReadFloatThunk(void* stream, const u8* ctx);   // -> mat[+200], 1 byte
int ModelIoReadFloatThunk2(void* stream, const u8* ctx);  // -> mat[+201], 1 byte
int ModelIoReadDwordThunk(void* stream, const u8* ctx);   // -> mat[+204], dword
int ModelIoReadDwordThunk2(void* stream, const u8* ctx);  // -> mat[+208], dword
int ModelIoReadDwordThunk3(void* stream, const u8* ctx);  // -> mat[+212], dword
int ModelIoReadDwordThunk4(void* stream, const u8* ctx);  // -> mat[+216], dword
int ModelIoReadDwordThunk5(void* stream, const u8* ctx);  // -> mat[+220], dword

// ===========================================================================
//  Resource-handle / cache management  (0x40decc, 0x40df94, 0x5d91d4)
// ===========================================================================
// VIBE_Resource_FindFreeSlot @0x40df94 — linear scan of the handle table at
// `table` (740-byte stride, capacity 511: scan bound 378140 == 740*511); the
// "in use" flag is the dword at slot+4. Returns the first free slot index, or -1
// if the table is full. (Original reads the global dword_69FFB4; here the table
// base is passed in.)
guild::i32 ResourceFindFreeSlot(const u8* table);

// --- LRU cache eviction (VIBE_Resource_EvictOldestEntry @0x40decc) ----------
// Entry record (84-byte stride in the binary). The original packs a 4-byte data
// pointer @+52 and i32 size @+56 (these alias on a 64-bit host), so rather than
// a byte-exact layout the evictor uses a typed view holding only the fields it
// reads/writes. Original byte offsets are noted per field.
struct ResourceEntry {
    void* data = nullptr;  // +52  resident data pointer (0 == empty)
    u32   size = 0;        // +56  resident byte size
    i32   lockCount = 0;   // +64  >0 == in use, not evictable
    u8    flags = 0;       // +68  bit0 == pinned
    u32   timestamp = 0;   // +72  LRU timestamp
};

// The cache descriptor the original keeps in globals:
//   entries  (dword_62D204)  base ptr, 84-byte-strided entry records
//   count    (dword_62D208)  entry count
//   usedMem  (dword_62D20C)  running total of resident bytes
//   floor    (dword_62EB38)  the "oldest timestamp seen" seed
struct ResourceCache {
    ResourceEntry* entries = nullptr;  // dword_62D204
    i32  count   = 0;        // dword_62D208
    i32  usedMem = 0;        // dword_62D20C
    u32  floor   = 0;        // dword_62EB38 seed value
};
// Allocator-free hook — mirrors VIBE_Memory_FreeDebug @0x43923c. Frees the data
// pointer of the evicted entry. Default is inert.
struct ResourceFreeHook {
    void (*freeData)(void* data) = nullptr;
};
void SetResourceFreeHook(const ResourceFreeHook& h);
ResourceFreeHook GetResourceFreeHook();

// Evict the oldest unlocked / unpinned / resident entry from `cache`. Scans
// entries [1, count): an entry is eligible iff its timestamp < the running
// minimum AND lockCount <= 0 AND (flags & 1) == 0 AND data ptr != 0 AND size != 0.
// Frees the winner's data, zeroes its data ptr (+52) and subtracts its size from
// usedMem. Returns 1 if something was evicted, 0 otherwise.
guild::i32 ResourceEvictOldestEntry(ResourceCache& cache);

// ===========================================================================
//  VIBE_Resource_FreeEntryData @0x5d91d4
// ===========================================================================
// gilde.exe 0x5d91d4 — VIBE_Resource_FreeEntryData (__usercall eax=entry,
// edx=alsoClose). Tears down a resident stream entry: optionally flushes its
// write buffer, releases the memory block, flush/seek + (optionally) closes the
// OS file handle, and returns any spill block / temp file. The entry record
// (`ent`, the original ecx == a1):
//   +0x08 (ptr)  blockHdr  — sub-record; blockHdr[+8] is a spill pointer, and
//                            blockHdr[+20] (byte) a temp-file id.
//   +0x0C (dword) infoWord — tested != 0 to detect "has data" (the -1 guard).
//                            Its low byte (+0x0C) carries flags1 (bit3 0x08:
//                            has a spill block to return); byte +0x0D carries
//                            flags2 (bit4 0x10: dirty write buffer to flush;
//                            bit3 0x08: has a temp file to delete).
//   +0x10 (i32)  fileHandle
// Returns the OR of the flush/close result codes (0 == clean), or -1 if the
// entry has no data (infoWord == 0).
//
// All file/memory operations cross the platform boundary (rule 4), so they are
// routed through this installable hook struct (defaults are inert). The opaque
// `entry`/`blockHdr` pointers are passed straight through to the hooks.
struct ResourceEntryView {
    void* blockHdr = nullptr;   // entry +0x08
    u32   infoWord = 0;         // entry +0x0C (dword; low byte=flags1, byte1=flags2)
    i32   fileHandle = 0;       // entry +0x10
    // The spill pointer + temp id the hooks need (blockHdr +8 / +20):
    void* spill    = nullptr;   // *(blockHdr + 8)
    u8    tempId   = 0;         // *(blockHdr + 20)
    // Convenience accessors mirroring the byte reads at +0x0C / +0x0D.
    u8 flags1() const { return (u8)(infoWord & 0xFF); }
    u8 flags2() const { return (u8)((infoWord >> 8) & 0xFF); }
};
struct ResourceFreeEntryHooks {
    int  (*flushBuffer)()            = nullptr;  // VIBE_File_FlushBuffer @0x5fb620
    void (*onMemoryRelease)(int fh)  = nullptr;  // ds:off_64A910 (default null-stub)
    int  (*freeBlock)(int fh)        = nullptr;  // VIBE_Memory_FreeBlock @0x5dbe10 (-1 == none)
    void (*flushAndSeek)(int fh, int pos) = nullptr; // VIBE_File_FlushAndSeek @0x5fb740
    int  (*closeHandle)(int fh)      = nullptr;  // VIBE_File_CloseHandle @0x5fc9a0
    void (*returnToFreeList)(void* p)= nullptr;  // VIBE_Memory_ReturnToFreeList @0x5dbf60
    void (*deleteTempFile)(u8 id)    = nullptr;  // VIBE_Vfs_* temp-file delete path
    void (*onPostRelease)()          = nullptr;  // ds:off_64A914
    void (*onCloseExtra)()           = nullptr;  // ds:off_64A91C (only if alsoClose)
};
void SetResourceFreeEntryHooks(const ResourceFreeEntryHooks& h);
ResourceFreeEntryHooks GetResourceFreeEntryHooks();

// Returns -1 if the entry has no data (flags1 == 0), else the OR of the
// flush/close status codes. `alsoClose` mirrors the edx arg.
guild::i32 ResourceFreeEntryData(ResourceEntryView& ent, bool alsoClose);

} // namespace guild::render
