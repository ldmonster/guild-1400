#pragma once
// Per-actor action system for the Guild simulation (gilde.exe) — the action-node
// substrate, the action-type registry, and the CharAction_* builders/helpers.
//
// This is the data layer and the action *catalog* that the coroutine driver in
// actionqueue.{h,cpp} runs over and that the step handlers in character.{h,cpp}
// implement. See recon/04_sim.md §1 "Character record" and §4 "Character action
// queue node".
//
// Translated functions (this TU):
//   VIBE_ActionQueue_GetFreeEntry      0x40431c   (pool allocator)
//   VIBE_ActionQueue_UnlinkEntry       0x404370   (unlink + free)
//   VIBE_ActionQueue_ClearAll          0x4043dc
//   VIBE_ActionQueue_ValidateLinks     0x40442c
//   VIBE_CharAction_QueueInsertEntry   0x40c15c
//   VIBE_CharAction_InsertActionVararg 0x40c1e4
//   VIBE_ActionQueue_InsertAction      0x404470   (link-after variant)
//   VIBE_Character_DeclareAction       0x405558   (registry fill)
//   VIBE_CharAction_RegisterHandlers   0x40be30   (catalog registration)
//   VIBE_CharAction_QueueShutdown      0x40c07c
//   VIBE_CharAction_CancelForObject    0x40c3c8
#include "guild/common/types.h"

namespace guild::sim {

struct Character; // character.h

// ===========================================================================
// Action-queue node  (gilde.exe pool dword_62CEFC @0x62CEFC)
// ---------------------------------------------------------------------------
// The original allocates one big block of 1280 nodes of stride 404 (0x194):
//   alloc size 0x7E400 = 517120 = 404 * 1280  (VIBE_CharAction_RegisterHandlers).
// GetFreeEntry linear-scans the pool treating node[0] (the step fn ptr) as the
// "occupied" marker: 0 == free. UnlinkEntry zeroes the whole 404-byte node.
//
// The node is a coroutine frame. DispatchCurrent calls node->step(char) once per
// frame; node->next (a "chained / next-phase" fn) is run when present and the
// motion gate clears (see actionqueue.cpp). node->ready must be nonzero to run.
//
// Field offsets recovered from DispatchCurrent (0x404768), QueueInsertEntry
// (0x40c15c), InsertActionVararg (0x40c1e4), InsertAction (0x404470),
// UnlinkEntry (0x404370), CheckDurationExpiry (0x40bdd8), the step handlers, and
// CancelForObject (0x40c3c8, which reads node[5]==+20 as the owner ptr).
// ---------------------------------------------------------------------------
constexpr int kActionNodeStride   = 404;   // 0x194
constexpr int kActionNodeCapacity = 1280;

struct ActionNode;
// Step / chained function signature. The original called node[0]/node[4] as
// __fastcall(char*) coroutine steps; we pass the node (which carries owner+args).
using ActionStepFn = void (*)(ActionNode* node);

struct ActionNode {
    ActionStepFn step;       // +0x00   step fn (also the free-slot marker: null == free)
    ActionStepFn chained;    // +0x04   chained / next-phase fn (run after motion gate)
    u8  ready;               // +0x08   ready flag (must be nonzero to dispatch)
    u8  type;                // +0x09   action-type id (0..63)
    u8  pad0A[2];            // +0x0A   padding
    i32 callCount;           // +0x0C   invocation counter (0 == first call)
    u8  state;               // +0x10   per-action state byte (cleared on (re)insert)
    u8  pad11[3];            // +0x11   padding
    Character* owner;        // +0x14   owning character (+20)
    u8  pad18[16];           // +0x18   scratch (turn anim build buffer etc.)
    ActionNode* prev;        // +0x24   intrusive list prev (+36)
    ActionNode* next_link;   // +0x28   intrusive list next (+40)
    i32 args[64];            // +0x2C   variadic arg slots (+44); arg[0]==+44 ...
    // Named aliases into args[] for the handlers we translate:
    //   +0x30 (+48) arg[1]  : duration ticks (CheckDurationExpiry) / type id
    //   +0x34 (+52) arg[2]  : start tick stamp (CheckDurationExpiry)
    //   +0x38 (+56) arg[3]  : type-51 endpoint id
    // For take/drop the carried bone byte is HIBYTE(*(node+53)) i.e. arg[2] hi.

    // --- reconstruction-only mirrors for the sound/sample/use-gate steps -------
    // The original packs these into the 404-byte node tail (animation-name buffer
    // at node+240, the sample playback speed at node+376, the AttachAni-seek flag
    // at node+396). Since this reconstruction uses native pointers the exact byte
    // tail differs; we model the observable fields the misc step handlers read.
    char  animBuf[72];       // node+240 : animation/sample name (CreateSound/Sample/UseGate)
    float speedScale;        // node+376 : sample playback speed scalar (1.0 default)
    u8    seekFlag;          // node+396 & 1 : PlaySample AttachAni seek-to-frame request
    i32   gateRoom;          // node+372 (idx 92) : UseGate destination room id
    i32   gateScene;         // node+368 (idx 93) : UseGate destination scene id
};
// NB: the original node stride is 404 bytes on 32-bit x86. This reconstruction
// uses native (64-bit) pointers, so sizeof(ActionNode) differs; field *order*
// and arg-slot semantics are preserved (the nodes are transient heap state, never
// serialized, so the exact byte stride is not observable).
static_assert(sizeof(ActionNode) >= 4 + 4 + 64 * 4,
              "ActionNode must cover the arg-slot range");

// Convenience accessors for the named arg slots (byte offsets into the node).
//   nodeArg(n,k) addresses args starting at +44; the engine's "+48" is k==1.
inline i32&  NodeArgAt(ActionNode* n, int k) { return n->args[k]; }

// ===========================================================================
// Hook interface — render/anim/command side effects.
// ---------------------------------------------------------------------------
// The real step handlers call into the renderer/animation/command subsystems
// (AttachAni, CreateObjectAnim, AttachItemToBone, Heightmap, EnqueueCmd*, ...).
// Those clusters are out of this module's scope, so each side effect is routed
// through a hook the host installs. Tests install a recording mock; the real
// game would install the render/anim bridge. State-machine control flow inside
// the handlers is translated faithfully; only the leaf effects are indirected.
// ===========================================================================
struct CharActionHooks {
    // Attach a movement/named animation; returns an opaque non-null handle on
    // success (mirrors VIBE_Character_AttachAni / AttachMovementAni).
    void* (*attachAnim)(Character* ch, const char* name, int loop);
    // Returns nonzero once the attached animation has finished this tick
    // (mirrors the +109 & 0x20 "anim done" poll the handlers use).
    int   (*animDone)(Character* ch);
    // Toggle the character's carried-item slot for the given bone (take/drop).
    // carried!=0 attaches, ==0 detaches (mirrors AttachItemToBone).
    void  (*setCarried)(Character* ch, int bone, int carried);
    // Turn the character toward arg-encoded yaw by one quantized step; returns
    // nonzero when the remaining angle has reached the target (turn complete).
    int   (*turnStep)(Character* ch, ActionNode* node);
    // Show/hide the character (FinishSetVisible -> VIBE_Character_SetVisible).
    void  (*setVisible)(Character* ch, int visible);
};

// Installs the hook table used by the step handlers. Passing nullptr installs an
// inert default (every effect is a no-op, animDone/turnStep return 1 so actions
// complete immediately) — the lifecycle stays exercisable without a host.
void SetCharActionHooks(const CharActionHooks* hooks);
const CharActionHooks& GetCharActionHooks();

// ===========================================================================
// Action-type registry  (dword_66FCD0 @0x66FCD0, dword_66FD18 @0x66FD18)
// ---------------------------------------------------------------------------
// VIBE_Character_DeclareAction(type, step, animName, ready, argCount) fills:
//   dword_66FCD0[19*type] = step           (entry stride 19 dwords == 76 bytes)
//   byte_66FCD4[76*type]  = ready
//   dword_66FD18[19*type] = argCount
//   the ~70 trailing bytes of the entry hold the animation-name string.
// Type ids 0..63. VIBE_CharAction_RegisterHandlers registers the full catalog.
// ===========================================================================
struct ActionTypeDef {
    ActionStepFn step;       // +0x00  default step fn (null == unregistered)
    u8  ready;               // +0x04  initial ready flag
    char animName[71];       // +0x05  animation-name string (e.g. "bewegung/gehen")
    i32 argCount;            // recovered from dword_66FD18[19*type]
};

// gilde.exe 0x405558 — VIBE_Character_DeclareAction. Registers one action type.
// Returns 1 on success, 0 if type>63 or already declared.
int DeclareAction(int type, ActionStepFn step, const char* animName, u8 ready,
                  int argCount);

// gilde.exe 0x40be30 — VIBE_CharAction_RegisterHandlers. Allocates the node pool
// and registers the built-in action catalog (turn/walk/take/drop/wait/...).
int RegisterHandlers();

// gilde.exe 0x40c07c — VIBE_CharAction_QueueShutdown. Frees the pool + registry.
void QueueShutdown();

const ActionTypeDef& ActionType(int type);

// ===========================================================================
// Node pool lifecycle.
// ===========================================================================

// gilde.exe 0x40431c — VIBE_ActionQueue_GetFreeEntry. Returns a zeroed free node,
// or nullptr if the pool is exhausted.
ActionNode* GetFreeEntry();

// gilde.exe 0x404370 — VIBE_ActionQueue_UnlinkEntry. Unlinks `node` from its
// owner's queue (fixing prev/next links and the +296 head) and frees it.
// Returns 1 if a node was freed, 0 if node was null.
int UnlinkEntry(ActionNode* node);

// gilde.exe 0x40442c — VIBE_ActionQueue_ValidateLinks. Debug link-integrity walk
// over a character's queue (asserts in the original via __debugbreak()).
void ValidateLinks(Character* ch, ActionNode* node);

// gilde.exe 0x4043dc — VIBE_ActionQueue_ClearAll. Frees every node in `ch`'s
// queue (releasing the type-45 waypoint buffer at +244 if present).
void ClearAll(Character* ch);

// ===========================================================================
// Builders — link a typed action onto a character's queue.
// ===========================================================================

// gilde.exe 0x40c15c — VIBE_CharAction_QueueInsertEntry. Grabs a free node and
// appends it to the tail of `ch`'s queue (creating the head at +296 if empty).
// Leaves type/step unset — InsertActionVararg fills those. Returns the node.
ActionNode* QueueInsertEntry(Character* ch);

// gilde.exe 0x40c1e4 — VIBE_CharAction_InsertActionVararg. Builds and enqueues an
// action of `type` for `ch`, copying `argc` args into the node's arg slots. The
// node's step fn defaults from the type registry. Returns the node (or null).
ActionNode* InsertActionVararg(Character* ch, int type, const i32* args, int argc);

// gilde.exe 0x404470 — VIBE_ActionQueue_InsertAction. Like the vararg builder but
// links the new node *immediately after* `after` (head insert if after==null).
// Used by step handlers that chain a follow-on action (e.g. TurnToTarget -> Turn).
ActionNode* InsertActionAfter(Character* ch, ActionNode* after, int type,
                              const i32* args, int argc);

// gilde.exe 0x40c3c8 — VIBE_CharAction_CancelForObject. Clears the `next` fn of
// every live node whose owner's universe matches `universe` (cancel pending
// chained phases for a despawning scene/object). `universe` is an opaque tag.
void CancelForObject(void* universe);

// Action-type ids referenced across the module (from RegisterHandlers).
enum ActionTypeId : u8 {
    kActStepMotion   = 0,    // RunActionOrFree (base motion-queue step)
    kActTurnAnim     = 7,    // TurnStepActionUpdate
    kActSound        = 23,   // SoundActionUpdate
    kActWalk         = 45,   // WalkUpdate
    kActTakeObject   = 49,   // TakeObjectActionUpdate
    kActDropObject   = 50,   // DropObjectActionUpdate
    kActMove2Univ    = 51,   // Move2UniverseActionUpdate
    kActTurnToTarget = 53,   // TurnToTargetActionUpdate
    kActLoadAnim     = 54,   // LoadAnimActionUpdate
    kActSetVisible   = 55,   // FinishSetVisible
    kActWalkOnPath   = 58,   // Command_Dispatcher (walk executor)
    kActWaitDuration = 59,   // CheckDurationExpiry
    kActUseGate      = 52,   // UseGateActionUpdate
};

} // namespace guild::sim
