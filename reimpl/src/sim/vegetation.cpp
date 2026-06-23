#include "sim/vegetation.h"

#include <cstdio>
#include <cstring>

namespace guild::sim {

// ===========================================================================
// Hooks
// ===========================================================================
static const VegetationHooks g_inert{};
static const VegetationHooks* g_hooks = &g_inert;
void SetVegetationHooks(const VegetationHooks* hooks) { g_hooks = hooks ? hooks : &g_inert; }
const VegetationHooks& GetVegetationHooks() { return *g_hooks; }
void ResetVegetationHooks() { g_hooks = &g_inert; }

// ===========================================================================
// Path-select core (1:1).
//
// gilde.exe 0x56ec34..0x56ec4f / 0x56eecc..0x56eedf:
//   edx = plant.stagePacked >> 24      (arithmetic shift -> signed high byte)
//   esi = species.levelDivisor (byte +0x43)
//   eax = edx (sign-extended via cdq) / esi   (signed idiv, truncates toward 0)
//   if (eax < 1) eax = 1;
// The same computation is used in both the plain and the _FALL branch; the only
// difference between the branches is the format string suffix.
// ===========================================================================
i32 Veg_ComputeLevel(const VegPlantView& plant, const VegSpeciesRecord& species) {
    i32 stageHi = plant.stagePacked >> 24;  // ASR: signed high byte (-128..127)
    i32 divisor = static_cast<i32>(species.levelDivisor);
    // The original executes a signed idiv; divisor 0 would #DE in the binary, but
    // a real species record always has a nonzero +0x43. Guard to avoid UB while
    // preserving the result for every valid divisor.
    i32 level = (divisor != 0) ? (stageHi / divisor) : stageHi;
    if (level < 1)
        level = 1;
    return level;
}

std::string Veg_BuildModelPath(const std::string& dataDir,
                               const VegSpeciesRecord& species,
                               i32 level, bool fall) {
    // gilde.exe 0x625334 "%svegetation/*pfl_%s_0%i.ogr"
    //           0x625354 "%svegetation/*pfl_%s_0%i_FALL.ogr"
    char buf[256];
    const char* fmt = fall ? "%svegetation/*pfl_%s_0%i_FALL.ogr"
                           : "%svegetation/*pfl_%s_0%i.ogr";
    std::snprintf(buf, sizeof(buf), fmt, dataDir.c_str(), species.name.c_str(),
                  static_cast<int>(level));
    return std::string(buf);
}

// ===========================================================================
// VIBE_Plant_LoadVegetationModel  0x56ec14
// ===========================================================================
void* Plant_LoadVegetationModel(const VegPlantView& plant) {
    const VegetationHooks& h = *g_hooks;

    // species = FindOfficeTypeRecord(HIWORD(rec[2]))  (typeId).
    VegSpeciesRecord species;
    if (h.findSpecies)
        species = h.findSpecies(plant.typeId);

    bool fall = h.isFallSeason ? h.isFallSeason() : false;
    std::string dataDir = h.dataDir ? std::string(h.dataDir()) : std::string();

    i32 level = Veg_ComputeLevel(plant, species);
    std::string path = Veg_BuildModelPath(dataDir, species, level, fall);

    // grp = Scene_LoadObjectGroup(path, 0, 0, 0)
    void* node = h.loadObjectGroup ? h.loadObjectGroup(path.c_str()) : nullptr;
    if (!node) {
        // init_SetPflanzenMap(): Could not load 3D-Objekt-roup '%s'...
        char msg[256];
        std::snprintf(msg, sizeof(msg),
                      "init_SetPflanzenMap(): Could not load 3D-Objekt-roup '%s'...",
                      path.c_str());
        if (h.reportError)
            h.reportError(msg);
        return nullptr;
    }

    // rec[5] = node  (caller stores; we also signal via return).
    // suffix = "_ID%i" % rec[0]; appended onto the node's name buffer.
    char suffix[256];
    std::snprintf(suffix, sizeof(suffix), "_ID%i", static_cast<int>(plant.recId));
    if (h.appendNameSuffix)
        h.appendNameSuffix(node, suffix);

    // node[+0x218 dword] = 1
    if (h.markVegetationFlag)
        h.markVegetationFlag(node);

    // Placement: if the placement-ref object (dword_64A028) exists, compute the
    // world position from its 3x3 transform basis applied to the plot coords,
    // then SetPosition. (gilde.exe 0x56ece8..0x56eeb4.)
    const void* ref = h.placementRef ? h.placementRef() : nullptr;
    if (ref) {
        const int A = static_cast<int>(plant.plotX);  // (i16)rec+8
        const int B = static_cast<int>(plant.plotY);  // (i16)rec+9

        auto F = [&](int off) -> float {
            return h.placementFloat ? h.placementFloat(ref, off) : 0.0f;
        };

        // Each term is `fild word; fmul dword[coef]; fadd dword[acc]; fstp dword`
        // (0x56ed1b..0x56eea8): the (i16)*coef product and the +acc sum are kept in
        // the 80-bit x87 register and only rounded to a float on the final fstp.
        // Model that as a double product+sum stored back to float (NOT a float*float
        // multiply, which would round the product early).
        auto step = [](float acc, i16 v, float coef) -> float {
            return static_cast<float>(static_cast<double>(v) * static_cast<double>(coef)
                                      + static_cast<double>(acc));
        };

        float x = F(0x90);
        float y = F(0x94);
        float z = F(0x98);

        x = step(x, static_cast<i16>(A), F(0xA0));
        y = step(y, static_cast<i16>(A), F(0xA4));
        z = step(z, static_cast<i16>(A), F(0xA8));

        x = step(x, static_cast<i16>(B), F(0xB0));
        y = step(y, static_cast<i16>(B), F(0xB4));
        z = step(z, static_cast<i16>(B), F(0xB8));

        // The same index-map byte feeds all three remaining terms:
        //   idxByte = *(u8*)( ref[0]*B + A + ref[0x10] )
        u8 mb = h.placementMapByte ? h.placementMapByte(ref, A, B) : 0;
        x = step(x, static_cast<i16>(mb), F(0xC0));
        y = step(y, static_cast<i16>(mb), F(0xC4));
        z = step(z, static_cast<i16>(mb), F(0xC8));

        float pos[3] = {x, y, z};
        if (h.setPosition)
            h.setPosition(node, pos);
    }

    // Light_RequestObjectCache(node)
    if (h.lightRequestCache)
        h.lightRequestCache(node);

    return node;
}

}  // namespace guild::sim
