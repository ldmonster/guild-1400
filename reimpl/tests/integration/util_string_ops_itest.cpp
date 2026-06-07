#include "test.h"
#include "util/string_ops.h"
#include "util/mem_ops.h"
#include "util/sort.h"

#include <cstring>

// Integration: drive the newly-translated string/mem leaves together with the
// REAL sibling sort core (QuickSort / SwapElements from util/sort.cpp). This is
// the exact shape the engine uses: build fixed-width name records, sort them
// case-insensitively, then binary-search by name — the file-table / asset-name
// lookup pattern (mesh_asset / samplebank_load) reconstructed end to end.

using namespace guild::util;

namespace {

constexpr int kNameLen = 16;
struct NameRec {
    char name[kNameLen];
    int  id;
};

// QuickSort comparator over the real sort core: case-insensitive by name, using
// the translated StrCmpNoCase sibling.
int recCmp(const void* a, const void* b) {
    const NameRec* ra = static_cast<const NameRec*>(a);
    const NameRec* rb = static_cast<const NameRec*>(b);
    return StrCmpNoCase(ra->name, rb->name);
}

// BinarySearch comparator: key is a C string, elem is a NameRec.
int recSearchCmp(const void* key, const void* elem) {
    const char* k = static_cast<const char*>(key);
    const NameRec* r = static_cast<const NameRec*>(elem);
    return StrCmpNoCase(k, r->name);
}

int sgn0(int x) { return (x > 0) - (x < 0); }

} // namespace

TEST(UtilStrOpsItest, SortThenSearchRecords) {
    const char* raw[] = {"Zebra", "apple", "Mango", "cherry", "Banana", "fig", "DATE"};
    constexpr int N = 7;

    NameRec recs[N];
    for (int i = 0; i < N; ++i) {
        // Real engine pattern: StrnCpy into a fixed-width field, MemMove the id in.
        StrnCpy(recs[i].name, raw[i], kNameLen);
        recs[i].name[kNameLen - 1] = '\0';
        int id = 100 + i;
        MemMove(&recs[i].id, &id, sizeof(int));   // exercise MemMove sibling path
    }

    // Sort with the REAL QuickSort + SwapElements core.
    QuickSort(recs, N, sizeof(NameRec), &recCmp);

    // Verify ascending case-insensitive order via the StrCmpNoCase sibling.
    for (int i = 1; i < N; ++i)
        CHECK(StrCmpNoCase(recs[i - 1].name, recs[i].name) <= 0);

    // Every original name must be findable by BinarySearch in the sorted array.
    for (int i = 0; i < N; ++i) {
        char upper[kNameLen];
        StrnCpy(upper, raw[i], kNameLen);
        upper[kNameLen - 1] = '\0';
        StrToUpper(upper);    // search with an upper-cased key -> case-insensitive hit
        void* hit = BinarySearch(upper, recs, sizeof(NameRec), N, &recSearchCmp);
        CHECK(hit != nullptr);
        if (hit) {
            NameRec* r = static_cast<NameRec*>(hit);
            CHECK_EQ(sgn0(StrCmpNoCase(r->name, raw[i])), 0);
        }
    }

    // A name not present must not be found.
    CHECK(BinarySearch("nonexistent", recs, sizeof(NameRec), N, &recSearchCmp) == nullptr);
}

// StrStr over a sorted-then-flattened buffer: locate a record name as a substring
// of a concatenated blob (the text-DB scan pattern), combining StrStr + MemMove.
TEST(UtilStrOpsItest, StrStrInFlattenedBlob) {
    const char* parts[] = {"alpha;", "bravo;", "charlie;", "delta;"};
    char blob[64];
    char* w = blob;
    for (const char* p : parts) {
        std::size_t len = std::strlen(p);
        MemMove2(w, p, len);     // exercise the second memmove translation
        w += len;
    }
    *w = '\0';

    CHECK(StrStr(blob, "charlie") == blob + std::strlen("alpha;bravo;"));
    CHECK(StrStr(blob, "echo") == nullptr);
    CHECK(StrChrLast(blob, ';') == blob + (std::strlen(blob) - 1));  // last separator
}
