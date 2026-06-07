#pragma once
// History / Chronicle — the dated-text formatter (inverse of VIBE_History_ParseDate)
// and the remaining "target" Notify text-id selectors.
//
// world/history.h already recovers VIBE_History_ParseDate's parse half, the
// ScanNextEventForward classifier and the first Notify batch; world/history_chronicle.h
// adds the wander/attack/plague selectors + an in-memory chronicle. This module
// completes the family with:
//   * the DD.MM.YYYY formatter that ParseDate renders via VIBE_Text_RenderFormattedMessage
//     ("%s, %i %s %i" / "%s, %s %i" / "%s, %i") — modelled as a fixed-field encoder
//     so a parse->format->parse roundtrip is bit-stable.
//   * VIBE_History_NotifyTargetFound      0x535afc  -> 3953
//   * VIBE_History_NotifyTargetReachedA   0x535bb0  -> 3963
//   * VIBE_History_NotifyTargetReachedB   0x535c68  -> 3973
//
// Each Notify shares the kind-6/7 gate and draws RandomModulo voice-line offsets;
// the He broadcast + text render are the engine's, so the recoverable rule is the
// gate, the chronicle text id, and the voice-line base selection.
#include "guild/common/types.h"
#include "world/history.h"   // ParsedDate, HistoryNotifyKindIsImportant

namespace guild::world {

// ===========================================================================
// Dated-text formatter (the inverse of VIBE_History_ParseDate).
// ===========================================================================
// Formats (day, month, year) as the chronicle's fixed-width "DD.MM.YYYY" label
// into `out` (>= 11 bytes). day/month are two zero-padded digits, year four. A day
// or month of 0 is normalised to 1 first (mirroring ParseDate's normalisation), so
// FormatThenParse is idempotent. Returns `out`.
char* HistoryFormatDate(int day, int month, int year, char* out);

// Parses then re-formats a "DD.MM.YYYY" string into `out` (>= 11 bytes); returns
// true when the input parsed (10 chars). Use to verify a parse<->format roundtrip:
// the re-formatted string equals the (normalised) input.
bool HistoryRoundtripDate(const char* dateText, char* out, ParsedDate* parsed);

// ===========================================================================
// "Target" Notify selectors (kind 6/7 gated).
// ===========================================================================
// gilde.exe 0x535afc — VIBE_History_NotifyTargetFound: chronicle text 3953, with a
// voice line at 3960 + RandomModulo(3). Returns 3953 (kind 6/7) or -1.
int HistoryNotifyTargetFoundTextId(int recordKindByte);
constexpr int kTargetFoundVoiceBase = 3960;   // voice = base + RandomModulo(3)

// gilde.exe 0x535bb0 — VIBE_History_NotifyTargetReachedA: chronicle text 3963; the
// voice-line base is 3967 when the target carries the "detained" flag (+9), else
// 3964, plus RandomModulo(3); a leading line 3970 + RandomModulo(3) precedes it.
// Returns 3963 (kind 6/7) or -1.
int HistoryNotifyTargetReachedATextId(int recordKindByte);
// Selects the reached-A voice-line base from the detained flag (3964 / 3967).
int HistoryNotifyTargetReachedAVoiceBase(bool detained);
constexpr int kTargetReachedALeadBase = 3970; // lead line = base + RandomModulo(3)

// gilde.exe 0x535c68 — VIBE_History_NotifyTargetReachedB: chronicle text 3973; the
// voice-line base is 3977 when detained (+9), else 3974, plus RandomModulo(3); a
// leading line 3980 + RandomModulo(3) precedes it. Returns 3973 (kind 6/7) or -1.
int HistoryNotifyTargetReachedBTextId(int recordKindByte);
int HistoryNotifyTargetReachedBVoiceBase(bool detained);
constexpr int kTargetReachedBLeadBase = 3980;

} // namespace guild::world
