#pragma once
// building_upgrade_window — faithful 1:1 reconstruction of the Guild (gilde.exe)
// building upgrade-tree window opener (32-bit x86, imagebase 0x400000).
//
//   gilde.exe 0x594100  VIBE_Building_OpenUpgradeTreeWindow
//       __usercall(a1@edx=typeRecord, a2@ebx=targetIdPtr, a3@eax=formName)
//
// This is the entry the building upgrade UI calls to build & show the upgrade-tree
// window for a starting building type. It:
//   1. opens the ".form" window  : dword_13CE288 = GameTick_Finalize(0,0,a3)
//                                  (the .form VFS loader @0x41beb8 — a UI/file
//                                  boundary; returns the form/window handle),
//   2. centres + selects + titles : Form_CenterChildWindows / dword_13CE280 =
//                                  dword_62EB38 / Form_SelectWindow(0,1) /
//                                  Text_RenderRichString("$[%s$]", 0x19) /
//                                  Form_SelectWindow(0,3)  (UI boundary),
//   3. lays out the upgrade DAG  : RoadComputeNetworkLayout(a1, a2, w, h)
//                                  (REUSED 1:1 from world/road_network) — on a
//                                  non-zero (empty/absent) result it destroys the
//                                  form (Form_Destroy(handle,-1)) and returns -1,
//   4. builds the node windows   : BuildUpgradeTree(a1, a2) (the per-node
//                                  Window_AddChildWindow / Object_AddToWindow /
//                                  progress-bar construction @0x59361c — pure UI
//                                  presentation, a boundary; its per-node STATE
//                                  classifier is reconstructed in
//                                  building_lifecycle::Building_ClassifyUpgradeNode),
//   5. draws the edges           : the genuine geometry loop reconstructed here —
//                                  for every ordered (outer,inner) node pair with
//                                  inner.nodeId == outer.parentFromId ||
//                                  inner.nodeId == outer.parentToId, draws a
//                                  Paintbox_DrawLine connecting the two node boxes,
//   6. returns the window handle (dword_13CE288).
//
// === The edge-draw geometry (recovered byte-exact at 0x5941e1..0x59425b) ===
// The node array is the flat 44-byte (0x2C) record store the road layout fills (the
// dword_12CDD8E-aliased buffer). The draw loop indexes it at byte stride 0x2C and
// reads, per node N (esi/edi step by 0x2C):
//   X(N)         = dword_12CDD98[N]  = record +0x30 == RoadNode.cost (the scaled X)
//   Y(N)         = dword_12CDD9C[N]  = record +0x34 == node (N+1)'s coordY (+0x08);
//                  this is the documented cross-record field aliasing (+0x34 of N is
//                  +0x08 of N+1). For the last node +0x34 reads the zeroed tail.
//   nodeId(N)    = dword_12CDD8E[N]  >> 16  = record +0x28 hi-word == RoadNode.nodeId
//   parentFrom(N)= (dword_12CDD8E+2)[N] >> 16 = +0x2A == RoadNode.parentFromId
//   parentTo(N)  = dword_12CDD92[N]  >> 16  = +0x2C == RoadNode.parentToId
// Line for matching (outer=edi, inner=esi):
//   DrawLine(innerX+24, innerY+48, outerY+24, outerX+24, 88, 0x1F, 20).
// The outer loop runs N over [0, nodeCount), the inner over [0, N) — only pairs
// where inner < outer are tested (the original's `v6 < 11*v11`).

#include "guild/common/types.h"

namespace guild::sim {

// Recovered draw constants (pixels / Paintbox_DrawLine args).
constexpr int kUpgradeNodeBoxHalf  = 24;   // +0x18 added to box-X centres
constexpr int kUpgradeNodeBoxFoot  = 48;   // +0x30 added to the inner-Y endpoint
constexpr int kUpgradeLineColor    = 88;   // 0x58  — Paintbox_DrawLine arg5
constexpr int kUpgradeLineStyle    = 31;   // 0x1F  — arg6
constexpr int kUpgradeLineWidthArg = 20;   // 0x14  — arg7
constexpr int kUpgradeRichTitleId  = 0x19; // 25    — Text_RenderRichString("$[%s$]",25)
constexpr int kUpgradeFormSelectA  = 1;    // Form_SelectWindow(0,1)
constexpr int kUpgradeFormSelectB  = 3;    // Form_SelectWindow(0,3)

// ---------------------------------------------------------------------------
// One drawn edge (the genuine geometry output, surfaced for tests + the renderer).
// ---------------------------------------------------------------------------
struct UpgradeTreeEdge {
    int x1, y1, x2, y2;   // the four Paintbox_DrawLine coordinates (already offset)
    int color, style, w;  // 88, 31, 20
};

// ---------------------------------------------------------------------------
// Boundary hooks. nullptr installs inert defaults. The window/GPU/.form plumbing is
// the SDL/Vulkan boundary (rules 3-4); only the layout + edge geometry is logic.
// ---------------------------------------------------------------------------
struct UpgradeWindowHooks {
    // .form open: GameTick_Finalize(0,0,formName) -> window handle (dword_13CE288).
    int  (*formOpen)(const char* formName) = nullptr;
    // Form_CenterChildWindows(handle).
    void (*formCenter)(int handle) = nullptr;
    // dword_13CE280 = dword_62EB38 — the active palette/context snapshot.
    int  (*contextSnapshot)() = nullptr;
    // Form_SelectWindow(0, index) + (for index 1) Text_RenderRichString("$[%s$]",25).
    void (*formSelectWindow)(int form, int index) = nullptr;
    void (*renderRichTitle)(int textId) = nullptr;
    // The layout rectangle: width = rect.right>>16, height = rect.bottom>>16 (the
    // a2[8]/a2[6] fields the original sars by 16). Returned via out-params.
    void (*layoutExtent)(int* width, int* height) = nullptr;
    // The building-type table bases for RoadComputeNetworkLayout (game-data boundary):
    //   typeTableBase = dword_13CE294 (589-stride), typeFlagBase = dword_13CE27C (65).
    const u8* (*typeTableBase)() = nullptr;
    const u8* (*typeFlagBase)() = nullptr;
    // BuildUpgradeTree(a1,a2): builds the per-node windows (pure UI presentation).
    void (*buildUpgradeTree)(const i8* typeRecord, const i16* targetIdPtr) = nullptr;
    // Paintbox_DrawLine(x1,y1,x2,y2,color,style,w).
    void (*drawLine)(int x1, int y1, int x2, int y2, int color, int style, int w) = nullptr;
    // Form_Destroy(handle, -1) — destroys the window on the empty-tree path.
    void (*formDestroy)(int handle) = nullptr;
};

void SetUpgradeWindowHooks(const UpgradeWindowHooks* hooks);
const UpgradeWindowHooks& GetUpgradeWindowHooks();

// gilde.exe dword_13CE288 — the upgrade-tree window handle slot. Set when the
// .form window opens (0x594116) and read back as the return value (0x594292);
// Building_CloseUpgradeWindow (0x5942b0) destroys+clears it. (Previously these
// lived in building_lifecycle; that unit was trimmed, so the dword_13CE288
// accessors live here, their only remaining user.)
void SetUpgradeWindowHandle(int handle);
int  UpgradeWindowHandle();

// gilde.exe 0x594100 — VIBE_Building_OpenUpgradeTreeWindow.
//   typeRecord   : a1 — the building-type record pointer (road layout's a1).
//   targetIdPtr  : a2 — pointer to the target id (road layout's a2 / *a2 == type).
//   formName     : a3 — the .form resource name to open.
// Returns the window handle (dword_13CE288) on success, or -1 when the upgrade tree
// is empty (RoadComputeNetworkLayout returned non-zero -> Form_Destroy path).
int Building_OpenUpgradeTreeWindow(const i8* typeRecord, const i16* targetIdPtr,
                                   const char* formName);

} // namespace guild::sim
