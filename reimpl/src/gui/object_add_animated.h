#pragma once
// guild::gui — VIBE_Object_AddAnimatedToWindow (recon generation 3 / object cluster).
//
// gilde.exe 0x41af64 — VIBE_Object_AddAnimatedToWindow
//   __userpurge eax = fn(x@eax, y@dx, winSlot@ecx, entityIdx@ebx, animArg@stack)
//
// The animated sibling of VIBE_Object_AddToWindow (0x41ae10, already reconstructed in
// gui/window.cpp). Where AddToWindow places a static control, this leaf places an
// *animated* 3D/sprite object (the kind spawned by VIBE_GameLogic_Objects) into a
// window's retained-mode child tree and then resolves its on-screen anim scale.
//
// Recovered control flow (matches the original instruction-for-instruction):
//   1. w = &dword_67EB80[238*winSlot]                       (the Window record)
//   2. if !w.enabled()          -> return -1                (w[+640] == 0)
//   3. if w.objCount() >= 384   -> ErrorLog("Too many objects on window!"); return -1
//   4. id = VIBE_GameLogic_Objects(x + w.x,                 (NOTE: no -w[+600] x bias,
//                                  y + w.y - w.scrollCur,    unlike AddToWindow)
//                                  entityIdx)
//      childList[w.objCount()] = id      ( *(w[+24] + 4*(w[+26]>>16)) = id )
//   5. if id == -1              -> return id (the stored -1)
//   6. ZOrder_InsertObject(id, w)
//   7. obj = &dword_69FFB4[740*id]                          (the Widget/object record)
//        obj.clipY0(+32) = 0 ; obj.clipX0(+28) = 0
//        obj.groupLink(+44) = (Window*)w                    (parent window back-link)
//        obj[+34] = LOWORD(dword_69FFBC)                    (screen clip extent low; no
//                                                            named accessor — clipX1() is +30)
//        obj.clipX1(+30) = HIWORD(dword_69FFBC)             (screen clip extent high)
//        bw = &dword_69FFB4[740*w.backWidget(+620)]
//        obj.parentClip(+60) = bw.parentClip(+60)
//        obj.renderPtr(+52)  = bw.renderPtr(+52)
//   8. VIBE_Object_ApplyAnimScale(id)                       (animArg is passed in edx but
//                                                            ApplyAnimScale ignores it)
//   9. ++w.objCount()
//  10. bottom = obj.h() + y      ( (obj[+20]>>16) + (y sign-extended) )
//      if bottom > w.contentHeight(+580): w.contentHeight() = bottom
//  11. return id
//
// Coupled leaves routed through inert-default edges (overridable for tests):
//   * VIBE_GameLogic_Objects   @0x412fa0 — the object/draw-slot factory (sim+render
//     cluster; modeled in app/render_submit.cpp with a different signature). Here it is
//     a forward edge that allocates a widget slot and returns its index, so the
//     child-list / z-order / extent model is fully exercisable.
//   * VIBE_Object_ApplyAnimScale @0x41e388 — recompute the node's on-screen anim scale
//     (anim-flags cluster; modeled in sim/object_lifecycle7.cpp on its own struct).
//     Inert default: no-op (the scale is left as seeded by the factory).
//
// REUSED (not redefined): Widget/Window model + g_widgets/g_windows, WindowChildList,
// Widget_AllocSlot, ZOrder_InsertObject, g_screenClipExt (dword_69FFBC).

#include "gui/types.h"

namespace guild::gui {

// ---- Forward-declared coupled edges (weak inert defaults; tests may override) ----------

// gilde.exe 0x412fa0 — VIBE_GameObject/GameLogic factory edge. Allocates the underlying
// animated object/draw slot at window-relative screen coords and returns its widget slot
// index, or -1 on failure. Default routes to Widget_AllocSlot + minimal field seed.
int AnimObjectFactory(i16 screenX, i16 screenY, int entityIdx);

// gilde.exe 0x41e388 — VIBE_Object_ApplyAnimScale edge (single arg: the object handle).
// Default: no-op. The real implementation lives in the anim-flags cluster.
void ApplyAnimScaleEdge(int objId);

// ---- The leaf ---------------------------------------------------------------------------

// gilde.exe 0x41af64 — VIBE_Object_AddAnimatedToWindow.
// Places an animated object into `winSlot` at window-relative (x, y); `entityIdx` selects
// the object kind, `animArg` is the original's 5th stack argument (forwarded to the anim
// scale call, which ignores it). Returns the new object/widget slot index, or -1.
int Object_AddAnimatedToWindow(i16 x, i16 y, int winSlot, int entityIdx, int animArg);

// Adapter matching the GuiDialogs8Hooks::objectAddAnimatedToWindow hook ABI
// (int win, int x, int y, int obj, int z) so the panel/tooltip builders that reach this
// leaf (PlayerBar_BuildContent/Create, InfoPanel_BuildDetailed, Tooltip_BuildObject,
// TradePanel_Build*) can bind their hook directly to the reconstruction.
inline int Object_AddAnimatedToWindowHook(int win, int x, int y, int obj, int z) {
    return Object_AddAnimatedToWindow(static_cast<i16>(x), static_cast<i16>(y), win, obj, z);
}

} // namespace guild::gui
