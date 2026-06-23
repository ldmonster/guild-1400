#include "gui/talent_dialog.h"

namespace guild::gui {

namespace {

TalentCommandSink  g_defaultSink;
TalentCommandSink* g_sink = &g_defaultSink;

} // namespace

void TalentDialog_SetCommandSink(TalentCommandSink* sink) {
    g_sink = sink ? sink : &g_defaultSink;
}

// gilde.exe 0x546ce8 — the v48 ladder:
//   if (points >= 0x2A) { if (>=0x54){ if(>=0x7E){ if(>=0xA8){ if(>=0xD2) 10 else 7 }
//                          else 5 } else 3 } else 2 } else 1.
int TalentDialog_PointsToLevel(int points) {
    int p = points & 0xFF;
    if (p >= kTalThresholds[4]) return kTalLevels[5]; // >= 0xD2 -> 10
    if (p >= kTalThresholds[3]) return kTalLevels[4]; // >= 0xA8 -> 7
    if (p >= kTalThresholds[2]) return kTalLevels[3]; // >= 0x7E -> 5
    if (p >= kTalThresholds[1]) return kTalLevels[2]; // >= 0x54 -> 3
    if (p >= kTalThresholds[0]) return kTalLevels[1]; // >= 0x2A -> 2
    return kTalLevels[0];                              // < 0x2A -> 1
}

// gilde.exe 0x546ce8 (layout half).
//   v48 = PointsToLevel(points); v48 += debugAdjust;  (the v37 = (toggle&2)?0:-1 term)
//   slot-3 status:
//     if (trainer)             -> 4806
//     else if (points >= 0xFC) -> 4805
//     else if (level > avail)  -> 4807
//     else { (debugAdjust!=0) ? 4803 : 4804; canTrain = 1; }
TalentLayout TalentDialog_Build(const TalentState& s) {
    TalentLayout l{};
    l.form = kFormTalent;
    l.talent = s.talent;
    l.nameTextId = kTextTalNameBase + s.talent; // 4810 + talent
    l.descTextId = kTextTalDescBase + s.talent; // 4822 + talent
    l.sliderValue = s.points;

    int level = TalentDialog_PointsToLevel(s.points) + s.debugAdjust;
    l.requiredLevel = level;

    if (s.hasTrainer) {
        l.statusText = kTextTalTrainer;        // 4806
    } else if ((s.points & 0xFF) >= kTalPointsMax) {
        l.statusText = kTextTalMaxed;          // 4805
    } else if (level > s.availableLevel) {
        l.statusText = kTextTalCantJump;       // 4807
    } else {
        // 0x5471c0: cmp [v37],0; jz -> 4804 (loc_547225); else -> 4803. The 4803/4804
        // discriminator is the debug-adjust term v37 (debugAdjust != 0), NOT a training
        // flag. (dword_12CE919 only selects the 525/560 arg base *inside* the 4803 text.)
        l.statusText = (s.debugAdjust != 0) ? kTextTalCanTrain : kTextTalCanTrain2; // 4803/4804
        l.canTrain = true;
    }
    return l;
}

// gilde.exe 0x546ce8 (wiring half).
//   if (dword_75BF38 == 1210 && canTrain) { RequestBuildOp90(building, -level);
//     QueueRequestSlotReset28(kind 96); done; }
//   else if (dword_75BF38 == 1155) cancel.
bool TalentDialog_Dispatch(const TalentLayout& l, const TalentState& s, int clickedId) {
    if (clickedId == kTalClickOK) {
        if (l.canTrain) {
            g_sink->Train(s.building, l.requiredLevel); // op 90 with -level handled by sink
            return true;
        }
        return false;
    }
    if (clickedId == kTalClickCancel)
        return true;
    return false;
}

} // namespace guild::gui
