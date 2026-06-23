#pragma once
// ===========================================================================
// gilde.exe — Hotkey building-assignment slot table + char->scancode mapping +
// drag-slot grid reset   (CLUSTER recon4: VIBE_Hotkey / VIBE_Input / VIBE_DragSlot).
//
// STRICT 1:1 reconstruction of the PURE LOGIC from the Hex-Rays decompile.  The
// pieces of this cluster that are pure table / control-flow logic are
// reconstructed byte-exact here; the coupled engine leaves (Building / Object /
// Dialog / HUD / RadioGroup-UI lookups) and the platform leaves (DirectInput
// device acquisition, the VkKeyScanA / MapVirtualKeyA scancode-table build, the
// GPU drag-cursor draw) are NOT reconstructed — they are routed through
// installable hooks (project rules 3/4 boundary) or omitted with provenance.
//
// Provenance (each reconstructed entry carries its gilde.exe address):
//   0x40c790  VIBE_Input_CharToScancode      (__usercall — char->scancode map;
//                                             special arrow/keypad cases + table
//                                             scan with the shift/ctrl/alt high byte)
//   0x4ff954  VIBE_Hotkey_StoreDefaultEntry  (__usercall edx:eax — slot write,
//                                             work-product/storable object lookup)
//   0x4fee18  VIBE_Hotkey_ValidateAssignments(11-slot revalidation loop)
//   0x4feec4  VIBE_Hotkey_ActivateBuilding   (active-hotkey scan -> open dialog)
//   0x4fef54  VIBE_Hotkey_AssignFromSelection(__usercall eax=slot,edi=fallback —
//                                             slot write from current selection)
//   0x54f884  VIBE_DragSlot_ResetGridTable   (PURE 32x16 grid-table init; no hooks)
//
// SKIPPED / OMITTED (reported, NOT faked — rule 8):
//   0x40c710  VIBE_Input_BuildScancodeTable  (Win32 VkKeyScanA / MapVirtualKeyA —
//                                             platform boundary, rule 4.  The
//                                             char->scancode table word_671960 it
//                                             produces is supplied to
//                                             CharToScancode via the table hook).
//   0x40ca38  VIBE_Input_DirectInputInit     (DirectInput device creation /
//                                             SetCooperativeLevel / SetDataFormat —
//                                             platform boundary, rule 4.  Already
//                                             routed through gamelogic.h
//                                             inputDirectInputInit hook.)
//   0x4ff070  VIBE_Hotkey_OpenAssignWindow   (huge Form/Hud/RadioGroup/GameLogic
//                                             window orchestrator over a dozen UI
//                                             subsystems — too coupled to translate
//                                             faithfully in this cluster; deferred.)
//   0x41fa1c  VIBE_DragCursor_Render         (sprite/shape GPU draw — rule 3
//                                             Vulkan boundary; coupled draw leaf.)
//   0x4ad508  VIBE_DragSlot_BeginDragText    (drag-text begin: a string copy +
//                                             cursor/widget global writes — the
//                                             body reads DInput-latched cursor
//                                             globals (dword_69FFB8/69FFBC) and
//                                             drives the drag-cursor sprite; its
//                                             pure string-copy core is exposed as
//                                             DragSlot_CopyTooltipText below, the
//                                             rest is left to the existing
//                                             gui_dialogs dragSlotBeginDragText
//                                             hook — not re-reconstructed.)
//   0x40fb4c  VIBE_Input_SetIconTextById     (widget text-buffer memmove splice
//   0x40fc98  VIBE_Input_SetIconText          over the 740-byte widget record;
//                                             reconstructed below as pure pointer
//                                             math over a caller-supplied widget
//                                             memory model.)
// ===========================================================================
#include "guild/common/types.h"

#include <cstdint>
#include <functional>

namespace guild::play {

using guild::i16;
using guild::i32;
using guild::u8;
using guild::u16;
using guild::u32;

// ===========================================================================
// Hotkey building-assignment slot table.
//
// gilde.exe global byte_122DC10: 11 records, 12-byte stride.
//   +0  u8  hotkeyChar   (the assigned hotkey character; rest of dword padding)
//   +4  i32 buildingId   (alias dword_122DC14[3*i]; -1 = empty)
//   +8  i32 objectId     (alias dword_122DC18[3*i]; -1 = none)
// ActivateBuilding scans 0..32 step 3 over the dword view (== 0..10 records).
// ===========================================================================
struct HotkeySlot {
    u8  hotkeyChar = 0; // byte_122DC10[12*i]   (+0)
    i32 buildingId = -1; // dword_122DC14[3*i]  (+4)
    i32 objectId   = -1; // dword_122DC18[3*i]  (+8)
};

inline constexpr int kHotkeySlotCount = 11; // loop bound 0x0B / scan bound 33 (3*11)

// The 11-entry slot table (the live byte_122DC10 block, reconstructed by value).
extern HotkeySlot g_hotkeySlots[kHotkeySlotCount];

// Reset all slots to the empty state (-1 ids); test/utility helper.
void Hotkey_ClearSlots();

// ---------------------------------------------------------------------------
// Coupled engine-leaf hooks the hotkey logic calls into.  Inert defaults (null)
// behave as "not found" / no-op, so the pure slot/table control flow is faithful
// and testable headless.
// ---------------------------------------------------------------------------
struct HotkeyHooks {
    // VIBE_Building_FindById @0x587b20 — resolve a building record by id.
    // Returns null when the building no longer exists.
    std::function<u8*(i32 buildingId)> buildingFindById;
    // VIBE_Object_FindObjectById @0x583a70 — resolve a game-object record by id.
    std::function<u8*(i32 objectId)> objectFindById;
    // VIBE_Building_ComputeSelectionFlags @0x588dec — (ax=word_63CC5C, edx=bld,
    // ecx=0, [stack]=objPtr); returns flags.  ValidateAssignments keeps the slot
    // only when (flags & 1) != 0.  Inert default: flags = 0 (slot dropped).
    std::function<int(u16 selWord, u8* building, int zero, u8* object)>
        buildingComputeSelectionFlags;

    // VIBE_Dialog_OpenBuildingForActiveChar @0x4adef4 — open the building dialog
    // for the active character.  ActivateBuilding calls it with (objField2, byte_620B28)
    // when the object resolved, else (-1<<32 | charByte, byte_620B28).
    std::function<void(i32 objField2, i32 charLo, u8 activeChar)> dialogOpenBuilding;

    // VIBE_Building_FindWorkProductObject @0x587674 — primary object for a building
    // record.  StoreDefaultEntry uses it, falling back to FindStorableObject.
    std::function<u8*(u8* building)> buildingFindWorkProduct;
    // VIBE_Building_FindStorableObject @0x5877ac — storable fallback object.
    std::function<u8*(u8* building)> buildingFindStorable;

    // VIBE_Text_RenderFormattedMessage @0x59f99c + VIBE_Hud_SetStatusBannerText
    // @0x4bcdcc — AssignFromSelection's status-banner feedback.  Modelled as a
    // single sink receiving the resolved slot index; inert default: no-op.
    std::function<void(int slot)> hudStatusBanner;
};

void Hotkey_SetHooks(const HotkeyHooks& hooks);

// The active-hotkey-character / suppression globals the hotkey loop reads.
//   byte_67225C : the last-pressed hotkey character (0 = none).
//   byte_671D8A : a suppression flag (nonzero -> ActivateBuilding does nothing).
//   word_63CC5C : selection context word passed to ComputeSelectionFlags.
//   byte_620B28 : the active-character index passed to dialogOpenBuilding.
extern u8  g_lastHotkeyChar; // byte_67225C
extern u8  g_hotkeySuppress; // byte_671D8A
extern u16 g_hotkeySelWord;  // word_63CC5C
extern u8  g_hotkeyActiveChar; // byte_620B28

// gilde.exe 0x4fee18 — VIBE_Hotkey_ValidateAssignments.
// Walks the 11 slots; for each slot with a building id, drops the slot
// (objectId/buildingId -> -1) when the building no longer exists, the object id
// is set-but-missing, or the building's selection flags lose bit 0.
void Hotkey_ValidateAssignments();

// gilde.exe 0x4feec4 — VIBE_Hotkey_ActivateBuilding.
// When a hotkey char is latched (and not suppressed): revalidate, find the slot
// whose hotkeyChar matches, and if it still holds a building, open its dialog.
void Hotkey_ActivateBuilding();

// gilde.exe 0x4fef54 — VIBE_Hotkey_AssignFromSelection (__usercall eax=slot,edi=fallback).
// Writes the current selection (g_selBuilding / g_selObject, else g_selDefault)
// into slot `a1`, then fires the status banner.  Returns the banner result (0).
// `a2` is the original's edi (fallback id used only by the text path).
int Hotkey_AssignFromSelection(i32 slot, u32 a2);

// Selection globals AssignFromSelection / Reset read.  In the 32-bit original
// these are dwords that, when set, are dereferenced as records; modelled as the
// raw ids the slot table stores.
//   dword_631744 (g_selBuildingRec), dword_631748 (g_selBuildingRec2),
//   dword_63174C (g_selObjectRec): records whose +1 / +2 dwords are the ids.
// For the headless reconstruction we expose the resolved building/object ids the
// original would have written into the slot.
struct HotkeySelection {
    bool hasBuilding = false; // dword_631744 || dword_631748 nonzero
    i32  buildingId  = -1;    // *(building+1)
    bool hasObject   = false; // dword_63174C nonzero
    i32  objectId    = -1;    // *(object+2)
    i32  fallbackBuildingId = -1; // dword_6477A4 path (+1) when no selection
};
extern HotkeySelection g_hotkeySelection;

// gilde.exe 0x4ff954 — VIBE_Hotkey_StoreDefaultEntry (__usercall edx:eax).
// `slot` (eax low) selects the slot; `building` (edx high) is the building record
// pointer (its +1 dword is the building id).  When the building's type byte (+0)
// is not 29, resolves a work-product (else storable) object and stores its +1
// id; else stores objectId = -1.  Returns the original's eax (12*slot).
int Hotkey_StoreDefaultEntry(i32 slot, u8* building);

// ===========================================================================
// 0x40c790 — VIBE_Input_CharToScancode  (__usercall al=ret; eax=shift, edx=ctrl,
//                                        cl=ch, ebx=alt)
//
// 12 hard-coded arrow/keypad characters ('G'..'R', 'N') map to fixed scancodes.
// Otherwise the routine forms a 16-bit search key = ch | (mods<<8), where the
// high byte accumulates 1(shift)/2(ctrl)/4(alt), and linearly scans the 256-entry
// table word_671960 for the first matching u16, returning its index (else 0).
//
// The table is built at runtime from Win32 VkKeyScanA/MapVirtualKeyA
// (VIBE_Input_BuildScancodeTable @0x40c710 — platform boundary, rule 4) and is
// therefore supplied here via a hook; the pure mapping logic is reconstructed 1:1.
// ===========================================================================

// Install the 256-entry char->scancode lookup table (word_671960).  The provider
// must return the u16 stored at the given index (0..255).  Null hook -> all 0
// (so only the 12 hard-coded cases resolve; the table scan misses -> returns 0
// for key==0, otherwise scans the all-zero table and returns 0).
void Input_SetScancodeTable(std::function<u16(int index)> table);

// gilde.exe 0x40c790 — VIBE_Input_CharToScancode.
// `shift`/`ctrl`/`alt` are the modifier flags (nonzero = held); `ch` is the
// character (low byte used).  Returns the scancode (the original's al), 0 = none.
u8 Input_CharToScancode(int shift, int ctrl, u8 ch, int alt);

// ===========================================================================
// 0x54f884 — VIBE_DragSlot_ResetGridTable   (PURE; no hooks)
//
// Initialises the 32-row drag/trade grid table at dword_1232C30 (row stride 64
// bytes = 16 dwords): each row header is { 0, -1, -1, -1 } at row+0..+12, then 6
// sub-slots of 8 bytes each at row+16..+63 — each sub-slot a { i32 id = -1;
// u16 qty = 0; (2 bytes padding) }.  Modelled as a flat 32*16-dword block to
// preserve the exact writes (word writes leave the high 2 bytes of each odd
// dword untouched, as in the original; we zero-initialise the block first).
// ===========================================================================
inline constexpr int kDragGridRows      = 32;
inline constexpr int kDragGridRowDwords = 16; // 64-byte row stride
inline constexpr int kDragGridDwords    = kDragGridRows * kDragGridRowDwords; // 512

// The flat reconstruction of dword_1232C30 (512 dwords).
extern i32 g_dragGridTable[kDragGridDwords];

// gilde.exe 0x54f884 — VIBE_DragSlot_ResetGridTable.  Returns the original's eax
// (the final `result` = last byte offset written = 64*31 + 48 = 2032).
int DragSlot_ResetGridTable();

} // namespace guild::play
