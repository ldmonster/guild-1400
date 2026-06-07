#pragma once
// guild::audio — worker-comment VOICE NAME BUILDER of gilde.exe.
//
// The game speaks contextual lines ("worker comments") by composing a sample
// name from a base string + a per-person voice suffix (+ an optional 2-digit
// variation index), then playing it out of a loaded language sample bank. This
// module recovers that name composition byte-for-byte and the small dispatch
// helpers around it; the actual playback routes through the SampleBank lookup
// and shim::IAudioDevice (no Miles).
//
// Core:
//   VIBE_Voice_BuildSampleNameAndPlay @0x581c68  (compose + play)
//   VIBE_Voice_LoadLanguageBank       @0x582074  ("sprache\\%s")
//   VIBE_Voice_LoadWorkerCommentBank  @0x58209c  (pick .sbf set by building type)
//   VIBE_Voice_PlayPositionalSample   @0x581fc0
//   VIBE_Voice_PlayQueuedSample       @0x582024
//   VIBE_Voice_PlayWorkerClickComment @0x5823a4
//   VIBE_Voice_PlayCraftFavorComment  @0x582448
//   VIBE_Voice_PlayBuildingFavorComment @0x58269c
//
// ----------------------------------------------------------------------------
// VOICE SUFFIX selection (VIBE_Voice_BuildSampleNameAndPlay):
//
//  `person` is a 32-bit code interpreted as:
//   * code >= 0x300  -> a SENTINEL voice type (the value, read as a signed int):
//       -1 -> "_W2"   -2 -> "_W1"   -3 -> "_M3"   -4 -> "_M2"
//       -5 -> "_M1"   -6 -> "_HS"   -8 -> ""  (empty suffix)
//       -7 -> special: emit ONLY the base name (no suffix, skip variation)
//   * code <  0x300  -> a PERSON INDEX into word_12CE910 (stride 268 *words*).
//       gender byte at person+9:
//         == 1 (female) -> suffix sprintf("_W%i", (person.field1 & 1) + 1)
//                          => "_W1" or "_W2"
//         == 0 (male)   -> suffix sprintf("_M%i", person.field1 % 3 + 1)
//                          => "_M1".."_M3"
//       (`field1` is the dword at person+4; person.field1 = voice variant seed.)
//
//  Final name = base + suffix; if `variation` (a3) >= 0 AND the resolved sample
//  is NOT an mp3, append sprintf("%02i", variation) to the suffix and rebuild.
//  Sentinel string bytes (verified): "_W1","_W2","_M1","_M2","_M3","_HS" at
//  0x62608c.. ; the -8 / empty case is the zero gap at 0x6260A4.
// ----------------------------------------------------------------------------
#include "guild/common/types.h"
#include "audio/samplebank_load.h"
#include <string>

namespace guild::audio {

// person-code threshold: values >= this are sentinels, below are person indices.
constexpr u32 kVoiceSentinelThreshold = 0x300;

// A person record used by the builder. Only the two fields the suffix logic
// touches are modeled (gender byte at +9, variant seed dword at +4). In the
// original these are slots of word_12CE910 (stride 268 words = 536 bytes).
struct VoicePerson {
    int  variantSeed = 0; // person+4  (field1; & 1 for female, %3 for male)
    u8   gender = 0;      // person+9  (1 = female -> _W, 0 = male -> _M)
};

// Compose the per-person/sentinel voice suffix (the "_W%i"/"_M%i"/"_HS"/... part).
// `person` is the raw 32-bit code; `lookup` resolves a person index to a record
// (return false if the index is out of range / unknown — the original would
// read garbage, but tests inject a real table). On the special -7 sentinel,
// `onlyBase` is set true and the suffix is left empty.
std::string BuildVoiceSuffix(u32 person, const VoicePerson* personRec, bool& onlyBase);

// VIBE_Voice_BuildSampleNameAndPlay @0x581c68 — compose the full sample name.
// Returns the resolved name string (does not itself touch a device). `variation`
// (>=0) appends "%02i" unless the base+suffix names an mp3 in `bank`. `bank` is
// used only for the mp3 test (VIBE_Audio_SampleIsMp3); pass a parsed language
// bank. `onlyBaseOut`/`useVariationOut` report which play path the original took
// (PlaySample vs StartVariation).
std::string BuildSampleName(u32 person, const VoicePerson* personRec,
                            int variation, const std::string& base,
                            const SbBank& bank, bool audioReady,
                            bool& useVariationOut);

// VIBE_Voice_LoadLanguageBank @0x582074 — compose the language bank path
// "sprache\\<file>" the original passes to VIBE_Sound_LoadSampleBank.
std::string LanguageBankPath(const std::string& file);

// The four worker-comment bank filenames selected for a building type by
// VIBE_Voice_LoadWorkerCommentBank @0x58209c. `command`/`click` vary by the
// building's worker class (thieves / robbers / mercenaries / craft); `noise` is
// always the same; `greeting` is set only for a matched building type ("" else).
struct WorkerCommentBanks {
    std::string command;  // dword_6476E8 source ("..._BEFEHL_...")
    std::string click;    // dword_6476EC source ("..._KLICK_...")
    std::string noise;    // dword_6476F0 source (always KLICK_GERAEUSCHE)
    std::string greeting; // dword_6476F8 source ("BEGRUESSUNG_*", may be empty)
};

// VIBE_Voice_LoadWorkerCommentBank @0x58209c — pick the .sbf filenames for a
// building `type` byte. Recovered switch (verified against the string table):
//   type 4              -> BEFEHL_DIEBE     / KLICK_DIEBE
//   type 16             -> BEFEHL_RAEUBER   / KLICK_RAEUBER
//   type 19             -> BEFEHL_SOELDNER  / KLICK_SOELDNER
//   else (craft)        -> BEFEHL_HANDWERK  / KLICK_HANDWERK
//   noise always        -> ARBEITER_KOMMENTARE_KLICK_GERAEUSCHE.sbf
//   greeting by type: 7->KIRCHE 5->GELDLEIHE 8/14->KRAEUTERLADEN_PARFUEMERIE
//     9->LAGERHAUS 18/20/21->SCHMIEDE_STEIMETZ_TISCHLER
//     11/12/13->WALDSTUECK_STEINBRUCH_MINE 22->WIRTSHAUS  (else "")
WorkerCommentBanks SelectWorkerCommentBanks(u8 type);

} // namespace guild::audio
