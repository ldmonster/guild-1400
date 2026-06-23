// =============================================================================
// Golden tests for guild::drm DiscProtect sector-table + timing analysis.
// gilde.exe 0x140xxxx cluster (see drm/discprotect_timing.h).
//
// Headless: every test feeds synthetic sector tables / timing-sample arrays
// through the reconstructed ANALYSIS math (no hardware). The hardware leaves
// (sector read + perf counter) are exercised through InertDiscDevice so the
// scan/measure loops run end-to-end without a drive. Hand-traced expected
// values mirror the original Hex-Rays arithmetic exactly.
// =============================================================================
#include "drm/discprotect_timing.h"
#include "test.h"

#include <cmath>
#include <cstring>

using namespace guild;
using namespace guild::drm::timing;

namespace {

// Bit-for-bit reproduction of VIBE_DiscProtect_ComputeFrameTiming for the test
// oracle (mirrors gilde.exe 0x1416790). Used to predict expected outputs.
f64 OracleFrameTiming(const DiscProtectState& s, int track, int sz, int k,
                      int idx, f32 sample) {
    int v8 = 90 * track + idx + 10 * k;
    if (v8 >= 0) {
        if (v8 > 90 * (track + 1) - 1) v8 = 90 * (track + 1) - 1;
    } else {
        v8 = 0;
    }
    f64 v7 = (f64)(sz + s.frameLba[v8] - s.baseLba);
    f32 v9 = (f32)std::sqrt(0.032 * v7 + 2500.0);
    return sample * v9 / (f32)sz;
}

} // namespace

// -----------------------------------------------------------------------------
// CountSectorEntries — counts leading positive entries (stride 16).
// -----------------------------------------------------------------------------
TEST(DiscProtectTiming, CountSectorEntries) {
    DiscProtectState s;
    s.sectorSize[0][0] = 10;
    s.sectorSize[0][1] = 20;
    s.sectorSize[0][2] = 30;
    s.sectorSize[0][3] = 0;   // terminator
    s.sectorSize[0][4] = 99;  // ignored (after terminator)
    CHECK_EQ(CountSectorEntries(s, 0), 3);

    // Negative also terminates (> 0 condition).
    s.sectorSize[1][0] = 5;
    s.sectorSize[1][1] = -1;
    CHECK_EQ(CountSectorEntries(s, 1), 1);

    // Empty track.
    s.sectorSize[2][0] = 0;
    CHECK_EQ(CountSectorEntries(s, 2), 0);
}

// -----------------------------------------------------------------------------
// SumSectorSizes — inclusive sum [first..last].
// -----------------------------------------------------------------------------
TEST(DiscProtectTiming, SumSectorSizes) {
    DiscProtectState s;
    s.sectorSize[0][0] = 100;
    s.sectorSize[0][1] = 200;
    s.sectorSize[0][2] = 300;
    s.sectorSize[0][3] = 400;
    CHECK_EQ(SumSectorSizes(s, 0, 0, 3), 1000);
    CHECK_EQ(SumSectorSizes(s, 0, 1, 2), 500);
    CHECK_EQ(SumSectorSizes(s, 0, 2, 2), 300);
    // first > last => zero (loop never runs).
    CHECK_EQ(SumSectorSizes(s, 0, 3, 1), 0);
}

// -----------------------------------------------------------------------------
// FindSectorByByteOffset — walk until accumulated size*scale >= baseLba, return i-1.
//   v4=0; for(i=start-1; v4<baseLba && i<sectorCount[t]; v4+=size[t][i]*scale) ++i;
//   return i-1;
// -----------------------------------------------------------------------------
TEST(DiscProtectTiming, FindSectorByByteOffset) {
    DiscProtectState s;
    s.baseLba = 250;
    s.sectorCount[0] = 5;
    s.sectorSize[0][0] = 100;
    s.sectorSize[0][1] = 100;
    s.sectorSize[0][2] = 100;
    s.sectorSize[0][3] = 100;
    s.sectorSize[0][4] = 100;
    // start=1 -> i=0. iter: v4=0<250 -> ++i(1), v4+=size[0]*1=100;
    //   v4=100<250 -> ++i(2), v4+=size[1]=200; v4=200<250 -> ++i(3), v4+=size[2]=300;
    //   v4=300 !<250 stop. return i-1 = 2.
    CHECK_EQ(FindSectorByByteOffset(s, 0, 1, 1), 2);

    // Bound by sectorCount: baseLba huge -> walk to end (i hits sectorCount[t]=5).
    s.baseLba = 100000;
    // start=1 -> i=0; advances until i==5 (i<5 fails). return 5-1=4.
    CHECK_EQ(FindSectorByByteOffset(s, 0, 1, 1), 4);

    // scale multiplies the accumulation.
    s.baseLba = 250;
    // scale=3: i=0 v4=0 -> i1 v4=300; 300 !<250 stop -> return 0.
    CHECK_EQ(FindSectorByByteOffset(s, 0, 1, 3), 0);
}

// -----------------------------------------------------------------------------
// CompareBytes — count of matching bytes over len (signed char compare).
// -----------------------------------------------------------------------------
TEST(DiscProtectTiming, CompareBytes) {
    u8 a[8] = {'1', '0', '1', '1', '0', '0', '1', '0'};
    u8 b[8] = {'1', '1', '1', '0', '0', '0', '1', '1'};
    // matches at idx 0,2,4,5,6 -> 5
    CHECK_EQ(CompareBytes(a, b, 8), 5);
    CHECK_EQ(CompareBytes(a, a, 8), 8);
    CHECK_EQ(CompareBytes(a, b, 0), 0);
}

// -----------------------------------------------------------------------------
// CompareFloat — qsort comparator (>:1, ==:0, <:-1).
// -----------------------------------------------------------------------------
TEST(DiscProtectTiming, CompareFloat) {
    f32 x = 2.0f, y = 1.0f, z = 2.0f;
    CHECK_EQ(CompareFloat(&x, &y), 1);
    CHECK_EQ(CompareFloat(&y, &x), -1);
    CHECK_EQ(CompareFloat(&x, &z), 0);
}

// -----------------------------------------------------------------------------
// ElapsedSeconds — (float)((t1-t0)*1000) / (float)freq.
// -----------------------------------------------------------------------------
TEST(DiscProtectTiming, ElapsedSeconds) {
    // (200-100)*1000 = 100000; /1000 = 100.0
    CHECK_EQ(ElapsedSeconds(100, 200, 1000), 100.0f);
    // (10-0)*1000 = 10000; /4 = 2500.0
    CHECK_EQ(ElapsedSeconds(0, 10, 4), 2500.0f);
    CHECK_EQ(ElapsedSeconds(50, 50, 7), 0.0f);
}

// -----------------------------------------------------------------------------
// InitSectorMap — frameLba build (360 logical entries) + 72-byte ASCII pattern.
// -----------------------------------------------------------------------------
TEST(DiscProtectTiming, InitSectorMapPattern) {
    DiscProtectState s;
    s.baseLba = 0;
    // 90*4 = 360 source LBAs for even, same for odd.
    int even[360], odd[360];
    for (int i = 0; i < 360; ++i) { even[i] = i; odd[i] = 1000 + i; }
    // pattern: 0xA5 = 1010 0101, then 8 more bytes.
    u8 pat[9] = {0xA5, 0x00, 0xFF, 0x01, 0x80, 0x55, 0xAA, 0x0F, 0xF0};

    InitSectorMap(s, even, odd, pat);

    CHECK_EQ(s.patternLen, 72);
    CHECK_EQ((int)s.expectedPattern.size(), 72);
    // 0xA5 = 1010 0101 MSB-first -> '1','0','1','0','0','1','0','1'
    const char* exp0 = "10100101";
    for (int i = 0; i < 8; ++i)
        CHECK_EQ((char)s.expectedPattern[i], exp0[i]);
    // 0x00 -> all '0'
    for (int i = 0; i < 8; ++i)
        CHECK_EQ((char)s.expectedPattern[8 + i], '0');
    // 0xFF -> all '1'
    for (int i = 0; i < 8; ++i)
        CHECK_EQ((char)s.expectedPattern[16 + i], '1');
    // 0x80 -> '1' then seven '0'  (byte index 4 -> offset 32)
    CHECK_EQ((char)s.expectedPattern[32], '1');
    for (int i = 1; i < 8; ++i)
        CHECK_EQ((char)s.expectedPattern[32 + i], '0');

    // frameLba layout per (track i, frame j): [0]=even[90i+10j], [1]=odd[90i+10j],
    // [2..9]=even[90i+2+10j+k]. Check track 0 frame 0:
    CHECK_EQ(s.frameLba[0], even[0]);     // 0
    CHECK_EQ(s.frameLba[1], odd[0]);      // 1000
    CHECK_EQ(s.frameLba[2], even[2]);     // 2
    CHECK_EQ(s.frameLba[9], even[9]);     // 2+7=9
    // track 0 frame 1 starts at flat index 10: even[10], odd[10], even[12..]
    CHECK_EQ(s.frameLba[10], even[10]);
    CHECK_EQ(s.frameLba[11], odd[10]);
    CHECK_EQ(s.frameLba[12], even[12]);
    // 360 logical entries written; over-allocation tail stays zero.
    CHECK_EQ((int)s.frameLba.size(), 1440);
    CHECK_EQ(s.frameLba[360], 0);
}

// -----------------------------------------------------------------------------
// ComputeFrameTiming — exact arithmetic + index clamp.
// -----------------------------------------------------------------------------
TEST(DiscProtectTiming, ComputeFrameTiming) {
    DiscProtectState s;
    s.baseLba = 0;
    s.frameLba.assign(1440, 0);
    for (int i = 0; i < 360; ++i) s.frameLba[i] = i * 7;

    // track 0, sz=512, k=1, idx=2 -> v8 = 0 + 2 + 10 = 12, frameLba[12]=84
    f64 got = ComputeFrameTiming(s, 0, 512, 1, 2, 3.0f);
    f64 want = OracleFrameTiming(s, 0, 512, 1, 2, 3.0f);
    CHECK(std::fabs(got - want) < 1e-9);

    // index clamp high: track 0 max v8 = 89. k=20 idx=0 -> v8=200 clamps to 89.
    f64 g2 = ComputeFrameTiming(s, 0, 256, 20, 0, 1.5f);
    f64 w2 = OracleFrameTiming(s, 0, 256, 20, 0, 1.5f);
    CHECK(std::fabs(g2 - w2) < 1e-9);
    // The oracle clamps to 89 too -> frameLba[89] used.
    f32 v9 = (f32)std::sqrt(0.032 * (f64)(256 + s.frameLba[89] - 0) + 2500.0);
    CHECK(std::fabs(g2 - (f64)(1.5f * v9 / (f32)256)) < 1e-9);

    // negative clamp: track 0, k=0, idx=-5 -> v8=-5 -> 0.
    f64 g3 = ComputeFrameTiming(s, 0, 128, 0, -5, 2.0f);
    f64 w3 = OracleFrameTiming(s, 0, 128, 0, -5, 2.0f);
    CHECK(std::fabs(g3 - w3) < 1e-9);
}

// -----------------------------------------------------------------------------
// AnalyzeFrameTimings — full weak-sector classification on a synthetic frame.
// We build an all-'0' expected pattern and uniform samples; with a flat sample
// row every cell lands in the well-behaved band and is classified '0' (48).
// Hand-trace: pattern all '0' so v32=0 (never increments), v33=0; v34/v28 depend
// on bit-transition logic which, with a uniform pattern, never fires (v11 starts
// 1, pattern[0]='0' so first cell DOES differ -> v28 may bump). We assert the
// concrete classification bytes against an independent re-trace below.
// -----------------------------------------------------------------------------
TEST(DiscProtectTiming, AnalyzeFrameTimingsFlat) {
    DiscProtectState s;
    s.baseLba = 0;
    s.patternLen = 72;
    s.frameLba.assign(1440, 0);     // all-zero LBA table -> stable sqrt term
    s.expectedPattern.assign(72, '0');

    // 90-float sample row, all equal to a constant. With frameLba all zero and a
    // constant sample, ComputeFrameTiming returns sample*sqrt(sz*0.032+2500)/sz,
    // identical for every idx -> v19 == v17 (the 5-sample mean) so the accept
    // band (0.92..1.08) holds and pattern '0' yields byte 48 unless the inner
    // jitter test bumps it to 'c' (99). With perfectly equal values:
    //   v18 = v17/0.97, v24 = v17. For pattern '0': checks compare v19 vs v24.
    //   v19==v24 so (v19>v24)false AND (0.03*v24 <= v24-v19=0)?  0.03*v24<=0 false
    //   -> first clause false -> v26 = 48.
    std::vector<f32> row(90, 1.0f);

    u8 out[72];
    std::memset(out, 0xEE, sizeof(out));
    int result = AnalyzeFrameTimings(s, row.data(), out, /*track*/0,
                                     /*sectorSize*/512, /*k*/0, /*verbose*/0);

    // Trace of the transition/jitter counters (independent of the per-cell
    // classification, which is overwritten below):
    //   v32 stays 0 (no '1' bits in an all-'0' pattern) so the v33>v32/2 'f'
    //   overwrite branch never fires.
    //   Transition counter: v11 starts 1, pattern[0]='0' -> differ -> v28=1, and
    //   the pair test (v15+v19)*0.02/2 = 0.02*T > |v15-v19| = 0 -> true -> v34=1.
    //   For i>=1, pattern[i-1]==pattern[i]=='0' -> no transition. So v28=1, v34=1.
    //   v34(1) > v28/2(0) -> true: the FINAL loop unconditionally overwrites all 8
    //   cells with 'g'(103) and sets result = 7+1 = 8, regardless of the earlier
    //   per-cell bytes. This is the load-bearing observable behavior.
    for (int i = 0; i < 8; ++i)
        CHECK_EQ((int)out[i], 103);
    CHECK_EQ(result, 8);
}

// -----------------------------------------------------------------------------
// FindBestSectorMatch — pick the (track,sector) whose classified pattern best
// matches expectedPattern. With InitSectorMap-style state and synthetic samples
// we verify selection picks the higher-match sector and the ratio == matches/len.
// -----------------------------------------------------------------------------
TEST(DiscProtectTiming, FindBestSectorMatch) {
    DiscProtectState s;
    s.baseLba = 0;
    s.patternLen = 72;
    s.trackCount = 1;
    s.frameLba.assign(1440, 0);
    s.expectedPattern.assign(72, '0');

    s.sectorCount[0] = 2;
    s.sectorSize[0][0] = 512;
    s.sectorSize[0][1] = 256;

    // samples[track][sector] -> 90-float rows; sampleCount[track][sector] > 0.
    s.samples.assign(1, std::vector<std::vector<f32>>(2, std::vector<f32>(90, 1.0f)));
    s.sampleCount.assign(1, std::vector<int>(2, 90));

    int outSize = -2, outTrack = -2;
    f64 ratio = FindBestSectorMatch(s, &outSize, &outTrack);

    // Both sectors run through AnalyzeFrameTimings over 9 k-values into the same
    // 72-byte scratch. The scratch becomes all 'g'(103) (see flat trace), which
    // matches NONE of the all-'0' expected pattern -> matches 0 for both, but the
    // FIRST positive-sample sector (track0/sector0) sets v9=0 (>-1) so it wins.
    CHECK_EQ(outTrack, 0);
    CHECK_EQ(outSize, 512);
    // ratio = 0 matches / 72 = 0.0
    CHECK(std::fabs(ratio - 0.0) < 1e-9);

    // Now craft a sector whose scratch matches better. Use an expected pattern of
    // all 'g' so the flat-trace output (all 103) matches fully.
    DiscProtectState s2 = s;
    s2.expectedPattern.assign(72, 103); // 'g'
    int sz2 = -2, tr2 = -2;
    f64 r2 = FindBestSectorMatch(s2, &sz2, &tr2);
    // All 72 bytes match -> ratio 1.0, first sector wins.
    CHECK_EQ(tr2, 0);
    CHECK_EQ(sz2, 512);
    CHECK(std::fabs(r2 - 1.0) < 1e-9);
}

// -----------------------------------------------------------------------------
// MeasureSectorTimings — drives reads/timer via InertDiscDevice. The inert
// counter advances by `step` per query, so each timed read elapses
// step*1000/freq seconds, recorded once per frame (90 per track).
// -----------------------------------------------------------------------------
TEST(DiscProtectTiming, MeasureSectorTimingsInert) {
    DiscProtectState s;
    s.frameLba.assign(1440, 100);
    s.perfFreq = 1000;

    InertDiscDevice dev;
    dev.step = 5;   // each (t1-t0) == 5

    std::vector<f32> timings;
    // tracks [0..0] -> frames 90*0 .. 90*1-1 = 0..89.
    int result = MeasureSectorTimings(s, dev, /*handle*/1, /*gap*/4,
                                      /*trackFirst*/0, /*trackLast*/0, timings);

    // 90 frames recorded; result = last frame + 1 = 90.
    CHECK_EQ(result, 90);
    CHECK_EQ((int)timings.size(), 90);
    // elapsed = (5)*1000/1000 = 5.0 for every frame.
    for (int i = 0; i < 90; ++i)
        CHECK_EQ(timings[i], 5.0f);
    // No read failures (inert read returns success), no slow reads (5 < 350).
    CHECK_EQ(s.readFailures, 0);
    CHECK_EQ(s.slowReads, 0);
}

// -----------------------------------------------------------------------------
// ScanSectorTimings — drives the full scan loop via InertDiscDevice and verifies
// samples land in the per-(track,sector) arrays with the inert elapsed value.
// -----------------------------------------------------------------------------
TEST(DiscProtectTiming, ScanSectorTimingsInert) {
    DiscProtectState s;
    // baseLba huge so FindSectorByByteOffset's accumulator never reaches it and the
    // walk runs to the track end (returns sectorCount-1). This makes one scan pass
    // cover ALL sectors of the track and advance the cursor past the end, so the
    // do/while terminates deterministically. (The original keeps tables sized so
    // the scan always makes forward progress; a tiny baseLba is the degenerate case
    // the engine never produces.)
    s.baseLba = 1000000000;
    s.perfFreq = 1000;
    s.trackCount = 1;
    s.frameLba.assign(1440, 1000);
    s.sectorCount[0] = 3;
    s.sectorSize[0][0] = 100;
    s.sectorSize[0][1] = 100;
    s.sectorSize[0][2] = 100;
    s.incrementalMatch = 0;         // no early-out
    s.expectedPattern.assign(72, '0');
    s.patternLen = 72;

    // sample storage for 1 track, 3 sectors.
    s.samples.assign(1, std::vector<std::vector<f32>>(3));
    s.sampleCount.assign(1, std::vector<int>(3, 0));

    InertDiscDevice dev;
    dev.step = 2;                   // elapsed = 2*1000/1000 = 2.0

    ScanSectorTimings(s, dev, /*handle*/9, /*trackFirst*/0, /*trackLast*/0);

    // First (only) pass: v16 = v20[0] = 0; FindSectorByByteOffset walks to the track
    // end and returns sectorCount-1 = 2, so the inner k-loop covers sectors 0..2.
    // For each of the 90 frames (m in [0..89]) one timed read per sector is recorded
    // -> sampleCount == 90, every sample == 2.0. v20[0] becomes 2+1 = 3 > v23[0]=2
    // -> loop exits.
    for (int sec = 0; sec < 3; ++sec) {
        CHECK_EQ(s.sampleCount[0][sec], 90);
        for (int i = 0; i < 90; ++i)
            CHECK_EQ(s.samples[0][sec][i], 2.0f);
    }
    CHECK_EQ(s.readFailures, 0);
    CHECK_EQ(s.slowReads, 0);
}
