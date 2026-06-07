// tests/integration/save_roundtrip_itest.cpp — P3 / M3 save round-trip,
// INTEGRATION tier.
//
// Wire the SAVE path (play::SaveLiveWorld -> the real io::SaveWrite* serializers
// over a VFS memory stream) against the REAL LOAD path (play::ReloadSavedWorld ->
// the real io::Load* sub-loaders the LoadWorld driver calls) over a crafted
// real-FORMAT world that exercises the load-bearing record kinds:
//
//   * a KIND-30 (plant) object -> the 0x600 plantmap sub-record path
//     (WriteOneObject / LoadOneObject @0x5a4134 / 0x5a8190);
//   * ordinary object records with the loader-injected defaults (r48=5000,
//     r149=-1) already applied (so the pre-save world matches the post-reload one);
//   * person/scene records with the bias-A/-B counters (+84/+396) the serializer
//     carries with the -1342 / -1468 round-trip.
//
// Asserts the full M3 invariant: load -> save -> reload -> hash equivalence, and
// save-byte stability through a reload.
#include "test.h"

#include "io/save_person.h"      // kKindPlant / kPlantBytes
#include "play/save_roundtrip.h"
#include "play/world_digest.h"
#include "sim/entity.h"
#include "crt/rand.h"

#include <cstdint>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::play;

namespace {

// A persistent 0x600 plantmap scratch the crafted kind-30 record points at.
std::vector<u8> g_plantMap;

// Craft a small real-FORMAT live world: `objects` objects (the LAST one a kind-30
// plant if `withPlant`), `persons` person/scene records. Mirrors the loader's
// injected defaults so the pre-save world == the post-reload world.
void CraftRealFormatWorld(std::uint32_t seed, int objects, int persons, bool withPlant) {
    using namespace guild::sim;
    RoundTripZeroWorld(seed);

    g_plantMap.assign(io::kPlantBytes, 0);
    for (int i = 0; i < objects && i < kObjectCapacity; ++i) {
        u8* r = reinterpret_cast<u8*>(&g_objects[i]);
        bool plant = withPlant && (i == objects - 1);
        r[0] = plant ? io::kKindPlant : (u8)1;     // alive; kind 30 = plant
        i32 id = 3000 + i * 5 + (i32)(seed & 0xF);
        std::memcpy(r + 1, &id, 4);
        std::memcpy(r + 43, &id, 4);
        r[47] = (u8)(i & 0x7);
        r[92] = (u8)(0x20 + i);
        if (plant) {
            // Point +113 at the plantmap scratch and fill its 64 sub-records with a
            // deterministic pattern the serializer's sub-fields carry.
            u8* p = g_plantMap.data();
            std::memcpy(r + 113, &p, sizeof p);
            for (int s = 0; s < io::kPlantBytes; s += 24) {
                u8* sub = p + s;
                i32 a = 11 + s, b = 22 + s;
                std::memcpy(sub + 0, &a, 4);
                std::memcpy(sub + 4, &b, 4);
                sub[8] = (u8)(s & 0xFF); sub[9] = (u8)((s >> 8) & 0xFF);
                u16 w = (u16)(s + 7); std::memcpy(sub + 10, &w, 2);
                i32 c = 33 + s; std::memcpy(sub + 16, &c, 4);
                sub[12] = (u8)(s + 1); sub[13] = (u8)(s + 2);
            }
        } else {
            for (int k = 101; k < 101 + 0x30; ++k) r[k] = (u8)(k + i);
        }
        // Loader-injected defaults (so pre-save world == post-reload world).
        i32 fiveK = 5000; std::memcpy(r + 48, &fiveK, 4);
        i32 m1 = -1;      std::memcpy(r + 149, &m1, 4);
        for (int k = 153; k < 153 + 16; ++k) r[k] = (u8)(k * 3 + i);  // +153 lightmap
    }
    for (int i = 0; i < persons && i < kPersonCapacity; ++i) {
        u8* r = reinterpret_cast<u8*>(&g_persons[i]);
        std::memset(r, 0, kPersonStride);
        i16 m = (i16)i; std::memcpy(r + 0, &m, 2);       // marker == slot index
        i32 id = 6000 + i; std::memcpy(r + 4, &id, 4);
        g_persons[i].id = id; g_personIds[i] = id;
        i32 cA = 1342 + 50 + i, cB = 1468 + 60 + i;       // > the biases (stay >=0)
        std::memcpy(r + 84, &cA, 4);
        std::memcpy(r + 396, &cB, 4);
        for (int k = 136; k < 136 + 168; ++k) r[k] = (u8)(k + i * 2); // a big body run
        i32 l91 = 700 + i, l92 = 800 + i, l95 = 900 + i, l97 = 1000 + i;
        std::memcpy(r + 364, &l91, 4);
        std::memcpy(r + 368, &l92, 4);
        std::memcpy(r + 380, &l95, 4);
        std::memcpy(r + 388, &l97, 4);
    }
    g_personArrayLoaded = persons > 0;
}

} // namespace

// --- full round-trip over a crafted real-format world (with a plant) --------
TEST(SaveRoundtripIntegration, RealFormatWorld_RoundTrip_HashAndBytes) {
    const std::uint32_t seed = 0xBEEF42;
    const int objects = 5, persons = 4;

    CraftRealFormatWorld(seed, objects, persons, /*withPlant=*/true);

    // h1 over the crafted world (NormalizePlantPointers + RNG anchor are inside
    // RoundTripLiveWorld). The whole invariant runs through the real serializers.
    RoundTripResult r = RoundTripLiveWorld(objects, persons, /*sceneTiles=*/8, seed);

    CHECK(r.saved);
    CHECK(r.reloaded);
    CHECK(r.saveBytes1 > 0);
    CHECK_EQ(r.hashAfterLoad, r.hashAfterReload);   // the save carried the plant + bodies
    CHECK(r.hashEquivalent());
    CHECK(r.saveBytesStable);                        // fixed point through a reload
    CHECK_EQ(r.saveBytes1, r.saveBytes2);
    CHECK(r.ok());
}

// --- the bias counters round-trip through the -1342/-1468 conversion --------
TEST(SaveRoundtripIntegration, BiasCounters_SurviveRoundTrip) {
    const std::uint32_t seed = 0x33;
    CraftRealFormatWorld(seed, 4, 3, /*withPlant=*/false);

    // Snapshot the live +84/+396 counters before the round trip.
    i32 a0, b0;
    std::memcpy(&a0, reinterpret_cast<u8*>(&sim::g_persons[1]) + 84, 4);
    std::memcpy(&b0, reinterpret_cast<u8*>(&sim::g_persons[1]) + 396, 4);

    std::vector<u8> s1;
    CHECK(SaveLiveWorld(s1, 4, 3, 8));
    RoundTripZeroWorld(seed);
    std::uint32_t ro, rp, rt;
    CHECK(ReloadSavedWorld(s1, &ro, &rp, &rt));

    // After the write (-bias) + load (+bias), the in-memory counters are restored.
    i32 a1, b1;
    std::memcpy(&a1, reinterpret_cast<u8*>(&sim::g_persons[1]) + 84, 4);
    std::memcpy(&b1, reinterpret_cast<u8*>(&sim::g_persons[1]) + 396, 4);
    CHECK_EQ(a0, a1);
    CHECK_EQ(b0, b1);
    CHECK_EQ(rp, (std::uint32_t)3);
}

// --- a mutated body byte changes both the hash and the save bytes -----------
TEST(SaveRoundtripIntegration, MutatedBodyByteChangesRoundTrip) {
    const std::uint32_t seed = 0x99;

    CraftRealFormatWorld(seed, 4, 3, false);
    RoundTripResult base = RoundTripLiveWorld(4, 3, 8, seed);
    CHECK(base.ok());

    CraftRealFormatWorld(seed, 4, 3, false);
    reinterpret_cast<u8*>(&sim::g_persons[2])[200] ^= 0xFF;  // a +136..+304 body byte
    RoundTripResult mut = RoundTripLiveWorld(4, 3, 8, seed);
    CHECK(mut.ok());

    CHECK(mut.hashAfterLoad != base.hashAfterLoad);
    CHECK_EQ(mut.hashAfterLoad, mut.hashAfterReload);  // the change survives a reload
}
