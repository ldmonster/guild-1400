#include "render/object_project.h"

#include "tests/framework/test.h"

#include <cmath>

// =============================================================================
// ObjectProject — golden tests for the per-object perspective project + backface
// cull of VIBE_Render_ProjectObjectVertices @0x5ac970 (render::ProjectObjectVertices):
// the 1/z perspective divide, the +76 visible gate, and the projected signed-area
// backface cull. No assets.
// =============================================================================
using namespace guild;
using guild::render::Vertex;
using guild::render::Polygon;
using guild::render::ObjectProjectScalars;
using guild::render::ProjectObjectVertices;

namespace {
bool feq(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
Vertex Vv(float x, float y, float z, u8 clip) {
    Vertex v{}; v.x = x; v.y = y; v.z = z; v.clipFlags = clip; return v;
}
} // namespace

// (a) Perspective divide: screenX = xScale*x/z + xOffset, screenY = yScale*y/z + yOffset.
TEST(ObjectProject, PerspectiveDivide) {
    Vertex verts[2] = {Vv(2, 0, 4, 0x80), Vv(0, 8, 2, 0x80)};
    ObjectProjectScalars s; s.xScale = 10; s.xOffset = 80; s.yScale = 10; s.yOffset = 60;
    int n = ProjectObjectVertices(verts, 2, nullptr, 0, s);
    CHECK_EQ(n, 2);
    // v0: invZ=0.25 -> sx = 10*2*0.25+80 = 85 ; sy = 10*0*0.25+60 = 60.
    CHECK(feq(verts[0].screenX, 85.0f)); CHECK(feq(verts[0].screenY, 60.0f));
    // v1: invZ=0.5 -> sx = 10*0*0.5+80 = 80 ; sy = 10*8*0.5+60 = 100.
    CHECK(feq(verts[1].screenX, 80.0f)); CHECK(feq(verts[1].screenY, 100.0f));
}

// (b) The +76 visible gate: only sign-bit (0x80) vertices project (projectAll overrides).
TEST(ObjectProject, VisibleGate) {
    Vertex verts[2] = {Vv(1, 1, 1, 0x80), Vv(1, 1, 1, 0x00)};
    ObjectProjectScalars s;  // identity-ish (1,0,1,0)
    int n = ProjectObjectVertices(verts, 2, nullptr, 0, s);
    CHECK_EQ(n, 1);                              // only the flagged vertex
    // projectAll projects both.
    Vertex v2[2] = {Vv(1, 1, 1, 0x00), Vv(1, 1, 1, 0x00)};
    CHECK_EQ(ProjectObjectVertices(v2, 2, nullptr, 0, s, /*projectAll*/true), 2);
}

// (c) Backface cull by projected signed area: a back-facing front-candidate is culled
// (sign bit cleared); a front-facing one is kept.
TEST(ObjectProject, BackfaceCull) {
    // Two verts pre-projected (screenX/Y set), one poly. Use projectAll=false and set
    // screen coords directly so only the cull runs.
    Vertex v[3]{};
    v[0].screenX = 0;  v[0].screenY = 0;
    v[1].screenX = 10; v[1].screenY = 0;
    v[2].screenX = 0;  v[2].screenY = 10;
    Polygon back{}; back.v0 = &v[0]; back.v1 = &v[1]; back.v2 = &v[2];
    back.flags36 = 0x80;   // front-candidate
    // lhs = (0-0)*(0-0) = 0 ; rhs = (0-10)*(0-10) = 100 ; lhs(0) > rhs(100)? no -> kept.
    ProjectObjectVertices(nullptr, 0, &back, 1, ObjectProjectScalars{});
    CHECK((back.flags36 & 0x80) != 0);   // kept (front-facing winding)

    // Swap v1/v2 to flip the winding -> lhs > rhs -> culled.
    Polygon front{}; front.v0 = &v[0]; front.v1 = &v[2]; front.v2 = &v[1];
    front.flags36 = 0x80;
    // lhs = (0-10)*(0-10) = 100 ; rhs = (0-0)*(0-0) = 0 ; 100 > 0 -> cull.
    ProjectObjectVertices(nullptr, 0, &front, 1, ObjectProjectScalars{});
    CHECK((front.flags36 & 0x80) == 0);  // culled (back-facing winding)

    // The no-cull flag (+38 & 4) keeps a back-facing poly.
    Polygon noCull{}; noCull.v0 = &v[0]; noCull.v1 = &v[2]; noCull.v2 = &v[1];
    noCull.flags36 = 0x80; noCull.flags38 = 0x04;
    ProjectObjectVertices(nullptr, 0, &noCull, 1, ObjectProjectScalars{});
    CHECK((noCull.flags36 & 0x80) != 0); // kept despite back-facing
}
