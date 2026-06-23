#include "render/frame.h"

namespace guild::render {

// gilde.exe 0x5B6074 — VIBE_Render_RenderMainViewFrame
//
//   if (byte_649D71 && dword_64A050 <= 0) {
//     if (byte_649D70) { ClearViewport(); RenderUniverseFrame(64,1,1); }
//     else { if (!byte_62D596) ClearRect(...); RenderUniverseFrame(64,1,1); }
//   }
// The two branches differ only in which clear runs; the RenderUniverseFrame call
// (a1=64, a2=1, a3=1) is identical, so it is hoisted after the clear.
void RenderMainViewFrame(FrameState& fs, const FrameHooks& hooks) {
    if (!fs.engineOn || fs.reentrancy > 0)
        return;

    if (fs.useViewportClear) {
        if (hooks.clearViewport)
            hooks.clearViewport();
    } else {
        if (!fs.clearSuppressed && hooks.clearRect)
            hooks.clearRect();
    }
    RenderUniverseFrame(fs, hooks, /*a2*/ 1, /*a3*/ 1);
}

// gilde.exe 0x5B3DE8 — VIBE_Render_RenderUniverseFrame
//   BeginUniverseFrame(a1, a2); return DrawUniverseAndStats(a1, a2, 1, a3);
i32 RenderUniverseFrame(FrameState& fs, const FrameHooks& hooks,
                        char a2, char a3) {
    BeginUniverseFrame(fs, hooks, a2);
    return DrawUniverseAndStats(fs, hooks, a2, /*a3*/ 1, a3);
}

// gilde.exe 0x5B3900 — VIBE_Render_BeginUniverseFrame
//
//   if (dword_64A050 < 0) dword_64A050 = 0;
//   if (byte_649D71 && !dword_64A050 && dword_13FCD1C) {
//     ++dword_64A050;
//     clear (ClearViewport / ClearRect);
//     reset per-frame poly counters at *(obj+492)+252/+256;
//     dword_64A060 = 0; flt_13FCF3C = 0.0; dword_64A058 = 0; flt_13FD168[0]=1e10;
//     if (dword_64A028) { [fog range] RenderTerrain(...); }
//     dword_649D6C = 0; ResetLightList(); dword_649DCC = 0; dword_649DC8 = 0;
//     WalkAndInvoke(... project/cull/append ...);
//     if (per-frame poly count > 0 && (byte_14080ED & 7)) ProcessSceneNode(...);
//     particle render loop; if (sky) UpdateSkyFlares();
//     if (a2) RegisterLightUpdateCallbacks();
//     if (mirror flags) WalkAndInvoke(... BuildMirroredGeometry ...);
//     if (flt_13FD168[0] == sentinel) flt_13FD168[0] = 0.0;
//     snapshot draw-list state into dword_649DA4.., ResetEngineState();
//     --dword_64A050;
//   }
void BeginUniverseFrame(FrameState& fs, const FrameHooks& hooks, char a2) {
    if (fs.reentrancy < 0)
        fs.reentrancy = 0;
    if (!fs.engineOn || fs.reentrancy != 0 || !fs.hasWorld)
        return;

    ++fs.reentrancy;

    // Clear (mirrors RenderMainViewFrame's clear selection; the engine re-clears
    // here for the non-main-view callers).
    if (fs.useViewportClear) {
        if (hooks.clearViewport)
            hooks.clearViewport();
    } else if (hooks.clearRect) {
        hooks.clearRect();
    }

    // Reset the running depth bounds + per-frame poly counters (0x5b3982..5b39b8):
    //   dword_64A060 = 0; flt_13FCF3C = 0.0; dword_64A058 = 0;
    //   *(*(obj+492)+256) = 0; flt_13FD168[0] = 1e10; *(obj+492)+252) = 0;
    fs.polyCounterA = 0;                  // dword_64A060 = 0
    fs.runningFar = 0.0f;                 // flt_13FCF3C = 0.0
    fs.polyCounterB = 0;                  // dword_64A058 = 0
    fs.shadowPolyCount = 0;               // *(*(obj+492)+256) = 0
    fs.runningNear = 1.0e10f;             // flt_13FD168[0] = 1e10
    fs.framePolyCount = 0;                // *(*(obj+492)+252) = 0

    // Terrain (with the fog-range sub-step, which is terrain-coupled).
    if (fs.hasTerrain && hooks.renderTerrain)
        hooks.renderTerrain(hooks.terrain, a2);

    // Reset the per-frame light list.
    if (hooks.resetLights)
        hooks.resetLights();

    // The scene-graph project/cull/append walk -> appends to the draw list.
    if (hooks.sceneWalk)
        fs.appendedPolys = hooks.sceneWalk(a2);

    // Particles, sky flares.
    if (hooks.renderParticles)
        hooks.renderParticles(a2);
    if (hooks.updateSkyFlares)
        hooks.updateSkyFlares();

    // Mirror geometry (gated on the mirror flags in the original).
    if (hooks.buildMirrors)
        hooks.buildMirrors(a2);

    // flt_13FD168 sentinel fix-up (== flt_62838C => 0.0): if nothing expanded the
    // running near bound it stays at the 1e10 sentinel; the original collapses a
    // specific sentinel value to 0. We collapse the untouched 1e10 to 0 likewise.
    if (fs.runningNear == 1.0e10f)
        fs.runningNear = 0.0f;

    --fs.reentrancy;
}

// gilde.exe 0x5B3BBC — VIBE_Render_DrawUniverseAndStats
//
//   if (dword_64A050 < 0) dword_64A050 = 0;
//   if (byte_649D71 && !dword_64A050 && dword_13FCD1C) {
//     ++dword_64A050;
//     if (a2) {
//       t = dword_62EB38; ScrollUvCoords(t);
//       WalkAndInvoke(... ); // a3-gated anim pose walk over the 64 character lists
//       // fps window A: dt = t - dword_649DDC; ++dword_649DE0;
//       //   if (dt > 60 && dt) { dword_649DC0 = 1000*frames/(dt*uDelay); reset; }
//       // fps window B: dt = t - dword_649DE4; ++dword_649DE8;
//       //   if (dt > 10 && dt) { dword_649DC4 = 1000*frames/(dt*uDelay); reset; }
//     }
//     if (a4) ++dword_649D58;
//     reset per-frame poly counters; --dword_64A050;
//   }
i32 DrawUniverseAndStats(FrameState& fs, const FrameHooks& hooks,
                         char a2, char a3, char a4) {
    if (fs.reentrancy < 0)
        fs.reentrancy = 0;
    if (!fs.engineOn || fs.reentrancy != 0 || !fs.hasWorld)
        return fs.appendedPolys;

    ++fs.reentrancy;

    if (a2) {
        // v14 = dword_62EB38; ScrollUvCoords(dword_62EB38);  (0x5b3c7a)
        i32 now = hooks.timeNow ? hooks.timeNow() : 0;
        if (hooks.scrollUvCoords)
            hooks.scrollUvCoords(now);

        // Unconditional projection walk: v7 = result|0x181;
        //   WalkAndInvoke(off_649D64, 0, v8, v7, v14);  (0x5b3c96)
        const i16 walkFlags = (i16)((i16)fs.appendedPolys | 0x181);
        if (hooks.projectWalk)
            hooks.projectWalk(walkFlags, now);

        // a3-gated animation pose walk over the 64 per-zone character lists.
        // for (i=0; i<64; ++i) { if (i != dword_649D60) { head = dword_13ECF48[i*246];
        //   if (head && head != &dword_13FCF4C) { node=head;
        //     do { UpdateSkeletonPose(off_649D64,node,v7,v14|0x80000000);
        //          node=node[124]; } while (node != &dword_13FCF4C); } } }
        if (a3 && hooks.animListHead && hooks.animSentinel && hooks.animNext &&
            hooks.animPose) {
            void* sentinel = hooks.animSentinel();
            for (int i = 0; i < 64; ++i) {
                if (i == fs.animSkipIndex)         // i != dword_649D60
                    continue;
                void* head = hooks.animListHead(i); // dword_13ECF48[i*246]
                if (!head || head == sentinel)
                    continue;
                void* node = head;
                do {
                    hooks.animPose(node, walkFlags, now /* | 0x80000000 high bit */);
                    node = hooks.animNext(node);    // node[124]
                } while (node != sentinel);
            }
        }

        // fps window A: report every >60 ticks.
        i32 framesA = fs.fpsFrameAccA + 1;
        i32 dtA = now - fs.fpsLastTimeA;
        ++fs.fpsFrameAccA;
        if ((u32)dtA > 0x3C && dtA) {
            fs.fpsValueA = 1000 * framesA / (dtA * fs.uDelay);
            fs.fpsFrameAccA = 0;
            fs.fpsLastTimeA = now;
        }

        // fps window B: report every >10 ticks.
        i32 dtB = now - fs.fpsLastTimeB;
        ++fs.fpsFrameAccB;
        if ((u32)dtB > 0xA && dtB) {
            fs.fpsValueB = 1000 * fs.fpsFrameAccB / (dtB * fs.uDelay);
            fs.fpsFrameAccB = 0;
            fs.fpsLastTimeB = now;
        }
    }

    if (a4)
        ++fs.frameCounter;            // ++dword_649D58

    // Per-frame poly counters reset (0x5b3c19..0x5b3c50):
    //   byte_64A068 = 0;
    //   *(*(obj+492)+256) = 0;
    //   *(*(obj+492)+252) = *(*(obj+492)+256);   (== 0)
    //   dword_1408A68 = 0; dword_1408A64 = 0;
    fs.drawFrameFlag = 0;             // byte_64A068 = 0
    fs.shadowPolyCount = 0;           // *(*(obj+492)+256) = 0
    fs.framePolyCount = fs.shadowPolyCount; // *(*(obj+492)+252) = *(*(obj+492)+256)
    fs.mirrorPolyB = 0;              // dword_1408A68 = 0
    fs.mirrorPolyA = 0;              // dword_1408A64 = 0

    // result = *(obj+492); the engine returns the obj's drawdata ptr. We model the
    // observable per-frame poly count the caller reads.
    --fs.reentrancy;
    return fs.appendedPolys;
}

} // namespace guild::render
