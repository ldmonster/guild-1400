#pragma once
// guild::gui — VIBE_PamphletBoard_Show @0x549238.  The town pamphlet/notice board.
//
//   form = "misc\pamphlet_brett";  CenterChildWindows;  SelectWindow(form,0)
//     RenderRichString(5409, ...) board header.
//   Every ~500 ticks the board re-scans up to 4 active pamphlet handlers
//   (He_FindFirstHandlerByFilter(1,0,53)):
//     for each handler with a resolvable author (FindRecordById):
//       SelectWindow(form, oddSlot)   RenderRichString("$C"); RenderRichString(5410,
//         flagByte+5413, *author);            // author/title line in slot 1,3,5,7
//       SelectWindow(form, evenSlot)  RenderRichString("$C"); RenderRichString(5411, 2)
//         -> GetChildObjectId stored in slot[i]; remember the handler entity at slot[i].
//       SetVisibleRecursive(window, 1).        // reveal this row
//     The remaining (unused) odd slots are RenderRichString("$C") + SetVisibleRecursive(0).
//   modal loop:
//     right-click -> exit;
//     a click whose object matches one of the stored pamphlet objects -> if the player
//       passes the skill check, copy the timestamp into the handler and
//       QueueRequestEntity29 (sign/endorse the pamphlet); on success RequestBuildOp90.
//
// We recover the form name, the header text id (5409), the per-entry text ids (5410 author
// + flag base 5413, 5411 body), the odd/even slot pairing (slot 1+2, 3+4, ... up to 4
// entries), the "$C" centering token, the skill-gate + sign command (entity 29 / op 90),
// and the object->entity click mapping populated from a synthetic pamphlet set.

#include "gui/types.h"

#include <vector>

namespace guild::gui {

inline constexpr int kPamLoopForm = 423879;

inline constexpr const char* kFormPamphlet = "misc\\pamphlet_brett";
inline constexpr const char* kPamCenter    = "$C"; // centering markup token

inline constexpr int kTextPamHeader   = 5409; // board header (slot 0)
inline constexpr int kTextPamAuthor   = 5410; // author line (flagByte + 5413, *author)
inline constexpr int kTextPamFlagBase = 5413; // 5413 + handler[193] flag byte
inline constexpr int kTextPamBody     = 5411; // body line (slot even), 2 child objects

inline constexpr int kPamMaxEntries   = 4;    // up to 4 pamphlets shown
inline constexpr int kPamRescanTicks  = 500;  // re-scan cadence (dword_62EB38 + 500)
inline constexpr int kPamCmdEntity29  = 29;   // QueueRequestEntity29 (sign pamphlet)
inline constexpr int kPamCmdOp90      = 90;   // RequestBuildOp90 on success

// A pamphlet to show on the board.
struct Pamphlet {
    int author = 0;   // author entity (0 / unresolved -> skipped)
    int handler = 0;  // the pamphlet handler entity (returned on click)
    int flagByte = 0; // handler[193] -> author-line variant (5413 + flagByte)
};

// A placed board entry.
struct PamphletEntry {
    int handler  = 0;  // the handler entity this entry signs
    int objectId = -1; // the clickable body object id
    int oddSlot  = 0;  // author-line window slot (1,3,5,7)
    int evenSlot = 0;  // body-line window slot (2,4,6,8)
};

struct PamphletLayout {
    const char* form = nullptr;
    int headerText = kTextPamHeader;
    std::vector<PamphletEntry> entries; // up to 4
};

struct PamphletCommandSink {
    virtual ~PamphletCommandSink() = default;
    // Sign / endorse a pamphlet (QueueRequestEntity29 + RequestBuildOp90 on success).
    virtual void Sign(int /*handler*/) {}
};
void PamphletBoard_SetCommandSink(PamphletCommandSink* sink);

// gilde.exe 0x549238 (layout half) — place up to 4 resolvable pamphlets on the board.
//   Entry i -> odd slot (2*i+1), even slot (2*i+2); author with unresolved record skipped.
PamphletLayout PamphletBoard_Build(const std::vector<Pamphlet>& pamphlets);

// gilde.exe 0x549238 (wiring half) — a click on a pamphlet object signs it (when the skill
// check passes) and returns the handler entity; -1 otherwise.  `passesSkill` mirrors the
// VIBE_Dialog_CheckSkillRequirement gate.
int PamphletBoard_Dispatch(const PamphletLayout& l, int clickedObj, bool passesSkill);

} // namespace guild::gui
