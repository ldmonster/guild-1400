#pragma once
// History-event ("He") engine tail — gilde.exe VIBE_He_* cluster (0x4c4xxx..0x4c6cxx).
//
// The "He" subsystem is the Guild's history/event-notification engine: it owns
// the per-player news/punishment record tables and the floating 3D "event icon"
// pool that hovers over entities to advertise pending events. The History
// notification wrappers (VIBE_History_Notify*, already in world/history*.cpp) feed
// this engine; the Statistics/Stammbaum windows read its tables. The exact
// VIBE_History_*/VIBE_Statistics_*/VIBE_Stammbaum_* prefixes are fully
// reconstructed, so this file translates the still-untranslated deterministic
// slice of the engine they wire into:
//
//   0x4c45e4 VIBE_He_ValidatePunishmentType  — pre-flight check of a punishment
//       code (0..9) against a target person record (class byte +358, flag +13).
//   0x4c5244 VIBE_He_NullHandler             — the no-op handler slot.
//   0x4c51b4 VIBE_He_LogInvalidAllocType     — "he_AllocNone(): Invalid HE-Type"
//       diagnostic (sprintf of type + offending player name).
//   0x4c51fc VIBE_He_LogInvalidRunType       — "he_RunNone(): Invalid HE-Type"
//       diagnostic; then falls through to NullHandler.
//   0x4c5018 VIBE_He_ProcessAllPlayerNews    — sweep slots 0..767, dispatching
//       per-player news (VIBE_He_ProcessPlayerNews).
//   0x4c68d8 VIBE_He_DestroyIconGfx          — tear down one event-icon slot
//       (release mesh/universe node; rearrange the survivors).
//   0x4c6c50 VIBE_He_DestroyStaleIcons       — sweep the 64-slot icon pool,
//       destroying icons whose owning entity no longer matches.
//   0x4c6ca4 VIBE_He_DestroyIconsForEntity   — destroy every icon owned by one
//       entity (the 1024-byte / 64 * 16 icon table).
//   0x4c67e0 VIBE_He_CreateGfxInfo           — allocate a free icon slot, bind it
//       to a parent entity, and build its mesh (mesh build routed through a hook).
//
// All cross-module leaves (mesh/object/universe teardown, the per-player news
// dispatch, the diagnostic sink) are routed through an installable WorldHistory2Hooks
// struct whose inert defaults are defined in world_history2.cpp, exactly like
// CourtCouncilHooks / AmtEconomy2Hooks. Tests install their own hooks; nothing in
// src/ is left referencing an undefined symbol.
#include "guild/common/types.h"

namespace guild::world {

// ---------------------------------------------------------------------------
// Event-icon slot. The original icon pool is the flat 64-entry array
// dword_11C6160 (16-byte stride): [0]=entityId (-1 == free), [1]=parentEntity,
// [2]=mesh object handle, [3]=universe node handle. Recovered byte-for-byte.
// ---------------------------------------------------------------------------
struct HeIconSlot {
    i32 entityId;   // +0x00  owning entity id (-1 == free slot)
    i32 parent;     // +0x04  parent entity pointer/handle (a1 in CreateGfxInfo)
    i32 mesh;       // +0x08  built mesh object handle (0 == none)
    i32 node;       // +0x0C  attached universe node handle
};

inline constexpr int kHeIconSlotCount = 64;   // dword_11C6160 .. (64 * 16 bytes)

// ---------------------------------------------------------------------------
// Installable cross-module leaves. Inert defaults (defined in the .cpp) make the
// whole module run deterministically with no live sim/render/universe.
// ---------------------------------------------------------------------------
struct WorldHistory2Hooks {
    // The diagnostic sink (VIBE_Crt_Sprintf_0 -> log). Default: drop. `text` is
    // the already-formatted message.
    void (*logMessage)(const char* text) = nullptr;

    // Per-player news dispatch (VIBE_He_ProcessPlayerNews @0x4c4be8). Default:
    // no-op. Called once per slot index 0..767 by ProcessAllPlayerNews.
    void (*processPlayerNews)(u16 slot) = nullptr;

    // Universe active-slot toggle (VIBE_Universe_SwitchActiveSlot @ ...). The
    // original brackets node create/destroy with two calls; the return is the
    // prior slot. Default returns 0.
    int (*universeSwitchActiveSlot)(int slot, int a, int b, int node) = nullptr;

    // VIBE_Object_FindByHandle @0x... — non-zero when the handle is live. Default
    // returns 1 (treat as live) so the rearrange path is exercised.
    int (*objectFindByHandle)(int node) = nullptr;

    // VIBE_Object_DetachAndRelease @0x... — release a universe node. Default: drop.
    void (*objectDetachAndRelease)(int node) = nullptr;

    // VIBE_He_ArrangeIconsInCircle @0x4c64bc — re-layout survivors around the
    // parent. Default: no-op.
    void (*arrangeIconsInCircle)(int parentEntity) = nullptr;

    // VIBE_He_CreateIconMesh @0x4c662c — build the mesh for a freshly-bound slot.
    // Returns the mesh handle (stored at slot.mesh). Default returns 0.
    int (*createIconMesh)(HeIconSlot* slot, int gfxArg, int parentEntity) = nullptr;

    // The "global icons enabled" gate (byte_123356B). Non-zero == enabled.
    // Default returns 1 (icons enabled).
    int (*iconsEnabled)() = nullptr;

    // Read the parent entity's id (the original loads *(parentEntity + 4) into
    // the slot's entityId). Default: identity (returns `parent`).
    int (*parentEntityId)(int parent) = nullptr;

    // Store the bound slot pointer back into the parent record (the original
    // writes it at *(parentEntity + 136)). Default: drop.
    void (*storeSlotInParent)(int parent, HeIconSlot* slot) = nullptr;
};

void SetWorldHistory2Hooks(const WorldHistory2Hooks* hooks);
const WorldHistory2Hooks& GetWorldHistory2Hooks();

// The 64-slot icon pool (the recovered dword_11C6160 table). Owned here.
HeIconSlot* HeIconPool();
// Reset the pool to all-free (entityId == -1, the rest zeroed). Test helper that
// mirrors the engine's table-init state; not a 1:1 function.
void HeIconPoolReset();

// ===========================================================================
// Translated functions.
// ===========================================================================

// gilde.exe 0x4c45e4 — VIBE_He_ValidatePunishmentType  (__usercall, eax=(rec@eax), edx=(type)).
// rec: pointer to the target person record (or 0). Returns 0 (ok) or a negative
// error: -1 (null record), -3 (type >= 10), -4 (eligibility byte missing).
int He_ValidatePunishmentType(const void* rec, u32 type);

// gilde.exe 0x4c5244 — VIBE_He_NullHandler. The empty handler slot.
void He_NullHandler();

// gilde.exe 0x4c51b4 — VIBE_He_LogInvalidAllocType  (__usercall, eax=(rec@eax), ecx=(playerName)).
// rec: pointer to the offending He record; its byte[0] is the HE-type and
// word[4] (+8) is the player index used to fetch the player name. Logs the
// "he_AllocNone(): Invalid HE-Type : %i, von Spieler %s" diagnostic.
void He_LogInvalidAllocType(const u8* rec, const char* playerName);

// gilde.exe 0x4c51fc — VIBE_He_LogInvalidRunType  (__usercall, eax=(rec@eax)).
// Logs "he_RunNone(): Invalid HE-Type : %i, von Spieler %s" then runs the
// null handler (the original tail-calls VIBE_He_NullHandler).
void He_LogInvalidRunType(const u8* rec, const char* playerName);

// gilde.exe 0x4c5018 — VIBE_He_ProcessAllPlayerNews.
// Dispatch per-player news for slots 0..767 (the original loop: edx=0; ax=dx;
// edx++; ProcessPlayerNews(ax); while edx < 0x300).
void He_ProcessAllPlayerNews();

// gilde.exe 0x4c68d8 — VIBE_He_DestroyIconGfx  (__usercall, eax=(slot@eax)).
// Tear down one icon slot. If it owns a node (slot.node != 0) the node is
// released and the survivors re-arranged; otherwise just the id/parent are
// cleared. Returns the low byte of the slot pointer image (faithful to the
// original char return) — callers ignore it.
void He_DestroyIconGfx(HeIconSlot* slot);

// gilde.exe 0x4c6c50 — VIBE_He_DestroyStaleIcons.
// Sweep the 64-slot pool: an active slot (node != 0) whose owning entity id no
// longer matches the parent's current id — or with icons globally disabled — is
// destroyed in place. Faithful to the original's re-scan-after-destroy loop.
void He_DestroyStaleIcons();

// gilde.exe 0x4c6ca4 — VIBE_He_DestroyIconsForEntity  (__usercall, eax=(parent@eax)).
// Destroy every icon whose parent (+4) equals `parent`.
void He_DestroyIconsForEntity(int parent);

// gilde.exe 0x4c67e0 — VIBE_He_CreateGfxInfo  (__usercall, eax=(parentEntity@eax), edx=(gfxArg), ebx=(parentName)).
// Allocate the first free icon slot, bind it to the parent entity, build its
// mesh (routed through the createIconMesh hook), and store the slot pointer back
// into the parent (the original writes it at parent+136). Returns 1 on success,
// 0 if icons are disabled / the pool is full / the parent handle is invalid.
int He_CreateGfxInfo(int parentEntity, int gfxArg, const char* parentName);

} // namespace guild::world
