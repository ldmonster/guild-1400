#pragma once
// guild::render — the per-frame SHADOW LIGHT LIST collector, reconstructed 1:1.
//
//   VIBE_Shadow_ResetLightList @0x5f4428 — called by BeginUniverseFrame (0x5b3900) each
//   frame to (re)build the small list of active shadow-casting lights. It zeroes the
//   count (dword_1408A5C) then walks the whole scene graph with walk-mask 16 (0x10 ==
//   node type 8, the light/effect nodes; TestNodeFlag's 8->0x10) invoking
//   VIBE_Render_PushToDrawList as the per-node callback.
//
//   VIBE_Render_PushToDrawList @0x5f43f4 — the collector callback. A node is appended only
//   when its +529 flags byte has bit2 (0x4) set (the "active shadow-caster" flag
//   VIBE_Character_CreateMesh @0x4029c4 sets); the list holds at most FOUR (dword_1408A0C),
//   and the callback returns (count < 4) so WalkAndInvoke STOPS the walk once four lights
//   are gathered. Non-casting nodes are skipped but the walk continues while there is room.
//
// Reuses the reconstructed scene-graph walker (render/scene_walk.h WalkAndInvoke +
// TestNodeFlag). The engine's 1-based slot indexing (dword_1408A0C[1..4]) is an internal
// memory detail; the observable result — up to four flagged type-8 nodes, in walk order,
// with the walk aborting at four — is preserved exactly.
#include "render/scene_walk.h"

namespace guild::render {

inline constexpr int kMaxShadowLights = 4;        // dword_1408A0C holds slots 1..4
inline constexpr i16 kShadowLightWalkMask = 16;   // 0x10 == node type 8 (TestNodeFlag)
inline constexpr u8  kShadowCasterFlag = 0x4;     // +529 bit2

// The collected shadow lights (mirrors dword_1408A5C count + dword_1408A0C slots).
struct ShadowLightList {
    int count = 0;
    SceneNode* lights[kMaxShadowLights] = {nullptr, nullptr, nullptr, nullptr};
};

// gilde.exe 0x5f43f4 — VIBE_Render_PushToDrawList (collector callback). Appends `node`
// into `list` when it is an active caster (flags529 & 0x4) and there is room; returns the
// walk-control byte (1 = keep walking while count < 4, 0 = stop once four are gathered).
// `list` is the engine's global (here threaded explicitly so it is testable).
char ShadowPushLight(ShadowLightList& list, SceneNode* node);

// gilde.exe 0x5f4428 — VIBE_Shadow_ResetLightList. Clears `list`, then walks the scene
// graph under `root` (mask 16) collecting up to four active shadow lights into `list`.
void ShadowResetLightList(ShadowLightList& list, UniverseRoot* root);

// ---- live-frame binding (the void() resetLights FrameHook the engine's off_649D64 uses) ----
// Set the active universe root the void thunk walks, then ShadowResetLightListActive() can be
// installed directly as render::FrameHooks::resetLights. CollectedShadowLights() exposes the
// list the last reset built (the engine's process-global pair).
void SetActiveShadowUniverse(UniverseRoot* root);
void ShadowResetLightListActive();                 // void() — rebuilds CollectedShadowLights()
const ShadowLightList& CollectedShadowLights();

} // namespace guild::render
