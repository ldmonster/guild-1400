// Golden-vector unit tests for the VIBE_Console_* primitive layer
// (src/sim/console_recon.{h,cpp}). Self-contained: drives the translated
// state machines through the inert ConsoleHooks boundary with scripted input.
#include "tests/framework/test.h"
#include "sim/console_recon.h"

#include <vector>

using namespace guild::sim;
using guild::u8;
using guild::u16;
using guild::u32;

namespace {

// Build a KEY_EVENT key record matching the gilde INPUT_RECORD subset.
ConsoleKeyRecord MakeKey(u16 eventType, u32 keyDown, u16 repeat, u16 vk,
                         u16 scan, u8 ascii, u8 ctrl) {
    ConsoleKeyRecord r{};
    r.eventType      = eventType;
    r.keyDown        = keyDown;
    r.repeatCount    = repeat;
    r.virtualKeyCode = vk;
    r.virtualScanCode = scan;
    r.asciiChar      = ascii;
    r.controlKeyState = ctrl;
    return r;
}

// A scripted ReadConsoleInput source: pops records in order; reports failure
// when exhausted (mirrors ReadConsoleInputA==0 → ReadCharEvent returns -1).
std::vector<ConsoleKeyRecord>* g_script = nullptr;
std::size_t g_scriptPos = 0;

bool ScriptedRead(guild::i32 /*h*/, ConsoleKeyRecord* out) {
    if (!g_script || g_scriptPos >= g_script->size()) return false;
    *out = (*g_script)[g_scriptPos++];
    return true;
}

void InstallScript(std::vector<ConsoleKeyRecord>& s) {
    g_script = &s;
    g_scriptPos = 0;
}

} // namespace

// ---------------------------------------------------------------------------
// 0x609090 — VIBE_Console_IsValidKeyEvent.
TEST(ConsoleReconKeyEvent, ValidPrintableKeyDown) {
    // KEY_EVENT, keyDown, vk='A'(0x41) → valid.
    auto r = MakeKey(1, 1, 1, 0x41, 0x1E, 'a', 0);
    CHECK_EQ(Console_IsValidKeyEvent(r), 1);
}

TEST(ConsoleReconKeyEvent, RejectNonKeyEvent) {
    auto r = MakeKey(2 /*not KEY_EVENT*/, 1, 1, 0x41, 0x1E, 'a', 0);
    CHECK_EQ(Console_IsValidKeyEvent(r), 0);
}

TEST(ConsoleReconKeyEvent, RejectKeyUp) {
    auto r = MakeKey(1, 0 /*bKeyDown=0*/, 1, 0x41, 0x1E, 'a', 0);
    CHECK_EQ(Console_IsValidKeyEvent(r), 0);
}

TEST(ConsoleReconKeyEvent, RejectModifierKeys) {
    // VK_SHIFT(0x10), VK_CONTROL(0x11), VK_MENU(0x12) are filtered out.
    CHECK_EQ(Console_IsValidKeyEvent(MakeKey(1, 1, 1, 0x10, 0, 0, 0)), 0);
    CHECK_EQ(Console_IsValidKeyEvent(MakeKey(1, 1, 1, 0x11, 0, 0, 0)), 0);
    CHECK_EQ(Console_IsValidKeyEvent(MakeKey(1, 1, 1, 0x12, 0, 0, 0)), 0);
    // Boundaries just outside the range are valid.
    CHECK_EQ(Console_IsValidKeyEvent(MakeKey(1, 1, 1, 0x0F, 0, 0, 0)), 1);
    CHECK_EQ(Console_IsValidKeyEvent(MakeKey(1, 1, 1, 0x13, 0, 0, 0)), 1);
}

// ---------------------------------------------------------------------------
// 0x6090b8 / 0x609128 / 0x609134 — handle caching.
TEST(ConsoleReconHandles, LazyOpenAndCache) {
    ResetConsoleState();
    SetConsoleHooks(nullptr);  // inert defaults: openInput→1, openOutput→2
    CHECK_EQ(GetConsoleState().inputHandle, -1);

    CHECK_EQ(Console_GetInputHandle(), 1);
    CHECK_EQ(Console_GetOutputHandle(), 2);
    // Cached: handles are now non-(-1).
    CHECK_EQ(GetConsoleState().inputHandle, 1);
    CHECK_EQ(GetConsoleState().outputHandle, 2);
}

TEST(ConsoleReconHandles, OpenOnlyWhenMinusOne) {
    ResetConsoleState();
    static int s_opens = 0;
    s_opens = 0;
    ConsoleHooks h{};
    h.openInput  = []() -> guild::i32 { ++s_opens; return 7; };
    h.openOutput = []() -> guild::i32 { return 8; };
    SetConsoleHooks(&h);

    CHECK_EQ(Console_GetInputHandle(), 7);
    CHECK_EQ(Console_GetInputHandle(), 7);  // cached; no second open
    CHECK_EQ(s_opens, 1);
    SetConsoleHooks(nullptr);
}

// ---------------------------------------------------------------------------
// 0x60929c / 0x6092cc — ctrl handler install/remove guard.
TEST(ConsoleReconCtrl, InstallOnceAndRemove) {
    ResetConsoleState();
    static int s_calls = 0; static int s_lastAdd = -99;
    s_calls = 0;
    ConsoleHooks h{};
    h.setCtrlHandler = [](guild::i32 add) -> guild::i32 {
        ++s_calls; s_lastAdd = add; return 1; };
    SetConsoleHooks(&h);

    CHECK_EQ(Console_InstallCtrlHandler(), 1);
    CHECK_EQ(s_lastAdd, 1);
    // Already installed → no second OS call.
    CHECK_EQ(Console_InstallCtrlHandler(), 1);
    CHECK_EQ(s_calls, 1);

    // Remove succeeds → flag cleared → returns true (flag==0).
    CHECK(Console_RemoveCtrlHandler());
    CHECK_EQ(s_lastAdd, 0);
    CHECK_EQ(GetConsoleState().ctrlInstalled, 0);
    SetConsoleHooks(nullptr);
}

TEST(ConsoleReconCtrl, RemoveFailureKeepsFlag) {
    ResetConsoleState();
    ConsoleHooks h{};
    // install succeeds, remove fails (returns 0) → flag stays set, returns false.
    h.setCtrlHandler = [](guild::i32 add) -> guild::i32 {
        return add == 1 ? 1 : 0; };
    SetConsoleHooks(&h);
    CHECK_EQ(Console_InstallCtrlHandler(), 1);
    CHECK(!Console_RemoveCtrlHandler());
    CHECK_EQ(GetConsoleState().ctrlInstalled, 1);
    SetConsoleHooks(nullptr);
}

// ---------------------------------------------------------------------------
// 0x609c40 — ReadCharEvent: single printable char (repeat 1).
TEST(ConsoleReconReadChar, SinglePrintable) {
    ResetConsoleState();
    ConsoleHooks h{};
    h.readConsoleInput = &ScriptedRead;
    SetConsoleHooks(&h);

    std::vector<ConsoleKeyRecord> script = {
        MakeKey(1, 1, 1, 0x41, 0x1E, 'X', 0),
    };
    InstallScript(script);
    CHECK_EQ(Console_ReadCharEvent(0), 'X');
    // repeat==1 → no peek pending.
    CHECK_EQ(GetConsoleState().peekPhase, 0);
    SetConsoleHooks(nullptr);
}

// Repeating printable: repeatCount=3 → first call returns char + arms phase 1;
// subsequent calls replay until exhausted.
TEST(ConsoleReconReadChar, RepeatingPrintable) {
    ResetConsoleState();
    ConsoleHooks h{};
    h.readConsoleInput = &ScriptedRead;
    SetConsoleHooks(&h);

    std::vector<ConsoleKeyRecord> script = {
        MakeKey(1, 1, 3, 0x41, 0x1E, 'Z', 0),
    };
    InstallScript(script);

    CHECK_EQ(Console_ReadCharEvent(0), 'Z');     // read, repeatLeft=2, phase=1
    CHECK_EQ(GetConsoleState().peekPhase, 1);
    CHECK_EQ(Console_ReadCharEvent(0), 'Z');     // phase1 replay, repeatLeft=1
    CHECK_EQ(GetConsoleState().peekPhase, 1);
    CHECK_EQ(Console_ReadCharEvent(0), 'Z');     // phase1 replay, repeatLeft=0
    CHECK_EQ(GetConsoleState().peekPhase, 0);    // drained
    SetConsoleHooks(nullptr);
}

// Function/enhanced key: emits 0 first (arms phase 2 + stashes scan code),
// then the scan code on the next call.
TEST(ConsoleReconReadChar, FunctionKeyTwoByteSequence) {
    ResetConsoleState();
    ConsoleHooks h{};
    h.readConsoleInput = &ScriptedRead;
    SetConsoleHooks(&h);

    // ascii==0 → function key; scan code 0x3B (F1).
    std::vector<ConsoleKeyRecord> script = {
        MakeKey(1, 1, 1, 0x70 /*VK_F1*/, 0x3B, 0, 0),
    };
    InstallScript(script);

    CHECK_EQ(Console_ReadCharEvent(0), 0);          // prefix byte
    CHECK_EQ(GetConsoleState().peekPhase, 2);
    CHECK_EQ(GetConsoleState().pendingFnByte, 0x3B);
    CHECK_EQ(Console_ReadCharEvent(0), 0x3B);       // scan code
    CHECK_EQ(GetConsoleState().peekPhase, 0);
    SetConsoleHooks(nullptr);
}

// Read failure → -1.
TEST(ConsoleReconReadChar, ReadFailureReturnsMinusOne) {
    ResetConsoleState();
    ConsoleHooks h{};
    h.readConsoleInput = [](guild::i32, ConsoleKeyRecord*) { return false; };
    SetConsoleHooks(&h);
    CHECK_EQ(Console_ReadCharEvent(0), -1);
    SetConsoleHooks(nullptr);
}

// ---------------------------------------------------------------------------
// 0x609d44 — GetCh: ungetch slot fast-path.
TEST(ConsoleReconGetCh, UngetSlotReturnedFirst) {
    ResetConsoleState();
    SetConsoleHooks(nullptr);
    GetConsoleState().ungetSlot = 0x42;
    CHECK_EQ(Console_GetCh(0), 0x42);
    CHECK_EQ(GetConsoleState().ungetSlot, 0);  // consumed
}

// GetCh redirect path: argSink receives `this`, getRedirect supplies the char.
TEST(ConsoleReconGetCh, RedirectPath) {
    ResetConsoleState();
    static guild::i32 s_sunk = -1;
    s_sunk = -1;
    ConsoleHooks h{};
    h.argSink     = [](guild::i32 a) { s_sunk = a; };
    h.getRedirect = []() -> guild::i32 { return 0x55; };
    SetConsoleHooks(&h);
    CHECK_EQ(Console_GetCh(0x1234), 0x55);
    CHECK_EQ(s_sunk, 0x1234);
    SetConsoleHooks(nullptr);
}

// GetCh full read path: saves/restores console mode around the read.
TEST(ConsoleReconGetCh, ModeSaveRestoreAroundRead) {
    ResetConsoleState();
    static u32 s_setModes[4]; static int s_n = 0;
    s_n = 0;
    ConsoleHooks h{};
    h.readConsoleInput = &ScriptedRead;
    h.getConsoleMode = [](guild::i32) -> u32 { return 0xABCD; };
    h.setConsoleMode = [](guild::i32, u32 m) { if (s_n < 4) s_setModes[s_n++] = m; };
    SetConsoleHooks(&h);

    std::vector<ConsoleKeyRecord> script = {
        MakeKey(1, 1, 1, 0x42, 0x30, 'Q', 0),
    };
    InstallScript(script);
    CHECK_EQ(Console_GetCh(0), 'Q');
    // SetConsoleMode(h,0) then SetConsoleMode(h, savedMode).
    CHECK_EQ(s_n, 2);
    CHECK_EQ(s_setModes[0], 0u);
    CHECK_EQ(s_setModes[1], 0xABCDu);
    SetConsoleHooks(nullptr);
}

// ---------------------------------------------------------------------------
// 0x609dc0 — PutCh: write path emits the byte and returns ch unchanged.
TEST(ConsoleReconPutCh, WritePathEmitsByte) {
    ResetConsoleState();
    static u8 s_written = 0; static guild::i32 s_handle = -1;
    s_written = 0; s_handle = -1;
    ConsoleHooks h{};
    h.openOutput = []() -> guild::i32 { return 9; };
    h.writeConsoleChar = [](guild::i32 hnd, u8 c) { s_handle = hnd; s_written = c; };
    SetConsoleHooks(&h);

    CHECK_EQ(Console_PutCh('M', 0), 'M');
    CHECK_EQ(static_cast<int>(s_written), 'M');
    CHECK_EQ(s_handle, 9);
    SetConsoleHooks(nullptr);
}

// PutCh redirect path: argSink gets the ecx arg, putRedirect fires, ch returned.
TEST(ConsoleReconPutCh, RedirectPath) {
    ResetConsoleState();
    static guild::i32 s_arg = -1; static int s_fired = 0;
    s_arg = -1; s_fired = 0;
    ConsoleHooks h{};
    h.argSink     = [](guild::i32 a) { s_arg = a; };
    h.putRedirect = []() { ++s_fired; };
    SetConsoleHooks(&h);

    CHECK_EQ(Console_PutCh('N', 0x99), 'N');
    CHECK_EQ(s_arg, 0x99);
    CHECK_EQ(s_fired, 1);
    SetConsoleHooks(nullptr);
}
