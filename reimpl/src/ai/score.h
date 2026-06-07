#pragma once
// Score-table behavior planner for the Guild AI (gilde.exe).
//   AiMethod_SelectBestRecursive  0x46965c  (the planner)
//   AiMethod_SelectBestForPerson  0x469248  (top-level per-person entry)
//
// The planner walks a 61-entry method catalog (byte_B57210, 148-byte stride). For
// each enabled candidate method whose class differs from the person's current
// method class AND which is not already on the method stack, it:
//   1. pushes the method id onto the stack (byte_B5965C[count++]),
//   2. calls the method's eval fn (tbl[9]) which fills two 24-byte action frames
//      and writes two float scores: scoreA (criterion A) and scoreB (criterion B),
//   3. if either score beats the running best, calls the apply fn (tbl[10]) which
//      returns the chosen-action descriptor byte (0 == rejected),
//   4. tracks best-by-A and best-by-B independently, pops the stack, recurses.
// Finally it picks the criterion-A winner when scoreA >= scoreB (and scoreA's bit
// pattern is non-zero, i.e. it was ever set), else the criterion-B winner. It
// copies the winning frames out and returns the chosen-action byte.
//
// In the binary the eval/apply fns are entry function pointers (tbl[9]/tbl[10]);
// here each catalog entry carries C++ callbacks with the same contract so the
// planner logic is exercised exactly. The score-vector decay (MemMove +80<-+48)
// is modeled in ExecuteChosen.
#include <vector>
#include "guild/common/types.h"
#include "ai/types.h"
#include "ai/methodstack.h"

namespace guild::ai {

// 24-byte action descriptor frame the eval fn fills (two 12-byte halves in the
// original: v15/v16). We keep it opaque; the planner only copies it.
struct ActionFrame {
    u8 bytes[24] = {};
};

// Result of an eval call: the two criterion scores + the two frames.
struct EvalResult {
    float scoreA = -1e30f;   // criterion A (v22 in the planner)
    float scoreB = -1e30f;   // criterion B (v19)
    ActionFrame frameA;      // v15 (primary action frame)
    ActionFrame frameB;      // v16 (secondary frame)
    bool produced = false;   // eval fn ran and yielded a candidate
};

// A catalog method. `id`/`classId`/`enabled` mirror byte_B57210[+0]/(>>24)/enable.
// `eval` corresponds to tbl[9]; `apply` to tbl[10] (returns the chosen-action byte,
// 0 == reject). `apply` may be null (treated as "no apply fn", candidate skipped).
struct Method {
    u8   id      = 0;
    u8   classId = 0;
    bool enabled = false;
    EvalResult (*eval)(const Method& m, int personIndex) = nullptr;  // tbl[9]
    u8 (*apply)(const Method& m, int personIndex, EvalResult& r) = nullptr; // tbl[10]
};

// The method catalog + planner. Holds the 61-entry table and the shared method
// stack (the originals are file globals byte_B57210 + dword_B59658).
class Planner {
public:
    Planner() : methods_(kMethodTableCount) {}

    Method& method(int i) { return methods_[i]; }
    const Method& method(int i) const { return methods_[i]; }
    int method_count() const { return static_cast<int>(methods_.size()); }

    MethodStack& stack() { return stack_; }

    // The person's "current method class" — compared against each candidate's
    // class so the planner won't re-pick a same-class method. In the binary this
    // is (person[+303] >> 24). Set per person before planning.
    void set_current_class(u8 c) { currentClass_ = c; }
    u8   current_class() const { return currentClass_; }

    // VIBE_AiMethod_SelectBestRecursive @0x46965c — run the planner over the
    // catalog for `personIndex`. Fills `outA`/`outB` with the winning primary /
    // secondary frames and returns the chosen-action byte (0 == nothing chosen).
    u8 SelectBest(int personIndex, ActionFrame* outA, ActionFrame* outB);

private:
    std::vector<Method> methods_;
    MethodStack stack_;
    u8 currentClass_ = 0;
};

} // namespace guild::ai
