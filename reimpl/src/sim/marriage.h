#pragma once
// ===========================================================================
// marriage.h — marriage / courtship / family-formation cluster
// ===========================================================================
// MODULE: sim person social cluster (namespace guild::sim).
//
// Faithful 1:1 ports of the marriage / spouse-selection / family-formation leaves
// from gilde.exe (imagebase 0x400000). These sit alongside person_relations.cpp
// (the relation/heir collectors) and reuse its PersonRelEntry / PersonRelCtx
// substrate verbatim — they are NOT redefined here.
//
// Translated functions (addresses absolute, imagebase 0x400000):
//   VIBE_Person_CountAdultChildren                  0x58c454
//   VIBE_Office_CollectRelativeCandidates           0x5556c4
//   VIBE_Office_CollectSpouseAndBusinessCandidates  0x5559d8
//   VIBE_NpcAction_BeginMarriage                    0x5687b0
//
// SHARED SUBSTRATE (reused, not redefined):
//   * PersonRelEntry, PersonRelCtx, SetPersonRelCtx/GetPersonRelCtx, the
//     HeMatchKey172/HeMatchRel176/HeMatchKey196 accessors, PersonResolveStatusFlags
//     — all from sim/person_relations.h.
//   * g_persons, PersonFindRecordById — from sim/entity.h (real, called directly).
//
// CROSS-CLUSTER LEAVES injected for the two candidate collectors are the SAME ones
// person_relations already routes through PersonRelCtx (heFindFirst/heFindNext,
// evaluateEligibility). The two collectors additionally read the live game-date low
// dword (qword_13CE852); that is taken from PersonRelCtx::gameDateLow (added here).
//
// VIBE_NpcAction_BeginMarriage is a UI-driven orchestrator: it picks the two
// partners (directly by id, or via an office-overview picker window), then queues
// the two reciprocal "begin courtship/marriage" relation commands and sends the
// wedding notification messages. The UI / command / text leaves are routed through
// MarriageCtx so the partner-resolution + command-emission logic is exercisable in
// isolation; the shipping game wires them to the real Amt/Command/He clusters.
#include "guild/common/types.h"
#include "sim/types.h"
#include "sim/he.h"
#include "sim/person_relations.h"   // PersonRelEntry, PersonRelCtx, He* accessors

namespace guild::sim {

// ---------------------------------------------------------------------------
// He-handler fields the relative/spouse collectors read beyond the three shared
// keys (HeMatchKey172 @+172, HeMatchRel176 @+176, HeMatchKey196 @+196).
//   *((DWORD*)h + 45) == h+180 : relative id #2  (CollectRelativeCandidates)
//   *((DWORD*)h + 46) == h+184 : relative id #3
//   *((DWORD*)h + 47) == h+188 : relative id #4
//   *((DWORD*)h + 48) == h+192 : relative id #5
//   *(DWORD*)(h + 197)         : handler creation/last-seen date (compared to
//                                qword_13CE852 to pick the MOST RECENT relative
//                                handler). Unaligned dword read — memcpy below.
inline i32& HeMatchRel180(HeRecord* h) { return *reinterpret_cast<i32*>(HeBytes(h) + 180); }
inline i32& HeMatchRel184(HeRecord* h) { return *reinterpret_cast<i32*>(HeBytes(h) + 184); }
inline i32& HeMatchRel188(HeRecord* h) { return *reinterpret_cast<i32*>(HeBytes(h) + 188); }
inline i32& HeMatchRel192(HeRecord* h) { return *reinterpret_cast<i32*>(HeBytes(h) + 192); }
i32  HeReadDate197(HeRecord* h);   // unaligned dword @ +197 (date)

// ---------------------------------------------------------------------------
// gilde.exe 0x58c454 — VIBE_Person_CountAdultChildren (__usercall, eax=person@a1,
// edx=outCount@a2). Walks the reference person's 5-entry child-id array (relation
// ids at person+104 .. person+120, i.e. (person+12)[+92] stepping +4 five times),
// resolving each via PersonFindRecordById. For every resolved child that is a live
// actor (child+8 != 0): increments the child count, and if the child's age word
// (child+10) is > 11 sets the "has an adult child" flag. Writes the child count to
// *outCount (when non-null) and RETURNS the has-adult-child flag (0/1).
//   child id array: person+104, +108, +112, +116, +120  (5 slots)
//   live-actor gate: *(BYTE*)(child+8) != 0
//   adult gate:      *(WORD*)(child+10) > 0xB  (> 11)
int PersonCountAdultChildren(const Person* person, i32* outCount);

// ---------------------------------------------------------------------------
// gilde.exe 0x5556c4 — VIBE_Office_CollectRelativeCandidates
//   (__usercall: eax=refRec@a1, edx=capacity@a2, ecx=filter@a3, ebx=out@a4).
// Scans the He pool (filter 44 = "relatives" handler) for the relative group tied
// to `refRec`. Each filter-44 handler carries up to 6 relative ids at +172/+176/
// +180/+184/+188/+192; the handler is "owned" by the group when `refRec` is one of
// those 6 (record-pointer identity). With capacity 0 it returns the total number
// of resolvable relatives across all owning handlers (the count pass increments per
// resolvable id only when the group includes refRec).
//
// With capacity != 0 it selects the SINGLE most-recent owning handler (smallest
// `handlerDate - gameDateLow`, see PersonRelCtx::gameDateLow), then fills up to
// min(capacity, 6) entries from that handler's 6 relative records — each entry
// gets a relation-class index (0..4, the v28[] table: 0,1,2,3,3,4), the handler id
// at entry +13, status flags (PersonResolveStatusFlags), and eligibility at +12.
// Returns the number of entries written.
int OfficeCollectRelativeCandidates(Person* refRec, u32 capacity, int filter,
                                    PersonRelEntry* out);

// ---------------------------------------------------------------------------
// gilde.exe 0x5559d8 — VIBE_Office_CollectSpouseAndBusinessCandidates
//   (__usercall: eax=refRec@a1, edx=capacity@a2, ebx=out@a4, ecx=filter@a3).
// Collects the reference person's SPOUSE (if married — bit 0x4 of the +457 status
// byte) plus their BUSINESS PARTNERS:
//   * Spouse: if (refRec+457 & 4), scan filter-71 handlers for one whose +172 or
//     +176 key matches refRec id; the OTHER key resolves to the spouse record. The
//     spouse becomes entry 0 (entry +11 cleared) and seeds the count.
//   * Business partners: scan filter-24 handlers; each handler's +196 key resolves
//     to a partner record. With capacity 0 it just counts (spouse + partners);
//     otherwise it appends partners after the spouse (entry +11 set to 1), up to
//     `capacity`, then resolves status flags + eligibility for every filled entry.
// Returns the number of entries (count pass) or the number written (fill pass).
int OfficeCollectSpouseAndBusinessCandidates(Person* refRec, u32 capacity,
                                             int filter, PersonRelEntry* out);

// ---------------------------------------------------------------------------
// VIBE_NpcAction_BeginMarriage cross-cluster leaves (UI / command / text / He).
// Each null function pointer installs an inert default (see .cpp). The shipping
// game wires these to VIBE_Amt_RunOfficeOverviewWindow / VIBE_Command_* / the text
// formatter / VIBE_He_SendEntityMessage.
// ---------------------------------------------------------------------------
struct MarriageCtx {
    // VIBE_Amt_RunOfficeOverviewWindow(@0x5575c8) — runs the partner-picker overview
    // window and returns the chosen Person record (or null if cancelled). `prompt`
    // is the localized prompt text the caller built; `excludeMarker` is the marker
    // word of the already-chosen partner (-1 for the first pick). Default null.
    Person* (*runPartnerPicker)(const char* prompt, i32 excludeMarker) = nullptr;

    // VIBE_Text_RenderFormattedMessage(@0x59f99c) — formats message `textId` (with a
    // single u16 arg) into `buf`. Default: writes an empty string. `arg` is the
    // partner marker word the wedding-announcement templates interpolate.
    void (*renderText)(char* buf, int textId, u16 arg) = nullptr;

    // VIBE_Command_QueueRequestCoord27(@0x494878) — queues the reciprocal
    // "begin courtship/marriage" relation command (idA, idB, coord=-40). Called
    // twice (A->B and B->A). Default: no-op.
    void (*queueCourtship)(i32 idA, i32 idB, int coord) = nullptr;

    // VIBE_He_SendEntityMessage(@0x4c5c54) — sends the wedding-announcement message
    // to entity `id` (text `buf`, kind 1418). Default: no-op.
    void (*sendEntityMessage)(i32 id, const char* buf, int textId) = nullptr;
};

void SetMarriageCtx(const MarriageCtx* ctx);
const MarriageCtx& GetMarriageCtx();

// gilde.exe 0x5687b0 — VIBE_NpcAction_BeginMarriage (__usercall, eax=ctxKind@a1,
// edx=partnerPair@a2). `ctxKindByte` is the byte the original reads at a1+2 (the
// He/NpcAction context kind). `partnerAId`/`partnerBId` are the two partner ids the
// original reads at a2+4 / a2+8 when ctxKind != 6 (the "already chosen" path).
//
// When ctxKind != 6 both partners are resolved directly from the two ids. When
// ctxKind == 6 the two partners are chosen interactively via runPartnerPicker
// (second pick excludes the first). If either partner is unresolved the function
// returns 0 (no marriage begun). Otherwise it:
//   * queues the two reciprocal courtship commands (A->B, B->A, coord -40),
//   * if partner A is a player-class person (kind 6 or 7) sends A the wedding
//     announcement naming B, and likewise for partner B (naming A),
// and returns 1.
int NpcActionBeginMarriage(u8 ctxKindByte, i32 partnerAId, i32 partnerBId);

} // namespace guild::sim
