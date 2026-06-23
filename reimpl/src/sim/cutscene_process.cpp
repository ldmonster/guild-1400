#include "sim/cutscene_process.h"

namespace guild::sim {

// ===========================================================================
// Leaf-hook plumbing.
// ===========================================================================
namespace {
const CutsceneProcHooks* g_hooks = nullptr;
CutsceneProcHooks        g_inert{};   // all-null -> no-op
}

void SetCutsceneProcHooks(const CutsceneProcHooks* hooks) { g_hooks = hooks; }
const CutsceneProcHooks& GetCutsceneProcHooks() {
    return g_hooks ? *g_hooks : g_inert;
}

// ===========================================================================
// Slot field accessors. The original addresses each slot field through a
// per-field global aliased onto the flat 96*276 array; here we read the
// CutsceneSlot struct fields directly (offsets verified in cutscene.h):
//   id(+0) finished(+4) type(+8) master(+12) readyTime(+24,GameTime)
//   minuteWordAt+28 stateFlags(+38) started(+40) partCount(+48) partIds(+52)
//   seed dword index 30 == +120 (0x78)
// ===========================================================================
namespace {
// Read the slot's +28 word (word_11AE6CC) — the "type magnitude" the original
// compares against 0x16 to decide whether to push the ready window forward 9h.
inline u16 SlotWord28(const CutsceneSlot* s) {
    return *reinterpret_cast<const u16*>(
        reinterpret_cast<const u8*>(s) + 28);
}
// The cutscene RNG seed lives at dword index 30 (+0x78) of the slot.
inline i32 SlotSeed(const CutsceneSlot* s) {
    return *reinterpret_cast<const i32*>(
        reinterpret_cast<const u8*>(s) + 120);
}
// Fetch the per-type table entry for a slot's type byte. The slot type (+8) is a
// u8 (0..255); the recovered table holds exactly kCutsceneTypeCount (12) types.
// A malformed slot with type >= 12 would index past our std::array (OOB read of
// stale fn pointers — the same garbage the original's dword_11AE5C0[5*type] would
// read, except our bounded array makes it a hard overflow). Treat an out-of-range
// type as having no main/step fn: valid types (0..11) are byte-identical, and a
// garbage type can no longer dispatch through an OOB function pointer.
inline const CutsceneTypeEntry* SlotTypeEntry(const CutsceneTypeTable* types,
                                              u8 type) {
    if (!types || type >= kCutsceneTypeCount)
        return nullptr;
    return &(*types)[type];
}
}

// ===========================================================================
// gilde.exe 0x4abec8 — VIBE_Cutscene_ActorHasParticipant
//   for (i = 0; i < slot->partCount; ++i)
//     if (personId == slot->partIds[i])
//       return Person_FindRecordById(slot->partIds[i]) != 0;
//   return 0;
// ===========================================================================
bool CutsceneActorHasParticipant(const CutsceneContext& ctx,
                                 i32 personId, const CutsceneSlot* slot) {
    // partCount (+48) is a u8 (0..255); the participant id array (+52) holds
    // exactly kMaxParticipants (16). AddParticipant enforces that bound, but a
    // malformed slot (e.g. AllocSlot from a bad template) could carry a larger
    // count — clamp so we never read past partIds[] (OOB). Valid slots are
    // unaffected.
    int count = slot->partCount;
    if (count > kMaxParticipants) count = kMaxParticipants;
    for (int i = 0; i < count; ++i) {
        if (personId == slot->partIds[i]) {
            if (!ctx.personKind) return true;                  // resolves always
            return ctx.personKind(slot->partIds[i]) != 0xFF;   // record present?
        }
    }
    return false;
}

// ===========================================================================
// gilde.exe 0x4b0844 — VIBE_Cutscene_AddActorToSlot
//   actorList is the Person "+140" 8-entry list. Find an entry already equal to
//   masterEntityId; otherwise write it into the first free (-1) slot.
//   Returns the index it settled on (0..7); the original returns the &entry, we
//   return the index (observationally equivalent for the callers).
// ===========================================================================
int CutsceneAddActorToSlot(i32 masterEntityId, i32* actorList) {
    int idx = 0;
    if (actorList[0] != masterEntityId) {
        do {
            ++idx;
        } while (idx < 8 && actorList[idx] != masterEntityId);
    }
    if (idx >= 8) {                       // not already present
        int j = 0;
        if (actorList[0] == -1) {
            actorList[0] = masterEntityId;
            return 0;
        }
        while (true) {
            ++j;
            if (j >= 8) break;
            if (actorList[j] == -1) {
                actorList[j] = masterEntityId;
                return j;
            }
        }
        return j;                          // list full
    }
    return idx;
}

// ===========================================================================
// gilde.exe 0x4b08bc — VIBE_Cutscene_RemoveActorFromSlot
//   Clear the entry whose value == masterEntityId (set to -1). Returns index.
// ===========================================================================
int CutsceneRemoveActorFromSlot(i32 masterEntityId, i32* actorList) {
    int idx = 0;
    if (actorList[0] == masterEntityId) {
        actorList[0] = -1;
        return 0;
    }
    while (true) {
        ++idx;
        if (idx >= 8) break;
        if (actorList[idx] == masterEntityId) {
            actorList[idx] = -1;
            return idx;
        }
    }
    return idx;
}

// ===========================================================================
// Per-participant parallel-state table.
// ===========================================================================
// gilde.exe 0x4aa9b4 — VIBE_Cutscene_InitParticipantTable (deterministic core).
void CutsceneParticipants::Init(const CutsceneSlot* slot) {
    int count = slot ? slot->partCount : 0;
    for (int i = 0; i < kCutsceneParticipants; ++i) {
        rows_[i].enabled = 1;          // dword_11AB000[i] = 1
        rows_[i].done    = 0;          // dword_11AB008[i] = 0
        rows_[i].ready   = 0;          // dword_11AB004[i] = 0
        rows_[i].active  = 1;          // dword_11AB00C[i] = 1
        if (slot && i < count)
            rows_[i].personId = slot->partIds[i];   // dword_11AB010[i]
        else
            rows_[i].personId = -1;
    }
}

// gilde.exe 0x4aaa74 — VIBE_Cutscene_AllParticipantsDone.
bool CutsceneParticipants::AllDone(int count) const {
    for (int i = 0; i < count; ++i) {
        if (rows_[i].enabled && !rows_[i].done && rows_[i].personId != -1)
            return false;
    }
    return true;
}

// gilde.exe 0x4aaab8 — VIBE_Cutscene_AllParticipantsReady.
bool CutsceneParticipants::AllReady(int count, bool (*resolves)(i32)) const {
    for (int i = 0; i < count; ++i) {
        const auto& r = rows_[i];
        if (r.enabled && r.personId != -1 &&
            (!resolves || resolves(r.personId)) && !r.ready)
            return false;
    }
    return true;
}

// ===========================================================================
// gilde.exe 0x4ab55c — VIBE_Cutscene_ExecMainFunc (deterministic skeleton).
//   The original is render/voice/fade/window/command heavy. The deterministic
//   spine the simulation depends on:
//     1. mark the slot busy: slot->stateFlags |= 4;
//     2. seed the cutscene RNG from the slot's seed word (RandSeed = slot[30]);
//     3. run the per-type "main func" (dword_11AE5C0[5*type]);
//     4. reset the slot's participant table.
//   Returns the main fn's result (0 if none).
// ===========================================================================
int CutsceneExecMainFunc(CutsceneContext& ctx, CutsceneSlot* slot) {
    slot->stateFlags |= 0x04;                       // *((BYTE*)v73+38) |= 4u
    if (ctx.rng)
        ctx.rng->SetSeed(SlotSeed(slot));           // VIBE_Cutscene_SetRandSeed(v73[30])

    int result = 0;
    if (const CutsceneTypeEntry* te = SlotTypeEntry(ctx.types, slot->type)) {
        CutsceneTypeFn fn = te->main;                        // dword_11AE5C0[5*type]
        if (fn)
            result = fn(slot);
    }

    // VIBE_Cutscene_InitParticipantTable((int)v73) at the tail.
    if (ctx.types) {
        // (participant rearm-deltas / cmd23 requests are the host's job)
    }
    ++ctx.execCount;
    return result;
}

// ===========================================================================
// gilde.exe 0x4ac680 — VIBE_Cutscene_RunForMaster.
//   while ((slot = FindLowestPriority())):
//     if (ActorHasParticipant(masterId, slot)) { ExecMainFunc(slot); ran = 1; }
//     RemoveById(slot->id);          // tear down
//   The original spins until no active slot remains; we bound the loop by the
//   table size since each iteration removes (or skips-then-removes) a slot.
// ===========================================================================
int CutsceneRunForMaster(CutsceneContext& ctx, i32 masterId) {
    int ran = 0;
    for (int guard = 0; guard < kCutsceneSlotCount; ++guard) {
        CutsceneSlot* slot = ctx.table->FindLowestPriority();
        if (!slot) break;
        i32 id = slot->id;
        if (CutsceneActorHasParticipant(ctx, masterId, slot)) {
            CutsceneExecMainFunc(ctx, slot);
            ran = 1;
        }
        ctx.table->RemoveById(id);     // FindSlotById + clear + id = -1
    }
    return ran;
}

// ===========================================================================
// Built-in PrepareReady skeleton (gilde.exe 0x4ac1b0).
//   The original walks the slot's participants, validates that any "player-class"
//   participant (kind 6 or 7) whose home != this slot can be coerced (via cmd42),
//   then checks the master (cmd95). The command round-trips are host-side; the
//   deterministic decision is: ready iff CheckMaster passes. We model it as:
//     -1  if the master is not a participant and cannot be resolved,
//      1  otherwise (ready).
//   Tests/host can override via hooks.prepareReady for the full protocol.
// ===========================================================================
namespace {
int BuiltinPrepareReady(CutsceneContext& ctx, CutsceneSlot* slot) {
    // VIBE_Cutscene_CheckMaster: master in participant list -> ready.
    if (CutsceneActorHasParticipant(ctx, slot->master, slot))
        return 1;
    // master absent: if it resolves at all, the original re-broadcasts (cmd95)
    // and returns "not yet"; otherwise it is invalid.
    if (ctx.personKind && slot->master != -1 &&
        ctx.personKind(slot->master) != 0xFF)
        return 0;                  // pending re-broadcast
    return -1;                     // invalid master
}
}

// ===========================================================================
// gilde.exe 0x4ac31c — VIBE_Cutscene_ProcessActive.
// ===========================================================================
int CutsceneProcessActive(CutsceneContext& ctx) {
    const auto& hooks = GetCutsceneProcHooks();
    CutsceneSlot* slots = ctx.table->Slots();
    ctx.execCount = 0;

    for (int i = 0; i < kCutsceneSlotCount; ++i) {
        CutsceneSlot* s = &slots[i];

        // alive gate: byte_11AE6E0[v0] (+48) must be nonzero.
        if (!s->partCount)
            continue;
        // state-flag skip: bit0 (active-in-priority) or bit2 (busy/done).
        u8 sf = s->stateFlags;
        if ((sf & 0x01) != 0 || (sf & 0x04) != 0)
            continue;

        // window A = slot readyTime (+24) advanced by -60 seconds. The slot's
        // own readyTime (+24) is the "timeout"/cleanup window (windowB below).
        GameTime windowA = s->readyTime;
        GameTimeAdvance(&windowA, /*days*/0, /*sec*/0, /*min*/-60);
        const GameTime* windowB = &s->readyTime;

        // master fill: if master unset and a relevant mode flag is on, adopt the
        // local master. (byte_63CC28 & 8) || (word_63C740 & 8).
        if (s->master == -1 &&
            (((ctx.modeFlagsB & 0x08) != 0) || ((ctx.modeFlags & 0x08) != 0)))
            s->master = ctx.localMaster;

        bool ownedByLocal = (s->master == ctx.localMaster);

        // ----- ready window: owned, not started, clock past windowA -----
        if (ownedByLocal && s->started == 0 &&
            GameTimeCompare(&ctx.clock, &windowA) > 0) {
            int ready = hooks.prepareReady ? hooks.prepareReady(s)
                                           : BuiltinPrepareReady(ctx, s);
            if (ready == -1) {
                // re-arm the ready window by +5s (+9h if the +28 word >= 0x16).
                GameTimeAdvance(&s->readyTime, 0, 5, 0);
                if (SlotWord28(s) >= 0x16u)
                    GameTimeAdvance(&s->readyTime, 0, 0, 9 * 60);   // +9h
            } else if (ready == 0) {
                GameTimeAdvance(&s->readyTime, 0, 30, 0);           // +30s
                if (SlotWord28(s) >= 0x16u)
                    GameTimeAdvance(&s->readyTime, 0, 0, 9 * 60);
            } else {
                s->started = 1;                                     // dword+40 = 1
                int stepResult = 1;
                const CutsceneTypeEntry* te = SlotTypeEntry(ctx.types, s->type);
                CutsceneTypeFn step = te ? te->step : nullptr;
                if (step)
                    stepResult = step(s);
                if (!stepResult) {
                    if (hooks.requestBuildOp88)
                        hooks.requestBuildOp88(s->id);              // op88 timeout
                    continue;
                }
            }
        }

        // ----- no step fn + clock past timeout window -> mark finished -----
        // (0x4ac4cc) if (step fn absent && GameTime_Compare(clock, windowB) > 0)
        //               finished = 1;
        const CutsceneTypeEntry* te2 = SlotTypeEntry(ctx.types, s->type);
        CutsceneTypeFn step = te2 ? te2->step : nullptr;
        if (!step && GameTimeCompare(&ctx.clock, windowB) > 0)
            s->finished = 1;                                        // dword+4 = 1

        // ----- participant / orphan cleanup (clock past timeout, not finished) -
        // (0x4ac4f6) if (GameTime_Compare(clock, windowB) > 0 && finished == 0):
        if (GameTimeCompare(&ctx.clock, windowB) > 0 && !s->finished) {
            bool localIsParticipant =
                CutsceneActorHasParticipant(ctx, ctx.localMaster, s);
            // (0x4ac525/0x4ac52d) participant AND state-flags high bit (0x80) set
            if (localIsParticipant && (s->stateFlags & 0x80) != 0) {
                // (0x4ac54e) owned by this client's slot -> finish, else nothing
                if (s->id == ctx.localSlotId)
                    s->finished = 1;                                // loc_4AC556
            } else {
                // (loc_4AC5D9) master == local master AND ready bit (0x02) set ->
                // finish; otherwise drop the orphaned slot.
                if (s->master == ctx.localMaster && (s->stateFlags & 0x02) != 0) {
                    s->finished = 1;                                // loc_4AC556
                } else {
                    ctx.table->RemoveById(s->id);                   // loc_4AC602
                }
            }
        }

        // ----- timeout reached + finished -> exec and tear down -----
        // (0x4ac567) if (GameTime_Compare(clock, windowB) > 0 && finished):
        //               ExecMainFunc(slot); FindSlotById -> clear -> id = -1;
        if (GameTimeCompare(&ctx.clock, windowB) > 0 && s->finished) {
            i32 id = s->id;
            CutsceneExecMainFunc(ctx, s);
            ctx.table->RemoveById(id);
        }
    }
    return 0;   // the original returns v17 (always 0); execCount is in ctx.
}

// ===========================================================================
// gilde.exe 0x4acefc — VIBE_Cutscene_InitCommandTable: the recovered per-type
// step-table layout. The fn -> slot mapping is byte-for-byte from the original
// initializer (see the header table). Types not listed leave their slots null.
// ===========================================================================
void InitCutsceneTypeTable(CutsceneTypeTable& table, const CutsceneTypeFns& f) {
    table.Clear();
    // type 0
    table[0]  = {f.playTobyScene, f.nullSub, nullptr, 0, nullptr};
    // type 1
    table[1]  = {f.councilSession, f.successorA, f.successorB, 0, nullptr};
    // type 2
    table[2]  = {f.courtTrial, f.tortureForm, f.sessionDecision, 0, f.electionForm};
    // type 3
    table[3]  = {f.battleSetup, f.beginBattle, nullptr, 1, f.battleInfoText};
    // type 4
    table[4]  = {f.duel, f.showDuelWindow, f.rollDuelTier, 0, f.duelCheckParticipants};
    // type 5
    table[5]  = {f.execution, nullptr, nullptr, 0, f.duelBuildMessages};
    // type 6
    table[6]  = {f.wedding, nullptr, nullptr, 0, f.checkMarriage};
    // type 7
    table[7]  = {f.birth, nullptr, nullptr, 0, f.checkBirth};
    // type 8
    table[8]  = {f.death, nullptr, nullptr, 0, f.checkDeath};
    // type 9
    table[9]  = {f.bankruptcy, nullptr, nullptr, 0, nullptr};
    // type 10
    table[10] = {f.auction, f.leaseWindow, f.leaseAutoResolve, 1, f.broadcastMessage};
    // type 11
    table[11] = {f.salon, nullptr, nullptr, 0, nullptr};
}

} // namespace guild::sim
