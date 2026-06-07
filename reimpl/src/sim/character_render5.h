#pragma once
// character_render5 — the remaining deterministic leaves of the Character cluster
// (gilde.exe): per-person NEED decay, action-queue readiness, type/turn counts, the
// camera-view-mode script dispatch, the sample-stop / pending-mesh-flush state
// transitions, and the terrain-tile queries. These are faithful 1:1 ports of pure
// data / arithmetic / predicate code. Every renderer / anim / object / heightmap
// leaf the engine calls out to is NOT reconstructed in this slice, so it is routed
// through an installable CharRender5Hooks struct with inert default implementations
// (defined in character_render5.cpp). Deterministic siblings that ARE reconstructed
// elsewhere (util::RandomModulo, render::AnimStrCmp, sim::PersonFindRecordById,
// the season day%4 rule) are reused via extern, exactly as the live wiring does.
//
// Translated functions (this TU):
//   VIBE_Character_CheckQueueReady               0x403474
//   VIBE_Character_UpdateNeedsDecay              0x4521cc
//   VIBE_Character_UpdateAllNeeds                0x452190
//   VIBE_Character_CountByType                   0x4b092c
//   VIBE_Character_GetIndex_Thunk                0x4b1e08  (-> GameTime_GetSeasonFromDay)
//   VIBE_Character_SetCameraViewMode             0x43d8f0
//   VIBE_Character_CmdGetCharacterHandle         0x43d208
//   VIBE_Character_CmdGetCharacterSubObjectHandle 0x43de10
//   VIBE_Character_StopSample                    0x405894
//   VIBE_Character_QueryTerrainType              0x404650
//   VIBE_Character_ComputeTargetTile             0x404f6c
//   VIBE_Character_FlushPendingMesh              0x426924
#include "guild/common/types.h"
#include "sim/types.h"   // kPersonStride (canonical 536-byte person-record stride)

namespace guild::sim {

// ===========================================================================
// Record views. The engine addresses these fields by raw byte offset on the heap
// actor record (the 0x204-byte "ch_t") and on the action-stream / mesh records it
// points to. On a 32-bit target those links are i32 pointers stored in the record;
// on a 64-bit host we model the linked records with NAMED-FIELD structs (real
// pointers) so the cross-record arithmetic is faithful AND pointer-clean (never
// round-tripping a host pointer through a 32-bit field). The byte offsets each
// modeled field stands for are documented in the comments. Records that hold only
// scalar (float/int) fields (the 536-byte person needs block) keep the faithful
// raw-byte (u8*) view, since no pointer is ever truncated there.
// ===========================================================================

// --- action-stream record (the +112 / +128 ch_t action streams) ------------
struct ActionStream {
    int  cursor;           // +0x00  current stream frame cursor
    void* desc;            // +0x68  (+104) stream descriptor ptr
    u8   flags;            // +0x6D  (+109) flag byte (0x20 == loop / no-end gate)
};
// --- stream descriptor (the +104 target) ------------------------------------
struct StreamDesc {
    int  altEnd;           // +0x148 (+328) end frame used when endFrame == -1
    int  endFrame;         // +0x154 (+340) end frame (-1 == use altEnd)
};
// --- mesh handle record (the +52 ch_t mesh) ---------------------------------
struct MeshRec {
    float worldPos[3];     // +0x4C..+0x54 (+76,+80,+84) world XYZ
    int   material;        // +0x1CC (+460) material handle (0 == none)
    int   lowPolyBase;     // +0x1EC (+492) low-poly base (for the +244 attach slot)
};
// --- morph/sample attachment record (the +116 ch_t anim morph) --------------
struct MorphRec {
    int  attached;         // +0x68 (+104) nonzero == an attachment is live
};
// --- live-actor (ch_t) named view (only the fields this slice touches) ------
struct ChActor {
    ActionStream* actionSlot1; // +0x70  (+112) primary action stream
    int           animMorph0;  // +0x74  (+116) raw morph attach (0 == none)
    MorphRec*     animMorph;   //         decoded view of +116 (null when 0)
    int           actionSlot2; // +0x80  (+128) secondary action stream (0 == none)
    u8            flagsA;      // +0x8C  (+140) flag byte A (0x10 sample-active)
    MeshRec*      mesh;        // +0x34  (+52) mesh handle
};

// --- person record offsets touched by the needs decay -----------------------
// The need block is a 13-entry table (index 0..12) of 12-byte entries based at
// +144: entry[k] = { value@+0, rate@+? , decay@+? } addressed as base + 12*k.
//   +0x90 (144) value[k]  : the current need level (clamped to [0,1000])
//   +0x88 (136) rate[k]   : per-tick increment scalar
//   +0x8C (140) decay[k]  : per-tick proportional decay scalar
// and the "primary"/mood need:
//   +0x124 (292) primaryRate
//   +0x128 (296) primaryDecay
//   +0x12C (300) primaryValue
//   +0x12D (301) activeNeedIndex (the high byte of the dword at +301 is the index)
//   +0x130 (304) lastDominantIndex (written when the dominant need changes)
//   +0x170 (368) busyFlag (case-2 gate)
//   +0x194 (404) timeOfDay/phase (case-else gate: > 6)
//   +0x1E0 (480) scaleFactor (case-3 multiplier)
//   +0x02  (2)   kind byte (case-2 alt gate: == 3)
enum PersonNeedField : int {
    kPnNeedBase     = 144,  // +0x90  value[0]
    kPnRateBase     = 136,  // +0x88  rate[0]
    kPnDecayBase    = 140,  // +0x8C  decay[0]
    kPnPrimaryRate  = 292,  // +0x124
    kPnPrimaryDecay = 296,  // +0x128
    kPnPrimaryValue = 300,  // +0x12C
    kPnActiveIndex  = 301,  // +0x12D (read as the >>24 byte of dword @+301)
    kPnLastDominant = 304,  // +0x130
    kPnBusyFlag     = 368,  // +0x170
    kPnPhase        = 404,  // +0x194
    kPnScale        = 480,  // +0x1E0
    kPnKind         = 2,    // +0x02
};
constexpr int kNeedCount      = 13;   // loop bound (v19[4] < 13)
constexpr int kNeedEntryStride = 12;  // 12 * idx
constexpr float kNeedCeiling  = 1000.0f;  // dbl_619110 / 1148846080 == 1000.0
constexpr double kNeedDecayEps = 0.002;   // dbl_619118
constexpr float kNeedAdjust   = -50.0f;   // flt_619120
constexpr double kNeedTwo     = 2.0;      // dbl_619128

// ===========================================================================
// Hook surface — every NON-reconstructed renderer / anim / object / heightmap /
// scene leaf the originals call. Inert defaults make every function deterministic
// in isolation; tests / live wiring install real implementations.
// ===========================================================================
struct CharRender5Hooks {
    // --- StopSample ---------------------------------------------------------
    // VIBE_Character_TouchMeshFrames(actor): re-inflate the actor's mesh frames.
    void (*touchMeshFrames)(void* actor);
    // VIBE_Anim_PruneExpiredAttachments(slot): drop the finished morph attachment.
    void (*pruneAttachments)(void* meshSlotBase);

    // --- SetCameraViewMode --------------------------------------------------
    // VIBE_Util_StrCmp(a,b): faithful byte compare, 0 == equal. (Reused sibling.)
    int  (*strCmp)(const char* a, const char* b);
    // VIBE_Character_SetupAttachCamera(actor, mode): attach the camera in `mode`.
    void (*setupAttachCamera)(void* actor, int mode);
    // VIBE_Script_ReportError(msg): report a script-time error string.
    void (*scriptError)(const char* msg);

    // --- Cmd handle lookups -------------------------------------------------
    // VIBE_Character_FindByPredicate(name): live actor whose mesh-name == name.
    void* (*findByName)(const char* name);
    // VIBE_Object_FindByHandle(meshBase, kind, name, flag, extra): sub-object find.
    void* (*objectFindByHandle)(void* meshBase, int kind, int name, int flag, int extra);

    // --- QueryTerrainType / ComputeTargetTile -------------------------------
    // VIBE_Character_ResolveMesh(actor): the actor's collision/heightmap handle.
    void* (*resolveMesh)(void* actor);
    // VIBE_Heightmap_WorldToTileWithHeight(map, world, outTile2, outHeight):
    // returns nonzero when `world` lands on a tile; fills the 2 tile coords + height.
    int  (*worldToTileWithHeight)(void* map, const float* world, int* outTile2, float* outHeight);
    // terrain code byte at the resolved (col,row) on map `m` (0 / 13 == blocked).
    u8   (*terrainCodeAt)(void* m, int col, int row);
    // VIBE_Map_TraceLineOfSight(map, fromPos, toCol, fromPos2, toRow, &outCol,
    // &outRow, budget, flag): trace; returns nonzero when a tile is reached.
    int  (*traceLineOfSight)(void* map, const float* fromPos, int toCol,
                             int toRow, int* outCol, int* outRow);

    // --- FlushPendingMesh ---------------------------------------------------
    // VIBE_Anim_FreeObjAnimData(base): release the pending object-anim data.
    void (*freeObjAnimData)(void* base);
    // VIBE_Light_UpdateDayCycle(rec): refresh the day-cycle light of `rec`.
    void (*lightUpdateDayCycle)(void* rec);
};
void SetCharRender5Hooks(const CharRender5Hooks* hooks);
const CharRender5Hooks& GetCharRender5Hooks();

// ===========================================================================
// FlushPendingMesh global state (the engine's dword_62D4E8 / dword_62D4E4 /
// dword_62D564 / off_649D64). Modeled as a small state block so the flush is
// exercisable. `pendingMesh+45` is the flag byte (bit 0x20 == "ready to free").
// ===========================================================================
struct PendingMeshState {
    void* pendingAnim;    // dword_62D4E8 (the object whose anim data is pending)
    u8    pendingFlag;    // *(pendingAnim+45) bit 0x20 gate
    int   flushBusy;      // dword_62D4E4 (set 1 while flushing, cleared on free)
    void* pendingLight;   // dword_62D564 (light rec to refresh)
    void* pendingLightScene; // *(pendingLight+520) (its owning scene ptr)
    void* activeScene;    // off_649D64 (the active scene ptr)
};
extern PendingMeshState g_pendingMesh;
void ResetCharRender5();

// ===========================================================================
// Functions.
// ===========================================================================

// gilde.exe 0x403474 — VIBE_Character_CheckQueueReady. True (1) unless the primary
// action stream (+112) is present and its stream cursor has not yet reached the
// stream's end frame (with the +109 0x20 "loop" bit clear). Pure predicate.
bool CheckQueueReady(const ChActor* actor);

// gilde.exe 0x4521cc — VIBE_Character_UpdateNeedsDecay. Advances the 13-entry need
// table of one person record by its per-need rate/decay scalars, clamps each to
// [0,1000], and returns the index of the now-dominant need (also recorded at +304
// and, when it changed, the primary value at +300 is reset and the new dominant
// need is nudged by +flt_619120). `person` is the raw 536-byte record; `seed` is
// the incoming dominant-index hint (engine passes it in bl). Faithful 1:1 port.
int UpdateNeedsDecay(u8* person, int seed);

// gilde.exe 0x452190 — VIBE_Character_UpdateAllNeeds. Runs UpdateNeedsDecay over
// every live person in the array (skipping free slots, marker == -1). Returns the
// last dominant-need index produced. `persons` is the 768*536 person array base.
int UpdateAllNeeds(u8* persons, int personCount, int seed);

// gilde.exe 0x4b092c — VIBE_Character_CountByType. Walks an actor's 3-entry owner
// list (dwords at base+20..base+32, by person id), resolves each via
// PersonFindRecordById, and tallies how many match the owner-object's role byte at
// +559 (into out[+20]) versus +560 (into out[+24]). `idTriple` = the 3 person ids;
// `out2`/`out3` receive the two counts. `roleAt(id)` supplies the resolved person's
// matched-role byte and the +559/+560 expected pair via the object def. Returns the
// total scanned. (The render-callee-free data rule; def-table reads via hook arg.)
struct CountByTypeDefs { u8 wantA; u8 wantB; };  // object-def +559 / +560
int CountByType(const int* idTriple, int triple,
                u8 (*resolveRole)(int personId, CountByTypeDefs* defs),
                int* out2, int* out3);

// gilde.exe 0x4b1e08 — VIBE_Character_GetIndex_Thunk. A thunk to
// VIBE_GameTime_GetSeasonFromDay (day % 4). Reuses the season rule.
int GetIndexThunk(int day);

// gilde.exe 0x43d8f0 — VIBE_Character_SetCameraViewMode. Dispatches a camera-view
// name string ("CLOSEUP"/"LEFT_SHOULDER"/"RIGHT_SHOULDER"/"EGO") to
// SetupAttachCamera(actor, mode 0..3) via the hook; reports a script error and
// returns 1 for a null actor, else returns 0. `actorPtr0` is *(a1) (0 == null).
int SetCameraViewMode(int actorPtr0, void* actor, const char* viewName);

// gilde.exe 0x43d208 — VIBE_Character_CmdGetCharacterHandle. FindByName(name);
// reports "character '%s' not found" on miss. Returns the actor (or null on miss).
void* CmdGetCharacterHandle(const char* name);

// gilde.exe 0x43de10 — VIBE_Character_CmdGetCharacterSubObjectHandle. For a valid
// actor, ObjectFindByHandle(mesh, 320, name, 0, extra); else reports an error and
// returns null. `actorPtr0` == *(a1) (0 == invalid).
void* CmdGetCharacterSubObjectHandle(int actorPtr0, void* meshBase, int name, int extra);

// gilde.exe 0x405894 — VIBE_Character_StopSample. If the sample-active flag
// (+140 & 0x10) is set and a morph anim is attached (+116): touch mesh frames (when
// the mesh has no material at +460) or prune the attachment, clear +116, then clear
// the sample-active flag.
void StopSample(ChActor* actor);

// gilde.exe 0x404650 — VIBE_Character_QueryTerrainType. Resolves the actor's mesh
// heightmap, probes the actor world pos to a tile, reads the terrain code there;
// then (when `force` or the code is a walkable kind) snaps the actor onto the floor
// pick and re-sets its position. Returns the terrain code (0 on failure).
// `force` == the a2 flag.
int QueryTerrainType(ChActor* actor, int force);

// gilde.exe 0x404f6c — VIBE_Character_ComputeTargetTile. Picks a random nearby tile
// (±4 of the source tile, clamped to [2, mapDim-2]) on the active-scene heightmap,
// validates it with a line-of-sight trace, and converts the chosen tile back to a
// world position in `outWorld`. Returns 1 on success, 0 if no active scene.
// `srcWorld` is the source world pos; uses util::RandomModulo for the offsets.
int ComputeTargetTile(void* activeMap, const float* srcWorld, float* outWorld);

// gilde.exe 0x426924 — VIBE_Character_FlushPendingMesh. If a pending object-anim is
// queued and its +45 0x20 flag is set, frees it (and clears the busy/pending
// state); separately, if a pending light belongs to the active scene, refreshes its
// day cycle. Operates on g_pendingMesh; the two leaves go through the hook.
void FlushPendingMesh();

} // namespace guild::sim
