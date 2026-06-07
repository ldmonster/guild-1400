#include "render/skeleton_pose.h"
#include "render/skeleton.h"
#include "render/bone_palette.h"
#include "render/node_lod.h"
#include "render/vertex_lighting.h"
#include "render/geometry_types.h"
#include "util/matrix.h"
#include "tests/framework/test.h"

#include <cmath>
#include <cstring>
#include <string>

using namespace guild::render;
using guild::u8;
using guild::i32;

namespace {
bool feq(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

// A bone float-record large enough for the offsets the skeleton primitives touch.
struct Bone {
    float f[160];
    Bone() { std::memset(f, 0, sizeof(f)); }
    void SetParent(Bone* p) {
        float** slot = reinterpret_cast<float**>(reinterpret_cast<char*>(f) + 504);
        *slot = p ? p->f : nullptr;
    }
    void SetIdentityRotation() { f[99]=1; f[104]=1; f[109]=1; f[114]=1; }
    void SetLocalTranslation(float x, float y, float z) { f[30]=x; f[31]=y; f[32]=z; }
};

AnimFrame MakeFrame(int dur, float tx, float ty, float tz) {
    AnimFrame f{}; f.duration = dur; f.tx = tx; f.ty = ty; f.tz = tz; return f;
}
} // namespace

// =============================================================================
// Full skeletal flow: build a 3-bone chain, accumulate world matrices, interpolate
// a keyframed bone translation, skin vertices through the morph-vertex object walk,
// then build the name-keyed bone palette — verifying every stage vs a Python ref.
// =============================================================================
TEST(RenderPoseE2E, SkeletonToSkinnedVerticesAndPalette) {
    // --- 1. Build the bone chain b0 -> b1 -> b2 (b2 root). ---
    Bone b0, b1, b2;
    b0.SetIdentityRotation(); b0.SetLocalTranslation(1, 0, 0);
    b1.SetIdentityRotation(); b1.SetLocalTranslation(0, 2, 0);
    b2.SetIdentityRotation(); b2.SetLocalTranslation(0, 0, 3);
    b0.SetParent(&b1); b1.SetParent(&b2); b2.SetParent(nullptr);

    // --- 2. Accumulate b0's world matrix up the chain (non-root). ---
    float ident[16]; guild::util::MatrixIdentity(ident);
    float world[16];
    AccumulateBoneMatrices(b0.f, /*isRoot=*/0, world, ident);
    // World translation column == (1,2,3) (sum of local translations).
    CHECK(feq(world[12], 1.0f)); CHECK(feq(world[13], 2.0f)); CHECK(feq(world[14], 3.0f));
    CHECK(feq(world[0], 1.0f)); CHECK(feq(world[5], 1.0f)); CHECK(feq(world[10], 1.0f));

    // --- 3. Skin two model-space vertices through the morph-vertex object walk. ---
    VertexSource sources[2] = {
        {{10, 0, 0}, {0, 0, 1}},
        {{0, 5, 0}, {0, 0, 1}},
    };
    Vertex verts[2]{};
    MorphMeshBlock mesh; mesh.vertices = verts; mesh.sources = sources; mesh.vertexCount = 2;
    float nearZ, farZ;
    TransformMeshVertices(mesh, world, /*attachClamp=*/true, &nearZ, &farZ);

    // vtx(10,0,0) -> (11,2,3); vtx(0,5,0) -> (1,7,3).
    CHECK(feq(verts[0].x, 11.0f)); CHECK(feq(verts[0].y, 2.0f)); CHECK(feq(verts[0].z, 3.0f));
    CHECK(feq(verts[1].x, 1.0f));  CHECK(feq(verts[1].y, 7.0f)); CHECK(feq(verts[1].z, 3.0f));
    CHECK(feq(nearZ, 3.0f)); CHECK(feq(farZ, 3.0f));

    // --- 4. Env-map lighting walk over the skinned vertices. ---
    float m3x3[9] = {1,0,0, 0,1,0, 0,0,1};
    verts[0].x = 1; verts[0].y = 1; verts[0].z = 1;     // place at (1,1,1)
    VertexSource lsources[2] = { {{0,0,0},{0,0,1}}, {{0,0,0},{0,0,1}} };
    u8 lit[2] = {1, 0};                                  // vtx0 lit, vtx1 not
    MorphMeshBlock lmesh; lmesh.vertices = verts; lmesh.sources = lsources;
    lmesh.litFlags = lit; lmesh.vertexCount = 2;
    ComputeMeshVertexLightingNonSkinned(lmesh, m3x3);
    // pos(1,1,1) normal(0,0,1) -> env uv (0.78867513, 0.78867513).
    CHECK(feq(verts[0].u, 0.78867513f)); CHECK(feq(verts[0].v, 0.78867513f));

    // --- 5. Interpolate a keyframed bone translation across frames. ---
    AnimFrame frames[3] = {
        MakeFrame(10, 0, 0, 0),
        MakeFrame(10, 1, 2, 3),
        MakeFrame(10, 5, 4, 9),
    };
    float interpBone[160] = {0};
    interpBone[99] = 1; interpBone[104] = 1; interpBone[109] = 1; // identity 3x3
    // (base translation 0). from=0,to=1,phaseNum=5,phaseEnd=5.
    float interpOut[3];
    InterpolateBoneFrame(frames, interpBone, /*from=*/0, /*to=*/1, /*phaseNum=*/5,
                         /*phaseEnd=*/5, interpOut);
    CHECK(feq(interpOut[0], 2.5f)); CHECK(feq(interpOut[1], 2.0f)); CHECK(feq(interpOut[2], 4.5f));
}

// =============================================================================
// Palette across frames: advance a track's phase, then fold its bone into the
// name-keyed palette and verify the pushed pose.
// =============================================================================
namespace {
struct Cap { int n=0; float pos[3]; float euler[3]; char name[64]; };
void CaptureApply(void* ctx, const char* name, const float* pos, const float* euler) {
    auto* c = static_cast<Cap*>(ctx);
    std::strncpy(c->name, name, 63); c->name[63]=0;
    for (int i=0;i<3;++i){ c->pos[i]=pos[i]; c->euler[i]=euler[i]; }
    ++c->n;
}
AnimFrame MakeAttach(int dur, float tx, float ty, float tz) {
    AnimFrame f{}; f.duration = dur;
    float* a = reinterpret_cast<float*>(reinterpret_cast<u8*>(&f) + 84);
    a[0]=tx; a[1]=ty; a[2]=tz; a[3]=0; a[4]=0; a[5]=0;  // trans + identity euler
    return f;
}
} // namespace

TEST(RenderPoseE2E, TrackAdvanceThenPalette) {
    // Track plays frames 0..2, durations 10. Start phase mid-frame and advance.
    i32 durs[3] = {10, 10, 10};
    TrackState ts; ts.fromFrame = 0; ts.toFrame = 1; ts.phase = 12; ts.mode = 0x01;
    AdvanceTrackPhase(ts, durs, 0, 2, 3);
    CHECK(ts.phase < durs[ts.fromFrame]);   // phase consumed into a valid segment

    // Fold the advanced bone into the palette. With from==to and phase 0 at the
    // resolved frame the blend reads that frame's attach translation.
    static AnimFrame af[3] = {
        MakeAttach(10, 3, 4, 5),
        MakeAttach(10, 3, 4, 5),
        MakeAttach(10, 3, 4, 5),
    };
    BoneGroup groups[1];
    BoneTrack& t = groups[0].tracks[0];
    t.active = true; t.boneIndex = 0; t.name = "root"; t.weight = 1.0f;
    t.fromFrame = ts.fromFrame; t.toFrame = ts.fromFrame; t.phaseNum = 0; t.frames = af;
    groups[0].tracks[1].active = false;
    groups[0].tracks[2].active = false;

    BoneNameEntry names[1]; names[0].name = "root";
    names[0].refT[0]=1; names[0].refT[1]=1; names[0].refT[2]=1;

    BonePaletteRecord pal[4];
    Cap cap;
    int n = ComputeBoneMatrices(groups, 1, names, 1, pal, &CaptureApply, &cap);
    CHECK_EQ(n, 1);
    CHECK_EQ(cap.n, 1);
    CHECK(std::string(cap.name) == "root");
    // accT = 1.0 * ((3,4,5) - (1,1,1)) = (2,3,4); identity euler -> (0,0,0).
    CHECK(feq(cap.pos[0], 2.0f)); CHECK(feq(cap.pos[1], 3.0f)); CHECK(feq(cap.pos[2], 4.0f));
    CHECK(feq(cap.euler[0], 0.0f)); CHECK(feq(cap.euler[1], 0.0f)); CHECK(feq(cap.euler[2], 0.0f));
}

// =============================================================================
// LOD-select -> cull gate, the front of the ProcessSceneNode chain.
// =============================================================================
TEST(RenderPoseE2E, LodSelectThenCull) {
    LodFrame frames[4]; for (auto& f : frames) { f.polyCount = 2; f.polyCap = 4; }
    LodObject obj; obj.lodCount = 4; obj.drawDataReady = true; obj.frames = frames;
    obj.renderFlags = 0; obj.currentFrameIndex = -1;
    LodView view; view.worldPresent = true; view.fovScale = 0.05f;
    obj.pos[0] = 10.0f;  // dist 10 -> lod 2

    bool setBit = false;
    i32 lod = SelectLodFrame(obj, view, &setBit);
    CHECK_EQ(lod, 2);
    CHECK(setBit);

    // Cull the selected node against a frustum. Box inside -> not culled (no 0x40).
    Frustum fr;
    fr.plane[0][0]=1; fr.plane[0][1]=0; fr.plane[0][2]=0; fr.plane[0][3]=-100;
    fr.plane[1][0]=-1;fr.plane[1][1]=0; fr.plane[1][2]=0; fr.plane[1][3]=-100;
    fr.plane[2][0]=0; fr.plane[2][1]=1; fr.plane[2][2]=0; fr.plane[2][3]=-100;
    fr.plane[3][0]=0; fr.plane[3][1]=-1;fr.plane[3][2]=0; fr.plane[3][3]=-100;
    fr.nearZ = 1.0f; fr.farZ = 1000.0f;
    float corners[24]; int k=0;
    for (int x=-1;x<=1;x+=2) for (int y=-1;y<=1;y+=2) for (int z=8;z<=12;z+=4) {
        corners[k++]=(float)x; corners[k++]=(float)y; corners[k++]=(float)z;
    }
    u8 cull = CullNodeAgainstFrustum(0x00, 0x00, /*hasAabb=*/true, corners, fr, nullptr, nullptr);
    CHECK_EQ((int)(cull & 0x40), 0);  // visible
}
