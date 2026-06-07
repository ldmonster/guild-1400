#pragma once
#include "guild/common/types.h"
#include "sim/command.h"

// command_apply7 — a further batch of self-contained VIBE_Command_* packet
// builders, the game-speed control commands, and three trivial apply handlers
// (guild::sim). Like the other builder clusters, each builder stages a 153-byte
// CommandPacket, writes the opcode at +0 and its fields into the header/payload
// region, then calls CommandQueue::EnqueuePacket which stamps the length / cmdId
// / sequence Count and links the slot onto the pending-send list.
//
// Staging-offset convention (recovered from the original stack-temp builders):
// the leading `char vN[16]` local covers packet bytes 0..15 (the header), and
// the payload locals that follow live at packet offset +0x10, +0x14, ...  Some
// builders take register args that land in a stack slot OUTSIDE the 153-byte
// packet ([ebp-4] / the canary slot) and therefore do NOT reach the wire; those
// are documented per-builder below and faithfully dropped.
//
// Opcode map (this file):
//    3  EnqueueKeepAlive          6  EnqueueBuildingActionEnd
//   10  EnqueueTargetedAction    12  EnqueueTradeRequest
//   13  EnqueueCmd13             14  EnqueueCmd14
//   18  QueueRequest18           19  QueueRequest19
//   21  QueueRequestBlob21       31  QueueRequest31
//   32  QueueRequestFlagBlob32   44  QueueRequestMixed44
//   45  QueueRequestMixed45      47  QueueRequestString47
//   50  QueueRequestVectors50    62  QueueRequestString62
//   63  QueueRequestFlagBlob63   77  RequestBuildOp77
//   78  RequestBuildOp78DualStr  82  RequestBuildOp82
//   90  RequestBuildOp90
// Game-speed control: SetGameSpeed / Increase / Decrease / GetGameSpeed (opcode
// 32 via QueueRequestFlagBlob32, with the level blob at +0x11).

namespace guild::sim {

// dword_631284 — current game-speed level (0..4), module-private global. Mutated
// only by the SetGameSpeed family (after they enqueue the change command).
extern i32 g_gameSpeed;

// gilde.exe 0x494ab4 — VIBE_Command_QueueRequestFlagBlob32 (opcode 32).
// flag byte @ +0x10; if `blob` is non-null, 124 bytes copied @ +0x11.
i32 QueueRequestFlagBlob32(CommandQueue& q, i8 flag, const void* blob);

// gilde.exe 0x493478 — VIBE_Command_EnqueueKeepAlive (opcode 3). Stamps the
// magic 0xB325939D (-1288263715) at +0x10, no other payload.
i32 EnqueueKeepAlive(CommandQueue& q);

// gilde.exe 0x49444c — VIBE_Command_EnqueueBuildingActionEnd (opcode 6). Copies
// up to 128 chars of `name` (NUL-padded) @ +0x10.
i32 EnqueueBuildingActionEnd(CommandQueue& q, const char* name);

// gilde.exe 0x49447c — VIBE_Command_EnqueueTargetedAction (opcode 10). Returns
// -1 immediately if `a1` (kind byte) is 0. Wire: a1(i16)@+0x14, a2(int)@+0x16,
// a3(i16)@+0x1A, present-flag(int)@+0x1C (1 if `blob`!=null else 0), and 48
// blob bytes @+0x20 (cleared to zero when blob==null in the original — it calls
// VIBE_Light_SetGrayColorThunk(0,48) which clears the 48-byte field).
i32 EnqueueTargetedAction(CommandQueue& q, u8 a1, i32 a2, i16 a3, const void* blob);

// gilde.exe 0x494548 — VIBE_Command_EnqueueTradeRequest (opcode 12). Wire:
// a1(int)@+0x14, a2(int)@+0x18, a4(i16)@+0x1C, a3(u8)@+0x1E, a5(int)@+0x1F,
// a6(u8)@+0x23, a7(u8)@+0x24, then name1 (StrNCopyPad 16) @+0x25 and name2
// (StrNCopyPad 16) @+0x35. (Arg order matches the original __userpurge frame.)
i32 EnqueueTradeRequest(CommandQueue& q, i32 a1, i32 a2, i8 a3, i16 a4, i32 a5,
                        i8 a6, i8 a7, const char* name1, const char* name2);

// gilde.exe 0x4945c0 — VIBE_Command_EnqueueCmd13 (opcode 13). a1@+0x10,
// a2@+0x14.
i32 EnqueueCmd13(CommandQueue& q, i32 a1, i32 a2);

// gilde.exe 0x4945e4 — VIBE_Command_EnqueueCmd14 (opcode 14). a1(i16)@+0x10.
i32 EnqueueCmd14(CommandQueue& q, i16 a1);

// gilde.exe 0x4946a4 — VIBE_Command_QueueRequest18 (opcode 18). a1(int)@+0x10,
// a2(i16)@+0x14, a4(int)@+0x16. (a3 register arg stored outside the packet.)
i32 QueueRequest18(CommandQueue& q, i32 a1, i16 a2, i32 a3, i32 a4);

// gilde.exe 0x4946cc — VIBE_Command_QueueRequest19 (opcode 19). a1(int)@+0x10,
// a2(int)@+0x14, a4(int)@+0x18. (a3 register arg stored outside the packet.)
i32 QueueRequest19(CommandQueue& q, i32 a1, i32 a2, i32 a3, i32 a4);

// gilde.exe 0x494718 — VIBE_Command_QueueRequestBlob21 (opcode 21).
// a1(int)@+0x10, a2(i16)@+0x14, then 31 bytes copied from `blob` @+0x16.
i32 QueueRequestBlob21(CommandQueue& q, i32 a1, i16 a2, const void* blob);

// gilde.exe 0x494a9c — VIBE_Command_QueueRequest31 (opcode 31). No payload.
i32 QueueRequest31(CommandQueue& q);

// gilde.exe 0x494cf0 — VIBE_Command_QueueRequestMixed44 (opcode 44).
// a1(int)@+0x14, a2(u8)@+0x18, a4(u8)@+0x19, a3(i16)@+0x1A, a5(u8)@+0x1C,
// a6(int)@+0x1D.
i32 QueueRequestMixed44(CommandQueue& q, i32 a1, i8 a2, i16 a3, i8 a4, i8 a5, i32 a6);

// gilde.exe 0x494d34 — VIBE_Command_QueueRequestMixed45 (opcode 45).
// a1(int)@+0x14, a2(int)@+0x18, a3 low word(i16)@+0x1C, a3 byte2(u8)@+0x1E.
i32 QueueRequestMixed45(CommandQueue& q, i32 a1, i32 a2, i32 a3);

// gilde.exe 0x494d90 — VIBE_Command_QueueRequestString47 (opcode 47).
// a1(int)@+0x10, a2(int)@+0x14, a4(int)@+0x18, then a HE_NULL-class string copy
// of `name` @+0x1C. (a3 carries the name pointer.)
i32 QueueRequestString47(CommandQueue& q, i32 a1, i32 a2, const char* name, i32 a4);

// gilde.exe 0x494e6c — VIBE_Command_QueueRequestVectors50 (opcode 50).
// a1(int)@+0x10; if `vecA` != null its 3 ints @+0x14..+0x1C; if `vecB` != null
// its 3 ints @+0x24..+0x2C. (a3 register arg stored outside the packet.)
i32 QueueRequestVectors50(CommandQueue& q, i32 a1, const i32* vecA, i32 a3, const i32* vecB);

// gilde.exe 0x495230 — VIBE_Command_QueueRequestString62 (opcode 62).
// a1(int)@+0x10, then a HE_NULL-class string copy of `name` @+0x14.
i32 QueueRequestString62(CommandQueue& q, i32 a1, const char* name);

// gilde.exe 0x495274 — VIBE_Command_QueueRequestFlagBlob63 (opcode 63).
// flag(u8)@+0x10, then 128 bytes copied from `blob` @+0x11.
i32 QueueRequestFlagBlob63(CommandQueue& q, const void* blob, i8 flag);

// gilde.exe 0x4956c8 — VIBE_Command_RequestBuildOp77 (opcode 77). a1@+0x10.
i32 RequestBuildOp77(CommandQueue& q, i32 a1);

// gilde.exe 0x4956e8 — VIBE_Command_RequestBuildOp78DualStr (opcode 78). A
// HE_NULL-class copy of `name1` @+0x10, a2(int)@+0x28, a4(int)@+0x2C, then a
// HE_NULL-class copy of `name2` @+0x30 ("" if name2==null).
i32 RequestBuildOp78DualStr(CommandQueue& q, const char* name1, i32 a2,
                            const char* name2, i32 a4);

// gilde.exe 0x49592c — VIBE_Command_RequestBuildOp82 (opcode 82). a1(int)@+0x10,
// a2(int)@+0x14, a4(int)@+0x18. (a3 register arg stored outside the packet.)
i32 RequestBuildOp82(CommandQueue& q, i32 a1, i32 a2, i32 a3, i32 a4);

// gilde.exe 0x495b58 — VIBE_Command_RequestBuildOp90 (opcode 90). a1(int)@+0x10,
// a2(int)@+0x14.
i32 RequestBuildOp90(CommandQueue& q, i32 a1, i32 a2);

// --- game-speed control commands -------------------------------------------

// gilde.exe 0x493dec — VIBE_Command_SetGameSpeed. Clamps `level` to 4; if it
// already equals g_gameSpeed (after clamp) does nothing and returns it;
// otherwise enqueues QueueRequestFlagBlob32(18, {level}). Returns the clamped
// level. NOTE: the apply handler (not this builder) updates g_gameSpeed; this
// only reflects the original's compare-against-current behavior.
i32 SetGameSpeed(CommandQueue& q, u32 level);

// gilde.exe 0x493e2c — VIBE_Command_IncreaseGameSpeed. If g_gameSpeed < 4,
// enqueues QueueRequestFlagBlob32(18, {g_gameSpeed + 1}).
void IncreaseGameSpeed(CommandQueue& q);

// gilde.exe 0x493e58 — VIBE_Command_DecreaseGameSpeed. If g_gameSpeed != 0,
// enqueues QueueRequestFlagBlob32(18, {g_gameSpeed - 1}).
void DecreaseGameSpeed(CommandQueue& q);

// gilde.exe 0x493e80 — VIBE_Command_GetGameSpeed.
i32 GetGameSpeed();

} // namespace guild::sim
