#include "render/mesh_normals.h"

#include "tests/framework/test.h"

#include <cmath>
#include <vector>

using namespace guild::render;

namespace {

bool Near(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }

// A single flat quad in the XY plane (z=0), two triangles, CCW so the face normal
// points +Z (TriangleNormal = (v1-v0)x(v2-v0) with the engine's component order).
//   2 --- 3
//   | \   |
//   |  \  |
//   0 --- 1
std::vector<SourceMeshVertex> QuadVerts() {
    return {
        {{0.0f, 0.0f, 0.0f}, {0, 0, 0}},  // 0
        {{1.0f, 0.0f, 0.0f}, {0, 0, 0}},  // 1
        {{0.0f, 1.0f, 0.0f}, {0, 0, 0}},  // 2
        {{1.0f, 1.0f, 0.0f}, {0, 0, 0}},  // 3
    };
}

}  // namespace

// (a) Flat quad -> every vertex normal is the shared face normal (+Z). Pins both
//     the face-normal sign convention and the per-vertex averaging.
TEST(MeshNormals, FlatQuadAllPlusZ) {
    auto verts = QuadVerts();
    std::vector<NormalTriangle> tris = {{{0, 1, 2}}, {{1, 3, 2}}};
    std::vector<std::array<float, 3>> faces;
    GenerateVertexNormals(verts, tris, &faces);

    // Both face normals are +Z (CCW winding in XY).
    CHECK_EQ(faces.size(), static_cast<size_t>(2));
    for (auto& f : faces) {
        CHECK(Near(f[0], 0.0f));
        CHECK(Near(f[1], 0.0f));
        CHECK(Near(f[2], 1.0f));
    }
    for (auto& v : verts) {
        CHECK(Near(v.normal[0], 0.0f));
        CHECK(Near(v.normal[1], 0.0f));
        CHECK(Near(v.normal[2], 1.0f));
    }
}

// (b) Reversed winding flips the face normal to -Z.
TEST(MeshNormals, ReversedWindingFlips) {
    auto verts = QuadVerts();
    std::vector<NormalTriangle> tris = {{{0, 2, 1}}};  // CW
    GenerateVertexNormals(verts, tris, nullptr);
    // Vertices 0,1,2 touch the triangle -> -Z; vertex 3 is unreferenced -> (0,0,0).
    for (int i = 0; i < 3; ++i)
        CHECK(Near(verts[i].normal[2], -1.0f));
    CHECK(Near(verts[3].normal[0], 0.0f));
    CHECK(Near(verts[3].normal[1], 0.0f));
    CHECK(Near(verts[3].normal[2], 0.0f));
}

// (c) Unit cube (triangulated): the engine averages UNIT face normals over EVERY
//     adjacent TRIANGLE (not face), so corners touched by a shared diagonal edge
//     get 4 contributions, not 3. Corners 0 and 7 touch exactly one triangle per
//     face -> clean ±1/sqrt(3) diagonals; all corners stay unit length with the
//     correct outward sign. These are the engine's exact 1:1 values
//     (VIBE_Mesh_ComputeVertexNormals @0x5D1A6C), not an idealized symmetric guess.
TEST(MeshNormals, UnitCubeCornerDiagonals) {
    // 8 corners of [0,1]^3, indices = (x) + 2*(y) + 4*(z).
    std::vector<SourceMeshVertex> v = {
        {{0, 0, 0}, {0, 0, 0}},  // 0
        {{1, 0, 0}, {0, 0, 0}},  // 1
        {{0, 1, 0}, {0, 0, 0}},  // 2
        {{1, 1, 0}, {0, 0, 0}},  // 3
        {{0, 0, 1}, {0, 0, 0}},  // 4
        {{1, 0, 1}, {0, 0, 0}},  // 5
        {{0, 1, 1}, {0, 0, 0}},  // 6
        {{1, 1, 1}, {0, 0, 0}},  // 7
    };
    // 12 triangles, all wound so face normals point OUTWARD.
    std::vector<NormalTriangle> tris = {
        // -Z (z=0): outward = -Z. CW seen from +Z = CCW seen from -Z.
        {{0, 2, 1}}, {{1, 2, 3}},
        // +Z (z=1): outward = +Z.
        {{4, 5, 6}}, {{5, 7, 6}},
        // -Y (y=0): outward = -Y.
        {{0, 1, 4}}, {{1, 5, 4}},
        // +Y (y=1): outward = +Y.
        {{2, 6, 3}}, {{3, 6, 7}},
        // -X (x=0): outward = -X.
        {{0, 4, 2}}, {{2, 4, 6}},
        // +X (x=1): outward = +X.
        {{1, 3, 5}}, {{3, 7, 5}},
    };
    GenerateVertexNormals(v, tris, nullptr);

    const float inv3 = 1.0f / std::sqrt(3.0f);
    // Corners 0=(0,0,0) and 7=(1,1,1) touch exactly one triangle on each of their
    // three faces -> clean corner diagonal.
    CHECK(Near(v[0].normal[0], -inv3, 2e-4f));
    CHECK(Near(v[0].normal[1], -inv3, 2e-4f));
    CHECK(Near(v[0].normal[2], -inv3, 2e-4f));
    CHECK(Near(v[7].normal[0],  inv3, 2e-4f));
    CHECK(Near(v[7].normal[1],  inv3, 2e-4f));
    CHECK(Near(v[7].normal[2],  inv3, 2e-4f));

    // Every corner: unit length + correct outward sign (sign(coord - 0.5)).
    for (int i = 0; i < 8; ++i) {
        float len = std::sqrt(v[i].normal[0] * v[i].normal[0] +
                              v[i].normal[1] * v[i].normal[1] +
                              v[i].normal[2] * v[i].normal[2]);
        CHECK(Near(len, 1.0f, 2e-4f));
        CHECK((v[i].normal[0] > 0.0f) == (v[i].pos[0] > 0.5f));
        CHECK((v[i].normal[1] > 0.0f) == (v[i].pos[1] > 0.5f));
        CHECK((v[i].normal[2] > 0.0f) == (v[i].pos[2] > 0.5f));
    }
}

// (d) Degenerate triangle (collinear) -> zero face normal -> vertex collapses to 0
//     (VIBE_Math_VectorNormalize zero-length behaviour @0x5cb148).
TEST(MeshNormals, DegenerateCollapsesToZero) {
    std::vector<SourceMeshVertex> v = {
        {{0, 0, 0}, {9, 9, 9}},
        {{1, 0, 0}, {9, 9, 9}},
        {{2, 0, 0}, {9, 9, 9}},  // collinear with 0,1
    };
    std::vector<NormalTriangle> tris = {{{0, 1, 2}}};
    GenerateVertexNormals(v, tris, nullptr);
    for (auto& vv : v) {
        CHECK(Near(vv.normal[0], 0.0f));
        CHECK(Near(vv.normal[1], 0.0f));
        CHECK(Near(vv.normal[2], 0.0f));
    }
}

// (e) No topology -> all normals zeroed; null/empty inputs are safe no-ops.
TEST(MeshNormals, EmptyAndNullGuards) {
    auto verts = QuadVerts();
    GenerateVertexNormals(verts.data(), 4, nullptr, 0, nullptr);  // no tris
    for (auto& v : verts) {
        CHECK(Near(v.normal[0], 0.0f));
        CHECK(Near(v.normal[1], 0.0f));
        CHECK(Near(v.normal[2], 0.0f));
    }
    // Null/zero guards must not crash.
    GenerateVertexNormals(nullptr, 4, nullptr, 0, nullptr);
    GenerateVertexNormals(verts.data(), 0, nullptr, 0, nullptr);
    CHECK(true);
}

// (f) Instance binding: kBindToVertexBase points +0x48 at the source vertex BASE
//     (readers deref +12); the resolved normal[] is the +12 normal.
TEST(MeshNormals, BindToVertexBase) {
    std::vector<SourceMeshVertex> v = {
        {{1, 2, 3}, {0.0f, 0.0f, 1.0f}},
        {{4, 5, 6}, {0.0f, 1.0f, 0.0f}},
    };
    std::vector<InstanceVertexNormalBinding> b;
    BindInstanceNormals(v, MeshNormalSource::kBindToVertexBase, b);
    CHECK_EQ(b.size(), static_cast<size_t>(2));
    // The +0x48 pointer is the vertex base; reading it at +12 (the .normal field)
    // recovers the normal — verify by pointer arithmetic on the source record.
    for (size_t i = 0; i < b.size(); ++i) {
        CHECK_EQ(b[i].sourcePtr, static_cast<const void*>(&v[i]));
        const float* asNormal =
            reinterpret_cast<const float*>(static_cast<const char*>(b[i].sourcePtr) + 12);
        CHECK(Near(asNormal[0], v[i].normal[0]));
        CHECK(Near(asNormal[1], v[i].normal[1]));
        CHECK(Near(asNormal[2], v[i].normal[2]));
        CHECK(Near(b[i].normal[0], v[i].normal[0]));
        CHECK(Near(b[i].normal[1], v[i].normal[1]));
        CHECK(Near(b[i].normal[2], v[i].normal[2]));
    }
}

// (g) Instance binding: kBindToVertexNormal points +0x48 directly at the normal
//     (sun cache reads it at +0); resolved normal[] still the +12 normal.
TEST(MeshNormals, BindToVertexNormal) {
    std::vector<SourceMeshVertex> v = {
        {{1, 2, 3}, {0.6f, 0.0f, 0.8f}},
    };
    std::vector<InstanceVertexNormalBinding> b;
    BindInstanceNormals(v, MeshNormalSource::kBindToVertexNormal, b);
    CHECK_EQ(b[0].sourcePtr, static_cast<const void*>(v[0].normal));
    // Sun arm reads the pointer at +0:
    const float* atZero = reinterpret_cast<const float*>(b[0].sourcePtr);
    CHECK(Near(atZero[0], 0.6f));
    CHECK(Near(atZero[1], 0.0f));
    CHECK(Near(atZero[2], 0.8f));
    CHECK(Near(b[0].normal[0], 0.6f));
    CHECK(Near(b[0].normal[2], 0.8f));
}

// =============================================================================
// WAVE-10 HARDENING — degenerate / edge / OOB coverage (ASAN+UBSAN). These pin
// the memory-safety guards added to GenerateVertexNormals / BindInstanceNormals.
// =============================================================================

// (w1) Single vertex, zero triangles -> the no-topology branch zeroes the normal;
//      no OOB on a 1-element span.
TEST(MeshNormals, HardenSingleVertexZeroTris) {
    std::vector<SourceMeshVertex> v = {{{1, 2, 3}, {9, 9, 9}}};
    GenerateVertexNormals(v, {}, nullptr);
    CHECK(Near(v[0].normal[0], 0.0f));
    CHECK(Near(v[0].normal[1], 0.0f));
    CHECK(Near(v[0].normal[2], 0.0f));
    // Also exercise the raw overload with count==1, triangleCount==0.
    SourceMeshVertex one = {{4, 5, 6}, {7, 7, 7}};
    GenerateVertexNormals(&one, 1, nullptr, 0, nullptr);
    CHECK(Near(one.normal[0], 0.0f));
}

// (w2) Zero triangles but a faceNormalsOut request -> no write, no crash.
TEST(MeshNormals, HardenZeroTrisWithFaceOut) {
    auto verts = QuadVerts();
    std::vector<std::array<float, 3>> faces;
    GenerateVertexNormals(verts, {}, &faces);   // tc == 0 -> faces stays empty
    CHECK_EQ(faces.size(), static_cast<size_t>(0));
    for (auto& v : verts) {
        CHECK(Near(v.normal[0], 0.0f));
        CHECK(Near(v.normal[2], 0.0f));
    }
}

// (w3) Out-of-range triangle index — the MEMORY-SAFETY GUARD must skip the bad
//      triangle (no OOB read of verts[]). The skipped triangle leaves a zero face
//      normal, so the touched vertices collapse to (0,0,0). A second, in-range
//      triangle on the same mesh still produces its correct face normal — proving
//      the guard only drops the OOB triangle and keeps the valid path identical.
TEST(MeshNormals, HardenOutOfRangeTriangleIndexGuard) {
    auto verts = QuadVerts();                       // 4 vertices [0..3]
    std::vector<NormalTriangle> tris = {
        {{0, 1, 99}},   // index 99 is OOB -> skipped by the guard (no OOB read)
        {{0, 1, 2}},    // valid -> +Z face normal
    };
    std::vector<std::array<float, 3>> faces;
    GenerateVertexNormals(verts, tris, &faces);
    // Bad triangle's face normal stays zero (skipped).
    CHECK(Near(faces[0][0], 0.0f));
    CHECK(Near(faces[0][1], 0.0f));
    CHECK(Near(faces[0][2], 0.0f));
    // Valid triangle keeps its +Z normal.
    CHECK(Near(faces[1][2], 1.0f));
    // Vertex 2 only touches the valid +Z triangle -> +Z.
    CHECK(Near(verts[2].normal[2], 1.0f));
    // Vertex 3 is untouched by either triangle -> zero.
    CHECK(Near(verts[3].normal[2], 0.0f));
}

// (w4) Every triangle OOB -> all face normals zero, all vertex normals collapse,
//      and crucially no out-of-bounds access (raw pointer overload, tight span).
TEST(MeshNormals, HardenAllTrianglesOutOfRange) {
    SourceMeshVertex v[2] = {{{0, 0, 0}, {5, 5, 5}}, {{1, 0, 0}, {5, 5, 5}}};
    NormalTriangle tris[1] = {{{0, 1, 7}}};         // 7 >= vertexCount(2)
    float faceOut[3] = {1, 2, 3};
    GenerateVertexNormals(v, 2, tris, 1, faceOut);
    CHECK(Near(faceOut[0], 0.0f));
    CHECK(Near(faceOut[1], 0.0f));
    CHECK(Near(faceOut[2], 0.0f));
    for (auto& vv : v) {
        CHECK(Near(vv.normal[0], 0.0f));
        CHECK(Near(vv.normal[2], 0.0f));
    }
}

// (w5) Degenerate (collinear) single triangle on a 3-vertex mesh -> zero face
//      normal -> zero vertex normals; faceNormalsOut request exercised too.
TEST(MeshNormals, HardenDegenerateWithFaceOut) {
    std::vector<SourceMeshVertex> v = {
        {{0, 0, 0}, {1, 1, 1}}, {{2, 0, 0}, {1, 1, 1}}, {{5, 0, 0}, {1, 1, 1}},
    };
    std::vector<NormalTriangle> tris = {{{0, 1, 2}}};
    std::vector<std::array<float, 3>> faces;
    GenerateVertexNormals(v, tris, &faces);
    CHECK_EQ(faces.size(), static_cast<size_t>(1));
    CHECK(Near(faces[0][0], 0.0f));
    CHECK(Near(faces[0][2], 0.0f));
    for (auto& vv : v) CHECK(Near(vv.normal[2], 0.0f));
}

// (w6) BindInstanceNormals bounds: count must not exceed the source span. The
//      vector overload derives count from verts.size() (always in-bounds); the raw
//      overload with count == size touches exactly [0,size) and no further.
TEST(MeshNormals, HardenBindInstanceBounds) {
    std::vector<SourceMeshVertex> v = {
        {{0, 0, 0}, {1, 0, 0}}, {{0, 0, 0}, {0, 1, 0}}, {{0, 0, 0}, {0, 0, 1}},
    };
    // Raw overload, count == size: writes exactly 3 bindings, no OOB.
    std::vector<InstanceVertexNormalBinding> out(v.size(), InstanceVertexNormalBinding{});
    BindInstanceNormals(v.data(), static_cast<int>(v.size()),
                        MeshNormalSource::kBindToVertexBase, out.data());
    CHECK_EQ(out.size(), static_cast<size_t>(3));
    CHECK(Near(out[2].normal[2], 1.0f));

    // Null/zero guards: must not write or crash.
    BindInstanceNormals(nullptr, 3, MeshNormalSource::kBindToVertexBase, out.data());
    BindInstanceNormals(v.data(), 0, MeshNormalSource::kBindToVertexBase, out.data());
    BindInstanceNormals(v.data(), 3, MeshNormalSource::kBindToVertexBase, nullptr);

    // Empty vector overload: zero-length result, no OOB on .data()/.front().
    std::vector<SourceMeshVertex> empty;
    std::vector<InstanceVertexNormalBinding> eb;
    BindInstanceNormals(empty, MeshNormalSource::kBindToVertexNormal, eb);
    CHECK_EQ(eb.size(), static_cast<size_t>(0));

    // FlattenInstanceNormals null/zero guards.
    float scratch[3] = {1, 1, 1};
    FlattenInstanceNormals(nullptr, 3, scratch);
    FlattenInstanceNormals(out.data(), 0, scratch);
    FlattenInstanceNormals(out.data(), 3, nullptr);
    CHECK(Near(scratch[0], 1.0f));   // untouched by the guarded no-ops
}

// (h) FlattenInstanceNormals -> 3 floats/vertex for the wave-7 consumers.
TEST(MeshNormals, FlattenForWave7) {
    std::vector<SourceMeshVertex> v = {
        {{0, 0, 0}, {1, 0, 0}},
        {{0, 0, 0}, {0, 1, 0}},
        {{0, 0, 0}, {0, 0, 1}},
    };
    std::vector<InstanceVertexNormalBinding> b;
    BindInstanceNormals(v, MeshNormalSource::kBindToVertexBase, b);
    float flat[9] = {0};
    FlattenInstanceNormals(b.data(), 3, flat);
    CHECK(Near(flat[0], 1.0f));
    CHECK(Near(flat[4], 1.0f));
    CHECK(Near(flat[8], 1.0f));
}
