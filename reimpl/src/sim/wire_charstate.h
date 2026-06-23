#pragma once
// wire_charstate — GLUE only (rule 13). Binds the previously-inert CharStateHooks
// (character_state.h) and the still-inert wireable leaves of CharacterFactoryHooks
// (character_factory.h) to their REAL reconstructed cross-module leaves.
//
// Before this pass:
//   * CharStateHooks            — NEVER installed in src/ (only tests). Both render
//                                 leaves (applyPivot / applyVisibility) ran inert, so
//                                 ProcessFlaggedLocal / RefreshFlaggedLocal did nothing.
//   * CharacterFactoryHooks     — only ONE field (attachToUniverseNode) is bound, by
//                                 sim/object_attach_wiring.cpp. Every other leaf still
//                                 ran its inert default. This installer COMPOSES onto
//                                 that table (seed-from-defaults) and binds the rest
//                                 that have a clean reconstructed target.
//   * CharIntroRunHooks         — every field is a GUI/SDL/input HOST boundary (rule 4)
//                                 with no portable reconstructed leaf; it stays fully
//                                 inert (driven only by the live native runtime). See
//                                 the .cpp / the report — there is nothing to wire here.
//
// SEED-FROM-DEFAULTS: each table is seeded from its module's Get<Bridge>Hooks() (the
// non-null inert stubs) and only the wireable fields are overridden, so unchecked call
// sites keep their safe stubs. This MUST run AFTER InstallRealObjectAttachWiring() so
// the genuine attach leaf binding is preserved (we seed from the post-attach table).
namespace guild::sim {

// Bind the CharState render leaves + the wireable CharacterFactory leaves to their
// real reconstructions. Idempotent; safe to call once at command-system init.
void InstallRealCharStateWiring();

} // namespace guild::sim
