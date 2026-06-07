// gilde.exe — entity record sub-refcount step (guild::app). See entity_movement.h.
// 1:1 reconstruction of VIBE_GameLogic_Movement @0x412f18 (d2_SubRefcount).
#include "app/entity_movement.h"

namespace guild::app {

// The exact message string the original passes to VIBE_ErrorLog_ReportMessage
// (gilde.exe aD2SubrefcountI @0x610e8c).
static const char* const kSubRefcountMsg = "d2_SubRefcount: invalid refcount!";

// gilde.exe 0x412f18 — VIBE_GameLogic_Movement.
void EntitySubRefcount(EntityMoveRecord* records, int index,
                       const std::function<void(const char*)>& onUnderflow) {
    int v1 = index;

    // v2 = records[index].type;  (record = base + 84*index, +60)
    int type = records[index].type;
    if (type == 5 || type == 8) {
        // if ( !records[index].suppress ) v1 = records[index].linkIndex;
        if (records[index].suppress == 0)
            v1 = records[index].linkIndex;
    }

    // v5 = records[v1].refcount;  (+64)
    int v5 = records[v1].refcount;
    if (v5 <= 0) {
        // VIBE_ErrorLog_ReportMessage("d2_SubRefcount: invalid refcount!");
        if (onUnderflow)
            onUnderflow(kSubRefcountMsg);
    } else {
        records[v1].refcount = v5 - 1;
    }
}

} // namespace guild::app
