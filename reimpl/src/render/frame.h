#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::render — the scene-render frame walk orchestration (d3_engine.c).
// Faithful 1:1 reconstruction of the per-frame top-level flow:
//
//   0x5B6074  VIBE_Render_RenderMainViewFrame   (frame entry: gate + clear + walk)
//   0x5B3DE8  VIBE_Render_RenderUniverseFrame   (Begin -> DrawUniverseAndStats)
//   0x5B3900  VIBE_Render_BeginUniverseFrame    (clear, fog, terrain, scene walk
//                                                that projects/culls/appends to the
//                                                draw list, particles, sky flares,
//                                                shadows, mirrors)
//   0x5B3BBC  VIBE_Render_DrawUniverseAndStats  (animation update + fps stats)
//
// The originals are a thicket of file-scope globals (the reentrancy guard
// dword_64A050, the engine-on gate byte_649D71, the software/viewport-clear
// selector byte_649D70, the active world ptr dword_13FCD1C, frame counters, fps
// accumulators). To keep this re-entrant and testable we gather the orchestration
// state into FrameState, and route every subsystem the walk drives (clear,
// terrain, the scene-graph project/cull/append walk, particles, sky flares,
// shadows, mirrors, animation pose update, fps timing) through a FrameHooks
// vtable. The control flow — the reentrancy guard, the gate flags, the order of
// the subsystem calls, the draw-list snapshotting and the fps-window maths — is
// reproduced verbatim; only the leaf subsystem dispatch is indirected (the same
// decoupling the render/scenegraph + render/scene walks use). The deeply
// DDraw/present-coupled clear/lock/present steps are reused via the hooks.
// =============================================================================
namespace guild::render {

// Orchestration state — the per-frame engine globals the walk reads/writes.
struct FrameState {
    // -- gate / reentrancy ---------------------------------------------------
    bool engineOn = true;       // byte_649D71  master engine-enabled gate
    bool useViewportClear = false; // byte_649D70 (1 => ClearViewport, else ClearRect)
    bool clearSuppressed = false;  // byte_62D596 (skip the ClearRect in main view)
    bool hasWorld = true;       // dword_13FCD1C != 0 (an active camera/world)
    bool hasTerrain = false;    // dword_64A028 != 0 (a terrain root to render)
    i32  reentrancy = 0;        // dword_64A050  re-entrancy guard (clamped >= 0)

    // -- per-frame draw-list / depth snapshots (written by BeginUniverseFrame) -
    float runningNear = 0.0f;   // flt_13FD168[0]  running near depth (reset 1e10)
    float runningFar  = 0.0f;   // flt_13FCF3C     running far  depth (reset 0)
    i32   appendedPolys = 0;    // dword_13FC770 snapshot -> dword_649DA4

    // -- fps stats (DrawUniverseAndStats) ------------------------------------
    i32  fpsFrameAccA = 0;      // dword_649DE0  frame accumulator A
    i32  fpsLastTimeA = 0;      // dword_649DDC  last sample time A
    i32  fpsValueA = 0;         // dword_649DC0  computed fps A (1000*frames/(dt*delay))
    i32  fpsFrameAccB = 0;      // dword_649DE8  frame accumulator B
    i32  fpsLastTimeB = 0;      // dword_649DE4  last sample time B
    i32  fpsValueB = 0;         // dword_649DC4  computed fps B
    i32  frameCounter = 0;      // dword_649D58  bumped when a4 set
    i32  uDelay = 1;            // uDelay        ms/tick divisor for the fps maths
};

// Subsystem callbacks the frame walk drives. A null pointer means "subsystem not
// present this frame" (the original's `if (global) call(...)` guards). All take
// the frame-flag byte the original threaded as a2 (the dl argument).
struct FrameHooks {
    void* world = nullptr;   // dword_13FCD1C (opaque; passed through)
    void* terrain = nullptr; // dword_64A028
    void  (*clearViewport)() = nullptr;          // VIBE_Render_ClearViewport
    void  (*clearRect)() = nullptr;              // VIBE_Render_ClearRect
    void  (*renderTerrain)(void* terrain, char a2) = nullptr; // VIBE_Floor_RenderTerrain
    void  (*resetLights)() = nullptr;            // VIBE_Shadow_ResetLightList
    // The scene-graph project/cull/append walk (VIBE_SceneGraph_WalkAndInvoke
    // with the per-object project callback). Returns the number of draw-list
    // entries appended this walk (snapshotted into appendedPolys).
    i32   (*sceneWalk)(char a2) = nullptr;
    void  (*renderParticles)(char a2) = nullptr; // VIBE_Particle_RenderSystem loop
    void  (*updateSkyFlares)() = nullptr;        // VIBE_Render_UpdateSkyFlares
    void  (*buildMirrors)(char a2) = nullptr;    // VIBE_Mirror_BuildMirroredGeometry
    void  (*updateAnim)(char a2) = nullptr;      // VIBE_Anim_UpdateSkeletonPose walk
    void  (*flushDrawList)() = nullptr;          // VIBE_Render_RasterizeMeshList
    i32   (*timeNow)() = nullptr;                // dword_62EB38 frame timestamp
};

// gilde.exe 0x5B6074 — VIBE_Render_RenderMainViewFrame
//   if (engineOn && reentrancy <= 0):
//     clear (ClearViewport if useViewportClear, else ClearRect unless suppressed)
//     RenderUniverseFrame(64, /*a2*/1, /*a3*/1)
// `frameFlags` is the a2 byte threaded down (1 in the main-view call).
void RenderMainViewFrame(FrameState& fs, const FrameHooks& hooks);

// gilde.exe 0x5B3DE8 — VIBE_Render_RenderUniverseFrame
//   BeginUniverseFrame(a1, a2); return DrawUniverseAndStats(a1, a2, 1, a3);
i32 RenderUniverseFrame(FrameState& fs, const FrameHooks& hooks,
                        char a2, char a3);

// gilde.exe 0x5B3900 — VIBE_Render_BeginUniverseFrame
//   Re-entrancy-guarded; clears, resets the running depth bounds + per-frame
//   poly counters, renders terrain, resets the light list, runs the scene-graph
//   project/cull/append walk (sceneWalk -> appendedPolys), renders particles and
//   sky flares, builds mirror geometry, then snapshots the draw-list state.
void BeginUniverseFrame(FrameState& fs, const FrameHooks& hooks, char a2);

// gilde.exe 0x5B3BBC — VIBE_Render_DrawUniverseAndStats
//   Re-entrancy-guarded; when a2 set: scroll UVs, run the animation pose walk,
//   advance the two fps-sampling windows; when a4 set bump the frame counter;
//   resets per-frame poly counters. Returns the engine state ptr (here `result`,
//   modeled as the appended-poly snapshot for testability).
i32 DrawUniverseAndStats(FrameState& fs, const FrameHooks& hooks,
                         char a2, char a3, char a4);

} // namespace guild::render
