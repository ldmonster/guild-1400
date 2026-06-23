#pragma once
// Law / crime / evidence / office record layouts for the Guild legal-political
// system (gilde.exe). Kept in a separate header from world/types.h (owned by the
// economy agent) so neither clobbers the other; both share namespace guild::world.
//
// All offsets below are byte offsets recovered from the table accessors:
//   Law (Gesetz)    unk_631E98     36 B x 26   VIBE_Gesetz_GetRecord 0x4c244c
//   Crime (Straftat)dword_11BC760  45 B x 512  VIBE_Straftat_* 0x4c33xx
//   Evidence(Beweis)dword_11C2160/64           VIBE_Beweis_* (interleaved, 2048 pairs)
//   OfficeDef       dword_62EC8E   12 B x 37   VIBE_Office_GetDefinition 0x47f008
//   OfficeHolder    byte_B59848    24 B x 30   VIBE_Office_GetHolderEntryByCity 0x47dfec
#include <cstddef>

#include "guild/common/types.h"

namespace guild::world {

// ===========================================================================
// Law / Gesetz record  (gilde.exe unk_631E98 @0x631E98, stride 36, 26 entries)
// ===========================================================================
// VIBE_Gesetz_GetRecord copies 0x24 bytes per record. The evaluator reads it as
// 9 dwords v21[0..8]; the fields it actually touches are:
//   v21[0]  (+0)  id/type byte (used as char)
//   v21[4]  (+16) penalty (read as i16)
//   v21[5]  (+20) comparison operator byte (LOBYTE): 1..7
//   v21[6]  (+24) threshold (int)
// ApplyAndNotify writes the threshold via dword_631EB0[9*id] == base+24 (stride
// 36 == 9 dwords). The bytes between are descriptor / text-id fields.
constexpr int kLawCount  = 26;
constexpr int kLawStride = 36;

// Comparison operator codes (LOBYTE(v21[5])) used by EvaluateViolation.
// gilde.exe 0x4c2cd8: the roll/"violation" path (loc_4C2CE9) is taken when:
enum LawOp : guild::u8 {
    kLawOpEqual        = 1,  // value == threshold -> violation
    kLawOpNotEqual     = 2,  // value != threshold -> violation
    kLawOpLess         = 3,  // value <  threshold -> violation (jge -> no match)
    kLawOpLessEqual    = 4,  // value <= threshold -> violation (jg  -> no match)
    kLawOpGreater      = 5,  // value >  threshold -> violation (jle -> no match)
    kLawOpGreaterEqual = 6,  // value >= threshold -> violation (jl  -> no match)
    kLawOpSpecial      = 7,  // value!=threshold && threshold!=2 -> violation
};

GUILD_PACKED_BEGIN
struct LawRecord {
    u8  id;            // +0x00  law id / type (read as byte by the evaluator)
    u8  pad1[15];      // +0x01  TODO: descriptor / book fields
    i16 penalty;       // +0x10  (+16) penalty magnitude
    u8  pad18[2];      // +0x12  TODO
    u8  op;            // +0x14  (+20) comparison operator (LawOp 1..7)
    u8  pad21[3];      // +0x15  TODO
    i32 threshold;     // +0x18  (+24) comparison threshold
    u8  pad28[8];      // +0x1C  TODO: trailing text-id / float fields
} GUILD_PACKED;
GUILD_PACKED_END
static_assert(sizeof(LawRecord) == kLawStride, "LawRecord must be 36 bytes");
static_assert(offsetof(LawRecord, penalty)   == 16, "penalty @+16");
static_assert(offsetof(LawRecord, op)        == 20, "op @+20");
static_assert(offsetof(LawRecord, threshold) == 24, "threshold @+24");

// ===========================================================================
// Crime / Straftat table  (gilde.exe dword_11BC760, stride 45, 512 entries)
// ===========================================================================
// The originals address parallel named globals at fixed byte offsets of a 45-byte
// record. dword_11BC785 (proven-state, +37) doubles as the "occupied" marker
// (FindFreeSlot scans for proven-state == 0).
//   dword_11BC760 (+0)   crime id (int)         -- (-1 == free after clear)
//   dword_11BC776 (+22)  perpetrator person id (int)
//   word_11BC77A  (+26)  wanted counter (u16)
//   byte_11BC77C  (+28)  location byte
//   dword_11BC781 (+33)  target / location id (int)
//   dword_11BC785 (+37)  proven-state (int; 1 == proven, 0 == free, >1 == pending)
constexpr int kCrimeCount  = 512;
constexpr int kCrimeStride = 45;

GUILD_PACKED_BEGIN
struct CrimeRecord {
    i32 id;            // +0x00  (+0)  crime id
    u8  pad4[18];      // +0x04  TODO
    i32 perpetrator;   // +0x16  (+22) perpetrator person id
    u16 wanted;        // +0x1A  (+26) wanted counter
    u8  location;      // +0x1C  (+28) location byte
    u8  pad29[4];      // +0x1D  TODO
    i32 target;        // +0x21  (+33) target / location id
    i32 provenState;   // +0x25  (+37) proven-state (1==proven, 0==free)
    u8  pad41[4];      // +0x29  (+41) trailing bytes to the 45-byte stride
} GUILD_PACKED;
GUILD_PACKED_END
static_assert(sizeof(CrimeRecord) == kCrimeStride, "CrimeRecord must be 45 bytes");
static_assert(offsetof(CrimeRecord, perpetrator) == 22, "perpetrator @+22");
static_assert(offsetof(CrimeRecord, wanted)      == 26, "wanted @+26");
static_assert(offsetof(CrimeRecord, location)    == 28, "location @+28");
static_assert(offsetof(CrimeRecord, target)      == 33, "target @+33");
static_assert(offsetof(CrimeRecord, provenState) == 37, "provenState @+37");

// ===========================================================================
// Evidence / Beweis  (gilde.exe dword_11C2160 owner / dword_11C2164 crime-id)
// ===========================================================================
// Two interleaved dword arrays sharing a 2-dword (8-byte) stride: slot k uses
// owner[2*k] and crimeId[2*k]. 4096 dwords / 2 == 2048 logical pairs. A free slot
// is (owner==-1 && crimeId==-1).
constexpr int kEvidenceCapacity = 2048;   // logical pairs
constexpr int kEvidenceDwords   = 4096;   // == 2 * capacity

// ===========================================================================
// Office definition table  (gilde.exe dword_62EC8E @0x62EC8E, 12 B x 37)
// ===========================================================================
// GetDefinition reads 3 dwords starting at base + 2 + 12*rank. The category id is
// packed in the HIGH byte of dword[0]; the requirement code is at byte +1 of
// dword[0] (BYTE1). The +2 record skew is part of the original layout.
//   record dword[0]: id (low byte) | reqCode (byte1) | .. | category (high byte)
//   record dword[1]: flag / enable (non-zero == promotable pair)
//   record dword[2]: text id
constexpr int kOfficeDefCount = 37;   // a1 < 0x25

// ===========================================================================
// Office holder table  (gilde.exe byte_B59848 @0xB59848, 24 B x 30)
// ===========================================================================
// Field-name globals at fixed offsets of a 24-byte record:
//   byte_B59848  (+0)   holder character id (byte)
//   dword_B5984C (+4)   city / slot value (int; -1 == vacant assignment)
//   byte_B59850  (+8)   office type (byte)
//   dword_B59854 (+12)  rank-level (int; < 4 to be assignable)
//   byte_B59858  (+16)  state (byte; == 3 -> vacant / electable)
// CanRunForOffice / ApplyForCandidacy scan up to 30 entries (stride 24, < 720).
// (GetEntryByCity/AddTableEntry scan 37 entries, < 888; the candidacy rules use
//  the 30-entry / 720-byte bound, faithfully preserved per function.)
constexpr int kOfficeHolderCount  = 30;
constexpr int kOfficeHolderStride = 24;

GUILD_PACKED_BEGIN
struct OfficeHolder {
    u8  holder;        // +0x00  holder character id
    u8  pad1[3];       // +0x01  alignment to the city dword
    i32 city;          // +0x04  city / slot value (-1 == vacant)
    u8  type;          // +0x08  office type
    u8  pad9[3];       // +0x09  alignment to the rank dword
    i32 rank;          // +0x0C  rank-level (< 4 assignable)
    u8  state;         // +0x10  state (3 == vacant/electable)
    u8  pad17[3];      // +0x11  alignment to the secondary dword
    i32 secondary;     // +0x14  (+20) secondary holder/owner id (dword_B5985C; -1 == none)
} GUILD_PACKED;
GUILD_PACKED_END
static_assert(sizeof(OfficeHolder) == kOfficeHolderStride, "OfficeHolder must be 24 bytes");
static_assert(offsetof(OfficeHolder, city)  == 4,  "city @+4");
static_assert(offsetof(OfficeHolder, type)  == 8,  "type @+8");
static_assert(offsetof(OfficeHolder, rank)  == 12, "rank @+12");
static_assert(offsetof(OfficeHolder, state) == 16, "state @+16");
static_assert(offsetof(OfficeHolder, secondary) == 20, "secondary @+20");

} // namespace guild::world
