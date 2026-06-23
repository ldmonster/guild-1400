#pragma once
// gilde.exe — Command-queue + char-action + turn-tick + city-stats LEAVES
// (namespace guild::sim).  Wave-20 cluster: the small, entry-reachable leaves
// that sit between the lockstep command codec (sim/command.{h,cpp}) and the
// order/AI front-ends.  Each function is translated 1:1 from the decompile;
// where the original operates on raw runtime sim memory (the 16-slot order
// roster, the per-character action queue, the city-stats ACK block) we model the
// exact byte arithmetic over a flat buffer / typed view so the math is faithful
// AND unit-testable without the whole sim.
//
//   VIBE_Command_FindOrAllocSlot            0x485f7c — find/alloc the 44-byte
//       order slot keyed by *(target+4) in the owner's 16-slot roster (+0x94).
//   VIBE_Command_QueueRequestGuardTarget61  0x49514c — assemble an opcode-61
//       (guard) command packet from a squad-slot + nearest-enemy lookup.
//   VIBE_CharAction_InsertActionArgs        0x40c2f0 — build+enqueue an action
//       node whose type/chained-fn/ready/args come from the packed __int64 arg.
//   VIBE_GameTick_HandleTurnControlCommand  0x5799a8 — the 'enbl'/'dsbl'/'init'/
//       'set ' turn-control state machine over the 36-byte turn block.
//   VIBE_City_ApplyStatsFromAck             0x5792e0 — copy a city-stats ACK
//       block into the live city aggregates and recompute the 6 averaged bytes
//       (ConvertX truncation, fixed divisor 1/count).
//
// The CommandQueue / OrderStage / RequestBuildOp* sink are reused from
// sim/command.h and sim/combat_packets.h (extern); the action-queue node pool,
// registry, and QueueInsertEntry are reused from sim/charaction.h.
#include <array>
#include <cstring>
#include <functional>
#include <vector>

#include "guild/common/types.h"
#include "sim/charaction.h"  // ActionNode / Character / ActionStepFn / registry
#include "sim/command.h"

namespace guild::sim {

// ===========================================================================
// 0x40c2f0 — VIBE_CharAction_InsertActionArgs
// ===========================================================================
// Build + enqueue an action node onto `ch`'s queue.  The packed __int64 `packed`
// carries the action-type id in BYTE4 ((packed>>32)&0xFF) and the ready flag in
// its low byte; `chained` is the +4 chained-fn (a2); `args` supplies the variadic
// arg slots (registry argCount of them, landing at node+48+4*i).  Returns the
// node, or null on bad ch / pool exhaustion.  Reuses QueueInsertEntry +
// ActionType + RunActionOrFree from sim/charaction.{h,cpp}.
ActionNode* CharActionInsertActionArgs(Character* ch, ActionStepFn chained,
                                       u64 packed, const i32* args);

// ===========================================================================
// 0x485f7c — VIBE_Command_FindOrAllocSlot   (__usercall, eax=ownerBase, edx=keyRec)
// ===========================================================================
// The owner's order roster is 16 slots of 44 bytes starting at ownerBase+0x94
// (148).  Each slot's first dword (roster offset 0, i.e. ownerBase+148+44*i)
// holds the key; an unused slot's key is -1.  The lookup key is *(keyRec+4).
//
//   for i in 0..15:  if slot[i].key == *(keyRec+4)  return &slot[i]   // reuse
//   for i in 0..15:  if slot[i].key == -1  -> alloc:                  // empty
//       zero 44 bytes of slot[i]; slot[i].claimedByte(+4)=0; slot[i].key=*(keyRec+4)
//       return &slot[i]
//   return null    // roster full
//
// The original returns the absolute address ownerBase+148+44*i; the alloc path's
// VIBE_Light_SetGrayColorThunk(0,44,slot) zero-fills the 44 bytes, then writes
// slot[+4]=0 (a claimed-state byte) and slot[+0]=key.
//
// SlotRoster wraps the 16*44-byte buffer; FindOrAllocSlot returns the slot index
// (0..15) or -1 when full.  The byte semantics (key at +0, the +4 claimed byte,
// the freshly-zeroed body) are reproduced exactly so the staging copy that
// follows (combat_packets BuildSimplePacket etc.) starts from the same bytes.
constexpr int kOrderSlotBytes  = 44;   // 0x2C
constexpr int kOrderSlotCount  = 16;
constexpr u32 kOrderSlotKeyOff = 0;    // roster-relative key dword (ownerBase+148)
constexpr u32 kOrderSlotClaimedOff = 4; // the +0x98 byte cleared on alloc

struct SlotRoster {
    // 16 * 44 bytes; slot i begins at bytes[44*i].  Mirrors ownerBase+0x94.
    u8 bytes[kOrderSlotBytes * kOrderSlotCount] = {};

    SlotRoster() { reset(); }
    void reset() {                       // all keys = -1 (free)
        std::memset(bytes, 0, sizeof(bytes));
        for (int i = 0; i < kOrderSlotCount; ++i)
            put_key(i, -1);
    }
    i32  key(int slot) const { return get32(static_cast<u32>(44 * slot) + kOrderSlotKeyOff); }
    void put_key(int slot, i32 v) { put32(static_cast<u32>(44 * slot) + kOrderSlotKeyOff, v); }
    u8&  slot_byte(int slot, u32 off) { return bytes[44 * slot + off]; }
    u8*  slot_ptr(int slot) { return bytes + 44 * slot; }

    void put32(u32 off, i32 v) {
        bytes[off]     = static_cast<u8>(v);
        bytes[off + 1] = static_cast<u8>(v >> 8);
        bytes[off + 2] = static_cast<u8>(v >> 16);
        bytes[off + 3] = static_cast<u8>(v >> 24);
    }
    i32 get32(u32 off) const {
        return static_cast<i32>(static_cast<u32>(bytes[off])
             | (static_cast<u32>(bytes[off + 1]) << 8)
             | (static_cast<u32>(bytes[off + 2]) << 16)
             | (static_cast<u32>(bytes[off + 3]) << 24));
    }
};

// Returns the slot index (0..15) that matches/was allocated for `key`, or -1 if
// the roster is full.  `key` is the original's *(keyRec+4).
int CommandFindOrAllocSlot(SlotRoster& roster, i32 key);

// ===========================================================================
// 0x49514c — VIBE_Command_QueueRequestGuardTarget61  (__usercall)
//   eax=ownerHandle, dl=mode(a2), cl=arg(a3), bl=arg(a4)
// ===========================================================================
// Assembles an opcode-61 guard-order command and enqueues it.  The original:
//   if (!ownerHandle) return -1;
//   slot   = VIBE_Combat_FindAvailableSquadSlot(ownerHandle, a4, a3);
//   v9     = *(ownerHandle + 1);                       // owner id (unaligned +1)
//   v12    = slot ? *(slot+1 dword) : -1;              // squad id
//   if (!slot || (slot && !*(slot+92 dword)))          // slot empty/underfull
//       enemy = VIBE_Combat_FindNearestEnemyTarget(ownerHandle, ownerHandle);
//   v10    = enemy ? *(enemy+1) : -1;                  // enemy id
//   packet[0]=61; v9; v10; v11=a2; v12; v13=a4; v14=a3;
//   return VIBE_Command_EnqueuePacket(packet);
//
// FindAvailableSquadSlot / FindNearestEnemyTarget walk raw sim arrays; we model
// their *results* (the ids + the underfull flag) via a small inputs struct so the
// packet-assembly RULE is reconstructed 1:1 and golden-pinnable.  The squad/enemy
// scans themselves are already reconstructed in sim/combat_slots*.cpp.
struct GuardTargetInputs {
    bool hasOwner   = true;   // ownerHandle != null
    i32  ownerId    = -1;     // *(ownerHandle + 1)
    bool hasSquad   = false;  // FindAvailableSquadSlot returned a slot
    i32  squadId    = -1;     // *(slot + 1 dword)
    bool squadFull  = false;  // *(slot + 92 dword) != 0  (slot already underfull-filled)
    bool hasEnemy   = false;  // FindNearestEnemyTarget returned a record
    i32  enemyId    = -1;     // *(enemy + 1)
};

// Returns the enqueued packet's ring id, or -1 if ownerHandle was null.
i32 CommandQueueRequestGuardTarget61(CommandQueue& q, const GuardTargetInputs& in,
                                     u8 mode, u8 a3, u8 a4);

// ===========================================================================
// 0x5799a8 — VIBE_GameTick_HandleTurnControlCommand  (__usercall, eax=packet)
// ===========================================================================
// Reads the 4-char tag at packet+36 (*((u32*)a1 + 9)) and drives the turn-control
// latch dword_1235238 + the 36-byte turn block at 0x1235238:
//   'enbl' (0x656E626C) -> g_turnEnabled = 1;            return 1
//   'dsbl' (0x6473626C) -> g_turnEnabled = 0;            return 1
//   'init' (0x696E6974) -> memcpy(g_turnBlock, packet, 36); return 1
//   'set ' (0x73657420) -> memcpy(g_turnBlock, packet, 36); return 1
//   else                                                 return 0
// (The memcpy overwrites the latch dword too — it is g_turnBlock[0].)
//
// The 36-byte turn block + latch are file globals in the original; we surface them
// as a small struct so the state machine is testable.  The packet is the 153-byte
// command record (first 36 bytes copied for init/set).
struct TurnControlState {
    std::array<u8, 36> block{};   // dword_1235238 .. +35
    i32  latch() const {          // dword_1235238 (== block[0..3])
        return static_cast<i32>(static_cast<u32>(block[0])
             | (static_cast<u32>(block[1]) << 8)
             | (static_cast<u32>(block[2]) << 16)
             | (static_cast<u32>(block[3]) << 24));
    }
    void set_latch(i32 v) {
        block[0] = static_cast<u8>(v);
        block[1] = static_cast<u8>(v >> 8);
        block[2] = static_cast<u8>(v >> 16);
        block[3] = static_cast<u8>(v >> 24);
    }
};

// 4-char turn-control tags (little-endian dwords as the original compares them).
enum TurnControlTag : u32 {
    kTagEnable  = 0x656E626Cu,  // 'enbl'
    kTagDisable = 0x6473626Cu,  // 'dsbl'
    kTagInit    = 0x696E6974u,  // 'init'
    kTagSet     = 0x73657420u,  // 'set '
};

// Drives `st` from the packet's +36 tag.  Returns 1 if the tag was handled, 0 if
// it was unrecognised (the original's two no-match return-0 paths).
int GameTickHandleTurnControlCommand(TurnControlState& st, const CommandPacket& packet);

// ===========================================================================
// 0x5792e0 — VIBE_City_ApplyStatsFromAck   (__usercall, eax=ackBlock)
// ===========================================================================
// Copies a city-stats ACK block into the live city aggregates, then walks the
// 768-entry person array accumulating 5 satisfaction sums + a need sum, and
// finally averages each by the active-person count via ConvertX (truncation).
//
// The first 40 bytes of the ACK block become dword_1234910..flt_1235234 (10
// dwords/floats), and a trailing 14-byte tag (qword + dword + word) becomes
// qword_1235262 .. unk_123526E.  flt_641DA8 (the cap divisor) and flt_641DAC are
// re-seeded from two of the copied fields.  Then:
//   count=0; needSum=0; sat[0..4]={0}
//   for each active person (id != -1, status < 10, alive byte set):
//       count++; need = person.needByte;
//       for j in 0..4: needSum += need; sat[j] += person.satByte[j]
//   for j in 0..4: out[j] = (u8) trunc( sat[j] * (1.0/count) )
//   out[5] = (u8) trunc( count * (1.0/count) )    // == 1 (faithful quirk: uses needSum-less count*recip)
//   VIBE_Statistics_BuildEconomyReport()
//
// The person array + globals are runtime sim state; we model the apply over a
// typed view (the 10 copied fields, the trailing tag, and the per-person sat/need
// bytes) and return the 6 computed averaged bytes so the math is golden-pinnable.
// flt_641DA8 (the cap divisor reused by economy_quality) IS written through to
// world/city.cpp's g_capDivisor.
//
// The 40-byte head of the ACK block as 10 little-endian dwords; field 6 (index 6,
// byte +24) is the float that re-seeds the cap divisor; field 5 (+20) re-seeds
// flt_641DAC.  (Matches the decompile's field shuffles.)
struct CityStatsAck {
    std::array<u32, 10> head{};      // ackBlock[0..39] : 10 dwords/floats
    std::array<u8, 14>  tag{};       // ackBlock[44..57]: qword+dword+word tail
    // The per-person contributions (already filtered to the active set).  Each
    // active person carries one need byte and 5 satisfaction bytes.
    struct Person {
        u8 needByte = 0;             // byte_12CE91D[i]
        std::array<u8, 5> sat{};     // byte_12CE98F[i + 0..4]
    };
    std::vector<Person> persons;
};

struct CityStatsResult {
    std::array<u8, 5> averaged{};    // byte_123525B[0..4]
    u8 overall = 0;                  // byte_1235261
    int activeCount = 0;             // the divisor (v3)
    float capDivisor = 0.0f;         // flt_641DA8 written through
};

CityStatsResult CityApplyStatsFromAck(const CityStatsAck& ack);

// ===========================================================================
// 0x4ac0c8 — VIBE_Cutscene_CheckMaster   (__usercall, eax=slot)
// ===========================================================================
// Validate / resolve the "master" of a cutscene slot, broadcasting a master-
// change command (op95) and blocking until it acks when the master must change.
//
//   if ( ActorHasParticipant(slot.master, slot) ) return 1;     // master present
//   rec = FindRecordById(slot.master);                          // current master rec
//   keep = 1;
//   for ( edx = 0; edx < 16 && keep; ++edx ) {                  // scan participants
//       pid = slot.participant[edx].id;   (a1[edx]+0x34)
//       if ( pid == -1 ) continue;
//       r = FindRecordById(pid);   if ( !r ) continue;
//       rec = r;                                                 // edi tracks last rec
//       kind = r.kind;  (r+2)
//       if ( kind == 6 || kind == 7 ) keep = 0;                 // found new master
//   }
//   if ( !rec ) return 1;                                       // nothing resolvable
//   if ( rec.ownerId == slot.master ) return 1;  (rec+4)        // unchanged
//   id = RequestBuildOp95(slot, rec);                           // broadcast change
//   while ( !GetPacketStatusById(id) ) Amt_RefreshGuildState(); // block until ack
//   return 0;
//
// `rec` (edi) holds the LAST successfully looked-up participant record (or the
// current-master record if the loop resolved none); the loop STOPS the moment it
// hits a kind-6/7 participant, so that participant becomes the candidate master.
//
// The participant scan + record lookups are runtime sim; modeled via a typed view.
// The op95 broadcast + block is a host round-trip (it mutates the lockstep
// command channel and spins the guild-state pump) — routed through a callback so
// the decision logic is reconstructed 1:1 and testable.
struct CutsceneMasterRecord {
    i32 id      = -1;   // the participant id (slot.participant[i].id, +0x34)
    u8  kind    = 0;    // record kind byte (rec+2): 6/7 == player-class master
    i32 ownerId = -1;   // record owner id (rec+4) — compared against slot.master
    bool valid  = false; // FindRecordById returned a record
};

struct CutsceneMasterInputs {
    i32 master = -1;                 // slot[3] (+0x0C) — current master id
    bool masterIsParticipant = false; // ActorHasParticipant(master, slot)
    bool masterRecValid = false;     // FindRecordById(master) != null
    i32  masterRecOwnerId = -1;      // (masterRec+4)
    // Up to 16 participant slots (slot.participant[0..15].id at a1[i]+0x34).
    // Entries with id == -1 are skipped (as the original does); resolved records
    // carry their kind + owner id.  Order matters (the loop tracks the LAST rec
    // and stops at the first kind-6/7 hit).
    std::array<CutsceneMasterRecord, 16> participants{};
};

// `broadcastMaster` performs RequestBuildOp95 + the ack spin (returns when acked);
// it is only invoked when a master change is required.  Returns 1 if the master is
// valid/unchanged (no broadcast), or 0 if a master change was broadcast.
int CutsceneCheckMaster(const CutsceneMasterInputs& in,
                        const std::function<void(i32 newMasterOwnerId)>& broadcastMaster);

// ===========================================================================
// 0x41e814 — VIBE_Gfx_CrossFadeStep   (__usercall, eax=fade)
// ===========================================================================
// Advance one cross-fade descriptor by 8 alpha units per frame.  While alpha
// <= 255 it re-arms the blend (SetFadeParams); once alpha > 255 it blits the
// captured "from" surface rows over the live back buffer; after enough frames
// (frameCounter > 288) it tears the descriptor down (frees both surfaces + the
// widget) and clears the global active-fade pointer.
//
//   if ( !fade || !fade.surfFrom ) return result;       // *(fade) == surfFrom
//   fade.alpha += 8;        (fade+28, dword index 7 — alpha AND the teardown gate)
//   if ( fade.alpha <= 255 )
//       SetFadeParams(fade.w(+16), fade.srcStride(+4), fade.h(+20), fade.alpha); // re-arm
//   else
//       for ( row = 0; row < fade.rows(+20); ++row )    // blit captured rows
//           memcpy(backbuf + 2*(x + screenW*(row + y)), fade.src + 2*row*stride, 2*stride);
//   if ( fade.alpha(v3[7] == +28) > 288 ) {             // teardown gate (same field)
//       free(fade.surfFrom);   free(fade.surfTo(+4));
//       Widget_DestroyByType(fade.widget(+24));
//       free(fade);  g_activeFade(dword_69FF94) = 0;
//   }
//   return result;
//
// NB. +28 (v3[7]) is BOTH the running alpha and the teardown gate: the blit runs
// while alpha is in (255, 288]; teardown fires once alpha > 288.  The surface blit
// + the SetFadeParams re-arm + the widget/heap frees are the swapped-tech (Vulkan/
// SDL) boundary — routed through a small ops interface so the STEP SEQUENCING +
// the alpha arithmetic + the teardown gate are reconstructed 1:1.
struct CrossFadeDesc {
    bool active = false;          // *(fade) != 0  (surfFrom present)
    i32  alpha = 0;               // fade+28 (dword 7) — current alpha AND teardown gate
    i32  rows = 0;                // fade+20 (dword 5) — captured row count (== height)
    // re-arm params passed to SetFadeParams in the alpha<=255 branch.
    i32  width = 0;               // fade+16 (0x10)
    i32  srcStride = 0;           // fade+4
};

// The swapped-tech leaves (SetFadeParams / row blit / heap+widget free).
struct CrossFadeOps {
    // VIBE_Gfx_SetFadeParams @0x5d9078 — re-arm the blend for the next frame.
    std::function<void(i32 width, i32 srcStride, i32 height, i32 alpha)> setFadeParams;
    // The captured-row blit (alpha > 255 branch).  Called once per row.
    std::function<void(i32 row)> blitRow;
    // Teardown: free surfaces + destroy the widget + free the descriptor.
    std::function<void()> teardown;
};

// Advances `fade` one frame.  Returns whether the descriptor is still active
// (false once torn down).  Mirrors the original's control flow exactly.
bool GfxCrossFadeStep(CrossFadeDesc& fade, const CrossFadeOps& ops);

} // namespace guild::sim
