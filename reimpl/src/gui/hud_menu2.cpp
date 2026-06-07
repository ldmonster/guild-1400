#include "gui/hud_menu2.h"

#include <cstring>

namespace guild::gui {

// The 8 angle bucket boundaries (radians): pi/8 + k*pi/4, last entry = 2*pi.
const double kBridgeAngleBuckets[9] = {
    0.39269908169875, 1.17809724509625, 1.96349540849375, 2.74889357189125,
    3.53429173528875, 4.31968989868625, 5.10508806208375, 5.89048622548125,
    6.28318530718,
};

// ---------------------------------------------------------------------------
// Minimap marker classifiers.
// ---------------------------------------------------------------------------
// gilde.exe 0x543d1c.  vg_LBAUM01 -> kind 23, else the 2nd tree name -> kind 24.
// Both branches push (id, kind) then return whether the TREE count (+392) < 48.
bool MapMarker_ClassifyTree(MarkerTable& tbl, int objId, bool isLbaum, bool isOtherTree) {
    if (isLbaum) {
        tbl.push(objId, kMarkerKindTreeLbaum);   // +0 = id ; +192 = 23 ; ++count
        return tbl.count < kTreeCap;
    }
    if (!isOtherTree)
        return tbl.count < kTreeCap;             // no second tree name matched
    tbl.push(objId, kMarkerKindTreeOther);       // +0 = id ; +192 = 24 ; ++count
    return tbl.count < kTreeCap;
}

// gilde.exe 0x544048.  gb_TURM_SCHIEFERDACH -> kind 22.  Returns count < 128.
bool MapMarker_ClassifyTower(MarkerTable& tbl, int objId, bool isSlateTower) {
    if (isSlateTower)
        tbl.push(objId, kMarkerKindTower);       // +0 = id ; +512 = 22 ; ++count
    return tbl.count < kMarkerCap;
}

// gilde.exe 0x543da0 (angle -> icon).  Sequential ifs, exactly as the original (the
// last matching bucket wins; buckets [6] and [7] overlap and both write 28).
int MapMarker_BridgeAngleKind(double angle) {
    int kind = 0; // the original leaves the kind cell untouched if no bucket matches
    const double* b = kBridgeAngleBuckets;
    if (angle >= 0.0     && angle <  b[0]) kind = kMarkerKindBridgeEW;    // 26
    if (angle >= b[0]    && angle <  b[1]) kind = kMarkerKindBridgeNESW;  // 27
    if (angle >= b[1]    && angle <  b[2]) kind = kMarkerKindBridgeNS;    // 28
    if (angle >= b[2]    && angle <  b[3]) kind = kMarkerKindBridgeNWSE;  // 29
    if (angle >= b[3]    && angle <  b[4]) kind = kMarkerKindBridgeEW;    // 26
    if (angle >= b[4]    && angle <  b[5]) kind = kMarkerKindBridgeNESW;  // 27
    if (angle >= b[5]    && angle <  b[6]) kind = kMarkerKindBridgeNS;    // 28
    if (angle >= b[5]    && angle <  b[7]) kind = kMarkerKindBridgeNS;    // 28 (overlap)
    if (angle >= b[7]    && angle <= b[8]) kind = kMarkerKindBridgeEW;    // 26
    return kind;
}

// gilde.exe 0x543da0 (whole).  ob_BRUECKE_FLUSS -> push id then set the directional
// kind from the angle bucket; the two fallback names -> kinds 30/31.
bool MapMarker_ClassifyTerrainFeature(MarkerTable& tbl, int objId,
                                      TerrainFeature which, double angle) {
    if (which == TerrainFeature::Bridge) {
        // Push id first (the original writes *(a2 + 4*count) = id before the buckets).
        int slot = tbl.count;
        if ((int)tbl.ids.size() <= slot)   tbl.ids.resize(slot + 1);
        if ((int)tbl.kinds.size() <= slot) tbl.kinds.resize(slot + 1);
        tbl.ids[slot]   = objId;
        tbl.kinds[slot] = MapMarker_BridgeAngleKind(angle);
        ++tbl.count;
    } else if (which == TerrainFeature::Name2) {
        tbl.push(objId, kMarkerKindFeature30);
    } else if (which == TerrainFeature::Name3) {
        tbl.push(objId, kMarkerKindFeature31);
    }
    return tbl.count < kMarkerCap;
}

// ---------------------------------------------------------------------------
// Map-view gold label — gilde.exe 0x5440a4.
//   VIBE_Text_RenderFormattedMessage(value, "%1G", dest) — "%1G" substitutes arg 1 as
//   the value's decimal form.  We reproduce the substitution result.
// ---------------------------------------------------------------------------
std::string MapView_FormatGoldLabel(int goldValue) {
    return std::to_string(goldValue);
}

// ---------------------------------------------------------------------------
// HUD slider panel layout — gilde.exe 0x4bd388.
//   down = AddToWindow(win, baseY)        ; btnFlagB=1 ; radioFlag=3
//   up   = AddToWindow(win, baseY + 42)   ; btnFlagB=1 ; radioFlag=3 ; both[+476]=groupId
//   label= AddTextLabel(baseX-19, baseY+25, win, "") ; editFlags(+132)=67 ;
//          [+88]=1 ; [+20]=48
//   win[146]=0 ; win[153]=userData
// ---------------------------------------------------------------------------
namespace {
int g_sliderWidgetCounter = 0; // local AddToWindow stand-in (deterministic ids)
int AllocWidget(std::vector<Widget>& widgets) {
    int id = g_sliderWidgetCounter++;
    if ((int)widgets.size() <= id)
        widgets.resize(id + 1);
    return id;
}
} // namespace

SliderPanelResult Hud_BuildSliderPanel(std::vector<Widget>& widgets, int baseX, int baseY,
                                       int windowSlot, int groupId, int userData) {
    (void)windowSlot;
    g_sliderWidgetCounter = 0; // ids are per-panel deterministic for golden vectors
    SliderPanelResult r;

    // First arrow (a7 == outUp slot in the original, written first): AddToWindow(win,baseY)
    r.upId = AllocWidget(widgets);
    widgets[r.upId].x()        = (i16)0;          // placement encoded via the AddToWindow arg
    widgets[r.upId].btnFlagB() = 1;               // +72 = 1
    widgets[r.upId].radioFlag()= 3;               // +444 = 3
    widgets[r.upId].editValue()= groupId;         // +476? -> store groupId (476 mod 4 alias)
    widgets[r.upId].at<i32>(476) = groupId;

    // Second arrow (a6 == outDown): AddToWindow(win, baseY + 42)
    r.downId = AllocWidget(widgets);
    widgets[r.downId].btnFlagB() = 1;             // +72 = 1
    widgets[r.downId].radioFlag()= 3;             // +444 = 3
    widgets[r.downId].at<i32>(476) = groupId;     // +476 = groupId

    // Value label (a8): AddTextLabel(baseX-19, baseY+25, win, "")
    r.labelId = AllocWidget(widgets);
    widgets[r.labelId].x()         = (i16)(baseX - 19);
    widgets[r.labelId].y()         = (i16)(baseY + 25);
    widgets[r.labelId].editFlags() = 67;          // +132 = 67
    widgets[r.labelId].at<i32>(88) = 1;           // +88 = 1
    widgets[r.labelId].at<i16>(20) = 48;          // +20 = 48
    (void)userData;
    return r;
}

// ---------------------------------------------------------------------------
// HUD window colour sync — gilde.exe 0x4bd5dc.
//   if (curWin[226] != -1) { curWin.word[4] = srcWin.word[ (84*link+80)/2 ];
//       curWin.word[5] = srcWin.word[...82]; border quad = {24,24,27,11} }
// We model the palette copy abstractly: when `srcLink` != -1 copy the source colour pair
// (word at +80 / +82 of the source record region) into the current window's word[4]/[5].
// ---------------------------------------------------------------------------
int Hud_SyncWindowColors(Window& curWin, const Window& srcWin, int srcLink,
                         BorderColorCell& cell) {
    int result = 0;
    if (srcLink != -1) {
        // word[4] (+8) <- src color0 ; word[5] (+10) <- src color1
        curWin.at<u16>(8)  = srcWin.at<u16>(80 + 84 * 0); // src palette color lo
        curWin.at<u16>(10) = srcWin.at<u16>(82 + 84 * 0); // src palette color hi
        cell.c0 = 24;
        cell.c1 = 24;
        cell.c2 = 27;
        cell.c3 = 11;
        result = 1; // non-zero "applied"
    }
    return result;
}

// ---------------------------------------------------------------------------
// HUD object-action panel — gilde.exe 0x4bd0d4.
// ---------------------------------------------------------------------------
void Hud_BuildObjectActionPanel_Colors(BorderColorCell& cell) {
    cell.c0 = 24;
    cell.c1 = 24;
    cell.c2 = 24;
    cell.c3 = 24;
}

std::vector<int> Hud_ObjectActionPanelButtons(int captionId, bool buildFlag,
                                              bool transportFlag) {
    std::vector<int> out;
    out.push_back(9);              // AddToWindow(a1, 9)  (unconditional first)
    out.push_back(9);              // AddToWindow(v5, 9)  (second, branch picks the slot)
    bool flag = buildFlag || transportFlag; // dword_631744 || dword_631748
    if (flag) {
        if (captionId == kActionCaptionSpecial)
            out.push_back(9);      // extra AddToWindow(a1, 9) for the 1709 case
    }
    out.push_back(9);              // AddToWindow(v6, 9)
    out.push_back(0);              // AddToWindow(a1, 0)
    if (captionId == kActionCaptionSpecial) {
        out.push_back(67);         // AddToWindow(a1, 67)
        out.push_back(67);         // AddToWindow(a1, 67)
        out.push_back(424);        // AddToWindow(a1, 424)
    } else {
        out.push_back(400);        // AddToWindow(a1, 400)
        out.push_back(77);         // AddToWindow(a1, 77)
        out.push_back(77);         // AddToWindow(a1, 77)
    }
    return out;
}

// ---------------------------------------------------------------------------
// HUD enable-object list — gilde.exe 0x555f64.
// ---------------------------------------------------------------------------
namespace {
EnableHooks g_defaultEnableHooks;
EnableHooks* g_enableHooks = &g_defaultEnableHooks;
} // namespace

void Hud_SetEnableHooks(EnableHooks* hooks) {
    g_enableHooks = hooks ? hooks : &g_defaultEnableHooks;
}

int Hud_EnableObjectList(const std::vector<std::vector<guild::u8>>& records, int count) {
    int result = 0; // the original returns a2 (=record base) when count<=0; 0 here
    if (count > 0) {
        int i = 0;
        do {
            int objId = 0;
            if (i < (int)records.size() && records[i].size() >= 12)
                std::memcpy(&objId, records[i].data() + 8, sizeof(objId)); // +8 = id
            result = g_enableHooks->SetEnabled(objId, 1);
            ++i;
        } while (i < count);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Player bar drag-slot reset — gilde.exe 0x4b1d17.
// ---------------------------------------------------------------------------
DragSlotTables::DragSlotTables() {
    t6F8.assign(kDragSlotLoopEnd, 0);
    t700.assign(kDragSlotLoopEnd, 0);
    t704.assign(kDragSlotLoopEnd, 0);
    t708.assign(kDragSlotLoopEnd, 0);
    t70C.assign(kDragSlotLoopEnd, 0);
    t710.assign(kDragSlotLoopEnd, 0);
    b714.assign(kDragSlotLoopEnd, 0);
}

void PlayerBar_ResetDragSlots(DragSlotTables& tbl) {
    for (int i = 0; i != kDragSlotLoopEnd; i += kDragSlotStep) {
        tbl.t6F8[i] = -1;
        tbl.t700[i] = -1;
        tbl.t704[i] = -1;
        tbl.t708[i] = 0xFFFF;
        tbl.t70C[i] = -1;
        tbl.t710[i] = -1;
        tbl.b714[i] = 0; // byte_11BB714[i*4] = 0
    }
}

// ---------------------------------------------------------------------------
// Player bar destroy — gilde.exe 0x4b1dc4.
// ---------------------------------------------------------------------------
namespace {
PlayerBarHooks g_defaultPlayerBarHooks;
PlayerBarHooks* g_playerBarHooks = &g_defaultPlayerBarHooks;
} // namespace

void PlayerBar_SetHooks(PlayerBarHooks* hooks) {
    g_playerBarHooks = hooks ? hooks : &g_defaultPlayerBarHooks;
}

void PlayerBar_Destroy(int& playerBarForm, int prevForm) {
    g_playerBarHooks->FormDestroy(playerBarForm);
    playerBarForm = kPlayerBarInvalidForm; // dword_631764 = -1
    g_playerBarHooks->DragSlotResetTable();
    if (prevForm != kPlayerBarInvalidForm)
        g_playerBarHooks->FormSetObjectsVisible(prevForm, 1);
    g_playerBarHooks->InfoPanelUpdate();
}

// ---------------------------------------------------------------------------
// Mission building-name scratch prep — gilde.exe 0x59b8cc.
//   copy name (2 bytes/iter until NUL) into String ; copy subName into byte_122F4CA ;
//   hdr0=856692811 ; hdr528=1555 ; flagA8=0 ; flagA9=0 ; hdr4=1342 ;
//   LookupTypeRecordA(HIBYTE(hdr0), &scratch) ; qmemcpy(...)
// The 2-bytes-per-iteration copy stops at the first NUL — it is a plain byte copy of a
// NUL-terminated string (the wide stride is the original's loop-unroll, not UTF-16).
// ---------------------------------------------------------------------------
int Menu_FormatMissionBuildingName(const std::string& name, const std::string& subName,
                                   MissionNameScratch& out) {
    out.name    = name;
    out.subName = subName;
    out.hdr0    = kMissionHdr0;
    out.hdr528  = kMissionHdr528;
    out.flagA8  = 0;
    out.flagA9  = 0;
    out.hdr4    = kMissionHdr4;
    // VIBE_Building_LookupTypeRecordA(SHIBYTE(hdr0), &scratch): the "type" byte is the
    // sign-extended high byte of hdr0 (0x331E5_4B -> 0x33 = 51).
    int buildingType = (int)(std::int8_t)((kMissionHdr0 >> 24) & 0xFF);
    return buildingType;
}

} // namespace guild::gui
