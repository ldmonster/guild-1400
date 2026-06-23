#pragma once
// gilde.exe — "He" handler-entry helpers (the icon/score/tutorial slice of the
// VIBE_He_* family). namespace guild::sim.
//
// These operate on the separate 45-byte-stride "He entity" table (base
// dword_11BC760; owner@+0 dword, marker@+22 word? id@+22, action@+6 byte index,
// state@+15 dword) and on the per-NPC He handler RECORD (the 332-byte record
// recovered in handler_entry.h / he.h). The deep command-emitting punishment
// path (VIBE_He_ExecutePunishment @0x4c4654) is intentionally NOT reconstructed
// here — see he_recon.cpp for the rationale (it is a 9-case command-packet
// builder whose faithfulness depends on the exact command-queue packet structs).
//
// Reconstructed (pure table / state-machine logic, leaves injected):
//   VIBE_He_ComputeEntityScore  @0x4c5030 — score one He-entity row (table + leaf).
//   VIBE_He_TutorialEventHandler@0x4db8c8 — the tutorial He-record state machine.
//
// Deferred (see he_recon.cpp): VIBE_He_AssignIconForHandler @0x4c6964 (world-icon
// gfx dispatch, render-coupled) and VIBE_He_ExecutePunishment @0x4c4654 (a
// command-packet builder coupled to the command-queue packet structs).
#include "guild/common/types.h"
#include "sim/he.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// VIBE_He_ComputeEntityScore @0x4c5030
// ---------------------------------------------------------------------------
// Same physical 45-byte-stride He-entity table as He_SumPlayerHandlerValues, but
// the original addresses it here from the TABLE START column (dword_11BC760),
// whereas Sum addresses it from the owner/person column (dword_11BC776 = start
// + 22). To avoid aliasing the differently-based HeEntityTable struct, this slice
// uses its own view rooted at the true table start. Columns (relative to start):
//   +0  (dword) entityId        — matched against `a1`        (dword_11BC760)
//   +22 (dword) personId        — person-record resolver key  (dword_11BC776)
//   +28 (byte)  action index    — index into kHeScoreTable<26 (byte_11BC77C)
//   23040 bytes => 512 rows of 45 bytes
// The score-row base value is kHeScoreTable[action][3] (column +12), identical to
// the value Sum reads; ComputeEntityScore adds the office rank and scales:
// Score = (favorability*0.005 + 0.5) * (scoreTable[action][3] + officeRank) * 1600.0
// (flt_61E650=0.005, flt_61E654=0.5, flt_61E658=1600.0; recovered via get_bytes).
struct HeScoreEntityView {
    static constexpr u32 kStride = 45;
    static constexpr u32 kCount  = 512;            // 23040 / 45
    static constexpr u32 kBytes  = kStride * kCount;
    u8 bytes[kBytes];                              // base = table start (dword_11BC760)

    void set_entity(u32 i, i32 v) { *reinterpret_cast<i32*>(bytes + 45 * i) = v; }
    void set_person(u32 i, i32 v) { *reinterpret_cast<i32*>(bytes + 45 * i + 22) = v; }
    void set_action(u32 i, u8 v)  { bytes[45 * i + 28] = v; }
};
struct HeScoreLeaves {
    // VIBE_Person_FindRecordById @0x58bc6c — resolve personId -> record base; the
    // record's +0 word is the person marker copied into *outMarker. Null => fail.
    const void* (*personFind)(i32 personId) = nullptr;
    // *(WORD*)record — the marker read from the resolved person record.
    u16 (*personMarker)(const void* record) = nullptr;
    // VIBE_Person_ComputeOfficeRank @0x58bccc(marker, 1).
    i32 (*officeRank)(u16 marker, i32 mode) = nullptr;
    // VIBE_Ai_ComputePersonFavorability @0x594330(marker, evalMarker, 1).
    double (*favorability)(u16 marker, u16 evalMarker, i32 mode) = nullptr;
};

// Returns 1 and fills *outMarker (the resolved person marker) + *outScore (the
// truncated i32 score) on success; 0 otherwise. `evalMarker` is *a2 (the
// evaluating person's marker). Mirrors the original's __userpurge signature
// (a1=entityId, a2=evalMarkerPtr, a3=outMarker, a4=outScore, a5=stack arg_0).
//
// outRowByte is the original's a5 (arg_0, the single pushed stack arg). On the
// success path the original does `mov [edx],al` @0x4c512f writing the LOW BYTE of
// the score row's first dword (kHeScoreTable[action][0] & 0xFF) into *a5 — a real
// output, not a clobber artifact: every live caller pushes a stack buffer pointer
// (e.g. VIBE_AiPlayer_FindOpponentBuilding @0x47dbf6: `lea eax,[local]; push eax`).
// Passed last with a nullptr default so the byte-store is performed faithfully
// whenever a caller supplies the slot.
int He_ComputeEntityScore(const HeScoreEntityView& table, i32 entityId, u16 evalMarker,
                          u16* outMarker, i32* outScore,
                          const HeScoreLeaves& leaves, u8* outRowByte = nullptr);

// ---------------------------------------------------------------------------
// VIBE_He_TutorialEventHandler @0x4db8c8
// ---------------------------------------------------------------------------
// Per-record tutorial state machine. `record` is the 332-byte He record; the
// state lives at +112 (record dword 28). byte_63CC40 gates the whole thing
// (tutorial-active flag). Leaves are injected, inert by default.
struct TutorialLeaves {
    bool tutorialActive = false;            // byte_63CC40
    // VIBE_Tutorial_OpenMainEventPanel @0x5971ac — returns nonzero if opened.
    i32 (*openMainPanel)() = nullptr;
    // VIBE_Hud_ProcessDragDropClick @0x59542c(arg, record) — returns nonzero when
    // the drag-drop step completes.
    i32 (*processDragDropClick)(i32 arg, void* record) = nullptr;
    // VIBE_Tutorial_CloseEventPanel @0x597270(arg).
    void (*closeEventPanel)(i32 arg) = nullptr;
    // VIBE_Tutorial_ResetChapterPointer @0x597b80.
    void (*resetChapterPointer)() = nullptr;
    // VIBE_He_FreeHandlerEntry @0x4c6144 — release this record (terminal).
    void (*freeHandlerEntry)(void* record) = nullptr;
    // VIBE_Hud_FindModeIndex @0x4bea2c(VIBE_GameLogic_InitOrLoadSession) — final result.
    i32 (*findModeIndex)() = nullptr;
    // dword_63CC30 — "tutorial chapter advanced" flag, set to 1 on the terminal path.
    i32* chapterAdvancedFlag = nullptr;
};

// Returns the original's eax. `arg` is the ecx argument (a2/a3 in the original,
// forwarded to the leaves). Drives the +112 state field exactly as the original.
u32 He_TutorialEventHandler(void* record, i32 arg, TutorialLeaves& leaves);

// State field offset within the He record (record byte +112).
constexpr std::size_t kHeRecordStateOffset = 112;

} // namespace guild::sim
