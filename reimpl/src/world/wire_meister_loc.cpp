// See wire_meister_loc.h. Binds the MeisterAi command-emitter bridge, the
// LocationDialog4 dungeon-jailer gate, and the mission-name building-type lookup to
// their real reconstructed leaves. Glue only — no module logic.
//
// All MeisterAi command emits stage onto the SAME shared real CommandQueue that the
// other real_hooks bridges own (sim::RealCommandQueue()), exactly as wire_charaction
// routes its emitters. The office / building-type leaves are pure table reads over
// the reconstructed global tables (g_officeHolders / kTypeRecordTableA).
#include "world/wire_meister_loc.h"

#include "ai/meisterai3.h"            // ai::Meister3Hooks / Set/GetMeister3Hooks
#include "world/location4.h"          // world::LocationDialog4Hooks / Set/GetLocationDialog4Hooks
#include "world/office.h"             // world::OfficeGetEntryByHolder / OfficeHolder
#include "gui/mission_load_run.h"     // gui::MissionNameHooks / Menu_SetMissionNameHooks

#include "sim/real_hooks.h"           // sim::RealCommandQueue()
#include "sim/command_apply7.h"       // sim::RequestBuildOp90  @0x495b58
#include "sim/command_codec.h"        // sim::QueueRequestCoord27 @0x494878
#include "sim/building2.h"            // sim::Building_LookupTypeRecordA / TypeRecord @0x589778

namespace guild::world {

namespace {

guild::sim::CommandQueue& Q() { return *guild::sim::RealCommandQueue(); }

// =========================================================================
// ai::Meister3Hooks — MeisterAi command emitters -> the shared real queue.
// =========================================================================

// VIBE_Command_RequestBuildOp90(buildId, amount) @0x495b58. The meisterai3 callers
// pass h.request_build_op90(actorBuildId, -cost); the original is
// VIBE_Command_RequestBuildOp90(*(buildId), -cost) -> RequestBuildOp90(a1, a2),
// byte-faithful (a1=buildId@+0x10, a2=amount@+0x14; confirmed via the 0x46a488 site).
void WmlRequestBuildOp90(int buildId, int amount) {
    guild::sim::RequestBuildOp90(Q(), buildId, amount);
}

// VIBE_Command_QueueRequestCoord27(selfId, otherId, code) @0x494878. The meisterai3
// type-3 branch emits queue_coord27(selfId, otherId, 35); the original packet carries
// a1@+0x10, a2@+0x14, a3@+0x18 (the converted world-coord pair is zero here).
void WmlQueueCoord27(int selfId, int otherId, int code) {
    guild::sim::QueueRequestCoord27(Q(), selfId, otherId, code, 0, 0);
}

// =========================================================================
// world::LocationDialog4Hooks — the one cleanly-bindable side effect.
// =========================================================================

// VIBE_Office_GetEntryByHolder(17, &out) @0x47ef28 — true when a jailer (office type
// 17) holder exists. Confirmed at the DungeonBribe site 0x523d1c:
//   if ( VIBE_Office_GetEntryByHolder(17, v29) ) { ... }
// Reads the reconstructed g_officeHolders table; an empty table returns false (the
// inert default), so this stays safe headless and is faithful when populated.
bool WmlDungeonHasJailer() {
    guild::world::OfficeHolder out{};
    return guild::world::OfficeGetEntryByHolder(/*type*/ 17, &out) != 0;
}

// =========================================================================
// gui::MissionNameHooks — the mission-name building-type record lookup.
// =========================================================================

// VIBE_Building_LookupTypeRecordA(typeByte, out) @0x589778, then qmemcpy(.., v10).
// Confirmed at FormatMissionBuildingName 0x59b8cc:
//   VIBE_Building_LookupTypeRecordA(SHIBYTE(dword_122F4A0), v11); qmemcpy(.., v11, v10);
// The reconstructed builder writes a 6-byte TypeRecord; the original copies that same
// record and returns its length. We write the 6-byte record into outBuf and return 6.
struct RealMissionNameHooks final : guild::gui::MissionNameHooks {
    unsigned LookupBuildingTypeRecord(int typeByte, void* outBuf) override {
        guild::sim::TypeRecord rec{};
        guild::sim::Building_LookupTypeRecordA(static_cast<std::uint8_t>(typeByte), &rec);
        if (outBuf) {
            // qmemcpy of the 6-byte record (dword0 @ +0, word4 @ +4) into outBuf.
            auto* dst = static_cast<unsigned char*>(outBuf);
            dst[0] = static_cast<unsigned char>(rec.dword0 & 0xFF);
            dst[1] = static_cast<unsigned char>((rec.dword0 >> 8) & 0xFF);
            dst[2] = static_cast<unsigned char>((rec.dword0 >> 16) & 0xFF);
            dst[3] = static_cast<unsigned char>((rec.dword0 >> 24) & 0xFF);
            dst[4] = static_cast<unsigned char>(rec.word4 & 0xFF);
            dst[5] = static_cast<unsigned char>((rec.word4 >> 8) & 0xFF);
        }
        return 6u;  // the qmemcpy length (v10): dword0(4)+word4(2). NOT
                    // sizeof(TypeRecord) which is 8 due to struct padding.
    }
};

// --- process-lifetime wired hook tables / instances ----------------------
guild::world::LocationDialog4Hooks g_loc4{};
RealMissionNameHooks               g_missionName{};

} // namespace

void InstallRealMeisterLocWiring() {
    Q();  // force the shared real command queue to exist (composes with real_hooks)

    // --- ai::Meister3Hooks (meisterai3.h) ------------------------------------
    // SEED-FROM-DEFAULTS: start from the module's inert (non-null) stubs so the
    // unbound fields keep their safe no-ops, then override only the two faithfully
    // bindable command emitters.
    guild::ai::Meister3Hooks m3 = guild::ai::GetMeister3Hooks();
    m3.request_build_op90 = &WmlRequestBuildOp90;
    m3.queue_coord27      = &WmlQueueCoord27;
    // queue_slot_reset28      (@0x4948c8): the reconstructed builder takes a 248-byte
    //   SlotResetScratch + PendingState body, NOT the (slotId,slotKind,field7,field8)
    //   abstraction the meisterai3 callers expose; a faithful bind needs each caller's
    //   exact stack-struct (confirmed at 0x46a488) -> inert.
    // emit_group_state        (BeginDeltaPacket/AppendDeltaField/QueueRequestState22):
    //   the (slotIndex,mask) abstraction drops the delta entity base + field
    //   offsets/widths the real DeltaWriter chain needs -> inert.
    // update_handler_worldpos (@0x4c6cdc): He_UpdateHandlerWorldPos needs a HeRecord*
    //   + world-pos table, only an int index is passed -> inert.
    // dispatch_handler        (funcs_4C6EE9[type]()): per-type tick dispatch table is
    //   unreconstructed -> inert.
    guild::ai::SetMeister3Hooks(m3);

    // --- world::LocationDialog4Hooks (location4.h) ---------------------------
    g_loc4 = guild::world::GetLocationDialog4Hooks();  // seed from inert defaults
    g_loc4.dungeonHasJailer = &WmlDungeonHasJailer;
    // checkSkillRequirement   (@0x4ad594): Dialog_CheckSkillRequirement needs the live
    //   skill value + kind; the hook only carries the required level -> inert.
    // requestBuildOp / evaluateViolation: law subsystem (Gesetz_EvaluateViolation) +
    //   live perp/target state -> inert.
    // countExistingHandlers   (He scan with current-actor filters dword_631748) -> inert.
    // dungeonBribePending     (FindFirstHandlerByFilter(2,0,57,2,word_63CC5C)): needs
    //   the process-global current-master slot word_63CC5C -> inert.
    // enqueueBribe            (EnqueueCmd15 with live Money_MultiplyByRate) -> inert.
    // selectedBuilding / queueInfoRequest (radio UI + Quad43) -> inert.
    // dragSlotCount / commitTrainingItem  (drag-grid UI) -> inert.
    guild::world::SetLocationDialog4Hooks(&g_loc4);

    // --- gui::MissionNameHooks (mission_load_run.h) --------------------------
    // The only field is the building-type record lookup; bind it fully (real table).
    guild::gui::Menu_SetMissionNameHooks(&g_missionName);

    // NOTE — world::LocationDialogHooks (location3.h) is intentionally NOT installed:
    // every field is GUI form-shell (openForm/destroyForm/frameStep), command queue
    // (queueBatch/changePlayerAction), voice/message feedback, or a process-global
    // current-actor gate (findExistingRequest reads dword_631748 selectors; the hook
    // abstraction drops them, so binding to RealHandlerTable would not be byte-faithful;
    // activeCharFlag/targetBusy need live char/animal state). ZERO bindable fields ->
    // nothing installed for it (rule 8 / per the bridge-with-no-bindable-fields rule).
}

} // namespace guild::world
