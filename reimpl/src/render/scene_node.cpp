#include "render/scene_node.h"

namespace guild::render {

// gilde.exe 0x5add1c — VIBE_Render_ProcessSceneNode draw-list append loops.
i32 ProcessSceneNodeAppend(u8 cullByte, const Polygon* polys, i32 polyCount,
                           const u32* texSortId, const NodeAppendContext& ctx,
                           DrawList* out, u32* texFrameStamp) {
    // Fully-culled node: the original returns 1 without appending anything when
    // (v29 & 0x40) != 0 (the function falls through to the shadow/light tail, which
    // is the deferred part; the draw-list payload is skipped).
    if (cullByte & 0x40) return 0;

    // Clamp the poly count to the remaining PolyList capacity:
    //   v13 = min(dword_13ECE80 - dword_13FC770, *(v3+12))
    i32 remain = out->capacity - out->count;
    i32 limit = (remain <= polyCount) ? remain : polyCount;
    i32 appended = 0;

    if (ctx.mode == NodeAppendMode::Software) {
        // Software path (byte_649D70 != 0): texture-id sort key.
        for (i32 i = 0; i < limit; ++i) {
            const Polygon& p = polys[i];
            // *(char*)(v14+36) < 0  => front-facing / visible.
            if ((i8)p.flags36 >= 0) continue;
            DrawListEntry& e = out->entries[out->count];
            e.poly = const_cast<Polygon*>(&p);
            u32 tex = texSortId[i];
            if (tex) {
                // *v18 = v27 + ((v19 - dword_1406A84) >> 7); the caller supplied
                // texSortId == ((tex - base) >> 7); add the base key (v27).
                e.sortKey = ctx.baseKey + tex;
                // *(v19 + 84) = dword_649D58  (frame stamp into the texture record).
                if (texFrameStamp) texFrameStamp[i] = ctx.frameStamp;
            } else {
                e.sortKey = 0;   // *v18 = 0
            }
            ++out->count;
            ++appended;
        }
    } else {
        // Hardware path (byte_649D70 == 0): depth sort key from max vertex z.
        for (i32 i = 0; i < limit; ++i) {
            const Polygon& p = polys[i];
            if ((i8)p.flags36 >= 0) continue;   // skip non-visible (original `while >=0`)
            DrawListEntry& e = out->entries[out->count];
            e.poly = const_cast<Polygon*>(&p);
            // v28 = max(v0->z, v1->z, v2->z)  (reads *(vtx+8)).
            float maxZ = p.v0->z;
            if (p.v1->z > maxZ) maxZ = p.v1->z;
            if (p.v2->z > maxZ) maxZ = p.v2->z;
            // *dword_13FC570 = (int)(flt_13FC774 * flt_628080 * flt_628084 * v28).
            e.sortKey = (u32)(i32)(ctx.depthScale * maxZ);
            if (texSortId && texFrameStamp && texSortId[i]) {
                // *(*(v21+20)+84) = dword_649D58  (stamp the texture if present).
                texFrameStamp[i] = ctx.frameStamp;
            }
            ++out->count;
            ++appended;
        }
    }
    return appended;
}

} // namespace guild::render
