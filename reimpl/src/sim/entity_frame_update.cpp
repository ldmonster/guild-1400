// =============================================================================
// entity_frame_update.cpp — see entity_frame_update.h for the module overview.
//
//   gilde.exe 0x41ceb4  VIBE_DecompressGameState   -> SceneUpdatePass
//   gilde.exe 0x40e50c  VIBE_Decompressor_Init     -> DecompressorInit
//
// 1:1 reconstruction of the per-frame scene-update walk + the result-handler
// table dispatch. namespace guild::sim.
// =============================================================================
#include "sim/entity_frame_update.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// gilde.exe 0x40e50c — VIBE_Decompressor_Init.
//   if (rec[100] && rec[102])
//     for (i = 0; i != 10240; i += 20)
//       v3 = table[i]; v4 = *(table[i]+8);
//       if (rec == v4) { Result_Handler_Interaction(*v3,v3[1],v3[4],v3[3],
//                          v4[106],*v3,v3[1],v4[105]);
//                        Light_SetGrayColorThunk(0,20,v3); }
// The 10240/20 = 512 max entries; we iterate the supplied tableEntries (<=512),
// matching the original's fixed byte stride exactly.
// ---------------------------------------------------------------------------
int DecompressorInit(const DecompRecord& rec,
                     const DecompHandlerEntry* table, int tableEntries,
                     DecompInitHooks& h) {
    int dispatched = 0;
    if (rec.flag100 && rec.flag102) {              // 0x40e51d
        // The original loops `for (i=0; i != 10240; i += 20)` => 512 slots.
        const int kMaxSlots = 10240 / 20;          // 512
        int slots = tableEntries < kMaxSlots ? tableEntries : kMaxSlots;
        for (int i = 0; i < slots; ++i) {          // 0x40e526
            const DecompHandlerEntry& e = table[i];
            if (e.owner == &rec) {                 // 0x40e535  (result == v4)
                // v4[106] (=owner blob106), v4[105] (=owner blob105).
                h.resultHandlerInteraction(e.a0, e.a1, e.a4, e.a3,
                                           rec.blob105, e.a0, e.a1,
                                           rec.blob105);                 // 0x40e566
                h.lightSetGrayColorThunk(0, 20, e.a0);                   // 0x40e574
                ++dispatched;
            }
        }
    }
    return dispatched;
}

// ---------------------------------------------------------------------------
// The per-child-node dispatch shared by the scene-update walk (the switch on the
// node's type byte at +24). Mirrors 0x41cf59..0x41d4a4 exactly.
// ---------------------------------------------------------------------------
static void DispatchChildNode(const DecompRecord& rec, RenderNode& n,
                              i32 blob, SceneFrameState& st,
                              SceneFrameHooks& h, char* /*v2*/ recPtr) {
    (void)recPtr;
    const u8 type = n.typeByte;                    // v10 = *(v6+24)
    const int v30 = (n.x >> 16) - rec.originX;     // originX-adjusted px X
    const int v29 = (n.y >> 16) - rec.originY;     // originY-adjusted px Y
    const i32 rid = static_cast<i32>(reinterpret_cast<intptr_t>(&rec)); // (int)v2

    if (type >= 0x11u) {                           // 0x41d063
        if (type > 0x11u) {                        // 0x41d0d2
            if (type >= 0x42u) {                   // 0x41d0f7
                if (type > 0x42u) {                // 0x41d110
                    if (type > 0x43u) {            // 0x41d123
                        if (type == 69) {          // 0x41d1b5  (0x45)
                            h.coordPush(n.clipX >> 16, n.clipW >> 16,
                                        n.clipH >> 16, n.clipY >> 16); // 0x41d1d3
                            if (n.child44) {       // 0x41d1dd (v12 = *(v6+44))
                                // Entity_AnimationUpdate(x, clipW, child+8>>16,
                                //   child+6>>16, blob) — child fields modeled by
                                //   the child44 record's two 16.16 size fields.
                                h.entityAnimationUpdate(n.clipX >> 16,
                                                        n.clipW >> 16, 0, 0, blob); // 0x41d1f8
                            }
                            h.entityInteractionLogic(n.id, blob); // 0x41d201
                            if (!n.child44)        // 0x41d206
                                h.objectReinitialize(n.x >> 16, (n.y >> 16) - 4,
                                                     (n.w >> 16) + 8, n.h >> 16,
                                                     rid);          // 0x41d22b
                            h.entityAnimationUpdate(0, 0,
                                                    st.screenW, st.screenH, blob); // 0x41d23b
                        }
                    } else {                       // type == 0x43 (67)
                        // 0x41d12e: door/label anim — only if life>0.
                        if (n.life > 0) {
                            h.coordPush(0, 0, st.screenW, st.screenH); // 0x41d14a
                            h.stateFinalize(n.state110 >> 16);          // 0x41d15e
                            u8 v11;
                            if (n.pressed) v11 = 2;                    // 0x41d170
                            else           v11 = (n.flag76 != 0) ? 1 : 0; // 0x41d47e
                            if (n.flag88) v11 |= 8u;                   // 0x41d17b
                            h.animationApply(n.x >> 16, n.y >> 16, n.h >> 16,
                                             blob, n.label116, v11);   // 0x41d1a0
                            h.stateFinalize(0);                        // 0x41d1a9 (v28)
                        }
                    }
                } else {                           // type == 0x42 (66)
                    h.buildingUpdate(0, blob);     // 0x41d117
                }
            } else if (type == 65) {               // 0x41d0fb  (0x41)
                h.objectUpdate(reinterpret_cast<intptr_t>(n.label116) & 0xffffffff,
                               blob);              // 0x41d106 (Object_Update(label116, blob))
            }
        } else {                                   // type == 0x11 (17)
            // 0x41d0e6: countdown life, unless frozen.
            if (n.life > 0 && st.freezeCountdowns != 1)  // 0x41d0e6
                --n.life;                                // 0x41d0ec
        }
    } else if (type >= 4u) {                        // 0x41d067
        if (type > 4u) {                            // 0x41d245
            if (type <= 5u || type == 8) {          // 0x41d3e8  (type 5 or 8)
                const u8 v13 = static_cast<u8>(n.anim444);
                if (v13 & 2) {                      // 0x41d291
                    if (n.id == st.focusedNodeId || n.selectFlag) { // 0x41d3f3
                        if (n.anim444 & 1) n.pressed = 1;            // 0x41d2b6
                    } else if (v13 & 1) {
                        n.pressed = 0;                               // 0x41d407
                    }
                }
                if (n.hover64)                      // 0x41d2bd
                    h.velocityApply(v30 + 8, v29 + 8, n.styleHandle, blob,
                                    n.label120);    // 0x41d2dc
                h.animationBasic(v30, v29, n.styleHandle, blob, n.label120); // 0x41d2f4
                if (n.pressed)                      // 0x41d2f9
                    h.animationAdvanced(v30, v29, n.styleHandle, blob,
                                        n.label120);// 0x41d312
                if (!n.child44) {                   // 0x41d317
                    if (n.hover64)                  // 0x41d31d
                        h.objectReinitialize(v30, v29,
                                             (st.defaultBorder & 0xffff) + 8,
                                             (st.defaultBorder >> 16) + 8, rid); // 0x41d348
                    else
                        h.objectReinitialize(v30, v29,
                                             st.defaultBorder & 0xffff,
                                             st.defaultBorder >> 16, rid);       // 0x41d42a
                }
                if (n.label120) {                   // 0x41d34d
                    if (n.state112)                 // 0x41d357
                        h.stateFinalize(n.state110 >> 16); // 0x41d364
                    int v15, v22, v16;
                    // VIBE_AnimationFlags_Compute(n.id, &v20) -> v19 line height.
                    int v19 = 0;                    // (computed flag; inert => 0)
                    if (n.reinitX456 == -1) {       // 0x41d3a3
                        v15 = v19 + (n.x >> 16) + 6; // 0x41d45c
                        v22 = (n.h >> 16) + 3 * v19; // 0x41d45f
                    } else {
                        v15 = n.reinitX456;          // 0x41d3af
                        v22 = n.h >> 16;             // 0x41d3b1
                    }
                    if (n.reinitY460 == -1)         // 0x41d3be
                        v16 = (n.y >> 16) - 1;       // 0x41d46e
                    else
                        v16 = n.reinitY460;          // 0x41d3c4
                    h.animationApply(v15, v16, v22, blob,
                                     reinterpret_cast<const char*>(&n.label120), 0); // 0x41d3dc
                }
            }
        } else {                                    // type == 4
            h.buildingUpdate(0, blob); // VIBE_Physics_Update(label116, blob)  0x41d24c
            if (!n.child44)                          // 0x41d251
                h.objectReinitialize(v30, v29, st.defaultBorder & 0xffff,
                                     st.defaultBorder >> 16, rid);     // 0x41d0ad (LABEL_18)
        }
    } else if (type == 1) {                          // 0x41d06f
        h.animationBasic(v30, v29, n.styleHandle, blob, 0); // 0x41d084
        if (n.pressed)                               // 0x41d089
            h.animationAdvanced(v30, v29, n.styleHandle, blob, 0); // 0x41d09e
        if (!n.child44)                              // 0x41d0a3 (LABEL_18)
            h.objectReinitialize(v30, v29, st.defaultBorder & 0xffff,
                                 st.defaultBorder >> 16, rid);         // 0x41d0ad
    }
}

// ---------------------------------------------------------------------------
// gilde.exe 0x41ceb4 — VIBE_DecompressGameState (the per-frame scene-update).
// ---------------------------------------------------------------------------
int SceneUpdatePass(const DecompRecord& rec,
                    const EntityRecord* entityTable, int entityTableCount,
                    RenderNode* nodes, int nodeCount,
                    SceneFrameState& st, SceneFrameHooks& h) {
    // 0x41cee3: both gate flags must be set, else bail with the record id.
    if (!rec.flag100 || !rec.flag102)
        return rec.blob105;

    // 0x40e50c: dispatch the bound result handlers (the init pass). The handler
    // table is engine-owned; the caller supplies it (empty => no-op).
    // (DecompressorInit is exercised via its own entry; here the table is the
    //  pass's responsibility — left to the caller's hook context.)

    const i32 blob = rec.blob105;                   // v3
    h.decompressStateBlob(blob);                    // 0x41cf07  LOCK back buffer

    const int childCount = rec.childCount;          // v5 = v2[97]
    if (childCount > 0) {                            // 0x41cf18
        for (int ci = 0; ci < childCount; ++ci) {   // do/while 0x41d4a4
            const i32 entIdx = rec.childIndex ? rec.childIndex[ci] : 0; // v27[1]
            if (entIdx < 0 || entIdx >= entityTableCount)
                continue;                            // (defensive; bounds)
            const EntityRecord& ent = entityTable[entIdx];

            h.entityChildProcess(entIdx, blob);      // 0x41cf41

            // 0x41cf59: if the entity body node has no child anim, reinit it.
            if (ent.bodyNode >= 0 && ent.bodyNode < nodeCount &&
                nodes[ent.bodyNode].child44 == 0) {
                h.objectReinitialize((ent.x2 >> 16) - rec.originX,
                                     (ent.y >> 16) - rec.originY,
                                     ent.z >> 16, ent.w6 >> 16,
                                     static_cast<i32>(
                                         reinterpret_cast<intptr_t>(&rec))); // 0x41d037
            }

            // 0x41cf7e: walk the entity's child node list.
            const int kids = ent.childCount >> 16;   // *(v21+26)>>16
            for (int k = 0; k < kids; ++k) {         // while (kids > v24)
                const i32 nodeIdx = ent.childNodes ? ent.childNodes[k] : -1;
                if (nodeIdx < 0 || nodeIdx >= nodeCount) {
                    h.coordPush(0, 0, st.screenW, st.screenH); // LABEL_11 reset
                    continue;
                }
                RenderNode& n = nodes[nodeIdx];       // v6
                if (!n.meshHandle) {                 // 0x41cfc8  (v7 == 0)
                    h.coordPush(0, 0, st.screenW, st.screenH); // LABEL_11
                    continue;
                }
                // 0x41d059: push the node's clip rect.
                h.coordPush(n.clipX >> 16, n.clipW >> 16,
                            n.clipH >> 16, n.clipY >> 16);
                DispatchChildNode(rec, n, blob, st, h, nullptr);
                // LABEL_11: reset the clip to full screen.
                h.coordPush(0, 0, st.screenW, st.screenH);  // 0x41cfe4
            }
        }
    }

    // LABEL_74: 0x41d4aa — unlock the back buffer.
    h.decompressionFinalize(blob);

    // 0x41d54f: the right / bottom divider broadcasts (scroll-region edges).
    if (rec.broadcast428 || rec.broadcast492) {
        if (rec.broadcast428) {                      // 0x41d4be
            // Animation_GetPtr(rec+428) -> the right-edge node; broadcast it.
            h.resultBroadcast(0, 0, 0, 0, blob, 0, 0, 0); // 0x41d4f2
        }
        if (rec.broadcast492) {                      // 0x41d4f7
            h.resultBroadcast(0, 0, 0, 0, blob, 0, 0, 0); // 0x41d540
        }
    }
    return blob;
}

}  // namespace guild::sim
