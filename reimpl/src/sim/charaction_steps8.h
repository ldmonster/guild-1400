#pragma once
// charaction_steps8 — batch 8 (and the final remaining slice) of the CharAction
// step / state-machine LEAVES of the Guild simulation (gilde.exe,
// VIBE_CharAction_* family). The earlier batches (charaction.cpp,
// charaction_steps2..7, charaction_brawl/misc/motion/walk) translated the bulk of
// the family; the inventory query (`VIBE_CharAction_*`, 115 functions) is now
// nearly exhausted. This batch fills in the LAST untranslated functions:
//
//   * RunArrestPerson      (0x4e4204) — arrest coroutine working/finish phases.
//   * InitArrestPerson     (0x4e3fdc) — arrest one-shot setup + accomplice notify.
//   * InitEscortPrisoner   (0x4e4484) — escort one-shot setup (timed walk).
//   * RunLagerFuellen      (0x4dffa0) — "fill warehouse" 3-phase machine.
//   * RunLagerErweitern    (0x4e0d8c) — "expand warehouse" coroutine.
//   * RunAdjustObjectField (0x4e0a40) — clamp/bump two object byte fields (+28/+29).
//   * RunPickFromGround    (0x4e1c78) — production "pick from ground" gather step.
//   * RunHerdAnimals       (0x4e17dc) — herd up to N animals; produce + notify.
//   * FindGestureTarget    (0x4dc374) — proximity-scan for a gesture partner.
//   * MorphMovementInit    (0x409200) — swap walk/cart mesh + arm a morph anim.
//   * QueueFreeAll         (0x40c120) — destroy every live character in the pool.
//   * ResetWalkTarget      (0x40be0c) — clear the four walk-target globals.
//
//   DEFERRED: RunEventMessagebox (0x4e0294, 1908 bytes) — see the .cpp report
//   comment. It is an EventPanel/Form/Cutscene/Voice/Audio/Input/Dialog UI state
//   machine whose Hex-Rays output carries several genuinely uninitialized SSA temps
//   (v8/v10/v16/v21 used before def) on register-arg paths; a faithful 1:1
//   translation cannot be pinned without the UI cluster, which is out of scope.
//
// The state-machine control flow, the phase ids, the RNG draws, the GameTime
// arithmetic and the exact He-record field offsets are translated 1:1. All
// cross-cluster leaf side effects (person/object/building resolves, the cmd-queue
// emits, the formatted-message renders + quickjump sends, the character/mesh ops,
// the pool walk and the global walk-target slots) are routed through the
// CharActionStep8Hooks bridge below plus the shared NpcLeafHooks (npcaction.h).
// Installing a null hook table restores the inert default (every resolve reports
// absent, every emit/render is a no-op, every query/counter returns 0).
// NpcClock()/GameTimeAdvance/GameTimeCompare/GameTimeDiffMinutes are reused.
//
// Addresses are absolute, imagebase 0x400000.
#include "guild/common/types.h"
#include "sim/he.h"

namespace guild::sim {

// ===========================================================================
// He-record fields this batch reaches beyond the shared he.h map. All are
// byte-faithful offsets; the structured he.h view reserves enough tail bytes.
//   +0x10 (+16)  : escort/lager partner entity id (dword)        — He_Misc16.
//   +0xA9 (+169) : packed field selector (byte at >>24 of dword) — He_FieldSel.
//   +0xAC (+172) : object/cart entity id (dword)                 — He_Counter172.
//   +0xB0 (+176) : count A / room-worth budget (dword)           — He_TargetObjId.
//   +0xB4 (+180) : count B / fill threshold (dword)              — He_ScanStep.
//   +0xB8 (+184) : iteration counter (RunArrest) (dword)         — He_SeqId.
//   +0xC4 (+196) : herd member-id array base (dword each, x4)    — He_Member196.
//   +0xD4 (+212) : herd "started" cursor / escort window (dword) — He_Misc212.
//   +0xD8 (+216) : RunLagerFuellen "announce" flag (dword)       — He_Misc216.
//   +0x52 (+82)  : appointment GameTime ; +0x60(+96) scratch GameTime.
//   +0x44 (+68)  : saved GameTime (escort pose base).
//   +0x84 (+132) : entity-request packet handle (-1 == none).
// ===========================================================================
inline i32& Cas8_Misc16(HeRecord* h)    { return *reinterpret_cast<i32*>(HeBytes(h) + 16); }
inline u8&  Cas8_FieldSelB(HeRecord* h)  { return *reinterpret_cast<u8*>(HeBytes(h) + 172); }
inline i32& Cas8_CountA(HeRecord* h)     { return *reinterpret_cast<i32*>(HeBytes(h) + 176); }
inline i32& Cas8_CountB(HeRecord* h)     { return *reinterpret_cast<i32*>(HeBytes(h) + 180); }
inline i32& Cas8_Iter(HeRecord* h)       { return *reinterpret_cast<i32*>(HeBytes(h) + 184); }
inline i32& Cas8_Member(HeRecord* h, int slot) { return *reinterpret_cast<i32*>(HeBytes(h) + 140 + 4 * slot); }
inline i32& Cas8_Misc216(HeRecord* h)    { return *reinterpret_cast<i32*>(HeBytes(h) + 216); }
// The packed field selector: the original reads `*(int*)(a1+169) >> 24` to pick the
// person record's per-object field byte. We expose the byte at +0xAC (which is the
// high byte the >>24 lands on after the dword load at +0xA9). The translated code
// uses the dedicated FieldSelector() helper to keep the read byte-faithful.
inline i32  Cas8_FieldSelDword(HeRecord* h) { return *reinterpret_cast<i32*>(HeBytes(h) + 169); }

// ===========================================================================
// Recovered .rdata float constants (gilde.exe). Default values are the observed
// .rdata image; they feed opaque production / probability math and are not asserted
// by the golden control-flow tests, but are named + addressed for the record.
//   flt_61F624 / flt_61F628 — RunArrest room-worth ratio + scale.
//   dbl_61F630              — InitEscort speed->minutes factor.
//   dbl_61F518             — RunPick produce ceiling (per-field cap).
//   dbl_61F520 / dbl_61F528 — RunPick output multipliers.
//   dbl_61F530             — RunPick workstation bonus factor.
//   flt_61F538 / flt_61F53C — RunPick field-fill divisor base + ceiling.
//   dbl_61F4F8             — RunHerd workstation bonus factor.
// ===========================================================================
constexpr double kArrestWorthBias  = 1.0;   // flt_61F624
constexpr double kArrestWorthScale = 0.5;   // flt_61F628
constexpr double kEscortSpeedFac   = 0.5;   // dbl_61F630
constexpr double kPickCeiling      = 100.0; // dbl_61F518
constexpr double kPickMulA         = 0.1;   // dbl_61F520
constexpr double kPickMulB         = 1.0;   // dbl_61F528
constexpr double kPickWorkBonus    = 0.1;   // dbl_61F530
constexpr double kPickFieldDiv     = 0.1;   // flt_61F538
constexpr double kPickFieldCeil    = 100.0; // flt_61F53C
constexpr double kHerdWorkBonus    = 0.1;   // dbl_61F4F8

// ===========================================================================
// CharActionStep8 cross-cluster leaves. A null member installs an inert default.
// The shared NpcLeafHooks (npcaction.h) supplies freeHandlerEntry / packetStatus /
// queueRequestEntity29; this struct adds the resolves, the cmd-queue emits, the
// render/send bridge, the character/mesh ops, the pool walk and the global
// walk-target slots. Return-value semantics match the original leaves.
// ===========================================================================
struct CharActionStep8Hooks {
    // --- resolves / queries ---------------------------------------------
    // VIBE_Person_QueryBegin(self, a, b, key) — resolve a person record (null=absent).
    HeRecord* (*personQueryBegin)(i32 self, int a, int b, i32 key);
    // VIBE_Person_FindRecordById(id) — person record by id (null=absent).
    HeRecord* (*personFindRecordById)(i32 id);
    // VIBE_Person_FindActiveByEntity(rec) — the active behavior record (null=none).
    HeRecord* (*personFindActiveByEntity)(HeRecord* rec);
    // VIBE_GameObject_QueryFind(scene, a, b, c, key) — find a world object (null=none).
    HeRecord* (*objectQueryFind)(i32 scene, int a, int b, int c, i32 key);
    // VIBE_GameObject_ResolveEntityById(scene, &out, id, flag) — resolve into *out.
    HeRecord* (*resolveEntityById)(int scene, HeRecord** out, i32 id, int flag);
    // VIBE_GameObject_SumChildMoney(objId) — total money carried by an object tree.
    int (*sumChildMoney)(i32 objId);
    // VIBE_Building_FindById(id) — building record (null=absent).
    HeRecord* (*buildingFindById)(i32 id);
    // VIBE_Building_FindWorkProductObject(rec) — product object (null=none).
    HeRecord* (*findWorkProduct)(HeRecord* rec);
    // VIBE_Building_FindStorableObject(rec) — a storable target object (null=none).
    HeRecord* (*findStorable)(HeRecord* rec);
    // VIBE_Building_SumWorkstationByCategory(rec, cat, flag) — matching slot count.
    int (*sumWorkstation)(HeRecord* rec, int cat, int flag);
    // VIBE_BuildingValue_ComputeRoomWorth(rec, kind, key) — room appraisal value.
    int (*computeRoomWorth)(HeRecord* rec, int kind, i32 key);
    // VIBE_Production_ComputeOutputOverTime(scratchTime, clock) — produced volume.
    int (*productionOutput)(GameTime* scratchTime, const GameTime* clock);

    // --- handler iteration (FindGestureTarget) --------------------------
    // VIBE_He_FindFirstHandlerByFilter(a,b,c) / FindNextMatchingHandler() — iterate
    // the live handler pool filtered by kind. null terminates.
    HeRecord* (*heFindFirst)(int a, int b, int c);
    HeRecord* (*heFindNext)();

    // --- emits / effects -------------------------------------------------
    // VIBE_Command_QueueRequestArgs25(a,b,c,d,e) — opaque cmd-25 emit.
    void (*cmdRequestArgs25)(i32 a, int b, int c, int d, int e);
    // VIBE_Command_QueueRequestSingle49(a).
    void (*cmdRequestSingle49)(i32 a);
    // VIBE_Command_QueueRequestNamedObject53(a,b,c,d,e,name).
    void (*cmdRequestNamedObject53)(i32 a, i32 b, int c, int d, int e, const char* name);
    // VIBE_Command_QueueRequestFlag55(a, flag).
    void (*cmdRequestFlag55)(i32 a, int flag);
    // VIBE_Command_QueueRequest17(a,b,c,d,e,f).
    void (*cmdRequest17)(i32 a, int b, int c, int d, int e, int f);
    // VIBE_Command_QueueRequestSlotReset28(blob, scratch).
    void (*cmdRequestSlotReset28)(void* blob, int scratch);
    // VIBE_Command_QueueRequestEntity29(arg, scratch) — RunArrest re-arm; returns hdl.
    i32 (*cmdRequestEntity29)(i32 arg, void* scratch);
    // VIBE_Command_QueueRequestState22() — flush the pending delta packet.
    void (*cmdRequestState22)();
    // VIBE_Command_BeginDeltaPacket(objPtr, objId) — open a delta packet.
    void (*cmdBeginDeltaPacket)(i32 objPtr, i32 objId);
    // VIBE_Command_AppendRawField(size, count, src, off) — append a raw field.
    void (*cmdAppendRawField)(unsigned size, unsigned count, void* src, int off);
    // VIBE_Command_AppendDeltaField(size, count, src, off) — append a delta field.
    void (*cmdAppendDeltaField)(unsigned size, unsigned count, void* src, int off);
    // VIBE_Command_EnqueueCmd15(a, b, amount, flag) — money/transfer command.
    void (*cmdEnqueue15)(i32 a, i32 b, int amount, u8 flag);
    // VIBE_Object_RequestChangeZustand(a, delta, key) — object-state change.
    void (*requestChangeZustand)(i32 a, int delta, i32 key);

    // --- render / narrative ---------------------------------------------
    // VIBE_Text_RenderFormattedMessage(dst, textId, args...) — fill dst with text;
    // exposed with up to four scalar args (the originals pass 1..4).
    void (*renderFormatted)(char* dst, int textId, int a, int b, int c);
    // VIBE_He_SendQuickjumpMessage(recipient, ..., textId) — narrative push (opaque).
    void (*sendQuickjump)(i32 recipient, i32 from, i32 to, const char* body, int textId);
    // VIBE_ErrorLog_ReportMessage(msg) — diagnostic log line.
    void (*reportError)(const char* msg);

    // --- character / mesh (MorphMovementInit) ---------------------------
    // VIBE_Character_ChangePlayerAction(rec, a, b, idx) — switch an NPC's anim/action.
    void (*changePlayerAction)(i32 rec, int a, int b, int idx);
    // VIBE_Character_Destroy(handle) — destroy a pooled character; returns the slot.
    int (*characterDestroy)(i32 handle);
    // VIBE_Anim_FindFreeMeshSlot() — allocate a free mesh slot index.
    int (*animFindFreeMeshSlot)();
    // VIBE_Anim_ReleaseMeshData(slot, ...) — release a mesh slot.
    void (*animReleaseMeshData)(int slot);
    // VIBE_Character_AttachAni(charRec, name, mode, anim) — attach a movement anim;
    // returns the new anim-entry record (null == failed). MorphMovementInit branches
    // on the result and writes its flag bytes at +92/+109/+110. `cart` selects
    // "BewegungKarren" vs "BewegungGehen".
    void* (*attachAni)(i32 charRec, bool cart, int anim);

    // --- globals / counters ---------------------------------------------
    // VIBE_Math_RandomModulo(n) — uniform draw in [0,n). Inert default 0.
    int (*randomModulo)(int n);
    // dword_12CE914[134*cityIndex] — the city's recipient entity id.
    i32 (*cityRecipientId)(u16 cityIndex);
    // byte_12CE912[536*cityIndex] — the city category byte (6/7 == player-relevant).
    u8 (*cityCategory)(u16 cityIndex);
    // dword_66F0D0[i>>2] — the i-th live character-pool slot handle (0 == empty).
    // QueueFreeAll walks the 2048-byte (512 slot) pool.
    i32 (*poolSlot)(int byteIndex);
    // clear the pool slot (write 0) after Destroy.
    void (*poolClear)(int byteIndex);
};
void SetCharActionStep8Hooks(const CharActionStep8Hooks* hooks);
const CharActionStep8Hooks& GetCharActionStep8Hooks();

// ===========================================================================
// The four walk-target globals ResetWalkTarget clears (gilde .data):
//   dword_62D080 / dword_62D084 (set to -1) ; dword_62D0AC / dword_62D0B0 (to 0).
// Exposed so tests can observe the reset. Owned by THIS module.
// ===========================================================================
extern i32 g_walkTargetA;   // dword_62D080
extern i32 g_walkTargetB;   // dword_62D084
extern i32 g_walkTargetC;   // dword_62D0AC
extern i32 g_walkTargetD;   // dword_62D0B0

// ===========================================================================
// Translated step / leaf functions.
// ===========================================================================

// gilde.exe 0x40be0c — VIBE_CharAction_ResetWalkTarget. Clears the four globals.
void ResetWalkTarget();

// gilde.exe 0x40c120 — VIBE_CharAction_QueueFreeAll. Destroys every live pool slot.
i32 QueueFreeAll();

// gilde.exe 0x409200 — VIBE_CharAction_MorphMovementInit(rec@eax, anim@edx).
//   Swap the walk/cart mesh and arm the morph anim. Returns 1 on success (anim
//   attached), else 0 (mesh freed, entry unlinked). NOTE: the Hex-Rays output for
//   this function reuses several SSA temps (v6/v9/v13) that alias the same record
//   base loaded into different registers; we read them through the single record
//   pointer `self` to stay byte-faithful while readable. The opaque mesh/anim ops
//   route through hooks; the 1109393408/1101004800 float bit patterns (3.0f / 20.0f)
//   and dbl_610804/dbl_61080C are preserved verbatim. `anim` is the animation
//   descriptor pointer (edx in the original; +16 holds its base play-rate float).
int MorphMovementInit(HeRecord* self, void* anim);

// gilde.exe 0x4dc374 — VIBE_CharAction_FindGestureTarget(ctx@eax).
//   Proximity-scan the handler pool (filter 1,0,67) for a partner within `radius`.
//   On a hit, stamp ctx.found, queue the gesture commands for each of the up-to-4
//   partner slots, copy the goal city ref, arm an 8-minute wake + cmd29, and return
//   1. Returns 0 when no partner is found.
//   The original ctx is a packed 32-bit record (self ptr @0, radius @4, goal ptr @8,
//   found handler @12); on the host the same four slots are host-width fields below.
struct GestureCtx {
    u8*   self;     // ctx[0] — self person record (its +97 holds the character ptr)
    float radius;   // ctx[1] — match radius
    u8*   goal;     // ctx[2] — goal record (its +4 city ref, +12 gesture id)
    u8*   found;    // ctx[3] — written with the chosen handler on success
};
int FindGestureTarget(GestureCtx* ctx);

// gilde.exe 0x4e3fdc — VIBE_CharAction_InitArrestPerson(h@eax).
//   One-shot arrest setup: roll a 4..6 day wake, resolve the target, bail on dead/
//   immune class (11/12/13), arm the arrest cmd, then (if not a player city) notify
//   the guild masters and (class 5 + non-guild target) seize the target's money.
HeRecord* InitArrestPerson(HeRecord* h);

// gilde.exe 0x4e4204 — VIBE_CharAction_RunArrestPerson(h@eax, edi, target@esi).
//   Arrest coroutine: gate on the in-flight packet; phase machine on +112
//   (states <-2 abort, -2/-1 finish, 0 work). In the working phase, on the 3rd+
//   iteration roll the bribe/escape check (room-worth ratio vs RandomModulo(100));
//   on escape, re-arm the entity packet; bump the iteration, re-stamp the clock and
//   re-arm a (RandomModulo(3)+4)-day wake.
i32 RunArrestPerson(HeRecord* h, int edi, HeRecord* target);

// gilde.exe 0x4e4484 — VIBE_CharAction_InitEscortPrisoner(h@eax, edi, target@esi).
//   One-shot escort setup: resolve the prisoner, store the id (+16), arm the escort
//   cmd, snapshot the clock into the three pose blocks (+96/+82/+68), advance +10
//   minutes, copy the appointment into +184..+196, then advance by the speed-derived
//   minute count. Returns the free-handler result on a failed resolve.
i32 InitEscortPrisoner(HeRecord* h, int edi, HeRecord* target);

// gilde.exe 0x4dffa0 — VIBE_CharAction_RunLagerFuellen(h@eax, edi, esi).
//   "Fill warehouse" coroutine. State (= +112 + 2) machine: phases 0/1 cancel the
//   reservation and free; phase 2 fills toward the threshold (+180), bumping the
//   object state and re-arming a (2*+184)-minute wake; phase 3 announces (text 6230)
//   and finalizes the delta packet.
i32 RunLagerFuellen(HeRecord* h, int edi, int esi);

// gilde.exe 0x4e0d8c — VIBE_CharAction_RunLagerErweitern(h@eax, edi).
//   "Expand warehouse" coroutine. State machine on +112: phase 0 advance; phase 1
//   resolve the object, emit a +1 delta, decrement the remaining count (+176); when
//   it reaches 0 finalize (announce via text 6088 unless the worker is busy) else
//   re-arm a 15-minute wake.
i32 RunLagerErweitern(HeRecord* h, int edi);

// gilde.exe 0x4e0a40 — VIBE_CharAction_RunAdjustObjectField(h@eax, edi, target@esi).
//   Clamp/bump the two object byte fields at +28 (count A budget +176) and +29
//   (count B budget +180) toward the worker's per-record limits, emitting one raw
//   delta per bump; re-arm a 15-minute wake while either budget remains, else
//   announce (text 6087) + free.
i32 RunAdjustObjectField(HeRecord* h, int edi, int esi);

// gilde.exe 0x4e1c78 — VIBE_CharAction_RunPickFromGround(h@eax, edi, esi).
//   Production "pick from ground" gather step. Phase machine on +112 (<-1 abort,
//   -2/-1 finish, 0 work). Working: bail when the field is full or the deadline
//   passed; else compute the produced amount (output*0.1*1.0*(work*0.1+1)) / a
//   field-fill divisor, clamp to the ceiling, emit one raw delta, and re-arm a
//   30-minute wake.
i32 RunPickFromGround(HeRecord* h, u16* edi, char* esi);

// gilde.exe 0x4e17dc — VIBE_CharAction_RunHerdAnimals(h@eax, esi).
//   Herd up to +172 animals. State (= +112 + 2) machine: phases 0/1 reset each
//   animal's action and free; phase 2 issue the named "Getier" move to each + arm a
//   (2 day + RandomModulo(30) min) wake; phase 3 produce goods (cmd17) for one found
//   animal pen, render the combined 5144/5145 message, and finish.
i32 RunHerdAnimals(HeRecord* h, int esi);

} // namespace guild::sim
