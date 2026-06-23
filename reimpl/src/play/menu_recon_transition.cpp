// =============================================================================
// guild::play — see menu_recon_transition.h.
// 1:1 reconstructions of the fade-all / hotspot / screen-transition leaves.
// =============================================================================
#include "play/menu_recon_transition.h"

#include <cstring>

namespace guild::play {

// ---------------------------------------------------------------------------
// Module-owned state.
// ---------------------------------------------------------------------------
namespace {
FadeSlotTable g_fadeSlots;     // dword_672280[32]
HotspotTable  g_hotspots;      // unk_75BA38 + dword_75BEE8 + dword_62D31C
SceneRect     g_sceneRect;     // dword_69FF80/84/88/8C
i32           g_frameStateWord = 0; // dword_11BC2D0
i32           g_dirtyFlag = 0;      // dword_631638
} // namespace

FadeSlotTable& FadeSlots()      { return g_fadeSlots; }
HotspotTable&  Hotspots()       { return g_hotspots; }
SceneRect&     SceneRectGlobals() { return g_sceneRect; }
i32&           FrameStateWord() { return g_frameStateWord; }
i32&           DirtyFlag()      { return g_dirtyFlag; }

// ===========================================================================
// gilde.exe 0x421ab8 — VIBE_Hotspot_Register
//   v6 = ++dword_75BEE8; dword_62D31C = v6;
//   if ( v6 > 64 ) return 0;
//   v8 = &unk_75BA38 + 16*v6;
//   *((DWORD*)v8 + 3) = 1; *(WORD*)v8 = a1; *((WORD*)v8+1)=a2;
//   *((WORD*)v8+2)=a4; *((WORD*)v8+3)=a3; *((DWORD*)v8+2)=a5;
//   return v6;
// ===========================================================================
i32 Hotspot_Register(i16 x, i16 y, i16 cx, i16 bx, i32 payload) {
    HotspotTable& t = g_hotspots;
    i32 v6 = t.count + 1;        // v6 = dword_75BEE8 + 1
    t.count  = v6;               // dword_75BEE8 = v6
    t.mirror = v6;               // dword_62D31C = v6
    if (v6 > kHotspotMax)        // if ( v6 > 64 )
        return 0;                //   return 0  (count stays incremented)
    HotspotRec& r = t.recs[v6];  // &unk_75BA38 + 16*v6
    r.active  = 1;               // *((DWORD*)v8 + 3) = 1
    r.w0 = static_cast<u16>(x);  // *(WORD*)v8       = a1 (ax)
    r.w1 = static_cast<u16>(y);  // *((WORD*)v8 + 1) = a2 (dx)
    r.w2 = static_cast<u16>(bx); // *((WORD*)v8 + 2) = a4 (bx)
    r.w3 = static_cast<u16>(cx); // *((WORD*)v8 + 3) = a3 (cx)
    r.payload = payload;         // *((DWORD*)v8 + 2) = a5
    return v6;                   // return v6
}

// ===========================================================================
// gilde.exe 0x421b18 — VIBE_Hotspot_Remove
//   if ( dword_75BEE8 >= 1 && result >= 0 ) {
//       VIBE_Light_SetGrayColorThunk(0, 16, &unk_75BA38 + 16*result);  // memset 16 -> 0
//       --dword_75BEE8;
//   }
// ===========================================================================
void Hotspot_Remove(i32 index) {
    HotspotTable& t = g_hotspots;
    if (t.count >= 1 && index >= 0) {
        // VIBE_Light_SetGrayColorThunk(0,16,dst) -> VIBE_Memory_FillDwordAligned(dst,0,16):
        // a gray RGB of 0 is fill value 0; 16 bytes -> the whole 16-byte record.
        if (index >= 0 && index < kHotspotSlots)
            std::memset(&t.recs[index], 0, sizeof(HotspotRec)); // 16 bytes
        --t.count;               // --dword_75BEE8
    }
}

// ===========================================================================
// Fade-all hooks.
// ===========================================================================
namespace {
void DefaultFadeUpdate(i32, i32) {}
void DefaultFadeUnregister(i32, i32) {}
const FadeAllHooks kDefaultFadeAll = { &DefaultFadeUpdate, &DefaultFadeUnregister, 0 };
const FadeAllHooks* g_fadeAll = &kDefaultFadeAll;
} // namespace

const FadeAllHooks* SetFadeAllHooks(const FadeAllHooks* hooks) {
    const FadeAllHooks* prev = g_fadeAll;
    g_fadeAll = hooks ? hooks : &kDefaultFadeAll;
    return prev;
}

// ===========================================================================
// gilde.exe 0x41f47c — VIBE_Fade_UpdateAll
//   for ( i = 0; i != 128; i += 4 ) {
//       while ( !*(int*)((char*)dword_672280 + i) ) { i += 4; if (i==128) return; }
//       result = VIBE_Fade_Update(*(int*)((char*)dword_672280 + i), dword_62D210);
//   }
// The inner while skips empty slots; the outer loop re-tests the just-updated slot.
// ===========================================================================
void Fade_UpdateAll() {
    const FadeAllHooks& h = *g_fadeAll;
    i32* base = g_fadeSlots.slots;                  // dword_672280
    for (int i = 0; i != kFadeSlotCount; ) {        // i in dword units (orig: bytes, step 4)
        while (base[i] == 0) {                      // while ( !slot )
            ++i;                                    // i += 4
            if (i == kFadeSlotCount)                // if ( i == 128 ) return
                return;
        }
        h.fadeUpdate(base[i], h.backSurface);       // VIBE_Fade_Update(slot, dword_62D210)
        // outer-loop increment `i = v2 + 4`: after processing a non-empty slot, advance
        // by one dword (4 bytes) and re-enter the outer for (which re-tests i!=128).
        ++i;
    }
}

// ===========================================================================
// gilde.exe 0x41f4b8 — VIBE_Fade_UnregisterAll(arg@edi)
//   for ( i = 0; i != 128; i += 4 ) {
//       while ( !slot ) { i += 4; if (i==128) return; }
//       result = VIBE_Fade_Unregister(slot, a1);
//       slot = 0;
//   }
// ===========================================================================
void Fade_UnregisterAll(i32 arg) {
    const FadeAllHooks& h = *g_fadeAll;
    i32* base = g_fadeSlots.slots;
    for (int i = 0; i != kFadeSlotCount; ) {
        while (base[i] == 0) {
            ++i;
            if (i == kFadeSlotCount)
                return;
        }
        h.fadeUnregister(base[i], arg);  // VIBE_Fade_Unregister(slot, a1)
        base[i] = 0;                     // *(int*)(dword_672280 + v3) = 0
        ++i;
    }
}

// ===========================================================================
// Transition hooks.
// ===========================================================================
namespace {
i32  DefFadeRegister(i32, i32, i32, i32, const char*, i32, i32) { return 0; }
bool DefFadeDone(i32) { return true; }   // inert: report "done" so loops terminate
void DefRunFrameLoop(i32, i32, void*) {}
void DefFadeUnregister(i32, void*) {}
void DefHudToggle(i32) {}
void DefRenderScene(i32, i32) {}
void DefRenderList(i32) {}
void DefGroundVisible(i32) {}
void DefGroundFadeIn(void*) {}

TransitionHooks MakeDefaultTransitionHooks() {
    TransitionHooks t;
    t.fadeRegister        = &DefFadeRegister;
    t.fadeDone            = &DefFadeDone;
    t.runFrameLoop        = &DefRunFrameLoop;
    t.fadeUnregister      = &DefFadeUnregister;
    t.hudToggleHighlight  = &DefHudToggle;
    t.renderEntityScene   = &DefRenderScene;
    t.renderEntityList    = &DefRenderList;
    t.groundplanSetVisible= &DefGroundVisible;
    t.groundplanFadeIn    = &DefGroundFadeIn;
    return t;
}
const TransitionHooks kDefaultTransition = MakeDefaultTransitionHooks();
const TransitionHooks* g_trans = &kDefaultTransition;
} // namespace

const TransitionHooks* SetTransitionHooks(const TransitionHooks* hooks) {
    const TransitionHooks* prev = g_trans;
    g_trans = hooks ? hooks : &kDefaultTransition;
    return prev;
}

// The "BLACK" tag string constant (aBlack_3 @0x625284).
static const char kBlack[] = "BLACK";

// ===========================================================================
// gilde.exe 0x56d2cc — VIBE_Transition_FadeOutToBlack(scratch@edi)
//   v7 = dword_11BC2D0 | 0x100000;
//   BYTE1(v7) = BYTE1(dword_11BC2D0) & 0xDF;        // clear 0x2000 in v7
//   h = VIBE_Fade_Register(0,0, scrW, scrH, "BLACK", 30, 1);
//   if ( (*(BYTE*)h & 4) == 0 )
//       do VIBE_GameLogic_RunFrameLoop(v7, 0, scratch); while ( (*h & 4) == 0 );
//   dword_69FF80 = scrH;  dword_69FF88 = 0;  dword_69FF84 = 0;  dword_69FF8C = scrW;
//   dword_11BC2D0 = 147591;
//   VIBE_Hud_ToggleObjectHighlight(1);
//   VIBE_Window_RenderEntityScene(..);
//   VIBE_Window_RenderEntityList(1770);
//   VIBE_Fade_Unregister(.., scratch);
//   VIBE_Groundplan_SetWidgetsVisible(0);
//   return VIBE_Fade_Register(0,?, scrW, scrH, "BLACK", 50, 10);
// ===========================================================================
i32 Transition_FadeOutToBlack(void* scratch) {
    const TransitionHooks& h = *g_trans;

    // v7 = (dword_11BC2D0 | 0x100000) with byte1 &= 0xDF (clear bit 0x2000 overall).
    i32 v7 = g_frameStateWord | 0x100000;
    u32 uv7 = static_cast<u32>(v7);
    u32 byte1 = (static_cast<u32>(g_frameStateWord) >> 8) & 0xFF;
    byte1 &= 0xDF;
    uv7 = (uv7 & ~0x0000FF00u) | (byte1 << 8);
    v7 = static_cast<i32>(uv7);

    i32 handle = h.fadeRegister(0, 0, h.scrW, h.scrH, kBlack, 30, 1);
    if (!h.fadeDone(handle)) {                 // if ( (*h & 4) == 0 )
        do {                                   // do { RunFrameLoop } while (!done)
            h.runFrameLoop(v7, 0, scratch);
        } while (!h.fadeDone(handle));
    }

    g_sceneRect.x80 = h.scrH;  // dword_69FF80 = dword_69FFBC >> 16
    g_sceneRect.x88 = 0;       // dword_69FF88 = 0
    g_sceneRect.x84 = 0;       // dword_69FF84 = 0
    g_sceneRect.x8c = h.scrW;  // dword_69FF8C = (&dword_69FFB8 + 2) >> 16

    g_frameStateWord = 147591; // dword_11BC2D0 = 147591
    h.hudToggleHighlight(1);   // VIBE_Hud_ToggleObjectHighlight(1)
    h.renderEntityScene(0, 0); // VIBE_Window_RenderEntityScene(v3, v2)  (args uninit in orig)
    h.renderEntityList(1770);  // VIBE_Window_RenderEntityList(1770)
    h.fadeUnregister(handle, scratch); // VIBE_Fade_Unregister(v4, a1)
    h.groundplanSetVisible(0); // VIBE_Groundplan_SetWidgetsVisible(0)

    // return VIBE_Fade_Register(0, v5, scrW, scrH, "BLACK", 50, 10);
    return h.fadeRegister(0, 0, h.scrW, h.scrH, kBlack, 50, 10);
}

// ===========================================================================
// gilde.exe 0x56d3b0 — VIBE_Transition_FadeInScene(scratch@edi)
//   h = VIBE_Fade_Register(0,0, scrW, scrH, "BLACK", 30, 1);
//   while ( (*h & 4) == 0 ) VIBE_GameLogic_RunFrameLoop(147591, 147591, scratch);
//   if ( dword_63CC30 ) {                                   // snapshot present
//       VIBE_Window_RenderEntityScene(h, dword_63CC30);
//       result = VIBE_Fade_Unregister(.., scratch);
//       dword_631638 = v4;                                  // dirty flag (uninit reg)
//       return result;
//   } else {
//       dword_69FF88 = dword_63CC54; dword_69FF80 = dword_63CC4C;
//       dword_69FF84 = dword_63CC50; dword_69FF8C = dword_63CC58;
//       VIBE_Window_RenderEntityScene(h, 0);
//       VIBE_Groundplan_SetWidgetsVisible(1);
//       VIBE_Groundplan_FadeInScene(.., scratch);
//       VIBE_Hud_ToggleObjectHighlight(0);
//       VIBE_Fade_Unregister(.., scratch);
//       return VIBE_Fade_Register(0,?, scrW, scrH, "BLACK", 50, 10);
//   }
// ===========================================================================
i32 Transition_FadeInScene(void* scratch) {
    const TransitionHooks& h = *g_trans;

    i32 handle = h.fadeRegister(0, 0, h.scrW, h.scrH, kBlack, 30, 1);
    while (!h.fadeDone(handle)) {               // while ( (*h & 4) == 0 )
        h.runFrameLoop(147591, 147591, scratch);
    }

    if (h.sceneSnapshot) {                       // if ( dword_63CC30 )
        h.renderEntityScene(handle, h.sceneSnapshot);
        i32 result = 0;                          // VIBE_Fade_Unregister return (modeled 0)
        h.fadeUnregister(handle, scratch);
        g_dirtyFlag = 0;                         // dword_631638 = v4 (uninit edx; modeled 0)
        return result;
    }

    // No snapshot: restore the scene rect from dword_63CC4C/50/54/58.
    g_sceneRect.x88 = h.srcBottom; // dword_69FF88 = dword_63CC54
    g_sceneRect.x80 = h.srcLeft;   // dword_69FF80 = dword_63CC4C
    g_sceneRect.x84 = h.srcTop;    // dword_69FF84 = dword_63CC50
    g_sceneRect.x8c = h.srcRight;  // dword_69FF8C = dword_63CC58

    h.renderEntityScene(handle, 0);    // VIBE_Window_RenderEntityScene(h, 0)
    h.groundplanSetVisible(1);         // VIBE_Groundplan_SetWidgetsVisible(1)
    h.groundplanFadeIn(scratch);       // VIBE_Groundplan_FadeInScene(v5, a1)
    h.hudToggleHighlight(0);           // VIBE_Hud_ToggleObjectHighlight(0)
    h.fadeUnregister(handle, scratch); // VIBE_Fade_Unregister(v6, a1)

    // return VIBE_Fade_Register(0, v7, scrW, scrH, "BLACK", 50, 10);
    return h.fadeRegister(0, 0, h.scrW, h.scrH, kBlack, 50, 10);
}

// ===========================================================================
void ResetMenuReconTransition() {
    g_fadeSlots = FadeSlotTable{};
    g_hotspots  = HotspotTable{};
    g_sceneRect = SceneRect{};
    g_frameStateWord = 0;
    g_dirtyFlag = 0;
}

} // namespace guild::play
