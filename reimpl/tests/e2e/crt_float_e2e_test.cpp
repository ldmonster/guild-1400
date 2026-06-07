#include "test.h"

#include "crt/dtoa.h"
#include "crt/printf_float.h"
#include "crt/scanf.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <cstdint>
#include <cstring>

using namespace guild::crt;

// End-to-end: format a table of doubles with our %f/%e/%g formatters (which must
// match the C library), then parse the formatted text back via our scanf and
// confirm a clean round-trip. This exercises dtoa + printf_float + scanf together.
TEST(CrtFloatE2E, FormatThenParseRoundTrip) {
    const double table[] = {
        0.0, 1.0, -1.0, 3.14159265358979, 123.456, -987.654,
        0.001, 1000.0, 6.022e23, 1.6e-19, 0.1, 2.0/3.0,
        42.5, -0.25, 100000.0, 0.0009765625, 9999.99,
    };

    for (double v : table) {
        // %f path (use precision 6, matching C default).
        char fbuf[128];
        FormatFixed(v, fbuf, 6);
        char cref[128];
        std::snprintf(cref, sizeof(cref), "%.6f", v);
        CHECK_EQ(std::string(fbuf), std::string(cref));

        double back = 0;
        int n = Sscanf(fbuf, "%lf", &back);
        CHECK_EQ(n, 1);
        // Round-trip through 6-digit fixed text: compare to C's own re-parse.
        double cback = 0;
        std::sscanf(cref, "%lf", &cback);
        CHECK(std::fabs(back - cback) < 1e-9 * (1.0 + std::fabs(cback)));

        // %e path.
        char ebuf[128];
        FormatExponential(v, ebuf, 9, false);
        char eref[128];
        std::snprintf(eref, sizeof(eref), "%.9e", v);
        CHECK_EQ(std::string(ebuf), std::string(eref));

        double eback = 0, ecback = 0;
        int en = Sscanf(ebuf, "%lf", &eback);
        std::sscanf(eref, "%lf", &ecback);
        CHECK_EQ(en, 1);
        CHECK(std::fabs(eback - ecback) < 1e-12 * (1.0 + std::fabs(ecback)));

        // %g path.
        char gbuf[128];
        FormatGeneral(v, gbuf, 12, false);
        char gref[128];
        std::snprintf(gref, sizeof(gref), "%.12g", v);
        CHECK_EQ(std::string(gbuf), std::string(gref));
    }
}

// Randomised round-trip: format many doubles at full precision (%.17g, which is
// exactly invertible for IEEE doubles), parse them back with our scanf, and
// require bit-identical recovery. Also confirm our %.17g text matches C's.
TEST(CrtFloatE2E, RandomRoundTrip17) {
    std::uint64_t state = 0x123456789abcdef0ull;
    auto next = [&]() {
        state ^= state << 13; state ^= state >> 7; state ^= state << 17;
        return state;
    };
    int checked = 0;
    for (int i = 0; i < 5000; ++i) {
        std::uint64_t bits = next();
        double v;
        std::memcpy(&v, &bits, sizeof(v));
        if (!std::isfinite(v)) continue;          // skip inf/nan
        char ours[64];
        FormatGeneral(v, ours, 17, false);
        char cref[64];
        std::snprintf(cref, sizeof(cref), "%.17g", v);
        CHECK_EQ(std::string(ours), std::string(cref));

        double back = 0;
        int n = Sscanf(ours, "%lf", &back);
        CHECK_EQ(n, 1);
        CHECK(back == v);                          // exact round-trip
        ++checked;
    }
    CHECK(checked > 4000);
}

// Parse a whitespace-separated record (mix of int / double / string) the way a
// game data line would be scanned, then verify each field.
TEST(CrtFloatE2E, MixedRecordScan) {
    const char* line = "  17  3.5  widget  -1.25e2  0xFF";
    int id = 0; double weight = 0; char name[32] = {0};
    double price = 0; unsigned flags = 0;
    int n = Sscanf(line, "%d %lf %s %lf %x", &id, &weight, name, &price, &flags);
    CHECK_EQ(n, 5);
    CHECK_EQ(id, 17);
    CHECK(std::fabs(weight - 3.5) < 1e-12);
    CHECK_EQ(std::string(name), std::string("widget"));
    CHECK(std::fabs(price - (-125.0)) < 1e-9);
    CHECK_EQ(flags, 255u);

    // Compare the assignment count against the C library on the same line.
    int cid = 0; double cw = 0; char cn[32] = {0}; double cp = 0; unsigned cf = 0;
    int cn_rc = std::sscanf(line, "%d %lf %s %lf %x", &cid, &cw, cn, &cp, &cf);
    CHECK_EQ(n, cn_rc);
}
