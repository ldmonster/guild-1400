#include "io/save_relink.h"

#include <cstring>

namespace guild::io {

namespace {

// Read the object record's fields at the recovered offsets. memcpy keeps the
// unaligned reads (id @+1, actor @+97) faithful and UB-free.
guild::u8 ObjAlive(const guild::u8* rec) {
    return rec[kRelinkObjAliveOff];
}
guild::u32 ObjId(const guild::u8* rec) {
    guild::u32 v;
    std::memcpy(&v, rec + kRelinkObjIdOff, 4);
    return v;
}
guild::u32 ObjActor(const guild::u8* rec) {
    guild::u32 v;
    std::memcpy(&v, rec + kRelinkObjActorOff, 4);
    return v;
}

// The object-array pass shared by both functions: for every populated record whose
// alive byte is non-zero and whose actor back-pointer is non-zero, write `valueFn`
// into the actor's +512 slot. `byOffset` selects the id (+1) vs the record offset.
// Mirrors the original's `for (i = 0; i != 43264; i += 169)` walk exactly.
template <class ValueFn>
void ObjectPass(const RelinkPersonEnv& env, ValueFn valueFn) {
    if (!env.objectBase || !env.writeActorBack)
        return;
    const int bound = kRelinkObjScanBound;                 // 43264
    int idx = 0;
    for (int off = 0; off != bound; off += kRelinkObjStride) {
        if (idx >= env.objectCount)
            break;
        const guild::u8* rec = env.objectBase + off;
        if (ObjAlive(rec)) {                               // if (*(byte)v6)
            guild::u32 actor = ObjActor(rec);             // *(dword)(v6+97)
            if (actor)                                    // if (v4)
                env.writeActorBack(actor, valueFn(rec, static_cast<guild::u32>(off)),
                                   env.ctx);
        }
        ++idx;
    }
}

} // namespace

// ---------------------------------------------------------------------------
// gilde.exe 0x5a3d8c — VIBE_Save_RelinkPersonObjects.
void SaveRelinkPersonObjects(const RelinkPersonEnv& env, guild::u8* relinkTable) {
    // v3 = dword_649D60; SwitchActiveSlot(0, 1)
    int saved = env.getActiveSlot ? env.getActiveSlot(env.ctx) : 0;
    if (env.switchActiveSlot) env.switchActiveSlot(0, env.ctx);

    // Object pass: actor+512 = object id (+1).
    ObjectPass(env, [](const guild::u8* rec, guild::u32) { return ObjId(rec); });

    // SwitchActiveSlot(v3, 1)
    if (env.switchActiveSlot) env.switchActiveSlot(saved, env.ctx);

    // Relink-table pass: pointer -> id, tag-dispatched (member read off the pointer).
    if (!relinkTable)
        return;
    for (guild::u32 n = 0; n != kRelinkBytes; n += kRelinkStride) {
        guild::u32 ptr;
        std::memcpy(&ptr, relinkTable + n + kRelinkPtrOffset, 4); // *(table+n+6)
        if (ptr == 0)                                             // if (v8)
            continue;
        guild::u8 tag = relinkTable[n + kRelinkTagOffset];        // byte_B5FB61[n]
        guild::u32 id = ptr;
        switch (tag) {
        case kRelinkPerson:                                       // tag 1 -> *(ptr+4)
            if (env.ptrToIdPerson)   id = env.ptrToIdPerson(ptr, env.ctx);
            break;
        case kRelinkObject:                                       // tag 3 -> *(ptr+2)
            if (env.ptrToIdObject)   id = env.ptrToIdObject(ptr, env.ctx);
            break;
        case kRelinkBuilding:                                     // tag 2 -> *(ptr+1)
            if (env.ptrToIdBuilding) id = env.ptrToIdBuilding(ptr, env.ctx);
            break;
        case kRelinkCutscene:                                     // tag 9 -> *(ptr+0)
            if (env.ptrToIdCutscene) id = env.ptrToIdCutscene(ptr, env.ctx);
            break;
        default:
            break;
        }
        std::memcpy(relinkTable + n + kRelinkPtrOffset, &id, 4);
    }
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5a3f14 — VIBE_Save_RelinkPersonExtraData.
int SaveRelinkPersonExtraData(const RelinkPersonEnv& env, int sceneArg) {
    // v5 = dword_649D60; SwitchActiveSlot(0, 1)
    int saved = env.getActiveSlot ? env.getActiveSlot(env.ctx) : 0;
    if (env.switchActiveSlot) env.switchActiveSlot(0, env.ctx);

    // Object pass #1: actor+512 = object id (+1).
    ObjectPass(env, [](const guild::u8* rec, guild::u32) { return ObjId(rec); });

    // VIBE_WorldIo_SaveSceneObjects(0, a3)
    if (env.saveSceneObjects) env.saveSceneObjects(sceneArg, env.ctx);

    // Object pass #2: actor+512 = object record ADDRESS (the offset token here).
    ObjectPass(env, [](const guild::u8*, guild::u32 off) { return off; });

    // SwitchActiveSlot(v5, 1)
    if (env.switchActiveSlot) env.switchActiveSlot(saved, env.ctx);
    return 1;
}

} // namespace guild::io
