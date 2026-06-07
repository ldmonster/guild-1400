#include "test.h"

#include "util/color.h"
#include "util/guid.h"
#include "util/sort.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

using namespace guild;
using namespace guild::util;

namespace {

struct Record {
    int   key;
    int   payload;
};

int CompareRecordByKey(const void* a, const void* b) {
    int ka = static_cast<const Record*>(a)->key;
    int kb = static_cast<const Record*>(b)->key;
    return (ka > kb) - (ka < kb);
}

} // namespace

// End-to-end: build a struct array, sort it by key with the qsort core, then
// bsearch several keys (hits and misses) and verify payloads survive the sort.
TEST(UtilContainersE2E, SortStructsThenBsearch) {
    std::mt19937 rng(20260604u);
    std::uniform_int_distribution<int> keyDist(0, 999);

    std::vector<Record> records;
    // Unique keys so each lookup has a single well-defined target.
    std::vector<int> used(1000, 0);
    while (records.size() < 250) {
        int k = keyDist(rng);
        if (used[k]) continue;
        used[k] = 1;
        records.push_back(Record{k, k * 7 + 3}); // payload derived from key
    }

    QuickSort(records.data(), records.size(), sizeof(Record), CompareRecordByKey);

    // Sorted ascending by key.
    CHECK(std::is_sorted(records.begin(), records.end(),
                         [](const Record& a, const Record& b) {
                             return a.key < b.key;
                         }));

    // Every original (key,payload) pair is still present and intact.
    for (int k = 0; k < 1000; ++k) {
        if (!used[k]) continue;
        Record probe{k, 0};
        const void* hit = std::bsearch(&probe, records.data(), records.size(),
                                       sizeof(Record), CompareRecordByKey);
        CHECK(hit != nullptr);
        const Record* r = static_cast<const Record*>(hit);
        CHECK_EQ(r->key, k);
        CHECK_EQ(r->payload, k * 7 + 3); // payload moved with its key
    }

    // Misses: keys never inserted are not found.
    int misses = 0;
    for (int k = 0; k < 1000 && misses < 20; ++k) {
        if (used[k]) continue;
        ++misses;
        Record probe{k, 0};
        const void* hit = std::bsearch(&probe, records.data(), records.size(),
                                       sizeof(Record), CompareRecordByKey);
        CHECK(hit == nullptr);
    }
}

// End-to-end: sort GUIDs by their canonical text, confirm a known total order,
// and confirm format/parse roundtrips for each sorted element.
TEST(UtilContainersE2E, SortAndRoundtripGuids) {
    std::vector<Guid> guids = {
        {0x00000002u, 0, 0, {0, 0, 0, 0, 0, 0, 0, 0}},
        {0x00000001u, 0, 0, {0, 0, 0, 0, 0, 0, 0, 0}},
        {0x00000003u, 0x000a, 0, {0, 0, 0, 0, 0, 0, 0, 0}},
        {0x00000003u, 0x0009, 0, {0, 0, 0, 0, 0, 0, 0, 0}},
    };

    auto cmp = [](const void* a, const void* b) -> int {
        char ta[kGuidTextSize], tb[kGuidTextSize];
        GuidFormat(static_cast<const Guid*>(a), ta);
        GuidFormat(static_cast<const Guid*>(b), tb);
        return std::strcmp(ta, tb);
    };
    QuickSort(guids.data(), guids.size(), sizeof(Guid), cmp);

    // Expected ascending text order.
    const char* expected[] = {
        "00000001-0000-0000-0000000000000000",
        "00000002-0000-0000-0000000000000000",
        "00000003-0009-0000-0000000000000000",
        "00000003-000a-0000-0000000000000000",
    };
    for (std::size_t i = 0; i < guids.size(); ++i) {
        char buf[kGuidTextSize];
        CHECK(GuidFormat(&guids[i], buf) != 0);
        CHECK_EQ(std::strcmp(buf, expected[i]), 0);
        // Parse back and confirm an identical record.
        Guid back{};
        CHECK(GuidParse(buf, &back));
        CHECK_EQ(std::memcmp(&back, &guids[i], sizeof(Guid)), 0);
    }
}
