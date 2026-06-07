#include "gui/form_asset.h"
#include "gui/form_loader.h"   // Form_LoadFromBuffer (REUSED — defined there)

#include "io/vfs.h"

#include <vector>

namespace guild::gui {

// VIBE_Gui_LoadGfxFile path build:  VIBE_Crt_Sprintf_0(buf, "%sgfx\\%s", a3, a5)
std::string Gfx_BuildPath(const char* prefix, const char* name) {
    std::string p = prefix ? prefix : "";
    p += "gfx\\";
    p += name ? name : "";
    return p;
}

namespace {
// Slurp an open VFS read stream fully into `buf`. Mirrors the chunked-read flow
// in Form_LoadFromFile (the original reads field-by-field; the byte sequence is
// identical). VfsReadStream signature is (dst, size, handle, count).
bool SlurpStream(guild::io::VfsHandle* h, std::vector<u8>& buf) {
    u8 chunk[4096];
    for (;;) {
        u32 got = guild::io::VfsReadStream(chunk, 1, h, sizeof(chunk));
        if (got == 0 || got == 0xFFFFFFFFu)
            break;
        buf.insert(buf.end(), chunk, chunk + got);
        if (got < sizeof(chunk))
            break;
    }
    return true;
}
} // namespace

// gilde.exe 0x41b888 — VIBE_Gui_LoadGfxFile (open-by-name half).
bool Gui_LoadGfxFile(const char* name, const char* prefix) {
    std::string path = Gfx_BuildPath(prefix, name);

    // VIBE_File_OpenStream(path, "rb")  ->  VfsOpenFile (REUSED).
    guild::io::VfsHandle* h = guild::io::VfsOpenFile(path.c_str(), "rb");
    if (!h)
        return false;

    std::vector<u8> buf;
    SlurpStream(h, buf);
    guild::io::VfsCloseStream(h);

    // Hand the bytes to the existing parser (runs Form_InitTables first, exactly
    // as VIBE_Gui_LoadGfxFile inits the three tables before the read).
    return Form_LoadFromBuffer(buf.data(), buf.size(), /*initTables=*/true);
}

// "Form_LoadByName" — same VFS-open wrapper, named per the playable-path task.
bool Form_LoadByName(const char* name, const char* prefix) {
    return Gui_LoadGfxFile(name, prefix);
}

} // namespace guild::gui
