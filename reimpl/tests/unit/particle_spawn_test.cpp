#include "test.h"
#include "render/particle_spawn.h"

#include <cstring>

using namespace guild;
using namespace guild::render;

// =============================================================================
// Golden-vector unit tests for the particle spawn/alloc cluster. Vectors were
// computed independently in Python from the recovered byte-store sequence (see
// the implementer report). Suites prefixed PSpawn_ to avoid clashes.
// =============================================================================

namespace {
float as_f(const u8* p) { float v; std::memcpy(&v, p, 4); return v; }
i32   as_i(const u8* p) { i32 v;   std::memcpy(&v, p, 4); return v; }
} // namespace

// CreateEmitter fills the 0xA4 template with the exact recovered defaults.
TEST(PSpawn_CreateEmitter, TemplateDefaults) {
    ResetSpawnStats();
    EmitterTemplate tmpl;
    std::memset(tmpl.bytes, 0xCC, sizeof(tmpl.bytes)); // poison to prove zero-fill

    u8  kind = 2;            // lens
    i32 amp = 42;            // -> life 42.0
    int texSlot = 1;
    int owner = 0x1234;
    const u8 texName[] = "smoke";
    const u8* texNamePtr = texName;
    u8  userType = 0x07;
    u8  trigger = 1;
    float pos[3] = {1.0f, 2.0f, 3.0f};

    ParticleSystem* sys = CreateEmitter(&kind, &amp, &texSlot, &owner, &texNamePtr,
                                        &userType, &trigger, pos, tmpl,
                                        /*slotCount*/4, /*nowTick*/100);

    // Template recovered literals (bit-exact).
    CHECK_EQ(tmpl.bytes[0x00], 2u);
    CHECK(as_f(tmpl.bytes + 0x40) == 0.0f);
    CHECK(as_f(tmpl.bytes + 0x34) == 10.0f);
    CHECK(as_f(tmpl.bytes + 0x38) == 50.0f);
    CHECK(as_f(tmpl.bytes + 0x3C) == 30.0f);
    CHECK(as_f(tmpl.bytes + 0x44) == 1.0f);
    CHECK(as_f(tmpl.bytes + 0x48) == 0.0f);
    CHECK(as_f(tmpl.bytes + 0x80) == 1.0f);
    CHECK(as_f(tmpl.bytes + 0x8C) == 255.0f);
    CHECK_EQ(as_i(tmpl.bytes + 0x98), 180);
    CHECK_EQ(as_i(tmpl.bytes + 0x90), 20);
    CHECK_EQ(as_i(tmpl.bytes + 0x94), 160);
    CHECK_EQ(tmpl.bytes[0x9C], 0xFFu);
    CHECK_EQ(tmpl.bytes[0x9D], 0xFFu);
    CHECK_EQ(tmpl.bytes[0x9E], 0xFFu);
    CHECK_EQ(tmpl.bytes[0x9F], 0x00u);
    CHECK_EQ(tmpl.bytes[0xA0], 0x20u); // flagByte0 = initFill bit5

    // Created system used kind=2 -> lens integrator sentinel; copied flags.
    CHECK(sys != nullptr);
    CHECK_EQ(sys->owner, 0x1234);
    CHECK(sys->lifeBase == 42.0f);
    CHECK_EQ(sys->flagByte0, 0x20u);
    CHECK_EQ(sys->flagByte1, 0x00u);
    CHECK(sys->updateFn == reinterpret_cast<void*>(static_cast<size_t>(0x5e32c0)));
    // Placement landed at pos.
    CHECK(sys->position[0] == 1.0f);
    CHECK(sys->position[1] == 2.0f);
    CHECK(sys->position[2] == 3.0f);
    FreeAllSpawnAllocations(); // wave-10: reclaim all four AllocSystem blocks
}

// SpawnSystemByType dispatches the integrator by template[0].
TEST(PSpawn_SpawnByType, Dispatch) {
    EmitterTemplate tmpl;
    std::memset(tmpl.bytes, 0, sizeof(tmpl.bytes));
    float pos[3] = {0,0,0};
    const u8 name[] = "x";
    const u8* np = name;

    tmpl.bytes[0] = 0;
    ParticleSystem* a = SpawnSystemByType(0, pos, np, 1, 0, 1.0f, tmpl, 2, 0);
    CHECK(a && a->updateFn == reinterpret_cast<void*>(static_cast<size_t>(0x5e1e0c)));

    tmpl.bytes[0] = 1;
    ParticleSystem* b = SpawnSystemByType(0, pos, np, 1, 0, 1.0f, tmpl, 2, 0);
    CHECK(b && b->updateFn == reinterpret_cast<void*>(static_cast<size_t>(0x5e2814)));

    tmpl.bytes[0] = 2;
    ParticleSystem* c = SpawnSystemByType(0, pos, np, 1, 0, 1.0f, tmpl, 2, 0);
    CHECK(c && c->updateFn == reinterpret_cast<void*>(static_cast<size_t>(0x5e32c0)));

    tmpl.bytes[0] = 9; // out of range -> null
    ParticleSystem* d = SpawnSystemByType(0, pos, np, 1, 0, 1.0f, tmpl, 2, 0);
    CHECK(d == nullptr);

    (void)a; (void)b; (void)c;
    FreeAllSpawnAllocations(); // wave-10: reclaim all blocks (incl. scratch)
}

// SpawnSystemByType trigger path: flagByte1 bit0 set -> spawnedCount 0, bit1 set.
TEST(PSpawn_SpawnByType, TriggerPriming) {
    EmitterTemplate tmpl;
    std::memset(tmpl.bytes, 0, sizeof(tmpl.bytes));
    tmpl.bytes[0]    = 0;       // points
    tmpl.bytes[0xA1] = 0x01;    // flagByte1 bit0 = trigger
    float pos[3] = {0,0,0};
    const u8 name[] = "x";
    const u8* np = name;
    ParticleSystem* s = SpawnSystemByType(0, pos, np, 1, 0, 1.0f, tmpl, 2, 0);
    CHECK(s != nullptr);
    CHECK_EQ(s->spawnedCount, 0u);
    CHECK_EQ(s->flagByte1, 0x03u); // bit0 (orig) | bit1 (raised)
    FreeAllSpawnAllocations(); // wave-10
}

// AllocSystem reject conditions + alloc accounting + per-slot init.
TEST(PSpawn_AllocSystem, RejectsAndInits) {
    ResetSpawnStats();
    const u8 name[] = "p";
    // life <= 0 -> null
    CHECK(AllocSystem(1, name, 0, 0.0f, 0, nullptr, 4, 0) == nullptr);
    // owner == 0 -> null
    CHECK(AllocSystem(0, name, 0, 1.0f, 0, nullptr, 4, 0) == nullptr);

    ResetSpawnStats();
    int sc = 5;
    ParticleSystem* s = AllocSystem(7, name, 3, 9.0f, 0xAB, nullptr, sc, 0x55);
    CHECK(s != nullptr);
    // Four allocations: system + particles + points + polys.
    CHECK_EQ(MutableSpawnStats().allocCalls, 4);
    CHECK_EQ(MutableSpawnStats().allocBytes,
             static_cast<u32>(0x310 + 84 * sc + 80 * 4 * sc + 40 * 2 * sc));
    CHECK_EQ(MutableSpawnStats().textureCalls, 1);
    CHECK_EQ(MutableSpawnStats().initCalls, 1);
    CHECK_EQ(s->count, sc);
    CHECK(s->lifeBase == 9.0f);

    // Per-slot init: birth tick = nowTick, lifetime = lifeBase, active bit clear,
    // frame = 0, +76 dword = -1, position/velocity zeroed.
    unsigned char* base = static_cast<unsigned char*>(s->particles);
    for (int i = 0; i < sc; ++i) {
        unsigned char* p = base + static_cast<size_t>(i) * 84;
        CHECK_EQ(as_i(p + 48), static_cast<i32>(0x55));
        CHECK(as_f(p + 72) == 9.0f);
        CHECK_EQ(p[81] & 1u, 0u);
        CHECK_EQ(p[80], 0u);
        CHECK_EQ(as_i(p + 76), -1);
        CHECK(as_f(p + 56) == 0.0f);
        CHECK(as_f(p + 0)  == 0.0f);
    }
    FreeAllSpawnAllocations(); // wave-10
}

// SetPosition forwards placement; null is a no-op returning null.
TEST(PSpawn_SetPosition, Forwards) {
    const u8 name[] = "p";
    ParticleSystem* s = AllocSystem(1, name, 0, 1.0f, 0, nullptr, 2, 0);
    CHECK(s != nullptr);
    float pos[3] = {4.0f, 5.0f, 6.0f};
    CHECK(SetPosition(s, pos) == s);
    CHECK(s->position[0] == 4.0f && s->position[1] == 5.0f && s->position[2] == 6.0f);
    CHECK(SetPosition(nullptr, pos) == nullptr);
    FreeAllSpawnAllocations(); // wave-10
}

// Kill* guard: valid -> free, invalid -> report.
TEST(PSpawn_Kill, ValidAndInvalid) {
    ClearLastKillError();
    int node = 0;
    void* h = &node;
    CHECK_EQ(KillParticle(&h), 0);
    CHECK(LastFreedNode() == &node);
    CHECK(LastKillError()[0] == '\0');

    void* nullh = nullptr;
    CHECK_EQ(KillParticle(&nullh), 0);
    CHECK(std::strcmp(LastKillError(), "ecmd_KillParticle: invalid handle...") == 0);

    ClearLastKillError();
    CHECK_EQ(KillEmitter(&h), 0);
    CHECK(LastFreedNode() == &node);
    CHECK_EQ(KillEmitter(&nullh), 0);
    CHECK(std::strcmp(LastKillError(), "KillEmitter: invalid handle...") == 0);
}
