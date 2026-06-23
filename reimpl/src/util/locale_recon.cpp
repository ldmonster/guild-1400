// =====================================================================================
// Locale reconstruction — implementation. See locale_recon.h for the spec.
// =====================================================================================
#include "util/locale_recon.h"

#include <cstring>

namespace guild::util {

// -------------------------------------------------------------------------------------
// Language-name table @0x5a3260 — 5 records x 0x40 bytes, recovered byte-for-byte
// (only the leading C-string of each 64-byte slot is meaningful; the rest is zero pad).
//   [0]="GERMAN" [1]="ENGLISH" [2]="FRENCH" [3]="ITALIAN" [4]="SPANISH"
// gilde.exe 0x5a3260 — aGerman_1 (table base used by the ecx scan).
static const char kLanguageTable[5][64] = {
    "GERMAN",
    "ENGLISH",
    "FRENCH",
    "ITALIAN",
    "SPANISH",
};

// VIBE_Util_StrToUpper @0x5e9f50 — in-place ASCII upper-case ('a'..'z' -> 'A'..'Z').
// Reconstructed here locally to keep the cluster self-contained (the live util module
// provides the same routine; behavior identical for the ASCII language names).
static void StrToUpperAscii(char* s) {
    for (; *s; ++s) {
        unsigned char c = static_cast<unsigned char>(*s);
        if (c >= 'a' && c <= 'z')
            *s = static_cast<char>(c - 32);
    }
}

// VIBE_Util_StrCmp @0x5d3f10 — faithful byte strcmp; returns 0 when equal. The original
// compares unsigned bytes; for the match test only equality (==0) matters here.
static int UtilStrCmp(const char* a, const char* b) {
    for (;; ++a, ++b) {
        unsigned char ca = static_cast<unsigned char>(*a);
        unsigned char cb = static_cast<unsigned char>(*b);
        if (ca != cb)
            return ca < cb ? -1 : 1;
        if (ca == 0)
            return 0;
    }
}

u16& LanguageIdGlobal() {
    static u16 g_word_649D48 = 0;  // word_649D48
    return g_word_649D48;
}

// gilde.exe 0x5a33a0 — VIBE_Locale_CopyLanguageString
u32 LocaleCopyLanguageString(const char* name) {
    // The original copies `name` 2 bytes at a time into a 0x40-byte stack buffer (v11),
    // stopping at the terminating NUL, then upper-cases it in place.
    char buf[0x40];
    {
        const char* src = name;
        char* dst = buf;
        char c0;
        do {
            c0 = src[0];
            dst[0] = c0;
            if (!c0)
                break;
            char c1 = src[1];
            src += 2;
            dst[1] = c1;
            dst += 2;
            if (!c1)
                break;
        } while (true);
    }
    StrToUpperAscii(buf);

    // word_649D48 = 0xFFFF (the dx initialiser before the scan loop).
    LanguageIdGlobal() = 0xFFFF;

    // Linear scan over the 5 x 64-byte language records.
    unsigned int idx = 0;
    while (UtilStrCmp(buf, kLanguageTable[idx]) != 0) {
        ++idx;
        if (idx >= 5)
            return 0xFFFF;
    }
    LanguageIdGlobal() = static_cast<u16>(idx);
    return idx;
}

// -------------------------------------------------------------------------------------
// SetupMbcsCodePage
// -------------------------------------------------------------------------------------
static unsigned int DefaultGetACP()   { return 1252; }
static unsigned int DefaultGetOEMCP() { return 437; }
static int DefaultGetCPInfo(unsigned int /*cp*/, u8* leadByteOut12) {
    // Inert C-locale answer: success, no DBCS lead-byte ranges.
    std::memset(leadByteOut12, 0, 12);
    return 1;
}

MbcsHooks& MbcsHookTable() {
    static MbcsHooks g_hooks{DefaultGetACP, DefaultGetOEMCP, DefaultGetCPInfo};
    return g_hooks;
}

MbcsState& MbcsStateGlobal() {
    static MbcsState g_state{};  // byte_140A9D0 / dword_140A9C0 / CodePage (all zero)
    return g_state;
}

// VIBE_Light_SetGrayColorThunk(0, n, p) is, at these call sites, a memset(p, 0, n)
// (clears the 257-byte lead-byte table). Reproduce that clear directly.
static void ClearLeadByteTable() {
    std::memset(MbcsStateGlobal().leadByte, 0, 257);
}

// gilde.exe 0x609f90 — VIBE_Locale_SetupMbcsCodePage
int LocaleSetupMbcsCodePage(unsigned int cp) {
    MbcsHooks& hooks = MbcsHookTable();
    MbcsState& st = MbcsStateGlobal();

    unsigned int acp = cp;
    switch (cp) {
        case 0xFFFFFFFFu:
            acp = hooks.getACP();
            break;
        case 0xFFFFFFFEu:
            acp = hooks.getOEMCP();
            break;
        case 0xFFFFFFFDu:
            ClearLeadByteTable();
            st.dbcs = 0;       // dword_140A9C0 = 0
            st.codePage = 0;   // CodePage = 0
            return 0;
        case 0xFFFFFFFCu:
            ClearLeadByteTable();
            for (int i = 129; i <= 159; ++i)
                st.leadByte[i] = 1;
            for (int j = 224; j <= 252; ++j)
                st.leadByte[j] = 1;
            st.dbcs = 1;        // dword_140A9C0 = 1
            st.codePage = 932;  // CodePage = 932 (Shift-JIS)
            return 0;
        default:
            break;
    }

    if (!acp)
        acp = 1;

    u8 leadByte[12];
    if (!hooks.getCPInfo(acp, leadByte))
        return 1;

    ClearLeadByteTable();
    st.dbcs = (leadByte[0] != 0);  // dword_140A9C0 = LeadByte[0] != 0

    // Walk the LeadByte pairs (range lo..hi), terminated by a 0,0 pair.
    for (int k = 0; leadByte[k] || leadByte[k + 1]; k += 2) {
        for (int m = leadByte[k]; m <= leadByte[k + 1]; ++m)
            st.leadByte[m] = 1;
    }

    if (acp == 1) {
        st.codePage = static_cast<int>(hooks.getOEMCP());
        return 0;
    }
    st.codePage = static_cast<int>(acp);
    return 0;
}

} // namespace guild::util
