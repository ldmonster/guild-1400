#include "audio/samplebank.h"

namespace guild::audio {

SampleRecord* SampleBank::findSampleByName(const std::string& name) {
    // VIBE_SampleBank_FindSampleByName @0x447704: linear StrCmp over +0x38 list.
    for (auto& s : samples_) {
        if (s.name == name)
            return &s;
    }
    return nullptr;
}

VariationRecord* SampleBank::findVariationByName(const std::string& name) {
    // VIBE_SampleBank_FindVariationByName @0x44773c: linear StrCmp over +0x3C list.
    for (auto& v : variations_) {
        if (v.name == name)
            return &v;
    }
    return nullptr;
}

SampleRecord* SampleBank::findSampleInVariation(const std::string& variation,
                                                const std::string& sampleName) {
    // VIBE_SampleBank_FindSampleInVariation @0x447888.
    VariationRecord* v = findVariationByName(variation);
    if (!v)
        return nullptr;
    for (auto& s : v->samples) {
        if (s.name == sampleName)
            return &s;
    }
    return nullptr;
}

SampleRecord* SampleBank::getLastSample(const std::string& variation) {
    // VIBE_SampleBank_GetLastSample @0x4476dc: walk var's sample list to the end.
    VariationRecord* v = findVariationByName(variation);
    if (!v || v->samples.empty())
        return nullptr;
    return &v->samples.back();
}

int SampleBank::computeVariationSize(const std::string& variation, int variationBaseSize) {
    // VIBE_SampleBank_ComputeVariationSize @0x4478d4.
    VariationRecord* v = findVariationByName(variation);
    if (!v)
        return 0;
    int count = static_cast<int>(v->samples.size());
    return kSubSampleStride * count + variationBaseSize;
}

int SampleBank::computeTotalSize(int subSampleCount, int baseSize) const {
    // VIBE_SampleBank_ComputeTotalSize @0x447614.  Disasm-exact formula
    // (S=topSamples, V=variations, C=subSampleCount=CountSamples(0)):
    //   12*(S+C) + 12*V + (S+V)<<6 + 324 + bankSize
    // Decomposed in the binary as:
    //   lea/sub/shl  -> 12*(S+C)   (0x447665..0x447671)
    //   shl ebx,6    -> (S+V)<<6   (0x44766e)
    //   lea/sub/shl  -> 12*V       (0x44767c..0x447685)
    // The earlier reconstruction dropped the `12*V` term; restored here.
    int sampleCount = static_cast<int>(samples_.size());
    int variationCount = static_cast<int>(variations_.size());
    int totalNodes = sampleCount + variationCount;
    return kSubSampleStride * (subSampleCount + sampleCount) // 12*(S+C)
           + kSubSampleStride * variationCount               // 12*V
           + (totalNodes << 6)                               // (S+V)<<6
           + 324
           + baseSize;
}

SampleRecord& SampleBank::addSample(const std::string& name) {
    samples_.push_back(SampleRecord{});
    samples_.back().name = name;
    return samples_.back();
}

VariationRecord& SampleBank::addVariation(const std::string& name) {
    variations_.push_back(VariationRecord{});
    variations_.back().name = name;
    return variations_.back();
}

} // namespace guild::audio
