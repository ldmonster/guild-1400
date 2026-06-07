#pragma once
// Determinism harness (PLAYABLE_PLAN P4/P7) — the measurement oracle.
//
// Since there is no Wine/original-binary oracle available, the reconstruction
// proves correctness by SELF-CONSISTENCY: the same seed + the same inputs must
// drive the live world to a byte-identical state across runs. This file is the
// tool that measures that. It computes a stable 64-bit FNV-1a hash over the live
// world/entity state — the reconstructed entity arrays (g_persons, g_objects,
// g_sceneNodes, g_buildingPersons) plus the key scalar globals (scene-node count,
// loaded flags, and the CRT RNG state). It also snapshots a state and diffs two
// snapshots field/region by region so a mismatch can be localized.
//
// It reads the REAL globals via extern (declared in sim/entity.h,
// sim/building_lifecycle.h, crt/rand.h) — it never redefines them.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace guild::play {

// 64-bit FNV-1a. Offset basis / prime are the standard 64-bit constants. The
// hash folds bytes in array order, so it is deterministic and order-stable: the
// same world state always yields the same digest, and any single changed byte
// flips the digest with very high probability (avalanche of FNV-1a).
class Fnv1a64 {
public:
    static constexpr std::uint64_t kBasis = 1469598103934665603ull;
    static constexpr std::uint64_t kPrime = 1099511628211ull;

    Fnv1a64() = default;

    // Fold a raw byte region.
    Fnv1a64& bytes(const void* data, std::size_t n);
    // Fold a trivially-copyable scalar (fixed width, no padding surprises).
    template <class T>
    Fnv1a64& scalar(const T& v) { return bytes(&v, sizeof(T)); }

    std::uint64_t value() const { return h_; }

private:
    std::uint64_t h_ = kBasis;
};

// A measured snapshot of the live world. `hash` is the whole-world digest;
// `regions` carries a per-region sub-hash + label so two snapshots can be
// diffed to localize where they diverge.
struct WorldSnapshot {
    struct Region {
        const char*   label;
        std::uint64_t hash;
        std::size_t   bytes;
    };
    std::uint64_t       hash = 0;   // whole-world digest
    std::vector<Region> regions;    // per-region digests, in fold order
};

// Hash the entire live world state. The fold order is fixed (persons, person
// ids, objects, scene nodes + count, building-person slots, loaded flags, RNG
// state) so the digest is order-stable and reproducible.
std::uint64_t HashWorldState();

// Same fold, but also records each region's sub-hash for diffing.
WorldSnapshot SnapshotWorld();

// Diff two snapshots. Returns true if identical (same whole-world hash). When
// they differ, `diffOut` (if non-null) is filled with human-readable lines
// naming each region whose sub-hash mismatches.
bool CompareSnapshots(const WorldSnapshot& a, const WorldSnapshot& b,
                      std::string* diffOut = nullptr);

} // namespace guild::play
