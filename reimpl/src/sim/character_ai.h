#pragma once
// character_ai — the per-turn character AI behaviour + visibility/freeze/animation
// leaves of gilde.exe (32-bit x86, imagebase 0x400000) reached from the frame loop
// (0x4c09a0) and the render init (0x527fa4). Faithful 1:1 ports of:
//
//   0x4526d8  VIBE_Character_FindNearestTarget       (target selection brain)
//   0x452d38  VIBE_Character_UpdateGuardBehavior     (guard-spawn + AI dispatch)
//   0x40244c  VIBE_Character_UpdateLowPolyMesh       (LOD mesh swap)
//   0x426488  VIBE_Character_LoadObjectAnimation     (attach .baf/.oam anim)
//   0x401894  VIBE_Character_SetVisible
//   0x40238c  VIBE_Character_SetAllFreezeState
//   0x405504  VIBE_Character_StandUp
//
// These functions read large global person tables (word_12CE910 stride 536, the
// rank/favourability byte planes byte_1333110 / unk_133310D, the He-handler ring)
// and call object/anim/command leaves owned by sibling modules. To keep the
// reconstruction testable AND faithful (rule 1) we model the touched globals as a
// PersonTable view passed through CharacterAiHooks, and route the cross-module
// leaves through the same hook set with inert defaults (tests install real ones).
//
// W19-OBJANIM owns the anim leaves (VIBE_Anim_*, VIBE_Mesh_LoadObjectAnimation,
// VIBE_Object_ToggleSuspendStateNamed …); we reference them through hooks rather
// than redefining them (no ODR). The deterministic decision math (the favourability
// curve, the walk-step clamp, the rank gate) is shared with character_path.h and
// re-exported here for golden tests.
//
// The constants/curve helpers (kRankDeltaScale, NearestTargetRankWeight,
// NearestTargetWalkSteps …) already live in sim/character_path.h — we include and
// reuse them, never redefine.

#include "guild/common/types.h"
#include "sim/character_path.h"   // constants + NearestTargetRankWeight / WalkSteps

namespace guild::sim {

// ===========================================================================
// Person-table view. The originals index three parallel arrays keyed by a person
// index p in [0,768):
//   word_12CE910[268*p + ...]  — the person record block (stride 536 bytes; the
//                                scan reads the type byte planes byte_12CE912[536*p]
//                                and word_12CE910[2*p+1], and ids at dword_12CE914).
//   byte_1333110[id + 768*p]   — the favourability/affinity byte plane (signed).
//   unk_133310D + 768*p + id   — the high-byte rank plane (read as >>24).
//   dword_123D6CD[192*p]       — the secondary affinity plane (>>24).
// We expose just the scalar reads the two AI functions need, through callbacks, so
// a synthetic table can drive exact golden vectors without modelling 411 KB of
// globals. Every callback name documents the original global + index expression.
// ===========================================================================
struct PersonTable {
    // byte_12CE912[536 * p]  — the person "state/type" byte (6,7 == enemy types).
    u8 (*stateType)(int p);
    // word_12CE910[268 * p + 1]  — the person "class" word (2,3,4,5 == factions).
    u16 (*classWord)(int p);
    // byte_12CEA76[536 * p] / byte_12CEA79[536 * p]  — two "hostile" flag planes.
    u8 (*hostileA)(int p);
    u8 (*hostileB)(int p);
    // byte_1333110[id + 768*p]  — signed affinity byte for (person p, ref id).
    i8 (*affinity)(int p, int id);
    // (*(int*)(unk_133310D + 768*p + id)) >> 24  — rank-plane high byte.
    int (*rankPlaneHi)(int p, int id);
    // (*(int*)(dword_123D6CD[192*p] + id)) >> 24  — secondary affinity high byte.
    int (*affinityPlaneHi)(int p, int id);
    // dword_12CE914[134 * p]  — the person's primary linked-object id.
    int (*linkedObjectId)(int p);
    // &word_12CE910[268 * p]  — base of person p's record (for delta fields).
    void* (*recordBase)(int p);
};

// ===========================================================================
// Cross-module AI leaves + command-delta codec. Inert defaults defined in the
// .cpp; tests install real wiring. Field provenance addresses in comments.
// ===========================================================================
struct CharacterAiHooks {
    PersonTable persons;

    // VIBE_Person_ComputeOfficeRank(idx, stopAtSelf)  (0x58bccc)
    int (*computeOfficeRank)(int idx, int stopAtSelf);
    // VIBE_Ai_ComputePersonFavorability(candId, refId, mode)  (0x594330) 0..100
    double (*computeFavorability)(int candId, int refId, int mode);
    // VIBE_Math_RandomModulo(n)  (0x58b89c)
    int (*randomModulo)(u16 n);
    // VIBE_Person_FindRecordById(id)  (0x58bc6c)
    void* (*personFindRecordById)(int id);

    // dword_63C744 — the "debug speed" walk-step base.
    int debugSpeed;

    // --- command-delta codec emitted by FindNearestTarget ---
    void (*beginDeltaPacket)(void* actor, int id);                            // 0x493a94
    void (*appendDeltaField)(unsigned size, unsigned count, const void* src,
                             unsigned field);                                 // 0x493aec
    void (*appendRawField)(unsigned size, unsigned count, const void* src,
                           unsigned off);                                     // 0x493c14
    void (*queueRequestState22)();                                           // 0x494750
    void (*queueRequestCoord27)(int objId, int actorId, int c);             // 0x494878
    void (*historyNotifyTargetReachedA)(void* actor, void* rec);            // 0x535bb0
    void (*historyNotifyTargetReachedB)(void* actor, void* rec);            // 0x535c68
    void (*historyNotifyTargetFound)(void* actor, void* rec);               // 0x535afc

    // --- UpdateGuardBehavior leaves ---
    // VIBE_Character_IsActiveTypeForTurn(actor)  (0x452660)
    int (*isActiveTypeForTurn)(void* actor);
    // VIBE_Command_QueueRequestGuardTarget61(target, mode, b, c)  (0x49514c)
    void (*queueRequestGuardTarget61)(void* target, int mode, int b, int c);
    // VIBE_AiNeeds_EvaluateActions(actor)  (0x47852c)
    void (*aiNeedsEvaluateActions)(void* actor);
    // VIBE_AiMethod_SelectBestForPerson(p)  (0x469248) -> nonzero if a method chosen
    int (*aiMethodSelectBest)(int p);
    // VIBE_AiMethod_ExecuteSelected(p)  (0x469a18)
    int (*aiMethodExecuteSelected)(int p);
    // byte_B572A1[4*(5*v12 + 32*v9)] (0xB572A1) — the AI-method descriptor table
    // probed in the re-dispatch test at 0x452f22: returns that byte (the guard pass
    // checks bit 1 (mask 2)). v9 = prev method (actor[306], signed), v12 = selected
    // method (actor[303]>>24). Inert default: 0 (no forced re-dispatch).
    unsigned char (*aiMethodTableByte)(int v9, int v12);
    // dword_62EB8C — the "current target id" scratch the guard pass parks.
    int* currentTargetSlot;

    // --- low-poly mesh / visibility / freeze leaves (W19-OBJANIM owns the anim ones) ---
    // These take/return opaque node handles (the original int pointers).
    int (*objectToggleSuspend)(int node, int enable, int actor);            // 0x5b4274
    int (*lightBuildObjectCache)(int node);                                 // 0x5c8218
    int (*objectSetPosition)(int node, const float* vec3);                  // 0x5af38c
    int (*objectSetWorldTranslation)(int node, const float* vec3);          // 0x5af50c
    bool (*vectorWithinTolerance)(const float* a, const float* b, float t);  // 0x5caa4c
    int (*animFindFreeMeshSlot)();                                          // 0x5cf114
    int (*animLoadStreamToStock)(const char* path, int flag);              // 0x5d3858
    int (*animAttachToBone)(int boneSlot, int meshHandle);                 // 0x5d0b64
    int (*characterDrawSubMeshes)(int node);                               // 0x4266b0
    int (*meshLoadObjectAnimation)(const char* path, int actor, int flag); // 0x5d367c
    // VIBE_Universe_RestoreObjectStates / SwitchActiveSlot / DetachAndRelease.
    void (*universeRestoreObjectStates)(int node, int vis);                // 0x5b43f0
    int  (*universeSwitchActiveSlot)(int a, int b, int c, int d);          // 0x5b4a24
    void (*objectDetachAndRelease)(int node);                              // 0x5b4258
    int  (*characterIndexFromPointer)(int universe);                       // 0x426724
    void (*reportError)(const char* msg);                                  // 0x438da8

    // --- StandUp ---
    void (*actionQueueUnlinkEntry)(int entry);                             // 0x404370
    void (*characterCreateSampleLoopAction)(int actor);                    // 0x40598c

    // --- node-field accessors (the originals dereference *(_TYPE*)(actor+off)) ---
    int (*readNodeI32)(int node, int byteOff);
    void (*writeNodeI32)(int node, int byteOff, int value);
    unsigned char (*readNodeU8)(int node, int byteOff);
    void (*writeNodeU8)(int node, int byteOff, unsigned char value);

    // globals that gate UpdateLowPolyMesh:
    int  lowPolyMode;        // dword_62D088 (0=full,1=lod-by-distance,2=frozen)
    int  lowPolyCellX;       // dword_62D080
    int  lowPolyCellY;       // dword_62D084
    int  cameraNode;         // dword_13FCD1C (active camera for distance test)
    int  sceneLodAnchor;     // byte_13ECEC8 (the parent that enables LOD swap)
    float lodSwapDistance;   // flt_6100FC == 1500.0
    int  oamPathPrefixIsSet; // dword_1406110 != 0 (anim path prefix configured)
    // dword_1406110 — the .oam path prefix string passed as the first "%s" of the
    // "%s%s.oam" format at 0x4265d3. Null models an empty prefix (host installs the
    // real pointer); LoadObjectAnimation formats sprintf(path,"%s%s.oam",prefix,baf).
    const char* oamPathPrefix;
};

CharacterAiHooks CharacterAiSetHooks(const CharacterAiHooks* hooks);
const CharacterAiHooks& CharacterAiGetHooks();

// gilde.exe 0x4526d8 — VIBE_Character_FindNearestTarget.
// `actor` is the live-actor record handle (the original's a1, a u16* whose dwords
// [1]=id, [131]=current-target id, plus the flag bytes at +358/+361/+2 and the
// engage counters at +529/+530). Fields are read through the hooked node accessors.
// Selects the best hostile person to engage and emits the command-delta packet to
// move/attack it; returns the chosen target record (or 0).
void* FindNearestTarget(int actor);

// gilde.exe 0x452d38 — VIBE_Character_UpdateGuardBehavior.
// Per-turn pass for a guard-type actor: spawns guard targets from the building
// roster, then (if the actor is "ready") runs FindNearestTarget + the AI needs/
// method evaluators. Returns the last status byte (matches the original's al).
unsigned char UpdateGuardBehavior(int actor);

// gilde.exe 0x40244c — VIBE_Character_UpdateLowPolyMesh.
// Refreshes the actor's LOD mesh: full mesh vs low-poly proxy based on the global
// lowPolyMode + camera distance + cell visibility. Returns the actor (passthrough).
int UpdateLowPolyMesh(int actor);

// gilde.exe 0x426488 — VIBE_Character_LoadObjectAnimation.
// Loads (`a2` = "<name>.baf") and attaches an object animation to the actor's
// attach bone; falls back to "<prefix><name>.oam". `a3` is the flag bitset
// (1=clear-loop, 2=loop, 4=once, 0x10=ping-pong, 0x20=reverse). Returns 1 on a
// successful attach, 0 / the loader result otherwise.
int LoadObjectAnimation(int actor, const char* baf, int flags);

// gilde.exe 0x401894 — VIBE_Character_SetVisible.
void SetVisible(int actor, int visible);

// gilde.exe 0x40238c — VIBE_Character_SetAllFreezeState (al=state, edi=arg).
// Walks all 512 live actors switching their universe slot and (state-dependent)
// dropping/refreshing the low-poly proxy; sets the global lowPolyMode = 2 - state.
unsigned char SetAllFreezeState(unsigned char state, int arg);

// gilde.exe 0x405504 — VIBE_Character_StandUp. Cancels a "sitting" actor's queued
// actions and (if its +0x8C bit 4 is set) restarts the idle sample loop. Returns
// 1 if it had an action queue, 0 if not, and the (null) actor on a null input.
int StandUp(int actor);

// The live-actor slot table dword_66F0D0[512] and dword_649D60 (the global slot
// anchor) used by SetAllFreezeState are owned by character_query.cpp; we reuse them
// via the readNodeI32/g_live wiring rather than redefining.
extern int g_freezeSlotAnchor;  // dword_649D60 view (host installs the real value)

} // namespace guild::sim
