#pragma once
// gilde.exe — shared infrastructure for the large AiMethod social/behavior Eval
// handlers (the score-table-driven ones). namespace guild::ai.
//
// The big Eval* functions in interaction_eval.{h,cpp} (VIBE_Interaction_Eval*)
// all share three building blocks recovered from the binary:
//
//  1. A 24-byte "action descriptor frame" (the v15/v16 BYREF blocks every Eval
//     fills and `qmemcpy(...,0x18u)`s out). Field layout recovered from the field
//     writes: +0 byte = frame kind/opcode, +4 dword arg0, +8 dword arg1/flag,
//     +16 dword arg2, +20 float weight/score. The planner copies it verbatim.
//
//  2. The behavior planner `VIBE_AiMethod_SelectBestRecursive` @0x46965c, which the
//     Eval fns call as SelectBestRecursive(classId, personId, frameA, mode, frameB)
//     and which returns the chosen-action byte (0 == nothing chosen). The
//     class-table-driven recursion + score-vector decay reads the runtime-loaded
//     byte_B57210 method table (cold IDB = 0). Here it is a mockable hook so the
//     Eval RULES (the score computation + slot selection + frame build) are
//     exercised exactly; the planner core itself is in ai/score.{h,cpp}.
//
//  3. `VIBE_AiObject_ClassifyAccessibleItems` @0x47a79c — walks the actor's owned
//     scene objects and, for each requested item-id (a gesture/talk/flirt string
//     id), records whether a matching usable object exists. It fills an N-slot
//     candidate table (6 dwords/slot: [0]=item-id word, [+2]=available dword (>0 or
//     -1), [+4]=object-id dword) and a 3-int summary {accessibleCount, blocked,
//     usable}. This is an object-search leaf (render/inventory entangled), routed
//     through a mockable hook so the slot-selection logic is testable.
//
// The score/eval-constant TABLES the Eval fns embed are the per-action item-id
// lists (the gesture/talk/flirt/drink/insult/gesture string-ids) and the frame
// score floats; both are recovered byte-for-byte in interaction_eval.cpp.
#include <cstring>
#include "guild/common/types.h"

namespace guild::ai {

// ---------------------------------------------------------------------------
// 24-byte action descriptor frame (the Eval fns' v15/v16 BYREF blocks).
// Modeled as 6 dwords; accessors mirror the binary's byte/dword/float writes.
// ---------------------------------------------------------------------------
struct EvalFrame {
    u32 w[6] = {0, 0, 0, 0, 0, 0};

    void set_kind(u8 k) { w[0] = (w[0] & 0xFFFFFF00u) | k; }  // LOBYTE(frame[0]) = k
    u8   kind() const { return static_cast<u8>(w[0] & 0xFFu); }
    void set_arg0(i32 v) { w[1] = static_cast<u32>(v); }      // frame[1]
    void set_arg1(i32 v) { w[2] = static_cast<u32>(v); }      // frame[2]
    void set_arg2(i32 v) { w[4] = static_cast<u32>(v); }      // frame[4]
    void set_scoreBits(u32 bits) { w[5] = bits; }             // frame[5] (raw float bits)
    void set_scoreF(float f) { u32 b; std::memcpy(&b, &f, 4); w[5] = b; }
};

// The score floats the Eval fns write into frame[5] (recovered as raw int bits).
//   1028443341 == 0.05f  (the social-gesture/talk/flirt/drink/insult/group weight)
//   1054951342 == 0.44f  (the patrol/destination weight)
//   1046562734 == 0.22f  (the "go to building 20" weight in pick/attack)
//   1059648963 == 0.66f  (the equip-weapon fallback weight)
constexpr u32 kScoreBits005 = 1028443341u; // 0.05f
constexpr u32 kScoreBits044 = 1054951342u; // 0.44f
constexpr u32 kScoreBits022 = 1046562734u; // 0.22f
constexpr u32 kScoreBits066 = 1059648963u; // 0.66f

// ---------------------------------------------------------------------------
// Candidate item table built by ClassifyAccessibleItems (6 dwords / slot).
// ---------------------------------------------------------------------------
struct ItemSlot {
    u16 itemId   = 0;  // slot[0] (word) — requested gesture/talk/etc. string id
    u16 pad      = 0;
    i32 available = 0; // slot[2] (>0 == usable here, -1 == present-but-blocked, 0 == absent)
    i32 objectId = 0;  // slot[4] — resolved scene-object id (when available)
    i32 pad3 = 0;
    i32 pad4 = 0;
    i32 pad5 = 0;
};
// The 3-int summary a3 the classifier fills: {accessibleNodes, blockedCount, usableCount}.
struct ItemSummary {
    i32 accessibleNodes = 0; // a3[0] — # non-type-9 scene nodes scanned
    i32 blockedCount    = 0; // a3[1] — # requested items present-but-unusable
    i32 usableCount     = 0; // a3[2] — # requested items usable
};

// ClassifyAccessibleItems hook: given the actor (person index/ptr opaque), the
// requested item-id list (filled into `slots[0..n).itemId` by the caller), fill in
// each slot's availability/objectId and the summary. Default: marks nothing usable
// (all absent) → matches a cold actor with no accessible objects.
using ClassifyItemsFn = void (*)(const void* actor, int n, ItemSlot* slots,
                                 ItemSummary* summary);
void SetClassifyItemsHook(ClassifyItemsFn fn);
ClassifyItemsFn ClassifyItemsHook();

// The planner hook (VIBE_AiMethod_SelectBestRecursive call shape used by the Eval
// fns). Returns the chosen-action byte (0 == reject). Default: returns frameA.kind
// (i.e. "accept the proposed action") so the RULES' frame-build + copy-out path is
// exercised. Tests can override to force reject / specific selection.
using SelectBestFn = u8 (*)(u8 classId, int personId, EvalFrame* frameA,
                            int mode, EvalFrame* frameB);
void SetSelectBestHook(SelectBestFn fn);
SelectBestFn SelectBestHook();

// Reset both hooks to defaults (call between tests).
void ResetEvalHooks();

// gilde.exe 0x58b89c — VIBE_Math_RandomModulo, re-exported for the Eval fns (they
// all derive their random slot offsets through it). Forwards to ai::RandomModulo.
int EvalRandomModulo(u16 n);

} // namespace guild::ai
