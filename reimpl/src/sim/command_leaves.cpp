#include "sim/command_leaves.h"

#include "sim/actionqueue.h"  // RunActionOrFree (0x406a00) — declared here, used below
#include "sim/charaction.h"
#include "util/coord.h"
#include "world/city.h"

#include <cstring>

namespace guild::sim {

// ===========================================================================
// 0x485f7c — VIBE_Command_FindOrAllocSlot
// ===========================================================================
// Disasm (the decompile's v11 was uninitialised noise; the disasm shows the
// alloc path consistently uses ecx == edi+0x94+edx where edx = 44*v7):
//   for ( i=0; i<16; ++i, base+=44 )
//       if ( *(base+0x94) == *(keyRec+4) ) return base+0x94+44*i;   // reuse
//   for ( v7=0; v7<16; ++v7, base+=44 )
//       if ( *(base+0x94) == -1 ) goto alloc;                       // empty
//   return 0;                                                       // full
//   alloc: slot = edi+0x94 + 44*v7;
//          VIBE_Light_SetGrayColorThunk(0,44,slot);  // zero 44 bytes
//          *(slot+4 byte) = 0;                        // +0x98 claimed byte
//          *(slot+0)      = *(keyRec+4);              // key
//          return slot;
int CommandFindOrAllocSlot(SlotRoster& roster, i32 key) {
    // Reuse pass: first slot whose key already equals `key`.
    for (int i = 0; i < kOrderSlotCount; ++i) {
        if (roster.key(i) == key)
            return i;
    }
    // Alloc pass: first free slot (key == -1).
    for (int i = 0; i < kOrderSlotCount; ++i) {
        if (roster.key(i) == -1) {
            std::memset(roster.slot_ptr(i), 0, kOrderSlotBytes); // SetGrayColorThunk(0,44)
            roster.slot_byte(i, kOrderSlotClaimedOff) = 0;        // *(slot+0x98) = 0
            roster.put_key(i, key);                               // *(slot+0x94) = key
            return i;
        }
    }
    return -1; // roster full
}

// ===========================================================================
// 0x49514c — VIBE_Command_QueueRequestGuardTarget61
// ===========================================================================
// Faithful packet-assembly RULE.  The original threads the squad-slot scan and
// the nearest-enemy scan (both already reconstructed in sim/combat_slots*.cpp)
// and packs their resulting ids into an opcode-61 packet.
//
//   v8[0]=61; if(!a1) return -1;
//   slot = FindAvailableSquadSlot(a1, a4, a3);
//   v9   = *(a1+1);          v10 = -1;
//   v12  = slot ? *(slot+4) : -1;
//   if ( (slot && !*(slot+0x170)) || !slot )
//       enemy = FindNearestEnemyTarget(a1, a1);
//   v10  = enemy ? *(enemy+1) : -1;
//   v11=a2; v13=a4; v14=a3;
//   return EnqueuePacket(v8).
//
// Packet field layout (recovered from the decompile's stack temp v8):
//   v8[0]  (+0x00)  opcode 61
//   v9     (+0x09)  owner id   (*(a1+1))      -> the kFCmdId-adjacent owner slot
//   v10    (+0x0D)  enemy id   (*(enemy+1))
//   v11    (+0x18)  a2 (mode)
//   v12    (+0x19)  squad id   (*(slot+4))
//   v13    (+0x1D)  a4
//   v14    (+0x1E)  a3
// The exact +offsets come from the byte indices the decompile assigns (v9 at
// ebp-9C == +0x10 of v8; the rest follow).  We stamp them onto the 153-byte
// CommandPacket and enqueue.
i32 CommandQueueRequestGuardTarget61(CommandQueue& q, const GuardTargetInputs& in,
                                     u8 mode, u8 a3, u8 a4) {
    if (!in.hasOwner)
        return -1;                       // if (!a1) return -1;

    CommandPacket p{};
    p.bytes[0] = 61;                     // v8[0] = 61

    i32 v9  = in.ownerId;                // *(a1 + 1)
    i32 v10 = -1;
    i32 v12 = in.hasSquad ? in.squadId : -1;

    // (slot && !*(slot+0x170)) || !slot  -> consult the nearest-enemy scan.
    bool consultEnemy = (in.hasSquad && !in.squadFull) || !in.hasSquad;
    if (consultEnemy)
        v10 = in.hasEnemy ? in.enemyId : -1;
    else
        v10 = -1;

    // Stamp the packet payload at the recovered offsets (v8 stack temp -> packet).
    p.put32(0x10, static_cast<u32>(v9));   // v9  (ebp-9C)
    p.put32(0x14, static_cast<u32>(v10));  // v10 (ebp-98)
    p.bytes[0x18] = mode;                  // v11 = a2
    p.put32(0x19, static_cast<u32>(v12));  // v12 (ebp-93)
    p.bytes[0x1D] = a4;                    // v13 = a4
    p.bytes[0x1E] = a3;                    // v14 = a3

    return q.EnqueuePacket(p);
}

// ===========================================================================
// 0x40c2f0 — VIBE_CharAction_InsertActionArgs
// ===========================================================================
// Sibling of VIBE_CharAction_InsertActionVararg (0x40c1e4); the type id comes
// from BYTE4(a3) (the packed __int64), the chained-fn from a2, the ready byte
// from a3&0xFF, and the variadic args from &a4.
//
//   if (!a1) return 0;
//   type = BYTE4(a3);
//   if (!registry[19*type].step) BYTE4(a3) = 0;   // clamp unregistered -> 0
//   node = QueueInsertEntry(a1);  if (!node) return 0;
//   node[0]  = registry[19*type].step;            // step fn
//   node[12] = 0;                                  // callCount
//   node[9]  = BYTE4(a3);                          // type byte
//   node[20] = a1;                                 // owner
//   for (i=0; i < registry[19*type].argCount; ++i) node[48+4*i] = (&a4)[i];
//   node[40] = 0; node[16] = 0;                    // next_link / state
//   node[4]  = a2;                                 // chained fn
//   node[8]  = a3;                                 // ready byte (low)
//   if (node[0] == RunActionOrFree) node[48] = type;
//   return node;
//
// `chained` is a function pointer in the original; this reconstruction's
// ActionNode uses native pointers, so a2 is passed as an ActionStepFn.  The
// ready byte (a3 & 0xFF) lands in node->ready (the original's node[8]).
ActionNode* CharActionInsertActionArgs(Character* ch, ActionStepFn chained,
                                       u64 packed, const i32* args) {
    if (!ch)
        return nullptr;                          // if (!a1) return 0;

    int type = static_cast<int>((packed >> 32) & 0xFF);   // BYTE4(a3)
    if (ActionType(type).step == nullptr)
        type = 0;                                          // clamp -> 0

    ActionNode* node = QueueInsertEntry(ch);
    if (!node)
        return nullptr;

    const ActionTypeDef& def = ActionType(type);
    node->step      = def.step;                  // node[0]
    node->callCount = 0;                          // node[12]
    node->type      = static_cast<u8>(type);     // node[9]
    node->owner     = ch;                         // node[20]

    int n = def.argCount;                         // registry argCount
    if (n > 63) n = 63;
    for (int i = 0; i < n; ++i)
        node->args[1 + i] = args ? args[i] : 0;  // node[48 + 4*i]

    node->next_link = nullptr;                    // node[40]
    node->state     = 0;                          // node[16]
    node->chained   = chained;                    // node[4] = a2
    node->ready     = static_cast<u8>(packed & 0xFF); // node[8] = a3 (low byte)

    if (node->step == &RunActionOrFree)
        node->args[1] = type;                     // node[48] = type
    return node;
}

// ===========================================================================
// 0x5799a8 — VIBE_GameTick_HandleTurnControlCommand
// ===========================================================================
// The tag at packet+36 (*((u32*)a1 + 9)) selects the latch/copy action.  The
// original's nested compares form an ordered binary search over the 4 magic
// dwords; we reproduce the SAME control flow (and the same memcpy that overwrites
// the latch dword) but as a plain switch on the recovered tags.
int GameTickHandleTurnControlCommand(TurnControlState& st, const CommandPacket& packet) {
    u32 tag = packet.get32(36);                  // *((u32*)a1 + 9)
    switch (tag) {
    case kTagDisable:                            // 'dsbl'
        st.set_latch(0);                         // dword_1235238 = 0
        return 1;
    case kTagEnable:                             // 'enbl'
        st.set_latch(1);                         // dword_1235238 = 1
        return 1;
    case kTagInit:                               // 'init'
    case kTagSet:                                // 'set '
        std::memcpy(st.block.data(), packet.bytes, 36); // copy 36 bytes
        return 1;
    default:
        return 0;
    }
}

// ===========================================================================
// 0x5792e0 — VIBE_City_ApplyStatsFromAck
// ===========================================================================
// Copies the 40-byte ACK head into the live city aggregates, re-seeds flt_641DA8
// (the cap divisor reused by economy_quality) and flt_641DAC, then averages the
// 5 satisfaction sums + the need sum over the active-person count via ConvertX
// (truncation toward zero — a bare fistp would round-to-nearest; the original
// inserts ConvertX's RC=truncate frndint before each fistp).
//
// Per active person the inner loop runs 5 times: each iteration adds the SAME
// need byte to v4 (so v4 == 5*Σneed) and adds one of 5 distinct satisfaction
// bytes (byte_12CE98F[base+1..+5]) to sat[0..4].  The averaged outputs:
//   out[j]   = (u8) trunc( sat[j] * (1.0/count) )      j = 0..4
//   overall  = (u8) trunc( v4     * (1.0/count) )      v4 = 5*Σneed
// When count == 0 the original divides by zero (1.0/0 -> +inf, trunc -> a huge
// value truncated to a byte).  We guard count==0 to a defined 0 result rather
// than reproduce host-specific inf->int UB; with no active persons the report is
// degenerate either way (documented divergence, count==0 is not a live state).
CityStatsResult CityApplyStatsFromAck(const CityStatsAck& ack) {
    CityStatsResult r;

    // --- copy head fields into the live aggregates (modeled: only the two reused
    // floats are observable cross-module) -----------------------------------
    // flt_641DA8 = *(float*)(a1+24)  (head[6]);  flt_641DAC = (a1+20) (head[5]).
    float capDivisor;
    std::memcpy(&capDivisor, &ack.head[6], sizeof(float)); // flt_1234928 -> flt_641DA8
    world::g_capDivisor = capDivisor;                      // write-through
    r.capDivisor = capDivisor;

    // --- accumulate over the active person set -----------------------------
    int count = 0;                 // v3
    i64 v4 = 0;                     // need accumulator (== 5*Σneed)
    std::array<i64, 5> sat{};       // v20[1..5]
    for (const CityStatsAck::Person& p : ack.persons) {
        ++count;                                  // ++v3
        int need = p.needByte;                    // v11 = byte_12CE91D[i]
        for (int j = 0; j < 5; ++j) {
            sat[j] += p.sat[j];                   // v20[j+1] += byte_12CE98F[base+1+j]
            v4 += need;                           // v4 += v11 (5x per person)
        }
    }
    r.activeCount = count;

    if (count == 0) {
        // Degenerate: no active persons (documented divergence from the original's
        // divide-by-zero); leave the averaged bytes at 0.
        return r;
    }

    double recip = 1.0 / static_cast<double>(count); // v15 = 1.0/(double)v3
    for (int j = 0; j < 5; ++j) {
        double v = static_cast<double>(static_cast<i32>(sat[j])) * recip;
        r.averaged[j] = static_cast<u8>(static_cast<i32>(util::ConvertX(v)));
    }
    double overall = static_cast<double>(static_cast<i32>(v4)) * recip;
    r.overall = static_cast<u8>(static_cast<i32>(util::ConvertX(overall)));

    // VIBE_Statistics_BuildEconomyReport() is the live report rebuild; the host
    // wires it (it reads the byte_123525B/byte_1235261 outputs we return).
    return r;
}

// ===========================================================================
// 0x4ac0c8 — VIBE_Cutscene_CheckMaster
// ===========================================================================
// The disasm makes the loop exact: edx 0..15, keep-flag ecx starts 1, ebp==0 is
// the "stop" value.  `rec` (edi) tracks the last successfully looked-up record;
// it is seeded with the current-master record before the loop, and overwritten by
// each resolved participant.  The loop stops the instant a kind-6/7 participant is
// found (so rec == that participant's record).
int CutsceneCheckMaster(const CutsceneMasterInputs& in,
                        const std::function<void(i32 newMasterOwnerId)>& broadcastMaster) {
    // if ( ActorHasParticipant(master, slot) ) return 1;
    if (in.masterIsParticipant)
        return 1;

    // rec = FindRecordById(master);  (edi seeded with the master record)
    bool recValid = in.masterRecValid;
    i32  recOwnerId = in.masterRecOwnerId;

    int keep = 1;   // ecx
    for (int edx = 0; edx < 16 && keep; ++edx) {
        const CutsceneMasterRecord& part = in.participants[edx];
        i32 pid = part.id;                      // v6 = &a1[edx]; v6[13] (== id @+0x34)
        if (pid == -1)
            continue;                            // skip empty participant
        // Original (0x4ac13f): v7 = FindRecordById(id); RecordById = v7;
        // The assignment is UNCONDITIONAL — even when FindRecordById returns null,
        // `rec` (edi) is OVERWRITTEN with null. So a valid id whose record fails to
        // resolve clears `rec`. Only the kind check is guarded by `if (v7)`.
        recValid = part.valid;                   // rec = r (may be null)
        if (!part.valid)
            continue;                            // FindRecordById returned null; rec=null
        recOwnerId = part.ownerId;               // rec+4
        u8 kind = part.kind;                     // rec+2
        if (kind == 6 || kind == 7)
            keep = 0;                            // ecx = ebp = 0 -> stop
    }

    // if ( !rec ) return 1;
    if (!recValid)
        return 1;
    // if ( rec.ownerId == master ) return 1;
    if (recOwnerId == in.master)
        return 1;

    // RequestBuildOp95(slot, rec) + spin GetPacketStatusById/Amt_RefreshGuildState.
    if (broadcastMaster)
        broadcastMaster(recOwnerId);
    return 0;
}

// ===========================================================================
// 0x41e814 — VIBE_Gfx_CrossFadeStep
// ===========================================================================
// +28 (the alpha) is incremented by 8 each frame; it doubles as the teardown
// gate.  Control flow: re-arm while alpha<=255, blit captured rows while alpha in
// (255,288], teardown once alpha>288.  The re-arm/blit/free leaves are the
// Vulkan/SDL boundary (routed through ops); the sequencing + arithmetic is 1:1.
bool GfxCrossFadeStep(CrossFadeDesc& fade, const CrossFadeOps& ops) {
    // if ( !fade || !*fade ) return;  (fade present AND surfFrom present)
    if (!fade.active)
        return fade.active;

    fade.alpha += 8;                              // *(fade+28) += 8
    if (fade.alpha <= 255) {
        // SetFadeParams(width, srcStride, height, alpha) — re-arm next frame.
        if (ops.setFadeParams)
            ops.setFadeParams(fade.width, fade.srcStride, fade.rows, fade.alpha);
    } else {
        // Blit each captured row (alpha in (255,288]).
        for (int row = 0; row < fade.rows; ++row) {
            if (ops.blitRow)
                ops.blitRow(row);
        }
    }

    // Teardown once alpha > 288 (v3[7] > 288).
    if (fade.alpha > 288) {
        if (ops.teardown)
            ops.teardown();
        fade.active = false;                     // dword_69FF94 = 0 (active-fade cleared)
    }
    return fade.active;
}

} // namespace guild::sim
