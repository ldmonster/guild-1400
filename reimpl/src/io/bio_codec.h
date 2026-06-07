#pragma once
// gilde.exe — guild::io  (MODULE: binary-IO codec — VIBE_Bio_* vector / block /
// array serializers, plus two pure zlib/zip leaves)
//
// This closes the still-deferred slice of the VIBE_Bio_* family. The existing
// io/worldio.{h,cpp} already reconstructed the scalar primitives
// (BioReadByte / BioReadDword / BioWriteByte / BioWriteDword / BioWriteDwordPair);
// this file adds the COMPOUND codecs that the asset loaders (AGF/BGF meshes,
// scene blocks) use to (de)serialize fixed vectors, length-prefixed blocks and
// length+stride-prefixed arrays. Every one is a 1:1 wrapper over the already-
// reconstructed VFS stream verbs (VIBE_Vfs_ReadStream @0x4514ac /
// VIBE_Vfs_WriteStream @0x4517a8 / VIBE_Vfs_Seek @0x4518f0).
//
// Reconstructed here:
//   VIBE_Bio_ReadVec4        @0x5dc980  (4 raw dwords -> dst[0..3])
//   VIBE_Bio_WriteVec3       @0x5dc9dc  (3 raw dwords, each its own stream call)
//   VIBE_Bio_WriteVec4       @0x5dca40  (4 raw dwords)
//   VIBE_Bio_WriteBlock      @0x5dcae4  (u32 length, then `length` raw bytes)
//   VIBE_Bio_ReadBlockAlloc  @0x5dcb20  (u32 length -> alloc(length) -> read)
//   VIBE_Bio_ReadBlockQuick  @0x5dcb68  (same, fixed alloc tag)
//   VIBE_Bio_WriteArray      @0x5dcbb0  (u32 count, u32 stride, then count*stride)
//   VIBE_Bio_ReadArrayDebug  @0x5dcc08  (u32 count, u32 stride; verify stride==expect)
//   VIBE_Bio_ReadArrayQuick  @0x5dcca0  (same, fixed alloc tag)
//   VIBE_Zip_TellCurrentFile @0x5eb5f0  (pure: byte offset of the current member)
//   VIBE_Inflate_SyncPoint   @0x5ffae0  (pure: *state == 1)
//   VIBE_Inflate_SetDictionary_ffab0 @0x5ffab0  (pure: window-buffer memcpy)
//
// The Read*Alloc / Read*Array codecs allocate through VIBE_Memory_AllocDebug in
// the original. Per the project build model, the allocator is routed through an
// installable hooks struct whose default (defined in bio_codec.cpp) calls the
// real guild allocator semantics via plain malloc/free; tests may install their
// own to observe sizes / tags.
#include "guild/common/types.h"
#include "io/vfs.h"

namespace guild::io {

// --- installable allocator hooks (VIBE_Memory_AllocDebug @0x438f10 /
//     VIBE_Memory_FreeDebug @0x43923c) -------------------------------------
struct BioCodecHooks {
    // size in bytes, tag is the call-site string (for diagnostics only).
    void* (*alloc)(guild::u32 size, const char* tag) = nullptr;
    void  (*free)(void* p) = nullptr;
};
// Install custom hooks (nullptr members fall back to the inert defaults).
void SetBioCodecHooks(const BioCodecHooks& h);
// The currently-effective hooks (with defaults substituted for null members).
BioCodecHooks GetBioCodecHooks();

// --- fixed-vector serializers ---------------------------------------------
// VIBE_Bio_ReadVec4 @0x5dc980 — read four contiguous 32-bit values into dst[0..3].
// Returns true iff all four reads were complete (mirrors the original returning the
// last read's byte count, which the callers treat as success when == 4).
bool BioReadVec4(VfsHandle* h, guild::u32 dst[4]);

// VIBE_Bio_WriteVec3 @0x5dc9dc — write three 32-bit values, each via its own
// 4-byte stream call (the original stages each through a separate stack slot).
bool BioWriteVec3(VfsHandle* h, const guild::u32 src[3]);

// VIBE_Bio_WriteVec4 @0x5dca40 — write four 32-bit values, one stream call each.
bool BioWriteVec4(VfsHandle* h, const guild::u32 src[4]);

// --- length-prefixed block ------------------------------------------------
// VIBE_Bio_WriteBlock @0x5dcae4 — emit a u32 length then `length` raw bytes.
bool BioWriteBlock(VfsHandle* h, const void* data, guild::u32 length);

// VIBE_Bio_ReadBlockAlloc @0x5dcb20 — read a u32 length, allocate `length` bytes
// through the codec allocator (tag `allocTag`), read them in, store the pointer in
// *out and return the length. *out receives nullptr / 0 on a zero-length block.
guild::u32 BioReadBlockAlloc(VfsHandle* h, void** out, const char* allocTag);

// VIBE_Bio_ReadBlockQuick @0x5dcb68 — identical with the fixed tag
// "bio:bio_rd_block_quick".
guild::u32 BioReadBlockQuick(VfsHandle* h, void** out);

// --- count+stride-prefixed array ------------------------------------------
// VIBE_Bio_WriteArray @0x5dcbb0 — emit a u32 count, a u32 stride, then count*stride
// raw bytes. (The original passes `count` as the VfsWriteStream element count and
// `stride` as the element size.)
bool BioWriteArray(VfsHandle* h, const void* data, guild::u32 stride, guild::u32 count);

// VIBE_Bio_ReadArrayDebug @0x5dcc08 — read count, stride; if stride != expectStride
// the array is skipped (seek forward count*stride bytes), *out=nullptr and 0 is
// returned (the original logs "Tried to load invalid array-sizes"). Otherwise
// allocate count*stride bytes, read them, store in *out and return `count`.
guild::u32 BioReadArrayDebug(VfsHandle* h, guild::u32 expectStride, const char* allocTag,
                             void** out);

// VIBE_Bio_ReadArrayQuick @0x5dcca0 — identical with the fixed tag
// "bio:bio_rd_array_quick".
guild::u32 BioReadArrayQuick(VfsHandle* h, guild::u32 expectStride, void** out);

// --- pure leaves (no I/O state) -------------------------------------------
// VIBE_Zip_TellCurrentFile @0x5eb5f0 — given the unz file handle (the +124 slot is
// the per-file-in-zip info block whose +24 dword is the current byte offset),
// return that offset, or UNZ_PARAMERROR (-102) if either pointer is null.
//   unzPtr: base of the unz_s struct (a1); *(a1+124) is the file-info block.
int ZipTellCurrentFile(const void* unzPtr);
constexpr int kUnzParamError = -102;

// VIBE_Inflate_SyncPoint @0x5ffae0 — true iff the inflate-block-state byte == 1.
bool InflateSyncPoint(const guild::u8* state);

// VIBE_Inflate_SetDictionary_ffab0 @0x5ffab0 — copy `length` bytes of dictionary
// into the inflate window at window->write (slot 10), then set window->read and
// window->end (slots 12,13) to write+length. Returns `length`.
//   window: the inflate_blocks_state* (its slot[10] is the window write pointer).
guild::u32 InflateSetWindowDictionary(void* window, const void* dict, guild::u32 length);

} // namespace guild::io
