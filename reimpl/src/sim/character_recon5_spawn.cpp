// character_recon5_spawn — see header for provenance / layout.
#include "character_recon5_spawn.h"

#include <cstring>

namespace guild::sim {

// ---------------------------------------------------------------------------
// gilde.exe 0x57c744 — VIBE_Character_SpawnOfficeStaffActor
// ---------------------------------------------------------------------------
int SpawnOfficeStaffActor(ActorRec* actor, int uni, const f32* rotPtr, const f32* posPtr,
                          void* findCtx, int findHandle, SpawnHooks& H) {
    // v20 = dword_577A68 (pos default) ; v22 = dword_577A78 (rot default)
    f32 defPos[4] = { kDefaultSpawnPos[0], kDefaultSpawnPos[1], kDefaultSpawnPos[2], kDefaultSpawnPos[3] };
    f32 defRot[3] = { kDefaultSpawnRot[0], kDefaultSpawnRot[1], kDefaultSpawnRot[2] };
    int restoreSlot = H.prevActiveSlot;                 // v23 = dword_649D60

    const f32* v7 = posPtr ? posPtr : defPos;           // v7 = a4 ? a4 : v20
    const f32* v8 = rotPtr ? rotPtr : defRot;           // v8 = a3 ? a3 : v22

    if (uni < 0)                                         // if ( a2 < 0 ) return 0
        return 0;
    // VIBE_Universe_SwitchActiveSlot(uni, 1, 0, rot)
    if (H.switchActiveSlot) H.switchActiveSlot(uni, 1, 0, (int)(intptr_t)v8);  // 0x57c79e

    TObject* anchor = nullptr;                           // v9
    if (findHandle) {                                    // if ( a6 )
        if (H.findByHandle)
            anchor = H.findByHandle(findCtx, 256, findHandle, 0, (int)(intptr_t)v8);  // 0x57c7b9
    }

    if (anchor || v7 || v8) {                            // if ( v9 || v7 || v8 )
        f32 spawnPt[4] = {0,0,0,0};                      // v21
        if (anchor) {                                    // if ( v9 )
            // PointThroughBoneChain(anchor, anchor.pos (+76), v21)
            if (H.pointThroughBoneChain) H.pointThroughBoneChain(anchor, anchor->pos, spawnPt);  // 0x57c7d4
        } else {
            spawnPt[0] = v7[0];                          // v21[0]=*v7
            spawnPt[1] = v7[1];                          // v21[1]=v7[1]
            spawnPt[2] = v7[2];                          // v21[2]=v7[2]
        }
        // v10 = ResolveStaffModel(*a1)
        StaffModelDef* def = H.resolveStaffModel ? H.resolveStaffModel(actor->modelKey) : nullptr;  // 0x57c7df
        if (def) {                                       // if ( v10 )
            // CreateFromModel(def->name (v10+4), universe v8)
            int handle = H.createFromModel ? H.createFromModel(def->name, (int)(intptr_t)v8) : 0;  // 0x57c7f5
            actor->characterHandle = handle;             // *((DWORD*)a1+97) = v12
            if (handle) {                                // if ( v12 )
                // sprintf(obj, "sp_%i", a1[1]) — name only.
                // *(obj+72) = *a1 | 0x3000000 — type word (object subsystem).
                // if rotPtr: SetWorldTranslation(obj, rotPtr)
                if (rotPtr) {                            // if ( v13 ) i.e. a3 != 0
                    if (H.setWorldTranslation)
                        H.setWorldTranslation(handle, rotPtr);  // 0x57c857
                }
                // 2-byte NUL-terminated copy of def->name (v11+4) into actor+248
                const char* src = def->name;             // v14 = v11 + 4
                char* dst = actor->headVariant;          // v15 = a1 + 248
                while (true) {
                    char c0 = src[0];                    // v16 = *v14
                    dst[0] = c0;                         // *v15 = *v14
                    if (!c0) break;                      // if ( !v16 ) break
                    char c1 = src[1];                    // v17 = v14[1]
                    src += 2;                            // v14 += 2
                    dst[1] = c1;                         // *(v15+1) = v17
                    dst += 1;                            // v15++ (note: dst advances by 1!)
                    if (!c1) break;                      // while ( v17 )
                }
                if (H.applyHeadVariant) H.applyHeadVariant(actor);   // 0x57c881
                if (H.resolveHeadBone) H.resolveHeadBone(actor, 0);  // 0x57c888
            }
        }
    }
    // VIBE_Universe_SwitchActiveSlot(restoreSlot, 1, anchor, rot)
    if (H.switchActiveSlot) H.switchActiveSlot(restoreSlot, 1, (int)(intptr_t)anchor, (int)(intptr_t)v8);  // 0x57c896
    return actor->characterHandle;                       // return *((DWORD*)a1+97)
}

// ---------------------------------------------------------------------------
// gilde.exe 0x57c8f0 — VIBE_Character_SpawnAtBuildingEntrance
// ---------------------------------------------------------------------------
int SpawnAtBuildingEntrance(int personId, int building, int objHandle,
                            const f32* posPtr, const u16* rotPtr, EntranceHooks& H) {
    int restoreSlot = H.spawn ? H.spawn->prevActiveSlot : 0;   // v6/v21 = dword_649D60
    ActorRec* person = H.findPersonById ? H.findPersonById(personId) : nullptr;  // 0x57c906
    if (!person)
        return 0;                                        // return result (null)
    if (person->characterHandle)                         // if ( *(p+97) ) return 0
        return 0;                                        // 0x57c91f

    int rec = H.personQueryBegin ? H.personQueryBegin(person, 1, 1, building) : 0;  // 0x57c933
    if (!rec)
        return 0;

    // if ( objHandle == -1 || (v9 = *(rec+93), (obj = GameObjectQueryFind(v9,1,1,objHandle)) != 0) )
    // NOTE: in the binary, on the objHandle != -1 path v9 is REASSIGNED from the
    // query record field *(rec+93) before both GameObjectQueryFind and the later
    // EnsureBuildingAvatar call.  `rec` is the opaque query-record handle returned
    // by personQueryBegin and is not a modelled struct here, so +93 is supplied by
    // the gameObjectQueryFind hook's view of `rec`; v9 carries `building` only on
    // the objHandle == -1 path (where the query is skipped entirely).
    int objRec = 0;
    int v9 = building;                                   // v9 = a1 (objHandle==-1 path)
    if (objHandle != -1) {
        objRec = H.gameObjectQueryFind ? H.gameObjectQueryFind(rec, 1, 1, objHandle) : 0;  // 0x57c95f (v9=*(rec+93))
        if (!objRec)
            return 0;                                    // query failed -> result
    }

    // Decide the owning avatar slot.
    int slot;
    if (H.isProductionType && H.isProductionType(rec)) {        // 0x57c963
        slot = H.ensureBuildingAvatar ? H.ensureBuildingAvatar(rec, v9, 0) : -1;  // 0x57c972
    } else {
        if (objRec == 0) {                               // if ( !v11 ) -> v13=0, LABEL_11
            slot = 0;
        } else {
            slot = H.ensureObjectAvatar ? H.ensureObjectAvatar(objRec) : -1;  // 0x57ca2e
        }
    }
    if (slot < 0)                                        // if ( v12 < 0 ) return 0
        return 0;                                        // (only on the ensure paths)

    // LABEL_11:
    if (H.spawn && H.spawn->switchActiveSlot)
        H.spawn->switchActiveSlot(slot, 1, objRec, slot);    // 0x57c97d

    // a queue object (v14 == objRec) chooses a wait anim, else dummy_TUER path.
    if (objRec) {                                        // if ( v14 )
        const char* anim = H.pickWaitAnimation ? H.pickWaitAnimation(objRec) : nullptr;  // 0x57c991
        const char* tag = anim ? anim : "dummy_EINGANG";    // 0x57ca38
        // 0x57c9a9: SpawnOfficeStaffActor(v8, v13, a5, a2, 0, v20) — the staff
        // call's a3(rotPtr) := entrance a5 (rotPtr), a4(posPtr) := entrance a2
        // (posPtr).  Pass rotPtr/posPtr in their OWN slots (was swapped here).
        if (H.spawn)
            SpawnOfficeStaffActor(person, slot,
                                  reinterpret_cast<const f32*>(rotPtr),  // a3 = entrance rotPtr (a5)
                                  posPtr,                                 // a4 = entrance posPtr (a2)
                                  nullptr, /*findHandle*/0, *H.spawn);   // 0x57c9a9 (tag passed as a6 in binary)
        (void)tag;
    } else {
        if (H.spawn)
            SpawnOfficeStaffActor(person, slot, nullptr, nullptr,
                                  /*findCtx*/(void*)(intptr_t)rec, /*findHandle from rec+97*/0,
                                  *H.spawn);                  // 0x57ca54 (dummy_TUER)
    }

    int handle = person->characterHandle;                // v17 = *(p+97)
    if (handle) {                                        // if ( v17 )
        if (H.preloadAniSet) H.preloadAniSet(handle, 1, "bewegung/gehen");  // 0x57c9c0
    }
    if (H.spawn && H.spawn->switchActiveSlot)
        H.spawn->switchActiveSlot(restoreSlot, 1, 0, slot);   // 0x57c9d0

    handle = person->characterHandle;                    // v18 = *(p+97)
    if (!handle)
        return 0;                                        // 0x57c9dd
    // *(v18+44) = building ; *(p+97 obj +48) = objHandle
    // (object-record fields; recorded via hook side-effects upstream.)

    // visibility: hidden unless the building is one of the two special interiors.
    bool keepVisible = false;
    if (H.special1 == 0 || rec == H.special1) {          // !v19 || v10==v19
        if (H.special2 == 0)                             // !dword_631748
            keepVisible = true;                          // 0x57ca61
        else if (rec == H.special2)                      // v10 == dword_631748
            keepVisible = true;                          // 0x57ca67
    }
    if (!keepVisible) {
        if (H.setVisible) H.setVisible(handle, 0);       // 0x57ca0d
    }
    return handle;                                       // 0x57ca12 / 0x57ca61
}

// ---------------------------------------------------------------------------
// gilde.exe 0x50650c — VIBE_Character_PreloadSceneAnimations
// ---------------------------------------------------------------------------
void PreloadSceneAnimations(int actorTypeByte, SceneObject* sceneObjects, int count,
                            int setKey, int setMatch, PreloadHooks& H) {
    // VIBE_Character_CollectByOwner(actor)  — single arg in the binary (a1@<eax>).
    // The hook's second parameter is unused (kept for ABI shape only).
    if (H.collectByOwner) H.collectByOwner(actorTypeByte, actorTypeByte);  // 0x506567
    (void)actorTypeByte;

    // The anim-set row's 16 string slices (offsets +1,+49,+97,...,+721; 48-byte
    // stride in the binary -> (721-1)/48 + 1 = 16 slices).  PreloadAniSet is
    // invoked with count arg 16 and exactly these 16 name pointers.  Modelled as
    // 16 empty slices; the per-object preload hook receives them.  The decision
    // is: for each scene object whose key matches the located set, preload.
    static const char* kSlices[16] = {0};

    for (int i = 0; i < count; ++i) {                    // v9 += 268 until byte_1333110
        SceneObject& so = sceneObjects[i];
        if (!so.objPtr)                                  // if ( *((DWORD*)v9+97) )
            continue;
        // && ( v7 == *((DWORD*)v9+91) || *(*((DWORD*)v9+97)+44) == *(DWORD*)(v7+1) )
        if (setKey == so.key || so.matchVal == setMatch) {
            if (H.preloadAniSet)
                H.preloadAniSet(so.objPtr, 16, kSlices, 16);  // 0x50666c (count=16, 16 slices)
        }
    }
}

} // namespace guild::sim
