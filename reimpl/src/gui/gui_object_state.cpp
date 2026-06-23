// gui_object_state.{h,cpp} — see header. 1:1 translation of the three object-state
// helpers' Hex-Rays pseudocode; the engine record array + the lazy realise (State_Helper
// / d2_LoadObj VFS load) + the small globals are injected through StateContext.
#include "gui/gui_object_state.h"

namespace guild::gui {

namespace {
// VIBE_State_Helper(idx, 0): realise record `idx` into its +52 handle. The original
// only does the load when record[idx].stateHandle==0 (it re-checks internally); we
// mirror that guard at the call site exactly as the decompiles do (they test +52 first)
// and forward to the injected hook. A null hook is the documented neutral outcome
// (record stays unrealised; the original cannot reach here without a loader).
inline void Realise(StateContext& ctx, int idx) {
    if (ctx.realiseHook)
        ctx.realiseHook(&ctx, idx, 0);
}
} // namespace

// gilde.exe 0x40e9e8 — VIBE_State_Update.
i32 StateUpdate(StateContext& ctx, int idx) {
    ObjectStateRecord* rec = ctx.records;

    int v1 = idx;                                   // v1 = a1
    ctx.redirectDelta = 0;                           // dword_62D2A4 = 0

    // if ( (record[a1].flag68 & 2) != 0 ) v1 = (u8)gridOffset + a1;
    if ((rec[idx].flag68 & 2) != 0)
        v1 = static_cast<u8>(ctx.gridOffset) + idx;

    int v2 = rec[v1].kind;                           // *(rec(v1)+60)
    // if ( (v2==5 || v2==8) && record[v1].loadOffset==0 )
    if ((v2 == 5 || v2 == 8) && rec[v1].loadOffset == 0) {
        int partner = rec[v1].linkIndex;             // *(rec(v1)+76)
        // if ( !record[partner].stateHandle ) VIBE_State_Helper(partner, 0);
        if (rec[partner].stateHandle == 0)
            Realise(ctx, partner);
        int v4 = v1;                                 // v4 = v1
        v1 = rec[v1].linkIndex;                       // v1 = *(rec(v1)+76)
        ctx.redirectDelta = v4 - v1;                  // dword_62D2A4 = v4 - v1
    }
    // else if ( !record[v1].stateHandle ) VIBE_State_Helper(v1, 0);
    else if (rec[v1].stateHandle == 0) {
        Realise(ctx, v1);
    }

    // return record[v1].stateHandle;
    return rec[v1].stateHandle;
}

// gilde.exe 0x40eaf0 — VIBE_Gui_ResolveObjectState.
int ResolveObjectState(StateContext& ctx, int idx, i32* outState, int* outIndex) {
    ObjectStateRecord* rec = ctx.records;

    int v3 = idx;                                    // v3 = a1
    int v6 = idx;                                    // v6 = a1
    ctx.redirectDelta = 0;                            // dword_62D2A4 = 0

    // if ( (record[a1].flag68 & 2) != 0 ) v3 = (u8)gridOffset + a1;
    if ((rec[idx].flag68 & 2) != 0)
        v3 = static_cast<u8>(ctx.gridOffset) + idx;

    int v7 = rec[v3].kind;                            // *(rec(v3)+60)
    // if ( (v7==5 || v7==8) && record[v3].loadOffset==0 )
    if ((v7 == 5 || v7 == 8) && rec[v3].loadOffset == 0) {
        int partner = rec[v3].linkIndex;             // *(rec(v3)+76)
        // if ( !record[partner].stateHandle ) VIBE_State_Helper(partner, 0);
        if (rec[partner].stateHandle == 0)
            Realise(ctx, partner);
        v6 = rec[v3].linkIndex;                        // v6 = *(rec(v3)+76)
        ctx.redirectDelta = v3 - v6;                  // dword_62D2A4 = v3 - v6
    }
    // else if ( !record[v3].stateHandle ) VIBE_State_Helper(v3, 0);
    else if (rec[v3].stateHandle == 0) {
        Realise(ctx, v3);
    }

    // *outState = record[v6].stateHandle;  *outIndex = v3;  return 1;
    if (outState) *outState = rec[v6].stateHandle;    // *a2 = *(rec(v6)+52)
    if (outIndex) *outIndex = v3;                      // *a3 = v3
    return 1;
}

// gilde.exe 0x412ea4 — VIBE_Gui_MarkObjectUsed.
int MarkObjectUsed(StateContext& ctx, int idx) {
    ObjectStateRecord* rec = ctx.records;

    int v1 = idx;                                    // v1 = a1
    int v2 = rec[idx].kind;                           // *(rec(a1)+60)
    // if ( v2==5 || v2==8 ) { if ( !record[a1].loadOffset ) v1 = record[a1].linkIndex; }
    if (v2 == 5 || v2 == 8) {
        if (rec[idx].loadOffset == 0)                 // !*(rec(a1)+48)
            v1 = rec[idx].linkIndex;                   // *(rec(a1)+76)
    }

    // ++record[v1].useCount;  record[v1].frameStamp = dword_62EB38;  return 84*v1+base;
    ++rec[v1].useCount;                                // ++*(rec(v1)+64)
    rec[v1].frameStamp = ctx.frameStamp;              // *(rec(v1)+72) = dword_62EB38
    return v1;                                         // resolved record index (orig: byte addr)
}

} // namespace guild::gui
