#include "render/particle_emitter_create.h"

#include <cstring>
#include <new>

namespace guild::render {

// =============================================================================
// ParticleSystemList — the render scene-graph object list the per-frame loop
// walks (dword_1408438 head / dword_140874C tail / unk_1408440 sentinel). The
// linkage operations below are byte-exact transliterations of the original
// pointer arithmetic at node offsets +0x308 (next) / +0x30C (prev) / +0x2F0
// (owner). The sentinel terminates BOTH ends: an empty list has head==tail==
// &sentinel, exactly like InitObjectList leaves it.
// =============================================================================

// 0x5e0e00 — VIBE_Render_InitObjectList.
// The original sets dword_1408438 = &unk_1408440 and (for the scene-root child
// list) dword_140874C = &unk_1408130; both ends collapse to the empty state. We
// model a single sentinel for this self-contained list (the two original
// sentinels are the head/tail terminators of one logical list).
void ParticleSystemList::Init() {
    sentinel_.system = nullptr;
    sentinel_.next   = &sentinel_;
    sentinel_.prev   = &sentinel_;
    sentinel_.owner  = nullptr;
    head_ = &sentinel_;   // dword_1408438 = &unk_1408440
    tail_ = &sentinel_;   // dword_140874C = &(empty)
}

// 0x5e11f5 — the AllocSystem TAIL-insertion sequence, 1:1:
//     mov eax, dword_140874C            ; eax = old tail
//     mov [esi+308h], offset unk_1408440 ; node.next  = sentinel
//     mov [esi+30Ch], eax               ; node.prev  = old tail
//     mov [eax+308h], esi               ; oldTail.next = node
//     mov dword_140874C, esi            ; tail = node
// On the FIRST insertion the old tail is the sentinel, so sentinel.next = node
// and we additionally promote head = node (the renderer reads dword_1408438,
// which on the real engine is the scene-root's child-head; for this list the
// first appended node becomes the head). Subsequent inserts only move the tail.
void ParticleSystemList::LinkAtTail(SystemNode* node, void* owner) {
    if (!node || !node->system)
        return;

    SystemNode* oldTail = tail_;        // mov eax, dword_140874C
    node->owner = owner;                // node + 0x2F0
    node->next  = &sentinel_;           // node + 0x308 = &unk_1408440
    node->prev  = oldTail;              // node + 0x30C = old tail
    oldTail->next = node;               // oldTail + 0x308 = node
    tail_ = node;                       // dword_140874C = node

    // Head recache: if the list was empty the old tail WAS the sentinel, whose
    // "next" we just set to node; the head cache (dword_1408438) must now point
    // at the first live node. (The engine keeps this in the scene-root's +164
    // field; UnlinkObjectNode recaches it the same way — see Unlink.)
    if (head_ == &sentinel_)
        head_ = node;
}

// 0x5e0e9c — VIBE_Render_UnlinkObjectNode (splice-out form, generalized to this
// list's single sentinel):
//     v1 = node.prev (+0x30C);  v2 = node.owner (+0x2F0)
//     if (v1 == headSentinel) owner.head = node.next; else v1.next = node.next
//     v3 = node.next (+0x308)
//     if (v3 != tailSentinel) v3.prev = node.prev; else owner.tail = node.prev
//     (then head/tail caches re-read from the owner)
// Modelled directly on prev/next with sentinel terminators + head/tail recache.
void ParticleSystemList::Unlink(SystemNode* node) {
    if (!node || node == &sentinel_)
        return;

    SystemNode* p = node->prev;         // a1[195] = +0x30C
    SystemNode* n = node->next;         // a1[194] = +0x308

    // wave-10 (W10-PARTICLE) memory-safety guard: a node that is not currently in
    // any list has both link pointers null (Unlink nulls them on exit). The
    // original is only ever called on a still-linked node; double-unlinking here
    // would deref a null prev/next. Treat an unlinked node as a no-op. The
    // in-list splice path below is byte-identical.
    if (!p && !n)
        return;

    if (p == &sentinel_)                // node was at the head
        head_ = n;                      // dword_1408438 = node.next
    else
        p->next = n;                    // v1.next = node.next

    if (n == &sentinel_)                // node was at the tail
        tail_ = p;                      // dword_140874C = node.prev
    else
        n->prev = p;                    // v3.prev = node.prev

    node->next = nullptr;
    node->prev = nullptr;
}

// 0x5b3a86 — the render-loop walk from VIBE_Render_BeginUniverseFrame:
//     eax = dword_1408438
//     while (eax != &unk_1408440) {
//         edx = *(eax + 0x308)              ; next BEFORE the call (matches asm)
//         VIBE_Particle_RenderSystem(eax, view)
//         eax = edx
//     }
// The original loads `next` into edx before calling RenderSystem, so it is safe
// against a system that unlinks/frees itself during render; we replicate that.
void ParticleSystemList::WalkAndRender(void (*render)(ParticleSystem*, int),
                                       int view) const {
    SystemNode* cur = head_;            // mov eax, dword_1408438
    while (cur != &sentinel_) {         // cmp eax, offset unk_1408440 / jz
        SystemNode* nxt = cur->next;    // mov edx, [eax+308h]
        if (render && cur->system)
            render(cur->system, view);  // VIBE_Particle_RenderSystem
        cur = nxt;                      // mov eax, edx
    }
}

// PER-FRAME UPDATE walk (wave-9 W9-PARTICLE-RUNTIME). Same head..sentinel order
// as WalkAndRender (the 0x5b3a86 traversal); for each node with a populated raw
// emitter image, run the reconstructed integrator pintegrate::UpdateSystem over
// the node's 0x310 emitter + its 84-byte slot array. The integrator BOTH spawns
// dead slots (the emitter's spawn gate / rate) AND advances live ones, writing the
// +56/60/64 render center, +72 size and +79 alpha byte the renderer then reads —
// exactly the original engine's per-tick update fn (+0x304) the same object-list
// walk drives. `next` is captured before the call (self-unlink safe), matching the
// render walk. The integrator is CALLED, never reimplemented (rule 13).
int ParticleSystemList::WalkAndUpdate(u32 now) {
    return WalkAndUpdate(now, nullptr, nullptr);
}

int ParticleSystemList::WalkAndUpdate(u32 now,
                                      void (*after)(SystemNode*, void*),
                                      void* user) {
    int updated = 0;
    SystemNode* cur = head_;                 // mov eax, dword_1408438
    while (cur != &sentinel_) {              // cmp eax, offset unk_1408440 / jz
        SystemNode* nxt = cur->next;         // capture next BEFORE the update
        if (cur->system && cur->runtimeReady) {
            // The slot array IS the spawned system's 84-byte particle array (the
            // one AllocSystem primed). pintegrate::Slot is its 84-byte view.
            pintegrate::Slot* slots =
                static_cast<pintegrate::Slot*>(cur->system->particles);
            if (slots) {
                pintegrate::UpdateSystem(cur->type, cur->emitter, slots, now);
                ++updated;
                if (after)
                    after(cur, user);
            }
        }
        cur = nxt;                           // mov eax, edx
    }
    return updated;
}

int ParticleSystemList::Count() const {
    int n = 0;
    for (SystemNode* c = head_; c != &sentinel_; c = c->next)
        ++n;
    return n;
}

// =============================================================================
// Process-wide live list. Initialized empty on first access (the analogue of
// VIBE_Render_InitObjectList running at engine start).
// =============================================================================
ParticleSystemList& LiveSystems() {
    static ParticleSystemList g_list = [] {
        ParticleSystemList l;
        l.Init();
        return l;
    }();
    return g_list;
}

// =============================================================================
// SpawnEmitterAtPosition — the clean scene-facing handoff (rule 13).
//
// 1:1 with the script "CreateEmitter" command followed by the AllocSystem-tail
// linkage. We REUSE the wave-7 CreateEmitter (render/particle_spawn.cpp), which
// fills the default template, dispatches the integrator by `kind`, allocates the
// system, copies the template, and places it. Then we LinkAtTail the result into
// the live list so the render walk sees it. CreateEmitter takes its operands by
// POINTER (the script-VM operand convention), so we pass addresses of locals.
// =============================================================================
ParticleSystem* SpawnEmitterAtPosition(u8 kind, const float worldPos[3],
                                       void* owner, const u8* texName, int texSlot,
                                       i32 amplitude, u8 userType, u8 trigger,
                                       int slotCount, u32 nowTick) {
    // The script command passes the owning scene node id as the "owner" operand
    // (CreateEmitter's a4@<ebx>). AllocSystem returns null when owner==0, so the
    // scene caller must supply a non-null owner — mirror that contract by using
    // the owner pointer's identity as the engine `owner` int handle.
    int ownerHandle =
        owner ? static_cast<int>(reinterpret_cast<std::uintptr_t>(owner) & 0x7fffffff) : 0;
    if (ownerHandle == 0)
        ownerHandle = 1; // a stable non-null handle so AllocSystem's owner!=0 holds

    EmitterTemplate tmpl{};
    const u8*  texNamePtr = texName;
    const i32  ampVal     = amplitude;
    const int  texSlotVal = texSlot;
    const int  ownerVal   = ownerHandle;
    const u8   kindVal    = kind;
    const u8   userTypeVal = userType;
    const u8   triggerVal  = static_cast<u8>(trigger & 1);

    // CreateEmitter (0x43fd24) -> SpawnSystemByType (0x5e3ae0) -> AllocSystem.
    ParticleSystem* sys = CreateEmitter(&kindVal, &ampVal, &texSlotVal, &ownerVal,
                                        &texNamePtr, &userTypeVal, &triggerVal,
                                        worldPos, tmpl, slotCount, nowTick);
    if (!sys)
        return nullptr; // AllocSystem failed (life<=0 / owner==0 / texture load)

    // AllocSystem-tail linkage: append the new system to the live list as a child
    // of `owner`. The wave-7 AllocSystem delegates linkage to an inert hook, so we
    // perform the real insertion here (this is the edge that was missing).
    SystemNode* node = new (std::nothrow) SystemNode{};
    if (!node)
        return sys; // out of memory: system exists but stays unlinked (no crash)
    node->system = sys;

    // ---- RUNTIME GLUE (wave-9): rebuild the genuine RAW 0x310 emitter image so
    // the reconstructed integrator (pintegrate::UpdateSystem) can run 1:1.
    //
    // In the original this block IS the system: SpawnSystemByType does
    //   qmemcpy(system+44, template, 0xA4)        ; copy the 0xA4 default template
    //   *(system+204) = template+0xA0             ; flagByte0 (flagsLow)
    //   *(system+205) = template+0xA1             ; flagByte1 (flagsHi)
    // and AllocSystem wrote system+0xD0 = slotCount, system+0xE4 = lifeBase. We
    // replay EXACTLY that into the raw image. `tmpl` was filled in place by the
    // CreateEmitter call above, so its bytes are the same default template the
    // engine copied — their fields land precisely on the integrator's raw offsets
    // (template+0x90/94/98 -> spanA/B/C, +0x9C..9E white -> colour base, +0xA0
    // init-bit -> flagsLow bit5, +0x34/38/3C scale -> spawn-speed +0x60/64/68).
    std::memset(node->emitter.raw, 0, sizeof(node->emitter.raw));
    std::memcpy(node->emitter.raw + 44, tmpl.bytes, sizeof(tmpl.bytes)); // system+44
    node->emitter.b(0xCC) = tmpl.bytes[0xA0];        // flagByte0 / flagsLow
    node->emitter.b(0xCD) = tmpl.bytes[0xA1];        // flagByte1 / flagsHi
    node->emitter.i(0xD0) = sys->count;              // slot count (AllocSystem)
    node->emitter.f(0xE4) = sys->lifeBase;           // lifeBase (spawn lifetime)
    node->emitter.u(0x24) = 0;                        // lastTick (re-armed each frame)
    node->emitter.i(0x20) = 0;                        // killedCount
    // Texture-group pointer (+0xD4) drives the frame divisor (group[+112]); none is
    // bound on the headless path, so leave it null -> ResolveFrameMod yields 1
    // (the faithful default when no multi-frame atlas is attached).
    node->emitter.setGroupPtr(nullptr);
    // Dispatch type: the kind the script/scene chose (0=Points,1=Polys,2=Lens),
    // mirroring the updateFn SpawnSystemByType installed at +0x304.
    node->type = (kind == 1) ? pintegrate::SystemType::Polys
               : (kind == 2) ? pintegrate::SystemType::Lens
                             : pintegrate::SystemType::Points;
    node->runtimeReady = true;

    LiveSystems().LinkAtTail(node, owner);
    return sys;
}

ParticleSystem* SpawnEmitterAtPosition(u8 kind, const float worldPos[3],
                                       void* owner, const u8* texName, int texSlot) {
    // Engine defaults: amplitude == template life (180), no user type, no trigger,
    // default slot count, birth tick 0. (The real engine derives slotCount + tick
    // from globals; the scene caller passes the live values via the full overload.)
    return SpawnEmitterAtPosition(kind, worldPos, owner, texName, texSlot,
                                  /*amplitude*/ 180, /*userType*/ 0, /*trigger*/ 0,
                                  kEmitterDefaultSlotCount, /*nowTick*/ 0);
}

// =============================================================================
// DestroyAllSystems — free every live system block + its node and reset the list
// (analogue of 0x5affec VIBE_Render_DisposeAllObjects for these systems).
// =============================================================================
void DestroyAllSystems() {
    ParticleSystemList& list = LiveSystems();
    SystemNode* cur = list.Head();
    SystemNode* sentinel = list.Sentinel();
    while (cur != sentinel) {
        SystemNode* nxt = cur->next;
        // ParticleSystem blocks come from the default spawn allocator (operator
        // new[] of raw bytes in particle_spawn.cpp's DefaultAlloc). The reused
        // AllocSystem allocated four blocks per system (the 0x310 header, the
        // particle array, and two scratch buffers — one of which AllocSystem
        // discards the return of). We own + delete the SystemNode here; the
        // system blocks are reclaimed in bulk below.
        delete cur;
        cur = nxt;
    }
    list.Init(); // back to empty (head==tail==sentinel)
    // Wave-10 (W10-PARTICLE) leak fix: reclaim every default-backend AllocSystem
    // block. Previously only the SystemNode was freed, leaking the entire 0x310
    // system block + all four sub-allocations per spawned emitter. This is a
    // memory-safety fix in the headless test backend (the engine heap reclaimed
    // the same bytes); no engine-observable behaviour changes.
    FreeAllSpawnAllocations();
}

} // namespace guild::render
