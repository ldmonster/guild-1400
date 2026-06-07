#include "world/amt_economy2.h"

#include <cstdio>
#include <cstring>

// Faithful 1:1 ports of the Amt office-administration leaves. Table lookups,
// command/network commits, RNG and coord-truncation route through the installed
// AmtEconomy2Hooks (inert defaults below). See amt_economy2.h for per-function
// addresses and the recovered constants.

namespace guild::world {

namespace {
AmtEconomy2Hooks g_hooks{};

// Default coord truncation == VIBE_Coord_ConvertX (round-toward-zero of st0).
i32 DefaultTruncate(double v) { return static_cast<i32>(v); }
} // namespace

void AmtEconomy2SetHooks(const AmtEconomy2Hooks& hooks) { g_hooks = hooks; }
const AmtEconomy2Hooks& AmtEconomy2GetHooks() { return g_hooks; }
void AmtEconomy2ResetHooks() { g_hooks = AmtEconomy2Hooks{}; }

namespace {
// Resolve the truncation hook (defaulting to round-toward-zero).
i32 Trunc(double v) {
    return g_hooks.truncate ? g_hooks.truncate(v) : DefaultTruncate(v);
}
float RandRoll() {
    return g_hooks.randFloatScaled ? g_hooks.randFloatScaled() : 0.0f;
}
} // namespace

// ===========================================================================
// VIBE_Amt_TriggerOfficeNotice (0x483570)
//   result = 0; while (byte_B59850[result] != a1) { result += 24; if >= 720 return; }
//   return VIBE_Office_AddTableEntry(byte_B59848[result], 0, 3, 0, 255);
// byte_B59850 is the type byte (+8) of the 24-byte slot record; the scan walks
// raw byte index in steps of 24 (== one record). On a match the slot's holder key
// (byte_B59848, +0) is re-posted vacant (state 3).
// ===========================================================================
i32 TriggerOfficeNotice(u8 officeType, const OfficeHolder* slots, int slotCount) {
    int byteIndex = 0;
    const int span = slotCount * kOfficeHolderStride;
    while (byteIndex < span && byteIndex < kSlotByteSpan) {
        const OfficeHolder& slot = slots[byteIndex / kOfficeHolderStride];
        if (slot.type == officeType) {
            if (g_hooks.officeAddTableEntry)
                return g_hooks.officeAddTableEntry(slot.holder, 0, 3, 0, 255);
            return -1;
        }
        byteIndex += kOfficeHolderStride;
    }
    return byteIndex; // terminal byte index (>= span / 720) — no match
}

// ===========================================================================
// VIBE_Amt_ResetGuildSlots (0x480b50)
// Phase 1: for each of 30 slots (byte index 0..720 step 24):
//   rec = FindRecordById(dword_B5984C[i/4]);            // the +4 holder id
//   if (rec && rec.present(+8)):                        // active holder
//       if (state(+16) != 1) AddTableEntry(holder, recId, 1, 0, 255);
//   else:                                               // vacant holder
//       if (state(+16) != 3) AddTableEntry(holder, 0, 3, 0, 255);
//       if (rec && rec.dirty(+358)) commit delta field 0x166;
// Phase 2: for each dirty building, commit delta field 0x168.
// (The original carries the "state-1 path needs an active record" guard: the
// state-1 AddTableEntry only fires inside the rec-present branch.)
// ===========================================================================
ResetGuildSlotsResult ResetGuildSlots(const OfficeHolder* slots, int slotCount,
                                      const bool* buildingDirty, int buildingCount) {
    ResetGuildSlotsResult r;
    const int slots30 = slotCount < 30 ? slotCount : 30;

    for (int idx = 0; idx < slots30; ++idx) {
        const OfficeHolder& slot = slots[idx];
        bool present = false, dirty = false;
        bool found = false;
        if (g_hooks.personFind)
            found = g_hooks.personFind(slot.city, &present, &dirty);

        if (found && present) {
            if (slot.state != 1) {
                if (g_hooks.officeAddTableEntry)
                    g_hooks.officeAddTableEntry(slot.holder, slot.city, 1, 0, 255);
                ++r.slotAssignsQueued;
            }
        } else {
            if (slot.state != 3) {
                if (g_hooks.officeAddTableEntry)
                    g_hooks.officeAddTableEntry(slot.holder, 0, 3, 0, 255);
                ++r.slotVacanciesQueued;
            }
            if (found && dirty) {
                if (g_hooks.queueDeltaFlag)
                    g_hooks.queueDeltaFlag(slot.city, 0x166);
                ++r.holderFlagsCommitted;
            }
        }
    }

    for (int b = 0; b < buildingCount; ++b) {
        if (buildingDirty[b]) {
            if (g_hooks.queueDeltaFlag)
                g_hooks.queueDeltaFlag(b, 0x168);
            ++r.buildingFlagsCommitted;
        }
    }
    return r;
}

// ===========================================================================
// VIBE_Amt_HighlightGuildMembers (0x48311c)
//   if (1 <= category <= 7):
//     n = CollectByCategory(category, 6, buf);          // resolved holder ids
//     for each entry: id = buf[+4]; if (id != self(+4) && id != -1)
//         QueueRequestCoord27(self, id, arg);
// The collect is the caller's job (it owns the category table); we take the
// resolved id list and faithfully reproduce the dedup + emit loop.
// ===========================================================================
int HighlightGuildMembers(i32 selfId, u8 category, int arg,
                          const i32* holderIds, int collected) {
    int queued = 0;
    if (category >= 1 && category <= 7 && collected > 0) {
        for (int i = 0; i < collected; ++i) {
            const i32 id = holderIds[i];
            if (id != selfId && id != -1) {
                if (g_hooks.queueCoord27)
                    g_hooks.queueCoord27(selfId, id, arg);
                ++queued;
            }
        }
    }
    return queued;
}

// ===========================================================================
// VIBE_Amt_BuildOfficeInfoText (0x483414)
//   if (rank(+13) >= 2):
//     n = BuildPromotionList(rec, 6, list);
//     if (n):
//        render header into buf; for each list entry render "%s$A" of
//        text(list[i]); SendEntityMessage(holderId, buf).
// We keep the rank gate + the empty-list gate + the per-entry text concat (the
// "$A" separator) and the single SendEntityMessage; the form/text renderer is
// abstracted to a caller string resolver.
// ===========================================================================
bool BuildOfficeInfoText(i32 holderId, u8 rankByte, int promotionCount,
                         const u8* entryIndices,
                         const char* (*entryText)(u8 index, void* ctx), void* ctx) {
    if (rankByte < 2)
        return false;
    if (promotionCount <= 0)
        return false;

    char buf[2048];
    buf[0] = '\0';
    std::size_t len = 0;
    for (int i = 0; i < promotionCount; ++i) {
        const char* s = entryText ? entryText(entryIndices[i], ctx) : "";
        // "%s$A" — entry text followed by the "$A" separator token.
        const std::size_t sl = std::strlen(s);
        if (len + sl + 2 < sizeof(buf)) {
            std::memcpy(buf + len, s, sl);
            len += sl;
            buf[len++] = '$';
            buf[len++] = 'A';
            buf[len] = '\0';
        }
    }
    if (g_hooks.sendEntityMessage)
        g_hooks.sendEntityMessage(holderId, buf);
    return true;
}

// ===========================================================================
// VIBE_Amt_ComputeOfficeRenderOffset (0x4834e4)
//   GetRecord(14, &law);  stretch = flt_47DE10[law.field0];
//   x = Trunc(stretch);                                  // ConvertX(stretch)
//   acc = 0;
//   for each slot (byte 0..888 step 24):                 // 37 records scanned
//     if (dword_B5984C[i] != -1):                        // occupied (+4)
//        tiles = (i16)byte_62EC93[12*byte_B59850[i]];    // per-type tile count
//        step  = Trunc(tiles * flt_61AFC8 * flt_61AFCC * x);
//        acc  += step;
//   return acc;
// (The original scans 888 bytes == 37 records for the marker layout; we honor
// the caller-supplied slotCount, capped at 37.)
// ===========================================================================
i32 ComputeOfficeRenderOffset(int baseX, u8 lawLevel,
                              const OfficeHolder* slots, int slotCount) {
    (void)baseX;
    const float stretch = kRenderStretch[lawLevel & 3];
    const i32 x = Trunc(static_cast<double>(stretch));

    i32 acc = 0;
    const int n = slotCount < 37 ? slotCount : 37;
    for (int idx = 0; idx < n; ++idx) {
        const OfficeHolder& slot = slots[idx];
        if (slot.city != -1) {
            const u8 type = slot.type;
            const u8 tiles = (type < 9) ? kOfficeTileCount[type] : 0;
            const double step = static_cast<double>(static_cast<i16>(tiles))
                              * static_cast<double>(kRenderPitchA)
                              * static_cast<double>(kRenderPitchB)
                              * static_cast<double>(x);
            acc += Trunc(step);
        }
    }
    return acc;
}

// ===========================================================================
// VIBE_Amt_FindNextActiveBuilding (0x57bb50)
//   if (!cache || generation < gen || cachedWord==0xFFFF || !cachedActive):
//      best = -100000; bestId = 0xFFFF; cache->gen = gen;
//      for v6 in 0..768: if (word[v6]!=-1 && type<10 && active):
//          w = ComputeTotalWealth(v6); if (w > best) { best=w; bestId=v6; }
//      if (bestId == 0xFFFF) { cache invalid; wealth=3200; -> return 0; }
//      else { cache->wealth=best; cache->valid=true; cachedId=bestId; }
//   if (cache valid) { *outId = cachedId; *outWealth = cachedWealth; return 1; }
//   else { *outId = -1; *outWealth = cachedWealth; return 0; }
// The wealth lookup per building is taken from the caller's pre-computed view
// (BuildingScanEntry.wealth) so the scan is deterministic in tests.
// ===========================================================================
int FindNextActiveBuilding(ActiveBuildingCache* cache, i32 generation,
                           const BuildingScanEntry* buildings, int count,
                           i32* outId, i32* outWealth) {
    const bool stale = !cache->cachedValid
                     || generation < cache->generation
                     || cache->cachedTypeWord == 0xFF
                     || !cache->cachedActive;
    if (stale) {
        i32 best = -100000;
        i32 bestId = 0xFFFF;
        cache->generation = generation;
        const int n = count < 768 ? count : 768;
        for (int v6 = 0; v6 < n; ++v6) {
            const BuildingScanEntry& b = buildings[v6];
            if (b.id != -1 && b.type < 10 && b.active) {
                if (b.wealth > best) {
                    best = b.wealth;
                    bestId = v6;
                }
            }
        }
        if (bestId == 0xFFFF) {
            cache->cachedValid = false;
            cache->cachedWealth = 3200;
            *outId = -1;
            *outWealth = cache->cachedWealth;
            return 0;
        }
        cache->cachedWealth = best;
        cache->cachedValid = true;
        cache->cachedId = buildings[bestId].id;
        cache->cachedTypeWord = buildings[bestId].type;
        cache->cachedActive = buildings[bestId].active;
    }

    if (cache->cachedValid) {
        *outId = cache->cachedId;
        *outWealth = cache->cachedWealth;
        return 1;
    }
    *outId = -1;
    *outWealth = cache->cachedWealth;
    return 0;
}

// ===========================================================================
// VIBE_Amt_ComputeBuildingRivalryScore (0x57bc60)
// `self` is the acting building; `rivals` is the building table walk. For each
// rival (id != -1, type not in {6,7,8} and < 10, different cityId than self):
//   base   = (i16)rival.workForce(+432) * flt_6258C8;        // 0.25 scale
//   supply = self.workstationSum * flt_6258C4 + 1.0;         // 0.0125 scale
//   if (rival.cityRegion == self.cityRegion):
//        repDelta = +0.05;  base *= supply;
//   else:
//        f = (self.cityRegion == 2) ? 0.8 : (rival.cityRegion == self.cityRegion2 ? 0.7 : 0.6);
//        base = f * supply * base;  repDelta = -0.05;
//   if (RandomFloatScaled() * 2.0 <= base):                  // roll won
//        if (rival.wealth / self.wealth * 160 > 0)
//             wf = rival.wealth / self.wealth * 160;
//        else wf = 0;
//        payout = Trunc((wf + 160) * base);  total += payout;
//        QueueRequest16(-1, rival.id, payout, currency);
//        ++count;
//        if (0.05 < rival.reputation(+460) < 0.95)
//             QueueRequestArgs26(rival.id, 460, repDelta);
//        if (self.payoutCoordFlag):
//             QueueRequestCoord27 payout coord (RatingCurveA(4)*10+1).
// Returns {count, total}.  (self.cityRegion2 is the v19 the active-building scan
// produced; self.cityRegion / .cityRegion2 / workForce etc. are caller view.)
// ===========================================================================
RivalryResult ComputeBuildingRivalryScore(const RivalrySelf& self,
                                          const RivalryRival* rivals, int count,
                                          int currency) {
    RivalryResult r;
    const float supply = self.workstationSum * kRivalrySupplyScale + 1.0f;

    for (int i = 0; i < count; ++i) {
        const RivalryRival& rv = rivals[i];
        if (rv.id == -1)
            continue;
        if (rv.type == 6 || rv.type == 7 || rv.type == 8 || rv.type >= 10)
            continue;
        if (rv.cityId == self.cityId)
            continue;

        double base = static_cast<double>(rv.workForce) * kRivalryDistanceScale;
        float repDelta;
        if (rv.cityRegion == self.cityRegion) {
            repDelta = kRivalryRepUp;
            base = base * supply;
        } else {
            double f;
            if (self.cityRegion == 2)
                f = kRivalryFarCity;       // dbl_6258DC (0.8)
            else if (rv.cityRegion == self.cityRegion2)
                f = kRivalryAdjacentCity;  // dbl_6258D4 (0.7)
            else
                f = kRivalrySameCity;      // dbl_6258CC (0.6)
            base = f * supply * base;
            repDelta = kRivalryRepDown;
        }

        if (RandRoll() * kRivalryRollScale <= base) {
            double wf;
            const double ratio = static_cast<double>(rv.wealth)
                               / static_cast<double>(self.wealth)
                               * kRivalryWealthScale;
            if (ratio > 0.0)
                wf = static_cast<double>(rv.wealth)
                   / static_cast<double>(self.wealth)
                   * kRivalryWealthScale;
            else
                wf = 0.0;

            const i32 payout = Trunc((wf + kRivalryWealthScale) * base);
            r.totalPayout += payout;
            if (g_hooks.queueRequest16)
                g_hooks.queueRequest16(-1, rv.id, payout, currency);
            ++r.rivalsPaid;

            if (rv.reputation < kRivalryRepHigh && rv.reputation > kRivalryRepLow) {
                if (g_hooks.queueArgs26)
                    g_hooks.queueArgs26(rv.id, 460, repDelta);
            }
            if (self.payoutCoordFlag) {
                const double coord =
                    static_cast<double>(self.ratingCurveA) * kRivalryPayoutScale + 1.0;
                const i32 cv = Trunc(coord);
                if (g_hooks.queueCoord27)
                    g_hooks.queueCoord27(rv.id, cv, 0);
            }
        }
    }
    return r;
}

// ===========================================================================
// VIBE_City_LookupSelectionInfoText (0x507b18)
//   sprintf(key, "_STADTAUSWAHL_%s_INFO+0", a1);
//   StrToUpper(key);
//   idx = FindTextArrayIndex(key); src = (idx!=-1) ? textArray[idx] : a1;
//   do { *out = *src; if (!*src) break; out[1]=src[1]; src+=2; out+=2; } while(src[-1]);
// The 2-byte widen copy faithfully reproduces the original byte-pair stride.
// ===========================================================================
int LookupSelectionInfoText(const char* key, char* out,
                            const char* (*lookup)(const char* upperKey, void* ctx),
                            void* ctx) {
    char keyBuf[256];
    std::snprintf(keyBuf, sizeof(keyBuf), "_STADTAUSWAHL_%s_INFO+0", key);
    for (char* p = keyBuf; *p; ++p) {
        if (*p >= 'a' && *p <= 'z')
            *p = static_cast<char>(*p - 'a' + 'A');
    }

    const char* src = lookup ? lookup(keyBuf, ctx) : key;
    if (!src)
        src = key;

    // 2-byte stride widen copy: copy src[0], src[1] pairs until a NUL terminator.
    for (;;) {
        const char c0 = src[0];
        out[0] = c0;
        if (!c0)
            break;
        const char c1 = src[1];
        out[1] = c1;
        src += 2;
        out += 2;
        if (!c1)
            break;
    }
    return 1;
}

// ===========================================================================
// VIBE_Amt_OpenOfficeWindow (0x5546a0)
//   SetGrayColorThunk(0, 40); v5[2]=?; v5[1]=516; v6=6;
//   return RunCandidateSelectionWindow(v5, a2, 0, 0);
// The window run is deferred (pure UI); return the prepared arg block.
// ===========================================================================
OfficeWindowArgs OpenOfficeWindow(int a1, int a2) {
    (void)a2;
    OfficeWindowArgs args;
    args.slot0 = a1;        // v5[2] is primed from ecx (the caller's a1 path)
    args.windowId = 516;    // v5[1]
    args.officeType = 6;    // v6
    return args;
}

} // namespace guild::world
