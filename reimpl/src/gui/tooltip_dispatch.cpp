#include "gui/tooltip_dispatch.h"

#include <cstdint>

namespace guild::gui {

// gilde.exe 0x4f7424 — VIBE_Tooltip_DispatchByType.
//
// The original is one long function over a dozen globals. It splits into five phases,
// translated here 1:1 (the messy register-aliased classification in the middle is the
// piece tooltip.cpp already factored out as Tooltip_ClassifySubject, reused below).
TooltipAction Tooltip_Dispatch(TooltipDispatchState& s, TooltipDispatchSink& sink,
                               const TooltipTables& tables, const u8* sceneRef) {
    TooltipAction action = TooltipAction::kNone;

    // The classified subject for this pass. v0/v1/v2/v3 in the decompile correspond to
    // objectCode / buildingCode / personPtr / contactPtr in TooltipSubject.
    TooltipSubject subject;

    // ---- Phase 1: teardown ---------------------------------------------------------
    // if ( dword_75BF3C == -1 && dword_63390C != -1
    //   || dword_633914 && !dword_672238 && dword_633908 != -1 )
    if ((s.hoveredTooltipId == -1 && s.shownForId != -1) ||
        (s.builtForReqId && !s.scenePickActive && s.formHandle != -1)) {
        sink.Destroy(s.formHandle);     // VIBE_Form_Destroy(dword_633908)
        s.formHandle   = -1;
        s.formAnchorId = -1;
        s.shownForId   = -1;
        subject.contactPtr = nullptr;   // v3 = 0
        s.redrawFlag   = 1;             // dword_62D0D4 = 1
        s.builtForReqId = 0;            // dword_633914 = 0
        // VIBE_Coord_ConvertY(scene coord) — refresh cursor-space anchor; the conversion
        // has no effect on the dispatch decision, so we keep the input but do not need
        // its result here.
        (void)s.hoverSceneRefRaw;
        action = TooltipAction::kHidden;
    }

    // ---- Phase 2: pending contact-tooltip request ----------------------------------
    // if ( dword_631724 && dword_631724 != dword_633914 && dword_672238 )
    if (s.contactReqId && s.contactReqId != s.builtForReqId && s.scenePickActive) {
        if (s.contactReqPtr) {
            // LOWORD(v0) = *(_WORD*)dword_63172C; if nonzero -> take the request id.
            std::uint16_t firstWord = *reinterpret_cast<const std::uint16_t*>(s.contactReqPtr);
            subject.objectCode = static_cast<std::int16_t>(firstWord);
            if (firstWord) {
                s.builtForReqId = s.contactReqId; // LABEL_10
                s.shownForId    = -1;
            } else {
                // *ptr == 0: the original then sets v3 = dword_631724 (the request id,
                // reused as the contact handle) and goes to LABEL_10.
                subject.contactPtr = reinterpret_cast<void*>(
                    static_cast<std::uintptr_t>(static_cast<unsigned>(s.contactReqId)));
                s.builtForReqId = s.contactReqId;
                s.shownForId    = -1;
            }
        } else {
            // No record ptr: v0 = 0, then v3 = dword_631724 (id as contact handle).
            subject.objectCode = 0;
            subject.contactPtr = reinterpret_cast<void*>(
                static_cast<std::uintptr_t>(static_cast<unsigned>(s.contactReqId)));
            s.builtForReqId = s.contactReqId;
            s.shownForId    = -1;
        }
    }

    // ---- Phase 3: classify the hovered widget --------------------------------------
    // if ( dword_75BF3C != -1 && dword_75BF3C != dword_63390C )
    if (s.hoveredTooltipId != -1 && s.hoveredTooltipId != s.shownForId) {
        TooltipSubject classified = Tooltip_ClassifySubject(tables, sceneRef, s.hoveredTooltipId);
        // The classification supplies v0/v1/v2 (object/building/person). A contact ptr
        // (v3) only ever comes from phase 2, so preserve any already set.
        subject.objectCode   = classified.objectCode;
        subject.buildingCode = classified.buildingCode;
        subject.personPtr    = classified.personPtr;
        s.builtForReqId = 0;            // dword_633914 = 0
    }

    // Recompute the builder for the assembled subject (object class byte may pick
    // Object vs Upgrade). contactPtr from phase 2 must win when no other subject set.
    subject.kind = Tooltip_SelectBuilder(tables, subject);

    // ---- Phase 4: (re)build ---------------------------------------------------------
    // if ( (_WORD)v0 || v1 || v3 || v2 )
    bool haveSubject = subject.objectCode || subject.buildingCode ||
                       subject.contactPtr || subject.personPtr;
    if (haveSubject) {
        if (s.formHandle != -1) {
            sink.Destroy(s.formHandle); // tear down the previous tooltip first
            s.formHandle   = -1;
            s.formAnchorId = -1;
        }
        int anchorId = 0;
        int newHandle = sink.Build(subject.kind, subject, anchorId);
        s.formHandle = newHandle;
        // LABEL_26 falls through with the new handle (and anchorId for the !=-1 path).
        if (s.formHandle == -1) {
            s.shownForId    = -1;
            s.builtForReqId = 0;
        } else {
            s.shownForId   = s.hoveredTooltipId;       // dword_63390C = dword_75BF3C
            s.formAnchorId = anchorId;                 // dword_676A64[171*handle]
            if (s.scenePickActive)
                s.redrawFlag = 0;                      // dword_62D0D4 = 0
            action = TooltipAction::kBuilt;
        }
    } else {
        // LABEL_26 reached without building anything new.
        if (s.formHandle == -1) {
            s.shownForId    = -1;
            s.builtForReqId = 0;
        } else {
            s.shownForId   = s.hoveredTooltipId;
            // formAnchorId keeps its value (dword_676A64 reread, unchanged here).
            if (s.scenePickActive)
                s.redrawFlag = 0;
        }
    }

    // ---- Phase 5: visibility flags (LABEL_29) --------------------------------------
    if (s.formHandle == -1) {
        if (s.visibleFlag) {
            s.raiseFlag    = 0;   // dword_62D314 = 0
            s.visibleFlag  = 0;   // dword_633918 = 0
        }
    } else {
        s.raiseFlag   = 1;        // dword_62D314 = 1
        s.visibleFlag = 1;        // dword_633918 = 1
        sink.Raise(s.formHandle); // VIBE_Form_RaiseWindows(dword_633908)
        if (action == TooltipAction::kNone)
            action = TooltipAction::kBuilt; // a form is up; treat as shown
    }

    return action;
}

} // namespace guild::gui
