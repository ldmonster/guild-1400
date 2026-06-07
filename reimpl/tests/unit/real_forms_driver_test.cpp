// Unit test for the real-asset GUI-forms driver (guild::app::DriveFormBuffer).
//
// Drives the FRM2 parse + widget-tree build over SMALL SYNTHETIC in-format bytes —
// no real assets needed. We hand-build a 2-window "FRM2" blob (root + one child)
// with a labeled mix of object types and assert the driver's structural summary
// matches the bytes exactly (windows, child links, per-type widget counts).
//
// The driver installs strong inert hooks for the FRM2 parser's renderer/text edges
// (Form_PropertyValidate / Form_FindTextArrayIndex), so labels/sliders DO build and
// the edge counters tick — we assert both.
#include "tests/framework/test.h"

#include "app/real_forms_driver.h"
#include "gui/form_parse.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::app;

namespace {

// Little-endian writers into a record (matches the parser's rd_u16/rd_u32 reads).
void wr_u16(u8* p, int off, u16 v) { p[off] = u8(v); p[off + 1] = u8(v >> 8); }
void wr_u32(u8* p, int off, u32 v) {
    p[off] = u8(v); p[off + 1] = u8(v >> 8); p[off + 2] = u8(v >> 16); p[off + 3] = u8(v >> 24);
}
void wr_name(u8* p, int off, const char* s) {
    std::size_t n = std::strlen(s);
    if (n > 63) n = 63;
    std::memcpy(p + off, s, n);
    p[off + n] = 0;
}

// One FRM2 window record (kFrm2RecordStride bytes). The object arrays follow the
// recovered per-object offsets in gui/form_parse.cpp.
struct ObjSpec { i32 type; i32 aux; i32 x; i32 y; const char* name; };

void WriteWindow(u8* rec, int x, int y, int w, int h, u32 flags,
                 i32 parentIndex, const char* font, const char* text,
                 const std::vector<ObjSpec>& objs) {
    wr_u16(rec, 0, u16(x));   // word[0] x
    wr_u16(rec, 2, u16(y));   // HIWORD(dword@0) y
    wr_u16(rec, 6, u16(w));   // HIWORD(dword@4) w
    wr_u16(rec, 4, u16(h));   // HIWORD(dword@2) h
    wr_u32(rec, 8, flags);    // dword@8 flags
    // objectCount = HIWORD(dword@+202) == word@+204.
    wr_u16(rec, 204, u16(objs.size()));
    wr_u32(rec, 3792, u32(parentIndex)); // FRM2 parent @ +3792
    wr_name(rec, 3728, font);            // font @ +3728
    wr_name(rec, 3664, text);            // window text @ +3664
    for (std::size_t o = 0; o < objs.size(); ++o) {
        const ObjSpec& s = objs[o];
        wr_u32(rec, 208  + 8 * int(o), u32(s.type));      // type
        wr_u32(rec, 3472 + 8 * int(o), u32(s.aux));       // aux
        wr_u32(rec, 10   + 2 * int(o), u32(s.x) << 16);   // x16.16 (>>16 = x)
        wr_u32(rec, 106  + 2 * int(o), u32(s.y) << 16);   // y16.16
        wr_name(rec, 400 + 64 * int(o), s.name);          // 64-byte name slot
    }
}

// Build a complete 2-window FRM2 blob: header ("FRM2" + u32 windowCount) + records.
std::vector<u8> BuildFrm2(const std::vector<std::vector<ObjSpec>>& windowObjs,
                          const std::vector<i32>& parents) {
    const int stride = gui::kFrm2RecordStride;
    const int hdr    = gui::kFrm2HeaderBytes;
    int nw = int(windowObjs.size());
    std::vector<u8> buf(hdr + nw * stride, 0);
    buf[0] = 'F'; buf[1] = 'R'; buf[2] = 'M'; buf[3] = '2';
    wr_u32(buf.data(), 4, u32(nw));
    for (int i = 0; i < nw; ++i) {
        u8* rec = buf.data() + hdr + i * stride;
        WriteWindow(rec, /*x*/ 10 + i * 5, /*y*/ 20 + i * 5, /*w*/ 200, /*h*/ 100,
                    /*flags*/ 0, parents[i], "_FONT", "WinText", windowObjs[i]);
    }
    return buf;
}

} // namespace

TEST(RealFormsDriver, SyntheticFrm2BuildsWidgetTree) {
    // Window 0 = root (parent 0): a sprite, a clickable sprite, a label, an input,
    // a slider, and one inert object. Window 1 = child of window 0 (parentIndex 1).
    std::vector<ObjSpec> w0 = {
        {gui::kFormObjSprite, 0,                       5,  5,  "img"},
        {gui::kFormObjSprite, gui::kSpriteAuxClickable, 30, 5,  "btn"},
        {gui::kFormObjLabel,  0,                       5,  30, "lbl"},
        {gui::kFormObjInput,  0,                       5,  50, "field"},
        {gui::kFormObjSlider, 0,                       5,  70, "sld"},
        {gui::kFormObjInert,  0,                       5,  90, "spacer"},
    };
    std::vector<ObjSpec> w1 = {
        {gui::kFormObjLabel, 0, 2, 2, "childlbl"},
    };

    std::vector<u8> blob = BuildFrm2({w0, w1}, /*parents*/ {0, 1});

    DrivenForm df = DriveFormBuffer(blob.data(), blob.size(), "synthetic.form");

    CHECK(df.parsed);
    CHECK(df.frm2);
    CHECK(df.formId >= 1);
    CHECK_EQ(df.windowCount, 2);
    CHECK_EQ(df.rootWindows, 1);
    CHECK_EQ(df.childWindows, 1);

    // 6 records in w0 + 1 in w1 = 7 object records seen.
    CHECK_EQ(df.objectRecords, 7);

    // Built widgets: 2 sprites + 1 label + 1 input + 1 slider in w0, 1 label in w1.
    // The inert (type 0) object builds nothing.
    CHECK_EQ(df.spriteCount, 2);
    CHECK_EQ(df.labelCount, 2);
    CHECK_EQ(df.inputCount, 1);
    CHECK_EQ(df.sliderCount, 1);
    CHECK_EQ(df.widgetCount, 6); // 2 + 2 + 1 + 1

    // The inert edge hooks fired: sprites + slider resolve a property (3), labels
    // resolve a text index (2). Read the module counters back.
    FormHookCounts hc = RealFormsHookCounts();
    CHECK_EQ(hc.propertyValidate, 3); // 2 sprites + 1 slider
    CHECK_EQ(hc.findTextIndex, 2);    // 2 labels
}

TEST(RealFormsDriver, ShortBufferIsRejectedCleanly) {
    // A buffer too short to hold the declared record count must be rejected (ok=false)
    // without crashing — the driver's guard path.
    std::vector<u8> blob(gui::kFrm2HeaderBytes, 0);
    blob[0] = 'F'; blob[1] = 'R'; blob[2] = 'M'; blob[3] = '2';
    // Claim 3 windows but provide no record bytes.
    blob[4] = 3;

    DrivenForm df = DriveFormBuffer(blob.data(), blob.size(), "short.form");
    CHECK(!df.parsed);
    CHECK_EQ(df.windowCount, 0);
    CHECK_EQ(df.widgetCount, 0);
}

TEST(RealFormsDriver, EmptyBufferRejected) {
    DrivenForm df = DriveFormBuffer(nullptr, 0, "empty.form");
    CHECK(!df.parsed);
}

TEST(RealFormsDriver, FormatSniffDistinguishesLayouts) {
    // The format is selected by byte[3]-'0': '2' => FRM2, anything else => old layout.
    u8 frm2[8] = {'F', 'R', 'M', '2', 1, 0, 0, 0};
    u8 old1[8] = {'F', 'R', 'M', '1', 1, 0, 0, 0};
    CHECK_EQ(gui::Form_SniffFormat(frm2, sizeof frm2), 2); // FRM2
    CHECK(gui::Form_SniffFormat(old1, sizeof old1) != 2);  // old layout
    u8 tiny[2] = {0, 0};
    CHECK_EQ(gui::Form_SniffFormat(tiny, sizeof tiny), -1); // too short
}
