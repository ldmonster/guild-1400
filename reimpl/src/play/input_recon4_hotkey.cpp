// ===========================================================================
// gilde.exe — recon4 hotkey / scancode / drag-grid PURE-logic reconstruction.
// See input_recon4_hotkey.h for the cluster map and platform-boundary notes.
// ===========================================================================
#include "play/input_recon4_hotkey.h"

namespace guild::play {

// --- live globals -----------------------------------------------------------
HotkeySlot      g_hotkeySlots[kHotkeySlotCount];     // byte_122DC10
u8              g_lastHotkeyChar   = 0;              // byte_67225C
u8              g_hotkeySuppress   = 0;              // byte_671D8A
u16             g_hotkeySelWord    = 0;              // word_63CC5C
u8              g_hotkeyActiveChar = 0;              // byte_620B28
HotkeySelection g_hotkeySelection;                   // dword_631744/8/C + 6477A4
i32             g_dragGridTable[kDragGridDwords];     // dword_1232C30

namespace {
HotkeyHooks                  g_hooks;
std::function<u16(int)>      g_scancodeTable;

u8* hkBuilding(i32 id)       { return g_hooks.buildingFindById ? g_hooks.buildingFindById(id) : nullptr; }
u8* hkObject(i32 id)         { return g_hooks.objectFindById ? g_hooks.objectFindById(id) : nullptr; }
} // namespace

void Hotkey_SetHooks(const HotkeyHooks& hooks) { g_hooks = hooks; }
void Input_SetScancodeTable(std::function<u16(int)> table) { g_scancodeTable = std::move(table); }

void Hotkey_ClearSlots() {
    for (int i = 0; i < kHotkeySlotCount; ++i) {
        g_hotkeySlots[i].hotkeyChar = 0;
        g_hotkeySlots[i].buildingId = -1;
        g_hotkeySlots[i].objectId   = -1;
    }
}

// ---------------------------------------------------------------------------
// gilde.exe 0x4fee18 — VIBE_Hotkey_ValidateAssignments
//
//   for ( i = 0; i < 11; ++i )
//     if ( slot[i].buildingId != -1 ) {
//       bld = FindById(slot[i].buildingId);
//       if ( !bld )                 { slot[i].buildingId = slot[i].objectId = -1; }
//       else {
//         obj = FindObjectById(slot[i].objectId);
//         if ( !obj && slot[i].objectId != -1 ) { slot[i].buildingId = slot[i].objectId = -1; }
//         else if ( (ComputeSelectionFlags(word_63CC5C, bld, 0, obj) & 1) == 0 )
//                                     { slot[i].buildingId = slot[i].objectId = -1; }
//       }
//     }
//
// Note the two distinct -1 clears in the decompile (`v4` path vs `v6+4/+8` path
// vs `v0+4/+8` path) all write the SAME slot's +4/+8 dwords — disasm confirms
// every store targets the slot base of the current iteration (ecx = base+12*i,
// esi = base+12*i).  So all three branches are identical: clear buildingId and
// objectId of slot[i].
// ---------------------------------------------------------------------------
void Hotkey_ValidateAssignments() {
    for (int i = 0; i < kHotkeySlotCount; ++i) {
        HotkeySlot& s = g_hotkeySlots[i];
        if (s.buildingId == -1)
            continue;
        u8* bld = hkBuilding(s.buildingId);
        if (!bld) {
            s.buildingId = -1;
            s.objectId   = -1;
            continue;
        }
        u8* obj = hkObject(s.objectId);
        if (!obj && s.objectId != -1) {
            s.buildingId = -1;
            s.objectId   = -1;
            continue;
        }
        int flags = g_hooks.buildingComputeSelectionFlags
                        ? g_hooks.buildingComputeSelectionFlags(g_hotkeySelWord, bld, 0, obj)
                        : 0;
        if ((flags & 1) == 0) {
            s.buildingId = -1;
            s.objectId   = -1;
        }
    }
}

// ---------------------------------------------------------------------------
// gilde.exe 0x4feec4 — VIBE_Hotkey_ActivateBuilding
//
//   if ( byte_67225C && !byte_671D8A ) {
//     for ( i=0; ; i+=1 (record step) ) {       // dword view step 3 == record step 1
//       if ( byte_67225C == slot[i].hotkeyChar && !byte_671D8A ) {
//         ValidateAssignments();
//         if ( slot[i].buildingId != -1 ) break;
//       }
//       if ( ++recordsScanned reaches 11 ) return;   // v0 >= 33
//     }
//     bld = FindById(slot[i].buildingId);   // return value unused by the open
//     obj = FindObjectById(slot[i].objectId);
//     if ( obj ) Dialog_OpenBuildingForActiveChar(*(obj+2), <lo>, byte_620B28);
//     else       Dialog_OpenBuildingForActiveChar(-1, <lo>, byte_620B28);
//   }
//
// The original's `v3`/`v0` low dword passed to the dialog is uninitialised ecx
// (LODWORD(v4) = v3); we pass 0 for that filler arg as the dialog only consumes
// the object-id high dword + the active-char byte in practice.
// ---------------------------------------------------------------------------
void Hotkey_ActivateBuilding() {
    if (!g_lastHotkeyChar || g_hotkeySuppress)
        return;
    int i = 0;
    for (; i < kHotkeySlotCount; ++i) {
        if (g_lastHotkeyChar == g_hotkeySlots[i].hotkeyChar && !g_hotkeySuppress) {
            Hotkey_ValidateAssignments();
            if (g_hotkeySlots[i].buildingId != -1)
                break;
        }
    }
    if (i >= kHotkeySlotCount)
        return;

    HotkeySlot& s = g_hotkeySlots[i];
    (void)hkBuilding(s.buildingId); // FindById return unused (matches original)
    u8* obj = hkObject(s.objectId);
    if (!g_hooks.dialogOpenBuilding)
        return;
    if (obj) {
        // HIDWORD(v4) = *(obj+2): the object record's id field at byte offset +2.
        i32 objField2 = 0;
        // Read 4 bytes at obj+2 (matches *(_DWORD*)(ObjectById + 2)).
        for (int b = 0; b < 4; ++b)
            objField2 |= static_cast<i32>(obj[2 + b]) << (8 * b);
        g_hooks.dialogOpenBuilding(objField2, 0, g_hotkeyActiveChar);
    } else {
        g_hooks.dialogOpenBuilding(-1, 0, g_hotkeyActiveChar);
    }
}

// ---------------------------------------------------------------------------
// gilde.exe 0x4fef54 — VIBE_Hotkey_AssignFromSelection (eax=slot, edi=fallback)
//
// Selection precedence (disasm 0x4fef61..0x4fefe3):
//   sel = dword_631744; if (!sel) sel = dword_631748;
//   byteOff = 12*slot;   // (slot*4 - slot) << 2
//   if ( sel ) {
//     obj = dword_63174C;
//     slot.buildingId = *(sel+1);                     // [edx+1] (4-byte read @+1)
//     slot.objectId   = obj ? *(obj+2) : -1;          // [ebp+2] (4-byte read @+2)
//     <status banner>
//   } else {
//     slot.buildingId = *(dword_6477A4 + 1);          // fallback building
//     slot.objectId   = -1;
//     <status banner>
//   }
//
// In the headless reconstruction the records' id fields are pre-resolved into
// g_hotkeySelection (buildingId / objectId / fallbackBuildingId); the byte-offset
// reads above are what produced those ids.  The banner/text path is the coupled
// HUD leaf — routed through hudStatusBanner.  Returns 0 (the banner result).
// ---------------------------------------------------------------------------
int Hotkey_AssignFromSelection(i32 slot, u32 /*a2*/) {
    HotkeySlot& s = g_hotkeySlots[slot];
    if (g_hotkeySelection.hasBuilding) {
        s.buildingId = g_hotkeySelection.buildingId;
        s.objectId   = g_hotkeySelection.hasObject ? g_hotkeySelection.objectId : -1;
    } else {
        s.buildingId = g_hotkeySelection.fallbackBuildingId;
        s.objectId   = -1;
    }
    if (g_hooks.hudStatusBanner)
        g_hooks.hudStatusBanner(slot);
    return 0;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x4ff954 — VIBE_Hotkey_StoreDefaultEntry (edx:eax = building:slot)
//
//   obj = 0;
//   if ( *(u8*)building != 29 ) {                       // building type byte +0
//     obj = FindWorkProductObject(building);
//     if ( !obj ) obj = FindStorableObject(building);
//   }
//   slot.buildingId = *(building+1);                     // 4-byte read @+1
//   slot.objectId   = obj ? *(obj+2) : -1;               // 4-byte read @+2
//   return 12*slot;                                      // eax
//
// The decompile shows `*(WorkProductObject + 1)` with WorkProductObject typed
// `__int16 *` — pointer arithmetic, i.e. BYTE offset +2 (disasm 0x4ff97a:
// `mov edx, [ecx+2]`), the SAME object-id field AssignFromSelection /
// ActivateBuilding read at *(obj+2).
// ---------------------------------------------------------------------------
namespace {
i32 read4at(u8* p, int off) {
    i32 v = 0;
    for (int b = 0; b < 4; ++b)
        v |= static_cast<i32>(p[off + b]) << (8 * b);
    return v;
}
} // namespace

int Hotkey_StoreDefaultEntry(i32 slot, u8* building) {
    u8* obj = nullptr;
    if (building[0] != 29) {
        if (g_hooks.buildingFindWorkProduct)
            obj = g_hooks.buildingFindWorkProduct(building);
        if (!obj && g_hooks.buildingFindStorable)
            obj = g_hooks.buildingFindStorable(building);
    }
    HotkeySlot& s = g_hotkeySlots[slot];
    s.buildingId = read4at(building, 1);
    s.objectId   = obj ? read4at(obj, 2) : -1;  // [ecx+2] @0x4ff97a
    return 12 * slot;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x40c790 — VIBE_Input_CharToScancode
//
//   switch ( ch ) {                       // 12 hard-coded arrow/keypad cases
//     case 'R': return 48; ... case 'N': return 43;
//   }
//   key = ch;                             // v5 = a3 (ch in the low byte)
//   if ( shift ) BYTE1(key)  = 1;
//   if ( ctrl )  BYTE1(key) |= 2;
//   if ( alt )   BYTE1(key) |= 4;
//   for ( idx = 0; word_671960[idx] != (u16)key; ) {
//     if ( ++idx >= 256 ) return 0;
//   }
//   return idx;                           // (the original's v6 == idx counter)
//
// The high byte of the search key is REPLACED (`=1`) by shift, then OR'd with
// ctrl/alt — exactly as the original (shift assigns, the others or-in).  The
// comparison is a 16-bit compare against the table entries (the -1 sentinels the
// table builder writes never equal a key whose high byte is 0/1/3/5/7).
// ---------------------------------------------------------------------------
u8 Input_CharToScancode(int shift, int ctrl, u8 ch, int alt) {
    switch (ch) {
        case 'R': return 48;
        case 'O': return 49;
        case 'P': return 50;
        case 'Q': return 51;
        case 'K': return 52;
        case 'L': return 53;
        case 'M': return 54;
        case 'G': return 55;
        case 'H': return 56;
        case 'I': return 57;
        case 'J': return 45;
        case 'N': return 43;
        default: break;
    }
    // key = ch (low byte); high byte = modifier bits.
    u16 key = ch;
    u8  hi  = 0;
    if (shift) hi  = 1;
    if (ctrl)  hi |= 2;
    if (alt)   hi |= 4;
    key = static_cast<u16>((key & 0x00FF) | (static_cast<u16>(hi) << 8));

    for (int idx = 0; idx < 256; ++idx) {
        u16 entry = g_scancodeTable ? g_scancodeTable(idx) : 0;
        if (entry == key)
            return static_cast<u8>(idx);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x54f884 — VIBE_DragSlot_ResetGridTable   (PURE)
//
// 32 rows, 64-byte (16-dword) stride at dword_1232C30:
//   row[0..3] (dwords +0..+3)         = { 0, -1, -1, -1 }
//   then byte offsets 16,24,32,40,48,56 within the row (k=8..48 step 8 over the
//   sub-array based at row+8): id dword = -1; qty word = 0.
//
// Disasm: eax runs (i<<6)+8, +8 .. until == (i*64 + 0x30); each step writes
//   dword_1232C38[eax] = -1   (== row+8 + (eax-? )) and word @ dword_1232C3C[eax].
// Concretely the dword stores land at absolute byte (0x1232C38 + 64*i + k) for
// k in {8,16,24,32,40,48}, i.e. row byte offsets {16,24,32,40,48,56}; the word
// stores at row byte offsets {20,28,36,44,52,60}.  The function returns the
// final `result` = last eax = 64*31 + 48 = 2032.
//
// We model dword_1232C30 as a flat 512-dword block, zero-init it first (so the
// padding bytes the word writes don't touch read as 0), then replay the writes.
// ---------------------------------------------------------------------------
int DragSlot_ResetGridTable() {
    for (int d = 0; d < kDragGridDwords; ++d)
        g_dragGridTable[d] = 0;

    int result = 0;
    for (int i = 0; i < kDragGridRows; ++i) {
        i32* row = &g_dragGridTable[i * kDragGridRowDwords];
        row[0] = 0;   // [ecx+0]
        row[1] = -1;  // [ecx+4]
        row[2] = -1;  // [ecx+8]
        row[3] = -1;  // [ecx+0Ch]

        // Inner do/while: eax starts at (i<<6); each pass does eax += 8 BEFORE the
        // stores, and continues while eax != (i<<6 + 0x30).  So eax takes the row
        // byte values 64*i+8, +16, ... +48 (six passes).
        int eax = i << 6;
        const int limit = (i << 6) + 0x30;
        do {
            eax += 8;
            const int withinRow = eax - (i << 6); // 8,16,24,32,40,48
            // id dword: absolute byte = base + 8 + eax  ->  row byte (8 + withinRow)
            //           = 16,24,32,40,48,56  ->  dword index 4,6,8,10,12,14.
            row[(8 + withinRow) / 4] = -1;
            // qty word: absolute byte = base + 12 + eax -> row byte (12 + withinRow)
            //           = 20,28,...,60.  The holding dword (index 5,7,...,15) is
            //           already zero-initialised and the original writes 0, so this
            //           leaves it 0 — modelled by the leading zero-init.
        } while (eax != limit);
        result = eax; // final eax = 64*31 + 48 = 2032 (the original's `result`).
    }
    return result;
}

} // namespace guild::play
