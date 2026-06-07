#pragma once
#include "guild/common/types.h"
#include "sim/command.h"
#include "sim/command_pending.h"
#include "sim/command_apply7.h" // RequestBuildOp90, QueueRequestFlagBlob32 (reused)

// command_apply8 — a further batch of genuinely-untranslated VIBE_Command_*
// SEND-side request builders, plus the two justice-severity command builders
// (whose clamp arithmetic is the testable core) and the RequestBuildOp90 thunk.
// namespace guild::sim.
//
// Every name/address below was checked against command_apply..command_apply7,
// command_codec, command_builders, command_pending and command_inherit; none is
// defined there (the genuinely-untranslated slice of the 288 VIBE_Command_*).
//
// Builder idiom (recovered from the original stack-temp builders, identical to
// command_apply7): each stages a 153-byte CommandPacket, writes the opcode byte
// at +0 and its fields into the payload region (which begins at +0x10), then
// calls CommandQueue::EnqueuePacket (which stamps the wire length / cmdId /
// sequence Count and links the slot onto the pending-send list). A leading
// `char vN[16]` local in the original covers the 16-byte header; payload locals
// follow at +0x10, +0x14, ...
//
// Cross-module leaves that the binary reads from large unmodelled file globals
// (the per-player money snapshot, the opcode-23/24 state blobs, the opcode-34
// object-spawn template + RNG, and the gesetz/law string-table + record lookups
// the justice builders use) are routed through an installable hooks struct with
// inert defaults defined in the library .cpp. Tests install spies.

namespace guild::sim {

// ---------------------------------------------------------------------------
// Installable cross-module leaf hooks (inert defaults).
// ---------------------------------------------------------------------------
struct CommandApply8Hooks {
    // Snapshot of the local player's money dword (dword_12CE914[134*word_63CC5C]
    // in SendPlayerMoneyState). Default: 0.
    i32 (*localPlayerMoney)();

    // The 124-byte opcode-23 state blob (dword_11AA3E0). Copies 124 bytes into
    // `dst`. Default: zero-fill.
    void (*copyState23Blob)(void* dst);
    // The 124-byte opcode-24 state blob (dword_11AA360). Default: zero-fill.
    void (*copyState24Blob)(void* dst);

    // The opcode-34 object-spawn template: copies 0x2D bytes into `dst`, returns
    // the leading scene-object id (dword_632240) that overwrites dst[0]. Default:
    // zero-fill, id 0.
    i32 (*objectTemplate34)(void* dst);
    // VIBE_Util_RandNext @ — next PRNG dword (the opcode-34 nonce). Default: 0.
    u32 (*randNext)();

    // --- gesetz / justice (QueueSetJusticeSeverity / QueueAdjustJusticeSeverity) ---
    // VIBE_Gesetz_GetRecord(lawType, out): fills out->{enabled, min, max} for the
    // law; returns nonzero on success. Default: returns 0 (no record).
    struct LawRecord { i32 min; i32 max; bool adjustable; };
    int (*gesetzGetRecord)(int lawType, LawRecord* out);
    // VIBE_Gesetz_RequestApply(arg0, lawType, value): enqueue the law change.
    void (*gesetzRequestApply)(int arg0, int lawType, int value);
};

void SetCommandApply8Hooks(const CommandApply8Hooks* hooks);
const CommandApply8Hooks& GetCommandApply8Hooks();

// ---------------------------------------------------------------------------
// Simple opcode request builders.
// ---------------------------------------------------------------------------

// gilde.exe 0x4946f4 — VIBE_Command_QueueRequest20 (opcode 20). a1(int)@+0x10,
// a2(i16)@+0x14.
i32 QueueRequest20(CommandQueue& q, i32 a1, i16 a2);

// gilde.exe 0x494b98 — VIBE_Command_QueueRequestPair36 (opcode 36). a1@+0x10,
// a2@+0x14.
i32 QueueRequestPair36(CommandQueue& q, i32 a1, i32 a2);

// gilde.exe 0x494790 — VIBE_Command_QueueRequestState23 (opcode 23). Copies the
// 124-byte opcode-23 state blob into the payload @+0x10 (via copyState23Blob).
i32 QueueRequestState23(CommandQueue& q);

// gilde.exe 0x4947d0 — VIBE_Command_QueueRequestState24 (opcode 24). Copies the
// 124-byte opcode-24 state blob into the payload @+0x10 (via copyState24Blob).
i32 QueueRequestState24(CommandQueue& q);

// gilde.exe 0x494c58 — VIBE_Command_QueueRequest40 (opcode 40). Stages a 0x114
// (276)-byte pending block from `src`, then enqueues the opcode-40 header that
// owns it (EmitWithPendingBlock). `pending` models the staging globals.
i32 QueueRequest40(CommandQueue& q, PendingState& pending, const void* src);

// gilde.exe 0x494b28 — VIBE_Command_QueueRequestObject34 (opcode 34). Copies the
// 0x2D-byte object-spawn template into the payload @+0x10, overwrites payload[0]
// with the scene-object id, sets the present flag (payload+0x25 dword) to 1, the
// nonce (payload+0x29 dword) to RandNext(), and stashes the caller's `tail` arg
// at payload+0x98 (the original v4[38], a stack-only field not on the wire — it
// lands at packet+0xA8, OUTSIDE the 153-byte record, so it is dropped here and
// documented). Returns the enqueue slot.
i32 QueueRequestObject34(CommandQueue& q, const void* templateSrc, i32 tail);

// gilde.exe 0x49441c — VIBE_Command_EnqueueBuildingActionStart (opcode 5). Copies
// up to 128 chars of `name` (NUL-padded to 128) into payload @+0x10.
i32 EnqueueBuildingActionStart(CommandQueue& q, const char* name);

// gilde.exe 0x493e88 — VIBE_Command_SendPlayerMoneyState. Reads the local player
// money snapshot, then enqueues QueueRequestFlagBlob32(q, 19, {money}) — i.e. an
// opcode-32 packet with flag byte 19 and the money dword as the 124-byte blob's
// first field.
i32 SendPlayerMoneyState(CommandQueue& q);

// gilde.exe 0x594c8c — VIBE_Command_RequestBuildOp90_Thunk (thunk). Swaps the two
// register args and forwards to RequestBuildOp90(a@eax, b@edx) -> (b, a). The
// original is RequestBuildOp90(a2, a1); we keep that exact ordering.
i32 RequestBuildOp90_Thunk(CommandQueue& q, i32 a, i32 b);

// ---------------------------------------------------------------------------
// Justice-severity command builders. The parse/clamp arithmetic is the testable
// core (exposed separately); the string-table match + record lookup are routed
// through the gesetz hooks.
// ---------------------------------------------------------------------------

// gilde.exe 0x4fbd00 — VIBE_Command_QueueSetJusticeSeverity. After matching the
// console token and resolving the law's [min,max] band, the requested ABSOLUTE
// value is clamped into the band. The clamp the original performs is exactly:
//     v = min(value, max); if (v <= min) v = min; else v = (value<=max?value:max)
// which reduces to clamp(value, min, max). Returns the value to apply.
i32 JusticeClampAbsolute(i32 value, i32 min, i32 max);

// gilde.exe 0x4fbe04 — VIBE_Command_QueueAdjustJusticeSeverity. The DELTA form:
//     target = current + sign*delta   (sign = +1 for '+', -1 for '-')
//     clamp(target, min, max)
// Returns the clamped target. (`sign` is the token's +/- selector.)
i32 JusticeClampDelta(i32 current, int sign, i32 delta, i32 min, i32 max);

// Full builders: resolve the law record via the hook, clamp, and emit via
// gesetzRequestApply. `lawType` is the resolved law id (the console token match
// is the caller's responsibility / passed in). The absolute form returns 1 if a
// change was emitted, 0 if rejected (out-of-band when adjustable) or if the law
// has no record. The delta form takes the law's `current` severity explicitly
// (the original reads it from the record body, v23).
i32 QueueSetJusticeSeverity(int lawType, i32 requestedValue);
i32 QueueAdjustJusticeSeverity(int lawType, int sign, i32 delta, i32 current);

} // namespace guild::sim
