#include "ai/score.h"

#include <cstring>

namespace guild::ai {

namespace {
// The planner's "ever set" test on the criterion-A best: (bits & 0x7FFFFFFF)!=0.
// (Mirrors `LODWORD(v25) & 0x7FFFFFFF` in the binary — true unless the float is
// exactly +0.0 or -0.0, i.e. it was actually updated from the -1e30 sentinel.)
bool ScoreEverSet(float f) {
    u32 bits;
    std::memcpy(&bits, &f, sizeof(bits));
    return (bits & 0x7FFFFFFFu) != 0;
}
} // namespace

// gilde.exe 0x46965c — VIBE_AiMethod_SelectBestRecursive
//   (__userpurge: al=, edx=personIndex, ecx=outA, bl=, +stack outB)
//
// Iterative port of the catalog scan (the original "recursion" is the same scan
// guarded by the method stack: it pushes a candidate, evaluates, pops, and the
// best-of-two tracking is identical at every depth, so one pass with the stack
// gate reproduces the observable selection). For each enabled, different-class,
// not-already-stacked method: push id, eval, conditionally apply, track best-by-A
// and best-by-B, pop. Then choose A's winner when A was set and A>=B, else B's.
u8 Planner::SelectBest(int personIndex, ActionFrame* outA, ActionFrame* outB) {
    float bestA = -1e30f;   // v25
    float bestB = -1e30f;   // v24
    u8 resultA = 0;         // v31 — chosen-action byte for criterion A
    u8 resultB = 0;         // v28 — chosen-action byte for criterion B
    ActionFrame frameA_primary, frameA_secondary; // v13 / v18 (criterion-A frames)
    ActionFrame frameB_primary, frameB_secondary; // v17 / v14 (criterion-B frames)

    for (int i = 0; i < static_cast<int>(methods_.size()); ++i) {
        const Method& m = methods_[i];

        // enable byte (byte_B57210[148*i]) and class-difference gate. The binary
        // gates a candidate ONLY on the enable byte and the class difference — it
        // does NOT test the id for zero (it would still push/eval an id-0 method),
        // so we mirror exactly that (no `m.id == 0` guard).
        if (!m.enabled)
            continue;
        if (m.classId == currentClass_)
            continue;
        // Skip methods already on the stack (the recursion's "in progress" guard).
        if (stack_.Contains(m.id))
            continue;

        // Push this method id (byte_B5965C[count++]); eval; will pop after.
        stack_.Push(m.id);

        EvalResult r;
        if (m.eval)
            r = m.eval(m, personIndex);

        if (r.produced && (r.scoreA >= bestA || r.scoreB >= bestB)) {
            u8 chosen = 0;
            if (m.apply)
                chosen = m.apply(m, personIndex, r);
            if (chosen) {
                if (r.scoreA >= bestA) {
                    bestA = r.scoreA;
                    resultA = chosen;
                    frameA_primary = r.frameA;
                    frameA_secondary = r.frameB;
                }
                if (r.scoreB >= bestB) {
                    bestB = r.scoreB;
                    resultB = chosen;
                    frameB_primary = r.frameA;
                    frameB_secondary = r.frameB;
                }
            }
        }

        stack_.Pop();
    }

    // Final choice: criterion A wins when it was ever set and A >= B.
    if (ScoreEverSet(bestA) && bestA >= bestB) {
        if (outA) *outA = frameA_primary;
        if (outB) *outB = frameA_secondary;
        return resultA;
    }
    if (outA) *outA = frameB_primary;
    if (outB) *outB = frameB_secondary;
    return resultB;
}

} // namespace guild::ai
