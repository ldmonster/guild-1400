#include "sim/building_lifecycle.h"

#include <cmath>
#include <cstring>

#include "sim/building.h"             // BuildingTypeDefAt, Building_IsProductionType
#include "sim/building_production.h"  // (type table)
#include "sim/gametime.h"             // GameTime (qword_13CE852 image)
#include "sim/npcaction.h"            // NpcClock() — the global game clock (13CE852)
#include "util/coord.h"               // ConvertX (truncate toward zero)
#include "util/math.h"                // VectorWithinTolerance (0x5caa4c)

namespace guild::sim {

BuildingPersonRec g_buildingPersons[kBuildingSlots];

BuildingPersonRec* BuildingPersonAt(int slot) {
    if (slot < 0 || slot >= kBuildingSlots)
        return nullptr;
    return &g_buildingPersons[slot];
}

static ILifecycleHooks  g_defaultLifecycleHooks;
static ILifecycleHooks* g_lifecycleHooks = &g_defaultLifecycleHooks;
void SetLifecycleHooks(ILifecycleHooks* hooks) {
    g_lifecycleHooks = hooks ? hooks : &g_defaultLifecycleHooks;
}
ILifecycleHooks* LifecycleHooks() { return g_lifecycleHooks; }

static int g_buildCounter = 0;   // dword_647724
void SetBuildCounter(int v) { g_buildCounter = v; }
int  BuildCounter() { return g_buildCounter; }

void ResetBuildingPersons() {
    std::memset(g_buildingPersons, 0, sizeof(g_buildingPersons));
    // free slots are marker == -1 in the original.
    for (auto& r : g_buildingPersons)
        r.marker = -1;
    g_buildCounter = 0;
}

// The building's raw type code is the low byte of the +0 marker word (the
// original's `*v2` / `*i` reads the byte at +0). Aliased for the type-table read.
static inline u8 RawType(const BuildingPersonRec& r) {
    return static_cast<u8>(r.marker & 0xFF);
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5894b0 — VIBE_Building_RemoveAndCleanup
// ---------------------------------------------------------------------------
void Building_RemoveAndCleanup(int slot, bool freeSlot) {
    BuildingPersonRec* v2 = BuildingPersonAt(slot);
    if (!v2)
        return;

    if (v2->kind != 15) {
        // mark inactive + stamp removal time, release holdings, mark destroyed.
        // 0x58960b/0x58960f: *(v2+8)=0; v2[20] = (WORD)qword_13CE852 (game clock
        // low word == LOWORD(day)). Stamp the live clock, not 0.
        v2->activeFlag = 0;
        v2->removalTs  = static_cast<u16>(NpcClock().day);   // qword_13CE852 low word
        g_lifecycleHooks->ReleaseOccupantHoldings(slot);
        v2->kind = 15;

        // Clear trade routes pointing at this building; the hook returns the
        // count that hit a sibling owner (drives the rival notification).
        i32 buildingId;
        std::memcpy(&buildingId, reinterpret_cast<u8*>(v2) + 4, sizeof(i32));
        int siblingHits = g_lifecycleHooks->ClearTradeRoutes(buildingId);
        if (siblingHits)
            g_lifecycleHooks->NotifyRivalEvent(-1, slot);
    }

    g_lifecycleHooks->FreeChildList(slot);

    if (v2->charHandle) {
        g_lifecycleHooks->DestroyCharacter(v2->charHandle);
        v2->charHandle = 0;
    }

    if (v2->typeRecord) {
        g_lifecycleHooks->DecrementTypeActiveCount(v2->typeRecord);
        v2->typeRecord = 0;
    }

    if (freeSlot) {
        if (v2->marker != -1)
            --g_buildCounter;
        // free the slot: marker = -1.
        v2->marker = -1;
    }
}

// ---------------------------------------------------------------------------
// gilde.exe 0x587908 — VIBE_Building_FindNearestSameType
//   for each building i (Person query kind 6):
//       td = typeDef[*i];
//       if !IsProductionType(i) && td.kind != 10 && i has object && i != self
//          && td.kind == typeCode:
//             d = distance(self, i); track nearest.
//   `selfHasObject` is the *(a1+97) gate; the slot must have a live object.
// ---------------------------------------------------------------------------
int Building_FindNearestSameType(int slot, u8 typeCode) {
    BuildingPersonRec* self = BuildingPersonAt(slot);
    if (!self)
        return 0;

    int best = 0;
    // 0x58791b: *(_DWORD *)v17 = 1287568416 == 0x4CBF2360 == 100000000.0f (1e8),
    // NOT 1e5. Verified via get_bytes/IEEE754 decode.
    float bestDist = 100000000.0f;

    for (int i = 0; i < kBuildingSlots; ++i) {
        if (i == slot)
            continue;
        BuildingPersonRec& r = g_buildingPersons[i];
        if (r.marker == -1 || r.kind == 15)
            continue;

        u8 raw = RawType(r);
        const BuildingTypeDef* td = BuildingTypeDefAt(raw);
        if (!td)
            continue;

        // skip production + storage(10) buildings.
        if (Building_IsProductionKind(td->kind) || td->kind == 10)
            continue;

        if (td->kind != typeCode)
            continue;

        float d = std::sqrt(g_lifecycleHooks->BuildingDistanceSq(slot, i));
        if (d < bestDist) {
            bestDist = d;
            best = i;
        }
    }
    return best;
}

// ===========================================================================
// WAVE-19 — build/upgrade lifecycle + bauplatz geometry
// ===========================================================================

// ---------------------------------------------------------------------------
// gilde.exe 0x5878b0 — VIBE_Building_MapTypeToCategory.  Pure switch on the
// type-record KIND byte (the original reads *(dword_13CE294 + 589*type) and
// switches; we take the kind byte directly).
// ---------------------------------------------------------------------------
u8 Building_MapTypeToCategory(u8 kindByte) {
    switch (kindByte) {
        case 1: case 3: case 6: case 0xF: return 3;
        case 2:                            return 6;
        case 4: case 5: case 9:            return 8;
        case 7:                            return 4;
        case 8: case 0xE: case 0x12: case 0x14: case 0x15: case 0x16: return 1;
        case 0xB: case 0xC: case 0xD:      return 2;
        case 0x13:                         return 7;
        case 0x17: case 0x18: case 0x19: case 0x1A: return 5;
        default:                           return 0;
    }
}

// Building_ClassifyTypeFlag (0x589818) and BuildingType_GroupFromCode (0x58a4c8)
// are defined in building_type.cpp (declared via sim/building_type.h). They were
// previously duplicated here; the duplicates are removed to avoid an ODR clash.
// The canonical GroupFromCode is 1:1 with the 0x58a4c8 decompile: the 52-57 band
// returns 10 (JUMPOUT 0x58A27B) and 58-63 returns 2 (JUMPOUT 0x589850, a literal
// `mov al,2; ret`) — the earlier copy here wrongly tail-called ClassifyTypeFlag.

// ---------------------------------------------------------------------------
// Hook plumbing
// ---------------------------------------------------------------------------
static IBuildLifecycleHooks  g_defaultBuildHooks;
static IBuildLifecycleHooks* g_buildHooks = &g_defaultBuildHooks;
void SetBuildLifecycleHooks(IBuildLifecycleHooks* h) {
    g_buildHooks = h ? h : &g_defaultBuildHooks;
}
IBuildLifecycleHooks* BuildLifecycleHooks() { return g_buildHooks; }

static IBuildingFrameHooks  g_defaultFrameHooks;
static IBuildingFrameHooks* g_frameHooks = &g_defaultFrameHooks;
void SetBuildingFrameHooks(IBuildingFrameHooks* h) {
    g_frameHooks = h ? h : &g_defaultFrameHooks;
}

static int g_upgradeWindowHandle = 0;   // dword_13CE288
void SetUpgradeWindowHandle(int h) { g_upgradeWindowHandle = h; }
int  UpgradeWindowHandle() { return g_upgradeWindowHandle; }

// VIBE_Building_MapActionToCategory (0x58a25c) — used by CheckBuildRequirements.
// The original's switch with its JUMPOUT epilogues resolves to:
//   4->11 | 5->9 | 7->7 | 8->6 | 9->8 | 14->5 | 16->12 | 18->1 | 19->10 |
//   20->ClassifyTypeFlag | 21->3 | 22->4 | default->ClassifyTypeFlag(kind).
// (The case-5/7/8/9/14/20/22 JUMPOUTs land on the same epilogues decoded above:
//  0x589aa7=9, 0x589a38=7, 0x589c74=6, 0x589aa4=8, 0x589aa1=5, 0x589850=
//  ClassifyTypeFlag, 0x589c77=3, 0x589a35=4. default == 0x5898e2 xor al,al = 0.)
static u8 MapActionToCategory(u8 kind) {
    switch (kind) {
        case 4:  return 11;
        case 5:  return 9;
        case 7:  return 7;
        case 8:  return 6;
        case 9:  return 8;
        case 14: return 5;
        case 16: return 12;
        case 18: return 1;
        case 19: return 10;
        case 20: return Building_ClassifyTypeFlag(kind);
        case 21: return 3;
        case 22: return 4;
        default: return 0;   // 0x5898e2 xor al,al
    }
}

// ---------------------------------------------------------------------------
// gilde.exe 0x587bfc — VIBE_Building_CheckBuildRequirements.
//   Translated 1:1: the v5/v6 selector pair come from MapActionToCategory's
//   computed code; the original keeps `v5` (the action category) and `v6` (its
//   high byte == the level/group nibble for case 6).  We resolve v5 = category =
//   MapTypeToCategory(srcCode) (the original calls MapTypeToCategory(a1) FIRST,
//   then maps the record kind through MapActionToCategory), and the inner scans
//   walk record.roomList[547..552] (the +547 6-wide array) comparing against
//   objCat; the CollectByType fallback re-scans collected siblings' records.
// ---------------------------------------------------------------------------
static u8 ScanRoomList(const BuildTypeRecord* rec, u8 objCat) {
    // for (n=0; n<6 && rec.roomList[n]; ++n) if (objCat==rec.roomList[n]) return 1;
    // returns 1 on hit, 0 on miss/empty.
    if (!rec || !rec->roomList[0]) return 0;
    for (int n = 0; n < 6; ++n) {
        if (objCat == rec->roomList[n]) return 1;
        if (n + 1 >= 6 || !rec->roomList[n + 1]) break;
    }
    return 0;
}

u8 Building_CheckBuildRequirements(u8 srcCode, int targetSlot) {
    IBuildLifecycleHooks* h = g_buildHooks;

    // v5 (cl) = MapTypeToCategory(a1) — the SOURCE category selector (outer switch).
    u8 category = Building_MapTypeToCategory(srcCode);
    // record = 589*srcCode + dword_13CE294
    const BuildTypeRecord* rec = h->TypeRecord(srcCode);
    u8 recKind = rec ? rec->kind : 0;
    // VIBE_BuildingType_MapActionToCategory(*record) is called for its SIDE EFFECT
    // only in the original (result discarded); reproduced to match call order.
    (void)MapActionToCategory(recKind);
    // bl = GetCategoryForObject(targetSlot).
    u8 objCat = h->CategoryForObject(targetSlot);

    if (targetSlot == 0xFFFF) return 0;

    // The FreeBuildEverywhere (dword_63C7B8) cheat: target kind 6/7 -> always 1.
    if (h->FreeBuildEverywhere()) {
        u8 k = h->PersonKindByte2(targetSlot);
        if (k == 6 || k == 7) return 1;
    }

    switch (category) {
        case 1: {
            if (ScanRoomList(rec, objCat)) return 1;
            std::vector<u8> collected;
            int cnt = h->CollectByType(recKind, collected);
            for (int i = 0; i < cnt; ++i) {
                const BuildTypeRecord* sib = h->TypeRecord(collected[i]);
                if (sib && sib->roomList[0] && ScanRoomList(sib, objCat)) return 2;
            }
            return 0;
        }
        case 2:
            return 0;
        case 3: {
            u8 st = h->PersonStateByte358(targetSlot);
            if (st != 15) return (st == 10 && recKind == 3) ? 1 : 0;
            if (recKind == 1) return 1;
            if (recKind == 15 || recKind == 6) return 1;
            return (st == 10 && recKind == 3) ? 1 : 0;
        }
        case 7: {
            if (recKind != 19) return 0;
            if (ScanRoomList(rec, objCat)) return 1;
            std::vector<u8> collected;
            int cnt = h->CollectByType(recKind, collected);
            for (int i = 0; i < cnt; ++i) {
                const BuildTypeRecord* sib = h->TypeRecord(collected[i]);
                if (sib && sib->roomList[0] && ScanRoomList(sib, objCat)) return 2;
            }
            return 0;
        }
        case 4:
            return ScanRoomList(rec, objCat) ? 1 : 0;
        case 6: {
            // v6 == ch == the ORIGINAL srcCode (a1). Map it to a level threshold:
            //   4->1 | 5->3 | 6->4 | 7->5 | else->0.  Then if person+13 < thr -> 2.
            u8 thr = 0;
            switch (srcCode) {
                case 4: thr = 1; break;
                case 5: thr = 3; break;
                case 6: thr = 4; break;
                case 7: thr = 5; break;
                default: break;
            }
            if (h->PersonLevelByte13(targetSlot) < thr) return 2;
            return 1;
        }
        case 8: {
            // 0x587eb7: a DIRECT roomList match `break`s out -> return 1; otherwise
            // (no roomList or scan exhausted) the CollectByType fallback returns 2
            // on a sibling hit and 0 when collect is empty/no match (0x587ef4 /
            // 0x587f44 return 0). The earlier `return 1` fall-through was WRONG.
            if (ScanRoomList(rec, objCat)) return 1;
            std::vector<u8> collected;
            int cnt = h->CollectByType(recKind, collected);
            for (int i = 0; i < cnt; ++i) {
                const BuildTypeRecord* sib = h->TypeRecord(collected[i]);
                if (sib && sib->roomList[0] && ScanRoomList(sib, objCat)) return 2;
            }
            return 0;   // collect empty / no sibling hit -> fail (0x587ef4/0x587f44)
        }
        default:
            return 1;   // the outer default switch's tail `return 1`
    }
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5942b0 — VIBE_Building_CloseUpgradeWindow.
// ---------------------------------------------------------------------------
int Building_CloseUpgradeWindow() {
    // VIBE_Form_Destroy(dword_13CE288) — the GUI leaf destroys the window; we
    // surface + clear the handle so the caller can drive the real Form_Destroy.
    int handle = g_upgradeWindowHandle;
    g_upgradeWindowHandle = 0;
    return handle;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x40e2b4 — VIBE_Building_Update.
//   v3 = State_Update(dword_62D2A8); return Animation_Basic(*v4,v5,v3,a2,5).
//   The state tick result feeds the animation step; ordering preserved.
// ---------------------------------------------------------------------------
int Building_Update(int obj, int dt) {
    int stateResult = g_frameHooks->StateUpdate(0 /*dword_62D2A8*/);
    return g_frameHooks->AnimationBasic(obj, 0, stateResult, dt);
}

// ---------------------------------------------------------------------------
// gilde.exe 0x50f7c0 — the OpenUpgradeWindow tech-upgrade charge math.
//   v53 = (double)guildLevel * dbl_621620 + dbl_621628;     (0.25*lvl + 0.5)
//   v18 = ComputeMarketPrice(type,100); ConvertX(); v56=(int)v18;  (TRUNCATE)
//   v19 = (double)v56 * v53;            ConvertX(); v56=(int)v19;  (TRUNCATE)
//   -> v56 is the charge amount.
// ---------------------------------------------------------------------------
int Building_ComputeUpgradeChargeAmount(double marketPrice, int guildLevel) {
    double factor = (double)guildLevel * kUpgradeFactorSlope + kUpgradeFactorOffset;
    int base = (int)util::ConvertX(marketPrice);          // ConvertX truncates
    int amount = (int)util::ConvertX((double)base * factor);
    return amount;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x59361c — per-node upgrade-tree STATE classifier (v40 in {0,1,2,3}).
//   Reproduced from the two symmetric branches (office-handler type vs normal):
//     own?       -> 1 (and if a build handler matches -> 3)
//     prereq met? -> 2  (count==2, or count met w/ the prereqA/B flags)
//     handler active for buildable -> 3
//     else        -> 0
// ---------------------------------------------------------------------------
u8 Building_ClassifyUpgradeNode(const UpgradeTreeNode& node, int prereqMetCount,
                                IUpgradeTreeHooks* hooks) {
    IUpgradeTreeHooks dflt;
    IUpgradeTreeHooks* h = hooks ? hooks : &dflt;

    u8 v40 = 0;
    if (h->IsHandlerType(node.typeCode)) {
        // office/handler node (type record byte +0 == 6)
        if (h->BuildingOwns(node.typeCode)) {
            v40 = 1;                                   // owned/available
            if (h->HandlerActiveFor(node.typeCode))   // matching active handler
                v40 = 3;                               // in progress
        } else {
            // buildable iff the prereq count is satisfied for this node.
            if (prereqMetCount == 0 && !node.prereqAReq)        v40 = 2;
            else if (prereqMetCount == 1 && !node.prereqBReq)   v40 = 2;
            else if (prereqMetCount == 2)                       v40 = 2;
        }
    } else {
        // normal tech node.
        if (h->ObjectOwns(node.typeCode) || h->BuildingOwns(node.typeCode)) {
            v40 = 1;
        } else {
            bool buildable =
                (prereqMetCount == 0 && !node.prereqAReq) ||
                (prereqMetCount == 1 && !node.prereqBReq) ||
                (prereqMetCount == 2);
            if (buildable) {
                v40 = 2;
                if (h->HandlerActiveFor(node.typeCode)) v40 = 3;  // LABEL_62
            }
        }
    }
    return v40;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x50cb4c — bauplatz adjacency geometry core.
//   Original: for each owned building (Person_QueryBegin kind 6) with a live
//   object, PointThroughBoneChain the building's anchor and test
//   VectorWithinTolerance(buildingPos, plotPos, 100.0). Connected == a hit.
// ---------------------------------------------------------------------------
bool Building_PlotHasAdjacentOwned(const float anchor[3],
                                   const float* ownedAnchors, int ownedCount) {
    float plot[3] = {anchor[0], anchor[1], anchor[2]};
    for (int i = 0; i < ownedCount; ++i) {
        float bp[3] = {ownedAnchors[3 * i + 0], ownedAnchors[3 * i + 1],
                       ownedAnchors[3 * i + 2]};
        if (util::VectorWithinTolerance(bp, plot, kPlotConnectTol))
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x50ed14 — selection-click decision tree.
//   Mirrors the nested gate exactly (see header).  The dispatch leaves (dialog
//   open / camera zoom) are returned as the action enum.
// ---------------------------------------------------------------------------
SelectionClickAction Building_DecideSelectionClick(const SelectionClickState& s) {
    if (!s.mapModeActive) return SelectionClickAction::kNone;
    if (!s.pickedValid)   return SelectionClickAction::kNone;
    if (s.suppress6317B4) return SelectionClickAction::kNone;

    if (s.flag631748) {
        // if (!IsProduction || kind!=71) -> open building dialog; else gated open.
        if (!s.isProduction || s.pickedKind != 71)
            return SelectionClickAction::kOpenBuildingDialog;
        // LABEL_19: gated open
        return SelectionClickAction::kOpenBuildingGated;
    }
    if (s.isProduction && s.pickedKind == 71) {
        // ComputeSelectionFlags & 1 -> gated open, else nothing.
        return s.selectionFlagBit ? SelectionClickAction::kOpenBuildingGated
                                  : SelectionClickAction::kNone;
    }
    if (s.flag631748 || s.flag631744) {
        return SelectionClickAction::kOpenFamily;
    }
    return SelectionClickAction::kCameraZoomIn;
}

void ResetBuildLifecycleModule() {
    g_buildHooks  = &g_defaultBuildHooks;
    g_frameHooks  = &g_defaultFrameHooks;
    g_upgradeWindowHandle = 0;
}

}  // namespace guild::sim
