#include "util/bitset.h"

namespace guild::util {

namespace {
// gilde.exe 0x1428dae — VIBE_Math_UAddCarry: *out = a+b; return carry-out.
// Inlined here (trivial, owned by the math module) to keep this unit self-contained.
inline int UAddCarry(u32 a, u32 b, u32* out) {
    u32 sum = a + b; // 32-bit wraparound is intentional
    int carry = (sum < a || sum < b) ? 1 : 0;
    *out = sum;
    return carry;
}
} // namespace

// gilde.exe 0x1426de9 — VIBE_BitSet_TestRange
int BitSetTestTail(const u32* words, int bit) {
    // mask = ~(0xFFFFFFFF << (31 - bit%32)) -> low (31 - bit%32) bits set.
    // These are the bits strictly below the rounding bit within its word.
    int shift = 31 - bit % 32;
    u32 mask = ~(0xFFFFFFFFu << shift);
    if ((mask & words[bit / 32]) == 0) {
        int w = bit / 32 + 1;
        if (w >= kBitSetWords)
            return 1;
        const u32* p = words + w;
        while (*p == 0) {
            if (++w >= kBitSetWords)
                return 1;
            ++p;
        }
    }
    return 0;
}

// gilde.exe 0x1426e32 — VIBE_BitSet_AtomicClearRange
int BitSetAddRoundCarry(u32* words, int bit) {
    int wi = bit / 32;
    // Add 1 at the rounding bit (MSB-first: position (31 - bit%32) from LSB).
    int result = UAddCarry(words[wi], 1u << (31 - bit % 32), &words[wi]);
    // Propagate the carry toward more-significant (lower-index) words.
    for (int w = wi - 1; w >= 0 && result; --w)
        result = UAddCarry(words[w], 1u, &words[w]);
    return result;
}

} // namespace guild::util
