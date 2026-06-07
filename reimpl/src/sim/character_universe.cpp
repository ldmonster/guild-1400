// character_universe — full 1:1 translation of VIBE_Character_Move2UniverseActionUpdate
// (0x4063c8). The Hex-Rays pseudocode is the reference of record; the render /
// universe leaves route through UniverseHooks. The validation ladder, field-clear
// order and the visibility-mismatch logic are reproduced exactly.
#include "sim/character_universe.h"

namespace guild::sim {

// --- active-slot globals ----------------------------------------------------
int g_univActiveSlot        = 0;    // dword_649D60
int g_univActiveUniverseId  = 0;    // dword_62D080
int g_univActiveSubUniverse = -1;   // dword_62D084

// ---------------------------------------------------------------------------
// Hook table (inert default: move succeeds, entry dummy resolves).
// ---------------------------------------------------------------------------
namespace {
int  InertMove(UniverseTransition*, int)                  { return 1; }
void InertSwitch(int)                                     {}
void InertSelectTex(UniverseTransition*, int, u8)         {}
void InertStopSample(UniverseTransition*)                 {}
int  InertPlaceEntry(UniverseTransition*, const char*)    { return 1; }
void InertVisible(UniverseTransition* st, int v)          { st->visible = v; }
const UniverseHooks kInert = { &InertMove, &InertSwitch, &InertSelectTex,
                               &InertStopSample, &InertPlaceEntry, &InertVisible };
const UniverseHooks* g_hooks = &kInert;
} // namespace

void SetUniverseHooks(const UniverseHooks* hooks) { g_hooks = hooks ? hooks : &kInert; }
const UniverseHooks& GetUniverseHooks() { return *g_hooks; }

// ===========================================================================
// VIBE_Character_Move2UniverseActionUpdate (0x4063c8).
//
//   v3 = Data[0];
//   if ( v3 < 0 ) { log "Invalid universe (<0)"; unlink; }
//   if ( (!v3 && Data[1]!=-1) || (v3 && Data[1]==-1) ) {
//       log (Data0 ? combo_1 : combo_0); unlink; }
//   if ( !MoveToUniverse(ch, &universe[984*Data0]) ) { log "Could not change"; unlink; }
//   v25 = dword_649D60;                       // saved active slot
//   SwitchActiveSlot(Data0, 1, ...);
//   // texture-set select:
//   if ( !Data0 && dword_62D080 == ch+44 )    // moving into the active universe (outer)
//        SelectTextureSet(mesh, +244, /*outer=*/0, /*indoor=*/0);
//   else if ( dword_62D080 == ch+44 )         // in the active universe
//        SelectTextureSet(mesh, +244, /*outer=*/1, /*indoor=*/ch+506);
//   StopSample(ch);
//   if ( !ch+380 ) {                          // entry-dummy placement
//        name = ch+240 ? ch+240 : "dummy_EINGANG";
//        obj = FindByHandle(name) ?: FindByHandle("dummy_EINGANG") ?: FindByHandle(0,"dummy_EINGANG");
//        if ( obj ) { PointThroughBoneChain(obj, &spawn); ApplyVisibilityState(ch,0); }
//   } else { ApplyVisibilityState(ch,0); }     // skipEntry -> place at current
//   ch+44 = Data1; ch+56=ch+60=ch+64 = 0; ch+48 = Data2;
//   SetVisible(ch, 1);
//   if ( v25 == Data0 && ( (ch+44!=-1 && dword_62D080!=ch+44) ||
//                          (dword_62D084!=-1 && dword_62D084!=ch+48) ) )
//        SetVisible(ch, 0);
//   if ( node+40 && node+40[9] == 56 ) SetVisible(ch, 0);
//   SwitchActiveSlot(v25, 1, ...);
//   unlink;
// ===========================================================================
UniverseResult Move2UniverseActionUpdate(UniverseTransition* st) {
    const int data0 = st->dataUniverse;   // *(a1+48)
    const int data1 = st->dataId;         // *(a1+52)
    const int data2 = st->dataSubId;      // *(a1+56)

    // if ( v3 < 0 ) -> "ch_Move2Universe(): Invalid universe (<0)!"
    if (data0 < 0)
        return UniverseResult::kInvalidUniverse;

    // if ( (!Data0 && Data1!=-1) || (Data0 && Data1==-1) ) -> invalid combination.
    if ((data0 == 0 && data1 != -1) || (data0 != 0 && data1 == -1))
        return UniverseResult::kInvalidCombo;

    // if ( !MoveToUniverse(ch, &universe[984*Data0]) ) -> "Could not change..."
    st->moveResult = GetUniverseHooks().moveToUniverse(st, data0);
    if (!st->moveResult)
        return UniverseResult::kMoveFailed;

    // v25 = dword_649D60 (saved active slot); SwitchActiveSlot(Data0).
    st->prevSlot = g_univActiveSlot;
    GetUniverseHooks().switchActiveSlot(data0);

    // Texture-set select (render leaf). The two arms mirror the engine:
    //   !Data0 && in-active-universe -> outer set, indoor=0;
    //   in-active-universe          -> inner set, indoor = ch+506.
    if (data0 == 0 && st->inActiveUniverse) {
        GetUniverseHooks().selectTextureSet(st, /*outer=*/0, /*indoor=*/0);
    } else if (st->inActiveUniverse) {
        GetUniverseHooks().selectTextureSet(st, /*outer=*/1, st->indoorFlag);
    }

    GetUniverseHooks().stopSample(st);

    // Entry-dummy placement (or place-at-current when skipEntry).
    st->entryFound = false;
    if (!st->skipEntry) {
        const char* name = (st->entryDummy && st->entryDummy[0])
                               ? st->entryDummy : "dummy_EINGANG";
        // FindByHandle(name) ?: FindByHandle("dummy_EINGANG") ?: FindByHandle(0,...).
        // The hook resolves the fallback chain and applies the bone transform.
        if (GetUniverseHooks().placeAtEntryDummy(st, name)) {
            st->entryFound = true;
            // ApplyVisibilityState(ch, 0): position + light/transport refresh.
        }
        // if the dummy could not be resolved the engine skips ApplyVisibilityState
        // (LABEL_19) and proceeds directly to the field bookkeeping.
    } else {
        // ch+380 set: place at the current position (ApplyVisibilityState(ch,0)).
        st->entryFound = true;
    }

    // Field bookkeeping (engine: *(ch+44)=Data1; ch[14..16]=0; ch[12]=Data2).
    st->curUniverse = data1;   // ch +44  ([11])
    st->char56 = 0;            // ch +56  ([14])
    st->char60 = 0;            // ch +60  ([15])
    st->char64 = 0;            // ch +64  ([16])
    st->char48 = data2;        // ch +48  ([12])

    GetUniverseHooks().setVisible(st, 1);

    // Slot-mismatch hide: if we are back on the slot we relocated into and either
    // the new universe id mismatches the active universe, or the active sub-universe
    // mismatches the new sub-id -> hide.
    if (st->prevSlot == data0 &&
        ((st->curUniverse != -1 && g_univActiveUniverseId != st->curUniverse) ||
         (g_univActiveSubUniverse != -1 && g_univActiveSubUniverse != st->char48))) {
        GetUniverseHooks().setVisible(st, 0);
    }

    // Next-action-is-hide: if the chained action is a SetVisible(56) hide -> hide.
    if (st->nextIsHide)
        GetUniverseHooks().setVisible(st, 0);

    // Restore the previously active slot.
    GetUniverseHooks().switchActiveSlot(st->prevSlot);
    return UniverseResult::kOk;
}

} // namespace guild::sim
