// =============================================================================
// guild::drm — CopyProtect driver extraction / loading + DiscIo timer thunks
// 1:1 reconstruction of the gilde.exe protection bring-up sub-cluster.
// See copyprotect_driver.h for the per-function provenance overview.
// =============================================================================
#include "copyprotect_driver.h"

#include <cstring>

namespace guild::drm {

// -----------------------------------------------------------------------------
// Embedded obfuscated seed blobs, byte-exact from the binary (recovered with
// get_bytes). Kept here so the de-obfuscated name tables below are *derived* from
// the original bytes via DeobfuscateName, not hand-typed — proving 1:1 decode.
// Each `len` is the byte count the original copies into aPlaybackletter.
// -----------------------------------------------------------------------------
namespace {

// 0x1452144 "sintfnt.dll", 0x1452138 "sintf32.dll", 0x145212C "SIntf16.dll",
// 0x1452120 "SIntf32.dll", 0x1452114 "SIntfNT.dll"  (len 0xB each)
constexpr u8 kSeed1452144[] = {0x1a,0x07,0x1a,0x12,0x08,0x1a,0x5a,0x4a,0x08,0x00,0x6c};
constexpr u8 kSeed1452138[] = {0x1a,0x07,0x1a,0x12,0x55,0x01,0x1c,0x4a,0x08,0x00,0x6c};
constexpr u8 kSeed145212C[] = {0x1a,0x27,0x1a,0x12,0x57,0x07,0x18,0x4a,0x08,0x00,0x6c};
constexpr u8 kSeed1452120[] = {0x1a,0x27,0x1a,0x12,0x55,0x01,0x1c,0x4a,0x08,0x00,0x6c};
constexpr u8 kSeed1452114[] = {0x1a,0x27,0x1a,0x12,0x28,0x1a,0x7a,0x4a,0x08,0x00,0x6c};

// ASPI seeds.
constexpr u8 kSeed1452194[] = // "wnaspi32.dll"  len 0xC
    {0x19,0x0f,0x12,0x03,0x19,0x5a,0x01,0x1c,0x4a,0x08,0x00,0x6c};
constexpr u8 kSeed145217C[] = // "GetASPI32SupportInfo" len 0x14
    {0x22,0x11,0x35,0x12,0x03,0x19,0x7a,0x01,0x61,0x26,
     0x05,0x00,0x1f,0x1d,0x06,0x3d,0x27,0x08,0x09,0x6f};
constexpr u8 kSeed1452168[] = // "SendASPI32Command" len 0x11
    {0x36,0x0b,0x0a,0x25,0x12,0x03,0x19,0x7a,0x01,0x71,
     0x2c,0x02,0x00,0x0c,0x0f,0x0a,0x64};
constexpr u8 kSeed1452150[] = // "TranslateASPI32Address" len 0x16
    {0x26,0x13,0x0f,0x1d,0x1f,0x0d,0x15,0x11,0x24,0x12,0x03,
     0x19,0x7a,0x01,0x73,0x25,0x00,0x16,0x17,0x16,0x00,0x73};

// Lazily-decoded persistent storage for the exported name tables.
struct DecodedNames {
    std::string sintf[5];
    std::string aspiDll, aspiGet, aspiSend, aspiTranslate;
    DecodedNames();
};

} // namespace

// -----------------------------------------------------------------------------
// gilde.exe — the pervasive XOR-chain de-obfuscator (the
// `for (i = len-2; i >= 0; --i) s[i] ^= s[i+1];` loop, with dword_145A800 as i).
// -----------------------------------------------------------------------------
std::string DeobfuscateName(const u8* obfuscated, std::size_t len) {
    if (len == 0) return std::string();
    std::vector<u8> s(obfuscated, obfuscated + len);
    // dword_145A800 = len-2; while (>= 0) s[i] ^= s[i+1]; --i
    for (std::ptrdiff_t i = static_cast<std::ptrdiff_t>(len) - 2; i >= 0; --i)
        s[static_cast<std::size_t>(i)] ^= s[static_cast<std::size_t>(i) + 1];
    // ASCII up to first NUL (the original then writes aPlayback[len]=0 / [len-1]=0).
    std::string out;
    for (std::size_t i = 0; i < len; ++i) {
        if (s[i] == 0) break;
        out.push_back(static_cast<char>(s[i]));
    }
    return out;
}

namespace {
DecodedNames::DecodedNames() {
    sintf[0] = DeobfuscateName(kSeed1452144, sizeof(kSeed1452144)); // sintfnt.dll
    sintf[1] = DeobfuscateName(kSeed1452138, sizeof(kSeed1452138)); // sintf32.dll
    sintf[2] = DeobfuscateName(kSeed145212C, sizeof(kSeed145212C)); // SIntf16.dll
    sintf[3] = DeobfuscateName(kSeed1452120, sizeof(kSeed1452120)); // SIntf32.dll
    sintf[4] = DeobfuscateName(kSeed1452114, sizeof(kSeed1452114)); // SIntfNT.dll
    aspiDll       = DeobfuscateName(kSeed1452194, sizeof(kSeed1452194));
    aspiGet       = DeobfuscateName(kSeed145217C, sizeof(kSeed145217C));
    aspiSend      = DeobfuscateName(kSeed1452168, sizeof(kSeed1452168));
    aspiTranslate = DeobfuscateName(kSeed1452150, sizeof(kSeed1452150));
}

const DecodedNames& Names() {
    static const DecodedNames g;
    return g;
}
} // namespace

// -----------------------------------------------------------------------------
// Exported name tables / literals.
// -----------------------------------------------------------------------------
const std::array<const char*, 5> kSintfDriverNames = {
    Names().sintf[0].c_str(), Names().sintf[1].c_str(), Names().sintf[2].c_str(),
    Names().sintf[3].c_str(), Names().sintf[4].c_str()};

const char* const kAspiDllName          = Names().aspiDll.c_str();
const char* const kAspiGetSupportInfo   = Names().aspiGet.c_str();
const char* const kAspiSendCommand      = Names().aspiSend.c_str();
const char* const kAspiTranslateAddress = Names().aspiTranslate.c_str();

const char* const kGenuineVolumeId   = "UKD_548520-001.001"; // aUkd54852000100
const char* const kSystemDir         = "C:\\WINDOWS\\SYSTEM\\"; // aCWindowsSystem_0
const char* const kExtractOutputName = "sintf32.dll";          // aSintfntDll decode
const char* const kScrambleMatchName = "SIntfNT.dll";          // aSintfntDll @0x145eba0

const std::vector<u16> kNtDriverOrdinals = {2,3,4,5,6,7,8,9,10,11,12,13,14,15,17,18};
const std::vector<u16> kWinOrdinals      = {2,3,4,5,6,7,8,9,10,11,12,13,14,15};

const DrmDriverOs& DefaultDrmOs() {
    static const DrmDriverOs g{};
    return g;
}

// =============================================================================
// gilde.exe 0x141d890 — VIBE_CopyProtect_LoadDriverExports
// =============================================================================
DriverExports LoadDriverExports(bool isNt, const DrmDriverOs& os) {
    DriverExports r;

    // NT branch decodes byte_1452144="sintfnt.dll"; 9x decodes byte_1452138="sintf32.dll".
    const std::string basename = isNt ? Names().sintf[0] : Names().sintf[1];
    // aCWindowsSystem is seeded with the system dir; VIBE_String_Concat appends.
    r.driverPath = std::string(kSystemDir) + basename;

    const std::vector<u16>& ordinals = isNt ? kNtDriverOrdinals : kWinOrdinals;

    // dword_1459FF8 = LoadLibrary(path)  (dword_1467738)
    if (os.loadLibrary)
        r.module = os.loadLibrary(r.driverPath);

    if (r.module == 0) {
        // VIBE_Drm_StateDispatch(0xA/0xC); Crt_Exit(10/12)
        r.loaded = false;
        r.allRequiredPresent = false;
        r.exitCode = isNt ? 12 : 10;
        return r;
    }

    r.loaded = true; // dword_1459FF4 = 1
    bool allPresent = true;
    for (u16 ord : ordinals) {
        std::uintptr_t addr =
            os.getProcByOrdinal ? os.getProcByOrdinal(r.module, ord) : 0;
        r.exports.emplace_back(ord, addr);
        if (addr == 0) allPresent = false;
    }
    // The original's gate checks every resolved slot is non-null. (Ordinals 17/18
    // are NT-only; the 9x gate simply lacks them — both reduce to "all resolved".)
    r.allRequiredPresent = allPresent;
    r.exitCode = allPresent ? 0 : (isNt ? 12 : 10);
    return r;
}

// =============================================================================
// gilde.exe 0x141ddd0 — VIBE_CopyProtect_ResolveAspiFunctions
// =============================================================================
AspiBinding ResolveAspiFunctions(const DrmDriverOs& os, AspiSupportInfo supportInfo) {
    AspiBinding b;

    // LoadLibrary("wnaspi32.dll")  (dword_1467738)
    b.module = os.loadLibrary ? os.loadLibrary(Names().aspiDll) : 0;
    if (b.module == 0) {
        b.available = false; // dword_145A14C = 0
        return b;
    }

    b.available = true; // dword_145A14C = 1

    // GetProcAddress by name for the three exports (dword_1467718).
    b.getSupportInfo =
        os.getProcByName ? os.getProcByName(b.module, Names().aspiGet) : 0;
    if (b.getSupportInfo == 0) b.available = false;

    b.sendCommand =
        os.getProcByName ? os.getProcByName(b.module, Names().aspiSend) : 0;
    if (b.sendCommand == 0) b.available = false;

    // TranslateASPI32Address is resolved but NOT gated (its absence does not clear
    // availability in the original).
    b.translateAddress =
        os.getProcByName ? os.getProcByName(b.module, Names().aspiTranslate) : 0;

    if (b.available) {
        // result = GetASPI32SupportInfo(); dword_145A13C/140/144 = 0 already.
        u32 result = supportInfo ? supportInfo() : 0;
        b.supportInfoRaw    = result;                          // dword_145CBE8
        b.statusByte        = static_cast<u8>((result >> 8) & 0xFF); // BYTE1 -> byte_145AE88
        b.hostAdapterCount  = static_cast<u8>(result & 0xFF);  // (char) -> byte_145F058
        b.available         = (b.statusByte == 1);             // dword_145A14C = BYTE1==1
    }
    return b;
}

// =============================================================================
// gilde.exe 0x141e0d0 — VIBE_CopyProtect_ScanScsiDevices
// =============================================================================
// We model only the iterating branch (dword_145AE9C == 0x80000000, i.e. no forced
// triple from TranslateASPI32Address). The forced-triple branch shares the same
// per-device probe; the loop body is identical save for the loop control. The
// reconstruction below reproduces the iterating branch exactly: target=0
// (dword_145A024), lun=0 (dword_145A020), iterate adapter 0..count-1 while ASPI is
// available and not yet found.
ScsiScanResult ScanScsiDevices(int hostAdapterCount,
                               bool aspiAvailable,
                               u32 expectedChecksum,
                               ScsiScanner scan) {
    ScsiScanResult r;

    const int target = 0; // dword_145A024
    const int lun = 0;    // dword_145A020

    for (int adapter = 0;
         adapter < hostAdapterCount && aspiAvailable && !r.found;
         ++adapter) {
        ScsiProbe p = scan ? scan(adapter, target, lun) : ScsiProbe{};

        // First SendASPI32Command (HA inquiry): status must == 1.
        if (!p.haInquiryOk) continue;
        // Device INQUIRY: status==1 && device type byte_145DB3A == 5 (CD-ROM).
        if (!p.present) continue;
        // TEST UNIT READY (called twice; second result gates).
        // READ sector 16.
        if (!p.readOk) continue;

        // Volume id compare at sector offset 0x28 (40) vs "UKD_548520-001.001".
        const std::string vid = kGenuineVolumeId;
        const std::size_t vlen = vid.size();
        bool volMatch = true;
        for (std::size_t i = 0; i < vlen; ++i) {
            if (p.sector[0x28 + i] != static_cast<u8>(vid[i])) { volMatch = false; break; }
        }

        if (volMatch) {
            // Checksum = sum of sector bytes [21..511]; found only if it matches.
            u32 sum = 0; // dword_145E214
            for (int i = 21; i < 512; ++i)
                sum += p.sector[static_cast<std::size_t>(i)];
            r.checksum = sum;
            r.volumeIdMatched = true;
            if (expectedChecksum == sum) { // dword_142DD70[0] == dword_145E214
                r.checksumMatched = true;
                r.found = true;            // dword_145A150 = 1
                r.adapter = adapter;
                r.target = target;
                r.lun = lun;
            }
        } else {
            // Original quirk: a FAILED volume-id compare also accepts the device.
            r.volumeIdMatched = false;
            r.found = true; // dword_145A150 = 1
            r.adapter = adapter;
            r.target = target;
            r.lun = lun;
        }
    }
    return r;
}

// =============================================================================
// gilde.exe 0x141d0f0 — VIBE_CopyProtect_ExtractSintfDriver — scramble blocks
// =============================================================================
// Block 1 (0x141d401): seed byte_145212C => "SIntf16.dll".
void ScrambleBlock1(std::vector<u8>& payload, std::array<u8, 9>& accum,
                    u16& checksum, std::array<u8, 9>& blob, u32& tag) {
    // dword_145F134 = alloc(2); *WORD=0; memset(byte_145AE60,0,9).
    checksum = 0;
    accum.fill(0);
    const std::size_t len = payload.size();
    if (len >= 1) {
        for (std::size_t i = 0; i + 1 < len; ++i) { // i < len-1
            payload[i] ^= payload[i + 1];
            accum[i % 9] += payload[i];
            accum[(i + 3) % 9] ^= payload[i];
            checksum = static_cast<u16>(checksum + payload[i]);
        }
    }
    tag ^= static_cast<u32>(len);                  // dword_142DD64 ^= length
    for (int j = 0; j < 9; ++j) blob[j] += accum[j];
}

// Block 2 (0x141d5db): seed byte_1452120 => "SIntf32.dll".
void ScrambleBlock2(const std::vector<u8>& payload, std::array<u8, 9>& accum,
                    u16& checksum, std::array<u8, 9>& blob, u32& tag) {
    const std::size_t len = payload.size();
    for (std::size_t i = 0; i < len; ++i) {
        accum[i % 9] += payload[i];
        accum[(i + 4) % 9] ^= payload[i];
        checksum = static_cast<u16>(checksum + payload[i]);
    }
    // *(int*)((char*)&dword_142DD60 + 2) ^= length — XOR a dword starting at blob
    // byte offset 2 (touches blob[2..5]).
    u32 mid = static_cast<u32>(blob[2]) | (static_cast<u32>(blob[3]) << 8) |
              (static_cast<u32>(blob[4]) << 16) | (static_cast<u32>(blob[5]) << 24);
    mid ^= static_cast<u32>(len);
    blob[2] = static_cast<u8>(mid & 0xFF);
    blob[3] = static_cast<u8>((mid >> 8) & 0xFF);
    blob[4] = static_cast<u8>((mid >> 16) & 0xFF);
    blob[5] = static_cast<u8>((mid >> 24) & 0xFF);
    (void)tag;
    for (int j = 0; j < 9; ++j) blob[j] ^= accum[j];
}

// Block 3 (0x141d787): seed byte_1452114 => "SIntfNT.dll" (the one that matches).
void ScrambleBlock3(const std::vector<u8>& payload, std::array<u8, 9>& accum,
                    u16& checksum, u32& matchedLength) {
    const std::size_t len = payload.size();
    for (std::size_t i = 0; i < len; ++i) {
        accum[i % 9] += payload[i];
        accum[(i + 5) % 9] ^= payload[i];
        checksum = static_cast<u16>(checksum + payload[i]);
    }
    matchedLength = static_cast<u32>(len); // dword_145B90C = length
}

ExtractResult ExtractSintfDriver(bool isNt,
                                 int entryCount,
                                 SintfRecordProvider provider,
                                 const DrmDriverOs& os) {
    ExtractResult r;

    // Path prefix build. NT: aCWindowsSystem_0 used directly. 9x: dir + "\\".
    std::string dir = kSystemDir;
    if (!isNt) dir += "\\"; // asc_1451B34 "\\" join on the 9x branch

    // Decoded scramble-seed names (fixed in the binary), compared to "SIntfNT.dll".
    const std::string seedName1 = Names().sintf[2]; // SIntf16.dll
    const std::string seedName2 = Names().sintf[3]; // SIntf32.dll
    const std::string seedName3 = Names().sintf[4]; // SIntfNT.dll
    const std::string matchName = kScrambleMatchName;

    for (int idx = 0; idx < entryCount; ++idx) {
        SintfRecord rec;
        if (!(provider && provider(idx, rec)))
            break;

        // Output path = system dir + "sintf32.dll" (rebuilt each record).
        std::string outPath = dir + kExtractOutputName;

        // Seek to rec.offset, read rec.length bytes (rec.payload), write them out.
        std::vector<u8> data = rec.payload;
        if (data.size() > rec.length) data.resize(rec.length);
        if (os.writeFile) os.writeFile(outPath, data);
        r.outputPaths.push_back(outPath);
        ++r.recordsWritten;

        // Three de-obfuscate-and-compare blocks. Each block compares its decoded
        // seed name to "SIntfNT.dll"; only the third (SIntfNT.dll) matches.
        if (seedName1 == matchName) {
            ScrambleBlock1(rec.payload, r.integrityAccum, r.payloadChecksum,
                           r.globalBlob, r.globalBlobTag);
            r.scrambleRan = true; r.scrambleBlock = 1;
        }
        if (seedName2 == matchName) {
            ScrambleBlock2(rec.payload, r.integrityAccum, r.payloadChecksum,
                           r.globalBlob, r.globalBlobTag);
            r.scrambleRan = true; r.scrambleBlock = 2;
        }
        if (seedName3 == matchName) {
            // Block 3 in the binary does NOT pre-allocate/zero the accumulator or
            // checksum (those happen only inside block 1's body). To stay 1:1 with
            // the common path where only block 3 runs, accum/checksum start from
            // their pre-record state; for golden testing we expose them as-is.
            ScrambleBlock3(rec.payload, r.integrityAccum, r.payloadChecksum,
                           r.matchedLength);
            r.scrambleRan = true; r.scrambleBlock = 3;
        }
    }
    return r;
}

// =============================================================================
// DiscIo timer thunks
// =============================================================================

// gilde.exe 0x1416080 — indirect call through dword_1467750.
int QueryFrequencyThunk(i64* out, const DrmDriverOs& os) {
    if (os.perfFrequency) return os.perfFrequency(out);
    if (out) *out = 0;
    return 0;
}

// gilde.exe 0x14160a0 — indirect call through dword_146774C.
int QueryCounterThunk(i64* out, const DrmDriverOs& os) {
    if (os.perfCounter) return os.perfCounter(out);
    if (out) *out = 0;
    return 0;
}

// gilde.exe 0x14160c0 — VIBE_DiscIo_ElapsedSeconds.
// v5 = (float)VIBE_Math_UInt64Multiply(c2-c1, 1000); v4 = (float)freq; return v5/v4.
f64 ElapsedSeconds(i64 counter1, i64 counter2, i64 frequency) {
    // VIBE_Math_UInt64Multiply((c2-c1), 1000) — full 64-bit product (the high-dword
    // branch); the result is then cast to float.
    i64 product = (counter2 - counter1) * static_cast<i64>(1000);
    float v5 = static_cast<float>(product);
    float v4 = static_cast<float>(frequency);
    return static_cast<f64>(v5 / v4);
}

// gilde.exe 0x1417db0 — VIBE_DiscIo_HasTimerFrequency.
bool HasTimerFrequency(const DrmDriverOs& os, i64* outFreq) {
    i64 freq = 0; // qword_1464CD0 (static)
    int ok = QueryFrequencyThunk(&freq, os);
    if (outFreq) *outFreq = freq;
    return ok != 0;
}

// gilde.exe 0x14165a0 — VIBE_DiscIo_ReadSectorRaw.
int ReadSectorRaw(int lba, u32 copyLen, u8* dst, DiscSectorReader reader) {
    u8 buf[4096]; // _BYTE v6[4096]
    std::memset(buf, 0, sizeof(buf));
    int status = reader ? reader(buf, lba) : 0; // VIBE_Disc_ReadSector(buf, lba, 1)
    if (dst && copyLen) {
        u32 n = copyLen > sizeof(buf) ? static_cast<u32>(sizeof(buf)) : copyLen;
        std::memmove(dst, buf, n); // VIBE_Mem_MoveOverlapping(dst, buf, copyLen)
    }
    return status;
}

} // namespace guild::drm
