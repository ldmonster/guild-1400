// ===========================================================================
// session_select.cpp — see session_select.h for the recovered pipeline.
//
// Provenance:
//   0x4b950c  VIBE_Object_ResolveQuickJumpContact   (the set-selection commit)
//   0x4b94d8  VIBE_Selection_ClearAll               (full worker deselect)
//   0x4bc280  VIBE_Hud_HandleMouseClick (tail 0x4bc45a..0x4bc51b — status latch)
// Reused (NOT redefined):
//   0x4b9444  VIBE_Selection_Reset      -> play::Selection_Reset (input_recon_select)
//   0x4bcc4c  VIBE_StatusText_ResetEntries -> gui::ResetStatusText (gui/hud)
//   0x5d3f10  VIBE_Util_StrCmp          -> util::ReconStrCmp
//   0x59ccf4  VIBE_Text_FormatItemLabelWithIcon -> play::FormatItemLabelWithIcon
// ===========================================================================
#include "play/session_select.h"

#include "gui/hud.h"                      // gui::ResetStatusText (@0x4bcc4c recon)
#include "gui/text/textdb.h"              // gui::text::TextDb (dword_8C36B0 model)
#include "play/text_recon3_itemlabel.h"   // FormatItemLabelWithIcon (@0x59ccf4 recon)
#include "util/util_recon.h"              // util::ReconStrCmp (@0x5d3f10 recon)

#include <cstdio>
#include <cstring>

namespace guild::play {

// ---------------------------------------------------------------------------
// State (one definition each; grep'd for ODR before defining).
// ---------------------------------------------------------------------------
SelectionContactLatch  g_selectionContact;
SelectionAnchorRecords g_selectionAnchorRecords;
SelectionCommitState   g_selectionCommit;
SelectionOwnerRecords  g_selectionOwners;
QuickJumpRequest       g_quickJump;
SelectGateState        g_selectGate;
SelectionStatusLatch   g_selectionStatus;
char                   g_quickJumpError[128] = {0};

namespace {

SelectCommitHooks g_commitHooks;
StatusLatchHooks  g_statusHooks;

// Default empty-click deselect: the real VIBE_Selection_Reset (clears the i32
// anchor view) + the pointer-width dual view of the same four stores
// (0x4b944f/0x4b9454/0x4b9459/0x4b945e zero dword_11BC274/11BC278/11BC260/631740).
void DefaultSelectionReset() {
    Selection_Reset(0);
    g_selectionAnchorRecords = SelectionAnchorRecords{};
}

} // namespace

void Selection_SetCommitHooks(const SelectCommitHooks& hooks) {
    g_commitHooks = hooks;
}

void Selection_SetStatusLatchHooks(const StatusLatchHooks& hooks) {
    g_statusHooks = hooks;
}

// ===========================================================================
// gilde.exe 0x4b94d8 — VIBE_Selection_ClearAll
//
//   int VIBE_Selection_ClearAll() {
//     result = 0;
//     dword_11BC270 = 0;                       /*0x4b94de*/
//     dword_631740  = 0;                       /*0x4b94e4*/
//     do { result += 536; byte_12CE880[result] = 0; } while (result != 411648);
//     dword_6317B0 = 0;                        /*0x4b9500*/
//     return result;                           /*0x4b9507*/
//   }
//
// The sweep clears the first byte of person records 1..768 (the counter is
// pre-incremented, exactly as compiled); the table itself is the sim cluster's,
// so the per-record store goes through clearWorkerSelectionMarks (which models
// the whole do-while).  The 11BC270/631740 stores hit our state directly
// (pointer view for 631740 + the i32 anchor view kept in lockstep at zero).
// ===========================================================================
i32 Selection_ClearAll() {
    g_selectionCommit.g11BC270 = nullptr;                       /*0x4b94de*/
    g_selectionAnchorRecords.a631740 = nullptr;                 /*0x4b94e4*/
    g_selectionAnchors.g631740 = 0;                             // i32 dual view
    if (g_commitHooks.clearWorkerSelectionMarks)                /*0x4b94ea..0x4b94fc*/
        g_commitHooks.clearWorkerSelectionMarks();
    g_selectionCommit.g6317B0 = 0;                              /*0x4b9500*/
    return 411648;                                              /*0x4b9507*/
}

// ===========================================================================
// gilde.exe 0x4b950c — VIBE_Object_ResolveQuickJumpContact (the set-selection).
// Control flow translated 1:1 from the Hex-Rays decompile; every store cites
// its original address.
// ===========================================================================
void Selection_CommitContact() {
    bool v0 = false;            // forced-commit flag (quickjump resolved)
    bool haveContactType = false;
    u8   contactType = 0;       // v1 -> *v1 (the dereferenced type byte)
    bool v14 = false;           // door-like gate

    g_selectionCommit.g11BC2F4 = 0;                              /*0x4b952b*/
    g_selectionCommit.g11BC2F0 = nullptr;                        /*0x4b9531*/
    g_selectionCommit.g631720  = nullptr;                        /*0x4b9537*/
    g_selectionCommit.g11BC2F8 = nullptr;                        /*0x4b953d*/
    g_selectionCommit.g631738  = nullptr;                        /*0x4b9543*/

    // Drop a stale worker selection: if (dword_11BC270 && !*(BYTE*)(+392)) ...
    if (g_selectionCommit.g11BC270 && !g_selectionCommit.g11BC270[392])  /*0x4b954d*/
        g_selectionCommit.g11BC270 = nullptr;                    /*0x4b9556*/

    // ---- pending QuickJump request -------------------------------------- //
    if (g_quickJump.g11BC27C) {                                  /*0x4b955f*/
        u8* v2 = g_selectionOwners.g631744 ? g_selectionOwners.g631744 /*0x4b9570*/
                                           : g_selectionOwnerB;        /*0x4b9902*/
        if (v2 == g_quickJump.g11BC280 &&
            g_selectionOwners.g63174C == g_quickJump.g11BC284) { /*0x4b958a*/
            if (g_quickJump.name[0]) {                           /*0x4b9598*/
                u8* v3 = g_commitHooks.objectFindByHandle
                             ? g_commitHooks.objectFindByHandle(g_quickJump.name)
                             : nullptr;                          /*0x4b95a4*/
                if (v3) {
                    v0 = true;                                   /*0x4b95b1*/
                    g_selectionContact.g631724 = v3;             /*0x4b95b6*/
                } else {
                    std::snprintf(g_quickJumpError, sizeof g_quickJumpError,
                                  "sv_HandleObjects(): Could not find "
                                  "QuickJump.ContactName: %s",
                                  g_quickJump.name);             /*0x4b95bd (v13)*/
                }
            }
            g_quickJump.g11BC27C = 0;                            /*0x4b95bd*/
        }
        if (v2 == g_quickJump.g11BC280 &&                        /*0x4b95cd*/
            (!g_quickJump.g11BC284 ||
             *reinterpret_cast<u16*>(g_quickJump.g11BC284) == 253)) {
            if (g_quickJump.name[0]) {                           /*0x4b95e4*/
                u8* v4 = g_commitHooks.objectFindByHandle
                             ? g_commitHooks.objectFindByHandle(g_quickJump.name)
                             : nullptr;                          /*0x4b95f0*/
                if (v4) {
                    v0 = true;                                   /*0x4b95fd*/
                    g_selectionContact.g631724 = v4;             /*0x4b9602*/
                } else {
                    std::snprintf(g_quickJumpError, sizeof g_quickJumpError,
                                  "sv_HandleObjects(): Could not find "
                                  "QuickJump.ContactName: %s",
                                  g_quickJump.name);             /*0x4b9609 (v12)*/
                }
            }
            g_quickJump.g11BC27C = 0;                            /*0x4b9609*/
        }
    }

    // ---- the input gate -------------------------------------------------- //
    const SelectGateState& gt = g_selectGate;
    const bool gate =                                            /*0x4b995a*/
        (gt.g67221C && !gt.g62D4E8 &&
         (gt.cursorX16 >> 16) > gt.g63CC4C &&
         (gt.cursorX16 >> 16) < gt.g63CC54 &&
         (gt.cursorY16 >> 16) > gt.g63CC50 &&
         (gt.cursorY16 >> 16) < gt.g63CC58 &&
         gt.g75BF08 == -1 && gt.g62D31C == -1) ||
        v0;
    if (!gate)
        return;

    if (!g_selectionContact.g631724) {                           /*0x4b968f*/
        // empty click: the REAL deselect (unless build-mode byte_6317B4).
        if (!g_selectionAnchors.g6317B4) {                       /*0x4b9993*/
            if (g_commitHooks.selectionReset) g_commitHooks.selectionReset();
            else DefaultSelectionReset();                        /*0x4b9999*/
        }
        return;
    }

    // ---- commit the hover latch ------------------------------------------ //
    g_selectionCommit.g11BC2F4 = g_selectionContact.g631730;     /*0x4b969a*/
    g_selectionCommit.g11BC2F0 = g_selectionContact.g63172C;     /*0x4b96a4*/
    g_selectionCommit.g631720  = g_selectionContact.g631724;     /*0x4b96ae*/
    g_selectionCommit.g11BC2F8 = g_selectionContact.g631734;     /*0x4b96b4*/
    g_selectionCommit.g631738  = g_selectionContact.g63173C;     /*0x4b96c4*/

    if (g_selectionContact.g63172C) {                            /*0x4b96cb*/
        // v1 = dword_13CE27C + 65 * *(i16*)dword_63172C        /*0x4b96dd*/
        haveContactType = true;
        contactType = g_commitHooks.objectTypeByte
            ? g_commitHooks.objectTypeByte(
                  *reinterpret_cast<i16*>(g_selectionContact.g63172C))
            : 0;
    }

    // v14 = door-like: !StrCmp("tp_TUER", rec) || (*v1 == 6) || dword_631738
    if (!util::ReconStrCmp("tp_TUER",
            reinterpret_cast<const char*>(g_selectionContact.g631724)) /*0x4b96e9*/
        || (haveContactType && contactType == 6)                 /*0x4b9981*/
        || g_selectionCommit.g631738)
        v14 = true;                                              /*0x4b96f6*/

    // highlight pulse: ((rec+529) & 1) && !v14
    if ((g_selectionContact.g631724[529] & 1) != 0 && !v14) {    /*0x4b9717*/
        if (g_commitHooks.applyBoneTransform)
            g_commitHooks.applyBoneTransform(g_selectionContact.g631724); /*0x4b971f*/
        g_selectionCommit.g631728 = g_selectionContact.g631724;  /*0x4b9729*/
        if (g_selectionContact.g631724) {                        /*0x4b9730*/
            // (*(rec+468)+264)(0, 2) — handler block, hook boundary
            if (g_commitHooks.invokeHighlightHandler)            /*0x4b9754*/
                g_commitHooks.invokeHighlightHandler(g_selectionContact.g631724);
        }
        g_selectionCommit.g63161C = g_selectionCommit.g631610;   /*0x4b975f*/
    }

    g_selectionCommit.g631E50 = g_selectionContact.g631724;      /*0x4b976f*/

    // ---- worker (person) selection ---------------------------------------- //
    if (g_selectionContact.g631734 && g_selectionContact.g631734[8]) { /*0x4b977c*/
        const u16 flags = g_commitHooks.computeSelectionFlags
            ? g_commitHooks.computeSelectionFlags(
                  g_selectionStatus.w63CC5C, nullptr,
                  g_selectionContact.g631734, nullptr)
            : 0;                                                 /*0x4b979c*/
        const u8 v6 = static_cast<u8>(flags >> 8);
        g_selectionCommit.g11BC270 = nullptr;                    /*0x4b979f*/
        g_selectionAnchorRecords.a631740 = nullptr;              /*0x4b97a5*/
        g_selectionAnchors.g631740 = 0;                          // i32 dual view
        if (g_commitHooks.clearWorkerSelectionMarks)             /*0x4b97ab..0x4b97ad*/
            g_commitHooks.clearWorkerSelectionMarks();
        g_selectionCommit.g6317B0 = 0;                           /*0x4b97c3*/
        if ((v6 & 8) != 0) {                                     /*0x4b97cc*/
            const u16 wid =
                *reinterpret_cast<u16*>(g_selectionContact.g631734); /*0x4b97ce*/
            if (g_commitHooks.markWorkerSelected)                /*0x4b97e9*/
                g_commitHooks.markWorkerSelected(wid);
            g_selectionCommit.g62D098 = g_commitHooks.workerMeshPtr
                ? g_commitHooks.workerMeshPtr(wid) : nullptr;    /*0x4b9806*/
            g_selectionCommit.g6317B0 = 1;                       /*0x4b980b*/
            g_selectionCommit.g11BC270 = g_selectionContact.g631734; /*0x4b9817*/
            if (g_commitHooks.voiceWorkerClickComment)           /*0x4b981d*/
                g_commitHooks.voiceWorkerClickComment(
                    g_selectionContact.g631734, wid);
        }
        // unconditional second store, exactly as compiled
        g_selectionCommit.g62D098 = g_commitHooks.workerMeshPtr  /*0x4b983f*/
            ? g_commitHooks.workerMeshPtr(
                  *reinterpret_cast<u16*>(g_selectionContact.g631734))
            : nullptr;
    }

    // ---- anchor stores ----------------------------------------------------- //
    if (!g_selectionCommit.g6317B0 && !g_selectionAnchors.g6317B4 /*0x4b9866*/
        && g_selectionContact.g631730 && !g_selectionAnchorRecords.a11BC274)
        g_selectionAnchorRecords.a631740 = g_selectionContact.g631724; /*0x4b986d*/

    g_selectionAnchorRecords.a11BC278 = g_selectionContact.g631730; /*0x4b9877*/
    g_selectionAnchorRecords.a11BC274 = g_selectionContact.g63172C; /*0x4b9887*/
    if (g_selectionContact.g631730)                              /*0x4b988e*/
        g_selectionAnchorRecords.a631740 = g_selectionContact.g631724; /*0x4b9895*/

    if (g_selectionAnchorRecords.a11BC274) {                     /*0x4b98a2*/
        // v10 = *(BYTE*)(dword_13CE27C + 65 * *(i16*)dword_11BC274)
        const u8 v10 = g_commitHooks.objectTypeByte              /*0x4b98bc*/
            ? g_commitHooks.objectTypeByte(
                  *reinterpret_cast<i16*>(g_selectionAnchorRecords.a11BC274))
            : 0;
        g_selectionAnchorRecords.a631740 = g_selectionContact.g631724; /*0x4b98be*/
        if (v10 == 29) {                                         /*0x4b98c7*/
            g_selectionCommit.g11BC270 = nullptr;                /*0x4b98cd*/
            g_selectionAnchorRecords.a631740 = nullptr;          /*0x4b98d3*/
            g_selectionAnchors.g631740 = 0;                      // i32 dual view
            if (g_commitHooks.clearWorkerSelectionMarks)         /*0x4b98d9..0x4b98eb*/
                g_commitHooks.clearWorkerSelectionMarks();
            g_selectionCommit.g6317B0 = 0;                       /*0x4b98ef*/
        }
    }
}

// ===========================================================================
// gilde.exe 0x4bc280 tail (0x4bc45a..0x4bc51b) — the status-text latch.
// ===========================================================================
bool Selection_UpdateStatusTextLatch() {
    u8* v5 = nullptr;                                            /*0x4bc292*/
    if (g_selectionOwners.g631744)                               /*0x4bc45a*/
        v5 = g_selectionOwners.g631744;                          /*0x4bc460*/
    else if (g_selectionOwnerB)                                  /*0x4bcac2*/
        v5 = g_selectionOwnerB;                                  /*0x4bcac8*/

    if (v5 != g_selectionStatus.g631E54                          /*0x4bc4d2*/
        || g_selectionStatus.g631E58 != g_selectionOwners.g63174C
        || g_selectionStatus.g631754) {
        g_selectionStatus.w631758 = static_cast<i16>(            /*0x4bc4f1*/
            g_statusHooks.computeSelectionFlags
                ? g_statusHooks.computeSelectionFlags(
                      g_selectionStatus.w63CC5C, v5, nullptr,
                      g_selectionOwners.g63174C)
                : 0);
        if (g_statusHooks.statusTextReset)                       /*0x4bc4f7*/
            g_statusHooks.statusTextReset();
        else
            gui::ResetStatusText();
        g_selectionStatus.g631E54 = v5;                          /*0x4bc501*/
        g_selectionStatus.g631754 = 0;                           /*0x4bc507*/
        g_selectionStatus.g631E58 = g_selectionOwners.g63174C;   /*0x4bc50d*/
        return true;
    }
    return false;
}

// ===========================================================================
// SessionSelect
// ===========================================================================
SessionSelect::SessionSelect(const gui::text::TextDb* textDb) : textDb_(textDb) {
    resetLatch();
}

void SessionSelect::resetLatch() {
    g_selectionContact = SelectionContactLatch{};
}

void SessionSelect::installHooks() {
    SelectCommitHooks h;
    // The worker-table ops over this session's mark array (the byte_12CE880 /
    // byte_12CEA98 view; both sweeps clear the same per-record mark byte).
    h.clearWorkerSelectionMarks = [this]() {
        std::memset(workerMarks_, 0, sizeof workerMarks_);
    };
    h.markWorkerSelected = [this](u16 id) {
        workerMarks_[id % kWorkerCount] = 1;
    };
    h.workerMeshPtr = [this](u16 id) -> u8* {
        // dword_12CEA94[134*id] is the worker's mesh record pointer; the session
        // has no live mesh table (render cluster), so the hook reports the
        // worker's stable per-id slot so dword_62D098 still carries the
        // selected-worker identity. The mesh record itself is a named gap.
        return &workerMarks_[id % kWorkerCount];
    };
    // dword_13CE27C type byte: the roster entry's typeCode (the pick boundary
    // resolves the contact id back to the entry).
    h.computeSelectionFlags = [this](u16, u8*, u8*, u8*) -> u16 {
        return selValid_ ? sel_.selectionFlags : 0;
    };
    h.objectTypeByte = [this](int) -> u8 {
        return selValid_ ? static_cast<u8>(sel_.typeCode) : 0;
    };
    Selection_SetCommitHooks(h);
}

void SessionSelect::OnPick(const ScenePickResult& pick,
                           const SessionSelectEntry* roster, int rosterCount,
                           float cursorX, float cursorY, float pickRadius) {
    resetLatch();
    selValid_ = false;
    selRadius_ = pickRadius;

    // Locate the picked roster entry (the hit-test boundary: the session's
    // RealCityRenderer::Pick already chose it; see header).
    const SessionSelectEntry* hit = nullptr;
    if (pick.index >= 0 && pick.id != 0) {
        for (int i = 0; i < rosterCount; ++i) {
            if (roster[i].id == pick.id) { hit = &roster[i]; break; }
        }
    }

    if (hit) {
        sel_ = *hit;
        selValid_ = true;

        // Materialise the shadow records 0x4b950c dereferences.
        std::memset(objShadow_.bytes, 0, sizeof objShadow_.bytes);
        std::memset(contactShadow_.bytes, 0, sizeof contactShadow_.bytes);
        std::memset(personShadow_.bytes, 0, sizeof personShadow_.bytes);

        // object record: handle string at +0, highlight bit at +529.
        std::snprintf(reinterpret_cast<char*>(objShadow_.bytes), 64, "%s",
                      hit->handle ? hit->handle : "");
        if (hit->highlightable) objShadow_.bytes[529] |= 1;

        g_selectionContact.g631724 = objShadow_.bytes;     // dword_631724
        if (hit->kind == 3) {
            // person record: id word at +0, active byte at +8, name at +48.
            *reinterpret_cast<u16*>(personShadow_.bytes) =
                static_cast<u16>(hit->id);
            personShadow_.bytes[8] = 1;
            std::snprintf(reinterpret_cast<char*>(personShadow_.bytes + 48),
                          sizeof personShadow_.bytes - 48, "%s",
                          hit->name ? hit->name : "");
            g_selectionContact.g631734 = personShadow_.bytes; // dword_631734
            g_selectionContact.g631730 = 0;
        } else {
            // contact sub-record: game-object id word at +0.
            *reinterpret_cast<i16*>(contactShadow_.bytes) =
                static_cast<i16>(hit->id);
            g_selectionContact.g63172C = contactShadow_.bytes; // dword_63172C
            g_selectionContact.g631730 = hit->selectable ? 1 : 0; // dword_631730
        }
    }

    // Gate: the session click is inside the live viewport by construction;
    // feed the real gate with the click cursor (16.16 fixed, as the original).
    g_selectGate.g67221C = 1;
    g_selectGate.g62D4E8 = 0;
    g_selectGate.cursorX16 = static_cast<i32>(cursorX) << 16;
    g_selectGate.cursorY16 = static_cast<i32>(cursorY) << 16;
    g_selectGate.g63CC4C = static_cast<i32>(cursorX) - 1;
    g_selectGate.g63CC50 = static_cast<i32>(cursorY) - 1;
    g_selectGate.g63CC54 = static_cast<i32>(cursorX) + 1;
    g_selectGate.g63CC58 = static_cast<i32>(cursorY) + 1;
    g_selectGate.g75BF08 = -1;
    g_selectGate.g62D31C = -1;

    installHooks();
    Selection_CommitContact();                 // gilde.exe 0x4b950c

    // The selection result may have been rejected (type 29 / empty click):
    // current() reads the anchors, so drop the roster latch when nothing stuck.
    if (!g_selectionAnchorRecords.a631740 && !g_selectionCommit.g11BC270)
        selValid_ = false;

    Selection_UpdateStatusTextLatch();         // gilde.exe 0x4bc280 tail
}

void SessionSelect::Clear() {
    installHooks();
    // The REAL deselect pair the engine's deselect paths use:
    DefaultSelectionReset();                   // VIBE_Selection_Reset @0x4b9444
    Selection_ClearAll();                      // VIBE_Selection_ClearAll @0x4b94d8
    selValid_ = false;
    resetLatch();
    Selection_UpdateStatusTextLatch();         // gilde.exe 0x4bc280 tail
}

SessionSelect::Info SessionSelect::current() const {
    Info out;
    const bool objectSel = g_selectionAnchorRecords.a631740 != nullptr;
    const bool workerSel = g_selectionCommit.g11BC270 != nullptr;
    if (!selValid_ || (!objectSel && !workerSel))
        return out;

    out.has  = true;
    out.id   = sel_.id;
    out.kind = sel_.kind;

    if (sel_.kind == 3 && workerSel) {
        // Person: VIBE_InfoPanel_BuildPerson @0x4b7104 —
        // FormatItemLabelWithIcon(*(u16*)record, kind=4) over the person record
        // (job title "$A" name+48; plain persons emit the +48 name).
        char buf[256] = {0};
        FormatItemLabelWithIcon(static_cast<u32>(sel_.id) & 0xFFFF,
                                /*mode=*/0, buf, /*kind=*/4,
                                g_selectionCommit.g11BC270,
                                /*is_default_record=*/false);
        std::snprintf(out.name, sizeof out.name, "%s", buf);
    } else {
        // Object/building: VIBE_InfoPanel_BuildStandard @0x4b6db8 —
        // "$Z%s$A%s$A%s": item label / kind name (text id 1078 + 14*code) /
        // custom name (record+5).  Info.name returns the most specific line:
        // the custom name when present, else the kind-name text entry.
        const char* custom = sel_.name && sel_.name[0] ? sel_.name : nullptr;
        const char* kindName = nullptr;
        if (textDb_)
            kindName = textDb_->Text(kSelectKindNameBias +
                                     kSelectKindNameStride * sel_.typeCode);
        std::snprintf(out.name, sizeof out.name, "%s",
                      custom ? custom : (kindName ? kindName : ""));
    }
    return out;
}

SessionSelect::Highlight SessionSelect::highlight() const {
    Highlight h;
    if (!selValid_)
        return h;
    if (!g_selectionAnchorRecords.a631740 && !g_selectionCommit.g11BC270)
        return h;
    h.has = true;
    h.id = sel_.id;
    h.screenX = sel_.screenX;
    h.screenY = sel_.screenY;
    h.radius = selRadius_;
    return h;
}

bool SessionSelect::workerSelected(i32 personId) const {
    return workerMarks_[static_cast<u16>(personId) % kWorkerCount] != 0;
}

} // namespace guild::play
