#pragma once
#include "guild/common/types.h"
#include "sim/command.h"
#include "sim/types.h"
#include "sim/combat_types.h" // CombatUnit (combat-damage apply target record)
#include "sim/cutscene.h"     // CutsceneTable / CutsceneSlot (cutscene applies)
#include <cstring>            // std::memset / std::memcpy (inline field helpers)
//
// NOTE on combat: we deliberately do NOT include combat.h here. Both combat.h
// and cutscene.h define a `CutsceneRng` type (a struct vs a class with the same
// LCG), which collide if co-included. We only need combat.h's CombatField as a
// 32-slot unit array + linear id lookup, so we model a byte-faithful equivalent
// (CombatUnitField) over the SAME CombatUnit record from combat_types.h. This
// mirrors VIBE_Combat_FindUnitById (0x486430) exactly without the header clash.

// gilde.exe — Command APPLY handlers, THIRD batch (namespace guild::sim).
//
// A third, NON-overlapping set of the ~120 VIBE_Command_Ex* jump-table targets
// (jump table funcs_4941F4 @0x631298, invoked from VIBE_Command_ExecCommands
// @0x494088). Batches 1 and 2 own:
//   batch1 (command_apply):  0x16 0x17 0x18 0x19 0x1A 0x24 0x25 0x41 0x56 0x5A
//                            0x5B 0x5D
//   batch2 (command_apply2): 0x0D 0x0E 0x0F 0x10 0x1D 0x2B 0x3F 0x44 0x45 0x46
//                            0x5C 0x5E
// This file owns a DIFFERENT, coherent set — the char-action / walk / sound,
// cutscene, combat and select/chat opcodes — and registers ONLY those, so the
// three registries compose without clobbering each other:
//
//   char-action / walk / sound (all character/render-coupled -> leaf hooks)
//   0x2E ExCharPlaySample        49 98E4   queue a "sp_%i" sample action (45)
//   0x2F ExChrWalkToDummy        49 9950   walk a char to a dummy object/building
//   0x30 ExCharUseObject         49 9B28   queue use-object action chain
//   0x31 ExCharStandUp           49 9DC4   stand the char up
//   0x32 ExCharSetVisibility     49 9E1C   apply visibility state
//   0x33 ExCharQueueAction       49 9E84   queue action (type 56) w/ arg
//   0x34 ExCharSpawnAtEntrance   49 9EEC   spawn char at building entrance
//   0x35 ExChrGotoBuilding       49 9F24   walk char to a building (huge)
//   0x36 ExCharUseGate           49 AC6C   queue use-gate action between rooms
//   0x37 ExCharPlaySound         49 ADF4   queue a sound action
//   0x3E ExCharApplyInteraction  49 B8D8   swap a person's avatar mesh
//   0x43 ExTriggerCharacterAction 49 BD3C  attach a person's building storage
//   0x48 ExApplyCharacterUpdate  49 BE74   update occupant category + head bone
//   0x49 ExSpawnAndPlaceCharacter 49 BEC4  create+spawn a person (sets lastObj id)
//   0x4A ExDeselectObject        49 C094   detach+destroy a building occupant
//
//   cutscene (operate on the global CutsceneTable)
//   0x27 ExAllocCutsceneWithId   49 93A4   alloc a cutscene slot, set secondary id
//   0x28 ExAllocCutscene         49 947C   alloc a cutscene slot
//   0x29 ExSetCutsceneState      49 94E8   add participant to a slot
//   0x2A ExClearCutsceneState    49 9520   remove participant from a slot
//   0x59 ExCutsceneReady         49 CFC0   stamp the cutscene id onto participants
//   0x5F ExSetObjectField        49 D3F0   write a cutscene slot's master field
//
//   combat (operate on the global CombatField / combat-slot tables)
//   0x3D ExEquipCombatObject     49 B65C   spawn/equip a combat unit (huge)
//   0x50 ExStoreCombatSlot       49 C754   store a 0x2C combat-order slot
//   0x51 ExUpdateCombatSlot      49 C824   update a 0x2C combat-order slot
//   0x52 ExApplyCombatDamage     49 C8EC   subtract HP from a unit, set its flag
//   0x55 ExDispatchUnitOrder     49 CDA0   dispatch a combat unit order (switch)
//
//   select / chat
//   0x4B ExAppendChatLine        49 C0C0   append a chat line to the chat buffer
//
// Common ABI (recovered from the originals, all __usercall):
//   * packet record base in EAX -> CommandPacket&. Payload begins at +0x10. The
//     character handlers format "sp_%i" from the dword at +0x10 to find the
//     target actor by name (VIBE_Character_FindByPredicate).
//   * the 10-byte ACK/status entry -> AckEntry* (may be null). Handlers stamp it:
//     status 2 = in-progress, 1 = applied; +1 = sub-tag (0/7/8/9), +6 = result
//     ptr/id. We keep the exact sub-tags the originals write.
//   * return 0 == applied, nonzero == target not found / rejected (the dispatcher
//     ignores the value; the ACK byte carries the outcome).
//
// The -2/-3/-4 "last-created object/scene/trade id" remap tokens
// (dword_631288/63128C/631290) are the SAME globals batch 1 exposes
// (g_lastObjectId / g_lastSceneId / g_lastTradeId in command_apply.h); we reuse
// them. ExSpawnAndPlaceCharacter writes g_lastObjectId, mirroring dword_631288.
//
// Deeply-coupled leaves (character actor system, render/scene, path/heightmap,
// combat presentation, person create/spawn) are NOT present in src/. They are
// reached through mockable function-pointer hooks declared below with faithful
// default backends that model just the observable record mutation. The full leaf
// functions are listed as DEFERRED in the module report.

namespace guild::sim {

// ---------------------------------------------------------------------------
// Opcode -> handler constants for THIS batch (jump-table indices @0x631298).
// ---------------------------------------------------------------------------
enum ApplyOpcode3 : u8 {
    kOp3AllocCutsceneWithId   = 0x27, // 39
    kOp3AllocCutscene         = 0x28, // 40
    kOp3SetCutsceneState      = 0x29, // 41
    kOp3ClearCutsceneState    = 0x2A, // 42
    kOp3CharPlaySample        = 0x2E, // 46
    kOp3ChrWalkToDummy        = 0x2F, // 47
    kOp3CharUseObject         = 0x30, // 48
    kOp3CharStandUp           = 0x31, // 49
    kOp3CharSetVisibility     = 0x32, // 50
    kOp3CharQueueAction       = 0x33, // 51
    kOp3CharSpawnAtEntrance   = 0x34, // 52
    kOp3ChrGotoBuilding       = 0x35, // 53
    kOp3CharUseGate           = 0x36, // 54
    kOp3CharPlaySound         = 0x37, // 55
    kOp3EquipCombatObject     = 0x3D, // 61
    kOp3CharApplyInteraction  = 0x3E, // 62
    kOp3TriggerCharacterAction= 0x43, // 67
    kOp3ApplyCharacterUpdate  = 0x48, // 72
    kOp3SpawnAndPlaceCharacter= 0x49, // 73
    kOp3DeselectObject        = 0x4A, // 74
    kOp3AppendChatLine        = 0x4B, // 75
    kOp3StoreCombatSlot       = 0x50, // 80
    kOp3UpdateCombatSlot      = 0x51, // 81
    kOp3ApplyCombatDamage     = 0x52, // 82
    kOp3DispatchUnitOrder     = 0x55, // 85
    kOp3CutsceneReady         = 0x59, // 89
    kOp3SetObjectField        = 0x5F, // 95
};

// ===========================================================================
// Person cutscene-id field (ExCutsceneReady).
//   The original writes RecordById[130] (dword index 130 == byte +520) — the
//   per-person "active cutscene id" — and reads kind byte at +2.
// ===========================================================================
constexpr int kPfCutsceneId = 520;   // dword index 130 into the 536-byte record
constexpr int kPfKindByte   = 2;     // byte +2 (kind/class)

// ===========================================================================
// Combat-unit field (gilde.exe word_B5A350 @0xB5A350, stride 536, 32 slots).
// A byte-faithful equivalent of combat.h's CombatField, kept here to avoid the
// CutsceneRng header clash (see the note above). Same flat array + linear scan.
// ===========================================================================
class CombatUnitField {
public:
    CombatUnitField() { Clear(); }
    void Clear() {
        for (auto& u : units_) { std::memset(&u, 0, sizeof(u)); u.marker = -1; }
    }
    CombatUnit* units() { return units_; }
    int capacity() const { return kUnitCapacity; }

    // gilde.exe 0x486430 — VIBE_Combat_FindUnitById (linear scan, bound 17152).
    // Occupied (marker != -1) slot whose id column equals `id`, else nullptr.
    CombatUnit* FindUnitById(i32 id) {
        for (int i = 0; i < kUnitCapacity; ++i)
            if (units_[i].marker != -1 && ids_[i] == id)
                return &units_[i];
        return nullptr;
    }
    // Test/setup helper: claim the first free slot, set marker/id/hp/alive.
    CombatUnit* Spawn(i32 id, i32 hp, i32 teamId) {
        for (int i = 0; i < kUnitCapacity; ++i) {
            if (units_[i].marker == -1) {
                std::memset(&units_[i], 0, sizeof(CombatUnit));
                units_[i].marker = static_cast<i16>(id);
                units_[i].id = id;
                units_[i].alive = 1;
                units_[i].hp = hp;
                units_[i].teamId = teamId;
                ids_[i] = id;
                return &units_[i];
            }
        }
        return nullptr;
    }

private:
    CombatUnit units_[kUnitCapacity];
    i32        ids_[kUnitCapacity] = {}; // dword_B5A354 id column
};

// ===========================================================================
// Module-global engine tables the originals mutate (the game's file globals).
// We hold them here as the third batch's owned state, mirroring how batch 2
// holds its modeled leaf tables. Tests seed them via the accessors below.
//   g_cutsceneTable : the 96-slot cutscene table (dword_11AE6B0).
//   g_combatField   : the 32-slot combat-unit array (word_B5A350).
// ===========================================================================
CutsceneTable&   Apply3_Cutscenes();  // the global cutscene table
CombatUnitField& Apply3_Combat();     // the global combat-unit field

// The chat ring buffer (byte_11B6BA0). The original appends formatted chat
// lines here when the message targets the local player; we model the buffer and
// the "active tick" stamp (dword_631E88 <- dword_62EB38).
constexpr int kChatBufferBytes = 512;
const char* Apply3_ChatBuffer();      // read the accumulated chat text
void        Apply3_ClearChat();       // reset the chat buffer

// The local-player person id (dword_12CE914[134 * word_63CC5C]). The chat /
// cutscene-alloc handlers compare the target id against this to decide whether
// the command applies locally. Default -1 (no local player); tests set it.
void Apply3_SetLocalPlayerId(i32 id);
i32  Apply3_LocalPlayerId();

// dword_764CE0 == -1 standalone flag mirror (ExSpawnAndPlaceCharacter / the
// combat-slot handlers branch on it). Default: standalone (-1).
void Apply3_SetStandalone(bool v);

// ===========================================================================
// Modeled leaf hooks (deferred character/render/combat/person-create leaves).
// Default backends model only the record mutation the apply path performs.
// ===========================================================================

// VIBE_Character_FindByPredicate @0x402314 — find the live actor whose name is
// "sp_<id>". Returns an opaque non-null handle on hit, null on miss. Default:
// returns a sentinel non-null for any id the test has "spawned" (see the
// modeled actor set), else null.
using CharFindFn = void* (*)(i32 spId);
void SetCharFindHook(CharFindFn fn);

// A faithful-enough observable record of the character-action mutations the
// delegating handlers perform. The originals queue action nodes / attach meshes
// through the render bridge; we record the effect so a test can verify which
// command ran and on which actor.
struct CharActionLog {
    i32  lastActorSp;     // sp id of the last actor a handler acted on (-1 none)
    i32  lastActionType;  // last action-type byte queued (e.g. 45/56/57/0x2D)
    i32  lastArg;         // the primary action arg (sample id / object id / flag)
    int  standUpCount;    // ExCharStandUp invocations
    int  visibilityCount; // ExCharSetVisibility invocations
    int  soundCount;      // ExCharPlaySound invocations
    int  interactionCount;// ExCharApplyInteraction invocations
    int  triggerCount;    // ExTriggerCharacterAction invocations
    int  charUpdateCount; // ExApplyCharacterUpdate invocations
    int  spawnCount;      // ExSpawnAndPlaceCharacter invocations
    int  deselectCount;   // ExDeselectObject invocations
    int  equipCount;      // ExEquipCombatObject invocations
    i32  lastOrderKind;   // ExDispatchUnitOrder: last order code (1..7)
};
const CharActionLog& Apply3_CharLog();
void                 Apply3_ResetCharLog();

// VIBE_Person_CreateAndSpawn @0x58da70 — create a new person of the requested
// kind and return its (new) entity id, or -1 on failure. Default: returns a
// synthetic incrementing id and records it as g_lastObjectId. Tests can install
// a spy or a failure backend.
using PersonCreateFn = i32 (*)(u8 kind, u8 a, u8 b);
void SetPersonCreateHook(PersonCreateFn fn);

// VIBE_Object_FindObjectById @0x583a70 — resolve an object record by id; returns
// nonzero on hit. The deselect / walk handlers gate on it. Default: delegates to
// BuildingFindById (the object/building share one array).
using ObjectFindFn = int (*)(i32 id);
void SetObjectFindHook(ObjectFindFn fn);

// VIBE_Command_CheckTargetNotInUse @0x4963ec — returns nonzero if the cutscene
// target is busy (ExCutsceneReady rejects with status 2). Default: returns 0
// (target free). Tests can force a "busy" rejection.
using TargetBusyFn = int (*)(const CommandPacket& pkt);
void SetTargetBusyHook(TargetBusyFn fn);

// Combat-order slot store: the originals copy a 0x2C-byte order record into the
// per-squad slot tables (dword_11AB000 / dword_11AB010). The slot tables are a
// deferred combat-mode subsystem; we model the write through a hook so a test
// can observe the stored bytes. Returns nonzero if the slot was written.
//   squadId : the packet's owner/cmd id (+4). unitId : the order's unit (+5 / +53).
//   record  : pointer to the 0x2C payload bytes. update : false=store, true=update.
using CombatSlotStoreFn = int (*)(i32 squadId, i32 unitId, const u8* record, bool update);
void SetCombatSlotStoreHook(CombatSlotStoreFn fn);

// Reset every modeled table + hook + the local-player/standalone state to its
// default backend (test helper).
void ResetApply3State();

// ===========================================================================
// Handlers. Each takes the received packet and the dispatcher ACK (may be null).
// ===========================================================================

// --- cutscene ---------------------------------------------------------------

// gilde.exe 0x4993A4 — opcode 0x27. Alloc a cutscene slot (template), in
// networked mode seed the active-count from payload +16; set slot field [30]
// (byte +120) from payload +20. ack +0=1,+1=9,+6=slot.
int ExAllocCutsceneWithId(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x49947C — opcode 0x28. Alloc a cutscene slot from the template.
// ack +0=1,+1=9,+6=slot. Returns 1 if the table is full.
int ExAllocCutscene(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x4994E8 — opcode 0x29. Find slot by id (+16); add participant
// (person +20). ack +0=1. Returns 1 if the slot is missing / add failed.
int ExSetCutsceneState(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x499520 — opcode 0x2A. Find slot by id (+16); remove participant.
// ack +0=1. Returns 1 if the slot is missing.
int ExClearCutsceneState(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x49CFC0 — opcode 0x59. If the target is in use, reject (ack +0=2).
// Else find the slot by id (+16) and, for each of the 4 participant ids
// (payload +20/+24/+28/+32), stamp the person's cutscene-id field (+520) with
// the slot id when not already set / when not kind 6/7. ack +0=1.
int ExCutsceneReady(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x49D3F0 — opcode 0x5F. Find slot by id (+16); write its master
// field (dword index 3, byte +12) from payload +20. ack +0=2 then 1.
int ExSetObjectField(CommandPacket& pkt, AckEntry* ack);

// --- character / walk / sound (delegating) ----------------------------------

// gilde.exe 0x4998E4 — opcode 0x2E. Find actor "sp_<+16>"; queue a sample
// action (type 45 / 0x2D) with args +20/+24/+28. ack +0=2. Returns find result.
int ExCharPlaySample(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x499950 — opcode 0x2F. Walk the actor to a dummy object/building.
// ack +0=2. Returns 1 (no actor) / 2 (no universe) / 3 (busy) / 4 / 0.
int ExChrWalkToDummy(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x499B28 — opcode 0x30. Queue the use-object action chain on the
// actor. ack +0=2. Returns 1/2/3 on the resolve failures.
int ExCharUseObject(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x499DC4 — opcode 0x31. Stand the actor up. ack +0=2. Returns 2 if
// the actor is missing.
int ExCharStandUp(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x499E1C — opcode 0x32. Apply the actor's visibility state. ack
// +0=2. Returns 1 if the actor is missing.
int ExCharSetVisibility(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x499E84 — opcode 0x33. Queue action (type 56 / 0x38) with arg +20.
// ack +0=2. Returns 1 if the actor is missing.
int ExCharQueueAction(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x499EEC — opcode 0x34. Spawn the actor at a building entrance
// (building +20, entry +24). ack +0=2,+1=7,+6=actor. Returns 1 on failure.
int ExCharSpawnAtEntrance(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x499F24 — opcode 0x35. Walk the actor to a building (the large
// path/room handler). ack +0=1,+1=7,+6=actor. Returns 1 if the actor is missing.
int ExChrGotoBuilding(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x49AC6C — opcode 0x36. Queue the use-gate action between rooms.
// ack +0=2. Returns 1 if the actor is missing.
int ExCharUseGate(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x49ADF4 — opcode 0x37. Queue a sound action (sound id at +68).
// ack +0=2. Returns 2 if the actor is missing.
int ExCharPlaySound(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x49B8D8 — opcode 0x3E. Swap a person's avatar mesh (interaction).
// ack +0=2 then 1. Returns 1 if the person/avatar is missing.
int ExCharApplyInteraction(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x49BD3C — opcode 0x43. Attach a person's building storage rooms.
// ack +0=1. Always returns 0.
int ExTriggerCharacterAction(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x49BE74 — opcode 0x48. Update the person's occupant category and
// resolve its head bone. ack +0=1. Returns 1 if the person is missing.
int ExApplyCharacterUpdate(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x49BEC4 — opcode 0x49. Create+spawn a person (kind = HIBYTE(+17)),
// set g_lastObjectId to its id, spawn it at a building entrance. ack +0=1,+1=1,
// +6=record. Returns 1 on failure.
int ExSpawnAndPlaceCharacter(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x49C094 — opcode 0x4A. Resolve an object by id (+16); detach and
// destroy its building occupant. ack +0=1. Returns 1 if the object is missing.
int ExDeselectObject(CommandPacket& pkt, AckEntry* ack);

// --- select / chat ----------------------------------------------------------

// gilde.exe 0x49C0C0 — opcode 0x4B. If the message targets the local player (any
// of the 8 ids at +16..+44 equals the local player id) OR the ack is non-null,
// append the text at payload +48 to the chat buffer and stamp the active tick.
// ack +0=1. Always returns 0.
int ExAppendChatLine(CommandPacket& pkt, AckEntry* ack);

// --- combat -----------------------------------------------------------------

// gilde.exe 0x49B65C — opcode 0x3D. Spawn / equip a combat unit (the large
// equip handler). ack +0/+1 per outcome, +6=unit. Returns 0 on apply, 1/2 on
// the person/target resolve failures.
int ExEquipCombatObject(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x49C754 — opcode 0x50. Store a 0x2C-byte combat-order slot for the
// squad. ack +0=1,+1=8. Returns 1 if the packet is not for the local battle.
int ExStoreCombatSlot(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x49C824 — opcode 0x51. Update a 0x2C-byte combat-order slot.
// ack +0=1,+1=8. Returns 1 if the packet is not for the local battle.
int ExUpdateCombatSlot(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x49C8EC — opcode 0x52. Find the combat unit by id (+16); subtract
// HP (payload +20) from unit+36 and copy the flag byte (payload +24) to unit+8.
// ack +0=1,+1=8. Always returns 0 (a missing unit is tolerated).
int ExApplyCombatDamage(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x49CDA0 — opcode 0x55. Dispatch a combat unit order: switch on the
// order byte (+20) -> celebrate/attack/conquer/pickup/standup/sound, gated on
// the unit's alive byte. ack +0=1,+1=8. Returns 1 if not the local battle.
int ExDispatchUnitOrder(CommandPacket& pkt, AckEntry* ack);

// ---------------------------------------------------------------------------
// Registry. Adds ONLY this batch's opcodes to a CommandQueue dispatch table —
// call it IN ADDITION TO RegisterApplyHandlers (batch 1) and
// RegisterApplyHandlers2 (batch 2); the three sets are disjoint.
// ---------------------------------------------------------------------------
void RegisterApplyHandlers3(CommandQueue& q);

// Apply a single packet directly (bypassing the queue) for this batch's
// opcodes. Unknown/guarded opcodes return -1 without touching state.
int ApplyPacket3(CommandPacket& pkt, AckEntry* ack);

} // namespace guild::sim
