#pragma once
// character_render4 — a fourth cluster of VIBE_Character_* leaves from gilde.exe: the
// remaining (genuinely untranslated) action-queue BUILDERS — CreateSoundActionEx and
// the take/drop-object builders and their "Alt" (left-hand) variants — plus a small
// set of script-command handlers (SitDown / GetUp / SitDownAtOnce / a live-actor
// count). These are 1:1 ports.
//
// The sound / play-sample / sample-loop / use-gate builders (0x405670 / 0x405838 /
// 0x40598c / 0x405bec) and their step handlers were ALREADY reconstructed in
// charaction_misc.{h,cpp}; this TU REUSES them (CreateSoundAction / CreatePlaySample
// Action / CreateSampleLoopAction) rather than redefining them. The Cmd* handlers call
// straight into those genuine siblings.
//
// The builders here share the recovered shape:
//   1. grab a free action node from the REAL VIBE_CharAction_QueueInsertEntry sibling
//      (guild::sim::QueueInsertEntry) — wired through a hook so the control flow is
//      testable in isolation, but in the live build it is the genuine pool allocator;
//   2. write the node's step-handler identity, the action-type byte (node+9), and a
//      handful of scratch fields (hand slot node+56, param node+48);
//   3. copy the action's NAME string into node+240 using the original's verbatim
//      STRIDE-2, byte-pairwise copy loop (a recovered quirk — see CopyNamePairwise);
//   4. (take) also copy the carried object's model name (node+304) when the source has
//      a model, else UNLINK the node (the REAL VIBE_ActionQueue_UnlinkEntry sibling);
//      (drop) writes both names (or a single null terminator when absent), no unlink.
//
// The step fn-ptr identities (PlaySampleActionUpdate @0x405740, TakeObjectActionUpdate
// @0x405c88, DropObjectActionUpdate @0x405f28) live in the charaction / charaction_misc
// cluster; to keep this TU free of those handlers while preserving the observable
// "which handler is bound" result, the builder records an ActionStepKind tag on the
// node's `step` slot. The node itself is the REAL reconstructed guild::sim::ActionNode.
//
// The Cmd* handlers reproduce the script-command entry points: the self-referential
// "am I being re-entered as the active chained command?" guard the original does
// (comparing the executing command's +44 step-fn against itself) and the
// invalid-character error path (VIBE_Script_ReportError). Both the script context and
// the live-actor table are injected so the deterministic control flow is testable.
#include "guild/common/types.h"

#include "sim/charaction.h"        // REAL ActionNode / QueueInsertEntry / UnlinkEntry
#include "sim/charaction_misc.h"   // REAL CreatePlaySampleAction / CreateSampleLoopAction

namespace guild::sim {

struct Character;             // character.h (reused)

// ===========================================================================
// Recovered constants.
//   Action-type bytes written to node+9 by the builders:
//     46 sound, 47 play-sample, 48 sample-loop, 49 take-object, 50 drop-object,
//     52 use-gate. (These match the charaction.h ActionTypeId family.)
//   node+376 default speed scalar 1065353216 == 1.0f (sound / play-sample paths).
//   The sitting gate (CreateSampleLoopAction) tests Character.flagsA & 0x10.
//   CreateTakeObject* set node+56 to 2 (right hand) / 1 (left hand); the carried
//   object's source name is read from src+492 -> +260.
//   CreateUseGate warns "ch_UseGate: room_id == ID_NONE" when node[94] (+376 dword
//   pre-write) is -1, then stores the speed at node+376.
// ===========================================================================
constexpr u8  kTypeSound        = 46;
constexpr u8  kTypeTakeObject   = 49;
constexpr u8  kTypeDropObject   = 50;
constexpr float kSpeedOne       = 1.0f;   // 1065353216
constexpr u8  kSittingFlag      = 0x10;   // Character.flagsA & 0x10

// The step-handler identity bound onto a freshly-built node. The original stores a
// raw fn-ptr; we record which handler so tests can assert it without pulling in the
// (separate) step bodies. Only the handlers THIS TU's builders bind are enumerated.
enum class ActionStepKind : int {
    kNone = 0, kSound, kTakeObject, kDropObject,
};
// Reads back the tagged step-handler identity a builder bound onto a node.
ActionStepKind StepKindOf(const ActionNode* node);

// ===========================================================================
// Name copy — the verbatim STRIDE-2 byte-pairwise loop the original builders use to
// copy an action's name into the node. It reads/writes bytes in pairs but advances
// the source/dest by 2 each iteration, stopping on the FIRST null of a pair (the
// odd byte of a pair is copied unconditionally, then the loop re-tests the even
// byte). For a normal C string this reproduces a plain strcpy; the pair structure is
// preserved for fidelity (and matters for embedded interior nulls). `dst` must hold
// the full source length + 1. Returns the number of bytes written (incl. the null).
int CopyNamePairwise(char* dst, const char* src);

// ===========================================================================
// Builder hook table. The ONE cross-cluster leaf the builders touch is the free-node
// allocator (and, for take/drop, the unlink-on-failure). The live wiring forwards
// these into the genuine guild::sim::QueueInsertEntry / UnlinkEntry; the default
// table is inert (allocator returns null -> builder early-outs, mirroring an
// exhausted pool).
// ===========================================================================
struct CharRender4Hooks {
    ActionNode* (*queueInsertEntry)(Character* ch);   // VIBE_CharAction_QueueInsertEntry 0x40c15c
    int         (*unlinkEntry)(ActionNode* node);     // VIBE_ActionQueue_UnlinkEntry     0x404370
};
void SetCharRender4Hooks(const CharRender4Hooks* hooks);
const CharRender4Hooks& GetCharRender4Hooks();

// ===========================================================================
// Action-queue builders.
// ===========================================================================

// gilde.exe 0x4056d8 — VIBE_Character_CreateSoundActionEx. Like the (already-done)
// CreateSoundAction @0x405670 but does NOT clear node+12, takes a final speed override
// written to node+376 AFTER the 1.0f default, and returns the node ptr (non-null) on
// success / 0 on failure. Type-46, binds the sound step, copies `name` into node+240.
ActionNode* CreateSoundActionEx(Character* ch, const char* name, int param, int speedBits);

// gilde.exe 0x405da8 / 0x405e68 — VIBE_Character_CreateTakeObjectAction[Alt]. Type-49:
// binds TakeObjectActionUpdate, +9=49, +40=0, +16=0, +52=0, +56=hand (2 normal /
// 1 alt), +20=owner, copies `name` into +240. Then, IFF the source object `src` has
// a valid model (src+492 -> +260 name): copies that object name into node+304 and
// returns 1; otherwise UNLINKS the node and returns 0. `srcObjName` is the recovered
// src+492+260 string (null == no model -> unlink). `hand` is 2 (normal) or 1 (alt).
int CreateTakeObjectAction(Character* ch, const char* name, const char* srcObjName, int hand);

// gilde.exe 0x40602c / 0x4060dc — VIBE_Character_CreateDropObjectAction[Alt]. Type-50:
// binds DropObjectActionUpdate, +9=50, +40=0, +16=0, +52=0, +56=hand, +20=owner,
// +48=param. Copies `name` into +240 (or writes a single null if `name`==null), then
// copies `objName` into node+304 (or a single null if `objName`==null). Always
// returns 1 on a successful insert (no unlink path). `hand` is 2 (normal) / 1 (alt).
int CreateDropObjectAction(Character* ch, int param, const char* name,
                           const char* objName, int hand);

// ===========================================================================
// Script-command handlers.
// ===========================================================================
// Minimal script-execution context the Cmd* handlers branch on (mirrors the engine
// globals dword_62E8A8 currentCtx / dword_62E8CC execCmd). `execStepIsSelf` models
// the original's `*(execCmd+44) == <this handler>` re-entry test; `actionHead` is the
// character's +296 action-queue head (nonzero => an action is already pending);
// `chainLatchByte` is *(currentCtx+2564) (==1 => latch this handler as the chained
// command at currentCtx+2528). The handler returns the original's int result and
// reports via the injected error sink (VIBE_Script_ReportError) when the actor is
// null. `*latched` is set true when the handler stored itself as the chained cmd.
//
// `actor` is the resolved Character* (the original's *handle); `sampleName` is the
// sit/stand sample the create call uses. When `actor != nullptr` the handler genuinely
// invokes the matching builder (CreatePlaySampleAction / CreateSampleLoopAction) — so
// the SitDownAtOnce seek-flag and the latch decision are observed on the REAL node.
struct CmdScriptCtx {
    bool        hasExecCmd;     // dword_62E8CC != 0
    bool        execStepIsSelf; // *(execCmd+44) == this handler
    bool        actionHead;     // *(actor+296) != 0  (only read when execStepIsSelf)
    int         chainLatchByte; // *(currentCtx+2564)
    Character*  actor;          // the resolved Character* (null == invalid)
    const char* sampleName;     // sit/stand sample name for the create call
    void      (*reportError)(const char* msg);  // VIBE_Script_ReportError leaf
};

// gilde.exe 0x43d6b8 — VIBE_Character_CmdSitDown. If re-entered as the active chained
// command and the actor still has a pending action, re-latch and return 0. Else, on a
// valid actor, create a play-sample action (the sit-down sample) and — when the
// chain-latch byte is 1 — latch this handler. Invalid actor -> report + return 1.
// `*latched` => latched this handler; `*created` => the play-sample node was inserted.
int CmdSitDown(const CmdScriptCtx& c, bool* latched, bool* created);

// gilde.exe 0x43d7c4 — VIBE_Character_CmdGetUp. As CmdSitDown but creates a
// sample-LOOP action (the stand-up loop) instead of a play-sample action.
int CmdGetUp(const CmdScriptCtx& c, bool* latched, bool* created);

// gilde.exe 0x43d738 — VIBE_Character_CmdSitDownAtOnce. As CmdSitDown but ALSO sets
// the created play-sample node's +396 bit 0 (the "seek to final frame at once" flag);
// `*seekFlagSet` reports whether that bit was applied (only when a node was created).
int CmdSitDownAtOnce(const CmdScriptCtx& c, bool* latched, bool* created, bool* seekFlagSet);

// gilde.exe 0x43db90 — VIBE_Character_CmdCharacterCount. Counts live actors in the
// 512-slot table whose universe (+136) equals the active-scene tag (off_649D64).
// The table + active tag are injected; pure deterministic count. Returns the count.
int CmdCharacterCount(Character* const* table, int count, const void* activeUniverse);

} // namespace guild::sim
