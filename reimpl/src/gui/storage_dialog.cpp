#include "gui/storage_dialog.h"

namespace guild::gui {

namespace {

StorageCommandSink  g_defaultSink;
StorageCommandSink* g_sink = &g_defaultSink;

// Stable child-object id bases for the NewSlot lines.
constexpr int kWidthObjBase = 3000;
constexpr int kDepthObjBase = 3001;

} // namespace

void StorageDialog_SetCommandSink(StorageCommandSink* sink) {
    g_sink = sink ? sink : &g_defaultSink;
}

// gilde.exe 0x545fc8 (layout half) — VIBE_StorageDialog_NewSlot.
//   RenderRichString(263);  ChildObjectId = -1; v5 = -1;
//   if (*a1 == 278) {
//     if (cap[578] > a4) { RenderRichString(266, 8000); ChildObjectId = GetChildObjectId; }
//   } else {
//     if (cap[576] > a4) { RenderRichString(264, 8000); ChildObjectId = GetChildObjectId; }
//     else RenderRichString(268);
//     if (cap[577] > a3) { RenderRichString(265, 8000); v5 = GetChildObjectId; }
//     else (falls to 269)
//   }
//   RenderRichString(269 / 267 ...);
NewSlotLayout StorageDialog_BuildNewSlot(const StorageState& s) {
    NewSlotLayout l{};
    l.form = kFormNewSlot;

    if (s.single) {
        // 278-type storage: one dimension; cap[578] vs usedWidth (a4).
        if (s.capWidth > s.usedWidth) {
            l.needWidth = true;
            l.widthObjId = kWidthObjBase; // RenderRichString(266, 8000); GetChildObjectId
        }
    } else {
        if (s.capWidth > s.usedWidth) {
            l.needWidth = true;
            l.widthObjId = kWidthObjBase; // RenderRichString(264, 8000); GetChildObjectId
        }
        // else: RenderRichString(268) "enough width"
        if (s.capDepth > s.usedDepth) {
            l.needDepth = true;
            l.depthObjId = kDepthObjBase; // RenderRichString(265, 8000); GetChildObjectId
        }
        // else: RenderRichString(269)
    }
    return l;
}

// gilde.exe 0x545fc8 (wiring half).
//   if (dword_75BF38 != -1 && (ChildObjectId == clicked || v5 == clicked || dword_75BF38 == 1155))
//     loop ends. We classify which object was hit.
NewSlotResult StorageDialog_DispatchNewSlot(const NewSlotLayout& l, int clickedId,
                                            int clickedObj) {
    if (l.widthObjId != -1 && clickedObj == l.widthObjId)
        return NewSlotResult::kWidth;
    if (l.depthObjId != -1 && clickedObj == l.depthObjId)
        return NewSlotResult::kDepth;
    if (clickedId == kStorageCancel)
        return NewSlotResult::kCancel;
    return NewSlotResult::kCancel;
}

// gilde.exe 0x5461b0 (wiring half) — VIBE_StorageDialog_Options action buttons.
//   if (dword_62D22C == v82 /*action OK*/) {
//       compute needWidth = cap - used (minus pending handler counts);
//       if (CheckResourceAmount(8000)) EnqueueCmd15(8000); ... -> enlarge.
//   } else if (dword_62D22C == v84 /*buy*/ && !(flag&0x80)) {
//       if (CheckResourceAmount(12800)) { RenderFormattedMessage(271,12800);
//         if (ShowMessageBox(257)) EnqueueCmd15(12800); }  -> buy whole slot.
//   }
bool StorageDialog_DispatchOptions(const StorageState& s, int okObj, int buyObj,
                                   int clickedId, int clickedObj) {
    if (clickedId == kStorageCancel)
        return false;

    if (clickedObj == okObj) {
        // Enlarge the deficient dimension(s) for 8000 each.
        bool dispatched = false;
        if (s.single) {
            if (s.usedWidth >= s.capWidth) { // single-dim storage full -> enlarge width
                g_sink->EnlargeSlot(s.handle, 0, kSlotEnlargeCost);
                dispatched = true;
            }
        } else {
            if (s.usedWidth >= s.capWidth) {
                g_sink->EnlargeSlot(s.handle, 0, kSlotEnlargeCost);
                dispatched = true;
            }
            if (s.usedDepth >= s.capDepth) {
                g_sink->EnlargeSlot(s.handle, 1, kSlotEnlargeCost);
                dispatched = true;
            }
        }
        return dispatched;
    }

    if (clickedObj == buyObj) {
        g_sink->BuyNewSlot(s.handle, kSlotBuyCost); // 12800
        return true;
    }
    return false;
}

} // namespace guild::gui
