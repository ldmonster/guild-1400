#pragma once
#include "guild/common/types.h"
#include <cstdint>  // std::intptr_t for record-identity fields

// =============================================================================
// guild::render — render-math leaves, batch 3 (gilde.exe gfx/light/texture).
//
// A third slice of self-contained, deterministic VIBE_Render_*/Light_*/Texture_*/
// Surface_* leaves translated 1:1 from the Hex-Rays reference. Each function is
// pure arithmetic / pointer-graph bookkeeping over caller-supplied records — no
// DDraw/GDI/device calls — so it is golden-testable.
//
// Cross-module callees that are NOT yet reconstructed are routed through an
// installable RenderLeaves3Hooks struct with inert default implementations
// defined in render_leaves3.cpp (tests install their own). Reconstructed callees
// are reused directly:
//   guild::compress::CrcCompute  (0x5dc6e0 VIBE_Util_Crc32; CrcCompute(0,d,n)==orig)
//
// Translated functions (all addresses verified UNTRANSLATED at time of writing):
//   0x5b9e74  VIBE_Render_SetMipFilterLevel    (mip box-filter level selection)
//   0x5db094  VIBE_Texture_ScrollUvCoords      (per-channel UV scroll, rate LUT)
//   0x5daf78  VIBE_Texture_AdvanceAnimFrames   (CRC-driven anim frame select)
//   0x5c80a0  VIBE_Light_CollectAffectedObject (light/object distance cull)
//   0x42e0b0  VIBE_Light_ApplyAmbient          (universe-slot detach bracket)
//   0x5dbbb4  VIBE_Texture_DetachClone         (recursive clone detach)
//   0x5db928  VIBE_Texture_CreateTileRecord    (tile-record init + bitfields)
//   0x5e0e9c  VIBE_Render_UnlinkObjectNode     (doubly-linked draw-list unlink)
//   0x431f18  VIBE_Render_IsSurfaceLost        (surface-lost predicate)
//   0x5b5404  VIBE_Surface_ReleaseTexture      (guarded texture release)
//   0x5af260..0x5af290  VIBE_Render_GetViewParamA..G (7 view-state accessors)
//
// Recovered constant tables (decoded from gilde.exe raw bytes):
//   kUvScrollRate[16]  (flt_5D938C; 16.16-ish per-tick UV velocities)
//   kAnimDivisor[16]   (dword_5D93C8; per-speed frame divisors)
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// Recovered constant tables (exposed for golden-vector tests).
// ---------------------------------------------------------------------------
// flt_5D938C[1..15] — per-channel UV scroll velocity (units/tick). Index 0 is
// padding (the original array begins one slot before the first used entry).
extern const float kUvScrollRate[16];
// dword_5D93C8[1..10] — anim-frame time divisor selected by the 4-bit speed field.
extern const int kAnimDivisor[16];

// ---------------------------------------------------------------------------
// Cross-module hooks (inert defaults in render_leaves3.cpp).
// ---------------------------------------------------------------------------
struct RenderLeaves3Hooks {
    // 0x5b94cc VIBE_Render_ComputeFilterWeights — rebuild the mip box-filter
    // weight matrix into the supplied buffer (snow/particle filter LUT).
    void (*computeFilterWeights)(float* weightMatrix);
    // 0x5b9f54 VIBE_TextureCache_Reset — flush the texture/mip cache.
    int  (*textureCacheReset)();
    // 0x5daec0 VIBE_Texture_FindGroupMember — choose an anim-group frame.
    //   member = findGroupMember(groupId, frameByte)
    u32  (*findGroupMember)(int groupId, u8 frameByte);
    // 0x5c8c40 / 0x5c8ab4 transform helpers used by light cull (light->local).
    //   ok = pointToBoneLocal(light, out3, basePos, frame)  (return ignored)
    void (*pointToBoneLocal)(void* light, float* out3, const float* basePos,
                             const float* frame);
    void (*rotateVectorWithFrame)(void* light, float* dir3, const float* frameMat,
                                  const float* refDir);
    // 0x5b4a24 VIBE_Universe_SwitchActiveSlot(slot, mode, a, b)
    void (*switchActiveSlot)(u32 slot, int mode, int a, int b);
    // 0x5b4258 VIBE_Object_DetachAndRelease(obj) -> status byte
    i8   (*detachAndRelease)(int obj);
    // 0x5d9a0c VIBE_Texture_ReleaseEntry(record)
    void (*releaseEntry)(void* record);
    // 0x5da244 VIBE_Texture_FindActiveRecord(slot, &outIdx) -> record ptr (or null)
    void* (*findActiveRecord)(int slot, int* outIdx);
    // 0x5d9360 VIBE_Util_StrNCopyPad(dst, src, n) — bounded copy with NUL pad.
    void (*strNCopyPad)(char* dst, const char* src, int n);
    // 0x431f18 backing query: device-surface-lost test (mode!=0 path).
    //   true == lost/unavailable.
    bool (*surfaceLost)();
};

void InstallRenderLeaves3Hooks(const RenderLeaves3Hooks& hooks);
const RenderLeaves3Hooks& GetRenderLeaves3Hooks();

// ---------------------------------------------------------------------------
// Recovered module globals (owned here; one definition).
// ---------------------------------------------------------------------------
// 0x64A038 mip filter size, 0x64A044/0x64A045 mip level bytes (SetMipFilterLevel).
struct MipFilterState {
    u32 size = 0;     // dword_64A038
    u8  shift = 0;    // byte_64A044 = 6 - log2(size)
    u8  lodBias = 0;  // byte_64A045 = min(2, requested>>7)
};
MipFilterState& MipState();

// 0x1406A6C last scroll tick, flt_1406950[32]/flt_14069D0[32] U/V phase banks.
struct UvScrollState {
    i32   lastTick = 0;     // dword_1406A6C
    float bankU[32] = {};   // flt_1406950
    float bankV[32] = {};   // flt_14069D0
};
UvScrollState& UvState();

// 0x649DA4.. view-parameter block read by GetViewParamA..G.
struct ViewParams {
    int a = 0, b = 0, c = 0, d = 0, e = 0, f = 0, g = 0;  // dword_649DA4 + ...
};
ViewParams& ViewParamState();

// ---------------------------------------------------------------------------
// 0x5b9e74 — VIBE_Render_SetMipFilterLevel.
// `requested` is the desired box-filter size; if in [4,256] it is clamped to a
// power-of-two <=64, MipState() is updated, the filter-weight LUT is rebuilt and
// the texture cache is flushed. Returns the (possibly unmodified) input, exactly
// like the original (which returns `result` untouched on the out-of-range path).
u32 SetMipFilterLevel(u32 requested);

// 0x5db094 — VIBE_Texture_ScrollUvCoords. Advance the 32-slot U/V phase banks by
// (tick - lastTick). Slots 1..15 use additive wrap at >1.0 (subtract 1.0 via the
// -1.0 constant); slots 17..31 subtract rate*dt and wrap at <0.0 (add 1.0). No-op
// if tick == lastTick. Operates on UvState().
void ScrollUvCoords(i32 tick);

// --- AdvanceAnimFrames record views (typed reconstruction of the pool records) -
// The original walks raw pointers; we expose the exact fields it touches.
struct AnimMesh {
    u8  frames = 0;     // mesh+112  total frames (0 == not animated)
    u8  locked = 0;     // mesh+113  lock byte (skip when set)
    u8  speedNibble = 0;// mesh+114  low nibble = speed selector (0 == skip)
    i32 refCount = 0;   // mesh+64
    i32 groupId = 0;    // mesh+80
    std::intptr_t handle = 0;  // identity used for (mesh - texBase) >> 7
};
struct AnimMaterial {
    // The original material entry's +20 slot holds a record pointer that is read
    // (its group id is compared) and then overwritten with the chosen member id.
    AnimMesh* slot = nullptr;  // matEntry+20 record pointer (or null -> skipped)
    i32 boundMember = 0;       // matEntry+20 written-back member id (== slot id field)
};
struct AnimGroup {
    i32 memberCount = 0;            // gd+8   ( > 0 required )
    bool hasBank = false;           // gd+16 != 0
    int meshStride = 0;             // gd+12  material-entry count
    AnimMesh** meshes = nullptr;    // bank   (array of mesh-record pointers)
    int meshCount = 0;              // bank+480
    AnimMaterial* materials = nullptr;  // gd+4  (array, stride 40)
};
// 0x5daf78 — VIBE_Texture_AdvanceAnimFrames. For an object with an animated
// texture group, pick each mesh's active frame via CRC32(objHandle) +
// tick/divisor (kAnimDivisor[speed]), then rebind every material entry whose
// looked-up record matches the mesh group id. `objHandle` is the 4-byte object
// handle hashed by the original `Crc32(&self,4)`. `texBaseHandle` models
// dword_1406A84 (the texture-bank base) for the (mesh-base)>>7 group index.
// Returns 1. A null `group` (or empty/unbanked group) is a no-op returning 1.
i8 AdvanceAnimFrames(u32 objHandle, AnimGroup* group, u32 tick,
                     std::intptr_t texBaseHandle);

// --- CollectAffectedObject views --------------------------------------------
// Object record fields touched by the cull (byte offsets from the decompile):
//   +488 boundBlock ptr (its +424 byte = "affected" flag, +392 frame matrix)
//   +533 kind byte (7 == directional special case), +529 flag byte (&0x10)
//   +148 radius float, +472/476/480 world pos, +484 cull radius, +144 src radius
//   +76 local frame matrix.
struct LightBoundBlock {
    u8    affected = 0;       // boundBlock+424
    float frameMatrix[12] = {};  // boundBlock+392 (rotate ref)
};
struct LightObject {
    LightBoundBlock* bound = nullptr;  // obj+488
    u8    kind = 0;           // obj+533
    u8    flags = 0;          // obj+529
    float radius = 0.0f;      // obj+148
    float pos[3] = {};        // obj+472,476,480
    float cullRadius = 0.0f;  // obj+484  (written from srcRadius in generic path)
    float srcRadius = 0.0f;   // obj+144
    float localFrame[12] = {};// obj+76
};
struct LightAccumulator {
    LightObject* reference = nullptr;  // acc[0]
    int          count = 0;            // acc[1]
    LightObject** outArray = nullptr;  // acc[2] (null == compute path)
};
// 0x5c80a0 — VIBE_Light_CollectAffectedObject. When `acc->outArray` is set, append
// `obj` (guarded by its affected flag); otherwise run the distance/radius cull via
// the injected transform hooks and set the affected flag + bump count if affected.
// Returns 1 in all paths (like the original).
i8 CollectAffectedObject(LightObject* obj, LightAccumulator* acc);

// 0x42e0b0 — VIBE_Light_ApplyAmbient. Compute the universe slot index from the
// object's bound-block pointer, bracket a DetachAndRelease with two
// SwitchActiveSlot calls. `slotArrayBase` models byte_13ECEC8 (the slot table
// base) so the index math `(ptr - base)/0x3D8` is reproducible.
i8 ApplyAmbient(int obj, int extra, int slotPtr, int slotArrayBase, u32 activeSlot);

// --- DetachClone views -------------------------------------------------------
// Texture pool record fields touched (byte offsets): +92 clone-master id (0 ==
// no clone), +80 group id (== dword index 20, the clone key), +112 tile-owner
// byte, +64 ref count.
struct TexRecord {
    int master = 0;     // +92
    int groupId = 0;    // +80  (also rec[20])
    u8  tileOwner = 0;  // +112
    int refCount = 0;   // +64
};
// 0x5dbbb4 — VIBE_Texture_DetachClone. Clear the record's clone master; if it is a
// tile-owner with a valid group id and `propagate` is set, recurse into every pool
// record whose group id matches (the recursive call passes propagate==0, since the
// original computes a2 = id^id == 0). `pool`/`poolCount` model dword_1406A84 /
// dword_1406A80. Returns the detached master id (or the record's group id when no
// clone was attached, mirroring the original's `return rec`).
int DetachClone(TexRecord* record, i8 propagate, TexRecord* pool, u32 poolCount);

// 0x5db928 — VIBE_Texture_CreateTileRecord. Allocate (via FindActiveRecord) and
// initialise a tile texture record: zero it, copy the name, set the size, derive
// the mip-step mask `(w-1)|(w*w-1)`, and pack the flag/format bytes. `name` is the
// tile name, `size`/`extra` the dimensions, `fmt`/`extraFlag1`/`extraFlag2` the
// flag inputs. `recordCounter` models dword_1406A74. Returns the record (or null).
char* CreateTileRecord(const char* name, int size, i8 fmt, int extra,
                       char extraFlag1, char extraFlag2, u32* recordCounter,
                       int batchTag);

// ---------------------------------------------------------------------------
// Object draw-list node, as walked by UnlinkObjectNode. The original reads a flat
// _DWORD[]: a1[195]=prev (+0x30C), a1[194]=next (+0x308), a1[188]=owner (+0x2F0).
// `prev`/`next` chain neighbour nodes; the end-of-list sentinels are &unk_1408130
// (head) and &unk_1408440 (tail). When a node is the list head/tail its neighbour
// link is stored on the OWNER container instead (owner +164 / +168 = a1[41]/[42]).
// Neighbour back-links live at +776/+780 (a1[194]/[195]) of the neighbour node.
// ---------------------------------------------------------------------------
struct UnlinkNode {
    UnlinkNode* prev = nullptr;   // a1[195] / +0x30C  (nullptr == head sentinel)
    UnlinkNode* next = nullptr;   // a1[194] / +0x308  (nullptr == tail sentinel)
    void*       owner = nullptr;  // a1[188] / +0x2F0  owning container
    // Back-link slots written when this node is a neighbour:
    UnlinkNode* fwdBack = nullptr;  // +0x308 (a1[194]) of a prev-neighbour
    UnlinkNode* revBack = nullptr;  // +0x30C (a1[195]) of a next-neighbour
};
// Owning container head/tail link slots (+164 / +168) plus the view-cache fields
// (+41/+42 dwords) refreshed when the unlinked node belonged to the active view.
struct UnlinkOwner {
    UnlinkNode* headLink = nullptr;   // +164
    UnlinkNode* tailLink = nullptr;   // +168
    int viewFieldA = 0;               // +41 dwords (copied to dword_1408438)
    int viewFieldB = 0;               // +42 dwords (copied to dword_140874C)
};
// 0x5e0e9c — VIBE_Render_UnlinkObjectNode. Splice `node` out of its doubly-linked
// list. `activeViewOwner` models off_649D64 (the active-view container): when the
// node's owner equals it, the view-cache scratch is refreshed from the owner's
// +41/+42 dwords (returned via outViewA/outViewB; pass null to ignore). Returns
// the surviving back-link (the original's `result`).
UnlinkNode* UnlinkObjectNode(UnlinkNode* node, UnlinkOwner* nodeOwner,
                             const UnlinkOwner* activeViewOwner,
                             int* outViewA = nullptr, int* outViewB = nullptr);

// 0x431f18 — VIBE_Render_IsSurfaceLost. true when no present surface is bound or
// the device reports the surface lost. `mode` models byte_762721 (0 == GDI path,
// always available), `hasDevice` models dword_62D578 != 0; the lost query is the
// injected hook.
bool IsSurfaceLost(u8 mode, bool hasDevice);

// 0x5b5404 — VIBE_Surface_ReleaseTexture. If `record` is non-null, release it.
// Returns the input.
int SurfaceReleaseTexture(int record);

// 0x5af260..0x5af290 — VIBE_Render_GetViewParamA..G (seven trivial accessors).
int GetViewParamA();
int GetViewParamB();
int GetViewParamC();
int GetViewParamD();
int GetViewParamE();
int GetViewParamF();
int GetViewParamG();

} // namespace guild::render
