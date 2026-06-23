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

// VIBE_Util_StrCmpNoCase @0x5cb8f0 — ASCII case-insensitive compare.
// Disasm-exact: fold A-Z (0x41..0x5A) to lower-case by +0x20, compare folded bytes,
// stop on mismatch or NUL, return (foldedA - foldedB) as a signed int (the original
// does `xor edx,edx; mov dl,al; movzx eax,ah; sub edx,eax`). Equal -> 0.
int StrCmpNoCase(const char* a, const char* b) {
    for (;; ++a, ++b) {
        unsigned char ca = static_cast<unsigned char>(*a);
        unsigned char cb = static_cast<unsigned char>(*b);
        if (ca >= 0x41 && ca <= 0x5A) ca = static_cast<unsigned char>(ca + 0x20);
        if (cb >= 0x41 && cb <= 0x5A) cb = static_cast<unsigned char>(cb + 0x20);
        if (ca != cb || cb == 0)
            return static_cast<int>(ca) - static_cast<int>(cb);
    }
}

// VIBE_Util_StrNCopyPad @0x5d9360 — copy up to `max` chars from `src`, then zero-fill
// the remainder so EXACTLY `max` bytes of `dst` are written. The original writes no
// terminator beyond the buffer: if `src` is >= `max` long, dst[max-1] is a data byte
// (NOT NUL). It returns dst (unused here).
void StrNCopyPad(char* dst, const char* src, std::size_t max) {
    std::size_t i = 0;
    for (; i < max && src[i]; ++i) dst[i] = src[i];   // copy until NUL or max
    for (; i < max; ++i) dst[i] = '\0';               // zero-pad the rest
}

// VIBE_Util_StrChr @0x5d3ef0 — returns a pointer to the LAST occurrence of `c` in `s`
// (the original scans the whole string, recording the last match), or null if none.
// For c==0 it matches the NUL terminator, returning a pointer to it.
char* StrChr(char* s, char c) {
    char* found = nullptr;
    for (;; ++s) {
        if (*s == c) found = s;
        if (*s == '\0') break;
    }
    return found;
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
// Disassembly map (@0x569530, verified line-for-line):
//   dir = NormalizeDirPath(basePath, g_vfsRoot, caseMode)        (0x569542)
//   count = *(dir+0x100); array = *(dir+0x104); 24-byte entries  (0x569558/0x569587)
//   per entry e:
//     v7 = StrChr(e.name, '.')                                   (0x569597)  [LAST dot]
//     if (v7) StrNCopyPad(extScratch, v7, 32)                    (0x56965c)  [only if dot]
//     if (StrCmpNoCase(extScratch, extFilter) == 0):             (0x5695ac)  [stale if no dot]
//       copy e.name verbatim (unbounded 2-byte loop) into record+9   (0x5695bb)
//       Sprintf(record+265, "%s/%s", basePath, e.name)              (0x5695e7) [NOT truncated]
//       v14 = StrChr(record+9, '.'); if (v14) *v14 = 0              (0x5695f6) [truncate NAME]
//       record += 0x210; ++emitted                                  (0x56960a/0x569616)
// NOTE: it is the NAME field (+9) that is truncated at its (last) '.', NOT the full
// path. The full path keeps the extension. extScratch is NOT re-cleared between
// entries, so a no-dot entry compares stale scratch (faithfully preserved here).
int SaveBrowserEnumerateSaveFiles(const char* basePath, VfsNode* startDir,
                                  const char* extFilter,
                                  SaveBrowserRecord* outRecords, int maxRecords) {
    // result = NormalizeDirPath(basePath, dword_62EB78, caseMode)
    VfsNode* dir = NormalizeDirPath(basePath, startDir);
    if (!dir)
        return 0;

    const int count = static_cast<int>(dir->countOrTime);   // dir +0x100 file count
    VfsFileEntry* arr = nullptr;
    if (count > 0)
        arr = ArrayOf(dir->arrayOrArch);                    // dir +0x104 file array

    int emitted = 0;                                        // v20
    char extScratch[32];                                    // v16[32] (uninit, as orig)
    for (int i = 0; i < count; ++i) {
        VfsFileEntry* e = &arr[i];                          // entry stride 24
        char* dot = StrChr(e->name, '.');                   // VIBE_Util_StrChr(*v6,'.')
        if (dot)
            StrNCopyPad(extScratch, dot, 32);               // copy ext into scratch
        // if (!StrCmpNoCase(scratch, filter))  -> match  (runs even when no dot)
        if (StrCmpNoCase(extScratch, extFilter) == 0) {
            // fail-safe: stop before overrunning the caller's buffer (default
            // maxRecords<0 keeps the original unbounded 1:1 behavior).
            if (maxRecords >= 0 && emitted >= maxRecords)
                break;
            SaveBrowserRecord& rec = outRecords[emitted];
            // name field @+9 = verbatim file name (loop copies *v6 into ecx=record+9,
            // unbounded — the original trusts the name to fit the 256-byte field).
            std::size_t k = 0;
            for (; e->name[k]; ++k)
                rec.name[k] = e->name[k];
            rec.name[k] = '\0';
            // full path @+265 = "basePath/name" (kept whole, extension included).
            FormatPathSlashName(rec.fullPath, basePath, e->name);
            // truncate the NAME field at its (last) '.'  (StrChr -> *v14 = 0).
            char* pdot = StrChr(rec.name, '.');
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
