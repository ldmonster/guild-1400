// ===========================================================================
// session_input.cpp — the REAL input -> selection frame path.
//
// Provenance (reconstructed here):
//   0x40cdd0  VIBE_Input_ProcessMouseClicks   (click/double-click state machine,
//             event-ring spill, the 0x4C packet latch)
//   0x40d920  VIBE_Input_PollKeyboardDevice   (pure auto-repeat tail; the
//             DirectInput ring drain is the rule-4 boundary -> shim key events)
// Device-step BINDINGS (rule 4 — the boundary glue, each write cited):
//   0x40d388  VIBE_Input_PollMouseDevice      (global writes bound to shim input;
//             the absolute-cursor latch is the original's own GetCursorPos
//             branch 0x40d74c..0x40d786)
// Reused (extern, NOT redefined):
//   0x40da88  Input_PollMouseAndKeyboard      (play/input_recon_select)
//   0x40d338  gui::Input_SaveMouseButtonSnapshot (gui/input_state)
//   0x40dc2c  gui::Input_SwapCursorClampState    (gui/input_state)
//   0x40c870  gui::Input_SetWheelBase            (gui/input_state)
//   0x414a38  sim::GameTickMainLoop           (sim/gametick_entityscan_recon)
//   0x4147cc  SelectEntity_ComputeResult      (play/input_recon_select)
//   0x4b950c  Selection_CommitContact         (play/session_select)
//   0x4b9444  Selection_Reset                 (play/input_recon_select)
//   0x4b94d8  Selection_ClearAll              (play/session_select)
//   0x4bc280  Selection_UpdateStatusTextLatch (play/session_select)
//   0x5b7134  ComputeSelectionVolumeSolve     (play/picksel_recon kernel)
//   byte_67225C -> play::g_lastHotkeyChar     (play/input_recon4_hotkey)
//   dword_75BF40 -> gui::g_hoverPrev          (gui/gui_dialogs4)
//   dword_62D22C/62D290/672220/672228/75BF08 dual views -> gui/input.h
// ===========================================================================
#include "play/session_input.h"

#include "gui/gui_dialogs4.h"          // gui::g_hoverPrev (dword_75BF40)
#include "gui/input.h"                 // gui::g_hoverObject / g_hoverWindow / ...
#include "gui/input_state.h"           // gui::g_mouseInput block + helpers
#include "play/input_recon4_hotkey.h"  // play::g_lastHotkeyChar (byte_67225C)

#include <array>
#include <cstdio>
#include <cstring>
#include <memory>

namespace guild::play {

// ---------------------------------------------------------------------------
// Module state (ODR-grepped; none of these dwords are modelled elsewhere).
// ---------------------------------------------------------------------------
InputClickState g_inputClick;
u32 g_inputClock = 0;                                   // dword_62EB44 view
i32 g_softCursorMode = 0;                               // dword_62D0D4
i16 g_widgetCursorX = 0;                                // word_75BF4A
i16 g_widgetCursorY = 0;                                // word_75BF48
u8  g_mouseEventRing[kMouseRingRows * kMouseRingRowBytes]; // unk_670FE0
u8  g_keyDownTable[256];                                // byte_671D60
u8  g_keyReleasedTable[256];                            // byte_671F60
u8  g_keyRepeatLatch = 0;                               // byte_62D100
u32 g_keyRepeatDueTick = 0;                             // dword_672260

namespace {

// Byte-exact accessors over the gui::g_mouseInput block (base 0x672174), at
// possibly MISALIGNED absolute addresses (the original's overlapping 16.16
// reads, e.g. *(i32*)0x67220E).
inline u8* blockAt(int absAddr) {
    return gui::g_mouseInput.raw + (absAddr - gui::kInputStateBase);
}
inline i32 rd32(int absAddr) {
    i32 v;
    std::memcpy(&v, blockAt(absAddr), sizeof v);
    return v;
}
inline void wr32(int absAddr, i32 v) { std::memcpy(blockAt(absAddr), &v, sizeof v); }
inline i16 rd16(int absAddr) {
    i16 v;
    std::memcpy(&v, blockAt(absAddr), sizeof v);
    return v;
}
inline void wr16(int absAddr, i16 v) { std::memcpy(blockAt(absAddr), &v, sizeof v); }

// Event-ring row flag (dword_671028[19*row] == row byte offset +72).
inline i32& ringFlagAtDwordIndex(int dwordIdx) {
    // dword_671028 = unk_670FE0 + 0x48; index in dwords.
    return *reinterpret_cast<i32*>(g_mouseEventRing + 0x48 + 4 * dwordIdx);
}

} // namespace

// ===========================================================================
// gilde.exe 0x40cdd0 — VIBE_Input_ProcessMouseClicks.
// Control flow translated 1:1 (v0 = the click-time latch dword_62D0FC; every
// store cites its original address).
// ===========================================================================
i32 Input_ProcessMouseClicks() {
    i32 v0 = g_inputClick.g62D0FC;                      /*0x40cdd2*/
    i32 result = static_cast<i32>(g_inputClock);        /*0x40cdd8 dword_62EB44*/
    if (g_inputClick.g62D0E0)                           /*0x40cde4*/
        return result;                                  /*0x40d0b2*/

    const i32 clk  = static_cast<i32>(g_inputClock);
    // *(int*)((char*)&hWnd + 2) >> 16  == the cursor X word at 0x6721C4;
    // dword_6721C4 >> 16              == the cursor Y word at 0x6721C6.
    const i32 curX = rd32(0x6721C2) >> 16;
    const i32 curY = rd32(0x6721C4) >> 16;

    // ---- left button: stale press window (15 ticks / ±3 px) -------------- //
    if (g_inputClick.g62D0E8                            /*0x40d105*/
        && (clk - g_inputClick.g62D0E8 > 15
            || curX < g_inputClick.g62D0F4 - 3
            || curX > g_inputClick.g62D0F4 + 3
            || curY > g_inputClick.g62D0F8 + 3
            || curY < g_inputClick.g62D0F8 - 3)) {
        v0 = -1;                                        /*0x40ce0a*/
        wr32(0x6721D8, 1);                              /*0x40ce11*/
        g_inputClick.g62D0E8 = 0;                       /*0x40ce17*/
    }
    if (rd32(0x6721D0) && g_inputClick.g62D0DC) {       /*0x40ce31*/
        v0 = -1;                                        /*0x40ce39*/
        g_inputClick.g62D0E8 = 0;                       /*0x40ce3e*/
        wr32(0x6721D0, 0);                              /*0x40ce44*/
        g_inputClick.g62D0DC = 0;                       /*0x40ce4a*/
        wr32(0x6721D4, 0);                              /*0x40ce50*/
        wr32(0x6721D8, 0);                              /*0x40ce56*/
    } else {
        if (rd32(0x6721D4) && !g_inputClick.g62D0E8) {  /*0x40d120*/
            g_inputClick.g62D0F4 = curX;                /*0x40d12b*/
            g_inputClick.g62D0E8 = clk;                 /*0x40d13a*/
            g_inputClick.g62D0F8 = curY;                /*0x40d13f*/
        }
        if (v0 != -1 && clk - v0 > 40) {                /*0x40d151*/
            v0 = -1;                                    /*0x40d155*/
            g_inputClick.g62D0E8 = 0;                   /*0x40d15a*/
        }
        bool gotoLabel8 = false;
        if (rd32(0x6721D0) && v0 != -1) {               /*0x40d16c*/
            if (clk - v0 < 40) {                        /*0x40d175*/
                v0 = -1;                                /*0x40d17c*/
                wr32(0x6721E0, 1);                      /*0x40d183 double-click*/
                wr32(0x6721D0, 0);                      /*0x40d189*/
                g_inputClick.g62D0E8 = 0;               /*0x40d18f*/
                gotoLabel8 = true;                      /*0x40d195 goto LABEL_8*/
            } else {
                v0 = -1;                                /*0x40d19a*/
            }
        }
        if (!gotoLabel8 && rd32(0x6721D0)) {            /*0x40d1a6*/
            g_inputClick.g62D0E8 = 0;                   /*0x40d1b4*/
            v0 = clk;                                   /*0x40d1ba*/
            if (rd32(0x6721D8)) {                       /*0x40d1be*/
                v0 = -1;                                /*0x40d1cf*/
                wr32(0x6721D0, 0);                      /*0x40d1d4*/
            }
            wr32(0x6721D8, 0);                          /*0x40d1c2*/
        }
    }

    // LABEL_8: ---- right button double-click window ------------------------ //
    if (rd32(0x6721E8) && !g_inputClick.g62D0EC) {      /*0x40ce6c*/
        g_inputClick.g62D0F4 = curX;                    /*0x40ce77*/
        g_inputClick.g62D0EC = clk;                     /*0x40ce86*/
        g_inputClick.g62D0F8 = curY;                    /*0x40ce8b*/
    }
    if (g_inputClick.g62D0EC && !rd32(0x6721EC) && rd32(0x6721E8)) { /*0x40ceab*/
        if (clk - g_inputClick.g62D0EC > 15) {          /*0x40ceba*/
            g_inputClick.g62D0EC = rd32(0x6721EC);      /*0x40cec1*/
            wr32(0x6721EC, 1);                          /*0x40cec7*/
        }
        if (curX < g_inputClick.g62D0F4 - 3             /*0x40d21d*/
            || curX > g_inputClick.g62D0F4 + 3
            || curY > g_inputClick.g62D0F8 + 3
            || curY < g_inputClick.g62D0F8 - 3) {
            wr32(0x6721EC, 1);                          /*0x40ceee*/
            g_inputClick.g62D0EC = 0;                   /*0x40cef4*/
        }
    }
    if (rd32(0x6721E4)) {                               /*0x40cf01*/
        if (rd32(0x6721EC)) {                           /*0x40cf0a*/
            wr32(0x6721E4, 0);                          /*0x40cf0e*/
            wr32(0x6721EC, 0);                          /*0x40cf14*/
            wr32(0x6721D4, 0);                          /*0x40cf1a*/
            wr32(0x6721D8, 0);                          /*0x40cf20*/
        }
        g_inputClick.g62D0EC = 0;                       /*0x40cf28*/
    }

    // ---- middle button (same shape over 6721F8/6721FC/6721F4, 62D0F0) ---- //
    if (rd32(0x6721F8) && !g_inputClick.g62D0F0) {      /*0x40cf3e*/
        g_inputClick.g62D0F4 = curX;                    /*0x40cf49*/
        g_inputClick.g62D0F0 = clk;                     /*0x40cf58*/
        g_inputClick.g62D0F8 = curY;                    /*0x40cf5d*/
    }
    if (g_inputClick.g62D0F0 && !rd32(0x6721FC) && rd32(0x6721F8)) { /*0x40cf7e*/
        if (clk - g_inputClick.g62D0F0 > 15) {          /*0x40cf85*/
            g_inputClick.g62D0F0 = rd32(0x6721FC);      /*0x40cf8c*/
            wr32(0x6721FC, 1);                          /*0x40cf92*/
        }
        if (curX < g_inputClick.g62D0F4 - 3             /*0x40d25d*/
            || curX > g_inputClick.g62D0F4 + 3
            || curY > g_inputClick.g62D0F8 + 3
            || curY < g_inputClick.g62D0F8 - 3) {
            wr32(0x6721FC, 1);                          /*0x40cfb8*/
            g_inputClick.g62D0F0 = 0;                   /*0x40cfbe*/
        }
    }
    if (rd32(0x6721F4)) {                               /*0x40cfcb*/
        wr32(0x6721F4, 0);                              /*0x40cfd5*/
        if (rd32(0x6721FC))                             /*0x40cfdd*/
            wr32(0x6721FC, 0);                          /*0x40cfdf*/
        g_inputClick.g62D0F0 = 0;                       /*0x40cfe7*/
    }

    g_inputClick.g62D0FC = v0;                          /*0x40cff8*/

    // ---- event-ring spill when the live state differs from the latched
    //      packet (the 12-pair compare 0x40d31d) --------------------------- //
    const bool same =
        rd32(0x6721D0) == rd32(0x672180) && rd32(0x6721D4) == rd32(0x672184) &&
        rd32(0x6721D8) == rd32(0x672188) && rd32(0x6721E0) == rd32(0x672190) &&
        rd32(0x6721E4) == rd32(0x672194) && rd32(0x6721E8) == rd32(0x672198) &&
        rd32(0x6721EC) == rd32(0x67219C) && rd32(0x6721F4) == rd32(0x6721A4) &&
        rd32(0x6721F8) == rd32(0x6721A8) && rd32(0x6721FC) == rd32(0x6721AC) &&
        rd32(0x672208) == rd32(0x6721B8) && rd32(0x672204) == rd32(0x6721B4);
    if (!same) {
        int v2 = 31;                                    /*0x40d006*/
        int v3 = 589;                                   /*0x40d00b*/
        if (!g_inputClick.g67195C) {                    /*0x40d01e*/
            do {                                        /*0x40d02f*/
                v3 -= 19;                               /*0x40d020*/
                --v2;                                   /*0x40d023*/
            } while (v3 >= 0 && !ringFlagAtDwordIndex(v3));
        }
        if (v2 < 0)                                     /*0x40d033*/
            v2 = 0;                                     /*0x40d32e*/
        if (v2 < 31) {                                  /*0x40d042 (>=31 -> skip)*/
            const int v4 = 19 * (v2 + 1);               /*0x40d061*/
            std::memcpy(g_mouseEventRing + 4 * v4, blockAt(0x6721C4), 0x4C); /*0x40d075*/
            ringFlagAtDwordIndex(v4) = 1;               /*0x40d07f*/
        }
    }

    // LABEL_43: latch the live packet into the current packet.
    result = 76;                                        /*0x40d089*/
    std::memcpy(blockAt(0x672174), blockAt(0x6721C4), 0x4C); /*0x40d09e*/
    return result;
}

// ===========================================================================
// gilde.exe 0x40d920 (pure tail) — keyboard latch + auto-repeat.
// ===========================================================================
u8 Input_PollKeyboardLatch(const SessionKeyEvent* events, int count) {
    g_lastHotkeyChar = 0;                               /*0x40d928 byte_67225C*/
    for (int i = 0; i < count; ++i) {                   // ring drain -> events
        const u8 sc = events[i].scancode;
        if (events[i].pressed && !g_inputClick.g62D0E0) {    /*0x40d9ec*/
            g_lastHotkeyChar = sc;                      /*0x40d9f1*/
            g_keyDownTable[sc] = 1;                     /*0x40d9fb*/
        }
        if (!events[i].pressed && !g_inputClick.g62D0E0) {   /*0x40da13*/
            g_keyDownTable[sc] = 0;                     /*0x40da1e*/
            g_keyReleasedTable[sc] = 1;                 /*0x40da24*/
        }
    }
    u8 result = g_lastHotkeyChar;                       /*0x40d95f*/
    if (g_lastHotkeyChar) {                             /*0x40d966*/
        g_keyRepeatLatch = g_lastHotkeyChar;            /*0x40d968 byte_62D100*/
        g_keyRepeatDueTick = g_inputClock + 9;          /*0x40d975 dword_672260*/
    }
    if (g_keyRepeatLatch && !g_lastHotkeyChar           /*0x40d981/0x40d98a*/
        && g_keyDownTable[g_keyRepeatLatch]) {          /*0x40d993*/
        if (g_keyRepeatDueTick + 5 < g_inputClock) {    /*0x40d9aa (unsigned)*/
            g_lastHotkeyChar = g_keyRepeatLatch;        /*0x40d9b1*/
            g_keyRepeatDueTick = g_inputClock;          /*0x40d9bb*/
        }
    }
    if (g_keyRepeatLatch) {                             /*0x40d9c8*/
        result = g_keyRepeatLatch;                      /*0x40d9cc*/
        if (!g_keyDownTable[g_keyRepeatLatch]) {        /*0x40d9d6*/
            g_keyRepeatLatch = 0;                       /*0x40da34*/
            g_keyRepeatDueTick = 1316134911u;           /*0x40da3a sentinel*/
            return 0xFF;                                /*0x40da2f (al = -1)*/
        }
    }
    return result;                                      /*0x40d9dd*/
}

// ===========================================================================
// gilde.exe 0x40dab8 — VIBE_Input_LatchMouseState (the per-frame consumer).
// Pops ONE pending event-ring row into the packet mirror 0x672210.. (the block
// the selection gate / widget click core read), then polls the keyboard.
// ===========================================================================
u8 Input_LatchMouseState(const SessionKeyEvent* keys, int keyCount) {
    if (g_softCursorMode) {                             /*0x40dac1 dword_62D0D4*/
        wr32(0x672210, rd32(0x672174));                 /*0x40dc06 cursor dword*/
        wr16(0x672214, rd16(0x672178));                 /*0x40dc1e wheel word*/
    }
    u32 v0 = 0;                                         /*0x40dacf*/
    wr32(0x67221C, 0);                                  /*0x40dad1 left-up edge*/
    wr32(0x672228, 0);                                  /*0x40dad7 left click*/
    wr32(0x672230, 0);                                  /*0x40dadd right-up*/
    wr32(0x67223C, 0);                                  /*0x40dae3 right click*/
    wr32(0x672240, 0);                                  /*0x40dae9 middle-up*/
    wr32(0x67224C, 0);                                  /*0x40daef middle click*/

    bool pending = ringFlagAtDwordIndex(0) != 0;        /*0x40daf7 dword_671028[0]*/
    if (!pending) {
        while (true) {                                  /*0x40daf9*/
            v0 += 76;
            if (static_cast<i32>(v0) >= 2432)           /*0x40db01*/
                break;
            if (ringFlagAtDwordIndex(v0 / 4)) {         /*0x40db07*/
                pending = true;                         /*0x40db0e -> LABEL_6*/
                break;
            }
        }
    }
    if (pending) {
        // LABEL_6: copy the row (a 0x6721C4-block snapshot at unk_670FE0+v0)
        // field-by-field into the mirror, exactly as compiled.
        const u8* row = g_mouseEventRing + v0;
        auto rowD = [&](int off) {
            i32 v;
            std::memcpy(&v, row + off, sizeof v);
            return v;
        };
        auto rowW = [&](int off) {
            i16 v;
            std::memcpy(&v, row + off, sizeof v);
            return v;
        };
        wr32(0x67221C, rowD(0x0C));                     /*0x40db10 <- 6721D0*/
        wr32(0x672220, rowD(0x10));                     /*0x40db23 <- 6721D4*/
        wr32(0x672224, rowD(0x14));                     /*0x40db2f <- 6721D8*/
        wr32(0x672228, rowD(0x18));                     /*0x40db3b <- 6721DC*/
        wr32(0x67222C, rowD(0x1C));                     /*0x40db47 <- 6721E0*/
        wr32(0x672230, rowD(0x20));                     /*0x40db53 <- 6721E4*/
        wr32(0x672234, rowD(0x24));                     /*0x40db5f <- 6721E8*/
        wr32(0x672238, rowD(0x28));                     /*0x40db6b <- 6721EC*/
        wr32(0x67223C, rowD(0x2C));                     /*0x40db77 <- 6721F0*/
        wr32(0x672240, rowD(0x30));                     /*0x40db83 <- 6721F4*/
        wr32(0x672244, rowD(0x34));                     /*0x40db8f <- 6721F8*/
        wr32(0x672248, rowD(0x38));                     /*0x40db9b <- 6721FC*/
        wr32(0x67224C, rowD(0x3C));                     /*0x40dba7 <- 672200*/
        wr32(0x672254, rowD(0x44));                     /*0x40dbb3 <- 672208*/
        wr32(0x672250, rowD(0x40));                     /*0x40dbbf <- 672204*/
        wr16(0x672216, rowW(0x06));                     /*0x40dbcc <- 6721CA*/
        wr16(0x672218, rowW(0x08));                     /*0x40dbdc <- 6721CC*/
        const i16 v1 = rowW(0x0A);                      /*0x40dbe3 <- 6721CE*/
        ringFlagAtDwordIndex(v0 / 4) = 0;               /*0x40dbea*/
        wr16(0x67221A, v1);                             /*0x40dbf0*/
    }
    // tail call: VIBE_Input_PollKeyboardDevice(0)       /*0x40dbff*/
    return Input_PollKeyboardLatch(keys, keyCount);
}

// ===========================================================================
// Default record store — 740-byte records with the exact original layout.
// ===========================================================================
namespace {

// The 740-byte record table (dword_69FFB4 model) + side allocations.  File-
// scope so the C-function hooks (SelectEntityHooks take std::function — fine,
// but GameTickMainLoop's fallback is a plain pointer) can reach the active
// instance's store.
constexpr int kRecordStride = 740;

// SelectEntity fallback thunk state (the shared dword_62D22C/62D290 merge).
SelectEntityResult s_fbResult;
bool s_fbRan = false;
i32 SelectEntityFallbackThunk(i32 px, i32 py) {
    s_fbRan = true;
    s_fbResult = SelectEntityResult{};
    return SelectEntity_ComputeResult(px, py, &s_fbResult);   // 0x4147cc
}

// Active instance for the std::function hook bodies (one driver at runtime,
// exactly like the original's single global input chain).
SessionInput* s_active = nullptr;

// Store internals shared with the hooks.
struct Store {
    std::vector<u8> table;                  // 740 * (maxId+1) — dword_69FFB4
    std::vector<std::unique_ptr<i32>> secondaries;       // +44 targets
    std::vector<std::unique_ptr<std::array<u8, 412>>> children; // +60 targets
};
Store s_store;

inline void recWr32(u8* r, int off, i32 v) { std::memcpy(r + off, &v, sizeof v); }
inline void recWr16(u8* r, int off, i16 v) { std::memcpy(r + off, &v, sizeof v); }
inline void recWrPtr(u8* r, int off, const void* p) { std::memcpy(r + off, &p, sizeof p); }

} // namespace

// ---------------------------------------------------------------------------
// SessionInput
// ---------------------------------------------------------------------------
SessionInput::SessionInput() {
    // The real input-init leaves: open the cursor clamp wide (the parked state
    // VIBE_Input_SwapCursorClampState @0x40dc2c installs) and seed the wheel
    // base (VIBE_Input_SetWheelBase @0x40c870: base = wheel + 256).
    gui::Input_SwapCursorClampState(0);
    gui::Input_SetWheelBase(0);
    // dword_62D0D4 = 1 — the city-scene "software cursor" state (the scene
    // orchestrator sets it on scene entry; gates the mirror-cursor latch
    // 0x40dac1 and the word_75BF4A/48 copy 0x4c0f26).
    g_softCursorMode = 1;
    s_active = this;
}

void SessionInput::SetViewport(i32 left, i32 top, i32 right, i32 bottom) {
    vpLeft_ = left; vpTop_ = top; vpRight_ = right; vpBottom_ = bottom;
}
void SessionInput::SetCameraDragActive(bool active) { cameraDrag_ = active; }
void SessionInput::SetSoftwareCursor(bool on) { g_softCursorMode = on ? 1 : 0; }

void SessionInput::ClearEntities() { descs_.clear(); }
void SessionInput::UpdateEntity(const SessionInputEntity& e) {
    for (auto& d : descs_) {
        if (d.id == e.id) { d = e; return; }
    }
    descs_.push_back(e);
}
void SessionInput::ClearActors() { actors_.clear(); }
void SessionInput::UpdateActor(const SessionInputActor& a) {
    for (auto& ar : actors_) {
        if (ar.used && ar.desc.index == a.index) { ar.desc = a; return; }
    }
    actors_.emplace_back();
    actors_.back().used = true;
    actors_.back().desc = a;
}
void SessionInput::UpdateHotspotList(int listId, const i32* rectIds, int count) {
    for (auto& l : lists_) {
        if (l.listId == listId) { l.ids.assign(rectIds, rectIds + count); return; }
    }
    lists_.push_back(HotspotList{listId, std::vector<i32>(rectIds, rectIds + count)});
}
void SessionInput::SetProvider(const SessionInputProvider& p) { provider_ = p; }

// Rebuild the 740-stride table + the 512-slot view + the group gate from the
// descriptors (pointer fix-ups re-done wholesale; the table may reallocate).
void SessionInput::rebuildEntityTable() {
    i32 maxId = 0, maxGroup = 0;
    for (const auto& d : descs_) {
        if (d.id > maxId) maxId = d.id;
        if (d.groupId > maxGroup) maxGroup = d.groupId;
    }
    s_store.table.assign(static_cast<std::size_t>(kRecordStride) * (maxId + 1), 0);
    s_store.secondaries.clear();
    s_store.children.clear();
    entityTable_.clear();
    groupGate_.assign(238u * (maxGroup + 1), 0);

    for (const auto& d : descs_) {
        u8* r = s_store.table.data() + static_cast<std::size_t>(kRecordStride) * d.id;
        recWr32(r, 0, d.id);                       // +0 own table index
        recWr32(r, 8, d.actionCode);               // +8 action code
        recWr16(r, 16, static_cast<i16>(d.left));  // box (the +14.. 16.16 reads)
        recWr16(r, 18, static_cast<i16>(d.top));
        recWr16(r, 20, static_cast<i16>(d.width));
        recWr16(r, 22, static_cast<i16>(d.height));
        r[24] = d.typeByte;                        // +24
        recWr16(r, 28, static_cast<i16>(d.innerLeft));   // +26>>16
        recWr16(r, 30, static_cast<i16>(d.innerRight));  // +28>>16
        recWr16(r, 32, static_cast<i16>(d.yMin));        // +30>>16
        recWr16(r, 34, static_cast<i16>(d.yMax));        // +32>>16
        recWr32(r, 52, d.busy52);                  // +52
        recWr32(r, 56, d.busy56);                  // +56
        recWr32(r, 68, d.pickEnable68);            // +68
        recWr32(r, 72, d.pickEnable72);            // +72
        recWr32(r, 80, d.pickEnable80);            // +80
        recWr32(r, 116, d.groupId);                // +116 (group / type-64 payload)
        r[444] = d.flag444;                        // +444
        if (d.hasChild) {
            s_store.children.push_back(std::make_unique<std::array<u8, 412>>());
            auto& c = *s_store.children.back();
            c.fill(0);
            recWr32(c.data(), 408, d.childBusy408);
            recWrPtr(r, 60, c.data());             // +60 child record ptr
        }
        if (d.hasSecondary) {                      // rect profile: +44 secondary
            s_store.secondaries.push_back(std::make_unique<i32>(d.secondaryValue));
            recWrPtr(r, 44, s_store.secondaries.back().get());
        }
        if (d.groupGate && static_cast<std::size_t>(238 * d.groupId) < groupGate_.size())
            groupGate_[238u * d.groupId] = 1;      // dword_67EDE4[238*g]
    }
    // 512-slot view: registration order; the scans walk 511..0, so the LAST
    // registered slotted record is tested first (the original's interaction
    // list grows the same way — later-created widgets sit in higher slots).
    for (const auto& d : descs_) {
        if (!d.slotted) continue;
        if (entityTable_.size() >= 512) break;
        entityTable_.push_back(s_store.table.data() +
                               static_cast<std::size_t>(kRecordStride) * d.id);
    }
}

void SessionInput::bindScanTables(sim::GameTickScanState& st) {
    if (provider_.bindScanTables) { provider_.bindScanTables(st); return; }
    rebuildEntityTable();
    st.entityTable = entityTable_.data();          // dword_62D26C
    st.entityTableCount = entityTable_.size();
    st.objectTypeRecords = s_store.table.data();   // dword_69FFB4
    st.groupGateTable = groupGate_.data();         // dword_67EDE4
}

void SessionInput::installSelectEntityHooks() {
    if (provider_.bindSelectEntityHooks) { provider_.bindSelectEntityHooks(); return; }
    SelectEntityHooks h;
    h.actorRecord = [](int idx) -> u8* {
        if (!s_active) return nullptr;
        for (auto& a : s_active->actors_)
            if (a.used && a.desc.index == idx) {
                // encode the actor record fields the walk reads
                u8* b = a.b;
                std::memset(b, 0, sizeof a.b);
                recWr32(b, 400, a.desc.live ? 1 : 0);   // v5[100]
                recWr32(b, 408, a.desc.live ? 1 : 0);   // v5[102]
                recWr32(b, 412, a.desc.live ? 1 : 0);   // v5[103]
                std::snprintf(reinterpret_cast<char*>(b + 428), 64, "%s",
                              a.desc.anim0 ? a.desc.anim0 : "");
                std::snprintf(reinterpret_cast<char*>(b + 492), 64, "%s",
                              a.desc.anim1 ? a.desc.anim1 : "");
                recWr32(b, 388, a.desc.listCount);      // list count
                for (int i = 0; i < a.desc.listCount && i < 8; ++i)
                    recWr32(b, 4 * i + 4, a.desc.listIds[i]);
                // animation record: byte-pair name +0, scale +116
                std::snprintf(reinterpret_cast<char*>(a.anim), 100, "%s",
                              a.desc.animName ? a.desc.animName : "");
                recWr32(a.anim, 116, static_cast<i32>(a.desc.animScale));
                return b;
            }
        return nullptr;
    };
    h.animationGetPtr = [](const u8* namePtr, int) -> u8* {
        if (!s_active) return nullptr;
        for (auto& a : s_active->actors_)
            if (a.used && (namePtr == a.b + 428 || namePtr == a.b + 492))
                return a.anim;                          // VIBE_Animation_GetPtr
        return nullptr;
    };
    h.computeSelectionVolume = [](float x, float y, int, u8* anim, float* outU,
                                  float* outV, int* outHalfTileFlag) -> int {
        if (!s_active) return 0;
        for (auto& a : s_active->actors_)
            if (a.used && anim == a.anim) {
                if (!a.desc.hasVolume)
                    return 0;   // bone-extent fill absent -> original miss path
                *outHalfTileFlag = a.desc.halfTileFlag;
                // the REAL kernel of VIBE_Pick_ComputeSelectionVolume @0x5b7134
                return ComputeSelectionVolumeSolve(a.desc.cornerPos, a.desc.cornerUV,
                                                   x, y, a.desc.proj, outU, outV);
            }
        return 0;
    };
    h.hotspotListRecord = [](int listId, int* outCount, const i32** outIds) -> bool {
        if (!s_active) return false;
        for (auto& l : s_active->lists_)
            if (l.listId == listId) {
                *outCount = static_cast<int>(l.ids.size());
                *outIds = l.ids.data();
                return true;
            }
        return false;
    };
    h.hotspotRect = [](int rectId) -> u8* {
        const std::size_t off = static_cast<std::size_t>(kRecordStride) * rectId;
        if (off + kRecordStride > s_store.table.size()) return nullptr;
        return s_store.table.data() + off;             // dword_69FFB4 + 740*id
    };
    SelectEntity_SetHooks(h);
}

void SessionInput::installCommitHooks() {
    // Same live hook set SessionSelect installs (session parity).
    SelectCommitHooks h;
    h.clearWorkerSelectionMarks = []() {
        if (s_active) std::memset(s_active->workerMarks_, 0,
                                  sizeof s_active->workerMarks_);
    };
    h.markWorkerSelected = [](u16 id) {
        if (s_active) s_active->workerMarks_[id % kWorkerCount] = 1;
    };
    h.workerMeshPtr = [](u16 id) -> u8* {
        // dword_12CEA94[134*id] — render-cluster table (named gap); a stable
        // per-id slot keeps the dword_62D098 identity (session_select model).
        return s_active ? &s_active->workerMarks_[id % kWorkerCount] : nullptr;
    };
    h.computeSelectionFlags = [](u16, u8*, u8*, u8*) -> u16 {
        return (s_active && s_active->selValid_) ? s_active->sel_.selectionFlags : 0;
    };
    h.objectTypeByte = [](int) -> u8 {
        return (s_active && s_active->selValid_)
                   ? static_cast<u8>(s_active->sel_.typeCode) : 0;
    };
    Selection_SetCommitHooks(h);
}

// The VIBE_Object_UpdateGateContact @0x4b8ba8 boundary: materialise the hover
// latch from the picked descriptor (shadow records carrying exactly the byte
// fields 0x4b950c dereferences — the session_select precedent).
void SessionInput::defaultResolveContact(i32 pickedId) {
    g_selectionContact = SelectionContactLatch{};
    selValid_ = false;
    if (pickedId < 0) return;
    const SessionInputEntity* hit = nullptr;
    for (const auto& d : descs_)
        if (d.id == pickedId) { hit = &d; break; }
    if (!hit) return;

    sel_ = *hit;
    selValid_ = true;
    std::memset(objShadow_.bytes, 0, sizeof objShadow_.bytes);
    std::memset(contactShadow_.bytes, 0, sizeof contactShadow_.bytes);
    std::memset(personShadow_.bytes, 0, sizeof personShadow_.bytes);

    // object record: handle string at +0, highlight bit at +529.
    std::snprintf(reinterpret_cast<char*>(objShadow_.bytes), 64, "%s",
                  hit->handle ? hit->handle : "");
    if (hit->highlightable) objShadow_.bytes[529] |= 1;
    g_selectionContact.g631724 = objShadow_.bytes;        // dword_631724

    if (hit->kind == 3) {
        // person record: id word +0, active byte +8, name +48, and the alive
        // byte +392 — the commit's PER-FRAME stale-worker drop (0x4b954d
        // `if (dword_11BC270 && !*(BYTE*)(+392))`) reads it every frame, so a
        // live selected worker must carry it (the original person record's
        // worker-active byte).
        *reinterpret_cast<u16*>(personShadow_.bytes) = static_cast<u16>(hit->id);
        personShadow_.bytes[8] = 1;
        personShadow_.bytes[392] = 1;
        std::snprintf(reinterpret_cast<char*>(personShadow_.bytes + 48),
                      sizeof personShadow_.bytes - 48, "%s",
                      hit->name ? hit->name : "");
        g_selectionContact.g631734 = personShadow_.bytes; // dword_631734
        g_selectionContact.g631730 = 0;
    } else {
        // contact sub-record: game-object id word +0.
        *reinterpret_cast<i16*>(contactShadow_.bytes) = static_cast<i16>(hit->id);
        g_selectionContact.g63172C = contactShadow_.bytes; // dword_63172C
        g_selectionContact.g631730 = hit->selectable ? 1 : 0; // dword_631730
    }
}

// ---------------------------------------------------------------------------
// Device-step bindings for Input_PollMouseAndKeyboard @0x40da88 (rule 4).
// ---------------------------------------------------------------------------
void SessionInput::installPollHooks(const SessionInputFrame& in) {
    s_active = this;
    InputPollHooks h;
    const bool pl = prevLeft_, pr = prevRight_, pm = prevMiddle_;
    h.pollMouseDevice = [in, pl, pr, pm]() {
        // VIBE_Input_PollMouseDevice @0x40d388 global writes.  The DirectInput
        // GetDeviceData ring drain is replaced by the per-frame shim
        // transitions; each event iteration starts with the loop-head edge
        // reset (0x40d3d3..0x40d40f), exactly like the original's while(1).
        auto loopHeadReset = []() {
            wr32(0x6721DC, 0);                          /*0x40d3d3*/
            wr32(0x6721D0, 0);                          /*0x40d3da*/
            wr32(0x6721E0, 0);                          /*0x40d3e0*/
            wr32(0x6721F0, 0);                          /*0x40d3eb*/
            wr32(0x6721E4, 0);                          /*0x40d3f2*/
            wr32(0x672200, 0);                          /*0x40d3f8*/
            wr32(0x6721F4, 0);                          /*0x40d400*/
            wr32(0x672204, 0);                          /*0x40d409*/
            wr32(0x672208, 0);                          /*0x40d40f*/
        };
        wr16(0x6721CA, 0);                              /*0x40d39e*/
        wr16(0x6721CC, 0);                              /*0x40d3a5*/
        wr16(0x6721CE, 0);                              /*0x40d3ac*/

        // ---- wheel event (data offset 8, 0x40d5c3..0x40d5ea) ----------------
        if (in.wheel) {
            loopHeadReset();
            const i32 v5 = (gui::g_wheelBase * in.wheel) >> 8;  /*0x40d5c3*/
            if (v5 > 0) wr32(0x672208, 1);              /*0x40d5ce*/
            else if (v5 < 0) wr32(0x672204, 1);         /*0x40d741*/
            wr16(0x6721CE, static_cast<i16>(v5));       /*0x40d5dd*/
            wr16(0x6721C8, static_cast<i16>(rd16(0x6721C8) + v5)); /*0x40d5e3*/
            Input_ProcessMouseClicks();                 /*0x40d5ea*/
        }
        // ---- button transitions (data offsets 12/13/14) ---------------------
        if (in.left != pl) {
            loopHeadReset();
            if (in.left) {                              // press (v13 & 0x80)
                wr32(0x6721D4, 1);                      /*0x40d60e*/
                wr32(0x6721D0, 0);                      /*0x40d614*/
                if (!rd32(0x672238))                    /*0x40d61c*/
                    wr32(0x6721DC, 1);                  /*0x40d61e click edge*/
            } else {                                    // release
                wr32(0x6721D0, 1);                      /*0x40d638*/
                wr32(0x6721D4, 0);                      /*0x40d63e*/
            }
            gui::Input_SaveMouseButtonSnapshot();       /*0x40d624/0x40d644*/
            Input_ProcessMouseClicks();                 /*0x40d629/0x40d649*/
        }
        if (in.right != pr) {
            loopHeadReset();
            if (in.right) {
                wr32(0x6721E8, 1);                      /*0x40d65b*/
                wr32(0x6721E4, 0);                      /*0x40d661*/
                wr32(0x6721F0, 1);                      /*0x40d667 click edge*/
            } else {
                wr32(0x6721E4, 1);                      /*0x40d681*/
                wr32(0x6721E8, 0);                      /*0x40d687*/
            }
            gui::Input_SaveMouseButtonSnapshot();       /*0x40d66d/0x40d68d*/
            Input_ProcessMouseClicks();                 /*0x40d672/0x40d692*/
        }
        if (in.middle != pm) {
            loopHeadReset();
            if (in.middle) {
                wr32(0x6721F8, 1);                      /*0x40d6ab*/
                wr32(0x6721F4, 0);                      /*0x40d6b1*/
                wr32(0x672200, 1);                      /*0x40d6b7 click edge*/
            } else {
                wr32(0x6721F4, 1);                      /*0x40d6d1*/
                wr32(0x6721F8, 0);                      /*0x40d6d7*/
            }
            gui::Input_SaveMouseButtonSnapshot();       /*0x40d6bd/0x40d6dd*/
            Input_ProcessMouseClicks();                 /*0x40d6c2/0x40d6e2*/
        }
        // ---- the breaking iteration's head reset, then the cursor latch -----
        loopHeadReset();                                /*the GetDeviceData-empty pass*/

        // absolute cursor latch — the original's OWN OS-cursor branch
        // 0x40d74c..0x40d786 with its exact double-clamp idiom (the SDL cursor
        // IS the GetCursorPos/ScreenToClient point; relative-mickey
        // sensitivity (dword_62D0B8 * flt_610C5C, 0x40d588) stays with the
        // DirectInput decode — platform boundary).
        const i16 oldX = rd16(0x6721C4);
        const i16 oldY = rd16(0x6721C6);
        i32 x = in.mouseX;                              /*0x40d74c*/
        if (x >= gui::g_cursorClampX0) x = gui::g_cursorClampX0; /*0x40d758*/
        i16 v9;
        if (x >= gui::g_cursorClampX1) {                /*0x40d762*/
            v9 = static_cast<i16>(in.mouseX);           /*0x40d79f*/
            if (in.mouseX >= gui::g_cursorClampX0)      /*0x40d7ab*/
                v9 = static_cast<i16>(gui::g_cursorClampX0);
        } else {
            v9 = static_cast<i16>(gui::g_cursorClampX1); /*0x40d764*/
        }
        wr16(0x6721C4, v9);                             /*0x40d76c LOWORD(6721C4)*/
        i32 y = in.mouseY;                              /*0x40d772*/
        if (y >= gui::g_cursorClampY0) y = gui::g_cursorClampY0; /*0x40d778*/
        i16 v11;
        if (y >= gui::g_cursorClampY1) {                /*0x40d782*/
            v11 = static_cast<i16>(in.mouseY);          /*0x40d7b5*/
            if (in.mouseY >= gui::g_cursorClampY0)      /*0x40d7c1*/
                v11 = static_cast<i16>(gui::g_cursorClampY0);
        } else {
            v11 = static_cast<i16>(gui::g_cursorClampY1); /*0x40d784*/
        }
        wr16(0x6721C6, v11);                            /*0x40d786 HIWORD(6721C4)*/
        // the per-event delta words (the relative path's word_6721CA/CC view).
        wr16(0x6721CA, static_cast<i16>(rd16(0x6721C4) - oldX));
        wr16(0x6721CC, static_cast<i16>(rd16(0x6721C6) - oldY));

        Input_ProcessMouseClicks();                     /*0x40d54a/0x40d78c*/
    };
    h.copyPacket = [](void*, const void*, int bytes) {
        // 0x40da9d: qmemcpy(&dword_672210, &dword_672174, 0x4C) — the previous-
        // frame mirror THE SELECTION GATE reads (unk_67220E / dword_672210 /
        // dword_67221C / dword_672228 / ...).
        std::memcpy(blockAt(0x672210), blockAt(0x672174), bytes);
    };
    h.pollKeyboardDevice = [in]() -> u8 {
        return Input_PollKeyboardLatch(in.keys, in.keyCount);   // 0x40d920
    };
    Input_SetPollHooks(h);
}

// ---------------------------------------------------------------------------
// Frame — the chain (steps 1..7 of the header banner).
// ---------------------------------------------------------------------------
SessionInput::Result SessionInput::Frame(const SessionInputFrame& in) {
    Result R;
    s_active = this;
    g_inputClock = in.clock;                            // dword_62EB44 view
    installPollHooks(in);                               // shared device bindings

    // (1) 0x4c09bf — VIBE_Input_LatchMouseState @0x40dab8: mirror cursor <-
    //     current packet, pop ONE pending event-ring row into the mirror,
    //     keyboard poll tail.  Edges therefore surface ONE FRAME after the
    //     physical transition (the original's exact frame latency).
    R.repeatScancode = Input_LatchMouseState(in.keys, in.keyCount);

    // Edge views from the just-popped packet mirror (the gate's view).
    R.leftClickEdge  = rd32(0x672228) != 0;             // mirror of dword_6721DC
    R.leftRelease    = rd32(0x67221C) != 0;             // mirror of dword_6721D0
    R.rightClickEdge = rd32(0x67223C) != 0;             // mirror of dword_6721F0
    R.doubleClick    = rd32(0x67222C) != 0;             // mirror of dword_6721E0
    // gui dual views of dword_672220 / dword_672228 (gui/input.h click core).
    gui::g_mouseDown  = rd32(0x672220);
    gui::g_mouseClick = rd32(0x672228);

    // (2) the scan cursor: word_75BF4A / word_75BF48 — latched at the END of
    //     the PREVIOUS frame (0x4c0f2f..0x4c0f41), consumed by the widget
    //     dispatch mid-frame (0x4215b0).
    const i32 px = g_widgetCursorX;
    const i32 py = g_widgetCursorY;

    // (3)+(4) 0x414a38 MainLoop over the live tables, 0x4147cc fallback.
    sim::GameTickScanState st;
    bindScanTables(st);
    installSelectEntityHooks();
    s_fbRan = false;
    R.actionCode = sim::GameTickMainLoop(st, px, py, &SelectEntityFallbackThunk);
    gui::g_hoverPrev = R.actionCode;                    // dword_75BF40 (0x4215b0)
    if (s_fbRan) {
        // dword_62D22C / dword_62D290 are ONE global in the original; merge the
        // fallback's stores back into the scan-state view.
        if (R.actionCode != -1)
            st.selectionId = s_fbResult.pickedLabelId;  // the hit id
        if (s_fbResult.secondaryWritten)
            st.hoverChildId = s_fbResult.pickedSecondary;
    }
    R.pickedId     = st.selectionId;                    // dword_62D22C
    R.hoverChildId = st.hoverChildId;                   // dword_62D290
    R.mainGroupId  = st.mainGroupId;                    // dword_62D294
    R.secondaryId  = st.secondaryId;                    // dword_62D240
    gui::g_hoverObject = st.selectionId;                // dual view (gui/input.h)
    gui::g_hoverWindow = st.hoverChildId;

    // (5) 0x4215c9 / 0x4218e0 — dword_75BF08 = hover child or -1.
    const i32 modal = (st.hoverChildId == -1) ? -1 : st.hoverChildId;
    gui::g_lastClickedWindow = modal;                   // dword_75BF08 (gui view)
    g_selectGate.g75BF08 = modal;                       // same dword (gate view)

    // (6) hover-latch resolve (0x4b8ba8 boundary) + the commit @0x4b950c.
    if (provider_.resolveContact) provider_.resolveContact(st.selectionId);
    else defaultResolveContact(st.selectionId);

    installCommitHooks();
    // Gate feed — the REAL packet-mirror globals the original gate reads.
    g_selectGate.g67221C   = rd32(0x67221C);            // left-UP edge mirror
    g_selectGate.g62D4E8   = cameraDrag_ ? 1 : 0;       // dword_62D4E8
    g_selectGate.cursorX16 = rd32(0x67220E);            // unk_67220E (X<<16 | ..)
    g_selectGate.cursorY16 = rd32(0x672210);            // dword_672210
    g_selectGate.g63CC4C = vpLeft_;                     // viewport rect
    g_selectGate.g63CC50 = vpTop_;
    g_selectGate.g63CC54 = vpRight_;
    g_selectGate.g63CC58 = vpBottom_;
    g_selectGate.g62D31C = -1;  // dword_62D31C — the unk_75BA38 hotspot-strip
                                // scan (0x421793..0x4217f9) is a NAMED GAP
                                // (drag-cursor payload strip; no strip = -1).

    // The commit's own gate predicate (reported, the engine state is the gate's):
    const i32 cx = g_selectGate.cursorX16 >> 16;
    const i32 cy = g_selectGate.cursorY16 >> 16;
    R.commitRan = g_selectGate.g67221C && !g_selectGate.g62D4E8 &&
                  cx > vpLeft_ && cx < vpRight_ && cy > vpTop_ && cy < vpBottom_ &&
                  g_selectGate.g75BF08 == -1 && g_selectGate.g62D31C == -1;

    Selection_CommitContact();                          // 0x4b950c (every frame)
    if (!g_selectionAnchorRecords.a631740 && !g_selectionCommit.g11BC270)
        selValid_ = false;                              // nothing stuck
    Selection_UpdateStatusTextLatch();                  // 0x4bc280 tail

    // right-click edge -> the deselect pair (session parity: sdl_session calls
    // SessionSelect::Clear() on the right edge).
    if (R.rightClickEdge)
        ClearSelection();

    R.selected = g_selectionAnchorRecords.a631740 != nullptr ||
                 g_selectionCommit.g11BC270 != nullptr;
    if (R.selected && selValid_) {
        R.selectedId = sel_.id;
        R.selectedKind = sel_.kind;
    }

    // (7) 0x4c0f21 — VIBE_Input_PollMouseDevice (the device step, called
    //     DIRECTLY by the frame loop; drains the shim transitions into the
    //     live block, spilling event-ring rows + latching the current packet).
    if (Input_GetPollHooks().pollMouseDevice)
        Input_GetPollHooks().pollMouseDevice();

    // (8) 0x4c0f26..0x4c0f41 — when dword_62D0D4 != 0:
    //     word_75BF4A = *(u16*)0x672174, word_75BF48 = *(u16*)0x672176.
    if (g_softCursorMode) {
        g_widgetCursorX = rd16(0x672174);               /*0x4c0f2f/0x4c0f35*/
        g_widgetCursorY = rd16(0x672176);               /*0x4c0f3b/0x4c0f41*/
    }

    prevLeft_ = in.left;
    prevRight_ = in.right;
    prevMiddle_ = in.middle;
    return R;
}

void SessionInput::ClearSelection() {
    s_active = this;
    installCommitHooks();          // ClearAll's worker sweep runs through them
    Selection_Reset(0);                                 // 0x4b9444
    g_selectionAnchorRecords = SelectionAnchorRecords{}; // pointer-width dual view
    Selection_ClearAll();                               // 0x4b94d8
    selValid_ = false;
    g_selectionContact = SelectionContactLatch{};
    Selection_UpdateStatusTextLatch();                  // 0x4bc280 tail
}

bool SessionInput::workerSelected(i32 personId) const {
    return workerMarks_[static_cast<u16>(personId) % kWorkerCount] != 0;
}

const char* SessionInput::selectedName() const {
    if (!selValid_) return "";
    return sel_.name && sel_.name[0] ? sel_.name : (sel_.handle ? sel_.handle : "");
}

} // namespace guild::play
