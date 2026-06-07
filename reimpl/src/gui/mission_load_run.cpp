#include "gui/mission_load_run.h"

#include <cstdio>
#include <cstring>

namespace guild::gui {

// ---- trace helpers ----------------------------------------------------------
static void TraceCity(MapLoadCityRecord* rec, const char* tag) {
    if (rec && rec->traceCount < MapLoadCityRecord::kMaxTrace)
        rec->trace[rec->traceCount++] = tag;
}
static void TraceDlg(MissionDialogResult* res, const char* tag) {
    if (res && res->traceCount < MissionDialogResult::kMaxTrace)
        res->trace[res->traceCount++] = tag;
}

// ===========================================================================
// Map_LoadCityFile hooks (inert defaults).
// ===========================================================================
namespace {
struct DefaultMapLoadCityHooks : MapLoadCityHooks {};
DefaultMapLoadCityHooks g_defaultMapHooks;
MapLoadCityHooks* g_mapHooks = &g_defaultMapHooks;
} // namespace

MapLoadCityHooks* Map_SetLoadCityHooks(MapLoadCityHooks* hooks) {
    MapLoadCityHooks* prev = g_mapHooks;
    g_mapHooks = hooks ? hooks : &g_defaultMapHooks;
    return prev;
}

// gilde.exe 0x528bd0 — VIBE_Map_LoadCityFile.
// __usercall: eax = cityName, edx = networkFlag. (See both menu call sites @0x52a6ad
// edx=0/.CTY, @0x52a810 edx=1/.NET; eax = the "Stadtname" buffer.)
int Map_LoadCityFile(int networkFlag, const char* cityName, MapLoadCityRecord* rec) {
    MapLoadCityHooks* h = g_mapHooks;
    if (rec) { rec->networkFlag = networkFlag; rec->path[0] = '\0'; }

    // VIBE_Universe_SwitchActiveSlot(0,0); VIBE_Universe_ResetCurrentSlot();
    h->UniverseSwitchAndReset();
    TraceCity(rec, "universe.reset.pre");

    // VIBE_Scene_EnterCity(cityName);  (eax = ecx = the city-name pointer)
    h->SceneEnterCity(cityName);
    TraceCity(rec, "scene.entercity");

    // if (networkFlag) sprintf(path,"%s/%s.NET",dir,name) else "%s/%s.CTY".
    char path[256];
    const char* name = cityName ? cityName : "";
    std::snprintf(path, sizeof(path), networkFlag ? kCityNetFmt : kCityCtyFmt,
                  kCitiesDir, name);
    if (rec) std::snprintf(rec->path, sizeof(rec->path), "%s", path);
    TraceCity(rec, networkFlag ? "fmt.net" : "fmt.cty");

    // qword_13CE852: LODWORD=0; WORD2=6; *(dword*)(+6)=0  — a 10-byte chunk header the
    // writer reads (low dword 0, the word at +4 = 6, the dword at +6 = 0). Modeled inline;
    // it is consumed by SaveWriteGameFile in the original. (No shared home; behavior is
    // "tag the write as chunk-kind 6"; faithfully passed as the write below.)

    // VIBE_Save_WriteGameFile(path, "city", 0, 2);
    h->SaveWriteGameFile(path, kCityChunkTag, 0, 2);
    if (rec) rec->wroteFile = true;
    TraceCity(rec, "save.writegamefile");

    // VIBE_Building_ResetAllBuildings();
    h->BuildingResetAll();
    // VIBE_World_ResetPersonTable();
    h->WorldResetPersonTable();
    // VIBE_World_RelinkObjectOwners();
    h->WorldRelinkObjectOwners();
    // VIBE_Object_DestroySpawnedEntities();
    h->ObjectDestroySpawnedEntities();
    if (rec) rec->resetWorld = true;
    TraceCity(rec, "world.reset");

    // VIBE_Universe_SwitchActiveSlot(0,0); VIBE_Universe_ResetCurrentSlot();
    h->UniverseSwitchAndReset();
    TraceCity(rec, "universe.reset.post");

    // return VIBE_Scene_LoadFromStream("scenes/*ChooseCity.ed3", 0, 0, 0);
    int r = h->SceneLoadFromStream(kChooseCityScene);
    TraceCity(rec, "scene.loadfromstream");
    return r;
}

// ===========================================================================
// FormatMissionBuildingName hooks (inert defaults).
// ===========================================================================
namespace {
struct DefaultMissionNameHooks : MissionNameHooks {};
DefaultMissionNameHooks g_defaultNameHooks;
MissionNameHooks* g_nameHooks = &g_defaultNameHooks;
} // namespace

MissionNameHooks* Menu_SetMissionNameHooks(MissionNameHooks* hooks) {
    MissionNameHooks* prev = g_nameHooks;
    g_nameHooks = hooks ? hooks : &g_defaultNameHooks;
    return prev;
}

// gilde.exe 0x59b8cc — VIBE_Menu_FormatMissionBuildingName.
// Copies dword_8CA998 -> String and dword_8CA99C -> byte_122F4CA, each with a 2-byte
// stride (the original walks src/dst by 2 bytes, stopping at a NUL), seeds the scratch
// dwords, then looks up the building type record and qmemcpys it.
unsigned Menu_FormatMissionBuildingName(const char* src0, const char* src1,
                                        MissionNameScratch& out) {
    out = MissionNameScratch{};

    // dword_122F4A0 = 856692811; dword_122F528 = 1555; byte_122F4A8 = 0;
    // byte_122F4A9 = 0; dword_122F4A4 = 1342;
    out.d122F4A0 = 856692811u;
    out.d122F528 = 1555u;
    out.b122F4A8 = 0;
    out.b122F4A9 = 0;
    out.d122F4A4 = 1342u;

    // 2-byte-stride copy of src0 into String.
    {
        const char* s = src0 ? src0 : "";
        char* d = out.primary;
        size_t cap = sizeof(out.primary);
        size_t di = 0, si = 0;
        for (;;) {
            char c = s[si];                 // v3 = *v2
            if (di < cap) d[di] = c;        // *v1 = *v2
            if (!c) break;                  // if (!v3) break;
            char c1 = s[si + 1];            // v4 = v2[1]
            si += 2;                        // v2 += 2
            if (di + 1 < cap) d[di + 1] = c1; // v1[1] = v4
            di += 2;                        // v1 += 2
            if (!c1) break;                 // while (v4)
        }
    }
    // 2-byte-stride copy of src1 into byte_122F4CA.
    {
        const char* s = src1 ? src1 : "";
        char* d = out.secondary;
        size_t cap = sizeof(out.secondary);
        size_t di = 0, si = 0;
        for (;;) {
            char c = s[si];
            if (di < cap) d[di] = c;
            if (!c) break;
            char c1 = s[si + 1];
            si += 2;
            if (di + 1 < cap) d[di + 1] = c1;
            di += 2;
            if (!c1) break;
        }
    }

    // VIBE_Building_LookupTypeRecordA(SHIBYTE(dword_122F4A0), out); qmemcpy(byte_122F4F0,..);
    int typeByte = static_cast<int>((out.d122F4A0 >> 24) & 0xFF); // SHIBYTE
    unsigned len = g_nameHooks->LookupBuildingTypeRecord(typeByte, out.record);
    if (len > sizeof(out.record)) len = sizeof(out.record);
    out.recordLen = len;
    return len;
}

// ===========================================================================
// BuildChooseMissionDialog hooks (inert defaults).
// ===========================================================================
namespace {
struct DefaultMissionDialogHooks : MissionDialogHooks {};
DefaultMissionDialogHooks g_defaultDlgHooks;
MissionDialogHooks* g_dlgHooks = &g_defaultDlgHooks;
} // namespace

MissionDialogHooks* Menu_SetMissionDialogHooks(MissionDialogHooks* hooks) {
    MissionDialogHooks* prev = g_dlgHooks;
    g_dlgHooks = hooks ? hooks : &g_defaultDlgHooks;
    return prev;
}

// gilde.exe 0x59b998 — VIBE_Menu_BuildChooseMissionDialog.
int Menu_BuildChooseMissionDialog(const int seed[kMissionRowCount],
                                  MissionDialogResult& res, int maxFrames) {
    MissionDialogHooks* h = g_dlgHooks;
    res = MissionDialogResult{};

    int v25 = 0; // the return value (success latch)

    // v1 = GameTick_Finalize(0,0,"special\\CHOOSE_MISSION");
    int form = h->FormLoad();
    TraceDlg(&res, "form.load");
    // VIBE_Form_CenterChildWindows(v1);
    h->FormCenterChildWindows(form);
    // VIBE_Form_SelectWindow(...);  (title window)
    h->FormSelectWindow(form, 0);
    // VIBE_Text_RenderRichString("$Z%s", 0x1CB2);  (the title)
    h->TextRenderRichString("$Z%s", kMissionTitleId, "");
    TraceDlg(&res, "title");
    // VIBE_Form_SelectWindow(...);  (the rows window)
    h->FormSelectWindow(form, 1);

    // do { build the 5 rows ids 7347.. } while (i != 5)
    int objSlot[kMissionRowCount];
    for (int i = 0; i < kMissionRowCount; ++i) {
        int rowId = kMissionFirstRowId + i;                 // v4 = 7347 + i
        h->TextRenderRichString("%ia[%s]$N", rowId, "");    // v7 = RenderRichString
        int slot = h->FormGetChildObjectId(form);           // ChildObjectId
        objSlot[i] = slot;
        res.rowObj[i] = slot;
        h->SetRowEnabled(slot);                             // +68 = 1
        h->SetRowChecked(slot, seed ? seed[i] : 0);        // +36 = dword_649CD8[i]
        h->ClearRowFlag2(slot);                            // +444 &= ~2
    }
    TraceDlg(&res, "rows.built");

    // v13 = RenderRichString("%ia[%s]",7352); v15 = GetChildObjectId  (enable indicator)
    h->TextRenderRichString("%ia[%s]$N", kMissionEnableId, "");
    int v15 = h->FormGetChildObjectId(form);
    res.enableObj = v15;
    // v16 = RenderRichString("%ia[%s]",7353); v24 = GetChildObjectId  (OK button)
    h->TextRenderRichString("%ia[%s]$N", kMissionOkId, "");
    int v24 = h->FormGetChildObjectId(form);
    res.okObj = v24;
    TraceDlg(&res, "extras.built");

    // VIBE_Form_SelectWindow(v1, 2);
    h->FormSelectWindow(form, 2);

    // while (RunFrameLoop(134,..))
    int frame = 0;
    while ((maxFrames < 0 || frame < maxFrames) && h->RunFrameLoop()) {
        ++frame;

        // Scan rows: if any +36 != 0 -> SetEnabled(v15,1); if none (v18==5) -> SetEnabled(v15,0).
        int v18 = 0;
        bool anyChecked = false;
        for (int v19 = 0; v19 < kMissionRowCount; ++v19) {
            ++v18;
            if (h->GetRowChecked(objSlot[v19])) { anyChecked = true; break; }
        }
        if (anyChecked) h->ObjectSetEnabled(v15, 1);
        if (v18 == kMissionRowCount && !anyChecked) h->ObjectSetEnabled(v15, 0);

        int hover = h->HoverId(frame - 1);     // dword_62D22C
        int result = h->DialogResult(frame - 1); // dword_75BF38

        // if (dword_672230 || byte_67225C==1 || dword_75BF38==1155) dword_631614 = 1;
        if (h->CloseRequested(frame - 1)) res.dword_631614 = 1;

        // if (dword_75BF38 != -1) { ... }
        if (result != -1) {
            if (hover == v24) {                // OK button clicked
                res.dword_631614 = 1;
                TraceDlg(&res, "ok.clicked");
            } else if (v15 == hover) {         // the enable/confirm object clicked
                res.dword_631614 = 1;
                int v20 = 0;
                for (int i = 0; i < kMissionRowCount; ++i) {
                    if (h->GetRowChecked(objSlot[i])) {
                        ++v20;
                        res.selection[i] = 1;  // dword_649CD8[i] = 1
                    } else {
                        res.selection[i] = 0;  // dword_649CD8[i] = 0
                    }
                }
                if (v20) v25 = 1;              // success
                res.selectedCount = v20;
                TraceDlg(&res, "confirm.clicked");
            }
        }
    }
    res.frames = frame;

    // VIBE_Form_Destroy(v1);
    h->FormDestroy(form);
    TraceDlg(&res, "form.destroy");

    // The menu-side contract @0x52a5c5: nonzero return -> word_63C740=137, byte_63CC1D=1,
    // dword_631614=1 (recorded here so the dialog test tier can assert the implication).
    if (v25) {
        res.impliesMissionFlags = true;
        res.word_63C740 = 137;   // 0x89
        res.byte_63CC1D = 1;
        res.dword_631614 = 1;
    }
    return v25;
}

} // namespace guild::gui
