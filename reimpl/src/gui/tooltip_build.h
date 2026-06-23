#pragma once
// guild::gui — recoverable, self-contained cores of the VIBE_Tooltip_Build* builders.
//
// The builders proper (VIBE_Tooltip_Build{Building,Contact,Object,Upgrade,Person}) drive
// the form/text runtime and the world/economy databases, which live in other clusters.
// But each carries a small piece of *pure* logic the GUI owns outright — a key string it
// formats, a colour-table lookup, a text-index argument it computes. Those pieces are
// reproduced here byte-for-byte so they can be exercised without the GUI runtime, and so
// the builders' dispatch (tooltip.cpp / tooltip_dispatch.cpp) has concrete leaf behaviour.

#include "gui/tooltip.h"

#include <cstddef>

namespace guild::gui {

// ---------------------------------------------------------------------------
// VIBE_Tooltip_BuildContact @0x4f83e8 — contact help-text key.
// ---------------------------------------------------------------------------
// The builder copies the contact name, upper-cases it, formats "_HILFE_%s+0", and looks
// it up in the text array. If found it renders "$Z$[%s$]" with the found string and then
// the following entry. The recoverable core is the key construction + the found/not-found
// decision. `name` is the contact name; `out`/`outCap` receive the formatted key.
// Returns the number of characters written (excluding the NUL), matching sprintf.
int Tooltip_BuildContactKey(const char* name, char* out, std::size_t outCap);

// Result of resolving a contact tooltip: whether the help text exists and, if so, the
// text-array index the builder would render from. `findIndex` is the (mockable) text-
// array lookup — VIBE_Text_FindTextArrayIndex — returning -1 when absent.
struct ContactTooltip {
    bool hasText = false; // text key resolved
    int  textIndex = -1;  // resolved text-array index (the "$Z$[%s$]" subject)
};
ContactTooltip Tooltip_ResolveContact(const char* name,
                                      int (*findIndex)(const char* key));

// ---------------------------------------------------------------------------
// VIBE_Tooltip_BuildBuilding @0x4f78e4 — building tooltip layout values.
// ---------------------------------------------------------------------------
// The colour palette qmemcpy'd from dword_4F73F0 (7 dwords) and indexed by the building
// record's byte at +583. Recovered byte-exact from the binary.
inline constexpr int kBuildingColorCount = 7;
extern const u32 kBuildingColors[kBuildingColorCount];

// The set of text-index arguments the building tooltip renders, computed from the
// building record. All values are recovered directly from the decompile; the renderer
// (VIBE_Text_RenderRichString) and form selection are the only deferred parts.
struct BuildingTooltipLayout {
    u32 titleColor;   // kBuildingColors[record[+583]]  (id 0x27 subject, 4f7944-4f794b)
    int nameTextId;   // 14*(i8)code + 1078             (id 0x27, 4f794c-4f795f)
    int descTextId;   // 14*(i8)code + 1079             (RichString @0x4f7995, no explicit id)
    int descColor;    // kBuildingColors[record[+583]] again (id 0x2A, 4f79a9-4f79bd)
    int salePrice;    // VIBE_Building_ComputeSalePrice(code) (id 0x28)  -- supplied by caller
    int extraField;   // *(record + 579)                (id 0x29, 4f79f2)
};

// gilde.exe 0x4f78e4 — assemble the building tooltip's layout values from the record.
// `record` points at the 589-byte building record (record[+583] = colour selector,
// record[+579] = extra field). `code` is the building code (a1). `salePrice` is the
// economy value the caller computed via VIBE_Building_ComputeSalePrice (deferred).
BuildingTooltipLayout Tooltip_BuildingLayout(const u8* record, int code, int salePrice);

// ---------------------------------------------------------------------------
// VIBE_Tooltip_BuildObject / BuildUpgrade class selection.
// ---------------------------------------------------------------------------
// VIBE_Tooltip_BuildUpgrade @0x4f8154 early-outs (-1) when the object record's class byte
// at objectBase[65*code] == 29 (a non-upgradable). Exposed for the upgrade path.
inline constexpr int kUpgradeSkipClass = 29;
bool Tooltip_UpgradeApplies(const u8* objectBase, int objectCode);

} // namespace guild::gui
