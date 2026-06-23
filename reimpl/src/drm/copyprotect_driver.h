#pragma once
// =============================================================================
// guild::drm — CopyProtect driver extraction / loading + DiscIo timer thunks
// =============================================================================
//
// 1:1 reconstruction of the SafeDisc/SecuROM-class "sintf"/ASPI driver bring-up
// sub-cluster of gilde.exe (32-bit x86, imagebase 0x400000; protection overlay at
// >= 0x140b000). User-approved DRM reconstruction.
//
// Reconstructed functions (provenance kept on each definition in the .cpp):
//   0x141d0f0  VIBE_CopyProtect_ExtractSintfDriver   -> ExtractSintfDriver
//   0x141d890  VIBE_CopyProtect_LoadDriverExports    -> LoadDriverExports
//   0x141ddd0  VIBE_CopyProtect_ResolveAspiFunctions -> ResolveAspiFunctions
//   0x141e0d0  VIBE_CopyProtect_ScanScsiDevices      -> ScanScsiDevices
//   0x1416080  VIBE_DiscIo_QueryFrequency_Thunk      -> QueryFrequencyThunk
//   0x14160a0  VIBE_DiscIo_QueryCounter_Thunk        -> QueryCounterThunk
//   0x14160c0  VIBE_DiscIo_ElapsedSeconds            -> ElapsedSeconds
//   0x14165a0  VIBE_DiscIo_ReadSectorRaw             -> ReadSectorRaw
//   0x1417db0  VIBE_DiscIo_HasTimerFrequency         -> HasTimerFrequency
//
// WHAT THE ORIGINAL DOES (verified against the Hex-Rays decompile)
//   LoadDriverExports : de-obfuscates the sintf driver basename (NT branch uses
//     byte_1452144="sintfnt.dll"; 9x branch uses byte_1452138="sintf32.dll"),
//     concatenates it onto the system-dir buffer aCWindowsSystem, LoadLibrary's
//     it (dword_1467738), and resolves a fixed ordinal set via GetProcAddress
//     (dword_1467718) into individual globals. NT resolves ordinals
//     2..15,17,18; 9x resolves 2..15. If any required slot is null, the original
//     calls Drm_StateDispatch(0xC NT / 0xA 9x) then Crt_Exit(12 NT / 10 9x).
//   ResolveAspiFunctions : LoadLibrary("wnaspi32.dll"), then GetProcAddress for
//     "GetASPI32SupportInfo","SendASPI32Command","TranslateASPI32Address" (by
//     name). If the dll or GetSupportInfo/SendCommand are missing, ASPI is marked
//     unavailable. When available, calls GetASPI32SupportInfo(); stores the raw
//     return; hostAdapterCount=(char)ret, statusByte=BYTE1(ret); ASPI stays
//     available only if BYTE1(ret)==1.
//   ScanScsiDevices : if TranslateASPI32Address yields a forced (adapter,target,
//     lun) (dword_145AE9C != 0x80000000) it probes that single triple; otherwise
//     it iterates adapters 0..hostAdapterCount-1 (target dword_145A024=0,
//     lun dword_145A020=0) while ASPI available and not yet found. Each device is
//     probed via SendASPI32Command (HA_INQUIRY, then device INQUIRY type==5),
//     TEST UNIT READY x2, READ sector 16, volume-id compare at sector offset 0x28
//     against "UKD_548520-001.001", and a checksum = sum of sector bytes[21..511]
//     compared to dword_142DD70[0]. NOTE: in the original, a *failed* volume-id
//     compare ALSO sets found=1 (the else branch); a passing volume-id only sets
//     found when the checksum also matches. Reconstructed 1:1.
//   ExtractSintfDriver : opens archive dword_145BA10, iterates dword_145CA44
//     records; for each, reads a 48-byte header (&dword_145EB80 => offset
//     dword_145EB80 + length dword_145EB84), opens/creates the output file
//     "C:\\WINDOWS\\SYSTEM\\sintf32.dll" (aCWindowsSystem), seeks to offset in
//     archive, reads `length` payload bytes, writes them out. Then it runs THREE
//     de-obfuscate-and-compare blocks (seeds byte_145212C="SIntf16.dll",
//     byte_1452120="SIntf32.dll", byte_1452114="SIntfNT.dll"); each compares the
//     decoded name to aSintfntDll="SIntfNT.dll" and, on match, runs a distinct
//     integrity scramble over the payload folding into byte_145AE60[9],
//     a 16-bit checksum (*dword_145F134) and the global blob dword_142DD60.
//   DiscIo timers : QueryFrequencyThunk/QueryCounterThunk are indirect calls
//     through dword_1467750/dword_146774C (populated to
//     QueryPerformanceFrequency/Counter). ElapsedSeconds returns
//     ((float)((c2-c1)*1000)) / (float)freq as double. HasTimerFrequency gates on
//     QueryFrequencyThunk(&staticFreq) != 0. ReadSectorRaw reads one sector into a
//     4096-byte stack buffer via VIBE_Disc_ReadSector(buf, lba, 1) then memmoves
//     `copyLen` bytes to dst.
//
// OS COUPLINGS -> DrmDriverOs hooks (rule 6 / platform boundary). The pure control flow,
// name lists, scan loop, scramble math, and timer math are reconstructed 1:1 and
// tested; the actual OS calls (file write, LoadLibrary/GetProcAddress,
// QueryPerformanceFrequency/Counter, ASPI/disc transactions) are routed through an
// injectable hooks struct whose defaults are inert. NEVER faked: the logic that
// decides what to write, which ordinals to resolve, how to score a device, and how
// the integrity scramble accumulates is all real.
// =============================================================================

#include "guild/common/types.h"
#include <cstdint>
#include <string>
#include <vector>
#include <array>

namespace guild::drm {

using guild::u8;
using guild::u16;
using guild::u32;
using guild::u64;
using guild::i32;
using guild::i64;
using f64 = double;

// -----------------------------------------------------------------------------
// DeobfuscateName — the XOR-chain string de-obfuscator used pervasively in the
// cluster:  for (i = len-2; i >= 0; --i) s[i] ^= s[i+1];  (the original copies
// `len` obfuscated bytes into aPlaybackletter, runs the loop with dword_145A800
// from len-2 down to 0, copies them to aPlayback, then writes a terminating NUL).
// `len` is the number of source bytes the original copies (e.g. 0xB for the
// 11-char driver names; 0xC/0x11/0x14/0x16 for the ASPI strings). The recovered
// ASCII (up to the first NUL) is returned without a trailing terminator.
// -----------------------------------------------------------------------------
std::string DeobfuscateName(const u8* obfuscated, std::size_t len);

// -----------------------------------------------------------------------------
// Recovered name tables (de-obfuscated). Exposed so tests can assert the exact
// export/device strings the original resolves.
// -----------------------------------------------------------------------------

// sintf driver dll basenames recovered from the obfuscated seeds:
//   [0] byte_1452144 = "sintfnt.dll"  (LoadDriverExports NT branch)
//   [1] byte_1452138 = "sintf32.dll"  (LoadDriverExports 9x branch)
//   [2] byte_145212C = "SIntf16.dll"  (ExtractSintfDriver scramble block 1 seed)
//   [3] byte_1452120 = "SIntf32.dll"  (ExtractSintfDriver scramble block 2 seed)
//   [4] byte_1452114 = "SIntfNT.dll"  (ExtractSintfDriver scramble block 3 seed)
extern const std::array<const char*, 5> kSintfDriverNames;

// wnaspi32.dll + its three named exports, in resolution order.
extern const char* const kAspiDllName;            // "wnaspi32.dll"      byte_1452194
extern const char* const kAspiGetSupportInfo;     // "GetASPI32SupportInfo"  byte_145217C
extern const char* const kAspiSendCommand;        // "SendASPI32Command"     byte_1452168
extern const char* const kAspiTranslateAddress;   // "TranslateASPI32Address" byte_1452150

// Genuine-disc volume identifier compared at sector offset 0x28 (aUkd54852000100).
extern const char* const kGenuineVolumeId;        // "UKD_548520-001.001"

// sintf system directory prefix (aCWindowsSystem_0).
extern const char* const kSystemDir;              // "C:\\WINDOWS\\SYSTEM\\"

// The output basename ExtractSintfDriver always writes (the aCWindowsSystem buffer
// is seeded "C:\\WINDOWS\\SYSTEM\\sintf32.dll" but the path is rebuilt each record
// as system-dir + this basename, decoded from aSintfntDll => "sintf32.dll").
// Exposed for path assertions; the literal in the binary is "sintf32.dll".
extern const char* const kExtractOutputName;      // "sintf32.dll"

// The name the three scramble blocks compare each decoded seed against
// (aSintfntDll). Only the SIntfNT block (seed [4]) matches this.
extern const char* const kScrambleMatchName;      // "SIntfNT.dll"

// -----------------------------------------------------------------------------
// DrmDriverOs — injectable OS hooks (inert defaults). Every member maps to one Win32
// call the original made. Defaults are null -> built-in inert behavior so a
// headless build never touches real hardware or the filesystem.
// -----------------------------------------------------------------------------
struct DrmDriverOs {
    // Write `data` to file `path`. Return true on success.
    // (file open/create + write in ExtractSintfDriver.) Default: inert success.
    bool (*writeFile)(const std::string& path, const std::vector<u8>& data) = nullptr;

    // LoadLibrary(name) -> opaque non-null handle on success, 0 on failure.
    // (dword_1467738.)
    std::uintptr_t (*loadLibrary)(const std::string& name) = nullptr;

    // GetProcAddress by ORDINAL -> opaque non-null address, 0 if missing.
    // (dword_1467718 in LoadDriverExports.)
    std::uintptr_t (*getProcByOrdinal)(std::uintptr_t handle, u16 ordinal) = nullptr;

    // GetProcAddress by NAME -> opaque non-null address, 0 if missing.
    // (dword_1467718 in ResolveAspiFunctions.)
    std::uintptr_t (*getProcByName)(std::uintptr_t handle, const std::string& name) = nullptr;

    // QueryPerformanceFrequency thunk target (dword_1467750). Fills *out; returns
    // nonzero on success.
    int (*perfFrequency)(i64* out) = nullptr;

    // QueryPerformanceCounter thunk target (dword_146774C). Fills *out; returns
    // nonzero on success.
    int (*perfCounter)(i64* out) = nullptr;
};

// Process-wide inert default hook set (all null -> built-in no-op behavior).
const DrmDriverOs& DefaultDrmOs();

// =============================================================================
// gilde.exe 0x141d890 — VIBE_CopyProtect_LoadDriverExports
// =============================================================================
struct DriverExports {
    bool        loaded = false;          // dword_1459FF4
    std::uintptr_t module = 0;           // dword_1459FF8
    std::string driverPath;              // system dir + sintf basename
    // Resolved exports keyed by ordinal (only the ones the original resolves),
    // in original resolution order. address==0 means GetProcAddress failed.
    std::vector<std::pair<u16, std::uintptr_t>> exports;
    bool        allRequiredPresent = false;
    int         exitCode = 0;            // 0 = ok; 10 (9x) / 12 (NT) on failure
};

// The exact ordinal set resolved on each OS branch, in original order.
extern const std::vector<u16> kNtDriverOrdinals;  // {2..15,17,18}
extern const std::vector<u16> kWinOrdinals;       // {2..15}

DriverExports LoadDriverExports(bool isNt, const DrmDriverOs& os = DefaultDrmOs());

// =============================================================================
// gilde.exe 0x141ddd0 — VIBE_CopyProtect_ResolveAspiFunctions
// =============================================================================
struct AspiBinding {
    bool        available = false;       // dword_145A14C
    std::uintptr_t module = 0;           // dword_145A148
    std::uintptr_t getSupportInfo = 0;   // dword_145A130
    std::uintptr_t sendCommand = 0;      // dword_145A134
    std::uintptr_t translateAddress = 0; // dword_145A138
    u32         supportInfoRaw = 0;      // dword_145CBE8 (raw GetASPI32SupportInfo ret)
    u8          hostAdapterCount = 0;    // (char)result -> byte_145F058
    u8          statusByte = 0;          // BYTE1(result) -> byte_145AE88
};

// GetASPI32SupportInfo result provider (the original calls the resolved pointer
// with no args and reads (char)/BYTE1 of the eax return). Default: returns 0.
using AspiSupportInfo = u32 (*)();

AspiBinding ResolveAspiFunctions(const DrmDriverOs& os = DefaultDrmOs(),
                                 AspiSupportInfo supportInfo = nullptr);

// =============================================================================
// gilde.exe 0x141e0d0 — VIBE_CopyProtect_ScanScsiDevices
// =============================================================================
// Per-device probe result the scan loop consumes (abstracts the SendASPI32Command
// HA_INQUIRY / device INQUIRY / TEST UNIT READY / READ sequence).
struct ScsiProbe {
    bool haInquiryOk = false;   // first SendASPI32Command (HA inquiry) status==1
    bool present = false;       // device INQUIRY status==1 && deviceType==5
    bool readOk = false;        // sector-16 read succeeded
    std::array<u8, 512> sector{}; // raw sector 16 (offset 0x28 = volume id, [21..511] summed)
};

// Scanner: probe one (adapter,target,lun) triple. Default returns {} (no device).
using ScsiScanner = ScsiProbe (*)(int adapter, int target, int lun);

struct ScsiScanResult {
    bool   found = false;        // dword_145A150
    int    adapter = 0;          // dword_145A13C
    int    target = 0;           // dword_145A140
    int    lun = 0;              // dword_145A144
    u32    checksum = 0;         // dword_145E214 (sum of sector bytes [21..511])
    bool   volumeIdMatched = false;
    bool   checksumMatched = false;
};

// `hostAdapterCount` = byte_145F058 from ResolveAspiFunctions. `aspiAvailable` =
// dword_145A14C. `expectedChecksum` = dword_142DD70[0]. The original's fixed
// initial target = dword_145A024 = 0, lun = dword_145A020 = 0.
ScsiScanResult ScanScsiDevices(int hostAdapterCount,
                               bool aspiAvailable,
                               u32 expectedChecksum,
                               ScsiScanner scan = nullptr);

// =============================================================================
// gilde.exe 0x141d0f0 — VIBE_CopyProtect_ExtractSintfDriver
// =============================================================================
struct SintfRecord {
    std::string     name;       // decoded driver basename for this record (unused by write)
    u32             offset = 0; // dword_145EB80 — byte offset into archive
    u32             length = 0; // dword_145EB84 — payload length
    std::vector<u8> payload;    // raw payload bytes (size == length)
};

// Provider: yields record `index` of the archive. Default: yields nothing.
using SintfRecordProvider = bool (*)(int index, SintfRecord& out);

struct ExtractResult {
    int                       recordsWritten = 0;
    std::vector<std::string>  outputPaths;     // each system-dir output path written
    // State after the LAST record that hit a scramble block. Recovered exactly for
    // golden-vector testing.
    bool                      scrambleRan = false;     // any of the 3 blocks ran
    int                       scrambleBlock = 0;       // 1, 2, or 3 (which block matched)
    std::array<u8, 9>         integrityAccum{};        // byte_145AE60
    u16                       payloadChecksum = 0;     // *(u16*)dword_145F134
    std::array<u8, 9>         globalBlob{};            // dword_142DD60 9-byte fold
    u32                       globalBlobTag = 0;       // dword_142DD64 (block1) / +2 word (block2)
    u32                       matchedLength = 0;       // dword_145B90C (block3)
};

// `isNt` selects the path-build branch (NT: direct concat of dir + basename; 9x:
// dir + "\\" then basename). `provider` yields archive records. The decoded
// scramble-seed name for each record's three blocks is fixed in the binary
// (SIntf16/SIntf32/SIntfNT); the .cpp reproduces all three with their exact
// per-block index offsets and accumulator ops.
ExtractResult ExtractSintfDriver(bool isNt,
                                 int entryCount,
                                 SintfRecordProvider provider,
                                 const DrmDriverOs& os = DefaultDrmOs());

// The three integrity-scramble blocks applied to a payload when its decoded seed
// name matches "SIntfNT.dll". Exposed individually for golden testing. Each takes
// the payload (mutated for block 1), the 9-byte accumulator (byte_145AE60), the
// running 16-bit checksum, the 9-byte global blob (dword_142DD60) and its tag.
//
// Block 1 (seed "SIntf16.dll", 0x141d401): allocates checksum, zeroes accum, then
//   for i in [0,len-1): payload[i] ^= payload[i+1];
//                       accum[i%9] += payload[i];
//                       accum[(i+3)%9] ^= payload[i];
//                       checksum += (u8)payload[i];
//   then tag ^= len; for j in 0..8 blob[j] += accum[j].
void ScrambleBlock1(std::vector<u8>& payload, std::array<u8, 9>& accum,
                    u16& checksum, std::array<u8, 9>& blob, u32& tag);

// Block 2 (seed "SIntf32.dll", 0x141d5db): (does NOT pre-mutate payload)
//   for i in [0,len): accum[i%9] += payload[i];
//                     accum[(i+4)%9] ^= payload[i];
//                     checksum += (u8)payload[i];
//   then (blob bytes [2..5]) ^= len; for j in 0..8 blob[j] ^= accum[j].
void ScrambleBlock2(const std::vector<u8>& payload, std::array<u8, 9>& accum,
                    u16& checksum, std::array<u8, 9>& blob, u32& tag);

// Block 3 (seed "SIntfNT.dll", 0x141d787): (does NOT pre-mutate payload)
//   for i in [0,len): accum[i%9] += payload[i];
//                     accum[(i+5)%9] ^= payload[i];
//                     checksum += (u8)payload[i];
//   then matchedLength = len.
void ScrambleBlock3(const std::vector<u8>& payload, std::array<u8, 9>& accum,
                    u16& checksum, u32& matchedLength);

// =============================================================================
// DiscIo timer thunks
// =============================================================================

// gilde.exe 0x1416080 — VIBE_DiscIo_QueryFrequency_Thunk
// Indirect call through dword_1467750 (QueryPerformanceFrequency). Returns its int.
int QueryFrequencyThunk(i64* out, const DrmDriverOs& os = DefaultDrmOs());

// gilde.exe 0x14160a0 — VIBE_DiscIo_QueryCounter_Thunk
// Indirect call through dword_146774C (QueryPerformanceCounter). Returns its int.
int QueryCounterThunk(i64* out, const DrmDriverOs& os = DefaultDrmOs());

// gilde.exe 0x14160c0 — VIBE_DiscIo_ElapsedSeconds
// return (float)((c2-c1)*1000) / (float)freq, computed in single precision and
// returned as double (the *1000 makes the value milliseconds, per the original).
f64 ElapsedSeconds(i64 counter1, i64 counter2, i64 frequency);

// gilde.exe 0x1417db0 — VIBE_DiscIo_HasTimerFrequency
// Gate: QueryFrequencyThunk(&staticFreq) != 0. `outFreq` optionally receives it.
bool HasTimerFrequency(const DrmDriverOs& os = DefaultDrmOs(), i64* outFreq = nullptr);

// gilde.exe 0x14165a0 — VIBE_DiscIo_ReadSectorRaw
// Reads one sector into a 4096-byte stack buffer via VIBE_Disc_ReadSector(buf,
// lba, 1), then memmoves `copyLen` bytes out to `dst`. The disc read is injectable;
// default reads nothing (returns 0). Returns the disc-read status.
using DiscSectorReader = int (*)(u8* buf4096, int lba);
int ReadSectorRaw(int lba, u32 copyLen, u8* dst, DiscSectorReader reader = nullptr);

} // namespace guild::drm
