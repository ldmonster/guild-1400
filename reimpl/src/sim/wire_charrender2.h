#pragma once
// wire_charrender2 — installs the one reconstructed PURE-LOGIC leaf available across the
// CharRender2 / CharRender3 / CharMesh / CharQuery hook bridges into its live dispatch
// table (rule 13). A prior wiring pass (wire_charrender.cpp) bound CharRender4/5 and left
// these four bridges inert because their fields are overwhelmingly GPU/scene/object/anim/
// heightmap/sound leaves (rule 3 — tech we do NOT reconstruct). This pass revisits them:
//
//   * CharRender3Hooks (character_render3.h) — the ONLY field with a genuine reconstructed
//     pure-logic counterpart is `convertBackslashToSlash`
//     (VIBE_Path_ConvertBackslashToSlash @0x44eb54): a byte-faithful in-place '\\'->'/'
//     path-string transform, reconstructed as guild::io::ConvertBackslashToSlash
//     (src/io/path.cpp, verified 1:1 against the Hex-Rays of 0x44eb54 — strlen-once scan,
//     92->47). The attach/preload family in character_render3.cpp calls this hook WITHOUT
//     a null-check (h.convertBackslashToSlash(dlg)), so we SEED the table from the module's
//     inert defaults and override ONLY that field — every other CharRender3 field
//     (findFreeMeshSlot / loadStreamToStock / attachToBone / createMorphAnim / the object/
//     light/universe/sound/reportError leaves) is GPU/anim/sound tech with no reconstructed
//     pure-logic target, so it keeps its safe inert default.
//
//   * CharRender2Hooks (character_render2.h), CharMeshHooks (character_mesh.h),
//     CharQueryHooks (character_query.h) — every field is a renderer / scene-graph / object /
//     universe / light / heightmap / person-table / mesh-walk LEAF (rule 3). None has a
//     reconstructed standalone pure-logic counterpart in src/, so there is NOTHING to bind.
//     Per the no-empty-table rule we install NOTHING for these three and leave them on their
//     vetted inert defaults (every consumer reads through GetChar*Hooks(); CharRender2/Mesh/
//     Query fall back to their safe inert default when no table is installed).
namespace guild::sim {

// Install the one reconstructed CharRender3 binding (convertBackslashToSlash @0x44eb54)
// into its global hook bridge. SEED-FROM-DEFAULTS: the installed table is a copy of the
// module's inert defaults with only that field overridden, because the call sites invoke
// the hook unconditionally. Idempotent (rebinds a process-lifetime table). The sibling
// CharRender2 / CharMesh / CharQuery bridges have no reconstructed pure-logic leaf to bind
// and are intentionally left inert (rule 3) — see the header comment above.
void InstallRealCharRender2Wiring();

} // namespace guild::sim
