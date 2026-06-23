#pragma once
// wire_selinput — bind the SELECTION-driven command builders' cross-module
// leaves (command_apply9.h's SelectionHooks + CheckHooks) to their real
// reconstructed callees (rule 13: wire up what you build). Glue only — no module
// logic; mirrors the SEED-FROM-DEFAULTS pattern of wire_charaction.cpp.
//
// Scope. Of the five "selection / input" hook bridges this wave touched
//   * SetSelectionHooks  (command_apply9.h, guild::sim) — QueueSetSelectedFlag /
//                         QueueRevealAllPersons console builders,
//   * SetCheckHooks      (command_apply9.h, guild::sim) — Check* predicates,
//   * Input_SetPollHooks (input_recon_select.h, guild::play) — DirectInput poll,
//   * SetInputCommandApplyHooks (input_command.h, guild::play) — opcode-80 apply,
//   * Hud_SetEnableHooks (hud_menu2.h, guild::gui) — VIBE_Object_SetEnabled list,
// only the first two carry a field with a clean reconstructed target:
//   SelectionHooks.parseInt            -> VIBE_Util_ParseInt   @0x5dc070
//   CheckHooks.personQueryBeginFlag90  -> VIBE_Person_QueryBegin@0x586c20
// The remaining SelectionHooks/CheckHooks fields, and the three other bridges in
// their entirety, are platform-boundary (DirectInput, rule 4) / not-yet-recon
// (VIBE_Object_SetEnabled, VIBE_Office_*) / process-global table reads with no
// standalone callable leaf -> they stay on their inert module defaults and are
// documented in the .cpp. Those three bridges therefore get NO new glue here.
//
// Install seeds each table from its module inert defaults (non-null stubs) and
// overrides only the wireable field, so the unbound fields keep their safe stubs.

namespace guild::sim {

// Bind the SelectionHooks.parseInt + CheckHooks.personQueryBeginFlag90 fields of
// command_apply9 to the real reconstructed leaves; all other fields stay inert.
// Idempotent; safe to call after the entity pool exists.
void InstallRealSelInputWiring();

} // namespace guild::sim
