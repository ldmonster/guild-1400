#include "render/shadow_light_list.h"

namespace guild::render {

namespace {
// The engine's process-global pair (dword_1408A5C count + dword_1408A0C slots) that the
// void() resetLights FrameHook rebuilds; mirrored here for the live-frame binding.
ShadowLightList g_shadowLights;
UniverseRoot*   g_activeShadowRoot = nullptr;

// The current collection target while a walk is in progress (the callback has no ctx arg
// in the original — it writes the globals; we point this at the caller's list).
ShadowLightList* g_collecting = nullptr;

// gilde.exe 0x5f43f4 — the raw WalkAndInvoke callback signature (node, userArg) -> control.
char PushToDrawListCb(SceneNode* node, std::intptr_t /*userArg*/) {
    return ShadowPushLight(*g_collecting, node);
}
} // namespace

// gilde.exe 0x5f43f4 — VIBE_Render_PushToDrawList.
char ShadowPushLight(ShadowLightList& list, SceneNode* node) {
    // Non-casters are skipped, but the walk continues while the list has room
    // (`return dword_1408A5C < 4`).
    if (node == nullptr || (node->flags529 & kShadowCasterFlag) == 0)
        return list.count < kMaxShadowLights ? 1 : 0;
    // Append (engine: v2 = ++count; dword_1408A0C[v2] = node) — 1-based there, 0-based
    // here; the write only happens for the first four (count never exceeds 4 at push time
    // because the previous push returned 0 and aborted the walk).
    if (list.count < kMaxShadowLights)
        list.lights[list.count] = node;
    ++list.count;
    return list.count < kMaxShadowLights ? 1 : 0;   // stop the walk once four are gathered
}

// gilde.exe 0x5f4428 — VIBE_Shadow_ResetLightList.
void ShadowResetLightList(ShadowLightList& list, UniverseRoot* root) {
    list.count = 0;
    for (int i = 0; i < kMaxShadowLights; ++i) list.lights[i] = nullptr;
    if (!root) return;
    SceneWalkEnv env;
    env.root = root;
    env.applyRootInit = false;
    g_collecting = &list;
    WalkAndInvoke(root, /*node=*/nullptr, PushToDrawListCb, kShadowLightWalkMask,
                  /*userArg=*/0, env);
    g_collecting = nullptr;
}

void SetActiveShadowUniverse(UniverseRoot* root) { g_activeShadowRoot = root; }

void ShadowResetLightListActive() {
    ShadowResetLightList(g_shadowLights, g_activeShadowRoot);
}

const ShadowLightList& CollectedShadowLights() { return g_shadowLights; }

} // namespace guild::render
