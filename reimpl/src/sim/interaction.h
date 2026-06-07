#pragma once
// Object/object interaction resolution + per-tick interaction dispatch.
// Faithful 1:1 port from gilde.exe.
//
// Two pieces:
//   1. Handler resolution (VIBE_Interaction_Handler 0x411f8c): given an object
//      type and a 1-based field index, extract the matching '|'-delimited token
//      from that type's interaction descriptor string and copy it to `out`.
//      The descriptor lives at dword_69FFB4 + 740*type + 216 (a NUL-terminated
//      string of '|'-separated handler tokens; '||' marks an empty/skip field).
//   2. Dispatch (VIBE_GameObject_DispatchInteractions 0x40e6c0): drain the
//      current tile bucket's queue of 16-byte pending interaction events and
//      forward each (objectA, payloadB, c, d) to the result-handler.
//
// Translated functions:
//   VIBE_Interaction_Handler              0x411f8c
//   VIBE_GameObject_DispatchInteractions  0x40e6c0
#include "guild/common/types.h"

namespace guild::sim {

// Size of one object's interaction-descriptor block (gilde.exe stride 740).
constexpr int kInteractionDescStride = 740;
// Byte offset of the handler string within the descriptor block.
constexpr int kInteractionDescStringOffset = 216;

// ---------------------------------------------------------------------------
// Handler resolution.
// ---------------------------------------------------------------------------
// gilde.exe 0x411f8c — VIBE_Interaction_Handler  (__usercall al=(type@eax,
// field@edx, out@ebx)).
// `descTable` is the base of the per-type descriptor array (dword_69FFB4).
// Copies the descriptor string for object `type` into a local scratch, then:
//   - finds the first '|' delimiter token boundary;
//   - if `field` > 0, advances `field` tokens (each token ends at a '|');
//   - copies the selected token (NUL-terminated) into `out`.
// Returns the last byte processed (the original returns `al`); the resolved
// token text is written to `out`. If the field index runs past the end of the
// string, `out` is left holding whatever was last copied (matches original).
char InteractionResolveHandler(const u8* descTable, int type, int field, char* out);

// Convenience: resolve into a std::string-free fixed buffer and report whether
// a non-empty token was produced. (Not in the original; wraps the above.)
bool InteractionLookupToken(const u8* descTable, int type, int field,
                            char* out, int outCap);

// ---------------------------------------------------------------------------
// Per-tick interaction dispatch.
// ---------------------------------------------------------------------------
// One queued interaction event (16 bytes: gilde.exe reads +0,+4,+8,+12 dwords).
struct InteractionEvent {
    i32 objectA;  // +0  initiator entity
    i32 fieldB;   // +4  payload / second entity / param
    i32 paramC;   // +8  (+8 dword)
    i32 paramD;   // +12 (+12 dword)
};

// Handler signature for a resolved interaction (replaces VIBE_Result_Finalize).
// Returns are ignored by the dispatcher.
using InteractionResultFn = void (*)(i32 a, i32 b, i32 c, i32 d,
                                     i32 ctxA, i32 ctxB);

// gilde.exe 0x40e6c0 — VIBE_GameObject_DispatchInteractions.
// Drains `count` (== bucket event count) 16-byte events from `queue` and calls
// `fn` once per event with the unpacked fields, then clears the count. The
// original reads the active bucket via dword_62D2E0[bucket]/dword_62D2D0[bucket];
// here the resolved bucket pointer + count are passed in. `ctxA`/`ctxB` mirror
// the global context args (dword_62D218 / dword_62D210).
void InteractionDispatch(const InteractionEvent* queue, int count,
                         InteractionResultFn fn, i32 ctxA, i32 ctxB);

} // namespace guild::sim
