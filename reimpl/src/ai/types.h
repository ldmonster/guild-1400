#pragma once
// AI behavior-planner data layouts for the Guild simulation (gilde.exe).
//
// This header recovers the three table/array layouts the AI planner addresses:
//   * the per-person behavior method-stack (dword_B59658 / byte_B5965C),
//   * the score-table planner catalog (byte_B57210, 148-byte stride x 61), with
//     its parallel gate/enable/weight columns (dword_B5723C, byte_B57240,
//     flt_B57244, flt_B5725C, flt_B57264),
//   * the need arrays / eligibility default tables used by the random need-pick
//     family (dword_5830E0..dword_583138) and the decay params (unk_647738).
//
// Field offsets come from the planner/selector/exec accessors. Bytes the
// accessors never touch are left as raw padding with a TODO.
//
// Original global bases (live process):
//   Method-stack count       dword_B59658  0xB59658  (count; entries at +4)
//   Method-stack entries      byte_B5965C  0xB5965C  (one method-id byte each)
//   Method table (148B x 61)  byte_B57210  0xB57210
//   Method gate-fn column     dword_B5723C  0xB5723C  (148-byte stride per class)
//   Method enable column      byte_B57240  0xB57240
//   Method weight column A    flt_B57244   0xB57244
//   Method weight column B    flt_B5725C   0xB5725C
//   Method weight column C    flt_B57264   0xB57264
//   Person array              word_12CE910 0x12CE910 (stride 536; +303 method class)
#include "guild/common/types.h"

namespace guild::ai {

// ---------------------------------------------------------------------------
// Method stack  (gilde.exe dword_B59658 @0xB59658, byte_B5965C @0xB5965C)
//
// AiMethodStack_Push (0x4691e4):  count += 1; byte_B5965C[count-1] = id  (1-based
//   write: *((char*)&dword_B59658 + (count+1) + 3) = id, i.e. dword+4 == B5965C).
// AiMethodStack_Pop  (0x4691fc):  --count.
// The planner reads entries as (dword at &dword_B59658 + i + 1) >> 24, which is
// exactly byte_B5965C[i]. So: count at +0, entry bytes starting at +4.
//
// Capacity: the original is an unbounded file-global region; the byte array
// byte_B5965C spans the data between 0xB5965C and the next global. We model a
// fixed capacity (the planner never pushes more than the 61-method catalog).
// ---------------------------------------------------------------------------
constexpr int kMethodStackCapacity = 256; // bounded model (orig is open region)

// ---------------------------------------------------------------------------
// Method-table entry  (gilde.exe byte_B57210 @0xB57210, stride 148 / 0x94, 61)
//
// Per-entry fields the planner / executor touch:
//   +0   id            (byte)  method id; 0 == disabled slot (enable check)
//   +36  evalFn        (dword) score eval fn ptr (called as tbl[9])
//   +40  applyFn       (dword) apply/commit fn ptr (called as tbl[10])
//   +48  scoreVecCur   (32 B)  current score vector (8 stats x 4B)
//   +80  scoreVecPrev  (32 B)  decayed copy: MemMove(+80, +48, 32) each exec
// The high byte of the class word lives at byte index 3 of the entry; the planner
// compares (entry-as-int >> 24) classes via the parallel unk_B5720D view.
// ---------------------------------------------------------------------------
constexpr int kMethodTableStride = 148; // 0x94
constexpr int kMethodTableCount  = 61;
constexpr int kMethodCatalogBytes = kMethodTableStride * kMethodTableCount;

constexpr int kMethEvalFnOff   = 36; // dword index 9
constexpr int kMethApplyFnOff  = 40; // dword index 10
constexpr int kMethScoreCurOff = 48;
constexpr int kMethScorePrevOff= 80;
constexpr int kScoreVecBytes   = 32; // 8 stats x 4 bytes

GUILD_PACKED_BEGIN
struct MethodEntry {
    u8  id;              // +0x00  method id (0 == disabled slot)
    u8  pad1[2];         // +0x01  TODO
    u8  classHi;         // +0x03  class id (planner reads (entry>>24))
    u8  pad4[32];        // +0x04..+0x23  TODO: class/flags/descriptor
    i32 evalFn;          // +0x24  (+36) score-eval fn ptr (orig 32-bit ptr)
    i32 applyFn;         // +0x28  (+40) apply/commit fn ptr (orig 32-bit ptr)
    u8  pad44[4];        // +0x2C  TODO
    u8  scoreCur[32];    // +0x30  (+48) current score vector
    u8  scorePrev[32];   // +0x50  (+80) decayed previous score vector
    u8  pad112[36];      // +0x70..+0x93  TODO: name string + tail
} GUILD_PACKED;
GUILD_PACKED_END
static_assert(sizeof(MethodEntry) == kMethodTableStride, "MethodEntry stride 148");

// ---------------------------------------------------------------------------
// Need-pick eligibility tables (gilde.exe @0x5830E0..0x583138)
//
// The random need-pick family builds an N-slot eligibility array, seeding it from
// one of these default tables and overriding a slot to 1 when the corresponding
// need-flag bits in the person record (+44) are set. All defaults are 0 in the
// shipped data (no need is eligible unless its flag is set). The index->need-id
// map for the group-pick variant is dword_5830F0 = {2,3,4,5}.
// ---------------------------------------------------------------------------
constexpr u8 kNeedGroupIdMap[4] = {2, 3, 4, 5}; // dword_5830F0 (low bytes)

// ---------------------------------------------------------------------------
// Need decay params (gilde.exe unk_647738 @0x647738, word_64773C @0x64773C)
//   unk_647738  = 40.0f  (scale applied to the random magnitude)
//   word_64773C = 16     (modulo bound for the random decay magnitude)
// ApplyRandomDecayField: mag = RandNext() % 16; value -= (int)(mag * 40.0f).
// ---------------------------------------------------------------------------
constexpr float kDecayScale = 40.0f;  // unk_647738
constexpr u16   kDecayBound = 16;     // word_64773C

} // namespace guild::ai
