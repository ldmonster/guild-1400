#include "gui/options_run.h"

// guild::gui — the full RunOptionsGfx/Sfx/Game function bodies.
//
// gilde.exe 0x56c21c / 0x56c808 / 0x56cc44.  See options_run.h for the recovery notes.
// Each runner has the identical shape:
//   FormLoad -> Camera/PositionChildWindows/PositionAtCoord(2)/SelectWindow(0) +
//   HudSyncWindowColors + DragCursorSetSprite -> SelectWindow(2)+RenderRichString(title)
//   -> SelectWindow(1) + per-child GetChildObjectId/SetValueOrText/SetScrollLimit
//   (+AppendWideLines for dropdowns) -> SelectWindow(3) + HudBuildButtonRow(OK,Cancel)
//   -> do/while RunFrameLoop { close on window-close/ESC; on a click edge dispatch the
//   hovered id against OK (accept+close) / Cancel (close) } -> on OK: read back each
//   widget's live value, save into the setting struct + persist + apply (delegated to the
//   options_screens Xxx_Commit) -> FormDestroy.
//
// REUSE: the layout tables / save-back / commit live in options_screens.{h,cpp}; the
// setting structs live in config/*.  This file only adds the orchestration + the host hooks.

#include "guild/common/types.h"

namespace guild::gui {

// ---------------------------------------------------------------------------
// Inert default hooks.
// ---------------------------------------------------------------------------
namespace {

struct DefaultOptionsRunHooks : OptionsRunHooks {};
DefaultOptionsRunHooks g_default;
OptionsRunHooks* g_hooks = &g_default;

} // namespace

OptionsRunHooks* Menu_SetOptionsRunHooks(OptionsRunHooks* hooks) {
    OptionsRunHooks* prev = g_hooks;
    g_hooks = hooks ? hooks : &g_default;
    return prev;
}

// ---- record helpers ---------------------------------------------------------
static void Trace(OptionsRunRecord* rec, const char* tag) {
    if (rec && rec->traceCount < OptionsRunRecord::kMaxTrace)
        rec->trace[rec->traceCount++] = tag;
}

static void RecordWidget(OptionsRunRecord* rec, int childIndex, int widgetId, int minV,
                         int maxV, int seed, int scrollLim, int lines, bool hidden) {
    if (rec && rec->widgetCount < 16) {
        OptionsRunBuiltWidget& w = rec->widgets[rec->widgetCount++];
        w.childIndex = childIndex;
        w.widgetId   = widgetId;
        w.minV       = minV;
        w.maxV       = maxV;
        w.seedValue  = seed;
        w.scrollLim  = scrollLim;
        w.lines      = lines;
        w.hidden     = hidden;
    }
}

// ---------------------------------------------------------------------------
// Shared preamble: gilde.exe 0x56c247..0x56c2a8 (identical across the three screens).
// FormLoad(name) -> Camera_ComputeWorldTarget -> Form_PositionChildWindows ->
// Window_PositionAtCoord_Thunk(form,2) -> SelectWindow(0)+Hud_SyncWindowColors+
// DragCursor_SetSprite -> SelectWindow(2)+Text_RenderRichString(title) -> SelectWindow(1).
// ---------------------------------------------------------------------------
static int OptionsPreamble(const char* formName, int title, OptionsRunRecord* rec) {
    Trace(rec, "FormLoad");
    int form = g_hooks->FormLoad(formName);
    g_hooks->CameraComputeWorldTarget();
    g_hooks->FormPositionChildWindows(form);
    g_hooks->WindowPositionAtCoord(form, 2);
    g_hooks->FormSelectWindow(form, 0);
    g_hooks->HudSyncWindowColors();
    g_hooks->DragCursorSetSprite();
    g_hooks->FormSelectWindow(form, 2);
    g_hooks->TextRenderRichString(title);
    Trace(rec, "Title");
    g_hooks->FormSelectWindow(form, 1);
    if (rec) { rec->formName = formName; rec->title = title; rec->form = form; }
    return form;
}

// Build one slider/dropdown child: GetChildObjectId(form,1,child) ->
// SetValueOrText(min,max,seed) -> SetScrollLimit -> (dropdown) AppendWideLines.
static int BuildChild(int form, int childIndex, int minV, int maxV, int seed,
                      int scrollLim, int lines, OptionsRunRecord* rec) {
    int wid = g_hooks->FormGetChildObjectId(form, 1, childIndex);
    g_hooks->ObjectSetValueOrText(wid, minV, maxV, seed);
    g_hooks->WidgetSetScrollLimit(wid, scrollLim);
    if (lines > 0) g_hooks->TextAppendWideLines(wid, lines, childIndex);
    RecordWidget(rec, childIndex, wid, minV, maxV, seed, scrollLim, lines, false);
    return wid;
}

// Shared OK/Cancel frame loop: gilde.exe 0x56c6b5 / 0x56ca7d / 0x56d115.
// while(RunFrameLoop){ if window-close/ESC -> close; if click edge -> hover==OK ?
// accept+close : hover==Cancel ? close }.  Returns accepted.  `onDrag` (Sfx only) runs the
// live-preview branch each frame.
template <typename DragFn>
static bool RunOptionsLoop(int okId, int cancelId, int maxFrames, OptionsRunRecord* rec,
                           DragFn onDrag) {
    bool accepted = false;
    int frame = 0;
    while (g_hooks->RunFrameLoop(frame)) {
        if (g_hooks->WindowCloseOrEsc(frame)) {
            // dword_631614 = 1 (close armed).  No accept.
        }
        if (g_hooks->ClickEdge(frame)) {
            int hover = g_hooks->HoverId(frame);
            if (hover == okId) {
                accepted = true; // v5/v25/v6 = 1; dword_631614 = 1
            } else if (hover == cancelId) {
                // dword_631614 = 1 (close, no accept)
            }
        }
        onDrag(frame);
        ++frame;
        if (maxFrames > 0 && frame >= maxFrames) break;
    }
    if (rec) { rec->frames = frame; rec->accepted = accepted; }
    return accepted;
}

// ===========================================================================
// gilde.exe 0x56c21c — VIBE_Menu_RunOptionsGfx.
// ===========================================================================
OptionsResult Menu_RunOptionsGfx(OptionsRunState& st, OptionsRunRecord* rec, int maxFrames) {
    int form = OptionsPreamble(kOptionsGfxForm, kOptionsGfxTitle, rec);

    // Resolution dropdown range (0x56c369): byte_62D59A -> 1, byte_62D59B -> 2.
    int resRange = 0;
    if (st.gfxResCap1) resRange = 1;
    if (st.gfxResCap2) resRange = 2;
    const int resSeed = st.gfx.curRes; // byte_63D724 (cur_res)
    // child 0 (resolution): SetValueOrText(0, resRange, cur_res) (0x56c390), 130, 3 lines.
    int resWid = g_hooks->FormGetChildObjectId(form, 1, 0);
    g_hooks->ObjectSetValueOrText(resWid, 0, resRange, resSeed);
    g_hooks->WidgetSetScrollLimit(resWid, kScrollLimitDropdown);
    g_hooks->TextAppendWideLines(resWid, 3, 0);
    RecordWidget(rec, 0, resWid, 0, resRange, resSeed, kScrollLimitDropdown, 3, false);

    // children 1..8 (detail dropdowns + gamma slider), per kGfxWidgets[1..8].
    int wids[kGfxWidgetCount];
    wids[0] = resWid;
    for (int i = 1; i < kGfxWidgetCount; ++i) {
        const GfxWidgetDesc& d = kGfxWidgets[i];
        int seed = 0;
        switch (d.field) {
            case GfxField::kDetails:      seed = st.gfx.details; break;          // 0x56c3e3
            case GfxField::kTextureScale: seed = st.gfx.textureScale; break;     // 0x56c43f
            case GfxField::kFloorLod:     seed = st.gfx.floorLod; break;         // 0x56c49a
            case GfxField::kFloorMipmap:  seed = st.gfx.floorMipmapping; break;  // 0x56c4ed
            case GfxField::kLodHandling:  seed = st.gfx.lodHandling; break;      // 0x56c539
            case GfxField::kShadowDetail: seed = st.gfx.shadowDetail; break;     // 0x56c585
            case GfxField::kGamma:        seed = Gfx_GammaSeed(st.gfx.fogPlane); break; // 0x56c5df
            case GfxField::kCameraLimits: seed = st.gfx.cameraLimits; break;     // 0x56c617
            default: break;
        }
        int minV = (d.field == GfxField::kGamma) ? 50 : 0; // gamma slider min 50, base 50
        wids[i] = BuildChild(form, i, minV, d.range, seed, d.scrollLim, d.lines, rec);
    }

    // 0x56c667: in-game (byte_63CC40) -> hide the resolution dropdown.
    if (st.inGame) {
        g_hooks->ObjectSetVisibleRecursive(resWid, 0);
        if (rec) rec->widgets[0].hidden = true;
    }

    // OK/Cancel button row (0x56c676 SelectWindow(3) + Hud_BuildButtonRow).
    g_hooks->FormSelectWindow(form, 3);
    int row[2] = {-1, -1};
    g_hooks->HudBuildButtonRow(row);
    if (rec) { rec->okId = row[0]; rec->cancelId = row[1]; }

    bool accepted = RunOptionsLoop(row[0], row[1], maxFrames, rec, [](int) {});

    // OK read-back (0x56c726): live widget values in BUILD order -> Gfx_SaveBack via Commit.
    int values[kGfxWidgetCount];
    for (int i = 0; i < kGfxWidgetCount; ++i) values[i] = g_hooks->ObjectGetDataPtr(wids[i]);
    int resValue = g_hooks->ObjectGetDataPtr(resWid); // 0x56c7b1 child 0 vs byte_63D724
    OptionsResult r = Gfx_Commit(accepted, values, resValue, resSeed, st.gfx);
    Trace(rec, accepted ? "Commit" : "Discard");
    Trace(rec, "FormDestroy");
    g_hooks->FormDestroy(form);
    return r;
}

// ===========================================================================
// gilde.exe 0x56c808 — VIBE_Menu_RunOptionsSfx.
// ===========================================================================
OptionsResult Menu_RunOptionsSfx(OptionsRunState& st, OptionsRunRecord* rec, int maxFrames) {
    int form = OptionsPreamble(kOptionsSfxForm, kOptionsSfxTitle, rec);

    // children 0..4: master/sfx/music/speech volume sliders + the music-freq dropdown.
    int wids[kSfxWidgetCount];
    for (int i = 0; i < kSfxWidgetCount; ++i) {
        const SfxWidgetDesc& d = kSfxWidgets[i];
        int seed = 0;
        switch (d.field) {
            case SfxField::kMasterVol: seed = st.snd.masterVol; break; // byte_1233550
            case SfxField::kSfxVol:    seed = st.snd.sfxVol; break;    // byte_1233551
            case SfxField::kMsxVol:    seed = st.snd.msxVol; break;    // byte_1233552
            case SfxField::kSpeechVol: seed = st.snd.speechVol; break; // byte_1233553
            case SfxField::kMsxFreq:   seed = st.snd.msxFreq; break;   // byte_1233554
        }
        wids[i] = BuildChild(form, i, 0, d.range, seed, d.scrollLim, d.lines, rec);
    }

    g_hooks->FormSelectWindow(form, 3);
    int row[2] = {-1, -1};
    g_hooks->HudBuildButtonRow(row);
    if (rec) { rec->okId = row[0]; rec->cancelId = row[1]; }

    // The Sfx loop has the extra live-preview branch (0x56cbc2): while dragging a volume
    // slider, push the live master/sfx volume and, on the sfx slider, play a test sample.
    const int masterWid = wids[static_cast<int>(SfxField::kMasterVol)];
    const int sfxWid     = wids[static_cast<int>(SfxField::kSfxVol)];
    const int msxWid     = wids[static_cast<int>(SfxField::kMsxVol)];
    const int speechWid  = wids[static_cast<int>(SfxField::kSpeechVol)];
    auto onDrag = [&](int frame) {
        if (!g_hooks->DragActive(frame)) return;
        int hover = g_hooks->HoverId(frame);
        if (hover == masterWid || hover == sfxWid || hover == msxWid || hover == speechWid) {
            int master = g_hooks->ObjectGetDataPtr(masterWid);
            int sfx    = g_hooks->ObjectGetDataPtr(sfxWid);
            g_hooks->AudioPreviewVolume(master, sfx); // SetMasterVolume + ApplyMasterVolume
            if (hover == sfxWid) g_hooks->AudioStartVoiceSample(); // 0x56cb77 test sample
        }
    };
    bool accepted = RunOptionsLoop(row[0], row[1], maxFrames, rec, onDrag);

    int values[kSfxWidgetCount];
    for (int i = 0; i < kSfxWidgetCount; ++i) values[i] = g_hooks->ObjectGetDataPtr(wids[i]);
    OptionsResult r = Sfx_Commit(accepted, values, st.snd);
    Trace(rec, accepted ? "Commit" : "Discard");
    Trace(rec, "FormDestroy");
    g_hooks->FormDestroy(form);
    return r;
}

// ===========================================================================
// gilde.exe 0x56cc44 — VIBE_Menu_RunOptionsGame.
// ===========================================================================
OptionsResult Menu_RunOptionsGame(OptionsRunState& st, OptionsRunRecord* rec, int maxFrames) {
    int form = OptionsPreamble(kOptionsGameForm, kOptionsGameTitle, rec);

    // children per kGameWidgets[] (note the sparse form child indices 0..6,9,10,11,12).
    int wids[kGameWidgetCount];
    for (int i = 0; i < kGameWidgetCount; ++i) {
        const GameWidgetDesc& d = kGameWidgets[i];
        int seed = 0;
        switch (d.field) {
            case GameField::kSpeed:         seed = st.game.speed; break;        // dword_1233558
            case GameField::kScrollSpeed:   seed = st.game.scrollSpeed; break;  // dword_1233560
            case GameField::kMouseSpeed:    seed = st.game.mouseSpeed; break;   // dword_1233564
            case GameField::kCameraSpeed:   seed = st.game.cameraSpeed; break;  // byte_123355C
            case GameField::kInvertMouse:   seed = st.game.invertMouse; break;  // byte_1233568
            case GameField::kNachtwaechter: seed = st.game.nachtwaechter; break;// byte_123356A
            case GameField::kShowCursorTxt: seed = st.game.showCursorTxt; break;// byte_123356B
            case GameField::kDifficulty:    seed = st.game.difficulty; break;   // byte_12335B8
            case GameField::kHints:         seed = st.game.hints; break;        // byte_12335B9
            case GameField::kPanelHelp:     seed = st.game.panelHelp; break;    // byte_12335BB
            case GameField::kPanelMode:     seed = st.game.panelMode; break;    // byte_12335BC
        }
        wids[i] = BuildChild(form, d.childIndex, 0, d.range, seed, d.scrollLim, d.lines, rec);
        // 0x56cef1: child 4 (invert mouse) is created then force-hidden.
        if (d.field == GameField::kInvertMouse) {
            g_hooks->ObjectSetVisibleRecursive(wids[i], 0);
            if (rec && rec->widgetCount > 0) rec->widgets[rec->widgetCount - 1].hidden = true;
        }
    }

    g_hooks->FormSelectWindow(form, 3);
    int row[2] = {-1, -1};
    g_hooks->HudBuildButtonRow(row);
    if (rec) { rec->okId = row[0]; rec->cancelId = row[1]; }

    bool accepted = RunOptionsLoop(row[0], row[1], maxFrames, rec, [](int) {});

    // OK read-back (0x56d184): build-order values -> Game_SaveBack via Commit; the difficulty
    // (build index 7 == form child 9) change gates the groundplan rebuild (only in-game).
    int values[kGameWidgetCount];
    for (int i = 0; i < kGameWidgetCount; ++i) values[i] = g_hooks->ObjectGetDataPtr(wids[i]);
    const int diffIdx   = 7; // kGameWidgets row for kDifficulty (form child 9)
    const int diffValue = values[diffIdx];
    const int diffSaved = (accepted && st.inGame) ? st.game.difficulty : diffValue;
    OptionsResult r = Game_Commit(accepted, values, diffValue, diffSaved, st.game);
    Trace(rec, accepted ? "Commit" : "Discard");
    Trace(rec, "FormDestroy");
    g_hooks->FormDestroy(form);
    return r;
}

} // namespace guild::gui
