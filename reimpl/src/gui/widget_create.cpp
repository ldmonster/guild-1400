#include "gui/widget_create.h"
#include "gui/object.h"
#include "gui/window.h"
#include "gui/form.h"
#include "gui/zorder.h"
#include "gui/slider.h"

#include <cstdint>
#include <cstring>

// GUILD_WEAK: weak "default edge" on ELF (tests override); strong on PE/COFF
// (MinGW has no usable weak-definition support; no overrides in the app build).
#if defined(_WIN32)
#define GUILD_WEAK
#else
#define GUILD_WEAK __attribute__((weak))
#endif

namespace guild::gui {

// ---- Leaf-owned global tables (original BSS bases in comments) -------------------------
i32 g_defaultCtrlH  = 0;  // dword_69FFB0
i32 g_screenClipW   = 0;  // dword_69FFB8
i32 g_screenClipExt = 0;  // dword_69FFBC

InputField::InputField()  { std::memset(dw, 0, sizeof(dw)); }
ButtonState::ButtonState(){ std::memset(b, 0, sizeof(b)); }

InputField  g_inputFields[kMaxInputFields];     // unk_695080 / dword_695080
ButtonState g_buttonStates[kMaxButtonStates];   // dword_1406420

void ResetInputFields()  { for (auto& f : g_inputFields)  f = InputField{}; }
void ResetButtonStates() { for (auto& s : g_buttonStates) s = ButtonState{}; }

void ResetWidgetCreate() {
    ResetInputFields();
    ResetButtonStates();
    g_defaultCtrlH = g_screenClipW = g_screenClipExt = 0;
}

// ---- Default stubs for the renderer/property edges -------------------------------------
// These are the OS/renderer-boundary edges. The originals measure glyph widths and bind
// fonts; here we provide neutral defaults so the model is exercisable, and tests may
// override by linking their own definitions (the symbols are weak by being the only TU).
// (Faithful behaviour requires the real font cluster; see report "deferred edges".)
i16 GUILD_WEAK Property_Get(const char* text, int /*font*/) {
    // The original returns the rendered pixel width; the placeholder returns the byte
    // length so width-derived field math (+96 / +6) stays monotonic and testable.
    return static_cast<i16>(text ? std::strlen(text) : 0);
}
int GUILD_WEAK Property_Validate(const char* /*name*/) { return 0; }

// VIBE_GameObject_AttachToWindow @0x40e57c: the original links the widget into the window
// (Z-order + child list). We forward to the shared model epilogue.
void GUILD_WEAK GameObject_AttachToWindow(int widgetIdx, int winSlot) {
    Widget_LinkToWindow(widgetIdx, winSlot);
}

// ---------------------------------------------------------------------------------------
// Shared epilogue: link `widgetIdx` into window `winSlot`.
// Mirrors the identical tail of AddTextLabel/AddButtonLabel/AddFieldToWindow:
//   inherit clip x0/y0,x1/y1 + parentClip(+60) from the window/backing widget,
//   *(w[6] + 4*count) = widgetIdx ; ZOrder_InsertObject(widgetIdx, backing) ; ++count ;
//   grow content-height(+580).
void Widget_LinkToWindow(int widgetIdx, int winSlot) {
    if (winSlot < 0 || winSlot >= kMaxWindows) return;
    if (widgetIdx < 0 || widgetIdx >= kMaxWidgets) return;
    Window& win   = g_windows[winSlot];
    Widget& child = g_widgets[widgetIdx];
    Widget& backing = g_widgets[win.backWidget()];

    // +44 groupLink = the window pointer in the original; here the window's backing widget
    // index, consistent with the existing Object_AddToWindow + ZOrder model (ZOrder groups
    // children by groupLink == parentWidgetIdx). +60 parentClip inherited from the backing
    // widget. Clip bounds (+28..+34) are set per-leaf (they differ by control type) and are
    // intentionally NOT touched here.
    child.groupLink()  = win.backWidget();         // +44
    child.parentClip() = backing.parentClip();      // +60 inherited

    // Append to the window's child id-list, then Z-order, then bump the count.
    i32* list = WindowChildList(winSlot);
    list[win.objCount()] = widgetIdx;                // *(w[6] + 4*count) = idx
    ZOrder_InsertObject(widgetIdx, win.backWidget());
    ++win.objCount();                                // ++*((WORD*)w + 14)

    // Grow content-height: y + (height) if it exceeds the current extent.
    i32 bottom = child.y() + child.h();
    if (bottom > win.contentHeight())
        win.contentHeight() = bottom;
}

namespace {
// 2-byte-at-a-time string copy exactly as the originals emit it (copy pairs until a NUL).
void CopyStr(char* dst, const char* src) {
    const char* s = src;
    char* d = dst;
    for (;;) {
        char c0 = *s;
        *d = c0;
        if (!c0) break;
        char c1 = s[1];
        s += 2;
        d[1] = c1;
        d += 2;
        if (!c1) break;
    }
}

bool WindowReady(int winSlot) {
    if (winSlot < 0 || winSlot >= kMaxWindows) return false;
    return g_windows[winSlot].enabled() != 0;   // *(w+160)
}
} // namespace

// gilde.exe 0x41b288 — VIBE_Object_AddTextLabel
int Object_AddTextLabel(i16 x, i16 y, int winSlot, const char* text) {
    if (!WindowReady(winSlot))                       return -1;   // !v5[160]
    Window& win = g_windows[winSlot];
    if (win.objCount() >= kMaxChildren)              return -1;   // >=384 "Too many objects"

    int idx = Widget_AllocSlot();
    Widget& w = g_widgets[idx];

    // +116 text buffer (0x101 bytes in the original; we use the widget's editText slot as
    // the pointer marker and copy into a per-widget heap buffer kept by SetWidgetData).
    CopyStr(reinterpret_cast<char*>(&w.at<char>(116)), text); // copy into +116 region

    // Font (+112): window font (word @+636) or the default font global.
    i16 font = win.at<i16>(636);                     // v7[318]
    w.at<i16>(112) = font ? font : static_cast<i16>(g_defaultFont);

    w.type()       = kTypeLabel;                     // +24 = 67 'C'
    w.x()          = static_cast<i16>(win.x() + x);  // +16 = win.x + a1
    w.y()          = static_cast<i16>(win.y() + y - win.scrollCur()); // +18 = win.y + a2 - scroll
    i16 propW      = Property_Get(text, w.ld<i32>(110) >> 16); // VIBE_Property_Get
    w.order()      = 2;                              // +26
    w.w()          = static_cast<i16>(propW + 96);   // +20 = width + 96
    w.h()          = static_cast<i16>(g_defaultCtrlH); // +22 = dword_69FFB0 word
    // Clip bounds from window geometry (per-leaf; see disasm +20h/+22h/+1Ch/+1Eh).
    w.clipY0()     = win.y();                         // +32 = window y
    w.clipY1()     = static_cast<i16>(win.h() + win.y()); // +34 = h + y
    w.clipX0()     = win.x();                         // +28 = window x
    w.clipX1()     = static_cast<i16>(win.w() + win.x()); // +30 = w + x
    SetWidgetData(idx, &win);                        // +12 -> window (pointer-valued)

    Widget_LinkToWindow(idx, winSlot);
    return idx;
}

// gilde.exe 0x41b598 — VIBE_Object_AddButtonLabel
int Object_AddButtonLabel(i16 x, i16 y, int winSlot, const char* text) {
    if (!WindowReady(winSlot))                       return -1;
    Window& win = g_windows[winSlot];
    if (win.objCount() >= kMaxChildren)              return -1;

    int idx = Widget_AllocSlot();
    Widget& w = g_widgets[idx];

    CopyStr(reinterpret_cast<char*>(&w.at<char>(124)), text); // button text -> +124

    i16 font = win.at<i16>(636);
    w.at<i16>(112) = font ? font : static_cast<i16>(g_defaultFont);

    w.type()    = kTypeButton;                       // +24 = 70 'F'
    w.x()       = static_cast<i16>(win.x() + x);     // +16
    w.y()       = static_cast<i16>(win.y() + y - win.scrollCur()); // +18
    i16 propW   = Property_Get(text, w.ld<i32>(110) >> 16);
    w.order()   = 2;                                 // +26
    w.clipY0()  = 0;                                 // +32 = 0
    w.clipX0()  = 0;                                 // +28 = 0
    w.w()       = static_cast<i16>(propW + 96);      // +20
    w.h()       = static_cast<i16>(g_defaultCtrlH);  // +22 = dword_69FFB0
    w.clipY1()  = static_cast<i16>(g_screenClipExt); // +34 = dword_69FFBC (low word)
    w.clipX1()  = static_cast<i16>(g_screenClipExt >> 16); // +30 = HIWORD
    SetWidgetData(idx, &win);                        // +12/+44 -> window

    Widget_LinkToWindow(idx, winSlot);
    return idx;
}

// gilde.exe 0x41b0ec — VIBE_Object_SetText
int Object_SetText(int widgetIdx, const char* text) {
    Widget& w = g_widgets[widgetIdx];
    CopyStr(reinterpret_cast<char*>(&w.at<char>(116)), text); // overwrite text buffer +116
    // Recompute width unless either render-override flag (+88 / +92) is set.
    if (w.at<i32>(88) == 0 && w.at<i32>(92) == 0) {
        i16 propW = Property_Get(text, w.ld<i32>(110) >> 16);
        w.w() = propW;                               // +20
        return propW;
    }
    return widgetIdx;
}

// gilde.exe 0x4120a8 — VIBE_Widget_CreateSprite
// a4 (the 4th arg, ecx) is the recompute mode forwarded to Object_RecomputeSize.
int Widget_CreateSprite(i16 x, i16 y, int gfxId, int mode) {
    int idx = Widget_AllocSlot();
    Widget& w = g_widgets[idx];

    w.type()       = kTypeSprite;                    // +24 = 9
    w.x()          = x;                              // +16
    w.at<i32>(72)  = 1;                              // +72 button/clickable flag = 1
    w.at<u8>(444)  = 3;                              // +444 = 3
    // Height (+22) from the graphics-metric table (84-byte stride, word @+82).
    w.h()          = GfxMetricWord(gfxId, 82);
    w.clipY0()     = 0;                              // +32 = 0
    w.clipX0()     = 0;                              // +28 = 0
    w.id()         = gfxId;                          // +8 = gfx id
    w.y()          = y;                              // +18
    w.clipY1()     = static_cast<i16>(g_screenClipExt);       // +34
    w.clipX1()     = static_cast<i16>(g_screenClipExt >> 16); // +30
    w.at<i16>(112) = static_cast<i16>(g_defaultFont); // +112 default font
    SetWidgetData(idx, SceneStateFor(gfxId));        // +12 = VIBE_State_Update(gfx)

    Object_RecomputeSize(idx);                       // recompute width from text/metrics (a4 mode)
    (void)mode;                                      // a4 forwarded to RecomputeSize in the original
    // VIBE_Gui_MarkObjectUsed(gfxId) — usage bookkeeping, deferred.
    return idx;
}

// gilde.exe 0x41217c — VIBE_Widget_AddSpriteToWindow
int Widget_AddSpriteToWindow(i16 x, i16 y, int gfxId, int winSlot) {
    if (winSlot < 0 || winSlot >= kMaxWindows) return -1;
    Window& win = g_windows[winSlot];
    int idx = Widget_CreateSprite(static_cast<i16>(win.x() + x),
                                  static_cast<i16>(win.y() + y), gfxId, winSlot);
    GameObject_AttachToWindow(idx, winSlot);
    return idx;
}

// gilde.exe 0x410180 — VIBE_Widget_CreateSlider  (field-init model part)
int Widget_CreateSlider(i16 x, i16 y, int value, int range, int maxVal, int gfxBase, i16 flags) {
    int idx = Widget_AllocSlot();
    Widget& w = g_widgets[idx];

    // Thumb-track size (+20/+22) derived from the slider's three gfx tiles (track/thumb/cap).
    // The original picks horizontal vs vertical from flags bit 1/2 and walks the 84-byte
    // metric table at gfxBase+1 / gfxBase / gfxBase+3. We reproduce the size selection.
    int n = 0;
    // tile is a gfx metric (>>16 of the slider gfx record); in the real game the
    // slider gfx always resolves so tile != 0. Guard tile==0 (only reachable with an
    // unresolved gfxBase, e.g. a headless form parse) so the tile-count divide can't
    // fault — behaviour-identical for every real input.
    if (flags & 1) {                                 // vertical
        int tile = GfxMetricDword(gfxBase + 1, 80) >> 16;
        n = tile ? range / tile + (range % tile ? 1 : 0) : 0;
        w.w() = SliderTrackExtent(gfxBase, flags);   // +20
        w.h() = static_cast<i16>(range + 2 * GfxMetricWord(gfxBase, 82));
    } else if (flags & 2) {                          // horizontal
        int tile = GfxMetricDword(gfxBase + 1, 78) >> 16;
        n = tile ? range / tile + (range % tile ? 1 : 0) : 0;
        w.h() = SliderTrackExtent(gfxBase, flags);   // +22
        w.w() = static_cast<i16>(range + 2 * (GfxMetricDword(gfxBase, 80) >> 16)); // +20
    }
    (void)n;

    w.x()          = x;                              // +16
    w.y()          = y;                              // +18
    w.at<i32>(120) = value;                          // +120 current value
    w.at<i32>(128) = maxVal;                         // +128 max
    w.at<i16>(132) = flags;                          // +132 flags
    w.at<i32>(124) = value;                          // +124 value mirror
    w.type()       = kTypeEdit;                      // +24 = 69 'E'
    w.at<i32>(72)  = 1;                              // +72 clickable
    w.id()         = 0;                              // +8 = 0
    w.clipY0()     = 0;                              // +32 = 0
    w.clipX0()     = 0;                              // +28 = 0
    w.at<i32>(136) = range;                          // +136 range
    w.at<i32>(140) = maxVal / 2;                     // +140 step
    w.clipY1()     = static_cast<i16>(g_screenClipExt);       // +34
    w.clipX1()     = static_cast<i16>(g_screenClipExt >> 16); // +30
    w.at<i32>(144) = gfxBase;                        // +144 gfx base
    // The slider surface allocation + sprite-sheet blit loop (+148 surface, the
    // VIBE_Animation_Basic tiling) is DEFERRED to the renderer cluster.
    SetWidgetData(idx, SceneStateFor(gfxBase));      // +12 = VIBE_State_Update(gfxBase)
    return idx;
}

// gilde.exe 0x410604 — VIBE_Widget_AddSliderToWindow
int Widget_AddSliderToWindow(i16 x, i16 y, int value, int range, int maxVal, int gfxBase, i16 flags, int winSlot) {
    if (winSlot < 0 || winSlot > 0x60)               return -1;   // a8 > 0x60
    if (!WindowReady(winSlot))                        return -1;
    Window& win = g_windows[winSlot];
    if (win.objCount() >= kMaxChildren)              return -1;

    int idx = Widget_CreateSlider(static_cast<i16>(win.x() + x),
                                  static_cast<i16>(win.y() + y - win.scrollCur()),
                                  value, range, maxVal, gfxBase, flags);
    GameObject_AttachToWindow(idx, winSlot);
    return idx;
}

// gilde.exe 0x40fe04 — VIBE_Input_RegisterField
int Input_RegisterField(int x, int y, int step, int value, u8 flags) {
    // Find the first free field record (slot 0 sentinel; scan while dw[0] != 0).
    int slot = 1;
    if (g_inputFields[0].dw[0]) {
        int i = 87;
        int v10;
        do { v10 = g_inputFields[i / 87].dw[0]; i += 87; ++slot; } while (v10);
    }
    if (slot > 127) return -1;                       // "Too many inputs on screen"
    InputField& f = g_inputFields[slot - 1];         // (char*)base + (slot*87)*4 → record `slot`

    f.dw[6]  = 999;                                  // max
    f.dw[7]  = 0;
    f.dw[74] = 0;
    f.dw[79] = 1;
    f.dw[80] = 1;
    f.at<char>(36) = 3;                              // digit width default
    f.dw[75] = 1;
    f.dw[0]  = slot;                                 // record id
    f.dw[1]  = x;
    f.dw[2]  = y;
    f.dw[4]  = value;
    f.at<u16>(38) = flags;                           // word @+38
    f.dw[5]  = step;
    f.dw[78] = -1;
    f.dw[84] = 0;
    f.dw[77] = Property_Validate("_FONT");           // font id

    int idx = Widget_AllocSlot();
    Widget& w = g_widgets[idx];

    if (flags & 2) {                                 // numeric: derive digit count
        int v14 = f.dw[6], v15 = 0;
        if (v14 > 0) { do { ++v15; v14 /= 10; } while (v15 < 10 && v14 > 0); }
        f.at<char>(36) = static_cast<char>(v15);
        // (the original sprintf("%0Ni", f[79]*f[74]) into f[4] is a display refresh; the
        //  width measure goes through Property_Get and is deferred — value already set.)
        f.dw[4] = Property_Get("", f.dw[77]);
    }
    if (flags & 1) f.dw[6] = 32;                     // text field max len
    if ((flags & 0x10) || (flags & 0x20)) w.at<i32>(100) = 1; // +100 multiline flag

    // Widget geometry mirrors the field record's low-word fields:
    //   field x = word@+4 (dw[1]), y = word@+8 (dw[2]), w = word@+16 (dw[4]), h = word@+20 (dw[5]).
    i16 fx = f.at<i16>(4), fy = f.at<i16>(8), fw = f.at<i16>(16), fh = f.at<i16>(20);
    w.type()       = kTypeAnim;                      // +24 = 65 'A'
    w.x()          = fx;                             // +16 = field x
    w.y()          = fy;                             // +18 = field y
    w.w()          = fw;                             // +20 = field w
    w.h()          = fh;                             // +22 = field h
    w.id()         = 0;                              // +8 = 0
    w.order()      = 2;                              // +26
    w.at<i32>(116) = f.dw[0];                        // +116 = record id
    w.clipY0()     = fy;                             // +32 = field y
    w.clipX0()     = fx;                             // +28 = field x
    w.clipX1()     = static_cast<i16>(fw + fx);      // +30 = w + x
    w.clipY1()     = static_cast<i16>(fh + fy);      // +34 = h + y
    SetWidgetData(idx, &f);                          // +12 -> field record
    f.dw[76] = idx;                                  // record back-ref to widget
    return idx;
}

// gilde.exe 0x410030 — VIBE_Input_AddFieldToWindow
int Input_AddFieldToWindow(int x, int y, int step, int value, u8 flags, int winSlot) {
    if (!WindowReady(winSlot))                        return -1;
    Window& win = g_windows[winSlot];
    if (win.objCount() >= kMaxChildren)              return -1;

    // original: RegisterField(a1+winX, a2+winY-w[146], a3=step, a4=value, a5=flags);
    // w[146] == window +584 == scrollCur.
    int idx = Input_RegisterField(x + win.x(), y + win.y() - win.scrollCur(), step, value, flags);
    Widget_LinkToWindow(idx, winSlot);
    return idx;
}

// gilde.exe 0x40f800 — VIBE_Input_RegisterIcon
int Input_RegisterIcon(int x, int y, u8 flags, int gfxId) {
    int slot = 1;
    if (g_inputFields[0].dw[0]) {
        unsigned i = 348;
        int v9;
        do { v9 = g_inputFields[i / 348].dw[0]; i += 348; ++slot; } while (v9);
    }
    if (slot > 127) return -1;
    InputField& f = g_inputFields[slot - 1];

    f.dw[78] = gfxId;                                // +312 gfx id
    f.dw[2]  = y;
    f.dw[1]  = x;
    f.dw[0]  = slot;
    f.dw[4]  = GfxMetricDword(gfxId, 78) >> 16;      // width from metric
    f.dw[6]  = 999;
    f.dw[7]  = 0;
    f.dw[5]  = GfxMetricDword(gfxId, 80) >> 16;      // height from metric
    f.dw[74] = 0;
    f.at<char>(36) = 3;
    f.at<u16>(38) = static_cast<u16>(flags | 0x100); // word @+38 = (1<<8) | flags
    f.dw[75] = 1;
    f.dw[77] = Property_Validate("_FONT");

    if (flags & 2) {
        int v17 = f.dw[6], v18 = 0;
        if (v17 > 0) { do { ++v18; v17 /= 10; } while (v18 < 10 && v17 > 0); }
        f.at<char>(36) = static_cast<char>(v18);
    }
    if (static_cast<signed char>(flags) < 0) {       // flags & 0x80
        f.dw[6] = (f.dw[6] <= 25) ? 25 : f.dw[6];
    }

    int idx = Widget_AllocSlot();
    Widget& w = g_widgets[idx];
    i16 fx = f.at<i16>(4), fy = f.at<i16>(8), fw = f.at<i16>(16), fh = f.at<i16>(20);
    w.type()       = kTypeAnim;                      // +24 = 65 'A'
    w.x()          = fx;                             // +16
    w.y()          = fy;                             // +18
    w.w()          = fw;                             // +20
    w.h()          = fh;                             // +22
    w.order()      = 2;                              // +26
    w.clipY0()     = 0;                              // +32 = 0
    w.clipX0()     = 0;                              // +28 = 0
    w.id()         = gfxId;                          // +8 = gfx id
    w.at<i32>(116) = f.dw[0];                        // +116 record id
    w.clipY1()     = static_cast<i16>(g_screenClipExt);
    w.clipX1()     = static_cast<i16>(g_screenClipExt >> 16);
    w.at<i16>(112) = static_cast<i16>(g_defaultFont);
    SetWidgetData(idx, &f);                          // +12 -> field record
    f.dw[76] = idx;
    return idx;
}

// gilde.exe 0x40fa18 — VIBE_Input_AddIconToWindow
int Input_AddIconToWindow(int x, int y, u8 flags, int gfxId, int winSlot) {
    if (!WindowReady(winSlot))                        return -1;
    Window& win = g_windows[winSlot];
    if (win.objCount() >= kMaxChildren)              return -1;

    int idx = Input_RegisterIcon(x + win.x(), y + win.y() - win.scrollCur(), flags, gfxId);
    Widget_LinkToWindow(idx, winSlot);
    // Grow the scroll thumb if this icon sits below the current extent (w[145] @+580).
    if (y > win.contentHeight()) win.contentHeight() = y + 4;
    return idx;
}

// gilde.exe 0x41b164 — VIBE_Object_RecomputeSize
int Object_RecomputeSize(int widgetIdx) {
    Widget& w = g_widgets[widgetIdx];
    if (w.type() == kTypeSprite) {                   // type 9 sprite
        // Width from the two glyph metrics + property width + 4 (glyph edges via stub).
        int gw = GlyphAdvance(SceneStateFor(w.id()), 0) + GlyphAdvance(SceneStateFor(w.id()), 1);
        int width = Property_Get("", g_defaultFont) + gw + 4;
        w.w() = static_cast<i16>(width);
        return width;
    }
    // Non-sprite: width = Property_Get(text) + animMargin + 6 (animMargin default 24).
    int animMargin = 24;                             // v18 default when AnimationFlags fails
    if (w.at<i32>(88)) { w.w() = static_cast<i16>(animMargin); return animMargin; }
    int width = Property_Get(reinterpret_cast<const char*>(&w.at<char>(120)),
                             w.ld<i32>(110) >> 16) + animMargin + 6;
    w.w() = static_cast<i16>(width);
    return width;
}

// gilde.exe 0x41dad4 — VIBE_Object_SetUserData
int Object_SetUserData(int localId, int value) {
    // Resolve the current form's object-base: a1 + dword_676BF4[171*form].
    // dword_676BF4 == form base + 101 dwords (per-form object index base).
    int objBase = g_forms[g_currentFormId].dw[101]; // dword_676BF4[171*form]
    int idx = localId + objBase;
    if (idx >= 0 && idx < kMaxWidgets)
        g_widgets[idx].value() = value;             // +36 = a2
    return idx;
}

// gilde.exe 0x41dfec — VIBE_Object_SetValueOrText  (model branches)
char Object_SetObjectValueOrText(int widgetIdx, int textOrValue, int a3, int a4, int a5) {
    if (widgetIdx == -1 || widgetIdx > 512) return static_cast<char>(textOrValue);
    Widget& w = g_widgets[widgetIdx];               // 740*a1 + base

    // (The type-'A' (65) anim branches — inline text copy into the data record +40 and the
    //  +296 slider-range quantiser via Slider_QuantizeRange — are owned by the object/anim
    //  data record and reproduced through the slider helper; the data-record edges are
    //  pointer-valued and handled by the radiogroup/object modules. Here we translate the
    //  edit/slider ('E') and button branches, which are the leaf-creation targets.)

    if (w.type() == kTypeEdit) {                     // 69 'E'
        w.at<i32>(124) = textOrValue;               // +124 = a2
        w.at<i32>(128) = a3;                         // +128 = a3
        i16 fl = w.at<i16>(132);                     // +132 flags
        char result = static_cast<char>(a5);
        w.at<i32>(120) = a4;                         // +120 = a4
        if (fl & 0x10) {                             // clamp-to-step flag
            w.at<i32>(140) = a5;                     // +140 = a5
            return static_cast<char>(a5);
        }
        return result;
    }
    if (w.btnFlagA() || w.btnFlagB()) {             // +68 / +72 set => button
        u8 v = static_cast<u8>(textOrValue);
        w.value()       = v;                         // +36
        w.valueMirror() = v;                         // +40
        return static_cast<char>(v);
    }
    return static_cast<char>(textOrValue);
}

// gilde.exe 0x41b75c — VIBE_Object_SetEditText  (model part: inline +124 copy + width)
char Object_SetEditText(int widgetIdx, const char* text) {
    Widget& w = g_widgets[widgetIdx];
    // The owning-window shared text-buffer splice (+44 buffer, +120 cursor, +188 length;
    // the VIBE_Util_MemMove dance) is DEFERRED. Model part: recompute width + inline copy.
    w.w() = Property_Get(text, w.ld<i32>(110) >> 16); // +20
    CopyStr(reinterpret_cast<char*>(&w.at<char>(124)), text); // +124 inline copy
    return 0;
}

// gilde.exe 0x41f054 — VIBE_Widget_SetButtonState
int Widget_SetButtonState(int widgetIdx, char state) {
    int result = widgetIdx;
    if (widgetIdx <= 512) {
        Widget& w = g_widgets[widgetIdx];           // result*740 + base
        if (w.type() == kTypeToggle) {              // type 4
            int rec = w.at<i32>(116);               // +116 -> button-state record index
            if (rec < 0 || rec >= kMaxButtonStates) return result;
            ButtonState& bs = g_buttonStates[rec];  // byte_1406420[17*rec]
            if (state) {
                if (state == 1) {
                    // byte+4 = bank[rec].frameCount-1 ; byte+12 = 1 (pressed)
                    bs.b[4]  = static_cast<u8>(ButtonBankFrameCount(rec) - 1);
                    bs.b[12] = 1;
                    return 16 * rec;
                }
            } else {
                bs.b[4]  = 0;
                bs.b[12] = 0;
                result = 16 * rec;
            }
        }
    }
    return result;
}

// ---- Stubbed renderer/metric edges (weak; tests may override) ---------------------------
// Graphics-metric table dword_62D204 (84-byte stride): the slider/sprite size math reads
// packed 16.16 metrics. These are renderer-cluster data; we expose neutral accessors so the
// model math is exercisable and tests can seed them.
i16 GUILD_WEAK GfxMetricWord(int /*gfxId*/, int /*byteOff*/)   { return 0; }
i32 GUILD_WEAK GfxMetricDword(int /*gfxId*/, int /*byteOff*/)  { return 0; }
i16 GUILD_WEAK SliderTrackExtent(int /*gfxBase*/, int /*flags*/){ return 0; }
void* GUILD_WEAK SceneStateFor(int /*gfxId*/)                  { return nullptr; }
int GUILD_WEAK GlyphAdvance(void* /*state*/, int /*which*/)    { return 0; }
int GUILD_WEAK ButtonBankFrameCount(int /*rec*/)               { return 1; }

} // namespace guild::gui
