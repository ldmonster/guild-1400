#include "render/skeleton_pose.h"
#include "render/bone_palette.h"
#include "render/node_lod.h"
#include "render/geometry_types.h"
#include "tests/framework/test.h"

#include <cmath>
#include <cstring>
#include <string>

using namespace guild::render;
using guild::u8;
using guild::i32;

namespace {
bool feq(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
} // namespace

// ============================ Node LOD select ==============================

TEST(RenderPose, SelectLodFrame_Forced) {
    LodFrame frames[4]; for (auto& f : frames) { f.polyCount = 1; f.polyCap = 1; }
    LodObject obj; obj.lodCount = 4; obj.drawDataReady = true; obj.frames = frames;
    obj.currentFrameIndex = -1;
    LodView view; view.worldPresent = true;

    bool setBit = false;
    // flags 0x10: ((0xFF & 0x40) >> 6) - 1 = 1 - 1 = 0.
    obj.renderFlags = 0x10;
    CHECK_EQ(SelectLodFrame(obj, view, &setBit), 0);
    CHECK(setBit);
    // flags 0x20: 4*0x20 = 0x80, &0xFF = 0x80, >>6 = 2, -1 = 1.
    obj.renderFlags = 0x20;
    CHECK_EQ(SelectLodFrame(obj, view, &setBit), 1);
}

TEST(RenderPose, SelectLodFrame_Distance) {
    LodFrame frames[4]; for (auto& f : frames) { f.polyCount = 1; f.polyCap = 1; }
    LodObject obj; obj.lodCount = 4; obj.drawDataReady = true; obj.frames = frames;
    obj.currentFrameIndex = -1; obj.renderFlags = 0;
    LodView view; view.worldPresent = true; view.fovScale = 0.05f;
    view.camPos[0] = 0; view.camPos[1] = 0; view.camPos[2] = 0;

    bool setBit = false;
    // dist 5 -> lod = 5*4*0.05 = 1.0; lodCount(4) > truncLod(1) -> trunc(1.0) = 1.
    obj.pos[0] = 5.0f;
    CHECK_EQ(SelectLodFrame(obj, view, &setBit), 1);
    // dist 10 -> lod 2.0 -> 4>2 -> 2.
    obj.pos[0] = 10.0f;
    CHECK_EQ(SelectLodFrame(obj, view, &setBit), 2);
    // dist 100 -> lod 20 -> 4>20 false -> lodCount-1 = 3.
    obj.pos[0] = 100.0f;
    CHECK_EQ(SelectLodFrame(obj, view, &setBit), 3);
    // dist 0 -> lod 0 -> 4>0 -> 0.
    obj.pos[0] = 0.0f;
    CHECK_EQ(SelectLodFrame(obj, view, &setBit), 0);
}

TEST(RenderPose, SelectLodFrame_RejectEmptyFrame) {
    LodFrame frames[4]; for (auto& f : frames) { f.polyCount = 1; f.polyCap = 1; }
    frames[0].polyCount = 0;  // empty frame -> -1
    LodObject obj; obj.lodCount = 4; obj.drawDataReady = true; obj.frames = frames;
    obj.renderFlags = 0x10;  // forced -> index 0 (empty)
    LodView view; view.worldPresent = true;
    CHECK_EQ(SelectLodFrame(obj, view, nullptr), -1);
    // No drawable LOD at all.
    LodObject obj2; obj2.lodCount = 0; obj2.drawDataReady = false;
    CHECK_EQ(SelectLodFrame(obj2, view, nullptr), -1);
}

// ===================== Frustum classify / node cull ========================

TEST(RenderPose, ClassifyBoundingBox_Inside) {
    Frustum fr;
    fr.plane[0][0]=1; fr.plane[0][1]=0; fr.plane[0][2]=0; fr.plane[0][3]=-10;
    fr.plane[1][0]=-1;fr.plane[1][1]=0; fr.plane[1][2]=0; fr.plane[1][3]=-10;
    fr.plane[2][0]=0; fr.plane[2][1]=1; fr.plane[2][2]=0; fr.plane[2][3]=-10;
    fr.plane[3][0]=0; fr.plane[3][1]=-1;fr.plane[3][2]=0; fr.plane[3][3]=-10;
    fr.nearZ = 1.0f; fr.farZ = 100.0f;

    float inside[24]; int k=0;
    for (int x=-1;x<=1;x+=2) for (int y=-1;y<=1;y+=2) for (int z=2;z<=3;++z) {
        inside[k++]=(float)x; inside[k++]=(float)y; inside[k++]=(float)z;
    }
    float mn, mx;
    CHECK_EQ((int)ClassifyBoundingBoxPlanes(inside, fr, &mn, &mx), 0);
    CHECK(feq(mn, 2.0f)); CHECK(feq(mx, 3.0f));
}

TEST(RenderPose, ClassifyBoundingBox_BehindNear) {
    Frustum fr;
    fr.plane[0][0]=1; fr.plane[0][1]=0; fr.plane[0][2]=0; fr.plane[0][3]=-10;
    fr.plane[1][0]=-1;fr.plane[1][1]=0; fr.plane[1][2]=0; fr.plane[1][3]=-10;
    fr.plane[2][0]=0; fr.plane[2][1]=1; fr.plane[2][2]=0; fr.plane[2][3]=-10;
    fr.plane[3][0]=0; fr.plane[3][1]=-1;fr.plane[3][2]=0; fr.plane[3][3]=-10;
    fr.nearZ = 1.0f; fr.farZ = 100.0f;

    float behind[24]; int k=0;
    for (int x=-1;x<=1;x+=2) for (int y=-1;y<=1;y+=2) for (int z=-3;z<=-2;++z) {
        behind[k++]=(float)x; behind[k++]=(float)y; behind[k++]=(float)z;
    }
    // All corners z<near -> AND has 0x10 -> fullyOut 0x40 | (orMask&0xBF has 0x10) = 0x50.
    CHECK_EQ((int)ClassifyBoundingBoxPlanes(behind, fr, nullptr, nullptr), 0x50);
}

TEST(RenderPose, CullNode_ForceVisible) {
    Frustum fr{};
    // object+529 & 0x20 set -> force-visible: (signByte & 0x80) | 0x3F.
    CHECK_EQ((int)CullNodeAgainstFrustum(0x20, 0x80, false, nullptr, fr, nullptr, nullptr),
             0xBF);
    CHECK_EQ((int)CullNodeAgainstFrustum(0x20, 0x00, false, nullptr, fr, nullptr, nullptr),
             0x3F);
    // No AABB -> signByte | 0x40.
    CHECK_EQ((int)CullNodeAgainstFrustum(0x00, 0x01, false, nullptr, fr, nullptr, nullptr),
             0x41);
}

// ======================= Bone-matrix palette ===============================

namespace {
struct PaletteCapture {
    int n = 0;
    char names[4][64];
    float pos[4][3];
    float euler[4][3];
};
void CaptureApply(void* ctx, const char* name, const float* pos, const float* euler) {
    auto* cap = static_cast<PaletteCapture*>(ctx);
    std::strncpy(cap->names[cap->n], name, 63); cap->names[cap->n][63]=0;
    for (int i=0;i<3;++i){ cap->pos[cap->n][i]=pos[i]; cap->euler[cap->n][i]=euler[i]; }
    ++cap->n;
}
// Build an AnimFrame whose bone-0 attach block (frame+84) carries trans + euler.
AnimFrame MakeAttachFrame(int dur, float tx, float ty, float tz,
                          float ex, float ey, float ez) {
    AnimFrame f{};
    f.duration = dur;
    u8* base = reinterpret_cast<u8*>(&f);
    float* attach = reinterpret_cast<float*>(base + 84);  // bone 0 attach
    attach[0]=tx; attach[1]=ty; attach[2]=tz;             // trans
    attach[3]=ex; attach[4]=ey; attach[5]=ez;             // euler @ +12
    return f;
}
} // namespace

TEST(RenderPose, ComputeBoneMatrices_TwoBones) {
    // Two frames so [from=0,to=0] reads frame 0; identity euler -> rows identity.
    static AnimFrame framesA[2] = {
        MakeAttachFrame(10, 5, 6, 7, 0, 0, 0),
        MakeAttachFrame(10, 5, 6, 7, 0, 0, 0),
    };
    static AnimFrame framesB[2] = {
        MakeAttachFrame(10, 10, 0, 0, 0, 0, 0),
        MakeAttachFrame(10, 10, 0, 0, 0, 0, 0),
    };

    BoneGroup groups[1];
    BoneTrack& tA = groups[0].tracks[0];
    tA.active = true; tA.boneIndex = 0; tA.name = "head"; tA.weight = 1.0f;
    tA.fromFrame = 0; tA.toFrame = 0; tA.phaseNum = 0; tA.frames = framesA;
    BoneTrack& tB = groups[0].tracks[1];
    tB.active = true; tB.boneIndex = 0; tB.name = "hand"; tB.weight = 2.0f;
    tB.fromFrame = 0; tB.toFrame = 0; tB.phaseNum = 0; tB.frames = framesB;
    groups[0].tracks[2].active = false;

    BoneNameEntry names[2];
    names[0].name = "head"; names[0].refT[0]=1; names[0].refT[1]=1; names[0].refT[2]=1;
    names[1].name = "hand"; names[1].refT[0]=0; names[1].refT[1]=0; names[1].refT[2]=0;

    BonePaletteRecord pal[4];
    PaletteCapture cap;
    int n = ComputeBoneMatrices(groups, 1, names, 2, pal, &CaptureApply, &cap);
    CHECK_EQ(n, 2);
    CHECK_EQ(cap.n, 2);

    // head: accT = 1.0 * ((5,6,7) - (1,1,1)) = (4,5,6); euler identity -> (0,0,0).
    CHECK(std::string(cap.names[0]) == "head");
    CHECK(feq(cap.pos[0][0], 4.0f)); CHECK(feq(cap.pos[0][1], 5.0f)); CHECK(feq(cap.pos[0][2], 6.0f));
    CHECK(feq(cap.euler[0][0], 0.0f)); CHECK(feq(cap.euler[0][1], 0.0f)); CHECK(feq(cap.euler[0][2], 0.0f));
    // hand: accT = 2.0 * ((10,0,0) - 0) = (20,0,0).
    CHECK(std::string(cap.names[1]) == "hand");
    CHECK(feq(cap.pos[1][0], 20.0f)); CHECK(feq(cap.pos[1][1], 0.0f)); CHECK(feq(cap.pos[1][2], 0.0f));
}

TEST(RenderPose, ComputeBoneMatrices_NameDedupeAndCap) {
    static AnimFrame f2[2] = {
        MakeAttachFrame(10, 4, 0, 0, 0, 0, 0),
        MakeAttachFrame(10, 4, 0, 0, 0, 0, 0),
    };
    // Two tracks same bone name -> one record, count 2, contributions add.
    BoneGroup groups[1];
    for (int i=0;i<2;++i) {
        BoneTrack& t = groups[0].tracks[i];
        t.active=true; t.boneIndex=0; t.name="spine"; t.weight=1.0f;
        t.fromFrame=0; t.toFrame=0; t.phaseNum=0; t.frames=f2;
    }
    groups[0].tracks[2].active=false;
    BoneNameEntry names[1]; names[0].name="spine";
    names[0].refT[0]=0; names[0].refT[1]=0; names[0].refT[2]=0;

    BonePaletteRecord pal[4];
    int n = ComputeBoneMatrices(groups, 1, names, 1, pal, nullptr, nullptr);
    CHECK_EQ(n, 1);
    CHECK_EQ(pal[0].count, 2);
    // accT = (4 + 4) = 8 in x; average is only applied to the rotation rows, not accT.
    CHECK(feq(pal[0].accT[0], 8.0f));
}

// ================== Object morph / lighting vertex walks ===================

TEST(RenderPose, TransformMeshVertices) {
    // World matrix translates by (5,6,7).
    float world[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 5,6,7,1};
    VertexSource sources[2] = {
        {{1,2,3}, {0,0,0}},
        {{-2,-2,-2}, {0,0,0}},
    };
    Vertex verts[2]{};

    MorphMeshBlock mesh; mesh.vertices = verts; mesh.sources = sources; mesh.vertexCount = 2;
    float nearZ, farZ;
    TransformMeshVertices(mesh, world, /*attachClamp=*/true, &nearZ, &farZ);

    CHECK(feq(verts[0].x, 6.0f)); CHECK(feq(verts[0].y, 8.0f)); CHECK(feq(verts[0].z, 10.0f));
    CHECK(feq(verts[1].x, 3.0f)); CHECK(feq(verts[1].y, 4.0f)); CHECK(feq(verts[1].z, 5.0f));
    // Running depth bounds over z {10, 5}.
    CHECK(feq(nearZ, 5.0f)); CHECK(feq(farZ, 10.0f));
}

TEST(RenderPose, ComputeMeshVertexLightingNonSkinned) {
    float I[9] = {1,0,0, 0,1,0, 0,0,1};
    // pos (1,1,1), normal (0,0,1): env uv = (0.78867513, 0.78867513).
    VertexSource sources[1] = { {{0,0,0}, {0,0,1}} };  // normal @ +12
    u8 lit[1] = {1};
    Vertex verts[1]{};
    verts[0].x = 1; verts[0].y = 1; verts[0].z = 1;

    MorphMeshBlock mesh; mesh.vertices = verts; mesh.sources = sources;
    mesh.litFlags = lit; mesh.vertexCount = 1;
    ComputeMeshVertexLightingNonSkinned(mesh, I);
    CHECK(feq(verts[0].u, 0.78867513f)); CHECK(feq(verts[0].v, 0.78867513f));

    // Unlit vertex (flag 0) -> untouched UV.
    Vertex v2[1]{}; v2[0].u = 0.123f; v2[0].v = 0.456f;
    u8 lit0[1] = {0};
    MorphMeshBlock mesh2; mesh2.vertices = v2; mesh2.sources = sources;
    mesh2.litFlags = lit0; mesh2.vertexCount = 1;
    ComputeMeshVertexLightingNonSkinned(mesh2, I);
    CHECK(feq(v2[0].u, 0.123f)); CHECK(feq(v2[0].v, 0.456f));
}

// ======================= Per-track phase advance ===========================

TEST(RenderPose, AdvanceTrackPhase_LoopForward) {
    // 3 frames, durations 10 each, loop mode (0x1).
    i32 durs[3] = {10, 10, 10};
    TrackState st; st.fromFrame = 0; st.toFrame = 1; st.phase = 25; st.mode = 0x01;
    int steps = AdvanceTrackPhase(st, durs, /*first=*/0, /*last=*/2, /*count=*/3);
    // phase 25 -> step at frame0 (consume 10 ->15, frame1), frame1 (consume 10 ->5, frame2);
    // 5 < dur(10) stops. 2 forward steps.
    CHECK(steps >= 1);
    CHECK(st.phase < durs[st.fromFrame]);
}

TEST(RenderPose, AdvanceTrackPhase_ClampOneShot) {
    i32 durs[3] = {10, 10, 10};
    TrackState st; st.fromFrame = 1; st.toFrame = 2; st.phase = 50; st.mode = 0x10;
    AdvanceTrackPhase(st, durs, 0, 2, 3);
    // clamp-to-count: holds at the last segment, marks boundary.
    CHECK(st.boundary);
}

// ===========================================================================
// W11-ANIM hardening — degenerate skeleton-pose / palette inputs (ASAN/UBSAN).
// ===========================================================================

// Zero groups: ComputeBoneMatrices clears the 4-record scratch and returns 0 without
// folding any track. The apply callback is never invoked.
TEST(RenderPoseEdge, ComputeBoneMatrices_ZeroGroups) {
    BonePaletteRecord pal[4];
    PaletteCapture cap;
    int n = ComputeBoneMatrices(nullptr, 0, nullptr, 0, pal, &CaptureApply, &cap);
    CHECK_EQ(n, 0);
    CHECK_EQ(cap.n, 0);
}

// A group whose tracks are all inactive (or marked 0xFF) folds nothing.
TEST(RenderPoseEdge, ComputeBoneMatrices_AllInactive) {
    BoneGroup g[1];
    g[0].tracks[0].active = false;
    g[0].tracks[1].active = true; g[0].tracks[1].boneIndex = 0xFF; // inactive marker
    g[0].tracks[2].active = true; g[0].tracks[2].boneIndex = 0;
    g[0].tracks[2].name = nullptr;                                 // null name -> skip
    BonePaletteRecord pal[4];
    int n = ComputeBoneMatrices(g, 1, nullptr, 0, pal, nullptr, nullptr);
    CHECK_EQ(n, 0);
}

// More distinct bone names than the 4-record palette cap: only 4 records are written;
// the 5th+ distinct name is dropped (the `recCount >= 4` continue). ASAN proves the
// 4-record `pal[4]` scratch is never written past index 3.
TEST(RenderPoseEdge, ComputeBoneMatrices_RecordCap) {
    static AnimFrame f2[2] = {
        MakeAttachFrame(10, 1, 0, 0, 0, 0, 0),
        MakeAttachFrame(10, 1, 0, 0, 0, 0, 0),
    };
    const char* nm[6] = {"b0","b1","b2","b3","b4","b5"};
    BoneGroup g[2];
    int k = 0;
    for (int gi = 0; gi < 2; ++gi)
        for (int ti = 0; ti < 3; ++ti) {
            BoneTrack& t = g[gi].tracks[ti];
            t.active = true; t.boneIndex = 0; t.name = nm[k++]; t.weight = 1.0f;
            t.fromFrame = 0; t.toFrame = 0; t.phaseNum = 0; t.frames = f2;
        }
    BonePaletteRecord pal[4];
    int n = ComputeBoneMatrices(g, 2, nullptr, 0, pal, nullptr, nullptr);
    CHECK_EQ(n, 4);                       // capped at 4 distinct records
}

// AdvanceTrackPhase with zero frames: the W11 fail-safe returns 0 without reading the
// (empty/garbage) durations table.
TEST(RenderPoseEdge, AdvanceTrackPhase_ZeroFrames) {
    TrackState st; st.fromFrame = 0; st.toFrame = 0; st.phase = 100; st.mode = 0x01;
    int steps = AdvanceTrackPhase(st, nullptr, 0, 0, 0);   // frameCount == 0
    CHECK_EQ(steps, 0);
}

// AdvanceTrackPhase with a null durations table (but a positive count) also bails.
TEST(RenderPoseEdge, AdvanceTrackPhase_NullDurations) {
    TrackState st; st.fromFrame = 0; st.phase = 50; st.mode = 0;
    CHECK_EQ(AdvanceTrackPhase(st, nullptr, 0, 2, 3), 0);
}

// AdvanceTrackPhase whose fromFrame index is out of range for the table is clamped to
// frame 0 on the read (no OOB) — ASAN proves durs[fromFrame] never runs off `durs`.
TEST(RenderPoseEdge, AdvanceTrackPhase_OutOfRangeFromFrame) {
    i32 durs[3] = {10, 10, 10};
    TrackState st; st.fromFrame = 99; st.toFrame = 0; st.phase = 5; st.mode = 0;
    int steps = AdvanceTrackPhase(st, durs, 0, 2, 3);
    CHECK(steps >= 0);                    // no ASAN trap == pass
}

// A zero-duration table would spin forever without the loop guard; verify the guard
// terminates (and no OOB) on a 0-duration table with a large phase.
TEST(RenderPoseEdge, AdvanceTrackPhase_ZeroDurationTerminates) {
    i32 durs[3] = {0, 0, 0};
    TrackState st; st.fromFrame = 0; st.toFrame = 1; st.phase = 1000; st.mode = 0x01;
    int steps = AdvanceTrackPhase(st, durs, 0, 2, 3);
    CHECK(steps <= 3 * 2 + 2);            // bounded by the guard
}
