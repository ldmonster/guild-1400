#pragma once
// guild::gui — the selected-subject info-panel CONTENT BUILDERS.
//
// infopanel.{h,cpp} recovered VIBE_InfoPanel_Update's subject classification + change
// detection (which builder to run, and when).  This module recovers the builders it
// dispatches to:
//
//   VIBE_InfoPanel_AddIconSprite     @0x4b6454  add a 67-coloured sprite + caption to the bar
//   VIBE_InfoPanel_BuildBuilding     @0x4b64b0  building detail panel
//   VIBE_InfoPanel_BuildObject       @0x4b6930  scene-object detail panel
//   VIBE_InfoPanel_BuildTransporter  @0x4b6c80  passenger-transporter panel
//   VIBE_InfoPanel_BuildStandard     @0x4b6db8  the "nothing / own-building" default panel
//   VIBE_InfoPanel_BuildPerson       @0x4b7104  person detail panel
//   VIBE_InfoPanel_BuildDetailed (teardown) @0x4b7468  the dword_631774 4-slot widget reset
//
// Each builder loads a .form by name (VIBE_GameTick_Finalize, the "panel\infopanel_*"
// loader), selects sub-windows on it (VIBE_Form_SelectWindow), renders rich-text headers
// (VIBE_Text_RenderRichString / VIBE_Text_FormatItemLabelWithIcon), and adds icons /
// sprites / a value slider into the current window (VIBE_Object_AddToWindow,
// VIBE_Widget_AddSpriteToWindow, VIBE_Widget_AddSliderToWindow, VIBE_Object_SetColor,
// VIBE_Widget_SetTextColor, VIBE_Object_SetValueOrText).  The builders read the selected
// sim records (object / building / person) to decide the layout.
//
// The RECOVERABLE content-assembly logic — which form, which text format + arguments,
// which widgets at which positions, every record-field branch and every widget-state
// write — is translated here 1:1.  The runtime calls themselves are routed through an
// InfoPanelHost edge (matching the event_panel / trade_item_panel modules), so the build
// flow is exercisable headlessly.  The slot globals the builders publish (dword_63176C /
// 631768 / 631770 / 631774 / 631784 / 631788 / 631790..63179C) live here.

#include "gui/infopanel.h"
#include "gui/types.h"

namespace guild::gui {

// ---------------------------------------------------------------------------
// The info-panel slot globals the builders read/write.  -1 == "none" exactly as the
// binary stores them.  ResetInfoPanelBuild() restores the BSS state.
// ---------------------------------------------------------------------------
struct InfoPanelState {
    int form        = -1; // dword_631768 : the loaded panel form (-1 = panel free)
    int iconWidget  = -1; // dword_63176C : the building/object icon widget
    int slider      = -1; // dword_631770 : the value slider widget
    int personIcon  = -1; // dword_631788 : the person portrait widget
    int transWindow = -1; // dword_631784 : the transporter sub-window
    // The four corner/action sprite widgets the builders place (dword_631790..63179C).
    int sprite0     = -1; // dword_631790
    int sprite1     = -1; // dword_631794
    int sprite2     = -1; // dword_631798
    int sprite3     = -1; // dword_63179C
    // dword_631774[4] — the four "detail" sub-widget slots BuildDetailed manages.
    int detail[4]   = {-1, -1, -1, -1};
};
extern InfoPanelState g_infoPanel;
void ResetInfoPanelBuild();

// ---------------------------------------------------------------------------
// Recovered constants.
// ---------------------------------------------------------------------------
// Form names (gilde.exe rdata).
inline constexpr const char* kFormPanelBuilding    = "panel\\infopanel_gebaeude";     // aPanelInfopanel_2 @0x61df04
inline constexpr const char* kFormPanelStandard    = "panel\\infopanel_standard";     // aPanelInfopanel_1 @0x61df88
inline constexpr const char* kFormPanelPerson      = "panel\\infopanel_personen";     // aPanelInfopanel_0 @0x61dfb4
inline constexpr const char* kFormPanelTransporter = "panel\\infopanel_transporter";  // aPanelInfopanel   @0x61df60

// Text-id arithmetic the builders use to map a record code to a string-table index.
inline constexpr int kNameTextStride = 14;   // 14 * code + 1078  (object/building name)
inline constexpr int kNameTextBias   = 1078;
inline constexpr int kIconObjBias    = 1010; // building icon object id  = code + 1010
inline constexpr int kObjNameStride  = 2;    // 2 * code + 2151  (scene-object subtitle)
inline constexpr int kObjNameBias    = 2151;
inline constexpr int kIconObjLoBias  = 206;  // scene-object icon object id = code + 206

// Record class-byte sentinels.
inline constexpr int kBuildingTypeFairground = 71; // *record == 71 (no slider / special name)
inline constexpr int kBuildingTypeWell       = 29; // *record == 29 (no slider)
inline constexpr int kBuildingTypeMarket     = 30; // *record == 30 (extra sprite in BuildStandard)

// ---------------------------------------------------------------------------
// The runtime edge.  Each method maps to one VIBE_ runtime call the builders make; the
// default host is a no-op recorder (form/widget ids come back as -1).  Tests supply a
// host that hands out stable ids and records the call sequence.
// ---------------------------------------------------------------------------
struct InfoPanelHost {
    virtual ~InfoPanelHost() = default;

    // VIBE_GameTick_Finalize(HIWORD(dword_69FFBC)-104, 262, name) — load a panel form.
    // Returns the new form id (the panel becomes "occupied").
    virtual int LoadForm(const char* name) { (void)name; return -1; }

    // VIBE_Form_SelectWindow(form, sub) — make sub-window `sub` of `form` current.  The
    // current window id is then dword_62D230, which the Add* calls below target.
    virtual void SelectWindow(int form, int sub) { (void)form; (void)sub; }

    // The current window's content half-height: ((window.h >> 16) - 48) >> 1.  The
    // builders derive icon Y positions from it.  Default 0.
    virtual int CurrentWindowHalfHeight() { return 0; }

    // VIBE_Text_RenderRichString(fmt, ...) — render a header line.  `argc` args follow.
    virtual void RenderRichString(const char* fmt) { (void)fmt; }

    // VIBE_Object_AddToWindow(curWin, gfx, y, objId) -> widget id.
    virtual int AddObject(int gfx, int y, int objId) { (void)gfx; (void)y; (void)objId; return -1; }

    // VIBE_Widget_AddSpriteToWindow(x, y, gfxId, curWin) -> widget id.
    virtual int AddSprite(int x, int y, int gfxId) { (void)x; (void)y; (void)gfxId; return -1; }

    // VIBE_Widget_AddSliderToWindow(x, y, value, range, max, gfxBase, flags, curWin) -> id.
    virtual int AddSlider(int x, int y, int value, int range, int maxVal, int gfxBase, int flags) {
        (void)x; (void)y; (void)value; (void)range; (void)maxVal; (void)gfxBase; (void)flags; return -1;
    }

    // VIBE_Object_SetColor(widget, color).
    virtual void SetColor(int widget, int color) { (void)widget; (void)color; }
    // VIBE_Widget_SetTextColor(widget, color).
    virtual void SetTextColor(int widget, int color) { (void)widget; (void)color; }
    // VIBE_Object_SetValueOrText(widget, text, a3, value, a5).
    virtual void SetValueOrText(int widget, int text, int a3, int value, int a5) {
        (void)widget; (void)text; (void)a3; (void)value; (void)a5;
    }
    // VIBE_Widget_DestroyByType(widget, a2, a3) — teardown one detail sub-widget.
    virtual void DestroyWidget(int widget) { (void)widget; }

    // Sim-record edges the builders consult (so the layout branches are exercisable).
    // VIBE_Building_GetUpgradeLevel(record).
    virtual int BuildingUpgradeLevel(const void* record) { (void)record; return 0; }
};
void InfoPanel_SetHost(InfoPanelHost* host);

// ---------------------------------------------------------------------------
// Modelled records.  The builders read a handful of fields off the selected scene
// object / building / person record; we model exactly those (by original byte offset).
// ---------------------------------------------------------------------------

// A scene-object record (the +0 class byte, +5 custom-name string, +39 item word).
struct InfoObjectRecord {
    int  code   = 0;       // *record (low byte)               -> name/icon arithmetic
    const char* customName = nullptr; // record+5 (when non-empty -> ">name<" line)
};

// A building record.
struct InfoBuildingRecord {
    int  code        = 0;  // *record (low byte)               kBuildingType{Fairground,Well,Market}
    const char* customName = nullptr; // record+5
    int  item        = 0xFFFF; // *(u16*)(record+39)           item word (0xFFFF == none)
    int  upgradeLevel = 0; // VIBE_Building_GetUpgradeLevel    slider value
    bool noSlider    = false;  // (record[90]&1) or fairground/well -> no action sprites
};

// A person record.
struct InfoPersonRecord {
    int  nameCode  = 0;    // *(u16*)record                    name code (id 14*code+1078)
    int  classByte = 0;    // record[2]                        5/6/7 -> extra job icon
    int  job1      = 0;    // record[357]                      job-pair selector
    int  job2      = 0;    // record[353] high byte            job code (when job1==0)
    int  portraitObj = 0;  // *(u32*)(record+396)              portrait object id (offset 99*4)
    int  outputRatioPct = 0; // VIBE_Building_ComputeOutputRatio(record)*scale -> "%i%%"
    int  satisfaction = 0; // record[18]? slider value (relation matrix); supplied by caller
    bool aggregate  = false; // personMode>=2 or null -> "%i persons" aggregate header
    int  aggregateCount = 0; // dword_6317B0
};

// ---------------------------------------------------------------------------
// The translated builders.  Each returns the loaded form id (or -1 if the panel was
// already occupied / nothing built), and publishes the slot globals via g_infoPanel.
// ---------------------------------------------------------------------------

// gilde.exe 0x4b6454 — VIBE_InfoPanel_AddIconSprite (x@ax, y@dx)
// Adds a sprite (gfxId 180, at x+3,y) coloured 67 with a +88 caption flag and text colour
// 96 to the current window.  Returns the widget id.
int InfoPanel_AddIconSprite(int x, int y, int gfxId);

// gilde.exe 0x4b6930 — VIBE_InfoPanel_BuildObject (record@eax)
// Builds the scene-object detail panel for `obj` (and the optionally-owning room/building
// records `room`/`building`).  `slotsAfter` is VIBE_Building_CollectSlotsAfterObject (the
// caller resolves it); when true an extra action sprite is placed.  Returns the form id.
int InfoPanel_BuildObject(const InfoObjectRecord& obj,
                          const InfoObjectRecord* room,
                          const InfoObjectRecord* building,
                          bool ownerMatches, bool slotsAfter);

// gilde.exe 0x4b64b0 — VIBE_InfoPanel_BuildBuilding (record@eax)
// Builds the building detail panel for `bld`.  `flags` selects whether the action sprites
// are placed (VIBE_Building_ComputeSelectionFlags & 0x10 / item==own).  Returns the form id.
int InfoPanel_BuildBuilding(const InfoBuildingRecord& bld, int selectionFlags,
                            int category, bool itemIsOwn, bool noBuildingSelected);

// gilde.exe 0x4b6c80 — VIBE_InfoPanel_BuildTransporter (record@eax)
// Builds the passenger-transporter panel.  `owner` must resolve non-null (the caller did
// VIBE_GameObject_ResolveOwnerOrParentB).  Returns the form id (or -1 if owner null).
int InfoPanel_BuildTransporter(const InfoObjectRecord& transporter, bool ownerResolved,
                               int loadPct, int sliderValue);

// gilde.exe 0x4b6db8 — VIBE_InfoPanel_BuildStandard ()
// Builds the default panel: either the own-building detail (when the selected building's
// item word matches the player) or the empty "standard" panel.  `ownBuilding` chooses.
// Returns the form id.
int InfoPanel_BuildStandard(const InfoBuildingRecord* ownBuilding);

// gilde.exe 0x4b7104 — VIBE_InfoPanel_BuildPerson (record@eax)
// Builds the person detail panel.  When `person` is the aggregate (count form) the header
// is "%i persons"; otherwise it renders the job label + output-ratio slider + job icons.
// Returns the form id.
int InfoPanel_BuildPerson(const InfoPersonRecord& person);

// gilde.exe 0x4b7468 (the teardown tails) — VIBE_InfoPanel_BuildDetailed's dword_631774
// reset: walk the 4 detail slots, destroying each non-sentinel widget and resetting it to
// -1.  `count` is the number of slots to scan (4 in every tail).  Returns slots cleared.
int InfoPanel_ResetDetailSlots(int count);

} // namespace guild::gui
