#pragma once
// guild::app — REAL-asset skeletal-ANIMATION driver (INTEGRATION glue, not a
// translation; no VIBE_* provenance).
//
// Wires the already-reconstructed animation pieces into one end-to-end flow over
// the REAL shipped bytes:
//
//   MountRealGameAssets(fs, gameDir)                 [app/real_boot]        ->
//   the mounted Resources/animations.BIN member index [io/ArchiveMount]     ->
//   for every `.baf` member: OpenMember -> raw bytes  [io/ArchiveMount]     ->
//   render::LoadBinaryAnimation(bytes)                [render/anim_load]    ->
//   a posed-walk over the loaded frames:                                    .
//       render::AdvanceFrameIndex   (frame-advance state)  [render/skeleton]
//       render::InterpolateBoneFrame(keyframe accumulate)  [render/skeleton]
//       render::SampleBoneTranslation(playback select+blend)[render/animation_playback]
//       render::AccumulateBoneMatrices / ComputeBoneWorldMatrix (bone matrix)
//
// Resources/animations.BIN (~65 MB PKZIP, 2007 members) carries 953 `.baf` binary
// animations (the "BGF\0"-magic token stream LoadBinaryAnimation reads) plus a
// sibling `.ini` per clip ([4HEAD Studios Animation-Settings] NumKeys/Keys/LoopIn/
// LoopOut). This driver loads the REAL `.baf` blobs through the reconstructed loader,
// then exercises the keyframe-interpolation + bone-matrix-accumulation math over a
// few frames of each, asserting the loaded counts and a sane interpolated pose.
//
// The only OS boundary is shim::IFileSystem. The keyframe/transform leaves are all
// reconstructed (render/skeleton, render/animation_playback, render/anim_load); the
// scene-graph push leaves SampleBoneTranslation's siblings reach
// (Object_SetPosition / SetWorldTranslation / PropagateDirtyFlag) are NOT touched by
// this driver's load+interp+matrix path, and remain the inert library stubs in
// render/animation_playback_unowned_stubs.cpp (no symbol is defined in a test).
#include "guild/common/types.h"
#include "render/anim_load.h"   // render::Animation, LoadBinaryAnimation
#include "shim/IFileSystem.h"

#include <cstddef>
#include <string>
#include <vector>

namespace guild::app {

// Per-clip summary of one loaded `.baf` animation + the posed-walk results.
struct DrivenAnim {
    std::string name;            // member path, e.g. "BAUM/faellen_BUCHE_01.baf"
    bool        loaded = false;  // LoadBinaryAnimation returned true (.valid)
    int         frameCount = 0;  // header.frameCount (== NumKeys in the sibling .ini)
    int         startFrame = 0;  // header.startFrame (clamped LoopIn)
    int         endFrame = 0;    // header.endFrame   (clamped LoopOut)
    int         vertexCount = 0; // morph points per frame (0 for pure skeletal clips)

    int         framesWalked = 0;     // frames the AdvanceFrameIndex loop visited
    bool        poseSane = false;     // every sampled pose component was finite
    float       maxAbsPose = 0.0f;    // largest |component| over the walk (sanity bound)
    bool        matrixSane = false;   // ComputeBoneWorldMatrix produced finite floats
    std::size_t bytes = 0;            // inflated `.baf` size
};

// Aggregate result of driving the whole animations.BIN through the anim loader +
// interp/matrix math.
struct RealAnimResult {
    bool        assetsPresent = false;  // game dir / animations.BIN found
    bool        mounted       = false;  // animations.BIN mounted through the VFS
    std::size_t archiveMembers = 0;     // total members indexed in animations.BIN

    int  bafMembers   = 0;  // `.baf` members seen
    int  loadedAnims  = 0;  // members LoadBinaryAnimation accepted (.valid)
    int  failedAnims  = 0;  // members that failed to extract or load

    long totalFrames    = 0;  // sum of header.frameCount over all loaded clips
    long totalFramesWalked = 0; // sum of frames the advance/sample loop visited
    int  minFrames      = 0;  // smallest clip frame count (loaded)
    int  maxFrames      = 0;  // largest clip frame count (loaded)
    long posesSampled   = 0;  // SampleBoneTranslation / InterpolateBoneFrame calls
    int  poseSaneClips  = 0;  // clips whose every sampled pose was finite
    int  matrixSaneClips = 0; // clips whose bone matrix was finite

    int         maxFramesInOneClip = 0;  // richest single clip (frame count)
    std::string richestClipName;         // name of the max-frames clip

    std::vector<DrivenAnim> anims;        // per-clip detail (archive order)
};

// Drive a single already-extracted `.baf` byte buffer: load it with the
// reconstructed LoadBinaryAnimation, then walk a few frames running the real
// AdvanceFrameIndex + keyframe interpolation (InterpolateBoneFrame /
// SampleBoneTranslation) + bone-matrix accumulation (AccumulateBoneMatrices /
// ComputeBoneWorldMatrix) over an identity bone rig, summarizing what loaded and
// whether the posed result is finite/sane. `name` is recorded into the summary.
// Used by the unit + integration tests directly; the e2e path calls it per member.
// `loadFlag` is the LoadBinaryAnimation `a3` (delta-encode gate); default 0.
DrivenAnim DriveAnimBuffer(const guild::u8* data, std::size_t len, const char* name,
                           guild::u8 loadFlag = 0);

// Full real-asset run: mount `<gameDir>/Resources/animations.BIN` through the VFS
// bound to `fs`, then load EVERY `.baf` member through LoadBinaryAnimation and run
// the interp/matrix walk, tallying counts. `fs` MUST be rooted at the real game dir.
// If animations.BIN is absent, returns with assetsPresent=false (caller skips).
//
// Binds the process-global VFS (io::VfsInit) for the run and tears it down
// (io::VfsShutdown) before returning, so it is self-contained. `maxClips` caps the
// number of `.baf` clips driven (0 == all 953); the e2e drives all, the integration
// test a small slice.
RealAnimResult DriveRealAnimations(guild::shim::IFileSystem* fs,
                                   const std::string& gameDir, int maxClips = 0);

} // namespace guild::app
