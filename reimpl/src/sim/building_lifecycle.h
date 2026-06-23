#pragma once
// Building LIFECYCLE (remove/cleanup, slot free) + nearest-of-type query for the
// Guild simulation (gilde.exe). MODULE: buildings (namespace guild::sim).
//
// In the original, a "building" is a record in the PERSON array (word_12CE910,
// stride 536) — VIBE_Building_RemoveAndCleanup indexes word_12CE910[268*a1] and
// frees that 536-byte slot. We recover the building-as-person fields the cleanup
// path touches and model the slot store + the free path byte-faithfully. The
// trade-route table clearing, the history "rival" notification, the scene
// child-list free, the character destroy, and the production type-table decrement
// are routed through ILifecycleHooks (mocked in tests).
//
// Translated functions:
//   VIBE_Building_RemoveAndCleanup  0x5894b0
//   VIBE_Building_FindNearestSameType 0x587908
//
// WAVE-19 ADDITIONS (the build/upgrade lifecycle + bauplatz geometry cluster):
//   VIBE_Building_MapTypeToCategory          0x5878b0   (pure switch)
//   VIBE_BuildingType_GroupFromCode          0x58a4c8   (pure switch, JUMPOUT-merged)
//   VIBE_Building_ClassifyTypeFlag           0x589850   (range classifier; callee)
//   VIBE_Building_CheckBuildRequirements     0x587bfc   (requirement state machine)
//   VIBE_Building_CloseUpgradeWindow         0x5942b0   (form-destroy)
//   VIBE_Building_Update                     0x40e2b4   (per-frame state+anim wrapper)
//   VIBE_Building_ComputeUpgradeChargeAmount 0x50f7c0   (OpenUpgradeWindow cost block;
//                                                         ConvertX truncation, 1:1)
//   VIBE_Building_BuildUpgradeTree (state)   0x59361c   (per-node 0/1/2/3 classifier)
//   VIBE_Building_CheckPlotConnectivity      0x50cb4c   (bauplatz adjacency geometry)
//   VIBE_Building_ReserveBauplatzForActiveChar 0x50ccbc (plot filter+connectivity loop)
//   VIBE_Building_HandleSelectionClick (gate)0x50ed14   (selection dispatch decision)
// The window/scene/render leaves these orchestrate (Form_*, Window_AddChildWindow,
// Heightmap_*, SceneGraph_WalkAndInvoke, Paintbox_DrawLine, He_FindHandler*, the
// frame-loop pump) are routed through IBuildLifecycleHooks (rule 8: named, inert).
#include <cstddef>
#include <vector>

#include "guild/common/types.h"
#include "sim/types.h"
#include "sim/building_type.h"  // canonical BuildingType_GroupFromCode / Building_ClassifyTypeFlag

namespace guild::sim {

// ---------------------------------------------------------------------------
// Building-as-Person record fields the cleanup path reads/writes (byte offsets
// into the 536-byte Person record). Recovered from RemoveAndCleanup @0x5894b0.
//   +0   (word)  marker (-1 == free slot)
//   +2   (byte)  kind/state byte (15 == "removed/destroyed")
//   +4   (dword) building id
//   +8   (byte)  active flag (cleared on remove)
//   +40  (word)  removal timestamp word (v2[20], word index 20 == +40;
//                = qword_13CE852 low word — the game-time stamp on removal)
//   +368 (dword) production-type record ptr (dword index 92; type-table decrement)
//   +388 (dword) live character handle (dword index 97; destroyed on remove)
// DISASM-VERIFIED @0x5894b0: Character_Destroy reads *((_DWORD*)v2 + 97) == +388
// (charHandle); the type-table decrement reads *((_DWORD*)v2 + 92) == +368
// (typeRecord). An earlier copy had these two SWAPPED; corrected to the binary.
// ---------------------------------------------------------------------------
GUILD_PACKED_BEGIN
struct BuildingPersonRec {
    i16 marker;        // +0x000  -1 == free slot
    u8  kind;          // +0x002  15 == destroyed
    u8  pad3[5];       // +0x003..+0x007
    u8  activeFlag;    // +0x008
    u8  pad9[31];      // +0x009..+0x027
    u16 removalTs;     // +0x028 (word index 20 == +40) removal timestamp word
    u8  pad42[326];    // +0x02A..+0x16F
    i32 typeRecord;    // +0x170 (dword index 92 == +368) production type record
    u8  pad372[16];    // +0x174..+0x183
    i32 charHandle;    // +0x184 (dword index 97 == +388) live character handle
    u8  pad392[144];   // +0x188..+0x217
} GUILD_PACKED;
GUILD_PACKED_END
static_assert(offsetof(BuildingPersonRec, activeFlag) == 8,    "activeFlag @+8");
static_assert(offsetof(BuildingPersonRec, removalTs)  == 40,   "removalTs @+40");
static_assert(offsetof(BuildingPersonRec, typeRecord) == 368,  "typeRecord @+368");
static_assert(offsetof(BuildingPersonRec, charHandle) == 388,  "charHandle @+388");
static_assert(sizeof(BuildingPersonRec) == kPersonStride, "stride 536");

// The building/person slot store (aliases the Person array word_12CE910).
constexpr int kBuildingSlots = kPersonCapacity;   // 768
extern BuildingPersonRec g_buildingPersons[kBuildingSlots];
BuildingPersonRec* BuildingPersonAt(int slot);
void ResetBuildingPersons();

struct ILifecycleHooks {
    virtual ~ILifecycleHooks() = default;

    // gilde.exe VIBE_Building_ReleaseOccupantHoldings — release the building's
    // occupants' holdings (commands). Fire-and-forget.
    virtual void ReleaseOccupantHoldings(int slot) { (void)slot; }
    // gilde.exe trade-route table clearing (dword_11BC760/dword_11C2160): remove
    // every trade route whose endpoint id == this building's id. Returns the
    // number of routes that pointed at a sibling owner (drives the rival notify).
    virtual int ClearTradeRoutes(i32 buildingId) { (void)buildingId; return 0; }
    // gilde.exe VIBE_History_NotifyRivalEvent — log a rival-affecting removal.
    virtual void NotifyRivalEvent(int siblingSlot, int slot) {
        (void)siblingSlot; (void)slot;
    }
    // gilde.exe VIBE_GameObject_FreeChildList — free the scene child list.
    virtual void FreeChildList(int slot) { (void)slot; }
    // gilde.exe VIBE_Character_Destroy — destroy the live character handle.
    virtual void DestroyCharacter(i32 charHandle) { (void)charHandle; }
    // gilde.exe production type-table decrement: if the building's production
    // type record has kind 1, decrement its +101 active-count byte.
    virtual void DecrementTypeActiveCount(i32 typeRecord) { (void)typeRecord; }
    // gilde.exe FindNearestSameType's distance probe: world distance^2 between
    // building `slot` and `otherSlot`. Default 0 (co-located).
    virtual float BuildingDistanceSq(int slot, int otherSlot) {
        (void)slot; (void)otherSlot; return 0.0f;
    }
};
void SetLifecycleHooks(ILifecycleHooks* hooks);
ILifecycleHooks* LifecycleHooks();

// Global build counter (dword_647724) — decremented when a live slot frees.
void SetBuildCounter(int v);
int  BuildCounter();

// ---------------------------------------------------------------------------
// gilde.exe 0x5894b0 — VIBE_Building_RemoveAndCleanup (al ret, eax=slot, edx=free)
//   Marks the building destroyed (kind=15), clears its active flag, releases
//   occupant holdings, clears trade routes (notifying a sibling owner if hit),
//   frees the scene child list + character, decrements the production type count,
//   and — when `freeSlot` — frees the Person slot (marker=-1) and the build
//   counter. Idempotent: a slot already at kind 15 skips the destroy phase.
// ---------------------------------------------------------------------------
void Building_RemoveAndCleanup(int slot, bool freeSlot);

// ---------------------------------------------------------------------------
// gilde.exe 0x587908 — VIBE_Building_FindNearestSameType (eax=slot, dl=typeCode)
//   Scans the building/person array for the nearest *other* building of the same
//   raw type whose category matches `typeCode`, excluding production + storage
//   (kind 10) buildings. Returns the matching slot index, or 0 on none.
//   `selfHasObject` mirrors the original's `*(a1+97)` gate (the building must
//   have a live scene object to compare distances).
// ---------------------------------------------------------------------------
int Building_FindNearestSameType(int slot, u8 typeCode);

// ===========================================================================
// WAVE-19 — build/upgrade lifecycle + bauplatz geometry
// ===========================================================================

// ---------------------------------------------------------------------------
// gilde.exe 0x5878b0 — VIBE_Building_MapTypeToCategory (al ret, al = typeIndex)
//   switch on the TYPE-RECORD KIND byte (*(dword_13CE294 + 589*typeIndex)):
//     1,3,6,15 -> 3 | 2 -> 6 | 4,5,9 -> 8 | 7 -> 4 |
//     8,14,18,20,21,22 -> 1 | 11,12,13 -> 2 | 19 -> 7 | 23,24,25,26 -> 5 |
//     else -> 0.
//   `kindByte` is the type record's +0 kind byte (caller supplies it; tests
//   pass it directly, the live caller reads dword_13CE294[589*type]).
// ---------------------------------------------------------------------------
u8 Building_MapTypeToCategory(u8 kindByte);

// gilde.exe 0x589818 — Building_ClassifyTypeFlag and 0x58a4c8 —
// BuildingType_GroupFromCode are declared in sim/building_type.h (included
// above) and defined in building_type.cpp; reused here to avoid an ODR clash.
// NOTE: GroupFromCode's 52-57 band returns 10 (JUMPOUT 0x58A27B) and the 58-63
// band returns 2 (JUMPOUT 0x589850, the `mov al,2; ret` epilogue — NOT a
// tail-call into ClassifyTypeFlag). Verified against the 0x58a4c8 decompile.

// ---------------------------------------------------------------------------
// Hook surface for the build/upgrade lifecycle's genuine engine leaves
// (rule 8 — named, inert defaults, never faked). CheckBuildRequirements,
// CheckPlotConnectivity, the upgrade-tree node classifier and the selection
// dispatcher reach the scene-graph / person-array / window system through this.
// ---------------------------------------------------------------------------
struct BuildTypeRecord {
    u8 kind;          // +0   type-record kind byte
    u8 roomList[6];   // +547 outputProf[0..5] (the "roomList[547]" 6-wide array)
};
struct IBuildLifecycleHooks {
    virtual ~IBuildLifecycleHooks() = default;

    // gilde.exe 589*type + dword_13CE294: the building-type record. Returns the
    // KIND byte + the 6-wide +547 list CheckBuildRequirements scans. nullptr ==
    // no record (treated as kind 0 / empty list).
    virtual const BuildTypeRecord* TypeRecord(u8 typeIndex) {
        (void)typeIndex; return nullptr;
    }
    // gilde.exe 0x589d24 — VIBE_Building_GetCategoryForObject(personSlot).
    virtual u8 CategoryForObject(int personSlot) { (void)personSlot; return 0; }
    // gilde.exe person record byte +358 (the building-state byte the type-3/7/8
    // requirement branches read), and +13 (the level byte case 6 compares).
    virtual u8 PersonStateByte358(int personSlot) { (void)personSlot; return 0; }
    virtual u8 PersonLevelByte13(int personSlot)  { (void)personSlot; return 0; }
    // gilde.exe dword_63C7B8 — the "free build everywhere" cheat/debug flag.
    virtual bool FreeBuildEverywhere() { return false; }
    // gilde.exe person record kind byte +2 (the cheat-branch reads it for 6/7).
    virtual u8 PersonKindByte2(int personSlot) { (void)personSlot; return 0; }
    // gilde.exe 0x587b9c — VIBE_Person_CollectByType(kind, out[]): gather the
    // matching person/building slots; returns the count, fills `out` with the
    // type-record indices (each is *(int*)>>24 of the collected handle). Default
    // none.
    virtual int CollectByType(u8 kind, std::vector<u8>& outTypeIdx) {
        (void)kind; (void)outTypeIdx; return 0;
    }
};
void SetBuildLifecycleHooks(IBuildLifecycleHooks* hooks);
IBuildLifecycleHooks* BuildLifecycleHooks();

// ---------------------------------------------------------------------------
// gilde.exe 0x587bfc — VIBE_Building_CheckBuildRequirements (al ret).
//   al = the source action/type code, edx = the target person slot (0xFFFF == none).
//   Resolves category = MapTypeToCategory(code); record = TypeRecord(code);
//   actionCat = MapActionToCategory(record.kind); objCat = CategoryForObject(slot).
//   The FreeBuildEverywhere cheat short-circuits to 1 when the target person's
//   +2 kind byte is 6 or 7. Then a `switch(category)` state machine returns
//   0/1/2 from scanning record.roomList[547..552] and CollectByType fallbacks.
//   Returns the requirement code (0 fail, 1 satisfied here, 2 satisfied via a
//   collected sibling). `actionCat`/`category` are passed pre-resolved so the
//   leaves stay pure; `srcKind` is the record.kind (MapActionToCategory input).
// ---------------------------------------------------------------------------
u8 Building_CheckBuildRequirements(u8 srcCode, int targetSlot);

// ---------------------------------------------------------------------------
// gilde.exe 0x5942b0 — VIBE_Building_CloseUpgradeWindow.
//   VIBE_Form_Destroy(dword_13CE288). We expose the window-handle slot so the
//   real Form_Destroy leaf can be driven by the GUI; returns the destroyed handle.
// ---------------------------------------------------------------------------
void SetUpgradeWindowHandle(int handle);   // dword_13CE288
int  UpgradeWindowHandle();
int  Building_CloseUpgradeWindow();        // returns + clears the handle

// ---------------------------------------------------------------------------
// gilde.exe 0x40e2b4 — VIBE_Building_Update (per-frame).
//   v3 = VIBE_State_Update(dword_62D2A8); VIBE_Animation_Basic(*v4, v5, v3, a2, 5).
//   The whole body is a State_Update + Animation_Basic side-effect chain; we
//   surface the deterministic ordering (state tick THEN animate) via the hook.
//   `dt` is a2 (the frame delta the animation step receives).
// ---------------------------------------------------------------------------
struct IBuildingFrameHooks {
    virtual ~IBuildingFrameHooks() = default;
    virtual int StateUpdate(int stateObj) { (void)stateObj; return 0; }   // 0x40e9e8
    virtual int AnimationBasic(int obj, int prev, int stateResult, int dt) {
        (void)obj; (void)prev; (void)stateResult; (void)dt; return 0;     // 0x5d85b8
    }
};
void SetBuildingFrameHooks(IBuildingFrameHooks* hooks);
int  Building_Update(int obj, int dt);

// ---------------------------------------------------------------------------
// gilde.exe 0x50f7c0 — VIBE_Building_OpenUpgradeWindow upgrade-CHARGE block.
//   The deterministic money math the window runs for a tech upgrade (the path
//   where the type record's +64 cost byte is 0): a per-level discount factor
//   times the market price, both truncated by Coord_ConvertX (TRUNCATE toward 0).
//     factor = guildLevel * dbl_621620 + dbl_621628                (linear)
//     base   = trunc( ComputeMarketPrice(typeCode,100) )           // ConvertX
//     amount = trunc( base * factor )                              // ConvertX
//   Returns `amount` (the v56 the command path deducts). `marketPrice` is the
//   ComputeMarketPrice(typeCode,100) result (from trade_sell), `guildLevel` is
//   dword_63C744. flt consts recovered below.
// ---------------------------------------------------------------------------
constexpr double kUpgradeFactorSlope  = 0.25;  // dbl_621620 @0x621620 (0x3FD0..)
constexpr double kUpgradeFactorOffset = 0.5;   // dbl_621628 @0x621628 (0x3FE0..)
int Building_ComputeUpgradeChargeAmount(double marketPrice, int guildLevel);

// ---------------------------------------------------------------------------
// gilde.exe 0x59361c — VIBE_Building_BuildUpgradeTree per-node STATE classifier.
//   For each tree node the original computes a display state v40 in {0,1,2,3}:
//     - node already owned by the building/object        -> 1 (available/built)
//     - node buildable (prereq count satisfied)          -> 2 (buildable)
//     - an active build handler matches this node         -> 3 (in progress)
//     - otherwise                                         -> 0 (locked)
//   The window-construction (Window_AddChildWindow / Object_AddToWindow /
//   Paintbox_DrawLine / Text_RenderRichString / progress bar) is a pure render
//   side effect routed through IUpgradeTreeHooks. We reconstruct the v40
//   decision 1:1 from the prereq-count + ownership + handler-match logic.
// ---------------------------------------------------------------------------
struct UpgradeTreeNode {
    u16 typeCode;     // dword_12CDD8E[n] >> 16  (the node's building-type code)
    u16 prereqA;      // dword_12CDD8E[n]+2 >>16 (LOWORD of dword_12CDD92 == prereqA)
    u16 prereqB;      // dword_12CDD92[n] >> 16   (HIWORD == prereqB present-flag)
    bool prereqAReq;  // LOWORD(dword_12CDD92[n]) != 0  -> at least 1 prereq needed
    bool prereqBReq;  // HIWORD(dword_12CDD92[n]) != 0  -> 2 prereqs needed
};
struct IUpgradeTreeHooks {
    virtual ~IUpgradeTreeHooks() = default;
    // gilde.exe 0x5857fc — does the building (a1+93 tree) own a node of `typeCode`?
    virtual bool BuildingOwns(u16 typeCode) { (void)typeCode; return false; }
    // gilde.exe 0x5857fc — does the object (a2+20 tree) own a node of `typeCode`?
    virtual bool ObjectOwns(u16 typeCode) { (void)typeCode; return false; }
    // gilde.exe 65*type + dword_13CE27C +0 == 6  (the "office/handler" type gate).
    virtual bool IsHandlerType(u16 typeCode) { (void)typeCode; return false; }
    // gilde.exe He_FindFirstHandlerByFilter chain: is a build handler active for
    // this node on the building? (drives state 3).
    virtual bool HandlerActiveFor(u16 typeCode) { (void)typeCode; return false; }
};
// Classify ONE node 1:1. `ownedCountSoFar` is the running v16/v20 prereq-met
// count the original accumulates over preceding rows; the caller passes the count
// of already-built/owned prereq rows for this node's typeCode.
u8 Building_ClassifyUpgradeNode(const UpgradeTreeNode& node, int prereqMetCount,
                                IUpgradeTreeHooks* hooks);

// ---------------------------------------------------------------------------
// gilde.exe 0x50cb4c — VIBE_Building_CheckPlotConnectivity (bauplatz adjacency).
//   Walks the plot's adjacency: tests whether any owned building sits within a
//   100.0-unit tolerance of the plot's anchor (and of each linked sub-plot whose
//   name starts "bk_" / "!bk_"). Returns the reserve-result; v5 (the "free"
//   flag) is 1 when NO adjacent owned building was found, else 0.  We reconstruct
//   the geometry decision: given the plot anchor + the list of owned-building
//   anchors, return whether the plot is connected (an owned building is adjacent).
// ---------------------------------------------------------------------------
constexpr float kPlotConnectTol = 100.0f;   // VectorWithinTolerance tolerance
// Returns true when `anchor` is within kPlotConnectTol of ANY entry in `owned`
// (per-component abs <= tol, the VIBE_Math_VectorWithinTolerance test).
bool Building_PlotHasAdjacentOwned(const float anchor[3],
                                   const float* ownedAnchors, int ownedCount);

// ---------------------------------------------------------------------------
// gilde.exe 0x50ed14 — VIBE_Building_HandleSelectionClick selection DECISION.
//   Given the global map-mode gate (byte_67225C==50 or the placement-cursor
//   modes) and the picked panel object's flags, decides which action fires:
//   open-building-dialog / selection-flags-gated open / family-open / camera
//   zoom-in. We reconstruct the decision tree; the dispatch leaves (dialog open,
//   camera zoom) are returned as an enum for the caller to route.
// ---------------------------------------------------------------------------
enum class SelectionClickAction {
    kNone = 0,
    kOpenBuildingDialog,   // VIBE_Dialog_OpenBuildingForActiveChar
    kOpenBuildingGated,    // dialog open gated by ComputeSelectionFlags & 1
    kOpenFamily,           // family-open variant (HIWORD id branch)
    kCameraZoomIn,         // VIBE_Camera_ZoomIn fallback
};
struct SelectionClickState {
    bool mapModeActive;    // byte_67225C==50 || (dword_672228 && cursor==place)
    bool pickedValid;      // VIBE_MapView_PanelDispatcher returned non-null
    bool suppress6317B4;   // byte_6317B4 (suppress when set)
    bool flag631748;       // dword_631748
    bool flag631744;       // dword_631744
    bool isProduction;     // VIBE_Building_IsProductionType(picked)
    u8   pickedKind;       // *picked (==71 == "office/handler" object kind)
    bool selectionFlagBit; // ComputeSelectionFlags(...) & 1
};
SelectionClickAction Building_DecideSelectionClick(const SelectionClickState& s);

void ResetBuildLifecycleModule();   // test helper: restore default hooks/state

}  // namespace guild::sim
