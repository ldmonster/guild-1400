#pragma once
#include "guild/common/types.h"

#include <cstddef>

// =============================================================================
// guild::drm::detect — CopyProtect drive-detection + signature/checksum verify
// =============================================================================
//
// FAITHFUL 1:1 reconstruction (CLAUDE.md rules 1, 2, 8) of the SafeDisc-class
// CopyProtect sub-cluster of gilde.exe (overlay region, imagebase 0x400000):
//
//   0x141c3b0  VIBE_CopyProtect_SetSpeedParams
//   0x141c540  VIBE_CopyProtect_ProbeDriveGeometry
//   0x141c720  VIBE_CopyProtect_VerifyDiscSignature
//   0x141c980  VIBE_CopyProtect_VerifySectorChecksum
//   0x141cb90  VIBE_CopyProtect_ReadExeFooter
//   0x141e6a0  VIBE_CopyProtect_DetectVirtualDrive
//   0x141fe70  VIBE_CopyProtect_MatchDriveModelTable
//   0x14207b0  VIBE_CopyProtect_MatchDriveVendorTable
//
// RELATION TO src/drm/drm_stub.{h,cpp}
//   drm_stub already declares/DEFINES success-returning *stubs* named
//   guild::drm::SetSpeedParams / ProbeDriveGeometry / VerifyDiscSignature /
//   VerifySectorChecksum / DetectVirtualDrive (the OS/SCSI-bound entry points).
//   To avoid an ODR clash (CLAUDE.md "No ODR clashes"), the FAITHFUL pieces here
//   live in the nested namespace guild::drm::detect with distinct names. They
//   reconstruct the parts that are pure data logic — table matching, checksum /
//   signature math, EXE-footer parsing — and run on caller-supplied buffers, so
//   they are fully headless-testable. The original's actual disc reads / drive
//   geometry queries are abstracted behind the DiscDevice hook struct (inert
//   defaults), per CLAUDE.md rule 8 (no cheap analogues): we do not fake a disc,
//   we expose the hook the caller drives.
//
// THE TABLES ARE THE DETECTION
//   MatchDriveModelTable / MatchDriveVendorTable compare the drive's
//   INQUIRY-derived model (byte_145B5C0, 64 bytes) and vendor (byte_145CB28, 8B)
//   / product (byte_145CB30, 16B) fields against fixed tables stored in .rdata
//   (0x14522F0..0x145297F). In the binary those table bytes are stored in an
//   XOR-obfuscated form and the *input* fields are de-obfuscated by a rolling
//   self-XOR before the compare; the compare itself is plain VIBE_Mem_Compare
//   (memcmp-equality: 0 == match) or VIBE_Util_MemFindPattern (substring search).
//   We reproduce both the exact stored table bytes (byte-for-byte) and the exact
//   compare/substring semantics, so matching is bit-identical to the original.
// =============================================================================

namespace guild {
namespace drm {
namespace detect {

// -----------------------------------------------------------------------------
// gilde.exe 0x1422010 — VIBE_Mem_Compare  (__cdecl(_BYTE*,_BYTE*,unsigned))
// memcmp-style: 0 when the first `n` bytes are equal, else nonzero (the sign
// encodes ordering, but every CopyProtect call site only tests `== 0`).
// -----------------------------------------------------------------------------
int MemCompare(const u8* a, const u8* b, unsigned n);

// -----------------------------------------------------------------------------
// gilde.exe 0x140b000 — VIBE_Util_MemFindPattern  (__stdcall(int,int,int,int))
// Substring search: scans haystack[0..hayLen) for an occurrence of
// needle[0..needleLen). Returns the matching address (here: pointer) or null.
// The original returns int (a 32-bit pointer); callers test it as a boolean and
// (in DetectVirtualDrive) also use the returned address. We return const u8*.
// -----------------------------------------------------------------------------
const u8* MemFindPattern(const u8* haystack, const u8* needle, int hayLen,
                         int needleLen);

// =============================================================================
// Drive feature flags
// =============================================================================
// The original scatters its match results across ~50 global ints (dword_145A0xx
// etc.) consumed later by SetSpeedParams and the disc-read paths. We gather the
// ones the two table-matchers and SetSpeedParams actually read/write into one
// struct, preserving each field's original global address in a comment. A field
// is 1 (true) when the corresponding table entry matched, else 0.
struct DriveFlags {
    // Speed-relevant flags read by SetSpeedParams (0x141c3b0):
    int flag_1459FEC = 0; // dword_1459FEC  (SPTI-available selector; caller-set)
    int a145A04C = 0;     // dword_145A04C
    int a145A050 = 0;     // dword_145A050
    int a145A054 = 0;     // dword_145A054
    int a145A058 = 0;     // dword_145A058
    int a145A05C = 0;     // dword_145A05C
    int a145A060 = 0;     // dword_145A060
    int a145A064 = 0;     // dword_145A064
    int a145A068 = 0;     // dword_145A068
    int a145A06C = 0;     // dword_145A06C
    int a145A070 = 0;     // dword_145A070
    int a145A074 = 0;     // dword_145A074
    int a145A078 = 0;     // dword_145A078
    int a145A07C = 0;     // dword_145A07C

    // Geometry-gate flags read by ProbeDriveGeometry (0x141c540):
    int a145A09C = 0;     // dword_145A09C
    int a145A0A0 = 0;     // dword_145A0A0
    int a145A0A4 = 0;     // dword_145A0A4
    int a145A0AC = 0;     // dword_145A0AC
    int a145A0B0 = 0;     // dword_145A0B0
    int a145A0B4 = 0;     // dword_145A0B4
    int a145A14C = 0;     // dword_145A14C (TOC-probe selector; caller-set)

    // Remaining model/vendor match outputs (written by the table matchers):
    int a145A080 = 0;     // dword_145A080
    int a145A084 = 0;     // dword_145A084
    int a145A088 = 0;     // dword_145A088
    int a145A08C = 0;     // dword_145A08C
    int a145A090 = 0;     // dword_145A090
    int a145A094 = 0;     // dword_145A094
    int a145A098 = 0;     // dword_145A098
    int a145A0A8 = 0;     // dword_145A0A8
    int a145A0B8 = 0;     // dword_145A0B8
    int a145A0BC = 0;     // dword_145A0BC
    int a145A0C0 = 0;     // dword_145A0C0
    int a145A0C4 = 0;     // dword_145A0C4
    int a145A0C8 = 0;     // dword_145A0C8
    int a145A0CC = 0;     // dword_145A0CC
    int a145A0D0 = 0;     // dword_145A0D0
    int a145A0D4 = 0;     // dword_145A0D4
    int a145A0D8 = 0;     // dword_145A0D8
    int a145A0DC = 0;     // dword_145A0DC
    int a145A0E0 = 0;     // dword_145A0E0
    int a145A0E4 = 0;     // dword_145A0E4
    int a145A0E8 = 0;     // dword_145A0E8
    int a145A0EC = 0;     // dword_145A0EC
    int a145A0F0 = 0;     // dword_145A0F0
    int a145A0F4 = 0;     // dword_145A0F4
    int a145A0F8 = 0;     // dword_145A0F8
    int a145A1B0 = 0;     // dword_145A1B0
};

// =============================================================================
// gilde.exe 0x141fe70 — VIBE_CopyProtect_MatchDriveModelTable
// =============================================================================
// Matches a 64-byte INQUIRY-model buffer (byte_145B5C0) against the model table
// (.rdata 0x14522F0..0x14526F4) and sets the DriveFlags accordingly. `model`
// must point to >= 64 bytes (the original always passes the fixed 64-byte
// global). `byte145` is the byte at model+0x16 and `byte146` is model+0x15 (the
// original reads byte_145B5D6 / byte_145B5D5 for the a145A05C special case).
void MatchDriveModelTable(const u8* model, DriveFlags& out);

// =============================================================================
// gilde.exe 0x14207b0 — VIBE_CopyProtect_MatchDriveVendorTable
// =============================================================================
// Matches an 8-byte vendor id (byte_145CB28) plus a 16-byte product id
// (byte_145CB30) against the vendor table (.rdata 0x14522F0..0x145297F) and
// sets the DriveFlags. `vendor` >= 8 bytes, `product` >= 16 bytes.
void MatchDriveVendorTable(const u8* vendor, const u8* product, DriveFlags& out);

// =============================================================================
// gilde.exe 0x141c3b0 — VIBE_CopyProtect_SetSpeedParams
// =============================================================================
// Picks the (read-speed, retry-count) pair for the detected drive from the
// match flags. Original writes dword_145B5A0 (speed) and dword_145F39C (retry).
struct SpeedParams {
    int speed = 0;  // dword_145B5A0
    int retry = 1;  // dword_145F39C
};
SpeedParams SetSpeedParams(const DriveFlags& f);

// =============================================================================
// Checksum / signature verification
// =============================================================================
// gilde.exe 0x141c980 — VIBE_CopyProtect_VerifySectorChecksum (math core)
// The original sums dwords [21..512) of the 512-dword sector buffer
// (dword_145F3C0) and compares the sum against the 8-entry constant table
// dword_142DD70 (only entry [0] is nonzero in the shipped binary). It also
// accumulates the table into a running total. We expose the pure arithmetic:
// returns the 32-bit wrapping sum of buf[21..512).
u32 SectorChecksumSum(const u32* sectorDwords /* >= 512 */);

// The 8-entry checksum constant table dword_142DD70. Returned by-ref so callers
// (and tests) can see the exact shipped bytes. Index 0 is the only nonzero.
const u32* SectorChecksumTable();   // 8 entries
const u32* DiscSignatureTable();    // 8 entries (dword_142DD90; all zero shipped)

// Does `sum` match any of the 8 checksum-table entries? (the original's
// `for(i<8) if(sum==tbl[i]) break;` loop; match == i<8).
bool SectorChecksumMatches(u32 sum);

// gilde.exe 0x141c720 — VIBE_CopyProtect_VerifyDiscSignature (table-match core)
// Two loops over the 8-entry signature table dword_142DD90:
//  (1) "all zero?" — if every entry is 0, treat as genuine.
//  (2) else, does `sig` equal any entry? match == genuine.
// Returns true when the disc should be treated as genuine.
bool DiscSignatureMatches(u32 sig);

// =============================================================================
// gilde.exe 0x141cb90 — VIBE_CopyProtect_ReadExeFooter (parse core)
// =============================================================================
// The original opens GILDE.EXE, seeks to EOF-4 to read a little-endian u32
// offset, seeks there, reads a 132-byte footer record, MD5s it, and validates a
// stored byte-swapped MD5-fold trailer plus a magic dword. It also recognizes
// "PLAYBACK"-family marker prefixes ("\\.\PLAYBACK", "FWS", "59JP"/"00JP"/"10JP")
// that select an alternate footer location. We expose the pure parse/verify of a
// 132-byte footer buffer once it has been read (the file I/O is the DiscDevice
// hook). Layout recovered from the decompile + get_bytes:
//   footer[0x00..0x84)  : 132-byte signed region fed to MD5
//   The MD5 of those 132 bytes is folded by XORing its 4 dwords together
//   (v3^v4^v5^v6); the result is byte-reversed and must equal dword_145CA96
//   (footer field "expectedFold", a u32 at a caller-known offset), and a footer
//   status field dword_145CA8E must equal dword_142EEBC == 9 (kExeFooterMagic).
struct ExeFooter {
    u8  bytes[132];        // byte_145CA40 — the signed 0x84-byte region
    u32 expectedFold;      // dword_145CA96 — stored byte-swapped MD5 fold
    u32 statusField;       // dword_145CA8E — must equal kExeFooterMagic
};

// dword_142EEBC — the EXE-footer magic the status field must equal (== 9).
constexpr u32 kExeFooterMagic = 9u;
// dword_142DDB0 — length recorded for the "UKD_548520-001.001" master string.
constexpr u32 kMasterIdLength = 18u;

// Compute the byte-reversed XOR-fold of the MD5 of a 132-byte buffer, exactly as
// the original (md5(buf)[0]^[1]^[2]^[3], then bswap32). Requires VIBE_Md5_*.
u32 ExeFooterFold(const u8* footer132);

// Verify a parsed footer: fold matches expectedFold AND statusField == magic.
bool VerifyExeFooter(const ExeFooter& f);

// The "UKD_548520-001.001" master-disc id string (aUkd54852000100,
// 0x142DD28) that DetectVirtualDrive compares the read sector buffer against.
const char* MasterDiscId();

// =============================================================================
// DiscDevice — inert-default hook for the real hardware leaves
// =============================================================================
// Everything the original does against the OS / a physical disc lives here. The
// portable build supplies inert defaults (report "no disc / not present"); a
// real backend (or a test) can override. The faithful logic above never touches
// hardware directly — it consumes data these hooks (or the caller) provide.
struct DiscDevice {
    virtual ~DiscDevice() = default;

    // gilde.exe 0x141c540 leaves: read the TOC start/end LBA bounds for the drive.
    // Originals: VIBE_Disc_SptiReadTocBounds / VIBE_Disc_AspiReadTocBounds.
    // Return nonzero on success and fill startLba/endLba. Default: fail.
    virtual int ReadTocBounds(u32& startLba, u32& endLba) {
        startLba = 0;
        endLba = 0;
        return 0;
    }

    // gilde.exe 0x141cb90 leaf: read `len` bytes of the EXE footer at file offset
    // `off` into `dst`. Default: nothing read.
    virtual int ReadExeBytes(u32 /*off*/, u8* /*dst*/, u32 /*len*/) { return 0; }

    // gilde.exe DetectVirtualDrive leaf: read a 2048-byte raw sector (LBA 16) into
    // the 512-dword buffer. Default: no media.
    virtual int ReadRawSector(u32 /*lba*/, u32* /*dst512*/) { return 0; }

    // gilde.exe 0x141c720 leaf: read the disc lead-out / TOC to derive the disc
    // signature value. Default: 0 (no disc) and report failure.
    virtual int ReadDiscSignature(u32& sig) {
        sig = 0;
        return 0;
    }
};

// =============================================================================
// gilde.exe 0x141c540 — VIBE_CopyProtect_ProbeDriveGeometry (gate + fold core)
// =============================================================================
// Gated read of the TOC bounds via DiscDevice, then the original's bound check
// and the 16-bit fold it writes into dword_145A568. We return the computed
// result rather than scribbling a global. `gateOpen` mirrors the original's
// guard (none of the blocking flags set, and a TOC/SPTI selector is on).
struct GeometryResult {
    int ioOk = 0;       // dword_145A16C — read succeeded
    u32 startLba = 0;   // dword_145A164
    u32 endLba = 0;     // dword_145A168
    int inRange = 0;    // dword_145A170 — bounds passed the magic gate
    u16 fold = 0;       // value folded into *dword_145A568 when inRange
};
GeometryResult ProbeDriveGeometry(const DriveFlags& f, DiscDevice& dev,
                                  u16 foldSeed);

} // namespace detect
} // namespace drm
} // namespace guild
