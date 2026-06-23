// Trade-transport panel-open thunks — gilde.exe 0x54012c / 0x54013c.
#include "trade_recon_transport_thunks.h"

namespace guild::world {

namespace {
int defDispatch(void*, int) { return 0; }
TransportPanelDispatchHook g_dispatch = defDispatch;
} // namespace

void TradeTransportSetPanelDispatchHook(TransportPanelDispatchHook hook) {
    g_dispatch = hook ? hook : defDispatch;
}

// gilde.exe 0x54012c — VIBE_TradeTransport_OpenPanelMode2_Thunk.
int TradeTransportOpenPanelMode2(void* panel) {
    return g_dispatch(panel, kTransportPanelMode2);
}

// gilde.exe 0x54013c — VIBE_TradeTransport_OpenPanelMode4_Thunk.
int TradeTransportOpenPanelMode4(void* panel) {
    return g_dispatch(panel, kTransportPanelMode4);
}

} // namespace guild::world
