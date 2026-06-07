#pragma once
// guild::gui — tooltip dispatch: classify the hovered subject and pick its builder.
//
// VIBE_Tooltip_DispatchByType @0x4f7424 decides, each frame, what tooltip (if any) to
// show for the hovered widget. The decision has two recoverable model layers:
//
//  (1) Classify the hovered widget's "scene reference" (widget +736) by which runtime
//      base-table range it falls into.  Each range is a flat array of fixed-stride
//      records; the offset into the range yields a code (object / building / upgrade /
//      person) or a direct pointer (person record / contact string).  When +736 is 0 a
//      fallback uses the widget's tooltip id directly.
//  (2) For an object code, a class byte (the first byte of its 65-byte record) selects
//      between the "object" and "upgrade" tooltip builders.
//
//  The builders themselves (VIBE_Tooltip_Build{Object,Upgrade,Building,Contact,Person})
//  assemble a .form from the world/economy databases and are deferred to those clusters;
//  the type *dispatch* — the part the GUI owns — is reproduced here byte-for-byte.

#include "gui/types.h"

namespace guild::gui {

// Which builder the dispatcher would invoke for the classified subject.
enum class TooltipKind {
    kNone,      // nothing to show
    kObject,    // VIBE_Tooltip_BuildObject   (object code, class byte in {32,23,37})
    kUpgrade,   // VIBE_Tooltip_BuildUpgrade  (object code, other class byte)
    kBuilding,  // VIBE_Tooltip_BuildBuilding (building code)
    kContact,   // VIBE_Tooltip_BuildContact  (contact string ptr)
    kPerson,    // VIBE_Tooltip_BuildPerson   (person record ptr)
};

// Result of classifying a hovered subject: the kind plus the resolved code/handle.
struct TooltipSubject {
    TooltipKind kind = TooltipKind::kNone;
    int   objectCode   = 0; // v0  (object/person-via-code)
    int   buildingCode = 0; // v1
    void* personPtr    = nullptr; // v2
    void* contactPtr   = nullptr; // v3
};

// Runtime base pointers / spans of the scene-reference tables (dword_13CE2xx and the
// word_12CE910..byte_1333110 person-record region).  The renderer/scene cluster owns
// these; the dispatcher only needs their bases and the fixed strides/spans below.
struct TooltipTables {
    const u8* objectBase   = nullptr; // dword_13CE27C : object records, 65-byte stride
    const u8* personCode   = nullptr; // dword_13CE290 : person-code records (word each)
    const u8* upgradeBase  = nullptr; // dword_13CE294 : upgrade records, 589-byte stride
    const u8* buildingBase = nullptr; // dword_13CE298 : building records (byte code each)
    const u8* personRecLo  = nullptr; // word_12CE910  : person-record region low bound
    const u8* personRecHi  = nullptr; // byte_1333110  : person-record region high bound
};

// Fixed strides / spans recovered from VIBE_Tooltip_DispatchByType.
inline constexpr int   kObjectStride    = 65;     // object record size
inline constexpr int   kObjectSpan      = 47515;  // object table byte span
inline constexpr int   kUpgradeStride   = 589;    // upgrade record size
inline constexpr int   kUpgradeSpan     = 42408;  // upgrade table byte span
inline constexpr int   kPersonCodeSpan  = 548864; // person-code table byte span
inline constexpr int   kBuildingSpan    = 43264;  // building table byte span
inline constexpr int   kMaxBuildingCode = 72;     // building code >= 72 -> none
inline constexpr int   kMaxPersonCode   = 731;    // person code   >= 731 -> none
inline constexpr int   kObjIdLo         = 206;    // id-fallback object range [206,1010)
inline constexpr int   kObjIdHi         = 1010;
inline constexpr int   kBldIdHi         = 1082;   // id-fallback building range [1010,1082)
inline constexpr int   kBldIdBias       = 14;     // building code = id + 14

// gilde.exe 0x4f7424 (classification core) — classify a hovered subject.
// `sceneRef` is the widget +736 value (a pointer into one of the scene tables, or 0).
// `tooltipId` is the widget's tooltip id, used only when sceneRef == 0.
TooltipSubject Tooltip_ClassifySubject(const TooltipTables& t, const u8* sceneRef,
                                       int tooltipId);

// gilde.exe 0x4f7424 (final dispatch) — given a classified subject, return the builder
// that fires. For an object code it reads the class byte at objectBase[65*objectCode]
// and maps {32,23,37} -> kObject, else kUpgrade.
TooltipKind Tooltip_SelectBuilder(const TooltipTables& t, const TooltipSubject& s);

} // namespace guild::gui
