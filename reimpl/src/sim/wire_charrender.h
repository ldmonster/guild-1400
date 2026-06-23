#pragma once
// wire_charrender — installs the reconstructed Character render/anim/action leaves into
// the live CharRender*/CharMesh/CharQuery hook bridges (rule 13). Before this, those six
// dispatch tables were fully inert at runtime (nothing installed them), so:
//
//   * CharRender4Hooks (character_render4.h) defaulted its node allocator to a null-
//     returning stub — every action-queue BUILDER (CreateSoundActionEx / CreateTake|
//     DropObjectAction / the Cmd* sit/stand handlers) early-outed as on an exhausted
//     pool, never threading a real node. We bind it to the GENUINE reconstructed pool
//     allocator/unlinker (guild::sim::QueueInsertEntry @0x40c15c / UnlinkEntry @0x404370)
//     so the builders run over the real intrusive action queue.
//
//   * CharRender5Hooks (character_render5.h) routes the camera-view-mode dispatch's
//     string compare through `strCmp`; we bind it to the genuine reconstructed
//     VIBE_Util_StrCmp (render::AnimStrCmp @0x5d3f10) — exactly the live wiring the
//     character_render5 integration test pins (case-sensitive, 0 == equal). The table's
//     remaining fields are pure renderer / anim / object / heightmap LEAVES (rule 3 —
//     GPU/scene tech we do NOT reconstruct); they keep faithful inert defaults here so a
//     process-lifetime install never dereferences a null leaf.
//
// The other four bridges — CharRender2Hooks, CharRender3Hooks, CharMeshHooks,
// CharQueryHooks — expose ONLY pure render / scene-graph / object / anim / heightmap /
// sound leaves (DirectDraw/Direct3D-class technology, rule 3). None of their fields has a
// reconstructed pure-logic counterpart in src/, so there is nothing to bind: they stay on
// their existing inert default tables (every consumer in character_render2/3/mesh/query.cpp
// reads through GetChar*Hooks(), which falls back to the safe inert default). Installing an
// empty table would add no binding and risk shadowing those vetted defaults, so we leave
// them untouched and document the decision here.
namespace guild::sim {

// Install the reconstructed CharRender4 / CharRender5 bindings into their global hook
// bridges. Idempotent (rebinds process-lifetime tables). The remaining char-render
// bridges have no reconstructed leaf to bind and are intentionally left inert (rule 3).
void InstallRealCharRenderWiring();

} // namespace guild::sim
