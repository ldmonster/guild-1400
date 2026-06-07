#pragma once
// guild::audio — sample-bank (sb) indexing facade of gilde.exe.
//
// A sample bank is a tree of named records loaded from disk:
//   bank -> sample list (head at bank+0x38, next at sample+0x104)
//        -> variation list (head at bank+0x3C, next at variation+0x3C)
//   variation -> sample list (head at variation+0x38, next at sample+0x104)
// Lookups are case/exact name matches walking those singly-linked lists.
//
// Recovered from:
//   VIBE_SampleBank_FindSampleByName     @0x447704
//   VIBE_SampleBank_FindVariationByName  @0x44773c
//   VIBE_SampleBank_FindSampleInVariation@0x447888
//   VIBE_SampleBank_GetLastSample        @0x4476dc
//   VIBE_SampleBank_ComputeVariationSize @0x4478d4
//   VIBE_SampleBank_ComputeTotalSize     @0x447614
//
// The original walks raw records via pointer offsets (sample stride 0x104+name,
// variation node at +0x3C). We model the records as objects but keep the exact
// link/field semantics and the linear name-match search.
#include "guild/common/types.h"
#include <string>
#include <vector>

namespace guild::audio {

// 12 bytes per "sub-sample" counted toward bank/variation size by the original
// (CountSamples * 12). Each sample's PCM file entry: a format byte + name ptr.
constexpr int kSubSampleStride = 12;

// A single named sample record. In gilde.exe the name lives at +0x04 (50-byte
// padded), the next-in-list link at +0x104 (i[65] as DWORDs).
struct SampleRecord {
    std::string name;         // +0x04  (StrNCopyPad to 50 bytes in the original)
    int format = 0;           // 1 = .wav, 2 = .mp3 (VIBE_Audio_PlayVoiceSample)
    std::vector<int> entries; // sub-sample PCM entries (CountSamples)
    std::vector<u8> pcm;      // resolved PCM payload (for device playback)
    int sampleRate = 44100;
};

// A variation groups alternative samples under one name. Name at +0x04, samples
// head at +0x38 (var[14]), next-variation link at +0x3C (var[15]).
struct VariationRecord {
    std::string name;                    // +0x04
    std::vector<SampleRecord> samples;   // list head at +0x38
};

// A bank: a flat sample list plus a variation list. Sample list head at +0x38,
// variation list head at +0x3C, sample count at +0x134, sample array at +0x138.
class SampleBank {
public:
    // VIBE_SampleBank_FindSampleByName @0x447704 — first top-level sample whose
    // name matches exactly (StrCmp). Returns null when not found.
    SampleRecord* findSampleByName(const std::string& name);

    // VIBE_SampleBank_FindVariationByName @0x44773c — first variation by name.
    VariationRecord* findVariationByName(const std::string& name);

    // VIBE_SampleBank_FindSampleInVariation @0x447888 — within the named
    // variation, the first sample matching `sampleName`.
    SampleRecord* findSampleInVariation(const std::string& variation,
                                        const std::string& sampleName);

    // VIBE_SampleBank_GetLastSample @0x4476dc — last sample of the named
    // variation (walk to the end of its sample list). Null if empty/not found.
    SampleRecord* getLastSample(const std::string& variation);

    // VIBE_SampleBank_ComputeVariationSize @0x4478d4 —
    //   12 * countSamples + variationBaseSize.
    int computeVariationSize(const std::string& variation, int variationBaseSize);

    // VIBE_SampleBank_ComputeTotalSize @0x447614 — the per-bank size formula:
    //   12*subSamples + 12*sampleCount + (totalNodes << 6) + 324 + baseSize
    // where totalNodes = (#samples + #variations).
    int computeTotalSize(int subSampleCount, int baseSize) const;

    // Convenience builders for tests / loaders (not from a single VIBE_ fn).
    SampleRecord& addSample(const std::string& name);
    VariationRecord& addVariation(const std::string& name);

    std::vector<SampleRecord>& samples() { return samples_; }
    std::vector<VariationRecord>& variations() { return variations_; }

private:
    std::vector<SampleRecord> samples_;       // +0x38 list
    std::vector<VariationRecord> variations_; // +0x3C list
};

} // namespace guild::audio
