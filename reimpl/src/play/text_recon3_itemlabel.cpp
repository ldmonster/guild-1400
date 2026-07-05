#include "play/text_recon3_itemlabel.h"

#include <cstdio>
#include <cstring>

// gilde.exe 0x59ccf4 — VIBE_Text_FormatItemLabelWithIcon
//
// Faithful 1:1 reconstruction of the original's control flow. The decompile uses
// a record base pointer `v65 = &word_12CE910[268 * a1]` and reads fields at fixed
// byte offsets. We mirror those byte offsets exactly against the caller-supplied
// `record` byte pointer. The string-table and language-table reads are routed
// through the inert hooks (see header). Provenance addresses on every branch.

namespace guild::play {

namespace {
ItemLabelHooks g_hooks;

// Inert defaults: empty wide (UTF-16LE) string.
const char* DefResolveStringField(int /*id*/, char* scratch) {
    if (scratch) { scratch[0] = 0; scratch[1] = 0; }
    return scratch;
}
const char* kEmptyWide = "\0\0";
const char* DefDefaultLangLabel() { return kEmptyWide; }
const char* DefOutOfRangeLabel()  { return kEmptyWide; }

const char* HResolve(int id, char* scratch) {
    return g_hooks.resolveStringField ? g_hooks.resolveStringField(id, scratch)
                                      : DefResolveStringField(id, scratch);
}
const char* HDefaultLang() {
    return g_hooks.defaultLangLabel ? g_hooks.defaultLangLabel()
                                    : DefDefaultLangLabel();
}
const char* HOutOfRange() {
    return g_hooks.outOfRangeLabel ? g_hooks.outOfRangeLabel()
                                   : DefOutOfRangeLabel();
}

// VIBE_Crt_Sprintf_0 (0x5cba00) writes a narrow (byte) formatted string. In the
// original these calls splice wide name fields with "%s"/"%ss"/"%s$A%s" etc.
// using byte-string semantics (sprintf walks bytes until NUL). We reproduce the
// identical byte-level behavior with std::sprintf.
} // namespace

char* WideCopy(char* dst, const char* src) {
    // The original's inlined `do { *p=*q; if(!*q)break; p[1]=q[1]; p+=2; q+=2; }`
    // loop: copies code units (2 bytes) until the low byte is zero.
    char* p = dst;
    const char* q = src;
    char hi;
    do {
        char lo = q[0];
        p[0] = lo;               /* copy low byte */
        if (!lo) break;          /* low byte 0 -> stop (high byte NOT copied) */
        hi = q[1];               /* high byte */
        q += 2;
        p[1] = hi;               /* copy high byte */
        p += 2;
    } while (hi);                /* continue while high byte != 0 */
    return dst;
}

bool EndsWithPluralBlocker(const char* /*name*/) {
    // Provided for completeness/testing; the real branch uses the byte at
    // record[strlen(field)+offset] (see FieldEndChar below), not strlen(name)
    // on the destination. Kept as a documented helper; not called by the
    // formatter (the formatter uses the record-relative end byte exactly).
    return false;
}

namespace {
// The record is the fixed 536-byte (268-word) entry of word_12CE910 (see header).
// The original reads its name fields with strlen/sprintf/WideCopy assuming a NUL
// inside the record; a malformed (NUL-less) field would over-read past the entry.
// We bound every field read to the record's own 536-byte extent so a degenerate
// record cannot cause an OOB read. On a well-formed (NUL-terminated) field these
// bounds never trip, so the formatted output is byte-identical (goldens unchanged).
constexpr int kItemLabelRecordBytes = 536;

// strlen of `record + field_off`, capped so it never reads past the record end.
size_t BoundedFieldLen(const u8* record, int field_off) {
    if (field_off < 0 || field_off >= kItemLabelRecordBytes)
        return 0;
    const char* field = reinterpret_cast<const char*>(record) + field_off;
    const size_t maxLen = static_cast<size_t>(kItemLabelRecordBytes - field_off);
    size_t n = 0;
    while (n < maxLen && field[n] != 0)
        ++n;
    return n;   // == strlen(field) when a NUL exists in-record; capped otherwise
}

// Copy `record + field_off` into `scratch` (>= the record span) as a NUL-terminated
// byte string, capped at the record boundary. Returns scratch. For a well-formed
// field this is identical to the raw `record + field_off` pointer the original used.
const char* BoundedField(const u8* record, int field_off, char* scratch) {
    const size_t n = BoundedFieldLen(record, field_off);
    if (n) std::memcpy(scratch, record + field_off, n);
    scratch[n] = 0;
    return scratch;
}

// The original tests *((_BYTE*)v65 + strlen((const char*)v65 + field_off) + (field_off-1)).
// i.e. the byte at: record + (field_off - 1) + strlen(record + field_off).
// Reproduce that byte arithmetic exactly — but bound the strlen to the record extent.
unsigned char FieldEndChar(const u8* record, int field_off) {
    size_t n = BoundedFieldLen(record, field_off);
    int idx = (field_off - 1) + static_cast<int>(n);
    if (idx < 0 || idx >= kItemLabelRecordBytes)
        return 0;
    return record[idx];
}
bool IsPluralBlocker(unsigned char c) {
    // {115='s', 122='z', 0xDF='ß', 120='x'}
    return c == 115 || c == 122 || c == 0xDF || c == 120;
}
} // namespace

ItemLabelHooks SetItemLabelHooks(const ItemLabelHooks& hooks) {
    ItemLabelHooks prev = g_hooks;
    g_hooks = hooks;
    return prev;
}
const ItemLabelHooks& GetItemLabelHooks() { return g_hooks; }

int FormatItemLabelWithIcon(u32 item_id,
                            char mode,
                            char* out,
                            char kind,
                            const u8* record,
                            bool is_default_record) {
    const char v67 = mode;          // mode byte
    const char v66 = kind;          // label-flavour selector
    char* const v64_base = out;     // destination

    // The original copies three zero-filled 256-byte scratch buffers
    // (dword_59BF60/59C060/59C160) onto the stack. They are zero-initialized
    // global scratch. We size them to hold a full bounded record field (<=488
    // bytes) plus the "%ss" plural suffix, so a malformed (long, NUL-less) name
    // field cannot overflow the scratch when spliced via sprintf("%ss", name).
    // The hooks' >=256-byte contract is satisfied (kItemLabelRecordBytes==536).
    constexpr int kScratch = kItemLabelRecordBytes + 16;   // 552
    char v61[kScratch]; std::memset(v61, 0, sizeof v61);   /*0x59cd18*/
    char v62[kScratch]; std::memset(v62, 0, sizeof v62);   /*0x59cd2a*/
    char v63[kScratch]; std::memset(v63, 0, sizeof v63);   /*0x59cd3c*/

    // a1 >= 0x300: emit the out-of-range fallback name, return 0.   /*0x59cd40*/
    if (item_id >= 0x300) {
        WideCopy(v64_base, HOutOfRange());                          /*0x59ce94..0x59ceb7*/
        return 0;                                                   /*0x59ceb7*/
    }

    // v65 = &word_12CE910[268*a1]; if (v65 == dword_6498E4) default-label path.
    // We model "is the global default record" via the explicit flag / nullptr.
    if (record == nullptr || is_default_record) {                   /*0x59cda7 (!=) inverted*/
        // Default-record path: copy language default label, append "%s %s"
        // separator (unk_627E00) iff mode==2.                      /*0x59cefd*/
        char* dst = v64_base;
        const char* lang = HDefaultLang();
        WideCopy(dst, lang);                                        /*0x59cf00..0x59cf1c*/
        if (v67 == 2) {                                             /*0x59cf22*/
            // append unk_627E00 ("%s %s" region; the wide string at 0x627e00 is
            // empty in the binary). Concatenate at end-of-string.  /*0x59cf28*/
            char* tail = dst + std::strlen(dst);                    /*0x59ce67*/
            WideCopy(tail, "\0\0");                                 /*0x59ce70..0x59ce86*/
        }
        return 1;                                                   /*0x59ce89*/
    }

    // record-relative byte field pointers, bounded to the 536-byte record extent so
    // a NUL-less/overlong field can never over-read past the entry. For a well-formed
    // (in-record NUL-terminated) field these are byte-identical to the raw record
    // pointers the original used, so the output is unchanged.
    auto B = [&](int off) -> unsigned char {
        return (off >= 0 && off < kItemLabelRecordBytes) ? record[off] : 0;
    };
    char name48Buf[kItemLabelRecordBytes];
    char name64Buf[kItemLabelRecordBytes];
    const char* name48 = BoundedField(record, 48, name48Buf);  // word off 24 (v65+24)
    const char* name64 = BoundedField(record, 64, name64Buf);  // word off 32 (v65+32)
    // kind 8 uses `v65 + 24` (int16*) which is the SAME byte offset 48.
    const char* name24 = name48;

    switch (v66) {                                                  /*0x59cdbf*/
    case 1: {                                                       /*0x59cdc9*/
        WideCopy(v64_base, name48);                                 /*0x59cdd3..0x59cde9*/
        if (v67 != 2) return 1;                                     /*0x59cdef*/
        // plural-suffix on name48 end char.                        /*0x59ce59*/
        unsigned char e = FieldEndChar(record, 48);
        const char* suffix = IsPluralBlocker(e) ? "\0\0"            /*LABEL_27 unk_627E04*/
                                                : "\0\0";           /*LABEL_12 unk_627E00*/
        char* tail = v64_base + std::strlen(v64_base);              /*0x59ce67*/
        WideCopy(tail, suffix);                                     /*0x59ce70..0x59ce86*/
        return 1;
    }
    case 2: {                                                       /*0x59cf3f*/
        if (B(13)) {
            // resolve title string id and prefix it with "$A" + name field.
            int base = B(9) ? 279 : 272;                            /*0x59cf95 / 0x59cf5d*/
            const char* title = HResolve(B(13) + base, v61);        /*0x59cf73*/
            std::sprintf(v64_base, "%s$A%s", title, name48);        /*0x59cf82*/
        } else {
            std::sprintf(v64_base, "%s", name48);                   /*0x59cfaa*/
        }
        return 1;
    }
    case 3: {                                                       /*0x59cfc0*/
        if (B(358)) {
            int base = B(9) ? 560 : 525;                            /*0x59d0a6 / 0x59cffa*/
            const char* title = HResolve(B(358) + base, v61);       /*0x59d013 / 0x59d0e1*/
            if (B(64))
                std::sprintf(v64_base, "%s %s %s", title, name48, name64); /*0x59d022*/
            else
                std::sprintf(v64_base, "%s %s", title, name48);     /*0x59d0f0*/
        } else if (B(64)) {
            std::sprintf(v64_base, "%s %s", name48, name64);        /*0x59d11f*/
        } else {
            std::sprintf(v64_base, "%s", name48);                   /*0x59d13a*/
        }
        if (v67 != 2) return 1;                                     /*0x59d02e*/
        // plural-suffix keyed on name64 end char (offset 64).      /*0x59d09b*/
        unsigned char e = FieldEndChar(record, 64);
        const char* suffix = IsPluralBlocker(e) ? "\0\0" : "\0\0";
        char* tail = v64_base + std::strlen(v64_base);
        WideCopy(tail, suffix);
        return 1;
    }
    case 4: {                                                       /*0x59d14a*/
        if (B(356)) {
            int base = B(9) ? 370 : 294;                            /*0x59d21c / 0x59d173*/
            // id byte = (*(int*)(record+353) >> 24) — `sar ecx, 18h` @0x59d181:
            // an ARITHMETIC shift, i.e. the SIGN-EXTENDED byte at offset 356.
            int id = static_cast<int>(static_cast<signed char>(record[356]));
            const char* title = HResolve(id + base, v62);
            std::sprintf(v64_base, "%s$A%s", title, name48);        /*0x59d19c*/
        } else if (B(357)) {                                        /*0x59d226*/
            int base = B(9) ? 498 : 471;                            /*0x59d275 / 0x59d247*/
            // v65 is __int16*, so (v65 + 177) is byte offset 354;
            // *(int*)(record+354) >> 24 — `sar ecx, 18h` @0x59d255: the
            // SIGN-EXTENDED byte at offset 357.
            int id = static_cast<int>(static_cast<signed char>(record[357]));
            const char* title = HResolve(id + base, v62);           /*0x59d266*/
            std::sprintf(v64_base, "%s$A%s", title, name48);        /*0x59d270*/
        } else if (v67 == 2) {                                      /*0x59d285*/
            unsigned char e = FieldEndChar(record, 48);             /*0x59d2dd*/
            if (IsPluralBlocker(e))
                std::sprintf(v64_base, "%s", name48);               /*0x59d30a*/
            else
                std::sprintf(v64_base, "%ss", name48);              /*0x59d2ed*/
        } else {
            std::sprintf(v64_base, "%s", name48);                   /*0x59d321*/
        }
        if (v67 != 2) return 1;                                     /*0x59d1a8*/
        // trailing icon/plural-blocker suffix on name48.           /*0x59d211*/
        unsigned char e2 = FieldEndChar(record, 48);
        const char* suffix = IsPluralBlocker(e2) ? "\0\0"           /*LABEL_27*/
                                                 : "\0\0";          /*LABEL_12*/
        char* tail = v64_base + std::strlen(v64_base);
        WideCopy(tail, suffix);
        return 1;
    }
    case 5: {                                                       /*0x59d336*/
        int base = B(9) ? 560 : 525;                                /*0x59d37d / 0x59d342*/
        const char* title = HResolve(B(358) + base, v61);           /*0x59d35b*/
        std::sprintf(v64_base, "%s", title);                        /*0x59d36a*/
        return 1;
    }
    case 6: {                                                       /*0x59d387*/
        if (B(64))
            WideCopy(v64_base, name64);                             /*0x59d394..0x59d3aa*/
        else
            WideCopy(v64_base, name48);                             /*0x59d3bf..0x59d3d5*/
        return 1;
    }
    case 7: {                                                       /*0x59d3f5*/
        if (v67 == 2) {
            unsigned char e = FieldEndChar(record, 64);             /*0x59d4aa*/
            if (IsPluralBlocker(e)) {
                if (B(64))
                    std::sprintf(v64_base, "%s$A%s", name48, name64); /*0x59d459*/
                else
                    std::sprintf(v64_base, "%s", name48);           /*0x59d4f7*/
            } else {
                if (B(64))
                    std::sprintf(v64_base, "%s$A%ss", name48, name64); /*0x59d4bd*/
                else
                    std::sprintf(v64_base, "%ss", name48);          /*0x59d4da*/
            }
        } else {
            if (B(64))
                std::sprintf(v64_base, "%s$A%s", name48, name64);   /*0x59d40f*/
            else
                std::sprintf(v64_base, "%s", name48);               /*0x59d514*/
        }
        return 1;
    }
    case 8: {                                                       /*0x59d52a*/
        if (B(13)) {
            int base = B(9) ? 279 : 272;                            /*0x59d69a / 0x59d545*/
            const char* t = HResolve(B(13) + base, v62);            /*0x59d566*/
            WideCopy(v62, t);                                       /*0x59d569..0x59d57f*/
        }
        if (B(358)) {
            int base = B(9) ? 560 : 525;                            /*0x59d6a4 / 0x59d5a3*/
            const char* t = HResolve(B(358) + base, v63);           /*0x59d5c7*/
            WideCopy(v63, t);                                       /*0x59d5ca..0x59d5e0*/
        }
        if (v67 == 2) {                                             /*0x59d5e7*/
            unsigned char e = FieldEndChar(record, 48);             /*0x59d64d*/
            if (IsPluralBlocker(e))
                std::sprintf(v61, "%s", name24);                    /*0x59d6ba*/
            else
                std::sprintf(v61, "%ss", name24);                   /*0x59d65c*/
        }
        if (B(358))                                                 /*0x59d667*/
            std::sprintf(v64_base, "%s$A%s", v63, v61);             /*0x59d687*/
        else
            std::sprintf(v64_base, "%s$A%s", v62, v61);             /*0x59d6d3*/
        return 1;
    }
    default:                                                        /*0x59cdbf default*/
        return 1;
    }
}

} // namespace guild::play
