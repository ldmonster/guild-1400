#include "audio/sound3d.h"

namespace guild::audio {

namespace {

// VIBE_Math_VectorNormalize @0x5cb148 — in place; (0,0,0) on zero-length vector.
void Normalize3(float& x, float& y, float& z) {
    float len = static_cast<float>(
        std::sqrt(static_cast<double>(x) * x + static_cast<double>(y) * y
                  + static_cast<double>(z) * z));
    // Original tests (LODWORD(len) & 0x7FFFFFFF) != 0, i.e. len != 0 (any sign of 0).
    if (len != 0.0f) {
        float inv = 1.0f / len;
        x = x * inv;
        y = y * inv;
        z = z * inv;
    } else {
        x = 0.0f;
        y = 0.0f;
        z = 0.0f;
    }
}

// VIBE_Math_AcosGuarded @0x5f0b9c — acos(clamp(dot, -1, 1)) (computed via atan2 of
// sqrt(1-dot^2)). We model it with std::acos over the clamped argument.
double AcosGuarded(double dot) {
    if (dot > 1.0) dot = 1.0;
    else if (dot < -1.0) dot = -1.0;
    return std::acos(dot);
}

} // namespace

double AngleBetween(const Vec3& dir, const Vec3& forward) {
    // gilde.exe 0x5ca334 — VIBE_Math_VectorAngleBetween(a1=dir@eax, a2=forward@edx).
    // This is a *horizontal* (XZ-plane) signed angle, NOT a 3D magnitude acos:
    //   - the Y component of BOTH vectors is forced to 0 (v12 = v15 = 0.0),
    //   - both are normalized,
    //   - parallel  -> v2 (uninitialized in the binary; we return 0 => sin 0 => center),
    //   - antiparallel -> -PI (flt -3.1415927f, dbl_628CE8 is actually -2*PI used below),
    //   - else sign of the up-axis cross component picks  -acos(dot)  vs  acos(dot)-2*PI.
    // flt_5CA2D0..D8 = (0,1,0): the sign test reduces to cross.y = fwd.x*dir.z - fwd.z*dir.x.
    float v14 = dir.x, v15 = dir.y, v16 = dir.z;     // a1 copy
    float v11 = forward.x, v12 = forward.y, v13 = forward.z; // a2 copy
    v12 = 0.0f;                                       // 0x5ca36f: a2.y = 0
    v15 = 0.0f;                                       // 0x5ca373: a1.y = 0
    Normalize3(v14, v15, v16);                        // VIBE_Math_VectorNormalize(&v14)
    Normalize3(v11, v12, v13);                        // VIBE_Math_VectorNormalize(&v11)

    constexpr double kEps = 1e-07;                    // dbl_628CE0
    // Parallel (vectors equal after normalize): binary returns uninitialized edx.
    // No deterministic value exists; 0.0 reproduces the observable "center" result.
    if (std::fabs((double)(v11 - v14)) <= kEps && std::fabs((double)(v12 - v15)) <= kEps
        && std::fabs((double)(v13 - v16)) <= kEps)
        return 0.0;
    // Antiparallel: return -PI (single-precision -3.1415927f literal in the binary).
    if (std::fabs((double)(-v11 - v14)) <= kEps && std::fabs((double)(-v12 - v15)) <= kEps
        && std::fabs((double)(-v13 - v16)) <= kEps)
        return static_cast<double>(-3.1415927f);

    // cross = a2 x a1 (only the .y term survives the (0,1,0) dot):
    //   cross.y = v11*v16 - v13*v14  (= fwd.x*dir.z - fwd.z*dir.x)
    double crossY = static_cast<double>(v11) * v16 - static_cast<double>(v13) * v14;
    double dot = static_cast<double>(v14) * v11 + static_cast<double>(v15) * v12
                 + static_cast<double>(v16) * v13;
    constexpr double kNeg2Pi = -6.28318530718;        // dbl_628CE8
    if (crossY > 0.0)
        return -AcosGuarded(dot);
    return AcosGuarded(dot) + kNeg2Pi;
}

int Compute3dVolume(const Vec3& listener, const Vec3& source, float maxDist, int baseVol) {
    // VIBE_Sound3d_UpdateAttenuation @0x4249d0.
    float dx = source.x - listener.x;
    float dy = source.y - listener.y;
    float dz = source.z - listener.z;
    // v24 = sqrt(dx^2+dy^2+dz^2) * 0.0254  (world units -> meters)
    float dist = static_cast<float>(
        std::sqrt(static_cast<double>(dx) * dx + static_cast<double>(dy) * dy
                  + static_cast<double>(dz) * dz)
        * kMetersPerUnit);
    // v22 = falloff = radius - dist
    float falloff = maxDist - dist;
    if (falloff <= 0.0f)
        return 0; // v25 = 0

    double v4 = static_cast<double>(baseVol);
    // v5 = v4 - dist*(v4*dist)/(radius*radius)
    double curveA = v4 - static_cast<double>(dist) * (v4 * dist)
                             / (static_cast<double>(maxDist) * maxDist);
    int v21 = static_cast<int>(curveA); // VIBE_Coord_ConvertX truncation
    // v6 = v4 * (falloff/radius)
    double curveB = v4 * (static_cast<double>(falloff) / maxDist);
    int v7 = static_cast<int>(curveB);
    int v8 = (v7 <= 0) ? 0 : v7;
    // v9 = (v21 + v8) * 0.5
    double blended = static_cast<double>(v21 + v8) * kAttenBlend;
    return static_cast<int>(blended); // v25 = (int)v9
}

int Compute3dPan(const Vec3& listenerForward, const Vec3& dirToSource) {
    // VIBE_Sound3d_UpdatePosition @0x4248d4:
    //   v3 = sin(VectorAngleBetween(dir, fwd)) * 127.0 * 0.5 + 63.0;  pan = (int)v3
    // VectorAngleBetween is the signed XZ-plane angle (see AngleBetween), so a source
    // on the +x side and one on the -x side yield different pans (true stereo).
    double angle = AngleBetween(dirToSource, listenerForward);
    double pan = std::sin(angle) * kPanAmplitude * kPanHalf + kPanCenter;
    return static_cast<int>(pan); // VIBE_Coord_ConvertX truncation toward zero
}

Sound3dEntry* Sound3dPool::findFreeSlot() {
    // VIBE_Sound3d_FindFreeSlot @0x424744: first entry with handle(+0x38)==0.
    for (auto& e : entries_) {
        if (!e.inUse)
            return &e;
    }
    return nullptr;
}

Sound3dEntry* Sound3dPool::attach(const Vec3& pos, int baseVol, float radius,
                                  const void* pcm, std::size_t bytes, int sampleRate,
                                  bool oneShot) {
    // VIBE_Sound3d_AttachToEntity @0x4245f0 (entry seed + first update).
    Sound3dEntry* e = findFreeSlot();
    if (!e)
        return nullptr;
    e->inUse = true;     // +0x38 = source handle (non-zero)
    e->sourcePos = pos;
    e->baseVol = baseVol; // +0x40
    e->radius = radius;   // +0x48
    e->lostCounter = 0;   // +0x3C
    e->oneShot = oneShot; // +0x50
    e->pcm = pcm;
    e->pcmBytes = bytes;
    e->sampleRate = sampleRate;
    e->voice = nullptr;   // +0x34 voice bound lazily on first audible update
    return e;
}

void Sound3dPool::detach(Sound3dEntry* e) {
    // VIBE_Sound3d_DetachEntry @0x4246ec.
    if (!e)
        return;
    if (e->voice) {
        voices_->stopVoice(e->voice, /*fade=*/false);
        e->voice->flags |= kVoiceFlag_Released; // |= 8
        VoicePool::setLoopFlag(e->voice, false);
        e->voice = nullptr;
    }
    e->inUse = false; // entity link cleared (+0x38 = 0)
}

void Sound3dPool::stopAll() {
    // VIBE_Sound3d_StopAll @0x424890.
    for (auto& e : entries_) {
        if (e.inUse)
            detach(&e);
    }
}

void Sound3dPool::updateAll(const Vec3& listener, const Vec3& listenerForward) {
    // VIBE_Sound3d_UpdateAll @0x424790 + UpdateAttenuation + UpdatePosition.
    for (auto& e : entries_) {
        if (!e.inUse)
            continue;

        int vol = Compute3dVolume(listener, e.sourcePos, e.radius, e.baseVol);

        Vec3 dir{e.sourcePos.x - listener.x, e.sourcePos.y - listener.y,
                 e.sourcePos.z - listener.z};
        int pan = Compute3dPan(listenerForward, dir);

        // UpdateAttenuation: voice is started/kept only while audible (pan > 5).
        if (pan > kMinAudiblePan && vol > 0) {
            if (!e.voice) {
                // VIBE_Sound_PlaySample path: grab a voice, mark looping, start.
                e.voice = voices_->allocVoiceChannel();
                if (e.voice) {
                    e.voice->sample = e.pcm;
                    VoicePool::setLoopFlag(e.voice, true);
                    voices_->setVoiceVolume(e.voice, vol);
                    voices_->setVoicePan(e.voice, pan);
                    voices_->startVoice(e.voice, e.pcm, e.pcmBytes, e.sampleRate,
                                        /*loops=*/0);
                }
            } else {
                voices_->setVoiceVolume(e.voice, vol);
                voices_->setVoicePan(e.voice, pan);
            }
            e.lostCounter = 0;
        } else {
            // Out of range / inaudible: stop the voice (UpdateAttenuation tail).
            if (e.voice) {
                voices_->setVoiceVolume(e.voice, 0);
                voices_->stopVoice(e.voice, /*fade=*/false);
                if (!e.oneShot) {
                    VoicePool::setLoopFlag(e.voice, false);
                    e.voice = nullptr;
                }
            }
        }
    }
}

} // namespace guild::audio
