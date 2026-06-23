// guild::world — Tavern card-window slot-layout rule-cores.  See header for the map.
// Reconstructed 1:1 from gilde.exe; addresses on each function.
#include "world/tavern_cards_location_recon.h"

namespace guild::world {

// Shared geometry walk: place `n` slots using the CardSlotLayout state machine.
// 1:1 with the body every builder runs (x=28 first, +58 per column, wrap to 0 every 8,
// sprite 29 for the first 8 placements then 70).
int Tavern_LayoutCardSlots(int n, CardSlot* out, int cap) {
    CardSlotLayout L;
    int written = 0;
    for (int k = 0; k < n; ++k) {
        if (out && written < cap) {
            out[written].x = L.x();
            out[written].sprite = L.sprite();
        }
        ++written;
        L.advance();
    }
    return written;
}

// gilde.exe 0x5160f0 — VIBE_Tavern_BuildCardSlotsSmall.
//   edi(x)=28, dx(sprite)=29, esi(count)=0;
//   do { RandomModulo(6) -> back sprite (0x6CD+roll, side effect);
//        AddToWindow(x, sprite); ++count;
//        if (count % 8) x += 58; else x = 0;
//        sprite = (count <= 7) ? 29 : 70;
//   } while (count < 3);
// => exactly 3 slots: (28,29),(86,29),(144,29).
int Tavern_BuildCardSlotsSmall(CardSlot* out, int cap) {
    // The RandomModulo(6) back-sprite roll is a render side effect that does not alter
    // geometry; the slot layout is the shared walk for kSmallSlots cards.
    return Tavern_LayoutCardSlots(tavern_cards::kSmallSlots, out, cap);
}

// gilde.exe 0x51618c — VIBE_Tavern_BuildCardSlotsHand.
// Two structurally identical loops selected by (*(*(_DWORD*)a2 + 2) != 6); both place
// a2[4] (cardCount) slots with the shared geometry, then run one trailing
// RandomModulo(6)+AddToWindow (a face-down "back" card appended after the hand).  The
// geometry produced is the same regardless of the phase byte.
int Tavern_BuildCardSlotsHand(int pilePhase, int cardCount, CardSlot* out, int cap) {
    (void)pilePhase;  // routes window walk order only; geometry identical
    return Tavern_LayoutCardSlots(cardCount, out, cap);
}

// gilde.exe 0x5162c0 — VIBE_Tavern_BuildCardSlotsTable.
// Mirror of the hand builder: (*(*(_DWORD*)a2 + 2) == 6) selects the first loop, else the
// second; both place a2[4] slots with the shared geometry.
int Tavern_BuildCardSlotsTable(int pilePhase, int cardCount, CardSlot* out, int cap) {
    (void)pilePhase;
    return Tavern_LayoutCardSlots(cardCount, out, cap);
}

// gilde.exe 0x516794 — VIBE_Tavern_RefreshCardWindows : pile-to-window routing.
// The pile whose +2 phase byte == 6 is drawn into window 3; the other into window 2.
int Tavern_PileTargetWindow(int pilePhase) {
    return (pilePhase == tavern_cards::kPilePhaseTable) ? 3 : 2;
}

// gilde.exe 0x516794 — footer rule (function tail):
//   if ( (*(a1+8) == 5 && *(*(a1+12)+2) == 6) || (*(a1+8) == 6 && *(*(a1+40)+2) == 6) )
//        return RenderRichString(0x1496, *(a1+68));       // full pot
//   else return RenderRichString(0x1497, *(a1+68) / 2);   // half pot
CardFooter Tavern_CardFooter(int state8, int pileAphase, int pileBphase, int pot) {
    bool showdown =
        (state8 == tavern_cards::kStateShowA && pileAphase == tavern_cards::kPilePhaseTable) ||
        (state8 == tavern_cards::kStateShowB && pileBphase == tavern_cards::kPilePhaseTable);
    if (showdown)
        return CardFooter{tavern_cards::kFooterShowdownText, pot};
    return CardFooter{tavern_cards::kFooterNormalText, pot / 2};
}

} // namespace guild::world
