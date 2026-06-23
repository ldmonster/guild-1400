// mesh_recon3_geometry_test.cpp — golden tests for the recon3 mesh geometry leaves.
//   VIBE_Mesh_ComputeVertexNormals   @0x5d1a6c
//   VIBE_Mesh_ComputeBoundingExtents @0x5d1b54
#include "tests/framework/test.h"
#include "render/mesh_recon3_geometry.h"

#include <cmath>
#include <vector>

using namespace guild::render::mesh_recon3;

static bool approx(float a, float b, float eps = 1e-5f) {
    return std::fabs(a - b) <= eps;
}

// --- ComputeVertexNormals: single triangle in the XY plane ---------------
TEST(MeshRecon3, VertexNormals_SingleTriangle_XY) {
    std::vector<MeshVertex> verts(3);
    verts[0].pos[0] = 0; verts[0].pos[1] = 0; verts[0].pos[2] = 0;
    verts[1].pos[0] = 1; verts[1].pos[1] = 0; verts[1].pos[2] = 0;
    verts[2].pos[0] = 0; verts[2].pos[1] = 1; verts[2].pos[2] = 0;

    std::vector<MeshFace> faces(1);
    faces[0].idx[0] = 0; faces[0].idx[1] = 1; faces[0].idx[2] = 2;

    MeshView m{verts.data(), 3, faces.data(), 1};
    ComputeVertexNormals(m);

    // face normal = normalize((v1-v0)x(v2-v0)) = (0,0,1)
    CHECK(approx(faces[0].face_normal[0], 0.0f));
    CHECK(approx(faces[0].face_normal[1], 0.0f));
    CHECK(approx(faces[0].face_normal[2], 1.0f));

    for (int i = 0; i < 3; ++i) {
        CHECK(approx(verts[i].normal[0], 0.0f));
        CHECK(approx(verts[i].normal[1], 0.0f));
        CHECK(approx(verts[i].normal[2], 1.0f));
    }
}

// --- ComputeVertexNormals: two coplanar triangles share an edge ----------
// Quad (0,0)-(1,0)-(1,1)-(0,1) split into tris {0,1,2} and {0,2,3}.
// All face normals = (0,0,1); shared verts sum two (0,0,1) -> normalized (0,0,1).
TEST(MeshRecon3, VertexNormals_Quad_TwoTris) {
    std::vector<MeshVertex> verts(4);
    float pos[4][3] = {{0,0,0},{1,0,0},{1,1,0},{0,1,0}};
    for (int i = 0; i < 4; ++i)
        for (int k = 0; k < 3; ++k) verts[i].pos[k] = pos[i][k];

    std::vector<MeshFace> faces(2);
    faces[0].idx[0] = 0; faces[0].idx[1] = 1; faces[0].idx[2] = 2;
    faces[1].idx[0] = 0; faces[1].idx[1] = 2; faces[1].idx[2] = 3;

    MeshView m{verts.data(), 4, faces.data(), 2};
    ComputeVertexNormals(m);

    for (int i = 0; i < 4; ++i) {
        CHECK(approx(verts[i].normal[0], 0.0f));
        CHECK(approx(verts[i].normal[1], 0.0f));
        CHECK(approx(verts[i].normal[2], 1.0f));
    }
}

// --- ComputeVertexNormals: triangle in YZ plane, normal along +X ---------
TEST(MeshRecon3, VertexNormals_Triangle_YZ) {
    std::vector<MeshVertex> verts(3);
    // a=(0,0,0) b=(0,1,0) c=(0,0,1): e1=(0,1,0) e2=(0,0,1)
    // n = (e2z*e1y - e2y*e1z, e2x*e1z - e2z*e1x, e2y*e1x - e2x*e1y)
    //   = (1*1 - 0, 0 - 1*0, 0 - 0) = (1,0,0)
    verts[0].pos[0]=0; verts[0].pos[1]=0; verts[0].pos[2]=0;
    verts[1].pos[0]=0; verts[1].pos[1]=1; verts[1].pos[2]=0;
    verts[2].pos[0]=0; verts[2].pos[1]=0; verts[2].pos[2]=1;
    std::vector<MeshFace> faces(1);
    faces[0].idx[0]=0; faces[0].idx[1]=1; faces[0].idx[2]=2;
    MeshView m{verts.data(), 3, faces.data(), 1};
    ComputeVertexNormals(m);
    CHECK(approx(faces[0].face_normal[0], 1.0f));
    CHECK(approx(faces[0].face_normal[1], 0.0f));
    CHECK(approx(faces[0].face_normal[2], 0.0f));
}

// --- ComputeBoundingExtents: non-symmetric box at origin -----------------
TEST(MeshRecon3, BoundingExtents_BoxFromOrigin) {
    // verts (0,0,0) and (2,4,6); need 8 extra slots for the corners.
    std::vector<MeshVertex> verts(2 + 8);
    verts[0].pos[0]=0; verts[0].pos[1]=0; verts[0].pos[2]=0;
    verts[1].pos[0]=2; verts[1].pos[1]=4; verts[1].pos[2]=6;

    BoundsObject o{};
    o.verts = verts.data();
    o.vert_count = 2;
    ComputeBoundingExtents(o);

    // diagonal length sqrt(2^2+4^2+6^2) = sqrt(56)
    CHECK(approx(o.radius, std::sqrt(56.0f), 1e-3f));
    // center = ((min+max)/2) = (1,2,3)
    CHECK(approx(o.center[0], 1.0f));
    CHECK(approx(o.center[1], 2.0f));
    CHECK(approx(o.center[2], 3.0f));

    // corner[0] = min = (0,0,0); corner[7] = max = (2,4,6)
    CHECK(approx(verts[2 + 0].pos[0], 0.0f));
    CHECK(approx(verts[2 + 0].pos[1], 0.0f));
    CHECK(approx(verts[2 + 0].pos[2], 0.0f));
    CHECK(approx(verts[2 + 7].pos[0], 2.0f));
    CHECK(approx(verts[2 + 7].pos[1], 4.0f));
    CHECK(approx(verts[2 + 7].pos[2], 6.0f));
}

// --- ComputeBoundingExtents: symmetric box centred at origin -------------
TEST(MeshRecon3, BoundingExtents_SymmetricBox) {
    std::vector<MeshVertex> verts(2 + 8);
    verts[0].pos[0]= 1; verts[0].pos[1]= 2; verts[0].pos[2]= 3;
    verts[1].pos[0]=-1; verts[1].pos[1]=-2; verts[1].pos[2]=-3;

    BoundsObject o{};
    o.verts = verts.data();
    o.vert_count = 2;
    ComputeBoundingExtents(o);

    CHECK(approx(o.radius, std::sqrt(56.0f), 1e-3f)); // diag (2,4,6)
    CHECK(approx(o.center[0], 0.0f));
    CHECK(approx(o.center[1], 0.0f));
    CHECK(approx(o.center[2], 0.0f));
    // min corner = (-1,-2,-3), max corner = (1,2,3)
    CHECK(approx(verts[2 + 0].pos[0], -1.0f));
    CHECK(approx(verts[2 + 7].pos[2],  3.0f));
}

// --- ComputeBoundingExtents: empty (vert_count <= 0) keeps radius from pass1 -
TEST(MeshRecon3, BoundingExtents_Empty) {
    std::vector<MeshVertex> verts(8);   // only the 8 corner slots, no real verts
    BoundsObject o{};
    o.verts = verts.data();
    o.vert_count = 0;
    ComputeBoundingExtents(o);
    // pass1 left maxLen 0 -> radius 0; center zeroed; AABB block skipped.
    CHECK(approx(o.radius, 0.0f));
    CHECK(approx(o.radius_alias, 0.0f));
    CHECK(approx(o.center[0], 0.0f));
    CHECK(approx(o.center[1], 0.0f));
    CHECK(approx(o.center[2], 0.0f));
}

// --- the eight corner positions follow the exact original ordering -------
TEST(MeshRecon3, BoundingExtents_CornerOrder) {
    std::vector<MeshVertex> verts(2 + 8);
    verts[0].pos[0]=0; verts[0].pos[1]=0; verts[0].pos[2]=0;
    verts[1].pos[0]=2; verts[1].pos[1]=4; verts[1].pos[2]=6;
    BoundsObject o{};
    o.verts = verts.data();
    o.vert_count = 2;
    ComputeBoundingExtents(o);

    const float minX=0, minY=0, minZ=0, maxX=2, maxY=4, maxZ=6;
    float expect[8][3] = {
        {minX,minY,minZ}, {maxX,minY,minZ}, {minX,maxY,minZ}, {maxX,maxY,minZ},
        {minX,minY,maxZ}, {maxX,minY,maxZ}, {minX,maxY,maxZ}, {maxX,maxY,maxZ},
    };
    for (int c = 0; c < 8; ++c)
        for (int k = 0; k < 3; ++k)
            CHECK(approx(verts[2 + c].pos[k], expect[c][k]));
}
