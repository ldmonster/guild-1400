// History / Chronicle — chronicle "cart" group-slot table lifecycle.
// 1:1 port of the table-reset cores of the VIBE_History_* family (gilde.exe):
//   VIBE_History_ResetGroupSlot      0x4fd140
//   VIBE_History_ResetChronicleState 0x4fd090  (table-reset portion)
//   VIBE_History_FreeChronicleFiles  0x4fd194  (table-reset portion)
//   VIBE_History_ParseCommandlineFirstPass 0x4fd6ac (group-index core)
//
// See history_cart_table.h for the table layout. The original aliases two dword
// pointers (dword_122DADC == dword_122DAE0 - 1 dword) onto one 4x17 dword array:
// an id written through DADC at dword index (17g + 2k + 2) lands at struct dword
// (17g + 2k + 1), and the kind low byte written through DAE0 at the same index
// (17g + 2k + 2) is the next dword. Both map to slots[k] = {id, kind}.
#include "world/history_cart_table.h"

namespace guild::world {

// gilde.exe 0x4fd140 — VIBE_History_ResetGroupSlot
//   (__usercall __spoils<>@<eax>(unsigned __int16 a1@<ax>)).
// Original:
//   if (a1 >= 4u) return 0;
//   dword_122DAE0[17*a1] = 1;
//   for (i=0; i!=8; LOBYTE(dword_122DAE0[v3+i]) = -1)
//   { v3 = 17*a1; i += 2; dword_122DADC[v3+i] = -1; }
//   return 1;
// The byte counter i = 2,4,..,16 walks all 8 slots: id at DADC index (17a1+i)
// == struct dword (17a1+i-1), kind low byte at DAE0 index (17a1+i). Slot k uses
// i = 2k+2, so all 8 slots are freed.
bool HistoryResetGroupSlot(CartTable& table, unsigned group)
{
    if (group >= static_cast<unsigned>(kCartGroupCount)) // a1 >= 4u
        return false;

    CartGroup& g = table.groups[group];
    g.active = 1;                       // dword_122DAE0[17*a1] = 1
    for (int k = 0; k < kCartSlotCount; ++k) // i = 2,4,..,16 -> slot 0..7
    {
        g.slots[k].id   = -1;           // dword_122DADC[...] = -1
        g.slots[k].kind = 0xFF;         // LOBYTE(dword_122DAE0[...]) = -1
    }
    return true;
}

// gilde.exe 0x4fd090 — VIBE_History_ResetChronicleState (table-reset portion) and
// gilde.exe 0x4fd194 — VIBE_History_FreeChronicleFiles  (table-reset portion).
// Both share:
//   v1 = 32;
//   for (i=0; i<4; ++i) {
//     v3 = 68*i; dword_122DAE0[17*i] = 1;
//     do { v3 += 8; dword_122DADC[v3/4] = -1; LOBYTE(dword_122DAE0[v3/4]) = -1; }
//     while (v3 != v1);
//     v1 += 68;
//   }
// For group i the byte counter v3 runs 8,16,24,32 (4 iterations) — id at DADC
// dword (17i + v3/4 ... wait: v3/4) i.e. dword indices 17i+1,+3,+5,+7 (slots 0..3
// only). UNLIKE ResetGroupSlot, only the first 4 slots are cleared here.
void HistoryResetAllGroups(CartTable& table)
{
    for (int i = 0; i < kCartGroupCount; ++i)
    {
        CartGroup& g = table.groups[i];
        g.active = 1;                         // dword_122DAE0[17*i] = 1
        for (int k = 0; k < kResetAllGroupsSlots; ++k) // v3 = 8,16,24,32 -> slot 0..3
        {
            g.slots[k].id   = -1;             // dword_122DADC[v3/4] = -1
            g.slots[k].kind = 0xFF;           // LOBYTE(dword_122DAE0[v3/4]) = -1
        }
    }
}

// gilde.exe 0x4fd6ac — VIBE_History_ParseCommandlineFirstPass (group-index core).
// The recoverable decision: after resolving the label text and reading the leading
// keyword, the pass parses a group digit (VIBE_Util_ParseInt -> v19) and gates it
// on < 4:
//   v9 = v19;
//   if ((unsigned)v19 >= 4) { ... "Wrong group index" ...; return 0; }
//   if (v18) VIBE_History_ResetGroupSlot(v19);   // v18 set for the "_SET" keyword
//   return &dword_122DAE0[17 * v9];
// We return the group index (0..3) in place of the pointer, and -1 for the null
// the original returns on an out-of-range digit.
int HistoryParseCommandlineGroupIndex(CartTable& table, int groupDigit, bool isSet)
{
    if (static_cast<unsigned>(groupDigit) >= static_cast<unsigned>(kCartGroupCount))
        return -1;                            // (unsigned)v19 >= 4 -> return 0 (null)

    if (isSet)                                // v18 (the "_SET" reset flag)
        HistoryResetGroupSlot(table, static_cast<unsigned>(groupDigit));

    return groupDigit;                        // &dword_122DAE0[17 * v9]
}

} // namespace guild::world
