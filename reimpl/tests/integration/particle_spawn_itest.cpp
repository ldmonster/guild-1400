#include "test.h"
#include "render/particle_spawn.h"
#include "render/emitter_setup.h"   // REAL sibling module (the 18 setters)

#include <cstring>

using namespace guild;
using namespace guild::render;

// =============================================================================
// Integration: drive the spawn cluster (particle_spawn.cpp) with emitter flags
// authored by the REAL emitter_setup.cpp setters — not a private copy.
//
// The runtime emitter EmitterRecord (emitter_setup.h, 144 bytes) is the typed
// view of the bytes that live at system+0x40 .. system+0xCD; the 0xA4-byte
// EmitterTemplate that SpawnSystemByType copies into the system at +44 covers the
// same region, with EmitterRecord starting at template+0x14. So flagByte0/1 the
// setters write at EmitterRecord+0x8C/0x8D land at template+0xA0/0xA1 — exactly
// the bytes SpawnSystemByType reads for the trigger/init-fill path. This test
// overlays an EmitterRecord onto a template buffer, calls SetFlags/SetIsTrigger/
// SetTriggerOnce, spawns, and verifies the authored flags propagate into the
// live system.
// =============================================================================

namespace {
// EmitterRecord sits at template+0x14 (system+0x40 == template+0x14).
constexpr unsigned kRecordOffsetInTemplate = 0x14;
EmitterRecord* RecordView(EmitterTemplate& t) {
    return reinterpret_cast<EmitterRecord*>(t.bytes + kRecordOffsetInTemplate);
}
} // namespace

// SetIsTrigger (emitter_setup) raises flagByte0 bit7; the spawn must NOT prime
// the trigger path (that keys off flagByte1 bit0), but the bit must survive the
// template->system copy intact.
TEST(PSpawnEmitterI, IsTriggerBitSurvivesCopy) {
    EmitterTemplate tmpl;
    std::memset(tmpl.bytes, 0, sizeof(tmpl.bytes));
    tmpl.bytes[0] = 0; // points integrator

    EmitterErrorHook hook; // default: non-null handle => valid
    SetEmitterErrorHook(hook);
    EmitterRecord* rec = RecordView(tmpl);
    EmitterHandle handle = rec;

    u8 yes = 1;
    CHECK_EQ(SetIsTrigger(&handle, &yes), 0);     // REAL setter -> flagByte0 bit7
    CHECK_EQ(rec->flagByte0 & 0x80u, 0x80u);

    float pos[3] = {0, 0, 0};
    const u8 name[] = "x";
    const u8* np = name;
    ParticleSystem* s = SpawnSystemByType(0, pos, np, 1, 0, 1.0f, tmpl, 4, 0);
    CHECK(s != nullptr);
    // flagByte0 (with bit7) copied verbatim; flagByte1 bit0 not set -> no prime.
    CHECK_EQ(s->flagByte0 & 0x80u, 0x80u);
    CHECK_EQ(s->flagByte1 & 1u, 0u);
    CHECK_EQ(s->spawnedCount, 0u);
    FreeAllSpawnAllocations(); // wave-10: reclaim all four AllocSystem blocks
}

// SetTriggerOnce (emitter_setup) sets flagByte1 bit0 — the SAME bit the spawn's
// trigger-prime path keys on. Authoring it via the real setter must drive the
// spawn into the prime branch (spawnedCount=0, flagByte1 bit1 raised).
TEST(PSpawnEmitterI, TriggerOnceDrivesSpawnPrime) {
    EmitterTemplate tmpl;
    std::memset(tmpl.bytes, 0, sizeof(tmpl.bytes));
    tmpl.bytes[0] = 1; // polys integrator

    EmitterErrorHook hook;
    SetEmitterErrorHook(hook);
    EmitterRecord* rec = RecordView(tmpl);
    EmitterHandle handle = rec;

    u8 yes = 1;
    CHECK_EQ(SetTriggerOnce(&handle, &yes), 0);   // REAL setter -> flagByte1 bit0
    CHECK_EQ(rec->flagByte1 & 1u, 1u);

    float pos[3] = {7, 8, 9};
    const u8 name[] = "x";
    const u8* np = name;
    ParticleSystem* s = SpawnSystemByType(0, pos, np, 2, 0, 1.0f, tmpl, 3, 0);
    CHECK(s != nullptr);
    CHECK_EQ(s->flagByte1 & 1u, 1u);   // authored bit preserved
    CHECK_EQ(s->flagByte1 & 2u, 2u);   // spawn raised the pending bit
    CHECK_EQ(s->spawnedCount, 0u);
    CHECK(s->position[0] == 7.0f && s->position[2] == 9.0f);
    FreeAllSpawnAllocations(); // wave-10: reclaim all four AllocSystem blocks
}

// SetFlags (emitter_setup) packs textureMode + init/rebirth/trigger bits into
// flagByte0. After spawn, flagByte0 in the system must match what the real setter
// produced (modulo the spawn copy being byte-exact).
TEST(PSpawnEmitterI, SetFlagsPropagates) {
    EmitterTemplate tmpl;
    std::memset(tmpl.bytes, 0, sizeof(tmpl.bytes));
    tmpl.bytes[0] = 2; // lens

    EmitterErrorHook hook;
    SetEmitterErrorHook(hook);
    EmitterRecord* rec = RecordView(tmpl);
    EmitterHandle handle = rec;

    i32 time = 50;
    u8 texMode = 0x0A, initFill = 1, rebirth = 0, isTrig = 1, trigOnce = 0;
    CHECK_EQ(SetFlags(&handle, &time, &texMode, &initFill, &rebirth, &isTrig, &trigOnce), 0);
    u8 authored = rec->flagByte0;

    float pos[3] = {0, 0, 0};
    const u8 name[] = "x";
    const u8* np = name;
    ParticleSystem* s = SpawnSystemByType(0, pos, np, 1, 0, 1.0f, tmpl, 2, 0);
    CHECK(s != nullptr);
    CHECK_EQ(s->flagByte0, authored);  // exact byte copy through the spawn path
    FreeAllSpawnAllocations(); // wave-10: reclaim all four AllocSystem blocks
}
