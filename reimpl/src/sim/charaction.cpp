// Per-actor action system — node pool, action-type registry, and the
// CharAction_* builders/helpers. Faithful 1:1 port of the self-contained core of
// gilde.exe's action queue (the render/anim leaf effects are routed through the
// hook table so the lifecycle is exercisable in isolation).
#include "sim/charaction.h"

#include "sim/actionqueue.h"
#include "sim/character.h"
#include "sim/charaction_misc.h"

#include <cstring>

namespace guild::sim {

// ---------------------------------------------------------------------------
// Node pool. The original is one VIBE_Memory_AllocDebug(0x7E400) block of 1280
// nodes of stride 404 (dword_62CEFC). We back it with a real ActionNode array.
// GetFreeEntry treats node->step (node[0]) as the occupied marker.
// ---------------------------------------------------------------------------
namespace {
ActionNode g_nodePool[kActionNodeCapacity];
bool       g_poolReady = false;

ActionTypeDef g_actionTypes[64]; // dword_66FCD0 / dword_66FD18 catalog (ids 0..63)

// Hook table (render/anim/command leaf effects). Defaults to inert no-ops.
const CharActionHooks* g_hooks = nullptr;

// Inert defaults: effects are no-ops; pollers report "done" so actions that
// gate on an animation finishing complete on their next dispatch.
void* DefAttachAnim(Character*, const char*, int) { return reinterpret_cast<void*>(1); }
int   DefAnimDone(Character*)                     { return 1; }
void  DefSetCarried(Character*, int, int)         {}
int   DefTurnStep(Character*, ActionNode*)        { return 1; }
void  DefSetVisible(Character*, int)              {}

const CharActionHooks g_defaultHooks = {
    DefAttachAnim, DefAnimDone, DefSetCarried, DefTurnStep, DefSetVisible,
};
} // namespace

void SetCharActionHooks(const CharActionHooks* hooks) { g_hooks = hooks; }
const CharActionHooks& GetCharActionHooks() {
    return g_hooks ? *g_hooks : g_defaultHooks;
}

const ActionTypeDef& ActionType(int type) { return g_actionTypes[type & 63]; }

// gilde.exe 0x40431c — VIBE_ActionQueue_GetFreeEntry.
// Linear scan over the 1280-node pool; node->step == null marks a free slot.
// The original scanned (v0<1280) and returned base+404*(v0-1); we return the
// first node with a null step fn after zeroing it.
ActionNode* GetFreeEntry() {
    if (!g_poolReady)
        return nullptr;
    // The original marks a slot occupied by node[0] (the step fn) being nonzero.
    // A freshly allocated node has step==0 until the builder fills it, so the
    // original implicitly relies on the immediate fill that follows. We make the
    // reservation explicit by also requiring owner (node+20) to be clear: a freed
    // node is fully zeroed (owner==null), so owner serves as the live marker that
    // QueueInsertEntry/InsertAction set before returning.
    for (int i = 0; i < kActionNodeCapacity; ++i) {
        if (g_nodePool[i].step == nullptr && g_nodePool[i].owner == nullptr) {
            std::memset(&g_nodePool[i], 0, sizeof(ActionNode));
            return &g_nodePool[i];
        }
    }
    return nullptr;
}

// gilde.exe 0x404370 — VIBE_ActionQueue_UnlinkEntry.
// Unlinks `node` from its owner's intrusive list and frees it (zeroes the slot).
// Mirrors the original's two cases: head node (prev==null && node==owner->head)
// just clears the head; otherwise relink prev/next and fix the head if needed.
int UnlinkEntry(ActionNode* node) {
    if (!node)
        return 0;
    Character* owner = node->owner;
    if (!node->next_link && owner && node == owner->actions) {
        // Sole/head node: clear the head.
        owner->actions = nullptr;
    } else {
        ActionNode* prev = node->prev;   // +36
        ActionNode* next = node->next_link; // +40
        if (prev)
            prev->next_link = next;
        if (next)
            next->prev = prev;
        if (!prev && owner)
            owner->actions = next;       // node was the head -> promote next
    }
    std::memset(node, 0, sizeof(ActionNode)); // VIBE_Light_SetGrayColorThunk(0,404)
    return 1;
}

// gilde.exe 0x40442c — VIBE_ActionQueue_ValidateLinks.
// Debug walk: every node in ch's queue must point back to ch as its owner; the
// inserted node's prev/next must be consistent. The original __debugbreak()s on
// violation; we silently tolerate (no-op release path).
void ValidateLinks(Character* ch, ActionNode* node) {
    if (node && node != ch->actions) {
        // node must be linked (have a prev) and its next must back-link to it.
        ActionNode* next = node->next_link;
        (void)next;
    }
    for (ActionNode* it = ch->actions; it; it = it->next_link) {
        if (it->next_link && it->next_link->owner != ch) {
            // link inconsistency — original would __debugbreak()
            break;
        }
    }
}

// gilde.exe 0x4043dc — VIBE_ActionQueue_ClearAll.
// Frees every node in ch's queue, releasing the type-45 waypoint buffer (+244)
// first. We don't model the waypoint heap block, so just unlink each head.
void ClearAll(Character* ch) {
    while (ch->actions) {
        ActionNode* head = ch->actions;
        // (type-45 waypoint buffer free at +244 omitted: render/pathfinder data)
        UnlinkEntry(head);
    }
}

// gilde.exe 0x40c15c — VIBE_CharAction_QueueInsertEntry.
// Grabs a free node and appends it to the tail of ch's queue. Sets owner; leaves
// type/step for the caller. Returns the node, or null on pool exhaustion.
ActionNode* QueueInsertEntry(Character* ch) {
    ActionNode* node = GetFreeEntry();
    if (!node)
        return nullptr;        // original logs "Could not ch_GetFreeQueueEntry()"
    node->owner = ch;          // +20: reserve the slot (live marker)

    ActionNode* head = ch->actions; // +296
    if (head) {
        ActionNode* tail = head;
        while (tail->next_link)      // walk to the tail (orig: while node[40])
            tail = tail->next_link;
        tail->next_link = node;      // +40
        node->prev = tail;           // +36
    } else {
        ch->actions = node;          // +296 head
    }
    node->callCount = 0;             // +12
    node->step = nullptr;            // +0 (cleared; builder fills it)
    node->next_link = nullptr;       // +40
    node->state = 0;                 // +16
    node->owner = ch;                // +20
    ValidateLinks(ch, node);
    return node;
}

// Shared body for the two builders: fills a freshly enqueued node from the type
// registry and copies args. Mirrors the tail of InsertActionVararg/InsertAction.
namespace {
void FillActionNode(ActionNode* node, Character* ch, int type, const i32* args,
                    int argc) {
    const ActionTypeDef& def = g_actionTypes[type & 63];
    node->step  = def.step;          // +0 = dword_66FCD0[19*type]
    node->ready = def.ready;         // +8 (from byte_66FCD4)
    node->callCount = 0;             // +12
    node->type  = static_cast<u8>(type); // +9 = BYTE4(a1)
    node->owner = ch;                // +20
    int n = def.argCount;            // dword_66FD18[19*type]
    if (n > argc) n = argc;
    if (n > 63) n = 63;
    // The original's copy loop does `v2 += 4` BEFORE the store, so the i-th
    // variadic arg lands at node+48+4*i (== args[1+i] here), NOT node+44. The
    // raw +44 slot (args[0]) is left for the engine's own use.
    for (int i = 0; i < n; ++i)
        node->args[1 + i] = args[i]; // node+48+4*i
    node->next_link = nullptr;       // +40 (head/tail fixed by inserter)
    node->state = 0;                 // +16
    // RunActionOrFree base actions latch the type into +48 (args[1]) as in the
    // original's `if (node->step == RunActionOrFree) node[48] = type`.
    if (node->step == &RunActionOrFree)
        node->args[1] = type;
}
} // namespace

// gilde.exe 0x40c1e4 — VIBE_CharAction_InsertActionVararg.
// Builds + enqueues an action of `type`. If the type has no registered step fn,
// the original coerces the type byte to 0; we keep that behaviour.
ActionNode* InsertActionVararg(Character* ch, int type, const i32* args, int argc) {
    if (!ch)
        return nullptr;
    if (g_actionTypes[type & 63].step == nullptr)
        type = 0;                    // BYTE4(a1) = 0
    ActionNode* node = QueueInsertEntry(ch);
    if (!node)
        return nullptr;
    FillActionNode(node, ch, type, args, argc);
    ValidateLinks(ch, node);
    return node;
}

// gilde.exe 0x404470 — VIBE_ActionQueue_InsertAction.
// Same fill, but links the node immediately after `after` (head if after==null).
ActionNode* InsertActionAfter(Character* ch, ActionNode* after, int type,
                              const i32* args, int argc) {
    if (!ch)
        return nullptr;
    if (g_actionTypes[type & 63].step == nullptr)
        type = 0;
    ActionNode* node = GetFreeEntry();
    if (!node)
        return nullptr;
    node->owner = ch;          // +20: reserve the slot (live marker)

    // Link after `after`: node->prev = after->prev; after->prev = node; ...
    // The original wrote: v5 = after[36]; after[36] = node; node[36] = v5;
    //                     node[40] = after; if (v5) v5[40] = node;
    //                     else owner->head = node;
    ActionNode* prevOfAfter = after ? after->prev : nullptr;
    if (after) {
        after->prev = node;          // after[36] = node
    }
    node->prev = prevOfAfter;        // node[36] = old after->prev
    node->next_link = after;         // node[40] = after
    if (prevOfAfter)
        prevOfAfter->next_link = node;
    else
        ch->actions = node;          // owner->head = node
    FillActionNode(node, ch, type, args, argc);
    // FillActionNode (shared with the tail builder) zeroes node->next_link; the
    // insert-after path must restore the link it just established.
    node->next_link = after;         // re-establish node[40] = after
    ValidateLinks(ch, node);
    return node;
}

// gilde.exe 0x40c3c8 — VIBE_CharAction_CancelForObject.
// Walks the whole pool; for each live node (type byte +9 set, step set) whose
// owner's universe matches `universe`, clears the node's chained `next` fn.
void CancelForObject(void* universe) {
    for (int i = 0; i < kActionNodeCapacity; ++i) {
        ActionNode* n = &g_nodePool[i];
        if (n->type && n->step) {
            Character* owner = n->owner; // node[5] == +20
            if (owner && owner->universe == universe && n->chained)
                n->chained = nullptr;     // node[1] = 0
        }
    }
}

// gilde.exe 0x405558 — VIBE_Character_DeclareAction.
// Registers one action type into the catalog. Rejects type>63 / re-declaration.
int DeclareAction(int type, ActionStepFn step, const char* animName, u8 ready,
                  int argCount) {
    if (type > 63)
        return 0;
    ActionTypeDef& def = g_actionTypes[type];
    if (def.step != nullptr)         // original: "Function already declared!"
        return 0;
    def.step     = step;
    def.ready    = ready;
    def.argCount = argCount;
    std::memset(def.animName, 0, sizeof(def.animName));
    if (animName) {
        std::size_t k = 0;
        while (animName[k] && k + 1 < sizeof(def.animName)) {
            def.animName[k] = animName[k];
            ++k;
        }
    }
    return 1;
}

// gilde.exe 0x40be30 — VIBE_CharAction_RegisterHandlers.
// Allocates the node pool and registers the built-in action catalog. The exact
// (type, step, animName, ready, argCount) tuples are transcribed from the
// original. Steps whose handlers are deferred (render/anim heavy) are registered
// to a benign no-op so the catalog ids stay faithful and enqueue/dispatch work.
int RegisterHandlers() {
    // (Re)initialise the pool and catalog.
    std::memset(g_nodePool, 0, sizeof(g_nodePool));
    for (auto& def : g_actionTypes) {
        def.step = nullptr;
        def.ready = 0;
        def.argCount = 0;
        def.animName[0] = '\0';
    }
    g_poolReady = true;

    DeclareAction(7,  &TurnStepActionUpdate,        "bewegung/dreh", 1, 2);
    DeclareAction(0,  &RunActionOrFree,             "",              1, 0);
    DeclareAction(45, &LoadAnimActionUpdate,        "bewegung/gehen",1, 3);
    DeclareAction(49, &TakeObjectActionUpdate,      "",              1, 0);
    DeclareAction(50, &DropObjectActionUpdate,      "",              1, 0);
    DeclareAction(53, &TurnToTargetActionUpdate,    "",              1, 1);
    DeclareAction(54, &LoadAnimActionUpdate,        "",              1, 0);
    DeclareAction(55, &FinishSetVisible,            "",              1, 1);
    DeclareAction(59, &CheckDurationExpiryStep,     "",              1, 1);
    // Misc steps (charaction_misc.cpp): sound/sample/use-gate.
    DeclareAction(46, &SoundActionUpdate,           "",              1, 1);
    DeclareAction(47, &PlaySampleActionUpdate,      "",              1, 0);
    DeclareAction(48, &SampleLoopActionUpdate,      "",              1, 0);
    DeclareAction(52, &UseGateActionUpdate,         "",              1, 0);
    // Chained follow-on types used by the use-gate sequence (51 relocate /
    // 56 fade). The original registers a relocation step + a fade step here; the
    // step bodies are render-entangled (Move2Universe / RotateInterpolate's
    // sibling) so we register a benign base step to keep the type bytes faithful
    // and the chain enqueues unmangled (InsertAction would otherwise coerce an
    // unregistered type to 0).
    DeclareAction(51, &RunActionOrFree,             "",              1, 1);
    DeclareAction(56, &RunActionOrFree,             "",              1, 1);
    return 1;
}

// gilde.exe 0x40c07c — VIBE_CharAction_QueueShutdown.
// Frees the pool and the registered catalog (and would destroy live characters).
void QueueShutdown() {
    g_poolReady = false;
    std::memset(g_nodePool, 0, sizeof(g_nodePool));
    for (auto& def : g_actionTypes)
        def.step = nullptr;
}

} // namespace guild::sim
