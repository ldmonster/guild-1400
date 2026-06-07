// ===========================================================================
// object_lifecycle8_test.cpp — unit tests for the VIBE_Object_* batch-8 leaves.
// Golden flag words computed by hand from the verbatim bit ops; geometry checked
// against a <cmath> oracle. namespace guild::sim.
// ===========================================================================
#include <cmath>
#include <cstring>

#include "test.h"
#include "sim/object_lifecycle8.h"

using namespace guild;
using namespace guild::sim;

namespace {

// Captor state for hooks.
struct Cap {
    int        attachCount = 0;
    u32        lastFlagWord = 0;
    void*      lastModel = nullptr;
    int        loadCount = 0;
    char       lastLoaded[256] = {0};
    int        pruneCount = 0;
    const char* lastError = nullptr;
    void*      attachReturn = reinterpret_cast<void*>(0x1234);
    int        findFree = 0;          // 0 -> forces a load
};
Cap g_cap;

void* hkAttach(void* model, u32 word) {
    g_cap.attachCount++; g_cap.lastModel = model; g_cap.lastFlagWord = word;
    return g_cap.attachReturn;
}
void hkLoad(const char* name) { g_cap.loadCount++; std::strncpy(g_cap.lastLoaded, name, 255); }
int  hkFindFree() { return g_cap.findFree; }
void hkPrune(void*) { g_cap.pruneCount++; }
void hkErr(const char* m) { g_cap.lastError = m; }

ObjLife8Hooks MakeAnimHooks() {
    ObjLife8Hooks h;
    h.animAttachToBone = hkAttach;
    h.animLoadStreamToStock = hkLoad;
    h.animFindFreeMeshSlot = hkFindFree;
    h.animPruneExpiredAttachments = hkPrune;
    h.reportError = hkErr;
    return h;
}

// Build a node with a model ptr so the attach path runs.
SceneNode8 MakeModelNode() {
    SceneNode8 n;
    n.d(460) = 0x4000;   // non-null model handle
    return n;
}

}  // namespace

// --- CmdAttachAnimationOnce: flag word == 0x25000, loads + attaches + prunes ----
TEST(ObjLife8_Once, FlagWordAndLoad) {
    g_cap = Cap();
    ObjLife8SetHooks(MakeAnimHooks());
    SceneNode8 n = MakeModelNode();
    AnimAttachFlags fl;
    int r = ObjectCmdAttachAnimationOnce(&n, "walk", &fl);
    CHECK_EQ(r, 0);
    CHECK_EQ(fl.word, 151552u);          // 0x25000
    CHECK_EQ(g_cap.attachCount, 1);
    CHECK_EQ(g_cap.lastFlagWord, 151552u);
    CHECK_EQ(g_cap.loadCount, 1);        // findFree==0 forced a load
    CHECK_EQ(g_cap.pruneCount, 1);
    CHECK_EQ(std::strcmp(g_cap.lastLoaded, "walk.baf"), 0);  // ".baf" appended
    ObjLife8ResetHooks();
}

TEST(ObjLife8_Once, NoModelNoAttach) {
    g_cap = Cap();
    ObjLife8SetHooks(MakeAnimHooks());
    SceneNode8 n;                        // model == 0
    int r = ObjectCmdAttachAnimationOnce(&n, "walk", nullptr);
    CHECK_EQ(r, 0);
    CHECK_EQ(g_cap.attachCount, 0);      // bailed before attach
    ObjLife8ResetHooks();
}

// --- CmdAttachAnimationLooped: golden flag words for loop on/off ----------------
// Build: w=0; if(loop!=0) BYTE1&=~8 else BYTE1|=8; BYTE2|=2; BYTE0=0;
//        BYTE1=(BYTE1&0x2F)|0x50.
// loop!=0: BYTE1 starts 0 -> &~8 ->0 -> (0&0x2F)|0x50 = 0x50; BYTE2=2.
//          word = 0x00 | (0x50<<8) | (2<<16) = 0x025000 = 151552.
// loop==0: BYTE1 0 -> |8 ->8 -> (8&0x2F)|0x50 = 0x58; word = 0x025800 = 153600.
TEST(ObjLife8_Looped, GoldenFlagWords) {
    g_cap = Cap();
    ObjLife8SetHooks(MakeAnimHooks());
    SceneNode8 n = MakeModelNode();
    CmdPumpState pump;                   // no scan pass
    AnimAttachFlags fOn, fOff;
    ObjectCmdAttachAnimationLooped(&n, "run", 1, &pump, &fOn);
    CHECK_EQ(fOn.word, 0x025000u);       // 151552
    ObjectCmdAttachAnimationLooped(&n, "run", 0, &pump, &fOff);
    CHECK_EQ(fOff.word, 0x025800u);      // 153600
    CHECK_EQ(g_cap.attachCount, 2);
    ObjLife8ResetHooks();
}

TEST(ObjLife8_Looped, ScanPassRequeues) {
    g_cap = Cap();
    ObjLife8SetHooks(MakeAnimHooks());
    SceneNode8 n = MakeModelNode();
    CmdPumpState pump;
    pump.scanPassActive = true;
    pump.scanHandlerId = kCmdAttachAnimationLooped;
    int r = ObjectCmdAttachAnimationLooped(&n, "run", 1, &pump, nullptr);
    CHECK_EQ(r, 0);
    CHECK(pump.requeued);
    CHECK_EQ(pump.requeuedHandlerId, (int)kCmdAttachAnimationLooped);
    CHECK_EQ(g_cap.attachCount, 0);      // scan pass does not attach
    ObjLife8ResetHooks();
}

// --- CmdAttachAnimLoopedVerified: golden words + error on attach fail ------------
// w=0x20000; verify==1: BYTE1|=0x10; BYTE0=1; BYTE1=(0x10&0x3F)|0x40=0x50.
//   word = 1 | (0x50<<8) | (2<<16)=0x025001? -> BYTE2 of 0x20000 is 2. word=0x025001 = 151553.
// verify==0: BYTE1&=~0x10 (0); BYTE0=0; BYTE1=(0&0x3F)|0x40=0x40; word=0|(0x40<<8)|(2<<16)=0x024000.
TEST(ObjLife8_Verified, GoldenFlagWords) {
    g_cap = Cap();
    ObjLife8SetHooks(MakeAnimHooks());
    SceneNode8 n = MakeModelNode();
    CmdPumpState pump;
    AnimAttachFlags f1, f0;
    ObjectCmdAttachAnimLoopedVerified(&n, "jump", 1, &pump, &f1);
    CHECK_EQ(f1.word, 0x025001u);        // 151553
    ObjectCmdAttachAnimLoopedVerified(&n, "jump", 0, &pump, &f0);
    CHECK_EQ(f0.word, 0x024000u);        // 147456
    ObjLife8ResetHooks();
}

TEST(ObjLife8_Verified, ReportsErrorOnAttachFail) {
    g_cap = Cap();
    ObjLife8Hooks h = MakeAnimHooks();
    g_cap.attachReturn = nullptr;        // attach fails
    ObjLife8SetHooks(h);
    SceneNode8 n = MakeModelNode();
    CmdPumpState pump;
    ObjectCmdAttachAnimLoopedVerified(&n, "jump", 1, &pump, nullptr);
    CHECK(g_cap.lastError != nullptr);
    ObjLife8ResetHooks();
}

// --- CmdAttachAnimationToDummy: mode-driven flag word ----------------------------
// mode==1: w=0; BYTE1&=0xF7(0); BYTE2|=2; BYTE1=(0&0xE7)|0x10=0x10; BYTE0=1;
//          BYTE1=(0x10&0x3F)|0x40=0x50; word=1|(0x50<<8)|(2<<16)=0x025001.
// mode==0: BYTE1=(0&0xE7)=0; BYTE0=0; BYTE1=(0&0x3F)|0x40=0x40; word=0|(0x40<<8)|(2<<16)=0x024000.
TEST(ObjLife8_ToDummy, ModeFlagWords) {
    g_cap = Cap();
    ObjLife8SetHooks(MakeAnimHooks());
    SceneNode8 self = MakeModelNode();
    std::strcpy(self.s(64), "anim");     // self name at +64
    SceneNode8 d1; d1.b(192) = 1;
    SceneNode8 d0; d0.b(192) = 0;
    AnimAttachFlags f1, f0;
    char r1 = ObjectCmdAttachAnimationToDummy(&self, &d1, &f1);
    CHECK_EQ((int)r1, 1);
    CHECK_EQ(f1.word, 0x025001u);
    char r0 = ObjectCmdAttachAnimationToDummy(&self, &d0, &f0);
    CHECK_EQ((int)r0, 1);
    CHECK_EQ(f0.word, 0x024000u);
    ObjLife8ResetHooks();
}

TEST(ObjLife8_ToDummy, InvalidDummyBails) {
    g_cap = Cap();
    ObjLife8Hooks h = MakeAnimHooks();
    h.dummyValid = [](void*) { return 0; };
    ObjLife8SetHooks(h);
    SceneNode8 self = MakeModelNode();
    SceneNode8 d;
    char r = ObjectCmdAttachAnimationToDummy(&self, &d, nullptr);
    CHECK_EQ((int)r, 1);
    CHECK_EQ(g_cap.attachCount, 0);
    ObjLife8ResetHooks();
}

// --- CmdObjectFlightSingle / Flight: pump + find-by-handle -----------------------
TEST(ObjLife8_FlightSingle, ApplyTransformScaled) {
    static int g_lastScaled = -999;
    static void* g_found = reinterpret_cast<void*>(0x77);
    ObjLife8Hooks h;
    h.findByHandle = [](int, int, void*) -> void* { return reinterpret_cast<void*>(0x77); };
    h.characterApplyBoneTransform = [](void*, int s) { g_lastScaled = s; };
    ObjLife8SetHooks(h);
    (void)g_found;
    CmdPumpState pump;
    int r = ObjectCmdObjectFlightSingle(170, 5, nullptr, &pump);
    CHECK_EQ(r, 1);
    CHECK_EQ(g_lastScaled, 170 / 17);    // == 10
    ObjLife8ResetHooks();
}

TEST(ObjLife8_FlightSingle, ScanPassReturnsZero) {
    ObjLife8Hooks h;
    ObjLife8SetHooks(h);
    CmdPumpState pump;
    pump.scanPassActive = true;
    pump.scanHandlerId = kCmdObjectFlightSingle;
    pump.flightPending = true;
    int r = ObjectCmdObjectFlightSingle(170, 5, nullptr, &pump);
    CHECK_EQ(r, 0);
    CHECK(pump.requeued);
    ObjLife8ResetHooks();
}

TEST(ObjLife8_Flight, CountsFoundMarkers) {
    static int g_found = -1;
    static int g_color = -1;
    ObjLife8Hooks h;
    // ids 0..4: return non-null only for odd ids.
    h.findByHandle = [](int, int id, void*) -> void* {
        return (id & 1) ? reinterpret_cast<void*>(0x10 + id) : nullptr;
    };
    h.drawObjectMarkers3D = [](void*, int c, void* const[5], int f) { g_color = c; g_found = f; };
    ObjLife8SetHooks(h);
    int ids[5] = {1, 2, 3, 4, 5};        // odd: 1,3,5 -> 3 found
    CmdPumpState pump;
    int r = ObjectCmdObjectFlight(reinterpret_cast<void*>(0x1), 28, ids, nullptr, &pump);
    CHECK_EQ(r, 1);
    CHECK_EQ(g_found, 3);
    CHECK_EQ(g_color, 28 / 14);          // == 2
    ObjLife8ResetHooks();
}

TEST(ObjLife8_Flight, NullSelfReportsError) {
    static const char* g_err = nullptr;
    ObjLife8Hooks h;
    h.reportError = [](const char* m) { g_err = m; };
    ObjLife8SetHooks(h);
    int ids[5] = {1, 2, 3, 4, 5};
    CmdPumpState pump;
    int r = ObjectCmdObjectFlight(nullptr, 28, ids, nullptr, &pump);
    CHECK_EQ(r, 0);
    CHECK(g_err != nullptr);
    ObjLife8ResetHooks();
}

// --- AssignToRoomByName: prefix mismatch -> 1; match+near -> assign --------------
TEST(ObjLife8_AssignRoom, NoNameMatchReturnsOne) {
    ObjLife8ResetHooks();
    SceneNode8 n;
    std::strcpy(n.s(0), "kitchen");
    SceneNode8* out = nullptr;
    float anchor[3] = {0, 0, 0};
    char r = ObjectAssignToRoomByName(&n, "bedroom", anchor, &out);
    CHECK_EQ((int)r, 1);
    CHECK(out == nullptr);
}

TEST(ObjLife8_AssignRoom, MatchAndNearAssigns) {
    ObjLife8ResetHooks();                // identity bone-chain (copies local pos)
    SceneNode8 n;
    std::strcpy(n.s(0), "Bedroom_01");   // case-insensitive prefix "bedroom"
    n.f(76) = 10.f; n.f(80) = 20.f; n.f(84) = 30.f;   // local pos
    SceneNode8* out = nullptr;
    float anchor[3] = {10.f, 20.f, 30.f};             // within 100.0
    char r = ObjectAssignToRoomByName(&n, "bedroom", anchor, &out);
    CHECK_EQ((int)r, 0);
    CHECK(out == &n);
}

TEST(ObjLife8_AssignRoom, MatchButFarRejects) {
    ObjLife8ResetHooks();
    SceneNode8 n;
    std::strcpy(n.s(0), "bedroom_2");
    n.f(76) = 0.f; n.f(80) = 0.f; n.f(84) = 0.f;
    SceneNode8* out = nullptr;
    float anchor[3] = {500.f, 0.f, 0.f};              // far beyond 100.0
    char r = ObjectAssignToRoomByName(&n, "bedroom", anchor, &out);
    CHECK_EQ((int)r, 1);
    CHECK(out == nullptr);
}

// --- ComputeScreenBounds billboard core vs cmath oracle --------------------------
TEST(ObjLife8_ScreenBounds, BillboardOracle) {
    ObjLife8ResetHooks();
    SceneNode8 n;                        // attachKind 0 -> billboard
    n.f(76) = 2.f; n.f(80) = 3.f; n.f(84) = 4.f;      // local pos (z used as depth)
    n.f(104) = 1.5f;                     // billboard radius
    ScreenProj p;
    p.xScale = 100.f; p.yScale = 80.f; p.xOffset = 320.f; p.yOffset = 240.f;
    p.radiusScale = 1.0f;
    int out[4] = {0, 0, 0, 0};
    char r = ObjectComputeScreenBoundsBillboard(&n, nullptr, p, out);
    CHECK_EQ((int)r, 1);
    // oracle
    float invZ = 1.0f / 4.f;
    float rad = 100.f * 1.5f * invZ * 1.0f;
    float cx = 100.f * 2.f * invZ + 320.f;
    float cy = 80.f * 3.f * invZ + 240.f;
    CHECK_EQ(out[0], (int)(cx - rad));
    CHECK_EQ(out[1], (int)(cy + rad));
    CHECK_EQ(out[2], (int)(cx + rad));
    CHECK_EQ(out[3], (int)(cy - rad));
}

// --- SelectTextureSet: already-active / out-of-range / loop --------------------
TEST(ObjLife8_TextureSet, AlreadyActive) {
    ObjLife8ResetHooks();
    SceneNode8 n; n.d(492) = 0x900;      // mesh present
    char r = ObjectSelectTextureSet(&n, 2, 4, 5, /*current*/ 2);
    CHECK_EQ((int)r, 1);                 // no-op success
}

TEST(ObjLife8_TextureSet, OutOfRange) {
    ObjLife8ResetHooks();
    SceneNode8 n; n.d(492) = 0x900;
    char r = ObjectSelectTextureSet(&n, 9, 4, 5, /*current*/ 0);
    CHECK_EQ((int)r, 0);                 // set 9 >= setCount 5
    CHECK((n.b(528) & 4) != 0);          // dirty bit still got set
}

TEST(ObjLife8_TextureSet, LoopAllGroups) {
    static int g_swaps = 0;
    ObjLife8Hooks h;
    h.applyTextureSwap = [](void*, int, u8) -> int { g_swaps++; return 1; };
    ObjLife8SetHooks(h);
    SceneNode8 n; n.d(492) = 0x900;
    char r = ObjectSelectTextureSet(&n, 1, /*groups*/ 3, /*sets*/ 5, /*current*/ 0);
    CHECK_EQ((int)r, 1);
    CHECK_EQ(g_swaps, 3);
    ObjLife8ResetHooks();
}

TEST(ObjLife8_TextureSet, NewNotFoundFlipsResult) {
    ObjLife8Hooks h;
    h.applyTextureSwap = [](void*, int g, u8) -> int { return g == 1 ? 0 : 1; };
    ObjLife8SetHooks(h);
    SceneNode8 n; n.d(492) = 0x900;
    char r = ObjectSelectTextureSet(&n, 1, 3, 5, 0);
    CHECK_EQ((int)r, 0);                 // group 1 failed -> overall 0
    ObjLife8ResetHooks();
}

TEST(ObjLife8_TextureSet, NoMeshReturnsZero) {
    ObjLife8ResetHooks();
    SceneNode8 n;                        // mesh == 0
    char r = ObjectSelectTextureSet(&n, 1, 3, 5, 0);
    CHECK_EQ((int)r, 0);
}
