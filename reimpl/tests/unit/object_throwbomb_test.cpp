// Golden tests for VIBE_Object_SpawnThrownBomb @0x4869dc (guild::sim).
// Verifies slot allocation, the ballistic velocity math, and the 352-byte anim
// descriptor field fills against the gilde.exe decompilation, over injected hooks.
#include "tests/framework/test.h"

#include "sim/object_throwbomb.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {
// Recording hooks: a fixed target world pos so the delta/velocity is deterministic.
i32 g_attachCalls = 0;
i32 RecAttach(const f32 /*pos*/[3], const i32 /*cp*/[3]) { ++g_attachCalls; return 0x1000 + g_attachCalls; }
void RecTileToWorld(i32 /*tile*/, i32 /*arg*/, f32 out[3]) { out[0] = 110.0f; out[1] = 80.0f; out[2] = 30.0f; }
i32  RecCoordX() { return 0x77; }
i32  RecCreateAnim(i32 /*node*/, const void* /*p*/, int /*slot*/) { return 0xA000; }

ThrowBombHooks MakeHooks() {
    ThrowBombHooks h{};
    h.attachNode = &RecAttach;
    h.tileToWorld = &RecTileToWorld;
    h.coordConvertX = &RecCoordX;
    h.createObjectAnim = &RecCreateAnim;
    return h;
}
f32 bits(i32 b) { f32 v; std::memcpy(&v, &b, 4); return v; }
bool near(f32 a, f32 b) { f32 d = a - b; return (d < 0 ? -d : d) < 1e-3f; }
} // namespace

TEST(ObjectThrowBomb, SpawnFillsSlotVelocityAndDescriptor) {
    ResetBombTable();
    auto h = MakeHooks(); g_attachCalls = 0; SetThrowBombHooks(&h);

    BombSpawn s; s.x = 10.0f; s.y = 20.0f; s.z = 0;
    i32 desc[88];
    int slot = SpawnThrownBomb(s, /*tile=*/42, /*arg=*/7, desc);

    CHECK_EQ(slot, 0);                              // first spawn -> slot 0
    CHECK_EQ(g_bombTable[0].node, 0x1001);          // attachNode result stored
    CHECK_EQ(g_bombTable[0].anim, 0xA000);
    CHECK_EQ(g_bombTable[0].tile, 42);
    CHECK_EQ(g_bombTable[0].arg, 7);

    // delta = target(110,80,30) - spawn(10,20,0) = (100,60,30).
    // velocity = delta*0.5, with vy += 50: (50, 80, 15).
    CHECK(near(bits(desc[23]), 50.0f));             // vx = 100*0.5
    CHECK(near(bits(desc[24]), 80.0f));             // vy = 60*0.5 + 50
    CHECK(near(bits(desc[25]), 15.0f));             // vz = 30*0.5
    // descriptor deltas: dx=100, dy=60-60(adjust)=0, dz=30.
    CHECK(near(bits(desc[45]), 100.0f));
    CHECK(near(bits(desc[46]), 0.0f));              // dy + (-60)
    CHECK(near(bits(desc[47]), 30.0f));
    CHECK(near(bits(desc[68]), 0.0f));
    CHECK(near(bits(desc[69]), 30.0f));
    // height + coord + PI rotation slots.
    CHECK_EQ(desc[0], 18);
    CHECK_EQ(desc[22], 18);
    CHECK_EQ(desc[44], 1);
    CHECK_EQ(desc[67], 0x77);                        // Coord_ConvertX() result
    const i32 kPi = 1078530011;
    CHECK_EQ(desc[27], kPi); CHECK_EQ(desc[28], kPi); CHECK_EQ(desc[29], kPi);
    CHECK_EQ(desc[49], kPi); CHECK_EQ(desc[51], kPi);
    CHECK_EQ(desc[71], kPi); CHECK_EQ(desc[73], kPi);
    SetThrowBombHooks(nullptr);
}

TEST(ObjectThrowBomb, AllocatesNextFreeSlotThenFull) {
    ResetBombTable();
    auto h = MakeHooks(); g_attachCalls = 0; SetThrowBombHooks(&h);
    BombSpawn s; s.x = 1; s.y = 1; s.z = 0;

    // Occupy slot 0, then the next spawn must take slot 1.
    int a = SpawnThrownBomb(s, 1, 0, nullptr);
    int b = SpawnThrownBomb(s, 1, 0, nullptr);
    CHECK_EQ(a, 0);
    CHECK_EQ(b, 1);

    // Fill the rest; the 33rd spawn (table full) returns -1.
    for (int k = 2; k < kBombSlotCount; ++k) CHECK(SpawnThrownBomb(s, 1, 0, nullptr) == k);
    CHECK_EQ(SpawnThrownBomb(s, 1, 0, nullptr), -1);   // full -> null
    SetThrowBombHooks(nullptr);
}

// Inert default (no hooks): runs headless without crashing, allocates a slot.
TEST(ObjectThrowBomb, InertDefaultHeadless) {
    ResetBombTable();
    SetThrowBombHooks(nullptr);
    BombSpawn s; s.x = 5; s.y = 5; s.z = 0;
    int slot = SpawnThrownBomb(s, 3, 1, nullptr);
    CHECK_EQ(slot, 0);
    CHECK_EQ(g_bombTable[0].node, 0);   // inert attach -> node 0
    CHECK_EQ(g_bombTable[0].tile, 3);
}
