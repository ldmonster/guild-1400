#pragma once
// character_recon5_transport — 1:1 reconstruction of the VIBE_Character
// transport / morph / spawn cluster of gilde.exe.  A prior wave deferred these as
// "object/mesh-coupled"; this wave reconstructs the PURE control + position
// interpolation MATH verbatim, routing the genuinely engine-coupled leaves
// (object reparent, mesh resolve, heightmap sample, CreateFromModel, anim
// stream attach/release, transparency) through INERT-DEFAULT hook structs.
//
// Reconstructed here (byte-faithful control flow + exact float op-order):
//   VIBE_Character_MoveToUniverse          0x402d3c  (__usercall eax,edx)
//   VIBE_Character_AttachTransport         0x402e40  (__usercall eax)
//   VIBE_Character_UpdateTransportAttach   0x402f70  (__usercall eax)
//   VIBE_Character_ReleaseMorphAni         0x403708  (__usercall eax,ecx,edi,esi)
//   VIBE_Character_CheckAniMorph           0x403764  (__usercall eax)
//   VIBE_Character_FadeOutSlots            0x4015cc  (cdecl, walks slot table)
//   VIBE_Character_PreloadSceneAnimations  0x50650c  (__usercall eax)
//   VIBE_Character_SpawnOfficeStaffActor   0x57c744  (__userpurge)
//   VIBE_Character_SpawnAtBuildingEntrance 0x57c8f0  (__userpurge)
//
// ---------------------------------------------------------------------------
// Object-record layout (the "scene object" pointer held at char[+52]).
// All offsets are byte offsets into the object record.  Recovered from the
// transport-attach matrix math (0x402e40 / 0x402f70):
//   +76  float position.x   (also obj+76 == the world-position vec3 base)
//   +80  float position.y
//   +84  float position.z
//   +120 float worldTrans.x (translation accumulator added to +76..+84)
//   +124 float worldTrans.y
//   +128 float worldTrans.z
//   +132 int   worldTranslation block base (read as a vec for SetWorldTranslation)
//   +136 float worldTranslation[1]  (the yaw/angle component mutated by attach)
//   +140 int   worldTranslation[2]
//   The 4x3 affine transform (basis columns + translation):
//   +396 row0.x  +400 row1.x  +404 row2.x      (column = local X axis)
//   +412 row0.y  +416 row1.y  +420 row2.y      (column = local Y axis)
//   +428 row0.z  +432 row1.z  +436 row2.z      (column = local Z axis)
//   +444 trans.x +448 trans.y +452 trans.z
//   +460 int   ptr to the world / scene context (reached only via hooks)
//   +492 int   ptr to a secondary attached object (re-parented alongside)
//   +512 int   back-pointer to owning person record (Spawn*)
//   +529 byte  flag byte (bit3 = deflate state; bit1 manipulated by attach)
//   +530 byte  flag byte (bits2..3 manipulated by attach)
//   +531 byte  flag byte (bit2 cleared by attach)
//
// Character-record layout (the `result`/`a1` pointer):
//   +52   int   object pointer (the scene object above)
//   +112  int   "primary" anim-stream attachment handle
//   +124  int   "morph" anim-stream attachment handle
//   +128  int   pending CurrentAnimStream request (name ptr)
//   +132  byte  morph mode selector (0/1/other)
//   +133  byte  morph-attached marker (set to -1 on attach)
//   +136  int   universe record pointer (current universe)
//   +140  byte  flags (bit1 = morph-suppress; bit6 = fade-active)
//   +172  ...   (a1[34*?]) terrain/floor context  (a1[34] == +136 universe rec)
//   +292  int   transport-attach record pointer (TransportAttach struct, 0x18)
//   +304  char  model/skin name (used to build "character/%s/%s.baf")
//   +368  char  display-name string (used in deflate log)
//   +492  int   secondary attached object pointer
//   a1[13] (== +52) object ; a1[73] (== +292) transport-attach record
//
// TransportAttach record (0x18 bytes, allocated by AttachTransport):
//   +0  int    object pointer (the transport's scene object)
//   +4  float  last position.x   (the trailing anchor, lerped toward)
//   +8  float  last position.y
//   +12 float  last position.z
//   +20 int    flag word (cleared to 0 by AttachTransport: *(v3+5)=0)
//
// Constants (recovered via get_bytes):
//   kAttachLocalZ      = -70.0    flt @0x6101E0 reused as 70.0 in Update
//   kTwoPi             = 6.2831853 (0x40c90fdb) flt @0x6101E4
//   kHeightStepThresh  = -5.0     dbl @0x6101EC
//   kDeflateHeightBias = 3.0      dbl @0x6101F4
//   kHeightLerp        = 0.25     flt @0x6101FC
//   kFadeRate          = 0.02     dbl @0x610004 (1/50 per tick)
//   kFadeScale         = 255.0    dbl @0x61000C
//   kFadeFull          = 255.0    flt @0x610014
//   kMorphHalfBias     = 0.5      flt @0x6102F0
//
// No third-party tech introduced (rule 6 N/A).
#include "guild/common/types.h"

namespace guild::sim {

using f32 = float;

// ===========================================================================
// Object record (modelled by-value; only the fields the math touches).
// Layout-faithful at the byte offsets documented above so the same float
// op-order can be reproduced.  Native engine state (scene context at +460,
// the actual GPU object) lives behind hooks, not in this struct.
// ===========================================================================
struct TObject {
    // Position / world-translation (+76..+84 / +120..+128 / +132..+140).
    f32 pos[3]      = {0,0,0};     // +76,+80,+84
    f32 worldAdd[3] = {0,0,0};     // +120,+124,+128 (added to pos in Update)
    f32 wtrans[3]   = {0,0,0};     // +132 (i), +136 (yaw f32), +140 (i)
    // 4x3 affine (basis columns + translation), exactly as indexed at +396..+452.
    // row[c][r]: r = which of the three +396/+400/+404 cells; c = axis column.
    f32 m[12]       = {0,0,0, 0,0,0, 0,0,0, 0,0,0};  // [0..2]=+396.., etc.
    int sceneCtx    = 0;           // +460
    int secondary   = 0;           // +492
    int ownerPerson = 0;           // +512
    u8  f529 = 0, f530 = 0, f531 = 0;  // +529 / +530 / +531
};

// Matrix cell accessors mirroring the literal byte offsets used in the binary.
// m index map (12 floats):  +396=m[0] +400=m[1] +404=m[2]
//                           +412=m[3] +416=m[4] +420=m[5]
//                           +428=m[6] +432=m[7] +436=m[8]
//                           +444=m[9] +448=m[10] +452=m[11]
inline f32 M396(const TObject& o){ return o.m[0]; }  inline f32 M400(const TObject& o){ return o.m[1]; }
inline f32 M404(const TObject& o){ return o.m[2]; }  inline f32 M412(const TObject& o){ return o.m[3]; }
inline f32 M416(const TObject& o){ return o.m[4]; }  inline f32 M420(const TObject& o){ return o.m[5]; }
inline f32 M428(const TObject& o){ return o.m[6]; }  inline f32 M432(const TObject& o){ return o.m[7]; }
inline f32 M436(const TObject& o){ return o.m[8]; }  inline f32 M444(const TObject& o){ return o.m[9]; }
inline f32 M448(const TObject& o){ return o.m[10]; } inline f32 M452(const TObject& o){ return o.m[11]; }

// ===========================================================================
// Constants (exact bit patterns from get_bytes @0x610xxx).
// ===========================================================================
constexpr f32    kAttachLocalZ     = -70.0f;       // flt_5CA2E0 dir / 0x6101E0=70
constexpr f32    kSeventy          = 70.0f;        // flt_6101E0
constexpr f32    kTwoPi            = 6.2831853f;    // flt_6101E4 (0x40c90fdb)
constexpr double kHeightStepThresh = -5.0;         // dbl_6101EC
constexpr double kDeflateHeightBias= 3.0;          // dbl_6101F4
constexpr f32    kHeightLerp       = 0.25f;        // flt_6101FC
constexpr double kFadeRate         = 0.02;         // dbl_610004 (1/50)
constexpr double kFadeScale        = 255.0;        // dbl_61000C
constexpr f32    kFadeFull         = 255.0f;       // flt_610014
constexpr f32    kMorphHalfBias    = 0.5f;         // flt_6102F0

// ===========================================================================
// TransportAttach record (0x18 bytes), allocated by AttachTransport.
// ===========================================================================
struct TransportAttach {
    TObject* object = nullptr;   // +0  transport scene object (v8 = *(v3))
    f32  last[3] = {0,0,0};      // +4,+8,+12  trailing anchor
    int  flag    = 0;            // +20 (the *(v3+5) cleared word)
};

// ===========================================================================
// Character record (by-value; only the fields the cluster reads/writes).
// ===========================================================================
struct TChar {
    TObject* object   = nullptr;   // +52
    int   primaryAnim = 0;         // +112
    int   morphAnim   = 0;         // +124
    int   pendingStream = 0;       // +128 (nonzero -> name request)
    u8    morphMode   = 0;         // +132
    i8    morphMark   = 0;         // +133
    int   universe    = 0;         // +136 (a2 in MoveToUniverse)
    u8    f140        = 0;         // +140
    TransportAttach* transport = nullptr;  // +292
    TObject* secondary = nullptr;  // +492
    int   self        = 0;         // identity for sprintf "morph_%i"
};

// ===========================================================================
// INERT-DEFAULT hooks for the engine leaves this cluster reaches.  Each
// default reproduces the "nothing observable happened" branch so the
// reconstructed control + math path is exercised verbatim.  None of these
// leaves contains extractable in-scope math (they are object reparenting,
// mesh/heightmap sampling, anim-stream attach/release, transparency) — they
// are coupled to not-yet-reconstructed engine subsystems.
// ===========================================================================
struct TransportHooks {
    // VIBE_Object_MoveBetweenUniverses 0x5b51e0 — reparent obj to a universe;
    // returns nonzero on success.  Default: success (1) so the relocation path
    // is taken in MoveToUniverse.
    int (*moveBetweenUniverses)(TObject* obj, int oldUniverse, int newUniverse) = nullptr;
    // VIBE_Object_SetPivotVector 0x5af490 — set pivot to {0,0,0}. (no-op)
    void (*setPivotVector)(TObject* obj, const f32* v3) = nullptr;
    // VIBE_Object_SetPosition 0x5af38c — store a vec3 into obj position.
    // Default writes obj->pos so the computed point is observable.
    void (*setPosition)(TObject* obj, const f32* v3) = nullptr;
    // VIBE_Object_SetWorldTranslation 0x5af50c — push obj->wtrans block.
    void (*setWorldTranslation)(TObject* obj, const f32* wtrans) = nullptr;
    // VIBE_Character_IndexFromPointer 0x426724 — is this universe a real character
    // universe? returns slot index (0 == "the global universe").  Default: 0.
    int (*indexFromPointer)(int universe) = nullptr;
    // VIBE_Character_TouchMeshFrames 0x40194c — re-inflate a deflated mesh. (no-op)
    void (*touchMeshFrames)(TChar* ch) = nullptr;
    // VIBE_Character_ResolveMesh 0x4013fc — resolve the floor/terrain context;
    // returns the context pointer (here an int handle).  Default: 0.
    int (*resolveMesh)(TChar* ch) = nullptr;
    // VIBE_Math_VectorWithinTolerance 0x5caa4c — |a-b| < tol ?  Default: compute
    // it honestly (it is pure math, but lives in a math module → hook so this unit
    // stays leaf-free; the reconstructed default reproduces it exactly).
    int (*vectorWithinTolerance)(const f32* a, const f32* b, f32 tol) = nullptr;
    // VIBE_Math_AngleToTargetSigned 0x5b6d1c — signed yaw to a target. Default 0.
    double (*angleToTargetSigned)(TObject* obj) = nullptr;
    // VIBE_Heightmap_WorldToTileWithHeight 0x5c6644 — sample terrain tile/height;
    // returns nonzero if inside the map and fills tile/height.  Default: 0 (miss).
    int (*worldToTileWithHeight)(int ctx, const f32* worldPos, int* tileOut, f32* heightOut) = nullptr;
    // The two tile-type cells consulted after a hit (heightmap tile classification).
    // Provided by the same coupled subsystem.  Default tileType 0.
    int (*tileTypeAt)(int ctx, int tileX, int tileY) = nullptr;
    // VIBE_Floor_PickTileAtPoint 0x5c2ddc — pick a floor tile + height for a point.
    int (*pickTileAtPoint)(int floorCtx, const f32* worldPos, int flag, int* tileOut, f32* heightOut) = nullptr;
    // *(a1[34]+172) — the floor handle held by the character's universe record.
    // Used by UpdateTransportAttach both as the skip gate (when 0 and the tile is
    // walkable, the height blend is skipped) and as the PickTileAtPoint floorCtx.
    // Default: 0 (no floor handle).
    int (*floorHandle)(TChar* ch) = nullptr;
    void* ctx = nullptr;

    // The "stride" of the heightmap row (obj ctx +32) and the strideBase (+36)
    // needed to index the tile-type byte; abstracted into tileTypeAt above.
};

// ---------------------------------------------------------------------------
// gilde.exe 0x402d3c — VIBE_Character_MoveToUniverse(result@eax, a2@edx)
//   Relocate character `ch` (and its attached transport + secondary objects)
//   into universe-record `newUniverse`.  Returns 1 on success, 0 if the move
//   leaf failed, or the original `ch` pointer value (here: 0/nonzero) if ch is
//   null.  Updates the deflate flag (bit3 of obj+529) from byte_62D010 and,
//   when moving into the global universe with a deflated character, re-inflates.
// Returns: 1 success, 0 failure.  (ch==null path returns 0 here.)
// ---------------------------------------------------------------------------
int MoveToUniverse(TChar* ch, int newUniverse, TransportHooks& H, u8 globalDeflateBit,
                   int globalUniverseId);

// ---------------------------------------------------------------------------
// gilde.exe 0x402e40 — VIBE_Character_AttachTransport(a1@eax)
//   Allocate a 0x18 TransportAttach record, compute the world-space attach
//   point as the character object's local (0,0,-70) transformed by its 4x3
//   affine, push it as the transport object's position + world translation,
//   set the attach flag bits, and link the record at ch->transport.
//   Returns the new TransportAttach record.
//   `transportObject` is the object whose flags/position get set (the v8 the
//   original takes from the alloc'd record-state; here passed explicitly).
// ---------------------------------------------------------------------------
TransportAttach* AttachTransport(TChar* ch, TransportHooks& H, TObject* transportObject);

// ---------------------------------------------------------------------------
// gilde.exe 0x402f70 — VIBE_Character_UpdateTransportAttach(a1@eax)
//   Per-frame: if the character object moved more than 0.01 from the transport's
//   last anchor, recompute the transport object's position by trailing the
//   character at a fixed 70-unit offset along the character's local Z, snap yaw
//   toward the character (minus 2*pi-relative bias), clamp the trailing distance
//   to 70, sample terrain height and lerp the transport's Y by 0.25, then update
//   the anchor.  Returns the resolved-mesh context (or 0).
// ---------------------------------------------------------------------------
int UpdateTransportAttach(TChar* ch, TransportHooks& H);

} // namespace guild::sim
