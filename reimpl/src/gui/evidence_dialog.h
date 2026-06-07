#pragma once
// guild::gui — the evidence ("Beweise") dialogs.
//
//   VIBE_EvidenceDialog_Browse      @0x548168 — list every person we hold evidence on.
//   VIBE_EvidenceDialog_ShowDetails @0x547e88 — the per-person evidence detail sheet.
//
// Browse:
//   form = "privillegien\Beweise_Sichten";  CenterChildWindows;
//   SelectWindow(form,1) RenderRichString(4946) title;
//   SelectWindow(form,3) Window_CreateScrollButtons(..., 1753);   // scroll arrows
//   He_CollectPlayerEntityHandlers -> N persons.  For each (FindRecordById ok):
//     SelectWindow(form,2);  RenderFormattedMessage("%i (%i) %s", count, sum, plural-base)
//       (plural base = (count!=1) + 4955);  Hud_BuildPersonCard(x=50, y=105*i+10, ...);
//       Hud_AddRightAlignedLabel(label, winW-15, 200, 55+105*i, 68).
//     Row stride = 105 px; first row y = 10; the right label baseline starts at 55 (+105).
//   if N == 0 -> RenderRichString(4948).  Clicking a person card opens ShowDetails.
//
// ShowDetails:
//   He_FindMatchingEntityIndices -> M evidence rows (45-byte records from dword_11BC760);
//   SortEntitiesByRank;  form = "privillegien\Beweise_Details";  CenterChildWindows;
//   SelectWindow(form,4) Window_CreateScrollButtons(..., 1753);
//   SelectWindow(form,1) RenderRichString(4949, *target);
//   SelectWindow(form,2): for each row i: Hud_AddLeftAlignedLabel(name, x=10, w=315,
//     y=10+40*i, 68);  for each "seal" of the law (Gesetz_GetRecord -> count): add a seal
//     icon (Object_AddToWindow id 1227) at x=340+16*k, baseline 10+40*i.   Row stride 40 px.
//   SelectWindow(form,3): a debug-toggle extra line (4957).  Read-only modal (loop 415687).
//
// We recover the form names, window slots, title/empty text ids, the 105-px / 40-px / 16-px
// layout strides, the scroll-button id (1753), the seal icon id (1227), and the person-card
// click -> ShowDetails wiring, populating rows from a synthetic evidence set.

#include "gui/types.h"

#include <vector>

namespace guild::gui {

inline constexpr int kEvLoopForm = 415687; // RunFrameLoop panel-style selector

inline constexpr const char* kFormEvBrowse  = "privillegien\\Beweise_Sichten";
inline constexpr const char* kFormEvDetails = "privillegien\\Beweise_Details";

inline constexpr int kTextEvBrowseTitle = 4946; // Browse title
inline constexpr int kTextEvBrowseEmpty = 4948; // "no evidence" line
inline constexpr int kTextEvDetailTitle = 4949; // Details title (with person)
inline constexpr int kTextEvDetailExtra = 4957; // debug-toggle extra detail line
inline constexpr int kTextEvPluralBase  = 4955; // (count!=1)+4955 plural selector

inline constexpr int kEvScrollButtonId = 1753; // Window_CreateScrollButtons id
inline constexpr int kEvSealIconId      = 1227; // per-seal evidence icon (Object_AddToWindow)

// Browse layout strides.
inline constexpr int kEvBrowseRowStride = 105; // 105 px per person card
inline constexpr int kEvBrowseRowY0     = 10;  // first card y
inline constexpr int kEvBrowseCardX     = 50;  // person-card x
inline constexpr int kEvBrowseLabelY0   = 55;  // right-label first baseline
inline constexpr int kEvBrowseLabelW    = 200; // right-label width arg

// Details layout strides.
inline constexpr int kEvDetailRowStride = 40;  // 40 px per evidence row
inline constexpr int kEvDetailRowY0     = 10;  // first row y
inline constexpr int kEvDetailNameX     = 10;  // name label x
inline constexpr int kEvDetailNameW     = 315; // name label width
inline constexpr int kEvDetailSealX0    = 340; // first seal-icon x
inline constexpr int kEvDetailSealStep  = 16;  // seal-icon x step

// ---------------------------------------------------------------------------
// Synthetic evidence data.
// ---------------------------------------------------------------------------
struct EvidencePerson {
    int entity = 0;     // the person's entity id (person record handle)
    int count  = 0;     // number of evidence entries (the "%i (%i)" count)
    int sum    = 0;     // an aggregate value shown in parentheses
};

// One evidence row in the detail sheet: a law id with `seals` violation marks.
struct EvidenceRow {
    int lawId = 0;
    int seals = 0; // number of 1227 seal icons drawn for this row
};

// A widget the browse list places (one per person).
struct EvBrowseCard {
    int entity   = 0;
    int objectId = -1; // the card's clickable object id
    int cardY    = 0;  // 10 + 105*i
    int labelY   = 0;  // 55 + 105*i
};

struct EvBrowseLayout {
    const char* form = nullptr;
    int titleText = kTextEvBrowseTitle;
    bool empty = false; // N==0 -> RenderRichString(4948); no cards
    std::vector<EvBrowseCard> cards;
};

// A placed seal icon in the detail sheet.
struct EvSealIcon {
    int x = 0; // 340 + 16*k
    int y = 0; // 10 + 40*rowIndex
};

struct EvDetailRowLayout {
    int rowIndex = 0;
    int lawId = 0;
    int nameY = 0;            // 10 + 40*rowIndex
    std::vector<EvSealIcon> seals; // one per violation seal of the row's law
};

struct EvDetailLayout {
    const char* form = nullptr;
    int titleText = kTextEvDetailTitle;
    int targetEntity = 0;
    std::vector<EvDetailRowLayout> rows;
};

// gilde.exe 0x548168 (layout half) — build the browse list for the player's evidence set.
//   Each FindRecordById-resolvable person becomes a card at y = 10 + 105*i.
EvBrowseLayout EvidenceDialog_BuildBrowse(const std::vector<EvidencePerson>& people);

// gilde.exe 0x548168 (wiring half) — clicking a card returns the person's entity (or -1).
int EvidenceDialog_DispatchBrowse(const EvBrowseLayout& l, int clickedObj);

// gilde.exe 0x547e88 (layout half) — build the detail sheet for one person's evidence rows.
//   Row i at y = 10 + 40*i; seal k at x = 340 + 16*k.  Returns empty form (no rows) when the
//   evidence set is empty (the original returns early when count < 1).
EvDetailLayout EvidenceDialog_BuildDetails(int targetEntity,
                                           const std::vector<EvidenceRow>& rows);

} // namespace guild::gui
