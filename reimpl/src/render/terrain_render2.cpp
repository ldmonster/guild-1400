#include "render/terrain_render2.h"

#include "util/coord.h"  // ConvertX (x87 truncate-toward-zero)
#include "util/math.h"   // CatmullRomInterp

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace guild::render {

// ---------------------------------------------------------------------------
// Inert default hooks. allocDebug returns real heap so the init loops run; the
// rest are no-ops. loadTexture returns 0 ("layer 0 / success").
// ---------------------------------------------------------------------------
static void* DefaultAlloc(unsigned int size, const char*) { return std::malloc(size ? size : 1); }
static void  DefaultFree(void* p) { std::free(p); }
static void  DefaultFloorNoop(void*) {}

static const TerrainRender2Hooks kDefaultHooks = {
    DefaultAlloc, DefaultFree, DefaultFloorNoop, DefaultFloorNoop,
};
static const TerrainRender2Hooks* g_hooks = &kDefaultHooks;

void SetTerrainRender2Hooks(const TerrainRender2Hooks* hooks) {
    g_hooks = hooks ? hooks : &kDefaultHooks;
}
const TerrainRender2Hooks& GetTerrainRender2Hooks() { return *g_hooks; }

// Recovered .rdata constants.
static const float  kHalf       = 0.5f;        // flt_628B5C
static const float  kHeightMax  = 255.0f;      // flt_628B60
static const double kRayEps     = 1e-4;        // dbl_628C04
static const double kBigDist    = 1.0e10;      // dbl_628B84
static const double kRayDot     = -9.999999999999998e-08; // dbl_628B8C

// Byte access helpers (the original walks raw char* with byte offsets).
static inline float    F(const void* base, int off) { return *reinterpret_cast<const float*>(reinterpret_cast<const char*>(base) + off); }
static inline int      I(const void* base, int off) { return *reinterpret_cast<const int*>(reinterpret_cast<const char*>(base) + off); }
static inline unsigned char U8(const void* base, int off) { return *(reinterpret_cast<const unsigned char*>(base) + off); }
// Pointer-slot read. The 32-bit original stores a 32-bit pointer at these offsets;
// in this 64-bit reconstruction the record carries a native pointer in that slot
// (callers/tests write a real pointer), so read it pointer-width.
static inline const unsigned char* PU8(const void* base, int off) {
    return *reinterpret_cast<const unsigned char* const*>(reinterpret_cast<const char*>(base) + off);
}

// ===========================================================================
// 0x5C31F0  VIBE_Heightmap_FindNearestEntryToPoint
// ===========================================================================
int FindNearestEntryToPoint(int* a1, int a2, int a3, float a4, float* a5, float kEntryScale) {
    if (!a1) return -1;
    int* v23 = a1;
    float v32 = 1.0f;
    int v29 = -1;
    float v27 = a4 * a4;
    for (int i = 0; i < 8; ++i) {
        int* v24 = v23;
        for (int j = 0; j < 8; ++j) {
            int v33 = U8(v24, 318);
            if (U8(v24, 318)) {
                int v6 = a1[1] / v33;
                int v7 = v6 + 1;
                int v8 = (j == 7) ? (v7 - 4 / v33) : (v6 + 1);
                if (i == 7) v7 -= 4 / v33;
                int v9 = v7 * v8;            // entries-per-tile bound
                int v10 = 0;
                char* v11 = const_cast<char*>(reinterpret_cast<const char*>(PU8(v24, 62 * 4)));
                for (int k = v9; v10 < k; v11 += 80) {
                    if (*reinterpret_cast<signed char*>(v11 + 76) < 0) {
                        double dx = (double)a2 - F(v11, 16);
                        double dy = (double)a3 - F(v11, 20);
                        double v14 = dx * dx + dy * dy;
                        if (v14 < (double)v27) {
                            double scale = (kEntryScale != 0.0f) ? kEntryScale : 1.0f;
                            float v15 = (float)(std::sqrt((double)(float)v14) / a4 *
                                                (F(v11, 8) / scale));
                            if (v15 < v32) {
                                int v16 = v33 * (v10 / v8) + a1[1] * i;
                                if (v16 < *a1) {
                                    int v20 = a1[1] * j;
                                    int v17 = v33 * (v10 % v8);
                                    if (v17 + v20 < *a1) {
                                        v32 = v15;
                                        v29 = (v17 + v20) | (v16 << 16);
                                        if (a5) {
                                            a5[0] = F(v11, 0);
                                            a5[1] = F(v11, 4);
                                            a5[2] = F(v11, 8);
                                        }
                                    }
                                }
                            }
                        }
                    }
                    ++v10;
                }
            }
            v24 += 25;
        }
        v23 += 200;
    }
    return v29;
}

// ===========================================================================
// 0x5C34C4  VIBE_Heightmap_BlendSubdivideTerrain
// ===========================================================================
int BlendSubdivideTerrain(unsigned char* a1, int a2, int a3, int a4, int a5, int a6) {
    int result = a4;
    if (a6 == 1) return result;

    auto clampU8 = [](double v) -> unsigned char {
        if (!(v < 0.0) && (double)kHeightMax >= v) {
            double r = (v >= 0.0) ? v : 0.0;
            return (unsigned char)(int)r;
        }
        return (unsigned char)(int)kHeightMax;
    };
    int v28 = a2 - 1;            // torus mask

    // column extents
    int v8 = a4 + a5; if (v8 < 0) v8 = 0;
    int v25 = (a2 >= v8) ? ((a4 + a5 < 0) ? 0 : (a4 + a5)) : a2;
    int v10 = a3 + a5; if (v10 < 0) v10 = 0;
    int v26 = (a2 >= v10) ? ((a3 + a5 < 0) ? 0 : (a3 + a5)) : a2;

    int v23 = a4;
    float v32 = 1.0f / (double)a6;
    while (a6 + v23 <= 0) v23 += a6;
    int i = a3;
    for (; a6 + i <= 0; i += a6) ;

    // --- pass 1: interpolate along columns (vertical) ---
    for (int j = v23; j < v25; j += a6) {
        if (j >= 0) {
            for (int k = i; k < v26; k += a6) {
                float v41 = (float)a1[j + a2 * (k & v28)];
                float v42 = (float)a1[j + a2 * (v28 & (k + a6))];
                float v40 = (float)(a1[j + a2 * (v28 & ((k & v28) + a6))] -
                                    a1[j + a2 * (((k & v28) - a6) & v28)]);
                int v13 = 1;
                float v39 = (float)(a1[j + a2 * (v28 & ((v28 & (k + a6)) + a6))] -
                                    a1[j + a2 * (((v28 & (k + a6)) - a6) & v28)]);
                float v46 = 1.0f / (double)a6;
                if (a6 > 1) {
                    unsigned char* v14 = a1 + a2 * k + j + a2;
                    do {
                        if (k + v13 >= a2) break;
                        if (k + v13 >= 0) {
                            double cr = util::CatmullRomInterp(v41, v42, v40, v39, v46);
                            *v14 = clampU8(cr);
                        }
                        ++v13;
                        v14 += a2;
                        v46 = v46 + v32;
                    } while (v13 < a6);
                }
            }
        }
    }

    int v15 = (i < 0) ? 0 : i;
    result = v26;
    int v22 = v15;
    // --- pass 2: interpolate along rows (horizontal) ---
    if (v15 < v26) {
        int v21 = a2 * v15;
        do {
            for (int m = v23; m < v25; m += a6) {
                int v17 = v28 & (m + a6);
                float v44 = (float)a1[v21 + (m & v28)];
                float v43 = (float)a1[v17 + v21];
                float v37 = (float)(a1[v21 + (v28 & (a6 + (m & v28)))] -
                                    a1[v21 + (v28 & ((m & v28) - a6))]);
                float v38 = (float)(a1[v21 + (v28 & (v17 + a6))] -
                                    a1[v21 + (v28 & (v17 - a6))]);
                int v19 = 1;
                float v45 = 1.0f / (double)a6;
                if (a6 > 1) {
                    unsigned char* v20 = a1 + m + v21 + 1;
                    do {
                        if (m + v19 >= a2) break;
                        if (m + v19 >= 0) {
                            double cr = util::CatmullRomInterp(v44, v43, v37, v38, v45);
                            *v20 = clampU8(cr);
                        }
                        ++v20;
                        ++v19;
                        v45 = v45 + v32;
                    } while (v19 < a6);
                }
            }
            result = a2;
            v21 += a2;
            ++v22;
        } while (v22 < v26);
    }
    return result;
}

// ===========================================================================
// 0x5C4034  VIBE_Heightmap_ProjectPointToView
// ===========================================================================
double ProjectPointToView(int* a1, const float* a2, const float* a3) {
    // Helper: rotate a world vector v into the view frame (rows of a3 used as the
    // original does: a3[0,4,8] / a3[1,5,9] / a3[2,6,10]).
    auto rotX = [&](double x, double y, double z) { return x * a3[0] + y * a3[4] + z * a3[8]; };
    auto rotY = [&](double x, double y, double z) { return x * a3[1] + y * a3[5] + z * a3[9]; };
    auto rotZ = [&](double x, double y, double z) { return x * a3[2] + y * a3[6] + z * a3[10]; };

    // near corner = origin(grid+144) - point
    double n0 = F(a1, 144) - a2[0];
    double n1 = F(a1, 148) - a2[1];
    double n2 = F(a1, 152) - a2[2];
    double v45 = rotX(n0, n1, n2), v46 = rotY(n0, n1, n2), v47 = rotZ(n0, n1, n2);

    // face -Z
    double v29 = rotX(0.0, 0.0, -1.0), v30 = rotY(0.0, 0.0, -1.0), v31 = rotZ(0.0, 0.0, -1.0);
    double v32 = v45 * v29 + v46 * v30 + v47 * v31;
    // face +X
    double v33 = rotX(1.0, 0.0, 0.0), v34 = rotY(1.0, 0.0, 0.0), v35 = rotZ(1.0, 0.0, 0.0);
    double v36 = v45 * v33 + v46 * v34 + v47 * v35;

    // far corner = origin + (size-4)*axisX + (size-4)*axisY  - point
    int v60 = a1[0] - 4;
    double f0 = (double)v60 * F(a1, 160) + F(a1, 144);
    double f1 = (double)v60 * F(a1, 164) + F(a1, 148);
    double f2 = (double)v60 * F(a1, 168) + F(a1, 152);
    f0 = (double)v60 * F(a1, 176) + f0;
    f1 = (double)v60 * F(a1, 180) + f1;
    f2 = (double)v60 * F(a1, 184) + f2;
    f0 -= a2[0]; f1 -= a2[1]; f2 -= a2[2];
    v45 = rotX(f0, f1, f2); v46 = rotY(f0, f1, f2); v47 = rotZ(f0, f1, f2);

    // face -X
    double v37 = rotX(-1.0, 0.0, 0.0), v38 = rotY(-1.0, 0.0, 0.0), v39 = rotZ(-1.0, 0.0, 0.0);
    double v40 = v45 * v37 + v46 * v38 + v47 * v39;
    // face +Z
    double v41 = rotX(0.0, 0.0, 1.0), v42 = rotY(0.0, 0.0, 1.0), v43 = rotZ(0.0, 0.0, 1.0);
    double v44 = v45 * v41 + v46 * v42 + v47 * v43;

    // v19 = min over the active faces of the box extents (the nested min logic).
    float v17 = (v40 < (double)v44) ? (float)v40 : (float)v44;
    float v18;
    if (v36 >= (double)v17) {
        v18 = (v40 >= (double)v44) ? (float)v44 : (float)v40;
    } else {
        v18 = (float)v36;
    }
    float v19;
    if (v32 >= (double)v18) {
        float v25 = (v40 >= (double)v44) ? (float)v44 : (float)v40;
        if (v36 >= (double)v25) {
            v19 = (v40 >= (double)v44) ? (float)v44 : (float)v40;
        } else {
            v19 = (float)v36;
        }
    } else {
        v19 = (float)v32;
    }

    // Scan the 4 box plane-normals (flt_13DCE00 table, 4 rows of 4) against the
    // 4 escape distances (&v32 in the original; here {v32,v36,v40,v44}); pick the
    // smallest positive parameter > v19. The plane table is a runtime global; the
    // image initialises it to zero, so without it the loop finds no intersection
    // and the function returns |v19| — which we reproduce faithfully.
    float v26 = (float)kBigDist;
    // (plane table flt_13DCE00 is zero in the static image -> no update)
    (void)kRayDot;
    if ((double)v26 != 1.0e10) {
        return v26;
    }
    return (float)std::fabs(v19);
}

// ===========================================================================
// 0x5C67B8  VIBE_Heightmap_RaycastFromCursor
// ===========================================================================
int RaycastFromCursor(int* a1, const CursorRay* ray, int* outRow, int* outCol) {
    if (!a1 || !ray) return 0;

    // Original: v35/v37/v36 = (rayOrigin - grid.pos) / grid.step  (cell space).
    // ray->origin is already the world ray source; convert to cell space.
    float v35 = (ray->origin[0] - F(a1, 0))  / F(a1, 16);
    float v37 = (ray->origin[1] - F(a1, 4))  / F(a1, 20);
    float v36 = (ray->origin[2] - F(a1, 8))  / F(a1, 24);

    // Direction in cell space (the original derives it from the view matrix;
    // here it comes straight from ray->dir, divided by the per-axis step).
    float v23 = ray->dir[0] / F(a1, 16);
    float v24 = ray->dir[1] / F(a1, 20);
    float v25 = ray->dir[2] / F(a1, 24);

    int cellCount = I(a1, 32);
    const unsigned char* heights = PU8(a1, 40);

    if (std::fabs((double)v23) >= kRayEps || std::fabs((double)v25) >= kRayEps) {
        double v20 = std::fabs((double)v23);
        double v21 = std::fabs((double)v25);
        double v14 = (v20 <= v21) ? v21 : v20;
        double v15 = 1.0 / v14;
        v23 = v23 * v15;
        v24 = v24 * v15;
        v25 = v15 * v25;
        while (v35 >= 0.0f) {
            float v34 = (float)cellCount;
            if (v35 >= (double)v34 || v36 < 0.0f || v36 >= (double)v34) break;
            int v32 = (int)util::ConvertX((double)v36);   // col
            int v30 = (int)util::ConvertX((double)v35);   // row
            int v38 = heights[v30 + cellCount * v32];
            if ((double)(short)v38 >= (double)v37) {
                if (outRow) *outRow = v30;
                if (outCol) *outCol = v32;
                return 1;
            }
            v37 = v37 + v24;
            v36 = v36 + v25;
            v35 = v35 + v23;
        }
        return 0;
    }
    // near-vertical ray: sample the single cell at (v35,v36).
    int v29 = (int)util::ConvertX((double)v35);   // row
    int v31 = (int)util::ConvertX((double)v36);   // col
    if (v24 > 0.0f) {
        int v38 = heights[cellCount * v31 + v29];
        if ((double)(short)v38 < (double)v37) return 0;
    }
    if (outRow) *outRow = v29;
    if (outCol) *outCol = v31;
    return 1;
}

// ===========================================================================
// 0x5C2DDC  VIBE_Floor_PickTileAtPoint
// ===========================================================================
int PickTileAtPoint(int* a1, const float* a2, int a3, int* outCol, int* outRow, float* a5) {
    if (!a1) return 0;

    float v53 = a2[2] - F(a1, 152);
    float v42 = a2[0] - F(a1, 144);
    if ((I(a1, 160) & 0x7FFFFFFF) != 0) v42 = v42 / F(a1, 160);
    if ((I(a1, 184) & 0x7FFFFFFF) != 0) v53 = v53 / F(a1, 184);

    float v46 = v42 + kHalf;
    float v47 = kHalf + v53;
    int v40 = (int)util::ConvertX((double)v46);   // col
    if (outCol) *outCol = v40;
    int rowv = (int)util::ConvertX((double)v47);
    if (outRow) *outRow = rowv;                   // row (a4 in original)

    if (!(v40 >= 0 && v40 < *a1)) return 0;
    int v8 = rowv;
    if (!(rowv >= 0 && v8 < *a1)) return 0;
    if (!a5) return 1;

    unsigned int v9 = 1;
    int v10Off = 36;                       // offset of the active lod type-grid slot
    if (a3) {
        int v12 = I(a1, 4);
        // tile attr byte: a1 + 800*(row/tileSpan) + 100*(col/tileSpan) + 318
        char* tileBase = reinterpret_cast<char*>(a1) + 800 * (v8 / v12);
        int v13 = *reinterpret_cast<unsigned char*>(tileBase + 100 * (v40 / v12) + 318);
        if (v13 == 2 || v13 == 4) {
            v9 = (unsigned)v13;
            v10Off = 4 * (v13 >> 1) + 36;
        }
    }
    const unsigned char* v10 = PU8(a1, v10Off);  // lod type grid (null when absent)

    float v15 = v42 + kHalf;
    int v16 = ~((int)v9 - 1);
    int colTrunc = (int)util::ConvertX((double)v15);
    double v17 = v42 - (double)(v16 & colTrunc);
    double v18 = 1.0 / (double)v9;
    unsigned int v20 = (unsigned)(v16 & colTrunc);                       // col base (snapped)
    int v21 = (int)util::ConvertX((double)(kHalf + v53)) & v16;          // row base (snapped)
    float v44 = (float)(v17 * v18);
    float v43 = (float)(v18 * (v53 - (double)v21));

    int size = *a1;
    int mask = I(a1, 12);
    const unsigned char* H = PU8(a1, 16);
    float v45, v51, v52;

    // hole test: lodSrc nonzero and the lod cell's type byte negative
    bool branchA = false;
    if (v10) {
        int li = (int)(size * (((unsigned)(v21) / v9)) / v9 + v20 / v9);
        if (*reinterpret_cast<const signed char*>(v10 + li) < 0) branchA = true;
    }

    if (branchA) {
        unsigned int v22 = v20 + v9;            // col + step
        unsigned int v23 = (unsigned)v21 + v9;  // row + step
        if (v44 + v43 <= 1.0f) {
            int v41 = v21 * size;                                 // row*size
            float v54 = (float)H[(size_t)v41 + (v22 & mask)];     // corner (col+step)
            v45        = (float)H[(size_t)v41 + v20];             // base   (col,row)
            float v27  = (float)H[(size_t)(size * (mask & (int)v23)) + v20]; // (row+step,col)
            v51 = v54 - v45;
            v52 = v27 - v45;
        } else {
            int v25 = size * (mask & (int)v23);                   // (row+step)*size
            v44 = 1.0f - v44;
            v43 = 1.0f - v43;
            float v54 = (float)H[(size_t)v25 + v20];
            v45        = (float)H[(size_t)v25 + (v22 & mask)];
            float v27  = (float)H[(v22 & mask) + (size_t)(size * v21)];
            v51 = v54 - v45;
            v52 = v27 - v45;
        }
    } else {
        unsigned int v49 = (unsigned)v21 + v9;
        float v48 = 1.0f - v43;
        unsigned int v50 = v20 + v9;
        if (v44 + v48 <= 1.0f) {
            int v38 = size * (mask & (int)v49);
            v45 = (float)H[(size_t)v38 + v20];
            v51 = (float)H[(mask & (int)v50) + (size_t)v38] - v45;
            v52 = (float)H[(size_t)(size * (int)v21) + v20] - v45;
            v43 = 1.0f - v43;
        } else {
            int v35 = size * (int)v21;
            int v36 = mask & (int)v50;
            v44 = 1.0f - v44;
            v45 = (float)H[(size_t)v35 + v36];
            v51 = (float)H[(size_t)v35 + v20] - v45;
            v52 = (float)H[(size_t)v36 + (size_t)(size * (mask & (int)v49))] - v45;
        }
    }
    *a5 = (v44 * v51 + v43 * v52 + v45) * F(a1, 196) + F(a1, 148);
    return 1;
}

// ===========================================================================
// 0x5BCB38  VIBE_Floor_AllocTileBuffers
// ===========================================================================
int AllocTileBuffers(int* a1, unsigned int a2, unsigned char a3) {
    const TerrainRender2Hooks& H = *g_hooks;
    char* base = reinterpret_cast<char*>(a1);

    *reinterpret_cast<int*>(base + 88) = 0;
    *reinterpret_cast<unsigned char*>(base + 97) = 0;
    *reinterpret_cast<int*>(base + 32) = 0;
    *reinterpret_cast<unsigned char*>(base + 94) = 0xFF;
    void* v5 = *reinterpret_cast<void**>(base + 24);
    unsigned int v6 = a2 >> a3;     // LOD subdivision count
    *reinterpret_cast<int*>(base + 16) = I(a1, 32);

    if (!v5)
        *reinterpret_cast<void**>(base + 24) = H.allocDebug(80 * (v6 + 2) * (v6 + 2), "d3_fl:TilePoints");
    if (!*reinterpret_cast<void**>(base + 40))
        *reinterpret_cast<void**>(base + 40) = H.allocDebug(40 * (v6 + 1) * (2 * v6 + 2), "d3_fl:TilePolys");
    if (!*reinterpret_cast<void**>(base + 48))
        *reinterpret_cast<void**>(base + 48) = H.allocDebug(48 * (v6 + 2), "d3_fl:TileSplitUpdate");
    if (!*reinterpret_cast<void**>(base + 60)) {
        *reinterpret_cast<int*>(base + 68) = 8 * v6 + 16;
        *reinterpret_cast<void**>(base + 60) =
            H.allocDebug(24 * ((2 * v6 + 2) * (v6 + 1) + (unsigned)I(a1, 68)), "d3_fl:TileBPBuffer");
    }
    if (!*reinterpret_cast<void**>(base + 52)) {
        *reinterpret_cast<unsigned char*>(base + 96) = 0;
        *reinterpret_cast<void**>(base + 28) = nullptr;
        *reinterpret_cast<void**>(base + 44) = nullptr;
        *reinterpret_cast<void**>(base + 36) = nullptr;
        *reinterpret_cast<int*>(base + 20) = 0;
    }

    // init point records (stride 80): +72 = 0 (int), +76 = 0 (byte), +64 = 0xFFFFFF marker
    unsigned int v7 = (v6 + 2) * (v6 + 2);
    char* pts = reinterpret_cast<char*>(*reinterpret_cast<void**>(base + 24));
    for (unsigned int i = 0; i < v7; ++i) {
        char* rec = pts + 80 * i;
        *reinterpret_cast<int*>(rec + 72) = 0;
        *reinterpret_cast<unsigned char*>(rec + 76) = 0;
        *reinterpret_cast<int*>(rec + 64) = 0xFFFFFF;
    }

    // init poly records (stride 40)
    unsigned int v11 = (2 * v6 + 2) * (v6 + 1);
    char* poly = reinterpret_cast<char*>(*reinterpret_cast<void**>(base + 40));
    for (unsigned int i = 0; i < v11; ++i) {
        char* rec = poly + 40 * i;
        *reinterpret_cast<int*>(rec + 24) = 1098907648;  // 16.0f
        *reinterpret_cast<int*>(rec + 32) = 0;
        *reinterpret_cast<float*>(rec + 28) = 0.0f;
        *reinterpret_cast<unsigned char*>(rec + 36) = 0;
        *reinterpret_cast<int*>(rec + 0) = 0;
        *reinterpret_cast<int*>(rec + 4) = 0;
        *reinterpret_cast<int*>(rec + 8) = 0;
        *reinterpret_cast<int*>(rec + 16) = 0;
        *reinterpret_cast<int*>(rec + 20) = 0;
    }
    return (int)v11;
}

// ===========================================================================
// 0x5BCE10  VIBE_Floor_AllocInflateBuffers
// ===========================================================================
int AllocInflateBuffers(int* a1) {
    const TerrainRender2Hooks& H = *g_hooks;
    char* base = reinterpret_cast<char*>(a1);
    int size = *a1;

    if (!I(a1, 28))
        *reinterpret_cast<void**>(base + 28) = H.allocDebug(size * size + 1, "d3fl:Inflate(f->light)");
    for (int i = 0; i < 3; ++i) {
        if (!I(a1, 4 * i + 36)) {
            int s = (size >> i);
            *reinterpret_cast<void**>(base + 4 * i + 36) = H.allocDebug(s * s + 1, "d3fl:Inflate(f->divide)");
        }
    }
    for (int v4 = 0; v4 < 8; ++v4) {
        char* tile = base + 800 * v4 + 224;
        char* rowEnd = base + 1024 + 800 * v4;
        do {
            AllocTileBuffers(reinterpret_cast<int*>(tile), (unsigned)I(a1, 4),
                             (unsigned char)(U8(a1, 7281) & 0xF));
            tile += 100;
        } while (tile != rowEnd);
    }
    H.computeSlopeFlags(a1);
    *reinterpret_cast<unsigned char*>(base + 7280) |= 1u;
    return 0;
}

// ===========================================================================
// 0x5BCED8  VIBE_Floor_AllocLightBuffers
// ===========================================================================
int AllocLightBuffers(int* a1) {
    const TerrainRender2Hooks& H = *g_hooks;
    char* base = reinterpret_cast<char*>(a1);
    int size = *a1;

    *reinterpret_cast<unsigned char*>(base + 7276) = 0xFF;
    *reinterpret_cast<void**>(base + 36) = H.allocDebug(size * size, "d3_fl:Divide0");
    *reinterpret_cast<void**>(base + 40) = H.allocDebug((unsigned)(size * size) >> 2, "d3_fl:Divide1");
    *reinterpret_cast<void**>(base + 44) = H.allocDebug((unsigned)(size * size) >> 4, "d3_fl:Divide2");
    *reinterpret_cast<void**>(base + 28) = H.allocDebug(size * size, "d3_fl:Light");
    if (!I(a1, 32))
        *reinterpret_cast<void**>(base + 32) = H.allocDebug(4 * size * size, "d3_fl:LightOffset");

    for (int v13 = 0; v13 < 8; ++v13) {
        char* tile = base + 800 * v13 + 224;
        char* rowEnd = base + 1024 + 800 * v13;
        do {
            std::memset(tile, 0, 100);  // zero the 100-byte tile header
            AllocTileBuffers(reinterpret_cast<int*>(tile), (unsigned)I(a1, 4),
                             (unsigned char)(U8(a1, 7281) & 0xF));
            tile += 100;
        } while (tile != rowEnd);
    }
    H.computeSlopeFlags(a1);
    H.buildTilePolys(a1);
    *reinterpret_cast<unsigned char*>(base + 7280) |= 1u;
    return 0;
}

} // namespace guild::render
