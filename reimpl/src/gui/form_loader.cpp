#include "gui/form_loader.h"
#include "gui/form.h"
#include "gui/window.h"
#include "gui/object.h"

#include "io/vfs.h"

#include <cstring>
#include <vector>

namespace guild::gui {

GfxObject::GfxObject() { std::memset(raw, 0, sizeof(raw)); }

GfxObject g_gfxObjects[kMaxGfxObjects]; // dword_62D204 "d2:fileobj"
int       g_gfxObjectCount = 0;         // dword_62D208

void ResetGfxObjects() {
    for (auto& o : g_gfxObjects) o = GfxObject{};
    g_gfxObjectCount = 0;
}

// ---- Forward-declared renderer edge (default no-op; tests override) --------
// VIBE_State_Helper @0x40e014 registers a renderer scene-state for a gfx object.
// Marked weak so a test TU can supply a counting definition.
void __attribute__((weak)) RegisterGfxState(int /*gfxIndex*/) {}

// gilde.exe 0x41b888 — VIBE_Gui_LoadGfxFile (table-baseline init half).
//
// Original (3 loops):
//   for (i=0;i<48;++i)   { *(dword*)((char*)dword_676A60 + 684*i) = i; }
//   for (i=0;i<96;++i)   {  dword_67EB80[238*i]                   = i; }
//   for (i=0;i<512;++i)  { *(dword*)(740*i + dword_69FFB4)        = i; }
void Form_InitTables() {
    for (int i = 0; i < kFormInitCount; ++i)
        g_forms[i].dw[0] = i;                 // form +0 = formIndex (684-byte stride)
    for (int i = 0; i < kWindowInitCount; ++i)
        g_windows[i].at<i32>(0) = i;          // window +0 = windowIndex (238-dword stride)
    for (int i = 0; i < kWidgetInitCount; ++i)
        g_widgets[i].marker() = i;            // widget +0 = widgetIndex (740-byte stride)
}

// gilde.exe 0x41b888 — VIBE_Gui_LoadGfxFile (file-parse half).
//
// Original read sequence:
//   VIBE_File_Read(stream, &dword_62D208, 4, 1);           // u32 objectCount
//   if (dword_62D208 > 2048) { close; report "Too many"; return 0; }
//   for (i=0; i<dword_62D208; ++i)
//       VIBE_File_Read(stream, 84*i + dword_62D204, 0x54, 1);
//   close;
//   for (i=0; i<dword_62D208; ++i)
//       if ((rec[i].+68 & 1) && rec[i].+56) VIBE_State_Helper(i, 0);
bool Form_LoadFromBuffer(const u8* data, std::size_t len, bool initTables) {
    if (initTables)
        Form_InitTables();

    // get u32 objectCount @+0 (little-endian, matching VIBE_File_Read of 4 bytes).
    if (len < 4)
        return false;
    u32 count = static_cast<u32>(data[0]) | (static_cast<u32>(data[1]) << 8) |
                (static_cast<u32>(data[2]) << 16) | (static_cast<u32>(data[3]) << 24);

    // if ( dword_62D208 > 2048 ) -> "d2_Open:Too many Objects in GFX-File..."
    if (count > static_cast<u32>(kMaxGfxObjects)) {
        // original: VIBE_ErrorLog_ReportMessage(aD2OpenTooManyO)
        return false;
    }

    // Guard the buffer can satisfy the declared record count (the original trusts
    // the file; here a short buffer would otherwise read past the end).
    if (len < 4u + static_cast<std::size_t>(count) * kGfxObjStrideBytes)
        return false;

    // Read `count` 84-byte records into g_gfxObjects (the d2:fileobj table).
    const u8* p = data + 4;
    for (u32 i = 0; i < count; ++i) {
        std::memcpy(g_gfxObjects[i].raw, p, kGfxObjStrideBytes); // 84*i + dword_62D204
        p += kGfxObjStrideBytes;
    }
    g_gfxObjectCount = static_cast<int>(count);

    // Walk records: register a scene-state for any drawable source object.
    for (int i = 0; i < g_gfxObjectCount; ++i) {
        GfxObject& o = g_gfxObjects[i];
        if ((o.flags() & kGfxFlagHasState) != 0 && o.sceneHandle() != 0)
            RegisterGfxState(i); // VIBE_State_Helper(i, 0)
    }
    return true;
}

// gilde.exe 0x41b888 — VIBE_Gui_LoadGfxFile (the open+read flow, via the VFS).
bool Form_LoadFromFile(const char* path) {
    guild::io::VfsHandle* h = guild::io::VfsOpenFile(path, "rb");
    if (!h)
        return false;

    // Read the whole stream into a growable buffer (the original reads field-by-
    // field; here we slurp then hand to the byte-buffer parser, which is the same
    // sequence of bytes). Read in chunks until the stream is exhausted.
    std::vector<u8> buf;
    u8 chunk[4096];
    for (;;) {
        guild::u32 got = guild::io::VfsReadStream(chunk, 1, h, sizeof(chunk));
        if (got == 0 || got == 0xFFFFFFFFu)
            break;
        buf.insert(buf.end(), chunk, chunk + got);
        if (got < sizeof(chunk))
            break;
    }
    guild::io::VfsCloseStream(h);

    return Form_LoadFromBuffer(buf.data(), buf.size(), /*initTables=*/true);
}

} // namespace guild::gui
