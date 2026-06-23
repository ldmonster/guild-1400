#pragma once
// ===========================================================================
// wire_cutscene.{h,cpp} — bind the five cutscene leaf/driver hook bridges to
// their real reconstructed cross-cluster leaves (gilde.exe, guild::sim).
// ===========================================================================
//
// The cutscene substrate (cutscene.{h,cpp}: CutsceneSlot / CutsceneTable /
// CutsceneRng) and the per-type CONTROL flow (cutscene_misc{,2,3,4}.cpp,
// cutscene_process.cpp) are reconstructed 1:1. Their cross-module LEAVES are
// routed through five installable hook tables that, before this pass, were
// NEVER installed by the app spine — so every cutscene body ran against its
// inert defaults at runtime (rule 13 violation):
//
//   CutsceneMiscHooks   (cutscene_misc.h)    — script/character/audio/timebase/widget
//   Cutscene2Hooks      (cutscene_misc2.h)   — frame-pump/script/fade/sky/duel/person
//   CutsceneMisc3Hooks  (cutscene_misc3.h)   — per-type mains: audio/scene/script/person/ui
//   CutsceneMisc4Hooks  (cutscene_misc4.h)   — RunParticipants/SalonFade/LeaseWindow
//   CutsceneProcHooks   (cutscene_process.h) — ProcessActive: op88 / cmd28 speech / prepare
//
// InstallRealCutsceneWiring() SEEDS each table from its module inert defaults
// (so unbound fields keep their safe no-op stubs — several cutscene bodies
// invoke hooks WITHOUT a null-check, and GetCutsceneMisc4Hooks() in particular
// dereferences the installed pointer directly) and overrides ONLY the fields
// for which a clean reconstructed target exists:
//
//   * person resolves / field reads -> sim::PersonFindRecordById (entity.h) +
//     the recovered Person record offsets (kind@+2, id@+4, ill@+9, parent@+92).
//   * command emits -> the real command builders on the shared RealCommandQueue:
//       op88 timeout  -> RequestBuildOp88        (command_builders.h)
//       cmd28 speech  -> QueueRequestBuffer28    (command_builders2.h)
//       birth failure -> QueueRequestPair33      (command_builders.h)
//       salon swap    -> QueueRequestCoord27     (command_codec.h)
//   * the CRT random-modulo leaf -> util::RandomModulo (math_random.h).
//
// Everything else these tables carry is a rule-3/4/5 boundary leaf (the frame
// pump GameLogic_RunFrameLoop, Script_*, Music_*/Voice_* audio, Sky_*/Fade_*/
// Surface_* presentation, the Form/Widget windows, the GPU light pokes) or a
// process-global host table (the 11AB0xx participant text columns, the cached
// salon scene, the net wait loop) with no standalone reconstructed callable —
// those keep their inert stubs and are documented in wire_cutscene.cpp.
// ---------------------------------------------------------------------------

namespace guild::sim {

// Bind the five cutscene bridges to their real reconstructed leaves. Idempotent;
// SEEDS each table from its module inert defaults before overriding wireables.
void InstallRealCutsceneWiring();

}  // namespace guild::sim
