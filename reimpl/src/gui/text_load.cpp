#include "gui/text_load.h"

#include "io/vfs.h"

#include <cstring>
#include <vector>

namespace guild::gui::text {

namespace {
// Little-endian u32 read (matches VIBE_Vfs_ReadStream(&x, 4, h, 1)).
u32 ReadU32(const u8* p) {
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) |
           (static_cast<u32>(p[2]) << 16) | (static_cast<u32>(p[3]) << 24);
}

// A NUL-padded fixed-width name field -> std::string (stops at first NUL or the
// field end). byte_8D36B0 names are 80-byte (0x50) records.
std::string FieldName(const u8* p, int stride) {
    int n = 0;
    while (n < stride && p[n] != 0)
        ++n;
    return std::string(reinterpret_cast<const char*>(p), static_cast<std::size_t>(n));
}
} // namespace

// gilde.exe 0x44bb5c — VIBE_Text_BuildTextArray (the .res-binary -> TextDb half).
//
// Read sequence mirrors the writer at the tail of the original (and the reader in
// VIBE_Text_LoadTextFile):
//   u32 entryCount @+0
//   u32 baseIndex  @+4   (dword_77BF10 == slot+96)
//   u32 lastIndex  @+8   (dword_77BF14 == slot+100 == dword_62EB24-1)
//   u32 offset[entryCount]
//   u8  name[entryCount][80]
//   u8  tag[entryCount]
//   u32 blobSize
//   u8  blob[blobSize]
//   then: dword_8C36B0[baseIndex+i] = blob + offset[i]
TextResFile BuildTextArray(const u8* data, std::size_t len, TextDb& db) {
    TextResFile out;
    if (!data || len < 12)
        return out; // ok == false

    u32 entryCount = ReadU32(data + 0);
    u32 baseIndex  = ReadU32(data + 4);
    u32 lastIndex  = ReadU32(data + 8);

    // Compute the layout offsets and validate the buffer can satisfy them before
    // touching `db` (the original trusts the file; we guard so a short synthetic
    // buffer fails cleanly instead of reading past the end).
    std::size_t off = 12;
    std::size_t offTableBytes = static_cast<std::size_t>(entryCount) * 4;
    std::size_t nameBytes     = static_cast<std::size_t>(entryCount) * kResNameStride;
    std::size_t tagBytes      = static_cast<std::size_t>(entryCount);
    if (len < off + offTableBytes + nameBytes + tagBytes + 4)
        return out;

    const u8* offTable = data + off;                         off += offTableBytes;
    const u8* names    = data + off;                         off += nameBytes;
    const u8* tags     = data + off;                         off += tagBytes;
    u32 blobSize       = ReadU32(data + off);                off += 4;
    if (len < off + blobSize)
        return out;
    const u8* blob     = data + off;

    // Each string offset must land inside the blob (and the C string be NUL-
    // terminated within it). Validate up front.
    for (u32 i = 0; i < entryCount; ++i) {
        u32 so = ReadU32(offTable + 4 * i);
        if (so >= blobSize && !(so == blobSize && blobSize == 0))
            return out;
        // ensure a terminating NUL exists from `so` to blobSize-1
        bool term = false;
        for (u32 k = so; k < blobSize; ++k) {
            if (blob[k] == 0) { term = true; break; }
        }
        if (!term)
            return out;
    }

    // Place entries at [baseIndex, baseIndex+entryCount). Fill any gap before
    // baseIndex with empty placeholders so the TextDb index matches the original's
    // dword_8C36B0[baseIndex+i] slotting.
    while (db.Count() < static_cast<int>(baseIndex))
        db.Add("", "", kTagNone);

    for (u32 i = 0; i < entryCount; ++i) {
        u32 so = ReadU32(offTable + 4 * i);
        const char* str  = reinterpret_cast<const char*>(blob + so);
        std::string name = FieldName(names + static_cast<std::size_t>(i) * kResNameStride,
                                     kResNameStride);
        u8 tag = tags[i];
        db.Add(std::string(str), name, tag);
    }

    out.baseIndex  = baseIndex;
    out.lastIndex  = lastIndex;
    out.entryCount = static_cast<int>(entryCount);
    out.ok         = true;
    return out;
}

std::string Text_BuildPath(const char* lang, const char* name) {
    // VIBE_Crt_Sprintf_0(buf, "textbin_%s\\%s.res", lang, name)
    std::string p = "textbin_";
    p += (lang ? lang : "german");
    p += "\\";
    p += (name ? name : "");
    p += ".res";
    return p;
}

namespace {
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

// gilde.exe 0x44dba0 — VIBE_Text_LoadTextFile (open-by-name through the VFS).
bool Text_LoadTextFile(const char* name, TextDb& db, const char* lang) {
    std::string path = Text_BuildPath(lang, name);

    guild::io::VfsHandle* h = guild::io::VfsOpenFile(path.c_str(), "rb");
    if (!h)
        return false; // original: "Could not open textfile:%s"

    std::vector<u8> buf;
    SlurpStream(h, buf);
    guild::io::VfsCloseStream(h);

    return BuildTextArray(buf.data(), buf.size(), db).ok;
}

} // namespace guild::gui::text
