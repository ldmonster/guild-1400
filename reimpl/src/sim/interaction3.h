#pragma once
// interaction3 — the social/handler "orientation & move-target" slice of the Guild
// interaction system (gilde.exe, VIBE_Interaction_* family, 0x46bxxx..0x470bxx
// range). These are the geometry/targeting helpers the per-action evaluators call:
//
//   * OrientToActionTarget27..34 / OrientQuadToTarget33,35 — after the actor "uses"
//     an inventory object on a found target, they scale a per-action spin/visual
//     register (flt_B58xxx output globals) by the target object's scale field
//     (+0x0C) and a fixed 0.5 half-factor (flt_61A4xx) according to the action's
//     spin result (+1 / -1 / 0). The Quad variants write a 4-tuple; the scalar
//     variants write a single output.
//   * ComputeMoveTargetA/B/Social/SocialAlt — pick an approach vector for a social
//     move action by consulting a configured law record (Gesetz_GetRecord) and
//     delegating to the AiScore relation scorers (distance vs. weighted).
//   * ComputeApproachOffset — the half/×10 approach-offset scaler.
//   * EvalRestSlotFree — the "rest slot" planner-method predicate (action 26).
//
// The panel/drag slice (interaction2.*), the social-eval evaluators
// (interaction_eval.*), and the context-menu executors (interaction_handlers.*,
// contextaction2.*) are separate already-translated slices; this file does NOT
// redefine their layouts or globals.
//
// Recovered constants:
//   flt_61A4CC..flt_61A4E4 == 0x3F000000 == 0.5f  (the "half" spin factor; the
//     OrientToActionTarget30/31 variants use NO factor, i.e. an implicit 1.0).
//   flt_B581E0..flt_B58570 — per-action base spin magnitudes (BSS, zero at load,
//     written by config at runtime). Modeled as the `baseSpin` hook input.
//   flt_B58xxx output globals (flt_B58200/294/328/.../586B8) — the spin/visual
//     registers the originals write. Modeled as the OrientOutput record the module
//     owns so tests can observe the result.
//   flt_B57244/4C/54/5C — the 4-column direction table (row stride 37 dwords) the
//     Quad35 variant indexes by the action's direction byte. Modeled via the hook
//     `quadColumn` (returns one column value for a given (column, row)).
//   byte_B57210 — the 61-entry planner method catalog (148-byte stride) walked by
//     EvalRestSlotFree; modeled via the `methodCatalogByte`/`methodCatalogFloat`
//     hooks (same catalog ai/score.h models abstractly).
//
// Cross-module leaves are routed through an installable Interaction3Hooks struct
// with inert defaults defined in interaction3.cpp:
//   VIBE_GameObject_QueryFind        0x5857fc
//   VIBE_Inventory_FindSlotByItemId  0x54f04c
//   VIBE_Item_UseObjectAction        0x5671f4  (returns result + spin direction)
//   VIBE_Gesetz_GetRecord            0x4c244c  (law record: "active" flag at +0x18)
//   VIBE_AiScore_ComputeRelationWeighted   0x4796b0
//   VIBE_AiScore_ComputeRelationDistance   0x479a4c
//   VIBE_AiMethod_SelectBestRecursive      0x46965c
//
// Translated functions (absolute addresses, imagebase 0x400000):
//   0x46b654 ComputeApproachOffset       0x46dc80 ComputeMoveTargetA
//   0x46e2c4 ComputeMoveTargetB          0x470998 ComputeMoveTargetSocial
//   0x470b68 ComputeMoveTargetSocialAlt  0x46eb10 EvalRestSlotFree
//   0x46ee50 OrientToActionTarget27      0x46f1fc OrientToActionTarget28
//   0x46f4d0 OrientToActionTarget29      0x46f758 OrientToActionTarget30
//   0x46f9b0 OrientToActionTarget31      0x46fe58 OrientToActionTarget32
//   0x4708f0 OrientToActionTarget34      0x470510 OrientQuadToTarget33
//   0x470a30 OrientQuadToTarget35
#include "guild/common/types.h"

namespace guild::sim {

// ===========================================================================
// The "half" spin factor common to most Orient variants. In the binary these are
// eight separate read-only floats flt_61A4CC..flt_61A4E4, all == 0.5f.
// ===========================================================================
constexpr float kSpinHalfFactor = 0.5f;

// ===========================================================================
// Found-target / acting-object views. The Orient functions receive:
//   actorCtx  — the acting person ctx (read +0x178 == +376: its node key for the
//               GameObject query).
//   event     — the action event; event+4 is the target node-id used by QueryFind,
//               and (for the multi-arg variants) event+4 / +8 / +16 carry the
//               UseObjectAction parameter words.
// QueryFind returns an object record; the originals read its scale field at byte
// +0x0C (== float index 3). We expose a typed object handle.
// ===========================================================================
struct ActionObject {
    float scale = 0.0f;          // object record +0x0C (the spin multiplier source)
    int   itemId = 0;            // *record (the item-id word fed to FindSlotByItemId)
    bool  present = false;       // QueryFind found a node
};

struct ActionEvent {
    int actorNodeKey = 0;        // actorCtx + 0x178 (376)
    int targetNodeId = 0;        // event + 4
    int param1 = 0;              // event + 4  (UseObjectAction v[1] for multi variants)
    int param2 = 0;              // event + 8  (v[2] for the quad variants)
    int directionRow = 0;        // event[4]  (Quad35 direction-table row index)
};

// ===========================================================================
// Output spin/visual registers (the flt_B58xxx globals the originals write). Owned
// by the module so tests can observe the result. Scalar variants write `s[0]`; the
// Quad variants write the 4-tuple `s[0..3]`.
// ===========================================================================
struct OrientOutput {
    float s[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};
extern OrientOutput g_orientOut;
void ResetOrientOutput();

// ===========================================================================
// Cross-module leaves (inert defaults defined in interaction3.cpp).
// ===========================================================================
struct Interaction3Hooks {
    // VIBE_GameObject_QueryFind @0x5857fc — locate the action target object for the
    // actor's node key and the event's target node id. Default: not found.
    ActionObject (*queryFind)(int actorNodeKey, int targetNodeId) = nullptr;
    // VIBE_Inventory_FindSlotByItemId @0x54f04c — true iff the item has a usable
    // inventory slot (the originals only test the pointer for null). Default: true.
    bool (*hasInventorySlot)(int itemId) = nullptr;
    // VIBE_Item_UseObjectAction @0x5671f4 — perform the action; returns the call
    // result (the originals require == 1 to proceed) and writes the spin direction
    // (+1 / -1 / 0) into *spinDir. Default: result 1, spinDir 0 (no spin).
    int (*useObjectAction)(int actorCtx, const ActionEvent* ev, int* spinDir) = nullptr;

    // VIBE_Gesetz_GetRecord @0x4c244c — load law record `lawId`; returns the "active"
    // flag at record+0x18 (the only field the move-target fns read). Default: 0.
    int (*lawRecordActive)(int lawId) = nullptr;
    // ComputeMoveTargetA/B record clause: the original takes the weighted branch when
    //   !lawActive  ||  (*(BYTE*)(record+2) == 5 && (*(BYTE*)(record+456) & 2) != 0).
    // This predicate models the second disjunct (the object class==5 & flag test).
    // Default: false.
    bool (*objectForcesWeighted)(int record) = nullptr;
    // VIBE_AiScore_ComputeRelationDistance @0x479a4c — distance scorer. Writes the
    // approach vector to *outX/*outY and returns its byte result. Default: 0.
    int (*relationDistance)(float* outX, float* outY, char relFlag, int lawId) = nullptr;
    // VIBE_AiScore_ComputeRelationWeighted @0x4796b0 — weighted scorer. Default: 0.
    int (*relationWeighted)(float* outX, float* outY, int record, char relFlag,
                            int extra, int a5, int a6) = nullptr;

    // VIBE_AiMethod_SelectBestRecursive @0x46965c — planner method selector. Returns
    // a method id (0 == none). Default: 0.
    int (*selectBestMethod)(int actionId, int subject) = nullptr;
    // byte_B57210[148*method + 8*k] catalog walk for EvalRestSlotFree:
    //   methodCatalogByte(method,k)  == i[48]  / v9[112] (the "enabled/occupied" byte)
    //   methodCatalogFloat(method,k) == *((float*)i+13) / +29 (the slot-free score)
    // EvalRestSlotFree succeeds only if BOTH 4-slot scans run to completion.
    u8    (*methodCatalogByte)(int method, int column, int k) = nullptr;
    float (*methodCatalogFloat)(int method, int column, int k) = nullptr;

    // ComputeApproachOffset's ×10-vs-×1 gate: the original tests two record bytes,
    //   factor == 10.0  iff  *(BYTE*)(record+359) != 0  &&  *(BYTE*)(record+358) == 0
    //   factor ==  1.0  otherwise.
    // Modeled as a predicate on the record. Default: false (factor 1.0).
    bool (*approachFactorIsTen)(int record) = nullptr;

    // flt_B581E0..flt_B58570 base spin magnitudes (BSS, runtime-configured). The
    // originals read flt_B58<var> directly; here `baseSpin(action)` supplies it.
    float (*baseSpin)(int action) = nullptr;
    // flt_B57244/4C/54/5C direction-table value for (column 0..3, row). Quad35 only.
    float (*quadColumn)(int column, int row) = nullptr;
};
extern Interaction3Hooks g_i3Hooks;
void ResetInteraction3Hooks();

// ===========================================================================
// Translated functions.
// ===========================================================================

// 0x46b654 — compute the approach offset. If `skip` (a5) is set: write the
// "infinitely far" sentinel (-1e30, -1e30) to (*outX,*outY) and return 0. Else run
// the weighted scorer, then multiply both outputs by 10.0 (record+0x167 byte set &
// record+0x166 byte clear) or 1.0 otherwise; returns 1.
int ComputeApproachOffset(float* outX, float* outY, int record, char relFlag,
                          char skip, int a6, int a7);

// 0x46dc80 / 0x46e2c4 — social move target via law record 23 / 20. Zero the outputs;
// if the law record is inactive, OR the object is class 5 with flag (record+456)&2,
// use the weighted scorer; else use the distance scorer. Returns the scorer result.
int ComputeMoveTargetA(float* outX, float* outY, int record, char relFlag, int a5, int a6);
int ComputeMoveTargetB(float* outX, float* outY, int record, char relFlag, int a5, int a6);

// 0x470998 / 0x470b68 — social move target via law record 16 / 17, gated on the
// action code (record+0x10 == event code) being in 28..34. Zero the outputs; reject
// (0) outside the range; if the law record is active use the distance scorer, else
// the weighted scorer. Returns the scorer result (0 if out of range).
int ComputeMoveTargetSocial(float* outX, float* outY, int record, char relFlag,
                            int actionCode, int a5, int a6);
int ComputeMoveTargetSocialAlt(float* outX, float* outY, int record, char relFlag,
                               int actionCode, int a5, int a6);

// 0x46eb10 — the "rest slot free" planner predicate for action 26. If `armed` is
// set, return 0. Otherwise select the best method for (26, subject); if a method was
// chosen and BOTH of its two 4-slot column scans run to completion (no enabled byte
// and no non-negative score before 4 steps), copy the two scratch vectors out and
// return the method's result; otherwise return 26.
int EvalRestSlotFree(int subject, void* outA, char armed, void* outB);

// 0x46ee50/.../0x4708f0 — scalar orient: find the target object, require an
// inventory slot, run the action; on spin +1 write s[0] = base * scale, on spin -1
// write s[0] = -(base * scale) * half (action 30/31 omit the half factor). Returns
// the action code (27..34) on success, 0 on any early-out.
int OrientToActionTarget27(int actorCtx, const ActionEvent* ev);
int OrientToActionTarget28(int actorCtx, const ActionEvent* ev);
int OrientToActionTarget29(int actorCtx, const ActionEvent* ev);
int OrientToActionTarget30(int actorCtx, const ActionEvent* ev);
int OrientToActionTarget31(int actorCtx, const ActionEvent* ev);
int OrientToActionTarget32(int actorCtx, const ActionEvent* ev);
int OrientToActionTarget34(int actorCtx, const ActionEvent* ev);

// 0x470510 — quad orient (action 33): writes a 4-tuple s[0..3] = colBase[k] * scale
// (or negated × half on spin -1). The four base values come from baseSpin packed as
// columns; here OrientQuadToTarget33 uses baseSpin(33) for all four (the originals'
// flt_B58558/60/68/70 are four BSS slots set by config — modeled as one base).
int OrientQuadToTarget33(int actorCtx, const ActionEvent* ev);
// 0x470a30 — quad orient (action 35): like 33 but the four base values come from
// the direction table flt_B57244/4C/54/5C indexed by ev->directionRow.
int OrientQuadToTarget35(int actorCtx, const ActionEvent* ev);

} // namespace guild::sim
