#include "test.h"

// Integration: wire character_render5 against TWO real reconstructed siblings,
// exactly as the live engine wiring does — NOT mocks.
//
//   1) SetCameraViewMode's string comparator is VIBE_Util_StrCmp (gilde.exe
//      0x5d3f10). The real reconstruction of that exact function is
//      guild::render::AnimStrCmp (src/render/animation_playback_unowned_stubs.cpp).
//      We forward CharRender5Hooks.strCmp -> render::AnimStrCmp and assert the
//      camera-mode dispatch agrees with the real byte-compare's verdict.
//
//   2) CmdGetCharacterHandle / CountByType resolve a character/person by id; the
//      engine routes that through VIBE_Person_FindRecordById (gilde.exe 0x58bc6c),
//      reconstructed as guild::sim::PersonFindRecordById (src/sim/entity.cpp) over
//      the real g_persons / g_personIds arrays. We forward findByName/role resolution
//      through PersonFindRecordById and assert the cross-module lookup flows end to
//      end against the real linear-scan record store.
#include "sim/character_render5.h"
#include "sim/entity.h"                  // REAL sibling: PersonFindRecordById + arrays
#include "render/animation_playback.h"   // REAL sibling: AnimStrCmp == VIBE_Util_StrCmp

#include <cstring>
#include <cstdint>
#include <cstdlib>

using namespace guild;
using namespace guild::sim;

namespace {

int g_camMode;
void CamHook(void*, int m) { g_camMode = m; }
void ErrHook(const char*) {}

// REAL wiring: the camera dispatch's string comparator is the actual reconstructed
// VIBE_Util_StrCmp (render::AnimStrCmp). 0 == equal, exactly as the original.
int RealStrCmp(const char* a, const char* b) { return render::AnimStrCmp(a, b); }

} // namespace

TEST(CharRender5Itest, CameraDispatchViaRealStrCmp) {
    CharRender5Hooks h{};
    h.setupAttachCamera = CamHook;
    h.scriptError = ErrHook;
    h.strCmp = RealStrCmp;            // <-- real sibling, not a mock
    SetCharRender5Hooks(&h);

    int actor = 1;
    g_camMode = -1;
    // exact, case-sensitive matches the way VIBE_Util_StrCmp decides.
    CHECK_EQ(SetCameraViewMode(actor, &actor, "CLOSEUP"), 0);
    CHECK_EQ(g_camMode, 0);
    CHECK_EQ(SetCameraViewMode(actor, &actor, "EGO"), 0);
    CHECK_EQ(g_camMode, 3);

    // the real comparator is case-SENSITIVE: lowercase must NOT match -> no change.
    g_camMode = 99;
    CHECK_EQ(SetCameraViewMode(actor, &actor, "ego"), 0);
    CHECK_EQ(g_camMode, 99);          // unchanged (real StrCmp != 0)

    SetCharRender5Hooks(nullptr);
}

namespace {

// REAL wiring: resolve a "character handle" by id through the real
// VIBE_Person_FindRecordById over the real g_persons store.
void* FindByNameViaRecords(const char* name) {
    // The script "name" here is the decimal id; the live engine resolves the named
    // character to an id and finds its record. We exercise the real id->record scan.
    if (!name) return nullptr;
    int id = std::atoi(name);
    return PersonFindRecordById(id);   // <-- real sibling, real linear scan
}

CountByTypeDefs g_defs;
u8 RoleViaRecords(int personId, CountByTypeDefs* defs) {
    *defs = g_defs;
    Person* p = PersonFindRecordById(personId);   // <-- real sibling
    if (!p) return 0xFF;
    return p->kind;                                // the resolved role byte
}

} // namespace

TEST(CharRender5Itest, HandleAndCountViaRealRecordStore) {
    // Populate the REAL person arrays with a couple of live records.
    ResetEntityArrays();
    g_persons[0].marker = 0;  g_persons[0].id = 100; g_persons[0].kind = 3; g_personIds[0] = 100;
    g_persons[1].marker = 0;  g_persons[1].id = 200; g_persons[1].kind = 7; g_personIds[1] = 200;
    g_persons[2].marker = -1; g_persons[2].id = 300; g_personIds[2] = 300; // free slot

    CharRender5Hooks h{};
    h.findByName = FindByNameViaRecords;
    h.scriptError = ErrHook;
    SetCharRender5Hooks(&h);

    // CmdGetCharacterHandle resolves "100" -> &g_persons[0] via the real scan.
    void* found = CmdGetCharacterHandle("100");
    CHECK(found == static_cast<void*>(&g_persons[0]));
    // an unknown id resolves to null (and would emit the script error).
    CHECK(CmdGetCharacterHandle("999") == nullptr);
    // the free slot's id (300) is skipped by the real scan -> null.
    CHECK(CmdGetCharacterHandle("300") == nullptr);

    // CountByType buckets by the real records' kind byte.
    g_defs.wantA = 3;   // bucket A == kind 3 (person 100)
    g_defs.wantB = 7;   // bucket B == kind 7 (person 200)
    int ids[3] = { 100, 200, -1 };
    int outA = -1, outB = -1;
    int scanned = CountByType(ids, 3, RoleViaRecords, &outA, &outB);
    CHECK_EQ(scanned, 3);
    CHECK_EQ(outA, 1);   // person 100 (kind 3)
    CHECK_EQ(outB, 1);   // person 200 (kind 7)

    SetCharRender5Hooks(nullptr);
    ResetEntityArrays();
}
