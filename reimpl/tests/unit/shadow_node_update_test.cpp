// =============================================================================
// Golden-vector unit tests for the per-node shadow caster MANAGEMENT drivers:
//   VIBE_Shadow_CastFromLight    @0x5f3f98  (slot alloc/reuse + redraw decision)
//   VIBE_Shadow_UpdateNodeShadows@0x5f4494  (per-node light loop + frustum gate)
//
// All vectors derive directly from the gilde.exe decompile (control flow,
// constants, the integer/pointer/float slot logic). The coupled leaves are
// driven through the module hooks so the management is observed in isolation.
// =============================================================================
#include "tests/framework/test.h"
#include "render/fxrecon_particle_mirror_shadow.h"

#include <cstring>

using namespace guild::render::fxrecon;

namespace {

// ---- hook capture state ----------------------------------------------------
int   g_renderCalls = 0;
int   g_buildCalls  = 0;
int   g_texCalls    = 0;
int   g_acqCalls    = 0;
ShadowNodeSlot* g_lastRenderSlot = nullptr;
void* g_lastRenderLight = nullptr;
float g_lastRenderDir[3] = {0, 0, 0};

void  ResetCapture() {
    g_renderCalls = g_buildCalls = g_texCalls = g_acqCalls = 0;
    g_lastRenderSlot = nullptr;
    g_lastRenderLight = nullptr;
    g_lastRenderDir[0] = g_lastRenderDir[1] = g_lastRenderDir[2] = 0;
}

void* TexLoad() { ++g_texCalls; return reinterpret_cast<void*>(0x700); }
void* AcqSlot(ShadowNodeSlot*, guild::u8, void*, guild::u32) {
    ++g_acqCalls; return reinterpret_cast<void*>(0xCACE);
}
void  RenderHook(void*, void*, void*, void*, const float dir[3], const float[3],
                 void* light, ShadowNodeSlot* slot) {
    ++g_renderCalls;
    g_lastRenderSlot = slot;
    g_lastRenderLight = light;
    g_lastRenderDir[0] = dir[0]; g_lastRenderDir[1] = dir[1]; g_lastRenderDir[2] = dir[2];
}
int   BuildHook(ShadowNodeSlot*, void*) { ++g_buildCalls; return 1; }

// PointThroughBoneChain stand-in: returns a fixed world position depending on
// whether the argument is the light or the node (distinguished by a tag field).
// Default identity is fine for most tests; specific tests override.

// A fully-enabled node + light pair that passes the CastFromLight gate.
void MakeGatedPair(ShadowNode& node, ShadowLight& light) {
    node = ShadowNode{};
    node.flags529 = 0x04;          // caster
    node.hasCasterTable = true;    // +492
    node.casterCount = 0;          // < 5
    node.mesh = reinterpret_cast<void*>(0xE5);
    light = ShadowLight{};
    light.flags529 = 0x04;         // caster
    light.intensity = 1.0f;        // a2[37] != 0
    light.id = reinterpret_cast<void*>(0x116);
}

void InstallHooks() {
    SetLoadShadowTextureHook(&TexLoad);
    SetAcquireCacheSlotHook(&AcqSlot);
    SetRenderMeshShadowHook(&RenderHook);
    SetBuildGroundShadowHook(&BuildHook);
}
void RestoreHooks() {
    SetLoadShadowTextureHook(nullptr);
    SetAcquireCacheSlotHook(nullptr);
    SetRenderMeshShadowHook(nullptr);
    SetBuildGroundShadowHook(nullptr);
    SetPointThroughBoneChainHook(nullptr);
    SetRotateVectorByHierarchyHook(nullptr);
    SetClassifyBoundingBoxHook(nullptr);
    SetTransformBoundingVolumeHook(nullptr);
}

// Enable the module's shadow gate (dword_1408A60 / dword_1408A54).
void EnableShadows() {
    ShadowModuleState& s = ShadowState();
    s.shadowLimitB = 4;    // dword_1408A60 (enables)
    s.shadowLimitA = 8;    // dword_1408A54 (clamp)
    s.enableB = 0;         // byte_1408A6E (cache type)
}

} // namespace

// ---------------------------------------------------------------------------
// GATE — every clause of the 0x4025 conjunction blocks the cast.
// ---------------------------------------------------------------------------
TEST(ShadowNode, CastGateRejectsEachClause) {
    InstallHooks();
    EnableShadows();
    ShadowNode node; ShadowLight light;

    // shadows disabled
    MakeGatedPair(node, light);
    ShadowState().shadowLimitB = 0;
    ResetCapture();
    Shadow_CastFromLight2(&node, &light, nullptr, false);
    CHECK_EQ(g_renderCalls, 0);
    CHECK_EQ(g_acqCalls, 0);
    EnableShadows();

    // not a caster object
    MakeGatedPair(node, light); node.flags529 = 0; ResetCapture();
    Shadow_CastFromLight2(&node, &light, nullptr, false);
    CHECK_EQ(g_acqCalls, 0);

    // no caster table
    MakeGatedPair(node, light); node.hasCasterTable = false; ResetCapture();
    Shadow_CastFromLight2(&node, &light, nullptr, false);
    CHECK_EQ(g_acqCalls, 0);

    // caster budget exhausted (+533 >= 5)
    MakeGatedPair(node, light); node.casterCount = 5; ResetCapture();
    Shadow_CastFromLight2(&node, &light, nullptr, false);
    CHECK_EQ(g_acqCalls, 0);

    // light not a caster
    MakeGatedPair(node, light); light.flags529 = 0; ResetCapture();
    Shadow_CastFromLight2(&node, &light, nullptr, false);
    CHECK_EQ(g_acqCalls, 0);

    // zero light intensity
    MakeGatedPair(node, light); light.intensity = 0.0f; ResetCapture();
    Shadow_CastFromLight2(&node, &light, nullptr, false);
    CHECK_EQ(g_acqCalls, 0);

    RestoreHooks();
}

// ---------------------------------------------------------------------------
// ALLOC — a fresh node with no matching slot acquires the FIRST free slot via
// the cache path (flag124 == 0) and initialises it (+108=-1, +4=light).
// ---------------------------------------------------------------------------
TEST(ShadowNode, CastAllocatesFirstFreeCacheSlot) {
    InstallHooks();
    EnableShadows();
    ShadowNode node; ShadowLight light;
    MakeGatedPair(node, light);          // flags529 bit3 clear -> cache path
    ResetCapture();

    Shadow_CastFromLight2(&node, &light, reinterpret_cast<void*>(0xC78), false);

    // slot 0 was the first free slot, acquired via the cache path.
    CHECK_EQ(g_acqCalls, 1);
    CHECK_EQ(g_texCalls, 0);
    CHECK(node.slots[0].surface == reinterpret_cast<void*>(0xCACE));
    CHECK(node.slots[0].light == light.id);   // +4 = light
    CHECK_EQ(node.slots[0].bboxX0, -1);        // +108 = -1
    CHECK_EQ(node.slots[0].flag124, static_cast<guild::u8>(0)); // cache path
    // slots 1..3 stay empty
    CHECK(node.slots[1].surface == nullptr);
    RestoreHooks();
}

// ---------------------------------------------------------------------------
// ALLOC (texture path) — object +529 bit3 set routes to the "Schatten" texture
// load and sets +124.
// ---------------------------------------------------------------------------
TEST(ShadowNode, CastAllocatesTexturePathWhenBit3Set) {
    InstallHooks();
    EnableShadows();
    ShadowNode node; ShadowLight light;
    MakeGatedPair(node, light);
    node.flags529 |= 0x08;               // bit3 -> +124 = 1, texture path
    ResetCapture();

    Shadow_CastFromLight2(&node, &light, nullptr, false);

    CHECK_EQ(g_texCalls, 1);
    CHECK_EQ(g_acqCalls, 0);
    CHECK_EQ(node.slots[0].flag124, static_cast<guild::u8>(1));
    CHECK(node.slots[0].surface == reinterpret_cast<void*>(0x700));
    RestoreHooks();
}

// ---------------------------------------------------------------------------
// REUSE — a slot already casting the same light is found (no new alloc).
// ---------------------------------------------------------------------------
TEST(ShadowNode, CastReusesMatchingLightSlot) {
    InstallHooks();
    EnableShadows();
    ShadowNode node; ShadowLight light;
    MakeGatedPair(node, light);
    // Pre-seed slot 2 as casting this light, with a surface already bound and a
    // cached dir matching the (identity) computed dir so NO redraw is forced.
    node.slots[2].surface = reinterpret_cast<void*>(0xBEEF);
    node.slots[2].light   = light.id;
    node.slots[2].bboxX0  = 7;           // valid bbox
    node.slots[2].cachedDir = ShadowFVec3{0, 0, 0};
    ResetCapture();

    // identity bone-chain (default) => point-light dir = node.origin - light.origin
    // both transform the zero input to zero => dir == {0,0,0} == cachedDir.
    Shadow_CastFromLight2(&node, &light, nullptr, false);

    CHECK_EQ(g_acqCalls, 0);             // reuse, no alloc
    CHECK_EQ(g_texCalls, 0);
    CHECK_EQ(g_renderCalls, 0);          // dir unchanged + mesh clean -> no redraw
    CHECK_EQ(g_buildCalls, 1);           // ground shadow still emitted
    RestoreHooks();
}

// ---------------------------------------------------------------------------
// REDRAW — a changed light direction forces RenderMeshShadow and refreshes the
// cached dir. Point light: dir = node.origin - light.origin.
// ---------------------------------------------------------------------------
namespace {
// bone-chain that returns a distinct position for the node vs the light, so the
// point-light direction is non-zero.
void BoneChainNodeVsLight(const void* obj, const float[3], float out[3]) {
    // The node and light are distinguished by tag: we set node.mesh as a marker.
    // Simpler: encode via a global current pair. Here use the obj pointer parity.
    if (obj == g_lastRenderLight) { out[0]=1; out[1]=0; out[2]=0; }
    else                          { out[0]=0; out[1]=0; out[2]=0; }
}
}
TEST(ShadowNode, CastRedrawOnDirectionChange) {
    InstallHooks();
    EnableShadows();
    ShadowNode node; ShadowLight light;
    MakeGatedPair(node, light);
    node.slots[0].surface = reinterpret_cast<void*>(0xBEEF);
    node.slots[0].light   = light.id;
    node.slots[0].bboxX0  = 1;
    node.slots[0].cachedDir = ShadowFVec3{99, 99, 99};   // very different
    ResetCapture();

    Shadow_CastFromLight2(&node, &light, nullptr, false);

    CHECK_EQ(g_renderCalls, 1);          // dir changed -> redraw
    CHECK(g_lastRenderSlot == &node.slots[0]);
    CHECK(g_lastRenderLight == light.id);
    // cached dir refreshed to the freshly computed dir (point light, identity
    // bone-chain => {0,0,0}).
    CHECK(node.slots[0].cachedDir.x == 0.0f);
    CHECK(node.slots[0].cachedDir.y == 0.0f);
    CHECK(node.slots[0].cachedDir.z == 0.0f);
    RestoreHooks();
}

// ---------------------------------------------------------------------------
// REDRAW — a dirty mesh (mesh+380) forces a redraw even when the dir is steady.
// ---------------------------------------------------------------------------
TEST(ShadowNode, CastRedrawOnMeshDirty) {
    InstallHooks();
    EnableShadows();
    ShadowNode node; ShadowLight light;
    MakeGatedPair(node, light);
    node.slots[0].surface = reinterpret_cast<void*>(0xBEEF);
    node.slots[0].light   = light.id;
    node.slots[0].bboxX0  = 1;
    node.slots[0].cachedDir = ShadowFVec3{0, 0, 0};   // matches identity dir
    node.meshDirty = true;                             // *(mesh+380) set
    ResetCapture();

    Shadow_CastFromLight2(&node, &light, nullptr, false);

    CHECK_EQ(g_renderCalls, 1);          // mesh dirty -> redraw despite steady dir
    RestoreHooks();
}

// ---------------------------------------------------------------------------
// DIRECTIONAL light — +529 bit4 routes through RotateVectorByHierarchy with the
// flt_5CA2B0 up vector and tolerance 0.01.
// ---------------------------------------------------------------------------
namespace {
float g_rotInDir[3] = {0,0,0};
void  CaptureRotate(const void*, const float in[3], float out[3]) {
    g_rotInDir[0]=in[0]; g_rotInDir[1]=in[1]; g_rotInDir[2]=in[2];
    out[0]=in[0]; out[1]=in[1]; out[2]=in[2];
}
}
TEST(ShadowNode, CastDirectionalUsesUpVectorRotate) {
    InstallHooks();
    EnableShadows();
    SetRotateVectorByHierarchyHook(&CaptureRotate);
    ShadowNode node; ShadowLight light;
    MakeGatedPair(node, light);
    light.flags529 |= 0x10;              // directional
    node.slots[0].surface = reinterpret_cast<void*>(0xBEEF);
    node.slots[0].light   = light.id;
    node.slots[0].bboxX0  = 1;
    node.slots[0].cachedDir = ShadowFVec3{99, 99, 99};
    g_rotInDir[0]=g_rotInDir[1]=g_rotInDir[2]=0;
    ResetCapture();

    Shadow_CastFromLight2(&node, &light, nullptr, false);

    // RotateVectorByHierarchy was driven with flt_5CA2B0 == {0,0,1}.
    CHECK(g_rotInDir[0] == 0.0f);
    CHECK(g_rotInDir[1] == 0.0f);
    CHECK(g_rotInDir[2] == 1.0f);
    CHECK_EQ(g_renderCalls, 1);          // dir {0,0,1} vs cached {99..} -> redraw
    RestoreHooks();
}

// ---------------------------------------------------------------------------
// UpdateNodeShadows — clears +531 bit1, then per-light: visible -> set bit1 +
// cast; fully-outside (classify bit6) -> skip.
// ---------------------------------------------------------------------------
namespace {
guild::u8 g_classifyResult = 0;
guild::u8 ClassifyConst(const float*) { return g_classifyResult; }
int g_xformCalls = 0;
int XformBV(void*, const float*, bool) { ++g_xformCalls; return 0; }
}
TEST(ShadowNode, UpdateClearsCastBitAndRefreshesBVol) {
    InstallHooks();
    EnableShadows();
    SetTransformBoundingVolumeHook(&XformBV);
    SetClassifyBoundingBoxHook(&ClassifyConst);

    ShadowNode node; ShadowLight light;
    MakeGatedPair(node, light);
    node.flag531 = 0xFF;                 // all bits set; expect bit1 cleared first
    node.flag530sign = 0;                // >= 0 -> refresh bvol
    g_xformCalls = 0;
    g_classifyResult = 0x40;             // fully outside -> no cast this light

    ShadowLight* lights[1] = {&light};
    Shadow_UpdateNodeShadows(&node, lights, 1, /*viewMat*/nullptr, nullptr, false);

    CHECK_EQ(g_xformCalls, 1);                         // bvol refreshed (+530>=0)
    CHECK_EQ(node.flag531 & 0x02, 0);                  // cast-bit cleared, not set
    RestoreHooks();
}

TEST(ShadowNode, UpdateVisibleLightSetsBitAndCasts) {
    InstallHooks();
    EnableShadows();
    SetClassifyBoundingBoxHook(&ClassifyConst);

    ShadowNode node; ShadowLight light;
    MakeGatedPair(node, light);
    node.flag531 = 0;
    g_classifyResult = 0x00;             // visible (bit6 clear)
    ResetCapture();

    ShadowLight* lights[1] = {&light};
    Shadow_UpdateNodeShadows(&node, lights, 1, nullptr, nullptr, false);

    CHECK_EQ(node.flag531 & 0x02, 0x02);  // "cast this frame" set
    CHECK_EQ(g_acqCalls, 1);              // CastFromLight ran -> slot allocated
    RestoreHooks();
}

TEST(ShadowNode, UpdateZeroLightsNoCast) {
    InstallHooks();
    EnableShadows();
    ShadowNode node; ShadowLight light;
    MakeGatedPair(node, light);
    ResetCapture();

    Shadow_UpdateNodeShadows(&node, nullptr, 0, nullptr, nullptr, false);

    CHECK_EQ(g_acqCalls, 0);
    CHECK_EQ(g_renderCalls, 0);
    RestoreHooks();
}

// ---------------------------------------------------------------------------
// WAVE-10 HARDENING — the 4-slot caster table FULL with NO matching light and NO
// free slot: the search misses, the alloc finds no empty slot (v5 stays 0), so
// the redraw block is skipped entirely. No slot is written, no OOB across the 4
// fixed slots, returns 0.
// ---------------------------------------------------------------------------
TEST(ShadowNode, CastTableFullNoFreeSlotNoOp) {
    InstallHooks();
    EnableShadows();
    ShadowNode node; ShadowLight light;
    MakeGatedPair(node, light);
    // Fill ALL four slots with surfaces bound to OTHER lights (no match).
    for (int i = 0; i < kShadowNodeSlots; ++i) {
        node.slots[i].surface = reinterpret_cast<void*>(0x1000 + i);
        node.slots[i].light   = reinterpret_cast<void*>(0x9000 + i); // != light.id
        node.slots[i].bboxX0  = 5;
    }
    ResetCapture();
    int r = (int)Shadow_CastFromLight2(&node, &light, nullptr, false);
    CHECK_EQ(r, 0);
    CHECK_EQ(g_acqCalls, 0);     // no free slot to acquire into
    CHECK_EQ(g_texCalls, 0);
    CHECK_EQ(g_renderCalls, 0);  // redraw block skipped (v5 == 0)
    CHECK_EQ(g_buildCalls, 0);
    // The four occupied slots are untouched.
    for (int i = 0; i < kShadowNodeSlots; ++i)
        CHECK(node.slots[i].light == reinterpret_cast<void*>(0x9000 + i));
    RestoreHooks();
}

// WAVE-10 HARDENING — null node / null light short-circuit the gate with no deref.
TEST(ShadowNode, CastNullArgsNoOp) {
    InstallHooks();
    EnableShadows();
    ShadowNode node; ShadowLight light;
    MakeGatedPair(node, light);
    ResetCapture();
    CHECK_EQ((int)Shadow_CastFromLight2(nullptr, &light, nullptr, false), 0);
    CHECK_EQ((int)Shadow_CastFromLight2(&node, nullptr, nullptr, false), 0);
    CHECK_EQ(g_acqCalls, 0);
    CHECK_EQ((int)Shadow_UpdateNodeShadows(nullptr, nullptr, 0, nullptr, nullptr, false), 0);
    RestoreHooks();
}

// ---------------------------------------------------------------------------
// Size thresholds — flt_5F1D80 golden bytes.
// ---------------------------------------------------------------------------
TEST(ShadowNode, SizeThresholdConstants) {
    CHECK(kShadowSizeThresholds[0] == 60.0f);   // 0x42700000
    CHECK(kShadowSizeThresholds[1] == 200.0f);  // 0x43480000
}
