#include "render/particle_spawn.h"

#include <cstring>
#include <new>

namespace guild::render {

// =============================================================================
// Default OS/heap/vendor backend (testable, in-process). Production installs a
// backend that delegates to the real Memory/Texture/Object subsystems.
// =============================================================================
namespace {

SpawnStats g_spawnStats;

// A non-null sentinel texture handle the default loader hands back so the
// AllocSystem success path runs without a real GPU upload.
int g_defaultTextureSentinel = 0;

void* DefaultLoadTexture(const u8* /*name*/, int /*slot*/) {
    ++g_spawnStats.textureCalls;
    return &g_defaultTextureSentinel;
}

void* DefaultAlloc(u32 size, const char* /*tag*/) {
    ++g_spawnStats.allocCalls;
    g_spawnStats.allocBytes += size;
    // Zero-initialised, matching the cleared-then-used pattern in the original
    // (AllocSystem explicitly clears the slots it touches; we zero the rest so
    // untouched bytes are deterministic for tests).
    return new (std::nothrow) unsigned char[size]();
}

void DefaultInitObject(ParticleSystem* /*sys*/) {
    ++g_spawnStats.initCalls;
}

void DefaultPlaceObject(ParticleSystem* sys, const float pos[3]) {
    ++g_spawnStats.placeCalls;
    sys->position[0] = pos[0];
    sys->position[1] = pos[1];
    sys->position[2] = pos[2];
}

SpawnHooks g_spawnHooks = {
    &DefaultLoadTexture, &DefaultAlloc, &DefaultInitObject, &DefaultPlaceObject};

// --- Kill / validity backend -------------------------------------------------
char g_lastKillError[256] = {0};
void* g_lastFreedNode = nullptr;

bool DefaultIsValid(const void* node) { return node != nullptr; }

void DefaultFreeNode(void* node) { g_lastFreedNode = node; }

void DefaultReport(const char* message) {
    std::strncpy(g_lastKillError, message ? message : "", sizeof(g_lastKillError) - 1);
    g_lastKillError[sizeof(g_lastKillError) - 1] = '\0';
}

KillHooks g_killHooks = {&DefaultIsValid, &DefaultFreeNode, &DefaultReport};

} // namespace

void SetSpawnHooks(const SpawnHooks& hooks) {
    g_spawnHooks.loadTexture = hooks.loadTexture ? hooks.loadTexture : &DefaultLoadTexture;
    g_spawnHooks.alloc       = hooks.alloc       ? hooks.alloc       : &DefaultAlloc;
    g_spawnHooks.initObject  = hooks.initObject  ? hooks.initObject  : &DefaultInitObject;
    g_spawnHooks.placeObject = hooks.placeObject ? hooks.placeObject : &DefaultPlaceObject;
}
const SpawnHooks& CurrentSpawnHooks() { return g_spawnHooks; }

SpawnStats& MutableSpawnStats() { return g_spawnStats; }
void ResetSpawnStats() { g_spawnStats = SpawnStats(); }

void SetKillHooks(const KillHooks& hooks) {
    g_killHooks.isValid  = hooks.isValid  ? hooks.isValid  : &DefaultIsValid;
    g_killHooks.freeNode = hooks.freeNode ? hooks.freeNode : &DefaultFreeNode;
    g_killHooks.report   = hooks.report   ? hooks.report   : &DefaultReport;
}
const char* LastKillError() { return g_lastKillError; }
void ClearLastKillError() { g_lastKillError[0] = '\0'; }
void* LastFreedNode() { return g_lastFreedNode; }

// =============================================================================
// 0x5e1000 — VIBE_Particle_AllocSystem
//
// int __userpurge VIBE_Particle_AllocSystem@<eax>(
//     int owner@<eax>, u8* texName@<edx>, int texSlot@<ebx>,
//     float life, int userTag, void* updateFn)
//
// The original derives the slot count from a global on the system block before
// the per-particle loop (v22[52] == system+0xD0); we take `slotCount` explicitly
// and write it into the system before looping, then seed `nowTick` (the original
// reads dword_62EB38, the frame clock) into each slot's +0x30 birth-tick field.
//
// The four AllocDebug calls in the original are: the 0x310 system block, the
// 84*count particle array, the 80*(4*count) ParticlePoints scratch, and the
// 40*(2*count) ParticlePolys scratch. We reproduce all four (the scratch sizes
// affect the alloc-byte accounting the test checks).
// =============================================================================
ParticleSystem* AllocSystem(int owner, const u8* texName, int texSlot, float life,
                            int userTag, void* updateFn, int slotCount,
                            u32 nowTick) {
    if (life <= 0.0f)          // 0x5e1017
        return nullptr;
    if (!owner)                // 0x5e101b
        return nullptr;

    void* tex = g_spawnHooks.loadTexture(texName, texSlot); // 0x5e102c
    if (!tex)                  // 0x5e1035
        return nullptr;
    // VIBE_Texture_UploadToSurface(tex, 0, ...) — side-effect only; folded into
    // the loader hook (the default sentinel needs no upload).

    // 0x5e1048 — allocate the 0x310-byte system block.
    void* raw = g_spawnHooks.alloc(0x310, "d3_par:ParticleSystem");
    ParticleSystem* sys = static_cast<ParticleSystem*>(raw);
    sys->texture = tex;        // v11+53 (+0x2DC)
    sys->owner   = owner;      // v11+52 (+0x2D0)
    sys->count   = slotCount;  // v22[52] (+0xD0); original sets this earlier.
    // *((float*)v11 + 57) = life  (+0xE4): the spawn lifetime base.
    sys->lifeBase = life;      // (+0xE4)
    sys->userTag = userTag;    // v13+736
    sys->updateFn = updateFn;  // v11+193 (+0x304)
    sys->spawnedCount = 0;     // v22[8]
    sys->lastUpdateTick = 0;   // v22[9]

    // 0x5e109e — particle array: 84 bytes per slot.
    sys->particles =
        g_spawnHooks.alloc(static_cast<u32>(84 * slotCount), "d3_par:Particles");
    // 0x5e10c0 — ParticlePoints scratch: 80 * (4*count).
    g_spawnHooks.alloc(static_cast<u32>(80 * (4 * slotCount)), "d3_par:ParticlePoints");
    // 0x5e10e4 — ParticlePolys scratch: 40 * (2*count).
    sys->polysBuf =
        g_spawnHooks.alloc(static_cast<u32>(40 * (2 * slotCount)), "d3_par:ParticlePolys");

    // 0x5e1143 — per-particle slot init. Particle stride = 84 (0x54) bytes.
    unsigned char* base = static_cast<unsigned char*>(sys->particles);
    for (int i = 0; i < slotCount; ++i) {       // v24 < system+208
        unsigned char* p = base + static_cast<size_t>(i) * 84;
        std::memset(p + 56, 0, 12);             // +56/+60/+64 position = 0
        std::memset(p + 0,  0, 12);             // +0/+4/+8 velocity = 0
        std::memcpy(p + 48, &nowTick, 4);       // +48 birth tick = dword_62EB38
        float lb = sys->lifeBase;
        std::memcpy(p + 72, &lb, 4);            // +72 lifetime = system+228
        p[81] &= ~1u;                           // +81 clear active bit
        p[80] = 0;                              // +80 frame = 0
        i32 minusOne = -1;
        std::memcpy(p + 76, &minusOne, 4);      // v22[i/4+19] => +76 dword = -1
    }

    g_spawnHooks.initObject(sys);               // 0x5e11e1 VIBE_Object_InitStruct
    return sys;
}

// =============================================================================
// 0x5e3ae0 — VIBE_Particle_SpawnSystemByType
//
// Dispatch on template byte +0 (the emitter kind): 0 -> points integrator,
// 1 -> polys, 2 -> lens. Allocate via AllocSystem; on success copy the 0xA4
// template into the system at +44, place it, and (if the trigger bit is set)
// clear the spawned count and raise the trigger-pending bit.
// =============================================================================
ParticleSystem* SpawnSystemByType(int texSlot, const float position[3],
                                  const u8* texName, int owner, int userTag,
                                  float life, const EmitterTemplate& tmpl,
                                  int slotCount, u32 nowTick) {
    // updateFn is one of the per-frame integrators selected by template[0].
    // Modelled as a stable distinct sentinel per kind (the real addresses are
    // VIBE_Particle_UpdatePoints/UpdatePolys/UpdateLens, deferred — see tail).
    void* updateFn = nullptr;                   // 0x5e3af6 v8 = 0
    const u8 kind = tmpl.bytes[0];              // *a7
    if (kind == 0) {                            // 0x5e3af8 -> UpdatePoints
        updateFn = reinterpret_cast<void*>(static_cast<size_t>(0x5e1e0c));
    } else if (kind == 1) {                     // 0x5e3b04 -> UpdatePolys
        updateFn = reinterpret_cast<void*>(static_cast<size_t>(0x5e2814));
    } else if (kind == 2) {                     // 0x5e3b9a -> UpdateLens
        updateFn = reinterpret_cast<void*>(static_cast<size_t>(0x5e32c0));
    } else {
        return nullptr;                         // out-of-range kind -> LABEL_5 (v8==0)
    }

    ParticleSystem* sys =
        AllocSystem(owner, texName, texSlot, life, userTag, updateFn, slotCount, nowTick);
    if (!sys)                                   // 0x5e3b1d  if (!v8) return v8
        return nullptr;

    // 0x5e3b29 — VIBE_Object_SetPosition(system+232, position).
    // 0x5e3b33 — qmemcpy(system+44, template, 0xA4): copy the 164-byte emitter
    // template into the system. The fields land at their system offsets
    // (template+0xA0 == system+0xCC flagByte0, +0xA1 == flagByte1, etc.).
    sys->flagByte0 = tmpl.bytes[0xA0];          // system+204 <- template+0xA0
    sys->flagByte1 = tmpl.bytes[0xA1];          // system+205 <- template+0xA1

    // 0x5e3b5a/0x5e3b65/0x5e3b74 — BuildBasisFromAngle + MatrixToEuler +
    // SetWorldTranslation: place the system at `position` (the basis math only
    // affects orientation, not the world translation we observe).
    g_spawnHooks.placeObject(sys, position);

    // 0x5e3b80 — if (flagByte1 & 1) { spawnedCount = 0; flagByte1 |= 2; }
    if (sys->flagByte1 & 1) {
        sys->spawnedCount = 0;                  // system+32 = 0
        sys->flagByte1 |= 2;                    // raise trigger-pending bit
    }
    return sys;
}

// =============================================================================
// 0x43fd24 — VIBE_Particle_CreateEmitter
//
// Zero the 0xA4 template, write the recovered defaults, copy the emitter kind
// into template+0, pack (userType, trigger) into the userTag bits, then tail-call
// SpawnSystemByType. The template = byte_765320 (filled in place via `tmpl`).
//
// Tail call mapping (from the original):
//   SpawnSystemByType(*texSlotPtr, &flt_5CA2E0(=0,0,0), *texNamePtr, *ownerPtr,
//                     v19, (float)*ampPtr, byte_765320)
// =============================================================================
ParticleSystem* CreateEmitter(const u8* kindPtr, const i32* ampPtr,
                              const int* texSlotPtr, const int* ownerPtr,
                              const u8** texNamePtr, const u8* userTypePtr,
                              const u8* triggerPtr, const float position[3],
                              EmitterTemplate& tmpl, int slotCount, u32 nowTick) {
    // 0x43fd38..0x43fd57 — memset the 164-byte template to zero (the original
    // unrolls the byte/dword fill; net effect = zero-fill of 0xA4 bytes).
    std::memset(tmpl.bytes, 0, sizeof(tmpl.bytes));

    // Recovered default literals (template-relative offsets; see header). Float
    // bit-patterns stored as dwords by the original are written as the floats.
    tmpl.at<float>(0x40) = kCeAmpX;             // flt_765360 = flt_5CA2D0 (0.0)
    tmpl.at<float>(0x34) = 10.0f;               // dword_765354 = 0x41200000
    tmpl.at<float>(0x38) = 50.0f;               // dword_765358 = 0x42480000
    tmpl.at<float>(0x3C) = 30.0f;               // dword_76535C = 0x41F00000
    tmpl.at<float>(0x44) = kCeOne;              // flt_765364 = flt_5CA2D4 (1.0)
    tmpl.at<float>(0x48) = kCeZero;             // flt_765368 = flt_5CA2D8 (0.0)
    tmpl.bytes[0x00]     = *kindPtr;            // byte_765320[0] = *a1
    tmpl.at<float>(0x80) = 1.0f;                // dword_7653A0 = 0x3F800000
    tmpl.at<i32>(0x98)   = 180;                 // dword_7653B8 = 180
    tmpl.at<float>(0x8C) = 255.0f;              // dword_7653AC = 0x437F0000
    tmpl.bytes[0x9E]     = 0xFF;                // byte_7653BE = -1
    tmpl.bytes[0x9D]     = 0xFF;                // byte_7653BD = -1
    tmpl.bytes[0x9C]     = 0xFF;                // byte_7653BC = -1
    tmpl.at<i32>(0x90)   = 20;                  // dword_7653B0 = 20
    tmpl.at<i32>(0x94)   = 160;                 // dword_7653B4 = 160
    tmpl.bytes[0x9F]     = 0;                   // byte_7653BF = 0

    // 0x43fe00..0x43fe3a — build v19 (the packed userTag passed as a5):
    //   v16 = *triggerPtr & 1;
    //   LOBYTE(v19) = *userTypePtr;
    //   BYTE2(v19) = v16 | (BYTE2(v19) & 0xFE) | 2;   (v19 starts indeterminate;
    //     BYTE2 high bits are masked, so only bits 0/1 of BYTE2 matter: 0|2|trigger)
    //   byte_7653C0 |= 0x20;   (template+0xA0, system flagByte0 bit5 = initFill)
    const u8 trigger = static_cast<u8>(*triggerPtr & 1);
    u32 v19 = 0;
    reinterpret_cast<u8*>(&v19)[0] = *userTypePtr;          // LOBYTE
    reinterpret_cast<u8*>(&v19)[2] = static_cast<u8>(trigger | 2); // BYTE2: bit1|trigger
    tmpl.bytes[0xA0] |= 0x20u;                  // byte_7653C0 |= 0x20

    // v18 = (double)*ampPtr -> float life argument.
    const float life = static_cast<float>(*ampPtr);

    // 0x43fe5f — tail call.
    return SpawnSystemByType(*texSlotPtr, position, *texNamePtr, *ownerPtr,
                             static_cast<int>(v19), life, tmpl, slotCount, nowTick);
}

// =============================================================================
// 0x5e1228 — VIBE_Particle_SetPosition
// =============================================================================
ParticleSystem* SetPosition(ParticleSystem* sys, const float pos[3]) {
    if (sys)                                    // 0x5e122a
        g_spawnHooks.placeObject(sys, pos);     // VIBE_Object_SetPosition(sys+232,pos)
    return sys;                                 // 0x5e122c/0x5e1232
}

// =============================================================================
// 0x43fb88 — VIBE_Particle_KillParticle
// =============================================================================
int KillParticle(void** handle) {
    if (g_killHooks.isValid(*handle))           // 0x43fb8d
        g_killHooks.freeNode(*handle);          // VIBE_Render_FreeObjectNode
    else
        g_killHooks.report("ecmd_KillParticle: invalid handle..."); // 0x43fbb2
    return 0;                                    // 0x43fba0
}

// =============================================================================
// 0x43fe68 — VIBE_Particle_KillEmitter
// =============================================================================
int KillEmitter(void** handle) {
    if (g_killHooks.isValid(*handle))           // 0x43fe6d
        g_killHooks.freeNode(*handle);          // VIBE_Render_FreeObjectNode
    else
        g_killHooks.report("KillEmitter: invalid handle...");       // 0x43fe92
    return 0;                                    // 0x43fe80
}

// =============================================================================
// DEFERRED (not translated here, listed per AGENT_GUIDE "no silent skips"):
//   0x5e1e0c VIBE_Particle_UpdatePoints   — ~700 insns, ~99% x87 inline-asm over
//   0x5e2814 VIBE_Particle_UpdatePolys      engine globals (flt_62BA94 family,
//   0x5e32c0 VIBE_Particle_UpdateLens       Fmod, VectorNormalize, Coord_ConvertX)
// These per-frame INTEGRATORS read the slots AllocSystem primes here. A faithful
// 1:1 translation requires modelling the full 0x310 system layout and the
// fsin/fmod basis pipeline; the 7 named emitter-update kernels in particle.cpp
// (UpdateEmitter/SeedParticles/UpdateTrail/UpdateGravity/...) already cover the
// integrate-step golden-vector surface, so these are deferred to avoid a
// half-faithful asm transliteration.
// =============================================================================

} // namespace guild::render
