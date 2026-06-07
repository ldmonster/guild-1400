#include "gui/loadgame_run.h"

// guild::gui — VIBE_Menu_RunLoadGame @0x56a270 (the load-game / save-browser screen).
// See loadgame_run.h for the recovery notes. This module owns the full function body
// (preamble build + frame loop + slot-match dispatch + cleanup) and routes the host
// boundaries through inert hooks; it REUSES the already-reconstructed leaf models for the
// SAV path build and the save-file enumeration / slot-list build.

#include "gui/savebrowser.h"   // SaveBrowser_EnumerateSaveFiles / SaveBrowser_BuildSlots
                               // + SaveFileEntry / SaveMetadata / SaveSlot (REUSED model)

namespace guild::gui {

// loadgame.h also declares a gui::SaveSlot (a DIFFERENT row view) which would clash with
// savebrowser.h's gui::SaveSlot if both headers were included in one TU. We only need the
// path builder from loadgame.{h,cpp}, so forward-declare it rather than include the header.
// gilde.exe 0x56a3f5 — Crt_Sprintf(buf, "Gamedata\\Saves\\%s.SAV", name).
std::string LoadGame_BuildPath(const std::string& slotName);

// ===========================================================================
// Inert default hooks. The default LoadSlotMetadata reuses the real reconstructed
// enumeration + slot-build models (SaveBrowser_EnumerateSaveFiles / _BuildSlots) over an
// empty file listing, so headless it yields a 16-slot grid of placeholders (no real save
// dir is touched) — exactly the original's "no .SAV files" outcome.
// ===========================================================================
namespace {

struct DefaultRunHooks : LoadGameRunHooks {
    void LoadSlotMetadata(int /*form*/, const char* /*saveDir*/,
                          std::vector<LoadGameSlot>& outSlots) override {
        // Drive the real reconstructed list build with an empty listing (no .SAV files
        // visible headless) -> 16 placeholder slots (occupied == false).
        std::vector<SaveFileEntry> files =
            SaveBrowser_EnumerateSaveFiles(kRunLoadSaveDir, /*files*/ {}, ".SAV");
        std::vector<SaveMetadata> meta;
        meta.reserve(files.size());
        for (const SaveFileEntry& f : files) {
            SaveMetadata m;
            m.path = f.fullPath;
            m.label = f.displayName;
            meta.push_back(std::move(m));
        }
        std::vector<SaveSlot> built = SaveBrowser_BuildSlots(meta);
        outSlots.clear();
        outSlots.reserve(built.size());
        for (const SaveSlot& s : built) {
            LoadGameSlot row;
            row.present = s.occupied;     // row[12]
            row.objId  = s.occupied ? 0 : -1;  // row+8 (-1 == empty placeholder)
            row.name   = s.label;         // &row[25]
            row.widgetId = -1;            // no real AddChildWindow id headless
            outSlots.push_back(std::move(row));
        }
    }
};

DefaultRunHooks g_defaultRunHooks;
LoadGameRunHooks* g_runHooks = &g_defaultRunHooks;

void Trace(LoadGameRunRecord* rec, const char* tag) {
    if (rec && rec->traceCount < LoadGameRunRecord::kMaxTrace)
        rec->trace[rec->traceCount++] = tag;
}

} // namespace

LoadGameRunHooks* LoadGame_SetRunHooks(LoadGameRunHooks* hooks) {
    LoadGameRunHooks* prev = g_runHooks;
    g_runHooks = hooks ? hooks : &g_defaultRunHooks;
    return prev;
}

// gilde.exe 0x56a392 — the slot scan (the inner while(1) of the click branch).
int LoadGameRun_MatchSlot(const std::vector<LoadGameSlot>& slots, int hoveredWidgetId) {
    for (int i = 0; i < static_cast<int>(slots.size()); ++i) {
        const LoadGameSlot& s = slots[i];
        if (!s.present)                       // v15[v8+12] == 0  -> skip row
            continue;
        if (s.objId == -1)                    // *(v15+v8+8) == -1 -> empty -> skip row
            continue;
        if (s.widgetId == hoveredWidgetId)    // dword_62D22C == *(v15+v8+4) -> hit
            return i;
        // (The original's secondary obj-id branch here only re-targets the active save id
        // dword_75BF08 and never selects a different row; it falls through to the next.)
    }
    return -1;
}

// ===========================================================================
// gilde.exe 0x56a270 — the full body.
// ===========================================================================
int Menu_RunLoadGame(LoadGameRunState& st, LoadGameRunRecord* rec, int maxFrames) {
    LoadGameRunHooks* h = g_runHooks;

    int v20 = 0;  // 0x56a28b: v20 = 0 (nothing chosen yet)

    // ---- preamble / build ------------------------------------------------------
    int form = h->FormLoad(kRunLoadGameForm);  // 0x56a29c GameTick_Finalize
    Trace(rec, "FormLoad");
    h->FormPosition(form);                     // 0x56a2a5..0x56a2d9 position/select/colors
    Trace(rec, "FormPosition");
    h->RenderTitle(kRunLoadGameTitle);         // 0x56a2e3 RenderRichString(6244)
    Trace(rec, "RenderTitle");

    int win = h->GetWindowId(form);            // 0x56a318 Form_GetWindowId(form,1)
    h->BuildSliderPanel(kRunLoadPanelW, kRunLoadPanelH, win, kRunLoadPanelRows); // 0x56a32f
    Trace(rec, "BuildSliderPanel");

    std::vector<LoadGameSlot> slots;
    h->LoadSlotMetadata(form, kRunLoadSaveDir, slots);  // 0x56a34d
    Trace(rec, "LoadSlotMetadata");

    if (rec) {
        rec->form = form;
        rec->listWindow = win;
        rec->sliderBuilt = true;
        rec->sliderW = kRunLoadPanelW;
        rec->sliderH = kRunLoadPanelH;
        rec->sliderRows = kRunLoadPanelRows;
        rec->titleId = kRunLoadGameTitle;
        rec->slotCount = static_cast<int>(slots.size());
        rec->occupiedSlots = 0;
        for (const LoadGameSlot& s : slots)
            if (s.present) ++rec->occupiedSlots;
    }

    // ---- frame loop ------------------------------------------------------------
    int frame = 0;
    while (h->RunFrameLoop()) {                 // 0x56a360 RunFrameLoop
        if (h->CloseRequested(frame))           // 0x56a36d dword_672230
            st.close = 1;                       // 0x56a36f dword_631614 = 1

        if (h->ClickEdge(frame)) {              // 0x56a380 dword_672228
            int slot = LoadGameRun_MatchSlot(slots, h->HoverId(frame)); // 0x56a392
            if (slot >= 0) {
                if (rec) rec->matchedSlot = slot;
                // 0x56a49f: if (!byte_63CC40 || RunMessageBox(257)) accept.
                bool accept = true;
                if (st.confirmGate) {
                    if (rec) rec->confirmAsked = true;
                    accept = h->ConfirmLoad();  // 0x56a49f RunMessageBox(257)
                    Trace(rec, "ConfirmLoad");
                }
                if (accept) {
                    // 0x56a3f5: build "Gamedata\\Saves\\%s.SAV" + copy into byte_122F530.
                    st.loadPath = LoadGame_BuildPath(slots[slot].name);
                    st.sessionFlags = kRunSessLoad; // 0x56a426 word_63C740 = 10
                    st.close = 1;                   // 0x56a42d dword_631614 = 1
                    v20 = 1;                        // 0x56a432 v20 = 1
                    Trace(rec, "Pick");
                    break;  // dword_631614 set -> next RunFrameLoop ends the loop
                }
            }
        }

        ++frame;
        if (maxFrames > 0 && frame >= maxFrames)
            break;  // headless test guard (not in the original)
    }

    // ---- cleanup ---------------------------------------------------------------
    h->FormDestroy(form);   // 0x56a4b8 Form_Destroy(v21)
    Trace(rec, "FormDestroy");
    if (rec) rec->frames = frame;

    return v20;             // 0x56a4c4 return v20
}

} // namespace guild::gui
