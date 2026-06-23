// character_recon4_flags — implementation.  See header for provenance.
#include "character_recon4_flags.h"

namespace guild::sim {

// gilde.exe 0x4b5fa4 — VIBE_Character_RefreshAllFlags
void RefreshAllFlags(RefreshFlagsHooks& H, int player, const RefreshRecordView& view) {
    const int savedSlot = H.savedActiveSlot;  // v4 = dword_649D60

    // VIBE_Universe_SwitchActiveSlot(0, 1, a2, a1)
    if (H.switchActiveSlot)
        H.switchActiveSlot(H.ctx, 0, 1, /*a2*/ 0, player);

    // for (i = QueryBegin(rec,1,4,player); i; i = IterNext())
    //   if (person+97) WalkAndInvoke(off_649D64, obj, ShowFlag, 832, person)
    if (H.personQueryBegin) {
        for (void* person = H.personQueryBegin(H.ctx, nullptr, 1, 4, player);
             person;
             person = H.personIterNext ? H.personIterNext(H.ctx) : nullptr) {
            void* obj = H.personObjPtr ? H.personObjPtr(H.ctx, person) : nullptr;
            if (obj && H.walkShowFlag)
                H.walkShowFlag(H.ctx, obj, person);
        }
    }

    // for (j = QueryFind(0,3,7,3,player,4,29); j; j = IterNext())
    //   v9 = gameobject+59; if (v9) { idx = IndexFromPointer(char+136);
    //        SwitchActiveSlot(idx,..,player);
    //        texture-set predicate over the &word_12CE910[268*player] record }
    if (H.gameObjectQueryFind) {
        for (void* gobj = H.gameObjectQueryFind(H.ctx, player);
             gobj;
             gobj = H.gameObjectIterNext ? H.gameObjectIterNext(H.ctx) : nullptr) {
            void* charHandle = H.gameObjectCharHandle ? H.gameObjectCharHandle(H.ctx, gobj) : nullptr;
            if (!charHandle)
                continue;
            int universePtr = H.charUniversePtr ? H.charUniversePtr(H.ctx, charHandle) : 0;
            int idx = H.indexFromPointer ? H.indexFromPointer(H.ctx, universePtr) : 0;
            if (H.switchActiveSlot)
                H.switchActiveSlot(H.ctx, idx, /*v11*/ 0, /*v12*/ 0, player);

            // if (v3) { if (*((_DWORD*)v3+21)) { v14 = *((_BYTE*)v3+2);
            //   if ((v14==5||v14==6||v14==7) && *((_DWORD*)v3+21)!=1341)
            //     SelectTextureSet(**(char+292), ..+460, 1, *((_BYTE*)v3+84)-61, player) } }
            if (view.present && view.buildingId != 0) {
                u8 t = view.type;
                if ((t == 5 || t == 6 || t == 7) && view.buildingId != 1341) {
                    int texIndex = static_cast<int>(view.textureByte) - 61;
                    if (H.selectTextureSet)
                        H.selectTextureSet(H.ctx, charHandle, 1, texIndex, player);
                }
            }
        }
    }

    // return VIBE_Universe_SwitchActiveSlot(v4, 1, v8, a1)
    if (H.switchActiveSlot)
        H.switchActiveSlot(H.ctx, savedSlot, 1, /*v8*/ 0, player);
}

// gilde.exe 0x531e60 — VIBE_Character_SyncTurnState
void SyncTurnState(TurnStateHooks& H, const TurnStateView& v) {
    const u8 state = v.stateByte;

    // 0x531e93 `cmp dl, 0Ah` / 0x531e96 `jge` — the >=10 test is a SIGNED byte
    // compare (jge, not jae).  State bytes 0x80..0xFF are negative and therefore
    // fall through to the else branch, NOT the HIBYTE branch.  Reproduce with a
    // signed-char comparison (state==6/==7 are equality so sign-neutral).
    if (state == 6 || state == 7 || static_cast<i8>(state) >= 10) {
        // HIBYTE branch (0x532069): the original RE-READS byte_12CE912[536*a1] —
        // the SAME cell as `state` (0x532078 `mov bh, byte_12CE912[536*a1]`).  So
        // `prev` is identically `state`; the inner test reduces to state in {6,7}
        // (always false on the state>=10 sub-path).  We use `state` directly to be
        // 1:1; `v.prevStateByte` is retained only as a documentation alias and a
        // faithful caller sets it == stateByte.
        const u8 prev = state;
        (void)v.prevStateByte;
        if (prev == 6 || prev == 7) {
            int field = 1;  // v26 = 1
            if (H.beginDeltaPacket)
                H.beginDeltaPacket(H.ctx, /*rec*/ nullptr, v.recId);
            if (H.appendRawField)
                H.appendRawField(H.ctx, 4u, 1u, &field, 372);
            if (H.queueRequestState22)
                H.queueRequestState22(H.ctx);
        }
        return;
    }

    // else branch
    int field = 1;  // v25 = 1
    if (v.data372 != 0 || state == 5 || state == 1 || state == 2 || state == 4) {
        if (H.beginDeltaPacket)
            H.beginDeltaPacket(H.ctx, /*rec*/ nullptr, v.recId);
        if (H.appendRawField)
            H.appendRawField(H.ctx, 4u, 1u, &field, 372);
        if (H.queueRequestState22)
            H.queueRequestState22(H.ctx);
    }

    if (v.buildingFlag) {
        int rank = H.computeRank ? H.computeRank(H.ctx, v.rankInputByte) : 0;  // v6
        void* handler = H.findFirstHandler ? H.findFirstHandler(H.ctx, 2, 2, v.recId, 0, 36) : nullptr;

        // ++v5[93];  gauge = DrawProductionGauge(a1, v24);  v5[93] = v11(=before)
        // The refcount is bumped across the gauge draw then restored — a no-op on
        // the field's final value; reproduced for fidelity (observable to leaves).
        f32 gauge = H.drawProductionGauge ? H.drawProductionGauge(H.ctx, /*player*/ 0, nullptr) : 0.0f;

        if (!handler && rank != 0 && rank < 6 && gauge >= 1.0f && v.ledger >= 0) {
            // VIBE_Light_SetGrayColorThunk fills the request scratch; the packed
            // request bytes are engine-internal.  We emit the two command leaves.
            if (H.queueRequestSlotReset28)
                H.queueRequestSlotReset28(H.ctx, /*req*/ nullptr, /*packed*/ 0);
            // sprintf(name, fmt, recId, textByte, rank+1) — message build; omitted.
            if (H.requestBuildOp90)
                H.requestBuildOp90(H.ctx, -4, v.recId);
        }
    }
}

} // namespace guild::sim
