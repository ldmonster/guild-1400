#pragma once
// guild::world — Tavern card-game window slot-layout rule-cores.
//
// Reconstructed 1:1 from gilde.exe.  The tavern card game (VIBE_Location_TavernCardGame
// 0x516b78, via VIBE_Tavern_RunCardRoundPhase 0x5163f8) renders three card windows
// (the small/hand/table piles).  Four routines lay the card sprites out in those
// windows; what they share — and what is translated here byte-for-byte — is the
// SLOT-GRID GEOMETRY and the per-slot SPRITE-ID selection:
//
//   VIBE_Tavern_BuildCardSlotsSmall   0x5160f0 — fixed 8-slot strip (the dealer pile).
//   VIBE_Tavern_BuildCardSlotsHand    0x51618c — N cards, layout depends on a hand flag.
//   VIBE_Tavern_BuildCardSlotsTable   0x5162c0 — N cards, layout depends on a hand flag.
//   VIBE_Tavern_RefreshCardWindows    0x516794 — rebuilds windows 2/3/4 from game state.
//
// The window/render leaves (VIBE_Window_RemoveChildren, VIBE_Object_AddToWindow,
// VIBE_Text_RenderRichString, VIBE_Form_SelectWindow) and the RNG leaf
// (VIBE_Math_RandomModulo) live in the render/io clusters; here we translate the pure
// geometry the builders compute (x-coordinate per slot + which sprite id each slot
// gets), which is identical across all four and is the real reconstructed unit.
//
// Geometry (recovered from every builder, identical):
//   * x starts at 28 for the first slot;
//   * after placing slot k (1-based count just incremented), if (k % 8) != 0 then
//     x += 58, else x resets to 0  (8 cards per row, the row wrapping);
//   * the sprite id is 29 for the first 8 slots (count <= 7 *before* placing the 9th),
//     and 70 thereafter.  Concretely the original keeps the "next" sprite id and, after
//     each placement, sets it to 29 while count<=7 and 70 once count>7.
//
// We expose a tiny stateful layout helper that reproduces this exactly, plus the per-
// builder slot counts / gating so a test can replay the placement sequence.

#include "guild/common/types.h"

namespace guild::world {

namespace tavern_cards {

// gilde.exe constants (every builder).
constexpr int kStartX     = 28;   // v0/v3/v8/... = 28 (first slot x)
constexpr int kStepX      = 58;   // v0 += 58 between columns
constexpr int kRowWrapX   = 0;    // x resets to 0 at the start of a new row
constexpr int kPerRow     = 8;    // wrap every 8 cards (count % 8 == 0)
constexpr int kSpriteEarly = 29;  // sprite id for the first 8 cards (count <= 7)
constexpr int kSpriteLate  = 70;  // sprite id once count > 7
constexpr int kSmallSlots  = 3;   // VIBE_Tavern_BuildCardSlotsSmall loops while esi<3 (places 3)
constexpr int kBackSpriteBase = 0x6CD; // 1741 — face-down "back" sprite base (ebx = base + rand%6)
constexpr int kBackSpriteRollMod = 6;  // VIBE_Math_RandomModulo(6) per face-down card

// The card-pile record byte tested to pick the layout branch.
// In BuildCardSlotsHand/Table: *(*(_DWORD*)a2 + 2) — the pile's "phase" byte; value 6
// selects the table-style fill, anything else the hand-style fill.  Both branches use
// the SAME geometry; the byte only routes which window/order the original walks.
constexpr int kPilePhaseTable = 6;

} // namespace tavern_cards

// ---------------------------------------------------------------------------
// Card-slot layout state machine (the geometry shared by all four builders).
//
// Usage mirrors the original loop:
//   CardSlotLayout L;                 // x = 28, sprite = 29, count = 0
//   for (k = 0; k < n; ++k) {
//       int x      = L.x();           // place sprite L.sprite() at L.x()
//       int sprite = L.sprite();
//       L.advance();                  // bump count, update x + next sprite
//   }
// `advance()` reproduces the post-placement update exactly:
//   ++count;
//   if (count % 8) x += 58; else x = 0;
//   sprite = (count <= 7) ? 29 : 70;
// ---------------------------------------------------------------------------
class CardSlotLayout {
public:
    CardSlotLayout() = default;

    int x() const { return x_; }          // current slot x
    int sprite() const { return sprite_; } // current slot sprite id
    int count() const { return count_; }   // cards placed so far

    // Post-placement update (1:1 with the builders' loop tail).
    void advance() {
        ++count_;
        if (count_ % tavern_cards::kPerRow)
            x_ += tavern_cards::kStepX;
        else
            x_ = tavern_cards::kRowWrapX;
        sprite_ = (count_ <= (tavern_cards::kPerRow - 1)) ? tavern_cards::kSpriteEarly
                                                          : tavern_cards::kSpriteLate;
    }

private:
    int x_ = tavern_cards::kStartX;
    int sprite_ = tavern_cards::kSpriteEarly;
    int count_ = 0;
};

// One placed card slot (geometry only — the window handle and render are out of scope).
struct CardSlot {
    int x;       // slot x coordinate
    int sprite;  // sprite id (29 or 70)
};

// Compute the slot geometry for a pile of `n` cards (the layout every builder applies).
// Returns up to `n` slots in placement order.  `out`/`cap` is a caller buffer so the
// helper stays allocation-free and self-contained; returns the count written.
int Tavern_LayoutCardSlots(int n, CardSlot* out, int cap);

// VIBE_Tavern_BuildCardSlotsSmall 0x5160f0 — the fixed dealer strip.  The original loops
// while count < 3 (cmp esi,3 / jl), placing exactly 3 face-down cards, and rolls
// VIBE_Math_RandomModulo(6) per card to pick a "back" sprite from base 0x6CD (side effect
// only — does not change geometry).  Slots: x=28, 86, 144; sprite 29 throughout.  Ret 3.
int Tavern_BuildCardSlotsSmall(CardSlot* out, int cap);

// VIBE_Tavern_BuildCardSlotsHand 0x51618c / VIBE_Tavern_BuildCardSlotsTable 0x5162c0.
// Both lay out `cardCount` slots with the shared geometry; `pilePhase` (the +2 byte of
// the pile record) only selects which original walk order is taken — the produced
// geometry is identical, so a single helper covers both.  Returns the count.
int Tavern_BuildCardSlotsHand(int pilePhase, int cardCount, CardSlot* out, int cap);
int Tavern_BuildCardSlotsTable(int pilePhase, int cardCount, CardSlot* out, int cap);

// ---------------------------------------------------------------------------
// VIBE_Tavern_RefreshCardWindows 0x516794 — rebuilds the three card windows from the
// current game state.  The pure decision logic recovered here:
//   * window 2 / window 3 each show one player's pile; the pile whose +2 phase byte is 6
//     (the "open" pile) is drawn into window 3, the other into window 2;
//   * a per-slot sprite override: when the game-state byte at +36 == 4, the slot uses the
//     "other" window's sprite path (the face-down vs face-up swap); otherwise the normal
//     path.  Geometrically both paths place the same slots — the swap only changes which
//     status array the sprite is read from, which is out of scope — so the geometry is
//     the shared layout above.
//   * the footer text: if (state[8]==5 && pileA.phase==6) || (state[8]==6 && pileB.phase==6)
//     render rich-string 0x1496 with the pot value (a1+68); else render 0x1497 with
//     (a1+68)/2  (i.e. the stake shown is halved when the round is not in the "showdown"
//     state).
// We translate the footer rule (the only branchy NON-geometry decision) and the
// window-routing rule as testable predicates.
// ---------------------------------------------------------------------------

// Which window a pile is drawn into.  Pile phase 6 -> window 3; otherwise window 2.
// (RefreshCardWindows draws the !=6 pile into win 2 and the ==6 pile into win 3.)
int Tavern_PileTargetWindow(int pilePhase);

// Footer rule (0x516794 tail): returns the rich-string id and the value to show.
struct CardFooter { int textId; int value; };
// state8     = *(a1+8)  (whose turn / round phase)
// pileAphase = *(*(a1+12)+2)
// pileBphase = *(*(a1+40)+2)
// pot        = *(a1+68)
CardFooter Tavern_CardFooter(int state8, int pileAphase, int pileBphase, int pot);

// gilde.exe footer constants.
namespace tavern_cards {
constexpr int kFooterShowdownText = 0x1496; // full pot
constexpr int kFooterNormalText   = 0x1497; // pot / 2
constexpr int kStateShowA = 5; // state8 value that pairs with pileA showdown
constexpr int kStateShowB = 6; // state8 value that pairs with pileB showdown
} // namespace tavern_cards

} // namespace guild::world
