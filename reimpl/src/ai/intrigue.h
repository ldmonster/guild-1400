#pragma once
// AI intrigue scoring + action-label rules for the Guild simulation (gilde.exe).
//
// Two faithful, self-contained cores:
//
// 1. The AiScore relation-weighted scorers (0x4796b0 / 0x479898 / 0x479a4c).
//    These compute a pair of float scores (own-relation A, rival-relation B) for
//    a candidate AI method by summing, over the method's 4+4 target sub-entries,
//    the product of a per-sub-entry weight and the person's per-class relation
//    weight, divided by an "eligible / not" scalar (40/50 vs 50/100). They read:
//      * the method-table entry byte_B57210[148*class] (modeled in ai/types.h),
//      * the person scratch record (method class @+301, visited-bits @+532,
//        relation weights @+144 + 12*k).
//    We accept these as plain buffers so the scorer is exercised byte-faithfully.
//
// 2. The intrigue action-label evaluators (0x4715d8 / 0x471fa0 / 0x471ae0).
//    Tiny return-code rules: if the formatter accepted the action, return the
//    action's "label code" (39 / 41 ...), else a reject code. The formatters
//    themselves (string building) are deferred; the return-code rule is ported.
#include <cstring>
#include "guild/common/types.h"
#include "ai/types.h"

namespace guild::ai {

// --- method sub-entry view ---------------------------------------------------
// Each 148-byte method entry holds two 4-element arrays of 8-byte sub-entries
// (the "own" set at +40 and the "rival" set at +104 in the entry, indexed in the
// scorer from a base pointer walked +8 each step). Per sub-entry the scorer reads:
//   +5  i8  enable/sign byte (>=0 => counted; the v14[48]>=0 / v14[112]>=0 test)
//   +5  (>>24 of the dword at +45/+109) the sub-entry's class id (compared to the
//       person's current method class at +301)
//   +13th float (offset +52 within the entry block, i.e. *((float*)v14 + 13)) the
//       sub-entry weight.
// To stay faithful we operate directly on the raw 148-byte entry bytes plus a
// small person scratch; the offsets below name what each access means.

// Person scratch the scorers read (the `a3`/`v7` base). Only three regions used:
//   +301 i32   current method class (the >>24 high byte is the class id)
//   +532 u8    "visited / eligible bits" (AND'd with the entry's gate mask @+142)
//   +144 .. f  per-class relation weights, stride 12 (read at +144 + 12*classId)
struct PersonScoreScratch {
    u8 bytes[640] = {};

    i32 method_class_raw() const {
        int o = 301;
        return static_cast<i32>(bytes[o] | (bytes[o + 1] << 8) | (bytes[o + 2] << 16)
             | (static_cast<u32>(bytes[o + 3]) << 24));
    }
    int method_class() const { return method_class_raw() >> 24; }
    u8 visited_bits() const { return bytes[532]; }
    float relation_weight(int classId) const {
        float f;
        int o = 144 + 12 * classId;
        std::memcpy(&f, &bytes[o], sizeof(f));
        return f;
    }
};

// Result of a relation-weighted score: two float sums + the "eligible" flag.
struct RelationScore {
    float a = 0.0f;   // own-relation accumulator (*a1)
    float b = 0.0f;   // rival-relation accumulator (*a2)
    bool eligible = false; // the `v9` "this method is in scope" flag
};

// gilde.exe 0x4796b0 — AiScore_ComputeRelationWeighted. Eligibility-gated /40,/50
// vs /50,/100 weighting; sums both A and B over all 4+4 sub-entries regardless of
// class match (each scaled by /v23 if the sub-entry class matches the person's,
// else /v24). `entry` is the 148-byte method-table entry; `cls` is the method
// class (used to read the gate mask at entry+142).
RelationScore ComputeRelationWeighted(const u8* entry148, const PersonScoreScratch& p);

// gilde.exe 0x479898 — AiScore_ComputeRelationOwn. Same eligibility gate but only
// counts sub-entries whose class MATCHES the person's current class (others
// contribute 0); single divisor v19 (40 if eligible-by-visited, else 50).
RelationScore ComputeRelationOwn(const u8* entry148, const PersonScoreScratch& p);

// --- intrigue action-label return-code rules ---------------------------------
// gilde.exe 0x4715d8 — AiIntrigue_EvalActionLabel: returns 39 if the action
// formatter accepted, else the reject code from Interaction_EvalRejectStub
// (modeled as a hook). `formatted` is the formatter's accept/reject result.
u8 EvalActionLabel(bool formatted, u8 rejectCode);

// gilde.exe 0x471fa0 — AiIntrigue_EvalSlanderLabel: returns 45 if the slander
// formatter accepted, else the reject code from Interaction_EvalRejectStub.
u8 EvalSlanderLabel(bool formatted, u8 rejectCode);

// gilde.exe 0x471ae0 — AiPlayer_EvalPamphlet: if ExecPamphlet succeeded, emit the
// pamphlet command (modeled via a hook) and return 41; else 0. `execOk` is the
// ExecPamphlet result; the command emission is reported through the hook.
using PamphletCmdFn = void (*)(i32 targetId);
void SetPamphletCmdHook(PamphletCmdFn fn);
u8 EvalPamphlet(bool execOk, i32 targetId);

} // namespace guild::ai
