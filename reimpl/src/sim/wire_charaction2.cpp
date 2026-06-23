// See wire_charaction2.h. Binds the four CharAction step-leaf bridges
// (CharActionStep5..8Hooks) onto their real reconstructed siblings. The only
// type-faithful real binding across the four tables is the RNG draw
// (`randomModulo` -> util::RandomModulo, VIBE_Math_RandomModulo @0x58b89c); every
// other field is an engine-handler-record (HeRecord*) leaf with no byte-faithful
// native-record adapter and is left at its inert default (see the header).
//
// This file contains NO step logic — only the indirection that connects the
// already-translated step coroutines to the real RNG sibling.
#include "sim/wire_charaction2.h"

#include "sim/charaction_steps5.h"  // CharActionStep5Hooks / Set/GetCharActionStep5Hooks
#include "sim/charaction_steps6.h"  // CharActionStep6Hooks / ...
#include "sim/charaction_steps7.h"  // CharActionStep7Hooks / ...
#include "sim/charaction_steps8.h"  // CharActionStep8Hooks / ...
#include "util/math_random.h"       // util::RandomModulo (the real RNG sibling)

namespace guild::sim {

namespace {

// Adapter for the bridges' `int(*)(int n)` randomModulo field. The real sibling
// VIBE_Math_RandomModulo @0x58b89c takes the count in AX (16-bit). Verified by
// disasm @0x58b89c: `mov ebx, eax` then `mov cx, bx` — the 32-bit argument is
// TRUNCATED to its low 16 bits (`test bx,bx`; ecx = zero-extended bx), then
// `idiv ecx` gives RandNext() % (truncated count); returns 0 when the truncated
// count is 0. There is NO clamp in the original — it is a plain 16-bit truncation.
// So the faithful adapter forwards `(u16)n` directly (RandomModulo itself returns
// 0 for a zero count). Every call site across the four batches passes a small
// positive count (2, 3, 6, 10, 16, 30, 100, 105, 256), so the truncation is
// value-preserving in practice; reproducing the exact truncation keeps the
// adapter 1:1 for any dynamic count (e.g. steps7 `randomModulo(v5.count)`).
int RealRandomModulo(int n) {
    return util::RandomModulo(static_cast<u16>(n));  // 0x58b89c: AX-truncated count
}

} // namespace

void InstallRealCharAction2Wiring() {
    // Each table: rebase on the library's COMPLETE inert default (passing nullptr to
    // the setter installs it), copy it out, override only the real RNG field, then
    // re-install the copy. Process-lifetime storage backs each installed table.
    // (The step .cpp bodies call hook members without per-field null checks, so the
    // remaining inert fields MUST stay valid — copying the inert table preserves
    // them.)

    // --- CharActionStep5Hooks (steps5.h) -------------------------------------
    {
        SetCharActionStep5Hooks(nullptr);                 // = complete inert default
        static CharActionStep5Hooks h = GetCharActionStep5Hooks();
        h.randomModulo = &RealRandomModulo;               // 0x58b89c (real)
        SetCharActionStep5Hooks(&h);
    }

    // --- CharActionStep6Hooks (steps6.h) -------------------------------------
    {
        SetCharActionStep6Hooks(nullptr);
        static CharActionStep6Hooks h = GetCharActionStep6Hooks();
        h.randomModulo = &RealRandomModulo;               // 0x58b89c (real)
        SetCharActionStep6Hooks(&h);
    }

    // --- CharActionStep7Hooks (steps7.h) -------------------------------------
    {
        SetCharActionStep7Hooks(nullptr);
        static CharActionStep7Hooks h = GetCharActionStep7Hooks();
        h.randomModulo = &RealRandomModulo;               // 0x58b89c (real)
        SetCharActionStep7Hooks(&h);
    }

    // --- CharActionStep8Hooks (steps8.h) -------------------------------------
    {
        SetCharActionStep8Hooks(nullptr);
        static CharActionStep8Hooks h = GetCharActionStep8Hooks();
        h.randomModulo = &RealRandomModulo;               // 0x58b89c (real)
        SetCharActionStep8Hooks(&h);
    }
}

} // namespace guild::sim
