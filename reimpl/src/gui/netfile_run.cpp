#include "gui/netfile_run.h"

#include "gui/savebrowser.h"  // SaveBrowser_EnumerateSaveFiles (REAL reused sibling)

namespace guild::gui {

// ===========================================================================
// Inert default hooks (no-ops) so both bodies link in the unified build and run
// headless. Tests install recording/scripting overrides.
// ===========================================================================
namespace {

struct DefaultNetModeHooks : NetModeHooks {};
DefaultNetModeHooks g_defaultNetModeHooks;
NetModeHooks* g_netModeHooks = &g_defaultNetModeHooks;

struct DefaultFileSelHooks : FileSelHooks {};
DefaultFileSelHooks g_defaultFileSelHooks;
FileSelHooks* g_fileSelHooks = &g_defaultFileSelHooks;

void NetTrace(NetModeRecord* rec, const char* tag) {
    if (rec && rec->traceCount < NetModeRecord::kMaxTrace)
        rec->trace[rec->traceCount++] = tag;
}
void FileTrace(FileSelRecord* rec, const char* tag) {
    if (rec && rec->traceCount < FileSelRecord::kMaxTrace)
        rec->trace[rec->traceCount++] = tag;
}

// VIBE_Util_StripPathAndExt-style: the file-selector row label / committed token is the
// bare name with everything from the first '.' dropped (matches savebrowser's StripExt).
// SaveBrowser_EnumerateSaveFiles keeps the extension on `displayName`, so strip it here
// the way the original file selector renders/commits the name field (v25+1).
std::string BareName(const std::string& name) {
    std::string::size_type dot = name.find('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

} // namespace

NetModeHooks* SetNetModeHooks(NetModeHooks* hooks) {
    NetModeHooks* prev = g_netModeHooks;
    g_netModeHooks = hooks ? hooks : &g_defaultNetModeHooks;
    return prev;
}

FileSelHooks* SetFileSelHooks(FileSelHooks* hooks) {
    FileSelHooks* prev = g_fileSelHooks;
    g_fileSelHooks = hooks ? hooks : &g_defaultFileSelHooks;
    return prev;
}

// ===========================================================================
// gilde.exe 0x529a64 — VIBE_Menu_ChooseNetworkMode
// ===========================================================================
//   v1 = GameTick_Finalize(0,0,"Menu\\CHOOSENETWORK"); CenterChildWindows; SelectWindow(_,0)
//   RenderRichString(0x18B2)                       // title
//   ChildObjectId = GetChildObjectId(RenderRichString(0x18B3))  // host radio
//   v9          = GetChildObjectId(RenderRichString(0x18B4))    // search radio
//   v20         = GetChildObjectId(RenderRichString(0x18B5))    // profile radio
//   v13 = RadioGroup_Create(3, ChildObjectId); byte_63C8F4 = -2;
//   while (RunFrameLoop(147591, v13, v9)) {
//     InitStateReader(v13);
//     if (dword_672230 || byte_67225C == 1) { v12 = 0; dword_631614 = 1; }   // cancel
//     if (dword_75BF38 != -1) {                                              // click ready
//       if (host == dword_62D22C)     { word_63C740=5; hide; if(RunHostNetworkSetup){dword_631614=1;v12=1;} show; }
//       else if (search == dword_62D22C){ word_63C740=5; hide; if(SearchNetworkGames(0)){dword_631614=1;v12=1;} show; }
//       else if (profile == dword_62D22C){ word_63C740=4; hide; if(ChooseNetworkProfile()){v12=1;dword_631614=1;} show; }
//     }
//   }
//   RadioGroup_FreeSurface(v13); Form_Destroy(v1); return v12;
int Menu_ChooseNetworkMode(NetModeState& st, NetModeRecord* rec, int maxFrames) {
    NetModeHooks* h = g_netModeHooks;
    int v12 = 0; // function result (v12 in the decompile)

    // ---- form build ----
    int form = h->FormLoad();
    NetTrace(rec, "FormLoad");
    h->FormCenterChildWindows(form);
    h->FormSelectWindow(form, 0);

    h->RenderRichString(kNetModeTextTitle);                                  // 0x18B2
    int idHost    = h->GetChildObjectId(form, h->RenderRichString(kNetModeTextHost));    // 0x18B3
    int idSearch  = h->GetChildObjectId(form, h->RenderRichString(kNetModeTextSearch));  // 0x18B4
    int idProfile = h->GetChildObjectId(form, h->RenderRichString(kNetModeTextProfile)); // 0x18B5

    int group = h->RadioGroupCreate(kNetModeRadioCount, idHost);
    NetTrace(rec, "RadioGroupCreate");
    // byte_63C8F4 = -2 (0xFE) — radio-state seed written right before the loop.
    if (rec) {
        rec->radioGroup = group;
        rec->radioCount = kNetModeRadioCount;
        rec->idHost = idHost;
        rec->idSearch = idSearch;
        rec->idProfile = idProfile;
        rec->byteState = static_cast<int>(static_cast<unsigned char>(-2)); // 0xFE
    }

    // ---- frame loop ----
    int frame = 0;
    while (h->RunFrameLoop(frame) && frame < maxFrames) {
        h->InitStateReader(group);

        // cancel edge: dword_672230 || byte_67225C == 1
        if (h->CancelEdge(frame)) {
            v12 = 0;
            st.close = 1; // dword_631614 = 1
            NetTrace(rec, "Cancel");
        }

        if (h->ClickReady(frame)) {            // dword_75BF38 != -1
            int hover = h->HoverId(frame);     // dword_62D22C
            if (hover == idHost) {
                st.session = kNetModeSessHost; // word_63C740 = 5
                h->SetObjectsVisible(form, 0);
                NetTrace(rec, "Host");
                if (h->RunHostNetworkSetup()) {
                    st.close = 1;              // dword_631614 = 1
                    v12 = 1;
                }
                h->SetObjectsVisible(form, 1);
            } else if (hover == idSearch) {
                st.session = kNetModeSessHost; // word_63C740 = 5
                h->SetObjectsVisible(form, 0);
                NetTrace(rec, "Search");
                if (h->SearchNetworkGames(0)) {
                    st.close = 1;
                    v12 = 1;
                }
                h->SetObjectsVisible(form, 1);
            } else if (hover == idProfile) {
                st.session = kNetModeSessProfile; // word_63C740 = 4
                h->SetObjectsVisible(form, 0);
                NetTrace(rec, "Profile");
                if (h->ChooseNetworkProfile()) {
                    v12 = 1;
                    st.close = 1;
                }
                h->SetObjectsVisible(form, 1);
            }
        }
        ++frame;
    }
    if (rec) rec->frames = frame;

    // ---- cleanup ----
    h->RadioGroupFreeSurface(group);
    h->FormDestroy(form);
    NetTrace(rec, "Destroy");
    return v12;
}

// ===========================================================================
// gilde.exe 0x569668 — VIBE_Menu_RunFileSelector
// ===========================================================================
//   v6 = GameTick_Finalize(0,0,"menu\\fileselector"); CenterChildWindows;
//   DragCursor_SetSprite; SelectWindow(_,2); RenderRichString(a4); SelectWindow(_,0);
//   GetWindowId(_,1); BuildSliderPanel(...); enumerate via SaveBrowser_EnumerateSaveFiles
//   v36 = count.
//   if ((flags & 1) == 0) {  // EDIT mode
//     SelectWindow(_,3); ChildObjectId = GetChildObjectId(RenderRichString(0x18D9)); // OK
//     SelectWindow(_,0); v31 = GetChildObjectId(_,0,0);                              // edit field
//     SetValueOrText(v31, <initial>);
//   }
//   SelectWindow(_,1); for each row i: AddTextLabel(0, 24*i, name); store id; widget flags.
//   while (RunFrameLoop) {
//     if (dword_672230) dword_631614 = 1;                       // close request
//     if (dword_672228) {                                        // list click
//       find row whose id == dword_62D22C;
//       if (flags & 1) { Sprintf out="dir\\name ext"; dword_631614=1; v35=1; }   // DIRECT commit
//       else           { SetValueOrText(v31, name); }                            // copy to edit
//     }
//     if (dword_75BF38 != -1 && ChildObjectId == dword_62D22C) {  // OK pressed (edit)
//       if ((flags & 1) == 0) { name=GetDataPtr(v31); Sprintf out="dir\\name ext"; }
//       dword_631614 = 1; v35 = 1;
//     }
//   }
//   Form_Destroy(v32); return v35;
int Menu_RunFileSelector(const std::string& dir, const std::string& ext,
                         bool directMode, int listText, std::string& out,
                         FileSelRecord* rec, int maxFrames) {
    FileSelHooks* h = g_fileSelHooks;
    int v35 = 0; // function result

    // ---- form build ----
    int form = h->FormLoad();
    FileTrace(rec, "FormLoad");
    h->FormCenterChildWindows(form);
    h->DragCursorSetSprite();
    h->FormSelectWindow(form, 2);
    h->RenderRichString(listText);          // a4 — list title text
    h->FormSelectWindow(form, 0);
    h->BuildSliderPanel(form);              // GetWindowId(_,1)+Hud_BuildSliderPanel

    // Directory enumeration — REUSES the real SaveBrowser_EnumerateSaveFiles sibling.
    std::vector<SaveFileEntry> entries =
        SaveBrowser_EnumerateSaveFiles(dir, h->DirectoryListing(dir), ext);
    int count = static_cast<int>(entries.size()); // v36

    // ---- edit-mode-only widgets (OK button + edit field) ----
    int idOk = -1;     // ChildObjectId
    int idEdit = -1;   // v31
    if (!directMode) { // (v38 & 1) == 0
        h->FormSelectWindow(form, kFileSelEditWindow);
        idOk = h->GetChildObjectId(form, h->RenderRichString(kFileSelTextOk)); // 0x18D9
        h->FormSelectWindow(form, 0);
        idEdit = h->GetChildObjectId(form, 0);
        h->EditFieldSetText(idEdit, std::string()); // SetValueOrText(v31, initial)
    }

    // ---- list rows ----
    h->FormSelectWindow(form, kFileSelListWindow);
    if (rec) {
        rec->form = form;
        rec->directMode = directMode;
        rec->idOk = idOk;
        rec->idEdit = idEdit;
        rec->rows.clear();
    }
    std::vector<int> rowIds(static_cast<size_t>(count), -1);
    std::vector<std::string> rowNames(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        int y = kFileSelRowStrideY * i;                       // LOWORD(v37) = 24*i
        std::string name = BareName(entries[static_cast<size_t>(i)].displayName);
        rowNames[static_cast<size_t>(i)] = name;
        int id = h->AddTextLabel(y, name);
        rowIds[static_cast<size_t>(i)] = id;
        if (rec) rec->rows.push_back({id, y, name});
    }
    if (rec) rec->rowCount = count;
    FileTrace(rec, "Build");

    // ---- frame loop ----
    int frame = 0;
    while (h->RunFrameLoop(frame) && frame < maxFrames) {
        if (h->CancelEdge(frame)) {     // dword_672230
            if (rec) rec->close = 1;    // dword_631614 = 1 (no commit -> still returns 0)
            FileTrace(rec, "Cancel");
        }
        if (h->ListClickEdge(frame)) {  // dword_672228
            int hover = h->HoverId(frame); // dword_62D22C
            int row = -1;
            for (int i = 0; i < count; ++i) {
                if (rowIds[static_cast<size_t>(i)] == hover) { row = i; break; }
            }
            if (row >= 0) {
                const std::string& name = rowNames[static_cast<size_t>(row)];
                if (directMode) {       // (v38 & 1) != 0
                    out = dir + "\\" + name + ext; // Sprintf "%s\\%s%s"
                    if (rec) rec->close = 1;       // dword_631614 = 1
                    v35 = 1;
                    FileTrace(rec, "DirectCommit");
                    break; // dword_631614 armed -> loop exits next frame; commit done
                } else {
                    h->EditFieldSetText(idEdit, name); // copy into edit field
                    if (rec) rec->editFieldText = name;
                    FileTrace(rec, "Select");
                }
            }
        }
        // OK button (edit mode commit): dword_75BF38 != -1 && ChildObjectId == dword_62D22C
        if (h->OkReady(frame) && idOk != -1 && idOk == h->HoverId(frame)) {
            if (!directMode) {
                std::string name = h->EditFieldGetText(idEdit); // GetDataPtr(v31)
                out = dir + "\\" + name + ext;                  // Sprintf "%s\\%s%s"
                if (rec) rec->editFieldText = name;
            }
            if (rec) rec->close = 1; // dword_631614 = 1
            v35 = 1;
            FileTrace(rec, "OkCommit");
            break;
        }
        ++frame;
    }
    if (rec) rec->frames = frame;

    h->FormDestroy(form);
    FileTrace(rec, "Destroy");
    return v35;
}

} // namespace guild::gui
