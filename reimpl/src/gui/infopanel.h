#pragma once
// guild::gui — the selected-subject info panel: which builder to run and when.
//
// gilde.exe 0x4b84c0 — VIBE_InfoPanel_Update is the per-frame driver for the
// selected-unit info panel.  It (a) classifies the current selection into a subject
// kind (transporter / person / scene-object / building / nothing) and (b) only
// rebuilds the panel when the selection changed since the last frame, comparing the
// live selection globals against a cached snapshot (dword_631E24..631E40).
//
// This module recovers that SUBJECT-CLASSIFICATION + CHANGE-DETECTION dispatch
// byte-for-byte.  The individual builders (BuildTransporter / BuildPerson /
// BuildObject / BuildBuilding / BuildStandard / BuildDetailed) load forms and wire
// widgets via the wider GUI/sim clusters; here they are represented as the action the
// dispatcher selects, and the actual build is routed through the command hook so the
// dispatch decision is testable in isolation.

#include "gui/types.h"

namespace guild::gui {

// ---------------------------------------------------------------------------
// The live selection state the panel reads (the dword_631744/748/74C/11BC270/274/278
// globals).  A subject is identified by which of these handles is set; the original
// stores a snapshot in dword_631E24..631E40 to detect change.
// ---------------------------------------------------------------------------
struct InfoSelection {
    int room;        // dword_631744  (selected room/building-room handle)
    int building;    // dword_631748  (selected building handle)
    int subObject;   // dword_63174C  (sub-object within building)
    int person;      // dword_11BC270 (selected person handle)
    int transporter; // dword_11BC274 (selected transporter handle)
    int sceneObject; // dword_11BC278 (selected free scene object)
    int personGroup; // dword_631740  (person-group selection)
    int personMode;  // dword_6317B0  (>1 == multi-person aggregate view)
    bool charFlag;   // byte_6317B4   (character-selection flag)
    // The transporter "category" byte the original reads at
    // *(byte*)(dword_13CE27C + 65*transporterType); 29 == a passenger transporter
    // routed to BuildTransporter.
    int transporterCategory; // value of that byte (29 -> transporter panel)
};

// Snapshot of the selection used for change detection (dword_631E24..631E40).
struct InfoSnapshot {
    int room, building, subObject, person, transporter, sceneObject;
    int personGroup, personMode;
    bool charFlag;
    bool valid;
};

// The builder the dispatcher selects for the current subject.
enum class InfoBuilder {
    kNone,
    kTransporter, // VIBE_InfoPanel_BuildTransporter (0x4b6c80)
    kPerson,      // VIBE_InfoPanel_BuildPerson      (0x4b7104)
    kObject,      // VIBE_InfoPanel_BuildObject      (0x4b6930)
    kBuilding,    // VIBE_InfoPanel_BuildBuilding    (0x4b64b0)
    kStandard,    // VIBE_InfoPanel_BuildStandard    (0x4b6db8)
};

// The transporter-category sentinel the original tests against (== 29).
inline constexpr int kTransporterPanelCategory = 29;
// dword_6317B0 (personMode): values >1 select the aggregate multi-person view.
inline constexpr int kPersonModeMultiThreshold = 1;

// gilde.exe 0x4b84c0 (the rebuild block at 0x4b85e3..) — choose the builder for the
// current selection.  Mirrors the original priority:
//   1. transporter set, category 29, no person/multi-select  -> kTransporter
//   2. any of {personGroup, charFlag, personMode, subObject, person} set:
//        person set & mode<=1 -> kPerson ; else -> kStandard
//      (this is the "BuildStandard vs BuildPerson" else-branch)
//   3. else (object/building rebuild): transporter|subObject set & no playerbar
//        -> kObject ; else sceneObject|building set -> kBuilding
//   4. nothing selected -> kStandard
InfoBuilder InfoPanel_SelectBuilder(const InfoSelection& sel);

// gilde.exe 0x4b84c0 (the big OR-chain at 0x4b85e3) — does the live selection differ
// from the snapshot?  Returns true when the panel must be rebuilt.
bool InfoPanel_SelectionChanged(const InfoSelection& sel, const InfoSnapshot& snap);

// Capture the current selection into a snapshot (dword_631E24.. assignment block).
InfoSnapshot InfoPanel_Snapshot(const InfoSelection& sel);

// ---------------------------------------------------------------------------
// Command hook (mockable) — the builder the dispatcher runs.
// ---------------------------------------------------------------------------
struct InfoPanelCommandSink {
    virtual ~InfoPanelCommandSink() = default;
    virtual void Build(InfoBuilder) {}
};
void InfoPanel_SetCommandSink(InfoPanelCommandSink* sink);

// gilde.exe 0x4b84c0 — one Update pass: if the selection changed, pick the builder and
// run it (via the sink), then update the snapshot.  Returns the builder run, or kNone
// when no rebuild was needed.  `snap` is updated in place.
InfoBuilder InfoPanel_Update(const InfoSelection& sel, InfoSnapshot& snap);

} // namespace guild::gui
