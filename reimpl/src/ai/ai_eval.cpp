#include "ai/ai_eval.h"

#include "ai/method.h"

namespace guild::ai {

// Default ClassifyAccessibleItems: nothing accessible (cold actor). The caller has
// already seeded slots[i].itemId; we leave availability 0 and zero the summary.
static void DefaultClassify(const void* /*actor*/, int n, ItemSlot* slots,
                            ItemSummary* summary) {
    for (int i = 0; i < n; ++i) {
        slots[i].available = 0;
        slots[i].objectId = 0;
    }
    summary->accessibleNodes = 0;
    summary->blockedCount = 0;
    summary->usableCount = 0;
}

// Default SelectBestRecursive: accept the proposed primary action (its kind byte).
// In the binary the planner can also return a different chosen method; tests that
// need that override this hook.
static u8 DefaultSelectBest(u8 /*classId*/, int /*personId*/, EvalFrame* frameA,
                            int /*mode*/, EvalFrame* /*frameB*/) {
    return frameA ? frameA->kind() : 0;
}

static ClassifyItemsFn g_classify = DefaultClassify;
static SelectBestFn    g_select   = DefaultSelectBest;

void SetClassifyItemsHook(ClassifyItemsFn fn) { g_classify = fn ? fn : DefaultClassify; }
ClassifyItemsFn ClassifyItemsHook() { return g_classify; }
void SetSelectBestHook(SelectBestFn fn) { g_select = fn ? fn : DefaultSelectBest; }
SelectBestFn SelectBestHook() { return g_select; }

void ResetEvalHooks() {
    g_classify = DefaultClassify;
    g_select = DefaultSelectBest;
}

int EvalRandomModulo(u16 n) { return RandomModulo(n); }

} // namespace guild::ai
