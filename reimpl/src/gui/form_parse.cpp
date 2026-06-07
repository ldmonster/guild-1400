#include "gui/form_parse.h"
#include "gui/form.h"
#include "gui/window.h"
#include "gui/object.h"
#include "gui/widget_create.h"

#include <cstring>

namespace guild::gui {

// ---- Forward-declared renderer / text edges (default; tests override) -------
// Marked weak so a test TU can supply real-ish definitions; the data-model build
// works with these neutral defaults (Property_Validate->-1, no text resolved).
int __attribute__((weak)) Form_PropertyValidate(const char* /*name*/)   { return -1; }
int __attribute__((weak)) Form_FindTextArrayIndex(const char* /*name*/) { return -1; }

namespace {

// Little-endian readers over the raw record bytes (the original reads through a
// __int16* `v110`, i.e. native little-endian word/dword loads).
u16 rd_u16(const u8* p, int off) {
    return static_cast<u16>(p[off] | (static_cast<u16>(p[off + 1]) << 8));
}
u32 rd_u32(const u8* p, int off) {
    return static_cast<u32>(p[off]) | (static_cast<u32>(p[off + 1]) << 8) |
           (static_cast<u32>(p[off + 2]) << 16) | (static_cast<u32>(p[off + 3]) << 24);
}
i32 rd_i32(const u8* p, int off) { return static_cast<i32>(rd_u32(p, off)); }

// A bounded NUL-terminated string starting at record+off (the name fields are 64-byte
// fixed slots; the original treats them as C strings).
std::string rd_str(const u8* rec, int off, int cap) {
    int n = 0;
    while (n < cap && rec[off + n] != 0) ++n;
    return std::string(reinterpret_cast<const char*>(rec + off), n);
}

// gilde.exe 0x41a598 — VIBE_Window_AddChildWindow (data-model core).
// Faithful translation: create a window at parent-adjusted coordinates, append its
// backing widget to the parent's child-id list, inherit the parent's clip pointer,
// bump the parent's child count, and grow the parent content-height. The original's
// margin reads (v7[150]/word@292) are window-private layout fields not modeled in our
// Window struct; they are 0 in the on-disk baseline, so the parent-origin adjustment
// reduces to (parentX>>... + childX) — we use the window pixel x/y (word[2]/word[3]).
// Returns the new window slot, or -1 if the parent slot is not in use.
int BuildChildWindow(i16 x, i16 y, i16 w, i16 h, i32 flags, int parentSlot) {
    Window& parent = g_windows[parentSlot]; // &dword_67EB80[238*a6]
    if (!parent.enabled())                  // !v7[160]
        return -1;

    // v13 = Window_Create(parentX + x - margin, parentY + y - margin, w, h, flags).
    // margins (v7[150]/word@292) are 0 in the baseline; the caller already passes the
    // parent-relative (x - parentX, y - parentY) deltas, so the net is the absolute
    // child position. We reconstruct the absolute position the original computes.
    i16 cx = static_cast<i16>(parent.x() + x);
    i16 cy = static_cast<i16>(parent.y() + y);
    int slot = Window_Create(cx, cy, w, h, flags);
    if (slot < 0)
        return -1;

    Window& child = g_windows[slot];
    int childBacking = child.backWidget();              // dword_67EDEC[238*v13]

    // Append the child's backing widget to the parent's object-id list at its count.
    i32* plist = WindowChildList(parentSlot);
    plist[parent.objCount()] = childBacking;            // *(4*count + v7[6]) = backing

    // Link + inherit clip from the parent backing widget.
    Widget& cb = g_widgets[childBacking];
    cb.groupLink()  = parentSlot;                       // +44 = parent window
    cb.clipY0()     = static_cast<i16>(y + parent.y()); // +32
    cb.clipY1()     = static_cast<i16>(w + parent.y() + y); // +34
    cb.clipX0()     = static_cast<i16>(parent.x() + x); // +28
    cb.clipX1()     = static_cast<i16>(h + parent.x() + x); // +30
    cb.parentClip() = g_widgets[parent.backWidget()].parentClip(); // +60 inherit

    ++parent.objCount();                                // ++word@+28

    i32 bottom = child.h() + y;                         // unk_67EB88[238*v13]>>16 + a2
    if (bottom > parent.contentHeight())
        parent.contentHeight() = bottom;

    return slot;
}

} // namespace

int Form_SniffFormat(const u8* data, std::size_t len) {
    if (len < 4)
        return -1;
    // v97 = byte[3] - '0'; the original branches on (v97 == 2).
    return static_cast<int>(data[3]) - '0';
}

// gilde.exe 0x41beb8 — VIBE_Form_LoadFromResource (byte-buffer parse + table build).
FormFile Form_ParseResourceFile(const u8* data, std::size_t len, const char* name) {
    FormFile out;
    out.name = name ? name : "";

    if (len < 4)
        return out; // ok stays false

    // Header sniff: fmt = byte[3]-'0'; '2' -> FRM2, else old layout.
    int fmt = static_cast<int>(data[3]) - '0';
    out.frm2 = (fmt == 2);

    int headerBytes = out.frm2 ? kFrm2HeaderBytes : kOldHeaderBytes;
    int stride      = out.frm2 ? kFrm2RecordStride : kOldRecordStride;

    // windowCount: FRM2 reads u32 @+4 (after the 4-byte magic); old reads u32 @+0
    // (the original VIBE_Vfs_Seek(0) then re-reads the count).
    if (len < static_cast<std::size_t>(headerBytes))
        return out;
    u32 windowCount = out.frm2 ? rd_u32(data, 4) : rd_u32(data, 0);

    // Buffer must hold the declared records (the original trusts the file; we guard).
    if (len < static_cast<std::size_t>(headerBytes) +
                  static_cast<std::size_t>(windowCount) * stride)
        return out;

    // Allocate a form slot: scan dword_676A60 (form array, 171-dword stride) from
    // slot 1 for the first whose dword[0]==0; cap at 48 (>= 0x30 -> return -1).
    // (The original scans dword_676E90 which is dword_676A60 + an offset into the
    // first record; the recovered semantics are "first free form slot from 1".)
    int formId = 1;
    while (formId < kMaxFormSlots && g_forms[formId].dw[0] != 0)
        ++formId;
    if (formId >= kMaxFormSlots) {
        out.formId = -1;
        return out; // original returns -1
    }
    out.formId = formId;

    // FRM2-specific trailer offsets (byte offsets into the record).
    //   parentIndex @+3792, font @+3728, window text @+3664, palette @+3796.
    // Old-layout trailer differs (parent @+3732, font @v110[1864]=+3728 word, etc.);
    // the object arrays below are identical between the two layouts.
    const int kParentOff = out.frm2 ? 3792 : 3732;
    const int kFontOff    = 3728;
    const int kTextOff    = 3664;

    const u8* recs = data + headerBytes;

    for (u32 wi = 0; wi < windowCount; ++wi) {
        const u8* rec = recs + static_cast<std::size_t>(wi) * stride;

        FormWindowRecord wr;
        wr.x     = rd_u16(rec, 0);                 // word[0]
        wr.y     = rd_u16(rec, 2);                 // HIWORD(dword@0)
        wr.w     = rd_u16(rec, 6);                 // HIWORD(dword@4)
        wr.h     = rd_u16(rec, 4);                 // HIWORD(dword@2)
        wr.flags = rd_u32(rec, 8);                 // dword@8
        wr.parentIndex = rd_i32(rec, kParentOff);  // dword@parentOff
        wr.fontName    = rd_str(rec, kFontOff, 64);
        wr.windowText  = rd_str(rec, kTextOff, 64);

        // Resolve parent and create the window.
        int winSlot;
        if (wr.parentIndex) {
            // parentWindowSlot = g_forms[formId].dw[parentIndex] (== dword_676A60[
            // 171*formId + parentIndex]); parentIndex is 1-based so it indexes the
            // window-id table (dw[1+slot]).
            int parentSlot = g_forms[formId].dw[wr.parentIndex];
            wr.parentWindowSlot = parentSlot;
            // The original passes parent-relative deltas (x - parentX, y - parentY).
            i16 dx = static_cast<i16>(wr.x - g_windows[parentSlot].x());
            i16 dy = static_cast<i16>(wr.y - g_windows[parentSlot].y());
            winSlot = BuildChildWindow(dx, dy, static_cast<i16>(wr.w),
                                       static_cast<i16>(wr.h),
                                       static_cast<i32>(wr.flags), parentSlot);
        } else {
            winSlot = Window_Create(static_cast<i16>(wr.x), static_cast<i16>(wr.y),
                                    static_cast<i16>(wr.w), static_cast<i16>(wr.h),
                                    static_cast<i32>(wr.flags));
        }
        wr.windowSlot = winSlot;

        // Store the window slot into the form's window-id table at logical slot wi
        // (dword_676A64[171*formId + wi] == g_forms[formId].dw[1+wi]).
        if (winSlot >= 0)
            g_forms[formId].dw[1 + wi] = winSlot;

        // ---- Object loop -------------------------------------------------------
        // objectCount = HIWORD(dword@+202) == word@+204.
        int objCount = static_cast<int>(rd_u32(rec, 202) >> 16);
        for (int o = 0; o < objCount; ++o) {
            FormObjectRecord orec;
            orec.type = rd_i32(rec, 208 + 8 * o);          // *((int*)v35+52)
            orec.aux  = rd_i32(rec, 3472 + 8 * o);         // *((int*)v35+868)
            orec.x    = rd_i32(rec, 10  + 2 * o) >> 16;    // *(int*)(v36+5) >> 16
            orec.y    = rd_i32(rec, 106 + 2 * o) >> 16;    // *(int*)(v36+53) >> 16
            orec.name = rd_str(rec, 400 + 64 * o, 64);     // v37 (64-byte slot)

            if (winSlot < 0) { wr.objects.push_back(orec); continue; }

            // Build the widget by type, reusing the create leaves.
            if (orec.type == kFormObjSprite || orec.type == kFormObjWindow) {
                // The general image/window-backing object path: Object_AddToWindow
                // with the graphic id resolved from the name (Property_Validate).
                int gfx = Form_PropertyValidate(orec.name.c_str());
                int idx = Object_AddToWindow(winSlot, static_cast<i16>(orec.y),
                                             static_cast<i16>(orec.x), gfx);
                orec.widgetIdx = idx;
                if (idx >= 0) {
                    // aux==18 -> clickable button; aux==8 -> toggle button.
                    if (orec.aux == kSpriteAuxClickable) {
                        g_widgets[idx].btnFlagA() = 1;   // +68
                        g_widgets[idx].btnFlagB() = 0;   // +72
                    } else if (orec.aux == kSpriteAuxToggle) {
                        g_widgets[idx].btnFlagA() = 0;
                        g_widgets[idx].btnFlagB() = 1;
                    }
                }
            } else if (orec.type == kFormObjLabel) {
                // Text label: only built when the name resolves to a text-array id
                // (the original guards FindTextArrayIndex != -1).
                int ti = Form_FindTextArrayIndex(orec.name.c_str());
                if (ti != -1)
                    orec.widgetIdx =
                        Object_AddTextLabel(static_cast<i16>(orec.x),
                                            static_cast<i16>(orec.y), winSlot,
                                            orec.name.c_str());
            } else if (orec.type == kFormObjInput) {
                // Numeric input field: step = HIWORD(aux), value = BYTE1(aux), flags
                // = record +3472+... low byte (per the original BYTE2/BYTE1 reads).
                int step  = (orec.aux >> 16) & 0xFF;
                int value = (orec.aux >> 8) & 0xFF;
                u8  fl    = rec[3472 + 8 * o]; // *((_BYTE*)v35+3472) low byte of aux
                orec.widgetIdx = Input_AddFieldToWindow(orec.x, orec.y, step, value,
                                                        fl, winSlot);
            } else if (orec.type == kFormObjSlider) {
                // Slider: gfxBase from the object name; flags low byte of aux; range
                // from a per-object byte (v108+3868); value 0, max 100 (constants).
                int gfx = Form_PropertyValidate(orec.name.c_str());
                u8  fl  = rec[3472 + 8 * o];
                int range = rec[3868 + 8 * o]; // *((u8*)v108+3868)
                orec.widgetIdx = Widget_AddSliderToWindow(
                    static_cast<i16>(orec.x), static_cast<i16>(orec.y), 0, range, 100,
                    gfx, static_cast<i16>(fl), winSlot);
            }
            // type 0 (inert) and any other: no widget created.

            wr.objects.push_back(orec);
        }

        out.windows.push_back(std::move(wr));
    }

    // Stamp the form record valid + record the new form id + copy the name. The
    // original sets dword_676BF0/BFC/C00[171*formId]=1, dword_676A60[171*formId]=formId,
    // dword_62D258=formId, and copies `name` into form +139 dwords (+556 bytes).
    g_forms[formId].dw[0]   = formId;     // dword_676A60[171*formId] = formId
    g_forms[formId].valid() = 1;          // dw[100] guard (Form_Destroy)
    g_currentFormId         = formId;     // dword_62D258
    out.ok = true;
    return out;
}

} // namespace guild::gui
