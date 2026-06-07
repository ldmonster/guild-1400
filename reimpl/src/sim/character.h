#pragma once
// Live scene-actor (Character) substrate for the Guild simulation (gilde.exe).
//
// The Character record is the in-scene actor; live actors live in the 512-slot
// pointer array dword_66F0D0 @0x66F0D0 and are driven once per frame by
// VIBE_Character_Update, which (for actors with an active action) hands off to
// the action-queue coroutine driver (actionqueue.h). This header recovers the
// Character field offsets the action system touches and declares the handful of
// representative per-action step handlers (turn / take / drop) plus Update.
//
// Translated functions:
//   VIBE_Character_Update                  0x405148  (live-array driver)
//   VIBE_Character_TurnStepActionUpdate    0x406a18  (type 7, skeleton)
//   VIBE_Character_TakeObjectActionUpdate  0x405c88  (type 49)
//   VIBE_Character_DropObjectActionUpdate  0x405f28  (type 50)
//   VIBE_Character_LoadAnimActionUpdate    0x406250  (type 54)
//   VIBE_Character_TurnToTargetActionUpdate 0x40618c (type 53, skeleton)
#include "guild/common/types.h"

namespace guild::sim {

struct ActionNode;

// ===========================================================================
// Character record  (live scene actor; dword_66F0D0[0..511] @0x66F0D0)
// ---------------------------------------------------------------------------
// Offsets recovered from VIBE_Character_Update (0x405148), DispatchCurrent
// (0x404768), StepMotionQueue (0x4041e8) and the step handlers. The full record
// is large and render/anim-heavy; we model the fields the action system reads.
//   +112 : current motion/anim handle (0 == none) — the dispatch motion gate.
//   +128 : NextAnim ptr (take/drop gate; nonzero blocks completion).
//   +133 : chained-phase latch byte (DispatchCurrent compares vs +112's +108).
//   +136 : universe/scene tag (compared to the active scene off_649D64).
//   +140 : flag byte A (0x02 dirty-anim, 0x04 skip-update, 0x08 dirty-mesh,
//                       0x20 hidden).
//   +141 : flag byte B (0x10 idle-anim pending, 0x20 sit/anim, 0x80 busy).
//   +296 : action-queue head (current ActionNode*).
//   +400 : abort flag byte (forces motion/duration handlers to finish).
// Render-only fields (+20 avatar, +52 mesh, +292 target world XYZ, waypoints …)
// are out of scope and modeled as opaque pointers / a small carried-flag mirror.
// ===========================================================================
struct SocialAvatar; // character_social.h

struct Character {
    void*      avatar;        // +0x14 (+20)  avatar/entity struct (render; opaque)
    SocialAvatar* social;     // avatar view (worldId/groupId/pos) for the social scan
    void*      mesh;          // +0x34 (+52)  mesh handle (render; opaque)
    ActionNode* motion;       // +0x70 (+112) current motion/anim handle (gate)
    void*      animB;         // +0x7C (+124) secondary anim handle (SampleLoop gate)
    void*      nextAnim;      // +0x80 (+128) NextAnim ptr (take/drop completion gate)
    u8         phaseLatch;    // +0x85 (+133) chained-phase latch byte
    void*      universe;      // +0x88 (+136) universe/scene tag
    ActionNode* actions;      // +0x128 (+296) action-queue head
    u8         flagsA;        // +0x8C (+140) flag byte A
    u8         flagsB;        // +0x8D (+141) flag byte B
    u8         abort;         // +0x190 (+400) abort flag
    float      targetWorldX;  // +0x124 (+292) current target world X (idle-social gate: nonzero => skip)
    void*      scene;         // +0x88-adjacent: resolved scene slot (idle-social / use-gate)
    int        slotIndex;     // VIBE_Character_IndexFromPointer(universe): scene slot index

    // --- reconstruction-only mirrors (not in the original byte layout) -------
    // The original take/drop handlers attach/detach a carried item via the
    // renderer (AttachItemToBone). We mirror the observable result here so the
    // coroutine model is testable without the render bridge.
    int        carried;       // 0 == nothing carried, else carried bone id+1
    int        animTicks;     // mock anim countdown (set by attachAnim hook)
    float      yaw;           // mock facing angle for the turn handler
    int        turnTarget;    // mock target yaw quantum
    int        visible;       // mirror of the visible flag for FinishSetVisible
};

// ===========================================================================
// Live-character array  (dword_66F0D0 @0x66F0D0, 512 slots; slot != null == live)
// ===========================================================================
constexpr int kCharacterCapacity = 512;
extern Character* g_characters[kCharacterCapacity];

// Original: dword_62D094 @0x62D094 — live-character count gate (Update bails if 0).
extern int g_characterCount; // dword_62D094

// Test/setup helper (not in the original): clears the live array + count.
void ResetCharacters();

// ===========================================================================
// Per-frame driver.
// ===========================================================================
// gilde.exe 0x405148 — VIBE_Character_Update. Walks dword_66F0D0[0..511]; for
// each live actor not flagged skip-update (+140 & 0x04): if it has an active
// action (+296) it runs the coroutine via DispatchCurrent, else it would run the
// idle/social behaviour (which needs the pathfinder/heightmap and is deferred —
// see report). Brackets the frame by latching the tick clock. Returns 1 if it
// ran (live count > 0), else 0.
int CharacterUpdate();

// ===========================================================================
// Representative step handlers (registered by RegisterHandlers).
// ===========================================================================

// gilde.exe 0x406a18 — VIBE_Character_TurnStepActionUpdate (type 7). Skeleton:
// drives the turn animation one quantum per tick toward the encoded target yaw
// (via the turnStep hook); frees the node when the turn completes.
void TurnStepActionUpdate(ActionNode* node);

// gilde.exe 0x40618c — VIBE_Character_TurnToTargetActionUpdate (type 53).
// Computes the signed angle to the target and chains a type-7 turn action, then
// frees itself. Modeled via the turn hook + InsertActionAfter.
void TurnToTargetActionUpdate(ActionNode* node);

// gilde.exe 0x405c88 — VIBE_Character_TakeObjectActionUpdate (type 49). Sets the
// carried flag once the take animation reaches its trigger frame, then finishes.
void TakeObjectActionUpdate(ActionNode* node);

// gilde.exe 0x405f28 — VIBE_Character_DropObjectActionUpdate (type 50). Clears
// the carried flag once the drop animation reaches its trigger frame, finishes.
void DropObjectActionUpdate(ActionNode* node);

// gilde.exe 0x406250 — VIBE_Character_LoadAnimActionUpdate (type 54). Attaches a
// named animation; frees the node once the animation has played out.
void LoadAnimActionUpdate(ActionNode* node);

} // namespace guild::sim
