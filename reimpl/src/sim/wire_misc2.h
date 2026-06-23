#pragma once
// wire_misc2 — installs the genuinely-bindable field(s) of the "misc2" hook-bridge
// cluster into their real reconstructed leaves (rule 13: wire what you build).
//
// This cluster gathers five inert hook bridges that were surveyed for bindable
// cross-module leaves:
//
//   PersonnelGuiHooks  (gui/personnel_gui.h)        — staff-book / recruit dialogs
//   EventTableHooks    (sim/eventtable_recon.h)     — VIBE_EventTable_CreateEvent
//   BioCodecHooks      (io/bio_codec.h)             — VIBE_Bio_* compound codecs
//   ItemLabelHooks     (play/text_recon3_itemlabel.h) — VIBE_Text_FormatItemLabelWithIcon
//   MissionDialogHooks (world/history_mission.h)    — VIBE_Mission_Run*Dialog drivers
//
// Of these, exactly ONE field has a clean, signature-compatible reconstructed
// target: PersonnelGuiHooks::randomModulo -> util::RandomModulo (math_random.cpp
// @0x58b89c) — the bribe-bonus RNG source the personnel module documents as
// forwarding into the REAL util::RandomModulo. Everything else is a render / form /
// voice / audio / Win32-sync / process-global-table leaf with no clean leaf target
// (see the per-bridge rationale below) and is left at its module inert default.
//
// The installer SEEDS each touched table from its current module defaults (the
// non-null inert stubs) and overrides only the bound field, so the call sites that
// invoke hooks without a null-check keep their safe stubs — the same seed-from-
// defaults discipline as wire_charaction.cpp.
//
// ZERO-bindable bridges create NOTHING here:
//   EventTableHooks  — enterLock/leaveLock (EnterCriticalSection/LeaveCriticalSection,
//                      off_64A940/4) and createEvent (CreateEventA) are Win32 sync
//                      primitives; per the RULE-6 decision (user, 2026-06-10) they
//                      stay inert (createEvent's default already returns a non-null
//                      dummy handle so the table-append path runs 1:1). No leaf to bind.
//   BioCodecHooks    — alloc/free are VIBE_Memory_AllocDebug/FreeDebug (@0x438f10 /
//                      0x43923c). The real reconstruction (mem::MemoryTracker) is a
//                      stateful guard-word/group-accounting subsystem owned inside
//                      app::Wiring (tracker_), not a standalone callable leaf; binding
//                      it would change observable layout vs. the codec's documented
//                      byte-faithful malloc/free default. No clean leaf -> inert.
//   ItemLabelHooks   — resolveStringField resolves via the unmodeled process-global
//                      string table dword_8C36B0 (the original always indexes
//                      dword_8C36B0[id] before calling VIBE_String_GetDelimitedField);
//                      defaultLangLabel/outOfRangeLabel are the byte_13CD6A0 /
//                      dword_8C4784 language tables. None are modeled; faking them
//                      would violate rule 8 -> inert.
//   MissionDialogHooks — every field is a Form/Text/Voice/Audio/GameLogic frame-loop
//                      render leaf (createForm/renderText/playVoice/runFrameLoop/…).
//                      No sim leaf -> inert. (Not claimed by a dialog agent: no
//                      src/world/wire_dialogtut.* exists in the tree.)

namespace guild::sim {

// Seed-from-defaults installer: binds PersonnelGuiHooks::randomModulo to the real
// util::RandomModulo, leaving all other fields/bridges at their inert defaults.
// Idempotent; safe to call after the module defaults are constructed.
void InstallRealMisc2Wiring();

} // namespace guild::sim
