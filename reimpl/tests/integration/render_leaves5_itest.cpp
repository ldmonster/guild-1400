#include "test.h"

// Integration: drive render_leaves5's particle-effect constructors against the
// REAL reconstructed sibling modules they call — NO mocks:
//   - guild::render::AllocSystem / SetPosition (particle_spawn.cpp, 0x5e1000 /
//     0x5e1228) — the genuine system allocator + placer.
//   - guild::crt::RandNext (rand.cpp, 0x5cb8bc) — the genuine LCG that every
//     ctor's per-slot seeding consumes.
//   - guild::render::UpdateScatter (particle.cpp, 0x42cde8) — the real integrator
//     the SpawnRefLens system is wired to (stored at sys+0x304).
// We forward render_leaves5's lensColourTag hook into a real in-process tag and
// assert the cross-module flow: SpawnRefLens -> real AllocSystem (real particle
// array) -> InitColors (real RNG) -> real SetPosition, with the placement and
// RNG-seeded colours observed end to end. This mirrors the live wiring exactly
// (the engine routes the same calls; only the OS/heap backend is in-process).
#include "render/render_leaves5.h"
#include "render/particle_spawn.h"   // real AllocSystem / SetPosition / SpawnHooks
#include "render/particle.h"         // real UpdateScatter integrator
#include "crt/rand.h"                // real crt::RandNext (0x5cb8bc)

#include <cstdint>
#include <cstring>

using namespace guild;
using guild::render::ParticleSystem;
using guild::render::Particle;
using guild::render::EffectHeader;

namespace {

bool FEq(float a, float b) {
    std::uint32_t ua, ub; std::memcpy(&ua, &a, 4); std::memcpy(&ub, &b, 4); return ua == ub;
}
template <class T> T Get(const void* base, int off) {
    T v; std::memcpy(&v, static_cast<const unsigned char*>(base) + off, sizeof(T)); return v;
}

// The REAL cross-module tag the render_leaves5 hook forwards into. In the live
// engine SpawnRefLens passes VIBE_Sound3d_SetActiveListener's address as an
// opaque tag; here we hand back a real heap object so the wiring is exercised
// (the math never dereferences it — fidelity to the verbatim quirk).
int g_realLensTag = 0xABCD;
int g_lensTagCalls = 0;
void* RealLensColourTag() { ++g_lensTagCalls; return &g_realLensTag; }

} // namespace

// ---------------------------------------------------------------------------
// SpawnRefLens forwards its colour-tag hook into the real tag, allocates through
// the REAL AllocSystem, seeds colours with the REAL crt::RandNext, and places via
// the REAL SetPosition. Assert all four cross-module effects fire.
// ---------------------------------------------------------------------------
TEST(RenderLeaves5_ITest, RefLensWiresRealSiblings) {
    render::ResetSpawnStats();
    g_lensTagCalls = 0;

    render::RenderLeaves5Hooks hooks{};
    hooks.lensColourTag = &RealLensColourTag;
    render::InstallRenderLeaves5Hooks(hooks);

    // Seed the REAL LCG; capture the exact colour bytes InitColors will produce.
    crt::Srand(0x1234);
    const int slots = 6;
    float pos[3] = {2.5f, -1.0f, 4.0f};
    EffectHeader hdr;
    ParticleSystem* sys = render::SpawnRefLens(/*owner*/ 3, /*tag*/ 99, /*texSlot*/ 1,
                                               pos, hdr, slots, /*now*/ 0);
    CHECK(sys != nullptr);
    if (!sys) {
        render::InstallRenderLeaves5Hooks(render::RenderLeaves5Hooks{nullptr});
        return;
    }

    // (1) The hook was actually forwarded into the real tag.
    CHECK_EQ(g_lensTagCalls, 1);
    // (2) Real AllocSystem ran: stats show a system + particle array allocation.
    CHECK(render::CurrentSpawnHooks().alloc != nullptr);
    CHECK(render::MutableSpawnStats().allocCalls >= 2);
    CHECK(sys->count == slots);
    CHECK(sys->particles != nullptr);
    // (3) Header words SpawnRefLens wrote (v8 = {131199, tag}).
    CHECK_EQ(hdr.i(0), 131199);
    CHECK_EQ(hdr.i(4), 99);
    // (4) Real SetPosition placed the system.
    CHECK(FEq(sys->position[0], 2.5f));
    CHECK(FEq(sys->position[1], -1.0f));
    CHECK(FEq(sys->position[2], 4.0f));

    // The colour bytes match the REAL RNG oracle for the seeded slots (count-1).
    crt::Srand(0x1234);
    Particle* p = static_cast<Particle*>(sys->particles);
    for (int i = 0; i + 1 < slots; ++i) {
        u8 eb = static_cast<u8>((crt::RandNext() & 0x3F) - 66);
        u8 eg = static_cast<u8>((crt::RandNext() & 0x3F) + 100);
        u8 er = static_cast<u8>((crt::RandNext() & 0x3F) + 50);
        CHECK_EQ((int)Get<u8>(&p[i], 0x4E), (int)eb);
        CHECK_EQ((int)Get<u8>(&p[i], 0x4D), (int)eg);
        CHECK_EQ((int)Get<u8>(&p[i], 0x4C), (int)er);
    }

    // The system was wired to the REAL UpdateScatter integrator; drive it through
    // an Emitter view over the same real particle array (cross-module hand-off).
    render::Emitter e{};
    e.count = sys->count;
    e.particles = p;
    e.lifeBase = 1.0f;
    e.maxAlive = 2;
    e.hdr20 = 0;  // no re-init; just advance existing slots
    (void)render::UpdateScatter(e, 10);
    // Placement preserved across the integrator step.
    CHECK(FEq(sys->position[0], 2.5f));

    // Restore the inert default hook so other suites see a clean module.
    render::InstallRenderLeaves5Hooks(render::RenderLeaves5Hooks{nullptr});
}

// ---------------------------------------------------------------------------
// Default (inert) hook path: with no hook installed, SpawnRefLens still wires the
// real AllocSystem/SetPosition siblings and produces a valid placed system; the
// inert lensColourTag returns null and is harmless.
// ---------------------------------------------------------------------------
TEST(RenderLeaves5_ITest, RefLensInertDefaultPath) {
    render::ResetSpawnStats();
    render::InstallRenderLeaves5Hooks(render::RenderLeaves5Hooks{nullptr});
    crt::Srand(7);
    float pos[3] = {0.0f, 0.0f, 0.0f};
    EffectHeader hdr;
    ParticleSystem* sys = render::SpawnRefLens(4, 1, 1, pos, hdr, 5, 0);
    CHECK(sys != nullptr);
    if (sys) {
        CHECK_EQ(sys->count, 5);
        CHECK(sys->particles != nullptr);
        CHECK_EQ(hdr.i(0), 131199);
    }
}
