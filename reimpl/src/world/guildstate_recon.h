#pragma once
// ===========================================================================
// guildstate_recon.{h,cpp} — pure game-state / object-query rule kernels
// ===========================================================================
// Faithful 1:1 reconstructions (namespace guild::world) of two load-bearing,
// self-contained state-machines from gilde.exe whose surrounding shells are
// GUI / scene-graph coupled leaves:
//
//   * VIBE_GameObject_IterNextTree (gilde.exe 0x585488) — the depth-first
//     object-tree query ITERATOR. The game opens an object query (filter by
//     prototype word, location id, owner kind, resolved type field) and then
//     repeatedly calls this to advance a global cursor and return the next
//     matching record, optionally recursing into each node's child list with an
//     explicit DFS stack. This is pure cursor/stack state-machine logic; the
//     only thing it does not own is the flat node array itself (a coupled leaf,
//     supplied through IterTreeRecordAccess hooks) and the type-field resolve
//     (GameObjectResolveTypeFieldB, also a hook).
//
//   * VIBE_GameLogic_InitGuardState (gilde.exe 0x4520d0) — initialises the
//     town-guard / patrol state block: zeroes counters, seeds a 06:00 game-time
//     stamp via VIBE_GameTime_Set (0x5831f0, inlined here byte-for-byte), and on
//     a successful AI-data-file load writes a fixed table of sprite/action ids.
//     The data-file load (VIBE_AiMethod_LoadDataFile 0x468a40) is a coupled leaf
//     surfaced as a predicate hook.
//
// ODR: VIBE_Amt_CheckGuildRankLevel2 (0x481c18) and the Level-2 dispatch
// (0x520ac8) are ALREADY reconstructed in src/world/guild.{h,cpp} and are NOT
// duplicated here. The Form/Text/FrameLoop dialog shells in the cluster
// (0x5210e4 / 0x521234 / 0x5204e4 / 0x52149c / 0x506d34 / 0x40e1c8) and the
// large per-turn orchestrators (0x4139a8 / 0x52f8d0 / 0x5310a4 / 0x52aa34 /
// 0x530e50) are coupled leaves and are deferred (see the module report).
// ---------------------------------------------------------------------------
#include "guild/common/types.h"

namespace guild::world {

// ===========================================================================
//  VIBE_GameObject_IterNextTree (gilde.exe 0x585488)
// ===========================================================================
//
// The original walks a flat fixed-stride record array (base dword_13CE290,
// node stride 65 bytes for the "type table" indexing at +0, and 67/0x43 bytes
// for the linear scan stride) using raw 32-bit pointers, with this global
// cursor block:
//
//   dword_6498E0  cur          current node pointer (0 == exhausted)
//   dword_6498D0  index        linear index of the current node
//   dword_6498C0  count        node-count upper bound for the linear scan
//   dword_13CE274 stackTop     DFS stack top index (into dword_12356CC[])
//   dword_12356CC stack[]      DFS child-pointer stack
//   byte_6498CF   firstStep    1 on the very first IterNext after QueryBegin
//   byte_6498D4   linearMode   1 == flat linear scan; 0 == tree (child/sibling)
//   byte_6498D5   recurse      1 == push children onto the DFS stack
//   byte_6498D6   matchAll     1 == accept every visited node (no filtering)
//   word_6498C4   filterProto  prototype word filter (0x7FFF == disabled)
//   dword_6498C8  filterLoc    location/parent id filter (-1 == disabled)
//   word_6498CC   filterType   resolved-type-field filter (-1 == disabled)
//   byte_6498CE   filterKind   per-prototype "kind" byte filter (-1 == disabled)
//   dword_13CE27C typeTable    base of the 65-byte-stride prototype "kind" table
//                              (0 == query system not initialised)
//   dword_13CE290 nodeBase     base of the node array; the scan upper address is
//                              nodeBase + 548864 (== 0x85F00; 8192 * 67).
//
// The control flow is translated 1:1: same first-step short-circuits, same
// child-push points, same filter ordering, same do/while continuation
// condition. Pointer arithmetic is preserved by giving the cursor a *typed*
// view of the array through IterTreeRecordAccess (below) instead of raw byte
// pointers; the array itself (entity/object records) is the coupled leaf and is
// NOT owned here.

// Self-contained mirror of the IterNextTree global cursor block. A node is
// identified by a non-null opaque handle (void* in the original); here we use a
// signed node id with kNull (== 0, matching the original's "pointer == 0 means
// none") sentinel so the exhaustion tests translate verbatim.
struct IterTreeCursor {
    static constexpr i32 kNull = 0;
    static constexpr i32 kDfsStackSize = 256;  // dword_12356CC capacity

    // --- live cursor (read AND written each step) ---
    i32 cur      = kNull;   // dword_6498E0
    i32 index    = 0;       // dword_6498D0
    i32 count    = 0;       // dword_6498C0
    i32 stackTop = 0;       // dword_13CE274
    i32 stack[kDfsStackSize] = {};  // dword_12356CC[]

    // --- query parameters (set by QueryBegin; read-only during iteration) ---
    u8  firstStep  = 0;     // byte_6498CF
    u8  linearMode = 0;     // byte_6498D4
    u8  recurse    = 0;     // byte_6498D5
    u8  matchAll   = 0;     // byte_6498D6
    u16 filterProto = 0x7FFF; // word_6498C4 (0x7FFF == disabled)
    i32 filterLoc  = -1;    // dword_6498C8
    i16 filterType = -1;    // word_6498CC
    i8  filterKind = -1;    // byte_6498CE

    // --- environment ---
    bool queryReady = true; // dword_13CE27C != 0
    u32  nodeBase   = 0;    // dword_13CE290 (only its arithmetic role matters)
};

// Coupled-leaf accessors for the node array (the original dereferenced raw
// pointers into the record at byte offsets). All are pure reads except none —
// IterNextTree mutates only cursor state, never the records. Inert defaults
// make every node "empty / no links", so an iteration over an empty/absent
// array terminates exactly as the binary does.
struct IterTreeRecordAccess {
    // *(u16*)(node+0): prototype word at the node's base (0 == FREE slot). Also
    // the value compared against filterProto and used to index the type table.
    u16 (*protoWord)(i32 node) = [](i32) -> u16 { return 0; };
    // *(i32*)(node+0x14): child-list head (childHead). kNull == no children.
    i32 (*childHead)(i32 node) = [](i32) -> i32 { return IterTreeCursor::kNull; };
    // *(i32*)(node+0x3F): next-sibling link. kNull == end of list.
    i32 (*sibling)(i32 node) = [](i32) -> i32 { return IterTreeCursor::kNull; };
    // *(i32*)(node+0x02): the +2 dword the original compares to filterLoc.
    i32 (*locField)(i32 node) = [](i32) -> i32 { return -1; };
    // node advanced by one linear stride (the original's `node += 67`); used
    // only in linearMode. Returns the node at index+1, or kNull when the array
    // owner has none. Default: always kNull (empty array).
    i32 (*nextLinear)(i32 node) = [](i32) -> i32 { return IterTreeCursor::kNull; };
    // *(u8*)(typeTable + 65*protoWord): the per-prototype "kind" byte compared
    // against filterKind. Inert default: 0.
    u8 (*kindByte)(u16 proto) = [](u16) -> u8 { return 0; };
    // VIBE_GameObject_ResolveTypeFieldB(node): the resolved type word compared
    // against filterType. Inert default: -1.
    i16 (*resolveTypeField)(i32 node) = [](i32) -> i16 { return -1; };
    // Address-bound check: the original's loop continues only while the raw node
    // pointer stays below nodeBase + 548864. With opaque ids we model this as a
    // predicate; inert default accepts any non-null node.
    bool (*inRange)(i32 node) = [](i32 n) -> bool { return n != IterTreeCursor::kNull; };
};

// gilde.exe 0x585488 — VIBE_GameObject_IterNextTree.
// Advances the cursor and returns the next matching node id (kNull when the
// query is exhausted). The cursor and access hooks together stand in for the
// original's globals + raw-pointer record reads.
i32 GameObjectIterNextTree(IterTreeCursor& c, const IterTreeRecordAccess& acc);

// ===========================================================================
//  VIBE_GameLogic_InitGuardState (gilde.exe 0x4520d0)
// ===========================================================================
//
// Town-guard / patrol bootstrap. Layout recovered from the field writes:
//   dword_B56450  zeroed
//   dword_B56454  game-time stamp block; seeded to {hour=6, min=0, sec=0}
//   dword_B56464  zeroed
// then, ONLY if VIBE_AiMethod_LoadDataFile succeeds, a fixed sprite/action id
// table (word_B56FAC..word_B56FD0). The original returns the load result (the
// truthiness of the AI-data load); 1 on the populated-table path.

// Mirror of the seeded game-time stamp written by VIBE_GameTime_Set (0x5831f0):
//   *(WORD*)(blk+4) = (sec<<8) | hour   ->  +4 = hour, +5 = sec
//   *(DWORD*)(blk+6) = min               ->  +6 = min  (as dword)
//   *(DWORD*)(blk+10) = sec              ->  +10 = sec  (as dword)
// (the call site passes Set(&blk, 6, 0, 0): a2=hour=6, a3=sec=0, a4=min=0.)
struct GameTimeStamp {
    u8  hour = 0;   // +4
    u8  sec  = 0;   // +5  AND +10 (dword)
    u32 min  = 0;   // +6  (dword)
    u32 secDword = 0; // +10 (dword) — mirrors a3 written wide
};

// Self-contained mirror of the guard-state block (the dword_B56xxx globals that
// InitGuardState touches; the rest of the ~3KB block is untouched here).
struct GuardState {
    i32 counterA = 0;            // dword_B56450
    GameTimeStamp stamp{};       // dword_B56454 .. seeded via GameTime_Set
    i32 counterB = 0;            // dword_B56464[0]

    // Sprite / action id table, written verbatim on the success path. Field
    // names follow the global addresses; values are the exact constants.
    bool tableValid = false;
    i16 wFAC = 0;  // word_B56FAC = 342
    i16 wFAE = 0;  // word_B56FAE = 340
    i16 wFB0 = 0;  // word_B56FB0 = 344
    i16 wFB2 = 0;  // word_B56FB2 = 370
    i16 wFB4 = 0;  // word_B56FB4 = 366
    i16 wFB6 = 0;  // word_B56FB6 = 0
    i16 wFC8 = 0;  // word_B56FC8 = 372
    i16 wFCA = 0;  // word_B56FCA = 374
    i16 wFCC = 0;  // word_B56FCC = 350
    i16 wFCE = 0;  // word_B56FCE = 352
    i16 wFD0 = 0;  // word_B56FD0 = 0
};

// gilde.exe 0x5831f0 — VIBE_GameTime_Set, inlined exactly as used by
// InitGuardState. (a1=&stamp, a2=hour, a3=sec, a4=min in the call convention.)
void GameTimeSet(GameTimeStamp& s, u8 hour, u8 sec, u8 min);

// gilde.exe 0x4520d0 — VIBE_GameLogic_InitGuardState.
// `aiDataFileLoaded` is the coupled-leaf predicate standing in for
// VIBE_AiMethod_LoadDataFile (0x468a40). Returns the original's int result:
// 0 when the AI-data load fails (table left untouched), 1 on success.
int GameLogicInitGuardState(GuardState& g, bool aiDataFileLoaded);

// Shared guard-state instance — the in-tree mirror of the dword_B56xxx global
// block the original InitGuardState writes (the engine keeps it as process
// globals). One instance for the live engine-init path to mutate, so the
// reconstructed InitGuardState edge has a real target. (Rule 13 integration.)
GuardState& GuardStateGlobal();

} // namespace guild::world
