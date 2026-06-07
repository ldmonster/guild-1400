// Unit tests for the recovered `.form` resource-file parser
// (gilde.exe 0x41beb8 -> guild::gui::Form_ParseResourceFile). Synthetic FRM2 and
// old-layout buffers are built with known fields and the parsed records + the live
// retained-mode tables are checked.
#include "tests/framework/test.h"

#include "gui/form_parse.h"
#include "gui/form.h"
#include "gui/window.h"
#include "gui/object.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::gui;

// Override the renderer/text edges so label/property resolution is deterministic.
// Property_Validate returns a stable per-name id; FindTextArrayIndex resolves names
// that start with '#' (so labels can be selectively built).
namespace guild::gui {
int Form_PropertyValidate(const char* name) {
    if (!name || !name[0]) return -1;
    // map first char to a small positive id for assertions
    return 1000 + static_cast<unsigned char>(name[0]);
}
int Form_FindTextArrayIndex(const char* name) {
    if (name && name[0] == '#') return 7;  // resolvable text label
    return -1;
}
} // namespace guild::gui

namespace {

// Build a single FRM2 window record (4124 bytes) with the given header + objects.
struct ObjSpec { int32_t type; int32_t aux; int x; int y; const char* name; };

void putU16(std::vector<u8>& b, int off, uint16_t v) {
    b[off] = v & 0xFF; b[off + 1] = (v >> 8) & 0xFF;
}
void putU32(std::vector<u8>& b, int off, uint32_t v) {
    b[off] = v & 0xFF; b[off + 1] = (v >> 8) & 0xFF;
    b[off + 2] = (v >> 16) & 0xFF; b[off + 3] = (v >> 24) & 0xFF;
}

std::vector<u8> makeRecord(int stride, uint16_t x, uint16_t y, uint16_t w,
                           uint16_t h, uint32_t flags, int parentIndex,
                           int parentOff, const std::vector<ObjSpec>& objs) {
    std::vector<u8> rec(stride, 0);
    putU16(rec, 0, x);                 // word[0]
    putU16(rec, 2, y);                 // HIWORD(dword@0)
    putU16(rec, 4, h);                 // HIWORD(dword@2)
    putU16(rec, 6, w);                 // HIWORD(dword@4)
    putU32(rec, 8, flags);             // dword@8
    putU32(rec, parentOff, static_cast<uint32_t>(parentIndex));
    // font name "_FONT" at +3728
    std::memcpy(&rec[3728], "_FONT", 5);
    // object count = HIWORD(dword@202) == word@204
    putU16(rec, 204, static_cast<uint16_t>(objs.size()));
    for (size_t o = 0; o < objs.size(); ++o) {
        const ObjSpec& s = objs[o];
        putU32(rec, 208 + 8 * static_cast<int>(o), static_cast<uint32_t>(s.type));
        putU32(rec, 3472 + 8 * static_cast<int>(o), static_cast<uint32_t>(s.aux));
        // x = (dword@10+2o) >> 16 == word@(12+2o); y = (dword@106+2o)>>16 == word@(108+2o).
        // The per-object x/y are a packed 16-bit array (stride 2; consecutive dwords
        // overlap by 2 bytes) — store the HIGH word directly.
        putU16(rec, 12  + 2 * static_cast<int>(o), static_cast<uint16_t>(s.x));
        putU16(rec, 108 + 2 * static_cast<int>(o), static_cast<uint16_t>(s.y));
        if (s.name)
            std::strncpy(reinterpret_cast<char*>(&rec[400 + 64 * static_cast<int>(o)]),
                         s.name, 63);
    }
    return rec;
}

std::vector<u8> makeFrm2(const std::vector<std::vector<u8>>& records) {
    std::vector<u8> f(8, 0);
    std::memcpy(f.data(), "FRM2", 4);
    putU32(f, 4, static_cast<uint32_t>(records.size()));
    for (auto& r : records) f.insert(f.end(), r.begin(), r.end());
    return f;
}

} // namespace

TEST(GuiFormParse, SniffDetectsFrm2AndOld) {
    u8 frm2[4] = {'F', 'R', 'M', '2'};
    u8 old[4]  = {1, 0, 0, 0};                 // old-layout header: byte[3] == 0
    CHECK_EQ(Form_SniffFormat(frm2, 4), 2);    // '2' - '0'
    CHECK_EQ(Form_SniffFormat(old, 4), 0 - static_cast<int>('0')); // 0 - '0' = -48 (!= 2)
    CHECK_EQ(Form_SniffFormat(frm2, 2), -1);   // too short
}

TEST(GuiFormParse, ParsesSingleWindowHeaderAndGeometry) {
    ResetGuiState();
    auto rec = makeRecord(kFrm2RecordStride, /*x*/57, /*y*/69, /*w*/480, /*h*/550,
                          /*flags*/0x11, /*parent*/0, /*parentOff*/3792, {});
    auto file = makeFrm2({rec});

    FormFile f = Form_ParseResourceFile(file.data(), file.size(), "Geb_Bauen");
    CHECK(f.ok);
    CHECK(f.frm2);
    CHECK(f.formId >= 1);
    CHECK_EQ(f.windows.size(), static_cast<size_t>(1));

    const FormWindowRecord& w = f.windows[0];
    CHECK_EQ(w.x, 57);
    CHECK_EQ(w.y, 69);
    CHECK_EQ(w.w, 480);
    CHECK_EQ(w.h, 550);
    CHECK_EQ(w.flags, 0x11u);
    CHECK_EQ(w.parentIndex, 0);
    CHECK_EQ(w.fontName, std::string("_FONT"));
    CHECK(w.windowSlot >= 0);

    // The live window table got the geometry.
    Window& live = g_windows[w.windowSlot];
    CHECK_EQ(static_cast<int>(live.x()), 57);
    CHECK_EQ(static_cast<int>(live.y()), 69);
    CHECK_EQ(static_cast<int>(live.w()), 480);
    CHECK_EQ(static_cast<int>(live.h()), 550);

    // Form window-id table points at the created slot.
    CHECK_EQ(g_forms[f.formId].dw[1 + 0], w.windowSlot);
    CHECK_EQ(g_currentFormId, f.formId);
}

TEST(GuiFormParse, BuildsObjectsByType) {
    ResetGuiState();
    std::vector<ObjSpec> objs = {
        {kFormObjSprite, kSpriteAuxClickable, 10, 20, "_RAHMEN_HAUSBAU+1"}, // clickable
        {kFormObjSprite, kSpriteAuxToggle,    30, 40, "_RAHMEN_HAUSBAU+2"}, // toggle
        {kFormObjSprite, 0,                   50, 60, "_RAHMEN_HAUSBAU+3"}, // plain
        {kFormObjLabel,  0,                    5,  6, "#someLabel"},        // resolves
        {kFormObjLabel,  0,                    7,  8, "noLabel"},           // skipped
        {kFormObjInert,  0,                    0,  0, ""},                  // no widget
    };
    auto rec = makeRecord(kFrm2RecordStride, 0, 0, 200, 200, 0x10, 0, 3792, objs);
    auto file = makeFrm2({rec});

    FormFile f = Form_ParseResourceFile(file.data(), file.size(), "objs");
    CHECK(f.ok);
    CHECK_EQ(f.windows.size(), static_cast<size_t>(1));
    const FormWindowRecord& w = f.windows[0];
    CHECK_EQ(w.objects.size(), static_cast<size_t>(6));

    // Sprite types and positions.
    CHECK_EQ(w.objects[0].type, kFormObjSprite);
    CHECK_EQ(w.objects[0].x, 10);
    CHECK_EQ(w.objects[0].y, 20);
    CHECK_EQ(w.objects[0].name, std::string("_RAHMEN_HAUSBAU+1"));
    CHECK(w.objects[0].widgetIdx >= 0);
    CHECK_EQ(g_widgets[w.objects[0].widgetIdx].btnFlagA(), 1); // clickable
    CHECK_EQ(g_widgets[w.objects[0].widgetIdx].btnFlagB(), 0);

    CHECK(w.objects[1].widgetIdx >= 0);
    CHECK_EQ(g_widgets[w.objects[1].widgetIdx].btnFlagA(), 0); // toggle
    CHECK_EQ(g_widgets[w.objects[1].widgetIdx].btnFlagB(), 1);

    CHECK(w.objects[2].widgetIdx >= 0); // plain sprite still built

    // Label #4 resolves -> built; label #5 does not -> skipped.
    CHECK(w.objects[3].widgetIdx >= 0);
    CHECK_EQ(w.objects[4].widgetIdx, -1);

    // Inert object -> no widget.
    CHECK_EQ(w.objects[5].widgetIdx, -1);

    // Widget x/y are window-relative + window origin (window at 0,0 here so == obj).
    CHECK_EQ(static_cast<int>(g_widgets[w.objects[0].widgetIdx].x()), 10);
    CHECK_EQ(static_cast<int>(g_widgets[w.objects[0].widgetIdx].y()), 20);
}

TEST(GuiFormParse, ChildWindowResolvesParentAndOffsets) {
    ResetGuiState();
    // Root window at (100,50); child window declares parentIndex=1, absolute (130,70).
    auto root  = makeRecord(kFrm2RecordStride, 100, 50, 300, 300, 0x11, 0, 3792, {});
    auto child = makeRecord(kFrm2RecordStride, 130, 70,  80,  60, 0x10, 1, 3792, {});
    auto file  = makeFrm2({root, child});

    FormFile f = Form_ParseResourceFile(file.data(), file.size(), "nested");
    CHECK(f.ok);
    CHECK_EQ(f.windows.size(), static_cast<size_t>(2));

    const FormWindowRecord& c = f.windows[1];
    CHECK_EQ(c.parentIndex, 1);
    CHECK(c.windowSlot >= 0);
    CHECK_EQ(c.parentWindowSlot, f.windows[0].windowSlot);

    // The child window was created at the absolute declared position.
    Window& live = g_windows[c.windowSlot];
    CHECK_EQ(static_cast<int>(live.x()), 130);
    CHECK_EQ(static_cast<int>(live.y()), 70);

    // The parent gained a child (its backing widget appended to its child list).
    Window& parent = g_windows[f.windows[0].windowSlot];
    CHECK(parent.objCount() >= 1);
}

TEST(GuiFormParse, OldLayoutFormatIsParsed) {
    ResetGuiState();
    // Old layout: header is just u32 count (4 bytes), record stride 3796, parent@3732.
    // Header byte[3] must not be '2' (count=1 -> bytes 01 00 00 00, byte[3]=0).
    auto rec = makeRecord(kOldRecordStride, 12, 34, 100, 80, 0x4095, 0, 3732, {});
    std::vector<u8> file(4, 0);
    file[0] = 1; // count = 1
    file.insert(file.end(), rec.begin(), rec.end());

    CHECK_EQ(Form_SniffFormat(file.data(), file.size()), 0 - static_cast<int>('0')); // byte[3]=0
    FormFile f = Form_ParseResourceFile(file.data(), file.size(), "help");
    CHECK(f.ok);
    CHECK(!f.frm2);
    CHECK_EQ(f.windows.size(), static_cast<size_t>(1));
    CHECK_EQ(f.windows[0].x, 12);
    CHECK_EQ(f.windows[0].y, 34);
    CHECK_EQ(f.windows[0].w, 100);
    CHECK_EQ(f.windows[0].h, 80);
}

TEST(GuiFormParse, RejectsShortAndTruncatedBuffers) {
    ResetGuiState();
    u8 tiny[3] = {'F', 'R', 'M'};
    FormFile a = Form_ParseResourceFile(tiny, 3, "x");
    CHECK(!a.ok);

    // FRM2 header claiming 4 windows but no record bytes -> rejected (short guard).
    std::vector<u8> hdr(8, 0);
    std::memcpy(hdr.data(), "FRM2", 4);
    hdr[4] = 4;
    FormFile b = Form_ParseResourceFile(hdr.data(), hdr.size(), "trunc");
    CHECK(!b.ok);
}
