#include "crt/mbcs.h"

namespace guild::crt {

namespace {
MbcsState g_mbcs; // mirrors dword_140A9C0 / byte_140A9D1
} // namespace

void MbcsState::SetCp932LeadBytes() {
    active = true;
    for (int b = 0; b < 256; ++b)
        ctype[b] = 0;
    // Shift-JIS lead-byte ranges installed by _setmbcp(932).
    for (int b = 0x81; b <= 0x9F; ++b)
        ctype[b] |= kMbLeadBit;
    for (int b = 0xE0; b <= 0xFC; ++b)
        ctype[b] |= kMbLeadBit;
}

MbcsState& Mbcs() {
    return g_mbcs;
}

// gilde.exe 0x6067d0 — VIBE_Mbcs_IsLeadByte  (__usercall, eax = (byte@eax))
int IsLeadByte(int byte) {
    return Mbcs().ctype[byte & 0xFF] & kMbLeadBit;
}

// gilde.exe 0x60a100 — VIBE_Locale_CharByteLength (_mbclen)  (__usercall, eax = (s@eax))
int CharByteLength(const u8* s) {
    if (Mbcs().active && (Mbcs().ctype[*s] & kMbLeadBit) != 0)
        return 2;
    return 1;
}

// gilde.exe 0x60b050 — VIBE_Mbcs_IsStringEnd  (__usercall, eax = (s@eax))
int IsStringEnd(const u8* s) {
    if (!*s)
        return 1;
    if (Mbcs().active && (Mbcs().ctype[*s] & kMbLeadBit) != 0 && !s[1])
        return 2;
    return 0;
}

// gilde.exe 0x60b0f0 — VIBE_Mbcs_AdvanceChar (_mbsinc)  (__usercall, eax = (s@eax))
const u8* AdvanceChar(const u8* s) {
    if (Mbcs().active && (Mbcs().ctype[*s] & kMbLeadBit) != 0 && s[1])
        return s + 2;
    return s + 1;
}

} // namespace guild::crt
