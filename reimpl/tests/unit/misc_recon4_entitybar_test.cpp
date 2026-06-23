// Golden tests for the VIBE_Entity_InteractionLogic fill-fraction prologue.
//   gilde.exe 0x410797..0x410920
#include "test.h"
#include "sim/misc_recon4_entitybar.h"

using namespace guild::sim;

TEST(MiscRecon4, EntityBarHalfFill) {
    // value=50, min=0, max=100, total=200 -> v5 = 200/100 = 2.0
    // fillA = (50-0)*2 = 100; fillB = (value2-0)*2.
    EntityBarFields f;
    f.value = 50; f.minVal = 0; f.maxVal = 100; f.total = 200; f.value2 = 50;
    EntityBarFill r = EntityBarComputeFill(f);
    CHECK_EQ(r.fillA, 100.0f);
    CHECK_EQ(r.fillB, 100.0f);
}

TEST(MiscRecon4, EntityBarZeroFill) {
    // value==min -> fillA = 0 (not clamped, since the gate is >0).
    EntityBarFields f;
    f.value = 0; f.minVal = 0; f.maxVal = 100; f.total = 200; f.value2 = 0;
    EntityBarFill r = EntityBarComputeFill(f);
    CHECK_EQ(r.fillA, 0.0f);
    CHECK_EQ(r.fillB, 0.0f);
}

TEST(MiscRecon4, EntityBarTinyPositiveClampsToOne) {
    // total small enough that fill is in (0,1) -> clamped to 1.0.
    // value=1, min=0, max=100, total=1 -> v5 = 0.01; fillA = 1*0.01 = 0.01 -> 1.0
    EntityBarFields f;
    f.value = 1; f.minVal = 0; f.maxVal = 100; f.total = 1; f.value2 = 1;
    EntityBarFill r = EntityBarComputeFill(f);
    CHECK_EQ(r.fillA, 1.0f);
    CHECK_EQ(r.fillB, 1.0f);
}

TEST(MiscRecon4, EntityBarExactlyOneNotReclamped) {
    // fillA == 1.0 exactly: the gate is (>0 && <1.0), so 1.0 stays 1.0.
    // value=1, min=0, max=100, total=100 -> v5=1.0; fillA = 1*1 = 1.0
    EntityBarFields f;
    f.value = 1; f.minVal = 0; f.maxVal = 100; f.total = 100; f.value2 = 1;
    EntityBarFill r = EntityBarComputeFill(f);
    CHECK_EQ(r.fillA, 1.0f);
}

TEST(MiscRecon4, EntityBarOffsetMinimum) {
    // Non-zero min: value=30, min=10, max=110 (span 100), total=50 -> v5=0.5
    // fillA = (30-10)*0.5 = 10; value2=60 -> fillB = 0.5*(60-10)=25
    EntityBarFields f;
    f.value = 30; f.minVal = 10; f.maxVal = 110; f.total = 50; f.value2 = 60;
    EntityBarFill r = EntityBarComputeFill(f);
    CHECK_EQ(r.fillA, 10.0f);
    CHECK_EQ(r.fillB, 25.0f);
}
