#include "play/determinism.h"

#include <cstdio>

#include "crt/rand.h"                  // RandStatePtr — live CRT RNG state
#include "sim/entity.h"                // g_persons/g_personIds/g_objects/g_sceneNodes...
#include "sim/building_lifecycle.h"    // g_buildingPersons

namespace guild::play {

// ---------------------------------------------------------------------------
// FNV-1a core.
// ---------------------------------------------------------------------------
Fnv1a64& Fnv1a64::bytes(const void* data, std::size_t n) {
    const auto* p = static_cast<const unsigned char*>(data);
    std::uint64_t h = h_;
    for (std::size_t i = 0; i < n; ++i) {
        h ^= static_cast<std::uint64_t>(p[i]);
        h *= kPrime;
    }
    h_ = h;
    return *this;
}

namespace {

// Fold one named region into the running snapshot: hash the raw bytes both into
// the whole-world digest `whole` and into a fresh per-region digest recorded in
// `snap.regions`. Keeping the two folds in lockstep means the region sub-hashes
// localize exactly what the whole-world hash covers.
void FoldRegion(WorldSnapshot& snap, Fnv1a64& whole, const char* label,
                const void* data, std::size_t n) {
    whole.bytes(data, n);
    Fnv1a64 region;
    region.bytes(data, n);
    snap.regions.push_back({label, region.value(), n});
}

// Walk the live world in a FIXED order, folding every region. Shared by both
// HashWorldState (digest only) and SnapshotWorld (digest + region list).
WorldSnapshot FoldWorld() {
    using namespace guild::sim;
    WorldSnapshot snap;
    Fnv1a64 whole;

    FoldRegion(snap, whole, "g_persons",
               g_persons, sizeof(g_persons));
    FoldRegion(snap, whole, "g_personIds",
               g_personIds, sizeof(g_personIds));
    FoldRegion(snap, whole, "g_objects",
               g_objects, sizeof(g_objects));

    // Scene nodes: hash the full storage array, then the live flat-scan count.
    FoldRegion(snap, whole, "g_sceneNodes",
               g_sceneNodes, sizeof(g_sceneNodes));
    FoldRegion(snap, whole, "g_sceneNodeCount",
               &g_sceneNodeCount, sizeof(g_sceneNodeCount));

    FoldRegion(snap, whole, "g_buildingPersons",
               g_buildingPersons, sizeof(g_buildingPersons));

    // "Loaded" guard flags (game-loaded gates).
    FoldRegion(snap, whole, "g_personArrayLoaded",
               &g_personArrayLoaded, sizeof(g_personArrayLoaded));
    FoldRegion(snap, whole, "g_sceneArrayLoaded",
               &g_sceneArrayLoaded, sizeof(g_sceneArrayLoaded));

    // CRT RNG seed/state — the determinism anchor (crt::Srand seeds it). Reading
    // the live state pointer means the digest reflects how far the generator has
    // advanced, so two runs that consumed the generator identically match.
    const guild::u32* rng = guild::crt::RandStatePtr();
    const guild::u32 rngState = rng ? *rng : 0u;
    FoldRegion(snap, whole, "crt_rng_state",
               &rngState, sizeof(rngState));

    snap.hash = whole.value();
    return snap;
}

} // namespace

std::uint64_t HashWorldState() {
    return FoldWorld().hash;
}

WorldSnapshot SnapshotWorld() {
    return FoldWorld();
}

bool CompareSnapshots(const WorldSnapshot& a, const WorldSnapshot& b,
                      std::string* diffOut) {
    if (a.hash == b.hash)
        return true;

    if (diffOut) {
        char line[256];
        const std::size_t n = a.regions.size() < b.regions.size()
                                  ? a.regions.size()
                                  : b.regions.size();
        for (std::size_t i = 0; i < n; ++i) {
            if (a.regions[i].hash != b.regions[i].hash) {
                std::snprintf(line, sizeof(line),
                              "region '%s' differs: %016llx vs %016llx\n",
                              a.regions[i].label,
                              static_cast<unsigned long long>(a.regions[i].hash),
                              static_cast<unsigned long long>(b.regions[i].hash));
                *diffOut += line;
            }
        }
        if (a.regions.size() != b.regions.size()) {
            std::snprintf(line, sizeof(line),
                          "region count differs: %zu vs %zu\n",
                          a.regions.size(), b.regions.size());
            *diffOut += line;
        }
    }
    return false;
}

} // namespace guild::play
