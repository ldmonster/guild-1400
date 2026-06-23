#pragma once
#include "guild/common/types.h"

// gamelogic_recon5_certificate — VIBE_Meister_PopulateMasterCertificateFields
// (namespace guild::sim).
//
//   0x558c34 VIBE_Meister_PopulateMasterCertificateFields
//            (__userpurge al= a1@eax form, a2@edx outIds, a3@ebx, a4 personPtr,
//             a5 textArg, a6 flagBits, a7..a9 extra)
//            Populates a master-craftsman certificate form: selects window 1 and
//            stamps two rich strings, selects window 2 and stamps the name field,
//            then conditionally (when the person's building category byte != 5)
//            stamps four privilege rows (each gated by a bit of the a6 flag mask),
//            and finally a fee field. Each row resolves a child object id (stored
//            into the a2 id array) and pushes its value/text.
//
// The form/text/object subsystem (Form_SelectWindow, Text_RenderRichString,
// Form_GetChildObjectId, Object_SetValueOrText) and the person record table are
// live leaves -> installable hooks struct with inert defaults. The window
// sequence, the per-row string ids, the a6 bitmask decode (bit0..bit3 -> the
// "+5472/+5476/+5479/+5482" privilege-state offsets), the category-5 skip, and
// the back-write of child ids into a2[0..3] ARE reconstructed 1:1.

namespace guild::sim {

struct Recon5CertHooks {
    // VIBE_Form_SelectWindow(form, windowIndex).
    void (*selectWindow)(i32 form, int windowIndex) = nullptr;
    // VIBE_Text_RenderRichString(stringId, arg1, arg2, arg3) -> opaque text handle
    // used to resolve the child object id. We pass the four (id, 1210, textArg,
    // stateOffset) args; rows that take fewer args pass 0.
    i32  (*renderRichString)(u32 stringId, i32 a1, i32 a2, i32 a3) = nullptr;
    // VIBE_Form_GetChildObjectId(form, ?, textHandle) -> child object id.
    i32  (*getChildObjectId)(i32 form, i32 textHandle) = nullptr;
    // VIBE_Object_SetValueOrText(objId, value, arg, ...).
    void (*setValueOrText)(i32 objId, i32 value, i32 arg) = nullptr;
    // *(589 * (*personPtr) + dword_13CE294): the person's building/category byte.
    // Returns 5 to take the "skip privilege rows" branch.
    u8   (*personCategoryByte)(const void* personPtr) = nullptr;
};

void SetRecon5CertHooks(const Recon5CertHooks* h);
const Recon5CertHooks& GetRecon5CertHooks();

// Output id array (a2): [0]=fee field, [1..3]=privilege row child ids when the
// category-5 branch was NOT taken (initialized to -1 by the original). We model
// it as the 4-int block the original writes ( *a2, then v15[1..3] via the first
// row's id pointer; the originals interleave but the observable result is the
// fee id at a2[0] and the privilege ids at the row-id pointer block).
struct CertFieldIds {
    i32 ids[4] = { 0, -1, -1, -1 }; // [0] fee, [1..3] privilege rows
};

// gilde.exe 0x558c34 — VIBE_Meister_PopulateMasterCertificateFields
//   form       (a1) form/window handle
//   out        (a2) field id block to populate
//   personPtr  (a4) person record pointer (category byte read at +0 * 589 stride)
//   textArg    (a5) text render argument threaded into the rich-string calls
//   flagBits   (a6) privilege-state bitmask (bit0..bit3)
// Returns the original's al (last SetValueOrText result, here 0/1 success).
int Meister_PopulateMasterCertificateFields(i32 form, CertFieldIds& out,
                                            const void* personPtr, i32 textArg,
                                            u8 flagBits);

} // namespace guild::sim
