#include "io/save_browser.h"

#include <cstring>

namespace guild::io {

// String constants recovered byte-for-byte via get_bytes.
//   ".SAV"      @0x624f08
//   "QUICKSAVE" @0x624ef0
//   "AUTOSAVE"  @0x624efc
const char kSaveExt[5]       = {'.', 'S', 'A', 'V', '\0'};
const char kQuickSaveTag[10] = {'Q', 'U', 'I', 'C', 'K', 'S', 'A', 'V', 'E', '\0'};
const char kAutoSaveTag[9]   = {'A', 'U', 'T', 'O', 'S', 'A', 'V', 'E', '\0'};

namespace {

// VIBE_Util_StrCmpNoCase @0x5cb8f0 — ASCII case-insensitive compare; returns 0 on
// equal, non-zero otherwise (the original returns a strcmp-style sign).
int StrCmpNoCase(const char* a, const char* b) {
    for (;; ++a, ++b) {
        unsigned char ca = static_cast<unsigned char>(*a);
        unsigned char cb = static_cast<unsigned char>(*b);
        if (ca >= 'a' && ca <= 'z') ca = static_cast<unsigned char>(ca - 32);
        if (cb >= 'a' && cb <= 'z') cb = static_cast<unsigned char>(cb - 32);
        if (ca != cb) return ca < cb ? -1 : 1;
        if (ca == 0) return 0;
    }
}

// VIBE_Util_StrNCopyPad @0x5d9360 — copy up to `max` chars, always NUL-terminating.
void StrNCopyPad(char* dst, const char* src, std::size_t max) {
    std::size_t i = 0;
    for (; i < max && src[i]; ++i) dst[i] = src[i];
    dst[i] = '\0';
}

// VIBE_Util_StrChr @0x5d3ef0 — first occurrence of `c`, or null.
char* StrChr(char* s, char c) {
    for (; *s; ++s)
        if (*s == c) return s;
    return c == 0 ? s : nullptr;
}

// VIBE_Crt_Sprintf_0 emulation for the single "%s/%s" format the enumerator uses.
void FormatPathSlashName(char* out, const char* base, const char* name) {
    char* p = out;
    while (*base) *p++ = *base++;
    *p++ = '/';
    while (*name) *p++ = *name++;
    *p = '\0';
}

} // namespace

// ---------------------------------------------------------------------------
// gilde.exe 0x569530 — VIBE_SaveBrowser_EnumerateSaveFiles
//   (__usercall, eax=basePath, ecx=caseMode, ebx=outRecords, edx=extFilter)
//
// Disassembly map: NormalizeDirPath(basePath, g_vfsRoot, caseMode) -> dir node;
// iterate dir's file array (count @+0x100, array @+0x104, 24-byte entries); for each
// entry e: StrChr(e.name, '.') is the extension; if found, StrNCopyPad 32 bytes of
// it into a scratch and StrCmpNoCase against the filter. On match: copy e.name into
// record+9, Sprintf(record+265, "%s/%s", basePath, e.name), truncate that at the
// first '.', advance the record pointer by 0x210, increment the count.
int SaveBrowserEnumerateSaveFiles(const char* basePath, VfsNode* startDir,
                                  const char* extFilter,
                                  SaveBrowserRecord* outRecords) {
    // result = NormalizeDirPath(basePath, dword_62EB78, caseMode)
    VfsNode* dir = NormalizeDirPath(basePath, startDir);
    if (!dir)
        return 0;

    const int count = static_cast<int>(dir->countOrTime);   // dir +0x100 file count
    VfsFileEntry* arr = nullptr;
    if (count > 0)
        arr = ArrayOf(dir->arrayOrArch);                    // dir +0x104 file array

    int emitted = 0;                                        // v20
    char extScratch[32];                                    // v16[32]
    for (int i = 0; i < count; ++i) {
        VfsFileEntry* e = &arr[i];                          // entry stride 24
        char* dot = StrChr(e->name, '.');                   // VIBE_Util_StrChr(*v6,'.')
        if (dot)
            StrNCopyPad(extScratch, dot, 32);               // copy ext into scratch
        // if (!StrCmpNoCase(scratch, filter))  -> match
        if (StrCmpNoCase(extScratch, extFilter) == 0) {
            SaveBrowserRecord& rec = outRecords[emitted];
            // name field @+9 = verbatim file name (loop copies *v6 into ecx=record+9)
            StrNCopyPad(rec.name, e->name, sizeof(rec.name) - 1);
            // full path @+265 = "basePath/name"
            FormatPathSlashName(rec.fullPath, basePath, e->name);
            // truncate the path at its first '.'
            char* pdot = StrChr(rec.fullPath, '.');
            if (pdot)
                *pdot = '\0';
            ++emitted;
        }
    }
    return emitted;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x569c50 — VIBE_SaveBrowser_FindSaveSlot
//   (__usercall, edx=slotTable, ecx=record, ebx=preferredSlot)
//
// Faithful translation of the original goto control flow. StrCmpNoCase returns 0 on
// equal, so `if (StrCmpNoCase(...))` reads as "if NOT equal".
int SaveBrowserFindSaveSlot(guild::u8* slotTable, const SaveBrowserRecord* record,
                            int preferredSlot) {
    const char* name = record->name;                        // a2 + 9
    int a3 = preferredSlot;                                  // ebx

    if (StrCmpNoCase(name, kQuickSaveTag) != 0) {           // not "QUICKSAVE"
        bool toLabel11 = false;
        if (StrCmpNoCase(name, kAutoSaveTag) != 0) {        // not "AUTOSAVE"
            if (a3)                                          // preferred != 0
                toLabel11 = true;                            // goto LABEL_11
        } else {
            a3 = 0;                                          // is AUTOSAVE -> slot 0
        }
        if (!toLabel11) {
            if (StrCmpNoCase(name, kAutoSaveTag) != 0)      // not "AUTOSAVE"
                return -1;
            // fallthrough to LABEL_11
        }
        // LABEL_11:
        if (a3 == 1)
            goto LABEL_3;
        goto LABEL_12;
    }

    a3 = 1;                                                  // is QUICKSAVE
LABEL_3:
    if (StrCmpNoCase(name, kQuickSaveTag) != 0)             // not "QUICKSAVE"
        return -1;

LABEL_12:
    if (a3 != -1 &&
        *reinterpret_cast<const guild::u32*>(slotTable + kSlotStride * a3 + kSlotIdOff)
            != 0xFFFFFFFFu)
        return -1;                                          // slot occupied
    if (a3 == -1)
        return a3;

    guild::u8* slot = slotTable + kSlotStride * a3;         // 544*a3 + a1
    slot[kSlotMarkerOff] = static_cast<guild::u8>(a3);      // *(v7-16) = a3
    std::memcpy(slot + kSlotRecordOff, record, kSlotRecordSize); // qmemcpy(v7,a2,0x210)
    return a3;
}

} // namespace guild::io
