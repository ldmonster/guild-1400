#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::render — particle SYSTEM spawn / allocation cluster (gilde.exe d3_par.c).
//
// Faithful 1:1 reconstruction of the entry points that build a live particle
// SYSTEM object from a script-supplied emitter template, allocate its particle
// arrays, and seed the per-particle slots:
//
//   0x43fd24  VIBE_Particle_CreateEmitter      (fill a 0xA4 emitter template +
//                                               tail-call SpawnSystemByType)
//   0x5e3ae0  VIBE_Particle_SpawnSystemByType  (type-dispatch -> AllocSystem,
//                                               copy template into the system,
//                                               place + optionally prime trigger)
//   0x5e1000  VIBE_Particle_AllocSystem        (texture load + 4 allocations +
//                                               per-particle slot init loop)
//   0x5e1228  VIBE_Particle_SetPosition        (thin position forwarder)
//   0x43fb88  VIBE_Particle_KillParticle       (validity-guarded node free)
//   0x43fe68  VIBE_Particle_KillEmitter        (validity-guarded node free)
//
// These manage the *system header* (a 0x310-byte block) and the *particle array*
// (84-byte / 0x54 stride records — same Particle struct as particle.h). The
// per-frame INTEGRATORS that advance those particles (UpdatePoints @0x5e1e0c,
// UpdatePolys @0x5e2814, UpdateLens @0x5e32c0) are nearly pure x87 inline-asm
// over engine globals and are deferred (see particle_spawn.cpp tail comment); the
// AllocSystem loop here is what primes the slots those integrators read.
//
// OS / vendor boundary: AllocSystem's texture upload + heap allocation + object
// linkage are routed through the SpawnHooks vtable below (default = a tiny in-
// process allocator) so the spawn arithmetic is testable without the engine.
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// Particle SYSTEM header — the 0x310 (784) byte block VIBE_Particle_AllocSystem
// allocates. Only the fields the spawn/alloc cluster touches are modelled at
// their ORIGINAL byte offsets; the block is otherwise opaque (rendering / scene
// fields). Indexed as _DWORD* in the originals (v22[52]==+0xD0, etc.).
//
//   +0x20 (32)   spawnedCount   (u32)   running count of exhausted particles
//   +0x24 (36)   lastUpdateTick (u32)
//   +0x28 (40)   particles      (Particle*) 84-byte stride array base
//   +0xCC (204)  flagByte0      (u8)    emitter flags (copied from template +0xCC)
//   +0xCD (205)  flagByte1      (u8)    emitter flags (bit0 trigger, bit1 pending)
//   +0xD0 (208)  count          (i32)   particle slot count
//   +0xD4 (212)  pointsBuf      (void*) ParticlePoints scratch (AllocSystem)
//   +0xDC (220)  polysBuf       (void*) ParticlePolys scratch  (AllocSystem)
//   +0xD0 ...    (count drives both scratch buffer sizes)
//   +0xE4 (228)  lifeBase       (float) spawn-lifetime base -> particle +0x48/+72
//   +0xE8 (232)  object[]       (engine Object; SetPosition/world write here)
//   +0x2DC(732)  texture        (void*) loaded particle texture (v11+53)
//   +0x2D0(720)  owner          (int)   a1 (the owning scene node) (v11+52)
//   +0x2E0(736)  userTag        (int)   a5
//   +0x304(772)  updateFn       (void*) the per-frame integrator (a6)
// The template region copied by SpawnSystemByType spans system+44 .. system+208
// (0xA4 = 164 bytes), i.e. emitter velocity/accel/colour/flags. We expose it as
// a flat 164-byte block (EmitterTemplate) so the copy is byte-exact.
// ---------------------------------------------------------------------------
struct ParticleSystem {
    u32   spawnedCount;   // +0x20 (32)
    u32   lastUpdateTick; // +0x24 (36)
    void* particles;      // +0x28 (40)   Particle* (84-byte stride)
    u8    flagByte0;      // +0xCC (204)
    u8    flagByte1;      // +0xCD (205)
    i32   count;          // +0xD0 (208)
    void* pointsBuf;      // +0xD4 (212)
    void* polysBuf;       // +0xDC (220)
    float lifeBase;       // +0xE4 (228)
    void* texture;        // +0x2DC(732)
    i32   owner;          // +0x2D0(720)
    i32   userTag;        // +0x2E0(736)
    void* updateFn;       // +0x304(772)
    // Object translation captured by SetPosition / SetWorldTranslation (the
    // engine Object lives at +0xE8; we record the last placed position here for
    // test observability, matching the placeholder Object_* helpers).
    float position[3];    // +0xE8 (232)  (object world translation)
};

// ---------------------------------------------------------------------------
// Emitter TEMPLATE — the 0xA4 (164) byte block CreateEmitter fills (byte_765320)
// and SpawnSystemByType copies into the system at +44. The byte offsets below are
// relative to the TEMPLATE base (== system_base+44 once copied). The named fields
// are the ones CreateEmitter writes (verified bit-exact). The original wrote
// dword/byte literals at template-absolute offsets 0x765320..0x7653C0; we list
// the EMITTER-relative offset (= absolute - 0x765320).
//
//   +0x00  type/header byte    (= *a1, the emitter kind)
//   +0x34  amplitude.x         flt_5CA2D0 (= 0.0)
//   +0x40  scaleX              437F0000h? no: dword stores below
//   +0x34..+0x48 build basis / colour / scale literals (see CreateEmitter)
// We model the template as a raw 0xA4 byte array plus typed accessors at the
// specific offsets CreateEmitter touches; this keeps the copy byte-exact while
// letting the test inspect the recovered literals.
// ---------------------------------------------------------------------------
struct EmitterTemplate {
    u8 bytes[0xA4]; // 164 bytes (system+44 .. system+208)

    // Typed views at the offsets CreateEmitter writes. Offsets are
    // (absolute - 0x765320).  e.g. byte_765360 -> +0x40, dword_7653B8 -> +0x98.
    template <class T> T& at(unsigned off) {
        return *reinterpret_cast<T*>(bytes + off);
    }
    template <class T> const T& at(unsigned off) const {
        return *reinterpret_cast<const T*>(bytes + off);
    }
};

// ---------------------------------------------------------------------------
// Recovered literal constants used by CreateEmitter (bit-exact, get_bytes).
//   flt_5CA2D0 = 0.0          (amplitude.x default)
//   flt_5CA2D4 = 1.0          (0x3F800000)
//   flt_5CA2D8 = 0.0
//   flt_5CA2E0 = 0.0          (SpawnSystemByType angle base, passed by &)
// dword literals stored by CreateEmitter are float bit-patterns:
//   1092616192 = 0x41200000 = 10.0f   (dword_765354)
//   1112014848 = 0x42480000 = 50.0f   (dword_765358)
//   1106247680 = 0x41F00000 = 30.0f   (dword_76535C)
//   1065353216 = 0x3F800000 = 1.0f    (dword_7653A0)
//   1132396544 = 0x437F0000 = 255.0f  (dword_7653AC)
//   180 / 20 / 160 are plain integers (dword_7653B8 / B0 / B4).
// ---------------------------------------------------------------------------
constexpr float kCeAmpX     = 0.0f; // flt_5CA2D0
constexpr float kCeOne      = 1.0f; // flt_5CA2D4
constexpr float kCeZero     = 0.0f; // flt_5CA2D8
constexpr float kCeAngle    = 0.0f; // flt_5CA2E0

// ---------------------------------------------------------------------------
// Spawn hooks — the OS/heap/vendor boundary. AllocSystem in the original loads a
// texture, calls VIBE_Memory_AllocDebug four times and links the system into a
// global list. We route those through this vtable. The default backend allocates
// with new[] and tracks bytes/calls for tests; a production backend would call
// the real Memory/Texture/Object subsystems.
// ---------------------------------------------------------------------------
struct SpawnHooks {
    // VIBE_Texture_LoadByName @0x5da714 + UploadToSurface @0x5db234. Returns a
    // non-null texture handle on success, null on failure.
    void* (*loadTexture)(const u8* name, int slot) = nullptr;
    // VIBE_Memory_AllocDebug @0x438f10 — zero-or-garbage block of `size` bytes.
    void* (*alloc)(u32 size, const char* tag) = nullptr;
    // VIBE_Object_InitStruct @0x5b0e88 — init the embedded scene Object.
    void  (*initObject)(ParticleSystem* sys) = nullptr;
    // VIBE_Object_SetPosition @0x5af38c / SetWorldTranslation @0x5af50c — record
    // the placed position. Math_BuildBasisFromAngle/MatrixToEuler are folded into
    // SpawnSystemByType's placement and routed here as the final translation.
    void  (*placeObject)(ParticleSystem* sys, const float pos[3]) = nullptr;
};

void SetSpawnHooks(const SpawnHooks& hooks);  // null members restore defaults
const SpawnHooks& CurrentSpawnHooks();

// Test-observable counters for the default backend.
struct SpawnStats {
    int  allocCalls   = 0;
    u32  allocBytes   = 0;
    int  textureCalls = 0;
    int  initCalls    = 0;
    int  placeCalls   = 0;
};
SpawnStats& MutableSpawnStats();
void ResetSpawnStats();

// ---------------------------------------------------------------------------
// Validity / free boundary (KillParticle / KillEmitter). The originals call
// VIBE_Memory_IsValidPointer @0x4391f0 and VIBE_Render_FreeObjectNode @0x5e0f30
// (or VIBE_Script_ReportError on an invalid handle). Routed through a hook so
// the guard logic is testable. Default: non-null => valid; free records the freed
// node + an error sink.
// ---------------------------------------------------------------------------
struct KillHooks {
    bool (*isValid)(const void* node) = nullptr;       // VIBE_Memory_IsValidPointer
    void (*freeNode)(void* node) = nullptr;            // VIBE_Render_FreeObjectNode
    void (*report)(const char* message) = nullptr;     // VIBE_Script_ReportError
};
void SetKillHooks(const KillHooks& hooks);
const char* LastKillError();
void ClearLastKillError();
void* LastFreedNode();

// =============================================================================
// The reconstructed functions.
// =============================================================================

// 0x5e1000 — VIBE_Particle_AllocSystem (__userpurge, eax = ret;
//   eax=owner, edx=texName, ebx=texSlot, [stack] life, userTag, updateFn).
// Returns a freshly allocated ParticleSystem* (null on failure: life<=0 ||
// !owner || texture load failed). `slotCount` is the number of particle slots
// (the original derives it from a global; we take it explicitly). Performs the
// per-particle init loop (clears pos/vel, stamps birth tick + lifeBase, clears
// the active bit, sets frame=0, and writes -1 into the slot's +0x4C dword).
ParticleSystem* AllocSystem(int owner, const u8* texName, int texSlot, float life,
                            int userTag, void* updateFn, int slotCount,
                            u32 nowTick);

// 0x5e3ae0 — VIBE_Particle_SpawnSystemByType (__userpurge, eax = ret;
//   eax=texSlot(a1), edx=position(a2), ecx=texName(a3), ebx=owner(a4),
//   [stack] userTag(a5), life(a6), template(a7)).
// Dispatches on template byte +0 (0 -> points integrator, 1 -> polys, 2 -> lens),
// allocates via AllocSystem, copies the 0xA4 template into system+44, places the
// system at `position`, and — if flagByte1 bit0 (trigger) is set — clears the
// spawned count and raises the trigger-pending bit (bit1). Returns the system
// (or null if AllocSystem failed / type out of range).
ParticleSystem* SpawnSystemByType(int texSlot, const float position[3],
                                  const u8* texName, int owner, int userTag,
                                  float life, const EmitterTemplate& tmpl,
                                  int slotCount, u32 nowTick);

// 0x43fd24 — VIBE_Particle_CreateEmitter (__userpurge, eax = ret;
//   eax=kindPtr(a1), ecx=ampPtr(a2), edx=texSlotPtr(a3), ebx=ownerPtr(a4),
//   [stack] texNamePtr(a5), userTagPtr(a6), typePtr(a7)).
// Zeroes the template, writes the recovered defaults (scale 10/50/30, colour
// white, alpha-time 255, life 180, etc.), copies *a1 into template+0, packs the
// (type, trigger) bits, then tail-calls SpawnSystemByType. `tmpl` is filled in
// place (== byte_765320). Returns the created system pointer.
ParticleSystem* CreateEmitter(const u8* kindPtr, const i32* ampPtr,
                              const int* texSlotPtr, const int* ownerPtr,
                              const u8** texNamePtr, const u8* userTypePtr,
                              const u8* triggerPtr, const float position[3],
                              EmitterTemplate& tmpl, int slotCount, u32 nowTick);

// 0x5e1228 — VIBE_Particle_SetPosition. If `sys` is non-null, place its object
// at `pos`; return sys. (Original returns the eax it was given.)
ParticleSystem* SetPosition(ParticleSystem* sys, const float pos[3]);

// 0x43fb88 — VIBE_Particle_KillParticle. If *handle is valid, free the node;
// else report "ecmd_KillParticle: invalid handle...". Returns 0.
int KillParticle(void** handle);

// 0x43fe68 — VIBE_Particle_KillEmitter. If *handle is valid, free the node; else
// report "KillEmitter: invalid handle...". Returns 0.
int KillEmitter(void** handle);

} // namespace guild::render
