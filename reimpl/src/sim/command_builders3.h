#pragma once
#include "guild/common/types.h"
#include "sim/command.h"

// command_builders3 — the remaining VIBE_Command_RequestBuildOp* request
// builders (opcodes 65..95) plus RequestCreateGebaeude (76), RequestSendCutInfo
// (38) and RequestCutsceneReady (89) that were not yet present in
// command_codec.{cpp} / command_builders.{cpp} / command_builders2.{cpp}
// (guild::sim). ODR: every name below was confirmed absent elsewhere in src/
// (grep over src/sim and src/).
//
// Recovered staging convention (identical to command_builders): each builder
// declares `char v[16]` (the 16-byte header region, bytes 0..15) followed by the
// payload locals at packet offset +0x10, +0x14, +0x18, …  The builder writes the
// opcode at +0, fills the payload, and calls CommandQueue::EnqueuePacket, which
// stamps the header (len/cmdId/count) itself. Several builders also store one
// register arg into an `int v // [ebp-4]` slot that lies OUTSIDE the 153-byte
// staging buffer (packet base ebp-0xAC, packet end ebp-0x13); that value never
// reaches the wire and is documented per builder.
//
// Opcode map (this file):
//   65 RequestBuildOp65Blob     74 RequestBuildOp74         86 RequestBuildOp86Blob
//   66 RequestBuildOp66         75 RequestBuildOp75Blob     87 RequestBuildOp87
//   67 RequestBuildOp67         76 RequestCreateGebaeude    89 RequestCutsceneReady
//   68 RequestBuildOp68Blob     79 RequestBuildOp79Path     92 RequestBuildOp92
//   69 RequestBuildOp69Blob     83 RequestBuildOp83         94 RequestBuildOp94
//   70 RequestBuildOp70Blob     84 RequestBuildOp84         95 RequestBuildOp95
//   71 RequestBuildOp71         38 RequestSendCutInfo
//   72 RequestBuildOp72

namespace guild::sim {

// --- "blob" builders: copy raw bytes from a source struct into the payload.
// The original qmemcpy's fixed-size slices out of *src (the @<eax> pointer arg);
// a trailing register arg lands in an out-of-packet stack slot and is dropped.

// gilde.exe 0x4953d8 — VIBE_Command_RequestBuildOp65 (opcode 65). Copies 56
// bytes (src[0..55]) to +0x10 and 2 bytes (src[56..57]) to +0x48. `a2` (ecx) is
// stored outside the packet and is not on the wire.
i32 RequestBuildOp65Blob(CommandQueue& q, const void* src);

// gilde.exe 0x495414 — VIBE_Command_RequestBuildOp66 (opcode 66). a1@+0x10.
i32 RequestBuildOp66(CommandQueue& q, i32 a1);

// gilde.exe 0x495434 — VIBE_Command_RequestBuildOp67 (opcode 67). a1@+0x10.
i32 RequestBuildOp67(CommandQueue& q, i32 a1);

// gilde.exe 0x495454 — VIBE_Command_RequestBuildOp68 (opcode 68). Copies 4 bytes
// (src[0..3]) to +0x10 and 3 bytes (src[4..6]) to +0x14. `a2` not on wire.
i32 RequestBuildOp68Blob(CommandQueue& q, const void* src);

// gilde.exe 0x495490 — VIBE_Command_RequestBuildOp69 (opcode 69). Copies 12
// bytes (src[0..11]) to +0x10 and 3 bytes (src[12..14]) to +0x1C. `a2` not on
// wire.
i32 RequestBuildOp69Blob(CommandQueue& q, const void* src);

// gilde.exe 0x4954cc — VIBE_Command_RequestBuildOp70 (opcode 70). Copies 8 bytes
// (src[0..7]) to +0x10 and 1 byte (src[8]) to +0x18. `a2` not on wire.
i32 RequestBuildOp70Blob(CommandQueue& q, const void* src);

// gilde.exe 0x495508 — VIBE_Command_RequestBuildOp71 (opcode 71). a1@+0x10,
// a2@+0x14, a4@+0x18. (a3 register arg stored outside the packet, not on wire.)
i32 RequestBuildOp71(CommandQueue& q, i32 a1, i32 a2, i32 a3, i32 a4);

// gilde.exe 0x495530 — VIBE_Command_RequestBuildOp72 (opcode 72). a1@+0x10
// (dword), a2@+0x14 (byte).
i32 RequestBuildOp72(CommandQueue& q, i32 a1, i8 a2);

// gilde.exe 0x4955c0 — VIBE_Command_RequestBuildOp74 (opcode 74). a1@+0x10.
i32 RequestBuildOp74(CommandQueue& q, i32 a1);

// gilde.exe 0x4955e0 — VIBE_Command_RequestBuildOp75 (opcode 75). Copies 128
// bytes (src[0..127]) to +0x10. `a2` not on wire.
i32 RequestBuildOp75Blob(CommandQueue& q, const void* src);

// gilde.exe 0x495954 — VIBE_Command_RequestBuildOp83 (opcode 83). Copies the
// five dwords src[0..4] to +0x10,+0x14,+0x18,+0x1C,+0x20.
i32 RequestBuildOp83(CommandQueue& q, const i32* src5);

// gilde.exe 0x495980 — VIBE_Command_RequestBuildOp84 (opcode 84). Copies the
// three dwords src[0..2] to +0x10,+0x14,+0x18.
i32 RequestBuildOp84(CommandQueue& q, const i32* src3);

// gilde.exe 0x495a90 — VIBE_Command_RequestBuildOp86 (opcode 86). Copies 40
// bytes (src[0..39]) to +0x10. `a2` not on wire.
i32 RequestBuildOp86Blob(CommandQueue& q, const void* src);

// gilde.exe 0x495ac0 — VIBE_Command_RequestBuildOp87 (opcode 87). a1@+0x10.
i32 RequestBuildOp87(CommandQueue& q, i32 a1);

// gilde.exe 0x495ba4 — VIBE_Command_RequestBuildOp92 (opcode 92). src[0]@+0x10,
// src[1]@+0x14 (dwords), src word[4] (src+8)@+0x18 (u16).
i32 RequestBuildOp92(CommandQueue& q, const i32* src);

// gilde.exe 0x495bf8 — VIBE_Command_RequestBuildOp94 (opcode 94). The header
// region is 20 bytes here: a dword from *(base+1) lands at +0x14, then 28 bytes
// of `blob` land at +0x18. `base` points one byte before the source dword.
i32 RequestBuildOp94(CommandQueue& q, const void* base, const void* blob28);

// gilde.exe 0x495c3c — VIBE_Command_RequestBuildOp95 (opcode 95). src[0]@+0x10,
// *(rec+4)@+0x14 (the id field of a second record).
i32 RequestBuildOp95(CommandQueue& q, const i32* src, const void* rec);

// NOTE: VIBE_Command_RequestBuildOp79Path @0x495768 (opcode 79) is intentionally
// NOT translated here. Its decompilation contains overlapping/aliased writes into
// the same payload region (the actor name string is copied to +0x10, then the
// path-cursor dwords overwrite its first 8 bytes, with waypoint bytes scattered
// across two separate stack arrays), making a confident 1:1 reconstruction
// unsafe. Deferred and reported.

// gilde.exe 0x49561c — VIBE_Command_RequestCreateGebaeude (opcode 76). a1@+0x10
// (dword), a2@+0x18 (byte, also mirrored into an out-of-packet debug slot),
// 16 bytes of transform from `pos` at +0x19, 12 bytes from `meta` at +0x29.
// The original also sprintf's a debug line into an out-of-packet buffer.
i32 RequestCreateGebaeude(CommandQueue& q, i32 a1, i8 a2,
                          const void* meta12, const float* pos);

// gilde.exe 0x494be4 — VIBE_Command_RequestSendCutInfo (opcode 38). a1@+0x10,
// a2@+0x14. (gameTick is used only by the discarded debug sprintf.)
i32 RequestSendCutInfo(CommandQueue& q, i32 a1, i32 a2, i32 gameTick);

// gilde.exe 0x495b00 — VIBE_Command_RequestCutsceneReady (opcode 89). Copies 68
// bytes (src[0..67]) to +0x10; the original also sprintf's a debug line into an
// out-of-packet buffer. `a2` not on wire.
i32 RequestCutsceneReady(CommandQueue& q, const void* src68);

} // namespace guild::sim
