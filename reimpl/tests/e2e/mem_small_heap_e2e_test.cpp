#include "mem/small_heap.h"
#include "tests/framework/test.h"

#include <cstdint>
#include <cstring>
#include <vector>

using namespace guild::mem;

namespace {

// Tiny deterministic LCG so the churn workload is reproducible.
struct Rng {
    std::uint32_t s;
    explicit Rng(std::uint32_t seed) : s(seed) {}
    std::uint32_t next() { s = s * 1664525u + 1013904223u; return s; }
    std::uint32_t range(std::uint32_t lo, std::uint32_t hi) {
        return lo + next() % (hi - lo + 1);
    }
};

// A live allocation tagged with a fill byte + size so we can detect corruption.
struct Live {
    void* p;
    std::uint32_t n;
    std::uint8_t tag;
};

bool CheckFill(const Live& l) {
    const std::uint8_t* q = static_cast<const std::uint8_t*>(l.p);
    for (std::uint32_t i = 0; i < l.n; ++i)
        if (q[i] != l.tag)
            return false;
    return true;
}

} // namespace

// E2E: thousands of small alloc/free/realloc operations across every size class,
// with content verification on each touch — no corruption, correct accounting,
// and a well-formed heap throughout.
TEST(MemSmallHeapE2E, ChurnAcrossSizeClasses) {
    SmallHeap h;
    Rng rng(0xC0FFEEu);
    std::vector<Live> live;
    std::uint8_t tag = 1;

    const int kOps = 30000;
    std::size_t peakLive = 0;

    for (int op = 0; op < kOps; ++op) {
        std::uint32_t roll = rng.range(0, 99);

        if (roll < 50 || live.empty()) {
            // allocate across the full small-block range (1..1016 user bytes)
            std::uint32_t n = rng.range(1, kSmallBlockMax);
            void* p = h.Alloc(n);
            CHECK(p != nullptr);
            if (!p) continue;
            CHECK((reinterpret_cast<std::uintptr_t>(p) & 7u) == 0);
            std::memset(p, tag, n);
            live.push_back({p, n, tag});
            if (++tag == 0) tag = 1;
        } else if (roll < 80) {
            // free a random live block
            std::size_t i = rng.range(0, static_cast<std::uint32_t>(live.size() - 1));
            CHECK(CheckFill(live[i]));         // not clobbered while alive
            h.Free(live[i].p);
            live[i] = live.back();
            live.pop_back();
        } else {
            // realloc a random live block to a new size class
            std::size_t i = rng.range(0, static_cast<std::uint32_t>(live.size() - 1));
            CHECK(CheckFill(live[i]));
            std::uint32_t n2 = rng.range(1, kSmallBlockMax);
            void* np = h.Realloc(live[i].p, n2);
            CHECK(np != nullptr);
            if (!np) continue;
            std::uint32_t keep = live[i].n < n2 ? live[i].n : n2;
            // the preserved prefix must still hold the old tag
            const std::uint8_t* q = static_cast<const std::uint8_t*>(np);
            bool ok = true;
            for (std::uint32_t k = 0; k < keep; ++k)
                if (q[k] != live[i].tag) { ok = false; break; }
            CHECK(ok);
            // re-tag the whole new buffer
            std::memset(np, tag, n2);
            live[i].p = np;
            live[i].n = n2;
            live[i].tag = tag;
            if (++tag == 0) tag = 1;
        }

        if (live.size() > peakLive)
            peakLive = live.size();

        // periodic structural + accounting validation
        if ((op & 0x3FF) == 0) {
            CHECK(h.Validate());
            CHECK_EQ(h.LiveBlocks(), live.size());
        }
    }

    // final integrity: every survivor intact, accounting consistent
    CHECK(h.Validate());
    CHECK_EQ(h.LiveBlocks(), live.size());
    std::size_t expectBytes = 0;
    for (const Live& l : live) {
        CHECK(CheckFill(l));
        // payload accounted = rounded chunk size minus the per-chunk overhead
        // (8-byte head + 8-byte footer = 16; the 64-bit-widened boundary tags,
        // see small_heap.cpp). LiveBytes() uses the same accounting, but a block
        // may occupy a LARGER chunk than its size class when the split remainder
        // was below the 32-byte minimum and the whole chunk was taken — so the
        // live payload is a lower bound, not an exact sum.
        expectBytes += (SmallChunkSize(l.n) - 16);
    }
    CHECK(h.LiveBytes() >= expectBytes);
    CHECK(peakLive > 100);   // the workload actually stressed the heap

    // drain everything; heap must end empty and well-formed
    for (const Live& l : live)
        h.Free(l.p);
    CHECK_EQ(h.LiveBlocks(), 0u);
    CHECK_EQ(h.LiveBytes(), 0u);
    CHECK(h.Validate());
}

// E2E: stable-address invariant — a block's contents survive unrelated churn
// around it (catches accidental cross-block writes during split/coalesce).
TEST(MemSmallHeapE2E, NeighbourChurnDoesNotCorrupt) {
    SmallHeap h;
    Rng rng(0x1234u);

    // a long-lived sentinel block with a known pattern
    const std::uint32_t kN = 512;
    void* keep = h.Alloc(kN);
    CHECK(keep != nullptr);
    for (std::uint32_t i = 0; i < kN; ++i)
        static_cast<std::uint8_t*>(keep)[i] = static_cast<std::uint8_t>(i * 7 + 3);

    std::vector<void*> scratch;
    for (int op = 0; op < 8000; ++op) {
        if (rng.range(0, 1) && !scratch.empty()) {
            std::size_t i = rng.range(0, static_cast<std::uint32_t>(scratch.size() - 1));
            h.Free(scratch[i]);
            scratch[i] = scratch.back();
            scratch.pop_back();
        } else {
            std::uint32_t n = rng.range(1, kSmallBlockMax);
            void* p = h.Alloc(n);
            CHECK(p != nullptr);
            if (p) { std::memset(p, 0xEE, n); scratch.push_back(p); }
        }
        if ((op & 0xFF) == 0) {
            bool ok = true;
            for (std::uint32_t i = 0; i < kN; ++i)
                if (static_cast<std::uint8_t*>(keep)[i] !=
                    static_cast<std::uint8_t>(i * 7 + 3)) { ok = false; break; }
            CHECK(ok);
        }
    }
    // sentinel still pristine after all the churn
    bool ok = true;
    for (std::uint32_t i = 0; i < kN; ++i)
        if (static_cast<std::uint8_t*>(keep)[i] !=
            static_cast<std::uint8_t>(i * 7 + 3)) { ok = false; break; }
    CHECK(ok);
    CHECK(h.Validate());

    for (void* p : scratch)
        h.Free(p);
    h.Free(keep);
    CHECK(h.Validate());
}
