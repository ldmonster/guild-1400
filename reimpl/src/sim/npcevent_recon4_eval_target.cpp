// ===========================================================================
// npcevent_recon4_eval_target — implementation. Recovered from the disassembly
// of gilde.exe 0x475dd0 (Hex-Rays could not allocate locals cleanly there).
// ===========================================================================
#include "npcevent_recon4_eval_target.h"

#include <cstring>

namespace guild {
namespace sim {

// gilde.exe 0x475dd0 — VIBE_AiAction_EvalBestPersonTarget.
int VIBE_AiAction_EvalBestPersonTarget(f32* out0, f32* out1,
                                       const EvalActionRec& action,
                                       char relArg, bool earlyOut,
                                       int a5, int a6,
                                       const EvalTargetHooks& hooks) {
    // if ( a4 ) { *out0 = *out1 = sentinel; return 0; }            /*0x475de8*/
    if (earlyOut) {
        u32 bits = kEvalSentinelBits;                               /*0x475e75*/
        std::memcpy(out0, &bits, 4);
        std::memcpy(out1, &bits, 4);                                /*0x475e7b*/
        return 0;                                                   /*0x475e6f*/
    }

    // Pre-check: only when action[+2] != 5.                         /*0x475df2*/
    if (action.typeTag != 5) {
        const EvalPersonRec* rec =
            hooks.findRecordById ? hooks.findRecordById(action.primaryId, hooks.user)
                                 : nullptr;                          /*0x475e86*/
        if (rec) {                                                   /*0x475e8d*/
            // if rec.type==5 -> reject                              /*0x475e93*/
            if (rec->typeTag == 5)
                return 0;                                            /*0x475e97*/
            // if rec.flag9 == 0 && IsActiveType(rec) -> reject      /*0x475e99*/
            if (rec->flag9 == 0) {
                int active = hooks.isActiveType
                                 ? hooks.isActiveType(rec, hooks.user)
                                 : 0;                                /*0x475ea3*/
                if (active != 0)
                    return 0;                                        /*0x475eb0*/
            }
            // otherwise fall through to the loop.
        }
        // rec == null -> fall through to the loop.
    }

    // Candidate loop over the 5 person slots (action+0x68..+0x78).  /*0x475df8*/
    const EvalPersonRec* best = nullptr;                            /*0x475dfb  esi=0 */
    for (int i = 0; i < 5; ++i) {
        const EvalPersonRec* rec =
            hooks.findRecordById ? hooks.findRecordById(action.slotIds[i], hooks.user)
                                 : nullptr;                          /*0x475e03*/
        if (!rec)                                                    /*0x475e0e*/
            continue;
        if (rec->activeCount == 0)                                   /*0x475e12 jbe*/
            continue;
        // (double)((u16)rank + 4) >= threshold  (skip when below)   /*0x475e25..2f*/
        if ((f64)((u32)rec->rank + 4) < (f64)rec->threshold)
            continue;
        if (rec->busyFlag != 0)                                      /*0x475e31*/
            continue;
        // Existing-claim check.                                     /*0x475e48*/
        int handler = hooks.findFirstHandler
                          ? hooks.findFirstHandler(rec->filterId, hooks.user)
                          : 0;
        if (handler != 0)                                            /*0x475e52*/
            continue;
        // Selection: first candidate, or strictly-greater rank wins.
        if (best == nullptr) {                                       /*0x475e54*/
            best = rec;                                              /*0x475e58*/
        } else {
            // cmp (u16)rec.rank > (u16)best.rank   (signed jg)       /*0x475eb2*/
            if ((int)(u16)rec->rank > (int)(u16)best->rank)
                best = rec;                                          /*0x475e58*/
        }
    }

    if (best == nullptr)                                             /*0x475e61*/
        return 0;                                                    /*0x475e63*/

    // Score the chosen action via the relation-weighted leaf.
    *out0 = 0.0f;                                                    /*0x475ed2*/
    *out1 = 0.0f;                                                    /*0x475edf*/
    // The original sign-extends a4/a5 bytes for the scorer's a5/a6 (they are
    // unused by the scorer, which only consumes relArg); forwarded faithfully.
    if (hooks.computeRelationWeighted) {
        hooks.computeRelationWeighted(out0, out1, &action, relArg,
                                      a5 >> 24, a6, /*a7=*/0, hooks.user); /*0x475ef8*/
    }

    // Final gate multiply: v17 = (double)((rank+4) >= threshold) -> 0.0 or 1.0
    f32 gate = ((f64)((u32)best->rank + 4) >= (f64)best->threshold) ? 1.0f : 0.0f; /*0x475f23*/
    *out0 = *out0 * gate;                                           /*0x475f2b*/
    *out1 = gate * *out1;                                           /*0x475f38*/
    return 1;                                                       /*0x475f3a*/
}

} // namespace sim
} // namespace guild
