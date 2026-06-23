// ===========================================================================
// Golden-vector unit tests for src/play/input_recon_select.{h,cpp}.
//   VIBE_Input_PollMouseAndKeyboard (0x40da88), VIBE_SelectEntity_ComputeResult
//   (0x4147cc), VIBE_Selection_Reset (0x4b9444), VIBE_Coord_ConvertX (0x5c6b08).
// Self-contained: every coupled subsystem (entity arrays, query iterators,
// projection) is supplied as a synthetic in-test hook.
// ===========================================================================
#include "tests/framework/test.h"
#include "play/input_recon_select.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::play;

// ---------------------------------------------------------------------------
// Coord_ConvertX — VIBE_Coord_ConvertX @0x5c6b08.  Verified from the disasm
// (wave-15): the function forces the control word's high byte to 0x1F (CW =
// 0x1F7F), so RC = bits 11:10 = 0b11 = ROUND TOWARD ZERO, then frndint.  Hence
// ConvertX(x) == trunc(x) (NOT round-to-nearest — the earlier golden was wrong).
// Matches the canonical util::ConvertX recon (src/util/coord.cpp).
// ---------------------------------------------------------------------------
TEST(InputReconSelectCoord, TruncateTowardZero) {
    CHECK_EQ((int)Coord_ConvertX(2.4), 2);
    CHECK_EQ((int)Coord_ConvertX(2.6), 2);
    CHECK_EQ((int)Coord_ConvertX(-2.4), -2);
    CHECK_EQ((int)Coord_ConvertX(-2.6), -2);
    // Toward-zero truncation: .5 cases all drop the fraction.
    CHECK_EQ((int)Coord_ConvertX(0.5), 0);
    CHECK_EQ((int)Coord_ConvertX(1.5), 1);
    CHECK_EQ((int)Coord_ConvertX(2.5), 2);
    CHECK_EQ((int)Coord_ConvertX(3.5), 3);
    CHECK_EQ((int)Coord_ConvertX(-0.9), 0);
    CHECK_EQ((int)Coord_ConvertX(-1.999), -1);
}

// ---------------------------------------------------------------------------
// PollMouseAndKeyboard — orchestrator sequencing + return of the keyboard byte.
// ---------------------------------------------------------------------------
TEST(InputReconSelectPoll, OrchestratorOrderAndReturn) {
    std::vector<int> order;
    InputPollHooks h;
    h.pollMouseDevice  = [&]() { order.push_back(1); };
    h.copyPacket       = [&](void*, const void*, int bytes) {
        order.push_back(2);
        CHECK_EQ(bytes, 0x4C);   // the 0x4C-byte cursor packet width
    };
    h.pollKeyboardDevice = [&]() -> u8 { order.push_back(3); return 0x9C; };
    Input_SetPollHooks(h);

    u8 ret = Input_PollMouseAndKeyboard();
    CHECK_EQ((int)ret, 0x9C);
    CHECK_EQ((int)order.size(), 3);
    CHECK_EQ(order[0], 1);   // mouse poll first
    CHECK_EQ(order[1], 2);   // then packet copy
    CHECK_EQ(order[2], 3);   // then keyboard poll
}

TEST(InputReconSelectPoll, InertDefaultsReturnZero) {
    Input_SetPollHooks(InputPollHooks{});   // clear hooks
    CHECK_EQ((int)Input_PollMouseAndKeyboard(), 0);
}

// ---------------------------------------------------------------------------
// SelectEntity_ComputeResult — synthetic actor / animation / hotspot tables.
// ---------------------------------------------------------------------------
namespace {

// A 0x2AC-byte actor record (171 dwords).  Helper offsets match the original.
struct ActorRec {
    u8 b[0x2AC];
    ActorRec() { std::memset(b, 0, sizeof(b)); }
    void setLive()        { *reinterpret_cast<i32*>(b + 400) = 1;   // v5[100]
                            *reinterpret_cast<i32*>(b + 408) = 1;   // v5[102]
                            *reinterpret_cast<i32*>(b + 412) = 1; } // v5[103]
    void setName0()       { b[428] = 'A'; }                         // *((BYTE*)v5+428)
    void setListCount(i32 n) { *reinterpret_cast<i32*>(b + 388) = n; }
    void setListId(int i, i32 id) { *reinterpret_cast<i32*>(b + 4 * i + 4) = id; }
};

// Animation record: +116 is the u32 scale; +0 is the name byte-string.
struct AnimRec {
    u8 b[256];
    AnimRec() { std::memset(b, 0, sizeof(b)); }
    void setScale(u32 s) { *reinterpret_cast<u32*>(b + 116) = s; }
    // NB: plain copy (NOT strncpy — strncpy zero-pads and would wipe +116).
    void setName(const char* n) { std::strcpy(reinterpret_cast<char*>(b), n); }
};

// Hotspot list record — decoded form (the hook supplies count + id array; see
// the header note on the packed +24/+26 overlap that cannot hold a 64-bit ptr).
struct ListRec {
    int count = 0;
    i32 ids[16];
    ListRec() { count = 0; std::memset(ids, 0, sizeof(ids)); }
    void setCount(int n) { count = n; }
};

// Hotspot rect record (740 bytes): 16.16 rect at +14/+16/+18/+20; type +24;
// action +8; secondary-ptr +44; type-64 secondary +116.
struct RectRec {
    u8 b[740];
    i32 secondary;
    RectRec() { std::memset(b, 0, sizeof(b)); secondary = 0; }
    // The original reads 16.16 fixed via misaligned dword+shift: x = dword@14>>16
    // (= word@16), y = dword@16>>16 (= word@18), w = dword@18>>16 (= word@20),
    // hgt = dword@20>>16 (= word@22).  Set the underlying 16-bit words directly.
    void setRect(int x, int w, int y, int hgt) {
        *reinterpret_cast<i16*>(b + 16) = static_cast<i16>(x);
        *reinterpret_cast<i16*>(b + 18) = static_cast<i16>(y);
        *reinterpret_cast<i16*>(b + 20) = static_cast<i16>(w);
        *reinterpret_cast<i16*>(b + 22) = static_cast<i16>(hgt);
    }
    void setType(u8 t)   { b[24] = t; }
    void setAction(i32 a){ *reinterpret_cast<i32*>(b + 8) = a; }
    void setSecondaryPtr(i32 v) { secondary = v;
        *reinterpret_cast<i32**>(b + 44) = &secondary; }
    void setType64Secondary(i32 v) { *reinterpret_cast<i32*>(b + 116) = v; }
};

} // namespace

TEST(InputReconSelectEntity, NoActorsReturnsMinusOne) {
    SelectEntity_SetHooks(SelectEntityHooks{});  // no actorRecord -> nothing live
    SelectEntityResult r;
    // The actor-exhaustion exit (++v4 >= 48) returns -1 WITHOUT writing
    // dword_62D22C — only the per-actor no-hotspot-match path sets it to -1.
    CHECK_EQ(SelectEntity_ComputeResult(10, 10, &r), -1);
    CHECK_EQ(r.pickedLabelId, 0);   // left at its initial 0 (faithful)
}

TEST(InputReconSelectEntity, HitYieldsActionAndIds) {
    static ActorRec actor;  actor = ActorRec(); actor.setLive(); actor.setName0();
    actor.setListCount(1); actor.setListId(0, 0);

    static AnimRec anim; anim = AnimRec(); anim.setScale(1); anim.setName("Bob");

    static ListRec list; list = ListRec(); list.setCount(1); list.ids[0] = 7;

    static RectRec rect; rect = RectRec();
    rect.setRect(/*x*/0, /*w*/100, /*y*/0, /*h*/100);
    rect.setType(1); rect.setAction(0x55); rect.setSecondaryPtr(0xABCD);

    SelectEntityHooks h;
    h.actorRecord = [](int i) -> u8* { return i == 0 ? actor.b : nullptr; };
    h.animationGetPtr = [](const u8*, int) -> u8* { return anim.b; };
    h.computeSelectionVolume = [](float, float, int, u8*, float* u, float* v, int* flag) {
        *u = 30.0f; *v = 40.0f; *flag = 0; return 1;
    };
    h.hotspotListRecord = [](int id, int* c, const i32** a) -> bool {
        if (id != 0) return false;
        *c = list.count; *a = list.ids; return true;
    };
    h.hotspotRect = [](int id) -> u8* { return id == 7 ? rect.b : nullptr; };
    SelectEntity_SetHooks(h);

    SelectEntityResult r;
    // scale=1, u=30 -> v31=30 ; v=40 -> v32=40 ; inside rect (0..100, 0..100).
    i32 action = SelectEntity_ComputeResult(11, 22, &r);
    CHECK_EQ(action, 0x55);
    CHECK_EQ(r.pickedLabelId, 7);
    CHECK_EQ(r.pickedSecondary, 0xABCD);
    CHECK_EQ(r.pointX, 30);
    CHECK_EQ(r.pointY, 40);
    CHECK(std::strcmp(r.name, "Bob") == 0);
}

TEST(InputReconSelectEntity, PointOutsideRectMisses) {
    static ActorRec actor2; actor2 = ActorRec(); actor2.setLive(); actor2.setName0();
    actor2.setListCount(1); actor2.setListId(0, 0);
    static AnimRec anim2; anim2 = AnimRec(); anim2.setScale(1);
    static ListRec list2; list2 = ListRec(); list2.setCount(1); list2.ids[0] = 7;
    static RectRec rect2; rect2 = RectRec(); rect2.setRect(0, 10, 0, 10); rect2.setType(1);
    rect2.setAction(0x55);

    SelectEntityHooks h;
    h.actorRecord = [](int i) -> u8* { return i == 0 ? actor2.b : nullptr; };
    h.animationGetPtr = [](const u8*, int) -> u8* { return anim2.b; };
    h.computeSelectionVolume = [](float, float, int, u8*, float* u, float* v, int* f) {
        *u = 500.0f; *v = 500.0f; *f = 0; return 1;   // far outside the 10x10 rect
    };
    h.hotspotListRecord = [](int id, int* c, const i32** a) -> bool {
        if (id != 0) return false;
        *c = list2.count; *a = list2.ids; return true;
    };
    h.hotspotRect = [](int id) -> u8* { return id == 7 ? rect2.b : nullptr; };
    SelectEntity_SetHooks(h);

    SelectEntityResult r;
    CHECK_EQ(SelectEntity_ComputeResult(0, 0, &r), -1);
    CHECK_EQ(r.pickedLabelId, -1);
}

TEST(InputReconSelectEntity, Type64StoresSecondaryAndKeepsScanning) {
    // Two entries: first is type-64 (sets secondary, no return), second hits.
    static ActorRec actor3; actor3 = ActorRec(); actor3.setLive(); actor3.setName0();
    actor3.setListCount(1); actor3.setListId(0, 0);
    static AnimRec anim3; anim3 = AnimRec(); anim3.setScale(1);
    static ListRec list3; list3 = ListRec(); list3.setCount(2);
    list3.ids[0] = 1; list3.ids[1] = 2;

    static RectRec r64; r64 = RectRec(); r64.setRect(0, 100, 0, 100);
    r64.setType(64); r64.setType64Secondary(0x1111);
    static RectRec rhit; rhit = RectRec(); rhit.setRect(0, 100, 0, 100);
    rhit.setType(1); rhit.setAction(0x77);

    SelectEntityHooks h;
    h.actorRecord = [](int i) -> u8* { return i == 0 ? actor3.b : nullptr; };
    h.animationGetPtr = [](const u8*, int) -> u8* { return anim3.b; };
    h.computeSelectionVolume = [](float, float, int, u8*, float* u, float* v, int* f) {
        *u = 10.0f; *v = 10.0f; *f = 0; return 1;
    };
    h.hotspotListRecord = [](int id, int* c, const i32** a) -> bool {
        if (id != 0) return false;
        *c = list3.count; *a = list3.ids; return true;
    };
    h.hotspotRect = [](int id) -> u8* {
        if (id == 1) return r64.b;
        if (id == 2) return rhit.b;
        return nullptr;
    };
    SelectEntity_SetHooks(h);

    SelectEntityResult r;
    i32 action = SelectEntity_ComputeResult(0, 0, &r);
    CHECK_EQ(action, 0x77);
    CHECK_EQ(r.pickedLabelId, 2);
    CHECK_EQ(r.pickedSecondary, 0x1111);   // set by the type-64 entry before the hit
}

TEST(InputReconSelectEntity, HalfTileFlagAdds256ToY) {
    static ActorRec a4; a4 = ActorRec(); a4.setLive(); a4.setName0();
    a4.setListCount(1); a4.setListId(0, 0);
    static AnimRec an4; an4 = AnimRec(); an4.setScale(1);
    static ListRec l4; l4 = ListRec(); l4.setCount(1); l4.ids[0] = 0;
    static RectRec rc4; rc4 = RectRec(); rc4.setRect(0, 1000, 0, 1000); rc4.setType(1);
    rc4.setAction(9);

    SelectEntityHooks h;
    h.actorRecord = [](int i) -> u8* { return i == 0 ? a4.b : nullptr; };
    h.animationGetPtr = [](const u8*, int) -> u8* { return an4.b; };
    h.computeSelectionVolume = [](float, float, int, u8*, float* u, float* v, int* f) {
        *u = 5.0f; *v = 5.0f; *f = 1; return 1;   // half-tile flag -> +256 on Y
    };
    h.hotspotListRecord = [](int id, int* c, const i32** a) -> bool {
        if (id != 0) return false;
        *c = l4.count; *a = l4.ids; return true;
    };
    h.hotspotRect = [](int) -> u8* { return rc4.b; };
    SelectEntity_SetHooks(h);

    SelectEntityResult r;
    CHECK_EQ(SelectEntity_ComputeResult(0, 0, &r), 9);
    CHECK_EQ(r.pointX, 5);
    CHECK_EQ(r.pointY, 5 + 256);
}

// ---------------------------------------------------------------------------
// Selection_Reset — anchor clearing, owner resolution, child flag-bit clearing.
// ---------------------------------------------------------------------------
namespace {
// Game-object record: +0 is the i16 id; +32 is the flags byte; +93 is the
// object id (for the person record path).
struct ObjRec {
    u8 b[128];
    ObjRec() { std::memset(b, 0, sizeof(b)); }
    void setId(i16 id) { *reinterpret_cast<i16*>(b) = id; }
    void setFlags(u8 f){ b[32] = f; }
    void setOwnerObj(i32 o){ *reinterpret_cast<i32*>(b + 93) = o; }
};
} // namespace

TEST(InputReconSelectReset, ClearsAnchorsAndChildFlagBit) {
    // Reset module state.
    g_selectionOwnerA = 0;
    g_selectionOwnerB = nullptr;
    g_selectionAnchors = SelectionAnchors{};
    g_selectionAnchors.g11BC274 = 0xDEAD;   // dirty -> must be cleared
    g_selectionAnchors.g631740  = 0xBEEF;

    static ObjRec person; person = ObjRec(); person.setOwnerObj(0x40);

    static ObjRec child0; child0 = ObjRec(); child0.setId(1); child0.setFlags(0xFF);
    static ObjRec child1; child1 = ObjRec(); child1.setId(2); child1.setFlags(0xFF);
    static int iterStep;
    iterStep = 0;

    SelectionResetHooks h;
    h.personQueryBegin = [](int) -> u8* { return person.b; };
    h.gameObjectQueryFind = [](int objId) -> u8* {
        CHECK_EQ(objId, 0x40);   // read from person+93
        iterStep = 0;
        return child0.b;
    };
    h.gameObjectIterNext = []() -> u8* {
        ++iterStep;
        if (iterStep == 1) return child1.b;
        return nullptr;
    };
    // child id 1 -> type 29 (cleared); child id 2 -> type 5 (untouched).
    h.objectTypeByte = [](int objId) -> u8 { return objId == 1 ? 29 : 5; };
    Selection_SetResetHooks(h);

    Selection_Reset(0x1234);

    CHECK_EQ(g_selectionAnchors.g11BC274, 0);
    CHECK_EQ(g_selectionAnchors.g631740, 0);
    CHECK_EQ((int)g_selectionAnchors.g6317B4, 0);
    // child0 (type 29): bit 2 cleared -> 0xFF & ~2 == 0xFD.
    CHECK_EQ((int)child0.b[32], 0xFD);
    // child1 (type 5): untouched -> 0xFF.
    CHECK_EQ((int)child1.b[32], 0xFF);
}

TEST(InputReconSelectReset, PrefersLatchedOwnerB) {
    g_selectionOwnerA = 0;
    static ObjRec ownerB; ownerB = ObjRec(); ownerB.setOwnerObj(0x99);
    g_selectionOwnerB = ownerB.b;
    g_selectionAnchors = SelectionAnchors{};

    static bool queryBeginCalled; queryBeginCalled = false;
    SelectionResetHooks h;
    h.personQueryBegin = [](int) -> u8* { queryBeginCalled = true; return nullptr; };
    h.gameObjectQueryFind = [](int objId) -> u8* {
        CHECK_EQ(objId, 0x99);   // from ownerB+93, NOT from a fresh query
        return nullptr;
    };
    h.gameObjectIterNext = []() -> u8* { return nullptr; };
    h.objectTypeByte = [](int) -> u8 { return 0; };
    Selection_SetResetHooks(h);

    Selection_Reset(0);
    CHECK(!queryBeginCalled);   // ownerB present -> QueryBegin NOT called

    // restore
    g_selectionOwnerB = nullptr;
}

TEST(InputReconSelectReset, NoOwnerReturnsNull) {
    g_selectionOwnerA = 0;
    g_selectionOwnerB = nullptr;
    g_selectionAnchors = SelectionAnchors{};
    SelectionResetHooks h;
    h.personQueryBegin = [](int) -> u8* { return nullptr; };  // no person found
    Selection_SetResetHooks(h);
    CHECK(Selection_Reset(0) == nullptr);
}
