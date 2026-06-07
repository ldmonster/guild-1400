#include "audio/sound3d.h"

namespace guild::audio {

double AngleBetween(const Vec3& a, const Vec3& b) {
    // VIBE_Math_VectorAngleBetween @0x5ca334 analogue: acos(dot / (|a||b|)).
    double dot = static_cast<double>(a.x) * b.x + static_cast<double>(a.y) * b.y
                 + static_cast<double>(a.z) * b.z;
    double la = std::sqrt(static_cast<double>(a.x) * a.x + static_cast<double>(a.y) * a.y
                          + static_cast<double>(a.z) * a.z);
    double lb = std::sqrt(static_cast<double>(b.x) * b.x + static_cast<double>(b.y) * b.y
                          + static_cast<double>(b.z) * b.z);
    if (la <= 0.0 || lb <= 0.0)
        return 0.0;
    double c = dot / (la * lb);
    if (c > 1.0)
        c = 1.0;
    else if (c < -1.0)
        c = -1.0;
    return std::acos(c);
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
    //   v3 = sin(angle) * 127.0 * 0.5 + 63.0;  pan = (int)v3
    double angle = AngleBetween(dirToSource, listenerForward);
    double pan = std::sin(angle) * kPanAmplitude * kPanHalf + kPanCenter;
    return static_cast<int>(pan); // VIBE_Coord_ConvertX truncation
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
