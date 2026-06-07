#pragma once
// guild::gui — HUD / Menu / MapMarker / PlayerBar batch 2 (Wave 14).
//
// Deterministic layout / marker-classification / label-format leaves recovered 1:1
// from gilde.exe.  Functions in this file:
//
//   * VIBE_MapMarker_ClassifyTree           @0x543d1c — push a tree object into the
//       minimap marker table with marker kind 23 (LBAUM) or 24 (other tree).
//   * VIBE_MapMarker_ClassifyTower          @0x544048 — push a tower object with kind 22.
//   * VIBE_MapMarker_ClassifyTerrainFeature @0x543da0 — bridge orientation -> one of the
//       directional bridge marker icons (26..29) via an 8-bucket angle table; the two
//       fallback feature names map to kinds 30/31.
//   * VIBE_MapView_FormatGoldLabel          @0x5440a4 — render "%1G" with the gold value.
//   * VIBE_Hud_BuildSliderPanel             @0x4bd388 — three-object slider panel layout
//       (up/down arrow widgets + centered value label) with the +42 / -19,+25 offsets.
//   * VIBE_Hud_SyncWindowColors             @0x4bd5dc — copy the selected palette colour
//       pair from the source window into the current window and write the 24/24/27/11
//       border colour cells.
//   * VIBE_Hud_BuildObjectActionPanel       @0x4bd0d4 — action-button row (gfx-id list
//       depends on the build/transport flags and the 1709 special case) + 24/24/24/24
//       border colour cells.
//   * VIBE_Hud_EnableObjectList             @0x555f64 — enable the first `count` objects
//       in a 56-byte-stride record array (object id at +8).
//   * VIBE_PlayerBar_ResetDragSlots         @0x4b1d17 — the 320-entry drag-slot reset
//       loop inside VIBE_PlayerBar_Create (stride 10 dwords).
//   * VIBE_PlayerBar_Destroy                @0x4b1dc4 — tear down the player bar form,
//       reset the drag table, restore the previous form, refresh the info panel.
//   * VIBE_Menu_FormatMissionBuildingName   @0x59b8cc — copy two wide-ish strings into the
//       mission scratch record and prime its header constants, then look up the building.

#include "gui/types.h"
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace guild::gui {

// ===========================================================================
// Minimap marker table.  The original threads a single 1056+512+128*4-byte record
// (base reg "a2"): the object-id list starts at +0, the per-marker kind list at +512,
// and the live count is the dword at +1024.  Trees use a separate record whose count
// is at +392 / kind list at +192 / id list at +0.  We model both as a small typed
// table so the index math (4*count, +512+4*count, etc.) translates directly.
// ===========================================================================

// Tower / terrain record: id list at +0, kind list at +512, count at +1024, cap 128.
inline constexpr int kMarkerCap     = 128;  // count < 128 gate
inline constexpr int kMarkerKindOff = 512;  // bytes from base to the kind list
inline constexpr int kMarkerCntOff  = 1024;

// Tree record: id list at +0, kind list at +192, count at +392, cap 48.
inline constexpr int kTreeCap     = 48;
inline constexpr int kTreeKindOff = 192;
inline constexpr int kTreeCntOff  = 392;

// Recovered marker-kind constants.
inline constexpr int kMarkerKindTreeLbaum   = 23; // 0x543d42
inline constexpr int kMarkerKindTreeOther   = 24; // 0x543d80
inline constexpr int kMarkerKindTower       = 22; // 0x54407f
inline constexpr int kMarkerKindBridgeEW    = 26; // angle bucket 0,4,8 -> wrap
inline constexpr int kMarkerKindBridgeNESW  = 27; // bucket 1,5
inline constexpr int kMarkerKindBridgeNS    = 28; // bucket 2,6,7
inline constexpr int kMarkerKindBridgeNWSE  = 29; // bucket 3
inline constexpr int kMarkerKindFeature30   = 30; // 2nd fallback name
inline constexpr int kMarkerKindFeature31   = 31; // 3rd fallback name

// Object names the classifiers match (case-sensitive, as in the original strcmp).
inline constexpr const char* kNameTreeLbaum     = "vg_LBAUM01";
inline constexpr const char* kNameTowerSlate    = "gb_TURM_SCHIEFERDACH";
inline constexpr const char* kNameBridgeOverFlu = "ob_BRUECKE_FLUSS";

// The 8 angle bucket boundaries (radians): pi/8 + k*pi/4, last entry = 2*pi.
//   [0]=0.39269908  [1]=1.17809725  [2]=1.96349541  [3]=2.74889357
//   [4]=3.53429174  [5]=4.31968990  [6]=5.10508806  [7]=5.89048623  [8]=6.28318531
extern const double kBridgeAngleBuckets[9];

// A minimal minimap marker table mirroring the original record offsets.
struct MarkerTable {
    std::vector<int> ids;   // +0 (one slot per marker)
    std::vector<int> kinds; // +kindOff
    int count = 0;          // +cntOff

    void push(int id, int kind) {
        if ((int)ids.size() <= count)   ids.resize(count + 1);
        if ((int)kinds.size() <= count) kinds.resize(count + 1);
        ids[count]   = id;
        kinds[count] = kind;
        ++count;
    }
};

// gilde.exe 0x543d1c.  `isLbaum` is the result of the first strcmp(name,"vg_LBAUM01");
// when it fails the original tries a second tree name (here `isOtherTree`).  Pushes the
// object with kind 23 or 24 and returns whether the tree table still has room (<48).
bool MapMarker_ClassifyTree(MarkerTable& tbl, int objId, bool isLbaum, bool isOtherTree);

// gilde.exe 0x544048.  When `isSlateTower`, push `objId` with kind 22.  Returns
// whether the marker table still has room (count < 128).
bool MapMarker_ClassifyTower(MarkerTable& tbl, int objId, bool isSlateTower);

// gilde.exe 0x543da0.  Bridge orientation -> marker icon.  When `isBridge`, the angle
// (radians, in [0,2pi)) selects a directional icon from the 8-bucket table; the other
// two fallback names map to kinds 30/31.  `which` selects the matched branch:
//   0 = bridge-over-river (use `angle`), 1 = 2nd name (kind 30), 2 = 3rd name (kind 31).
// Returns whether the table still has room (count < 128).
enum class TerrainFeature { Bridge = 0, Name2 = 1, Name3 = 2, None = 3 };
int  MapMarker_BridgeAngleKind(double angle); // pure angle->kind lookup
bool MapMarker_ClassifyTerrainFeature(MarkerTable& tbl, int objId,
                                      TerrainFeature which, double angle);

// ===========================================================================
// Map-view gold label — gilde.exe 0x5440a4.
//   VIBE_Text_RenderFormattedMessage(value, "%1G", dest)
// We model the format substitution: "%1G" -> the gold value rendered as a decimal.
// Returns the formatted string the original would have produced.
// ===========================================================================
inline constexpr const char* kGoldLabelFmt = "%1G";
std::string MapView_FormatGoldLabel(int goldValue);

// ===========================================================================
// HUD slider panel layout — gilde.exe 0x4bd388.
// Builds three widgets in window `windowSlot` at baseX/baseY:
//   down-arrow  @ (baseY)           -> *outDown
//   up-arrow    @ (baseY + 42)      -> *outUp
//   value label @ (baseX-19, baseY+25)
// Each arrow gets btnFlagB(+72)=1, radioFlag(+444)=3; the label gets editFlags(+132)=67,
// value(+88?)=1, +20=48.  Both arrows store `groupId` at +476.  The window's [146] is
// cleared and [153] receives `userData`.
// ===========================================================================
struct SliderPanelResult {
    int downId = 0;   // *outDown (a6) — first arrow widget
    int upId   = 0;   // *outUp   (a7) — second arrow widget
    int labelId = 0;  // *outLabel(a8) — centered value label
};
// `addWidget(windowSlot, x)` returns a new widget id; the test installs a counter.
// The three widget records are written through `widgets` (indexed by id).
SliderPanelResult Hud_BuildSliderPanel(std::vector<Widget>& widgets, int baseX, int baseY,
                                       int windowSlot, int groupId, int userData);

// ===========================================================================
// HUD window-colour border cells.  Several builders write a 4-cell colour quad into the
// per-window border tables (dword_67EF18.. step 14 + 224*winColorIdx).  We model the
// four parallel tables as one [winColorIdx] -> {c0,c1,c2,c3} record.
// ===========================================================================
struct BorderColorCell { int c0 = 0, c1 = 0, c2 = 0, c3 = 0; };

// gilde.exe 0x4bd5dc.  Copy the palette colour pair from the source window record into
// the current window record (word at +8 / +10) when its "+226" link != -1, then write
// the 24/24/27/11 border quad at `winColorIdx`.  Models the two windows as Window
// records plus the border cell.  Returns the original return value (result*4).
int Hud_SyncWindowColors(Window& curWin, const Window& srcWin, int srcLink,
                         BorderColorCell& cell);

// gilde.exe 0x4bd0d4 (the colour-cell tail).  Action panel writes the 24/24/24/24 quad.
void Hud_BuildObjectActionPanel_Colors(BorderColorCell& cell);
// The action-button gfx-id sequence the panel adds to its window (the 1709 special
// case and the build/transport-flag branches).  Returned in order for golden testing.
std::vector<int> Hud_ObjectActionPanelButtons(int captionId, bool buildFlag,
                                              bool transportFlag);
inline constexpr int kActionCaptionSpecial = 1709;

// ===========================================================================
// HUD enable-object list — gilde.exe 0x555f64.
//   for (i=0; i<count; ++i) SetEnabled(rec[i*56 + 8], 1)
// We model the records as a vector of 56-byte blobs; the object id is the dword at +8.
// Returns the id of the last record enabled (the original `result`).
// ===========================================================================
inline constexpr int kEnableRecStride = 56;
struct EnableHooks {
    virtual ~EnableHooks() = default;
    virtual int SetEnabled(int objId, int enabled) { (void)enabled; return objId; }
};
void Hud_SetEnableHooks(EnableHooks* hooks);
int  Hud_EnableObjectList(const std::vector<std::vector<guild::u8>>& records, int count);

// ===========================================================================
// Player bar drag-slot reset — gilde.exe 0x4b1d17 (loop inside VIBE_PlayerBar_Create).
//   for (i=0; i!=320; i+=10) { d6F8[i]=-1; d700[i]=-1; d704[i]=-1; d708[i]=0xFFFF;
//                              d70C[i]=-1; d710[i]=-1; b714[i*4]=0 }
// 32 logical slots (320 / 10), 10 dwords each.  We model six parallel int32 tables
// (capacity 320 entries each, but only every 10th is written) + one byte flag table.
// ===========================================================================
struct DragSlotTables {
    std::vector<std::int32_t> t6F8, t700, t704, t708, t70C, t710;
    std::vector<std::int32_t> b714; // the byte_11BB714[i*4] writes (one int per group)
    DragSlotTables();
};
inline constexpr int kDragSlotLoopEnd = 320; // i != 320
inline constexpr int kDragSlotStep    = 10;  // i += 10
void PlayerBar_ResetDragSlots(DragSlotTables& tbl);

// ===========================================================================
// Player bar destroy — gilde.exe 0x4b1dc4.
//   Form_Destroy(g_playerBarForm); g_playerBarForm = -1; DragSlot_ResetTable();
//   if (g_prevForm != -1) Form_SetObjectsVisible(g_prevForm, 1);
//   InfoPanel_Update(...)
// We thread the two form handles + the side effects through a hooks struct so the
// control flow is exercisable without the renderer/form cluster.
// ===========================================================================
struct PlayerBarHooks {
    virtual ~PlayerBarHooks() = default;
    virtual void FormDestroy(int /*form*/) {}
    virtual void DragSlotResetTable() {}
    virtual void FormSetObjectsVisible(int /*form*/, int /*visible*/) {}
    virtual void InfoPanelUpdate() {}
};
void PlayerBar_SetHooks(PlayerBarHooks* hooks);
// `playerBarForm`/`prevForm` are the two globals (in/out).  After the call the player
// bar form is set to -1.  Records which side effects fired (for golden testing) via the
// installed hooks.
void PlayerBar_Destroy(int& playerBarForm, int prevForm);
inline constexpr int kPlayerBarInvalidForm = -1;

// ===========================================================================
// Mission building-name scratch prep — gilde.exe 0x59b8cc.
// The original copies two source strings (2 bytes per iteration, stop at NUL) into the
// scratch record, then primes header dwords/bytes and looks up the building record.
// We recover the string copy + the header constants exactly.
// ===========================================================================
struct MissionNameScratch {
    std::string name;     // String @0x122F4AA <- dword_8CA998
    std::string subName;  // byte_122F4CA @0x122F4CA <- dword_8CA99C
    std::int32_t hdr0 = 0;   // dword_122F4A0
    std::int32_t hdr4 = 0;   // dword_122F4A4
    std::int32_t hdr528 = 0; // dword_122F528
    guild::u8 flagA8 = 0;    // byte_122F4A8
    guild::u8 flagA9 = 0;    // byte_122F4A9
};
inline constexpr std::int32_t kMissionHdr0   = 856692811; // dword_122F4A0
inline constexpr std::int32_t kMissionHdr4   = 1342;      // dword_122F4A4
inline constexpr std::int32_t kMissionHdr528 = 1555;      // dword_122F528
// gilde.exe 0x59b8cc.  Copy `name`/`subName` into the scratch and prime its header.
// The "building type" the original then looks up is HIBYTE(hdr0); returned here.
int Menu_FormatMissionBuildingName(const std::string& name, const std::string& subName,
                                   MissionNameScratch& out);

} // namespace guild::gui
