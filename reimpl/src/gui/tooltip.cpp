#include "gui/tooltip.h"

#include <cstdint>

namespace guild::gui {

namespace {
// Unsigned pointer-range test matching the original's `base <= p && p < base+span`
// comparisons (all done as unsigned 32-bit in the binary).
bool InRange(const u8* p, const u8* base, std::size_t span) {
    return base && p >= base && p < base + span;
}
} // namespace

// gilde.exe 0x4f7424 — classification core.
TooltipSubject Tooltip_ClassifySubject(const TooltipTables& t, const u8* sceneRef,
                                       int tooltipId) {
    TooltipSubject s;

    if (sceneRef) {
        // (a) person-record region [personRecLo, personRecHi): a direct record pointer.
        if (t.personRecLo && sceneRef >= t.personRecLo && sceneRef < t.personRecHi) {
            s.personPtr = const_cast<u8*>(sceneRef);
            if (*reinterpret_cast<const i16*>(sceneRef) == -1) // *v6 == -1 -> empty
                s.personPtr = nullptr;
        }
        // (b) building table: byte code, capped at 72.
        else if (InRange(sceneRef, t.buildingBase, kBuildingSpan)) {
            int code = *sceneRef;
            if (code >= kMaxBuildingCode) code = 0;
            s.buildingCode = code;
        }
        // (c) upgrade table: index by 589-byte stride -> a building code (v1).
        else if (InRange(sceneRef, t.upgradeBase, kUpgradeSpan)) {
            s.buildingCode = static_cast<int>(sceneRef - t.upgradeBase) / kUpgradeStride;
        }
        // (d) person-code table: word code, capped at 731 -> an object/person code (v0).
        else if (InRange(sceneRef, t.personCode, kPersonCodeSpan)) {
            int code = *reinterpret_cast<const i16*>(sceneRef);
            if (code >= kMaxPersonCode) code = 0;
            s.objectCode = code;
        }
        // (e) object table: index by 65-byte stride -> object code (v0).
        else if (InRange(sceneRef, t.objectBase, kObjectSpan)) {
            s.objectCode = static_cast<int>(sceneRef - t.objectBase) / kObjectStride;
        }
    } else {
        // Fallback by tooltip id when the widget carries no scene reference.
        if (tooltipId >= kObjIdLo && tooltipId < kObjIdHi) {
            s.objectCode = tooltipId - kObjIdLo;             // object
        } else if (tooltipId >= kObjIdHi && tooltipId < kBldIdHi) {
            s.buildingCode = tooltipId + kBldIdBias;         // building
        }
    }

    s.kind = Tooltip_SelectBuilder(t, s);
    return s;
}

// gilde.exe 0x4f7424 — final dispatch decision.
TooltipKind Tooltip_SelectBuilder(const TooltipTables& t, const TooltipSubject& s) {
    if (s.objectCode) {
        // Class byte = first byte of the object's 65-byte record.
        u8 cls = 0;
        if (t.objectBase)
            cls = t.objectBase[kObjectStride * static_cast<i16>(s.objectCode)];
        if (cls == 32 || cls == 23 || cls == 37)
            return TooltipKind::kObject;
        return TooltipKind::kUpgrade;
    }
    if (s.buildingCode)
        return TooltipKind::kBuilding;
    if (s.contactPtr)
        return TooltipKind::kContact;
    if (s.personPtr)
        return TooltipKind::kPerson;
    return TooltipKind::kNone;
}

} // namespace guild::gui
