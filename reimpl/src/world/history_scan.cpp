// History / Chronicle — the "real-day" forward scanner classifier.
// 1:1 port of the classification core of VIBE_History_ScanNextEventReal
// (gilde.exe 0x4fe9cc). See history_scan.h for the relationship to the FORWARD
// scanner (0x4fe694, classified in history_full.h).
//
// Original control flow (the per-entry body, with the engine cursor/file state
// elided):
//   if (!byte_633924) return 3;                       // scan inactive
//   while (1) {
//     if (!VIBE_History_ParseDate(...)) { byte_633924=0; return 4; }  // parse fail
//     if (currentDay - entryDay == 1) {
//       if (VIBE_History_ParseLabelPasses(...)) { if (!*a2) v5 = 6; } // pass: 6 if empty
//       else v5 = 5;                                                  // gate failed
//     } else {
//       if (currentDay - entryDay <= 1) return 2;                     // past the day
//       v5 = 0;                                                       // older, continue
//     }
//     cursor += 3; if (past end) byte_633924 = 0;
//     if (v5 && v5 != 6 && v5 != 5) break;            // emit -> return v5
//     if (!byte_633924) return 3;                     // ran off the end
//   }
//   return v5;
// The break path (v5 not in {0,5,6}) is the emit signal; we model it as code 1
// (kEmit), matching the FORWARD scanner's emit code and the documented intent.
#include "world/history_scan.h"

namespace guild::world {

// gilde.exe 0x4fe9cc — VIBE_History_ScanNextEventReal (classification core).
int HistoryScanRealStep(bool parseOk, i32 dayDiff, bool gatePassed, bool textEmpty)
{
    if (!parseOk)                                  // !VIBE_History_ParseDate -> 4
        return static_cast<int>(HistoryScanCode::kParseError);

    if (dayDiff == 1)                              // the "yesterday" window
    {
        if (gatePassed)                            // VIBE_History_ParseLabelPasses != 0
        {
            if (textEmpty)                         // !*a2 -> v5 = 6
                return static_cast<int>(HistoryScanCode::kEmitEmpty);
            return static_cast<int>(HistoryScanCode::kEmit); // emit (break path)
        }
        return kHistoryScanLabelGateFailed;        // gate failed -> v5 = 5
    }

    if (dayDiff <= 1)                              // <= 1 but != 1, i.e. < 1
        return static_cast<int>(HistoryScanCode::kStop); // return 2

    return 0;                                      // dayDiff > 1: v5 = 0 (continue)
}

// The loop keeps scanning on codes 0/5/6 (the `if (v5 && v5 != 6 && v5 != 5)`
// guard does NOT break for these), and returns out of the call on 1/2/4.
bool HistoryScanRealStepContinues(int stepCode)
{
    return stepCode == 0
        || stepCode == kHistoryScanLabelGateFailed                  // 5
        || stepCode == static_cast<int>(HistoryScanCode::kEmitEmpty); // 6
}

} // namespace guild::world
