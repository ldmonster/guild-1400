#include "ai/favorability.h"

namespace guild::ai {

namespace {
// Recovered .rdata constants.
const float  kRelNorm  = 0.003921568859368563f;  // flt_626AA4  (1/255)
const float  kCeil     = 100.0f;                 // flt_626AA8
const float  kLawScale = 0.75f;                  // flt_626AAC
const float  kInv7     = 7.0f;                   // flt_626AB0
const float  kInv15    = 15.0f;                  // flt_626AB4
const float  kInv10    = 10.0f;                  // flt_626AB8
const float  kInv8     = 8.0f;                   // flt_626ABC
const double kAvgScale = 0.01;                   // dbl_626AC4

// The two intermediate clamps inside the applyLaw branch: if v <= 100 keep
// max(v,0), else snap to 100. Reproduces the (v<0 || 100>=v) ? max(v,0) : 100.
double ClampLaw(double v) {
    if (v < 0.0 || static_cast<double>(kCeil) >= v)
        return v >= 0.0 ? v : 0.0;
    return 100.0;
}
} // namespace

// gilde.exe 0x594330 — VIBE_Ai_ComputePersonFavorability
//   (__usercall: st0=ret, eax=self, edx=other, ebx=applyLaw)
double ComputePersonFavorability(int self, int other, bool applyLaw,
                                 FavorabilityEnv& env) {
    // if (self == other) return 100.0;
    if (self == other)
        return 100.0;

    FavPersonFields s = env.Person(self);

    // (1) self's own workstation worker count.
    double v39 = 0.0;
    if (s.workstationBuildingPtr)
        v39 = static_cast<double>(env.WorkstationWorkers(s.workstationBuildingPtr));

    // (2) office.kind==4 => add the "other"-good workstation worker count.
    if (s.officeId) {
        OfficeDefinition def = env.Office(s.officeId);
        if (def.kind == 4) {
            int b = env.QueryByGoodType(other);
            if (b)
                v39 += static_cast<double>(env.WorkstationWorkers(b));
        }
    }

    // (3) special title (30..33) => add the mapped worker query for `other`.
    u8 title = s.titleId;
    if (title >= 0x1Eu && title <= 0x21u) {
        int mapped = 0;
        switch (title) {
            case 30: mapped = 23; break;
            case 31: mapped = 24; break;
            case 32: mapped = 25; break;
            case 33: mapped = 26; break;
        }
        bool gateOk = false;
        int begin = env.QueryBeginWorkers(other, mapped, gateOk);
        if (begin && gateOk)
            v39 += static_cast<double>(env.WorkstationWorkers(begin));
    }

    // (4) base relation term: ((relByte) + 127) / 255 * 100.
    int relTerm = (s.relationByteSelf >> 24) + 127;
    double v40 = static_cast<double>(relTerm) * static_cast<double>(kRelNorm)
               * static_cast<double>(kCeil);
    v40 += v39;

    // (5) office relation weight for `other`, optionally law-adjusted.
    FavPersonFields o = env.Person(other);
    OfficeDefinition odef = env.Office(o.officeId);
    if (applyLaw) {
        float w = odef.weight;
        if (env.GesetzState() == 2)
            w = w * kLawScale;
        // Same faction high-byte => add the weight, else subtract; then clamp.
        if (s.factionHigh == o.factionHigh)
            v40 = ClampLaw(v40 + static_cast<double>(w));
        else
            v40 = ClampLaw(v40 - static_cast<double>(w));
    }

    // (6) office-tier inventory bonuses (probing SELF's inventory).
    if (o.officeId) {
        u8 tier = odef.tier;
        int inv = s.inventoryBase;
        if (tier >= 1 && tier < 4) {
            v40 += static_cast<double>(env.InventorySlot(inv, 341)) * kInv10;
            v40 += static_cast<double>(env.InventorySlot(inv, 349)) * kInv10;
        } else if (tier > 3 && tier < 8) {
            v40 += static_cast<double>(env.InventorySlot(inv, 343)) * kInv10;
            v40 += static_cast<double>(env.InventorySlot(inv, 351)) * kInv8;
        } else if (tier > 7 && tier < 0xA) {
            v40 += static_cast<double>(env.InventorySlot(inv, 345)) * kInv7;
            v40 += static_cast<double>(env.InventorySlot(inv, 357)) * kInv7;
            v40 += static_cast<double>(env.InventorySlot(inv, 358)) * kInv15;
        }
        // tier 0 or >= 10: no bonus (LABEL_30).
    }

    // (7) spouse bonus: if `other` is married and the spouse is `self`.
    if (o.spouseRecordPtr) {
        if (o.spousePartnerId == self)
            v40 += static_cast<double>(env.InventorySlot(s.inventoryBase, 363)) * kInv7;
    }

    // (8) guild-rank penalties (on SELF's guild bit word).
    int gb = s.guildBitsLow;
    if ((gb & 0x1C000) != 0 && s.rankHigh != o.rankHigh) {
        int pen = 3 * static_cast<int>((static_cast<unsigned>(gb) << 15) >> 29);  // bits 14-16
        v40 -= static_cast<double>(pen);
    }
    if ((s.guildBitsLow & 0x1800000) != 0) {
        int pen = 3 * static_cast<int>((static_cast<unsigned>(gb) << 7) >> 30);   // bits 23-24
        v40 -= static_cast<double>(pen);
    }

    // (9) final clamp to [0, 100].
    if (v40 < static_cast<double>(kCeil) && v40 <= 0.0)
        return 0.0;
    if (v40 >= static_cast<double>(kCeil))
        return 100.0;
    return v40;
}

// gilde.exe 0x594928 — VIBE_Ai_AverageObjectFavorability
//   (__usercall: st0=ret, eax=self, edx=count, ebx=ids)
double AverageObjectFavorability(int self, const int* ids, int count,
                                 FavorabilityEnv& env,
                                 int (*resolve)(int rawId)) {
    int resolved = 0;          // v5
    double sum = 0.0;          // i
    for (int k = 0; k < count; ++k) {
        if (ids[k] != -1) {
            int pid = resolve(ids[k]);  // VIBE_Person_FindRecordById -> person id
            if (pid != -1) {
                ++resolved;
                sum += ComputePersonFavorability(self, pid, false, env);
            }
        }
    }
    if (!resolved)
        return 0.5;
    return static_cast<float>(sum * kAvgScale / static_cast<double>(resolved));
}

} // namespace guild::ai
