#include "gui/infopanel.h"

namespace guild::gui {

// gilde.exe 0x4b84c0 — the rebuild dispatch.  The original runs two consecutive
// blocks after InfoPanel_Destroy():
//
//   (A) primary subject:
//         if (!transporter || transporterCategory != 29 || personMode || person)
//             if (personGroup||charFlag||personMode||subObject||person)
//                 if (personMode) { personMode<=1 ? BuildPerson(person) : BuildPerson(0) }
//             else BuildStandard()
//         else BuildTransporter()
//
//   (B) object/building detail (guarded by personGroup||building||room):
//         if ((transporter||subObject) && playerbar==-1)
//             transporter ? BuildObject(transporter) : (subObject ? BuildObject(subObject))
//         else if (sceneObject||building)
//             BuildBuilding(sceneObject ? sceneObject : building)
//
// The visible panel is the *last* builder to run, so we resolve the dominant builder
// following that evaluation order: transporter-passenger wins outright; otherwise the
// object/building detail (B) wins when a room/building/object is active; otherwise the
// person/standard primary (A) applies.  `playerbar` is dword_631768 (== -1 when the
// player bar is not occupying the panel); in this model we assume the panel is free
// (the common case the original takes for the detail build).
InfoBuilder InfoPanel_SelectBuilder(const InfoSelection& sel) {
    // (A-else) transporter passenger panel: transporter set, category 29, and not in a
    // person/multi-select state.
    bool transporterBranch = sel.transporter != 0 &&
                             sel.transporterCategory == kTransporterPanelCategory &&
                             sel.personMode == 0 && sel.person == 0;
    if (transporterBranch)
        return InfoBuilder::kTransporter;

    // (B) object/building detail block, guarded by (personGroup || building || room).
    if (sel.personGroup || sel.building || sel.room) {
        if ((sel.transporter || sel.subObject) /* && playerbar == -1 */) {
            if (sel.transporter)
                return InfoBuilder::kObject; // BuildObject(transporter)
            if (sel.subObject)
                return InfoBuilder::kObject; // BuildObject(subObject)
        } else if (sel.sceneObject || sel.building) {
            return InfoBuilder::kBuilding;   // BuildBuilding(sceneObject ? : building)
        }
    }

    // (A) person vs standard.
    if (sel.personGroup || sel.charFlag || sel.personMode || sel.subObject || sel.person) {
        if (sel.personMode) {
            // personMode<=1 -> BuildPerson(person); else aggregate BuildPerson(0).
            return InfoBuilder::kPerson;
        }
        // personMode == 0 but a person-ish flag is set with no detail subject: the
        // original falls through without a primary build -> nothing new.
        return InfoBuilder::kNone;
    }
    return InfoBuilder::kStandard;
}

// gilde.exe 0x4b84c0 (the OR-chain at 0x4b85e3).
bool InfoPanel_SelectionChanged(const InfoSelection& sel, const InfoSnapshot& snap) {
    if (!snap.valid)
        return true;
    return sel.sceneObject != snap.sceneObject   // dword_11BC278 != dword_631E2C
        || sel.building    != snap.building       // dword_631748 != dword_631E28
        || sel.room        != snap.room           // dword_631744 != dword_631E24
        || sel.subObject   != snap.subObject      // dword_63174C != dword_631E30
        || sel.transporter != snap.transporter    // dword_11BC274 != dword_631E34
        || sel.person      != snap.person          // dword_11BC270 != dword_631E38
        || sel.charFlag    != snap.charFlag        // byte_6317B4   != dword_631E3C
        || sel.personMode  != snap.personMode;     // dword_6317B0  != dword_631E40
}

InfoSnapshot InfoPanel_Snapshot(const InfoSelection& sel) {
    InfoSnapshot s{};
    s.room        = sel.room;
    s.building    = sel.building;
    s.subObject   = sel.subObject;
    s.person      = sel.person;
    s.transporter = sel.transporter;
    s.sceneObject = sel.sceneObject;
    s.personGroup = sel.personGroup;
    s.personMode  = sel.personMode;
    s.charFlag    = sel.charFlag;
    s.valid       = true;
    return s;
}

namespace {
InfoPanelCommandSink g_defaultSink;
InfoPanelCommandSink* g_sink = &g_defaultSink;
} // namespace

void InfoPanel_SetCommandSink(InfoPanelCommandSink* sink) {
    g_sink = sink ? sink : &g_defaultSink;
}

InfoBuilder InfoPanel_Update(const InfoSelection& sel, InfoSnapshot& snap) {
    if (!InfoPanel_SelectionChanged(sel, snap))
        return InfoBuilder::kNone;
    InfoBuilder b = InfoPanel_SelectBuilder(sel);
    g_sink->Build(b);
    snap = InfoPanel_Snapshot(sel);
    return b;
}

} // namespace guild::gui
