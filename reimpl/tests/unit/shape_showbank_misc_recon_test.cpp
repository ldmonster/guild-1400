// Golden tests for VIBE_Velocity_Apply / shp_ShowShapeFromBank
// (shape_showbank_misc_recon).
#include "tests/framework/test.h"
#include "render/shape_showbank_misc_recon.h"
#include <cstring>
#include <vector>

using namespace guild;
using guild::render::Shape_ShowFromBank;

namespace {
struct Capture {
    int a1 = 0, a2 = 0, a4 = 0;
    u8* shape = nullptr;
    u8 flagSeenDuringCall = 0xFF;
    int calls = 0;
    int errors = 0;
};
Capture g_cap;

void CaptureProcess(int a1, int a2, u8* shape, int a4) {
    g_cap.a1 = a1; g_cap.a2 = a2; g_cap.a4 = a4; g_cap.shape = shape;
    g_cap.flagSeenDuringCall = shape[0x0d]; // must be forced to 2 here
    ++g_cap.calls;
}
void CaptureError(const char* /*name*/) { ++g_cap.errors; }

// Build a bank: shapeCount at +0x2a, shape-offset table (u32) starting at +0x45.
// Place one shape record at offset `shapeOff`; its frame flag is at +0x0d.
std::vector<u8> MakeBank(int count, u32 shapeOff, u8 initialFlag) {
    std::vector<u8> bank(shapeOff + 0x20, 0);
    u16 c = static_cast<u16>(count);
    std::memcpy(&bank[0x2a], &c, 2);
    std::memcpy(&bank[0x45], &shapeOff, 4); // offset for shape index 0
    bank[shapeOff + 0x0d] = initialFlag;
    return bank;
}
} // namespace

TEST(MiscReconShowBank, DispatchesAndForcesFlagThenRestores) {
    g_cap = Capture{};
    auto bank = MakeBank(/*count*/ 4, /*shapeOff*/ 0x80, /*flag*/ 9);
    int r = Shape_ShowFromBank(11, 22, bank.data(), 33, /*shapeNr*/ 0,
                               CaptureProcess, CaptureError);
    CHECK_EQ(r, 1);
    CHECK_EQ(g_cap.calls, 1);
    CHECK_EQ(g_cap.a1, 11);
    CHECK_EQ(g_cap.a2, 22);
    CHECK_EQ(g_cap.a4, 33);
    CHECK_EQ(g_cap.flagSeenDuringCall, static_cast<u8>(2)); // forced to 2
    CHECK_EQ(bank[0x80 + 0x0d], static_cast<u8>(9));        // restored
    CHECK_EQ(g_cap.shape, bank.data() + 0x80);
    CHECK_EQ(g_cap.errors, 0);
}

TEST(MiscReconShowBank, NullBankReturnsZero) {
    g_cap = Capture{};
    int r = Shape_ShowFromBank(0, 0, nullptr, 0, 0, CaptureProcess, CaptureError);
    CHECK_EQ(r, 0);
    CHECK_EQ(g_cap.calls, 0);
}

TEST(MiscReconShowBank, ShapeNrGreaterThanCountErrors) {
    g_cap = Capture{};
    auto bank = MakeBank(/*count*/ 3, 0x80, 9);
    int r = Shape_ShowFromBank(0, 0, bank.data(), 0, /*shapeNr*/ 5,
                               CaptureProcess, CaptureError);
    CHECK_EQ(r, 0);
    CHECK_EQ(g_cap.errors, 1);
    CHECK_EQ(g_cap.calls, 0);
}

TEST(MiscReconShowBank, ShapeNrEqualToCountIsValid) {
    // Original test is `shapeNr > count` -> error; shapeNr == count passes.
    g_cap = Capture{};
    auto bank = MakeBank(/*count*/ 0, 0x80, 9);
    int r = Shape_ShowFromBank(1, 2, bank.data(), 3, /*shapeNr*/ 0,
                               CaptureProcess, CaptureError);
    CHECK_EQ(r, 1);
    CHECK_EQ(g_cap.calls, 1);
}
