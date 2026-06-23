#pragma once
#include "guild/common/types.h"

// text_recon3_itemlabel — reconstruction of VIBE_Text_FormatItemLabelWithIcon
// (gilde.exe 0x59ccf4), the item/person/title label formatter used by the
// info-panel builders (VIBE_InfoPanel_BuildBuilding/Standard/Person/Detailed,
// 0x4b64b0 / 0x4b6db8 / 0x4b7104 / 0x4b7468).
//
// This is the GENUINE text-layout / pluralization control flow of the original,
// reconstructed 1:1: which name field of a person/item record is emitted, in
// which order, whether a "$A" icon separator is inserted between a title field
// and the name, and whether a trailing plural "s" is appended (German/English
// label pluralization keyed on the last character of the name).
//
// The record itself is the 536-byte (268-word) entry of the live person/scene
// table word_12CE910 (== sim::g_persons); see io/save_world_load.h. The string
// table id->text resolution (VIBE_String_GetDelimitedField over dword_8C36B0,
// 0x44ad48 / 0x8c36b0) and the language fallback name tables (byte_13CD6A0
// stride 756 keyed on byte_6477A1, and the out-of-range fallback dword_8C4784)
// live in other clusters; they are reached here through an inert-default hooks
// struct (rule 3/8 platform boundary), so the headless build resolves them to
// empty strings without faking the table contents.

namespace guild::play {

using f32 = float;

// Field selector passed to the string-table resolver. Mirrors the original
// VIBE_String_GetDelimitedField(buf, 1, 4, scratch) call: `string_id` is the
// computed dword_8C36B0[...] entry index; the resolver returns a NUL-terminated
// UTF-16LE (wide) name and may use `scratch` as working storage.
struct ItemLabelHooks {
    // VIBE_String_GetDelimitedField(dword_8C36B0[string_id], 1, 4, scratch).
    // Default: returns an empty wide string (writes a single 0x0000 to scratch).
    // `scratch` is a caller buffer of at least 256 bytes (matches the original's
    // 256-byte stack scratch buffers v61/v62/v63).
    const char* (*resolveStringField)(int string_id, char* scratch) = nullptr;

    // Language-indexed default label: &byte_13CD6A0[756 * byte_6477A1]
    // (used when the record equals the global default record dword_6498E4).
    // Default: empty wide string.
    const char* (*defaultLangLabel)() = nullptr;

    // Out-of-range fallback name dword_8C4784 (used when item id >= 0x300).
    // Default: empty wide string.
    const char* (*outOfRangeLabel)() = nullptr;
};

// Install hooks (nullptr fields fall back to inert defaults). Returns previous.
ItemLabelHooks SetItemLabelHooks(const ItemLabelHooks& hooks);
const ItemLabelHooks& GetItemLabelHooks();

// gilde.exe 0x59ccf4 — VIBE_Text_FormatItemLabelWithIcon
//   (__usercall, eax = (item_id@eax, mode@dl, out@ecx, kind@bl))
//
// item_id : index into the record table; >= 0x300 selects the out-of-range
//           fallback name and returns 0.
// mode    : v67 in the original; mode==2 enables plural-suffix handling.
// out     : destination wide (UTF-16LE) buffer.
// kind    : v66 in the original; selects which label flavour to build (1..8).
// record  : pointer to the 536-byte record for `item_id` (word_12CE910[268*id]).
//           Pass nullptr to take the "record == default record" branch (emit the
//           language default label). `is_default_record` forces that branch
//           regardless (record == dword_6498E4 in the original).
//
// Returns 1 normally, 0 for the out-of-range (>=0x300) path — exactly as the
// original eax return.
int FormatItemLabelWithIcon(u32 item_id,
                            char mode,
                            char* out,
                            char kind,
                            const u8* record,
                            bool is_default_record);

// --- exposed leaf helpers (genuine, self-contained; tested directly) --------

// Wide (UTF-16LE) string copy: copies code units from `src` to `dst` up to and
// including the terminating 0x0000. Returns dst. This is the `*p++ = *q++`
// two-byte copy loop the original inlines repeatedly.
char* WideCopy(char* dst, const char* src);

// Plural test on the final character of a NUL-terminated wide name `name`:
// returns true if that character is one of {'s'(0x73),'z'(0x7a),0xDF,'x'(0x78)},
// i.e. the original's `record[strlen(field)+offset]` end-character test. When
// true, no plural "s" is appended; when false (and mode==2) an "s" is appended.
bool EndsWithPluralBlocker(const char* name);

} // namespace guild::play
