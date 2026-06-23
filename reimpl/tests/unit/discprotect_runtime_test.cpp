// =============================================================================
// Golden tests for guild::drm DiscProtect runtime (discprotect_runtime.{h,cpp}).
//
// Headless: a recording DrmOs fake drives the pure logic and observes the
// priority-name lookups, alloc/free bookkeeping, config-select branch, the
// obfuscated-name decode tables, the import-resolution walk, and the verify-disc /
// OS-detection control flow. No main() (shared test_main provides it).
// Suite prefix: DiscProtectRuntime.
// =============================================================================
#include "drm/discprotect_runtime.h"
#include "test.h"

#include <cstring>
#include <string>
#include <vector>

using guild::u8;
using guild::u32;
// Local alias so the test body can spell the cluster as `drm::Foo`. This is a
// file-local namespace alias (not the global guild::drm namespace).
namespace drm = guild::drm::runtime;
using guild::drm::runtime::DiscProtectState;
using guild::drm::runtime::DrmOs;
using guild::drm::runtime::ReadPath;

namespace {

// Recording fake: counts allocs/frees, captures log lines, and lets each test
// script the OS return values it needs.
struct FakeOs : DrmOs {
    // alloc/free bookkeeping
    int allocCalls = 0;
    int freeCalls = 0;
    std::vector<u32> allocSizes;
    long liveBytesTokens = 0; // alloc tokens handed out minus freed

    // logging
    std::vector<std::string> stdoutLog;
    std::vector<std::string> stderrLog;

    // scripted priority returns
    int getThreadRet = 0;
    int setThreadRet = 1;
    int getThreadAfter = 0;     // value returned on the 2nd GetThreadPriority
    int getThreadCallNo = 0;
    int getPriClassRet = 0x20;
    int setPriClassRet = 1;
    int getPriClassAfter = 0x20;
    int getPriClassCallNo = 0;

    // scripted version / FT_Thunk
    u32 versionRet = 0x08930005u;
    int regOpenRet = 1;
    int regQueryRet = 1;
    void* ftThunkRet = nullptr;
    int freeLibRet = 7;

    // import walk capture
    std::vector<std::string> resolvedNames;

    void* Alloc(u32 size) override {
        ++allocCalls; allocSizes.push_back(size);
        ++liveBytesTokens;
        return ::operator new(size ? size : 1);
    }
    void Free(void* p) override {
        ++freeCalls;
        if (p) --liveBytesTokens;
        ::operator delete(p);
    }
    void LogStdout(const char* m) override { stdoutLog.emplace_back(m ? m : ""); }
    void LogStderr(const char* m) override { stderrLog.emplace_back(m ? m : ""); }

    int GetCurrentThread() override { return -2; }
    int GetCurrentProcess() override { return -1; }
    int GetThreadPriority(int) override {
        return (getThreadCallNo++ == 0) ? getThreadRet : getThreadAfter;
    }
    int SetThreadPriority(int, int) override { return setThreadRet; }
    int GetPriorityClass(int) override {
        return (getPriClassCallNo++ == 0) ? getPriClassRet : getPriClassAfter;
    }
    int SetPriorityClass(int, int) override { return setPriClassRet; }

    u32 GetVersion() override { return versionRet; }
    void* LoadLibraryByName(const char*) override { return (void*)0x1; }
    void* GetProcByName(void* mod, const char* proc) override {
        resolvedNames.emplace_back(proc ? proc : "");
        if (proc && std::strcmp(proc, "FT_Thunk") == 0) return ftThunkRet;
        return mod ? (void*)0x2 : nullptr;
    }
    int FreeLibrary(void*) override { return freeLibRet; }
    int RegOpenKey(const char*, int* outKey) override { if (outKey) *outKey = 0x10; return regOpenRet; }
    int RegQueryValue(int, const char*) override { return regQueryRet; }
    int RegCloseKey(int) override { return 0; }
};

// A sector map: 16-wide rows, one row per group. Strictly-positive run then a
// terminating 0. Group g has g+1 entries here.
struct SectorMap {
    int rows[4 * 16] = {0};
    SectorMap() {
        for (int g = 0; g < 4; ++g)
            for (int j = 0; j <= g; ++j)
                rows[16 * g + j] = j + 1; // strictly positive
        // remaining columns are 0 => terminator
    }
};

} // namespace

// -----------------------------------------------------------------------------
// Priority-class / thread-priority name lookups (0x1416190 / 0x14162e0).
// -----------------------------------------------------------------------------
TEST(DiscProtectRuntime, PriorityClassNames) {
    CHECK_EQ(std::string(drm::PriorityClassName(32)),  std::string("normal"));
    CHECK_EQ(std::string(drm::PriorityClassName(64)),  std::string("idle"));
    CHECK_EQ(std::string(drm::PriorityClassName(128)), std::string("high"));
    CHECK_EQ(std::string(drm::PriorityClassName(256)), std::string("real-time"));
    CHECK_EQ(std::string(drm::PriorityClassName(0)),   std::string("unknown"));
    CHECK_EQ(std::string(drm::PriorityClassName(99)),  std::string("unknown"));
}

TEST(DiscProtectRuntime, ThreadPriorityNames) {
    CHECK_EQ(std::string(drm::ThreadPriorityName(-15)), std::string("idle"));
    CHECK_EQ(std::string(drm::ThreadPriorityName(-2)),  std::string("lowest"));
    CHECK_EQ(std::string(drm::ThreadPriorityName(-1)),  std::string("below normal"));
    CHECK_EQ(std::string(drm::ThreadPriorityName(0)),   std::string("normal"));
    CHECK_EQ(std::string(drm::ThreadPriorityName(1)),   std::string("above normal"));
    CHECK_EQ(std::string(drm::ThreadPriorityName(2)),   std::string("highest"));
    CHECK_EQ(std::string(drm::ThreadPriorityName(15)),  std::string("time critical"));
    CHECK_EQ(std::string(drm::ThreadPriorityName(7)),   std::string("unknown"));
}

// -----------------------------------------------------------------------------
// Obfuscated-name decode + library names (0x141b230 loop).
// -----------------------------------------------------------------------------
TEST(DiscProtectRuntime, DecodeLibraryNames) {
    drm::DecodedLibraryNames d = drm::DecodeLibraryNames();
    CHECK_EQ(std::string(d.kernel32), std::string("kernel32.dll"));
    CHECK_EQ(std::string(d.user32),   std::string("user32.dll"));
    CHECK_EQ(std::string(d.gdi32),    std::string("gdi32.dll"));
    CHECK_EQ(std::string(d.winmm),    std::string("winmm.dll"));
    CHECK_EQ(std::string(d.advapi32), std::string("advapi32.dll"));
}

TEST(DiscProtectRuntime, DecodeObfuscatedNameDirect) {
    // "EqualSid" encoded run (byte_1451CF0, n=8).
    const u8 enc[] = {0x34,0x04,0x14,0x0d,0x3f,0x3a,0x0d,0x64};
    char out[16] = {0};
    drm::DecodeObfuscatedName(enc, 8, out);
    CHECK_EQ(std::string(out), std::string("EqualSid"));
    CHECK_EQ(out[8], '\0');
}

// -----------------------------------------------------------------------------
// CountSectorEntries (0x14165f0): counts strictly-positive entries in a 16-wide
// row up to the first non-positive terminator.
// -----------------------------------------------------------------------------
TEST(DiscProtectRuntime, CountSectorEntries) {
    SectorMap m;
    CHECK_EQ(drm::CountSectorEntries(m.rows, 0), 1);
    CHECK_EQ(drm::CountSectorEntries(m.rows, 1), 2);
    CHECK_EQ(drm::CountSectorEntries(m.rows, 2), 3);
    CHECK_EQ(drm::CountSectorEntries(m.rows, 3), 4);
}

// -----------------------------------------------------------------------------
// AllocSectorBuffers / FreeSectorBuffers bookkeeping (0x1417bd0 / 0x1417a80).
//   3 base arrays + per group (caps,run,samp) + per entry one sample buf + scratch.
//   For group g (g+1 entries): 3 group arrays + (g+1) sample bufs.
//   total entries = 1+2+3+4 = 10.
//   allocs = 3 (base) + 4*3 (group arrays) + 10 (sample bufs) + 1 (scratch) = 26.
//   FreeSectorBuffers must free exactly the same count (every alloc has a free).
// -----------------------------------------------------------------------------
TEST(DiscProtectRuntime, AllocFreeBookkeeping) {
    FakeOs os;
    DiscProtectState st; st.os = &os;
    SectorMap m;

    bool ok = drm::AllocSectorBuffers(st, m.rows);
    CHECK(ok);                                  // scratch && all 3 bases non-null
    CHECK_EQ(st.entryCount[0], 1);
    CHECK_EQ(st.entryCount[1], 2);
    CHECK_EQ(st.entryCount[2], 3);
    CHECK_EQ(st.entryCount[3], 4);
    CHECK_EQ(st.defaultCap, 90);                // dword_1464C68
    CHECK_EQ(st.cursor, 0);                     // dword_1464CC8
    CHECK_EQ(os.allocCalls, 26);
    // base arrays hold groupCapacity(=4) pointer slots (4*4=16 bytes @x86).
    const u32 baseSz = 4u * (u32)sizeof(int*);
    CHECK_EQ(os.allocSizes[0], baseSz);
    CHECK_EQ(os.allocSizes[1], baseSz);
    CHECK_EQ(os.allocSizes[2], baseSz);
    // caps[g][j] all set to 90; sample buf size = 4*90 = 360 (ints, byte-exact)
    CHECK_EQ(st.capsBase[0][0], 90);
    CHECK_EQ(st.runValsBase[0][0], 0);
    // last alloc is the 360-byte scratch
    CHECK_EQ(os.allocSizes.back(), 360u);

    drm::FreeSectorBuffers(st);
    CHECK_EQ(os.freeCalls, 26);                 // every alloc freed exactly once
    CHECK_EQ(os.liveBytesTokens, 0);            // balanced
    CHECK(st.scratch360 == nullptr);
    CHECK(st.capsBase == nullptr);
    CHECK(st.runValsBase == nullptr);
    CHECK(st.sampBase == nullptr);
}

// FreeTimingTables (0x1417880) frees both tables and nulls them.
TEST(DiscProtectRuntime, FreeTimingTables) {
    FakeOs os;
    DiscProtectState st; st.os = &os;
    st.timingTableA = os.Alloc(8);
    st.timingTableB = os.Alloc(8);
    int before = os.freeCalls;
    drm::FreeTimingTables(st);
    CHECK_EQ(os.freeCalls - before, 2);
    CHECK(st.timingTableA == nullptr);
    CHECK(st.timingTableB == nullptr);
}

// SetVerifiedFlag (0x1417d90): both flags -> 1, returns 1.
TEST(DiscProtectRuntime, SetVerifiedFlag) {
    DiscProtectState st;
    int r = drm::SetVerifiedFlag(st);
    CHECK_EQ(r, 1);
    CHECK_EQ(st.verifiedFlag, 1);
    CHECK_EQ(st.verifiedFlag2, 1);
}

// -----------------------------------------------------------------------------
// SetThreadPriority control flow (0x1416390).
// -----------------------------------------------------------------------------
TEST(DiscProtectRuntime, SetThreadPriorityGetFails) {
    FakeOs os; DiscProtectState st; st.os = &os;
    os.getThreadRet = 0x7FFFFFFF;               // get fails immediately
    int r = drm::SetThreadPriority(st, 2);
    CHECK_EQ(r, 0);
    CHECK_EQ((int)os.stderrLog.size(), 1);
    CHECK_EQ(os.stderrLog[0], std::string("failed to get thread priority (1), err=%d\n"));
}

TEST(DiscProtectRuntime, SetThreadPrioritySetFails) {
    FakeOs os; DiscProtectState st; st.os = &os;
    os.getThreadRet = 1;                        // current = above normal
    os.setThreadRet = 0;                        // set fails
    int r = drm::SetThreadPriority(st, 2);
    CHECK_EQ(r, 0);
    CHECK_EQ(os.stdoutLog[0], std::string("running at thread priority %s\n"));
    CHECK_EQ(os.stderrLog.back(), std::string("failed to set thread priority, err=%d\n"));
}

TEST(DiscProtectRuntime, SetThreadPrioritySuccess) {
    FakeOs os; DiscProtectState st; st.os = &os;
    os.getThreadRet = -1;                       // prev = below normal
    os.setThreadRet = 1;
    os.getThreadAfter = 2;                      // cur = highest
    int r = drm::SetThreadPriority(st, 2);
    CHECK_EQ(r, -1);                            // returns previous priority
    CHECK_EQ((int)os.stdoutLog.size(), 2);
    CHECK_EQ(os.stdoutLog[1], std::string("now running at thread priority %s\n"));
}

// -----------------------------------------------------------------------------
// SetProcessPriority control flow (0x1416460).
// -----------------------------------------------------------------------------
TEST(DiscProtectRuntime, SetProcessPriorityGetFails) {
    FakeOs os; DiscProtectState st; st.os = &os;
    os.getPriClassRet = 0;                      // get fails
    int r = drm::SetProcessPriority(st, 128);
    CHECK_EQ(r, 0);
    CHECK_EQ(os.stderrLog[0], std::string("failed to get process priority class (1), err=%d\n"));
}

TEST(DiscProtectRuntime, SetProcessPrioritySuccess) {
    FakeOs os; DiscProtectState st; st.os = &os;
    os.getPriClassRet = 32;                     // prev = normal
    os.setPriClassRet = 1;
    os.getPriClassAfter = 128;                  // cur = high
    int r = drm::SetProcessPriority(st, 128);
    CHECK_EQ(r, 32);                            // returns previous class
    CHECK_EQ(os.stdoutLog[0], std::string("running priority class %s\n"));
    CHECK_EQ(os.stdoutLog[1], std::string("now running at priority class %s\n"));
}

// RaisePriorityHigh (0x1416530) requests HIGH (128) and stores the prev.
TEST(DiscProtectRuntime, RaisePriorityHigh) {
    FakeOs os; DiscProtectState st; st.os = &os;
    os.getPriClassRet = 32;                     // prev = normal
    os.getPriClassAfter = 128;
    int r = drm::RaisePriorityHigh(st);
    CHECK_EQ(r, 32);
    CHECK_EQ(st.savedProcessPriority, 32);
}

// RestorePriority (0x1416550): no saved class -> 128; saved thread 0x7FFFFFFF -> 0.
TEST(DiscProtectRuntime, RestorePriorityDefaults) {
    FakeOs os; DiscProtectState st; st.os = &os;
    os.getPriClassRet = 32; os.getPriClassAfter = 32;
    os.getThreadRet = 0; os.getThreadAfter = 0;
    st.savedProcessPriority = 0;                // -> SetProcessPriority(128)
    st.savedThreadPriority = 0x7FFFFFFF;        // -> SetThreadPriority(0)
    int r = drm::RestorePriority(st);
    CHECK_EQ(r, 0);                             // GetThreadPriority returned 0 as prev
    // process priority then thread priority were both touched
    CHECK(os.stdoutLog.size() >= 2);
}

// -----------------------------------------------------------------------------
// ReadConfigSelect branch (0x141b1c0).
// -----------------------------------------------------------------------------
TEST(DiscProtectRuntime, ReadConfigSelectBranch) {
    DiscProtectState st;
    st.useSpti = 0;
    CHECK(drm::ReadConfigSelectPath(st) == ReadPath::Aspi);
    st.useSpti = 1;
    CHECK(drm::ReadConfigSelectPath(st) == ReadPath::Spti);
}

// -----------------------------------------------------------------------------
// LoadObfuscatedLibraries (0x141b230): decode + LoadLibrary each, return advapi32.
// -----------------------------------------------------------------------------
TEST(DiscProtectRuntime, LoadObfuscatedLibraries) {
    FakeOs os; DiscProtectState st; st.os = &os;
    void* ret = drm::LoadObfuscatedLibraries(st);
    CHECK(st.hKernel32 != nullptr);
    CHECK(st.hUser32 != nullptr);
    CHECK(st.hGdi32 != nullptr);
    CHECK(st.hWinmm != nullptr);
    CHECK(st.hAdvapi32 != nullptr);
    CHECK(ret == st.hAdvapi32);                 // returns advapi32 handle
}

// -----------------------------------------------------------------------------
// ResolveImports (0x141b4e0): full ordered walk, names + module + ordering.
// -----------------------------------------------------------------------------
TEST(DiscProtectRuntime, ResolveImportsWalk) {
    FakeOs os; DiscProtectState st; st.os = &os;
    drm::LoadObfuscatedLibraries(st);
    os.resolvedNames.clear();
    void* last = drm::ResolveImports(st);
    CHECK(last != nullptr);

    const int n = drm::ImportEntryCount();
    CHECK_EQ((int)os.resolvedNames.size(), n);

    // First and last names of the walk.
    CHECK_EQ(os.resolvedNames.front(), std::string("CloseHandle"));
    CHECK_EQ(os.resolvedNames.back(),  std::string("OpenThreadToken"));

    // A decoded obfuscated entry appears at its position (DeviceIoControl is #7).
    CHECK_EQ(os.resolvedNames[6], std::string("DeviceIoControl"));

    // Verify the advapi32 ordering fix: RegDeleteValueA must precede RegCreateKeyExA.
    int idxDelete = -1, idxCreate = -1;
    for (int i = 0; i < (int)os.resolvedNames.size(); ++i) {
        if (os.resolvedNames[i] == "RegDeleteValueA") idxDelete = i;
        if (os.resolvedNames[i] == "RegCreateKeyExA") idxCreate = i;
    }
    CHECK(idxDelete >= 0);
    CHECK(idxCreate >= 0);
    CHECK(idxDelete < idxCreate);
}

// ImportEntryName decodes & reports the module for each entry.
TEST(DiscProtectRuntime, ImportEntryNameTable) {
    char buf[64];
    int m0 = drm::ImportEntryName(0, buf);
    CHECK_EQ(m0, 0);                            // K32
    CHECK_EQ(std::string(buf), std::string("CloseHandle"));

    int mlast = drm::ImportEntryName(drm::ImportEntryCount() - 1, buf);
    CHECK_EQ(mlast, 4);                         // ADV
    CHECK_EQ(std::string(buf), std::string("OpenThreadToken"));

    // out of range
    CHECK_EQ(drm::ImportEntryName(-1, buf), -1);
    CHECK_EQ(drm::ImportEntryName(drm::ImportEntryCount(), buf), -1);
}

// -----------------------------------------------------------------------------
// DetectOsAndThunk (0x141c270) — NT path and Win9x path.
// -----------------------------------------------------------------------------
TEST(DiscProtectRuntime, DetectOsNtPath) {
    FakeOs os; DiscProtectState st; st.os = &os;
    os.versionRet = 0x08930005u;                // major=5, minor=0, build=2195, NT
    int ret = drm::DetectOsAndThunk(st);
    CHECK_EQ(ret, 5);                           // returns (u8)version on NT path
    CHECK_EQ(st.osMajor, 5);
    CHECK_EQ(st.osMinor, 0);
    CHECK_EQ(st.osBuild, 0x0893);               // HIWORD = 2195
    CHECK_EQ(st.useSpti, 1);                    // NT forces SPTI
}

TEST(DiscProtectRuntime, DetectOs9xPathNoThunkForcesSpti) {
    FakeOs os; DiscProtectState st; st.os = &os;
    os.versionRet = 0xC3578004u;                // high bit set => Win9x; major=4
    os.regOpenRet = 0;                          // RegOpenKeyExA succeeds
    os.regQueryRet = 0;                         // CurrentVersion present
    os.ftThunkRet = nullptr;                    // FT_Thunk missing
    os.freeLibRet = 9;
    int ret = drm::DetectOsAndThunk(st);
    CHECK_EQ(ret, 9);                           // 9x path returns FreeLibrary's value
    CHECK_EQ(st.osMajor, 4);
    CHECK_EQ(st.regHasVersion, 1);
    CHECK_EQ(st.useSpti, 1);                    // regHasVersion && !FT_Thunk -> SPTI
}

TEST(DiscProtectRuntime, DetectOs9xPathWithThunkKeepsAspi) {
    FakeOs os; DiscProtectState st; st.os = &os;
    os.versionRet = 0xC3578004u;                // Win9x
    os.regOpenRet = 0; os.regQueryRet = 0;
    os.ftThunkRet = (void*)0x99;                // FT_Thunk present
    os.freeLibRet = 3;
    int ret = drm::DetectOsAndThunk(st);
    CHECK_EQ(ret, 3);
    CHECK_EQ(st.useSpti, 0);                    // FT_Thunk present -> stays ASPI
    CHECK(st.ftThunk != nullptr);
}
