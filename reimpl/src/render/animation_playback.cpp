#include "render/animation_playback.h"

#include "sim/actionqueue.h"  // extern u32 g_gameTick (dword_62EB38), owned in sim

#include <cstdint>
#include <cstring>

namespace guild::render {
namespace {

// Raw byte-offset accessors mirroring the original *(type*)(base + off) loads.
// Used ONLY for the bone/frame float records, which have no pointer/flag aliasing.
inline const u8* CB(const void* p) { return reinterpret_cast<const u8*>(p); }
inline u8*       B (void* p)       { return reinterpret_cast<u8*>(p); }
inline float F32 (const void* p, int off) { return *reinterpret_cast<const float*>(CB(p) + off); }

// Keyframe float field at byte `kfOff` of frame `idx` within the AnimFrame array.
inline float KF(const AnimFrame* base, int idx, int kfOff) {
    return *reinterpret_cast<const float*>(reinterpret_cast<const u8*>(base) + 192 * idx + kfOff);
}
// Segment duration *(frame+4) of keyframe `idx`.
inline i32 SegDur(const AnimFrame* base, int idx) {
    return *reinterpret_cast<const i32*>(reinterpret_cast<const u8*>(base) + 192 * idx + 4);
}

} // namespace

// gilde.exe 0x5cc920 — VIBE_Anim_SampleBoneTranslation
const float* SampleBoneTranslation(const float* bone, const AnimTrack& track,
                                   i32 phaseEnd, i32 toFrame, float* out) {
    const AnimHeaderView* hdr = track.hdr;   // v5 = a2[26]
    out[0] = 0.0f; out[1] = 0.0f; out[2] = 0.0f;
    const AnimFrame* frames = hdr->frames;   // *(v5+348)

    if (hdr->deltaMode) {                     // *(v5+361)
        i32 segFrom  = track.segFrom;         // v51 = a2[3]
        i32 segPhase = track.segPhase;        // v49 = a2[4]

        if (toFrame == segFrom && phaseEnd == segPhase) {
            // static pose: base + local translation, no accumulation.
            out[0] = bone[19] + bone[30];
            out[1] = bone[20] + bone[31];
            out[2] = bone[21] + bone[32];
            return bone;
        }

        float ax = 0.0f, ay = 0.0f, az = 0.0f;
        // Phase 1: leading partial segment (prorated by 1 - segPhase/dur(segFrom)).
        if (segFrom < toFrame) {
            double lead = 1.0 - (double)segPhase / (double)SegDur(frames, segFrom);
            ax = (float)((KF(frames, segFrom + 1, 32) - KF(frames, segFrom, 32)) * lead);
            ay = (float)((KF(frames, segFrom + 1, 36) - KF(frames, segFrom, 36)) * lead);
            az = (float)((KF(frames, segFrom + 1, 40) - KF(frames, segFrom, 40)) * lead);
        }
        // Phase 2: whole inner segments (segFrom+1 .. toFrame-1).
        for (int k = segFrom + 1; k < toFrame; ++k) {
            ax += KF(frames, k + 1, 32) - KF(frames, k, 32);
            ay += KF(frames, k + 1, 36) - KF(frames, k, 36);
            az += KF(frames, k + 1, 40) - KF(frames, k, 40);
        }
        // Phase 3: trailing partial segment at toFrame.
        double trailNum = (segFrom == toFrame) ? (double)(phaseEnd - segPhase)
                                               : (double)phaseEnd;
        double trail = trailNum / (double)SegDur(frames, toFrame);
        ax += (float)((KF(frames, toFrame + 1, 32) - KF(frames, toFrame, 32)) * trail);
        ay += (float)((KF(frames, toFrame + 1, 36) - KF(frames, toFrame, 36)) * trail);
        az += (float)((KF(frames, toFrame + 1, 40) - KF(frames, toFrame, 40)) * trail);

        // Rotate by the bone's local 3x3 (idx 99..109), add base + local translation.
        float rx = ax * bone[99]  + ay * bone[103] + az * bone[107];
        float ry = ax * bone[100] + ay * bone[104] + az * bone[108];
        float rz = ax * bone[101] + ay * bone[105] + az * bone[109];
        out[0] = bone[19] + rx;
        out[1] = bone[20] + ry;
        out[2] = bone[21] + rz;
        out[0] = bone[30] + out[0];
        out[1] = bone[31] + out[1];
        out[2] = bone[32] + out[2];
        return bone;
    }

    // delta mode CLEAR: plain two-keyframe lerp between a2[0] and a2[1] by a2[2].
    i32 f0 = track.fromFrame;   // *a2
    i32 f1 = track.toFrame;     // a2[1]
    i32 ph = track.phaseAccum;  // a2[2]
    float b0x = KF(frames, f0, 32), b0y = KF(frames, f0, 36), b0z = KF(frames, f0, 40);
    float dx = KF(frames, f1, 32) - b0x;
    float dy = KF(frames, f1, 36) - b0y;
    float dz = KF(frames, f1, 40) - b0z;
    double t = (double)ph / (double)SegDur(frames, f0);
    float lx = (float)(t * dx + b0x);
    float ly = (float)(t * dy + b0y);
    float lz = (float)(t * dz + b0z);
    float rx = lx * bone[99]  + ly * bone[103] + lz * bone[107];
    float ry = lx * bone[100] + ly * bone[104] + lz * bone[108];
    float rz = lx * bone[101] + ly * bone[105] + lz * bone[109];
    out[0] = bone[19] + rx;
    out[1] = bone[20] + ry;
    out[2] = bone[21] + rz;
    out[0] = bone[30] + out[0];
    out[1] = bone[31] + out[1];
    out[2] = bone[32] + out[2];
    return bone;
}

// gilde.exe 0x5cba40 — VIBE_Anim_ComputeBoneDelta
u8 ComputeBoneDelta(void* obj, const AnimTrack& track, i32 toFrame, i32 fromFrame, u8 mode) {
    u8 result = mode & 1;
    const AnimFrame* frames = track.hdr->frames;   // *(*(a2+104)+348)
    if ((mode & 1) == 1) {
        float d0 = KF(frames, toFrame, 32) - KF(frames, fromFrame, 32);  // +32
        float d1 = KF(frames, toFrame, 36) - KF(frames, fromFrame, 36);  // +36
        float d2 = KF(frames, toFrame, 40) - KF(frames, fromFrame, 40);  // +40
        // Rotate by obj's 3x3 columns at bytes +396.. (== bone idx 99..109 view).
        float r0 = d0 * F32(obj, 396) + d1 * F32(obj, 412) + d2 * F32(obj, 428);
        float r1 = d0 * F32(obj, 400) + d1 * F32(obj, 416) + d2 * F32(obj, 432);
        float r2 = d0 * F32(obj, 404) + d1 * F32(obj, 420) + d2 * F32(obj, 436);
        float pos[3];
        pos[0] = r0 + F32(obj, 76);
        pos[1] = r1 + F32(obj, 80);
        pos[2] = r2 + F32(obj, 84);
        result = (u8)ObjectSetPosition(obj, pos);
        if ((mode & 2) == 0)
            return result;
    } else if ((mode & 2) == 0) {
        return result;
    }
    // bit1 path (fall-through when bit0 set, or directly when only bit1 set).
    float w[3];
    w[0] = (KF(frames, toFrame, 44) - KF(frames, fromFrame, 44)) + F32(obj, 132);
    w[1] = (KF(frames, toFrame, 48) - KF(frames, fromFrame, 48)) + F32(obj, 136);
    w[2] = (KF(frames, toFrame, 52) - KF(frames, fromFrame, 52)) + F32(obj, 140);
    return (u8)ObjectSetWorldTranslation(obj, w);
}

// gilde.exe 0x5d0edc — VIBE_Anim_UpdateTrackBlendWeight
void UpdateTrackBlendWeight(AnimTrack& track, i32 wFromBits, i32 wToBits,
                            u32 startTime, u32 endTime) {
    u32 now = sim::g_gameTick;                  // v5 = dword_62EB38
    if (track.hdr && startTime <= endTime) {     // result && *(result+26) && a4<=a5
        track.blendToTime = (i32)endTime;        // result[14] = a5
        track.blendFromTime = (i32)startTime;    // result[13] = a4
        // result[17]/result[18] are the i32 args reinterpreted as float.
        std::memcpy(&track.blendFrom, &wFromBits, sizeof(float)); // result[17] = a2
        std::memcpy(&track.blendTo,   &wToBits,   sizeof(float)); // result[18] = a3
        if (now >= endTime) {
            track.blendCur = track.blendTo;      // result[16] = result[18]
        } else {
            float t;
            if (now < startTime)
                t = 0.0f;
            else
                t = (float)((double)(now - startTime) /
                            (double)(endTime - startTime));
            track.blendCur = (track.blendTo - track.blendFrom) * t + track.blendFrom;
        }
    }
}

// gilde.exe 0x5d0e84 — VIBE_Anim_FindHighestPriorityLayer
//   Returns the index of the heaviest active layer (the original returned the
//   layer address obj+28+116*k; the index is the same selection).
i32 FindHighestPriorityLayer(bool layersActive, const AnimLayer* layers, i32 count) {
    i32 best = -1;          // v1 (0/address in the original; -1 == "none" here)
    float bestW = 0.0f;     // v5
    if (layersActive) {
        for (i32 k = count - 1; k >= 0; --k) {   // v2 = 2..0
            if (layers[k].active != 0 && layers[k].weight > (double)bestW) {
                best = k;
                bestW = layers[k].weight;
            }
        }
    }
    return best;
}

// gilde.exe 0x5d0dcc — VIBE_Anim_ClearLoopFlags
void AnimClearLoopFlags(AnimTrack* tracks, i32 count, const char* name) {
    if (!tracks)
        return;
    for (i32 i = 0; i < count; ++i) {
        const char* tn = tracks[i].boneName;     // *(v3+132)
        if (tn && AnimStrCmp(tn, name) == 0) {
            tracks[i].loopFlags &= ~2u;          // *(v3+138) &= ~2
        }
    }
}

// gilde.exe 0x5d0e10 — VIBE_Anim_SetLoopFlags
void AnimSetLoopFlags(AnimTrack* tracks, i32 count, bool loop, const char* name,
                      i32* outStampTime) {
    if (!tracks)
        return;
    if (outStampTime)
        *outStampTime = (i32)sim::g_gameTick;    // *(*(obj+24)+64) = dword_62EB38
    u8 setBit = (u8)(2 * (loop ? 1 : 0));        // v5 = 2 * (a2 & 1)
    for (i32 i = 0; i < count; ++i) {
        const char* tn = tracks[i].boneName;
        if (tn && AnimStrCmp(tn, name) == 0) {
            u8 cleared = (u8)(tracks[i].loopFlags & 0xFD); // clear bit1
            tracks[i].loopFlags = (u8)(setBit | cleared);  // then OR in setBit
        }
    }
}

// gilde.exe 0x5cd184 — VIBE_Anim_FindFirstActiveBone
//   a1 = header (_DWORD*), a1[87] = frames ptr, a1[81] = bone count, names at
//   a1+16 (byte 64), 64-byte stride. (The bone-name table is a flat char buffer,
//   no pointer/flag aliasing, so raw-byte addressing is faithful here.)
i32 FindFirstActiveBone(const void* header, const char* name) {
    if (!header)
        return -1;
    if (*reinterpret_cast<const void* const*>(CB(header) + 87 * 4) == nullptr) // a1[87]
        return -1;
    i32 count = *reinterpret_cast<const i32*>(CB(header) + 81 * 4);            // a1[81]
    if (count <= 0)
        return -1;
    const char* entry = reinterpret_cast<const char*>(CB(header) + 64);        // a1 + 16
    i32 idx = 0;
    while (AnimStrCmp(entry, name) != 0) {
        ++idx;
        entry += 64;
        if (idx >= count)
            return -1;
    }
    return idx;
}

// gilde.exe 0x5d0f7c — VIBE_Anim_SeekToFrame
u8 SeekToFrame(void* obj, AnimTrack& track, i32 frame) {
    const AnimHeaderView* hdr = track.hdr;   // a2[26]
    if (!hdr)
        return 0;

    i32 frameCount = hdr->frameCount;        // *(a2[26]+340)
    if (frame >= frameCount)
        frame = frameCount - 1;
    if (frame < 0)
        frame = hdr->frameCount - 1;         // wrap negative to last frame

    const AnimFrame* frames = hdr->frames;
    if (frame + 1 < hdr->frameCount) {
        track.phaseAccum = 0;                // a2[2] = 0
    } else {
        // a2[2] = *(192*frame + frames + 4) - 1  (last segment duration - 1)
        track.phaseAccum = SegDur(frames, frame) - 1;
    }

    track.fromFrame = frame;                 // *a2 = frame
    // a2[1] = AdvanceFrameIndex(BYTE1(a2[27]), *a2, hdr[85], hdr[84], hdr[82])
    track.toFrame = AdvanceFrameIndex(track.modeFlags, frame,
                                      hdr->lastFrame, hdr->firstFrame,
                                      hdr->advanceCount);
    return (u8)ObjectPropagateDirtyFlag(obj, 1u);
}

} // namespace guild::render
