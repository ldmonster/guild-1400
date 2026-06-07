#include "gui/choosecity_run.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace guild::gui {

// ===========================================================================
// Inert default hooks (defined here so the pair links in the unified build headless).
// ===========================================================================
namespace {

ChooseCityHooks  g_defaultHooks;
ChooseCityHooks* g_hooks = &g_defaultHooks;

void Trace(ChooseCityRecord* rec, const char* tag) {
    if (rec && rec->traceCount < ChooseCityRecord::kMaxTrace)
        rec->trace[rec->traceCount++] = tag;
}

// VIBE_Util_StrToUpper (0x5e9f50) — the key strings are upper-cased before FindTextArrayIndex.
std::string ToUpper(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// VIBE_Crt_Sprintf_0(buf, fmt, name) — the per-city "%s" key builders.
std::string FormatKey(const char* fmt, const std::string& name) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), fmt, name.c_str());
    return std::string(buf);
}

} // namespace

ChooseCityHooks* Menu_SetChooseCityHooks(ChooseCityHooks* hooks) {
    ChooseCityHooks* prev = g_hooks;
    g_hooks = hooks ? hooks : &g_defaultHooks;
    return prev;
}

// ===========================================================================
// gilde.exe 0x52e6d8 — VIBE_Menu_RunChooseCity.
// ===========================================================================
int Menu_RunChooseCity(ChooseCityState& st, ChooseCityRecord* rec, int maxFrames) {
    ChooseCityHooks* h = g_hooks;

    st.close = 0;          // dword_631614 starts clear for this screen
    st.result = 0;         // v80 = 0
    st.pickedObject = 0;
    st.hoverCity = 0;
    st.hoverCityName.clear();

    // ---- scene preamble (0x52e6ea..0x52e742) ----
    h->SceneSetup();       Trace(rec, "SceneSetup");

    // ---- city-file enumeration (0x52e75c + the per-file body @0x52e797) ----
    // v5 = ".CTY"/".NET" depending on the network flag (selected upstream; modelled here
    // via ChooseCity_Extension, REUSED from gui/newgame_setup).
    const char* ext = ChooseCity_Extension(st.network);
    if (rec) rec->extension = ext;

    std::vector<std::string> files;
    int count = h->EnumerateCityFiles(kCityDir, ext, files); // 0x52e75c
    Trace(rec, "Enumerate");

    std::vector<CityEntry> cities;
    if (count > 0) {
        for (int i = 0; i < count && i < static_cast<int>(files.size()); ++i) {
            CityEntry e;
            e.file = files[i];
            // Vfs_OpenFile + Save_LoadHeaderAndThumbnail -> city name (0x52e7c5).
            if (!h->ReadCityName(e.file, e.name))
                continue;                                 // open/header failure: skip
            // Map_SpawnCityPointMarker(name) (0x52e7d9).
            e.marker = h->SpawnCityMarker(e.name);
            if (e.marker) {                               // if (v13)
                // "_STADTAUSWAHL_<NAME>_INFO+0" -> upper -> FindTextArrayIndex (0x52e802).
                std::string infoKey = ToUpper(FormatKey(kCityInfoKeyFmt, e.name));
                e.infoText = h->FindTextIndex(infoKey);
                // StatusText_Register("stadt_<name>", info text) (0x52e86f / 0x52e8a8).
                std::string statusKey = FormatKey(kCityStatusKeyFmt, e.name);
                std::string infoText = (e.infoText != -1) ? infoKey : e.name;
                h->RegisterStatusText(statusKey, infoText);
                e.registered = true;
            }
            cities.push_back(std::move(e));
        }
    }
    if (rec) rec->cities = cities;
    Trace(rec, "MarkersSpawned");

    // ---- scene + cutscene + form build (LABEL_15, 0x52e8c5..0x52e9c0) ----
    st.result = 0;
    int tower = h->SpawnCityTower();          Trace(rec, "SpawnTower"); // sp_STADTTURM
    if (rec) rec->towerObject = tower;
    h->RunIntroCutscene();                    Trace(rec, "Cutscene");   // A_Stadtwahl.esc

    int headerForm = h->FormLoad(kFormChooseCityHeader); // v81 (0x52e912)
    h->FormPositionAndSelect(headerForm, 0);
    int mainForm = h->FormLoad(kFormChooseCity);         // v85 (0x52e99a)
    h->FormPositionAndSelect(mainForm, 2);
    if (rec) { rec->headerForm = headerForm; rec->mainForm = mainForm; }
    Trace(rec, "FormsBuilt");

    // ---- main frame loop (0x52e9c8) ----
    Trace(rec, "LoopBegin");
    int frame = 0;
    int lastHoverObject = 0;   // v79 — drives the hover-change render
    for (;;) {
        if (!h->RunFrameLoop(frame))          // while (RunFrameLoop(4310,...))
            break;

        // --- pick the nearest object under the cursor (0x52ea03) ---
        st.hoverCity = 0;                                  // dword_631720 = 0
        int picked = h->PickNearestObject(frame);         // FindNearestObjectAt(...,96,1)
        st.pickedObject = picked;                          // dword_631724
        bool isHover = h->PickIsHover(frame);              // dword_67221C
        int key = h->KeyCode(frame);                       // byte_67225C

        if (isHover) {
            st.hoverCity = picked;                          // dword_631720 = picked
        } else if (h->RightClick(frame) || key == kKeyEsc) { // dword_672230 || esc(1)
            st.close = 1;                                   // dword_631614 = 1 (cancel)
        }

        // --- is the hovered object a "stadt_" city marker? (0x52ea27) ---
        int hoverObject = lastHoverObject;
        if (st.hoverCity) {
            std::string name = h->ObjectName(st.hoverCity);
            if (ChooseCity_IsCityObject(name)) {            // StrncmpN(name,"stadt_",6)==0
                hoverObject = st.hoverCity;
                st.hoverCityName = name.substr(std::strlen(kCityNamePrefix));
            }
        }

        // --- on hover-change, render the city's $C name + description (0x52ea49) ---
        if (hoverObject != lastHoverObject) {
            int infoText = -1;
            for (const CityEntry& e : cities)
                if (e.marker == hoverObject) { infoText = e.infoText; break; }
            h->RenderCityInfo(st.hoverCityName, infoText);  // Text_RenderRichString($C + ...)
            lastHoverObject = hoverObject;
            Trace(rec, "HoverChange");
        }

        // --- confirm? (dword_75BF38 == 1210 || enterKey 28) (0x52ed4e) ---
        int clickedId = h->ClickedWidgetId(frame);          // dword_75BF38
        if (ChooseCity_IsConfirm(clickedId, key)) {         // 1210 || 28 (REUSED predicate)
            h->FormSetVisible(mainForm, 0);                 // hide forms (0x52ebe9)
            h->FormSetVisible(headerForm, 0);
            if (st.network) {                               // if (v76) — already-picked path
                st.close = 1;                               // dword_631614 = 1
                st.result = 1;                              // v80 = 1
                if (rec) { rec->confirmed = true; rec->confirmedCity = st.hoverCityName; }
            } else {
                // chain: ChooseCharacterIntroVariant -> RunChooseHistory (0x52ed59).
                if (h->ChooseCharacterIntroVariant()) {     // 0x52e4e0
                    if (rec) rec->chainIntroRan = true;
                    Trace(rec, "Chain:Intro");
                    if (h->RunChooseHistory()) {            // 0x52d684
                        if (rec) rec->chainHistoryRan = true;
                        Trace(rec, "Chain:History");
                        st.close = 1;                       // dword_631614 = 1
                        st.result = 1;                      // v80 = 1
                        h->FormSetVisible(mainForm, 1);     // re-show main (0x52edad)
                        if (rec) { rec->confirmed = true;
                                   rec->confirmedCity = st.hoverCityName; }
                    }
                }
                // re-show forms (0x52ed6e / 0x52ed7f) — runs whether or not the chain took.
                h->FormSetVisible(mainForm, 1);
                h->FormSetVisible(headerForm, 1);
            }
            if (st.result) { Trace(rec, "Confirmed"); break; } // chain done -> loop exits
        }

        // cancel edge recorded (the loop still runs to its RunFrameLoop bound).
        if (st.close && !st.result && rec && !rec->cancelled) {
            rec->cancelled = true;
            Trace(rec, "Cancelled");
        }

        ++frame;
        if (rec) rec->frames = frame;
        if (maxFrames >= 0 && frame >= maxFrames) break;     // headless bound
    }
    Trace(rec, "LoopEnd");

    // ---- cleanup (0x52edbe..0x52ee24) ----
    if (mainForm != -1)   h->FormDestroy(mainForm);          // if (v85 != -1)
    if (headerForm != -1) h->FormDestroy(headerForm);        // if (v81 != -1)
    if (tower)            h->DestroyCityTower(tower);         // if (v78)
    for (const CityEntry& e : cities)                        // detach each marker
        if (e.marker) h->DestroyMarker(e.marker);
    Trace(rec, "Cleanup");

    return st.result;                                        // return v80
}

// ===========================================================================
// gilde.exe 0x52ee38 — VIBE_Menu_EnterChooseCity.
// ===========================================================================
int Menu_EnterChooseCity(ChooseCityState& st, ChooseCityRecord* rec, int maxFrames) {
    ChooseCityHooks* h = g_hooks;

    st.sessionFlags = 0;                       // word_63C740 = 0  (0x52ee3d)
    h->DragCursorSetSprite(0);                 // DragCursor_SetSprite(this,0) (0x52ee46)
    st.enterMarker = kEnterChooseCityMarker;   // byte_63CC1D = 1   (0x52ee4f)

    int result = Menu_RunChooseCity(st, rec, maxFrames); // 0x52ee5a

    if (!result) {                             // if (!result)  (0x52ee63)
        h->SurfaceColorFill();                 // Surface_ColorFill(dword_62D210) (0x52ee6e)
        h->RenderEntityList(kCancelRenderList);// Window_RenderEntityList(1773)   (0x52ee78)
    }
    return result;                             // return result  (0x52ee68 / 0x52ee7d)
}

} // namespace guild::gui
