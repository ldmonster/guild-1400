// anim_recon4_mesh_lru.cpp — anim/mesh/shape leaves (recon4 cluster)
// See anim_recon4_mesh_lru.h for provenance and per-function documentation.

#include "render/anim_recon4_mesh_lru.h"

namespace guild::render::anim_recon4 {

// gilde.exe 0x5cfd24 — VIBE_Anim_ComputeMeshMemorySize
i32 ComputeMeshMemorySize(const void* mesh, const Arena& arena) {
    if (!mesh)
        return -1;                                   // a1 == 0
    const u32 sub = rd_u32(mesh, mesh_off::SUBARRAY); // a1[87]
    if (!sub)
        return -1;
    const i32 vertCount = rd_i32(mesh, mesh_off::VERT_COUNT); // a1[80]
    const i32 subCount  = rd_i32(mesh, mesh_off::SUB_COUNT);  // a1[82]
    // The original probes the *first* sub-record's buffer presence via absolute
    // addresses (sub + 188/180/184). `sub` is the arena offset of the sub-array
    // record (a1[87]); resolve it through the arena, byte-exact.
    const void* subRec = arena.at(sub);
    i32 v2 = 0;
    if (rd_u32(subRec, mesh_off::SUB_BUF_C))          // +188
        v2 = 12 * vertCount;
    if (rd_u32(subRec, mesh_off::SUB_BUF_A))          // +180
        v2 += 3 * vertCount;
    if (rd_u32(subRec, mesh_off::SUB_BUF_B))          // +184
        v2 += 3 * vertCount;
    return subCount * (v2 + 192) + 364;
}

// gilde.exe 0x5cfdb8 — VIBE_Anim_EvictMeshesForBudget
char EvictMeshesForBudget(i32 budget, i32 usage,
                          u32 listHead, u32 sentinel,
                          u32 lruSeed,
                          const Arena& arena,
                          const MeshAnimHooks& hooks) {
    if (budget < 0)               // a1 < 0
        return 0;
    while (usage > budget) {      // a2 > a1
        u32  v4 = lruSeed;        // dword_649D58 (running-min seed)
        u32  victim = 0;          // i (arena offset, 0 == NULL)
        // Scan [head .. sentinel) for the smallest LRU key among non-busy nodes.
        for (u32 node = listHead; node != sentinel;
             node = rd_u32(arena.at(node), mesh_off::LIST_NEXT)) {
            const void* n = arena.at(node);
            if (!rd_u32(n, mesh_off::REFCOUNT)            // +332 == 0 (free)
                && v4 >= rd_u32(n, mesh_off::LRU_KEY)) {  // +344 <= running min
                victim = node;
                v4 = rd_u32(n, mesh_off::LRU_KEY);
            }
        }
        if (!victim)
            return 0;
        usage -= ComputeMeshMemorySize(arena.at(victim), arena);
        // gilde.exe 0x5cfe10: `mov edx, 1` — the eviction call passes a2 = 1
        // (unlink + free), NOT 0.
        ReleaseMeshData(victim, /*unlink=*/true, listHead, sentinel, budget,
                        arena, hooks);
    }
    return 1;
}

// gilde.exe 0x5cfe30 — VIBE_Anim_ReleaseMeshData
char ReleaseMeshData(u32 mesh, bool unlink,
                     u32 listHead, u32 sentinel,
                     i32 budget,
                     const Arena& arena,
                     const MeshAnimHooks& hooks) {
    void* v4 = arena.at(mesh);
    char ret = static_cast<char>(mesh);             // LOBYTE(a1)
    while (true) {
        i32 v5 = rd_i32(v4, mesh_off::REFCOUNT) - 1;  // v4[83] - 1
        wr_i32(v4, mesh_off::REFCOUNT, v5);
        if (v5 > 0)
            return ret;                               // still referenced
        if (unlink) {
            // Unlink from the intrusive list. The binary does:
            //   *(v4[88] + 356) = v4[89];   -> next.prev = prev
            //   *(v4[89] + 352) = v4[88];   -> prev.next = next
            const u32 next = rd_u32(v4, mesh_off::LIST_NEXT); // v4[88]
            const u32 prev = rd_u32(v4, mesh_off::LIST_PREV); // v4[89]
            if (next)
                wr_u32(arena.at(next), mesh_off::LIST_PREV, prev);
            if (prev)
                wr_u32(arena.at(prev), mesh_off::LIST_NEXT, next);
            // Free each sub-record's three buffers.
            const i32 subCount = rd_i32(v4, mesh_off::SUB_COUNT); // v4[82]
            const u32 subBase  = rd_u32(v4, mesh_off::SUBARRAY);  // v4[87]
            if (subCount > 0) {
                void* subRec = arena.at(subBase);
                i32 off = 0;
                for (i32 i = 0; i < subCount; ++i) {
                    const i32 bufC_off = off + mesh_off::SUB_BUF_C;
                    const u32 cbuf = rd_u32(subRec, bufC_off);
                    if (cbuf) {
                        if (hooks.freeDebug) hooks.freeDebug(cbuf, hooks.ctx);
                        wr_u32(subRec, bufC_off, 0);
                    }
                    const i32 bufA_off = off + mesh_off::SUB_BUF_A;
                    const u32 abuf = rd_u32(subRec, bufA_off);
                    if (abuf) {
                        if (hooks.freeDebug) hooks.freeDebug(abuf, hooks.ctx);
                        wr_u32(subRec, bufA_off, 0);
                    }
                    const i32 bufB_off = off + mesh_off::SUB_BUF_B;
                    const u32 bbuf = rd_u32(subRec, bufB_off);
                    if (bbuf) {
                        if (hooks.freeDebug) hooks.freeDebug(bbuf, hooks.ctx);
                        wr_u32(subRec, bufB_off, 0);
                    }
                    off += mesh_off::SUB_STRIDE; // 192
                }
            }
            if (subBase && hooks.freeDebug) hooks.freeDebug(subBase, hooks.ctx);
            wr_u32(v4, mesh_off::SUBARRAY, 0);     // v4[87] = 0
            // Free the node itself.
            if (hooks.freeDebug) hooks.freeDebug(mesh, hooks.ctx);
            return 0; // LOBYTE(a1) = FreeDebug(...); FreeDebug noop -> 0
        }
        // unlink == false: recompute global usage, evict if over budget.
        wr_i32(v4, mesh_off::REFCOUNT, 0);          // v4[83] = 0
        i32 total = 0;
        for (u32 node = listHead; node != sentinel;
             node = rd_u32(arena.at(node), mesh_off::LIST_NEXT)) {
            const void* n = arena.at(node);
            if (!rd_u32(n, mesh_off::REFCOUNT))
                total += ComputeMeshMemorySize(n, arena);
        }
        const i32 thisSize = ComputeMeshMemorySize(v4, arena);
        // if (thisSize + total <= budget) return thisSize;
        if (static_cast<u32>(thisSize) + static_cast<u32>(total)
            <= static_cast<u32>(budget))
            return static_cast<char>(thisSize);
        ret = EvictMeshesForBudget(budget - thisSize, total, listHead, sentinel,
                                   /*lruSeed=*/0, arena, hooks);
        if (ret)
            return ret;
        unlink = true;  // a2 = 1; loop (refcount already 0, decremented below)
    }
}

// gilde.exe 0x5cbfc0 — VIBE_Anim_AssignSubMeshBones
char AssignSubMeshBones(void* mesh, const MeshAnimHooks& /*hooks*/) {
    if (!mesh)
        return 0;
    const u32 rec = rd_u32(mesh, 492);     // *(mesh+492)
    if (!rec)
        return 0;
    // v10 = rec + 272 (sub-mesh slot base), v11 = rec + 244 (flag source base).
    const i32 slotBase = static_cast<i32>(rec) + 272;
    const i32 flagBase = static_cast<i32>(rec) + 244;
    for (i32 i = 0; i < 3; ++i) {                   // v12 < 3
        const i32 flagRec = flagBase + 116 * i;     // v11
        if (rd_u32(mesh, flagRec + 132)) {          // *(v11+132)
            const i32 slot = slotBase + 116 * i;    // v3
            const u32 src  = rd_u32(mesh, slot + 104); // v14 = *(v3+104)
            // Reset 4 bone-index bytes (+112..+115) to 0xFF.
            for (i32 b = 0; b < 4; ++b)
                wr_u8(mesh, slot + 112 + b, 0xFF);
            i32 v4 = 0;                              // free-slot cursor
            const i32 boneCount = rd_i32(mesh, static_cast<i32>(src) + 324);
            i32 v15 = 0;                             // bone index
            do {
                if (v15 >= boneCount)
                    break;
                const char* boneName = reinterpret_cast<const char*>(
                    static_cast<const u8*>(mesh) + static_cast<i32>(src) + 64
                    + 64 * v15);
                // Walk child list: head at mesh+508, next at child+496,
                // child name at *(child+492)+180.
                for (u32 child = rd_u32(mesh, 508); child;
                     child = rd_u32(mesh, static_cast<i32>(child) + 496)) {
                    const u32 childRec = rd_u32(mesh, static_cast<i32>(child) + 492);
                    const char* childName = reinterpret_cast<const char*>(
                        static_cast<const u8*>(mesh)
                        + static_cast<i32>(childRec) + 180);
                    if (UtilStrCmp(boneName, childName) == 0) {
                        const i32 n = v4++;
                        wr_u8(mesh, slot + 112 + n, static_cast<u8>(v15));
                    }
                }
                ++v15;
            } while (v4 < 4);
        }
    }
    return 0;
}

// gilde.exe 0x5b2ef8 — VIBE_Mesh_FreeAttachedBuffers
char FreeAttachedBuffers(void* mesh, const MeshAnimHooks& hooks) {
    char ret = static_cast<char>(reinterpret_cast<uintptr_t>(mesh)); // LOBYTE(a1)
    if (!mesh)
        return ret;
    const u32 rec = rd_u32(mesh, 492);
    if (!rec)
        return ret;
    char processed = 0;                              // v5
    for (i32 off = 0; off != 1536; off += 384) {     // 4 sub-records stride 384
        const i32 sr = static_cast<i32>(rec) + off;  // a1 = v2 + *(v1+492)
        if (rd_i32(mesh, sr + 252) > 0) {            // *(a1+252) > 0
            processed = 1;
            const u8 b620 = rd_u8(mesh, sr + 620);
            const u8 b622 = rd_u8(mesh, sr + 622);
            if (b620 != 0xFF || (b622 & 1) != 0) {
                if (hooks.changeTransparency)
                    hooks.changeTransparency(
                        mesh, static_cast<u32>(rec) + 244 + off, 255, mesh);
            }
            wr_u32(mesh, sr + 256, 0);               // *(...+256) = 0
            wr_u32(mesh, sr + 252, 0);               // *(...+252) = 0
            const u32 bufA = rd_u32(mesh, sr + 244);
            if (bufA) {
                if (hooks.freeDebug) hooks.freeDebug(bufA, hooks.ctx);
                wr_u32(mesh, sr + 244, 0);
            }
            const u32 bufB = rd_u32(mesh, sr + 248);
            if (bufB) {
                if (hooks.freeDebug) hooks.freeDebug(bufB, hooks.ctx);
                wr_u32(mesh, sr + 248, 0);
            }
        }
    }
    wr_u32(mesh, 460, 0);                            // *(v1+460) = 0
    if (processed) {
        if (hooks.sceneGraphWalkAndInvoke)
            ret = hooks.sceneGraphWalkAndInvoke(
                0 /*off_649D64 root*/, 0, 134,
                static_cast<u32>(reinterpret_cast<uintptr_t>(mesh)), hooks.ctx);
        else
            ret = 0;  // inert default
    }
    return ret;
}

// gilde.exe 0x43fc38 — VIBE_Anim_FreeObjAnimDataAndReset
i32 FreeObjAnimDataAndReset(i32 a1, i32 a2,
                            FreeAnimResetState& st,
                            const FreeAnimResetHooks& hooks) {
    if (hooks.freeObjAnimData)
        hooks.freeObjAnimData(hooks.g_13FCD1C, a1, a2, hooks.ctx);
    st.g_62D4E8 = 0;     // dword_62D4E8 = 0 (uninit edx, only well-defined value)
    st.g_62D4E4 = 0;     // dword_62D4E4 = 0
    return 0;
}

// gilde.exe 0x5d3f10 — VIBE_Util_StrCmp
i32 UtilStrCmp(const char* a, const char* b) {
    if (a == b)
        return 0;
    while (true) {
        const unsigned char ca = static_cast<unsigned char>(*a);
        const unsigned char cb = static_cast<unsigned char>(*b);
        if (ca != cb) {
            // result = -(ca < cb); LOBYTE |= 1  ->  +1 if a>b, -1 if a<b
            return (ca < cb) ? -1 : 1;
        }
        if (ca == 0)
            return 0;
        ++a;
        ++b;
    }
}

// gilde.exe 0x41dc74 — VIBE_AnimationFlags_Compute
AnimFlagsResult AnimationFlagsCompute(i32 handle, const void* recTable,
                                      const MeshAnimHooks& hooks) {
    AnimFlagsResult r;
    if (handle == -1) { r.ret = 0; return r; }       // a1 == -1
    const i32 recOff = 740 * handle;                 // 740*a1 + table
    const u8 type = rd_u8(recTable, recOff + 24);    // *(v2+24)
    if (type != 1 && type != 8 && type != 5) { r.ret = 0; return r; }
    const u32 stateHandle = rd_u32(recTable, recOff + 8); // *(v2+8)
    void* st = hooks.stateUpdate
                   ? hooks.stateUpdate(stateHandle, hooks.ctx)
                   : nullptr;
    if (!st) { r.ret = 0; return r; }                // result == 0
    if (type == 8 || type == 5) {
        const i32 sel = rd_i32(recTable, recOff + 116); // *(v5+116)
        // v7 = st + *(st + 4*sel + 69); the binary guards `if (v7)` — the SUM,
        // which is nonzero whenever st != 0 (already established above), so the
        // read happens even for a zero table offset (then it reads st+6/st+10).
        const u32 kfOff = rd_u32(st, 4 * sel + 69);
        {
            void* kf = static_cast<u8*>(st) + kfOff;
            r.outW = rd_u16(kf, 6);                   // *(v7+6)
            r.outH = rd_u16(kf, 10);                  // *(v7+10)
        }
        r.ret = 1;
    } else { // type == 1
        r.outW = rd_u16(st, 44);                      // *(result+44)
        r.outH = rd_u16(st, 46);                      // *(result+46)
        r.ret = 1;
    }
    return r;
}

// gilde.exe 0x5d367c — VIBE_Mesh_LoadObjectAnimation (post-load transform only)
void LoadObjectAnimation_ApplyTransform(void* oamRec, const void* objBlock,
                                        u8 loopFlag,
                                        const ObjAnimHooks& hooks) {
    using namespace oam_off;
    const i32 frameCount = rd_i32(oamRec, FRAME_COUNT);   // *(u32*)v5
    const u32 arr = rd_u32(oamRec, ARR_PTR);              // v5[14]
    // arr is modelled as a byte offset into oamRec (the harness lays the
    // keyframe array out within the same buffer), matching how the original
    // computes absolute addresses arr + 88*i.
    auto kf = [&](i32 i) -> i32 { return static_cast<i32>(arr) + KF_STRIDE * i; };

    // Fix-up last keyframe count from the previous one.
    // arr[frameCount-1][0] = arr[frameCount-2][0]
    wr_u32(oamRec, kf(frameCount - 1),
           rd_u32(oamRec, kf(frameCount - 2)));

    // Copy 6 transform dwords from the owning object.
    wr_u32(oamRec, XFORM0, rd_u32(objBlock, OBJ_X0));
    wr_u32(oamRec, XFORM1, rd_u32(objBlock, OBJ_X1));
    wr_u32(oamRec, XFORM2, rd_u32(objBlock, OBJ_X2));
    wr_u32(oamRec, XFORM3, rd_u32(objBlock, OBJ_X3));
    wr_u32(oamRec, XFORM4, rd_u32(objBlock, OBJ_X4));
    wr_u32(oamRec, XFORM5, rd_u32(objBlock, OBJ_X5));

    // Flag bits: v13 = (*(v11+46) & 0xFD); *(v11+45) &= ~0x20;
    //            *(v11+46) = v13; *(v11+46) = (2*(loopFlag&1)) | v13;
    const u8 v12 = loopFlag & 1;
    u8 f46 = rd_u8(oamRec, FLAG46);
    const u8 v13 = f46 & 0xFD;
    u8 f45 = rd_u8(oamRec, FLAG45);
    f45 &= static_cast<u8>(~0x20u);
    wr_u8(oamRec, FLAG45, f45);
    wr_u8(oamRec, FLAG46, static_cast<u8>((2 * v12) | v13));

    // Multiply each keyframe's count field by 3.
    if (frameCount > 0) {
        for (i32 i = 0; i < frameCount; ++i) {
            const i32 o = kf(i);
            wr_u32(oamRec, o, rd_u32(oamRec, o) * 3u);
        }
    }

    // Loop/range setup.
    if ((rd_u8(oamRec, FLAG45) & 2) != 0) {
        const i32 last = frameCount - 1;
        wr_i32(oamRec, IDX1, last);                       // v5[1] = frameCount-1
        const i32 v19 = rd_i32(oamRec, kf(last)) - 1;     // arr[last][0] - 1
        wr_i32(oamRec, IDX3, v19);                        // v5[3]
        const u8 b1 = static_cast<u8>(rd_u32(oamRec, 44) >> 8); // BYTE1(v5[11])
        const i32 adv = hooks.advanceFrameIndex
                            ? hooks.advanceFrameIndex(b1, last, frameCount - 1,
                                                      0, frameCount, hooks.ctx)
                            : last; // benign passthrough default
        wr_i32(oamRec, IDX2, adv);                        // v5[2]
    } else {
        wr_i32(oamRec, IDX3, 0);   // v5[3] = 0
        wr_i32(oamRec, IDX2, 1);   // v5[2] = 1
        wr_i32(oamRec, IDX1, 0);   // v5[1] = 0
    }
}

// gilde.exe 0x41f5e0 — VIBE_Shape_LoadAndRegister (registration logic only)
i32 Shape_RegisterLoaded(void* table, i32 startIndex,
                         const void* blob, u32 blobSize,
                         const char* nameSrc,
                         const ShapeRegHooks& hooks) {
    using namespace shape_off;
    auto slot = [&](i32 idx) { return STRIDE * idx; };

    const i32 base = slot(startIndex);            // 84*startIndex
    // Classify the blob and store the type.
    const u8 type = hooks.classifyType ? hooks.classifyType(blob, hooks.ctx) : 1;
    wr_u32(table, base + TYPE, type);             // *(...+60) = type

    if (type == 1 || type == 5 || type == 4 || type == 8) {
        const void* bounds = hooks.coordTransform
                                 ? hooks.coordTransform(blob, 0, hooks.ctx)
                                 : nullptr;
        if (bounds) {
            wr_u16(table, base + BOUND_W, rd_u16(bounds, 6));   // +80
            wr_u16(table, base + BOUND_H, rd_u16(bounds, 10));  // +82
        }
        wr_u32(table, base + PARENT, static_cast<u32>(startIndex)); // +76
    }
    if (type == 17) {
        wr_u16(table, base + BOUND_W, rd_u16(blob, 12)); // *(blob+12)
        wr_u16(table, base + BOUND_H, rd_u16(blob, 14)); // *(blob+14)
    }

    wr_u32(table, base + SIZE, blobSize);         // *(...+56) = size

    // Copy the stride-2 (wide) name into table[startIndex]+0.
    if (nameSrc) {
        i32 dst = base + NAME;
        const char* a2 = nameSrc;
        while (true) {
            const char c0 = a2[0];
            wr_u8(table, dst, static_cast<u8>(c0));
            if (!c0) break;
            const char c1 = a2[1];
            a2 += 2;
            wr_u8(table, dst + 1, static_cast<u8>(c1));
            dst += 2;
            if (!c1) break;
        }
    }

    // Flags: t = (table[+68] | 1) & 0xFD; store.
    u8 f = rd_u8(table, base + FLAGS);
    f = static_cast<u8>((f | 1) & 0xFD);
    wr_u8(table, base + FLAGS, f);

    // gilde.exe 0x41f5e0 tail: dword_62D208 = v38 — v38 stays at startIndex for
    // a single-frame shape and advances by (frames - 1) for multi-frame; the
    // stored global is NOT startIndex + frames. Return exactly that value.
    i32 count = startIndex;

    // Multi-frame shapes register extra sub-frame slots.
    const i32 frameType = rd_i32(table, base + TYPE);
    if (frameType == 5 || frameType == 8) {
        const i32 frames = rd_u16(blob, 42); // *(u16*)(blob+42)
        i32 v20 = base;                       // 84*startIndex
        i32 v19 = 0;
        i32 idx = startIndex;
        const char* fillName = "..."; // asc_611284
        while (v19 < frames - 1) {
            v20 += STRIDE;
            ++idx;
            // copy "..." (stride-2) into this slot.
            {
                i32 dst = v20;
                const char* a2 = fillName;
                while (true) {
                    const char c0 = a2[0];
                    wr_u8(table, dst, static_cast<u8>(c0));
                    if (!c0) break;
                    const char c1 = a2[1];
                    a2 += 2;
                    wr_u8(table, dst + 1, static_cast<u8>(c1));
                    dst += 2;
                    if (!c1) break;
                }
            }
            wr_u32(table, v20 + 48, 0);       // *(...+48) = 0
            const void* bounds = hooks.coordTransform
                                     ? hooks.coordTransform(
                                           blob, static_cast<u16>(v19 + 1),
                                           hooks.ctx)
                                     : nullptr;
            if (bounds) {
                wr_u16(table, v20 + BOUND_W, rd_u16(bounds, 6));
                wr_u16(table, v20 + BOUND_H, rd_u16(bounds, 10));
            }
            wr_u32(table, v20 + TYPE, rd_u32(table, base + TYPE)); // inherit type
            ++v19;
            wr_u32(table, v20 + PARENT, static_cast<u32>(startIndex));
        }
        count = idx;   // v38 after the loop = startIndex + frames - 1
    }
    return count; // dword_62D208 = v38
}

} // namespace guild::render::anim_recon4
