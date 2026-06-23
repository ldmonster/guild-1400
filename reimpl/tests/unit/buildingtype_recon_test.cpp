// Golden-vector unit tests for src/sim/buildingtype_recon.{h,cpp}.
// Covers the four reconstructed building-cluster functions:
//   VIBE_Bauplatz_GetSize           0x577628
//   VIBE_Bauplatz_MapOneToSupermap  0x5774b8
//   VIBE_Building_AllocStorageRoom  0x588988  (security clamp + flow)
//   VIBE_Building_RegisterNames     0x504a54  (candidate pick)
// plus the byte-faithful string comparators.  Self-contained.
#include "tests/framework/test.h"

#include <array>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "sim/buildingtype_recon.h"

using namespace guild;
using guild::sim::BauplatzSizeRec;
using guild::sim::BuildingTypeDef;
using guild::sim::ObjectRec;

// ===========================================================================
// String comparators (byte-faithful).
// ===========================================================================
TEST(BuildingTypeReconStr, StrCmpEquality) {
    CHECK_EQ(0, sim::Util_StrCmp("abc", "abc"));
    CHECK_EQ(0, sim::Util_StrCmp("", ""));
    const char* s = "same";
    CHECK_EQ(0, sim::Util_StrCmp(s, s));            // a==b early-out
}

TEST(BuildingTypeReconStr, StrCmpOrdering) {
    // signed difference of first differing byte
    CHECK(sim::Util_StrCmp("abc", "abd") < 0);
    CHECK(sim::Util_StrCmp("abd", "abc") > 0);
    CHECK(sim::Util_StrCmp("ab", "abc")  < 0);      // prefix shorter
    CHECK(sim::Util_StrCmp("abc", "ab")  > 0);
    CHECK_EQ(static_cast<int>('a') - static_cast<int>('b'),
             sim::Util_StrCmp("a", "b"));
}

TEST(BuildingTypeReconStr, StrCmpNoCaseFoldsUpperToLower) {
    CHECK_EQ(0, sim::Util_StrCmpNoCase("Bauplatz", "bauplatz"));
    CHECK_EQ(0, sim::Util_StrCmpNoCase("ABC", "abc"));
    CHECK(sim::Util_StrCmpNoCase("abc", "abd") != 0);
    // Non-letters are NOT folded: '[' (0x5B) vs '{' (0x7B) differ.
    CHECK(sim::Util_StrCmpNoCase("[", "{") != 0);
}

// ===========================================================================
// Bauplatz_GetSize  (0x577628)
// ===========================================================================
static std::vector<BauplatzSizeRec> MakeTable() {
    std::vector<BauplatzSizeRec> t(3);
    std::memset(t.data(), 0, t.size() * sizeof(BauplatzSizeRec));
    std::strcpy(t[0].name, "Marktplatz");
    t[0].cornerXlo = 10; t[0].cornerY = 5; t[0].cornerZlo = 20;
    t[0].cornerXhi = 110; t[0].cornerZhi = 220;
    std::strcpy(t[1].name, "Schmiede");
    t[1].cornerXlo = 1; t[1].cornerY = 2; t[1].cornerZlo = 3;
    t[1].cornerXhi = 4; t[1].cornerZhi = 6;
    std::strcpy(t[2].name, "Kirche");
    return t;
}

TEST(BuildingTypeReconBauplatz, GetSizeFindsCaseInsensitive) {
    auto t = MakeTable();
    sim::SetBauplatzTable(t.data(), static_cast<int>(t.size()));

    const BauplatzSizeRec* r = sim::Bauplatz_GetSize("marktplatz");  // lower-case
    CHECK(r != nullptr);
    CHECK_EQ(10, r->cornerXlo);
    CHECK_EQ(110, r->cornerXhi);

    r = sim::Bauplatz_GetSize("SCHMIEDE");
    CHECK(r != nullptr);
    CHECK_EQ(1, r->cornerXlo);

    r = sim::Bauplatz_GetSize("Kirche");
    CHECK(r != nullptr);
    CHECK_EQ(&t[2], r);

    sim::SetBauplatzTable(nullptr, 0);
}

TEST(BuildingTypeReconBauplatz, GetSizeMissReturnsNull) {
    auto t = MakeTable();
    sim::SetBauplatzTable(t.data(), static_cast<int>(t.size()));
    CHECK(sim::Bauplatz_GetSize("Rathaus") == nullptr);
    sim::SetBauplatzTable(nullptr, 0);
}

TEST(BuildingTypeReconBauplatz, GetSizeEmptyTableReturnsNull) {
    sim::SetBauplatzTable(nullptr, 0);
    CHECK(sim::Bauplatz_GetSize("Marktplatz") == nullptr);
}

// ===========================================================================
// Bauplatz_MapOneToSupermap  (0x5774b8) — corner assembly order via hook capture
// ===========================================================================
namespace {
struct CaptureMapHooks : sim::IBauplatzMapHooks {
    std::vector<std::array<int, 3>> worlds;
    int rasterFill = -1;
    int rasterCalls = 0;
    std::array<float, 8> quad{};
    void WorldToTile(float* outU, float* outV, const i32 world[3], int) override {
        worlds.push_back({world[0], world[1], world[2]});
        // deterministic projection: u = x, v = z  (lets us verify packing order)
        if (outU) *outU = static_cast<float>(world[0]);
        if (outV) *outV = static_cast<float>(world[2]);
    }
    int RasterizeBauplatzEdge(int, const float q[8], int fill) override {
        ++rasterCalls;
        rasterFill = fill;
        for (int i = 0; i < 8; ++i) quad[i] = q[i];
        return 77;
    }
};
}  // namespace

TEST(BuildingTypeReconBauplatz, MapOneAssemblesFourCornersInOrder) {
    auto t = MakeTable();
    sim::SetBauplatzTable(t.data(), static_cast<int>(t.size()));
    CaptureMapHooks hooks;
    sim::SetBauplatzMapHooks(&hooks);

    int rc = sim::Bauplatz_MapOneToSupermap(/*mapCtx*/123, "Marktplatz");
    CHECK_EQ(77, rc);                       // raster return propagated
    CHECK_EQ(1, hooks.rasterCalls);
    CHECK_EQ(255, hooks.rasterFill);        // fill code 255

    // Exactly four corners in the original's pick order:
    //  (Xlo,Y,Zlo) (Xhi,Y,Zlo) (Xhi,Y,Zhi) (Xlo,Y,Zhi)
    CHECK_EQ(4u, hooks.worlds.size());
    CHECK_EQ(10,  hooks.worlds[0][0]); CHECK_EQ(5, hooks.worlds[0][1]); CHECK_EQ(20,  hooks.worlds[0][2]);
    CHECK_EQ(110, hooks.worlds[1][0]); CHECK_EQ(5, hooks.worlds[1][1]); CHECK_EQ(20,  hooks.worlds[1][2]);
    CHECK_EQ(110, hooks.worlds[2][0]); CHECK_EQ(5, hooks.worlds[2][1]); CHECK_EQ(220, hooks.worlds[2][2]);
    CHECK_EQ(10,  hooks.worlds[3][0]); CHECK_EQ(5, hooks.worlds[3][1]); CHECK_EQ(220, hooks.worlds[3][2]);

    // Quad packing: (u0,v0,u1,v1,u2,v2,u3,v3) == projected (x,z) of each corner.
    CHECK_EQ(10.0f,  hooks.quad[0]); CHECK_EQ(20.0f,  hooks.quad[1]);
    CHECK_EQ(110.0f, hooks.quad[2]); CHECK_EQ(20.0f,  hooks.quad[3]);
    CHECK_EQ(110.0f, hooks.quad[4]); CHECK_EQ(220.0f, hooks.quad[5]);
    CHECK_EQ(10.0f,  hooks.quad[6]); CHECK_EQ(220.0f, hooks.quad[7]);

    sim::SetBauplatzMapHooks(nullptr);
    sim::SetBauplatzTable(nullptr, 0);
}

TEST(BuildingTypeReconBauplatz, MapOneUnknownPlotReturnsZeroNoRaster) {
    auto t = MakeTable();
    sim::SetBauplatzTable(t.data(), static_cast<int>(t.size()));
    CaptureMapHooks hooks;
    sim::SetBauplatzMapHooks(&hooks);

    int rc = sim::Bauplatz_MapOneToSupermap(1, "DoesNotExist");
    CHECK_EQ(0, rc);
    CHECK_EQ(0, hooks.rasterCalls);
    CHECK_EQ(0u, hooks.worlds.size());

    sim::SetBauplatzMapHooks(nullptr);
    sim::SetBauplatzTable(nullptr, 0);
}

// ===========================================================================
// Building_StorageSecurityClamp  (inline math of 0x588988)
// ===========================================================================
TEST(BuildingTypeReconStorage, ClampGateBranchUsesFloor) {
    // lo|hi present -> field28=max(lo,F), field29=max(hi,F); both written.
    auto s = sim::Building_StorageSecurityClamp(/*573*/5, /*574*/1, /*575*/9, /*F*/2, false);
    CHECK_EQ(5, s.field28);   // 5 >= 2
    CHECK_EQ(2, s.field29);   // 1 < 2 -> floor 2
    CHECK(s.wroteField29);

    // Market floor 6.
    auto m = sim::Building_StorageSecurityClamp(/*573*/3, /*574*/8, /*575*/0, /*F*/6, true);
    CHECK_EQ(6, m.field28);   // 3 < 6 -> 6
    CHECK_EQ(8, m.field29);   // 8 >= 6
    CHECK(m.wroteField29);
}

TEST(BuildingTypeReconStorage, ClampZeroLoHiTakesFloorBranch) {
    // b573==0 && b574==0 but b575==0 -> the (b575==0) clause keeps the gate branch.
    auto s = sim::Building_StorageSecurityClamp(0, 0, 0, 2, false);
    CHECK_EQ(2, s.field28);
    CHECK_EQ(2, s.field29);
    CHECK(s.wroteField29);
}

TEST(BuildingTypeReconStorage, ClampStorageSingleBranchNoFloorNoField29) {
    // storage node single branch: field28 = single (NO clamp), field29 untouched.
    auto s = sim::Building_StorageSecurityClamp(/*573*/0, /*574*/0, /*575*/1, /*F*/2, false);
    CHECK_EQ(1, s.field28);          // single, NOT clamped to floor 2
    CHECK(!s.wroteField29);
}

TEST(BuildingTypeReconStorage, ClampMarketSingleBranchClampsToFloor) {
    // market node single branch: field28 = max(single,6), field29 untouched.
    auto lo = sim::Building_StorageSecurityClamp(0, 0, /*575*/3, /*F*/6, true);
    CHECK_EQ(6, lo.field28);         // 3 < 6 -> 6
    CHECK(!lo.wroteField29);
    auto hi = sim::Building_StorageSecurityClamp(0, 0, /*575*/9, /*F*/6, true);
    CHECK_EQ(9, hi.field28);         // 9 >= 6
    CHECK(!hi.wroteField29);
}

// ===========================================================================
// Building_AllocStorageRoom  (0x588988) — full flow through hooks
// ===========================================================================
namespace {
struct StorageHooks : sim::IStorageRoomHooks {
    bool allow = true;
    BuildingTypeDef td{};
    u8 storageNode[64]{};   // kind 42
    u8 marketNode[64]{};    // kind 278/group 6
    void* obj = reinterpret_cast<void*>(0xBEEF);
    std::vector<std::pair<int,int>> findCalls;

    bool CanAllocate() override { return allow; }
    const BuildingTypeDef* TypeRecordFor(u8) override { return &td; }
    void* AddObjekt(i32, i16, int, void*) override { return obj; }
    u8* QueryFind(void*, int kind, int group) override {
        findCalls.push_back({kind, group});
        if (kind == 42)  return storageNode;
        if (kind == 278) return marketNode;
        return nullptr;
    }
};
}  // namespace

TEST(BuildingTypeReconStorage, AllocFailsGuardReturnsNull) {
    StorageHooks h; h.allow = false;
    sim::SetStorageRoomHooks(&h);
    ObjectRec b{}; b.alive = 7; b.id = 42;
    CHECK(sim::Building_AllocStorageRoom(&b, 100, nullptr) == nullptr);
    sim::SetStorageRoomHooks(nullptr);
}

TEST(BuildingTypeReconStorage, AllocWritesBothNodeSecurityBytes) {
    StorageHooks h;
    // Gate bytes at type record +573/+574/+575.
    reinterpret_cast<u8*>(&h.td)[573] = 5;   // lo
    reinterpret_cast<u8*>(&h.td)[574] = 1;   // hi
    reinterpret_cast<u8*>(&h.td)[575] = 0;   // single
    sim::SetStorageRoomHooks(&h);

    ObjectRec b{}; b.alive = 3 /*type idx*/; b.id = 99;
    void* r = sim::Building_AllocStorageRoom(&b, 100, nullptr);
    CHECK_EQ(h.obj, r);

    // Storage node (floor 2): field28=max(5,2)=5, field29=max(1,2)=2.
    CHECK_EQ(5, static_cast<int>(h.storageNode[28]));
    CHECK_EQ(2, static_cast<int>(h.storageNode[29]));
    // Market node (floor 6): field28=max(5,6)=6, field29=max(1,6)=6.
    CHECK_EQ(6, static_cast<int>(h.marketNode[28]));
    CHECK_EQ(6, static_cast<int>(h.marketNode[29]));

    // Both selectors were queried, with the right group ids.
    CHECK_EQ(2u, h.findCalls.size());
    CHECK_EQ(42,  h.findCalls[0].first);  CHECK_EQ(0, h.findCalls[0].second);
    CHECK_EQ(278, h.findCalls[1].first);  CHECK_EQ(6, h.findCalls[1].second);

    sim::SetStorageRoomHooks(nullptr);
}

TEST(BuildingTypeReconStorage, AllocSingleBranchSkipsField29) {
    StorageHooks h;
    reinterpret_cast<u8*>(&h.td)[573] = 0;
    reinterpret_cast<u8*>(&h.td)[574] = 0;
    reinterpret_cast<u8*>(&h.td)[575] = 4;   // single
    h.storageNode[29] = 0xAA;                // sentinel that must NOT change
    h.marketNode[29]  = 0xBB;
    sim::SetStorageRoomHooks(&h);

    ObjectRec b{}; b.alive = 1; b.id = 1;
    sim::Building_AllocStorageRoom(&b, 100, nullptr);

    // Storage node single branch: field28 = 4 (no floor), field29 untouched.
    CHECK_EQ(4, static_cast<int>(h.storageNode[28]));
    CHECK_EQ(0xAA, static_cast<int>(h.storageNode[29]));
    // Market node single branch: field28 = max(4,6)=6, field29 untouched.
    CHECK_EQ(6, static_cast<int>(h.marketNode[28]));
    CHECK_EQ(0xBB, static_cast<int>(h.marketNode[29]));

    sim::SetStorageRoomHooks(nullptr);
}

// ===========================================================================
// Building_PickName  (0x504a54 inner loop)
// ===========================================================================
namespace {
struct NameReg : sim::INameRegistry {
    std::vector<std::string> taken;
    int forcedModulo = 0;
    int lastModuloArg = -1;
    bool IsNameTaken(const char* name) override {
        for (auto& t : taken) if (t == name) return true;
        return false;
    }
    int RandomModulo(int n) override { lastModuloArg = n; return forcedModulo; }
};
}  // namespace

TEST(BuildingTypeReconNames, PickSkipsTakenAndEmpty) {
    const char* cands[] = {"", "Adler", "Loewe", "Baer"};
    NameReg reg;
    reg.taken = {"Adler"};         // Adler in use
    reg.forcedModulo = 0;          // pick first survivor
    int idx = sim::Building_PickName(cands, 4, reg);
    // survivors (in order): Loewe(2), Baer(3).  modulo over 2.
    CHECK_EQ(2, reg.lastModuloArg);
    CHECK_EQ(2, idx);              // survivor 0 -> candidate index 2
}

TEST(BuildingTypeReconNames, PickRespectsModuloIndex) {
    const char* cands[] = {"Eins", "Zwei", "Drei"};
    NameReg reg;
    reg.forcedModulo = 2;          // third survivor
    int idx = sim::Building_PickName(cands, 3, reg);
    CHECK_EQ(3, reg.lastModuloArg);
    CHECK_EQ(2, idx);              // candidate "Drei"
}

TEST(BuildingTypeReconNames, PickRejectsTooLongNames) {
    // 32-char name (>= 0x20) is rejected at collection time.
    std::string longName(32, 'x');
    const char* cands[] = {longName.c_str(), "Ok"};
    NameReg reg;
    int idx = sim::Building_PickName(cands, 2, reg);
    CHECK_EQ(1, reg.lastModuloArg);   // only "Ok" survives
    CHECK_EQ(1, idx);

    // 31-char name (< 0x20) is allowed.
    std::string okName(31, 'y');
    const char* c2[] = {okName.c_str()};
    NameReg reg2;
    int idx2 = sim::Building_PickName(c2, 1, reg2);
    CHECK_EQ(0, idx2);
}

TEST(BuildingTypeReconNames, PickNoSurvivorsReturnsMinusOne) {
    const char* cands[] = {"", ""};
    NameReg reg;
    CHECK_EQ(-1, sim::Building_PickName(cands, 2, reg));

    const char* c2[] = {"Taken"};
    NameReg reg2; reg2.taken = {"Taken"};
    CHECK_EQ(-1, sim::Building_PickName(c2, 1, reg2));
}

// WAVE-11 HARDENING — a `count` far larger than the survivor array (64). Pre-fix
// the survivor count outran the stored array, so RandomModulo(survivorCount) and
// survivors[pick] could index OOB. Now survivorCount is capped at the array size;
// the modulo arg and the chosen index stay in bounds. ASAN exercises the bound.
TEST(BuildingTypeReconNames, PickOversizedCountNoOverrun) {
    // 200 valid, short, unique names — well past the 64-entry survivor array.
    std::vector<std::string> store;
    store.reserve(200);
    std::vector<const char*> cands;
    cands.reserve(200);
    for (int i = 0; i < 200; ++i) {
        store.push_back("n" + std::to_string(i));   // all < 0x20, none taken
        cands.push_back(store.back().c_str());
    }
    NameReg reg;
    reg.forcedModulo = 63;          // last in-bounds survivor slot
    int idx = sim::Building_PickName(cands.data(), 200, reg);
    // The modulo argument is clamped to the array capacity (64), never 200.
    CHECK_EQ(64, reg.lastModuloArg);
    // The returned candidate index is one of the first 64 (the stored survivors).
    CHECK(idx >= 0 && idx < 64);
}
