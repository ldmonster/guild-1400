#pragma once
// =============================================================================
// guild::drm — DiscProtect sector-table + weak-sector timing analysis
// =============================================================================
//
// 1:1 reconstruction of the PURE math / buffer-management half of the DiscProtect
// copy-protection cluster (gilde.exe, imagebase 0x400000, overlay region 0x140xxxx).
//
// THE USER HAS EXPLICITLY APPROVED reconstructing this cluster 1:1. This file
// contains ONLY the deterministic timing-analysis and sector-table arithmetic:
// frame-timing computation, weak-sector statistics, best-match selection and the
// sector-map build. There are NO circumvention keys and NO disc-layout secrets —
// the tables are caller-supplied; the code reproduces the original's arithmetic.
//
// HARDWARE BOUNDARY (PLAN rules 4/5/8)
//   The original drives a physical drive via two hardware primitives:
//     * VIBE_DiscIo_ReadSectorRaw   — raw sector read (SCSI/ASPI)
//     * VIBE_DiscIo_QueryCounter    — QueryPerformanceCounter timestamp
//   Those are the only non-portable parts. They are routed through the
//   DiscDevice hook interface below, which has an INERT default (reads succeed,
//   counter advances by a fixed step). The ANALYSIS math is NOT faked: the scan /
//   measure loops are reconstructed exactly and operate on the sample arrays the
//   hook (or a test) supplies, so the whole thing runs and is verifiable headless.
//
// DATA MODEL
//   The original keeps its tables in file-scope globals (dword_142C45C, _474,
//   _478, _47C, _48C, 1455F80, 1455F84, 1464C60/64, qword_1464CD0...). To avoid
//   ODR clashes and to make the math testable, those tables live in the
//   DiscProtectState context struct and are passed explicitly. Field names carry
//   the original global address.
// =============================================================================

#include "guild/common/types.h"
#include <cstdint>
#include <vector>

namespace guild::drm::timing {

using namespace ::guild; // u8/u16/u32/i32/i64
using f32 = float;
using f64 = double;

// -----------------------------------------------------------------------------
// Threshold / scaling constants (recovered with get_bytes from .rdata).
// gilde.exe addresses noted; values exact.
// -----------------------------------------------------------------------------
constexpr f64 kSqrtScaleA       = 0.032;  // dbl_14557D8
constexpr f64 kSqrtBias         = 2500.0; // dbl_14557D0
constexpr f64 kHalfDivisor      = 2.0;    // dbl_1455800 (used as /2 and avg base)
constexpr f64 kPairScale        = 0.02;   // dbl_1455808
constexpr f64 kBestMatchCutoff  = 0.7;    // dbl_1455810 (early-out ratio in scan)
constexpr f32 kFiveCount        = 5.0f;   // flt_14557F8 (mean of 5 middle samples)
constexpr f32 kRatio097         = 0.97f;  // flt_14557F4 / weighting for '1' bits
constexpr f32 kEps015           = 0.015f; // flt_14557F0
constexpr f32 kHi108            = 1.08f;  // flt_14557EC (upper accept band)
constexpr f32 kLo092            = 0.92f;  // flt_14557E8 (lower accept band)
constexpr f32 kEps003           = 0.03f;  // flt_14557E4
constexpr f32 kTwo              = 2.0f;   // flt_14557E0
constexpr f32 kSigThreshold     = 0.0f;   // flt_1455820 (signature accept >= 0)
constexpr f32 kTimeoutSeconds   = 350.0f; // flt_142C46C (per-read "too slow" cap)

// -----------------------------------------------------------------------------
// Hardware hook interface. Inert default below. The original primitives:
//   gilde.exe 0x14165a0 — VIBE_DiscIo_ReadSectorRaw(handle, lba, size, buf)
//   gilde.exe 0x14160a0 — VIBE_DiscIo_QueryCounter_Thunk(&counter)
// -----------------------------------------------------------------------------
struct DiscDevice {
    virtual ~DiscDevice() = default;
    // Read `size` bytes of sector at `lba` into `buf`. Return non-zero on success
    // (matches VIBE_DiscIo_ReadSectorRaw / VIBE_Disc_ReadSector return semantics).
    virtual int  readSector(int handle, int lba, u32 size, u8* buf) = 0;
    // Sample the high-resolution performance counter (QueryPerformanceCounter).
    virtual i64  perfCounter() = 0;
};

// Inert default: every read "succeeds", counter advances by a fixed step. Lets the
// scan/measure loops run end-to-end headlessly without real hardware.
struct InertDiscDevice : DiscDevice {
    i64 step = 1;      // counter increment per query
    i64 counter = 0;   // monotonic counter
    int readSector(int /*handle*/, int /*lba*/, u32 /*size*/, u8* /*buf*/) override { return 1; }
    i64 perfCounter() override { i64 c = counter; counter += step; return c; }
};

// -----------------------------------------------------------------------------
// Sector / timing tables (originally file-scope globals). 4 tracks max.
// -----------------------------------------------------------------------------
struct DiscProtectState {
    // dword_142C45C — base LBA offset subtracted from every frame LBA.
    int baseLba = 0;
    // dword_142C474 — length of the expected weak-sector bit pattern (set to 72).
    int patternLen = 0;
    // dword_142C478 — number of active tracks (set to 4 by the caller / map build).
    int trackCount = 0;
    // dword_142C47C[track] — per-track sector-entry count.
    int sectorCount[4] = {0, 0, 0, 0};
    // dword_142C48C[16*track + i] — per-track sector-size table, stride 16 dwords.
    int sectorSize[4][16] = {};

    // dword_1455F80 — frame LBA table. The original Mem_Alloc(1440) reserves 1440
    // bytes == 360 dwords; InitSectorMap writes exactly 360 entries
    // (4 tracks * 9 frames * (2 + 8) = 360). We over-allocate to 1440 entries so the
    // clamp in ComputeFrameTiming (v8 <= 90*(track+1)-1 <= 359) can never read OOB —
    // behavior-identical, the tail is unused.
    std::vector<int> frameLba;            // built by InitSectorMap; logical size 360
    // dword_1455F84 — expected bit pattern, patternLen bytes ('0'/'1' ASCII).
    std::vector<u8>  expectedPattern;     // built by InitSectorMap; size 72

    // dword_1464C60[track] -> float[]   : per-(track,sector) timing-sample arrays.
    // dword_1464C64[track] -> int[]     : per-(track,sector) sample counts.
    // Modeled as owned 2D arrays indexed [track][sector].
    std::vector<std::vector<std::vector<f32>>> samples; // [track][sector] -> samples
    std::vector<std::vector<int>>              sampleCount; // [track][sector]

    // qword_1464CD0 — performance-counter frequency.
    i64 perfFreq = 1;

    // dword_145A034 — optional pre-read delay hook arg (0 == disabled). Inert here.
    int preReadHookArg = 0;
    // dword_142C468 — non-zero => run incremental best-match early-out during scan.
    int incrementalMatch = 0;

    // Diagnostic counters (dword_1455F78 read failures, _7C slow reads).
    int readFailures = 0;
    int slowReads = 0;
};

// -----------------------------------------------------------------------------
// Sector-table queries (pure).
// -----------------------------------------------------------------------------

// gilde.exe 0x14165f0 — VIBE_DiscProtect_CountSectorEntries
// Count leading positive sector-size entries in track `track`.
int CountSectorEntries(const DiscProtectState& s, int track);

// gilde.exe 0x1416630 — VIBE_DiscProtect_FindSectorByByteOffset
// Walk sectors from start, accumulating size*scale until >= baseLba or end of track.
int FindSectorByByteOffset(const DiscProtectState& s, int track, int start, int scale);

// gilde.exe 0x14166a0 — VIBE_DiscProtect_SumSectorSizes
// Inclusive sum of sector sizes [first..last] in track `track`.
int SumSectorSizes(const DiscProtectState& s, int track, int first, int last);

// gilde.exe 0x1416730 — VIBE_DiscProtect_CompareBytes
// Count matching bytes between two buffers over `len` bytes.
int CompareBytes(const u8* a, const u8* b, int len);

// gilde.exe 0x14166f0 — VIBE_Math_CompareFloat (qsort comparator)
int CompareFloat(const void* a, const void* b);

// -----------------------------------------------------------------------------
// Sector-table build (pure).
// -----------------------------------------------------------------------------

// gilde.exe 0x1415620 — VIBE_DiscProtect_BuildSectorTable
// Builds the per-frame timing/offset tables from a scramble key. Reconstructs the
// exact arithmetic. Parameters mirror the original __stdcall(9 args):
//   a1 baseTime, a2/a3 key-table pointers, a4 "passthrough" flag,
//   a5 -> u16 sector counter, a6 -> dword[ ] frame-offset out (4*9*10 region),
//   a7 -> u16 sector counter (second), a8 -> dword[] boundary list,
//   a9 -> 14-byte header record.
void BuildSectorTable(int baseTime, const u8* keyA, const u8* keyB, int passthrough,
                      u16* outSecCountA, i32* outFrameOffsets,
                      u16* outSecCountB, i32* outBoundaries, u8* outHeader);

// -----------------------------------------------------------------------------
// Timing analysis (pure). Operate on caller/hook-supplied sample arrays.
// -----------------------------------------------------------------------------

// gilde.exe 0x1416790 — VIBE_DiscProtect_ComputeFrameTiming
// Map (track, sectorSize, k, idx) + raw sample to a normalized frame time.
f64 ComputeFrameTiming(const DiscProtectState& s, int track, int sectorSize, int k,
                       int idx, f32 sample);

// gilde.exe 0x1416830 — VIBE_DiscProtect_AnalyzeFrameTimings
// For one (track, k): classify the 8 bit-cells of a frame into the result byte
// stream `outBytes` (size patternLen). `frameSamples` is the original
// *(float*)(a1 + ...) array (90 floats per track-row, 10 per frame). Returns the
// "result" value (original eax). `verbose` mirrors the a6 debug-print flag (no I/O
// here). Reconstructs the full weak-sector statistic 1:1.
int AnalyzeFrameTimings(const DiscProtectState& s, const f32* frameSamples,
                        u8* outBytes, int track, int sectorSize, int k, int verbose);

// gilde.exe 0x1416ff0 — VIBE_DiscProtect_FindBestSectorMatch
// Scan every (track, sector) that has positive samples, run AnalyzeFrameTimings
// over all 9 k-values, compare the resulting byte pattern against expectedPattern,
// and keep the best (most-matching) sector. Writes best sector size -> outSize,
// best track -> outTrack. Returns match ratio (best matches / patternLen).
f64 FindBestSectorMatch(DiscProtectState& s, int* outSize, int* outTrack);

// gilde.exe 0x14178c0 — VIBE_DiscProtect_InitSectorMap
// Build frameLba (1440 entries) from a source LBA table and the expected bit
// pattern (72 bytes) from a 9-byte pattern source. `srcLbaEven`/`srcLbaOdd` are the
// two interleaved LBA tables (dword_145AEA0 / _AEA4), `patternBytes` is byte_142DD50.
void InitSectorMap(DiscProtectState& s, const int* srcLbaEven, const int* srcLbaOdd,
                   const u8 patternBytes[9]);

// -----------------------------------------------------------------------------
// Sector timing measurement (read + timer behind hooks; loop math reconstructed).
// -----------------------------------------------------------------------------

// gilde.exe 0x14160c0 — VIBE_DiscIo_ElapsedSeconds
// (float)((t1 - t0) * 1000) / (float)freq. Pure; reconstructed exactly.
f32 ElapsedSeconds(i64 t0, i64 t1, i64 freq);

// gilde.exe 0x1417580 — VIBE_DiscProtect_MeasureSectorTimings
// For tracks [trackFirst..trackLast], for each of the 90 frames in trackFirst,
// read the prelude + the timed sector and record elapsed seconds into `outTimings`.
// `gap` is the original a2 (LBA gap added between prelude and timed read). Returns
// the last frame index processed + 1 (original eax). Read/timer via `dev`.
int MeasureSectorTimings(DiscProtectState& s, DiscDevice& dev, int handle, int gap,
                         int trackFirst, int trackLast, std::vector<f32>& outTimings);

// gilde.exe 0x1417160 — VIBE_DiscProtect_ScanSectorTimings
// Walk sectors of tracks [trackFirst..trackLast], read+time each per-frame sample
// into the per-(track,sector) sample arrays, optionally early-out on a good
// incremental best-match. Read/timer via `dev`.
void ScanSectorTimings(DiscProtectState& s, DiscDevice& dev, int handle,
                       int trackFirst, int trackLast);

} // namespace guild::drm::timing
