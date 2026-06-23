#pragma once
// privilege_panels_b.h — guild-office PRIVILEGE action panels, SET B
// (law / evidence / espionage + dispatch), reconstructed 1:1 from gilde.exe.
//
// Wave-19 agent W19-PRIV-B. Companion to the SET-A panels (W19-PRIV-A) and to the
// already-reconstructed shared predicates in world/office_recon_privilege.h and
// world/privilege.h (reused, NOT redefined here).
//
// Each VIBE_Privilege_Panel* in the original is a GUI frame-loop dialog: it builds
// a Form (VIBE_GameTick_Finalize over a "privillegien\\..." backdrop bitmap),
// spins VIBE_GameLogic_RunFrameLoop, and on the confirm/cancel button enqueues a
// lockstep network command (VIBE_Command_*). Those coupled leaves (Form/HUD/
// Vulkan; the command queue; the live person arrays word_12CE910 / byte_12CEA76)
// are surfaced as the SAME inert-default hook style used by the sibling modules.
//
// What is faithfully reconstructed here (the load-bearing, testable rules):
//   * The dispatcher's backdrop-bitmap selection (ShowDialog, 0x571218).
//   * Each panel's SUBJECT-KIND gate (record+2 == 6/7 office path vs. concrete
//     person path) and EARLY-OUT return codes (96 / 32 / 16 / 128 / -112 / 0).
//   * EnactLaw: the law-record lookup + amount clamp (VIBE_Gesetz_GetRecord /
//     VIBE_Gesetz_RequestApply, 26x36-byte table unk_631E98 golden-pinned), the
//     law-class privilege mask (0x80000 / 0x100000), and the penalty-mode (0..3)
//     dispatch.
//   * Embezzlement: the embezzle amount = ConvertX((RandomModulo(0xB0)+25) *
//     0.005 * wage), truncated (ConvertX@0x5c6b08 truncates).
//   * Miracle: RandomModulo(6) draw feeding the office-member rebuild.
//   * SwapSeats: the office-pair swap table (15->14/13, 21->20/19, 27->26/25) and
//     the seat-holder scan (768 persons, 536-byte stride / 268 word stride).
//   * RemoveFromOffice: office-definition / holder-entry category match.
//   * EvidenceReview / EvidenceReviewAlt: the kind-13(judge) reject, the
//     FindMatchingEntityIndices==0 reject, and the BuildEvidenceEntry mode arg
//     (Review=0, ReviewAlt=1).
//
// Provenance addresses are gilde.exe (imagebase 0x400000).
//
// REUSE (ODR): the Gesetz law table (unk_631E98) + VIBE_Gesetz_GetRecord @0x4c244c
// are ALREADY reconstructed in world/law.{h,cpp} (g_lawTable / GesetzGetRecord /
// LawRecord). This module does NOT redefine them — it externs world/law.h and adds
// only the two clamp fields (int @+4 / @+8) the EnactLaw apply path reads but which
// the existing LawRecord keeps as padding.
#include <cstddef>

#include "guild/common/types.h"
#include "world/law_types.h"          // LawRecord, kLawCount, kLawStride (reused)
#include "world/privilege_panels_a.h" // PrivPerson, PrivEvent (REUSED — no ODR)

namespace guild::world {

// ===========================================================================
// Shared confirm/cancel button ids (the dword_75BF38 button id every panel's
// frame loop polls). Matches office_recon_privilege.h's kPrivBtn* — kept local
// with distinct names to avoid any ODR/include coupling.
// ===========================================================================
constexpr int kPrivBBtnConfirm = 1210; // dword_75BF38 == 1210 (OK)
constexpr int kPrivBBtnCancel  = 1155; // dword_75BF38 == 1155 (cancel)

// ===========================================================================
// Dispatcher — VIBE_Privilege_ShowDialog @0x571218.
// A generic Form runner used by several panels. It selects one of three backdrop
// bitmaps by `size` (arg edx) and runs the frame loop until close. The pure,
// testable part is the bitmap selection.
//   size == 0 -> "privillegien\\prv_small"     (aPrivillegienPr_0 @0x6253d0)
//   size == 1 -> "privillegien\\prv_big"       (aPrivillegienPr   @0x6253e8)
//   else      -> "privillegien\\prv_very_big"  (aPrivillegienPr_1 @0x625400)
// ===========================================================================
enum class PrivDialogSize : int { kSmall = 0, kBig = 1, kVeryBig = 2 };
// gilde.exe 0x571221..0x57128d — the backdrop path the dispatcher picks.
const char* PrivShowDialogBitmap(int size);

// ===========================================================================
// Law records — VIBE_Gesetz_GetRecord @0x4c244c / VIBE_Gesetz_RequestApply
// @0x4c247c. The 26x36-byte table (unk_631E98) + GesetzGetRecord(u8, LawRecord*)
// are reused from world/law.{h,cpp} (do NOT redefine). RequestApply additionally
// reads two amount-clamp fields the existing LawRecord keeps as padding:
//   int +4 : minAmount (v5[1]: clamp lower bound, also the op70 arg)
//   int +8 : maxAmount (v5[2]: clamp upper bound)
// These accessors read those two fields out of the canonical LawRecord bytes.
// ===========================================================================
constexpr std::size_t kGesetzMinOffset = 4;  // int @ +4 (v5[1])
constexpr std::size_t kGesetzMaxOffset = 8;  // int @ +8 (v5[2])

// Read the clamp-min (v5[1], record int @+4) from a canonical LawRecord.
i32 GesetzRecordMinAmount(const LawRecord& rec);
// Read the clamp-max (v5[2], record int @+8) from a canonical LawRecord.
i32 GesetzRecordMaxAmount(const LawRecord& rec);

// gilde.exe 0x4c24b5 — amount clamp the apply path performs before enqueueing
// op70: a3 < v5[1] -> v5[1] ; a3 > v5[2] -> v5[2] ; else a3.
i32 GesetzClampAmount(const LawRecord& rec, i32 amount);

// gilde.exe 0x4c2488..0x4c24e7 head — RequestApply returns -1 (no command) when
// the law index is out of range (>= 26), OR when a target record exists but its
// id word is 0xFFFF. Returns true iff the op70 command would be enqueued.
//   `hasTarget`==false models a1==0 (the "no concrete target" path, v3=-1).
bool GesetzApplyWouldEnqueue(int lawIndex, bool hasTarget, bool targetIdIsFFFF);

// ===========================================================================
// Common subject-kind dispatch helper. The set-B panels share a head that picks
// the acting subject from the event record by the leading kind byte (*a2):
//   *a2 == 3 -> subject is the primary actor (a1).
//   *a2 == 4 -> subject is the linked person (*(a2+0x21C) for EnactLaw's word
//               index 135, or *(a2+540) for the dword-pointer panels); if that
//               link is null the panel returns 96.
//   else     -> return 96 (no subject).
// `linkValid` models the non-null check on the +4 link. Returns:
//   0  -> use the primary actor (a1)         (kind 3)
//   1  -> use the linked subject             (kind 4, link valid)
//  96  -> early-out "no subject"             (kind 4 null link, or other kind)
// ===========================================================================
constexpr int kPrivSubjectPrimary = 0;
constexpr int kPrivSubjectLinked  = 1;
constexpr int kPrivSubjectNone    = 96;
int PrivResolveSubjectKind(u8 kindByte, bool linkValid);

// ===========================================================================
// EnactLaw — VIBE_Privilege_PanelEnactLaw @0x561bb4.
// ===========================================================================
// gilde.exe 0x561be1 — the law-class -> privilege-flag mask (v47). The event's
// dword+0x214 (index 133) selects the class: 1 -> 0x80000, 2 -> 0x100000, else
// the panel returns 96.
constexpr u32 kEnactLawMaskClass1 = 0x80000;
constexpr u32 kEnactLawMaskClass2 = 0x100000;
// Returns the mask, or 0 to signal "return 96" (invalid class).
u32 EnactLawClassMask(int lawClass);

// gilde.exe 0x561bf8..0x561c11 — the non-office (actor+2 != 6) precheck path:
//   actor.rank (*(a1+404)) < 2          -> return 32 ("rank too low").
//   law record (HIBYTE(event+1)) absent -> return 96.
//   amount (event+8) == v46 || > v45 || < v44 (the record's bound triple) -> 96.
// `recordFound`, plus the three amount comparisons, decide the early code.
// Returns 0 to proceed, else the early-out code (32 or 96).
// (v44/v45/v46 are the record bytes; we expose them as min/max/forbidden.)
int EnactLawNonOfficePrecheck(int actorRank, bool recordFound, i32 amount,
                              i32 recMin, i32 recMax, i32 recForbidden);

// gilde.exe 0x561d3b — EnactLaw's penalty-mode (record byte+4 == v39[4]) switch.
// The four modes drive different text-id banks and widget setups; mode >= 4
// returns 0 ("invalid"). Returns true iff the mode is one of 0..3.
inline bool EnactLawModeValid(u8 mode) { return mode <= 3; /*0x561d3b*/ }

// gilde.exe 0x561cf0 — the per-mode text base used in the office dialog arm:
//   v11 = 5 * (record.firstHiByte) + 4145.
inline int EnactLawTextBase(u8 firstHiByte) {
    return 5 * static_cast<int>(firstHiByte) + 4145; /*0x561cf0*/
}

// gilde.exe — EnactLaw final return code (v52). 128 ("law enacted") is latched in
// the confirm arm only after the op70 command resolves; 0 otherwise (cancel /
// non-office no-op). The non-office direct path returns v52 (0 unless the op
// fired). Models: success && office-confirm-fired.
constexpr int kEnactLawEnacted = 128; // v52 = 128 (0x561f46)
inline int EnactLawResult(bool enacted) { return enacted ? kEnactLawEnacted : 0; }

// ===========================================================================
// Embezzlement — VIBE_Privilege_PanelEmbezzlement @0x562334.
// ===========================================================================
// gilde.exe 0x624c2c — flt_624C2C == 0.005f (the embezzlement rate constant).
constexpr float kEmbezzleRate = 0.005f;
constexpr u32   kEmbezzleRandRange = 0xB0; // RandomModulo(0xB0)
constexpr int   kEmbezzleRandBias  = 25;   // + 25

// gilde.exe 0x562373..0x5623ae — the embezzle amount:
//   r      = RandomModulo(0xB0) + 25         (25..199)
//   amount = ConvertX(r * 0.005f * wage)     (ConvertX truncates toward zero)
// `randMod` is the 0..0xAF draw (injected for determinism); `wage` is the office
// wage from VIBE_Amt_ComputeOfficeWages. Returns the truncated integer amount.
i32 EmbezzleAmount(int randMod, i32 wage);

// gilde.exe 0x5623cc — the non-office (a1+2 != 6) path returns 32 when the
// actor's rank (*(a1+404), word index 101) < 4; else it enqueues and returns v27
// (0 here, since no success message latches outside the dialog). Returns the
// early code (32) or 0 to proceed.
inline int EmbezzleNonOfficeRankGate(int actorRank) {
    return actorRank < 4 ? 32 : 0; /*0x5623cc*/
}

// ===========================================================================
// Miracle — VIBE_Privilege_PanelMiracle @0x5651bc.
// ===========================================================================
// gilde.exe 0x5651d9 — kind gate. Only kind 6 runs the dialog; kind 7 returns 0
// immediately (0x5651ce); non-office runs the direct path. Returns:
//   1  -> kind 6 (run dialog)
//   0  -> kind 7 (no-op)
//  -1  -> non-office (direct path)
int MiracleKindDispatch(u8 actorKindByte);

// gilde.exe 0x5651e2 — non-office direct path: rank (*(a1+404)) >= 4 draws
// RandomModulo(6), rebuilds the office-member table, and returns 16; else 32.
constexpr u32 kMiracleRandRange = 6; // RandomModulo(6)
inline int MiracleNonOfficeRankGate(int actorRank) {
    return actorRank >= 4 ? 16 : 32; /*0x5651e2*/
}

// ===========================================================================
// SwapSeats — VIBE_Privilege_PanelSwapSeats @0x562cdc.
// ===========================================================================
// gilde.exe 0x562d0b — the seat-swap pair selected by the subject's held-office
// byte (*(subject+358)). Each case yields two target seat ids (v25 the "lower",
// v26 the "upper"); any other office returns 32.
struct SwapSeatPair { u8 lower; u8 upper; bool valid; };
SwapSeatPair SwapSeatsPairFor(u8 officeByte);

// gilde.exe 0x562d21..0x562d80 — the seat-holder scan. It walks up to 768 person
// records (word_12CE910, 268-word stride; the office byte at byte_12CEA76,
// 536-byte stride) decrementing a "remaining to find" counter (start 2) each
// time a record's office byte equals one of the pair. Given the per-record office
// bytes, returns how many of the two seats were located (0..2).
int SwapSeatsCountHolders(const u8* officeBytes, int count,
                          const SwapSeatPair& pair);

// gilde.exe 0x562e49 / 0x562e66 — the post-scan verdict for the non-office actor
// path (kind != 6/7): returns 16 iff rank (*(a1+404)) >= 6 AND both seats found
// (remaining<=0) AND the promote await succeeded; else 32. For the office actor
// path: if a seat is still missing it returns 0 (with msg), else runs the dialog
// (returns 1). `bothFound` == (remaining<=0).
int SwapSeatsNonOfficeResult(int actorRank, bool bothFound, bool promoteOk);

// ===========================================================================
// RemoveFromOffice — VIBE_Privilege_RemoveFromOffice @0x561fd0.
// ===========================================================================
// gilde.exe 0x561fe4 — subject pick by *a2: kind 3 -> a1; kind 4 -> *(a2+540)
// (null -> 96); else 96. Then VIBE_Office_GetDefinition(subject.officeByte) must
// succeed (else 96). Returns the subject-selection code (reuses PrivResolveSubjectKind
// shape) — provided for completeness; the office-definition gate is below.
inline int RemoveOfficeDefinitionGate(bool officeDefFound) {
    return officeDefFound ? 0 : 96; /*0x562005 / 0x56211e*/
}

// gilde.exe 0x56200f (non-office actor path, actor+2 != 6): resolve the target
// person (*(actor+532)); if missing -> 96. Then the target must hold an office in
// this city whose category (>>24) equals the subject's office category, else 96.
// On a match the command pair is enqueued and the panel returns 16.
//   16 -> removed; 96 -> no target / category mismatch.
constexpr int kRemoveOfficeDone     = 16; // command enqueued (0x5621d6)
constexpr int kRemoveOfficeNoTarget = 96; // (0x56213d / 0x56217a)
int RemoveOfficeNonOfficeResult(bool targetFound, bool holderEntryFound,
                                bool categoryMatches);

// gilde.exe 0x56204a..0x562324 — the office-actor path (actor+2 == 6) result
// codes after the session window resolves:
//   session window returned null               -> 32  (0x5620f1)
//   holder-entry lookup failed                 -> 96  (0x5621ec)
//   holder entry byte+16 != 1 (not removable)  -> 32  (0x562289)
//   removed, but the op packet status == 2     -> 32  (0x5622e5, conflict)
//   removed cleanly                            -> -112 (0x562324)
constexpr int kRemoveOfficeSessionNull = 32;
constexpr int kRemoveOfficeNotRemovable = 32;
constexpr int kRemoveOfficeConflict     = 32;
constexpr int kRemoveOfficeCleanRemoved = -112;
int RemoveOfficeSessionResult(bool sessionPersonFound, bool holderEntryFound,
                              bool removable, bool packetConflict);

// ===========================================================================
// CounterEspionage — VIBE_Privilege_PanelCounterEspionage @0x5628c8.
// ===========================================================================
// gilde.exe 0x5628df — three arms by the actor kind byte (a1+2):
//   kind 6/7        -> run the dialog (returns 2 always).
//   rank < 4        -> return 32.
//   else            -> scan handlers, reset spy state, return v45 (2 | optional
//                      0x10 when at least one agent was reset).
// Returns: 2 (dialog), 32 (rank), or the direct-scan base verdict.
constexpr int kCounterEspDialogResult = 2;  // return 2 (0x562c8e)
constexpr int kCounterEspRankGate     = 32; // return 32 (0x562a06)
constexpr int kCounterEspBase         = 2;  // v45 = 2
constexpr int kCounterEspResetBit     = 0x10; // v45 |= 0x10 (0x5629dc)
// gilde.exe 0x562913 — the non-office direct path rank gate + verdict.
int CounterEspNonOfficeResult(int actorRank, bool anyAgentReset);

// ===========================================================================
// EvidenceReview / EvidenceReviewAlt — @0x565f9c / @0x5667a0.
// (EvidenceDetails @0x565b88 and BuildEvidenceEntry @0x56589c are already in
// world/office_recon_privilege.h: PrivEvidence* / PrivBuildEvidenceResult.)
// ===========================================================================
// gilde.exe 0x565fc3 — kind gate: kind 6/7 -> the HUD list dialog; else the
// concrete-evidence path. Returns true iff the office HUD path runs.
inline bool EvidenceReviewIsOfficePath(u8 actorKindByte) {
    return actorKindByte == 6 || actorKindByte == 7; /*0x565fd2*/
}

// gilde.exe 0x565fdf..0x566036 — the concrete (non-office) path early-outs:
//   actor.officeByte (a1+358) == 13 (judge)  -> 96.
//   target record (a2+532) absent            -> 96.
//   target.officeByte == 13                  -> 96.
//   FindMatchingEntityIndices == 0           -> 96.
// else -> BuildEvidenceEntry(actor, target, mode, matchCount).
// Returns the early code (96) or 0 to proceed to BuildEvidenceEntry.
int EvidenceReviewConcretePrecheck(u8 actorOfficeByte, bool targetFound,
                                   u8 targetOfficeByte, int matchCount);

// gilde.exe — the BuildEvidenceEntry "mode" arg (ecx). EvidenceReview passes 0;
// EvidenceReviewAlt passes 1. (Selects the witness/judge sourcing branch in
// BuildEvidenceEntry @0x565968.)
enum class EvidenceReviewMode : int { kReview = 0, kReviewAlt = 1 };
inline int EvidenceReviewBuildMode(EvidenceReviewMode m) {
    return static_cast<int>(m);
}

// ===========================================================================
// VIBE_Privilege_BuildEvidenceEntry — @0x56589c — FULL 1:1 BODY (wave-23).
//
// This is the lockstep "court case" command builder the Evidence panels call on
// confirm. Wave-22 reconstructed only its return-code predicate
// (PrivBuildEvidenceResult, office_recon_privilege.h); wave-23 reconstructs the
// COMPLETE body: the judge entity scan over the person array, the
// witness/accuser sourcing from the selection arrays, the two palette-range
// entity searches (the REAL guild::sim::ObjectSearchFindOneByPaletteRange), and
// the opcode-28 slot-reset command emission (the REAL
// guild::sim::QueueRequestSlotReset28). No engine leaf here is a genuine
// boundary — all are reconstructed in the live tree and reused (no ODR).
//
// The function reads a handful of engine globals; rather than hide them behind
// opaque hooks, we expose them as an explicit value view (EvidenceBuildWorld)
// matching each global the original touches, so the scan and the built command
// record are golden-pinnable byte-for-byte.
// ===========================================================================

// The engine globals VIBE_Privilege_BuildEvidenceEntry reads, modelled as a
// caller-supplied view. Each field carries its gilde.exe global + offset so the
// scan/record build stays verifiably 1:1. The two leaves it calls
// (FindOneByPaletteRange, QueueRequestSlotReset28) are routed through the REAL
// reconstructed implementations via the sink below.
struct EvidenceBuildWorld {
    // --- person array (dword_13CE298, stride 169) + AI office-byte table
    //     (dword_13CE294, office byte at [589*type]). The scan walks persons
    //     looking for office byte == 15 (a judge). ---
    // The per-person type byte sequence (record +0) for the live persons the
    // QueryBegin(op 6) iterator would visit, in order; empty / dead slots are
    // already excluded by the iterator. (We model the iterator's yield directly.)
    const u8* personTypes = nullptr;   // person.type byte per visited person
    int       personCount = 0;
    // AI office-byte table: officeByteForType[type] == aiTable[589*type]
    // (dword_13CE294). The scan tests this == 15 for the judge.
    const u8* officeByteForType = nullptr;   // dword_13CE294 (indexed by type)
    int       officeByteTypeCount = 0;

    // --- selection / live-person tables (the office HUD's 768-entry scene
    //     record; all three are views of the SAME 134-dword-stride record, the
    //     accessors below index by PERSON INDEX i, applying the original strides) ---
    // dword_12CE914[134*i] — the i-th live person's entity handle. Used for the
    //   judge handle (var_B8), the accuser handle (var_AC), the active selection
    //   id (var_10C := dword_12CE914[134*word_63CC5C]), and the witness handles
    //   (var_B4/var_B0 := dword_12CE914[134*v31[0]]). Indexed by person index.
    const i32* personHandleById = nullptr;    // dword_12CE914, per-index stride 134
    int        personHandleCount = 0;
    // word_12CE910[268*i] — the i-th live person's id word. Used for the judge id
    //   (v27.hi) and the accuser id (v28.lo). Indexed by person index.
    const u16* personIdById = nullptr;        // word_12CE910, per-index stride 268
    int        personIdCount = 0;
    // byte_12CEA76[536*i] — the i-th live person's office byte. The judge scan
    //   (a3==0 branch) walks this for office byte 13; the accuser scan for 17.
    //   Indexed by person index.
    const u8*  personOfficeById = nullptr;    // byte_12CEA76, per-index stride 536
    int        personOfficeCount = 0;

    // word_63CC5C — the local player / active-selection index (var_10C source).
    u16 localPlayerIndex = 0;                  // word_63CC5C

    // --- the two "court party" player records (dword_6498E8 / dword_6498EC[0]) ---
    // Each is a small record: word[0] == id, dword@+4 == handle. We model the two
    // fields the function reads (id + handle). Used by the mode!=0 judge branch
    // and the RNG fallback when no kind-13 holder is in the selection.
    u16 playerRecAId = 0;  i32 playerRecAHandle = 0;  // dword_6498E8 (*+0 / *+4)
    u16 playerRecBId = 0;  i32 playerRecBHandle = 0;  // dword_6498EC[0]

    // --- actor / target record scalars (a1+/a2+ fields the body reads). These
    //     are not in the shared PrivPerson view, so the world supplier fills them
    //     from the live records; the RunBuildEvidenceEntry bridge passes them on. ---
    u16 actorId = 0;          i32 actorHandle = 0;      // a1+0 / a1+4
    u8  actorOfficeBit12 = 0; u8  actorOfficeByte358 = 0; // a1+12 / a1+358
    u16 targetId = 0xFFFF;    i32 targetHandle = 0;     // a2+0 / a2+4
    u8  targetOfficeByte358 = 0;                         // a2+358

    // --- injected RNG draws (deterministic for golden tests) ---
    // The original draws these live; the reimpl takes them so the scan is exact:
    //   rngJudgePick : RandomModulo(2) for the no-holder judge fallback (0x5659b9).
    //   findStride/findProbe[2] : the FindOneByPaletteRange RandomModulo(16) /
    //     RandomModulo(768) draws per witness search (in ObjectSearch's body).
    u16 rngJudgePick = 0;
    int findStride[2] = {0, 0};
    int findProbe[2]  = {0, 0};
};

// The built 248-byte opcode-28 court record + the emitted-command sink. The
// reconstruction stages the exact field bytes (matching the original's stack
// frame writes) into `record`, then hands it to the REAL QueueRequestSlotReset28
// when `emit` is set. Tests inspect `record` for the byte-exact build and assert
// the emission fired.
struct EvidenceBuildSink {
    // The 248-byte scratch the original builds on its stack (&v16) and stages.
    // Field offsets (gilde.exe stack-var byte offsets): +4 op(44), +8 actor
    // handle, +0xC selection id, +0x36 byte(2), +0x58 actor handle, +0x5C target
    // handle, +0x60 judge handle, +0x64/+0x68 the two witness handles, +0x6C
    // accuser handle.
    u8  record[248] = {0};
    // The 4-word search filter blob (&v27): word[0]=target id, word[1]=judge id,
    // word[2]=accuser id (v28.lo) / witness1 (v28.hi), updated as the search runs.
    u16 filter[4] = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
    // The two witness palette indices the FindOne searches returned (v31[0]).
    u16 witnessIndex[2] = {0, 0};
    bool emitted = false;       // QueueRequestSlotReset28 fired (0x565b76)
    int  witnessSearches = 0;   // how many FindOne calls succeeded

    // Routed to the REAL leaves. If null, the build runs but does not call out
    // (headless): emit records `emitted=true`; the witness search uses
    // `witnessHit` to decide hit/miss deterministically.
    // Returns non-zero on a hit and writes *outIdx (mirrors FindOneByPaletteRange).
    int (*findWitness)(const EvidenceBuildWorld& w, const u16* filter, int which,
                       u16* outIdx, void* ctx) = nullptr;
    // Emits the staged 248-byte record through QueueRequestSlotReset28.
    void (*emitReset)(const u8* record248, void* ctx) = nullptr;
    void* ctx = nullptr;
};

// gilde.exe 0x56589c — VIBE_Privilege_BuildEvidenceEntry (FULL BODY).
//   a1 = actor record (eax), a2 = target record (edx), a3 = mode (ecx),
//   a4 = matchCount (ebx).
// Reconstructs the complete control flow:
//   1. Scan persons for a judge (office byte 15); if none -> return 16.
//   2. Build the 248-byte opcode-28 court record (actor/target/judge/witness/
//      accuser handles + selection id), sourcing the judge from the mode branch
//      or the selection scan (+ RNG fallback), and the accuser from the
//      selection scan (when neither party is office byte 17 and matchCount > 2).
//   3. Two palette-range witness searches; either miss -> return 64.
//   4. Emit the slot-reset command; return 16.
// `actorId`/`actorHandle`/`actorOfficeBit12`/`actorOfficeByte358` model the
// actor record fields the function reads (a1+0/+4/+12/+358); likewise for the
// target (`targetId`/`targetHandle`/`targetOfficeByte358`, a2+0/+4/+358).
char PrivBuildEvidenceEntry(u16 actorId, i32 actorHandle, u8 actorOfficeBit12,
                            u8 actorOfficeByte358, u16 targetId, i32 targetHandle,
                            u8 targetOfficeByte358, int mode, int matchCount,
                            const EvidenceBuildWorld& world,
                            EvidenceBuildSink& sink);

// Default sink wiring helper: binds `sink.findWitness` to the REAL
// guild::sim::ObjectSearchFindOneByPaletteRange (driven by world.findStride/
// findProbe) — production also binds `sink.emitReset` to QueueRequestSlotReset28
// (it needs the session CommandQueue/PendingState, so it lives in the provider).
int EvidenceWitnessSearchViaObjectSearch(const EvidenceBuildWorld& w,
                                         const u16* filter, int which,
                                         u16* outIdx, void* ctx);

// (RunBuildEvidenceEntry is declared after PrivilegePanelBHooks below.)

// ===========================================================================
// PANEL-SHAPED ENTRY POINTS + DISPATCHER (Rule 13 — the wirable surface).
//
// The predicate helpers above are the load-bearing decision math, golden-pinned
// and reused by the panel bodies below. Each Panel* function reproduces the FULL
// 1:1 control flow of its gilde.exe original — the subject-kind gate, the early
// return codes, the office (kind 6/7) GUI frame-loop arm vs. the non-office direct
// arm, the confirm/cancel verdict and the dword_631614 done-state — calling the
// coupled engine leaves (Form/HUD/command-queue/person-array, the SAME deferral
// boundary as SET-A) through the PrivilegePanelBHooks vtable. With the inert
// default hooks the office GUI arm records into the trace and produces the same
// verdict the original returns on a clean cancel/close; production wires the real
// leaves (see wire_privilege_panels_b.{h,cpp}).
//
// PrivPerson / PrivEvent are reused verbatim from privilege_panels_a.h.
// ===========================================================================

// One emitted lockstep command (recorded so tests assert the exact sequence).
struct PrivBCommand {
    enum Op {
        kBuildOp90,        // VIBE_Command_RequestBuildOp90(handle, delta)
        kArgs25,           // VIBE_Command_QueueRequestArgs25(handle, field, val, 4, 0)
        kRequest16,        // VIBE_Command_QueueRequest16(handle, -1, amount, 0)
        kCoord27,          // VIBE_Command_QueueRequestCoord27(a, b, delta)
        kEntity29,         // VIBE_Command_QueueRequestEntity29(arg, handler)
        kSlotReset28,      // VIBE_Command_QueueRequestSlotReset28
        kActionStart,      // VIBE_Command_EnqueueBuildingActionStart("absetzen")
        kActionEnd,        // VIBE_Command_EnqueueBuildingActionEnd()
        kBuildMemberTable, // VIBE_Privilege_BuildOfficeMemberTable(rngDraw)
    };
    int op = 0;
    i32 a = 0, b = 0, c = 0, d = 0;
};

struct PrivBTrace {
    PrivBCommand cmds[64];
    int          cmdCount = 0;
    int          lastFormScene = 0;   // GameTick_Finalize string base id
    int          lastMessageId = 0;   // last RenderFormattedMessage id
    int          doneState = 0;       // dword_631614 written
    i32          lastAmount = 0;      // last embezzle/law amount staged
    int          agentsReset = 0;     // CounterEspionage handler resets
    void Reset() { cmdCount = 0; lastFormScene = 0; lastMessageId = 0;
                   doneState = 0; lastAmount = 0; agentsReset = 0; }
};

// Frame-loop button source (mirrors SET-A's kPrivBtn* — reuse those values).
//   kPrivBtnOk (1210) confirm, kPrivBtnCancelId (1155) / a child-id cancel,
//   kPrivLoopExit (-2) ends the frame loop, kPrivLoopIdle (-1) no button.

// The coupled engine leaves SET-B panels call. Inert defaults => headless build.
struct PrivilegePanelBHooks {
    PrivBTrace* trace = nullptr;

    // VIBE_Person_FindRecordById @0x58bc6c — resolve a person id to a view.
    const PrivPerson* (*findRecord)(i32 id, void* ctx) = nullptr;

    // VIBE_Amt_ComputeOfficeWages @0x57b480 — office wage for a person id (the
    // embezzle base). Returns the wage int the embezzle math multiplies.
    i32 (*computeOfficeWages)(u16 personId, void* ctx) = nullptr;

    // VIBE_Math_RandomModulo @0x58b89c — RNG draw 0..n-1 (embezzle / miracle).
    int (*randomModulo)(u16 n, void* ctx) = nullptr;

    // VIBE_Office_GetDefinition @0x47f008 — true + category if the office byte is
    // a defined office; writes the office category (high byte of the def dword).
    bool (*officeGetDefinition)(u8 officeByte, u8* outCategory, void* ctx) = nullptr;

    // VIBE_Office_GetHolderEntryByCity @0x47dfec — true + category if the target
    // holds an office entry in this city.
    bool (*officeHolderEntry)(i32 personHandle, u8* outCategory, bool* outRemovable,
                              void* ctx) = nullptr;

    // VIBE_Panel_RunOfficeSession @0x5546e4 — modal office-holder picker (Remove).
    const PrivPerson* (*runOfficeSession)(u8 officeCat, int promptMsgId,
                                          void* ctx) = nullptr;

    // VIBE_Office_AwaitPromoteResult @0x562c9c — swap-seats promote await.
    bool (*awaitPromote)(const PrivPerson* actor, const PrivPerson* lower,
                         const PrivPerson* upper, void* ctx) = nullptr;

    // Live person/office arrays for the SwapSeats holder scan
    // (word_12CE910 / byte_12CEA76). Returns the per-person office bytes.
    const u8* (*officeByteArray)(int* outCount, void* ctx) = nullptr;

    // VIBE_He_FindMatchingEntityIndices @0x4c42c0 — evidence match count.
    int (*matchingEntityCount)(i32 targetHandle, u16 actorId, void* ctx) = nullptr;

    // EvidenceDetails aggregation source (0x565c35): for the i-th matched evidence
    // row (0..matchCount-1) returns the row's field37 value — the +37 dword of the
    // 45-byte record at dword_11BC760 + 45*indices[i]. The panel ORs (field37==1)
    // across all rows into the "any-actionable" flag (v28). Inert default => 0.
    i32 (*evidenceRowField37)(i32 targetHandle, u16 actorId, int rowIndex,
                              void* ctx) = nullptr;

    // CounterEspionage handler scan: how many spy agents targeting the actor were
    // reset (each sets v45 |= 0x10). Returns the count (>=1 => the 0x10 bit).
    int (*counterEspScan)(i32 actorHandle, bool office, void* ctx) = nullptr;

    // VIBE_Dialog_CheckSkillRequirement @0x4ad594 — actor has the privilege skill.
    bool (*checkSkill)(const PrivPerson* actor, int level, void* ctx) = nullptr;

    // VIBE_Privilege_BuildEvidenceEntry @0x56589c — returns 16 / 64 (judge/witness).
    // When `buildEvidenceEntry` is null but `evidenceWorld` is supplied, the panels
    // run the FULL reconstructed PrivBuildEvidenceEntry against the supplied world
    // view + sink (the real judge scan / witness search / command emission). When
    // both are null the call is a no-op (returns 0). Production may either install
    // a thin `buildEvidenceEntry` or supply `evidenceWorld`/`evidenceSink`.
    int (*buildEvidenceEntry)(const PrivPerson* actor, const PrivPerson* target,
                              int mode, int matchCount, void* ctx) = nullptr;

    // Supplies the engine-global view + the real-leaf-routed sink the FULL
    // PrivBuildEvidenceEntry body needs. `out`/`sink` are caller-owned scratch the
    // panel passes in; the supplier fills them from the live globals + binds the
    // sink's findWitness/emitReset to the real ObjectSearchFindOneByPaletteRange /
    // QueueRequestSlotReset28. Returns true if a world was supplied.
    bool (*evidenceWorld)(const PrivPerson* actor, const PrivPerson* target,
                          struct EvidenceBuildWorld* out,
                          struct EvidenceBuildSink* sink, void* ctx) = nullptr;

    // The frame-loop button source (kPrivBtn* / kPrivLoop*). Inert => loop-exit.
    int (*nextButton)(void* ctx) = nullptr;

    void* ctx = nullptr;
};

// Bridge the Evidence panels' confirm-click to the evidence-build leaf: if the
// hooks supply a thin `buildEvidenceEntry`, call it; else if they supply an
// `evidenceWorld`, run the FULL PrivBuildEvidenceEntry against it (the real judge
// scan + witness search + command emission). Returns the verdict (16/64) or 0
// when neither is wired (headless). This is what the panel bodies invoke.
int RunBuildEvidenceEntry(const PrivPerson* actor, const PrivPerson* target,
                          int mode, int matchCount, PrivilegePanelBHooks* h);

// --- the ten SET-B panel entry points (verdict == original's al, signed char) ---

char PrivilegePanelEnactLaw(const PrivPerson* actor, const PrivEvent* ev,
                            PrivilegePanelBHooks* h);                 // 0x561bb4
char PrivilegeRemoveFromOffice(const PrivPerson* actor, const PrivEvent* ev,
                               PrivilegePanelBHooks* h);              // 0x561fd0
char PrivilegePanelCounterEspionage(const PrivPerson* actor,
                                    PrivilegePanelBHooks* h);         // 0x5628c8
char PrivilegePanelEmbezzlement(const PrivPerson* actor, const PrivEvent* ev,
                                PrivilegePanelBHooks* h);             // 0x562334
char PrivilegePanelSwapSeats(const PrivPerson* actor, const PrivEvent* ev,
                             PrivilegePanelBHooks* h);                // 0x562cdc
char PrivilegePanelMiracle(const PrivPerson* actor,
                           PrivilegePanelBHooks* h);                  // 0x5651bc
char PrivilegePanelEvidenceReview(const PrivPerson* actor, const PrivEvent* ev,
                                  PrivilegePanelBHooks* h);           // 0x565f9c
char PrivilegePanelEvidenceReviewAlt(const PrivPerson* actor, const PrivEvent* ev,
                                     PrivilegePanelBHooks* h);        // 0x5667a0

// gilde.exe 0x565b88 — VIBE_Privilege_PanelEvidenceDetails. The per-evidence
// detail dialog the Evidence(Review/Alt) office HUD-list opens on a row click. It
//   * returns 0   when the target id word == 0xFFFF (no target);
//   * returns 96  when the evidence-match count < 1 (no evidence);
//   * otherwise aggregates each matched row's field37 (==1 => actionable), forces
//     the actionable flag false when the inspecting actor IS the target (self),
//     runs the frame loop, and on the confirm click calls BuildEvidenceEntry(actor,
//     target, mode=0, matchCount) and writes dword_631614 = 3; a window-close
//     writes dword_631614 = 1. ALWAYS returns 0 (the verdict is carried in
//     dword_631614 / the parent's latched v49). `actorIsTarget` models v30==v31.
char PrivilegePanelEvidenceDetails(const PrivPerson* actor, const PrivPerson* target,
                                   bool actorIsTarget, PrivilegePanelBHooks* h); // 0x565b88
// ShowDialog @0x571218 has void return; the wirable surface is the verdict-less
// runner. Exposed as a no-verdict dispatch (records the chosen backdrop scene).
void PrivilegeShowDialog(int richStringId, int size, PrivilegePanelBHooks* h); // 0x571218

// gilde.exe — routes a SET-B leaf-id (absolute address used as the opaque id in
// interaction_handlers' g_leafTrace) to the reconstructed panel. Returns the panel
// verdict (signed char widened to int). Unknown ids return 0.
int PrivilegeDispatchPanelB(int leafId, const PrivPerson* actor,
                            const PrivEvent* ev, PrivilegePanelBHooks* h);

} // namespace guild::world
