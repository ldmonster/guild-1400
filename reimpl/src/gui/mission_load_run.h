#pragma once
// guild::gui — the main-menu CITY-LOAD + CHOOSE-MISSION leaves, reconstructed 1:1.
//
//   * VIBE_Map_LoadCityFile @0x528bd0 — load a named city into the live world. The menu
//     calls it AFTER the file selector picks a .CTY: it reads the city's
//     "[A - ALLGEMEIN] Stadtname" key via GetPrivateProfileStringA and passes the result
//     plus a network flag. networkFlag != 0 builds "gamedata/cities/<name>.NET", else
//     "<name>.CTY"; the file is written out (Save_WriteGameFile, mode 2), the world is
//     reset (buildings/persons/owners/spawned-entities), the universe slot is reset twice,
//     and the ChooseCity.ed3 scene is reloaded. (Both menu call sites: edx=0 -> .CTY,
//     edx=1 -> .NET; eax = the Stadtname buffer.)
//
//   * VIBE_Menu_BuildChooseMissionDialog @0x59b998 — the CHOOSE_MISSION form the menu's
//     mission button opens. Builds the special\CHOOSE_MISSION form, fills 5 mission rows
//     (rich strings "%ia[%s]" ids 7347..7351), plus an "enable" indicator (7352) and an OK
//     button (7353); runs the RunFrameLoop loop; on OK with >=1 row selected returns 1 and
//     latches the selection into dword_649CD8[0..4]. The menu, on a nonzero return, then
//     calls FormatMissionBuildingName, copies "Tutorial" into the city-name buffer, and
//     sets byte_63CC1D=1, word_63C740=137, dword_631614=1.
//
//   * VIBE_Menu_FormatMissionBuildingName @0x59b8cc — the mission building-name formatter:
//     copies two 2-byte-stride strings into BSS scratch, seeds a few scratch dwords, then
//     looks up the building type record (Building_LookupTypeRecordA) and qmemcpys it.
//
// REUSE: the real loader pieces already live in src/ (io/save Save_WriteGameFile,
// sim/building3 ResetAllBuildings, render/scene_load Scene_LoadFromStream, etc.). Rather
// than re-pull those leaves here (and risk ODR + asset coupling), every host boundary —
// file IO, world reset, universe slot, scene load, form build/run, building lookup — is an
// installable Hooks struct with INERT DEFAULTS in the .cpp, exactly like main_menu_run.h.
// The reconstructed CONTROL FLOW + the exact path strings + the global-flag effects are
// faithful; the leaf work is delegated.
//
// ODR: defines no global owned elsewhere. The menu-side session flags this implies
// (word_63C740 / byte_63CC1D / dword_631614) are returned via MissionDialogResult; the
// menu (main_menu_run) owns its own copies. The dialog's own BSS words (dword_75BF38,
// byte_67225C, dword_672230, dword_62D22C, dword_649CD8) are modeled as the input/state
// of one reconstructed MissionDialogState, bit-for-bit.

#include <cstdint>

namespace guild::gui {

// ===========================================================================
// VIBE_Map_LoadCityFile @0x528bd0
// ===========================================================================

// The two path templates (gilde.exe aSSNet / aSSCty under aGamedataCities_0).
inline constexpr const char* kCitiesDir   = "gamedata/cities"; // aGamedataCities_0
inline constexpr const char* kCityNetFmt  = "%s/%s.NET";        // aSSNet (networkFlag != 0)
inline constexpr const char* kCityCtyFmt  = "%s/%s.CTY";        // aSSCty (networkFlag == 0)
inline constexpr const char* kChooseCityScene = "scenes/*ChooseCity.ed3"; // aScenesChooseci
inline constexpr const char* kCityChunkTag = "city"; // aCity (Save_WriteGameFile key)

// Host boundaries for Map_LoadCityFile (installable; inert defaults in the .cpp).
struct MapLoadCityHooks {
    virtual ~MapLoadCityHooks() = default;

    // VIBE_Universe_SwitchActiveSlot(0,0) @0x5b4a24 + VIBE_Universe_ResetCurrentSlot
    // @0x5b44c4 (called as a pair, both before and after the file write).
    virtual void UniverseSwitchAndReset() {}
    // VIBE_Scene_EnterCity @0x503a9c (receives the city-name pointer).
    virtual void SceneEnterCity(const char* cityName) { (void)cityName; }
    // VIBE_Save_WriteGameFile(path, "city", 0, 2) @0x5a348c — write the .CTY/.NET out.
    virtual void SaveWriteGameFile(const char* path, const char* tag, int a3, int mode) {
        (void)path; (void)tag; (void)a3; (void)mode;
    }
    // VIBE_Building_ResetAllBuildings @0x5896fc.
    virtual void BuildingResetAll() {}
    // VIBE_World_ResetPersonTable @0x58389c.
    virtual void WorldResetPersonTable() {}
    // VIBE_World_RelinkObjectOwners @0x5838d4.
    virtual void WorldRelinkObjectOwners() {}
    // VIBE_Object_DestroySpawnedEntities @0x4fff10.
    virtual void ObjectDestroySpawnedEntities() {}
    // VIBE_Scene_LoadFromStream("scenes/*ChooseCity.ed3", 0, .., 0) @0x5e7e38; the
    // original returns this byte. Inert default returns 1 (loaded).
    virtual int  SceneLoadFromStream(const char* path) { (void)path; return 1; }
};

MapLoadCityHooks* Map_SetLoadCityHooks(MapLoadCityHooks* hooks);

// A record of one Map_LoadCityFile run (the path built + the call order).
struct MapLoadCityRecord {
    char path[256] = {0};   // the formatted .CTY/.NET path passed to SaveWriteGameFile
    int  networkFlag = 0;   // the mode arg as seen
    bool wroteFile = false;
    bool resetWorld = false;
    static constexpr int kMaxTrace = 32;
    const char* trace[kMaxTrace];
    int  traceCount = 0;
};

// VIBE_Map_LoadCityFile @0x528bd0. networkFlag != 0 -> .NET, else .CTY. Returns the
// SceneLoadFromStream result byte (matches the original's `return`). `rec` optional.
int Map_LoadCityFile(int networkFlag, const char* cityName, MapLoadCityRecord* rec = nullptr);

// ===========================================================================
// VIBE_Menu_FormatMissionBuildingName @0x59b8cc
// ===========================================================================
struct MissionNameHooks {
    virtual ~MissionNameHooks() = default;
    // VIBE_Building_LookupTypeRecordA(typeByte, outBuf) @0x589778; returns the record size
    // (the original's qmemcpy length). Inert default returns 0 (nothing copied).
    virtual unsigned LookupBuildingTypeRecord(int typeByte, void* outBuf) {
        (void)typeByte; (void)outBuf; return 0;
    }
};
MissionNameHooks* Menu_SetMissionNameHooks(MissionNameHooks* hooks);

// The reconstructed BSS scratch the formatter fills (gilde.exe String / byte_122F4CA /
// dword_122F4A0.. / byte_122F4F0). Modeled locally; layout/seed values match.
struct MissionNameScratch {
    char     primary[128] = {0};   // String @0x122F4AA (2-byte-stride copy of src0)
    char     secondary[128] = {0}; // byte_122F4CA (2-byte-stride copy of src1)
    uint32_t d122F4A0 = 0;         // = 856692811
    uint32_t d122F4A4 = 0;         // = 1342
    uint8_t  b122F4A8 = 0;         // = 0
    uint8_t  b122F4A9 = 0;         // = 0
    uint32_t d122F528 = 0;         // = 1555
    char     record[256] = {0};    // byte_122F4F0 (qmemcpy of the type record)
    unsigned recordLen = 0;        // the LookupBuildingTypeRecord return / qmemcpy length
};

// VIBE_Menu_FormatMissionBuildingName @0x59b8cc. src0/src1 are the two wide-ish source
// strings (gilde.exe dword_8CA998 / dword_8CA99C); each is copied with a 2-byte stride.
// Returns the record length (the original returns the qmemcpy count).
unsigned Menu_FormatMissionBuildingName(const char* src0, const char* src1,
                                        MissionNameScratch& out);

// ===========================================================================
// VIBE_Menu_BuildChooseMissionDialog @0x59b998
// ===========================================================================
inline constexpr const char* kChooseMissionForm = "special\\CHOOSE_MISSION"; // aSpecialChooseM_0
inline constexpr int kMissionRowCount = 5;        // v23[0..4]
inline constexpr int kMissionFirstRowId = 7347;   // 0x1CB3 base for the 5 rows
inline constexpr int kMissionEnableId  = 7352;    // the "enable" indicator object (v15)
inline constexpr int kMissionOkId      = 7353;    // the OK button (v24)
inline constexpr int kMissionTitleId   = 0x1CB2;  // "$Z%s" title (7346)

// Host boundaries for the dialog form (installable; inert defaults in the .cpp).
struct MissionDialogHooks {
    virtual ~MissionDialogHooks() = default;
    // VIBE_GameTick_Finalize(0,0,"special\\CHOOSE_MISSION") @0x41beb8 -> form handle.
    virtual int  FormLoad() { return 1; }
    // VIBE_Form_CenterChildWindows @0x41d6ac.
    virtual void FormCenterChildWindows(int form) { (void)form; }
    // VIBE_Form_SelectWindow @0x41e4cc.
    virtual void FormSelectWindow(int form, int which) { (void)form; (void)which; }
    // VIBE_Text_RenderRichString @0x59d6e8 (the title + each "%ia[%s]" row label).
    virtual void TextRenderRichString(const char* fmt, int arg, const char* s) {
        (void)fmt; (void)arg; (void)s;
    }
    // VIBE_Form_GetChildObjectId @0x41dea8 -> the form object slot for a built row.
    virtual int  FormGetChildObjectId(int form) { (void)form; static int n = 0; return n++; }
    // VIBE_Object_SetEnabled @0x41e318 (the v15 "enable" indicator object).
    virtual void ObjectSetEnabled(int obj, int on) { (void)obj; (void)on; }
    // VIBE_GameLogic_RunFrameLoop(134,..) @0x4c09a0 — one tick; nonzero keeps running.
    virtual int  RunFrameLoop() { return 0; }
    // VIBE_Form_Destroy @0x41da04.
    virtual void FormDestroy(int form) { (void)form; }

    // ---- the form object-table fields the loop reads/writes (dword_69FFB4 + 740*slot) ----
    // +36 = the row's "checked" value (the original reads it as the selected state and as
    //       the seed copied from dword_649CD8 at build time).
    virtual int  GetRowChecked(int objSlot) { (void)objSlot; return 0; }
    virtual void SetRowChecked(int objSlot, int val) { (void)objSlot; (void)val; }
    // +68 = 1 (the row is enabled) ; +444 &= ~2 (clear a flag bit) at build time.
    virtual void SetRowEnabled(int objSlot) { (void)objSlot; }
    virtual void ClearRowFlag2(int objSlot) { (void)objSlot; }

    // ---- the click-edge + escape source (dword_62D22C / dword_672230 / byte_67225C /
    //      dword_75BF38) the loop dispatches on ----
    virtual int  HoverId(int frame) { (void)frame; return -1; }   // dword_62D22C
    virtual bool CloseRequested(int frame) { (void)frame; return false; } // dword_672230 || byte_67225C==1 || dword_75BF38==1155
    virtual int  DialogResult(int frame) { (void)frame; return -1; } // dword_75BF38 (-1 keeps the dialog open)
};

MissionDialogHooks* Menu_SetMissionDialogHooks(MissionDialogHooks* hooks);

// Reconstructed dialog state (the dialog's own BSS words) + the result the menu reads.
struct MissionDialogResult {
    // The five mission rows' final selection (gilde.exe dword_649CD8[0..4]). Latched on OK.
    int  selection[kMissionRowCount] = {0,0,0,0,0};
    int  selectedCount = 0;   // how many rows were checked on OK (>0 => result 1)

    // Built object slots (FormGetChildObjectId return per row + the two extras).
    int  rowObj[kMissionRowCount] = {-1,-1,-1,-1,-1};
    int  enableObj = -1;      // v15
    int  okObj = -1;          // v24

    // The menu-side flags a nonzero return IMPLIES (set by the menu, recorded here so the
    // dialog test tier can assert the contract). These match the @0x52a5c5 branch.
    bool impliesMissionFlags = false; // true iff return == 1
    int  word_63C740 = 0;     // -> 137 on success
    int  byte_63CC1D = 0;     // -> 1 on success
    int  dword_631614 = 0;    // -> 1 on success

    int  frames = 0;
    static constexpr int kMaxTrace = 64;
    const char* trace[kMaxTrace];
    int  traceCount = 0;
};

// VIBE_Menu_BuildChooseMissionDialog @0x59b998. `seed[0..4]` are the dword_649CD8 values
// copied into each row's +36 at build time (the previously-saved selection). Returns 1 if
// OK was pressed with >=1 row checked (latches the new selection into res.selection), else
// 0. `maxFrames` bounds the headless loop. Fills the implied menu-side flags on success.
int Menu_BuildChooseMissionDialog(const int seed[kMissionRowCount],
                                  MissionDialogResult& res, int maxFrames);

} // namespace guild::gui
