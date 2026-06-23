#pragma once
// npcaction_notify — faithful 1:1 reconstruction of the Guild (gilde.exe) group
// join/leave notification leaf (32-bit x86, imagebase 0x400000).
//
//   gilde.exe 0x4c9dec  VIBE_NpcAction_NotifyJoinLeaveGroup
//       __usercall(a1@eax, a2@edx, a3@ebx, a4@ecx)
//
// This is the "Stammtisch"/group membership notifier. It is dispatched by a FourCC
// tag (a1, read big-endian as text) and emits the appropriate broadcast / command
// stream over the resolved group:
//
//   "join" (0x6A6F696E) — a4's person joined; tell the leader (+ 3-member welcome).
//   "leav" (0x6C656176) — a4's person left;  tell the others + (4-member) farewell.
//   "cler" (0x636C6572) — clear: broadcast 5292 to every resolved member.
//   "new " (0x6E657720) — broadcast 5287 to every player/host city person.
//   "exec" (0x65786563) — settle round: distribute the bar tab (market price of
//                         goods 377+378) to each member, wire the social-link
//                         coord packets between every pair, and tell the leader.
//
// === Data model (recovered exactly) ===
// The original reads three things, all kept as opaque handles + leaf hooks here so
// the STATE logic is reconstructed 1:1 while the genuine boundaries (text render,
// message send, command-queue, person lookup, market price) stay swappable:
//
//   a2 (occupant)   — the joining/leaving NPC's entity record. The original reads
//                     *(u16*)(a2+39) as the team index, *(u32*)(a2+1) as its objId
//                     (the message "from" field), and passes a2 itself as the 5290/
//                     5291/5283/5284 text "%d" argument (its leading id word region).
//   a3 (seat)       — the type-301 group/seat scene node. Its 4 member slots live at
//                     +28/+32/+36/+40 (4 dwords of person ids; -1 == empty). The
//                     prologue resolves each to a person record (FindRecordById) into
//                     a local roster; `memberCount` = number resolved.
//   a4 (personId)   — the joining/leaving person's id (only read by join/leav, via
//                     FindRecordById(a4)).
//
//   word_12CE910[]  — the 768-entry city/person record array, stride 536 (268 u16).
//                     v52 = &word_12CE910[268 * teamIndex] is the GROUP LEADER record;
//                     leader objId = *(u32*)(v52+4), leader kind = *(u8*)(v52+2).
//                     byte_12CE912[i] (=+2) is person i's kind, dword_12CE914[i] (=+4)
//                     is person i's objId; the "new "/"cler"/"leav" sweeps walk all
//                     768, gating kind==6||7 (a human/host player).
//
// A roster member record exposes kind (+2) and objId (+4). A "kind" of 6 or 7 means a
// human/host player whose UI must receive the quickjump message; non-6/7 members are
// only used as command-graph endpoints (exec) but not messaged.
//
// Float→int: the "exec" tab total uses VIBE_Coord_ConvertX (TRUNCATE toward zero) on
// `price(377)+price(378)` — reproduced via util::ConvertX. (The fistp at 0x4c9f71
// stores the already-integral st0, exact.)

#include "guild/common/types.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// Recovered FourCC dispatch tags (a1, compared as little-endian dwords; the bytes
// spell the tag when read big-endian).
// ---------------------------------------------------------------------------
constexpr u32 kNotifyTagJoin = 0x6A6F696Eu;  // "join"
constexpr u32 kNotifyTagCler = 0x636C6572u;  // "cler"
constexpr u32 kNotifyTagLeav = 0x6C656176u;  // "leav"
constexpr u32 kNotifyTagNew  = 0x6E657720u;  // "new "
constexpr u32 kNotifyTagExec = 0x65786563u;  // "exec"

// Recovered text-message ids (decimal) used with VIBE_Text_RenderFormattedMessage.
constexpr int kNotifyTextExecLeader = 5282; // 0x14A2 — bar-tab total to leader
constexpr int kNotifyTextJoinMember = 5283; // 0x14A3 — "<a2> joined" to leader
constexpr int kNotifyTextLeaveLeadr = 5284; // 0x14A4 — "<a4> left" to leader
constexpr int kNotifyTextJoin3rd    = 5285; // 0x14A5 — appended when memberCount==3
constexpr int kNotifyTextLeaveLast  = 5286; // 0x14A6 — appended when memberCount==1
constexpr int kNotifyTextNewBroad   = 5287; // 0x14A7 — "new " all-player broadcast
constexpr int kNotifyTextLeaveAll   = 5288; // 0x14A8 — leave broadcast (count==4)
constexpr int kNotifyTextExecTab    = 5289; // 0x14A9 — "round costs <tab>"
constexpr int kNotifyTextJoinBroad  = 5290; // 0x14AA — join broadcast to members
constexpr int kNotifyTextLeaveBroad = 5291; // 0x14AB — leave broadcast to members
constexpr int kNotifyTextClearBroad = 5292; // 0x14AC — clear broadcast to members

constexpr int kNotifyHistoryTag   = 1418;   // 0x58A — He_SendQuickjumpMessage textId
constexpr int kNotifyTabGoodA     = 377;    // 0x179 — bar-tab good id A
constexpr int kNotifyTabGoodB     = 378;    // 0x17A — bar-tab good id B
constexpr int kNotifyMemberSlots  = 4;      // seat member-slot array length
constexpr int kNotifyPersonCount  = 768;    // word_12CE910 record count
constexpr u8  kNotifyPlayerKind   = 6;      // kind 6 -> human player
constexpr u8  kNotifyHostKind     = 7;      // kind 7 -> host

// ---------------------------------------------------------------------------
// Leaf hooks. nullptr installs an inert default (no broadcasts, no commands, every
// query absent). All handles are opaque (the engine's register-held record bases).
// Tests install recording/synthetic mocks. Mirrors the NpcAction10Hooks pattern.
// ---------------------------------------------------------------------------
struct NpcNotifyHooks {
    // a4 -> person record (FindRecordById). null when absent (join/leav early-out).
    void* (*findPersonById)(i32 id) = nullptr;

    // Occupant (a2) field reads.
    u16  (*occupantTeam)(void* occupant) = nullptr;   // *(u16*)(a2+39)
    i32  (*occupantFromId)(void* occupant) = nullptr; // *(u32*)(a2+1) — msg "from"
    // a2's name/id arg passed to RenderFormattedMessage for 5290/5291/5283/5284.
    u16  (*occupantNameArg)(void* occupant) = nullptr;// the a2-region "%d" value
    // The personRecord's name word (*(u16*)rec) used in 5290/5291/5283/5284.
    u16  (*personNameArg)(void* personRec) = nullptr;

    // Seat (a3) member-slot id read: slot in [0..3], returns the person id (or -1).
    i32  (*seatMemberId)(void* seat, int slot) = nullptr; // *(u32*)(a3+28+4*slot)

    // Group leader record &word_12CE910[268*team]: kind/objId.
    u8   (*leaderKind)(u16 team) = nullptr;           // *(u8*)(v52+2)
    i32  (*leaderObjId)(u16 team) = nullptr;          // *(u32*)(v52+4)

    // Roster member record (resolved from a seat slot id) field reads.
    u8   (*memberKind)(void* memberRec) = nullptr;    // *(u8*)(rec+2)
    i32  (*memberObjId)(void* memberRec) = nullptr;   // *(u32*)(rec+4)

    // City-person sweep (the 768-record array): per-index kind/objId.
    u8   (*cityPersonKind)(int index) = nullptr;      // byte_12CE912[index]
    i32  (*cityPersonObjId)(int index) = nullptr;     // dword_12CE914[index]

    // Bar-tab market price (VIBE_Building_LookupCachedMarketPrice(goodId, ctx)).
    double (*marketPrice)(i32 goodId, u8 ctx) = nullptr;
    u8     marketCtx = 0;                              // byte_6477A1

    // Boundaries (effects). textId is the *format* message id; the renderer fills it.
    //  sendQuickjump(recipientId, fromId, textId, historyTag) — He_SendQuickjumpMessage.
    void (*sendQuickjump)(i32 recipientId, i32 fromId, int textId, int historyTag) = nullptr;
    //  queueRequest16(leaderId, memberId, amount, ctx)        — money/tab transfer.
    void (*queueRequest16)(i32 leaderId, i32 memberId, i32 amount, u8 ctx) = nullptr;
    //  requestCoord27(a, b, delta)                            — social-link packet.
    void (*requestCoord27)(i32 a, i32 b, int delta) = nullptr;

    // Recording sink (test-only): every rendered message id, in emission order.
    void (*onRendered)(int textId) = nullptr;
};

void SetNpcNotifyHooks(const NpcNotifyHooks* hooks);
const NpcNotifyHooks& GetNpcNotifyHooks();

// gilde.exe 0x4c9dec — VIBE_NpcAction_NotifyJoinLeaveGroup(tag, occupant, seat, personId).
//   tag       : FourCC dispatch (kNotifyTag*). Unknown tags are a no-op (return).
//   occupant  : a2 — the joining/leaving NPC's entity record (null is UB in the
//               original, which dereferences it; callers always pass a live record).
//   seat      : a3 — the type-301 group/seat node (4 member slots at +28).
//   personId  : a4 — the joining/leaving person id (join/leav only).
void NpcActionNotifyJoinLeaveGroup(u32 tag, void* occupant, void* seat, i32 personId);

} // namespace guild::sim
