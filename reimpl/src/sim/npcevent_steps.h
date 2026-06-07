#pragma once
// NpcEventSteps — the large NpcEvent step state machines for the Guild simulation
// (gilde.exe, VIBE_NpcEvent_* family). Each function here is a step of a per-NPC
// "timed event" coroutine that drives a He/handler record (see he.h) through a
// sequence of phases. The coroutine wakes at the appointment time the previous
// step armed (+82 GameTime), reads the phase id at +112, mutates the record /
// emits network commands, then arms the next wake-up by stamping the clock into
// +82, advancing it, and queueing a cmd29 entity request whose handle lands in
// +132. A phase of -1 / -2 (or a missing prerequisite) frees the handler entry.
//
// This module translates the remaining large step machines that npcevent.cpp /
// npcaction5.cpp deferred (the small scheduling helpers): the Politician family,
// the Reaper/"Sensenmann" plague driver, the Dark-Corner intrigue machine, the
// Object-Interaction / Tavern / Gambling economy events, plus several smaller
// one-shot steps (protection money, push-object, smoke effect, master-exam and
// talent-level-up dialogs, the city-accident simulator, and the Unkendunk raid).
//
// Render / pathfinder / transform / sound / particle leaves and the command,
// inventory, person-array, text and event-panel clusters are reached through the
// NpcEventHooks bridge below (plus the shared NpcLeafHooks in npcaction.h) so the
// state machines are exercisable in isolation. The control flow inside each step
// is translated faithfully (phase switch, branch conditions, timer advances, RNG
// draws via crt::RandNext through util::RandomModulo / RandomFloatScaled); only
// the leaf side-effects are indirected.
//
// Addresses are absolute, imagebase 0x400000.
#include "guild/common/types.h"
#include "sim/he.h"

namespace guild::sim {

// ===========================================================================
// Recovered constant tables (byte-for-byte from the cold IDB).
// ---------------------------------------------------------------------------
//   dword_478450 @0x478450 — 16 odd "method/gesture" bytes a step picks at random
//     (RandomModulo(16)) to seed the NPC's sub-method byte. Values are the odd
//     numbers 1,3,5,7,11,13,17,19 and their byte-complement 0xED..0xFF.
//   dword_478410 @0x478410 — 8 strides coprime to 256/512 used to walk the
//     object array pseudo-randomly without repeats (1,5,7,11,13,17,19,23).
// ===========================================================================
extern const i32 kMethodByteTable[16];   // dword_478450
extern const i32 kScanStrideTable[8];     // dword_478410

// ===========================================================================
// Leaf hooks — side effects that cross into other clusters. Tests install a
// recording / scripted mock; passing nullptr (or leaving a member null) installs
// an inert default (queries report "absent", effects are no-ops). The shared
// NpcLeafHooks (npcaction.h) supplies freeHandlerEntry / queueRequestEntity29 /
// packetStatus; this struct supplies the rest the big steps need.
// ===========================================================================
struct NpcEventHooks {
    // --- entity / person / building resolution ---
    // VIBE_GameObject_ResolveEntityById(id) -> opaque entity handle (0 == absent).
    // The steps only test the handle for null and read a couple of fields off it
    // through the field accessors below.
    i32 (*resolveEntity)(i32 id);
    // Field reads off a resolved entity handle (returns 0 if handle == 0):
    //   field +97  : linked avatar/person sub-record ptr (Reaper/Smoke gate).
    //   field +39  : owner/faction person-index word (0xFFFF == none).
    //   field +1   : entity id (object id).
    i32 (*entityField)(i32 entityHandle, int byteOffset);
    // VIBE_Person_FindRecordById(id) -> opaque Person handle (0 == absent).
    i32 (*findPerson)(i32 id);
    // Field read off a Person handle (id col, flags). byteOffset is the raw record
    // offset; returns 0 if handle == 0.
    i32 (*personField)(i32 personHandle, int byteOffset);
    // VIBE_Building_FindById(id) -> nonzero handle (0 == absent).
    i32 (*findBuilding)(i32 id);
    // VIBE_Object_IsNearDoor(person, entity) -> nonzero if adjacent.
    int (*isNearDoor)(i32 personHandle, i32 entityHandle);

    // --- command emit leaves (each returns the new packet handle where used) ---
    i32 (*queueRequestPair33)(i32 id, int v);          // op33 (release / arrest)
    i32 (*queueRequestSingle49)(i32 id);                // op49
    i32 (*queueRequestArgs25)(i32 id, int a, int b, int c, int d); // op25
    i32 (*queueRequestQuad52)(i32 a, i32 b, int c, int d);         // op52
    i32 (*queueRequestNamedObject53)(i32 a, i32 b, int c, int d, int e, const char* tag); // op53
    i32 (*requestBuildOp87)(i32 id);                   // op87 (politician release)
    i32 (*requestBuildOp77)(i32 id);                   // op77
    i32 (*enqueueObjectInteraction)(int a, int b, int c, int d, int e, int f, int g, int h);
    i32 (*queueGestureFlag55)(i32 id, int v);          // op55 (gesture flag)

    // --- packet status / sequence (re-arm gating) ---
    // VIBE_Command_GetPacketStatusById(handle): 0 pending, 1 lost, 2 applied.
    i32 (*packetStatus)(i32 handle);
    // VIBE_Command_GetPacketSeqById(handle): the applied entity handle (0 == none).
    i32 (*packetSeq)(i32 handle);

    // --- person-array queries (district/economy steps) ---
    // VIBE_Economy_LoadDemandSnapshot(out[3]): supply/demand counts. Writes
    //   out[0]=total, out[1]=demand, out[2]=supplyRatioFloatBits. Returns ratio.
    float (*loadDemandSnapshot)(i32 out3[3]);

    // --- Reaper render/transform/sound leaves (return 0/1 progress codes) ---
    // VIBE_NpcEvent_ReaperApproachTarget(h): attach reaper avatar; 1 ok / 0 fail.
    int (*reaperApproach)(HeRecord* h);
    // VIBE_NpcEvent_ReaperMoveTowardTarget(h): 1 still moving / 2 arrived / 0 lost.
    int (*reaperMove)(HeRecord* h);
    // VIBE_NpcEvent_ReaperCacheTargetPose(h): 1 ok / 0 fail.
    int (*reaperCachePose)(HeRecord* h);
    // VIBE_NpcEvent_ReaperUpdateSoundPos(h): 1 ok / 0 fail.
    int (*reaperUpdateSound)(HeRecord* h);
    // VIBE_Object_DetachAndRelease(node): tear down the reaper / particle node.
    void (*reaperDetach)(i32 node);
    // Read/write a field on a render-object node handle (the Smoke step touches the
    // node's +32 done-flag byte and +36 emit dword). Returns 0 if node == 0.
    i32 (*nodeFieldGet)(i32 node, int byteOffset);
    void (*nodeFieldSet)(i32 node, int byteOffset, i32 value);
    // VIBE_Cutscene_PauseGame()/ResumeGame() bracket the plague cutscene.
    void (*cutscenePause)();
    void (*cutsceneResume)();

    // --- dialog / event-panel leaves (master-exam & talent steps) ---
    int (*eventPanelCreate)(HeRecord* h);   // returns nonzero panel handle
    void (*eventPanelDestroy)(HeRecord* h);
    // The dialog "result" the panel reported this tick (dword_75BF38 == button id;
    // 1210 == confirm, 1155 == cancel, -1 == none yet).
    i32 (*dialogResult)();
};

void SetNpcEventHooks(const NpcEventHooks* hooks);
const NpcEventHooks& GetNpcEventHooks();

// ===========================================================================
// Translated step functions. Each takes the He record and returns the original's
// eax (a packet handle, a phase id, or the FreeHandlerEntry passthrough).
// ===========================================================================

// gilde.exe 0x4d43f8 — VIBE_NpcEvent_ProtectionMoneyInit.
//   Picks the next eligible person via Person_QueryBegin(filter=+176), stamps the
//   clock into +82 (+2 minutes), and if a person was found emits an op25 request
//   (456, 2048, 2, 0) against it. Returns the op25 handle or the advance result.
i32 NpcEvent_ProtectionMoneyInit(HeRecord* h);

// gilde.exe 0x4d71d8 — VIBE_NpcEvent_AllocLoverStep.
//   If not already spawned: stamps the clock (+1 min); if the +176 person exists
//   and its +458 byte is >= 0, emits the "Alloc_Geliebte" building action (op25 +
//   op27 coord) and arms phase 2 (handle->+132, +180:=0,+184:=1,+188:=-1). Else
//   arms a -1 teardown request. Returns the entity-request handle.
i32 NpcEvent_AllocLoverStep(HeRecord* h);

// gilde.exe 0x4d72ec — VIBE_NpcEvent_PushObjectStep.
//   Phase machine that nudges a target object toward a person over repeated waits:
//   phase -1 free; phase -2 final shove + free; phases 1/2/3/else emit op25/op27
//   coord pushes (offset growing with the +180 counter) and re-arm. Returns the
//   request handle.
i32 NpcEvent_PushObjectStep(HeRecord* h);

// gilde.exe 0x4d8900 — VIBE_NpcEvent_SmokeEffectStep.
//   5-phase particle lifecycle (phase = state+2): spawn a smoke (item 375) or
//   sparkle (376) effect node at the +172 entity, hold until the +180 deadline,
//   stop emission, then free when the node reports done. Returns the He passthrough
//   or FreeHandlerEntry.
i32 NpcEvent_SmokeEffectStep(HeRecord* h);

// gilde.exe 0x4d8af4 — VIBE_NpcEvent_UnkendunkStep.
//   Building "raid": for every live person stationed in the +172 building whose
//   jail/target flags match and a RandomFloatScaled() roll passes, emit op71/op77
//   plus a "Unkendunk" op53. Stamps +82 (+30 min); frees when past the +176
//   deadline. Phase < 0 frees. Returns the phase / free result.
i32 NpcEvent_UnkendunkStep(HeRecord* h);

// gilde.exe 0x4d9600 — VIBE_NpcEvent_ReaperPickNextTarget.
//   Scan the 256-slot object array from +184 by stride +188 for a plague-eligible
//   object; store its id into +180, attach the reaper avatar (reaperApproach), and
//   advance +82 by +15 min. Frees on no target / attach failure. Returns the
//   advance result.
i32 NpcEvent_ReaperPickNextTarget(HeRecord* h);

// gilde.exe 0x4d96f8 — VIBE_NpcEvent_ReaperPlagueStep.
//   The Reaper coroutine driver. Pauses the game (cutscene), then per phase:
//   phase 0 move toward target (reaperMove: 1 arrived-near / 2 advance to next
//   target / else lost), phase 2 cache the death pose then drop to phase 0. On
//   completion / loss detaches the avatar, resumes the game, and frees. Returns
//   the per-phase advance result.
i32 NpcEvent_ReaperPlagueStep(HeRecord* h);

// gilde.exe 0x4d9f9c — VIBE_NpcEvent_PoliticianReleaseTarget.
//   Release one politician-target slot (a1 = &slot, 16-byte: +0 personId, +4 objId,
//   +8 active, +9 flags, +10/+11 method params, +12 sentinel). If the person is
//   still adjacent to its object and before the 22:00 cutoff -> return 2 (keep);
//   else clear the slot (emit op49/op87/op25 if it had been engaged) and return 0.
int NpcEvent_PoliticianReleaseTarget(HeRecord* h, int slotOffset);

// gilde.exe 0x4da0dc — VIBE_NpcEvent_PoliticianFindTarget.
//   Find a new target for a free slot (a1=&slot, a2=activeCount, a3=releasedCount).
//   Gated by city office counts, slot occupancy, the 11:00 start hour, and a
//   randomized accept roll. On success fills the slot (personId/objId/method bytes)
//   and emits op25 + op52 requests; returns the op52 handle. Returns -1 on no pick.
i32 NpcEvent_PoliticianFindTarget(HeRecord* h, int slotOffset, int activeCount, int releasedCount);

// gilde.exe 0x4da3c4 — VIBE_NpcEvent_PoliticianTalkToTarget.
//   Advance one engaged slot: if adjacent to its target, run the speech timer
//   (gesture/op25/op77), then either keep walking the same target or pick the next
//   nearby office NPC (op53). Sets the slot's "done" flag (+9 bit 4) when finished.
int NpcEvent_PoliticianTalkToTarget(HeRecord* h, int slotOffset);

// gilde.exe 0x4da340 — VIBE_NpcEvent_QueueGestureFlags.
//   RandomModulo(4) -> emit 0..3 op55 gesture-flag commands for the current actor
//   (the slot resolved by the caller). Returns the last op55 handle.
i32 NpcEvent_QueueGestureFlags(HeRecord* h, i32 actorId);

// gilde.exe 0x4da7c0 — VIBE_NpcEvent_SimPoliticiansStep.
//   The Politician coroutine driver. Walks the 10 target slots at +172 (16 bytes
//   each): release done ones, advance engaged ones, count actives; every wake
//   (gated on the +132 packet) tries up to 10 FindTarget calls; at the daily 17:00
//   boundary releases everyone and resets the hour. Re-arms a cmd29 request.
i32 NpcEvent_SimPoliticiansStep(HeRecord* h);

// gilde.exe 0x4daf88 — VIBE_NpcEvent_GamblingStep.
//   2-phase casino event. Phase 0: snapshot the table (op28 slot-reset), arm phase
//   1 (+82 advance). Phase 1: if the +176 "won" flag is set, transfer a wealth
//   share (stock + op16 payout) and message the winner, else message a loss; then
//   free. Phase >1 frees. Returns the advance result or FreeHandlerEntry.
i32 NpcEvent_GamblingStep(HeRecord* h);

// gilde.exe 0x4d6a8c — VIBE_NpcEvent_ObjectInteractionStep.
//   Multi-phase market/black-market interaction driver (phases 0..7). Phase 0 arms
//   the loop; phase 1 reads the demand snapshot and branches on supply ratio to
//   phase 2/6/7; phase 2 the haggle loop (probability decay vs RandomFloatScaled);
//   phase 3 resolves the interaction packet and may push a sale (op16/op57/delta);
//   phases 6/7 scan persons/buildings for follow-up trade. Re-arms cmd29. Returns
//   the entity-request handle or FreeHandlerEntry (phase -1/-2).
i32 NpcEvent_ObjectInteractionStep(HeRecord* h);

// gilde.exe 0x4d79e8 — VIBE_NpcEvent_DarkCornerInit.
//   Arms a "Dunkle Ecke" intrigue: seed the scan cursor (+196 random, +200 random
//   stride, +188 random rounds 5..8), copy the clock to +68/+82 (+1 min), and if a
//   matching building exists begin it (delta packet + op73 "DunkleEcke"); queue the
//   first cmd29. Returns the entity-request handle.
i32 NpcEvent_DarkCornerInit(HeRecord* h);

// gilde.exe 0x4d7bd0 — VIBE_NpcEvent_DarkCornerStep.
//   The Dark-Corner coroutine driver (phases 0..4). Re-arms only once the +132
//   packet is applied. Phase 0 picks/confirms the meeting; phase 1 scans the object
//   array (stride +200) for the next victim and starts the encounter (op49/op53);
//   phase 2 runs the proximity check + production-rating roll loop; phases 3/4
//   resolve the heist, drafting accomplices from the recent-crime ring
//   (dword_11BC760, 45-byte records) into op36 pair requests + a delta packet and a
//   quick-jump message. Phase >= -2 with flag 0x02 emits the final op49/op33. Frees
//   at end. Returns the per-phase entity-request handle.
i32 NpcEvent_DarkCornerStep(HeRecord* h);

// gilde.exe 0x4dadd4 — VIBE_NpcEvent_MasterExamDialogStep.
//   3-phase master-exam messagebox: phase 0 create the panel + render text; phase 1
//   wait for the panel result (confirm 1210 -> mark the related handler +44:=1;
//   cancel 1155 -> done) or the +82 timeout; phase -1/-2 tear down. Returns the
//   phase / panel result.
i32 NpcEvent_MasterExamDialogStep(HeRecord* h);

// gilde.exe 0x4d63e8 — VIBE_NpcEvent_TalentLevelUpStep.
//   Talent-up celebration dialog, gated to the active player's selected NPC. Phase
//   0 builds the panel, renders the talent strings, and plays the fanfare/voice
//   samples; phase 1 waits for the confirm/cancel result then frees. Returns the
//   phase / free result.
i32 NpcEvent_TalentLevelUpStep(HeRecord* h);

// gilde.exe 0x4d4fcc — VIBE_NpcEvent_RunSimAccident.
//   City-accident simulator: each wake scans 12 persons (cursor +172, stride +176)
//   for an accident candidate; a RandomFloatScaled roll vs the building's safety
//   rating may trigger a stock loss + scripted accident. Advances +82 by 30 min,
//   wrapping the hour at 18:00; resets the sweep after 768 persons. Returns the
//   phase / advance result.
i32 NpcEvent_RunSimAccident(HeRecord* h);

// gilde.exe 0x4d5cdc — VIBE_NpcEvent_TavernSimStep.
//   Tavern economy: gather up to 64 tavern owners (Person_QueryBegin) with their
//   beer(377)/wine(378) stock objects, then for a sweep of 384 persons find a
//   thirsty patron (FindTavernTargetSlot) and, if they can afford it, run a buy
//   (op17/op16 + delta consume). Advances +82 by 15 min, resetting at the daily
//   22:00 boundary. Returns the phase / advance result.
i32 NpcEvent_TavernSimStep(HeRecord* h);

// ===========================================================================
// Registration. These steps are CharAction-style He coroutines reached by address
// (they do not claim a dispatch-table type). RegisterNpcEvents() returns the count
// and is callable alongside the existing RegisterNpcActions* without clobbering.
// ===========================================================================
int RegisterNpcEvents();

// Returns the step fn for `address`, or nullptr if not one of this batch. The
// signature is the common single-arg coroutine form; multi-arg helpers
// (PoliticianFindTarget / *ReleaseTarget / *TalkToTarget / QueueGestureFlags) are
// not registered as table entries (they are sub-steps of SimPoliticiansStep).
i32 (*NpcEvent_TableEntry(int address))(HeRecord*);

} // namespace guild::sim
