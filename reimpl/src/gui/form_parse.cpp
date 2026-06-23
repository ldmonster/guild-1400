#include "gui/form_parse.h"
#include "gui/form.h"
#include "gui/window.h"
#include "gui/object.h"
#include "gui/widget_create.h"

#include <algorithm>
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
    // The id list holds kMaxChildren (384) ids; guard the write the same way
    // Object_AddToWindow does ("Too many objects on window!") so a parent that is
    // already full can't write plist[384..] out of bounds.
    if (parent.objCount() >= kMaxChildren)
        return -1;
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

    // Allocate a form slot (0x41c037): the original scans dword_676E90, which is
    // dword_676A60 + 268 dwords == the windowCount field (dw[97]) of form[1]. The
    // scan walks consecutive forms' windowCount() with stride 171, starting at v105=1,
    // and returns the first form (>=1) whose windowCount()==0 (i.e. the first free
    // slot); it caps at 48 (v30>=8208) and returns -1 when no slot is free.
    //   NOTE: it is windowCount() (dw[97]) that is the free marker here, NOT dw[0] —
    //   Form_InitTables stamps dw[0]=index for every slot, so dw[0] is never 0 for
    //   forms 1..47 and scanning it would never find a free slot.
    int formId = 1;
    while (formId < kMaxFormSlots && g_forms[formId].windowCount() != 0)
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
        // parentIndex is a 1-based index into the form window-id table dw[1..96]; a
        // valid `.form` only ever names a parent that already appeared earlier. A
        // malformed file can carry any dword here — guard the read against the form
        // window-id table range so we never index g_forms[].dw out of bounds, and
        // guard the resolved slot against g_windows[] (kMaxWindows). On a bad index
        // we fall through to a root window (the original assumes a valid file).
        if (wr.parentIndex >= 1 && wr.parentIndex <= kMaxWindows) {
            // parentWindowSlot = g_forms[formId].dw[parentIndex] (== dword_676A60[
            // 171*formId + parentIndex]); parentIndex is 1-based so it indexes the
            // window-id table (dw[1+slot]).
            int parentSlot = g_forms[formId].dw[wr.parentIndex];
            wr.parentWindowSlot = parentSlot;
            if (parentSlot < 0 || parentSlot >= kMaxWindows) {
                // Stale/garbage slot id (parent not yet built / malformed) — fail safe.
                winSlot = -1;
                wr.parentWindowSlot = -1;
            } else {
                // The original passes parent-relative deltas (x - parentX, y - parentY).
                i16 dx = static_cast<i16>(wr.x - g_windows[parentSlot].x());
                i16 dy = static_cast<i16>(wr.y - g_windows[parentSlot].y());
                winSlot = BuildChildWindow(dx, dy, static_cast<i16>(wr.w),
                                           static_cast<i16>(wr.h),
                                           static_cast<i32>(wr.flags), parentSlot);
            }
        } else if (wr.parentIndex) {
            // Out-of-range parent index in a malformed file: do not index the table.
            winSlot = -1;
        } else {
            winSlot = Window_Create(static_cast<i16>(wr.x), static_cast<i16>(wr.y),
                                    static_cast<i16>(wr.w), static_cast<i16>(wr.h),
                                    static_cast<i32>(wr.flags));
        }
        wr.windowSlot = winSlot;

        // Store the window slot into the form's window-id table at logical slot wi
        // (dword_676A64[171*formId + wi] == g_forms[formId].dw[1+wi]). The window-id
        // table is dw[1..96]; only 96 window slots exist, so a valid form never has
        // wi >= 96 (Window_Create returns -1 past 96). Guard the table write so a
        // malformed windowCount > 96 can't overwrite dw[97] (the window-count) or run
        // past the 171-dword Form record.
        if (winSlot >= 0 && wi < static_cast<u32>(kMaxWindows))
            g_forms[formId].dw[1 + wi] = winSlot;

        // Bump the form's windowCount (dw[97], == dword_676BE4[171*formId]) once per
        // window, exactly as the original does inside the loop (v43 = ...+1; store).
        // This is what makes the form usable afterward (Form_SelectWindow / Destroy /
        // SetChildrenVisible all read windowCount()). It runs unconditionally per
        // window in the original; the reconstruction increments it the same way.
        ++g_forms[formId].windowCount();

        // ---- Object loop -------------------------------------------------------
        // objectCount = HIWORD(dword@+202) == word@+204.  Original (0x41c3cb):
        //   mov eax,[v40+0CAh]; sar eax,10h  -> signed (int)dword@+202 >> 16.
        int objCount = static_cast<int>(rd_i32(rec, 202)) >> 16;
        // PER-OBJECT STRIDES (verified from disasm @0x41c4d6, NOT the +8/+8/+8 the
        // earlier reconstruction assumed):
        //   v40 (type/aux base)   `add esi,4`   -> type @+208+4*o ; aux @+3472+4*o
        //   v41 (x/y base)        `add edi,2`   -> x16 @+10 +2*o  ; y16 @+106 +2*o
        //   v42 (name base)       `add ebp,40h` -> name @+400+64*o (64-byte slot)
        //   v113 (slider range)   `inc`         -> range byte @+3868+1*o
        //
        // The per-object arrays live INSIDE the fixed-size window record, so the
        // object count is implicitly bounded by the record layout. A valid `.form`
        // never declares more objects than fit; a malformed/oversized count would
        // otherwise read a per-object field past the record. Clamp objCount so EVERY
        // unconditional per-object read stays within `stride` (min over all arrays —
        // byte-identical for valid forms, fail-safe for malformed ones).
        auto maxIdx = [](int base, int step, int span, int strideBytes) {
            if (step <= 0) return 1 << 30;
            return (strideBytes - base - span) / step; // largest o with base+step*o+span <= stride
        };
        int kMaxObjPerRecord = maxIdx(208, 4, 4, stride);
        kMaxObjPerRecord = std::min(kMaxObjPerRecord, maxIdx(3472, 4, 4, stride));
        kMaxObjPerRecord = std::min(kMaxObjPerRecord, maxIdx(10, 2, 4, stride));
        kMaxObjPerRecord = std::min(kMaxObjPerRecord, maxIdx(106, 2, 4, stride));
        kMaxObjPerRecord = std::min(kMaxObjPerRecord, maxIdx(400, 64, 64, stride));
        int objCap = kMaxObjPerRecord + 1; // count = highest index + 1
        if (objCount > objCap)
            objCount = objCap;
        if (objCount < 0)
            objCount = 0;
        for (int o = 0; o < objCount; ++o) {
            FormObjectRecord orec;
            orec.type = rd_i32(rec, 208 + 4 * o);          // [v40+0D0h]
            orec.aux  = rd_i32(rec, 3472 + 4 * o);         // [v40+0D90h]
            orec.x    = rd_i32(rec, 10  + 2 * o) >> 16;    // [v41+0Ah]  sar 10h
            orec.y    = rd_i32(rec, 106 + 2 * o) >> 16;    // [v41+6Ah]  sar 10h
            orec.name = rd_str(rec, 400 + 64 * o, 64);     // v42 (64-byte slot)

            if (winSlot < 0) { wr.objects.push_back(orec); continue; }

            // The original's dispatch is NOT a mutually-exclusive type switch — it is
            // two independent decisions (verified @0x41c3ea / 0x41c490 / 0x41c713):
            //   (A) if (type < 64)            -> Object_AddToWindow  (sprites + all
            //                                    types 0..63, INCLUDING inert type 0)
            //   (B) then, separately:
            //         if (type == 67 'C') && StrCmp("", name) -> text label
            //         else if (type == 65 'A')                -> Input_AddFieldToWindow
            //         else if (type == 69 'E')                -> Widget_AddSliderToWindow

            // (A) type < 64: Object_AddToWindow with gfx resolved from the name.
            if (orec.type < kFormObjWindow) { // < 64
                int gfx = Form_PropertyValidate(orec.name.c_str());
                int idx = Object_AddToWindow(winSlot, static_cast<i16>(orec.y),
                                             static_cast<i16>(orec.x), gfx);
                orec.widgetIdx = idx;
                if (idx >= 0) {
                    // aux==18 -> clickable button; aux==8 -> toggle button.
                    if (orec.aux == kSpriteAuxClickable) {
                        g_widgets[idx].btnFlagA() = 1;   // +68
                        g_widgets[idx].btnFlagB() = 0;   // +72
                    }
                    if (orec.aux == kSpriteAuxToggle) {  // original: two separate ifs
                        g_widgets[idx].btnFlagA() = 0;
                        g_widgets[idx].btnFlagB() = 1;
                    }
                }
            }

            // (B) the independent type switch.
            if (orec.type == kFormObjLabel) {            // 67 'C'
                // Original guard: VIBE_Util_StrCmp(&dword_610ECC, name) (dword_610ECC
                // is ""), i.e. only when name is non-empty; then FindTextArrayIndex.
                if (!orec.name.empty()) {
                    int ti = Form_FindTextArrayIndex(orec.name.c_str());
                    if (ti != -1)
                        orec.widgetIdx =
                            Object_AddTextLabel(static_cast<i16>(orec.x),
                                                static_cast<i16>(orec.y), winSlot,
                                                orec.name.c_str());
                }
            } else if (orec.type == kFormObjInput) {     // 65 'A'
                // Input_AddFieldToWindow(x>>16, y>>16, BYTE2(aux), BYTE1(aux),
                //                        aux_low_byte, win)  (0x41c76f).
                int step  = (orec.aux >> 16) & 0xFF;     // BYTE2
                int value = (orec.aux >> 8) & 0xFF;      // BYTE1
                u8  fl    = static_cast<u8>(orec.aux & 0xFF); // *(BYTE*)(v40+3472)
                orec.widgetIdx = Input_AddFieldToWindow(orec.x, orec.y, step, value,
                                                        fl, winSlot);
            } else if (orec.type == kFormObjSlider) {    // 69 'E'
                // Widget_AddSliderToWindow(x>>16, y>>16, a3, range, 100,
                //   Property_Validate(name), aux_low_byte, win)  (0x41c7d5).
                int gfx   = Form_PropertyValidate(orec.name.c_str());
                u8  fl    = static_cast<u8>(orec.aux & 0xFF);     // v91 = *(BYTE*)(v40+3472)
                // range = *(BYTE*)(v113+3868) = record byte @ +3868+1*o. Guard the read
                // against the record stride (the old layout's 3796-byte record cannot
                // reach +3868, but old-layout sliders never set type 69).
                int range = (3868 + o < stride) ? rec[3868 + o] : 0;
                orec.range = range;   // expose the track length for 1:1 renderers
                orec.widgetIdx = Widget_AddSliderToWindow(
                    static_cast<i16>(orec.x), static_cast<i16>(orec.y), 0, range, 100,
                    gfx, static_cast<i16>(fl), winSlot);
            }

            wr.objects.push_back(orec);
        }

        out.windows.push_back(std::move(wr));
    }

    // Stamp the form record valid + record the new form id + copy the name. The
    // original sets dword_676BF0/BFC/C00[171*formId]=1, dword_676A60[171*formId]=formId,
    // dword_62D258=formId, and copies `name` into form +139 dwords (+556 bytes).
    g_forms[formId].valid()           = 1;      // dword_676BF0[171*id] = dw[100]
    g_forms[formId].childrenVisible() = 1;      // dword_676BFC[171*id] = dw[103]
    g_forms[formId].objectsVisible()  = 1;      // dword_676C00[171*id] = dw[104]
    g_currentFormId                   = formId; // dword_62D258 = formId
    g_forms[formId].dw[0]             = formId;  // dword_676A60[171*id] = formId
    out.ok = true;
    return out;
}

} // namespace guild::gui
