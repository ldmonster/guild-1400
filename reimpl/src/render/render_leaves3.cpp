#include "render/render_leaves3.h"

#include "compress/crc.h"  // CrcCompute (0x5dc6e0 VIBE_Util_Crc32 equivalent)
#include "render/render_leaves4.h"  // g_rawLightingFlag (byte_649D70)

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace guild::render {

// ===========================================================================
// Recovered constant tables (decoded from gilde.exe raw bytes).
// ===========================================================================
// flt_5D938C[1..15] — per-channel UV scroll velocity (units/tick). Index 0 is a
// padding slot the original never reads (loop runs v2 = 1..15). Bit-exact floats.
const float kUvScrollRate[16] = {
    0.0f,                                 // [0]  (unused padding)
    3.333333370392211e-05f,               // [1]  0x380BCF65 (bit-exact, verified)
    6.666666740784422e-05f,               // [2]
    9.999999747378752e-05f,               // [3]
    0.00013333333481568843f,              // [4]
    0.00016666666488163173f,              // [5]
    0.00023333333956543356f,              // [6]
    0.0003000000142492354f,               // [7]
    3.9999998989515007e-05f,              // [8]
    4.999999873689376e-05f,               // [9]
    0.0006333333440124989f,               // [10]
    0.0007999999797903001f,               // [11]
    0.0009666666737757623f,               // [12]
    0.0011666666250675917f,               // [13]
    0.00139999995008111f,                 // [14]
    0.0016666667070239782f,               // [15]
};
// dword_5D93C8[1..10] — anim-frame time divisor by 4-bit speed field. Slots
// 11..15 fall outside the recovered table window in the binary; we leave them 0
// (the speed field for animated tiles is 1..10 in practice).
const int kAnimDivisor[16] = {
    0, 15, 13, 11, 9, 8, 7, 5, 3, 2, 1, 0, 0, 0, 0, 0,
};

// ===========================================================================
// Cross-module hooks (inert defaults) + recovered module globals.
// ===========================================================================
namespace {

void DefaultComputeFilterWeights(float*) {}
int  DefaultTextureCacheReset() { return 0; }
u32  DefaultFindGroupMember(int, u8 frameByte) { return frameByte; }
void DefaultPointToBoneLocal(void*, float* out3, const float*, const float*) {
    if (out3) { out3[0] = out3[1] = out3[2] = 0.0f; }
}
void DefaultRotateVectorWithFrame(void*, float*, const float*, const float*) {}
void DefaultSwitchActiveSlot(u32, int, int, int) {}
i8   DefaultDetachAndRelease(int) { return 1; }
void DefaultReleaseEntry(void*) {}
void* DefaultFindActiveRecord(int, int*) { return nullptr; }
void DefaultStrNCopyPad(char* dst, const char* src, int n) {
    // VIBE_Util_StrNCopyPad: copy up to n bytes then NUL-terminate at [n].
    int i = 0;
    if (dst && src) {
        for (; i < n && src[i]; ++i) dst[i] = src[i];
    }
    if (dst) {
        for (; i <= n; ++i) dst[i] = '\0';
    }
}
bool DefaultSurfaceLost() { return false; }  // assume not lost

RenderLeaves3Hooks g_hooks = {
    &DefaultComputeFilterWeights,
    &DefaultTextureCacheReset,
    &DefaultFindGroupMember,
    &DefaultPointToBoneLocal,
    &DefaultRotateVectorWithFrame,
    &DefaultSwitchActiveSlot,
    &DefaultDetachAndRelease,
    &DefaultReleaseEntry,
    &DefaultFindActiveRecord,
    &DefaultStrNCopyPad,
    &DefaultSurfaceLost,
};

MipFilterState g_mip;
UvScrollState  g_uv;
ViewParams     g_view;

} // namespace

void InstallRenderLeaves3Hooks(const RenderLeaves3Hooks& h) {
    g_hooks.computeFilterWeights  = h.computeFilterWeights  ? h.computeFilterWeights  : &DefaultComputeFilterWeights;
    g_hooks.textureCacheReset     = h.textureCacheReset     ? h.textureCacheReset     : &DefaultTextureCacheReset;
    g_hooks.findGroupMember       = h.findGroupMember       ? h.findGroupMember       : &DefaultFindGroupMember;
    g_hooks.pointToBoneLocal      = h.pointToBoneLocal      ? h.pointToBoneLocal      : &DefaultPointToBoneLocal;
    g_hooks.rotateVectorWithFrame = h.rotateVectorWithFrame ? h.rotateVectorWithFrame : &DefaultRotateVectorWithFrame;
    g_hooks.switchActiveSlot      = h.switchActiveSlot      ? h.switchActiveSlot      : &DefaultSwitchActiveSlot;
    g_hooks.detachAndRelease      = h.detachAndRelease      ? h.detachAndRelease      : &DefaultDetachAndRelease;
    g_hooks.releaseEntry          = h.releaseEntry          ? h.releaseEntry          : &DefaultReleaseEntry;
    g_hooks.findActiveRecord      = h.findActiveRecord      ? h.findActiveRecord      : &DefaultFindActiveRecord;
    g_hooks.strNCopyPad           = h.strNCopyPad           ? h.strNCopyPad           : &DefaultStrNCopyPad;
    g_hooks.surfaceLost           = h.surfaceLost           ? h.surfaceLost           : &DefaultSurfaceLost;
}
const RenderLeaves3Hooks& GetRenderLeaves3Hooks() { return g_hooks; }

MipFilterState& MipState() { return g_mip; }
UvScrollState&  UvState()  { return g_uv; }
ViewParams&     ViewParamState()     { return g_view; }

// ===========================================================================
// 0x5b9e74 — VIBE_Render_SetMipFilterLevel.
//   if (4 <= n <= 256) {
//     v2 = (n <= 64) ? n : 64;
//     for (i=0; (v2 & 1)==0; ++i) v2 >>= 1;     // i = trailing-zero count
//     dword_64A038 = 1 << i;  byte_64A044 = 6 - i;
//     v5 = n >> 7; if ((u8)v5 > 2) v5 = 2;  byte_64A045 = v5;
//     ComputeFilterWeights(flt_13FE540); return TextureCache_Reset();
//   }
//   return n;
// ===========================================================================
u32 SetMipFilterLevel(u32 requested) {
    u32 result = requested;
    if (requested <= 0x100 && requested >= 4) {
        u32 v2 = (requested <= 0x40) ? requested : 64u;
        u8 i = 0;
        for (; (v2 & 1) == 0; ++i) v2 >>= 1;
        g_mip.size  = 1u << i;
        g_mip.shift = static_cast<u8>(6 - i);
        u32 v5 = requested >> 7;
        if (static_cast<u8>(v5) > 2u) v5 = 2;
        g_mip.lodBias = static_cast<u8>(v5);
        // Original passes the 60x... weight buffer flt_13FE540 to the recompute.
        static float s_weightMatrix[6144 / 4];  // 0x600 floats per the loop bound
        g_hooks.computeFilterWeights(s_weightMatrix);
        return static_cast<u32>(g_hooks.textureCacheReset());
    }
    return result;
}

// ===========================================================================
// 0x5db094 — VIBE_Texture_ScrollUvCoords.
//   v1 = tick - lastTick; if (tick != lastTick) { lastTick = tick;
//     v5 = (float)(u32)v1;                         // FILD of the unsigned delta
//     for (v2 = 1; v2 != 16; ++v2) {               // additive channels
//       bank[v2] += rate[v2] * v5;  if (bank[v2] > 1.0) bank[v2] += -1.0f;  ... }
//     for (i = 17; i != 32; ++i) {                 // subtractive channels
//       bank[i] -= rate2[i] * v5;   if (bank[i] < 0.0) bank[i] += 1.0f;  ... } }
// rate2[i] (loc_5D934C + i*4) aliases rate[i-16]; we honour that via kUvScrollRate.
// ===========================================================================
void ScrollUvCoords(i32 tick) {
    UvScrollState& s = g_uv;
    const u32 delta = static_cast<u32>(tick - s.lastTick);
    if (tick == s.lastTick) return;
    s.lastTick = tick;
    const float dt = static_cast<float>(delta);

    auto wrapDownAdd = [](float v) -> float {
        // SLODWORD(v) > 1065353216  <=> v as float > 1.0 (and positive).
        return (v > 1.0f) ? (v + -1.0f) : v;
    };
    for (int v2 = 1; v2 != 16; ++v2) {
        const float r = kUvScrollRate[v2];
        s.bankU[v2] = r * dt + s.bankU[v2];
        s.bankU[v2] = wrapDownAdd(s.bankU[v2]);
        s.bankV[v2] = r * dt + s.bankV[v2];
        s.bankV[v2] = wrapDownAdd(s.bankV[v2]);
    }
    for (int i = 17; i != 32; ++i) {
        // rate2[i] == kUvScrollRate[i - 16]  (the array aliasing in the original).
        const float r = kUvScrollRate[i - 16];
        s.bankU[i] = s.bankU[i] - r * dt;
        if (s.bankU[i] < 0.0f) s.bankU[i] = s.bankU[i] + 1.0f;
        s.bankV[i] = s.bankV[i] - r * dt;
        if (s.bankV[i] < 0.0f) s.bankV[i] = s.bankV[i] + 1.0f;
    }
}

// ===========================================================================
// 0x5daf78 — VIBE_Texture_AdvanceAnimFrames.
// Reads through several record pointers; the cull/condition logic is reproduced
// 1:1, the texture-bank divisor select uses kAnimDivisor, the member lookup is
// the injected FindGroupMember, and the frame seed is CRC32 of the object handle
// (Crc32(&self,4) -> CrcCompute(0,bytes,4)). All non-pure callees are routed.
//
// Record layout (byte offsets, from the decompile):
//   obj+460 : anim-group descriptor (gd). gd+8 = member count, gd+16 = bank ptr,
//             gd+12 = mesh stride count, gd+4 = mesh array base.
//   bank+480: mesh-pointer count; bank is &mesh[0]; each entry is *(int*).
//   mesh+112: total frames (u8), +113 lock byte, +114 low-nibble = speed sel,
//             +64 ref count, +80 group id.
//   matEntry stride 40, +20 = material's mesh-id (compared to mesh+80).
// ===========================================================================
i8 AdvanceAnimFrames(u32 objHandle, AnimGroup* group, u32 tick,
                     std::intptr_t texBaseHandle) {
    if (!group) return 1;
    if (!group->hasBank) return 1;       // gd+16 != 0 required
    if (group->memberCount <= 0) return 1;  // gd+8 > 0 required

    // Frame seed = CRC32 of the 4-byte object handle (VIBE_Util_Crc32(&self,4)).
    u8 seedBytes[4];
    std::memcpy(seedBytes, &objHandle, 4);
    const u32 crc = compress::CrcCompute(0, seedBytes, 4);

    for (int n = 0; n < group->meshCount; ++n) {
        AnimMesh* mesh = group->meshes ? group->meshes[n] : nullptr;
        if (!mesh) continue;
        if (!mesh->frames) continue;          // *(v9+112) == 0
        if (mesh->locked) continue;           // *(v9+113) set
        const u8 speed = static_cast<u8>(mesh->speedNibble & 0x0F);
        if (!speed) continue;                 // (*(v9+114) & 0xF) == 0
        if (mesh->refCount <= 0) continue;    // *(v9+64) <= 0

        const int meshGroupId = mesh->groupId;          // v11 = *(v9+80)
        const int div = kAnimDivisor[speed];            // dword_5D93C8[speed]
        const int groupIndex = static_cast<int>((mesh->handle - texBaseHandle) >> 7);
        const u8  frameSel = (div != 0)
            ? static_cast<u8>((crc + tick / static_cast<u32>(div)) % mesh->frames)
            : 0;
        const u32 member = g_hooks.findGroupMember(groupIndex, frameSel);

        // Rebind every material entry whose record matches this mesh's group id.
        for (int k = 0; k < group->meshStride; ++k) {
            AnimMaterial& mat = group->materials[k];
            if (mat.slot && meshGroupId == mat.slot->groupId) {
                mat.boundMember = static_cast<int>(member);
            }
        }
    }
    return 1;
}

// ===========================================================================
// 0x5c80a0 — VIBE_Light_CollectAffectedObject.
//   if (acc[2]) {                         // a collected-array exists -> append
//     if (!*(byte*)(obj+488 ->+424)) return 1;   // already-flagged guard
//     acc[obj index slot] = obj; return 1;
//   } else { ... distance / radius cull, set the +424 flag if affected ... }
// We model the +488 bound-block as a separate byte buffer addressed by obj+488.
// The transform helpers are injected. acc is the light accumulator dword view:
//   acc[0] = reference object, acc[1] = count, acc[2] = output array base.
// ===========================================================================
i8 CollectAffectedObject(LightObject* obj, LightAccumulator* acc) {
    LightBoundBlock* bound = obj->bound;

    if (acc->outArray) {                  // acc[2] set -> append path
        if (!bound->affected) return 1;
        acc->outArray[acc->count] = obj;
        ++acc->count;
        return 1;
    }

    bound->affected = 0;
    float local[3] = {0, 0, 0};
    g_hooks.pointToBoneLocal(obj, local, obj->pos, obj->localFrame);

    if (obj->kind == 7) {                 // *(obj+533) == 7
        if (obj->radius == 0.0f || (obj->flags & 0x10) != 0) return 1;
        g_hooks.rotateVectorWithFrame(obj, local, bound->frameMatrix, nullptr);
        ++acc->count;
        bound->affected = 1;
        return 1;
    }

    // Generic distance cull against the reference object acc[0].
    obj->cullRadius = obj->srcRadius;     // *(obj+484) = *(obj+144)
    LightObject* ref = acc->reference;
    const float dx = obj->pos[0] - ref->pos[0];
    const float dy = obj->pos[1] - ref->pos[1];
    const float dz = obj->pos[2] - ref->pos[2];
    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (obj->radius == 0.0f || (obj->cullRadius + ref->cullRadius) <= dist) return 1;
    ++acc->count;
    bound->affected = 1;
    return 1;
}

// ===========================================================================
// 0x42e0b0 — VIBE_Light_ApplyAmbient.
//   v5 = -1; v6 = dword_649D60;            // saved active slot
//   if (*(obj+520) != off_649D64) {        // not already the active-view block
//     v7 = (*(obj+520) - byte_13ECEC8) / 0x3D8; if (v7 >= 0x40) v7 = -1;
//     v5 = v7; SwitchActiveSlot(v7,1,obj,extra);
//   }
//   result = DetachAndRelease(obj); dword_62D568 = <ecx spill>;
//   if (v5 != -1) return SwitchActiveSlot(v6,1,...,extra);
//   return result;
// slotPtr models *(obj+520); slotArrayBase models byte_13ECEC8; activeSlot=649D60.
// `activeViewPtr` (off_649D64) is taken as 0 so the index path is exercised; pass
// slotPtr==0 to model "already active view".
// ===========================================================================
i8 ApplyAmbient(int obj, int extra, int slotPtr, int slotArrayBase, u32 activeSlot) {
    int v5 = -1;
    const u32 v6 = activeSlot;
    const int activeViewPtr = 0;  // off_649D64 sentinel
    if (slotPtr != activeViewPtr) {
        u32 v7 = static_cast<u32>(slotPtr - slotArrayBase) / 0x3D8u;
        if (v7 >= 0x40) v7 = 0xFFFFFFFFu;  // -1
        v5 = static_cast<int>(v7);
        g_hooks.switchActiveSlot(v7, 1, obj, extra);
    }
    const i8 result = g_hooks.detachAndRelease(obj);
    if (v5 != -1) {
        g_hooks.switchActiveSlot(v6, 1, obj, extra);
        return 1;  // original returns SwitchActiveSlot's al (treated as truthy)
    }
    return result;
}

// ===========================================================================
// 0x5dbbb4 — VIBE_Texture_DetachClone.
//   if (rec && *(rec+92)) {
//     v4 = *(rec+92); v5 = *(rec+80); *(rec+92) = 0;
//     if (v5 != -1 && *(rec+112) && propagate) {
//       for each pool record r with *(r+64) > 0:
//         if (*(r+80) == rec[20]) DetachClone(r, rec[20]^*(r+80) /*==0*/, ...);
//     }
//     ReleaseEntry(rec); return v4;
//   }
//   return rec;
// The recursive call passes a2 = v9^v10 which is 0 when they match (so recursion
// does not re-propagate) — reproduced exactly.
// ===========================================================================
int DetachClone(TexRecord* record, i8 propagate, TexRecord* pool, u32 poolCount) {
    if (record && record->master) {
        const int master = record->master;   // v4 = *(rec+92)
        const int v5 = record->groupId;       // v5 = *(rec+80)
        record->master = 0;                   // *(rec+92) = 0
        if (v5 != -1 && record->tileOwner && propagate) {
            const int cloneId = record->groupId;  // v3[20] (dword 20 == byte +80)
            for (u32 i = 0; i < poolCount; ++i) {
                TexRecord* r = &pool[i];
                if (r->refCount > 0 && r->groupId == cloneId) {
                    // Original passes a2 = cloneId ^ r->groupId == 0 (no re-propagate).
                    DetachClone(r, static_cast<i8>(cloneId ^ r->groupId), pool, poolCount);
                }
            }
        }
        g_hooks.releaseEntry(record);
        return master;
    }
    return 0;  // original returns the record pointer unchanged (no detach occurred)
}

// ===========================================================================
// 0x5db928 — VIBE_Texture_CreateTileRecord.
//   rec = FindActiveRecord(slot,&idx); if (!rec || !byte_649D70) return 0;
//   memset(rec,0,4)  (the 4-byte head-zero unrolled in the original);
//   StrNCopyPad(rec, name, 63);
//   rec[29]=size; rec[30]=size; rec[17]=0; rec[24]=rec[25]=rec[23]=rec[22]=rec[21]=0;
//   rec[19] = (size-1) | (size*size-1);
//   rec[16]=1; rec[27]=extra;
//   rec+104 bits: bit0 = fmt&1; clear+set; rec+106 low5 = extraFlag1&0x1F;
//   rec+104 &= 0xF9; |= 4; &= 0xF7; |= (extraFlag2==0 ? 8 : 0);
//   rec[28]=0; rec[20]=-1; dword_1406A74++; rec[124] = dword_14080C0;
// (dword indices: rec[N] == *((int*)rec + N) == byte offset N*4.)
// ===========================================================================
char* CreateTileRecord(const char* name, int size, i8 fmt, int extra,
                       char extraFlag1, char extraFlag2, u32* recordCounter,
                       int batchTag) {
    int idx = 0;
    char* rec = static_cast<char*>(g_hooks.findActiveRecord(extra, &idx));
    // 0x5db955 — `if (!ActiveRecord || !byte_649D70) return 0`. Both the record
    // slot AND the system-init flag byte_649D70 (g_rawLightingFlag) must be set.
    if (!rec || !g_rawLightingFlag) return nullptr;

    std::memset(rec, 0, 4);  // head zero (unrolled memset in the original)
    g_hooks.strNCopyPad(rec, name, 63);

    auto dw = [rec](int n) -> int* { return reinterpret_cast<int*>(rec) + n; };

    *dw(29) = size;
    *dw(30) = size;
    *dw(17) = 0;
    *dw(24) = 0;
    *dw(25) = 0;
    *dw(23) = 0;
    *dw(22) = 0;
    *dw(21) = 0;
    *dw(19) = (size - 1) | (size * size - 1);
    *dw(16) = 1;
    *dw(27) = batchTag;

    // Flag byte at +104.
    rec[104] = static_cast<char>(rec[104] & 0xFE);
    rec[104] = static_cast<char>((fmt & 1) | rec[104]);
    // Format byte at +106 (low 5 bits).
    rec[106] = static_cast<char>(rec[106] & 0xE0);
    rec[106] = static_cast<char>((extraFlag1 & 0x1F) | rec[106]);

    *dw(28) = 0;
    *dw(20) = -1;
    rec[104] = static_cast<char>(rec[104] & 0xF9);
    rec[104] = static_cast<char>(rec[104] | 4);
    rec[104] = static_cast<char>(rec[104] & 0xF7);
    rec[104] = static_cast<char>((8 * (extraFlag2 == 0)) | rec[104]);

    if (recordCounter) ++(*recordCounter);
    *dw(31) = batchTag;  // rec[124] byte offset 124 == dword index 31 (dword_14080C0 tag)
    return rec;
}

// ===========================================================================
// 0x5e0e9c — VIBE_Render_UnlinkObjectNode.
//   v1=prev; v2=owner;
//   if (prev == headSentinel) owner->+164 = next; else prev->+776 = next;
//   v3 = next;
//   if (next != tailSentinel) { result = prev; next->+780 = prev;
//       if (owner == activeView) refreshViewCache; return result; }
//   else { result = prev; owner->+168 = prev; if (owner==activeView) refresh; }
//   return result;
// prev == headSentinel  <=>  node->prev == nullptr (we model the sentinel as null).
// next == tailSentinel  <=>  node->next == nullptr.
// Neighbour +776 is the prev-neighbour's `fwdBack`; +780 is the next-neighbour's
// `revBack`. Owner +164/+168 are headLink/tailLink.
// ===========================================================================
UnlinkNode* UnlinkObjectNode(UnlinkNode* node, UnlinkOwner* nodeOwner,
                             const UnlinkOwner* activeViewOwner,
                             int* outViewA, int* outViewB) {
    UnlinkNode* prev = node->prev;
    UnlinkNode* next = node->next;

    if (prev == nullptr) {            // prev == &unk_1408130 (head sentinel)
        if (nodeOwner) nodeOwner->headLink = next;
    } else {
        prev->fwdBack = next;          // prev->+776 = next
    }

    UnlinkNode* result;
    bool refresh = false;
    if (next != nullptr) {            // next != &unk_1408440 (tail sentinel)
        result = prev;
        next->revBack = prev;          // next->+780 = prev
        refresh = (nodeOwner == activeViewOwner);
    } else {
        result = prev;
        if (nodeOwner) nodeOwner->tailLink = prev;  // owner->+168 = prev
        refresh = (nodeOwner == activeViewOwner);
    }

    if (refresh && activeViewOwner) {
        if (outViewA) *outViewA = activeViewOwner->viewFieldA;  // dword_1408438
        if (outViewB) *outViewB = activeViewOwner->viewFieldB;  // dword_140874C
    }
    return result;
}

// ===========================================================================
// 0x431f18 — VIBE_Render_IsSurfaceLost.
//   return !byte_762721 || !dword_62D578 || device->IsLost(device) == 0;
// (mode==0 -> GDI path, never "lost"; no device -> never "lost"; else query.)
// ===========================================================================
bool IsSurfaceLost(u8 mode, bool hasDevice) {
    return !mode || !hasDevice || g_hooks.surfaceLost() == false;
}

// ===========================================================================
// 0x5b5404 — VIBE_Surface_ReleaseTexture.
//   if (record) ReleaseEntry(); return record;
// ===========================================================================
int SurfaceReleaseTexture(int record) {
    if (record) {
        g_hooks.releaseEntry(reinterpret_cast<void*>(static_cast<std::intptr_t>(record)));
    }
    return record;
}

// ===========================================================================
// 0x5af260..0x5af290 — VIBE_Render_GetViewParamA..G (return dword_649DA4 + k*4).
// ===========================================================================
int GetViewParamA() { return g_view.a; }
int GetViewParamB() { return g_view.b; }
int GetViewParamC() { return g_view.c; }
int GetViewParamD() { return g_view.d; }
int GetViewParamE() { return g_view.e; }
int GetViewParamF() { return g_view.f; }
int GetViewParamG() { return g_view.g; }

} // namespace guild::render
