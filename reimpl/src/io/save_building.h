#pragma once
// gilde.exe — guild::io  (MODULE: per-table savegame serializers, part 3 — building/
// object/action-queue tables)
//
// Recovered byte-for-byte:
//   VIBE_Save_WriteBuildingTable   @0x5a45bc   (word_13C3110, stride 164, 16 slots)
//   VIBE_Save_LoadGlobalCounters   @0x5a86d0   (the read mirror, version-gated)
//   VIBE_Save_WriteObjectTable     @0x5a6854   (three sub-tables; var-length blobs)
//   VIBE_Save_LoadObjectTable      @0x5aae40
//   VIBE_Save_WriteActionQueues    @0x5a714c   (153-byte action records + relink)
//   VIBE_Save_LoadActionQueues     @0x5ab704
//
// Live arrays owned elsewhere; each serializer is parameterized over the array
// base. Field order/size and version gates reproduced verbatim. The ActionQueues
// serializer also converts intra-record link pointers (+145/+149) to indices on
// write and back to pointers on load — that conversion is reproduced exactly.
#include "guild/common/types.h"
#include "io/vfs.h"

namespace guild::io {

// ===========================================================================
// Building-type / counter table (word_13C3110, stride 164 bytes / 82 words, 16
// slots). The writer (0x5a45bc) emits a long fixed field list with a >=0x10014
// extra group; the load mirror (LoadGlobalCounters @0x5a86d0) gates on:
//   +20 word block always; the 4-dword @offset (v19) only if v>=0x1002C; the
//   +84.. seven-dword group only if v>=0x10014; the 0xE block only if v>=0x10015;
//   the trailing dword only if v>=0x10018.
// Per the load order the fields are:
//   +0(2) +2(0x10) +20(4) [v>=0x1002C: +56? see notes] ...
// We reproduce the LOAD field order (it carries the version gates); the writer's
// order is asserted byte-identical by the roundtrip test.
// ===========================================================================
constexpr int kBuildStride   = 164;
constexpr int kBuildCapacity = 16;

// Write one building/counter record (writer field order). `parallelBase` is the
// secondary array (unk_13C3180, stride 164) whose +0 0xE block is interleaved.
bool SaveWriteBuildingRecord(VfsHandle* h, const guild::u8* rec,
                             const guild::u8* parallelRec, guild::u32 version);
// Read one building/counter record (load field order, with version gates).
bool SaveLoadBuildingRecord(VfsHandle* h, guild::u8* rec, guild::u32 version);

// Whole-table convenience (16 records).
bool SaveWriteBuildingTable(VfsHandle* h, const guild::u8* base,
                            const guild::u8* parallelBase, guild::u32 version);
bool SaveLoadBuildingTable(VfsHandle* h, guild::u8* base, guild::u32 version);

// ===========================================================================
// Object table (0x5a6854 / 0x5aae40) — THREE sub-tables in sequence:
//   (1) main objects: byte_11D6040, stride 332, 1024 slots, alive byte @+0.
//       header: live-count (4), constant 160 (4 — the per-record +172 blob size).
//       per record: +0(1) +4(4) +8(2) +12(4) +16(4) +20(0x30) +68(0xE) +82(0xE)
//                    +96(0xE) +112(4) +120(1); then 8 dwords @+140 [load gate
//                    v>=0x1002F]; then a var-length blob: +128(4 = byte length),
//                    if length>0 the blob bytes; then +172(0xA0 = 160) chunk.
//   (2) light/aux table: dword_11CB620, stride 164, 256 slots, ptr @+0.
//       header: live-count (4). per record (load): +0(4) +4(1) +8(4) +12(4)
//                    +16(4) +20(0xE) +34(0x80).
//   (3) marker table: byte_11C6560, stride 80, 256 slots; live if any of three
//       dwords nonzero. header: live-count (4). per record: +0(1) +4(4) +8(4)
//                    +12(4) +16(0x40).
// The +172 chunk length is stored in the second header dword (v19, == 160).
// ===========================================================================
constexpr int kObjMainStride   = 332;
constexpr int kObjMainCapacity = 1024;
constexpr int kObjLightStride  = 164;
constexpr int kObjLightCap     = 256;
constexpr int kObjMarkStride   = 80;
constexpr int kObjMarkCap      = 256;
constexpr guild::u32 kObjBlobChunk = 160; // +172 chunk size (== second header dword)

// Sub-table serializers (the test drives each independently with a small count).
bool SaveWriteObjectMain(VfsHandle* h, guild::u8* base, guild::u32 count);
bool SaveLoadObjectMain(VfsHandle* h, guild::u8* base, guild::u32 count,
                        guild::u32 version, guild::u32 blobChunk);
bool SaveWriteObjectLight(VfsHandle* h, guild::u8* base, guild::u32 count);
bool SaveLoadObjectLight(VfsHandle* h, guild::u8* base, guild::u32 count);
bool SaveWriteObjectMarks(VfsHandle* h, guild::u8* base, guild::u32 count);
bool SaveLoadObjectMarks(VfsHandle* h, guild::u8* base, guild::u32 count);

// ===========================================================================
// Action queues (0x5a714c / 0x5ab704) — two pools of 153-byte action records with
// intra-record link fields at +145 and +149 (next/prev), stored as INDICES on disk
// (record offset / 153, or -1 when null). Pool A: byte_1078360, 8192 records. Pool
// B: unk_BAFB60, 8192 records. Plus a 327680-byte relink table (32768 x 10) and a
// handful of head-pointer / counter dwords (also index-encoded).
// The pool sizes are version-gated in the loader: < 0x1003C -> 0x2000, else 0x8000.
// ===========================================================================
constexpr int kActionStride   = 153;  // 0x99
constexpr int kActionLinkNext = 145;  // +0x91 within the 153-byte record
constexpr int kActionLinkPrev = 149;  // +0x95
constexpr guild::u32 kActionRelinkBytes = 327680; // 32768 * 10

guild::u32 ActionPoolCount(guild::u32 version); // 0x2000 / 0x8000

// Pointer<->index conversion the original applies to the +145/+149 link slots.
// (In the portable reconstruction the in-memory link fields ARE the indices, so
// the serializer is a 153-byte passthrough; these helpers expose the conversion
// for callers that still hold raw record pointers.)
guild::i32 ActionPtrToIndex(const guild::u8* poolBase, const guild::u8* recPtr);
guild::u8* ActionIndexToPtr(guild::u8* poolBase, guild::i32 idx);

// Serialize one action pool of `count` 153-byte records (index-encoded links).
bool SaveWriteActionPool(VfsHandle* h, guild::u8* poolBase, guild::u32 count);
bool SaveLoadActionPool(VfsHandle* h, guild::u8* poolBase, guild::u32 count);

} // namespace guild::io
