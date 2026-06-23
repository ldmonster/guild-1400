#pragma once
// gui_object_state.{h,cpp} — the GUI/entity object-state mini state-machine (gilde.exe)
//
//   0x40e9e8  VIBE_State_Update          — realise a record's render state, return handle
//   0x40eaf0  VIBE_Gui_ResolveObjectState— resolve the live record (state handle + index)
//   0x412ea4  VIBE_Gui_MarkObjectUsed    — bump a record's use-count + frame stamp
//
// These three operate on the engine's flat 84-byte "object/entity state" record array
// (base dword_62D204, count dword_62D208). They are the per-frame object-state
// realisation helpers the render-submit walk (render_submit.cpp:
// VIBE_GameLogic_Objects/Entities @0x412fa0/0x413580) and VIBE_Object_Update call;
// render_submit.h declares them as the resolveObjectState/markObjectUsed/stateUpdate
// hooks — this module provides their faithful bodies (callers can bind the hooks to
// these via a thin shim, or call them directly).
//
// RECORD FIELDS (84-byte stride; the offsets these three functions touch):
//   +0x30 (+48)  load offset / redirect-suppress (State_Helper file seek key; when a
//                5/8 record's +48 is 0 it redirects to its +76 partner)
//   +0x34 (+52)  render-state handle (0 => not yet realised; State_Helper loads it)
//   +0x38 (+56)  byte size of the loaded record (State_Helper alloc/account size)
//   +0x3C (+60)  kind/type dword (5,8 => linked-pair record)
//   +0x40 (+64)  use-count (MarkObjectUsed ++)
//   +0x44 (+68)  flag byte (bit 0x02 => add the grid offset byte_62D220 to the index)
//   +0x48 (+72)  last-used frame stamp (MarkObjectUsed = dword_62EB38)
//   +0x4C (+76)  partner record index (for a 5/8 linked pair)
//
// ENGINE GLOBALS (modeled in the StateContext, owned by the caller — same decoupling
// the sibling leaves in misc_recon4_leaves.h use):
//   dword_62D204  record array base          -> StateContext.records / recordCount
//   byte_62D220   grid-cell index offset     -> StateContext.gridOffset
//   dword_62D2A4  redirect-delta publish     -> StateContext.redirectDelta (out)
//   dword_62EB38  current frame counter      -> StateContext.frameStamp
//   VIBE_State_Helper @0x40e014  the lazy resource load (d2_LoadObj: VFS open + read +
//                ShapeBank convert) — a genuine file/VFS boundary (rule 6), injected as
//                the realiseHook. It must set record[+52] nonzero on success.

#include "guild/common/types.h"

namespace guild::gui {

using guild::i16;
using guild::i32;
using guild::u8;
using guild::u32;

// One 84-byte object-state record as these three functions read it. Only the touched
// fields are named; the remainder is opaque padding so the stride is byte-exact (this
// is the same array render_submit.h models as EntitySubmitRecord, viewed for the
// state-machine fields — kept independent here to avoid a cross-module include).
#pragma pack(push, 1)
struct ObjectStateRecord {
    u8  pad0[48];     // +0x00
    i32 loadOffset;   // +0x30 (+48) file seek key / redirect-suppress word
    i32 stateHandle;  // +0x34 (+52) realised render-state handle (0 => unrealised)
    i32 byteSize;     // +0x38 (+56) loaded record size (resource accounting)
    i32 kind;         // +0x3C (+60) kind/type (5,8 => linked pair)
    i32 useCount;     // +0x40 (+64) bumped by MarkObjectUsed
    i32 flag68;       // +0x44 (+68) bit 0x02 => add gridOffset to the index
    i32 frameStamp;   // +0x48 (+72) last-used frame (MarkObjectUsed = frameStamp)
    i32 linkIndex;    // +0x4C (+76) partner record index (5/8 pair)
    u8  pad1[4];      // +0x50 .. +0x53 (subState words; untouched by these three fns)
};
#pragma pack(pop)
static_assert(sizeof(ObjectStateRecord) == 84, "ObjectStateRecord must be 84 bytes");

// The engine global state these functions read/write, made injectable so this TU is
// self-contained (the caller owns the real globals). `redirectDelta` mirrors the
// original dword_62D2A4 side effect: it is reset to 0 at the top of State_Update /
// ResolveObjectState and set to (v3 - v6) when a 5/8 record redirects.
struct StateContext {
    ObjectStateRecord* records = nullptr;  // dword_62D204
    int                recordCount = 0;    // (informational; the originals do not bound-check)
    u8                 gridOffset = 0;      // byte_62D220
    i32                frameStamp = 0;      // dword_62EB38 (current frame counter)
    i32                redirectDelta = 0;   // dword_62D2A4 (out side effect)

    // VIBE_State_Helper @0x40e014 — lazily load record `idx` into its +52 handle.
    // The original: if record[+52]==0, evict LRU until it fits, alloc record[+56]
    // bytes, open the VFS stream, seek to record[+48], read it, (kind<16 =>) convert
    // via ShapeBank, account the size, close. It returns 1 on success / 0 on OOM.
    // This is a genuine VFS/resource boundary (rule 6) -> injected hook. A bound hook
    // MUST set records[idx].stateHandle nonzero on success (the originals gate on it).
    // The second arg mirrors State_Helper's `a2` (an optional alternate stream path;
    // the engine always passes 0 from these three callers).
    int (*realiseHook)(StateContext* ctx, int idx, int streamArg) = nullptr;
};

// gilde.exe 0x40e9e8 — VIBE_State_Update(idx@eax).
// Realises object `idx`'s render state and returns the resolved state handle.
//   redirectDelta = 0;
//   if (record[idx].flag68 & 2) idx += gridOffset;
//   if ((kind==5||kind==8) && record[idx].loadOffset==0) {
//       if (record[partner].stateHandle==0) realise(partner);
//       redirectDelta = idx - partner;  idx = partner;
//   } else if (record[idx].stateHandle==0) realise(idx);
//   return record[idx].stateHandle;
i32 StateUpdate(StateContext& ctx, int idx);

// gilde.exe 0x40eaf0 — VIBE_Gui_ResolveObjectState(idx@eax, outState@edx, outIndex@ebx).
// Same redirect logic as StateUpdate, but instead of returning the handle it writes
// *outState = record[resolved].stateHandle and *outIndex = the (grid-adjusted, pre-
// redirect) index, then returns 1. Always returns 1. (When a 5/8 record redirects,
// outState is the PARTNER's handle but outIndex is the adjusted-but-not-redirected idx
// — matching the original's v6 vs v3 split.)
int ResolveObjectState(StateContext& ctx, int idx, i32* outState, int* outIndex);

// gilde.exe 0x412ea4 — VIBE_Gui_MarkObjectUsed(idx@eax).
// Resolves a 5/8 record to its +76 partner (only when loadOffset==0), then bumps that
// record's use-count (+64) and stamps it with the current frame (+72 = frameStamp).
// Returns the resolved record's byte offset (84*resolvedIdx) as the original does
// (`result` is the record byte address; here the resolved index so callers stay
// host-pointer-clean). Note: unlike Update/Resolve, MarkObjectUsed does NOT apply the
// flag68 grid offset (the original omits that test).
int MarkObjectUsed(StateContext& ctx, int idx);

} // namespace guild::gui
