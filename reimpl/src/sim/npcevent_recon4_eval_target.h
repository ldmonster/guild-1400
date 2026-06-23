#pragma once
// ===========================================================================
// npcevent_recon4_eval_target — AiAction best-person-target scoring/selection,
// reconstructed 1:1 from gilde.exe.
//
//   gilde.exe 0x475dd0 — VIBE_AiAction_EvalBestPersonTarget   (__userpurge,
//                          eax = retval; edx:eax in = two out-float ptrs,
//                          ecx = action record, bl = relArg, retn 0Ch)
//
// SCOPE: the candidate-iteration + selection logic and the final score-gate
// multiply, reconstructed exactly from the disassembly at 0x475dd0 (the Hex-Rays
// output reported "local variable allocation failed", so the loop body and the
// candidate-pick comparison were recovered from the asm).
//
// COUPLED LEAVES (supplied via EvalTargetHooks; inert defaults):
//   - VIBE_Person_FindRecordById        0x58bc6c  (id -> PersonRec*)
//   - VIBE_Character_IsActiveType       0x45263c  (pre-check predicate)
//   - VIBE_He_FindFirstHandlerByFilter  0x4c63f8  (existing-claim check)
//   - VIBE_AiScore_ComputeRelationWeighted 0x4796b0 (relation-table scoring;
//        148-byte-stride global byte_B57210 — its own scoring sub-tree)
// ===========================================================================

#include <guild/common/types.h>

namespace guild {
namespace sim {

using f32 = float;
using f64 = double;

// Person record, by-value reconstruction with the exact original byte offsets
// touched by EvalBestPersonTarget. Only the fields the function reads are
// modelled; layout offsets are documented for provenance.
// gilde.exe person record (subset used by 0x475dd0).
struct EvalPersonRec {
    u16 filterId;     // +0x00   (u16) -> He filter key
    u8  typeTag;      // +0x02   (u8)  5 == special / excluded
    u8  flag9;        // +0x09   (u8)  pre-check gate
    u8  activeCount;  // +0x08   (u8)  must be > 0
    u16 rank;         // +0x0A   (u16) selection key (greatest wins)
    f32 threshold;    // +0x20   (float) (rank+4) must be >= this
    u8  busyFlag;     // +0x164  (u8)  must be 0
};

// The AiAction record fields read by EvalBestPersonTarget.
//   action[+2]  (u8)  : when != 5, the pre-check on the primary person runs.
//   action[+0x5C] id  : the primary person id (pre-check).
//   slots 0..4 ids    : person ids resolved in the loop. In the binary the loop
//                       walks ecx from action+0x0C to action+0x20 step 4 and
//                       reads [slot+0x5C]; i.e. action offsets 0x68,0x6C,0x70,
//                       0x74,0x78.
struct EvalActionRec {
    u8  typeTag;        // action[+2]
    i32 primaryId;      // action[+0x5C]
    i32 slotIds[5];     // action[+0x68 .. +0x78]
};

struct EvalTargetHooks {
    // 0x58bc6c — returns a record pointer for an id, or null. The pointer is
    // opaque to the math; we round-trip it as void* and the caller maps it.
    const EvalPersonRec* (*findRecordById)(i32 id, void* user) = nullptr;
    // 0x45263c — VIBE_Character_IsActiveType: nonzero if the record is an
    // active-character type.
    int (*isActiveType)(const EvalPersonRec* rec, void* user) = nullptr;
    // 0x4c63f8 — VIBE_He_FindFirstHandlerByFilter(2,2,filterId,0,0x5E):
    // nonzero if a handler already claims this person (candidate rejected).
    int (*findFirstHandler)(u16 filterId, void* user) = nullptr;
    // 0x4796b0 — fills *out0,*out1 from the relation table; returns its int.
    int (*computeRelationWeighted)(f32* out0, f32* out1,
                                   const EvalActionRec* action, char relArg,
                                   int a5, int a6, int a7, void* user) = nullptr;
    void* user = nullptr;
};

// 0xF149F2CA — the sentinel written to both outputs when earlyOut (a4) is set.
constexpr u32 kEvalSentinelBits = 0xF149F2CAu;

// gilde.exe 0x475dd0 — VIBE_AiAction_EvalBestPersonTarget.
//   out0,out1 : the two float outputs (eax,edx targets).
//   action    : the AiAction record.
//   relArg    : bl (a3) -> relation-table row selector for the scoring leaf.
//   earlyOut  : a4 — when nonzero, write the sentinel and bail with 0.
//   a5,a6     : passthrough args (a5=arg_4, a6=arg_8) forwarded to the scorer.
// Returns 1 when a target was selected and scored, else 0.
int VIBE_AiAction_EvalBestPersonTarget(f32* out0, f32* out1,
                                       const EvalActionRec& action,
                                       char relArg, bool earlyOut,
                                       int a5, int a6,
                                       const EvalTargetHooks& hooks);

} // namespace sim
} // namespace guild
