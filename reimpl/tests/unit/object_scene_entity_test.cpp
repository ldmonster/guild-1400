#include "test.h"

// Unit tests for object_scene_entity (Wave 30 P6): the still-deferred
// deterministic VIBE_Entity_* / VIBE_Transform_* / VIBE_GameObject_* leaves.
// Golden values are derived directly from the Hex-Rays semantics (verbatim field
// offsets / constants). Float goldens were computed in python with explicit
// struct.pack('<f') truncation (see report). All cross-module leaves go through
// captor hooks so behaviour is deterministic and ASLR-independent.
#include "sim/object_scene_entity.h"
#include "sim/entity.h"
#include "sim/person.h"
#include "sim/types.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

// Seed g_persons[idx] with marker / live-actor(+8) / kind(+2).
void Seed(int idx, i16 marker, u8 livePlayer, u8 kind) {
    std::memset(&g_persons[idx], 0, sizeof(Person));
    g_persons[idx].marker = marker;
    PersonSetByte(&g_persons[idx], kPfIsPlayer, livePlayer);
    PersonSetByte(&g_persons[idx], kPfKind, kind);
}

}  // namespace

// ---------------------------------------------------------------------------
// Group A — entity type predicates (0x4f8e9c / 0x4f8ee4 / 0x4f8f2c)
// ---------------------------------------------------------------------------
TEST(ObjSceneEntity, IsAnimalTypeKindSet) {
    // gate: marker!=-1 && +8!=0 && kind in {5,6,7}.
    Seed(0, 4, 1, 5); CHECK(EntityIsAnimalType(0));
    Seed(0, 4, 1, 6); CHECK(EntityIsAnimalType(0));
    Seed(0, 4, 1, 7); CHECK(EntityIsAnimalType(0));
    Seed(0, 4, 1, 4); CHECK(!EntityIsAnimalType(0));   // 4 not in set
    Seed(0, 4, 1, 8); CHECK(!EntityIsAnimalType(0));
    Seed(0, 4, 1, 30); CHECK(!EntityIsAnimalType(0));
    // gate failures
    Seed(0, -1, 1, 6); CHECK(!EntityIsAnimalType(0));  // free slot
    Seed(0, 4, 0, 6);  CHECK(!EntityIsAnimalType(0));  // not live actor
}

TEST(ObjSceneEntity, IsPersonTypeKindSet) {
    // gate + kind<10 && kind!=6 && kind!=7.
    for (u8 k : {0u, 1u, 2u, 3u, 4u, 5u, 8u, 9u}) {
        Seed(1, 2, 1, (u8)k);
        CHECK(EntityIsPersonType(1));
    }
    Seed(1, 2, 1, 6);  CHECK(!EntityIsPersonType(1));  // carried excluded
    Seed(1, 2, 1, 7);  CHECK(!EntityIsPersonType(1));
    Seed(1, 2, 1, 10); CHECK(!EntityIsPersonType(1));  // >=10 excluded
    Seed(1, 2, 1, 30); CHECK(!EntityIsPersonType(1));
    Seed(1, -1, 1, 1); CHECK(!EntityIsPersonType(1));  // free
    Seed(1, 2, 0, 1);  CHECK(!EntityIsPersonType(1));  // not live
}

TEST(ObjSceneEntity, IsCarriedTypeKindSet) {
    Seed(2, 3, 1, 6); CHECK(EntityIsCarriedType(2));
    Seed(2, 3, 1, 7); CHECK(EntityIsCarriedType(2));
    Seed(2, 3, 1, 5); CHECK(!EntityIsCarriedType(2));  // 5 is animal, not carried
    Seed(2, 3, 1, 8); CHECK(!EntityIsCarriedType(2));
    Seed(2, -1, 1, 6); CHECK(!EntityIsCarriedType(2));
    Seed(2, 3, 0, 6);  CHECK(!EntityIsCarriedType(2));
}

// The three predicates classify the kind space at a live actor. They are NOT a
// clean partition: animal={5,6,7}, carried={6,7} (subset of animal), person=
// {0..9}\{6,7} — so kind 5 is BOTH animal AND person (verbatim from the binary).
TEST(ObjSceneEntity, PredicatesClassifyKindSpace) {
    for (int k = 0; k < 31; ++k) {
        Seed(3, 1, 1, (u8)k);
        bool a = EntityIsAnimalType(3);
        bool p = EntityIsPersonType(3);
        bool c = EntityIsCarriedType(3);
        bool expA = (k == 5 || k == 6 || k == 7);
        bool expC = (k == 6 || k == 7);
        bool expP = (k < 10 && k != 6 && k != 7);
        CHECK_EQ((int)a, (int)expA);
        CHECK_EQ((int)c, (int)expC);
        CHECK_EQ((int)p, (int)expP);
        // carried is a strict subset of animal.
        if (c) CHECK(a);
    }
}

// ---------------------------------------------------------------------------
// Group B — selection list (0x4f8e1c)
// ---------------------------------------------------------------------------
TEST(ObjSceneEntity, SelectionListEmptyIsNotIn) {
    EntityClearSelectionList();
    CHECK_EQ(EntityIsNotInSelectionList(42), 1);   // empty (all 0) -> 42 absent
    CHECK_EQ(EntityIsNotInSelectionList(0), 0);     // 0 present (all entries 0)
}

TEST(ObjSceneEntity, SelectionListEvenProbeQuirk) {
    EntityClearSelectionList();
    // The function only probes EVEN offsets {i, i+2, ..., i+14} of each 17-dword
    // group (groups at 0,17,34,51). Put an id at an ODD offset -> NOT found.
    EntitySetSelectionEntry(1, 777);   // odd offset within group 0 -> skipped
    CHECK_EQ(EntityIsNotInSelectionList(777), 1);   // not found (odd offset)
    EntityClearSelectionList();
    EntitySetSelectionEntry(2, 888);   // even offset i+2 in group 0 -> probed
    CHECK_EQ(EntityIsNotInSelectionList(888), 0);   // found
    EntityClearSelectionList();
    EntitySetSelectionEntry(34, 999);  // head of group 3 (i=34) -> probed
    CHECK_EQ(EntityIsNotInSelectionList(999), 0);
    EntityClearSelectionList();
    EntitySetSelectionEntry(48, 555);  // 34+14 even in group 3 -> probed
    CHECK_EQ(EntityIsNotInSelectionList(555), 0);
    EntitySetSelectionEntry(33, 444);  // 16+17 == odd offset in group 1 -> skipped
    CHECK_EQ(EntityIsNotInSelectionList(444), 1);
}

// ---------------------------------------------------------------------------
// Group B — ScaleField148 (0x5b839c) + AnimationUpdate (0x4244f8)
// ---------------------------------------------------------------------------
TEST(ObjSceneEntity, ScaleField148Multiplies) {
    char node[160] = {0};
    *reinterpret_cast<float*>(node + 148) = 4.0f;
    float scale = 2.5f;
    CHECK_EQ((int)EntityScaleField148(node, &scale), 1);
    CHECK_EQ(*reinterpret_cast<float*>(node + 148), 10.0f);
    // identity scale
    float one = 1.0f;
    EntityScaleField148(node, &one);
    CHECK_EQ(*reinterpret_cast<float*>(node + 148), 10.0f);
}

TEST(ObjSceneEntity, AnimationUpdateRectFields) {
    i32 rec[16] = {0};
    // a1=eax, a2=edx, a3=ecx, a4=ebx: rec[9]=a1; rec[10]=a2; rec[11]=a1+a4; rec[12]=a3+a2.
    int ret = EntityAnimationUpdate(/*a1*/10, /*a2*/20, /*a3*/100, /*a4*/5, rec);
    CHECK_EQ(rec[9], 10);
    CHECK_EQ(rec[10], 20);
    CHECK_EQ(rec[11], 15);    // 10 + 5
    CHECK_EQ(rec[12], 120);   // 100 + 20
    CHECK_EQ(ret, 15);        // returns a1 + a4
    // null rec: returns a1 unchanged (no += a4), writes nothing
    CHECK_EQ(EntityAnimationUpdate(7, 0, 0, 99, nullptr), 7);
}

// ---------------------------------------------------------------------------
// Group C — Transform pivot (0x5c8de8). Golden from python f32 truncation.
// ---------------------------------------------------------------------------
TEST(ObjSceneEntity, PivotToFrameSpaceIdentity) {
    float frame[140] = {0};
    frame[99] = 1.0f; frame[104] = 1.0f; frame[109] = 1.0f;   // identity 3x3
    frame[27] = 2.0f; frame[28] = 3.0f; frame[29] = 4.0f;     // +108 (cancels)
    frame[30] = 10.0f; frame[31] = 20.0f; frame[32] = 30.0f;  // +120 final translate
    // frame[132] (byte 528) high bit clear -> pivot branch.
    reinterpret_cast<signed char*>(frame)[528] = 0;

    float pivot[140] = {0};
    pivot[99] = 1.0f; pivot[104] = 1.0f; pivot[109] = 1.0f;   // identity 3x3
    pivot[19] = 1.0f; pivot[20] = 2.0f; pivot[21] = 3.0f;     // translation A
    pivot[30] = 0.5f; pivot[31] = 0.5f; pivot[32] = 0.5f;     // translation B

    float in[3] = {5.0f, 6.0f, 7.0f};
    float out[3] = {0};
    float* r = TransformPointPivotToFrameSpace(frame, pivot, out, in);
    CHECK(r == out);
    // world local = [15,26,37]; minus [1,2,3] minus [0.5,0.5,0.5] = [13.5,23.5,33.5]
    CHECK_EQ(out[0], 13.5f);
    CHECK_EQ(out[1], 23.5f);
    CHECK_EQ(out[2], 33.5f);
}

TEST(ObjSceneEntity, PivotFallthroughOnSignByteOrNullPivot) {
    float frame[140] = {0};
    frame[99] = 1.0f; frame[104] = 1.0f; frame[109] = 1.0f;
    frame[30] = 10.0f; frame[31] = 20.0f; frame[32] = 30.0f;
    float in[3] = {5.0f, 6.0f, 7.0f};
    float out[3] = {0};

    // null pivot -> bone-chain-only path: out = in + frame[30..32] (no parent).
    reinterpret_cast<signed char*>(frame)[528] = 0;
    TransformPointPivotToFrameSpace(frame, nullptr, out, in);
    CHECK_EQ(out[0], 15.0f);
    CHECK_EQ(out[1], 26.0f);
    CHECK_EQ(out[2], 37.0f);

    // sign byte set (byte 528 < 0) with a pivot present -> still fallthrough.
    float pivot[140] = {0};
    pivot[99] = 1.0f; pivot[104] = 1.0f; pivot[109] = 1.0f;
    pivot[19] = 100.0f;  // would shift if the pivot branch ran
    reinterpret_cast<signed char*>(frame)[528] = (signed char)0x80;  // < 0
    float out2[3] = {0};
    TransformPointPivotToFrameSpace(frame, pivot, out2, in);
    CHECK_EQ(out2[0], 15.0f);   // pivot ignored -> same as bone-chain
    CHECK_EQ(out2[1], 26.0f);
    CHECK_EQ(out2[2], 37.0f);
}

// ---------------------------------------------------------------------------
// Group D — FreeAllTables (0x583ab0). Default hook resets the arrays; a captor
// hook records the per-building unlink count.
// ---------------------------------------------------------------------------
namespace {
int g_unlinkCount = 0;
int g_releaseCount = 0;
void CaptorUnlink(void*) { ++g_unlinkCount; }
void CaptorRelease() { ++g_releaseCount; }
}  // namespace

TEST(ObjSceneEntity, FreeAllTablesSweepsBuildingsAndReleases) {
    ResetEntityArrays();
    // Seed three live buildings (op6 "match any" returns every alive record).
    g_personArrayLoaded = true;
    for (int i = 0; i < 3; ++i) {
        std::memset(&g_objects[i], 0, sizeof(ObjectRec));
        g_objects[i].alive = 1;
        g_objects[i].id = 100 + i;
    }
    g_unlinkCount = 0;
    g_releaseCount = 0;
    ObjectSceneEntityHooks hooks{};
    hooks.buildingFreeAndUnlink = &CaptorUnlink;
    hooks.releaseTables = &CaptorRelease;
    SetObjectSceneEntityHooks(&hooks);

    int swept = GameObjectFreeAllTables();
    CHECK_EQ(swept, 3);
    CHECK_EQ(g_unlinkCount, 3);
    CHECK_EQ(g_releaseCount, 1);   // release called once after the sweep

    SetObjectSceneEntityHooks(nullptr);  // restore default
}

TEST(ObjSceneEntity, FreeAllTablesDefaultReleaseZeroesArrays) {
    ResetEntityArrays();
    g_personArrayLoaded = true;
    g_objects[0].alive = 1; g_objects[0].id = 7;
    g_persons[0].marker = 4;
    SetObjectSceneEntityHooks(nullptr);  // default: release == ResetEntityArrays
    GameObjectFreeAllTables();
    // After default release the tables are back to the empty/unloaded state.
    CHECK_EQ((int)g_objects[0].alive, 0);
    CHECK(g_persons[0].marker == -1);
    CHECK(!g_personArrayLoaded);
}
