#include "sim/gamelogic_recon5_certificate.h"

namespace guild::sim {

// ===========================================================================
// Hooks plumbing (inert defaults).
// ===========================================================================
namespace {

void DefSelectWindow(i32, int) {}
i32  DefRenderRichString(u32, i32, i32, i32) { return 0; }
i32  DefGetChildObjectId(i32, i32) { return 0; }
void DefSetValueOrText(i32, i32, i32) {}
u8   DefPersonCategoryByte(const void*) { return 0; }

const Recon5CertHooks kDefaults = {
    &DefSelectWindow, &DefRenderRichString, &DefGetChildObjectId,
    &DefSetValueOrText, &DefPersonCategoryByte,
};

Recon5CertHooks g_hooks = kDefaults;

} // namespace

void SetRecon5CertHooks(const Recon5CertHooks* h) {
    if (!h) { g_hooks = kDefaults; return; }
    g_hooks = *h;
    if (!g_hooks.selectWindow)       g_hooks.selectWindow       = kDefaults.selectWindow;
    if (!g_hooks.renderRichString)   g_hooks.renderRichString   = kDefaults.renderRichString;
    if (!g_hooks.getChildObjectId)   g_hooks.getChildObjectId   = kDefaults.getChildObjectId;
    if (!g_hooks.setValueOrText)     g_hooks.setValueOrText     = kDefaults.setValueOrText;
    if (!g_hooks.personCategoryByte) g_hooks.personCategoryByte = kDefaults.personCategoryByte;
}
const Recon5CertHooks& GetRecon5CertHooks() { return g_hooks; }

// ===========================================================================
// gilde.exe 0x558c34 — VIBE_Meister_PopulateMasterCertificateFields
//
// String ids (from the decompile): the "$C" rich token (aC_0) plus the field
// resource ids 0x1558, 0x155E (name), 0x1562 (fee), and the four privilege rows
// 0x1563 / 0x1566 / 0x1569 stamped with the per-bit state offsets
//   bit0 -> 5472 + (a6&1)        (row stored at out.ids[0]-block, the name field)
//   bit1 -> 5476 + ((a6&2)!=0)
//   bit2 -> 5479 + ((a6&4)!=0)
//   bit3 -> 5482 + ((a6&8)!=0)
// The privilege rows are skipped when the person's category byte == 5.
// ===========================================================================
int Meister_PopulateMasterCertificateFields(i32 form, CertFieldIds& out,
                                            const void* personPtr, i32 textArg,
                                            u8 flagBits) {
    // Window 1: two header rich strings ($C token + 0x1558).
    g_hooks.selectWindow(form, 1);
    g_hooks.renderRichString(/*aC_0*/ 0, 0, 0, 0);
    g_hooks.renderRichString(0x1558u, 0, 0, 0);

    // Window 2: the certificate name/title field, gated by bit0 of the flag mask.
    g_hooks.selectWindow(form, 2);
    g_hooks.renderRichString(/*aC_0*/ 0, 0, 0, 0);

    bool v43 = (flagBits & 1) != 0;                 // v43 = (a6 & 1) != 0
    i32 h0 = g_hooks.renderRichString(0x155Eu, 1210, textArg, (i32)(v43 ? 1 : 0) + 5472);
    i32 obj0 = g_hooks.getChildObjectId(form, h0);
    out.ids[0] = obj0;                              // *v14 = ChildObjectId
    g_hooks.setValueOrText(obj0, (i32)(v43 ? 1 : 0), 0);
    out.ids[1] = -1;                                // v15[1] = -1
    out.ids[2] = -1;                                // v15[2] = -1
    out.ids[3] = -1;                                // v15[3] = -1

    // Privilege rows — skipped when the person category byte == 5.
    if (g_hooks.personCategoryByte(personPtr) != 5) {
        bool v44 = (flagBits & 2) != 0;
        i32 h1 = g_hooks.renderRichString(0x1563u, 1210, textArg, (i32)(v44 ? 1 : 0) + 5476);
        i32 obj1 = g_hooks.getChildObjectId(form, h1);
        out.ids[1] = obj1;                          // *(v22+4) = id
        g_hooks.setValueOrText(obj1, (i32)(v44 ? 1 : 0), 0);

        bool v37 = (flagBits & 4) != 0;
        i32 h2 = g_hooks.renderRichString(0x1566u, 1210, textArg, (i32)(v37 ? 1 : 0) + 5479);
        i32 obj2 = g_hooks.getChildObjectId(form, h2);
        out.ids[2] = obj2;                          // *(v26+8) = id
        g_hooks.setValueOrText(obj2, (i32)(v37 ? 1 : 0), 0);

        bool v42 = (flagBits & 8) != 0;
        i32 h3 = g_hooks.renderRichString(0x1569u, 1210, textArg, (i32)(v42 ? 1 : 0) + 5482);
        i32 obj3 = g_hooks.getChildObjectId(form, h3);
        out.ids[3] = obj3;                          // *(v30+12) = id
        g_hooks.setValueOrText(obj3, (i32)(v42 ? 1 : 0), 0);
    }

    // Fee field (0x1562) — always stamped; SetValueOrText(*, 0x3E8 == 1000, 9000).
    i32 h4 = g_hooks.renderRichString(0x1562u, textArg, /*byte_6477A1*/ 0, 0);
    i32 feeObj = g_hooks.getChildObjectId(form, h4);
    // The original writes the fee id into *a2 (the same block we model as ids[0]
    // after the name field; the name field id is the primary returned id). We keep
    // the name id in ids[0] and record the fee object via SetValueOrText.
    g_hooks.setValueOrText(feeObj, 1000 /*0x3E8*/, 9000);
    return feeObj != 0 ? 1 : 0;                     // al == last SetValueOrText path
}

} // namespace guild::sim
