// character_recon4_avatar — implementation.  See header for provenance.
#include "character_recon4_avatar.h"

namespace guild::sim {

namespace {

// Read the stored avatar slot for `type` out of `table` at the biased high-byte
// cell, sign-extended to int.  Reproduces `(dword_TABLE+bias)[type] >>(arith) 24`.
inline int ReadSlot(const u8* table, int type, int hiByteBias) {
    // The original forms a dword pointer at (base + type) [+1 for objects baked
    // into the symbol], loads the dword, and arithmetic-shifts right by 24 — i.e.
    // it returns the signed top byte.  That top byte lives at base + type + bias.
    return static_cast<i8>(table[type + hiByteBias]);
}

// The avatar-name copy loop (identical in object & building paths): copy a
// NUL-terminated string from `src` into `dst` two bytes per iteration, stopping
// at the terminating NUL.  Byte-faithful to the unrolled asm at 0x505103.
inline void CopyAvatarName(u8* dst, const u8* src) {
    for (;;) {
        u8 c0 = src[0];
        dst[0] = c0;
        if (!c0)
            break;
        u8 c1 = src[1];
        src += 2;
        dst[1] = c1;
        dst += 2;
        if (!c1)
            break;
    }
}

} // namespace

// gilde.exe 0x505074 — VIBE_Character_EnsureObjectAvatar
int EnsureObjectAvatar(AvatarSlotState& st, AvatarHooks& H, i16 objType) {
    const int type = objType;  // movsx — used as index below

    int slot = ReadSlot(st.objectTable, type, kObjectSlotHiByte);
    if (slot != -1)
        return slot;

    // Special object type forces a scene load instead of allocation.
    if (type == H.specialObjectType) {
        if (H.loadObjektScene)
            H.loadObjektScene(H.ctx, type);
        return ReadSlot(st.objectTable, type, kObjectSlotHiByte);
    }

    int free = H.findFreeSlot ? H.findFreeSlot(H.ctx) : -1;
    if (free == -1)
        return free;

    // byte_122DDB0[type] = (u8) free  — byte_122DDB0 aliases the slot hi-byte.
    st.objectTable[type + kObjectSlotHiByte] = static_cast<u8>(free);
    // byte_13ED29D[984*free] = 1
    st.avatarLive[kAvatarRecordStride * free] = 1;
    // copy name from (dword_13CE27C + 65*type + 1) into byte_13ECEC8[984*free]
    CopyAvatarName(&st.avatarRec[kAvatarRecordStride * free],
                   &st.objectNames[kObjectNameStride * type + 1]);

    return ReadSlot(st.objectTable, type, kObjectSlotHiByte);
}

// gilde.exe 0x505134 — VIBE_Character_EnsureBuildingAvatar
int EnsureBuildingAvatar(AvatarSlotState& st, AvatarHooks& H, i8 bldType, int a2, i16 a3) {
    const int type = bldType;  // movsx byte

    int slot = ReadSlot(st.buildingTable, type, kBuildingSlotHiByte);
    if (slot != -1)
        return slot;

    // Special building type OR type 30 (0x1E) forces a scene load.
    if (type == H.specialBuildingType || type == 30) {
        if (H.loadGebaeudeScene)
            H.loadGebaeudeScene(H.ctx, type, a2, a3);
        return ReadSlot(st.buildingTable, type, kBuildingSlotHiByte);
    }

    int free = H.findFreeSlot ? H.findFreeSlot(H.ctx) : -1;
    if (free == -1)
        return free;

    // *((_BYTE *)&dword_122DD5D + type + 3) = free
    st.buildingTable[type + kBuildingSlotHiByte] = static_cast<u8>(free);
    // byte_13ED29D[984*free] = 1
    st.avatarLive[kAvatarRecordStride * free] = 1;
    // copy name from (dword_13CE294 + 589*type + 1) into byte_13ECEC8[984*free]
    CopyAvatarName(&st.avatarRec[kAvatarRecordStride * free],
                   &st.buildingNames[kBuildingNameStride * type + 1]);

    return ReadSlot(st.buildingTable, type, kBuildingSlotHiByte);
}

// gilde.exe 0x505870 — VIBE_Character_EnsureGateAvatars
int EnsureGateAvatars(AvatarSlotState& st, AvatarHooks& H, int a1, int a2) {
    // result = VIBE_Person_QueryBegin(a2, 1, 5, 11)
    void* rec = H.personQueryBegin ? H.personQueryBegin(H.ctx, nullptr, 1, 5, 11) : nullptr;
    (void)a2;
    if (!rec)
        return -1;  // null path: original returns the (null) query result
    // return VIBE_Character_EnsureBuildingAvatar(result, a1, v3)
    // `result` is a building record pointer; *result is its type byte.  a3 (cx)
    // is uninitialised garbage in the original (v3 declared but never set before
    // the tail call) — only the low 16 bits matter and they feed the scene-load
    // path's a3; reproduce by passing 0.
    i8 type = *static_cast<const i8*>(rec);
    return EnsureBuildingAvatar(st, H, type, a1, 0);
}

// gilde.exe 0x43cb20 — VIBE_Character_DestroyThunk
int DestroyThunk(AvatarHooks& H, const int* p) {
    return H.characterDestroy ? H.characterDestroy(*p) : 0;
}

// gilde.exe 0x43d9f0 — VIBE_Character_CmdPreloadSitMeshStub
int CmdPreloadSitMeshStub() {
    return 0;
}

} // namespace guild::sim
