// real_hooks2 — second-wave cross-module "real wiring" installer. Glue only:
// binds the abstract interaction/pamphlet command sinks to the real command
// codec, and the character turn-bridge leaf to the real charaction_walk heading
// interpolation. See real_hooks2.h for the hook -> target table.
//
// This file contains NO module logic — only the indirection that connects the
// already-translated modules to each other.
#include "sim/real_hooks2.h"
#include "sim/real_hooks.h"       // RealCommandQueue() — the shared real queue

#include "sim/command_codec.h"    // QueueRequest16 / QueueRequest17 (real builders)
#include "sim/charaction.h"       // CharActionHooks / SetCharActionHooks
#include "sim/charaction_walk.h"  // WalkRotateTowardHeading (real heading interp)
#include "sim/character.h"        // Character (CharActionHooks operate on it)

#include "sim/interaction_handlers.h" // SetCommandEmitHook
#include "ai/intrigue.h"              // SetPamphletCmdHook

#include <cstring>

namespace guild::sim {

namespace {

// =========================================================================
// interaction command sink -> real command codec.
// interaction_handlers.h SetCommandEmitHook is `void(*)(const char* tag,
// int actionCode)` — an abstract "the Perform* handler emitted this command"
// sink. The original Perform* handlers built a real opcode-17 request packet
// through VIBE_Command_QueueRequest17 and EnqueuePacket'd it. We fold the
// (tag, actionCode) pair into that real builder: the action code is the
// primary field (a1), a stable hash of the tag string seeds the secondary
// field (a2) so distinct tags produce distinct wire bytes, and the staged
// packet is enqueued onto the shared real CommandQueue. This drives the real
// command codec + queue from the interaction layer.
// =========================================================================

// FNV-1a 32-bit over the tag string (deterministic; tag may be null -> 0).
i32 TagHash(const char* tag) {
    if (!tag) return 0;
    u32 h = 2166136261u;
    for (const char* p = tag; *p; ++p) {
        h ^= static_cast<u8>(*p);
        h *= 16777619u;
    }
    return static_cast<i32>(h);
}

void RealCommandEmit(const char* tag, int actionCode) {
    // opcode-17 request: a1=action code, a2=tag hash, a3=0, a4(word)=0,
    // a5(byte)=0, a6=0. The exact field placement is the codec's; we only
    // supply the values, the real builder writes the wire bytes.
    QueueRequest17(*RealCommandQueue(), actionCode, TagHash(tag), 0, 0, 0, 0);
}

// =========================================================================
// AI pamphlet command emit -> real command codec.
// intrigue.h SetPamphletCmdHook is `void(*)(i32 targetId)`. The original
// AiPlayer_EvalPamphlet emitted a pamphlet command for the target; there is no
// dedicated reconstructed pamphlet builder, but the generic opcode-16 request
// builder (the same one ai::SetBetCmdHook uses) faithfully stages+enqueues a
// real packet carrying the target id. We bind the pamphlet emit to it so the AI
// pamphlet path drives the real codec on the shared queue.
// =========================================================================
void RealPamphletCmd(i32 targetId) {
    QueueRequest16(*RealCommandQueue(), targetId, 0, 0, 0);
}

// =========================================================================
// character turn bridge -> real charaction_walk heading interpolation.
// charaction.h CharActionHooks.turnStep is
//   int(*)(Character* ch, ActionNode* node)
// returning nonzero when the turn has completed. The real reconstructed turn
// math is sim::WalkRotateTowardHeading (charaction_walk.cpp): given the current
// heading and a signed delta it advances one tick and reports completion.
//
// The Character mirror carries `yaw` (current facing quantum) and `turnTarget`
// (target yaw quantum) set up by the turn action builders; we feed them to the
// real interpolator as a small signed delta and write the advanced heading back,
// returning its `done` flag. This routes the turn-step render/anim leaf through
// real reconstructed code (instead of the inert default that completes
// immediately) without inventing a renderer. The other CharActionHooks fields
// stay at their inert default (see real_hooks2.h).
// =========================================================================
int RealTurnStep(Character* ch, ActionNode* /*node*/) {
    if (!ch)
        return 1; // nothing to turn -> treat as complete
    // Signed angle remaining to the target (radians). The mirror stores yaw and
    // target as small integer quanta; their difference drives the interpolation.
    float cur   = ch->yaw;
    float delta = static_cast<float>(ch->turnTarget) - cur;
    int done = 0;
    // One tick of the real heading interpolation (no cart, not mounted; a single
    // elapsed tick). Writes the advanced heading back into the mirror.
    ch->yaw = WalkRotateTowardHeading(cur, delta, /*elapsedTicks=*/1,
                                      /*fastMove=*/false, /*mounted=*/false, &done);
    return done;
}

// Inert backends for the render/anim leaves that have NO reconstructed target
// (mirroring charaction.cpp's own DefAttachAnim/etc). The step handlers call
// every field unconditionally (no per-field null guard), so the wired table must
// fill all fields; only turnStep is bound to real reconstructed code.
void* InertAttachAnim(Character*, const char*, int) { return reinterpret_cast<void*>(1); }
int   InertAnimDone(Character*)                     { return 1; }
void  InertSetCarried(Character*, int, int)         {}
void  InertSetVisible(Character*, int)              {}

// The wired CharAction hook table: turnStep -> real WalkRotateTowardHeading; the
// remaining render/anim leaves use inert backends (no reconstructed renderer).
CharActionHooks g_realCharActionHooks{};

} // namespace

void InstallRealSimHooks2() {
    // --- interaction command sink -> real codec on the shared queue ----------
    SetCommandEmitHook(&RealCommandEmit);

    // --- AI pamphlet command emit -> real codec ------------------------------
    guild::ai::SetPamphletCmdHook(&RealPamphletCmd);

    // --- character turn bridge -> real charaction_walk heading interp --------
    g_realCharActionHooks.attachAnim = &InertAttachAnim;
    g_realCharActionHooks.animDone   = &InertAnimDone;
    g_realCharActionHooks.setCarried = &InertSetCarried;
    g_realCharActionHooks.turnStep   = &RealTurnStep;   // REAL target
    g_realCharActionHooks.setVisible = &InertSetVisible;
    SetCharActionHooks(&g_realCharActionHooks);
}

} // namespace guild::sim
