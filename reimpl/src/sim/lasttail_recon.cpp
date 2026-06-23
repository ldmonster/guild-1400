#include "sim/lasttail_recon.h"

#include "util/string_ops.h"   // guild::util::StrCmpNoCase   @0x5cb8f0
#include "util/util_recon.h"   // guild::util::ReconStrCmp    @0x5d3f10

namespace guild::sim {

// --- Forwarder thunks to string-compare leaves ------------------------------

// gilde.exe 0x5dcd36 — VIBE_Util_StrCmpNoCase_Thunk: return StrCmpNoCase(a, b).
int Util_StrCmpNoCase_Thunk(const char* a, const char* b) {
    return guild::util::StrCmpNoCase(a, b);          /*0x5dcd36*/
}

// gilde.exe 0x44ec98 — VIBE_Util_StrCmpThunk: return StrCmp(*a2, a1).
int Util_StrCmpThunk(const char* a1, const char* const* a2) {
    return guild::util::ReconStrCmp(*a2, a1);        /*0x44ec9f*/
}

// gilde.exe 0x44eca0 — VIBE_Util_StrCmpNoCaseThunk: return StrCmpNoCase_Thunk(a1, *a2).
int Util_StrCmpNoCaseThunk(const char* a1, const char* const* a2) {
    return Util_StrCmpNoCase_Thunk(a1, *a2);         /*0x44eca7*/
}

// gilde.exe 0x44f6a4 — VIBE_Util_StrCmpNoCaseDerefThunk: return StrCmpNoCase_Thunk(*a1, *a2).
int Util_StrCmpNoCaseDerefThunk(const char* const* a1, const char* const* a2) {
    return Util_StrCmpNoCase_Thunk(*a1, *a2);        /*0x44f6ad*/
}

// --- Forwarder thunks to script-VM leaves -----------------------------------

// gilde.exe 0x43c6fc — VIBE_Script_FinishThunk: Script_Finish(*a1); return 0.
int Script_FinishThunk(const int* a1, const ScriptThunkHooks& hooks) {
    if (hooks.scriptFinish)
        hooks.scriptFinish(*a1, hooks.user);         /*0x43c6fe*/
    return 0;                                        /*0x43c705*/
}

// gilde.exe 0x43c788 — VIBE_Script_FindByNameThunk: return Script_FindByName(a1, a2).
int Script_FindByNameThunk(int a1, int a2, const ScriptThunkHooks& hooks) {
    if (hooks.scriptFindByName)
        return hooks.scriptFindByName(a1, a2, hooks.user); /*0x43c78f*/
    return 0;
}

// --- Trivial no-op / return-0 stubs -----------------------------------------

void Util_NullStub()  { /* retn */ }                 // 0x5f8224
void Util_NullStub2() { /* retn */ }                 // 0x5fa620
void Util_NullSub()   { /* retn */ }                 // 0x527ddc
int  Util_RetZero_27cc5() { return 0; }              // 0x1427cc5

// --- MSVC command-line program-name skip ------------------------------------

// Default isSpace predicate: matches VIBE_Locale_IsSpace's behavior for the
// plain ASCII space/tab range when no locale hook is injected. The original
// tests IsCType(ch, 0, 4) (the SPACE class). For the inert default we treat the
// classic C whitespace bytes as space.
static int DefaultIsSpace(int ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' ||
           ch == '\v' || ch == '\f' || ch == '\r';
}

// gilde.exe 0x1426221 — VIBE_CmdLine_SkipFirstArg.
const char* CmdLine_SkipFirstArg(const char* cmdline, int (*isSpace)(int)) {
    if (!isSpace) isSpace = &DefaultIsSpace;

    const char* v0 = cmdline;                        /*0x1426230*/
    // The lazy locale-ctype init (if (!dword_146606C) Locale_InitCTypeOnce())
    // is CRT one-time setup, omitted here (the predicate is injected instead).

    if (*v0 != '"') {                                /*0x142623a*/
        // Unquoted: skip the program-name token (bytes > 0x20).
        if (static_cast<unsigned char>(*v0) > 0x20u) {  /*0x1426263*/
            do {
                ++v0;                                /*0x1426265*/
            } while (static_cast<unsigned char>(*v0) > 0x20u); /*0x1426269*/
        }
        // fall through to the trailing-whitespace skip (LABEL_12, no ++v0).
    } else {
        // Quoted program name: scan to the matching closing quote.
        while (true) {                               /*0x142623c*/
            char c = *++v0;
            if (c == '"' || !c)                      /*0x1426246*/
                break;
            if (isSpace(static_cast<unsigned char>(c))) /*0x142624c*/
                ++v0;                                /*0x1426256*/ // quirk: extra advance
        }
        if (*v0 == '"') {                            /*0x142625c*/
            // matched closing quote: advance past it, THEN skip whitespace.
            ++v0;                                    /*0x142625e*/
            while (*v0 && static_cast<unsigned char>(*v0) <= 0x20u) /*0x1426273*/
                ++v0;
            return v0;                               /*0x1426278*/
        }
        // else: fall through to LABEL_12 (no ++v0).
    }

    // LABEL_12: trailing-whitespace skip (the do/while entered at its test).
    while (*v0 && static_cast<unsigned char>(*v0) <= 0x20u)  /*0x1426273*/
        ++v0;
    return v0;                                       /*0x1426278*/
}

} // namespace guild::sim
