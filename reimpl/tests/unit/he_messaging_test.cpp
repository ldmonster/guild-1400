// Unit tests for sim/he_messaging — the "He" entity-handler messaging + icon
// cluster (VIBE_He_SendEntityMessage @0x4c5c54, VIBE_He_SendQuickjumpMessage
// @0x4c5d98, VIBE_He_AssignIconForHandler @0x4c6964, VIBE_He_ArrangeIconsInCircle
// @0x4c64bc, VIBE_EventPanel_HandleSlotClick @0x4c5b40).
//
// Golden vectors:
//   * The message-packet header bytes (type 0x11 @+4, sub-kind 9 @+0x36, flags @+0x9C,
//     sender/arg dwords) are pinned from the two builders' stack stores.
//   * The icon-circle layout (x = sin(theta)*R + ax, y=(hi-lo)*0.5+lo, z=cos(theta)*R+az)
//     is pinned from the trig constants get_bytes'd from the binary:
//       flt_61E73C = 0x40C90FDB = 2*PI, dbl_61E744 = 0.5, radius 100.0 (42C80000).
//   * The AssignIconForHandler kind->icon dispatch is exercised per case.
//   * The EventPanel slot toggle (hide old / show+raise new) is recorded via mocks.
// Suite prefix: HeMsg.
#include "test.h"

#include "sim/he_messaging.h"
#include "world/world_history2.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

// ------------------------------------------------------------------ recording mock
struct Capture {
    // queueRequestBuffer28
    bool        queued = false;
    int         queueRet = 0;
    unsigned    totalLen = 0;
    int         secondArg = 0;
    std::string payload;
    unsigned char header[248];
    // error sink
    std::string lastError;
    // icon create
    std::vector<std::string> iconsCreated;
    // object positions (icon circle)
    std::vector<std::array<float, 3>> positions;
    // form/object ops
    std::vector<std::string> formOps;
};
Capture g_cap;

// person table for SendEntityMessage / AssignIcon
struct PersonRec { i32 id; u8 kind; u16 marker; };
std::vector<PersonRec> g_persons;
const void* PersonFind(i32 id) {
    for (auto& p : g_persons) if (p.id == id) return &p;
    return nullptr;
}
u8 PersonKind(const void* rec) { return static_cast<const PersonRec*>(rec)->kind; }

int Queue(const unsigned char* hdr, unsigned len, int second, const char* pay) {
    g_cap.queued = true;
    g_cap.totalLen = len;
    g_cap.secondArg = second;
    g_cap.payload = pay ? pay : "";
    std::memcpy(g_cap.header, hdr, 248);
    return g_cap.queueRet;
}
void ReportErr(const char* t) { g_cap.lastError = t ? t : ""; }

// building/record mock for AssignIcon
struct BuildingRec { i32 id; i32 gfx; };
std::vector<BuildingRec> g_buildings;
const void* BuildingFind(i32 id) {
    for (auto& b : g_buildings) if (b.id == id) return &b;
    return nullptr;
}
int g_iconsOn = 1;
u16 g_localCity = 0;
i32 g_suppress = 0;
i32 g_personFlags = 0;
int IconsEnabled() { return g_iconsOn; }
u16 LocalCity() { return g_localCity; }
i32 Suppress() { return g_suppress; }
i32 PersonFlags(u16) { return g_personFlags; }

// The He record under test: a flat byte buffer the recordDword/Byte/Word read.
unsigned char g_rec[512];
int g_objFindResult = 1;
// The reconstruction narrows record pointers to i32 (the engine's 32-bit handle).
// The mock narrows the same way so handles round-trip within a test run. g_rec is
// the He record itself; its handle is AsI32(g_rec).
i32 AsI32(const void* p) { return static_cast<i32>(reinterpret_cast<std::uintptr_t>(p)); }

i32 RecordDword(i32 base, int off) {
    // base may be the g_rec buffer or a BuildingRec; for the building +97 we resolve
    // through the BuildingRec gfx field.
    for (auto& b : g_buildings) if (AsI32(&b) == base) return off == 97 ? b.gfx : 0;
    if (base == AsI32(g_rec)) { i32 v; std::memcpy(&v, g_rec + off, 4); return v; }
    return 0;
}
// Registered widgets for the event-panel tests: handle -> {kind@+0, anchored@+240}.
struct WidgetRec { i32 handle; u8 kind; u8 anchored; };
std::vector<WidgetRec> g_widgets;

u8 RecordByte(i32 base, int off) {
    if (base == AsI32(g_rec)) return g_rec[off];
    for (auto& w : g_widgets)
        if (w.handle == base) return off == 240 ? w.anchored : (off == 0 ? w.kind : 0);
    return 0;
}
u16 RecordWord(i32 base, int off) {
    for (auto& p : g_persons) if (AsI32(&p) == base) return p.marker;
    if (base == AsI32(g_rec)) { u16 v; std::memcpy(&v, g_rec + off, 2); return v; }
    return 0;
}
int ObjFind(const char*, i32) { return g_objFindResult; }
int CreateGfx(i32, const char* name, const char*) { g_cap.iconsCreated.push_back(name ? name : ""); return 1; }

// geometry mocks
float g_anchor[3] = {10.0f, 0.0f, 20.0f};
void Anchor(const float*, const float*, float* out) { out[0]=g_anchor[0]; out[1]=g_anchor[1]; out[2]=g_anchor[2]; }
void MeshHeight(i32, float* lo, float* hi) { *lo = 1.0f; *hi = 3.0f; } // hi-lo = 2
void SetPos(i32, const float* xyz) { g_cap.positions.push_back({xyz[0], xyz[1], xyz[2]}); }

// form/object mocks
void ObjSetValue(i32 obj, const char* val, int, int, int) {
    g_cap.formOps.push_back(std::string("setval:") + std::to_string(obj) + ":" + (val ? "1" : "0"));
}
void FormVisible(i32 form, int vis) {
    g_cap.formOps.push_back(std::string("vis:") + std::to_string(form) + ":" + std::to_string(vis));
}
void FormRaise(i32 form) { g_cap.formOps.push_back(std::string("raise:") + std::to_string(form)); }
int g_featureSync = 0;
int FeatureSync() { return g_featureSync; }

HeMessagingHooks MakeHooks() {
    HeMessagingHooks h;
    h.personFindById = PersonFind;
    h.personKindByte = PersonKind;
    h.queueRequestBuffer28 = Queue;
    h.reportError = ReportErr;
    h.buildingFindById = BuildingFind;
    h.objectFindByHandle = ObjFind;
    h.createGfxInfo = CreateGfx;
    h.recordDword = RecordDword;
    h.recordByte = RecordByte;
    h.recordWord = RecordWord;
    h.iconsEnabled = IconsEnabled;
    h.localCityMarker = LocalCity;
    h.suppressFlag = Suppress;
    h.personFlagsDword = PersonFlags;
    h.transformAnchor = Anchor;
    h.meshHeightRange = MeshHeight;
    h.objectSetPosition = SetPos;
    h.objectSetValueOrText = ObjSetValue;
    h.formSetObjectsVisible = FormVisible;
    h.formRaiseWindows = FormRaise;
    h.featureSyncBit = FeatureSync;
    return h;
}

void ResetAll() {
    g_cap = Capture{};
    g_persons.clear();
    g_buildings.clear();
    std::memset(g_rec, 0, sizeof(g_rec));
    g_widgets.clear();
    g_iconsOn = 1; g_localCity = 0; g_suppress = 0; g_personFlags = 0;
    g_objFindResult = 1; g_featureSync = 0;
    g_anchor[0]=10.0f; g_anchor[1]=0.0f; g_anchor[2]=20.0f;
    HeMessagingHooks h = MakeHooks();
    SetHeMessagingHooks(&h);
    EventPanel_Reset();
}

i32 RecBase() { return static_cast<i32>(reinterpret_cast<std::uintptr_t>(g_rec)); }

} // namespace

// ===================================================================== messaging
TEST(HeMsg, SendEntityMessageRejectsMissingRecipient) {
    ResetAll();
    CHECK_EQ(He_SendEntityMessage(99, 0, "x", 0, nullptr), -1);
    CHECK(!g_cap.queued);
}

TEST(HeMsg, SendEntityMessageRejectsWrongKind) {
    ResetAll();
    g_persons.push_back({42, 5, 0});   // kind 5, not 6/7
    CHECK_EQ(He_SendEntityMessage(42, 0, "x", 0, nullptr), -1);
    CHECK(!g_cap.queued);
}

TEST(HeMsg, SendEntityMessageBuildsHeaderKind6) {
    ResetAll();
    g_persons.push_back({42, 6, 0});
    g_cap.queueRet = 7;
    int r = He_SendEntityMessage(42, 0x33, "Hi", 0x55, nullptr);
    CHECK_EQ(r, 7);
    CHECK(g_cap.queued);
    CHECK_EQ((int)g_cap.header[4], 0x11);            // type marker
    CHECK_EQ((int)g_cap.header[0x36], 9);            // sub-kind
    i32 sender; std::memcpy(&sender, g_cap.header + 8, 4);
    CHECK_EQ(sender, 42);
    i32 a2; std::memcpy(&a2, g_cap.header + 12, 4);
    CHECK_EQ(a2, 0x33);
    i32 paylen; std::memcpy(&paylen, g_cap.header + 0x58, 4);  // var_AC @+0x58
    CHECK_EQ(paylen, 0x55);
    CHECK_EQ((int)g_cap.header[0x9C], 0);            // no second-string flag (var_68 @+0x9C)
    CHECK_EQ((int)g_cap.totalLen, 3);               // strlen("Hi")+1
    CHECK_EQ(g_cap.secondArg, 0x55);                // a4 (no 2nd string)
}

TEST(HeMsg, SendEntityMessageSecondStringSetsFlag) {
    ResetAll();
    g_persons.push_back({42, 7, 0});                // kind 7 also accepted
    int r = He_SendEntityMessage(42, 0, "ab", 0, "cd");
    CHECK_EQ(r, 0);
    CHECK(g_cap.queued);
    CHECK_EQ((int)g_cap.header[0x9C], 0x10);        // second-string flag (var_68 @+0x9C)
    CHECK_EQ((int)g_cap.totalLen, 3 + 3);           // (len ab+1) + (len cd+1)
    CHECK_EQ(g_cap.secondArg, 2);                   // strlen("cd")+1-1
}

TEST(HeMsg, QuickjumpSetsFlag2AndArgs) {
    ResetAll();
    int r = He_SendQuickjumpMessage(11, 0x22, 0x01, "txt", 0x44, 0x55, 0x66, "Hans", nullptr);
    CHECK_EQ(r, 0);
    CHECK(g_cap.queued);
    CHECK_EQ((int)g_cap.header[4], 0x11);
    i32 a1; std::memcpy(&a1, g_cap.header + 8, 4);  CHECK_EQ(a1, 11);
    i32 a2; std::memcpy(&a2, g_cap.header + 12, 4); CHECK_EQ(a2, 0x22);
    i32 a5; std::memcpy(&a5, g_cap.header + 0x58, 4); CHECK_EQ(a5, 0x44);  // var_B4 @+0x58
    i32 a6; std::memcpy(&a6, g_cap.header + 0x5C, 4); CHECK_EQ(a6, 0x55);  // var_B0 @+0x5C
    i32 a7; std::memcpy(&a7, g_cap.header + 0x60, 4); CHECK_EQ(a7, 0x66);  // var_AC @+0x60
    CHECK_EQ((int)g_cap.header[0x9C], 0x01 | 0x02); // flags | quickjump bit (var_70 @+0x9C)
    CHECK_EQ(g_cap.secondArg, 0x66);               // a7 (no 2nd string)
    CHECK(g_cap.lastError.empty());
}

TEST(HeMsg, QuickjumpContactTooLongReports) {
    ResetAll();
    std::string longName(0x30, 'A');               // strlen+1 == 0x31 > 0x30
    He_SendQuickjumpMessage(1, 2, 0, "t", 0, 0, 0, longName.c_str(), nullptr);
    CHECK(g_cap.lastError.find("too long") != std::string::npos);
}

// ===================================================================== icon circle
TEST(HeMsg, ArrangeIconsCircleLayout) {
    ResetAll();
    world::HeIconPoolReset();
    world::HeIconSlot* pool = world::HeIconPool();
    // Two icons owned by the same parent (mesh field == parentKey).
    float dummyParent[64] = {0};
    i32 parentKey = static_cast<i32>(reinterpret_cast<std::uintptr_t>(dummyParent));
    pool[3].mesh = parentKey; pool[3].node = 1001;
    pool[7].mesh = parentKey; pool[7].node = 1002;

    He_ArrangeIconsInCircle(dummyParent);

    CHECK_EQ((int)g_cap.positions.size(), 2);
    // Golden: N=2, R=100, theta_0=0, theta_1=PI. anchor=(10,0,20). y=(3-1)*0.5+1=2.
    const double R = 100.0, PI = 3.14159265358979323846;
    auto close = [](float a, double b) { return std::fabs((double)a - b) < 0.01; };
    // i=0: x=sin(0)*100+10=10, y=2, z=cos(0)*100+20=120
    CHECK(close(g_cap.positions[0][0], 0.0 * R + 10.0));
    CHECK(close(g_cap.positions[0][1], 2.0));
    CHECK(close(g_cap.positions[0][2], 1.0 * R + 20.0));
    // i=1: x=sin(PI)*100+10~=10, z=cos(PI)*100+20=-80
    CHECK(close(g_cap.positions[1][0], std::sin(PI) * R + 10.0));
    CHECK(close(g_cap.positions[1][2], std::cos(PI) * R + 20.0));
}

TEST(HeMsg, ArrangeSingleIconRadiusZero) {
    ResetAll();
    world::HeIconPoolReset();
    world::HeIconSlot* pool = world::HeIconPool();
    float dummyParent[64] = {0};
    i32 parentKey = static_cast<i32>(reinterpret_cast<std::uintptr_t>(dummyParent));
    pool[5].mesh = parentKey; pool[5].node = 2001;
    He_ArrangeIconsInCircle(dummyParent);
    CHECK_EQ((int)g_cap.positions.size(), 1);
    // radius 0 -> x=anchorX=10, z=anchorZ=20, y=2
    CHECK(std::fabs(g_cap.positions[0][0] - 10.0f) < 0.01f);
    CHECK(std::fabs(g_cap.positions[0][2] - 20.0f) < 0.01f);
    CHECK(std::fabs(g_cap.positions[0][1] - 2.0f) < 0.01f);
}

// ===================================================================== assign icon
TEST(HeMsg, AssignIconDisabledNoOp) {
    ResetAll();
    g_iconsOn = 0;
    He_AssignIconForHandler(RecBase(), 0);
    CHECK_EQ((int)g_cap.iconsCreated.size(), 0);
}

TEST(HeMsg, AssignIconAlreadyHasGfx) {
    ResetAll();
    i32 existing = 1234;
    std::memcpy(g_rec + 136, &existing, 4);          // record+136 != 0
    He_AssignIconForHandler(RecBase(), 0);
    CHECK_EQ((int)g_cap.iconsCreated.size(), 0);
}

TEST(HeMsg, AssignIconKind1CHammerGold) {
    ResetAll();
    g_rec[0] = 0x1C;                                 // kind
    i32 bid = 77; std::memcpy(g_rec + 172, &bid, 4); // +172 building ref
    g_buildings.push_back({77, 0xABCD});             // gfx != 0
    He_AssignIconForHandler(RecBase(), 0);
    CHECK_EQ((int)g_cap.iconsCreated.size(), 1);
    CHECK_EQ(g_cap.iconsCreated[0], std::string("he_hammer_gold"));
}

TEST(HeMsg, AssignIconKind1BMuenze) {
    ResetAll();
    g_rec[0] = 0x1B;
    i32 bid = 88; std::memcpy(g_rec + 172, &bid, 4);
    g_buildings.push_back({88, 0});                  // muenze passes +97 even if 0
    He_AssignIconForHandler(RecBase(), 0);
    CHECK_EQ((int)g_cap.iconsCreated.size(), 1);
    CHECK_EQ(g_cap.iconsCreated[0], std::string("he_muenze"));
}

TEST(HeMsg, AssignIconKind2PlusHammer) {
    ResetAll();
    g_rec[0] = 2;
    i32 bid = 5; std::memcpy(g_rec + 16, &bid, 4);
    g_buildings.push_back({5, 0x10});
    He_AssignIconForHandler(RecBase(), 0);
    CHECK_EQ((int)g_cap.iconsCreated.size(), 1);
    CHECK_EQ(g_cap.iconsCreated[0], std::string("he_plus_hammer"));
}

TEST(HeMsg, AssignIconKind6BTribuneGatedByPhase) {
    ResetAll();
    g_rec[0] = 0x6B;
    i32 phase = 1; std::memcpy(g_rec + 112, &phase, 4);  // phase != 2 -> no icon
    He_AssignIconForHandler(RecBase(), 0);
    CHECK_EQ((int)g_cap.iconsCreated.size(), 0);
    // now phase 2, office bit clear -> ausrufezeichen
    phase = 2; std::memcpy(g_rec + 112, &phase, 4);
    g_objFindResult = 1; g_personFlags = 0;
    He_AssignIconForHandler(RecBase(), 0);
    CHECK_EQ((int)g_cap.iconsCreated.size(), 1);
    CHECK_EQ(g_cap.iconsCreated[0], std::string("he_ausrufezeichen"));
}

// ===================================================================== event panel
struct Slot { i32 widget; i32 valueObj; i32 form; };

TEST(HeMsg, EventPanelDisabledReturnsInput) {
    ResetAll();                                      // panel disabled by default
    Slot s{0, 5, 9};
    void* r = EventPanel_HandleSlotClick(&s, 0, 0, 0);
    CHECK_EQ(r, (void*)&s);
    CHECK_EQ((int)g_cap.formOps.size(), 0);
}

TEST(HeMsg, EventPanelActivatesNewSlot) {
    ResetAll();
    EventPanel_SetEnabled(true);
    i32 wh = 0x9000;                                 // synthetic widget handle
    g_widgets.push_back({wh, /*kind*/5, /*anchored*/0}); // kind 5 -> store & return
    Slot s; s.widget = wh; s.valueObj = 5; s.form = 9;
    EventPanel_HandleSlotClick(&s, 1, 2, 3);
    CHECK_EQ(EventPanel_ActiveSlot(), (void*)&s);
    // show + raise the new form, set its value
    bool sawVis = false, sawRaise = false, sawSet = false;
    for (auto& op : g_cap.formOps) {
        if (op == "vis:9:1") sawVis = true;
        if (op == "raise:9") sawRaise = true;
        if (op == "setval:5:1") sawSet = true;
    }
    CHECK(sawVis); CHECK(sawRaise); CHECK(sawSet);
}

TEST(HeMsg, EventPanelTogglesOldSlotHidden) {
    ResetAll();
    EventPanel_SetEnabled(true);
    i32 wh = 0x9100;
    g_widgets.push_back({wh, /*kind*/5, /*anchored*/0});
    Slot old; old.widget = wh; old.valueObj = 3; old.form = 4;
    EventPanel_SetActiveSlot(&old);

    Slot fresh; fresh.widget = 0; fresh.valueObj = -1; fresh.form = 8;
    EventPanel_HandleSlotClick(&fresh, 0, 0, 0);
    // old form 4 hidden, its value reset to 0
    bool oldHidden = false, oldReset = false, newShown = false;
    for (auto& op : g_cap.formOps) {
        if (op == "vis:4:0") oldHidden = true;
        if (op == "setval:3:0") oldReset = true;
        if (op == "vis:8:1") newShown = true;
    }
    CHECK(oldHidden); CHECK(oldReset); CHECK(newShown);
}
