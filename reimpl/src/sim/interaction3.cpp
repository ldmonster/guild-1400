// interaction3 — social/handler orientation & move-target slice. See interaction3.h.
// 1:1 translation of the VIBE_Interaction_* 0x46bxxx..0x470bxx orientation family.
#include "sim/interaction3.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// Module-owned output + hooks (defined ONCE here — the library home).
// ---------------------------------------------------------------------------
OrientOutput     g_orientOut;
Interaction3Hooks g_i3Hooks;

void ResetOrientOutput()     { g_orientOut = OrientOutput{}; }
void ResetInteraction3Hooks() { g_i3Hooks = Interaction3Hooks{}; }

namespace {

// --- inert-default leaf wrappers ------------------------------------------
inline ActionObject QueryFind(int actorNodeKey, int targetNodeId) {
    return g_i3Hooks.queryFind ? g_i3Hooks.queryFind(actorNodeKey, targetNodeId)
                               : ActionObject{};
}
inline bool HasInventorySlot(int itemId) {
    return g_i3Hooks.hasInventorySlot ? g_i3Hooks.hasInventorySlot(itemId) : true;
}
inline int UseObjectAction(int actorCtx, const ActionEvent* ev, int* spinDir) {
    if (g_i3Hooks.useObjectAction)
        return g_i3Hooks.useObjectAction(actorCtx, ev, spinDir);
    *spinDir = 0;
    return 1;
}
inline int LawRecordActive(int lawId) {
    return g_i3Hooks.lawRecordActive ? g_i3Hooks.lawRecordActive(lawId) : 0;
}
inline bool ObjectForcesWeighted(int record) {
    return g_i3Hooks.objectForcesWeighted ? g_i3Hooks.objectForcesWeighted(record) : false;
}
inline int RelationDistance(float* x, float* y, char rel, int lawId) {
    return g_i3Hooks.relationDistance ? g_i3Hooks.relationDistance(x, y, rel, lawId) : 0;
}
inline int RelationWeighted(float* x, float* y, int rec, char rel, int extra, int a5, int a6) {
    return g_i3Hooks.relationWeighted
               ? g_i3Hooks.relationWeighted(x, y, rec, rel, extra, a5, a6)
               : 0;
}
inline int SelectBestMethod(int actionId, int subject) {
    return g_i3Hooks.selectBestMethod ? g_i3Hooks.selectBestMethod(actionId, subject) : 0;
}
inline u8 MethodCatalogByte(int method, int col, int k) {
    return g_i3Hooks.methodCatalogByte ? g_i3Hooks.methodCatalogByte(method, col, k) : 0;
}
inline float MethodCatalogFloat(int method, int col, int k) {
    return g_i3Hooks.methodCatalogFloat ? g_i3Hooks.methodCatalogFloat(method, col, k) : 0.0f;
}
inline bool ApproachFactorIsTen(int record) {
    return g_i3Hooks.approachFactorIsTen ? g_i3Hooks.approachFactorIsTen(record) : false;
}
inline float BaseSpin(int action) {
    return g_i3Hooks.baseSpin ? g_i3Hooks.baseSpin(action) : 0.0f;
}
inline float QuadColumn(int col, int row) {
    return g_i3Hooks.quadColumn ? g_i3Hooks.quadColumn(col, row) : 0.0f;
}

// --- shared scalar-orient body ---------------------------------------------
// All OrientToActionTarget* share the same shape; only the action code, the base
// magnitude source, and whether the negative branch applies the half factor differ.
//   spin == +1 : s[0] =  base * scale
//   spin == -1 : s[0] = -(base * scale) [* half, unless useHalf == false]
//   spin ==  0 : leave s[0] unchanged
// Returns `action` on success, 0 on any early-out.
int OrientScalar(int actorCtx, const ActionEvent* ev, int action, bool useHalf) {
    ActionObject obj = QueryFind(ev->actorNodeKey, ev->targetNodeId);
    if (!obj.present)
        return 0;
    if (!HasInventorySlot(obj.itemId))
        return 0;
    int spin = 0;
    if (UseObjectAction(actorCtx, ev, &spin) != 1)
        return 0;

    float base = BaseSpin(action);
    if (spin == 1) {
        g_orientOut.s[0] = base * obj.scale;
        return action;
    }
    if (spin != -1)
        return action;
    float v = base * obj.scale;
    g_orientOut.s[0] = useHalf ? -(v) * kSpinHalfFactor : -(v);
    return action;
}

} // namespace

// 0x46b654 — VIBE_Interaction_ComputeApproachOffset
int ComputeApproachOffset(float* outX, float* outY, int record, char relFlag,
                          char skip, int a6, int a7) {
    if (skip) {
        *outX = -1.0e30f;
        *outY = -1.0e30f;
        return 0;
    }
    *outX = 0.0f;
    *outY = 0.0f;
    // factor = (record+359 != 0 && record+358 == 0) ? 10.0 : 1.0  (modeled predicate).
    float factor = ApproachFactorIsTen(record) ? 10.0f : 1.0f;
    RelationWeighted(outX, outY, record, relFlag, 0, a6, a7);
    *outX = *outX * factor;
    *outY = factor * *outY;
    return 1;
}

// 0x46dc80 — VIBE_Interaction_ComputeMoveTargetA  (law record 23)
int ComputeMoveTargetA(float* outX, float* outY, int record, char relFlag, int a5, int a6) {
    *outX = 0.0f;
    *outY = 0.0f;
    int active = LawRecordActive(23);
    // !active  ||  (object class==5 && (record+456)&2)  -> weighted, else distance.
    if (!active || ObjectForcesWeighted(record))
        return RelationWeighted(outX, outY, record, relFlag, 0, a5, a6);
    return RelationDistance(outX, outY, relFlag, 23);
}

// 0x46e2c4 — VIBE_Interaction_ComputeMoveTargetB  (law record 20)
int ComputeMoveTargetB(float* outX, float* outY, int record, char relFlag, int a5, int a6) {
    *outX = 0.0f;
    *outY = 0.0f;
    int active = LawRecordActive(20);
    if (!active || ObjectForcesWeighted(record))
        return RelationWeighted(outX, outY, record, relFlag, 0, a5, a6);
    return RelationDistance(outX, outY, relFlag, 20);
}

// 0x470998 — VIBE_Interaction_ComputeMoveTargetSocial  (law record 16)
int ComputeMoveTargetSocial(float* outX, float* outY, int record, char relFlag,
                            int actionCode, int a5, int a6) {
    *outX = 0.0f;
    *outY = 0.0f;
    if (actionCode < 28 || actionCode > 34)
        return 0;
    int active = LawRecordActive(16);
    if (active)
        return RelationDistance(outX, outY, relFlag, 16);
    return RelationWeighted(outX, outY, record, relFlag, 0, a5, a6);
}

// 0x470b68 — VIBE_Interaction_ComputeMoveTargetSocialAlt  (law record 17)
int ComputeMoveTargetSocialAlt(float* outX, float* outY, int record, char relFlag,
                               int actionCode, int a5, int a6) {
    *outX = 0.0f;
    *outY = 0.0f;
    if (actionCode < 28 || actionCode > 34)
        return 0;
    int active = LawRecordActive(17);
    if (active)
        return RelationDistance(outX, outY, relFlag, 17);
    return RelationWeighted(outX, outY, record, relFlag, 0, a5, a6);
}

// 0x46eb10 — VIBE_Interaction_EvalRestSlotFree (action 26)
// Two 4-slot column scans (columns 6 and 14 in the original — i[48]/(float)i+13
// then v9[112]/(float)v9+29 over byte_B57210[148*method], cursor i += 8 BYTES per
// step). Each scan walks up to 4 entries (8-byte stride); a scan that hits
// an "enabled" byte or a non-negative score before its 4th step stops the success
// path. The action succeeds (copies both scratch vectors, returns the method result)
// only when BOTH scans run to completion.
int EvalRestSlotFree(int subject, void* outA, char armed, void* outB) {
    if (armed)
        return 0;
    int method = SelectBestMethod(26, subject);
    if (!method)
        return 26;

    // First scan: column 6 (offset +48 byte / +13 float, stride 8 dwords).
    int n = 0;
    for (; MethodCatalogByte(method, 6, n) || MethodCatalogFloat(method, 6, n) >= 0.0f; ) {
        if (++n >= 4)
            break;
    }
    if (n < 4)
        return 26;   // first scan stopped early -> no rest slot

    // Second scan: column 14 (offset +112 byte / +29 float).
    int m = 0;
    while (MethodCatalogByte(method, 14, m) || MethodCatalogFloat(method, 14, m) >= 0.0f) {
        ++m;
        if (m >= 4) {
            // Both scans completed -> the rest slot is free; emit the vectors.
            if (outA) {
                float* a = static_cast<float*>(outA);
                a[0] = 0.0f; a[1] = 0.0f; a[2] = 1.0f; a[3] = 0.0f; a[4] = 0.0f; a[5] = 0.0f;
            }
            if (outB) {
                float* b = static_cast<float*>(outB);
                b[0] = 0.0f; b[1] = 0.0f; b[2] = 1.0f; b[3] = 0.0f; b[4] = 0.0f; b[5] = 0.0f;
            }
            return method;
        }
    }
    return 26;
}

// 0x46ee50 — VIBE_Interaction_OrientToActionTarget27  (half factor)
int OrientToActionTarget27(int actorCtx, const ActionEvent* ev) {
    return OrientScalar(actorCtx, ev, 27, /*useHalf=*/true);
}
// 0x46f1fc — VIBE_Interaction_OrientToActionTarget28  (half factor)
int OrientToActionTarget28(int actorCtx, const ActionEvent* ev) {
    return OrientScalar(actorCtx, ev, 28, /*useHalf=*/true);
}
// 0x46f4d0 — VIBE_Interaction_OrientToActionTarget29  (half factor)
int OrientToActionTarget29(int actorCtx, const ActionEvent* ev) {
    return OrientScalar(actorCtx, ev, 29, /*useHalf=*/true);
}
// 0x46f758 — VIBE_Interaction_OrientToActionTarget30  (NO half factor)
int OrientToActionTarget30(int actorCtx, const ActionEvent* ev) {
    return OrientScalar(actorCtx, ev, 30, /*useHalf=*/false);
}
// 0x46f9b0 — VIBE_Interaction_OrientToActionTarget31  (NO half factor)
int OrientToActionTarget31(int actorCtx, const ActionEvent* ev) {
    return OrientScalar(actorCtx, ev, 31, /*useHalf=*/false);
}
// 0x46fe58 — VIBE_Interaction_OrientToActionTarget32  (half factor)
int OrientToActionTarget32(int actorCtx, const ActionEvent* ev) {
    return OrientScalar(actorCtx, ev, 32, /*useHalf=*/true);
}
// 0x4708f0 — VIBE_Interaction_OrientToActionTarget34  (half factor)
int OrientToActionTarget34(int actorCtx, const ActionEvent* ev) {
    return OrientScalar(actorCtx, ev, 34, /*useHalf=*/true);
}

// 0x470510 — VIBE_Interaction_OrientQuadToTarget33  (half factor on negative)
int OrientQuadToTarget33(int actorCtx, const ActionEvent* ev) {
    ActionObject obj = QueryFind(ev->actorNodeKey, ev->targetNodeId);
    if (!obj.present)
        return 0;
    if (!HasInventorySlot(obj.itemId))
        return 0;
    int spin = 0;
    if (UseObjectAction(actorCtx, ev, &spin) != 1)
        return 0;

    float base = BaseSpin(33);
    if (spin == 1) {
        for (int k = 0; k < 4; ++k)
            g_orientOut.s[k] = base * obj.scale;
        return 33;
    }
    if (spin != -1)
        return 33;
    for (int k = 0; k < 4; ++k)
        g_orientOut.s[k] = -(base * obj.scale * kSpinHalfFactor);
    return 33;
}

// 0x470a30 — VIBE_Interaction_OrientQuadToTarget35  (direction-table columns)
int OrientQuadToTarget35(int actorCtx, const ActionEvent* ev) {
    ActionObject obj = QueryFind(ev->actorNodeKey, ev->targetNodeId);
    if (!obj.present)
        return 0;
    if (!HasInventorySlot(obj.itemId))
        return 0;
    int spin = 0;
    if (UseObjectAction(actorCtx, ev, &spin) != 1)
        return 0;

    int row = ev->directionRow;   // flt_B57244[37*row] etc.
    if (spin == 1) {
        for (int k = 0; k < 4; ++k)
            g_orientOut.s[k] = QuadColumn(k, row) * obj.scale;
        return 35;
    }
    if (spin != -1)
        return 35;
    for (int k = 0; k < 4; ++k)
        g_orientOut.s[k] = -(QuadColumn(k, row) * obj.scale * kSpinHalfFactor);
    return 35;
}

} // namespace guild::sim
