#include "test.h"

#include "util/float_math.h"
#include "util/fpu.h"

#include <cmath>
#include <cstdint>
#include <cstring>

using namespace guild;
using namespace guild::util;

namespace {

bool Close(double a, double b, double eps = 1e-12) {
    double scale = std::fmax(1.0, std::fabs(b));
    return std::fabs(a - b) <= eps * scale;
}

bool SameBits(double a, double b) {
    std::uint64_t x, y;
    std::memcpy(&x, &a, 8);
    std::memcpy(&y, &b, 8);
    return x == y;
}

} // namespace

// End-to-end: set truncate rounding, perform a conversion-like sequence under
// it, restore the control word, and verify both the saved/restored FCW and that
// the math results round-trip. This mirrors the CRT pattern of save-FCW /
// install-rounding / compute / restore-FCW used around x87 conversions.
TEST(UtilFpuE2E, TruncateRoundingSaveRestore) {
    // Start from the CRT default control word.
    SetFcw(0x023F);
    const u16 defaultFcw = GetFcw();
    CHECK_EQ(defaultFcw & kFcw_RC_Mask, (u16)kFcw_RC_Nearest);

    // Install truncate (round-toward-zero) rounding, saving the previous FCW.
    u16 saved = PushFcw(static_cast<u16>((defaultFcw & ~kFcw_RC_Mask) | kFcw_RC_Truncate));
    CHECK_EQ(saved, defaultFcw);
    CHECK_EQ(GetFcw() & kFcw_RC_Mask, (u16)kFcw_RC_Truncate);

    // "Conversion" sequence under the new rounding mode: truncate-to-integer of
    // several values. (Our math helpers are value-equivalent regardless of the
    // modeled rounding bits; the point is the FCW state is what we set.)
    const double samples[] = {2.7, -2.7, 3.999, -3.999, 0.5, -0.5};
    long truncated[6];
    for (int i = 0; i < 6; ++i)
        truncated[i] = static_cast<long>(std::trunc(samples[i]));
    CHECK_EQ(truncated[0], 2L);
    CHECK_EQ(truncated[1], -2L);
    CHECK_EQ(truncated[2], 3L);
    CHECK_EQ(truncated[3], -3L);

    // Restore the saved control word and confirm the round-trip.
    SetFcw(saved);
    CHECK_EQ(GetFcw(), defaultFcw);
    CHECK_EQ(GetFcw() & kFcw_RC_Mask, (u16)kFcw_RC_Nearest);
}

// End-to-end: a mixed math pipeline that exercises Fmod -> Sqrt -> Log -> Atan2
// across both the libm dispatch and the x87-software dispatch, verifying the
// flag selects equivalent results and that classify agrees on the outputs.
TEST(UtilFpuE2E, MathPipelineBothPaths) {
    auto run = [](double a, double b) {
        double r = Fmod(a, b);          // sign of dividend
        double s = Sqrt(std::fabs(r));  // guarded sqrt
        double l = Log(s + 1.0);        // ln, domain-safe
        double t = Atan2(l, b);         // atan2(y, x)
        return t;
    };

    // libm path
    SetX87SoftwarePath(false);
    double libm = run(17.5, 4.0);
    double oracle = std::atan2(std::log(std::sqrt(std::fabs(std::fmod(17.5, 4.0))) + 1.0), 4.0);
    CHECK(Close(libm, oracle));

    // x87-software path: same modeled values
    SetX87SoftwarePath(true);
    double soft = run(17.5, 4.0);
    CHECK(SameBits(libm, soft));
    SetX87SoftwarePath(false);

    // Classify the finite result.
    CHECK_EQ(Classify(libm), (int)kFpNormal);
}

// End-to-end: control-word splice via SetControlWord/ControlMask round-trips
// through the encode/decode pair and reports the resulting abstract word.
TEST(UtilFpuE2E, ControlWordSpliceFlow) {
    SetFcw(0x023F);  // all exceptions masked
    // Unmask zero-divide and invalid (abstract bits 0x08 | 0x10), keep the rest.
    u32 w = SetControlWord(0x00, 0x18);
    CHECK((w & 0x18) == 0);          // those two cleared
    CHECK((w & 0x07) == 0x07);       // others still masked

    // Re-mask the five abstract exception bits and verify the FCW low byte.
    // EncodeControlWord maps abstract 0x1F -> FCW 0x3D (IM|ZM|OM|UM|PM); the DM
    // bit (0x02) is the denormal mask, which is only set via the 0x80000
    // selector, never by the 0x1F exception word. 0x3D is the faithful value.
    u32 w2 = SetControlWord(0x1F, 0x1F);
    CHECK_EQ(w2 & 0x1F, (u32)0x1F);
    CHECK_EQ((u32)(GetFcw() & 0x3F), (u32)0x3D);

    // ControlMask cannot set the denormal selector bit.
    u32 w3 = ControlMask(0x80000, 0x80000);
    CHECK((w3 & 0x80000) == 0);
}
