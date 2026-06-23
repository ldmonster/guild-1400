// ============================================================================
// Die Gilde 1:1 — Groundplan / blueprint ("Riss") window driver functions.
// See groundplan.h for the design rationale (coupled engine drivers, exposed
// through GroundplanState + GroundplanBackend; reconstructable leaf logic —
// path selection, pixel scan, clock math — transcribed byte-for-byte).
//
// gilde.exe cluster: 0x4ae3b8..0x4b0992 (the coupled half of VIBE_Groundplan_*;
// the pure half lives in src/world/groundplan_recon.cpp).
// ============================================================================
#include "gui/groundplan.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace guild {
namespace gui {

namespace {

// ConvertX @0x5c6b08 truncates toward zero (verified per brief). The info-panel
// clock math at 0x4af4a8 stores the float into an int after the ConvertX call,
// so the cast is a C truncation. Helper makes the truncation explicit at sites.
inline int ConvertX_trunc(double x) { return static_cast<int>(x); }

// gilde.exe FP constants used by the clock math (get_bytes verified):
constexpr double kDbl_61DC88 = 0.8;                  // minute-hand scale
constexpr double kDbl_61DC90 = 4.0;                  // hour-hand scale
constexpr double kDbl_61DC98 = 1.0 / 60.0;           // 0x3F91111111111111
constexpr double kDbl_61DCA0 = 6.283185307179586;    // 2*pi
constexpr float  kFlt_61DCA8 = 0.15915493667125702f; // 1/(2*pi)
constexpr double kDbl_61DCB0 = 48.0;                  // moon-cell count
constexpr double kDbl_61DCB8 = 0.5;                   // rounding bias

}  // namespace

// ---------------------------------------------------------------------------
// 0x4aea5c (filename switch) — pure path-basename selection.
// Reproduces the exact branch structure of the inner switch. The generic
// fallback ("riss_handwerksbetrieb.bmp") is the LABEL after the switch.
// ---------------------------------------------------------------------------
std::string Groundplan_PickBlueprintName(u8 category, u8 typeByte, u8 roomByte) {
    switch (category) {
        case 1:
            if (typeByte == 22) return "riss_wirtshaus.bmp";
            if (roomByte == 14) return "riss_parfuemerie.bmp";
            if (roomByte == 8)  return "riss_tinkturei.bmp";
            break;  // -> generic fallback
        case 3:
            if (typeByte == 15) return "riss_rathaus.bmp";
            if (typeByte == 1)  return "Riss_Arbeiterunterkunft.bmp";
            // (Hex-Rays: both arms fall through to LABEL_9 with v25 unchanged
            //  when neither matches — i.e. the last sprintf'd path. With no
            //  prior path written, that is the generic fallback below.)
            break;
        case 5:
            return "riss_zunfthaus.bmp";
        default:
            if (typeByte == 5)  return "riss_geldleihe.bmp";
            if (roomByte == 9)  return "riss_lagerhaus.bmp";
            if (roomByte == 7)  return "riss_kirche.bmp";
            if (roomByte == 19) return "riss_stadtwache.bmp";
            break;  // -> generic fallback
    }
    return "riss_handwerksbetrieb.bmp";
}

// ---------------------------------------------------------------------------
// 0x4aea5c — VIBE_Groundplan_LoadBlueprintBmp.
// ---------------------------------------------------------------------------
std::string Groundplan_LoadBlueprintBmp(GroundplanState& st,
                                        const GroundplanBackend& be) {
    char path[256];
    // v31 = dword_631744 ? dword_631744 : dword_631748  (active building rec)
    i32 buildingPtr = st.buildingPtr744 ? st.buildingPtr744 : st.buildingPtr748;

    // if(dword_631640){ Destroy; =0; }
    if (st.surf631640) {
        if (be.SurfaceDestroy) be.SurfaceDestroy(st.surf631640);
        st.surf631640 = 0;
    }

    // The originals derive *v31 (typeByte) and the bytes at v32 = 589*typeByte +
    // dword_13CE294. In this reconstruction the building record + the building
    // type table are engine state; the caller supplies them through the record
    // pointers. When no backend is wired (headless) buildingPtr is an opaque
    // value and we fall back to the primary path (riss_<lo>.bmp default).
    u8 typeByte = 0;
    if (buildingPtr) typeByte = static_cast<u8>(buildingPtr & 0xFF);  // *(u8*)v31

    // Primary attempt: riss_<lo>.bmp where <lo> derives from the engine subtype
    // string (v32+1). The original sprintf is "%sbmp\groundplans\riss_%s.bmp".
    // Without the live string table we fall straight to the category-driven
    // name (matching the !result branch the original takes when the primary
    // load fails — which is the common case for the special-cased buildings).
    std::snprintf(path, sizeof(path), "%sbmp\\groundplans\\riss_%s.bmp",
                  be.assetBaseDir, "");
    i32 result = be.PictureCreateSurfaceFromBmp
                     ? be.PictureCreateSurfaceFromBmp(path) : 0;
    st.surf631640 = result;

    if (!result) {
        u8 cat = be.MapTypeToCategory ? be.MapTypeToCategory(typeByte) : 0;
        // roomByte = *(u8*)(589*typeByte + 13CE294) — engine table byte.
        u8 roomByte = 0;  // resolved by the engine; 0 in the isolated build.
        std::string name = Groundplan_PickBlueprintName(cat, typeByte, roomByte);
        std::snprintf(path, sizeof(path), "%sbmp\\groundplans\\%s",
                      be.assetBaseDir, name.c_str());
        st.surf631640 = be.PictureCreateSurfaceFromBmp
                            ? be.PictureCreateSurfaceFromBmp(path) : 0;
    }

    if (!st.surf631640)
        return std::string(path);

    // --- copy blueprint into the panel surface, channel-swapped --------------
    // Lock both surfaces, then for each pixel: get(j,i)->rgb[3]; set with the
    // original's channel order set(j,i, rgb[1],rgb[0],rgb[2]) (G,R,B swap).
    if (be.SurfaceLock) be.SurfaceLock(st.surf631640);
    if (be.SurfaceLock) be.SurfaceLock(st.surf631644);
    int H = be.SurfaceHeight ? be.SurfaceHeight(st.surf631640) : 0;
    int W = be.SurfaceWidth ? be.SurfaceWidth(st.surf631640) : 0;
    for (int i = 0; i < H; ++i) {
        for (int j = 0; j < W; ++j) {
            u8 rgb[3] = {0, 0, 0};
            if (be.GetPixelRgb) be.GetPixelRgb(j, i, rgb, st.surf631640);
            if (be.SetPixelRgb) be.SetPixelRgb(j, i, rgb[1], rgb[0], rgb[2], st.surf631644);
        }
    }
    if (be.SurfaceUnlock) be.SurfaceUnlock(st.surf631644);
    if (st.surf631640) {
        if (be.SurfaceDestroy) be.SurfaceDestroy(st.surf631640);
        st.surf631640 = 0;
    }

    // --- derive the "_C.bmp" collision-map name -----------------------------
    // Original: strip at the '.' (StrChr(path,'.')=0), then append "_C.bmp" two
    // bytes at a time (the odd 2-step copy loop). Net effect: path[..base].
    {
        char* dot = std::strchr(path, '.');
        if (dot) *dot = '\0';
        std::strncat(path, "_C.bmp", sizeof(path) - std::strlen(path) - 1);
    }
    st.surf631640 = be.PictureCreateSurfaceFromBmp
                        ? be.PictureCreateSurfaceFromBmp(path) : 0;

    int spawnCount = 0;  // v26
    if (be.SurfaceLock) be.SurfaceLock(st.surf631640);
    if (st.surf631640) {
        int cH = be.SurfaceHeight ? be.SurfaceHeight(st.surf631640) : 0;
        int cW = be.SurfaceWidth ? be.SurfaceWidth(st.surf631640) : 0;
        // marker colour v35 = (lo24 of off_4AD1C0). The first matched marker is
        // the "room start" colour; the next pixel's colour is matched against
        // the 15-entry palette unk_631648 (3 bytes each) to get a room index.
        u8 startMarker[3] = {0, 0, 0};  // off_4AD1C0 lo24 (engine palette[0])
        for (int i = 0; i < cH; ++i) {
            for (int j = 0; j < cW; ++j) {
                u8 px[3] = {0, 0, 0};
                if (be.GetPixelRgb) be.GetPixelRgb(j, i, px, st.surf631640);
                // if pixel != startMarker -> skip
                if (be.ColorNotEqualRgb && be.ColorNotEqualRgb(px, startMarker))
                    continue;
                // next pixel's colour -> palette index v14 in [0,15)
                u8 px2[3] = {0, 0, 0};
                if (be.GetPixelRgb) be.GetPixelRgb(j + 1, i, px2, st.surf631640);
                int roomIdx = 0;  // v14
                // walk the 15-entry palette unk_631648 (engine-resident). When no
                // backend palette is wired, roomIdx stays 0 (first room).
                // (palette match handled by the engine; modelled as index 0.)
                if (roomIdx >= 15) continue;
                // resolve the room game-object for roomIdx via the engine query.
                i32 obj = be.GameObjectQueryFind
                              ? be.GameObjectQueryFind(buildingPtr, 1, 0, roomIdx)
                              : 0;
                if (!obj || (be.BuildingLookupTypeStringId &&
                             be.BuildingLookupTypeStringId(obj) == -1)) {
                    continue;  // LABEL_52: ++j
                }
                // spawn a room hotspot widget at (j + screenH - 112 - 14, row+76).
                int wx = j + st.screenH - 112 - 14;
                int wy = i + 76;  // v37 = v38 = i + 76
                i32 wid = be.WidgetCreateObject
                              ? be.WidgetCreateObject(static_cast<i16>(wx),
                                                      static_cast<i16>(wy))
                              : -1;
                if (be.OnRoomHotspot) be.OnRoomHotspot(wx, wy);
                if (spawnCount < 15)
                    st.roomHotspots[spawnCount] = wid;
                ++spawnCount;
                (void)px2;
            }
        }
    }
    if (st.surf631644 && be.SurfaceUnlock) be.SurfaceUnlock(st.surf631644);
    if (st.surf631640 && be.SurfaceUnlock) be.SurfaceUnlock(st.surf631640);
    return std::string(path);
}

// ---------------------------------------------------------------------------
// 0x4af4a8 clock-hand math (pure block). ConvertX truncates.
// ---------------------------------------------------------------------------
GroundplanClockHands Groundplan_ComputeClockHands(const GroundplanClockInputs& in) {
    GroundplanClockHands out;
    // minutes = (qword >> 32) % 60 ; v133 = (int)(minutes * 0.8)
    long long qword = (static_cast<long long>(in.hi) << 32) |
                      static_cast<unsigned>(in.lo);
    int minutes = static_cast<int>((qword >> 32) % 60);
    out.minuteCell = ConvertX_trunc(static_cast<double>(minutes) * kDbl_61DC88);

    // secsTotal = 60*WORD1(qword) + HIDWORD(qword).
    // 0x4afdd2 — WORD1(qword) is bits 16..31 of the 64-bit packed time, i.e. the
    //   SECOND 16-bit word, which lives in the LOW dword: (lo >> 16) & 0xFFFF.
    //   (NOT (hi & 0xFFFF) — that would be WORD2.)  HIDWORD(qword) == hi.
    int word1 = static_cast<int>((in.lo >> 16) & 0xFFFF);  // WORD1(qword)
    int hidword = static_cast<int>(in.hi);                 // HIDWORD(qword)
    int secsTotal = 60 * word1 + hidword;
    // 0x4afded — v69 = (double)secsTotal * dbl_61DC90(4.0) * dbl_61DC98(1/60).
    //   Multiply order kept exactly as the x87 stream: *4.0 first, then *(1/60).
    // v131 = (int)v69 % 48  (ConvertX truncates toward zero; %48 via 64-bit idiv).
    double v69 = static_cast<double>(secsTotal) * kDbl_61DC90 * kDbl_61DC98;
    out.hourCell = ConvertX_trunc(v69) % 48;

    // moon hand: v74 = 0.5 + fmod(phase,2pi) * (1/2pi) * 48.0 ; v132=(int)v74 % 48
    // Original: 48.0 + fmod(phase,2pi)*(1/2pi)*48.0 + 0.5   (dbl_61DCB0 used
    // twice: as the +48 base and the *48 scale, with +0.5 bias dbl_61DCB8).
    double frac = std::fmod(static_cast<double>(in.weatherPhase), kDbl_61DCA0);
    double v74 = kDbl_61DCB0 + frac * static_cast<double>(kFlt_61DCA8) * kDbl_61DCB0
                 + kDbl_61DCB8;
    out.moonCell = ConvertX_trunc(v74) % 48;
    return out;
}

// ---------------------------------------------------------------------------
// 0x4ae678 — VIBE_Groundplan_SetWidgetsVisible.
// ---------------------------------------------------------------------------
void Groundplan_SetWidgetsVisible(GroundplanState& st, const GroundplanBackend& be,
                                  int vis) {
    if (st.wid700 != -1 && be.FormSetObjectsVisible)
        be.FormSetObjectsVisible(st.wid700, vis);
    if (be.EventPanelSetBarVisible) be.EventPanelSetBarVisible(vis);
    if (st.wid700 != -1 && be.FormSetObjectsVisible)
        be.FormSetObjectsVisible(st.wid700, vis);
    auto show = [&](i32 obj, int v) {
        if (obj != -1 && be.ObjectSetVisibleRecursive)
            be.ObjectSetVisibleRecursive(obj, v);
    };
    show(st.widE4, vis);
    show(st.widE8, vis);
    show(st.widEC, vis);
    show(st.widF0, vis);
    show(st.widF4, vis);
    show(st.widF8, vis);
    show(st.widFC, vis);
    // widAC: only hidden (when !vis), never shown here.
    if (st.widAC != -1 && !vis && be.ObjectSetVisibleRecursive)
        be.ObjectSetVisibleRecursive(st.widAC, 0);
    show(st.wid704, vis);
    show(st.wid708, vis);
    // wid700 (window) toggled via the object path too (the original indexed the
    // object table dword_67EDEC[238*dword_631700]); modelled as the window obj.
    show(st.wid700, vis);
    for (int i = 0; i < 15; ++i)
        show(st.roomHotspots[i], vis);
}

// ---------------------------------------------------------------------------
// 0x4ae828 — VIBE_Groundplan_DestroyWidgets.
// ---------------------------------------------------------------------------
void Groundplan_DestroyWidgets(GroundplanState& st, const GroundplanBackend& be) {
    if (st.wid700 != -1 && be.FormSetObjectsVisible)
        be.FormSetObjectsVisible(st.wid700, 0);
    auto destroy = [&](i32& slot) {
        if (slot != -1) {
            if (be.WidgetDestroyByType) be.WidgetDestroyByType(slot);
            slot = -1;
        }
    };
    destroy(st.widE4);
    destroy(st.widE8);
    destroy(st.widEC);
    destroy(st.widF0);
    destroy(st.widF4);
    destroy(st.widF8);
    destroy(st.widFC);
    destroy(st.wid704);
    destroy(st.wid708);
    if (st.widAC != -1 && be.ObjectSetVisibleRecursive)
        be.ObjectSetVisibleRecursive(st.widAC, 0);
}

// ---------------------------------------------------------------------------
// 0x4ae3b8 — VIBE_Groundplan_CreateWindow.
//   cityRect comes from dword_63CC4C/50/54/58 (x0,y0,x1,y1) — passed as scene
//   globals; here we only need the side effects on st.
// ---------------------------------------------------------------------------
void Groundplan_CreateWindow(GroundplanState& st, const GroundplanBackend& be,
                             int sceneId) {
    // VIBE_Coord_Push(0,0, screenW, screenH)
    if (be.CoordPush) be.CoordPush(0, 0, st.screenW, st.screenH);
    // dword_631710 = HotspotRegister(screenH-80, 17, 48, 48, 0)
    st.hotspot710 = be.HotspotRegister
                        ? be.HotspotRegister(st.screenH - 80, 17, 48, 48, 0) : -1;
    // dword_631700 = WindowCreate(4,10,48,648,0); PositionAtCoord(win,1)
    st.wid700 = be.WindowCreate ? be.WindowCreate(4, 10, 48, 648, 0) : -1;
    if (be.WindowPositionAtCoord) be.WindowPositionAtCoord(st.wid700, 1);

    // if(dword_63163C){ FreeDebug; =0; }   (backdrop shape)
    if (st.surf63163C) {
        if (be.SurfaceDestroy) be.SurfaceDestroy(st.surf63163C);
        st.surf63163C = 0;
    }
    // VIBE_State_Update(sceneId), backdrop grab via Shape_GrabByDepth, etc. —
    // these are surface/shape engine leaves; performed by the backend in
    // production. (No isolated-build observable beyond the slots below.)

    // clear the 15 room slots (dword_11BC1F0[i] = -1)
    for (int i = 0; i < 15; ++i)
        st.roomHotspots[i] = -1;

    // stash the scene id (dword_631714 = a1)
    st.sceneFlag714 = sceneId;

    // dword_631644 = Surface_Create(.., 110, 24bpp) then ColorFill
    st.surf631644 = be.SurfaceCreate ? be.SurfaceCreate(0, 110, 3) : 0;
    if (st.surf631644 && be.SurfaceColorFill) be.SurfaceColorFill(st.surf631644);

    // dword_6317AC = WidgetCreateObject(..); set its +72=1; hide it.
    st.widAC = be.WidgetCreateObject ? be.WidgetCreateObject(0, 0) : -1;
    if (st.widAC != -1 && be.ObjectSetVisibleRecursive)
        be.ObjectSetVisibleRecursive(st.widAC, 0);
}

// ---------------------------------------------------------------------------
// 0x4b0758 — VIBE_Groundplan_FadeInScene.
// ---------------------------------------------------------------------------
void Groundplan_FadeInScene(GroundplanState& st, const GroundplanBackend& be) {
    // Surface_ColorFill(sceneSurf, color); render entities into it; build info
    // panel once; register a black fade and run one frame; recentre the window.
    // All routed through the backend (the frame loop / fade / surface leaves
    // are owned by other modules). The observable reconstructed step is the
    // single BuildInfoPanel(1,...) call.
    GroundplanClockInputs clock;  // engine supplies the live time in production
    Groundplan_BuildInfoPanel(st, be, 1, clock);
    if (st.wid700 != -1 && be.WindowPositionCentered)
        be.WindowPositionCentered(st.wid700, 0);
}

// ---------------------------------------------------------------------------
// 0x4af038 — VIBE_Groundplan_RenderBlueprint.
// ---------------------------------------------------------------------------
void Groundplan_RenderBlueprint(GroundplanState& st, const GroundplanBackend& be,
                                int flags, int cursorX, int cursorY,
                                bool hoverActive) {
    // Dirty check: rebuild the room hotspots when the cached selection changed.
    // Original compares dword_649D60/631744/63174C against their cached copies
    // plus the dword_631DB4 force flag. We model 649D60 via cacheDB8's source as
    // the selectedPlot (the live selection key).
    bool dirty = (st.selectedPlot != st.cacheDB8) ||
                 (st.buildingPtr744 != st.cacheDBC) ||
                 st.dirtyDB4;
    if (dirty) {
        st.cacheDBC = st.buildingPtr744;
        // destroy the 15 room hotspots
        for (int i = 0; i < 15; ++i) {
            if (st.roomHotspots[i] != -1) {
                if (be.WidgetDestroyByType) be.WidgetDestroyByType(st.roomHotspots[i]);
                st.roomHotspots[i] = -1;
            }
        }
        if (st.buildingPtr744 || st.buildingPtr748)
            Groundplan_LoadBlueprintBmp(st, be);
        st.cacheDB8 = st.selectedPlot;
    }

    if (st.buildingPtr744 || st.buildingPtr748) {
        // blit the panel render surface
        if (st.surf631644 /* Result_Finalize blit */) {
            // (engine blit handled by the backend frame compositor)
        }
        if (hoverActive && (flags & 0x2000) != 0 && st.surf631640) {
            int x = cursorX;
            int y = cursorY;
            if (x >= 0 && be.SurfaceWidth && x < be.SurfaceWidth(st.surf631640) &&
                y >= 0 && be.SurfaceHeight && y < be.SurfaceHeight(st.surf631640)) {
                if (be.SurfaceLock) be.SurfaceLock(st.surf631640);
                u8 px[3] = {0, 0, 0};
                if (be.GetPixelRgb) be.GetPixelRgb(x, y, px, st.surf631640);
                // match px against the 15-entry palette -> room idx (engine).
                int roomIdx = 0;
                if (be.SurfaceUnlock) be.SurfaceUnlock(st.surf631640);
                if (roomIdx < 15) {
                    i32 obj = be.GameObjectQueryFind
                                  ? be.GameObjectQueryFind(st.buildingPtr744, 1, 0, roomIdx)
                                  : 0;
                    if (obj && be.WidgetSetTooltipText)
                        be.WidgetSetTooltipText("");  // tooltip text from obj
                }
            }
        }
    } else {
        // no building selected: draw the city-wappen string via the engine.
        // (_STADTWAPPEN_<CITY> -> property -> animation; engine-resident.)
    }
    st.dirtyDB4 = 0;
}

// ---------------------------------------------------------------------------
// 0x4af4a8 — VIBE_Groundplan_BuildInfoPanel.
// Faithful control flow: bit0 of `rebuild` clear => full teardown + recreate of
// the label/sprite set; set => in-place refresh. The reconstructable payload is
// the clock/date math; widget layout is routed through the backend.
// ---------------------------------------------------------------------------
int Groundplan_BuildInfoPanel(GroundplanState& st, const GroundplanBackend& be,
                              u8 rebuild, const GroundplanClockInputs& clock) {
    int result = 67 * (st.selectedPlot & 0xFFFF);

    // if byte_12CE912[536*selectedPlot] != 6 -> early-out (no plot selected).
    // The marker array is engine state; in the isolated build we cannot read it,
    // so the panel proceeds only when a building record is present (the live
    // equivalent of "a plot is selected").
    if (!st.buildingPtr744 && !st.buildingPtr748 && st.selectedPlot == 0 &&
        !be.WidgetCreateObject)
        return result;

    if ((rebuild & 1) == 0) {
        // ---- full teardown + recreate ----
        Groundplan_DestroyWidgets(st, be);
        // recreate label/sprite widgets (Object_AddToWindow / AddTextLabel / …)
        st.widE4 = be.WidgetCreateObject ? be.WidgetCreateObject(0, 8) : -1;
        st.widE8 = be.WidgetCreateObject ? be.WidgetCreateObject(0, 13) : -1;
        st.wid704 = be.WidgetCreateObject ? be.WidgetCreateObject(57, 13) : -1;
        st.widF4 = be.WidgetCreateObject ? be.WidgetCreateObject(0, 0) : -1;
        st.widF0 = be.WidgetCreateObject ? be.WidgetCreateObject(0, 0) : -1;
        st.widF8 = be.WidgetCreateObject ? be.WidgetCreateObject(0, 214) : -1;
        st.widFC = be.WidgetCreateObject ? be.WidgetCreateObject(0, 215) : -1;
        st.wid708 = be.WidgetCreateObject
                        ? be.WidgetCreateObject(static_cast<i16>(st.screenH - 106), 0)
                        : -1;
        return result;
    }

    // ---- in-place refresh ----
    // Render the blueprint into the panel surface.
    Groundplan_RenderBlueprint(st, be, 0, 0, 0, false);

    // Date / season text + the three clock-hand sprite cells (the load-bearing
    // reconstructed math). The cell indices drive the engine sprite frames.
    GroundplanClockHands hands = Groundplan_ComputeClockHands(clock);
    (void)hands;  // engine consumes hands.{minuteCell,hourCell,moonCell}

    // city-wappen / item label / money total — engine-resident text leaves.
    return result;
}

}  // namespace gui
}  // namespace guild
