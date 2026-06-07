#include "world/office_law3.h"

#include <cstring>

// Faithful 1:1 ports — see office_law3.h for the per-function mapping and the
// recovered tables/constants. Engine/GUI/sim leaves we do not own are reached
// through installable hooks whose default implementations (below) are inert.

namespace guild::world {

// ===========================================================================
// 0x47de78 — VIBE_Office_InitTable.
// ===========================================================================
// byte_B5985x assignment block, mapped to record-index order (type byte at +8).
const u8 kOfficeTypeSeed[kOfficeInitRecordCount] = {
    1, 1, 2, 3, 4, 4, 5, 6, 7, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
    20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34,
};

int OfficeInitHolderTable(OfficeInitRecord* table) {
    // memset the whole 888-byte (37 * 24) table to 0 (the original's unrolled
    // memset of byte_B59848). Done structurally as a record clear.
    std::memset(table, 0,
                static_cast<std::size_t>(kOfficeInitRecordCount) *
                    kOfficeInitRecordStride);

    // do { record.holder = index; record.city = -1; record.secondary = -1; }
    // while (index < 37). v4 advances by 6 dwords (== 24 bytes == one record).
    for (int v3 = 0; v3 < kOfficeInitRecordCount; ++v3) {
        table[v3].holder    = static_cast<u8>(v3); // byte_B59830[v4*4] = v3
        table[v3].city      = -1;                  // dword_B59834[v4] = -1
        table[v3].secondary = -1;                  // dword_B59844[v4] = -1
    }

    // Stamp the office-type seed into each record's type byte (byte_B598xx = k).
    for (int r = 0; r < kOfficeInitRecordCount; ++r)
        table[r].type = kOfficeTypeSeed[r];

    return 7709; // the original's tail `return 7709;`
}

// ===========================================================================
// 0x57c184 — VIBE_Office_FindRoleTemplate.
// ===========================================================================
const i32 kRoleTableA[kRoleTemplateSlots] = {
    33, 32, 31, 46, 47, 48, 49, 50, 51, 64, 65, 66, 67, 68, 69, 58, 59, 60, 61,
    62, 63, 70, 71, 72, 73, 74, 75, 13, 14, 15, 16, 17, 18, 34, 35, 36, 37, 38,
    39, 19, 20, 21, 22, 23, 24, 1, 2, 3, 4, 5, 6, 40, 41, 42, 43, 44, 45, 52, 53,
    54, 55, 56, 57, 25, 26, 27, 28, 29, 30, 7, 8, 9, 10, 11, 12, 0,
};
const i32 kRoleTableB[kRoleTemplateSlots] = {
    33, 32, 31, 46, 47, 48, 49, 50, 51, 64, 65, 66, 67, 68, 69, 58, 59, 60, 61,
    62, 63, 70, 71, 72, 73, 74, 75, 34, 35, 36, 37, 38, 39, 19, 20, 21, 22, 23,
    24, 1, 2, 3, 4, 5, 6, 40, 41, 42, 43, 44, 45, 52, 53, 54, 55, 56, 57, 25, 26,
    27, 28, 29, 30, 7, 8, 9, 10, 11, 12, 0, 0, 0, 0, 0, 0, 0,
};

int OfficeFindRoleTemplate(u8 which, u8 roleId) {
    // if ( !a2 || a2 >= 76 ) return 0;  (a2 == roleId)
    if (roleId == 0 || roleId >= 76)
        return kRoleNotFound;

    const i32* table;
    if (which == 0)
        table = kRoleTableA;        // v4 = &unk_63E338
    else if (which == 1)
        table = kRoleTableB;        // v4 = &unk_63EF18
    else
        return kRoleNotFound;       // if ( a1 != 1 ) return 0;

    // for ( result = v4; v3 < 76 && *result; result += 10 ) — *result is the id
    // dword; scan stops at the zero terminator or after 76 records.
    int v3 = 0;
    for (; v3 < kRoleTemplateSlots && table[v3] != 0; ++v3) {
        if (roleId == static_cast<u8>(table[v3])) // a2 == *result
            return v3;
    }
    // return &v4[10*v3]; — the end-of-scan record index (terminator slot).
    return v3;
}

// ===========================================================================
// 0x562c9c — VIBE_Office_AwaitPromoteResult.
// ===========================================================================
namespace {
i32  DefTryPromote(i32, i32, i32, void*) { return -1; }
u8   DefPersonType(i32, void*)           { return 0; }
void DefRefresh(void*)                   {}
int  DefPacketStatus(i32, void*)         { return 1; } // non-zero: never spins

OfficeFlowHooks g_flow = {&DefTryPromote, &DefPersonType, &DefRefresh,
                          &DefPacketStatus, nullptr};
} // namespace

void OfficeSetFlowHooks(const OfficeFlowHooks& h) {
    g_flow.tryPromote   = h.tryPromote   ? h.tryPromote   : &DefTryPromote;
    g_flow.personType   = h.personType   ? h.personType   : &DefPersonType;
    g_flow.refresh      = h.refresh      ? h.refresh      : &DefRefresh;
    g_flow.packetStatus = h.packetStatus ? h.packetStatus : &DefPacketStatus;
    g_flow.ctx          = h.ctx;
}
void OfficeFlowHooksReset() {
    g_flow = {&DefTryPromote, &DefPersonType, &DefRefresh, &DefPacketStatus,
              nullptr};
}

bool OfficeAwaitPromoteResult(i32 person, i32 fromCity, i32 toCity) {
    // v3 = VIBE_Office_TryPromoteCharacter(a1, a2, a3);
    i32 cmd = g_flow.tryPromote(person, fromCity, toCity, g_flow.ctx);
    if (cmd == -1)                          // if ( v3 == -1 ) return 0;
        return false;
    // if ( *(_BYTE *)(v4 + 2) != 6 ) return 1;  (the promoted person's type byte)
    if (g_flow.personType(person, g_flow.ctx) != 6)
        return true;
    // while ( !GetPacketStatusById(v3) ) RefreshGuildState();
    while (g_flow.packetStatus(cmd, g_flow.ctx) == 0)
        g_flow.refresh(g_flow.ctx);
    // return GetPacketStatusById(v3) != 2;
    return g_flow.packetStatus(cmd, g_flow.ctx) != 2;
}

// ===========================================================================
// Gesetz violation-description text-id selectors.
// ===========================================================================
namespace {
int  DefPortrait(i32, void*) { return kNoPortrait; }
void DefDescRender(char*, int, void*) {}
PortraitIdFn g_portrait = &DefPortrait;
DescRenderFn g_descRender = &DefDescRender;
void* g_descCtx = nullptr;

// The "Hm, das ist doch nicht verboten" fallback literal (aHmDasIstDochNi).
const char kNotForbidden[] = "Hm, das ist doch nicht verboten";
void CopyFallback(char* dest) {
    if (dest) std::strcpy(dest, kNotForbidden);
}
int ResolvePortrait(i32 id) {
    int p = g_portrait(id, g_descCtx);     // RecordById ? *RecordById : -2
    return p;
}
} // namespace

void GesetzSetDescHooks(PortraitIdFn portrait, DescRenderFn render, void* ctx) {
    g_portrait   = portrait ? portrait : &DefPortrait;
    g_descRender = render   ? render   : &DefDescRender;
    g_descCtx    = ctx;
}
void GesetzDescHooksReset() {
    g_portrait = &DefPortrait;
    g_descRender = &DefDescRender;
    g_descCtx = nullptr;
}

GesetzDescResult GesetzFormatDescriptionLow(char* dest, u8 op, int value,
                                            i32 subjectId, int subValue,
                                            int lowFlag) {
    GesetzDescResult r;
    int flag = (lowFlag <= 1) ? 1 : 0;     // v14 = a12 <= 1
    int portrait = ResolvePortrait(subjectId);
    (void)portrait;                        // resolved before the branch in the orig

    if (op == 2) {                         // (_BYTE)a8 == 2
        int baseId = flag + 4158;          // v17 = v14 + 4158
        // v18 = (a3 >= 117 ? *(&a8+1)+4143 : *(&a8+1)+4141)
        int arg = (value >= 117) ? (subValue + 4143) : (subValue + 4141);
        (void)arg;                         // 2nd render arg; deterministic id is baseId
        g_descRender(dest, baseId, g_descCtx);
        r.valid = true;
        r.textId = baseId;
        return r;
    }
    if (op == 3) {                         // (unsigned __int8)a8 == 3 path
        int baseId = flag + 4163;
        g_descRender(dest, baseId, g_descCtx);
        r.valid = true;
        r.textId = baseId;
        return r;
    }
    if (op == 4) {                         // (_BYTE)a8 == 4
        int baseId = flag + 4168;
        g_descRender(dest, baseId, g_descCtx);
        r.valid = true;
        r.textId = baseId;
        return r;
    }
    CopyFallback(dest);                    // fall through: copy literal, return 0
    r.usedFallback = true;
    return r;
}

GesetzDescResult GesetzFormatDescriptionMid(char* dest, u8 op, int value,
                                            i32 subjectId, int lowFlag) {
    (void)value;
    GesetzDescResult r;
    // The Mid leaf does NOT add the lowFlag offset (no v14 term); base is exact.
    (void)lowFlag;
    int portrait = ResolvePortrait(subjectId);
    (void)portrait;

    if (op == 13) {                        // a8 == 0xD
        int baseId = 4213;                 // v15 + 4213, v15 == 0 here
        g_descRender(dest, baseId, g_descCtx);
        r.valid = true;
        r.textId = baseId;
        return r;
    }
    if (op == 15) {                        // a8 == 15
        int baseId = 4223;
        g_descRender(dest, baseId, g_descCtx);
        r.valid = true;
        r.textId = baseId;
        return r;
    }
    CopyFallback(dest);
    r.usedFallback = true;
    return r;
}

GesetzDescResult GesetzFormatDescriptionHigh(char* dest, u8 op, int value,
                                             i32 subjectId, int lowFlag) {
    (void)value;
    GesetzDescResult r;
    int flag = (lowFlag <= 1) ? 1 : 0;     // v13 = a12 <= 1
    int portrait = ResolvePortrait(subjectId);
    (void)portrait;

    int baseId;
    switch (op) {                          // switch ( a8 ) cases 16..25
        case 16: baseId = 4228; break;
        case 17: baseId = 4233; break;
        case 18: baseId = 4238; break;
        case 19: baseId = 4243; break;
        case 20: baseId = 4248; break;
        case 21: baseId = 4253; break;
        case 22: baseId = 4258; break;
        case 23: baseId = 4263; break;
        case 24: baseId = 4268; break;
        case 25: baseId = 4273; break;
        default: return r;                 // default: return 0; (valid stays false)
    }
    baseId += flag;                        // v13 + base
    g_descRender(dest, baseId, g_descCtx);
    r.valid = true;                        // a message was selected/rendered
    r.textId = baseId;
    return r;                              // (the original returns 0 in EAX always)
}

GesetzDescResult GesetzFormatDescription(char* dest, u8 op, int value,
                                         i32 subjectId, int subValue,
                                         int lowFlag) {
    if (op < 8u)                           // a10 < 8u
        return GesetzFormatDescriptionLow(dest, op, value, subjectId, subValue,
                                          lowFlag);
    if (op < 16u)                          // a10 < 0x10u
        return GesetzFormatDescriptionMid(dest, op, value, subjectId, lowFlag);
    if (op >= 26u)                         // a10 >= 0x1Au -> return 0
        return GesetzDescResult{};
    return GesetzFormatDescriptionHigh(dest, op, value, subjectId, lowFlag);
}

// ===========================================================================
// Law-book modal dialog leaves.
// ===========================================================================
namespace {
int  DefMapTypeToState(u8, void*)                          { return 0; }
i32  DefRunPersonSelection(i32, const char*, i32, void*)   { return 0; }
int  DefShowModal(const char*, int, int, void*)            { return 0; }
GesetzUiHooks g_ui = {&DefMapTypeToState, &DefRunPersonSelection, &DefShowModal,
                      nullptr};
} // namespace

void GesetzSetUiHooks(const GesetzUiHooks& h) {
    g_ui.mapTypeToState     = h.mapTypeToState     ? h.mapTypeToState
                                                   : &DefMapTypeToState;
    g_ui.runPersonSelection = h.runPersonSelection ? h.runPersonSelection
                                                   : &DefRunPersonSelection;
    g_ui.showModal          = h.showModal          ? h.showModal : &DefShowModal;
    g_ui.ctx                = h.ctx;
}
void GesetzUiHooksReset() {
    g_ui = {&DefMapTypeToState, &DefRunPersonSelection, &DefShowModal, nullptr};
}

i32 GesetzOpenPersonSelectionIfValid(i32 subject, u8 buildingType,
                                     const char* caption, i32 arg) {
    if (subject == 0)                      // if ( !result ) return result; (0)
        return 0;
    // if ( VIBE_Building_MapTypeToState(*(_BYTE*)result, v7) ) return 0;
    if (g_ui.mapTypeToState(buildingType, g_ui.ctx))
        return 0;
    // else return VIBE_Gesetz_RunPersonSelectionWindow(v4, a2, v6, a3);
    return g_ui.runPersonSelection(subject, caption, arg, g_ui.ctx);
}

i32 GesetzShowApplicationErrorDialog(int errorTextId) {
    // v3 = GameTick_Finalize("Gesetze\\Antrag_Fehler"); ... RenderRichString(...)
    // the shown id is errorTextId, or the default literal when 0; the modal pumps
    // and returns Form_Destroy(v3). Collapsed into the showModal observable.
    int shown = (errorTextId != 0) ? errorTextId : kDefaultErrorTextId;
    return g_ui.showModal("Gesetze\\Antrag_Fehler", shown, 0, g_ui.ctx);
}

i32 GesetzShowLawBookInfoDialog(int recordBaseTextId) {
    // RenderRichString(a1+1, ...); RenderRichString(a1+2); modal pump; Form_Destroy.
    return g_ui.showModal("Gesetze\\Gesetzbuch_Info", recordBaseTextId + 1,
                          recordBaseTextId + 2, g_ui.ctx);
}

} // namespace guild::world
