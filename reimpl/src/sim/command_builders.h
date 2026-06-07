#pragma once
#include "guild/common/types.h"
#include "sim/command.h"
#include "sim/he.h"

// command_builders — the remaining VIBE_Command_Queue* / RequestBuildOp* /
// Enqueue* packet builders that the NPC/AI/interaction step machines drive
// (guild::sim). Each stages a 153-byte temp, writes the opcode at +0 and its
// fields into the payload (which begins at +0x10 for the simple builders), then
// calls CommandQueue::EnqueuePacket. The complementary delta encoder + the first
// representative builder set live in command_codec.{h,cpp}; this file completes
// the set the Set*Hook callers (npcaction*.h NpcLeafHooks / NpcAction2/3/4Hooks)
// expect. Field layouts are recovered from the original stack-temp builders.
//
// Recovered staging convention (e.g. QueueRequestPair33 @0x494b04): the builder
// declares `char v[16]` (the 16-byte header region, bytes 0..15) followed by the
// payload locals at [ebp-0x90] == packet offset +0x10, +0x14, +0x18, ...  Note
// several "Quad" builders take four register args but only three reach the wire
// (the third arg lands in a stack slot OUTSIDE the 153-byte packet) — documented
// per-builder below.
//
// Opcode map (this file):
//   29  QueueRequestEntity29   53  QueueRequestNamedObject53
//   33  QueueRequestPair33     56  QueueRequestQuad56
//   49  QueueRequestSingle49   57  QueueRequestPair57
//   73  RequestBuildOp73Str    88  RequestBuildOp88
//   91  RequestBuildOp91       93  RequestBuildOp93

namespace guild::sim {

// gilde.exe 0x4949c4 — VIBE_Command_QueueRequestEntity29 (opcode 29). Snapshots a
// large slice of a HeRecord into the payload: the entity id, three transform
// dwords, marker word, the +82 appointment GameTime (3 dwords), three more
// dwords, the +172 region (160 bytes via StagePendingBlock), plus the leading
// arg byte `a1`, and a HE_NULL-class null string. Returns the assigned ring slot.
// `h` is the HeRecord; `a1` is the leading byte argument (-1/0/1/2/3).
i32 QueueRequestEntity29(CommandQueue& q, i8 a1, HeRecord* h);

// gilde.exe 0x494b04 — VIBE_Command_QueueRequestPair33 (opcode 33). a1@+0x10,
// a2@+0x14.
i32 QueueRequestPair33(CommandQueue& q, i32 a1, i32 a2);

// gilde.exe 0x494e4c — VIBE_Command_QueueRequestSingle49 (opcode 49). a1@+0x10.
i32 QueueRequestSingle49(CommandQueue& q, i32 a1);

// gilde.exe 0x495098 — VIBE_Command_QueueRequestQuad56 (opcode 56). Wire payload:
// a1@+0x10, a2@+0x14, a4@+0x18. (The `a3` register arg is stored in a stack slot
// OUTSIDE the packet and does NOT reach the wire — faithful to the original.)
i32 QueueRequestQuad56(CommandQueue& q, i32 a1, i32 a2, i32 a3, i32 a4);

// gilde.exe 0x495100 — VIBE_Command_QueueRequestPair57 (opcode 57). Note the
// swap: a2@+0x10, a1@+0x14 (v4=a2, v5=a1 in the original).
i32 QueueRequestPair57(CommandQueue& q, i32 a1, i32 a2);

// gilde.exe 0x495ae0 — VIBE_Command_RequestBuildOp88 (opcode 88). a1@+0x10.
i32 RequestBuildOp88(CommandQueue& q, i32 a1);

// gilde.exe 0x495b7c — VIBE_Command_RequestBuildOp91 (opcode 91). a1@+0x10,
// a2@+0x14, a4@+0x18. (a3 register arg stored outside the packet, not on wire.)
i32 RequestBuildOp91(CommandQueue& q, i32 a1, i32 a2, i32 a3, i32 a4);

// gilde.exe 0x495bd0 — VIBE_Command_RequestBuildOp93 (opcode 93). a1@+0x10,
// a2@+0x14, a4@+0x18. (a3 register arg stored outside the packet, not on wire.)
// This is the relation-mood delta command emitted by NpcAdjustRelationByMood.
i32 RequestBuildOp93(CommandQueue& q, i32 a1, i32 a2, i32 a3, i32 a4);

// gilde.exe 0x495554 — VIBE_Command_RequestBuildOp73Str (opcode 73). The "PEST"
// slot packet. Header is 23 bytes here (the opcode at +0, then fields), with:
//   a2@+0x17, a4@+0x1B, a1(byte)@+0x14, a5(byte)@+0x1F, a3(byte)@+0x20,
//   a6(word)@+0x21, then a 31-char-max name copied from +0x27 (HE_NULL-class
//   2-byte-stride copy). Returns the assigned ring slot.
i32 RequestBuildOp73Str(CommandQueue& q, i8 a1, i32 a2, i8 a3, i32 a4,
                        i8 a5, i16 a6, const char* name);

// gilde.exe 0x494f0c — VIBE_Command_QueueRequestNamedObject53 (opcode 53). The
// named-object interaction packet (e.g. plague "Pest"). Wire payload: a1@+0x10,
// a2@+0x14, a4@+0x18, a5(byte)@+0x1C, then a HE_NULL-class string copy of `obj`
// at +0x1D, and a 31-char-padded `name` at +0x3D. The original also runs entity
// look-ups (Person/Building) that may rewrite a4/a5 to -1/1 when the building's
// storable object is absent; those queries are cross-cluster leaves, so this
// reconstruction takes the resolved a4/a5 directly (the caller supplies them).
// Returns the ring slot, or -1 if the resolved person record is "not stamped"
// (the original early-out when FindRecordById(a1)->+8 == 0 — modelled by the
// caller passing personStamped=false).
i32 QueueRequestNamedObject53(CommandQueue& q, i32 a1, i32 a2, const char* obj,
                              i32 a4, i8 a5, const char* name,
                              bool personStamped = true);

} // namespace guild::sim
