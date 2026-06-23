#pragma once
#include "guild/common/types.h"
#include "render/particle_spawn.h"
#include "render/particle_integrate.h"  // pintegrate::Emitter / Slot / UpdateSystem

// =============================================================================
// guild::render — LIVE particle-system creation + scene linkage (wave-8).
//
// The wave-6/7 cluster reconstructed the EMITTER TEMPLATE fill + system
// allocation + per-type integrator dispatch (render/particle_spawn.{h,cpp}:
// CreateEmitter 0x43fd24, SpawnSystemByType 0x5e3ae0, AllocSystem 0x5e1000) and
// the per-system RENDER walk (fx_recon3 / particle_render). But nothing in the
// reconstruction ever LINKS a freshly-spawned system into the live list that the
// per-frame render loop walks, so the city stays empty of chimney smoke /
// fountains / effects. This module reconstructs exactly that missing edge: the
// scene-graph object list the spawn path links into and the render loop walks.
//
// THE LIVE LIST (recovered 1:1)
// ---------------------------------------------------------------------------
// gilde.exe keeps every live particle SYSTEM as a node of the render scene-graph
// object list anchored at the scene-root object `off_649D64` and CACHED in two
// module globals:
//
//   dword_1408438  HEAD  (renderer walks from here)            @0x1408438
//   dword_140874C  TAIL  (AllocSystem inserts here)            @0x140874C
//   unk_1408440    SENTINEL (one-past-the-end terminator)      @0x1408440
//
// Each system block (the 0x310-byte ParticleSystem header) carries the list
// pointers at fixed byte offsets:
//
//   node + 0x2F0 (752)  owner   — the parent scene node (== off_649D64 for the
//                                 root-parented systems the render loop walks)
//   node + 0x308 (776)  next    — toward the sentinel
//   node + 0x30C (780)  prev    — toward the head
//
// Recovered from:
//   0x5e0e00 VIBE_Render_InitObjectList      — head=&sentinel, tail=&otherSentinel
//   0x5e11f5 VIBE_Particle_AllocSystem(tail) — the exact insertion sequence:
//       node.next  = &unk_1408440            ; [esi+308h] = offset unk_1408440
//       node.prev  = dword_140874C           ; [esi+30Ch] = old tail
//       oldTail.next = node                  ; [eax+308h] = esi
//       dword_140874C = node                 ; tail = node
//   0x5e0e9c VIBE_Render_UnlinkObjectNode    — splice-out + head/tail recache
//   0x5b3a86 VIBE_Render_BeginUniverseFrame  — the walk:
//       eax = dword_1408438
//       while (eax != &unk_1408440) {
//           VIBE_Particle_RenderSystem(eax, view);   // 0x5e1278
//           eax = *(node + 0x308);                    // next
//       }
//
// THE SCENE -> EMITTER HANDOFF (rule 13)
// ---------------------------------------------------------------------------
// Emitters are spawned by the script command "CreateEmitter" (registered at
// 0x440a6c by VIBE_Script_RegisterObjectCommands, 7 args:
//   [kind, amplitude, texSlot, owner, texName(string), userType, trigger]).
// Building / chimney / fountain object scripts invoke it; CreateEmitter fills the
// default template, tail-calls SpawnSystemByType -> AllocSystem, and AllocSystem
// links the new system as a CHILD of the owning scene node. The placement passed
// in the script path is &flt_5CA2E0 == {0,0,0} (verified via get_bytes), i.e. the
// emitter sits at the owner's origin and inherits the owner's WORLD transform —
// a chimney emitter rides its building. CityView3D / scene-load therefore spawn
// emitters by running the object scripts that call this command; the only thing
// missing was the LIST LINKAGE, which this module supplies.
//
// This module OWNS the list + a clean scene-facing spawn entry. It REUSES the
// wave-7 CreateEmitter / SpawnSystemByType / AllocSystem (does not redefine them).
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// Live particle-system list. Reconstructs the head/tail/sentinel + the +0x308 /
// +0x30C / +0x2F0 linkage of the render scene-graph object list. Because the
// reconstructed ParticleSystem header (particle_spawn.h) does not model the raw
// list pointers, this list holds them in a parallel intrusive node that owns the
// system pointer 1:1 with the original's in-block fields.
// ---------------------------------------------------------------------------
struct SystemNode {
    ParticleSystem* system = nullptr; // the 0x310 block this node represents
    SystemNode*     next   = nullptr; // node + 0x308 (toward sentinel)
    SystemNode*     prev   = nullptr; // node + 0x30C (toward head)
    void*           owner  = nullptr; // node + 0x2F0 (parent scene node)

    // ---- RUNTIME GLUE (wave-9 W9-PARTICLE-RUNTIME) -------------------------
    // In the ORIGINAL engine a particle system is ONE 0x310 block that serves
    // BOTH the spawn path (CreateEmitter/AllocSystem fill it) AND the per-frame
    // integrator (UpdatePoints/Polys/Lens read it). The reconstruction split it
    // into a COMPACT typed `ParticleSystem` (particle_spawn.h, 88 bytes, members
    // at packed offsets) and the integrator's RAW 0x310 `pintegrate::Emitter`
    // (particle_integrate.h, fields read at their true byte offsets). The two are
    // therefore NOT reinterpret-compatible. To run the reconstructed integrator
    // 1:1 we rebuild the genuine RAW 0x310 image here: the CreateEmitter default
    // template copied at +44 (its filled fields land EXACTLY on the integrator's
    // raw offsets — template+0x34 scale -> emitter+0x60 spawn-speed, template+0x90
    // -> +0xBC spanA, template+0x94 -> +0xC0 spanB, template+0x98 -> +0xC4 spanC,
    // template+0x8C -> +0xB8 alpha-bias, template+0x9C/9D/9E white -> +0xC8/C9/CA
    // colour base, template+0xA0 init-bit -> +0xCC flagsLow, exactly as the
    // original SpawnSystemByType's `qmemcpy(system+44, template, 0xA4)` does),
    // plus slotCount(+0xD0), lifeBase(+0xE4) and the texture-group ptr(+0xD4)
    // written by AllocSystem. The integrator's SLOT array IS the very same 84-byte
    // record array AllocSystem allocated (ParticleSystem::particles): pintegrate::
    // Slot and the renderer's fxrecon3::ParticleSlot are both 84-byte views of it.
    // So: integrate writes the +56/60/64 center + +72 size + +79 alpha into THIS
    // array, and the render walk reads the SAME array -> particles become visible.
    pintegrate::Emitter        emitter{};        // the rebuilt raw 0x310 image
    pintegrate::SystemType     type = pintegrate::SystemType::Points; // dispatch
    bool                       runtimeReady = false; // emitter image populated
};

class ParticleSystemList {
public:
    // 0x5e0e00 — VIBE_Render_InitObjectList: head = tail = &sentinel (empty).
    void Init();

    // 0x5e11f5 — the AllocSystem TAIL insertion, byte-exact:
    //   node.next = &sentinel; node.prev = oldTail; oldTail.next = node;
    //   tail = node.  `owner` is written into node+0x2F0 (the parent scene node).
    // Takes ownership of `node` (caller allocated it). No-op if node/system null.
    void LinkAtTail(SystemNode* node, void* owner);

    // 0x5e0e9c — VIBE_Render_UnlinkObjectNode: splice the node out, recache
    // head/tail when the node sat at an end. Does not free the node.
    void Unlink(SystemNode* node);

    // 0x5b3a86 — the render-loop walk. For every live node (head..!=sentinel,
    // following next), invoke `render(system, view)`. Faithful to the order and
    // the next-pointer traversal of VIBE_Render_BeginUniverseFrame.
    void WalkAndRender(void (*render)(ParticleSystem* sys, int view), int view) const;

    // PER-FRAME UPDATE walk (wave-9). In the original the SAME object-list walk
    // the renderer uses also drives each system's installed update fn (+0x304,
    // one of UpdatePoints/Polys/Lens) once per tick BEFORE the system is rendered
    // (the integrator computes the +56/60/64 center + +79 alpha the renderer
    // reads). The reconstruction's integrator lives in pintegrate::UpdateSystem;
    // here we walk the live list (head..sentinel, the 0x5b3a86 order) and run that
    // reconstructed integrator over each node's raw 0x310 emitter image + its
    // shared 84-byte slot array. `now` is the frame tick (the @<edx> argument).
    // Returns the number of systems updated. Nodes whose emitter image was not
    // populated (runtimeReady == false) are skipped (a system spawned through a
    // path that did not build the raw image stays inert, never faked).
    int WalkAndUpdate(u32 now);

    // Variant exposing the per-system callback (parity with WalkAndRender). The
    // callback is invoked AFTER the integrator runs, so it sees the freshly
    // integrated slots. Used by the host frame to drive integrate-then-render in
    // a single pass when desired.
    int WalkAndUpdate(u32 now, void (*after)(SystemNode* node, void* user), void* user);

    // Observability / introspection (the head/tail/sentinel caches).
    SystemNode* Head() const { return head_; }
    SystemNode* Tail() const { return tail_; }
    SystemNode* Sentinel() const { return const_cast<SystemNode*>(&sentinel_); }
    int Count() const;
    bool Empty() const { return head_ == &sentinel_; }

private:
    SystemNode  sentinel_{};            // unk_1408440 (one-past-the-end)
    SystemNode* head_ = &sentinel_;     // dword_1408438
    SystemNode* tail_ = &sentinel_;     // dword_140874C
};

// The process-wide live list (the analogue of the dword_1408438/140874C globals).
// The render loop walks THIS; the spawn entry below links INTO it.
ParticleSystemList& LiveSystems();

// ---------------------------------------------------------------------------
// Recovered ENGINE-DEFAULT emitter parameters (the values CreateEmitter writes
// when a script spawns an emitter — see particle_spawn.cpp). Exposed so the
// scene-facing entry below and its golden tests use the exact same constants.
//
//   amplitude(life arg)  : supplied by caller (the script "amplitude" operand)
//   userType / trigger   : supplied by caller
//   template life field  : 180     (dword_7653B8)
//   scale x/y/z          : 10/50/30 (dword_765354/358/35C)
//   colour               : white   (byte_7653BC/BD/BE = 0xFF)
//   alpha-over-time       : 255.0   (dword_7653AC)
//   initFill bit          : set     (byte_7653C0 |= 0x20)
// The placement default (script path) is the owner-local origin {0,0,0}.
// ---------------------------------------------------------------------------
constexpr float kEmitterDefaultOriginLocal[3] = {0.0f, 0.0f, 0.0f}; // flt_5CA2E0

// Slot count / frame-tick the original derives from engine globals at spawn
// (the particle array size and the birth-tick stamp). Surfaced as parameters so
// the create is testable without the engine clock; the scene caller passes the
// real values.
constexpr int kEmitterDefaultSlotCount = 32; // documented default; caller overrides

// ---------------------------------------------------------------------------
// CLEAN SCENE-FACING ENTRY (the de-inert handoff).
//
// Create a live particle SYSTEM of `kind` (0=points, 1=polys, 2=lens) at world
// position `worldPos`, with the engine-default emitter params, and LINK it into
// the live system list the render loop walks. Returns the created system (null if
// AllocSystem failed: life<=0, owner==0, or texture-load failed — same contract
// as SpawnSystemByType).
//
// This is the entry CityView3D / scene-load calls to spawn a chimney/fountain/
// effect: it runs the same CreateEmitter default-fill the script command runs,
// then performs the AllocSystem-tail linkage (LinkAtTail) so the system becomes
// visible to the render walk. `owner` is the parent scene node (the building /
// chimney node) recorded at node+0x2F0; `texName`/`texSlot` select the particle
// texture; `amplitude` is the life magnitude; `slotCount`/`nowTick` are the
// engine-derived array size + birth tick.
//
// NB: ownership — the returned system + its SystemNode are owned by the live
// list; DestroyAllSystems() (below) frees them, mirroring
// VIBE_Render_DisposeAllObjects.
ParticleSystem* SpawnEmitterAtPosition(u8 kind, const float worldPos[3],
                                       void* owner, const u8* texName, int texSlot,
                                       i32 amplitude, u8 userType, u8 trigger,
                                       int slotCount, u32 nowTick);

// Convenience overload using the documented engine defaults (slotCount, tick=0,
// amplitude defaulting to the template life). Equivalent to the script path's
// default-parameter spawn at a world position.
ParticleSystem* SpawnEmitterAtPosition(u8 kind, const float worldPos[3],
                                       void* owner, const u8* texName, int texSlot);

// Free every live system + its node and reset the list to empty (analogue of
// 0x5affec VIBE_Render_DisposeAllObjects for the particle systems this module
// created). Safe to call repeatedly.
void DestroyAllSystems();

} // namespace guild::render
