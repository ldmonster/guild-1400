// character_recon5_transport — see header for provenance / layout / constants.
#include "character_recon5_transport.h"

#include <cmath>

namespace guild::sim {

// ---------------------------------------------------------------------------
// gilde.exe 0x402d3c — VIBE_Character_MoveToUniverse
// ---------------------------------------------------------------------------
int MoveToUniverse(TChar* ch, int newUniverse, TransportHooks& H, u8 globalDeflateBit,
                   int globalUniverseId) {
    if (!ch)                                            // if ( result )
        return 0;                                       // 0x402d5d (ch==null -> result(0))
    int oldUniverse = ch->universe;                     // *(result+136)
    // if ( v3 && a2 && MoveBetweenUniverses(obj, v3, a2) )
    if (!oldUniverse || !newUniverse)
        return 0;                                       // 0x402d5b
    if (!(H.moveBetweenUniverses && H.moveBetweenUniverses(ch->object, oldUniverse, newUniverse)))
        return 0;                                       // 0x402d5b

    // VIBE_Object_SetPivotVector(obj, {0,0,0})          0x402d7d
    static const f32 kZeroPivot[3] = {0.0f, 0.0f, 0.0f}; // flt_5CA2E0
    if (H.setPivotVector) H.setPivotVector(ch->object, kZeroPivot);

    if (ch->transport) {                                 // v6 = *(v5+292); if (v6)
        // MoveBetweenUniverses(*v6, *(v5+136), a2)       (*v6 == transport->object)
        if (H.moveBetweenUniverses)
            H.moveBetweenUniverses(ch->transport->object, oldUniverse, newUniverse);
    }
    if (ch->secondary) {                                 // if ( *(v5+492) )
        if (H.moveBetweenUniverses)
            H.moveBetweenUniverses(ch->secondary, oldUniverse, newUniverse);
    }
    ch->universe = newUniverse;                          // *(v5+136) = a2  0x402db7

    // if ( IndexFromPointer(a2) ) clear bit3 ; else set bit3 from globalDeflateBit
    int idx = H.indexFromPointer ? H.indexFromPointer(newUniverse) : 0;
    if (idx) {
        ch->object->f529 &= ~8u;                         // 0x402e35
    } else {
        u8 v9 = (u8)(globalDeflateBit & 1);              // byte_62D010 & 1
        u8 v10 = (u8)(ch->object->f529 & 0xF7);          // clear bit3
        ch->object->f529 = v10;
        ch->object->f529 = (u8)((8 * v9) | v10);         // set bit3 = v9
    }

    // if ( a2 == globalUniverse && !obj->sceneCtx ) -> log + re-inflate
    if (newUniverse == globalUniverseId && !ch->object->sceneCtx) {
        // VIBE_Crt_Sprintf_0(buf,"ch_MoveCharacter2Universe(): character deflated...",name)
        // log only; behaviourally just the touch-mesh re-inflate matters here.
        if (H.touchMeshFrames) H.touchMeshFrames(ch);    // 0x402e1d
    }
    return 1;                                            // 0x402e22
}

// ---------------------------------------------------------------------------
// gilde.exe 0x402e40 — VIBE_Character_AttachTransport
// ---------------------------------------------------------------------------
TransportAttach* AttachTransport(TChar* ch, TransportHooks& H, TObject* transportObject) {
    // v3 = AllocDebug(0x18,"ch:ch_AttachTransport")
    TransportAttach* rec = new TransportAttach();        // 0x18 record

    const TObject& o = *ch->object;                      // *(a1+52)
    // local point = (0, 0, -70) ; v12[6]=-70, v12[4]=0, v12[5]=0
    // v4 (y) = 0*M400 + 0*M416 + -70*M432 + M448
    double v4 = 0.0 * M400(o) + 0.0 * M416(o) + (-70.0) * M432(o) + M448(o);
    // v5 (z) = 0*M404 + 0*M420 + -70*M436 + M452
    double v5 = 0.0 * M404(o) + 0.0 * M420(o) + (-70.0) * M436(o) + M452(o);
    // v12[0] (x) = 0*M396 + 0*M412 + -70*M428 + M444
    f32 out[3];
    out[0] = (f32)(0.0 * M396(o) + 0.0 * M412(o) + (-70.0) * M428(o) + M444(o));
    out[2] = (f32)v5;                                    // v12[2] = v5
    out[1] = (f32)v4;                                    // v12[1] = v4

    // VIBE_Object_SetPosition(transportObject, v12)
    if (H.setPosition) H.setPosition(transportObject, out);
    // VIBE_Object_SetWorldTranslation(transportObject, &obj.wtrans (obj+132))
    if (H.setWorldTranslation) H.setWorldTranslation(transportObject, o.wtrans);

    // flag fixup on transportObject (v8): +529..+531
    u8 v9 = transportObject->f529;                       // v8[529]
    u8 v10 = (u8)(transportObject->f530 & 0xF3);         // v8[530] & 0xF3
    transportObject->f531 &= ~4u;                        // v8[531] &= ~4
    transportObject->f530 = v10;                         // v8[530] = v10
    transportObject->f529 = (u8)(v9 & 0xFD);             // v8[529] = v9 & ~2
    transportObject->f530 = (u8)(v10 | 4);               // v8[530] = v10 | 4

    ch->transport = rec;                                 // *(a1+292) = v3
    rec->flag = 0;                                       // *(v3+5) = 0
    rec->object = transportObject;                       // *(v3) = v8 (transport object)
    return rec;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x402f70 — VIBE_Character_UpdateTransportAttach
// ---------------------------------------------------------------------------
int UpdateTransportAttach(TChar* ch, TransportHooks& H) {
    TObject* charObj = ch->object;                       // v3 = a1[13]
    int meshCtx = H.resolveMesh ? H.resolveMesh(ch) : 0; // result = ResolveMesh(a1)
    TransportAttach* tr = ch->transport;                 // v5 = a1[73]
    int result = meshCtx;                                // v33 = result
    if (!tr)
        return result;                                   // 0x402f90 (return ResolveMesh)

    // v7 = *((v6)-1) == *(a1[73]) == the transport's own scene object.
    TObject* transObj = tr->object;                      // 0x402fa6

    // result = VectorWithinTolerance(v3+19, v6=tr->last, 0.01); if (!result) {...}
    // NOTE: the binary REUSES `result` for the tolerance test value, so when the
    // character is within tolerance the function returns that (nonzero) value, NOT
    // the resolved-mesh context (0x402fa9/0x402fb0).
    f32 charPos[3] = { charObj->pos[0], charObj->pos[1], charObj->pos[2] };
    if (H.vectorWithinTolerance) {
        result = H.vectorWithinTolerance(charPos, tr->last, 0.0099999998f);
    } else {
        f32 dx = tr->last[0] - charPos[0];
        f32 dy = tr->last[1] - charPos[1];
        f32 dz = tr->last[2] - charPos[2];
        result = (std::fabs(dx) <= 0.0099999998f && std::fabs(dy) <= 0.0099999998f &&
                  std::fabs(dz) <= 0.0099999998f) ? 1 : 0;
    }
    if (result)
        return result;                                   // 0x402fb0 (no movement)

    // target = charObj.pos + charObj.worldAdd  (+76+ +120, etc.)
    f32 v19 = charObj->pos[0] + charObj->worldAdd[0];    // 0x402fbc
    f32 v20 = charObj->pos[1] + charObj->worldAdd[1];    // 0x402fc9
    f32 v21 = charObj->pos[2] + charObj->worldAdd[2];    // 0x402fd9
    // delta from transport last anchor
    f32 v16 = v19 - tr->last[0];                         // 0x402fea
    f32 v17 = v20 - tr->last[1];                         // 0x402fff
    f32 v18 = v21 - tr->last[2];                         // 0x403014
    f32 v35 = (f32)std::sqrt((double)(v16 * v16 + v17 * v17 + v18 * v18));  // 0x40303e

    const TObject& T = *transObj;                        // matrix of v7
    // local (0,0,v35) through transport matrix basis (no translation here):
    double v8d = 0.0 * M400(T) + 0.0 * M416(T) + (double)v35 * M432(T);  // y
    double v9d = 0.0 * M404(T) + 0.0 * M420(T) + (double)v35 * M436(T);  // z
    v19 = (f32)(0.0 * M396(T) + 0.0 * M412(T) + (double)v35 * M428(T));  // x
    v20 = (f32)v8d;
    v21 = (f32)v9d;
    // anchor base + local offset (transport object position +76..+84)
    f32 v30 = T.pos[0] + v19;                            // 0x4030a7
    f32 v31 = T.pos[1] + v20;                            // 0x4030b2
    f32 v32 = T.pos[2] + v21;                            // 0x4030bd
    // direction from char object to that point, normalised to 70 units
    v19 = charObj->pos[0] - v30;                         // 0x4030c8
    v20 = charObj->pos[1] - v31;                         // 0x4030d7
    v21 = charObj->pos[2] - v32;                         // 0x4030e6
    v35 = kSeventy / (f32)std::sqrt((double)(v19 * v19 + v20 * v20 + v21 * v21));  // 0x403108
    v30 = v19 * v35;                                     // 0x403114
    v31 = v20 * v35;                                     // 0x403120
    v32 = v21 * v35;                                     // 0x40312c
    // new transport position = char position - clamped offset
    f32 newPos[3];
    newPos[0] = charObj->pos[0] - v30;                   // 0x403137 (v19 reuse)
    newPos[1] = charObj->pos[1] - v31;                   // 0x403146
    newPos[2] = charObj->pos[2] - v32;                   // 0x403153
    if (H.setPosition) H.setPosition(transObj, newPos);  // 0x403157

    // yaw: v11 = AngleToTargetSigned(v7) ; wtrans[1] += -(2pi - v11)
    double v11 = H.angleToTargetSigned ? H.angleToTargetSigned(transObj) : 0.0;  // 0x403160
    f32 wt[3] = { transObj->wtrans[0], transObj->wtrans[1], transObj->wtrans[2] };
    f32 dyaw = -(f32)((double)kTwoPi - v11);             // v35 = -(flt_6101E4 - v11)
    wt[1] = wt[1] + dyaw;                                // v23 = v23 + v35
    transObj->wtrans[1] = wt[1];
    if (H.setWorldTranslation) H.setWorldTranslation(transObj, wt);  // 0x4031a1

    // Terrain / floor height blend.  In the binary BOTH branches of the
    // WorldToTileWithHeight test call SetPosition once (the hit-branch after the
    // height blend, the miss-branch with the un-blended position) — except the one
    // walkable/no-floor sub-case which jumps to LABEL_16 and skips SetPosition.
    f32 worldPos[3] = { transObj->pos[0], transObj->pos[1], transObj->pos[2] };
    int floorHandle = H.floorHandle ? H.floorHandle(ch) : 0;  // v13 = *(a1[34]+172)
    bool skipSetPos = false;                                  // goto LABEL_16 (no SetPosition)
    int tileXY[2] = {0,0};
    f32 wHeight = 0.0f;
    if (H.worldToTileWithHeight &&
        H.worldToTileWithHeight(meshCtx, worldPos, tileXY, &wHeight)) {  // 0x4031c0
        int v34 = H.tileTypeAt ? H.tileTypeAt(meshCtx, tileXY[0], tileXY[1]) : 0;  // 0x4031ed
        f32 v27 = wHeight;
        f32 v28 = 0.0f;
        // (!v34 || v34==13) && !floorHandle  ->  goto LABEL_16 (0x4032cb/0x4032de)
        if ((v34 == 0 || v34 == 13) && floorHandle == 0) {
            skipSetPos = true;                            // LABEL_16, SetPosition skipped
        } else {
            if (floorHandle) {                            // 0x403214: v13 != 0
                int ftile = 0;
                if (H.pickTileAtPoint)
                    H.pickTileAtPoint(floorHandle, worldPos, 1, &ftile, &v28);  // 0x40322e
            }
            if (!v34 || v34 == 13 || (double)(v28 - v27) >= kHeightStepThresh)  // 0x403259
                v27 = v28;                                // 0x4032e4
            // if ( !IndexFromPointer(universe) ) v27 += 3.0
            int uidx = H.indexFromPointer ? H.indexFromPointer(ch->universe) : 0;  // 0x403265
            if (!uidx)
                v27 = (f32)((double)v27 + kDeflateHeightBias);  // 0x40327c
            // y = (v27 - y) * 0.25 + y
            transObj->pos[1] = (v27 - transObj->pos[1]) * kHeightLerp + transObj->pos[1];  // 0x403295
            worldPos[1] = transObj->pos[1];
        }
    }
    // hit-non-skip OR miss: SetPosition(v7, v7+76).  Only the walkable/no-floor
    // sub-case (skipSetPos) jumps straight to LABEL_16.
    if (!skipSetPos) {
        if (H.setPosition) H.setPosition(transObj, worldPos);  // 0x403298
    }

    // LABEL_16: update anchor = char object position
    tr->last[0] = charObj->pos[0];                       // 0x40329d
    tr->last[1] = charObj->pos[1];                       // 0x4032b2
    tr->last[2] = charObj->pos[2];                       // 0x4032be
    return reinterpret_cast<intptr_t>(tr) != 0 ? 1 : 0;  // result = a1[73] (nonzero ptr)
}

} // namespace guild::sim
