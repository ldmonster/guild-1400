#pragma once
#include "guild/common/types.h"
#include "sim/command.h"
#include "sim/command_pending.h"

// command_builders2 — the remaining simple VIBE_Command_Queue* request builders
// not present in command_codec.{cpp} / command_builders.{cpp} (guild::sim). Each
// stages a 153-byte temp, writes the opcode at +0 and its fields in the payload
// (which begins at +0x10), then calls CommandQueue::EnqueuePacket. Field layouts
// are recovered from the original stack-temp builders. ODR: none of these names
// are defined elsewhere (verified by grep over src/sim).
//
// Opcode map (this file):
//   26  QueueRequestArgs26      37  QueueRequestQuad37     54  QueueRequestQuad54
//   28  QueueRequestBuffer28    41  QueueRequestPair41     58  QueueRequestSingle58
//   30  QueueRequestPerm30      42  QueueRequestPair42     59  QueueRequestSingle59
//   35  QueueRequestPair35      46  QueueRequestQuad46     60  QueueRequestQuad60
//   36  QueueRequestPair36b     51  QueueRequestPair51
//   43  QueueRequestQuad43      52  QueueRequestQuad52     55  QueueRequestFlag55
//
// "Quad" note: the four register args a1..a4 reach the wire as a1@+0x10, a2@+0x14,
// a4@+0x18 for opcodes 37/43/52/54 (a3 lands in a stack slot OUTSIDE the 153-byte
// packet — not on the wire). For opcodes 46/60 the third value a3 DOES reach the
// wire at +0x1C, so all four are sent (a1,a2,a4,a3). Documented per builder.

namespace guild::sim {

// gilde.exe 0x494848 — VIBE_Command_QueueRequestArgs26 (opcode 26). a1@+0x10,
// a2@+0x14, a3@+0x18.
i32 QueueRequestArgs26(CommandQueue& q, i32 a1, i32 a2, i32 a3);

// gilde.exe 0x494b74 — VIBE_Command_QueueRequestPair35 (opcode 35). a1@+0x10, a2@+0x14.
i32 QueueRequestPair35(CommandQueue& q, i32 a1, i32 a2);

// gilde.exe 0x494c80 — VIBE_Command_QueueRequestPair41 (opcode 41). a1@+0x10, a2@+0x14.
i32 QueueRequestPair41(CommandQueue& q, i32 a1, i32 a2);

// gilde.exe 0x494ca4 — VIBE_Command_QueueRequestPair42 (opcode 42). a1@+0x10, a2@+0x14.
i32 QueueRequestPair42(CommandQueue& q, i32 a1, i32 a2);

// gilde.exe 0x494ec0 — VIBE_Command_QueueRequestPair51 (opcode 51). a1@+0x10, a2@+0x14.
i32 QueueRequestPair51(CommandQueue& q, i32 a1, i32 a2);

// gilde.exe 0x494bbc — VIBE_Command_QueueRequestQuad37 (opcode 37). a1@+0x10,
// a2@+0x14, a4@+0x18 (a3 not on wire).
i32 QueueRequestQuad37(CommandQueue& q, i32 a1, i32 a2, i32 a3, i32 a4);

// gilde.exe 0x494cc8 — VIBE_Command_QueueRequestQuad43 (opcode 43). a1@+0x10,
// a2@+0x14, a4@+0x18 (a3 not on wire).
i32 QueueRequestQuad43(CommandQueue& q, i32 a1, i32 a2, i32 a3, i32 a4);

// gilde.exe 0x494d68 — VIBE_Command_QueueRequestQuad46 (opcode 46). a1@+0x10,
// a2@+0x14, a4@+0x18, a3@+0x1C (all four on wire).
i32 QueueRequestQuad46(CommandQueue& q, i32 a1, i32 a2, i32 a3, i32 a4);

// gilde.exe 0x494ee4 — VIBE_Command_QueueRequestQuad52 (opcode 52). a1@+0x10,
// a2@+0x14, a4@+0x18 (a3 not on wire).
i32 QueueRequestQuad52(CommandQueue& q, i32 a1, i32 a2, i32 a3, i32 a4);

// gilde.exe 0x495070 — VIBE_Command_QueueRequestQuad54 (opcode 54). a1@+0x10,
// a2@+0x14, a4@+0x18 (a3 not on wire).
i32 QueueRequestQuad54(CommandQueue& q, i32 a1, i32 a2, i32 a3, i32 a4);

// gilde.exe 0x495124 — VIBE_Command_QueueRequestQuad60 (opcode 60). a1@+0x10,
// a2@+0x14, a4@+0x18, a3@+0x1C (all four on wire).
i32 QueueRequestQuad60(CommandQueue& q, i32 a1, i32 a2, i32 a3, i32 a4);

// gilde.exe 0x4950c0 — VIBE_Command_QueueRequestSingle58 (opcode 58). a1@+0x10.
i32 QueueRequestSingle58(CommandQueue& q, i32 a1);

// gilde.exe 0x4950e0 — VIBE_Command_QueueRequestSingle59 (opcode 59). a1@+0x10.
i32 QueueRequestSingle59(CommandQueue& q, i32 a1);

// gilde.exe 0x495040 — VIBE_Command_QueueRequestFlag55 (opcode 55). a1@+0x10,
// then a 47-char NUL-padded name at +0x14, and the flag byte a2 at +0x44
// (= name field base +48). (VIBE_Util_StrNCopyPad(name,47).)
i32 QueueRequestFlag55(CommandQueue& q, i32 a1, i8 a2, const char* name);

// gilde.exe 0x494a50 — VIBE_Command_QueueRequestPerm30 (opcode 30). The
// "appointment-permission" request: only emitted if the supplied GameTime `appt`
// is still in the future (VIBE_GameTime_Compare(appt, gameClock) != 0); the
// original early-outs with -1 otherwise — here the caller passes `apptValid`.
// Wire payload: a1@+0x10, a2@+0x14, a3@+0x18, a4(word)@+0x1C  (read from the
// 4-dword arg block `appt[0..3]`, with appt[3]'s low word at +0x1C).
i32 QueueRequestPerm30(CommandQueue& q, i32 a1, i32 a2, i32 a3, i16 a4,
                       bool apptValid = true);

// gilde.exe 0x494910 — VIBE_Command_QueueRequestBuffer28 (opcode 28). The large
// speech/IO-buffer command: stages a 248-byte header struct + `bodyLen` body
// bytes as a pending block (StagePendingBlock(bodyLen+248, ...)), then enqueues an
// opcode-28 header packet whose +12 links to the generated fragment chain. The
// original early-outs with -1 if Person_FindRecordById(header->id) fails — here
// the caller passes `personFound`. `header` is the 248-byte struct; `body` is the
// trailing buffer. Returns the header packet's ring slot.
i32 QueueRequestBuffer28(CommandQueue& q, PendingState& pending,
                         const void* header, const void* body, u16 bodyLen,
                         bool personFound = true);

} // namespace guild::sim
