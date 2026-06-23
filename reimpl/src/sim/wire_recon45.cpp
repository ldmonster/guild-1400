// See wire_recon45.h. Binds the recon4/5 cross-cluster bridges to their real
// reconstructed leaves. Glue only — no module logic.
//
// All person-record / selection-table reads route to the SAME real g_persons[] /
// g_personIds[] arrays entity.h owns (the originals' word_12CE910 / byte_12CE912 /
// dword_12CE914 / byte_12CE918 columns are exactly the Person record fields at the
// matching byte offsets; see sim/types.h PersonField). All command emits stage onto
// the SAME shared real CommandQueue (RealCommandQueue()).
#include "sim/wire_recon45.h"

#include "sim/command_recon4_resolve.h"        // Recon4ResolveHooks / SetRecon4ResolveHooks
#include "sim/command_recon4_senders.h"        // Recon4SenderHooks / SetRecon4SenderHooks
#include "sim/gamelogic_recon5_resolve_stat.h" // Recon5StatHooks / SetRecon5StatHooks
#include "sim/gamelogic_recon5_turns.h"        // Recon5TurnHooks / SetRecon5TurnHooks
#include "sim/gamelogic_recon5_certificate.h"  // Recon5CertHooks / SetRecon5CertHooks

#include "sim/entity.h"        // PersonFindRecordById / g_persons / g_personIds
#include "sim/types.h"         // Person / kPersonCapacity / PersonField offsets
#include "sim/real_hooks.h"    // RealCommandQueue()
#include "sim/command.h"       // CommandQueue
#include "sim/command_codec.h" // DeltaWriter / QueueRequestState22
#include "util/math_random.h"  // util::RandomModulo

#include <cstring>

namespace guild::sim {

namespace {

CommandQueue& Q() { return *RealCommandQueue(); }

// --- raw byte readers over a Person record (the originals read by byte offset) ---
inline u8  recU8(const void* rec, int off) {
    u8 v; std::memcpy(&v, static_cast<const u8*>(rec) + off, 1); return v;
}
inline i16 recI16(const void* rec, int off) {
    i16 v; std::memcpy(&v, static_cast<const u8*>(rec) + off, 2); return v;
}
inline i32 recI32(const void* rec, int off) {
    i32 v; std::memcpy(&v, static_cast<const u8*>(rec) + off, 4); return v;
}

inline bool slotInRange(int slot) { return slot >= 0 && slot < kPersonCapacity; }

// =========================================================================
// Recon4ResolveHooks — person-selection table + resolve leaves.
// =========================================================================

// word_12CE910 et al. — read selection slot `i` (0..767) from the real g_persons[]
// array, surfacing exactly the columns the resolvers touch. The byte offsets are
// the literal record offsets the original reads (see Recon4Record header doc).
bool WrReadRecord(int i, Recon4Record* out) {
    if (!out) return false;
    if (!slotInRange(i)) { *out = Recon4Record{}; return false; }
    const Person& p = g_persons[i];
    Recon4Record r;
    r.typeWord   = p.marker;                                  // word_12CE910 (+0)
    r.kind       = recU8(&p, 0x02);                           // byte_12CE912 (+2)
    r.entityId   = g_personIds[i];                            // dword_12CE914 column
    r.alive      = recU8(&p, 0x08);                           // byte_12CE918 (+8)
    r.flag9      = recU8(&p, 0x09);                           // (+9) gender/role
    r.prof74     = recU8(&p, 0x164);                          // byte_12CEA74 (+0x164)
    r.prof76     = recU8(&p, 0x166);                          // byte_12CEA76 (+0x166)
    r.prof79     = recU8(&p, 0x169);                          // byte_12CEA79 (+0x169)
    r.personBase = const_cast<Person*>(&p);
    *out = r;
    return true;
}

// VIBE_Person_FindRecordById(id) -> opaque record base (or null).
void* WrFindRecordById(i32 id) {
    return PersonFindRecordById(id);
}

// Read a single byte flag from a FindRecordById record at byte offset `off`.
u8 WrRecordFlag(void* rec, int off) {
    if (!rec) return 0;
    return recU8(rec, off);
}

// *(rec+4) — the entity id stored in a FindRecordById record (used as render arg).
i32 WrRecordTypeWord(void* rec) {
    if (!rec) return 0;
    return recI32(rec, 0x04);
}

// VIBE_Math_RandomModulo(n) -> value in [0,n).
int WrRandomModulo(u16 n) {
    return util::RandomModulo(n);
}

// =========================================================================
// Recon4SenderHooks — resolved-record field reads + selection-table columns.
// =========================================================================

// VIBE_Person_FindRecordById for the office/notify arms.
void* WrSenderFindRecordById(i32 id) {
    return PersonFindRecordById(id);
}

// typeWord (+39 / +0x27) — the QueryBegin slot-4 owner word the senders key on.
i16 WrRecTypeWord(void* rec) {
    if (!rec) return -1;
    return recI16(rec, 0x27);
}
// entityId — *(rec+1) dword == record +4 (the person/entity id).
i32 WrRecEntityId(void* rec) {
    if (!rec) return 0;
    return recI32(rec, 0x04);
}
// statusKind — *(rec+2) byte == record +2 (the kind/class byte).
u8 WrRecStatusKind(void* rec) {
    if (!rec) return 0;
    return recU8(rec, 0x02);
}

// selection-table columns keyed by typeWord (== slot index). The original reads
// byte_12CE912[536*w] / dword_12CE914[134*w]; with w the slot, that is the real
// g_persons[w].kind / g_personIds[w].
u8 WrTblKind(u16 typeWord) {
    int slot = static_cast<int>(typeWord);
    if (!slotInRange(slot)) return 0;
    return recU8(&g_persons[slot], 0x02);
}
i32 WrTblId(u16 typeWord) {
    int slot = static_cast<int>(typeWord);
    if (!slotInRange(slot)) return 0;
    return g_personIds[slot];
}

// The "to" command record base id read at *(cmd+4).
i32 WrCmdToId(void* cmd) {
    if (!cmd) return 0;
    return recI32(cmd, 0x04);
}

// VIBE_Command_EnqueuePacket @0x49388c — stage a 153-byte buffer onto the SAME
// shared real CommandQueue (QueueRequestTransform64's terminal call).
i32 WrEnqueuePacket(const u8* staging153) {
    CommandPacket p;
    std::memcpy(p.bytes, staging153, kPacketStride);
    return Q().EnqueuePacket(p);
}
// dword_12CE914 column by slot index (EnqueueLawAction's type-1 scan).
i32 WrPersonSlotId(i32 slot) {
    if (!slotInRange(slot)) return 0;
    return g_personIds[slot];
}
// byte_12CEA74 column — building-type code i8 at record offset 0x164.
i8 WrPersonSlotBuildingCode(i32 slot) {
    if (!slotInRange(slot)) return 0;
    return static_cast<i8>(recU8(&g_persons[slot], 0x164));
}
// word_12CE910 column by row index (EnqueueDuelChallenge's group scan).
u16 WrTblRowTypeWord(u16 idx) {
    if (!slotInRange(idx)) return 0xFFFF;
    return static_cast<u16>(g_persons[idx].marker);
}

// =========================================================================
// Recon5StatHooks — SelectedStat person-table path.
// =========================================================================

// VIBE_Person_FindRecordById(id) -> opaque record base (or null).
void* WrStatFindRecordById(i32 id) {
    return PersonFindRecordById(id);
}
// The slot index stored in *RecordById: since FindRecordById returns &g_persons[slot],
// the slot is the record's position in the real array (matches the original, which
// uses the first word of the record as the word_12CE910 selector). Returns 0xFFFF
// (empty) for any record not in our array.
u16 WrRecordSlot(void* rec) {
    if (!rec) return 0xFFFF;
    const Person* p = static_cast<const Person*>(rec);
    std::ptrdiff_t slot = p - g_persons;
    if (slot < 0 || slot >= kPersonCapacity) return 0xFFFF;
    return static_cast<u16>(slot);
}
// word_12CE910[268*slot] — the record's type word (-1 == empty).
i16 WrTableTypeWord(u16 slot) {
    if (!slotInRange(slot)) return -1;
    return g_persons[slot].marker;
}
// byte_12CE918[536*slot] — alive flag.
u8 WrTableAlive(u16 slot) {
    if (!slotInRange(slot)) return 0;
    return recU8(&g_persons[slot], 0x08);
}
// byte_12CE912[536*slot] — kind byte.
u8 WrTableKind(u16 slot) {
    if (!slotInRange(slot)) return 0;
    return recU8(&g_persons[slot], 0x02);
}

// =========================================================================
// Recon5TurnHooks — per-tick player window.
// =========================================================================

// word_12CE910[268*slot] != -1 (slot occupied / valid person record).
bool WrSlotOccupied(int slot) {
    if (!slotInRange(slot)) return false;
    return g_persons[slot].marker != -1;
}
// record balance field *(record+36).
i32 WrRecordBalance(int slot) {
    if (!slotInRange(slot)) return 0;
    return recI32(&g_persons[slot], 36);
}
// Emit the balance-delta command: BeginDeltaPacket / AppendRawField(delta,36) /
// QueueRequestState22, exactly as the original (the delta is precomputed by the
// turn loop). The entity base is the live person record; the id its +4 field.
void WrEmitBalanceDelta(int slot, i32 delta) {
    if (!slotInRange(slot)) return;
    const Person& p = g_persons[slot];
    DeltaWriter dw;
    dw.BeginDeltaPacket(&p, static_cast<u32>(g_personIds[slot]));
    dw.AppendRawField(/*width*/4, /*count*/1, /*fieldOffset*/36, &delta);
    QueueRequestState22(Q(), dw);
}

// --- process-lifetime wired hook tables (the global hook ptrs reference these) ---
Recon4ResolveHooks g_resolve{};
Recon4SenderHooks  g_sender{};
Recon5StatHooks    g_stat{};
Recon5TurnHooks    g_turn{};
Recon5CertHooks    g_cert{};

} // namespace

void InstallRealRecon45Wiring() {
    Q(); // force the shared real command queue to exist (composes with real_hooks)

    // --- Recon4ResolveHooks (command_recon4_resolve.h) -----------------------
    // SEED FROM DEFAULTS so unbound fields keep their safe non-null stubs.
    g_resolve = GetRecon4ResolveHooks();
    g_resolve.readRecord     = &WrReadRecord;       // word_12CE910 family -> g_persons[]
    g_resolve.findRecordById = &WrFindRecordById;   // VIBE_Person_FindRecordById
    g_resolve.recordFlag     = &WrRecordFlag;       // record byte-flag reader
    g_resolve.recordTypeWord = &WrRecordTypeWord;   // record +4 id
    g_resolve.randomModulo   = &WrRandomModulo;     // VIBE_Math_RandomModulo
    // computeTotalWealth: needs live currency + owned-building worth not carried by
    //   the (slotIndex) hook -> inert (rule 8, no analogue).
    // isNotInSelectionList: VIBE_Entity_IsNotInSelectionList over the live selection
    //   list (dword_12CE880 alive array) — not modeled as a standalone callable -> inert.
    // parseTokens (StripNameTokens + ParseInt over the name tail) / renderMessage
    //   (VIBE_Text_RenderFormattedMessage): text-format + console-render leaves -> inert.
    SetRecon4ResolveHooks(&g_resolve);

    // --- Recon4SenderHooks (command_recon4_senders.h) ------------------------
    g_sender = GetRecon4SenderHooks();
    g_sender.personFindRecordById = &WrSenderFindRecordById; // VIBE_Person_FindRecordById
    g_sender.recTypeWord          = &WrRecTypeWord;          // record +0x27
    g_sender.recEntityId          = &WrRecEntityId;          // record +4
    g_sender.recStatusKind        = &WrRecStatusKind;        // record +2
    g_sender.tblKind              = &WrTblKind;               // byte_12CE912[slot]
    g_sender.tblId                = &WrTblId;                 // dword_12CE914[slot]
    g_sender.cmdToId              = &WrCmdToId;               // *(cmd+4)
    // wave-2 sender leaves (QueueRequestTransform64 / EnqueueLawAction /
    // EnqueueDuelChallenge):
    g_sender.enqueuePacket          = &WrEnqueuePacket;          // CommandQueue::EnqueuePacket (0x49388c)
    g_sender.personSlotId           = &WrPersonSlotId;           // dword_12CE914 column
    g_sender.personSlotBuildingCode = &WrPersonSlotBuildingCode; // byte_12CEA74 column (+0x164)
    g_sender.tblRowTypeWord         = &WrTblRowTypeWord;         // word_12CE910 column
    // personQueryBegin (op/value pairing unknowable, rule 8), mapViewPanel /
    // officeOverview (UI dispatchers, rules 3-5 seam), emit (recording sink only):
    //   left as inert defaults. Wave-2 inert remainders (rule 8, addr + reason):
    // findSlotByProt (0x5851fc): the live 0x13C3B50 building-slot table (7952/
    //   building, 62 x 0x80 slots) is not reconstructed as owned state yet — the
    //   apply-side twin sits behind SetSlotFindHook4 (command_apply4.h) for the
    //   same reason.
    // seasonByte6477A1 / localPlayerIndex (word_63CC5C) / localPlayerTypeWord
    //   (*(u16*)(*0x6498E4)) / gameTimeNow (qword_13CE852): save-block live
    //   globals (io/save.h SaveBlock) not modeled as standalone process state.
    // personIterNext (0x586a6c) / resolveOwnerOrParentB (0x591730): live person-
    //   query cursor / object-owner resolution leaves, unreconstructed.
    // tblRowOwnerRec (dword_12CEA7C col, +0x16C): a 32-bit record POINTER column
    //   in the original; the portable Person record carries no pointer there.
    // combatSetUnitFormationMode (0x48980c): NAMED GAP — needs its own disasm
    //   pass (see command_recon4_senders.h).
    // queueRequest39 (0x494c30): the StagePendingBlock(276)+cmd-39 wrapper is
    //   not yet reconstructed in command_codec.
    SetRecon4SenderHooks(&g_sender);

    // --- Recon5StatHooks (gamelogic_recon5_resolve_stat.h) -------------------
    g_stat = GetRecon5StatHooks();
    g_stat.findRecordById = &WrStatFindRecordById; // VIBE_Person_FindRecordById
    g_stat.recordSlot     = &WrRecordSlot;         // record -> g_persons[] slot index
    g_stat.tableTypeWord  = &WrTableTypeWord;      // word_12CE910[slot]
    g_stat.tableAlive     = &WrTableAlive;         // byte_12CE918[slot]
    g_stat.tableKind      = &WrTableKind;          // byte_12CE912[slot]
    // computeTotalWealth: same live-state limitation as the resolve bridge -> inert.
    // queryByGoodType / personAnchorId / queryFindBuilding / buildingChild* : the
    //   live object-graph QueryFind/iterator leaves -> inert.
    // parseStatPercent / convertToDisplayCoord / renderMessage: parse + money-display
    //   + console-render leaves -> inert.
    SetRecon5StatHooks(&g_stat);

    // --- Recon5TurnHooks (gamelogic_recon5_turns.h) --------------------------
    g_turn = GetRecon5TurnHooks();
    g_turn.slotOccupied     = &WrSlotOccupied;      // word_12CE910[slot] != -1
    g_turn.recordBalance    = &WrRecordBalance;     // *(record+36)
    g_turn.emitBalanceDelta = &WrEmitBalanceDelta;  // BeginDelta/AppendRaw(36)/State22
    // diffMinutes: the real VIBE_GameTime_DiffMinutes (gametime.h) operates on the
    //   PARSED GameTime record; this hook's TurnClock is the raw 14-byte qword_13CE852
    //   clock image the turn loop diffs directly. The recon module deliberately kept
    //   them distinct -> binding would require reinterpreting one as the other (rule 8).
    //   Inert (0 minutes -> 0 players-this-tick: the gate path stays faithful).
    // totalPlayerCount (dword_63C744 — ambiguous live global) / guardSuppressed
    //   (word_63C740) / ownedGate (composite of dword_764CE0/byte_63CC1D/dword_764CF4
    //   + balance): process-global gates not modeled as standalone callables -> inert.
    // refillTavernStock (@0x45f0a0) / updateGuardBehavior (@0x452d38): deferred AI/
    //   scene cluster (rule 8) -> inert.
    SetRecon5TurnHooks(&g_turn);

    // --- Recon5CertHooks (gamelogic_recon5_certificate.h) --------------------
    // FULLY INERT (seed-from-defaults, override nothing): the entire leaf surface is
    // the Form/Text/Object UI subsystem (selectWindow / renderRichString /
    // getChildObjectId / setValueOrText) plus personCategoryByte
    // (*(589*(*personPtr)+dword_13CE294) — a live type-def-base computation not
    // exposed as a standalone callable). Rules 3-5 / rule 8: no clean target. Seeded
    // so the certificate populator runs its faithful control flow over the stubs.
    g_cert = GetRecon5CertHooks();
    SetRecon5CertHooks(&g_cert);
}

} // namespace guild::sim
