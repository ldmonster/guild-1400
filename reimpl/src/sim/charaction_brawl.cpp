#include "sim/charaction_brawl.h"

// charaction_brawl.cpp — VIBE_CharAction_BrawlStep @0x4d201c, the brawl
// (street-fight) combat damage-resolution coroutine driven over a He handler
// record. Sibling of GroupInteractStep (charaction_misc.cpp): same +112 state
// machine, same hook-routed leaves. The damage MATH — the hit-count knockout
// gate at 5 blows, the 24-tick swing cadence, the negated AP magnitude — is
// translated 1:1; the Person/relation/AP-event/message/packet leaves run through
// BrawlHooks. Provenance address on the function; +offset comments on the fields.

namespace guild::sim {

namespace {
BrawlHooks g_hooks{};   // default: all null (a null-leaf is a no-op / "gone")
} // namespace

void SetBrawlHooks(const BrawlHooks* hooks) {
    g_hooks = hooks ? *hooks : BrawlHooks{};
}
const BrawlHooks& GetBrawlHooks() { return g_hooks; }

// ---------------------------------------------------------------------------
// gilde.exe 0x4d201c — VIBE_CharAction_BrawlStep.
//
//   v5 = *(_DWORD *)(result + 112);                       // state
//   if (v5 == -2 || v5 == -1) return VIBE_He_FreeHandlerEntry(result);
//   if ((*(_BYTE *)(result + 120) & 4) == 0
//       && (*(_DWORD *)(result + 132) == -1
//           || (result = GetPacketStatusById(*(_DWORD *)(result + 132))) != 0)) {
//       result = *(_DWORD *)(v4 + 112);
//       *(_DWORD *)(v4 + 132) = -1;
//       if (result) {                                     // state != 0
//           if (result == 1) {                            // KNOCKOUT BLOW
//               RecordById = FindRecordById(*(v4 + 172));
//               if (RecordById) {
//                   RenderFormattedMessage(buf, 5804, *v12);
//                   SendEntityMessage(rec+1, rec+1, 0, buf, 1418, 0);
//                   fam = GetFamilyRecord(rec); if (fam) ++*(fam + 24);
//               }
//               *(v4+82..) = savedPose; return QueueRequestEntity29(-1, v4);
//           }
//       } else {                                          // state == 0: LANDED BLOW
//           v6 = &word_12CE910[268 * *(WORD*)(v4 + 8)];    // aggressor record
//           v7 = FindRecordById(*(v4 + 172));              // victim record
//           if (v7) {
//               AdjustRelationByMood(v6, HIBYTE(*(v4 + 181)));
//               AdjustRelationByMood(v6, HIBYTE(*(v4 + 182)));
//               RenderFormattedMessage(buf, 5803, *v6);
//               v8 = *(v4 + 186) + 1; *(v4 + 186) = v8;    // hit counter
//               v10 = *(WORD*)(v4 + 86) + 24;
//               v11 = -*(v4 + 180);
//               *(v4 + 112) = (v8 >= 5u);                  // 5 blows -> state 1
//               *(WORD*)(v4 + 86) = v10;
//               return RegisterApEvent((WORD)*v7, v11, 0, buf);   // AP DAMAGE
//           } else {
//               *(v4+82..) = savedPose; return QueueRequestEntity29(-1, v4);
//           }
//       }
//   }
//   return result;
// ---------------------------------------------------------------------------
BrawlOutcome BrawlStep(HeRecord* h) {
    const i32 state = Brawl_State(h);

    // Terminal states -> free the handler entry.
    if (state == -2 || state == -1) {
        if (g_hooks.freeHandlerEntry)
            g_hooks.freeHandlerEntry(h);
        return BrawlOutcome::Freed;
    }

    // Busy gate: skip while the busy flag (+120 & 4) is set, or while an
    // entity-request packet (+132) is still in flight (status == 0).
    const bool busy = (Brawl_Flags(h) & 4) != 0;
    if (busy)
        return BrawlOutcome::Busy;
    const i32 packet = Brawl_Packet(h);
    if (packet != -1) {
        const int status = g_hooks.packetStatus ? g_hooks.packetStatus(packet) : 0;
        if (status == 0)
            return BrawlOutcome::Busy;   // packet not yet resolved
    }

    // Packet resolved: clear the handle and act on the state.
    Brawl_Packet(h) = -1;

    if (state != 0) {
        if (state == 1) {
            // KNOCKOUT BLOW — the final hit. Resolve the victim, emit the defeat
            // message + bump the victim's family defeat counter, then restore the
            // saved pose and re-queue the entity-29 request.
            void* victim = g_hooks.findPersonById ? g_hooks.findPersonById(Brawl_VictimId(h))
                                                  : nullptr;
            if (victim) {
                if (g_hooks.sendDefeatMessage)
                    g_hooks.sendDefeatMessage(static_cast<u16>(*reinterpret_cast<u16*>(victim)));
                if (g_hooks.bumpVictimFamilyDefeats)
                    g_hooks.bumpVictimFamilyDefeats(victim);
            }
            if (g_hooks.restorePoseAndRequeue)
                g_hooks.restorePoseAndRequeue(h);
            return victim ? BrawlOutcome::Knockout : BrawlOutcome::KnockoutVictimGone;
        }
        // Any other nonzero state: the original falls through to `return result`
        // (no action this tick).
        return BrawlOutcome::Idle;
    }

    // state == 0: a LANDED BLOW. Apply the aggressor's two mood/relation deltas
    // to the victim, bump the hit counter (5 blows -> knockout state 1), advance
    // the swing cadence (+24 ticks), and register the negated-AP DAMAGE event.
    void* aggressor = g_hooks.aggressorRecord ? g_hooks.aggressorRecord(h) : nullptr;
    void* victim    = g_hooks.findPersonById ? g_hooks.findPersonById(Brawl_VictimId(h))
                                             : nullptr;
    if (victim) {
        if (g_hooks.adjustRelationByMood) {
            g_hooks.adjustRelationByMood(aggressor, Brawl_MoodKind1(h));
            g_hooks.adjustRelationByMood(aggressor, Brawl_MoodKind2(h));
        }
        const u8 hits = static_cast<u8>(Brawl_HitCount(h) + 1);  // v8 = +186 + 1
        Brawl_HitCount(h) = hits;
        Brawl_State(h) = BrawlNextState(hits);                   // (v8 >= 5)
        Brawl_ActionWord(h) = static_cast<u16>(Brawl_ActionWord(h) + kBrawlActionTicks);
        const i32 negAp = -Brawl_ApDamage(h);                    // v11 = -(+180)
        if (g_hooks.registerApEvent)
            g_hooks.registerApEvent(static_cast<u16>(*reinterpret_cast<u16*>(victim)), negAp);
        return BrawlOutcome::BlowLanded;
    }

    // Victim gone: restore the saved pose and re-queue the entity-29 request.
    if (g_hooks.restorePoseAndRequeue)
        g_hooks.restorePoseAndRequeue(h);
    return BrawlOutcome::BlowMissedVictimGone;
}

} // namespace guild::sim
