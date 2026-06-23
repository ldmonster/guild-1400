#pragma once
#include "guild/common/types.h"

// command_recon4_senders — the VIBE_Command_Send*/Enqueue* entity-action packet
// emitters (namespace guild::sim):
//   0x493a08 RetZero                  — return 0 stub
//   0x4952b4 QueueRequestTransform64  — emit a type-64 transform-delta packet
//   0x4c2218 EnqueueLawAction         — law/decree action enqueue
//   0x538224 EnqueueDuelChallenge     — duel-challenge group packet
//   0x48980c CombatSetUnitFormationMode — formation-slot fill + mode packets
//   0x5675ac SendEntityActionA        — disease/contagion notify (cmd 118/375)
//   0x567944 SendEntityActionB        — two-stage notify (cmd 118+119/376)
//   0x567e24 SendEntityActionC        — office-overview action (cmd -126/2/3/2)
//   0x56801c SendEntityActionD        — office-overview action (cmd -126/2/4)
//   0x568278 SendMapEntityAction      — map-wide entity flag delta
//
// These are command-queue PACKET-EMISSION leaves. The queue staging primitives
// (EnqueuePacket, BeginDeltaPacket, AppendDeltaField, QueueRequestSlotReset28,
// QueueRequest*, StagePendingBlock), the UI dispatchers (MapView_PanelDispatcher,
// Amt_RunOfficeOverviewWindow), text rendering, person queries and game-time are
// NOT reconstructed here — they belong to other modules / live state — so they
// are routed through an installable hooks struct with inert defaults. The
// observable EMISSION SEQUENCE and the packet FIELD CONSTANTS / LAYOUT (which
// command byte, which slot offset, which value source) ARE reconstructed 1:1 and
// are reported to the recording hook for byte-for-byte verification.

namespace guild::sim {

// ---------------------------------------------------------------------------
// Recorded emission events. Each entry mirrors one observable packet primitive.
// ---------------------------------------------------------------------------
enum class Recon4Emit : u8 {
    EnqueuePacket,        // EnqueuePacket(buf): a/b = buf[0] (cmd byte), arg1 = aux
    SlotReset28,          // QueueRequestSlotReset28(rec, arg): a = packet cmd byte (rec[0]), b = arg
    BeginDeltaPacket,     // BeginDeltaPacket(a, b)
    AppendDeltaField,     // AppendDeltaField(width, count, valueSrcId, slotOff)
    QueueRequestState22,  // QueueRequestState22()
    QueueRequestArgs25,   // QueueRequestArgs25(id, a, b, c, d)
    SendEntityMessage,    // He_SendEntityMessage(id, -1, 0, buf, 1418, 0)
    // -- appended (wave 2; positional initializers above stay valid) --
    EnqueueCmd15,         // EnqueueCmd15(payer, id, costAux, modeByte) — cmd-15 charge packet
    QueueRequestCoord27,  // QueueRequestCoord27(id, targetId, mode, zero) — cmd-27 packet
    AppendRawField,       // AppendRawField(width, count, value, slotOff) — absolute (0x493c14)
    // -- appended (wave 2, part 3) --
    QueueRequest17,       // QueueRequest17(id@a, -1@b, 1@c, opcode@d, season@e) — 0x49465c;
                          // the 6th (stack) argument is the constant 0 at every site here
};

struct Recon4EmitEvent {
    Recon4Emit op;
    i32 a = 0, b = 0, c = 0, d = 0, e = 0;
};

struct Recon4SenderHooks {
    // Person / object resolution leaves.
    void* (*personQueryBegin)(i32 a1, i32 a2, i32 a3, i32 key) = nullptr;
    void* (*personFindRecordById)(i32 id) = nullptr;
    // UI dispatchers. Return the resolved object/record base (or null).
    void* (*mapViewPanel)(i32 mode, const void* hdr, const char* msg) = nullptr;
    void* (*officeOverview)(const void* hdr, const char* msg, i32 arg) = nullptr;

    // Read a field from a resolved record:
    //   typeWord (+39 / +0x27) ; entityId (+1 dword) ; statusKind (+2 byte).
    i16 (*recTypeWord)(void* rec) = nullptr;
    i32 (*recEntityId)(void* rec) = nullptr;  // *(rec+1) dword
    u8  (*recStatusKind)(void* rec) = nullptr; // *(rec+2) byte

    // selection-table column reads keyed by typeWord (word_12CE910 family):
    //   kind  byte_12CE912[536*w] ; id dword_12CE914[134*w]
    u8  (*tblKind)(u16 typeWord) = nullptr;
    i32 (*tblId)(u16 typeWord) = nullptr;

    // The "to" command record base id read at *(cmd+4).
    i32 (*cmdToId)(void* cmd) = nullptr;

    // Recording sink for every emitted primitive. Default: discard.
    void (*emit)(const Recon4EmitEvent* ev) = nullptr;

    // ----- wave-2 sender leaves (appended; positional inits stay valid) -----
    // gilde.exe 0x5851fc — VIBE_Building_FindSlotByProt: locate the canonical
    // 0x80-byte building production/transform slot for (buildingIdx, prot).
    // Live table: dword_13C3B50 + 7952*building + 0x10 + 0x80*i, i<62, key =
    // word @ slot+0; returns 0 on a miss. The original sender does NOT
    // null-check the result (its callers at 0x481f5f / 0x584700 guarantee a
    // hit); the inert default returns a static zeroed slot so the delta math
    // stays well-defined headless.
    const u8* (*findSlotByProt)(u8 building, i16 prot) = nullptr;
    // gilde.exe 0x49388c — VIBE_Command_EnqueuePacket: stage the 153-byte
    // buffer into the send ring. Returns the assigned ring slot, or -1 when
    // the queue is blocked (dword_764CF0 latch). Real wiring routes to
    // CommandQueue::EnqueuePacket (src/sim/command.cpp). Default: -1.
    i32 (*enqueuePacket)(const u8* staging153) = nullptr;

    // --- EnqueueLawAction (0x4c2218) leaves — person-table live state ---
    u8  (*personStatByte)(void* rec, u32 off) = nullptr; // *(u8*)(rec + 0x80 + off)
    i32 (*personSlotId)(i32 slot) = nullptr;             // dword_12CE914 col: *(i32*)(0x12CE910 + 536*slot + 4)
    i8  (*personSlotBuildingCode)(i32 slot) = nullptr;   // *(i8*)(0x12CE910 + 536*slot + 0x164) (byte_12CEA74 col)
    u8  (*seasonByte6477A1)() = nullptr;                 // byte_6477A1 (cmd-15 mode byte; save-block "season")

    // --- EnqueueDuelChallenge (0x538224) leaves ---
    // gilde.exe 0x586a6c — VIBE_Person_IterNext: advances the global person-
    // query cursor; returns the next record in EAX. The duel call site
    // DISCARDS the result (it re-tests the stale ECX, which IterNext
    // preserves); the hook is still invoked for the cursor side effect.
    void* (*personIterNext)() = nullptr;
    u16   (*localPlayerTypeWord)() = nullptr; // *(u16*)(*(void**)0x6498E4) — local player record word
    u16   (*localPlayerIndex)() = nullptr;    // word_63CC5C
    // gilde.exe 0x591730 — VIBE_GameObject_ResolveOwnerOrParentB: resolve
    // *(u32*)(obj+0x0A) to a record, else fall back to the person-table row
    // &word_12CE910[268*typeWord], else null. The caller reads +4 via cmdToId.
    void* (*resolveOwnerOrParentB)(void* obj) = nullptr;
    // person-table row columns by row index (stride 536 from 0x12CE910):
    u16   (*tblRowTypeWord)(u16 idx) = nullptr; // +0x00  word_12CE910 column (0xFFFF == free)
    void* (*tblRowOwnerRec)(u16 idx) = nullptr; // +0x16C dword_12CEA7C column (owner-record ptr)
    void  (*gameTimeNow)(void* out14) = nullptr; // copy the 14-byte clock qword_13CE852
    // gilde.exe 0x494c30 — VIBE_Command_QueueRequest39: StagePendingBlock(276,
    // pkt) then enqueue a cmd-39 wrapper packet. Default: 0.
    i32   (*queueRequest39)(const u8* pkt276) = nullptr;

    // ----- wave-2 part 3: CombatSetUnitFormationMode (0x48980c) leaves -------
    // (appended; positional initializers above stay valid)
    // gilde.exe dword_6498F0 — the handler/unit pointer array indexed by the
    // resolved formation-slot index (an 8-entry table in the save layout, see
    // io/save_person.h). The 16-slot scan can resolve idx 8..15, and idx == -1
    // reproduces the original's dword_6498F0[-1] read (0x489850 with edi = -4,
    // i.e. the dword at 0x6498EC) when all 16 packet id slots are occupied —
    // both out-of-bounds reads are modeled by the hook. Default: null.
    void* (*playerSlotUnit)(i32 idx) = nullptr;
    u16   (*unitProtWord)(void* unit) = nullptr; // *(u16*)(unit+0)    (0x48989f)
    i32   (*unitStock24)(void* unit) = nullptr;  // *(i32*)(unit+0x24) (0x48989a)
    // gilde.exe 0x485b94 — VIBE_Combat_ResetObjectHighlights(unit@eax).
    void  (*resetObjectHighlights)(void* unit) = nullptr;
    // gilde.exe 0x57d5b4 — VIBE_Building_AdjustStockAndNotify(tblWord@ax,
    // delta@edx) -> st0 (discarded by the caller: fstp st @0x4898bc/0x489982/
    // 0x489a7d/0x489b41). The `int@<ecx>` third arg in the IDA prototype is
    // spurious: 0x57d5b4 push/pops ECX (0x57d5b5/0x57d77d) and never reads it
    // before overwriting (first ECX use is `mov cx, ...` @0x57d6e4). Default:
    // -1.0f (the function's own table-miss return, 0x57d780).
    float (*adjustStockAndNotify)(u16 tblWord, i32 delta) = nullptr;
};

void SetRecon4SenderHooks(const Recon4SenderHooks* h);
const Recon4SenderHooks& GetRecon4SenderHooks();

// gilde.exe 0x493a08 — VIBE_Command_RetZero. Constant `return 0` stub. Named
// CommandRetZero to avoid ODR clash with the byte-identical guild::sim::RetZero
// (0x4dc070, VIBE_CharAction_RetZero, charaction_steps2).
int CommandRetZero();

// The senders take the command record base `cmd` (a u8*; the originals index it
// by byte offset, e.g. *(cmd+2) status, *(cmd+4) to-id) and a param/aux pointer.
// 0x5675ac — SendEntityActionA(cmd, params)  params[1] = person query key.
int SendEntityActionA(u8* cmd, i32* params);
// 0x567944 — SendEntityActionB(cmd, params, src)
int SendEntityActionB(u8* cmd, i32* params, i32 src);
// 0x567e24 — SendEntityActionC(cmd, params)
int SendEntityActionC(u8* cmd, i32* params);
// 0x56801c — SendEntityActionD(cmd, params, aux)
int SendEntityActionD(u8* cmd, i32* params, i32 aux);
// 0x568278 — SendMapEntityAction(cmd, params, src)
int SendMapEntityAction(u8* cmd, i32* params, i32 src);

// ---------------------------------------------------------------------------
// gilde.exe 0x4c1810 — dword_4C1810: 51 x 40-byte law/poem descriptor table
// (recovered verbatim with get_bytes). type 0 = stat poem (arg = stat byte
// offset 1..4, applied at rec+0x80+arg), type 1 = guild poem (arg = building
// group 1..12, broadcast), type 2 = story/ballad (no extra effect). Field +4
// is the same column world/law_text.cpp carries as kLawTypeVariantFlag
// (cross-validated byte-for-byte against this dump).
// ---------------------------------------------------------------------------
struct LawActionDef {
    u8   type;      // +0x00 action type (0/1/2)
    u8   pad;       // +0x01 (always 0 in the binary)
    u16  arg;       // +0x02 stat offset (type 0) / building group (type 1)
    u16  flag;      // +0x04 variant flag (read by 0x4c2070, not by 0x4c2218)
    u16  textId;    // +0x06 localized text id
    char name[32];  // +0x08 ASCII tag (NUL padded)
};
static_assert(sizeof(LawActionDef) == 40, "law record stride must be 40");
constexpr i32 kLawActionCount = 51;            // cmp si, 33h @0x4c2258
extern const LawActionDef kLawActionTable[kLawActionCount];

// gilde.exe 0x4952b4 — VIBE_Command_QueueRequestTransform64
//   (__usercall eax = rec : i16*, an 0x80-byte slot-shaped transform record
//    whose +0 word is the prot id; dl = building index byte).
// Emits an opcode-0x40 (wire size 47) transform-delta packet: int deltas of
// rec vs the canonical slot at +0x10/+0x14/+0x18, float deltas at
// +0x20/+0x24/+0x2C/+0x38. Returns the EnqueuePacket result. Call sites:
// 0x481fb0/0x482024 (Amt_BuildGuildOfficeMenu 0x481db8), 0x4e91ce/0x4e96c7/
// 0x4e9c02/0x4e9f98/0x4ea09f (NpcAction_RunMarktSupervisorStep 0x4e8bdc),
// 0x584761 (Building_RandomizeStockTransforms 0x584680).
int QueueRequestTransform64(const i16* rec, u8 building);

// gilde.exe 0x4c2218 — VIBE_Command_EnqueueLawAction
//   (__usercall, eax = fn(rec@eax, aux@edx, lawIdx@bx)).
// rec = 536-byte person-table slot of the performer (sole caller 0x549a0f in
// VIBE_BardDialog_PerformPoem 0x5495a8 builds it as &word_12CE910 +
// 0x218*word_63CC5C); the caller discards eax. Returns 40*lawIdx on the
// type!=0/1 exit (the only path whose eax is a defined value); 0 on the
// emission paths, whose original eax is an unobserved EnqueuePacket return.
int EnqueueLawAction(u8* rec, i32 aux, u16 lawIdx);

// gilde.exe 0x538224 — VIBE_Command_EnqueueDuelChallenge (__usercall, eax=obj).
// obj = challenged game object (dword_11BC274 at the only call site, debug key
// 0x25 in VIBE_DebugKey_ToggleUpdateFlags 0x4bf054; see cheat_recon.h
// DebugToggle::DuelChallenge). Builds the 276-byte duel-group packet (inner
// command id 0xA2/162) and queues it via QueueRequest39. PRESERVES the
// original's find-loop defect (see the .cpp comment at the loop): IterNext
// preserves ECX, so the loop terminates only when the queried record's word
// differs from the live local-player word. Re-verified 2026-06-11 against the
// raw disasm: loop cursor = ECX @0x538243-0x538261; IterNext 0x586a6c saves
// ECX in its prologue (push ecx @0x586a6d) and restores it in its epilogue
// (pop ecx @0x586bce), returning the NEW cursor in EAX (mov eax,ecx
// @0x586bb6) which the call site 0x53825a discards.
i32 EnqueueDuelChallenge(u8* obj);

// gilde.exe 0x48980c — VIBE_Combat_SetUnitFormationMode
//   (__usercall, eax = pkt : the 276-byte duel/formation group packet built by
//    its callers, dl = mode byte; the eax return is never observed — both
//    callers overwrite eax immediately after each call: 0x538348/0x538356 in
//    EnqueueDuelChallenge 0x538224 and the 0x4ed726/0x4ed72f pair in
//    VIBE_NpcAction_CityFormationMoveStep 0x4ed018).
// ANY nonzero mode first stores the zero-extended mode dword at pkt+0x110
// (0x4899bc..0x4899c0); mode 0 skips the store. Then the dispatch: mode 1 runs
// two emission passes with opcode 342/delta byte 0xA8 each (0x489882/0x489948,
// 0x4898ac/0x48996d); modes 2 and 3 share one arm (0x489b69 jumps into the
// mode-2 body) running opcode 344 then 352 with delta byte 0xD2 (0x489a44/
// 0x489b09, 0x489a6e/0x489b33); every other nonzero mode returns right after
// the +0x110 store (0x489b6f). Each pass: resolve the first -1 dword in
// pkt+0x80..+0xBC (16 slots, -1 when all full), unit = dword_6498F0[slot],
// store *(unit+4) into the slot (slot == -1 writes pkt+0x7C: edi = -4!),
// ResetObjectHighlights(unit), QueueRequest17(*(unit+4), -1, 1, opcode,
// byte_6477A1, 0), AdjustStockAndNotify(*(u16*)unit, -*(i32*)(unit+0x24)),
// BeginDeltaPacket(unit, *(unit+4)), AppendDeltaField(1, 1, &deltaByte,
// LOWORD(unit) + 0x83 - LOWORD(dword_11AA474)), QueueRequestState22().
// (The Hex-Rays decompile of this function is garbled — its v9/v14/v20/v22
// locals read "uninitialized" registers — because ECX is callee-saved in this
// binary's convention and the original reuses the `lea ecx,[esi+80h]` staged
// BEFORE each QueueRequestState22 call for the scan AFTER it; the disasm above
// is the reference. Deterministic cores shared with combat_drivers.h:
// FindFirstFreeFormationSlot / kFormOp* opcodes.)
void CombatSetUnitFormationMode(u8* pkt, u8 mode);

} // namespace guild::sim
