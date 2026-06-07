#pragma once
// History / Chronicle — the "real-day" forward scanner (the second of the two
// chronicle scan drivers). Complements world/history_full.h, which recovers the
// FORWARD-pass step classifier (VIBE_History_ScanNextEventForward 0x4fe694); here
// we recover the REAL-pass classifier (VIBE_History_ScanNextEventReal 0x4fe9cc).
//
// The two drivers walk the same chronicle label list in stored order, comparing
// each entry's parsed day to the current game day (qword_13CE852 low dword):
//   diff == currentDay - entryDay
//     diff <  1  -> STOP   (code 2): scanned past the current day
//     diff == 1  -> the "yesterday" window: resolve the entry's text
//     diff >  1  -> CONTINUE: older entry, advance to the next label
// They differ in how they resolve the matched entry:
//   * Forward (0x4fe694) calls VIBE_History_ParseTextReal (bracket collapse); a
//     non-empty result emits (code 1), an empty one is code 6.
//   * Real    (0x4fe9cc) calls VIBE_History_ParseLabelPasses (the 3-stage gate);
//     a FAILED gate is code 5, a passing gate with empty text is code 6, a passing
//     gate with non-empty text emits (code 1).
// Both stop the scan (clear byte_633924) when a date label fails to parse (code 4)
// or when the cursor walks past the end (code 3 == end-of-file).
#include <cstdint>

#include "guild/common/types.h"
#include "world/history_full.h"   // HistoryScanCode (kEmit/kStop/kEndOfFile/...)

namespace guild::world {

// The REAL pass adds one code the FORWARD pass lacks: a label-pass GATE FAILURE.
constexpr int kHistoryScanLabelGateFailed = 5;  // VIBE_History_ParseLabelPasses == 0

// gilde.exe 0x4fe9cc — VIBE_History_ScanNextEventReal (classification core).
// Classifies one scan step of the REAL pass given:
//   `parseOk`   : VIBE_History_ParseDate succeeded for this label
//   `dayDiff`   : currentDay - entryDay (qword_13CE852 - v7[0])
//   `gatePassed`: VIBE_History_ParseLabelPasses returned non-zero (only consulted
//                 when dayDiff == 1)
//   `textEmpty` : the resolved text is empty (*a2 == 0; only when gate passed)
// Returns:
//   kParseError (4) when !parseOk
//   kStop       (2) when dayDiff <= 1 but != 1 (i.e. dayDiff < 1)
//   5 (label-gate failed) when dayDiff == 1 and !gatePassed
//   kEmitEmpty  (6) when dayDiff == 1, gate passed, text empty
//   kEmit       (1) when dayDiff == 1, gate passed, text non-empty
//   kContinue (0, returned as the int 0) when dayDiff > 1
// (Codes 0/5/6 keep the original loop scanning; 1/2/4 terminate the call.)
int HistoryScanRealStep(bool parseOk, i32 dayDiff, bool gatePassed, bool textEmpty);

// True when a step code keeps the original's loop running (the original advances
// to the next label and re-tests): codes 0 (continue), 5 (gate failed) and 6
// (empty) all loop; 1/2/4 return out of the call.
bool HistoryScanRealStepContinues(int stepCode);

} // namespace guild::world
