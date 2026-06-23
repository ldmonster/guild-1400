// npcaction_notify — gilde.exe 0x4c9dec VIBE_NpcAction_NotifyJoinLeaveGroup, 1:1.
// See npcaction_notify.h for the data model. Control flow, dispatch order, loop
// bounds, the kind==6||7 gates, the message ids, the float→int truncation, and the
// command/quickjump emission order all mirror the disassembly at 0x4c9dec.
#include "sim/npcaction_notify.h"

#include "util/coord.h"   // util::ConvertX (0x5c6b08 truncate toward zero)

namespace guild::sim {

static const NpcNotifyHooks kInert{};
static const NpcNotifyHooks* g_hn = &kInert;

void SetNpcNotifyHooks(const NpcNotifyHooks* hooks) { g_hn = hooks ? hooks : &kInert; }
const NpcNotifyHooks& GetNpcNotifyHooks() { return *g_hn; }

namespace {

// A resolved roster member: the original keeps the person-record pointer in v48[].
struct Member { void* rec; };

// VIBE_Text_RenderFormattedMessage boundary: the renderer itself is text/locale and
// out of the logic scope. We record the *format id* (the observable: which message
// was produced, and in what order) via the test sink. Returns nothing — the original
// writes a stack buffer that is then handed to the message senders.
inline void Render(const NpcNotifyHooks* H, int textId) {
    if (H->onRendered) H->onRendered(textId);
}

// He_SendQuickjumpMessage(recipient, -1, 0, buf, 1418, fromId, -1, 0, ctx). The
// constant -1/0 fields are the original's fixed args; only recipient/from/text vary.
inline void SendJump(const NpcNotifyHooks* H, i32 recipient, i32 from) {
    if (H->sendQuickjump) H->sendQuickjump(recipient, from, kNotifyHistoryTag, kNotifyHistoryTag);
}

inline bool IsPlayerKind(u8 k) { return k == kNotifyPlayerKind || k == kNotifyHostKind; }

} // namespace

void NpcActionNotifyJoinLeaveGroup(u32 tag, void* occupant, void* seat, i32 personId) {
    const NpcNotifyHooks* H = g_hn;

    const i32 fromId = H->occupantFromId ? H->occupantFromId(occupant) : 0;
    const u16 team   = H->occupantTeam   ? H->occupantTeam(occupant)   : 0;
    const u8  leaderKind  = H->leaderKind  ? H->leaderKind(team)  : 0;
    const i32 leaderObjId = H->leaderObjId ? H->leaderObjId(team) : 0;

    // --- prologue: resolve the 4 seat member slots into the roster (0x4c9e38) ---
    Member roster[kNotifyMemberSlots];
    int memberCount = 0;
    for (int slot = 0; slot < kNotifyMemberSlots; ++slot) {
        i32 id = H->seatMemberId ? H->seatMemberId(seat, slot) : -1;
        void* rec = H->findPersonById ? H->findPersonById(id) : nullptr;
        if (rec)
            roster[memberCount++].rec = rec;
    }

    auto memberKind  = [&](int i) -> u8  { return H->memberKind  ? H->memberKind(roster[i].rec)  : 0; };
    auto memberObjId = [&](int i) -> i32 { return H->memberObjId ? H->memberObjId(roster[i].rec) : 0; };

    switch (tag) {
    // =====================================================================
    // "cler" (0x636C6572) — 5292 broadcast to every resolved roster member.
    // =====================================================================
    case kNotifyTagCler: {
        Render(H, kNotifyTextClearBroad);                    // 5292
        for (int i = 0; i < memberCount; ++i) {
            if (IsPlayerKind(memberKind(i)))
                SendJump(H, memberObjId(i), fromId);
        }
        return;
    }

    // =====================================================================
    // "exec" (0x65786563) — settle the round (bar tab + social link graph).
    // =====================================================================
    case kNotifyTagExec: {
        // per-member tab = ConvertX(price(377)+price(378)) truncated toward zero.
        double a = H->marketPrice ? H->marketPrice(kNotifyTabGoodA, H->marketCtx) : 0.0;
        double b = H->marketPrice ? H->marketPrice(kNotifyTabGoodB, H->marketCtx) : 0.0;
        i32 perMember = static_cast<i32>(util::ConvertX(a + b));   // 0x4c9f6c/0x4c9f71
        Render(H, kNotifyTextExecTab);                       // 5289 ("round costs <tab>")

        i32 total = 0;
        for (int i = 0; i < memberCount; ++i) {
            total += perMember;
            // money: leader pays each member the tab share.
            if (H->queueRequest16)
                H->queueRequest16(leaderObjId, memberObjId(i), perMember, H->marketCtx);
            if (IsPlayerKind(memberKind(i)))
                SendJump(H, memberObjId(i), fromId);
            // reciprocal social-link packets leader<->member (delta 3).
            if (H->requestCoord27) {
                H->requestCoord27(leaderObjId, memberObjId(i), 3);
                H->requestCoord27(memberObjId(i), leaderObjId, 3);
            }
        }
        // tell the leader the total (5282) when the leader is a player/host.
        if (IsPlayerKind(leaderKind)) {
            Render(H, kNotifyTextExecLeader);                // 5282
            SendJump(H, leaderObjId, fromId);
        }
        // pairwise member<->member social links (all ordered pairs except i==j).
        for (int i = 0; i < memberCount; ++i) {
            for (int j = 0; j < memberCount; ++j) {
                if (i != j && H->requestCoord27) {
                    H->requestCoord27(memberObjId(i), memberObjId(j), 3);
                    H->requestCoord27(memberObjId(j), memberObjId(i), 3);
                }
            }
        }
        return;
    }

    // =====================================================================
    // "new " (0x6E657720) — 5287 broadcast to every player/host city person
    // (except the leader). Sweeps all 768 person records.
    // =====================================================================
    case kNotifyTagNew: {
        Render(H, kNotifyTextNewBroad);                      // 5287
        for (int i = 0; i < kNotifyPersonCount; ++i) {
            u8 k = H->cityPersonKind ? H->cityPersonKind(i) : 0;
            i32 objId = H->cityPersonObjId ? H->cityPersonObjId(i) : 0;
            if (IsPlayerKind(k) && objId != leaderObjId)
                SendJump(H, objId, fromId);
        }
        return;
    }

    // =====================================================================
    // "join" (0x6A6F696E) — a4's person joined.
    // =====================================================================
    case kNotifyTagJoin: {
        void* personRec = H->findPersonById ? H->findPersonById(personId) : nullptr;
        if (!personRec)
            return;                                          // 0x4ca1cf early-out
        Render(H, kNotifyTextJoinBroad);                     // 5290 (join broadcast)
        // tell every player/host roster member.
        for (int i = 0; i < memberCount; ++i) {
            if (IsPlayerKind(memberKind(i)))
                SendJump(H, memberObjId(i), fromId);
        }
        // tell the leader (5283, or 5283+5285 welcome when memberCount==3).
        if (IsPlayerKind(leaderKind)) {
            if (memberCount == 3) {
                Render(H, kNotifyTextJoinMember);            // 5283 (a2,personName)
                Render(H, kNotifyTextJoin3rd);               // 5285
                // "%s%s" concat of the two — observable as both ids in order.
            } else {
                Render(H, kNotifyTextJoinMember);            // 5283
            }
            SendJump(H, leaderObjId, fromId);
        }
        return;
    }

    // =====================================================================
    // "leav" (0x6C656176) — a4's person left.
    // =====================================================================
    case kNotifyTagLeav: {
        void* personRec = H->findPersonById ? H->findPersonById(personId) : nullptr;
        if (!personRec)
            return;                                          // 0x4ca354 early-out
        const i32 leaverObjId = H->memberObjId ? H->memberObjId(personRec)
                                               : 0;          // *((u32*)v31+1)
        Render(H, kNotifyTextLeaveBroad);                    // 5291 (leave broadcast)
        // tell every player/host roster member EXCEPT the leaver itself.
        for (int i = 0; i < memberCount; ++i) {
            if (IsPlayerKind(memberKind(i)) && leaverObjId != memberObjId(i))
                SendJump(H, memberObjId(i), fromId);
        }
        // tell the leader (5284, or 5284+5286 farewell when memberCount==1).
        if (IsPlayerKind(leaderKind)) {
            if (memberCount == 1) {
                Render(H, kNotifyTextLeaveLeadr);            // 5284
                Render(H, kNotifyTextLeaveLast);             // 5286
            } else {
                Render(H, kNotifyTextLeaveLeadr);            // 5284
            }
            SendJump(H, leaderObjId, fromId);
        }
        // when the group held 4 members, broadcast 5288 (0x4ca494). Walk all 768 city
        // persons gated player/host && objId != leader; for each, the original scans
        // the roster and sends ONLY when the person is found in the roster (v40<v56,
        // i.e. the inner loop broke early on a match) — a roster member re-confirmation.
        if (memberCount == 4) {
            Render(H, kNotifyTextLeaveAll);                  // 5288
            for (int i = 0; i < kNotifyPersonCount; ++i) {
                u8 k = H->cityPersonKind ? H->cityPersonKind(i) : 0;
                i32 objId = H->cityPersonObjId ? H->cityPersonObjId(i) : 0;
                if (!IsPlayerKind(k) || objId == leaderObjId)
                    continue;
                bool inRoster = false;
                for (int m = 0; m < memberCount; ++m) {
                    if (objId == memberObjId(m)) { inRoster = true; break; }
                }
                if (inRoster)                                // v40 < v56
                    SendJump(H, objId, fromId);
            }
        }
        return;
    }

    default:
        return;                                              // unknown tag: no-op
    }
}

} // namespace guild::sim
