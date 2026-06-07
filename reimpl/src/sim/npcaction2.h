#pragma once
// NpcAction2 — second batch of NpcAction/CharAction behavior state-machine cores
// for the Guild simulation (gilde.exe). This batch covers the plague-outbreak
// behaviour: VIBE_CharAction_PlagueSelectTarget (0x4d25c4) which picks an initial
// infection target and arms the carrier NPCs, and VIBE_CharAction_PlagueSpreadStep
// (0x4d2900) which is the multi-phase spread coroutine driven by the He/handler
// record's state (+112), timestamp (+82), iteration counter (+172), object cursor
// (+176/+180), member packet array (+188), and current/home target ids (+204/+208).
//
// The state-machine control flow, the time-advance deltas, the RandomModulo draws
// (count + order), the coprime-step modular object walk, the per-phase QueueRequest
// re-arm and the +132 packet handle gating are translated 1:1. Leaf calls into the
// command / object-array / history / coordinate clusters are routed through
// NpcAction2Hooks (extending the established NpcLeafHooks pattern) so the behaviour
// is exercisable in isolation. The one opaque sub-block — the per-tick infection
// spread physics inside phase 2 (a float-decay scatter the decompiler renders with
// undefined locals) — is delegated to a single documented hook (plagueInfectNearby)
// while the control flow that surrounds it (door checks, counter decrement, phase
// re-arm) is translated exactly.
#include "guild/common/types.h"
#include "sim/he.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// Coprime-to-256 scan-step table (gilde.exe dword_478450, 16 entries). The plague
// walk advances the object-array cursor by one of these (chosen at random) so it
// visits all 256 slots in a pseudo-random order without repeats.
// ---------------------------------------------------------------------------
extern const i32 kPlagueScanSteps[16];

// ---------------------------------------------------------------------------
// Leaf hooks for the plague behaviour. Tests install a recording/synthetic mock;
// nullptr installs an inert default (every effect a no-op, queries return absent).
// These mirror the originals' leaf calls:
//   VIBE_Command_QueueRequestEntity29, VIBE_Command_GetPacketStatusById/SeqById,
//   VIBE_Command_QueueRequestSingle49, VIBE_Command_QueueRequestNamedObject53,
//   VIBE_Command_QueueRequestPair33, VIBE_Command_RequestBuildOp73Str,
//   VIBE_GameObject_ResolveEntityById, VIBE_Object_IsNearDoor,
//   VIBE_He_FreeHandlerEntry, VIBE_History_BroadcastPlagueOutbreak/Spread.
// ---------------------------------------------------------------------------
struct NpcAction2Hooks {
    // VIBE_Command_QueueRequestEntity29(arg, h) -> packet handle (stored at +132).
    i32 (*queueRequestEntity29)(int arg, HeRecord* h);
    // VIBE_Command_GetPacketStatusById(handle): nonzero == applied/acked.
    i32 (*packetStatus)(i32 handle);
    // VIBE_Command_GetPacketSeqById(handle): the linked seq record id (0 if none).
    i32 (*packetSeq)(i32 handle);
    // VIBE_He_FreeHandlerEntry(h): release the handler entry; returns orig result.
    i32 (*freeHandlerEntry)(HeRecord* h);

    // Object-array probes (the originals walk dword_13CE298 stride 169 / 256, and
    // the AiPlayer table dword_13CE294 stride 589).
    //   objectKindAt(slot): the object's *(_BYTE*)(base+169*slot) kind byte, or
    //       255 if the slot is empty/out of range.
    //   objectAiPlayerType(slot): byte_dword_13CE294[589 * objectKind] — the
    //       AiPlayer "faction type" of that object (7 == townsfolk/city).
    //   objectId(slot): *(_DWORD*)(base+169*slot+1) object id.
    //   objectSusceptible(slot): (plagueMask & (1 << aiPlayerType)) != 0 — is this
    //       object type a valid plague host (phase-1 target filter).
    u8  (*objectKindAt)(int slot);
    u8  (*objectAiPlayerType)(int slot);
    i32 (*objectId)(int slot);
    bool (*objectSusceptible)(int slot);

    // VIBE_GameObject_ResolveEntityById(id) -> nonzero if the object id resolves.
    bool (*resolveObjectId)(i32 id);
    // VIBE_Person_FindRecordById(id) -> nonzero if present (member liveness check).
    bool (*personPresent)(i32 id);
    // VIBE_Object_IsNearDoor(personId, objId) -> has the member reached the door.
    bool (*personNearDoor)(i32 personId, i32 objId);

    // Command builders (return value is the assigned packet handle where used).
    void (*queueSingle49)(i32 personId);
    void (*queueNamedObject53)(i32 personId, i32 objId, int flagA, int flagB);
    void (*queuePair33)(i32 personId, int v);
    i32  (*requestBuildOp73Str)(i32 objId);   // the "PEST" slot packet (4× per setup)
    void (*beginSlotResetPacket)(i32 personId); // cmd28 per-citizen reset accumulate

    // History notifications (news cluster).
    void (*broadcastOutbreak)(i32 objId);
    void (*broadcastSpreadDone)(i32 objId);

    // The opaque phase-2 infection scatter. Given the current target object and
    // the source person index, it applies the randomized need-decay to nearby
    // persons (the float physics the decompiler renders with undefined locals).
    // Returns the number of persons it infected (>0 -> the phase notes a spread).
    int (*plagueInfectNearby)(i32 targetObjId);
};

void SetNpcAction2Hooks(const NpcAction2Hooks* hooks);
const NpcAction2Hooks& GetNpcAction2Hooks();

// ===========================================================================
// gilde.exe 0x4d25c4 — VIBE_CharAction_PlagueSelectTarget(h@eax).
//   Stamps the clock into +82, then walks the object array from a random start
//   slot (RandomModulo(256)) until it finds an object whose AiPlayer type == 7;
//   up to 256 probes. On success: sets the scan cursor (+176) := RandomModulo(256),
//   the scan step (+180) := scanSteps[RandomModulo(16)], accumulates a per-citizen
//   slot-reset (cmd28) packet, issues 4 RequestBuildOp73Str("PEST") packets into
//   the member array (+188..+200), records the target id (+208), advances +1 min,
//   queues QueueRequestEntity29(0) into +132, and broadcasts the outbreak. On
//   failure: queues QueueRequestEntity29(-1) into +132.
void NpcAction2_PlagueSelectTarget(HeRecord* h);

// ===========================================================================
// gilde.exe 0x4d2900 — VIBE_CharAction_PlagueSpreadStep(h@eax).
//   The plague-spread coroutine. Returns the original's eax (a result/handle code;
//   the dispatcher ignores it except for the free path). Phases (state @+112):
//     teardown (state == -1/-2): if flag 0x02, send each member a "cured" pair33;
//        then free the handler entry.
//     gate: skip entirely while flag 0x04 set or the prior packet (+132) is still
//        pending (GetPacketStatusById == 0).
//     state 5 -> PlagueSelectTarget then fall through on the freshly-set state.
//     0 : refresh member packet ids from their seq records; +1 min; re-arm phase 1.
//     1 : find next susceptible object from the cursor; none -> +1 min, free(-1);
//         else send members to it (QueueRequestNamedObject53 "Pest"), advance 5
//         (someone present) or 20, set +204, re-arm phase 2.
//     2 : resolve +204; +5 min; if any member not yet at the door, re-arm phase 2;
//         else run the infection scatter (plagueInfectNearby), decrement +172; if
//         still >0 re-arm phase 1; else resolve the home object +208, send members
//         home, advance 1/10, re-arm phase 3 (or free(-1) if home is gone).
//     3 : +5 min; resolve +208; if any member not at the door, re-arm phase 3;
//         else broadcast spread-done and free(-1).
HeRecord* NpcAction2_PlagueSpreadStep(HeRecord* h);

// Registration entry point for this batch (does not clobber the first agent's
// NpcAction_TableEntry). The plague steps are CharAction coroutines driven off the
// He record, not the 0x63d964 NpcAction jump table, so no new table type is claimed.
void RegisterNpcActions2();

} // namespace guild::sim
