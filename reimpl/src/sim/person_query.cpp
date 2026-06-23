#include "sim/person_query.h"

#include "sim/entity.h"

// gilde.exe 0x5929f0 — VIBE_Person_QueryByGoodType. The original is a 5-way
// switch on (goodType - 1); each arm calls
//   VIBE_Person_QueryBegin(seed, 1, 5, key)
// with a good-type-specific key, and the default arm returns null. We reconstruct
// it as a table lookup over the existing PersonQueryBegin (entity.cpp, 0x586c20),
// which takes the filter array + count.
//
// NOTE on `seed` (the original's a2/esi): VIBE_Person_QueryBegin takes the seed
// as its first vararg (esi=a1), used to set dword_6498BC before the iterator
// re-bases to the array head (dword_6498DC = dword_13CE298). The current C++
// PersonQueryBegin(filters, count) wrapper iterates from the array base
// regardless of seed (the original's iterator likewise re-bases to head before
// the first IterNext), so the seed does not change which records match; it is
// accepted here for call-site fidelity and documented as a no-op for the match
// set. (If a future seed-sensitive iterator lands, thread `seed` through.)

namespace guild::sim {

// 0x592a0f / 0x592a1f / 0x592a2f / 0x592a3f / 0x592a4f — the op-5 keys.
const int kPersonGoodTypeKey[5] = { 15, 23, 24, 25, 26 };

ObjectRec* PersonQueryByGoodType(u8 goodType, int seed) {
    (void)seed; // see header note: forwards as PersonQueryBegin seed (match-neutral)

    // 0x5929f3: v2 = a1 - 1; switch (v2). Cases 0..4 valid; default -> null.
    int idx = static_cast<int>(goodType) - 1;
    if (idx < 0 || idx > 4)
        return nullptr; // 0x592a02 default

    // Each arm: Person_QueryBegin(seed, 1, 5, key) -> one filter {op:5, key}.
    PersonFilter filter{ 5, kPersonGoodTypeKey[idx] };
    return PersonQueryBegin(&filter, 1);
}

} // namespace guild::sim
