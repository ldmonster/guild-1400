#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::play — menu / screen-transition / fade-all / hotspot leaves of
// gilde.exe.  Faithful 1:1 reconstructions of:
//
//   0x41f47c  VIBE_Fade_UpdateAll          (sweep the fade-slot table, Update each)
//   0x41f4b8  VIBE_Fade_UnregisterAll      (sweep + Unregister each, then zero slot)
//   0x421ab8  VIBE_Hotspot_Register        (insert a 16-byte hotspot rect; cap 64)
//   0x421b18  VIBE_Hotspot_Remove          (memset a record to 0, decrement count)
//   0x56d2cc  VIBE_Transition_FadeOutToBlack
//   0x56d3b0  VIBE_Transition_FadeInScene
//
// The pure table / loop / arithmetic logic is reproduced exactly. Engine edges
// (per-slot Fade_Update, Fade_Register/Unregister, the frame-loop pump, the
// window/scene render calls) are routed through an inert-default hooks vtable so
// this translation unit is self-contained and orphan-free for the headless build.
//
// NOTE: the per-slot siblings VIBE_Fade_Register (0x41f0e8), VIBE_Fade_Unregister
// (0x41f18c) and the alpha core of VIBE_Fade_Update (0x41f1cc) are ALREADY present
// in the reimpl (src/gui/menu_frame_leaves.cpp + src/render/fade.cpp) and are NOT
// redefined here — this file adds only the not-yet-present *-All / hotspot /
// transition leaves and drives the existing ones through hooks.
// =============================================================================
namespace guild::play {

// ---------------------------------------------------------------------------
// Fade-slot table (dword_672280). 32 dword slots = 128 bytes; the *-All loops
// iterate by byte offset 0..128 step 4, exactly as the original.
// ---------------------------------------------------------------------------
inline constexpr int kFadeSlotCount = 32;        // 128 bytes / 4

// The fade-slot table this module sweeps. Mirrors dword_672280[32]; each entry is
// a fade-record handle (0 == empty). Exposed so a host can share the table with the
// per-slot Register/Unregister implementation.
struct FadeSlotTable {
    i32 slots[kFadeSlotCount] = {0};
};
FadeSlotTable& FadeSlots();          // module-owned dword_672280

// ---------------------------------------------------------------------------
// Hotspot table (unk_75BA38 base, dword_75BEE8 count, dword_62D31C mirror).
// 16-byte records; valid indices 1..64 (index 0 is unused — Register pre-increments).
// Field layout recovered from the Register writes:
//   +0x00 (w0)  WORD = a1 (ax)        +0x02 (w1)  WORD = a2 (dx)
//   +0x04 (w2)  WORD = a4 (bx)        +0x06 (w3)  WORD = a3 (cx)
//   +0x08 (payload) DWORD = a5        +0x0C (active) DWORD = 1
// ---------------------------------------------------------------------------
inline constexpr int kHotspotMax  = 64;          // dword_75BEE8 > 64 -> reject
inline constexpr int kHotspotSlots = kHotspotMax + 1; // indices 0..64

struct HotspotRec {                  // 16 bytes (unk_75BA38 stride)
    u16 w0 = 0;     // +0x00  a1 (ax)  — rect origin X at the create site
    u16 w1 = 0;     // +0x02  a2 (dx)  — rect origin Y
    u16 w2 = 0;     // +0x04  a4 (bx)  — second dimension word
    u16 w3 = 0;     // +0x06  a3 (cx)  — first dimension word
    i32 payload = 0;// +0x08  a5
    i32 active = 0; // +0x0C  set to 1 by Register
};

struct HotspotTable {
    HotspotRec recs[kHotspotSlots] = {}; // unk_75BA38 (record 0 unused)
    i32 count = 0;                       // dword_75BEE8
    i32 mirror = 0;                      // dword_62D31C (== last assigned count)
};
HotspotTable& Hotspots();

// gilde.exe 0x421ab8 — VIBE_Hotspot_Register@<eax>(ax,dx,cx,bx, a5).
// Pre-increments the count (== new index), mirrors it to dword_62D31C, and if the
// index exceeds 64 returns 0 (the count stays incremented, exactly as the original).
// Otherwise fills recs[index] and returns the index.
//   x  : a1 (ax) -> +0x00      y  : a2 (dx) -> +0x02
//   cx : a3      -> +0x06      bx : a4      -> +0x04
//   payload : a5 -> +0x08
i32 Hotspot_Register(i16 x, i16 y, i16 cx, i16 bx, i32 payload);

// gilde.exe 0x421b18 — VIBE_Hotspot_Remove@<eax>(index).
// If count>=1 and index>=0: zero the 16-byte record (VIBE_Light_SetGrayColorThunk
// -> memset 16 bytes to 0) and decrement the count.
void Hotspot_Remove(i32 index);

// ---------------------------------------------------------------------------
// Fade-all sweeps.
// ---------------------------------------------------------------------------
struct FadeAllHooks {
    // VIBE_Fade_Update(slot, back) — advance one fade. Default no-op.
    void (*fadeUpdate)(i32 slotHandle, i32 backSurface) = nullptr;
    // VIBE_Fade_Unregister(slot, arg) — release one fade. Default no-op.
    void (*fadeUnregister)(i32 slotHandle, i32 arg) = nullptr;
    // dword_62D210 — back surface id passed to Fade_Update. Default 0.
    i32 backSurface = 0;
};
const FadeAllHooks* SetFadeAllHooks(const FadeAllHooks* hooks); // nullptr -> defaults

// gilde.exe 0x41f47c — VIBE_Fade_UpdateAll().
// for (i=0; i!=128; i+=4) if (slot) Fade_Update(slot, dword_62D210);  (skips empties).
void Fade_UpdateAll();

// gilde.exe 0x41f4b8 — VIBE_Fade_UnregisterAll@<eax>(arg@edi).
// for (i=0; i!=128; i+=4) if (slot) { Fade_Unregister(slot, arg); slot = 0; }
void Fade_UnregisterAll(i32 arg);

// ---------------------------------------------------------------------------
// Screen transitions (0x56d2cc / 0x56d3b0). State sequencers around the fade
// register/pump/render leaves; routed through this hooks vtable.
// ---------------------------------------------------------------------------
struct TransitionHooks {
    // VIBE_Fade_Register(0,0/dy, scrW, scrH, "BLACK", dur, step) -> fade handle.
    // Returns a record handle whose +0 flags byte must expose bit2 (done) via fadeDone().
    i32 (*fadeRegister)(i32 x, i32 y, i32 scrW, i32 scrH, const char* tag,
                        i32 dur, i32 step) = nullptr;
    // *(handle) & 4 — read the +0 flags byte and test bit2 (fade complete).
    bool (*fadeDone)(i32 handle) = nullptr;
    // VIBE_GameLogic_RunFrameLoop(state, sel, scratch) — pump one frame.
    void (*runFrameLoop)(i32 state, i32 sel, void* scratch) = nullptr;
    void (*fadeUnregister)(i32 handle, void* scratch) = nullptr;
    void (*hudToggleHighlight)(i32 on) = nullptr;       // VIBE_Hud_ToggleObjectHighlight
    void (*renderEntityScene)(i32 a, i32 b) = nullptr;  // VIBE_Window_RenderEntityScene
    void (*renderEntityList)(i32 arg) = nullptr;        // VIBE_Window_RenderEntityList
    void (*groundplanSetVisible)(i32 on) = nullptr;     // VIBE_Groundplan_SetWidgetsVisible
    void (*groundplanFadeIn)(void* scratch) = nullptr;  // VIBE_Groundplan_FadeInScene

    // Engine globals the transitions read/write:
    i32 scrW = 0;        // *(int*)((char*)&dword_69FFB8 + 2) >> 16  (clip width)
    i32 scrH = 0;        // dword_69FFBC >> 16                       (clip height)
    i32 sceneSnapshot = 0;   // dword_63CC30 (FadeInScene: !=0 -> short path)
    // dword_63CC4C/50/54/58 -> dword_69FF80/84/88/8C scene-rect restore (FadeInScene)
    i32 srcLeft = 0, srcRight = 0, srcTop = 0, srcBottom = 0; // 63CC4C/54/50/58
};
const TransitionHooks* SetTransitionHooks(const TransitionHooks* hooks);

// Scene-rect globals dword_69FF80/84/88/8C, written by both transitions.
struct SceneRect { i32 x80 = 0, x84 = 0, x88 = 0, x8c = 0; };
SceneRect& SceneRectGlobals();          // dword_69FF80..8C
i32& FrameStateWord();                  // dword_11BC2D0
i32& DirtyFlag();                       // dword_631638 (FadeInScene short path)

// gilde.exe 0x56d2cc — VIBE_Transition_FadeOutToBlack@<eax>(scratch@edi).
// Returns the handle of the trailing 50/10 BLACK fade (last Fade_Register result).
i32 Transition_FadeOutToBlack(void* scratch);

// gilde.exe 0x56d3b0 — VIBE_Transition_FadeInScene@<eax>(scratch@edi).
// Returns either Fade_Unregister's result (snapshot path) or the trailing fade handle.
i32 Transition_FadeInScene(void* scratch);

// Reset all module state (slot table, hotspots, scene rect, frame word).
void ResetMenuReconTransition();

} // namespace guild::play
