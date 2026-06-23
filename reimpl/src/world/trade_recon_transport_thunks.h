#pragma once
// Trade-transport panel-open thunks — gilde.exe.
//
// Translated:
//   VIBE_TradeTransport_OpenPanelMode2_Thunk 0x54012c  (-> dispatcher(panel, 2))
//   VIBE_TradeTransport_OpenPanelMode4_Thunk 0x54013c  (-> dispatcher(panel, 4))
//
// Each thunk is a 13-byte __usercall trampoline that tail-calls the trade-transport
// panel dispatcher (VIBE_TradeTransport_PanelDispatcher 0x54014c) with a fixed
// "panel mode" selector in dl (2 or 4). The matching mode-1 thunk lives at
// 0x54011c. The dispatcher itself is a ~14KB GUI/entity-coupled state machine and
// is intentionally out of this cluster's scope; it is represented here by a hook
// so the thunks reproduce their exact dispatch (panel pointer + mode constant)
// without dragging in the live panel/object tables.
//
// Call sites (xrefs): VIBE_ContactMenu_RemoteTrade 0x5138d0 invokes mode-2 at
// 0x51393b and mode-4 at 0x513944.
#include "guild/common/types.h"

namespace guild::world {

// Panel-mode constants the thunks bake into the dispatcher call (dl).
constexpr int kTransportPanelMode2 = 2; // 0x54012c
constexpr int kTransportPanelMode4 = 4; // 0x54013c

// VIBE_TradeTransport_PanelDispatcher(panel, mode) — the dispatcher leaf. Default
// is a no-op returning 0; the real panel wiring installs the dispatcher backend.
using TransportPanelDispatchHook = int (*)(void* panel, int mode);
void TradeTransportSetPanelDispatchHook(TransportPanelDispatchHook hook);

// gilde.exe 0x54012c — VIBE_TradeTransport_OpenPanelMode2_Thunk.
//   return VIBE_TradeTransport_PanelDispatcher(a1, 2);
int TradeTransportOpenPanelMode2(void* panel);

// gilde.exe 0x54013c — VIBE_TradeTransport_OpenPanelMode4_Thunk.
//   return VIBE_TradeTransport_PanelDispatcher(a1, 4);
int TradeTransportOpenPanelMode4(void* panel);

} // namespace guild::world
