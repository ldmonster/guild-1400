#include "sim/he_recon.h"
#include "sim/handler_entry.h"   // kHeScoreTable (shared 26x36 score table)

#include <cstring>

namespace guild::sim {

namespace {
// flt_61E650 / flt_61E654 / flt_61E658, recovered via get_bytes:
//   0x3ba3d70a = 0.005f, 0x3f000000 = 0.5f, 0x44c80000 = 1600.0f.
constexpr double kFavWeight = 0.004999999888241291; // (float)0.005 widened
constexpr double kFavBias   = 0.5;
constexpr double kScoreMul  = 1600.0;
} // namespace

// gilde.exe 0x4c5030 — VIBE_He_ComputeEntityScore.
int He_ComputeEntityScore(const HeScoreEntityView& table, i32 a1, u16 evalMarker,
                          u16* a3, i32* a4, const HeScoreLeaves& leaves,
                          u8* a5) {
    if (a1 == -1)
        return 0;

    // Linear scan by entityId at column +0 (45-byte stride, 23040-byte table).
    i32 v9 = 0;           // row index
    i32 v10 = 0;          // byte offset
    if (a1 != *reinterpret_cast<const i32*>(table.bytes)) {
        do {
            v10 += 45;
            ++v9;
            if (v10 >= 23040)
                return 0;
        } while (a1 != *reinterpret_cast<const i32*>(table.bytes + v10));
    }
    if (v9 == -1)         // unreachable in practice; preserved 1:1
        return 0;

    i32 v22 = 45 * v9;
    u8 action = table.bytes[28 + v22];          // byte_11BC77C[45*v9]
    if (action >= kHeScoreEntries)
        return 0;

    // qmemcpy(v20, &unk_631E98 + 36*action, 36); v12 = v20[3].
    const i32* row = kHeScoreTable[action];
    i32 v12 = row[3];                            // column +12
    // v18 = (BYTE)v20[0] (loaded at 0x4c5120, `mov al,[esp+var_40]`), stored to
    // *a5 on the success path at 0x4c512f (`mov [edx],al`). Capture it now.
    u8 v18 = static_cast<u8>(row[0]);
    if (!v12)
        return 0;

    i32 personId = *reinterpret_cast<const i32*>(table.bytes + 22 + v22); // dword_11BC776
    const void* rec = leaves.personFind ? leaves.personFind(personId) : nullptr;
    if (!rec)
        return 0;                                // result==0 path

    u16 marker = leaves.personMarker ? leaves.personMarker(rec) : 0; // *(WORD*)result
    *a3 = marker;
    i32 v14 = leaves.officeRank ? leaves.officeRank(marker, 1) : 0;
    double v16 = leaves.favorability ? leaves.favorability(marker, evalMarker, 1) : 0.0;
    double v17 = (v16 * kFavWeight + kFavBias) * static_cast<double>(v12 + v14) * kScoreMul;
    // 0x4c5127 VIBE_Coord_ConvertX sets FPU RC=truncate then frndint's st0 in
    // place; the following bare `fistp` stores the already-integral value. Net:
    // round-toward-zero truncation of v17.
    *a4 = static_cast<i32>(v17);
    // 0x4c512f `mov [edx],al` — real output store to a5 (a caller stack buffer in
    // every live caller; e.g. VIBE_AiPlayer_FindOpponentBuilding @0x47dbf6 pushes
    // &local). Faithful, not a clobber artifact.
    if (a5)
        *a5 = v18;
    return 1;
}

// gilde.exe 0x4db8c8 — VIBE_He_TutorialEventHandler.
u32 He_TutorialEventHandler(void* record, i32 arg, TutorialLeaves& leaves) {
    u8* rec = static_cast<u8*>(record);
    u32 result = reinterpret_cast<std::uintptr_t>(record); // v5 = result (record base)

    if (!leaves.tutorialActive)                  // byte_63CC40
        return result;

    i32* state = reinterpret_cast<i32*>(rec + kHeRecordStateOffset); // record+112
    result = static_cast<u32>(*state);
    if (result) {
        if (result <= 1) {                       // state == 1
            i32 r = leaves.processDragDropClick
                        ? leaves.processDragDropClick(arg, record) : 0;
            result = static_cast<u32>(r);
            if (result)
                *state = 2;                       // *(record+112) = 2
        } else if (result == 2) {                 // state == 2 (terminal)
            if (leaves.closeEventPanel)     leaves.closeEventPanel(arg);
            if (leaves.resetChapterPointer) leaves.resetChapterPointer();
            if (leaves.freeHandlerEntry)    leaves.freeHandlerEntry(record);
            if (leaves.chapterAdvancedFlag) *leaves.chapterAdvancedFlag = 1; // dword_63CC30
            return leaves.findModeIndex ? static_cast<u32>(leaves.findModeIndex()) : 0;
        }
    } else {                                      // state == 0
        i32 r = leaves.openMainPanel ? leaves.openMainPanel() : 0;
        result = static_cast<u32>(r);
        *state = result ? 2 : 1;
    }
    return result;
}

// ---------------------------------------------------------------------------
// DEFERRED — not reconstructed here (would require fake analogues; rule 8).
//
// VIBE_He_AssignIconForHandler @0x4c6964 (size 748): a world-icon GFX dispatcher.
//   It switches on the He record's kind byte (record[0], values 2/4/0x1A/0x1C/
//   0x1E/0x1F/0x35/0x40/0x6B/...) and, after resolving a Building/Person/Object
//   via VIBE_Building_FindById/VIBE_Person_FindRecordById/VIBE_Object_FindByHandle,
//   calls VIBE_He_CreateGfxInfo(record, "<icon-name>", gfxPtr) with one of:
//   he_hammer_gold / he_saege_hammer_gold / he_muenze / he_schlaege / he_fernglas /
//   he_ausrufezeichen / he_plus_hammer (strings @0x61E774..). This is the 3D/2D
//   render layer (icon gfx). It is gated by byte_123356B and reads record render
//   fields (+136 gfx ptr, +172/+188/+208 entity refs, +112 phase). Reconstructing
//   it faithfully requires VIBE_He_CreateGfxInfo @0x4c67e0 and the Building/Person/
//   Object record layouts, none of which are pure table logic — deferred to the
//   icon/render cluster. Caller: VIBE_He_RunAllHandlers @0x4c6e38.
//
// VIBE_He_ExecutePunishment @0x4c4654 (size 1290, "gs_StrafeAusfuehren"): a 9-case
//   (court-sentence type 0..9) command-packet builder. Each case packs a stack
//   template (fields at +0x130/+0x10C frames) and submits it through the command
//   queue (VIBE_Command_QueueRequestSlotReset28 @0x4948c8, VIBE_Command_
//   RequestBuildOp91 @0x495b7c, VIBE_Command_BeginDeltaPacket/AppendDeltaField,
//   VIBE_Command_QueueRequest39, VIBE_Office_GetHolderEntryByCity/AddTableEntry).
//   Its faithfulness depends entirely on the exact command-packet struct layouts
//   (the +0x130 / +0x10C frames) which belong to the command-builders cluster;
//   reconstructing it standalone would require fabricating those packet structs.
//   Deferred. Caller: VIBE_Office_RunCourtTrial @0x4a0eb8.
// ---------------------------------------------------------------------------

} // namespace guild::sim
