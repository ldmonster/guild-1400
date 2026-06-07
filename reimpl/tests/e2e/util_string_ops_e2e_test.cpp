// tests/e2e/util_string_ops_e2e_test.cpp — GUARDED real-asset string-ops e2e.
//
// Reads the REAL Gilde.INI through the IFileSystem shim (DiskFileSystem) and runs
// the newly-translated CRT/util string + sort + memmove leaves over its actual
// bytes: split into lines (StrnLen / StrnCpy), case-insensitively sort the
// section-header lines with the REAL QuickSort core, BinarySearch a known header,
// upper-case a line, and substring-scan for a known key with StrStr. File IO goes
// through the shim (no direct OS calls in the test logic), per the OS boundary.
//
// GUARDED: if the real game dir is absent, the test records ZERO checks and
// returns (clean skip). Override the location with GUILD_GAME_DIR.
#include "test.h"

#include "util/string_ops.h"
#include "util/mem_ops.h"
#include "util/sort.h"

#include "shim_impl/disk_filesystem.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::util;

namespace {

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR"))
        return env;
    return "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";
}

// Read an entire file through the shim into a std::string. Empty on failure.
std::string ReadAll(shim::IFileSystem& fs, const char* path) {
    shim::IFile* f = fs.open(path, "rb");
    if (!f)
        return {};
    std::int64_t sz = f->size();
    std::string out;
    if (sz > 0) {
        out.resize(static_cast<std::size_t>(sz));
        std::size_t got = f->read(&out[0], out.size());
        out.resize(got);
    }
    fs.close(f);
    return out;
}

// Comparator over the real QuickSort core: case-insensitive C-string compare.
int lineCmp(const void* a, const void* b) {
    const char* const* pa = static_cast<const char* const*>(a);
    const char* const* pb = static_cast<const char* const*>(b);
    return StrCmpNoCase(*pa, *pb);
}

int lineSearchCmp(const void* key, const void* elem) {
    const char* k = static_cast<const char*>(key);
    const char* const* e = static_cast<const char* const*>(elem);
    return StrCmpNoCase(k, *e);
}

} // namespace

TEST(UtilStrOpsE2E, RealIniStringOps) {
    const std::string dir = GameDir();
    shim::DiskFileSystem fs(dir);

    if (!fs.exists("Gilde.INI")) {
        std::printf("  [skip] UtilStrOpsE2E.RealIniStringOps: real game dir absent (%s)\n",
                    dir.c_str());
        return; // clean skip
    }

    std::string ini = ReadAll(fs, "Gilde.INI");
    CHECK(!ini.empty());
    if (ini.empty())
        return;

    // Split into NUL-terminated lines using the translated bounded-length /
    // bounded-copy leaves. Section headers are lines starting with '['.
    std::vector<std::string> storage;
    std::vector<char*> headers; // owning pointers into `storage`
    {
        std::size_t i = 0, n = ini.size();
        while (i < n) {
            std::size_t j = i;
            while (j < n && ini[j] != '\n' && ini[j] != '\r')
                ++j;
            std::size_t len = j - i;
            // StrnLen on the (possibly non-terminated) span behaves like the
            // engine's bounded probe; cap is len+1 so a terminator is in range.
            char tmp[256];
            std::size_t copyLen = len < sizeof(tmp) - 1 ? len : sizeof(tmp) - 1;
            // MemMove the raw bytes into a scratch buffer, then NUL-terminate and
            // StrnLen it back — exercises MemMove + StrnLen on real bytes.
            MemMove(tmp, ini.data() + i, copyLen);
            tmp[copyLen] = '\0';
            CHECK_EQ(StrnLen(tmp, (int)sizeof(tmp)), (std::size_t)copyLen);
            if (copyLen > 0 && tmp[0] == '[') {
                storage.emplace_back(tmp, copyLen);
            }
            // advance past the EOL run
            i = j;
            while (i < n && (ini[i] == '\n' || ini[i] == '\r'))
                ++i;
        }
        for (auto& s : storage)
            headers.push_back(&s[0]);
    }

    CHECK(!headers.empty());           // a real INI has section headers
    if (headers.empty())
        return;

    // Sort the header pointers case-insensitively with the REAL QuickSort core.
    QuickSort(headers.data(), headers.size(), sizeof(char*), &lineCmp);
    for (std::size_t k = 1; k < headers.size(); ++k)
        CHECK(StrCmpNoCase(headers[k - 1], headers[k]) <= 0);

    // Every header must be findable by BinarySearch in the sorted array.
    for (std::size_t k = 0; k < headers.size(); ++k) {
        void* hit = BinarySearch(headers[k], headers.data(), sizeof(char*),
                                 headers.size(), &lineSearchCmp);
        CHECK(hit != nullptr);
    }

    // Substring scan for a well-known token in the whole file (case as-is). Real
    // Gilde.INI contains a "[General]" section; verify StrStr + StrChrLast.
    std::string flat = ini;
    char* gen = StrStr(&flat[0], "General");
    if (gen) {
        CHECK(gen >= &flat[0]);
        // The '[' just before "General" is found by scanning back via StrChrLast
        // over the prefix up to the hit.
        std::string prefix(flat.data(), gen - &flat[0] + 7);
        char* lastBracket = StrChrLast(&prefix[0], '[');
        CHECK(lastBracket != nullptr);
    }

    // Upper-case a copy of the first header and confirm round-trip vs a manual fold.
    {
        char buf[256];
        StrnCpy(buf, headers[0], sizeof(buf));
        buf[sizeof(buf) - 1] = '\0';
        StrToUpper(buf);
        for (std::size_t c = 0; buf[c]; ++c) {
            unsigned char ch = (unsigned char)headers[0][c];
            unsigned char expect = (ch >= 'a' && ch <= 'z') ? (unsigned char)(ch - 32) : ch;
            CHECK_EQ((unsigned char)buf[c], expect);
        }
    }
}
