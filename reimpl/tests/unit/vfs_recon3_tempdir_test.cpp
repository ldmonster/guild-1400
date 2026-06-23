// Golden tests for VIBE_Vfs_GetTempDir @0x5fc8d0 (guild::io::VfsGetTempDir).
// Headless: env + GetFullPath leaves are driven through the inert hooks; the
// working-dir fallback is driven through the already-present FileOps3 hook.
#include "test.h"

#include "io/vfs_recon3_tempdir.h"
#include "io/file_ops3.h"   // SetFileOps3Hooks / FileOps3Hooks (working-dir hook)

#include <cstring>
#include <cstdlib>
#include <string>

using namespace guild::io;

namespace {

// --- env-var hook plumbing --------------------------------------------------
std::string g_envTmp, g_envTemp, g_envTmpdir, g_envTempdir;
bool g_hasTmp = false, g_hasTemp = false, g_hasTmpdir = false, g_hasTempdir = false;

const char* TestFindEnvVar(const char* name) {
    if (std::strcmp(name, "TMP") == 0)     return g_hasTmp     ? g_envTmp.c_str()     : nullptr;
    if (std::strcmp(name, "TEMP") == 0)    return g_hasTemp    ? g_envTemp.c_str()    : nullptr;
    if (std::strcmp(name, "TMPDIR") == 0)  return g_hasTmpdir  ? g_envTmpdir.c_str()  : nullptr;
    if (std::strcmp(name, "TEMPDIR") == 0) return g_hasTempdir ? g_envTempdir.c_str() : nullptr;
    return nullptr;
}

// Stand-in canonicalizer: copy src verbatim into dst (capacity cap). This is
// the inert "GetFullPath succeeds, leaves the value unchanged" behavior, which
// lets us assert the exact bytes the table scan selected.
char* TestGetFullPathPassthrough(char* dst, const char* src, guild::u32 cap) {
    std::size_t n = std::strlen(src);
    if (n > cap) n = cap;
    std::memcpy(dst, src, n);
    dst[n] = '\0';
    return dst;
}

// "GetFullPath always fails" — forces the working-dir fallback even when an env
// var is present and within the length gate.
char* TestGetFullPathFail(char* /*dst*/, const char* /*src*/, guild::u32 /*cap*/) {
    return nullptr;
}

// Working-dir hook (GetCurrentDirectoryA shim): writes a fixed path.
std::string g_workdir;
guild::u32 TestGetCurrentDir(guild::u32 size, char* buf) {
    guild::u32 len = static_cast<guild::u32>(g_workdir.size());
    if (!buf) return len;
    if (len + 1 > size && size != 0) { /* mirror truncation guard */ }
    std::memcpy(buf, g_workdir.c_str(), len + 1);
    return len;
}

void ClearEnv() {
    g_hasTmp = g_hasTemp = g_hasTmpdir = g_hasTempdir = false;
}

void InstallEnvHooks(char* (*fullPath)(char*, const char*, guild::u32)) {
    VfsTempDirHooks h{};
    h.findEnvVar = &TestFindEnvVar;
    h.getFullPath = fullPath;
    VfsSetTempDirHooks(h);
}

// VfsGetWorkingDir(0,0) allocates its return buffer through the FileOps3 allocMem
// hook (the original mallocs); the default inert allocMem returns null, so the test
// must supply a real allocator for the working-dir fallback path to be exercised.
void* TestAllocMem(guild::u32 size) { return std::malloc(size); }
void  TestFreeMem(void* p)          { std::free(p); }

void InstallWorkdir(const char* wd) {
    g_workdir = wd;
    FileOps3Hooks fh{};
    fh.getCurrentDir = &TestGetCurrentDir;
    fh.allocMem      = &TestAllocMem;
    fh.freeMem       = &TestFreeMem;
    SetFileOps3Hooks(&fh);
}

void ResetAll() {
    VfsResetTempDirCache();
    ClearEnv();
}

} // namespace

// TMP wins (first in the table) and already ends in '\\' -> returned verbatim.
TEST(VfsRecon3, TmpAlreadyBackslashTerminated) {
    ResetAll();
    g_hasTmp = true; g_envTmp = "C:\\Windows\\Temp\\";
    InstallEnvHooks(&TestGetFullPathPassthrough);
    char* r = VfsGetTempDir();
    CHECK_EQ(std::string(r), std::string("C:\\Windows\\Temp\\"));
}

// A forward-slash terminator is also accepted as-is (no extra separator).
TEST(VfsRecon3, TmpForwardSlashTerminated) {
    ResetAll();
    g_hasTmp = true; g_envTmp = "/var/tmp/";
    InstallEnvHooks(&TestGetFullPathPassthrough);
    char* r = VfsGetTempDir();
    CHECK_EQ(std::string(r), std::string("/var/tmp/"));
}

// No trailing separator -> a single '\\' is appended.
TEST(VfsRecon3, AppendsBackslashWhenMissing) {
    ResetAll();
    g_hasTmp = true; g_envTmp = "C:\\Windows\\Temp";
    InstallEnvHooks(&TestGetFullPathPassthrough);
    char* r = VfsGetTempDir();
    CHECK_EQ(std::string(r), std::string("C:\\Windows\\Temp\\"));
}

// Table scan order: TMP unset, TEMP set -> TEMP chosen (TMPDIR/TEMPDIR ignored).
TEST(VfsRecon3, FallsThroughToTemp) {
    ResetAll();
    g_hasTemp = true; g_envTemp = "D:\\t";
    g_hasTmpdir = true; g_envTmpdir = "SHOULD_NOT_PICK";
    InstallEnvHooks(&TestGetFullPathPassthrough);
    char* r = VfsGetTempDir();
    CHECK_EQ(std::string(r), std::string("D:\\t\\"));
}

// First non-empty hit wins even if a later var is also set: TMP over TEMPDIR.
TEST(VfsRecon3, FirstHitWins) {
    ResetAll();
    g_hasTmp = true; g_envTmp = "A:\\one\\";
    g_hasTempdir = true; g_envTempdir = "B:\\two\\";
    InstallEnvHooks(&TestGetFullPathPassthrough);
    char* r = VfsGetTempDir();
    CHECK_EQ(std::string(r), std::string("A:\\one\\"));
}

// Length gate: value with strlen == 0x103 is accepted (boundary inclusive).
TEST(VfsRecon3, LengthGateAccepts0x103) {
    ResetAll();
    std::string p(0x103, 'x');   // 259 chars, no separator
    g_hasTmp = true; g_envTmp = p;
    InstallEnvHooks(&TestGetFullPathPassthrough);
    char* r = VfsGetTempDir();
    // passthrough copies all 0x103 chars (cap is exactly 0x103), then a '\\'
    // is appended since the last char is 'x'.
    CHECK_EQ(std::strlen(r), static_cast<std::size_t>(0x103 + 1));
    CHECK(r[0x103] == '\\');
}

// Length gate: value with strlen == 0x104 is rejected (> 0x103) -> falls back.
TEST(VfsRecon3, LengthGateRejects0x104) {
    ResetAll();
    std::string tooLong(0x104, 'y');
    g_hasTmp = true; g_envTmp = tooLong;
    InstallEnvHooks(&TestGetFullPathPassthrough);
    InstallWorkdir("E:\\work");
    char* r = VfsGetTempDir();
    // env rejected -> working-dir fallback "E:\\work" + appended '\\'.
    CHECK_EQ(std::string(r), std::string("E:\\work\\"));
}

// Working-dir fallback when no env var is set at all.
TEST(VfsRecon3, WorkingDirFallback) {
    ResetAll();
    InstallEnvHooks(&TestGetFullPathPassthrough);
    InstallWorkdir("/home/user/game");
    char* r = VfsGetTempDir();
    // decompile 0x5fc986 appends byte 92 ('\\') unconditionally when the last char
    // is neither '\\' nor '/', regardless of the path's own separator style.
    CHECK_EQ(std::string(r), std::string("/home/user/game\\"));
}

// Working-dir fallback when GetFullPath fails for a present env var.
TEST(VfsRecon3, FullPathFailureFallsBack) {
    ResetAll();
    g_hasTmp = true; g_envTmp = "C:\\ignored";
    InstallEnvHooks(&TestGetFullPathFail);
    InstallWorkdir("F:\\cwd\\");   // already separator-terminated
    char* r = VfsGetTempDir();
    CHECK_EQ(std::string(r), std::string("F:\\cwd\\"));
}

// Memoization: the result is computed once; changing hooks afterward does not
// recompute until the cache is reset (mirrors the process-lifetime static).
TEST(VfsRecon3, ResultIsMemoized) {
    ResetAll();
    g_hasTmp = true; g_envTmp = "X:\\first\\";
    InstallEnvHooks(&TestGetFullPathPassthrough);
    char* r1 = VfsGetTempDir();
    CHECK_EQ(std::string(r1), std::string("X:\\first\\"));

    // Change the env; without a reset the cached value must persist.
    g_envTmp = "Y:\\second\\";
    char* r2 = VfsGetTempDir();
    CHECK_EQ(std::string(r2), std::string("X:\\first\\"));

    // After a reset it recomputes.
    VfsResetTempDirCache();
    char* r3 = VfsGetTempDir();
    CHECK_EQ(std::string(r3), std::string("Y:\\second\\"));
}

// Returned pointer is stable across calls (same static buffer).
TEST(VfsRecon3, StablePointer) {
    ResetAll();
    g_hasTmp = true; g_envTmp = "Z:\\p\\";
    InstallEnvHooks(&TestGetFullPathPassthrough);
    char* a = VfsGetTempDir();
    char* b = VfsGetTempDir();
    CHECK(a == b);
}
