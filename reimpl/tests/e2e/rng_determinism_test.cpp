#include "test.h"
#include "crt/rand.h"
#include "util/math_random.h"
#include <vector>

using namespace guild;

// End-to-end: the RNG underpins the deterministic lockstep simulation. Reseeding
// with the same value must reproduce the identical stream across many draws and
// across the higher-level RandomModulo wrapper — this is the property the whole
// network/save determinism rests on (PLAN §5).
TEST(RngE2E, ReseedReproducesRawStream) {
    std::vector<int> a, b;
    crt::Srand(424242);
    for (int i = 0; i < 50000; ++i) a.push_back(crt::RandNext());
    crt::Srand(424242);
    for (int i = 0; i < 50000; ++i) b.push_back(crt::RandNext());
    CHECK(a == b);
}

TEST(RngE2E, ReseedReproducesModuloStream) {
    std::vector<int> a, b;
    crt::Srand(0xABCDEF);
    for (int i = 0; i < 20000; ++i) a.push_back(util::RandomModulo(100));
    crt::Srand(0xABCDEF);
    for (int i = 0; i < 20000; ++i) b.push_back(util::RandomModulo(100));
    CHECK(a == b);
}

TEST(RngE2E, DifferentSeedsDiverge) {
    crt::Srand(1);
    int x = crt::RandNext();
    crt::Srand(2);
    int y = crt::RandNext();
    CHECK(x != y);
}
