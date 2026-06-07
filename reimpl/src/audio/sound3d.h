#pragma once
// guild::audio — 3D positional sound pool (d3snd) of gilde.exe.
//
// A pool of fixed-size 84-byte entries, each binding a world entity to a voice.
// Each update recomputes the voice volume from listener<->source distance
// (attenuation) and the voice pan from the horizontal angle to the listener's
// forward vector, then drives the voice.
//
// Recovered from:
//   VIBE_Sound3d_InitPool          @0x424538  (84-byte entries)
//   VIBE_Sound3d_FreePool          @0x4245c4
//   VIBE_Sound3d_FindFreeSlot      @0x424744  (free == entry+0x38 == 0)
//   VIBE_Sound3d_UpdateAttenuation @0x4249d0  (distance -> volume + pan)
//   VIBE_Sound3d_UpdatePosition    @0x4248d4  (angle -> pan)
//   VIBE_Sound3d_DetachEntry       @0x4246ec
//
// The original's attenuation/pan read camera & bone-chain transforms (out of
// scope here). We extract the *recoverable curve math* as pure functions over
// plain 3D vectors so it is exact and testable; the pool wires them to voices.
#include "guild/common/types.h"
#include "audio/voice.h"
#include <cmath>
#include <vector>

namespace guild::audio {

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

// --- Recovered constants (exact bytes via get_bytes) -------------------------

// dbl_611524 / dbl_611504 @0x611524 / @0x611504 — world-units -> meters scale
// applied to the listener<->source distance (1 unit ≈ 0.0254 m, i.e. inches).
constexpr double kMetersPerUnit = 0.0254000508001016; // 0x3F9A02788E03404F

// Pan curve constants from VIBE_Sound3d_UpdatePosition @0x4248d4:
//   pan = sin(angle) * 127.0 * 0.5 + 63.0
constexpr double kPanAmplitude = 127.0; // dbl_61150C @0x61150C
constexpr double kPanHalf      = 0.5;   // dbl_611514 @0x611514
constexpr double kPanCenter    = 63.0;  // dbl_61151C @0x61151C

// Attenuation blend weight from VIBE_Sound3d_UpdateAttenuation @0x4249d0:
//   volume = (curveA + curveB) * 0.5
constexpr float kAttenBlend = 0.5f; // flt_61152C @0x61152C

// Minimum audible computed pan; below this the original stops/skips the voice
// (UpdateAttenuation: `if ( (int)v9 > 5 )`).
constexpr int kMinAudiblePan = 5;

// --- Pure curve math ---------------------------------------------------------

// VIBE_Sound3d_UpdateAttenuation @0x4249d0 — volume from distance.
//   dist     = |source - listener| * kMetersPerUnit
//   falloff  = maxDist - dist
//   if falloff <= 0: volume = 0
//   else:
//     curveA = baseVol - dist*(baseVol*dist)/(maxDist*maxDist)
//     curveB = max(0, baseVol*(falloff/maxDist))
//     volume = trunc((curveA + curveB) * 0.5)
// Near => loud, far => quiet, beyond maxDist => silent. `baseVol`/`maxDist` are
// the entry's per-emitter volume (+0x40) and radius (+0x48).
int Compute3dVolume(const Vec3& listener, const Vec3& source, float maxDist, int baseVol);

// VIBE_Sound3d_UpdatePosition @0x4248d4 — pan from horizontal angle.
//   angle = angleBetween(dirToSource, listenerForward)   [radians, 0..pi]
//   pan   = trunc(sin(angle) * 127.0 * 0.5 + 63.0)
// Source directly ahead => sin(0)=0 => pan≈63 (center); source to the side =>
// sin grows => pan rises toward the right. (The original's left/right sign comes
// from the camera basis; we expose the magnitude curve faithfully.)
int Compute3dPan(const Vec3& listenerForward, const Vec3& dirToSource);

// VIBE_Math_VectorAngleBetween @0x5ca334 analogue: unsigned angle in radians
// between two vectors (acos of normalized dot, clamped). Helper for Compute3dPan.
double AngleBetween(const Vec3& a, const Vec3& b);

// --- Pool --------------------------------------------------------------------

// One 84-byte 3D entry. Offsets recovered from the Sound3d functions:
//   +0x34 voice slot ptr (entry "owns" a VoiceSlot)
//   +0x38 entity/source handle (0 == free slot)  [FindFreeSlot test]
//   +0x40 base volume         (int, +0x40 in UpdateAttenuation)
//   +0x48 radius / max distance (float, +0x48 in UpdateAttenuation)
//   +0x3C lost-update counter (incremented in UpdateAll, reset on rebind)
//   +0x50 oneShot flag (0x50 in AttachToEntity: 1 => transient)
struct Sound3dEntry {
    VoiceSlot* voice = nullptr; // +0x34
    bool inUse = false;         // +0x38 != 0
    Vec3 sourcePos;             // world position of the emitter (bone-chain pt)
    int baseVol = 127;          // +0x40
    float radius = 0.0f;        // +0x48
    int lostCounter = 0;        // +0x3C
    bool oneShot = false;       // +0x50
    const void* pcm = nullptr;  // PCM to (re)start when audible
    std::size_t pcmBytes = 0;
    int sampleRate = 44100;
};

class Sound3dPool {
public:
    Sound3dPool(VoicePool* voices, int capacity)
        : voices_(voices), entries_(static_cast<std::size_t>(capacity > 0 ? capacity : 0)) {}

    // VIBE_Sound3d_FindFreeSlot @0x424744 — first entry with inUse == false.
    Sound3dEntry* findFreeSlot();

    // VIBE_Sound3d_AttachToEntity @0x4245f0 — claim a free slot for a source.
    // Returns null if the pool is full.
    Sound3dEntry* attach(const Vec3& pos, int baseVol, float radius,
                         const void* pcm, std::size_t bytes, int sampleRate,
                         bool oneShot);

    // VIBE_Sound3d_DetachEntry @0x4246ec — stop the voice and free the entry.
    void detach(Sound3dEntry* e);

    // VIBE_Sound3d_StopAll @0x424890 — detach every in-use entry.
    void stopAll();

    // VIBE_Sound3d_UpdateAll @0x424790 — for each in-use entry recompute volume
    // (attenuation) and pan (position) against the listener and drive its voice;
    // starts the voice when it becomes audible, stops it when out of range.
    void updateAll(const Vec3& listener, const Vec3& listenerForward);

    int capacity() const { return static_cast<int>(entries_.size()); }
    Sound3dEntry* entryAt(int i) {
        return (i >= 0 && i < capacity()) ? &entries_[i] : nullptr;
    }

private:
    VoicePool* voices_;
    std::vector<Sound3dEntry> entries_;
};

} // namespace guild::audio
