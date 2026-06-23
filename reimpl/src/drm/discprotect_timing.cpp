// =============================================================================
// guild::drm — DiscProtect sector-table + weak-sector timing analysis (impl)
// 1:1 reconstruction. See discprotect_timing.h for scope / hardware boundary.
// =============================================================================
#include "drm/discprotect_timing.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace guild::drm::timing {

// ---------------------------------------------------------------------------
// gilde.exe 0x14165f0 — VIBE_DiscProtect_CountSectorEntries
// for ( i = 0; dword_142C48C[16*a1 + i] > 0; ++i ) ; return i;
// ---------------------------------------------------------------------------
int CountSectorEntries(const DiscProtectState& s, int track) {
    int i;
    for (i = 0; s.sectorSize[track][i] > 0; ++i)
        ;
    return i;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x1416630 — VIBE_DiscProtect_FindSectorByByteOffset
//   v4 = 0;
//   for ( i = a2 - 1; v4 < dword_142C45C && i < dword_142C47C[a1];
//         v4 += dword_142C48C[16*a1 + i] * a3 )
//       ++i;
//   return i - 1;
// (baseLba == dword_142C45C, sectorCount == dword_142C47C)
// ---------------------------------------------------------------------------
int FindSectorByByteOffset(const DiscProtectState& s, int track, int start, int scale) {
    int v4 = 0;
    int i;
    for (i = start - 1; v4 < s.baseLba && i < s.sectorCount[track];
         v4 += s.sectorSize[track][i] * scale)
        ++i;
    return i - 1;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x14166a0 — VIBE_DiscProtect_SumSectorSizes
//   v4 = 0; while ( a2 <= a3 ) v4 += dword_142C48C[16*a1 + a2++]; return v4;
// ---------------------------------------------------------------------------
int SumSectorSizes(const DiscProtectState& s, int track, int first, int last) {
    int v4 = 0;
    while (first <= last)
        v4 += s.sectorSize[track][first++];
    return v4;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x1416730 — VIBE_DiscProtect_CompareBytes
// Count matching bytes over len. (signed char comparison in original)
// ---------------------------------------------------------------------------
int CompareBytes(const u8* a, const u8* b, int len) {
    int v5 = 0;
    for (int i = 0; i < len; ++i) {
        if ((char)a[i] == (char)b[i])
            ++v5;
    }
    return v5;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x14166f0 — VIBE_Math_CompareFloat (qsort comparator)
//   if ( *a1 >  *a2 ) return 1;
//   if ( *a1 >= *a2 ) return 0;   // i.e. equal
//   return -1;
// ---------------------------------------------------------------------------
int CompareFloat(const void* a, const void* b) {
    f32 x = *static_cast<const f32*>(a);
    f32 y = *static_cast<const f32*>(b);
    if ((f64)x > (f64)y) return 1;
    if ((f64)x >= (f64)y) return 0;
    return -1;
}

// ===========================================================================
// gilde.exe 0x1415620 — VIBE_DiscProtect_BuildSectorTable
// Builds the scrambled per-frame offset/boundary tables. The original mixes a
// 1024-byte scramble buffer v31 derived from the two key tables (a2/a3) with a
// running offset (67500*j + 4*((a1-3600)>>2) +/- per-byte deltas). When a4
// (passthrough) is set the *raw* running offset is written instead of the
// scrambled value. Reconstructed verbatim.
// ===========================================================================
void BuildSectorTable(int a1, const u8* a2, const u8* a3, int a4,
                      u16* a5, i32* a6, u16* a7, i32* a8, u8* a9) {
    // a9 -> 14-byte header record (see disasm).
    *reinterpret_cast<u16*>(a9 + 0) = 100;
    *reinterpret_cast<u16*>(a9 + 2) = 132;
    *reinterpret_cast<u16*>(a9 + 4) = 128;
    *reinterpret_cast<i32*>(a9 + 6) = 1022739087;

    *a5 = 0;
    *a7 = 0;
    int v28 = 1;
    int v11 = 0;

    u8 v31[1024];
    for (int i = 0; i < 1024; ++i) {
        if (a4) {
            v31[i] = 0x80;
        } else {
            int t = ((i + a2[i % 8]) ^ a3[i % 9]) / 4;
            u32 v10 = (4u * (u32)t) & 0x8000007F;
            if ((t & 0x20000000) != 0)
                v10 = (u8)(((v10 - 1) | 0x80) + 1);
            v31[i] = (u8)v10;
        }
    }

    for (int j = 0; j < 4; ++j) {
        int v14 = 67500 * j + 4 * (int)(((u32)(a1 - 3600)) >> 2) - (128 - (int)v31[v11]);
        u32 v22 = (u32)(67500 * j) + 4u * (((u32)(a1 - 3600)) >> 2);
        for (int k = 0; k < 9; ++k) {
            if (!v28) {
                a8[(u16)*a7] = a4 ? (i32)v22 : (i32)v14;
                ++*a7;
            }
            int v15 = 128 - (int)v31[v11] + v14 + 100;
            int v12 = v11 + 1;
            int v16 = (int)v31[v12] + v15 + 132;
            u32 v23 = v22 + 360;
            a6[(360 * j + 40 * k) / 4 + 0] = a4 ? (i32)v23 : (i32)v16;
            ++*a5;
            a8[(u16)*a7] = a4 ? (i32)v23 : (i32)v16;
            ++*a7;
            v28 = 0;
            int v17 = 128 - (int)v31[v12] + v16 + 100;
            v11 = v12 + 1;
            v14 = (int)v31[v11] + v17 + 132;
            v22 = v23 + 360;
            a6[(360 * j + 40 * k) / 4 + 1] = a4 ? (i32)v22 : (i32)v14;
            ++*a5;
            u8 v26 = 0x80;
            for (int m = 0; m < 8; ++m) {
                if ((a2[k] & v26) != 0) {
                    if (v28 == 1) {
                        a8[(u16)*a7] = a4 ? (i32)v22 : (i32)v14;
                        ++*a7;
                        v28 = 0;
                    }
                    int v18 = 128 - (int)v31[v11++] + v14 + 100;
                    v14 = (int)v31[v11] + v18 + 132;
                    v22 += 360;
                    a6[(40 * k + 360 * j) / 4 + m + 2] = a4 ? (i32)v22 : (i32)v14;
                    ++*a5;
                } else {
                    if (!v28) {
                        a8[(u16)*a7] = a4 ? (i32)v22 : (i32)v14;
                        ++*a7;
                        v28 = 1;
                    }
                    int v19 = 128 - (int)v31[v11++] + v14 + 100;
                    v14 = (int)v31[v11] + v19 + 132;
                    v22 += 360;
                    a6[(40 * k + 360 * j) / 4 + m + 2] = a4 ? (i32)v22 : (i32)v14;
                    ++*a5;
                }
                v26 >>= 1;
            }
        }
        if (v28 == 1) {
            a8[(u16)*a7] = a4 ? (i32)v22 : (i32)v14;
            ++*a7;
        }
        int v20 = 128 - (int)v31[v11] + v14 + 100;
        int v13 = v11 + 1;
        int v21 = (int)v31[v13] + v20 + 132;
        u32 v24 = v22 + 360;
        a8[(u16)*a7] = a4 ? (i32)v24 : (i32)v21;
        ++*a7;
        v28 = 1;
        v11 = v13 + 1;
    }
}

// ===========================================================================
// gilde.exe 0x1416790 — VIBE_DiscProtect_ComputeFrameTiming
//   v8 = 90*a1 + a4 + 10*a3;  clamp to [0, 90*(a1+1)-1]
//   v7 = (double)(a2 + frameLba[v8] - baseLba)
//   v9 = (float)sqrt(0.032*v7 + 2500.0)
//   return a5 * v9 / (float)a2
// (a1=track, a2=sectorSize, a3=k, a4=idx, a5=sample)
// ===========================================================================
f64 ComputeFrameTiming(const DiscProtectState& s, int a1, int a2, int a3,
                       int a4, f32 a5) {
    int v8 = 90 * a1 + a4 + 10 * a3;
    if (v8 >= 0) {
        if (v8 > 90 * (a1 + 1) - 1)
            v8 = 90 * (a1 + 1) - 1;
    } else {
        v8 = 0;
    }
    f64 v7 = (f64)(a2 + s.frameLba[v8] - s.baseLba);
    f32 v9 = (f32)std::sqrt(kSqrtScaleA * v7 + kSqrtBias);
    f32 v6 = (f32)a2;
    return a5 * v9 / v6;
}

// ===========================================================================
// gilde.exe 0x1416830 — VIBE_DiscProtect_AnalyzeFrameTimings
// Weak-sector classification for one (track,k). Reconstructed 1:1; the original
// fprintf debug output is gated on `verbose` and dropped (no observable state).
// frameSamples is the *(float*)(a1 + ...) sample row: index 4*(10*k + idx).
// outBytes is *(BYTE*)(a3 + i + 8*k).
// ===========================================================================
int AnalyzeFrameTimings(const DiscProtectState& s, const f32* frameSamples,
                        u8* outBytes, int a4, int sectorSize, int k, int /*verbose*/) {
    const int a1k = 10 * k;       // 10*HIDWORD(a5) base into the sample row
    const int patk = 8 * k;       // 8*HIDWORD(a5) base into pattern/out

    int v32 = 0, v33 = 0, v34 = 0, v28 = 0;

    f32 v30 = (f32)ComputeFrameTiming(s, a4, sectorSize, k, 0,
                                      frameSamples[a1k * 4 / 4 + 0]); // a1 + 40*k
    f32 v29 = (f32)ComputeFrameTiming(s, a4, sectorSize, k, 1,
                                      frameSamples[a1k * 4 / 4 + 1]); // a1 + 40*k + 4

    for (int i = 0; i < 8; ++i) {
        f32 v19 = (f32)ComputeFrameTiming(s, a4, sectorSize, k, i + 2,
                                          frameSamples[a1k + i + 2]);

        int v11;
        if (i)
            v11 = (char)s.expectedPattern[patk + i - 1];
        else
            v11 = 1;
        if (v11 != (char)s.expectedPattern[patk + i]) {
            f32 v15 = (f32)ComputeFrameTiming(s, a4, sectorSize, k, i + 1,
                                              frameSamples[a1k + i + 2 - 1]);
            if ((f64)((v15 + v19) * kPairScale) / kHalfDivisor > std::fabs((f64)(v15 - v19)))
                ++v34;
            ++v28;
        }

        f32 v23[17];
        f32 v27[13];
        for (int j = -8; j <= 8; ++j) {
            int v14 = j + a1k + i + 2;
            if (v14 >= 0) {
                if (v14 >= 90) v14 = 89;
            } else {
                v14 = 0;
            }
            v23[j + 8] = (f32)ComputeFrameTiming(s, a4, sectorSize, k, i + j + 2,
                                                 frameSamples[v14]);
            int v13 = j + i + patk;
            char v12;
            if (v13 >= -2) {
                if (v13 < s.patternLen) {
                    switch (j + i) {
                        case -2: v12 = 48; break;
                        case -1: v12 = 49; break;
                        case 8:  v12 = 48; break;
                        case 9:  v12 = 49; break;
                        default: v12 = (char)s.expectedPattern[v13]; break;
                    }
                } else {
                    v12 = (char)s.expectedPattern[s.patternLen - 1];
                }
            } else {
                v12 = 48;
            }
            f64 v10 = (v12 == 49) ? (f64)kRatio097 : 1.0;
            f32 v9 = (f32)v10;
            // ORIGINAL BUG (preserved-behavior): the binary writes v27[j+4] for
            // j in [-8..8] into a 13-element array, i.e. v27[-4..12]. The negative
            // indices (j < -4) stamp the stack slots just below v27 (v24/v25/v26),
            // which are unconditionally reassigned below before any read, so those
            // writes are dead. We elide the out-of-bounds writes (j < -4) — this is
            // behavior-identical and avoids C++ UB. v27[0..8] (j -4..4) is what the
            // 9-element qsort below sorts; v27[9..12] (j 5..8) are written but never
            // read. (gilde.exe 0x1416be4)
            if (j + 4 >= 0)
                v27[j + 4] = v9 * v23[j + 8];
        }

        std::qsort(v27, 9, sizeof(f32), &CompareFloat);
        std::qsort(v23, 17, sizeof(f32), &CompareFloat);

        // v25 accumulation loop runs but is overwritten by the (v23[2]+v23[14])/2
        // assignment below — reproduced faithfully (the loop has no side effect).
        f32 v25 = 0.0f;
        for (int kk = 4; kk < 13; ++kk) v25 = v25 + v23[kk];
        v25 = (f32)((f64)(v23[2] + v23[14]) / kHalfDivisor);

        f32 v16 = 0.0f;
        for (int m = 2; m < 7; ++m) v16 = v16 + v27[m];
        f32 v17 = v16 / kFiveCount;

        f32 v18 = v17 / kRatio097;
        f32 v24 = v17;

        if ((char)s.expectedPattern[patk + i] == 49) {
            ++v32;
            f64 v6 = std::fabs((f64)(v19 - v25));
            if ((f64)(v25 * kEps015) > v6)
                ++v33;
        }

        u8 v26;
        if ((f64)(v17 * kHi108) >= (f64)v19) {
            if ((f64)(v17 * kLo092) <= (f64)v19) {
                if ((char)s.expectedPattern[patk + i] == 49) {
                    if (((f64)v19 < (f64)v18 || (f64)(v18 * kEps003) <= (f64)v19 - (f64)v18)
                     && ((f64)v19 > (f64)v18 || (f64)(v18 * kEps015) <= (f64)v18 - (f64)v19)) {
                        v26 = 99;
                    } else {
                        v26 = 49;
                    }
                } else if ((char)s.expectedPattern[patk + i] == 48) {
                    if (((f64)v19 > (f64)v24 || (f64)(v24 * kEps003) <= (f64)v24 - (f64)v19)
                     && ((f64)v19 < (f64)v24 || (f64)(v24 * kEps015) <= (f64)v19 - (f64)v24)) {
                        v26 = 99;
                    } else {
                        v26 = 48;
                    }
                } else {
                    v26 = 119;
                }
            } else {
                v26 = 121;
            }
        } else {
            v26 = 120;
        }
        outBytes[patk + i] = v26;
    }

    if (v33 > v32 / 2) {
        for (int i = 0; i < 8; ++i) {
            f64 v7 = std::fabs((f64)(v30 - v29));
            if ((f64)((v30 + v29) / kTwo * kEps015) > v7
                || (char)s.expectedPattern[patk + i] == 48)
                outBytes[patk + i] = 102;
        }
    }

    int result = v28 / 2;
    if (v34 > v28 / 2) {
        for (int i = 0; i < 8; ++i) {
            outBytes[patk + i] = 103;
            result = i + 1;
        }
    }
    return result;
}

// ===========================================================================
// gilde.exe 0x1416ff0 — VIBE_DiscProtect_FindBestSectorMatch
// Scan all (track,sector) with positive samples; run AnalyzeFrameTimings over the
// 9 k-values into a 72-byte scratch; pick the sector whose pattern best matches.
// ===========================================================================
f64 FindBestSectorMatch(DiscProtectState& s, int* outSize, int* outTrack) {
    int v9 = -1;
    f64 v11 = -1.0;
    *outSize = -1;
    *outTrack = -1;

    u8 v12[72];
    std::memset(v12, 0, sizeof(v12));

    for (int i = 0; i < s.trackCount; ++i) {
        for (int j = 0; j < s.sectorCount[i]; ++j) {
            if (s.sampleCount[i][j] > 0) {
                const f32* row = s.samples[i][j].data();
                for (int k = 0; k < 9; ++k) {
                    AnalyzeFrameTimings(s, row, v12, i, s.sectorSize[i][j], k, 0);
                }
                int v7 = CompareBytes(s.expectedPattern.data(), v12, s.patternLen);
                if (v7 > v9) {
                    v9 = v7;
                    *outSize = s.sectorSize[i][j];
                    *outTrack = i;
                    v11 = (f64)((f32)v7 / (f32)s.patternLen);
                }
            }
        }
    }
    return v11;
}

// ===========================================================================
// gilde.exe 0x14178c0 — VIBE_DiscProtect_InitSectorMap
// Build frameLba (1440 entries) and the 72-byte expected pattern.
//   frame layout per (track i in 0..3, frame j in 0..8):
//     [0] = srcEven[90*i + 10*j]           - baseLba
//     [1] = srcOdd [90*i + 10*j]           - baseLba   (dword_145AEA4 == +4)
//     [2..9] = srcEven[90*i + 2 + 10*j + k]- baseLba  (k 0..7)
//   pattern: 9 bytes -> 72 bits, MSB-first, ASCII '0'/'1'.
// (baseLba == dword_1455F64, here folded into DiscProtectState::baseLba.)
// ===========================================================================
void InitSectorMap(DiscProtectState& s, const int* srcLbaEven, const int* srcLbaOdd,
                   const u8 patternBytes[9]) {
    s.frameLba.assign(1440, 0);
    int v7 = 0;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 9; ++j) {
            s.frameLba[v7] = srcLbaEven[90 * i + 10 * j] - s.baseLba;
            int v8 = v7 + 1;
            s.frameLba[v8] = srcLbaOdd[90 * i + 10 * j] - s.baseLba;
            v7 = v8 + 1;
            for (int k = 0; k < 8; ++k)
                s.frameLba[v7++] = srcLbaEven[90 * i + 2 + 10 * j + k] - s.baseLba;
        }
    }

    s.patternLen = 72;
    s.expectedPattern.assign(72, 0);
    int v9 = 0;
    for (int m = 0; m < 9; ++m) {
        int v2 = 128;
        for (int n = 0; n < 8; ++n) {
            s.expectedPattern[v9] = (u8)(((u8)(v2 & patternBytes[m]) != 0) + 48);
            ++v9;
            v2 >>= 1;
        }
    }
}

// ===========================================================================
// gilde.exe 0x14160c0 — VIBE_DiscIo_ElapsedSeconds
//   v5 = (float)UInt64Multiply(t1 - t0, 1000);  v4 = (float)freq;  return v5/v4;
// ===========================================================================
f32 ElapsedSeconds(i64 t0, i64 t1, i64 freq) {
    f32 v5 = (f32)((t1 - t0) * (i64)1000);
    f32 v4 = (f32)freq;
    return v5 / v4;
}

// ===========================================================================
// gilde.exe 0x1417580 — VIBE_DiscProtect_MeasureSectorTimings
// Read + timer routed through `dev`; loop / accumulation reconstructed 1:1.
//   for i in [trackFirst..trackLast]:
//     for j in [90*trackFirst .. 90*(trackFirst+1)-1]:
//       v6 = frameLba[j] - gap
//       (optional pre-read hook)
//       read(v6)                         (prelude)
//       v6 += gap
//       t0 = perf();  read(v6);  t1 = perf()
//       elapsed = ElapsedSeconds(t0,t1,freq); slow-read count if > 350
//       outTimings.push_back(elapsed)
// (a2==gap, a3==trackFirst, a4==trackLast)
// ===========================================================================
int MeasureSectorTimings(DiscProtectState& s, DiscDevice& dev, int handle, int gap,
                         int trackFirst, int trackLast, std::vector<f32>& outTimings) {
    u8 buf[2048];
    int result = trackFirst;
    int counterIdx = 0; // dword_1464CC8 reset each outer iteration in original
    for (int i = trackFirst; i <= trackLast; ++i) {
        result = 90 * trackFirst;
        int v9 = 90 * (trackFirst + 1) - 1;
        counterIdx = 0;
        for (int j = 90 * trackFirst; j <= v9; ++j) {
            int v6 = s.frameLba[j] - gap;
            // (dword_145A034 pre-read delay hook: inert here)
            int ok = dev.readSector(handle, v6, 2048, buf);
            if (!ok) ++s.readFailures;
            v6 += gap;
            i64 t0 = dev.perfCounter();
            ok = dev.readSector(handle, v6, 2048, buf);
            i64 t1 = dev.perfCounter();
            if (!ok) ++s.readFailures;
            f32 v7 = ElapsedSeconds(t0, t1, s.perfFreq);
            if ((f64)v7 > (f64)kTimeoutSeconds) ++s.slowReads;
            if ((int)outTimings.size() <= counterIdx) outTimings.resize(counterIdx + 1);
            outTimings[counterIdx++] = v7;
            result = j + 1;
        }
    }
    return result;
}

// ===========================================================================
// gilde.exe 0x1417160 — VIBE_DiscProtect_ScanSectorTimings
// Read + timer routed through `dev`; full scan loop reconstructed 1:1.
//   v20[track]=0 start cursor; v23[track]=sectorCount-1 end cursor.
//   do { for j in [trackFirst..trackLast]:
//          v14 = 90*j; v12 = 90*(j+1)-1; v16 = v20[j];
//          SectorByByteOffset = FindSectorByByteOffset(j, v16, 1)
//          zero sampleCount[j][v16..SectorByByteOffset]
//          for m in [v14..v12]:
//            sum = SumSectorSizes(j, v16, SectorByByteOffset)
//            lba = frameLba[m] - sum
//            read(lba)  (prelude)
//            for k in [v16..SectorByByteOffset]:
//              lba += sectorSize[j][k]
//              t0=perf(); read(lba); t1=perf()
//              elapsed = ElapsedSeconds(...)
//              samples[j][k][sampleCount[j][k]++] = elapsed
//          optional incremental best-match early-out (ratio > 0.7)
//          v20[j] = SectorByByteOffset + 1
//      } while ( all v20[t] <= v23[t] )
// (a1==handle, a2==trackFirst, a3==trackLast)
// ===========================================================================
void ScanSectorTimings(DiscProtectState& s, DiscDevice& dev, int trackFirstHandle,
                       int trackFirst, int trackLast) {
    const int handle = trackFirstHandle;
    u8 buf[2048];
    int v20[4] = {0, 0, 0, 0};
    int v23[4] = {0, 0, 0, 0};
    for (int i = 0; i < s.trackCount; ++i) {
        v20[i] = 0;
        v23[i] = s.sectorCount[i] - 1;
    }
    do {
        for (int j = trackFirst; j <= trackLast; ++j) {
            int v14 = 90 * j;
            int v12 = 90 * (j + 1) - 1;
            int v16 = v20[j];
            int sectorByByteOffset = FindSectorByByteOffset(s, j, v16, 1);
            for (int k = v16; k <= sectorByByteOffset; ++k)
                s.sampleCount[j][k] = 0;
            for (int m = v14; m <= v12; ++m) {
                int sum = SumSectorSizes(s, j, v16, sectorByByteOffset);
                int lba = s.frameLba[m] - sum;
                // (dword_145A034 pre-read delay hook: inert here)
                int ok = dev.readSector(handle, lba, 2048, buf);
                if (!ok) { /* "read sector %d failed" */ ++s.readFailures; }
                for (int k = v16; k <= sectorByByteOffset; ++k) {
                    int v8 = s.sectorSize[j][k];
                    lba += v8;
                    i64 t0 = dev.perfCounter();
                    ok = dev.readSector(handle, lba, 2048, buf);
                    i64 t1 = dev.perfCounter();
                    if (!ok) ++s.readFailures;
                    f32 v9 = ElapsedSeconds(t0, t1, s.perfFreq);
                    if ((f64)v9 > (f64)kTimeoutSeconds) ++s.slowReads;
                    int& cnt = s.sampleCount[j][k];
                    if ((int)s.samples[j][k].size() <= cnt)
                        s.samples[j][k].resize(cnt + 1);
                    s.samples[j][k][cnt++] = v9;
                }
            }
            if (s.incrementalMatch) {
                int bestSize, bestTrack;
                f64 best = FindBestSectorMatch(s, &bestSize, &bestTrack);
                if (best > kBestMatchCutoff)
                    return;
            }
            v20[j] = sectorByByteOffset + 1;
        }
    } while (v20[0] <= v23[0] && v20[1] <= v23[1] && v20[2] <= v23[2] && v20[3] <= v23[3]);
}

} // namespace guild::drm::timing
