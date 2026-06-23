// character_recon_tavern — see header. Faithful 1:1 port of
//   gilde.exe 0x4d5c60  VIBE_Character_FindTavernTargetSlot
#include "sim/character_recon_tavern.h"

namespace guild::sim {

namespace {
// Default turn predicate. The engine's real dependency is
// VIBE_Character_IsObjectForTurn (0x453228); callers must wire it. The default
// keeps the gate OPEN (returns true) so a un-wired build matches the standalone /
// non-networked case (IsObjectForTurn returns true when not networked), but it is
// the caller's responsibility to wire the real predicate for networked play.
bool DefaultTurnPredicate(const TavernObject*) { return true; }

TavernTurnPredicate g_turnPred = &DefaultTurnPredicate;
} // namespace

void SetTavernTurnPredicate(TavernTurnPredicate pred) {
    g_turnPred = pred ? pred : &DefaultTurnPredicate;
}

TavernTurnPredicate GetTavernTurnPredicate() { return g_turnPred; }

// gilde.exe 0x4d5c60 — VIBE_Character_FindTavernTargetSlot.
// Disassembly-faithful translation. Register/offset notes inline.
bool FindTavernTargetSlot(const TavernObject* obj, const TavernSlot* slots,
                          int* outIndex, TavernTurnPredicate pred) {
    // cmp word ptr [eax], 0FFFFh / cmp byte [eax+8],0 / cmp byte [esi+2],0 /
    // cmp dword [esi+184h],0  — any failing condition => return 0.
    if (obj->markerWord == 0xFFFF        // +0x00 free/invalid marker
        || obj->enabledByte == 0         // +0x08 must be enabled
        || obj->typeByte != 0            // +0x02 must be type 0
        || obj->tavernTarget == nullptr) // +0x184 linked target required
        return false;                    // loc_4D5C85: xor eax,eax

    // call VIBE_Character_IsObjectForTurn; test eax,eax; jz return 0.
    if (!pred(obj))
        return false;

    // mov eax,ecx (slot array) ; mov ebx,[ecx] (first entry ptr) ; test ; jz ret0.
    const TavernSlot* slot = slots;
    if (slot->entry == nullptr)          // slots[0].entry == 0 => no table
        return false;

    // mov esi,[esi+184h]  — the tavern target record.
    const TavernTarget* target =
        static_cast<const TavernTarget*>(obj->tavernTarget);

    int index = 0;                       // edx
    for (;;) {
        // loc_4D5CA7: mov ecx,[eax] (current entry) ; mov ebp,[esi+2Ch] (matchId)
        const TavernSlotEntry* entry = slot->entry;
        // cmp ebp,[ecx+1]  (matchId vs entry->entryId)
        // jnz advance ; cmp word [ecx+27h],0FFFFh ; jnz => found.
        if (target->matchId == entry->entryId && entry->ownerWord != 0xFFFF) {
            // loc_4D5CCD: mov eax,1 ; mov [edi],edx ; return 1.
            *outIndex = index;
            return true;
        }
        // loc_4D5CB8: inc edx ; add eax,0Ch ; cmp edx,10h ; jge ret0 ;
        //             cmp dword [eax],0 ; jnz loop.
        ++index;
        ++slot;                          // += 12 bytes (one TavernSlot)
        if (index >= 16)
            return false;
        if (slot->entry == nullptr)
            return false;
    }
}

bool FindTavernTargetSlot(const TavernObject* obj, const TavernSlot* slots,
                          int* outIndex) {
    return FindTavernTargetSlot(obj, slots, outIndex, g_turnPred);
}

} // namespace guild::sim
