#include "render/skeleton_pose_driver.h"

#include "render/skeleton.h"   // AdvanceFrameIndex (0x5ccf18)

#include <cmath>

namespace guild::render {

// ============================================================================
// gilde.exe 0x5cd1d8 — VIBE_Anim_UpdateSkeletonPose.
//
// The translation below mirrors the decompile's control flow block-for-block.
// Variable names map to the decompile's locals where it aids verification:
//   v165 = object (st)          v185 = masked time      v179 = elapsed (v185 - v2)
//   v190 = forced (sign bit)    v2   = previous +64     i/v178 = LOD layer index
//   j    = per-bone track index v5   = the 116-byte track record (PoseTrack)
//   v168 = the track's AnimHeader (PoseAnimHeader)       v59 = morph anim (PoseMorphAnim)
//   v191 = morphActiveOut       v193 = boundaryThisTick  v180 = vegCacheLayer
// Side effects (Object_SetPosition, Texture_AdvanceAnimFrames, the bone-delta /
// interpolate leaves, the veg-cache rebuild, the morph-block free) go through `h`;
// the frame-advance MATH is AdvanceFrameIndex (0x5ccf18) called directly.
// ============================================================================

namespace {

// Forward (non-reverse) leg of the skeletal inner while(1): the original's
// `(*(v5+109)&2)==0` block (decompile 0x5cd8c6..). Advances the frame cursor while
// the accumulated phase has consumed the current segment. `dur(k)` is the per-frame
// segment duration from the header (durations[k]). Mutates the track in place and
// sets `t.finished` on a clamp/one-shot endpoint. Returns when the segment fits.
//
// This is the *driver* advance — distinct from the simplified equivalent in
// skeleton_pose.cpp (AdvanceTrackPhase); here we reproduce the engine's exact branch
// ladder (repeat-counter decrement, the 0x10 clamp re-arm, the ping-pong flip).
inline i32 dur(const PoseAnimHeader& hd, i32 k) {
    return (hd.durations && k >= 0 && k < hd.frameCount) ? hd.durations[k] : 1;
}

// Decrement the repeat counter (+108) and, on the 2->1 transition, set the clamp
// bit (0x10) in the mode byte — the engine's `if (v34) { --v34; if (v34==2) mode|=0x10; }`
// idiom that appears at every boundary.
inline void DecRepeat(u8& repeatCount, u8& mode) {
    if (repeatCount) {
        u8 old = repeatCount;
        repeatCount = static_cast<u8>(repeatCount - 1);
        if (old == 2) mode |= 0x10;
    }
}

} // namespace

bool UpdateSkeletonPose(SkeletonPoseState& st, SkeletonPoseHooks& h, u32 time) {
    // 0x5cd1e8 — split the time word: top bit = forced/suppress-light flag.
    const bool forced = (time & 0x80000000u) != 0;        // v190
    const u32  now    = time & 0x7FFFFFFFu;               // v185
    const u32  prev   = st.lastUpdateTime;                // v2
    const u32  elapsed = now - prev;                      // v179
    st.lastUpdateTime = now;                              // *(a1+64) = v185
    if (now == prev)                                      // 0x5cd22e
        return true;

    st.morphActiveOut   = false;   // v191
    st.boundaryThisTick = false;   // v193
    st.vegCacheLayer    = 0;       // v180

    // ------------------------------------------------------------------ //
    // SKELETAL PHASE — only when the object has drawData (+492 != 0).      //
    // ------------------------------------------------------------------ //
    if (st.hasDrawData) {
        // 0x5cd6fd — texture-anim advance: forced-forward only, lighting on,
        // not draw-disabled, and a type-4 (vegetation) object.
        if (!forced && st.hasSkin && (st.drawFlags & 0x40) == 0 && st.objectType == 4) {
            if (h.textureAdvanceAnimFrames) h.textureAdvanceAnimFrames(st, now);
        }

        // 0x5cd278 — walk every LOD layer (the i += 384 loop, bounded by drawData+2316).
        for (i32 li = 0; li < st.layerCount; ++li) {
            SkeletonPoseState::Layer& layer = st.layers[li];
            if (!layer.active)                 // *(i+v4+624) gate
                continue;

            // 0x5cd2ce — the 3 per-bone tracks (j: 0,116,232 -> indices 0,1,2).
            for (int ti = 0; ti < 3; ++ti) {
                PoseTrack& t = layer.tracks[ti];          // v5
                const PoseAnimHeader& hd = layer.headers[ti]; // v168 view

                // 0x5cd30d — track active gate: header present (+104) and (+110 & 2).
                if (t.animHeaderId == 0 || (t.flags & 2) == 0)
                    continue;

                // 0x5cd335 — activity-expiry: elapsed beyond +60 arms a boundary.
                if (elapsed > t.expiry)
                    st.boundaryThisTick = true;

                // 0x5cd34f — pick the clamp upper bound: reverse-clamp (+109 & 0x10)
                // uses startFrame-1 and clears the delta scratch when not delta-encoded;
                // forward uses endFrame.
                const i32 firstFrame = hd.startFrame;     // v167
                const i32 lastFrame  = hd.endFrame;
                const i32 frameCount = hd.frameCount;     // v166
                i32 advLast;                              // v7
                if ((t.mode & 0x10) != 0) {               // 0x5cd359
                    advLast = frameCount - 1;
                    if (!hd.deltaEncoded)
                        t.deltaScratch = 0;
                } else {
                    advLast = lastFrame;                  // 0x5cd71b
                }
                t.mode &= ~0x20;                          // 0x5cd391 clear reset-pending
                if (h.globalFrameCounter)                 // 0x5cd3a7 AnimHeader+344 = counter
                    (void)h.globalFrameCounter();
                if (h.objectPropagateDirty) h.objectPropagateDirty(st, 1); // 0x5cd3bb

                t.finished = false;                       // v188 = 0

                // 0x5cd3cf — blend window: when blendFrom != blendTo, either lerp the
                // weight (blendTo still in the future) or snap to the end weight.
                if (t.blendFrom != t.blendTo) {
                    if (t.blendTo > now) {                 // 0x5cd737
                        double span = (double)(t.blendTo - t.blendFrom);
                        double s    = (double)(now - t.blendFrom) / span;
                        t.blendWeight = (t.blendEnd - t.blendBeg) * (float)s + t.blendBeg;
                    } else {                               // 0x5cd3e0
                        t.blendTo     = 0;
                        t.blendWeight = t.blendEnd;
                        t.blendFrom   = 0;
                        if (t.blendWeight == 0.0f)         // 0x5cd3fd
                            t.finished = true;             // v188 = 1
                    }
                }

                // 0x5cd40c — phase carry: add (reverseSign ? -rate : +rate)*elapsed to
                // the fractional phase, fold the integer part into the integer phase.
                // reverseSign = (*(v5+110) << 6) >> 7  i.e. (flags & 0x2) ? 1 : 0.
                const int reverseSign = (t.flags & 0x02) ? 1 : 0;   // v187
                // *(v5+96) is the per-track phase rate; the engine computes
                //   phaseFrac += sign*rate*elapsed   (sign = (flags&2)?-1:+1),
                // then folds the integer part into the integer phase. The rate field is
                // part of the 116-byte record but is supplied by the host (it pre-loads
                // phaseFrac with the per-tick increment), so we only do the integer fold.
                (void)reverseSign;
                // 0x5cd439/0x5cd442 — integer phase step direction depends on the MODE
                // byte's reverse bit (*(v5+109) & 2). The inner-loop leg selector reads
                // the SAME mode byte: 0x5cd490 is `test byte ptr [esi+6Dh], 2` (+0x6D ==
                // +109). (An earlier note claimed +110 here; the disasm shows +109 —
                // +110 is read at 0x5cd4a4 for the settle bit (&4) and via the
                // `(<<6)>>7` bit-1 extract for the phase-rate sign.)
                const float carry = t.phaseFrac;
                const i32 ci = (i32)carry;                 // VIBE_Coord_ConvertX truncation
                if ((t.mode & 0x02) != 0)
                    t.phase -= ci;                         // 0x5cd450 reverse: subtract
                else
                    t.phase += ci;                         // 0x5cd77e forward: add
                t.phaseFrac = carry - (float)ci;           // 0x5cd48d keep the fraction

                // 0x5cd494 — the inner while(1) frame-advance ladder.
                // The engine relies on the precise sub-frame phase arithmetic to make
                // monotonic progress; we bound the ladder defensively (a degenerate /
                // zero-duration table or a host-supplied overshoot could otherwise spin),
                // matching the guard convention used by AdvanceTrackPhase in
                // skeleton_pose.cpp. The bound is generous: every real tick consumes at
                // most a handful of segments.
                bool brokeOnSettle = false;
                int guard = frameCount * 4 + 8;
                for (; guard-- > 0;) {
                    // 0x5cd490: `test byte ptr [esi+6Dh], 2` — the leg selector is the
                    // MODE byte's reverse bit (+109 & 2), per the disasm.
                    if ((t.mode & 0x02) != 0) {
                        // ---- reverse / ping-pong leg (0x5cd49e) ----
                        if (t.phase < 0) {
                            if (firstFrame < t.toFrame) {  // 0x5cd7a3: step back one frame
                                t.fromFrame -= 1;
                                t.toFrame = AdvanceFrameIndex(t.mode, t.fromFrame,
                                                              advLast, firstFrame, frameCount);
                                t.phase += dur(hd, t.toFrame);
                            } else {
                                if ((t.mode & 1) != 0) {   // 0x5cd7af loop -> bounce forward
                                    i32 p = t.phase;
                                    t.fromFrame = firstFrame;
                                    t.flags &= ~0x10;      // clear hold on +110
                                    t.mode  &= ~0x02;      // leave reverse
                                    t.phase  = -p;
                                    t.toFrame = AdvanceFrameIndex(t.mode, firstFrame,
                                                                  advLast, firstFrame, frameCount);
                                    DecRepeat(t.repeatCount, t.mode);
                                } else {                   // 0x5cd809 hold/clamp
                                    t.flags &= ~0x10;
                                    DecRepeat(t.repeatCount, t.mode);
                                    t.fromFrame = advLast;
                                    t.phase += dur(hd, advLast);
                                    t.toFrame = AdvanceFrameIndex(t.mode, t.fromFrame,
                                                                  advLast, firstFrame, frameCount);
                                }
                            }
                        }
                    } else {
                        // ---- forward leg (0x5cd8c6) ----
                        i32 segDur = dur(hd, t.fromFrame);
                        if (t.phase >= segDur) {
                            if (t.fromFrame + 1 < advLast) {     // 0x5cd8d7 mid-run step
                                t.phase -= segDur;
                                t.fromFrame += 1;
                            } else {
                                if ((t.mode & 0x10) != 0) {      // 0x5cd8e3 clamp one-shot
                                    t.boundaryFrame = advLast;   // v161
                                    t.finished = true;           // v188 = 1
                                    t.phase = segDur - 1;
                                } else if ((t.mode & 1) != 0) {  // 0x5cd921 loop -> reverse
                                    t.mode |= 2;                 // *(v5+109) |= 2
                                    t.phase -= (reverseSign + t.phase - dur(hd, t.fromFrame) + 1);
                                    t.fromFrame = t.toFrame;
                                    DecRepeat(t.repeatCount, t.mode);
                                } else {                         // 0x5cd989 hold/wrap
                                    t.flags &= ~0x10;
                                    DecRepeat(t.repeatCount, t.mode);
                                    t.phase -= dur(hd, t.fromFrame);
                                    t.fromFrame = firstFrame;
                                    if (!hd.deltaEncoded && h.computeBoneDelta) // 0x5cd9f6
                                        h.computeBoneDelta(st, li, ti, advLast,
                                                           t.deltaScratch, t.flags >> 6);
                                }
                                t.toFrame = AdvanceFrameIndex(t.mode, t.fromFrame,
                                                              advLast, firstFrame, frameCount);
                            }
                        }
                    }

                    // 0x5cd4a8 — settle check (+110 & 4): when the sampled bone
                    // translation has settled within tolerance, stop the ladder.
                    if ((t.flags & 0x04) != 0) {
                        i32 sampleFrame = t.phase;                 // v162
                        i32 segDur = dur(hd, t.fromFrame);
                        if (!(t.phase >= 0 && t.phase < segDur))
                            sampleFrame = segDur / 2;
                        float trans[3] = {0,0,0};                  // v156
                        if (h.sampleBoneTranslation)
                            h.sampleBoneTranslation(st, li, ti, sampleFrame, t.fromFrame, trans);
                        if (std::fabs(trans[0] - t.tolPos[0]) < t.settleTol &&
                            std::fabs(trans[2] - t.tolPos[2]) < t.settleTol) {
                            // 0x5cd545 — settled: latch finished + hold.
                            t.phase = sampleFrame;
                            t.finished = true;       // v188 = 1
                            t.flags |= 0x08;         // *(v5+110) |= 8
                            brokeOnSettle = true;
                            break;
                        }
                    }

                    // 0x5cda17 — loop continuation guard: keep advancing while the
                    // phase is still out of the current segment in the active direction.
                    // Both direction tests are the MODE byte: 0x5cda44 / 0x5cda70 are
                    // `test byte ptr [esi+6Dh], 2` (+0x6D == +109) in the disasm.
                    if (!t.finished) {
                        i32 ph = t.phase;
                        if (ph < 0 ||
                            (ph >= dur(hd, t.fromFrame) && (t.mode & 0x02) == 0)) {
                            continue;                 // 0x5cda48
                        }
                        if (ph >= dur(hd, t.toFrame) && (t.mode & 0x02) != 0) {
                            continue;                 // 0x5cda74
                        }
                    }
                    break;  // LABEL_31
                }
                (void)brokeOnSettle;

                // LABEL_31 (0x5cd55c) — push the frame: delta-encoded headers use the
                // bone interpolate leaf; non-delta headers were handled by ComputeBoneDelta.
                if (hd.deltaEncoded && h.interpolateBoneFrame)
                    h.interpolateBoneFrame(st, li, ti, t.fromFrame, t.phase, t.flags >> 6);

                // 0x5cd582 — per-frame attachment-window blend (Object_SetPosition path).
                if (t.attachStart != -1 && t.attachEnd != -1 && t.attachCur != -1 &&
                    t.attachRec != 0) {
                    if (t.fromFrame >= t.attachStart) {
                        if (t.fromFrame < t.attachEnd) {
                            // 0x5cda7f — capture the attach base once (flags & 0x40).
                            if ((t.flags & 0x40) == 0) {
                                t.flags |= 0x40;
                                // attachBase <- the attachment record's stored base
                                // (v22[19..21]); host-supplied via attachBase.
                            }
                            // 0x5cdae5 — lerp the attachment position over the segment,
                            // rotate by the bone hierarchy, add base, then push it. The
                            // vector math (VectorLerp + RotateVectorByHierarchy) is the
                            // attachment leaf; the genuine scene-graph push is the hook.
                            if (h.objectSetPosition)
                                h.objectSetPosition(st, t.attachBase);
                        } else {
                            // 0x5cd5ad — window passed its end: release the attachment.
                            if ((t.flags & 0x40) != 0) {
                                t.attachCur = -1;
                                t.attachRec = 0;
                                t.attachEnd = -1;
                                t.attachStart = -1;
                                t.flags &= ~0x40;
                            }
                        }
                    }
                }

                // 0x5cd5d6 — boundary bookkeeping.
                t.boundary = false;   // v189
                if (t.finished) {
                    t.flags &= ~0x02;                       // clear active
                    if ((t.mode & 0x08) != 0) {             // 0x5cd5f1 finished-while-held
                        t.mode |= 0x20;                     // arm reset-pending
                    } else {
                        t.boundary = true;                  // v189 = 1
                        if (!hd.deltaEncoded && h.computeBoneDelta)  // 0x5cd62b
                            h.computeBoneDelta(st, li, ti, t.boundaryFrame,
                                               t.deltaScratch, t.flags >> 6);
                        if (h.pruneExpiredAttachments)       // 0x5cd652
                            h.pruneExpiredAttachments(st, li);
                    }
                }

                // 0x5cd65f — record the layer that drives the vegetation light cache
                // (only when the boundary did NOT prune, i.e. !v189).
                if (st.boundaryThisTick && !t.boundary) {
                    // v180 = 192 * fromFrame + frameArrayBase(+348). Here the layer
                    // index is the stable identity the host maps back to a frame base.
                    st.vegCacheLayer = li + 1;   // +1 so 0 stays "none" (engine uses a ptr)
                }
            }
        }
    }

    // ------------------------------------------------------------------ //
    // 0x5cdc18 — rebuild the bone-matrix palette after all tracks advanced.//
    // ------------------------------------------------------------------ //
    if (h.computeBoneMatrices) h.computeBoneMatrices(st);

    // ------------------------------------------------------------------ //
    // OBJECT MORPH PHASE — when object+464 (morph) is present and active.  //
    // 0x5cdc1d..0x5cebd7. This is the Catmull-Rom keyframed position/world //
    // translation track; its frame-advance ladder mirrors the skeletal one //
    // but over the 88-byte object-anim keyframes (PoseMorphAnim).          //
    // ------------------------------------------------------------------ //
    if (st.morph && (st.morph->flags & 0x02) != 0) {
        PoseMorphAnim& m = *st.morph;                 // v59
        m.flags &= ~0x20;                             // 0x5cdc3a clear reset-pending mirror
        m.finished = false;                           // v192

        // 0x5cdc47 — interpolation weight: reverse (mode & 2) blends toward toFrame,
        // forward toward fromFrame. (The exact Catmull-Rom blend + Object_SetPosition /
        // Object_SetWorldTranslation are the object-anim leaves; the DRIVER value here
        // is the advance ladder + the termination/free path, so the heavy per-axis
        // Catmull-Rom evaluation is routed through the position hooks below.)

        // 0x5ce184 — morph activity-expiry: elapsed past +16 arms the light cache.
        if ((now - st.animBaseTime) > m.expiry)
            st.morphActiveOut = true;                 // v191 = 1

        // 0x5ce19b — phase carry (same reverse-sign rule as the skeletal path).
        const int reverseSign = (m.flags & 0x02) ? 1 : 0;     // v187 mirror (uses +46&2)
        const float carry = m.phaseFrac;                      // host pre-loads the increment
        const i32 ci = (i32)carry;
        if ((m.mode & 0x02) != 0)
            m.phase -= ci;                            // 0x5ce1df reverse
        else
            m.phase += ci;                            // 0x5ce920 forward
        m.phaseFrac = carry - (float)ci;              // 0x5ce221
        (void)reverseSign;

        const i32 frameCount = m.frameCount;          // v160
        const i32 advLast    = frameCount - 1;        // v87

        // 0x5ce224 — the morph frame-advance while(1). Same defensive bound as the
        // skeletal ladder (the per-keyframe durations are host-owned here).
        int mguard = frameCount * 4 + 8;
        for (; mguard-- > 0;) {
            if ((m.mode & 0x02) != 0) {
                // reverse leg (0x5ce230)
                if (m.phase >= 0) break;              // 0x5ce235 `jl 0x5ce93b` else LABEL_116
                // 0x5ce93b: the dispatch key is *(v59+8) == TOFRAME (NOT phase). The
                // disasm is `cmp dword ptr [esi+8],0 / jle 0x5ce97f` (signed toFrame<=0).
                // (An earlier reconstruction tested phase<=0, which is vacuously true
                // here since phase<0 already; the binary keys on toFrame.)
                if (m.toFrame <= 0) {                 // 0x5ce93f jle -> 0x5ce97f
                    if ((m.mode & 0x10) != 0) {       // 0x5ce982 clamp -> finish
                        m.phase = 0; m.fromFrame = 0; m.finished = true;
                    } else if ((m.mode & 0x01) != 0) {// 0x5ce9a0 loop -> bounce
                        m.phase = -m.phase; m.fromFrame = 0;
                        m.mode  &= ~0x02; m.flags &= ~0x10;
                        DecRepeat(m.repeatCount, m.mode);
                    } else {                          // 0x5ce9dd hold
                        // The engine adds the keyframe(advLast) duration (read from the
                        // 88-byte frame +0) back into the phase. That per-keyframe
                        // duration is supplied by the host (it owns the resolved frame
                        // array); here we step the cursor and let the host re-fold phase.
                        m.fromFrame = advLast;
                        m.flags &= ~0x10;
                        DecRepeat(m.repeatCount, m.mode);
                    }
                } else if (m.fromFrame == 1 && (m.mode & 0x10) != 0) {
                    // 0x5ce944 `dec fromFrame / jnz` + 0x5ce94e `test cl,0x10` :
                    // fromFrame==1 && clamp -> finish at frame 0 (0x5ce954).
                    m.fromFrame = 0; m.phase = 0; m.finished = true;
                } else {
                    // 0x5cea19 step back one keyframe (the phase += dur(toFrame) add is
                    // host-owned, as the 88-byte keyframe duration is not modelled here).
                    m.flags &= ~0x10;
                    m.fromFrame -= 1;
                }
                m.toFrame = AdvanceFrameIndex(m.mode, m.fromFrame, advLast, 0, frameCount);
            } else {
                // forward leg (0x5cea3b)
                // The per-frame duration lives in the 88-byte keyframe (+0). Without the
                // resolved frame array we cannot read it here; the host advances the
                // morph cursor through the position hooks. Mirror the terminal cases:
                if (m.fromFrame + 1 >= advLast && (m.mode & 1) != 0) {   // 0x5cea5e loop->reverse
                    m.mode |= 0x02;
                } else if (advLast > m.fromFrame) {                      // 0x5ceab5 step
                    if ((m.flags & 0x10) != 0 && (m.mode & 0x10) != 0 && (m.mode & 1) == 0) {
                        // 0x5ceacb clamp terminal -> finish
                        m.fromFrame = m.frameCount - 1;
                        m.finished = true;
                    } else {
                        m.fromFrame += 1;             // 0x5ceb41
                    }
                } else if ((m.mode & 1) == 0) {       // 0x5ceb54 hold/wrap
                    m.fromFrame = 0;
                    DecRepeat(m.repeatCount, m.mode);
                } else {                              // 0x5ceb64 loop -> reverse
                    m.mode |= 0x02;
                }
                m.toFrame = AdvanceFrameIndex(m.mode, m.fromFrame, advLast, 0, frameCount);
            }

            // LABEL_116 (0x5ce275) — when finished or the phase is back in range, emit.
            if (m.finished || m.phase >= 0) {
                if (m.finished) {
                    // 0x5ce27f — terminal: push the final position + world translation
                    // (Object_SetPosition + Object_SetWorldTranslation), then either
                    // latch (mode & 8) or FREE the morph block.
                    if (h.objectSetPosition)         h.objectSetPosition(st, m.basePos);
                    if (h.objectSetWorldTranslation) h.objectSetWorldTranslation(st, m.baseRot);
                    if ((m.mode & 0x08) != 0) {       // 0x5cebd7 latch finished
                        m.flags &= ~0x02;
                        m.mode  |= 0x20;
                    } else if (h.freeMorphAnim) {     // 0x5ce370 free the morph anim
                        h.freeMorphAnim(st);
                    }
                }
                break;
            }
        }

        // 0x5cdc11.. (the non-terminal frames) — push the interpolated position +
        // world translation for this frame. These are the object-anim Catmull-Rom
        // evaluations + the genuine scene-graph pushes; routed through the hooks.
        if (!m.finished) {
            if (h.objectSetPosition)         h.objectSetPosition(st, m.basePos);
            if (h.objectSetWorldTranslation) h.objectSetWorldTranslation(st, m.baseRot);
        }
    }

    // ------------------------------------------------------------------ //
    // LABEL_126 (0x5cebed) — vegetation light-cache rebuild gate.          //
    // ------------------------------------------------------------------ //
    if (!st.morphActiveOut && !st.boundaryThisTick)
        return true;                       // nothing to relight
    if (forced)                            // 0x5ce3cf suppress on forced ticks
        return true;
    if (st.objectType != 4)                // 0x5ce3e3 only type-4 (vegetation)
        return true;
    if (!st.hasDrawData || !st.hasSkin || (st.drawFlags & 0x40) != 0)  // 0x5ce40b
        return true;

    // 0x5ce411/0x5ce419 — `cmp [v180],0 / jz` : FindHighestPriorityLayer is invoked
    // ONLY when a layer was already recorded this tick (v180 != 0). When v180 == 0 the
    // branch jumps straight to BuildVegetationCache(obj, 0) — it does NOT search.
    // (Earlier reconstruction inverted this gate; the disasm at 0x5ce419 is `jz` past
    // the call, i.e. skip-when-zero, so the call happens on the non-zero path.)
    if (st.vegCacheLayer != 0) {
        int hp = h.findHighestPriorityLayer ? h.findHighestPriorityLayer(st) : -1;
        if (hp >= 0) st.vegCacheLayer = hp + 1;
    }
    // 0x5ce45f — rebuild the vegetation light cache for the chosen layer.
    if (h.buildVegetationCache) h.buildVegetationCache(st, st.vegCacheLayer);
    return true;
}

} // namespace guild::render
