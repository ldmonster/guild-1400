// See wire_npcaction1.h. Binds the NpcAction5/6/7 bridges to their real
// reconstructed cross-cluster leaves. Glue only — no module logic.
//
// Record-layout note: NpcAction6/7 treat a Person record as a raw byte buffer
// (record[+0] index word, [+4] entity id, [+44]/[+128+kind] stat bytes, etc.) and
// the He/handler record likewise. The reinterpret_casts here between Person* /
// HandlerRecord* / HeRecord* and u8*/HeRecord* are byte-faithful: all three are POD
// blobs over the same record-base byte layout, exactly as wire_charaction.cpp /
// real_hooks3 already cast between them. g_persons[i] == word_12CE910[268*i]
// (stride 536), so &g_persons[index] is the original's &word_12CE910[268*index].
#include "sim/wire_npcaction1.h"

#include "sim/npcaction5.h"        // NpcAction5Hooks / SetNpcAction5Hooks
#include "sim/npcaction6.h"        // NpcAction6Hooks / SetNpcAction6Hooks
#include "sim/npcaction7.h"        // NpcAction7Hooks / SetNpcAction7Hooks
#include "sim/npcaction.h"         // NpcAdjustRelationByMood
#include "sim/entity.h"            // PersonFindRecordById / GameObjectResolveEntityById / g_persons
#include "sim/he.h"                // HeRecord / HeBytes
#include "sim/handler_entry.h"     // HandlerTable / HandlerRecord
#include "sim/building_type.h"     // Building_MapKindToCategory
#include "sim/command_builders.h"  // RequestBuildOp93 / QueueRequestEntity29
#include "sim/command_builders3.h" // RequestBuildOp67
#include "sim/command_codec.h"     // QueueRequest16 / QueueRequest17
#include "sim/real_hooks.h"        // RealCommandQueue()
#include "sim/real_hooks3.h"       // RealHandlerTable()

#include "ai/needs.h"              // PickRandomNeedAndClearGroup{,B} / PickRandomFlagFromFour{A,B} / NeedAgent
#include "util/util_misc.h"        // util::InitAndShuffleDwordArray
#include "gui/text/format.h"       // gui::text::SeasonFromYear
#include "app/session_init.h"      // app::MoneyMultiplyByRate

#include <cstdint>
#include <cstring>

namespace guild::sim {

namespace {

CommandQueue&  Q()   { return *RealCommandQueue(); }
HandlerTable&  HeT() { return *RealHandlerTable(); }

// =========================================================================
// NpcAction5 — entity-resolve leaf.
// =========================================================================

// VIBE_GameObject_ResolveEntityById(out, 0, id, 0) then read *(out + 97) (0x4d877c).
// The original resolves the OBJECT out-slot only (outScene=0, outPerson=0) and
// requires the resolved record's +97 dword to be nonzero. ObjectRec is 169 bytes,
// so +97 is in-range; 0 == unresolved or the +97 slot is 0.
i32 Na5ResolveEntityField97(i32 id) {
    ObjectRec* obj = nullptr;
    GameObjectResolveEntityById(&obj, /*outScene=*/nullptr, id, /*outPerson=*/nullptr);
    if (!obj)
        return 0;
    i32 v = 0;
    std::memcpy(&v, reinterpret_cast<const std::uint8_t*>(obj) + 97, sizeof(i32));
    return v;
}

// =========================================================================
// Shared id->record resolve + raw Person-record accessors (byte-faithful).
// =========================================================================

// VIBE_Person_FindRecordById(id) -> Person* viewed as a raw byte record.
u8* FindPersonBytes(i32 id) {
    return reinterpret_cast<u8*>(PersonFindRecordById(id));
}

// Build a needs.h NeedAgent view over a raw Person record. The needs pickers read
// *(word)record[+0] (group/type code), *(dword)record[+4] (entity id) and
// *(dword)record[+44] (the packed need word) — exactly the fields documented in
// needs.h's NeedAgent. (entity.cpp owns the array; this is a pure field view.)
ai::NeedAgent AgentFor(u8* rec) {
    ai::NeedAgent a;
    std::memcpy(&a.type,     rec + 0,  sizeof(i16));
    std::memcpy(&a.id,       rec + 4,  sizeof(i32));
    std::memcpy(&a.needWord, rec + 44, sizeof(u32));
    return a;
}

// =========================================================================
// NpcAction6 need/flag gates -> the real ai::needs pickers.
// ---------------------------------------------------------------------------
// The pickers run the seeded forward-scan selection (RNG-determinism-critical) and
// return the chosen need-id (0 == none); NpcAction6 treats nonzero as "gate passed".
// A NULL NeedsCommandHook == "selection only" (the deterministic logic + RNG draws
// still run; the internal command/stock emit side effect is deferred — see report).
int Na6PickNeedGroup(u8* rec)  { auto a = AgentFor(rec); return ai::PickRandomNeedAndClearGroup (a, nullptr) != 0; }
int Na6PickNeedGroupB(u8* rec) { auto a = AgentFor(rec); return ai::PickRandomNeedAndClearGroupB(a, nullptr) != 0; }
int Na6PickFlagFourA(u8* rec)  { auto a = AgentFor(rec); return ai::PickRandomFlagFromFourA     (a, nullptr) != 0; }
int Na6PickFlagFourB(u8* rec)  { auto a = AgentFor(rec); return ai::PickRandomFlagFromFourB     (a, nullptr) != 0; }

// VIBE_Money_MultiplyByRate(amount, ratePct) @0x58f19c.
i32 Na6MoneyMultiplyByRate(int amount, u8 currency) {
    return app::MoneyMultiplyByRate(amount, currency);
}

// VIBE_Command_RequestBuildOp93(id, kind, id2, amount) @0x495bd0 (relation/loyalty).
void Na6RequestBuildOp93(i32 id, int kind, i32 id2, u8 amount) {
    RequestBuildOp93(Q(), id, kind, id2, static_cast<i32>(amount));
}
// VIBE_Command_QueueRequest16(idA, idB, amount, currency) @0x494630 (salary/move).
void Na6QueueRequest16(i32 idA, i32 idB, i32 amount, u8 currency) {
    QueueRequest16(Q(), idA, idB, amount, currency);
}
// VIBE_Command_QueueRequestEntity29(arg, record) @0x4949c4 (re-arm an He request).
i32 Na6QueueRequestEntity29(int arg, HeRecord* h) {
    return QueueRequestEntity29(Q(), static_cast<i8>(arg), h);
}

// VIBE_GameTime_GetSeasonFromYear(clock) @0x583384 — (yearField >> 16) % 4. The
// ShowPositionCmd reads the season of the live game clock; the packed-year dword is
// the clock's day field (the original feeds the calendar dword the season helper
// folds). NpcClock() owns that calendar; pass its packed day word.
int Na6CurrentSeason() {
    const GameTime& clk = NpcClock();
    i32 packed = 0;
    std::memcpy(&packed, &clk.day, sizeof(i32));
    return gui::text::SeasonFromYear(packed);
}

// VIBE_He_FindFirstHandlerByFilter(1, 0, 53) / _FindNextMatchingHandler — the
// NotifyWanderPair scan: selector 0 == kind@+0, value 53 (one filter pair).
HeRecord* Na6WanderScanBegin() {
    return reinterpret_cast<HeRecord*>(HeT().FindFirstHandlerByFilter(1, 0, 53));
}
HeRecord* Na6WanderScanNext() {
    return reinterpret_cast<HeRecord*>(HeT().FindNextMatchingHandler());
}

// &word_12CE910[268 * index] == &g_persons[index] (stride 536). The history leaf
// these feed (VIBE_History_NotifyWanderPairEvent) is unreconstructed and stays
// inert, but the slot resolve itself is real.
void* Na6PersonSlotByIndex(u16 index) {
    if (index >= static_cast<u16>(kPersonCapacity))
        return nullptr;
    return reinterpret_cast<void*>(&g_persons[index]);
}

// =========================================================================
// NpcAction7 — staff-management leaves.
// =========================================================================

// VIBE_Building_MapTypeToCategory(typeByte) @0x5878b0 — table-driven kind->category.
int Na7MapTypeToCategory(int typeByte) {
    return Building_MapKindToCategory(static_cast<u8>(typeByte));
}

// Person array substrate (word_12CE910, 768 × 536). The Heal/AssignWork scans walk
// this directly with the disasm stride/bound (kNpc7PersonStride/Capacity).
u8* Na7PersonTableBase()     { return reinterpret_cast<u8*>(&g_persons[0]); }
int Na7PersonTableCapacity() { return kPersonCapacity; }

// VIBE_Npc_AdjustRelationByMood(personRecord, kind) @0x56840c. The reconstruction
// takes the person record as an HeRecord* (raw byte base) + an i8 kind — the same
// byte layout NpcAction7 hands us; cast is byte-faithful.
void Na7AdjustRelationByMood(u8* personRecord, int kind) {
    NpcAdjustRelationByMood(reinterpret_cast<HeRecord*>(personRecord),
                            static_cast<i8>(kind));
}

// VIBE_Util_InitAndShuffleDwordArray(n, dst) @0x58ba98 — Fisher-Yates 0..n-1.
void Na7ShuffleDwords(int n, i32* dst) {
    util::InitAndShuffleDwordArray(static_cast<u8>(n), reinterpret_cast<u32*>(dst));
}

// VIBE_Command_RequestBuildOp67(id) @0x495434 — gossip "spread" command.
void Na7RequestBuildOp67(i32 id) { RequestBuildOp67(Q(), id); }
// VIBE_Command_RequestBuildOp93(id, kind, id2, amount) @0x495bd0 — heal/relation.
void Na7RequestBuildOp93(i32 id, int kind, i32 id2, u8 amount) {
    RequestBuildOp93(Q(), id, kind, id2, static_cast<i32>(amount));
}
// VIBE_Command_QueueRequest17(idA, idB, count, kind, currency, flag) @0x49465c.
void Na7QueueRequest17(i32 idA, i32 idB, int count, int kind, u8 currency, int flag) {
    QueueRequest17(Q(), idA, idB, count, static_cast<i16>(kind), currency, flag);
}

// --- process-lifetime wired hook tables (the global hook ptr references these) ---
NpcAction5Hooks g_na5{};
NpcAction6Hooks g_na6{};
NpcAction7Hooks g_na7{};

} // namespace

void InstallRealNpcAction1Wiring() {
    HeT();   // force the shared real He pool to exist (composes with real_hooks3)
    Q();     // force the shared real command queue to exist

    // --- NpcAction5Hooks (npcaction5.h) --------------------------------------
    // SEED from the module's inert default (non-null stub) then override the one
    // wireable leaf. NpcAction5's free/queue29/inventory leaves arrive via the
    // sibling NpcLeafHooks (wired by InstallRealSimHooks3), not this bridge.
    g_na5 = GetNpcAction5Hooks();
    g_na5.resolveEntityField97 = &Na5ResolveEntityField97;
    SetNpcAction5Hooks(&g_na5);

    // --- NpcAction6Hooks (npcaction6.h) --------------------------------------
    g_na6 = GetNpcAction6Hooks();
    g_na6.findRecordById         = &FindPersonBytes;
    g_na6.moneyMultiplyByRate    = &Na6MoneyMultiplyByRate;
    g_na6.pickNeedAndClearGroup  = &Na6PickNeedGroup;
    g_na6.pickNeedAndClearGroupB = &Na6PickNeedGroupB;
    g_na6.pickFlagFromFourA      = &Na6PickFlagFourA;
    g_na6.pickFlagFromFourB      = &Na6PickFlagFourB;
    g_na6.requestBuildOp93       = &Na6RequestBuildOp93;
    g_na6.queueRequest16         = &Na6QueueRequest16;
    g_na6.queueRequestEntity29   = &Na6QueueRequestEntity29;
    g_na6.currentSeason          = &Na6CurrentSeason;
    g_na6.wanderScanBegin        = &Na6WanderScanBegin;
    g_na6.wanderScanNext         = &Na6WanderScanNext;
    g_na6.personSlotByIndex      = &Na6PersonSlotByIndex;
    // computeTotalWealth: VIBE_Person_ComputeTotalWealth (0x591f7c) — the
    //   reconstruction (PersonComputeTotalWealth) takes a ContainerView + owned-
    //   building worth vector, NOT the (u16 index, u8* record) the hook hands it;
    //   not signature-compatible -> INERT (returns 0).
    // sendEntityMessage: VIBE_He_SendEntityMessage (0x4c5c54) — formatted-text /
    //   message-render leaf (rule 4/5 boundary), no clean reconstructed target ->
    //   INERT (matches the wire_charaction sendEntityMessage decision).
    // notifyWanderPairEvent: VIBE_History_NotifyWanderPairEvent (0x535970) — not
    //   reconstructed -> INERT (the .cpp null-checks it; the scan/slot resolve and
    //   the clock-stamp + cmd29 re-arm still run).
    // currencyByte: byte_6477A1 (active player's coin) — a process-global not
    //   modeled as a callable leaf; left at the seeded default (0).
    SetNpcAction6Hooks(&g_na6);

    // --- NpcAction7Hooks (npcaction7.h) --------------------------------------
    g_na7 = GetNpcAction7Hooks();
    g_na7.findRecordById       = &FindPersonBytes;
    g_na7.mapTypeToCategory    = &Na7MapTypeToCategory;
    g_na7.personTableBase      = &Na7PersonTableBase;
    g_na7.personTableCapacity  = &Na7PersonTableCapacity;
    g_na7.adjustRelationByMood = &Na7AdjustRelationByMood;
    g_na7.shuffleDwords        = &Na7ShuffleDwords;
    g_na7.requestBuildOp67     = &Na7RequestBuildOp67;
    g_na7.requestBuildOp93     = &Na7RequestBuildOp93;
    g_na7.queueRequest17       = &Na7QueueRequest17;
    // queryBegin / iterNext: VIBE_Person_QueryBegin/IterNext (0x586c20/0x586a6c) —
    //   the hook passes the original's POSITIONAL filter args (player,1,3,index);
    //   the reconstructed PersonQueryBegin takes a structured (op,value) PersonFilter
    //   whose ops (id/faction/owner) don't map cleanly onto those positional args.
    //   Pairing unknowable -> INERT (rule 8; mirrors wire_charaction personQueryBegin).
    // gameObjectQueryFind3/5 / gameObjectIterNext: VIBE_GameObject_QueryFind/IterNext
    //   (0x5857fc/0x58529c) — same positional-vs-structured-filter mismatch (the
    //   reconstruction returns SceneNode* via a SceneFilter spec) -> INERT (rule 8).
    // sendEntityMessage / sendQuickjumpMessage: VIBE_He_SendEntityMessage /
    //   SendQuickjumpMessage — formatted-text / message-render leaves -> INERT.
    // currencyByte: byte_6477A1 — process-global, seeded default (0).
    SetNpcAction7Hooks(&g_na7);
}

} // namespace guild::sim
