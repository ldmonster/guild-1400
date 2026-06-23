// Golden-vector unit tests for guild::drm CopyProtect driver cluster.
// Suite prefix: CopyProtectDriver. Headless; no main() (shared test_main.cpp).
#include "test.h"
#include "drm/copyprotect_driver.h"

#include <cstring>
#include <string>
#include <vector>

using namespace guild::drm;

// -----------------------------------------------------------------------------
// Recording / injectable hooks (file-scope so they can be C function pointers).
// -----------------------------------------------------------------------------
namespace {

// writeFile recorder.
std::vector<std::pair<std::string, std::vector<u8>>> g_writes;
bool RecWrite(const std::string& path, const std::vector<u8>& data) {
    g_writes.emplace_back(path, data);
    return true;
}

// LoadLibrary that succeeds and remembers the requested name.
std::string g_loadName;
std::uintptr_t LoadOk(const std::string& name) { g_loadName = name; return 0x1000; }
std::uintptr_t LoadFail(const std::string&) { return 0; }

// GetProcAddress-by-ordinal that resolves everything to a synthetic non-null addr.
std::uintptr_t OrdAll(std::uintptr_t, u16 ord) { return 0x2000 + ord; }
// Same but ordinal 7 missing -> triggers the required gate.
std::uintptr_t OrdMiss7(std::uintptr_t, u16 ord) { return ord == 7 ? 0 : (0x2000 + ord); }

// GetProcAddress-by-name resolver.
std::uintptr_t NameAll(std::uintptr_t, const std::string&) { return 0x3000; }
std::uintptr_t NameNoSend(std::uintptr_t, const std::string& n) {
    return (n == kAspiSendCommand) ? 0 : 0x3000;
}

// GetASPI32SupportInfo returns: BYTE1 status==1, low byte = adapter count 3.
u32 SupportOk() { return 0x0000'0103u; } // status byte (BYTE1) = 1, count = 3
u32 SupportBadStatus() { return 0x0000'0203u; } // status byte = 2

// Disc sector reader: fills with a ramp, returns 1.
int DiscRamp(u8* buf, int /*lba*/) {
    for (int i = 0; i < 4096; ++i) buf[i] = static_cast<u8>(i & 0xFF);
    return 1;
}

// QPF/QPC fakes.
i64 g_freq = 0;
int PerfFreq(i64* out) { if (out) *out = g_freq; return g_freq != 0 ? 1 : 0; }
int PerfFreqFail(i64* out) { if (out) *out = 0; return 0; }

// Scanner producing a single matching device on adapter 2.
ScsiProbe ScanMatch(int adapter, int target, int lun) {
    ScsiProbe p;
    if (adapter == 2 && target == 0 && lun == 0) {
        p.haInquiryOk = true;
        p.present = true;
        p.readOk = true;
        const char* vid = kGenuineVolumeId;
        for (std::size_t i = 0; vid[i]; ++i) p.sector[0x28 + i] = static_cast<u8>(vid[i]);
        // leave the rest zero so checksum over [21..511] is well-defined
    }
    return p;
}

// Scanner with a device whose volume id does NOT match (the "else accepts" quirk).
ScsiProbe ScanVolMismatch(int adapter, int /*t*/, int /*l*/) {
    ScsiProbe p;
    if (adapter == 0) {
        p.haInquiryOk = true; p.present = true; p.readOk = true;
        // sector[0x28..] left zero -> mismatch vs "UKD_..."
    }
    return p;
}

} // namespace

// -----------------------------------------------------------------------------
// DeobfuscateName + recovered name tables.
// -----------------------------------------------------------------------------
TEST(CopyProtectDriver, NameTablesDecodeExactly) {
    CHECK(std::string(kSintfDriverNames[0]) == "sintfnt.dll");
    CHECK(std::string(kSintfDriverNames[1]) == "sintf32.dll");
    CHECK(std::string(kSintfDriverNames[2]) == "SIntf16.dll");
    CHECK(std::string(kSintfDriverNames[3]) == "SIntf32.dll");
    CHECK(std::string(kSintfDriverNames[4]) == "SIntfNT.dll");

    CHECK(std::string(kAspiDllName) == "wnaspi32.dll");
    CHECK(std::string(kAspiGetSupportInfo) == "GetASPI32SupportInfo");
    CHECK(std::string(kAspiSendCommand) == "SendASPI32Command");
    CHECK(std::string(kAspiTranslateAddress) == "TranslateASPI32Address");

    CHECK(std::string(kGenuineVolumeId) == "UKD_548520-001.001");
    CHECK(std::string(kSystemDir) == "C:\\WINDOWS\\SYSTEM\\");
    CHECK(std::string(kScrambleMatchName) == "SIntfNT.dll");
}

TEST(CopyProtectDriver, DeobfuscateRoundtrip) {
    // The decoder applied to an already-decoded ascii string is its own inverse
    // family; just assert it tolerates an empty input and a simple known seed.
    const u8 seed[] = {0x1a,0x07,0x1a,0x12,0x55,0x01,0x1c,0x4a,0x08,0x00,0x6c};
    CHECK(DeobfuscateName(seed, sizeof(seed)) == "sintf32.dll");
    CHECK(DeobfuscateName(nullptr, 0).empty());
}

// -----------------------------------------------------------------------------
// LoadDriverExports ordinal lists + gate.
// -----------------------------------------------------------------------------
TEST(CopyProtectDriver, NtOrdinalList) {
    const std::vector<u16> expect = {2,3,4,5,6,7,8,9,10,11,12,13,14,15,17,18};
    CHECK(kNtDriverOrdinals == expect);
    CHECK_EQ(kNtDriverOrdinals.size(), static_cast<std::size_t>(16));
}

TEST(CopyProtectDriver, WinOrdinalList) {
    const std::vector<u16> expect = {2,3,4,5,6,7,8,9,10,11,12,13,14,15};
    CHECK(kWinOrdinals == expect);
    CHECK_EQ(kWinOrdinals.size(), static_cast<std::size_t>(14));
}

TEST(CopyProtectDriver, LoadDriverExportsNtPathAndResolve) {
    DrmDriverOs os;
    os.loadLibrary = &LoadOk;
    os.getProcByOrdinal = &OrdAll;
    DriverExports r = LoadDriverExports(/*isNt=*/true, os);
    CHECK(r.loaded);
    CHECK(r.allRequiredPresent);
    CHECK_EQ(r.exitCode, 0);
    CHECK(r.driverPath == "C:\\WINDOWS\\SYSTEM\\sintfnt.dll");
    CHECK_EQ(r.exports.size(), static_cast<std::size_t>(16));
    CHECK_EQ(r.exports.front().first, static_cast<u16>(2));
    CHECK_EQ(r.exports.back().first, static_cast<u16>(18));
}

TEST(CopyProtectDriver, LoadDriverExports9xPath) {
    DrmDriverOs os;
    os.loadLibrary = &LoadOk;
    os.getProcByOrdinal = &OrdAll;
    DriverExports r = LoadDriverExports(/*isNt=*/false, os);
    CHECK(r.driverPath == "C:\\WINDOWS\\SYSTEM\\sintf32.dll");
    CHECK_EQ(r.exports.size(), static_cast<std::size_t>(14));
    CHECK(r.allRequiredPresent);
}

TEST(CopyProtectDriver, LoadDriverExportsMissingOrdinalFailsGate) {
    DrmDriverOs os;
    os.loadLibrary = &LoadOk;
    os.getProcByOrdinal = &OrdMiss7;
    DriverExports r = LoadDriverExports(/*isNt=*/true, os);
    CHECK(r.loaded);
    CHECK(!r.allRequiredPresent);
    CHECK_EQ(r.exitCode, 12); // NT exit code
}

TEST(CopyProtectDriver, LoadDriverExportsLoadLibraryFails) {
    DrmDriverOs os;
    os.loadLibrary = &LoadFail;
    DriverExports r = LoadDriverExports(/*isNt=*/false, os);
    CHECK(!r.loaded);
    CHECK_EQ(r.exitCode, 10); // 9x exit code
}

// -----------------------------------------------------------------------------
// ResolveAspiFunctions.
// -----------------------------------------------------------------------------
TEST(CopyProtectDriver, ResolveAspiSuccess) {
    DrmDriverOs os;
    os.loadLibrary = &LoadOk;
    os.getProcByName = &NameAll;
    AspiBinding b = ResolveAspiFunctions(os, &SupportOk);
    CHECK(b.available);
    CHECK_EQ(b.statusByte, static_cast<u8>(1));
    CHECK_EQ(b.hostAdapterCount, static_cast<u8>(3));
    CHECK_EQ(b.supportInfoRaw, 0x0103u);
}

TEST(CopyProtectDriver, ResolveAspiBadStatusByteClearsAvailable) {
    DrmDriverOs os;
    os.loadLibrary = &LoadOk;
    os.getProcByName = &NameAll;
    AspiBinding b = ResolveAspiFunctions(os, &SupportBadStatus);
    CHECK(!b.available); // BYTE1 != 1
    CHECK_EQ(b.statusByte, static_cast<u8>(2));
}

TEST(CopyProtectDriver, ResolveAspiMissingSendClearsAvailable) {
    DrmDriverOs os;
    os.loadLibrary = &LoadOk;
    os.getProcByName = &NameNoSend;
    AspiBinding b = ResolveAspiFunctions(os, &SupportOk);
    CHECK(!b.available);
    CHECK_EQ(b.sendCommand, static_cast<std::uintptr_t>(0));
}

TEST(CopyProtectDriver, ResolveAspiDllMissing) {
    DrmDriverOs os;
    os.loadLibrary = &LoadFail;
    AspiBinding b = ResolveAspiFunctions(os, &SupportOk);
    CHECK(!b.available);
    CHECK_EQ(b.module, static_cast<std::uintptr_t>(0));
}

// -----------------------------------------------------------------------------
// ScanScsiDevices.
// -----------------------------------------------------------------------------
TEST(CopyProtectDriver, ScanFindsMatchingDeviceByChecksum) {
    // Build expected checksum = sum of sector[21..511] for the ScanMatch device.
    u32 sum = 0;
    {
        ScsiProbe p = ScanMatch(2, 0, 0);
        for (int i = 21; i < 512; ++i) sum += p.sector[static_cast<std::size_t>(i)];
    }
    ScsiScanResult r = ScanScsiDevices(/*count=*/4, /*aspi=*/true, sum, &ScanMatch);
    CHECK(r.found);
    CHECK(r.volumeIdMatched);
    CHECK(r.checksumMatched);
    CHECK_EQ(r.adapter, 2);
    CHECK_EQ(r.target, 0);
    CHECK_EQ(r.lun, 0);
    CHECK_EQ(r.checksum, sum);
}

TEST(CopyProtectDriver, ScanChecksumMismatchDoesNotAccept) {
    // Volume id matches but expected checksum is wrong -> not found.
    ScsiScanResult r = ScanScsiDevices(4, true, 0xDEADBEEFu, &ScanMatch);
    CHECK(!r.found);
    CHECK(r.volumeIdMatched); // it did match the volume id on adapter 2
}

TEST(CopyProtectDriver, ScanVolumeIdMismatchAcceptsQuirk) {
    // The original accepts a device whose volume id does NOT match (else branch).
    ScsiScanResult r = ScanScsiDevices(4, true, 0, &ScanVolMismatch);
    CHECK(r.found);
    CHECK(!r.volumeIdMatched);
    CHECK_EQ(r.adapter, 0);
}

TEST(CopyProtectDriver, ScanNoAspiNoIteration) {
    ScsiScanResult r = ScanScsiDevices(4, /*aspi=*/false, 0, &ScanMatch);
    CHECK(!r.found);
}

TEST(CopyProtectDriver, ScanDefaultScannerFindsNothing) {
    ScsiScanResult r = ScanScsiDevices(4, true, 0, nullptr);
    CHECK(!r.found);
}

// -----------------------------------------------------------------------------
// ExtractSintfDriver path build + scramble blocks.
// -----------------------------------------------------------------------------
namespace {
SintfRecord g_rec;
bool ProvideOne(int index, SintfRecord& out) {
    if (index != 0) return false;
    out = g_rec;
    return true;
}
} // namespace

TEST(CopyProtectDriver, ExtractNtPath) {
    g_rec = SintfRecord{};
    g_rec.offset = 0x100;
    g_rec.length = 4;
    g_rec.payload = {0xDE, 0xAD, 0xBE, 0xEF};
    g_writes.clear();
    DrmDriverOs os; os.writeFile = &RecWrite;
    ExtractResult r = ExtractSintfDriver(/*isNt=*/true, 1, &ProvideOne, os);
    CHECK_EQ(r.recordsWritten, 1);
    CHECK_EQ(r.outputPaths.size(), static_cast<std::size_t>(1));
    CHECK(r.outputPaths[0] == "C:\\WINDOWS\\SYSTEM\\sintf32.dll");
    CHECK_EQ(g_writes.size(), static_cast<std::size_t>(1));
    CHECK(g_writes[0].second.size() == 4);
}

TEST(CopyProtectDriver, Extract9xPathJoinsBackslash) {
    g_rec = SintfRecord{};
    g_rec.length = 1; g_rec.payload = {0x00};
    g_writes.clear();
    DrmDriverOs os; os.writeFile = &RecWrite;
    ExtractResult r = ExtractSintfDriver(/*isNt=*/false, 1, &ProvideOne, os);
    // 9x branch joins dir + "\\" + basename -> double backslash before sintf32.dll
    CHECK(r.outputPaths[0] == "C:\\WINDOWS\\SYSTEM\\\\sintf32.dll");
}

TEST(CopyProtectDriver, ExtractRunsBlock3OnSintfNTSeed) {
    // The only seed matching "SIntfNT.dll" is block 3 -> scrambleBlock == 3.
    g_rec = SintfRecord{};
    g_rec.length = 12;
    g_rec.payload = {0x10,0x20,0x30,0x40,0x55,0x66,0x77,0x88,0x99,0xAA,0xBB,0xCC};
    DrmDriverOs os; os.writeFile = &RecWrite;
    ExtractResult r = ExtractSintfDriver(true, 1, &ProvideOne, os);
    CHECK(r.scrambleRan);
    CHECK_EQ(r.scrambleBlock, 3);
    CHECK_EQ(r.matchedLength, 12u);
}

// Direct golden vectors for the three scramble primitives.
TEST(CopyProtectDriver, ScrambleBlock1Golden) {
    std::vector<u8> payload = {0x10,0x20,0x30,0x40,0x55,0x66,0x77,0x88,0x99,0xAA,0xBB,0xCC};
    std::array<u8,9> accum{}; u16 cksum = 0; std::array<u8,9> blob{}; u32 tag = 0;
    ScrambleBlock1(payload, accum, cksum, blob, tag);
    const std::array<u8,9> expAccum = {224,120,67,84,52,129,20,68,68};
    const std::array<u8,12> expPayload = {48,16,112,21,51,17,255,17,51,17,119,204};
    CHECK(accum == expAccum);
    CHECK(blob == expAccum); // blob starts at 0, += accum
    CHECK_EQ(cksum, static_cast<u16>(724));
    CHECK_EQ(tag, 12u);
    for (int i = 0; i < 12; ++i) CHECK_EQ(payload[static_cast<std::size_t>(i)], expPayload[static_cast<std::size_t>(i)]);
}

TEST(CopyProtectDriver, ScrambleBlock3Golden) {
    std::vector<u8> payload = {0x10,0x20,0x30,0x40,0x55,0x66,0x77,0x88,0x99,0xAA,0xBB,0xCC};
    std::array<u8,9> accum{}; u16 cksum = 0; u32 mlen = 0;
    ScrambleBlock3(payload, accum, cksum, mlen);
    const std::array<u8,9> expAccum = {239,1,19,200,204,220,44,116,217};
    CHECK(accum == expAccum);
    CHECK_EQ(cksum, static_cast<u16>(1316));
    CHECK_EQ(mlen, 12u);
}

// -----------------------------------------------------------------------------
// DiscIo timer math.
// -----------------------------------------------------------------------------
TEST(CopyProtectDriver, ElapsedSecondsMilliseconds) {
    // (1000-0)*1000 / 1000 = 1000.0
    CHECK_EQ(ElapsedSeconds(0, 1000, 1000), 1000.0);
    // (5500-5000)*1000 / 1000000 = 0.5
    CHECK_EQ(ElapsedSeconds(5000, 5500, 1000000), 0.5);
}

TEST(CopyProtectDriver, ElapsedSecondsFloatRounding) {
    // Computed in single precision: assert it equals the float-rounded reference.
    i64 c1 = 7, c2 = 1234567;
    i64 freq = 3579545;
    float v5 = static_cast<float>((c2 - c1) * 1000);
    float v4 = static_cast<float>(freq);
    CHECK_EQ(ElapsedSeconds(c1, c2, freq), static_cast<double>(v5 / v4));
}

TEST(CopyProtectDriver, QueryFrequencyThunkAndHasTimer) {
    DrmDriverOs os;
    g_freq = 3579545;
    os.perfFrequency = &PerfFreq;
    i64 f = 0;
    CHECK(QueryFrequencyThunk(&f, os) != 0);
    CHECK_EQ(f, static_cast<i64>(3579545));
    i64 of = 0;
    CHECK(HasTimerFrequency(os, &of));
    CHECK_EQ(of, static_cast<i64>(3579545));

    DrmDriverOs bad; bad.perfFrequency = &PerfFreqFail;
    CHECK(!HasTimerFrequency(bad, nullptr));
    CHECK_EQ(QueryFrequencyThunk(nullptr, DefaultDrmOs()), 0); // inert default
}

TEST(CopyProtectDriver, ReadSectorRawCopiesPrefix) {
    u8 dst[64];
    std::memset(dst, 0xFF, sizeof(dst));
    int st = ReadSectorRaw(/*lba=*/16, /*copyLen=*/64, dst, &DiscRamp);
    CHECK_EQ(st, 1);
    for (int i = 0; i < 64; ++i) CHECK_EQ(dst[i], static_cast<u8>(i & 0xFF));

    // Default reader: returns 0, copies nothing meaningful.
    u8 dst2[8]; std::memset(dst2, 0x7E, sizeof(dst2));
    CHECK_EQ(ReadSectorRaw(16, 8, dst2, nullptr), 0);
}
