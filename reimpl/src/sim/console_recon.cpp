#include "sim/console_recon.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// Process-global hook table + console state (single definition).
namespace {

// Inert defaults. Pure / side-effect-free so the parse/dispatch logic runs
// headless. openInput/openOutput return a non-(-1) sentinel so OpenConHandles
// converges; readConsoleInput reports "no input" (the original's
// ReadConsoleInputA==0 → ReadCharEvent returns -1).
constexpr i32 kDefaultInputSentinel  = 1;   // any value != -1
constexpr i32 kDefaultOutputSentinel = 2;   // any value != -1

i32  DefaultOpenInput()  { return kDefaultInputSentinel; }
i32  DefaultOpenOutput() { return kDefaultOutputSentinel; }
i32  DefaultSetCtrlHandler(i32 /*add*/) { return 1; }   // success
u32  DefaultGetConsoleMode(i32 /*h*/)   { return 0; }
bool DefaultReadConsoleInput(i32 /*h*/, ConsoleKeyRecord* /*out*/) { return false; }

ConsoleHooks MakeDefaultHooks() {
    ConsoleHooks h;
    h.lock              = nullptr;
    h.unlock            = nullptr;
    h.openInput         = &DefaultOpenInput;
    h.openOutput        = &DefaultOpenOutput;
    h.setCtrlHandler    = &DefaultSetCtrlHandler;
    h.getConsoleMode    = &DefaultGetConsoleMode;
    h.setConsoleMode    = nullptr;
    h.readConsoleInput  = &DefaultReadConsoleInput;
    h.writeConsoleChar  = nullptr;
    h.argSink           = nullptr;
    h.getRedirect       = nullptr;
    h.putRedirect       = nullptr;
    return h;
}

ConsoleHooks  g_defaultHooks = MakeDefaultHooks();
const ConsoleHooks* g_hooks = &g_defaultHooks;
ConsoleState  g_state;

} // namespace

void SetConsoleHooks(const ConsoleHooks* hooks) {
    g_hooks = hooks ? hooks : &g_defaultHooks;
}
const ConsoleHooks& GetConsoleHooks() { return *g_hooks; }
ConsoleState&       GetConsoleState() { return g_state; }
void                ResetConsoleState() { g_state = ConsoleState{}; }

// ---------------------------------------------------------------------------
// gilde.exe 0x609090 — VIBE_Console_IsValidKeyEvent.
//
//   result = 0;
//   if ( *(_WORD*)a1 == 1 )            // EventType == KEY_EVENT
//     if ( *(_DWORD*)(a1+4) )          // bKeyDown
//     {   v1 = *(_WORD*)(a1+10);       // wVirtualKeyCode
//         if ( v1 < 0x10u || v1 > 0x12u ) return 1; }
//   return result;
i32 Console_IsValidKeyEvent(const ConsoleKeyRecord& rec) {
    i32 result = 0;
    if (rec.eventType == 1) {
        if (rec.keyDown != 0) {
            u16 v1 = rec.virtualKeyCode;
            if (v1 < 0x10u || v1 > 0x12u)
                return 1;
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x6090b8 — VIBE_Console_OpenConHandles.
//
//   off_64A910();                       // lock
//   if ( dword_64C124 == -1 ) dword_64C124 = CreateFileA("conin$", ...);
//   if ( dword_64C128 == -1 ) dword_64C128 = CreateFileA("conout$", ...);
//   off_64A914();                       // unlock
void Console_OpenConHandles() {
    const ConsoleHooks& h = GetConsoleHooks();
    ConsoleState& s = GetConsoleState();

    if (h.lock) h.lock();
    if (s.inputHandle == -1)
        s.inputHandle = h.openInput ? h.openInput() : -1;
    if (s.outputHandle == -1)
        s.outputHandle = h.openOutput ? h.openOutput() : -1;
    if (h.unlock) h.unlock();
}

// gilde.exe 0x609128 — VIBE_Console_GetInputHandle.
i32 Console_GetInputHandle() {
    Console_OpenConHandles();
    return GetConsoleState().inputHandle;
}

// gilde.exe 0x609134 — VIBE_Console_GetOutputHandle.
i32 Console_GetOutputHandle() {
    Console_OpenConHandles();
    return GetConsoleState().outputHandle;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x60929c — VIBE_Console_InstallCtrlHandler.
//
//   if ( !byte_64C194 )
//     byte_64C194 = SetConsoleCtrlHandler(HandlerRoutine, 1);
//   return (u8)byte_64C194;
i32 Console_InstallCtrlHandler() {
    const ConsoleHooks& h = GetConsoleHooks();
    ConsoleState& s = GetConsoleState();
    if (!s.ctrlInstalled) {
        i32 ok = h.setCtrlHandler ? h.setCtrlHandler(1) : 0;
        s.ctrlInstalled = static_cast<u8>(ok);
    }
    return static_cast<u8>(s.ctrlInstalled);
}

// gilde.exe 0x6092cc — VIBE_Console_RemoveCtrlHandler.
//
//   if ( byte_64C194 && SetConsoleCtrlHandler(HandlerRoutine, 0) )
//     byte_64C194 = 0;
//   return byte_64C194 == 0;
bool Console_RemoveCtrlHandler() {
    const ConsoleHooks& h = GetConsoleHooks();
    ConsoleState& s = GetConsoleState();
    if (s.ctrlInstalled && (h.setCtrlHandler ? h.setCtrlHandler(0) : 0))
        s.ctrlInstalled = 0;
    return s.ctrlInstalled == 0;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x609c40 — VIBE_Console_ReadCharEvent.
//
// State machine over (peekPhase=dword_64C1E8, lastChar=dword_140AB04,
// repeatLeft=dword_140AB08, pendingFnByte=dword_140AB00). Faithfully
// translated; goto LABEL_11 (the read path) preserved as a structured fallthrough.
i32 Console_ReadCharEvent(i32 handle) {
    const ConsoleHooks& h = GetConsoleHooks();
    ConsoleState& s = GetConsoleState();

    i32 v1        = s.repeatLeft;   // esi
    i32 AsciiChar = s.lastChar;     // edi
    i32 result;

    bool doRead = false;            // models the LABEL_11 entry

    if (s.peekPhase) {
        if (static_cast<u32>(s.peekPhase) <= 1u) {
            // peekPhase == 1: replay the buffered repeating char.
            v1 = s.repeatLeft - 1;
            if (s.lastChar) {
                if (s.repeatLeft == 1)
                    s.peekPhase = 0;
            } else {
                s.peekPhase = 2;
            }
            result = s.lastChar;
        } else if (s.peekPhase != 2) {
            // any phase > 2 → drop into the read path (goto LABEL_11).
            doRead = true;
            result = 0;  // overwritten in the read path
        } else {
            // peekPhase == 2: emit the pending function/scan byte.
            s.peekPhase = (s.repeatLeft != 0) ? 1 : 0;
            result = s.pendingFnByte;
        }
    } else {
        doRead = true;
        result = 0;  // overwritten in the read path
    }

    if (doRead) {
        // do { read } while ( !IsValidKeyEvent(&Buffer) );
        ConsoleKeyRecord buffer{};
        bool readOk;
        for (;;) {
            readOk = h.readConsoleInput ? h.readConsoleInput(handle, &buffer)
                                        : false;
            if (!readOk) {
                // ReadConsoleInputA failed → result = -1; restore last/repeat.
                result    = -1;
                AsciiChar = s.lastChar;
                v1        = s.repeatLeft;
                // LABEL_20
                s.lastChar   = AsciiChar;
                s.repeatLeft = v1;
                return result;
            }
            if (Console_IsValidKeyEvent(buffer))
                break;
        }

        v1        = static_cast<i32>(static_cast<u16>(buffer.repeatCount)) - 1;
        AsciiChar = static_cast<u8>(buffer.asciiChar);

        // (controlKeyState bit0 == 0) && asciiChar != 0  → printable key.
        if ((buffer.controlKeyState & 1) == 0 && buffer.asciiChar != 0) {
            if (buffer.repeatCount != 1)
                s.peekPhase = 1;
        } else {
            // Function/enhanced key: emit 0 now, stash scan code for next call.
            AsciiChar       = 0;
            s.peekPhase     = 2;
            s.pendingFnByte = buffer.virtualScanCode;
        }
        result = AsciiChar;
    }

    // LABEL_20
    s.lastChar   = AsciiChar;
    s.repeatLeft = v1;
    return result;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x609d44 — VIBE_Console_GetCh (__thiscall).
//
//   if ( dword_64A990 ) { dword_64A990 = 0; return that; }
//   else if ( dword_64AA84 ) { dword_64AA50(this); return dword_64AA84(); }
//   else {  off_64A910();
//           h = GetInputHandle();
//           mode = GetConsoleMode(h);
//           SetConsoleMode(h, 0);
//           c = ReadCharEvent(h);
//           SetConsoleMode(h, mode);
//           off_64A914();
//           return c; }
i32 Console_GetCh(i32 thisArg) {
    const ConsoleHooks& h = GetConsoleHooks();
    ConsoleState& s = GetConsoleState();

    i32 result = s.ungetSlot;
    if (s.ungetSlot) {
        s.ungetSlot = 0;
        return result;
    }

    if (h.getRedirect) {
        if (h.argSink) h.argSink(thisArg);
        return h.getRedirect();
    }

    if (h.lock) h.lock();
    i32 inputHandle = Console_GetInputHandle();
    u32 mode = h.getConsoleMode ? h.getConsoleMode(inputHandle) : 0;
    if (h.setConsoleMode) h.setConsoleMode(inputHandle, 0);
    i32 charEvent = Console_ReadCharEvent(inputHandle);
    if (h.setConsoleMode) h.setConsoleMode(inputHandle, mode);
    if (h.unlock) h.unlock();
    return charEvent;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x609dc0 — VIBE_Console_PutCh (__usercall, eax=ch, ecx=arg).
//
//   Buffer[0] = ch;
//   if ( dword_64AA8C ) { dword_64AA50(arg); dword_64AA8C(); }
//   else { off_64A910(); h = GetOutputHandle();
//          WriteConsoleA(h, Buffer, 1, ...); off_64A914(); }
//   return ch;
i32 Console_PutCh(i32 ch, i32 arg) {
    const ConsoleHooks& h = GetConsoleHooks();
    u8 byte = static_cast<u8>(ch);

    if (h.putRedirect) {
        if (h.argSink) h.argSink(arg);
        h.putRedirect();
    } else {
        if (h.lock) h.lock();
        i32 outputHandle = Console_GetOutputHandle();
        if (h.writeConsoleChar) h.writeConsoleChar(outputHandle, byte);
        if (h.unlock) h.unlock();
    }
    return ch;
}

} // namespace guild::sim
