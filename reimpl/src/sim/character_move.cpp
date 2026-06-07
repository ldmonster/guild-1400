#include "sim/character_move.h"

namespace guild::sim {

namespace {
// Turn-anim selection thresholds (dbl_6107C4 / dbl_6107CC).
constexpr double kTurnLeftThresh  = 0.2;    // dbl_6107C4
constexpr double kTurnRightThresh = -0.2;   // dbl_6107CC

// Inert hooks.
int  InertMove(MoveUniverseState*, int) { return 1; }
void InertSwitch(int)                   {}
void InertVisible(MoveUniverseState*, int) {}
const MoveUniverseHooks kInert = { &InertMove, &InertSwitch, &InertVisible };
const MoveUniverseHooks* g_hooks = &kInert;
} // namespace

void SetMoveUniverseHooks(const MoveUniverseHooks* hooks) {
    g_hooks = hooks ? hooks : &kInert;
}
const MoveUniverseHooks& GetMoveUniverseHooks() { return *g_hooks; }

// gilde.exe 0x4063c8 — VIBE_Character_Move2UniverseActionUpdate.
//
//   v3 = Data[0];
//   if ( v3 < 0 ) -> error "Invalid universe (<0)", unlink.
//   if ( (!v3 && Data[1]!=-1) || (v3 && Data[1]==-1) ) -> error combo, unlink.
//   if ( !MoveToUniverse(ch, universe[Data[0]]) ) -> error "Could not change", unlink.
//   v25 = activeSlot;
//   SwitchActiveSlot(Data[0], 1, ...);
//   ... texture-set select (render, deferred) ...
//   StopSample(ch);
//   ... entry-dummy bone transform + visibility (render, deferred) ...
//   ch[11] (=+44) = Data[1];   ch[14..16] (=+48..56) = 0;   ch[12] (sub) = Data[2];
//   SetVisible(ch, 1);
//   if (slot changed mismatch) SetVisible(ch, 0);
//   SwitchActiveSlot(v25, 1, ...);
//   unlink.
MoveUniverseResult Move2UniverseActionUpdate(MoveUniverseState* st) {
    // if ( v3 < 0 )  -> "Invalid universe (<0)!"
    if (st->dataUniverse < 0)
        return MoveUniverseResult::kInvalidUniverse;

    // if ( (!Data0 && Data1!=-1) || (Data0 && Data1==-1) ) -> invalid combination
    if ((st->dataUniverse == 0 && st->dataId != -1) ||
        (st->dataUniverse != 0 && st->dataId == -1))
        return MoveUniverseResult::kInvalidCombo;

    // if ( !MoveToUniverse(ch, &universe[984*Data0]) ) -> "Could not change..."
    st->valid = GetMoveUniverseHooks().moveToUniverse(st, st->dataUniverse);
    if (!st->valid)
        return MoveUniverseResult::kMoveFailed;

    const int prevSlot = st->curUniverse;        // v25 = dword_649D60 (active slot)

    // SwitchActiveSlot(Data[0]) — make the destination active (render leaves +
    // texture-set/visibility/entry-dummy transform are deferred to the hooks).
    GetMoveUniverseHooks().switchActiveSlot(st->dataUniverse);

    // Field-clear + id bookkeeping (engine: v13[11]=Data1; v13[14..16]=0; v13[12]=Data2):
    st->curUniverse = st->dataId;   // ch +44 = Data[1]
    st->scratch48 = 0;              // ch +48 = 0
    st->scratch52 = 0;              // ch +52 = 0
    st->scratch56 = 0;              // ch +56 = 0  (the [14],[15],[16] clears)

    GetMoveUniverseHooks().setVisible(st, 1);

    // if ( prevSlot == Data0 && (new universe id mismatch) ) SetVisible(ch, 0).
    if (prevSlot == st->dataUniverse &&
        st->curUniverse != -1 && st->curUniverse != prevSlot) {
        GetMoveUniverseHooks().setVisible(st, 0);
    }

    // Restore the previously active slot (engine: SwitchActiveSlot(v25)).
    GetMoveUniverseHooks().switchActiveSlot(prevSlot);
    return MoveUniverseResult::kOk;
}

// gilde.exe 0x408a10 — VIBE_Character_TurnByAngleAction (animation selection).
//   if ( a4 >= 0.2 )       v5 = "bewegung/dreh_90_links";
//   else if ( a4 >= -0.2 ) (keep current; goto LABEL_5 — no swap)
//   else                   v5 = "bewegung/dreh_90_rechts";
int TurnPickAnimation(double angle) {
    if (angle >= kTurnLeftThresh)
        return +1;   // left
    if (angle >= kTurnRightThresh)
        return 0;    // within deadzone: no anim swap
    return -1;       // right
}

} // namespace guild::sim
