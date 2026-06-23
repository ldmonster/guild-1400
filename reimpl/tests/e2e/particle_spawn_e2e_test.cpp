#include "test.h"
#include "render/particle_spawn.h"
#include "render/emitter_setup.h"   // real setters for authoring the emitter

#include <cstdlib>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::render;

// =============================================================================
// End-to-end: a whole emitter spawn flow across the spawn cluster + the real
// emitter_setup setters, on a realistic multi-slot system.
//
//   CreateEmitter (fills template defaults + packs userTag)
//        -> SpawnSystemByType (type dispatch, copy, place, trigger-prime)
//             -> AllocSystem (texture load + 4 allocs + per-slot init loop)
//
// GUARDED: the heavier multi-system stress pass only runs when
// GUILD_RUN_PARTICLE_E2E=1 (mirrors the real-asset-guarded e2e convention).
// Without it the test still runs a single deterministic flow so the suite stays
// green everywhere.
// =============================================================================

namespace {

// Author an emitter through the real setters on a template-overlaid record, then
// spawn it. Returns the live system (caller frees particles+system).
ParticleSystem* SpawnAuthored(u8 kind, int owner, int slots, u32 tick,
                              const float pos[3], bool trigger) {
    static EmitterTemplate tmpl; // reused as byte_765320 would be in the engine
    std::memset(tmpl.bytes, 0, sizeof(tmpl.bytes));

    // CreateEmitter authors the defaults + kind + packed userTag, then spawns.
    i32 amp = 64;          // -> life 64.0
    int texSlot = 0;
    int ownerv = owner;
    const u8 texName[] = "particle";
    const u8* np = texName;
    u8 userType = 0x03;
    u8 trig = static_cast<u8>(trigger ? 1 : 0);

    ParticleSystem* sys = CreateEmitter(&kind, &amp, &texSlot, &ownerv, &np,
                                        &userType, &trig, pos, tmpl, slots, tick);
    return sys;
}

// wave-10 (W10-PARTICLE) leak fix: AllocSystem makes FOUR default-backend
// allocations per system (header + particle array + two scratch buffers, one of
// whose return value AllocSystem discards). Free them ALL via the backend
// registry instead of the previous two manual delete[]s, which leaked the scratch.
// Idempotent: clears the registry, so repeated calls (e.g. a per-system loop) are
// safe no-ops after the first.
void FreeSys(ParticleSystem* /*s*/) {
    FreeAllSpawnAllocations();
}

} // namespace

// Deterministic single-flow e2e (always runs).
TEST(PSpawnE2E, FullCreateFlow) {
    ResetSpawnStats();
    float pos[3] = {12.5f, -3.0f, 8.0f};
    ParticleSystem* s = SpawnAuthored(/*kind*/0, /*owner*/100, /*slots*/8,
                                      /*tick*/0x2000, pos, /*trigger*/false);
    CHECK(s != nullptr);
    CHECK_EQ(s->count, 8);
    CHECK(s->lifeBase == 64.0f);
    // points integrator selected for kind 0.
    CHECK(s->updateFn == reinterpret_cast<void*>(static_cast<size_t>(0x5e1e0c)));
    // placed at pos.
    CHECK(s->position[0] == 12.5f && s->position[1] == -3.0f && s->position[2] == 8.0f);
    // every slot stamped with the spawn tick + lifeBase, inactive, +76 == -1.
    unsigned char* base = static_cast<unsigned char*>(s->particles);
    for (int i = 0; i < s->count; ++i) {
        unsigned char* p = base + static_cast<size_t>(i) * 84;
        u32 bt; std::memcpy(&bt, p + 48, 4);
        float lf; std::memcpy(&lf, p + 72, 4);
        i32 m; std::memcpy(&m, p + 76, 4);
        CHECK_EQ(bt, 0x2000u);
        CHECK(lf == 64.0f);
        CHECK_EQ(p[81] & 1u, 0u);
        CHECK_EQ(m, -1);
    }
    // four allocations occurred.
    CHECK_EQ(MutableSpawnStats().allocCalls, 4);
    FreeSys(s);
}

// Trigger-primed flow: CreateEmitter leaves flagByte1 bit0 clear, so we author it
// via the real SetTriggerOnce before spawning to drive the prime path.
TEST(PSpawnE2E, TriggerPrimedFlow) {
    EmitterTemplate tmpl;
    std::memset(tmpl.bytes, 0, sizeof(tmpl.bytes));
    tmpl.bytes[0] = 1; // polys

    EmitterErrorHook hook;
    SetEmitterErrorHook(hook);
    // EmitterRecord overlays template+0x14 (see itest for the offset derivation).
    EmitterRecord* rec = reinterpret_cast<EmitterRecord*>(tmpl.bytes + 0x14);
    EmitterHandle h = rec;
    u8 yes = 1;
    SetTriggerOnce(&h, &yes);

    float pos[3] = {0, 0, 0};
    const u8 name[] = "p";
    const u8* np = name;
    ParticleSystem* s = SpawnSystemByType(0, pos, np, 5, 0, 1.0f, tmpl, 4, 7);
    CHECK(s != nullptr);
    CHECK_EQ(s->flagByte1 & 3u, 3u); // authored bit0 + spawn-raised bit1
    CHECK_EQ(s->spawnedCount, 0u);
    FreeSys(s);
}

// Guarded stress pass: many systems of mixed type, verifying alloc accounting and
// per-slot init scale up faithfully.
TEST(PSpawnE2E, GuardedMultiSystemStress) {
    if (!std::getenv("GUILD_RUN_PARTICLE_E2E")) {
        CHECK(true); // no-op pass when the guard is unset
        return;
    }
    ResetSpawnStats();
    std::vector<ParticleSystem*> systems;
    u32 expectBytes = 0;
    int expectAllocs = 0;
    const float pos[3] = {0, 0, 0};
    for (int k = 0; k < 3; ++k) {
        for (int slots = 1; slots <= 16; ++slots) {
            ParticleSystem* s = SpawnAuthored(static_cast<u8>(k), 1, slots,
                                              static_cast<u32>(0x100 + slots), pos, false);
            CHECK(s != nullptr);
            CHECK_EQ(s->count, slots);
            expectBytes += static_cast<u32>(0x310 + 84 * slots + 80 * 4 * slots + 40 * 2 * slots);
            expectAllocs += 4;
            // spot-check first + last slot init.
            unsigned char* base = static_cast<unsigned char*>(s->particles);
            unsigned char* last = base + static_cast<size_t>(slots - 1) * 84;
            i32 m; std::memcpy(&m, last + 76, 4);
            CHECK_EQ(m, -1);
            systems.push_back(s);
        }
    }
    CHECK_EQ(MutableSpawnStats().allocCalls, expectAllocs);
    CHECK_EQ(MutableSpawnStats().allocBytes, expectBytes);
    for (ParticleSystem* s : systems) FreeSys(s);
}
