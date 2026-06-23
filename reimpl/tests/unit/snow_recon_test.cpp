// Golden-vector unit tests for the snow scene-update arithmetic.
// Vectors derived from the gilde.exe decompile (reference of record).
#include "tests/framework/test.h"
#include "render/snow_recon.h"

#include <cmath>

using namespace guild;
using namespace guild::render;

namespace {
bool close(float a, float b) { return std::fabs(a - b) < 1e-3f; }
}

TEST(SnowReconCoverage, ThresholdFormula) {
    u32 tr = 0;
    // coverage 100 -> v=100; 4*100/5 = 80; v-16 = 84; 84 < 80 ? no -> 80.
    CHECK_EQ(SnowCoverageThreshold(100.0f, &tr), 80u);
    CHECK_EQ(tr, 100u);
    // coverage 50 -> v=50; 4*50/5 = 40; v-16 = 34; 34 < 40 -> budget = 34.
    CHECK_EQ(SnowCoverageThreshold(50.0f, nullptr), 34u);
    // coverage 255 -> v=255; 4*255/5 = 204; v-16 = 239; 239<204? no -> 204.
    CHECK_EQ(SnowCoverageThreshold(255.0f, nullptr), 204u);
    // truncation toward zero: 99.9 -> 99; 4*99/5 = 79; 99-16=83; 83<79? no -> 79.
    CHECK_EQ(SnowCoverageThreshold(99.9f, &tr), 79u);
    CHECK_EQ(tr, 99u);
}

TEST(SnowReconCoverage, ThresholdSmallCoverageWraps) {
    // coverage 10 -> v=10; 4*10/5 = 8; v-16 = (10-16) wraps to 0xFFFFFFFA
    // (unsigned). 0xFFFFFFFA < 8 ? no -> budget stays 8.
    CHECK_EQ(SnowCoverageThreshold(10.0f, nullptr), 8u);
}

TEST(SnowReconAccum, AccumulatorStep) {
    // acc' = rate*dt + acc
    CHECK(close(SnowAccumulatorStep(100, 0.5f, 10.0f), 100.0f * 0.5f + 10.0f));
    CHECK(close(SnowAccumulatorStep(3, 2.0f, 1.0f), 7.0f));
}

TEST(SnowReconAccum, CoverageFromAccumulatorClamps) {
    // acc * 0.0004 below 255
    CHECK(close(SnowCoverageFromAccumulator(100000.0f), 100000.0f * 0.00039999998989515007f));
    // large acc clamps to 255
    CHECK(close(SnowCoverageFromAccumulator(10000000.0f), 255.0f));
}

TEST(SnowReconAccum, CoverageFromTimerClamps) {
    CHECK(close(SnowCoverageFromTimer(1000.0f), 1000.0f * 0.002f));
    CHECK(close(SnowCoverageFromTimer(1000000.0f), 255.0f));   // clamp
}

TEST(SnowReconBatch, BatchLimit) {
    // total - per >= done_cap -> total/per + done_cap
    // total=100 per=10 done_cap=5 : 100-10=90 >= 5 -> 100/10 + 5 = 15
    CHECK_EQ(SnowBatchLimit(100, 10, 5), 15);
    // total - per < done_cap -> total
    // total=12 per=10 done_cap=5 : 12-10=2 >= 5 ? no -> 12
    CHECK_EQ(SnowBatchLimit(12, 10, 5), 12);
}
