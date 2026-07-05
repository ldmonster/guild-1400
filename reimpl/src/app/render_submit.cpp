// gilde.exe — per-frame render-submit slot builders (guild::app). See
// render_submit.h for the data-flow + offset map. The control flow mirrors the
// Hex-Rays pseudocode of VIBE_GameLogic_Objects @0x412fa0 and
// VIBE_GameLogic_Entities @0x413580 verbatim; the cross-module sibling leaves are
// injected hooks and the entity array / widget slot use caller-supplied bases.
#include "app/render_submit.h"

namespace guild::app {

using guild::gui::Widget;
using guild::gui::g_widgets;
using guild::gui::Widget_AllocSlot;
using guild::gui::kMaxWidgets;

// gilde.exe 0x412fa0 — VIBE_GameLogic_Objects.
int GameLogicObjects(i16 x, i16 y, int entityIdx, EntitySubmitRecord* entities,
                     int entityCount, const RenderSubmitHooks& hooks) {
    // LOWORD(v24) = a1;  LOWORD(v25) = a2;  v23 = 0;
    int v24 = x;
    int v25 = y;

    // if ( a3 > dword_62D208 ) return -1;
    if (entityIdx > entityCount)
        return -1;

    // v4 = VIBE_Widget_AllocSlot();  if ( v4 >= 512 ) return -1;
    int v4 = Widget_AllocSlot();
    if (v4 < 0 || v4 >= kMaxWidgets)
        return -1;

    // VIBE_Gui_ResolveObjectState(a3, &v23, &v22);
    int v23 = 0;   // resolved render-state handle (-> widget +12)
    int v22 = 0;   // resolved entity index        (-> widget +8)
    if (hooks.resolveObjectState)
        hooks.resolveObjectState(entityIdx, &v23, &v22);

    // The original's v5/v8/v20 register-aliases are all the allocated widget ptr.
    Widget& w = g_widgets[v4];

    // *(v5 + 12) = v23;  *(v5 + 8) = v22;
    w.at<i32>(12) = v23;
    w.at<i32>(8)  = v22;

    // v7 = *(_BYTE *)(dword_62D204 + 84 * v6 + 60);   v6 = v22 (resolved index)
    u8 v7 = 0;
    if (v22 >= 0 && v22 <= entityCount)
        v7 = static_cast<u8>(entities[v22].kind);

    // *(_WORD *)(v5 + 26) = 2;  *(_BYTE *)(v5 + 24) = v7;
    w.at<i16>(26) = 2;
    w.at<u8>(24)  = v7;
    // *(_WORD *)(v5 + 16) = v24;  *(_WORD *)(v5 + 18) = v25;
    w.at<i16>(16) = static_cast<i16>(v24);
    w.at<i16>(18) = static_cast<i16>(v25);
    // clip/screen bounds (+28/+30/+32/+34) = dword_64A1B4/BC/B8/C0.
    w.at<i16>(28) = hooks.clipX0;
    w.at<i16>(30) = hooks.clipX1;
    w.at<i16>(32) = hooks.clipY0;
    w.at<i16>(34) = hooks.clipY1;

    // VIBE_Gui_MarkObjectUsed(v22);
    if (hooks.markObjectUsed)
        hooks.markObjectUsed(v22);

    // if ( *(_BYTE *)(v8 + 24) == 8 ) *(_DWORD *)(v8 + 72) = 1;
    if (w.at<u8>(24) == 8)
        w.at<i32>(72) = 1;
    // *(_DWORD *)(v8 + 464) = -1;  *(_DWORD *)(v8 + 468) = 0;  *(_DWORD *)(v8 + 472) = 0;
    w.at<i32>(464) = -1;
    w.at<i32>(468) = 0;
    w.at<i32>(472) = 0;

    // v9 = *(_BYTE *)(v8 + 24);   (the kind byte, dispatch below)
    u8 v9 = w.at<u8>(24);

    // The geometry handle for kinds 1/4/5/8/17 (= *(dword_69FFB4 + 740*v4 + 12)).
    int geom = hooks.slotGeometry ? hooks.slotGeometry(v4) : 0;

    if (v9 >= 5u) {
        if (v9 <= 5u)
            goto LABEL_18;
        if (v9 < 8u)        // kinds 6,7
            return v4;
        if (v9 <= 8u) {     // kind 8
        LABEL_18:           // kinds 5 and 8 — grid placement via Coord_Transform.
            // *(_DWORD *)(v8 + 116) = dword_62D2A4;
            // (the grid-offset delta ResolveObjectState published; here forwarded
            //  through the resolve hook's side effect — modeled as the metric src.)
            w.at<i32>(116) = 0; // dword_62D2A4 (set by resolveObjectState side effect)

            // v18 = *(geom + 12);  v19 = VIBE_Coord_Transform(v18, *(geom + 116));
            int v18 = geom;
            int v19 = hooks.coordTransform ? hooks.coordTransform(v18, w.at<i16>(116)) : 0;
            // *(_WORD *)(v20 + 20) = *(_WORD *)(v19 + 6);
            // LOWORD(v19) = *(_WORD *)(v19 + 10);
            i16 metric6  = hooks.gridMetricWord ? static_cast<i16>(hooks.gridMetricWord(v19, 6))  : 0;
            i16 metric10 = hooks.gridMetricWord ? static_cast<i16>(hooks.gridMetricWord(v19, 10)) : 0;
            w.at<i16>(20) = metric6;
            // *(_DWORD *)(v20 + 456) = -1;  *(_DWORD *)(v20 + 460) = -1;
            w.at<i32>(456) = -1;
            w.at<i32>(460) = -1;
            // *(_WORD *)(v20 + 22) = v19;  (low word of metric10)
            w.at<i16>(22) = metric10;
            // *(_DWORD *)(v20 + 448) = *(unsigned __int16 *)(v18 + 44);
            // *(_DWORD *)(v20 + 452) = *(unsigned __int16 *)(v18 + 46);
            i32 geom44 = hooks.gridMetricWord ? (hooks.gridMetricWord(v18, 44) & 0xFFFF) : 0;
            i32 geom46 = hooks.gridMetricWord ? (hooks.gridMetricWord(v18, 46) & 0xFFFF) : 0;
            w.at<i32>(448) = geom44;
            w.at<i32>(452) = geom46;
            // *(_BYTE *)(v17 + dword_69FFB4 + 104) = 8;  *(_BYTE *)(v17 + 105) = 8;
            w.at<u8>(104) = 8;
            w.at<u8>(105) = 8;
            return v4;
        } else {
            if (v9 != 17)
                return v4;
            // kind 17.
            // v12 = *(geom + 12);
            int v12 = geom;
            // *(_WORD *)(... + 20) = *(_WORD *)(v12 + 12);
            // LOWORD(v12) = *(_WORD *)(v12 + 14);
            i16 m12 = hooks.gridMetricWord ? static_cast<i16>(hooks.gridMetricWord(v12, 12)) : 0;
            i16 m14 = hooks.gridMetricWord ? static_cast<i16>(hooks.gridMetricWord(v12, 14)) : 0;
            w.at<i16>(20) = m12;
            // *(_DWORD *)(... + 116) = 2;
            w.at<i32>(116) = 2;
            // *(_WORD *)(... + 22) = v12;
            w.at<i16>(22) = m14;
            return v4;
        }
    } else {
        if (!v9)            // kind 0
            return v4;
        if (v9 <= 1u) {     // kind 1
            // v13 = *(geom + 12);
            int v13 = geom;
            // *(_WORD *)(v8 + 20) = *(_WORD *)(v13 + 44);
            // *(_WORD *)(v8 + 22) = *(_WORD *)(v13 + 46);
            w.at<i16>(20) = hooks.gridMetricWord ? static_cast<i16>(hooks.gridMetricWord(v13, 44)) : 0;
            w.at<i16>(22) = hooks.gridMetricWord ? static_cast<i16>(hooks.gridMetricWord(v13, 46)) : 0;
            return v4;
        } else {
            if (v9 != 4)    // kinds 2,3
                return v4;
            // kind 4 — register a shape-anim slot.
            // v14 = VIBE_ShapeAnim_RegisterSlot(v24, v25, 0, *(geom + 12));
            int v14 = hooks.shapeAnimRegisterSlot
                          ? hooks.shapeAnimRegisterSlot(v24, v25, geom)
                          : 0;
            // *(_DWORD *)(... + 116) = v14;
            w.at<i32>(116) = v14;
            // v16 = *(geom + 12);
            int v16 = geom;
            // *(_WORD *)(... + 20) = *(_WORD *)(v16 + 44);
            // *(_WORD *)(... + 22) = *(_WORD *)(v16 + 46);
            w.at<i16>(20) = hooks.gridMetricWord ? static_cast<i16>(hooks.gridMetricWord(v16, 44)) : 0;
            w.at<i16>(22) = hooks.gridMetricWord ? static_cast<i16>(hooks.gridMetricWord(v16, 46)) : 0;
            return v4;
        }
    }
}

// gilde.exe 0x413580 — VIBE_GameLogic_Entities.
int GameLogicEntities(i16 scrX, i16 scrY, int entityIdx, int out,
                      EntitySubmitRecord* entities, int entityCount,
                      const RenderSubmitHooks& hooks) {
    // v4 = result(scrX);  v13[0..3] = a3(entityIdx);  v13[4..5] = a2(scrY).
    i16 v4 = scrX;
    int idx = entityIdx;   // *(int *)v13
    int result = 0;

    // if ( a3 <= dword_62D208 ) { ... }
    if (idx <= entityCount) {
        int v6 = (idx >= 0 && idx <= entityCount) ? entities[idx].kind : 0; // +60
        int v7 = -1;       // partner index (-1 => no redirect happened)
        int v12 = 0;       // ecx — state handle (xor ecx,ecx @0x413598)

        // if ( (v6 == 5 || v6 == 8) && !*(_DWORD *)(rec(idx)+48) )
        if ((v6 == 5 || v6 == 8) && entities[idx].suppress == 0) {
            int partner = entities[idx].linkIndex; // *(rec(idx)+76)
            // if ( !*(_DWORD *)(rec(partner)+52) ) ecx = VIBE_State_Update(idx);
            // (0x41360f call + 0x413614 mov ecx, eax — the result IS kept.)
            if (partner >= 0 && partner <= entityCount &&
                entities[partner].stateHandle == 0) {
                if (hooks.stateUpdate)
                    v12 = hooks.stateUpdate(idx);
            }
            v7 = idx;
            idx = partner;  // *(int *)v13 = *(rec(idx)+76)
        }
        // else if ( !*(_DWORD *)(rec(idx)+52) ) ecx = VIBE_State_Update(idx);
        // (0x41374d call + 0x413752 mov ecx, eax)
        else if (idx >= 0 && idx <= entityCount && entities[idx].stateHandle == 0) {
            if (hooks.stateUpdate)
                v12 = hooks.stateUpdate(idx);
        }

        // if ( (*(_BYTE *)(rec(idx)+68) & 2) != 0 ) idx += byte_62D220;
        if (idx >= 0 && idx <= entityCount && (entities[idx].flag68 & 2) != 0)
            idx += hooks.gridOffset;

        // 0x413663: if ( !ecx ) ecx = *(rec(idx)+52);  — the state-handle
        // fallback loads the POST-redirect/POST-offset record's +52 dword.
        if (v12 == 0 && idx >= 0 && idx <= entityCount)
            v12 = entities[idx].stateHandle;

        // blob index: v7 == -1 ? 21*idx : 21*v7  (21 dwords == the 84-byte rec).
        int blobIdx = (v7 == -1) ? idx : v7;
        // 0x4136a4: edx = *(dword_62D204 + 4*(21*blobIdx) + 60) — the +60 dword
        // of record blobIdx (the same dword the kind reads use).
        int blob = hooks.stateBlob
                       ? hooks.stateBlob(blobIdx)
                       : ((blobIdx >= 0 && blobIdx <= entityCount)
                              ? entities[blobIdx].kind
                              : 0);
        // result = VIBE_DecompressState_Blob(out, blob);
        result = hooks.decompressBlob ? hooks.decompressBlob(out, blob) : 0;

        if (result) {
            // 0x4136b3: cmp edx, 5 — the dispatch selector is the BLOB VALUE
            // itself (edx survives the call), i.e. the +60 dword of the blobIdx
            // record — NOT the post-redirect record's kind.
            const unsigned v11 = static_cast<unsigned>(blob);
            bool doBasic = false;
            // if ( v11 >= 5 ) { if (v11>5 && v11!=8) goto L16;
            //                   if (v7 != -1) { Animation_Basic(..., (u8)(v7-idx)); goto L16; } }
            // else if ( !v11 || v11>1 && v11!=4 ) goto L16;
            // Animation_Basic(...,0);
            if (v11 >= 5) {
                if (v11 > 5 && v11 != 8) {
                    /* goto LABEL_16 */
                } else if (v7 != -1) {
                    // 0x41377d..0x41378f: al = (u8)v7 - (u8)idx; and eax, 0FFh;
                    // push eax — the delta is a BYTE subtraction zero-extended.
                    // 3rd arg (ecx) is the state handle v12 (0x4136d5 call path).
                    if (hooks.animationBasic)
                        hooks.animationBasic(
                            v4, scrY, v12, out,
                            static_cast<int>(
                                static_cast<u8>(v7 - idx)));
                    /* goto LABEL_16 */
                } else {
                    doBasic = true;
                }
            } else if (!v11 || (v11 > 1 && v11 != 4)) {
                /* goto LABEL_16 */
            } else {
                doBasic = true;
            }

            if (doBasic) {
                // VIBE_Animation_Basic(v4, scrY>>16, ecx=v12, out, 0);
                if (hooks.animationBasic)
                    hooks.animationBasic(v4, scrY, v12, out, 0);
            }

            // LABEL_16 (0x4136da..0x41370c):
            //   VIBE_State_GetCurrent(v4, scrY,
            //       *(int *)(rec(idx)+0x50) >> 16,   // = i16 at +82 (subStateHi)
            //       *(int *)(rec(idx)+0x4E) >> 16);  // = i16 at +80 (subStateLo)
            i32 w = (idx >= 0 && idx <= entityCount) ? entities[idx].subStateHi : 0;
            i32 h = (idx >= 0 && idx <= entityCount) ? entities[idx].subStateLo : 0;
            if (hooks.stateGetCurrent)
                hooks.stateGetCurrent(v4, scrY, w, h);
            // return VIBE_Decompression_Finalize(out);
            return hooks.decompressionFinalize ? hooks.decompressionFinalize(out)
                                               : 0;
        }
    }
    return result;
}

} // namespace guild::app
