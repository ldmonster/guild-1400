#include "gui/text/textfile_table.h"

#include "io/vfs.h"

#include <cstring>

namespace guild::gui::text {

namespace {
// VIBE_Util_StrCmpNoCase @0x5cb8f0 — ASCII A-Z case-insensitive compare; returns
// 0 on equal. (Internal copy; the original is shared, but it is a trivial leaf
// and keeping a file-local static avoids a cross-module link dependency.)
int StrCmpNoCase(const char* a, const char* b) {
    for (;;) {
        unsigned char va = static_cast<unsigned char>(*a);
        unsigned char vb = static_cast<unsigned char>(*b);
        if (va >= 0x41u && va <= 0x5Au)
            va += 32;
        if (vb >= 0x41u && vb <= 0x5Au)
            vb += 32;
        if (va != vb || !vb)
            return static_cast<int>(va) - static_cast<int>(vb);
        ++a;
        ++b;
    }
}

// Little-endian u32 read/write (matches VIBE_Vfs_ReadStream/WriteStream of a u32).
u32 ReadU32(const u8* p) {
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) |
           (static_cast<u32>(p[2]) << 16) | (static_cast<u32>(p[3]) << 24);
}
void PushU32(std::vector<u8>& v, u32 x) {
    v.push_back(static_cast<u8>(x & 0xFF));
    v.push_back(static_cast<u8>((x >> 8) & 0xFF));
    v.push_back(static_cast<u8>((x >> 16) & 0xFF));
    v.push_back(static_cast<u8>((x >> 24) & 0xFF));
}
} // namespace

// gilde.exe 0x44d8f8 — VIBE_Text_FindTextFileSlot
//   v5 = byte_77BEB0; v6 = 0;
//   while (1) {
//     v7 = 112 * v6;
//     if (!StrCmpNoCase(v5, name) && byte_77BEB0[v7]) break;   // match + live
//     ++v6; v5 += 112;
//     if (v6 >= 128) return -1;
//   }
//   return v6;
int TextFileTable::FindSlot(const char* name) const {
    for (int i = 0; i < Count(); ++i) {
        const TextFileSlot& s = slots_[i];
        if (s.used && StrCmpNoCase(s.name.c_str(), name) == 0)
            return i;
    }
    return -1;
}

int TextFileTable::Acquire(const char* name) {
    int slot = FindSlot(name);
    if (slot < 0) {
        for (int i = 0; i < Count(); ++i) {
            if (!slots_[i].used) {
                slot = i;
                break;
            }
        }
    }
    if (slot < 0)
        return -1;
    slots_[slot].name = name ? name : "";
    slots_[slot].used = true;
    return slot;
}

// gilde.exe 0x44d940 — VIBE_Text_FreeTextFile
//   result = FindTextFileSlot(name);
//   if (result != -1) {
//       FreeDebug(dword_77BF18[28*result]);   // free the blob
//       dword_77BF18[28*result] = 0;          // null the pointer
//   }
//   return result;
int TextFileTable::FreeTextFile(const char* name) {
    int slot = FindSlot(name);
    if (slot != -1) {
        slots_[slot].blob.clear();
        slots_[slot].blob.shrink_to_fit();
    }
    return slot;
}

// gilde.exe 0x44d8a4 — VIBE_Text_FreeAllTextFiles
//   for (i = 0; i != 14336; i += 112) {           // 128 records * 112 bytes
//       if (!dword_77BF18[i]) continue;           // skip slots with no blob
//       FreeDebug(dword_77BF18[i]);               // free blob
//       dword_77BF18[i] = 0;                      // null pointer
//       VIBE_Light_SetGrayColorThunk(0,112,&byte_77BEB0[i]);  // zero the record
//   }
void TextFileTable::FreeAllTextFiles() {
    // The original keys SOLELY on the blob pointer (dword_77BF18[i] != 0): a slot
    // whose blob pointer is null is skipped untouched (its name/indices survive);
    // only slots that own a blob are freed and have their whole 112-byte record
    // zeroed. The `used`/name flag is NOT consulted (DISASM @0x44d8b4: the loop
    // continues while `!*(int*)((char*)dword_77BF18 + i)`).
    for (int i = 0; i < Count(); ++i) {
        if (slots_[i].blob.empty())
            continue;
        slots_[i] = TextFileSlot();  // free blob + zero the 112-byte record
    }
}

// gilde.exe 0x44add4 — VIBE_Text_LookupLabelEntry
//   v5 = &unk_77F6B0; v6 = 0;
//   while (v6 < 0x3FFF) {
//       if (!StrCmpNoCase(v5, name)) return dword_76BEB0[v6];   // match -> value
//       v5 += 80; ++v6;
//   }
//   return -1;
void LabelTable::Add(const std::string& name, i32 value) {
    names_.push_back(name);
    values_.push_back(value);
}

i32 LabelTable::Lookup(const char* name) const {
    int n = Count();
    if (n > kLabelMaxEntries)
        n = kLabelMaxEntries;
    for (int i = 0; i < n; ++i) {
        if (StrCmpNoCase(names_[i].c_str(), name) == 0)
            return values_[i];
    }
    return -1;
}

// gilde.exe 0x44d970 — VIBE_Text_ReloadTextFile.
//
// Original flow (slot record fields in []):
//   slot = FindTextFileSlot(name); if (slot == -1) return 0;
//   sprintf(path, "textbin_%s\\%s.res", "german", name);
//   h = VfsOpenFile(path, ...); if (!h) return 0;
//   VfsSeek(h, 0, 0);
//   read entryCount; read [+96]=baseIndex; read [+100]=lastIndex;
//   offTable = alloc(4*entryCount); read offTable;
//   for i in [0,entryCount): read 80 bytes into byte_8D36B0[80*(baseIndex+i)];
//   for i in [0,entryCount): read 1 byte  into byte_767EB0[baseIndex+i];
//   read blobSize; blob = alloc(blobSize); read blob; [+104]=blob;
//   for i in [0,entryCount): dword_8C36B0[baseIndex+i] = blob + offTable[i];
//   close; free offTable; return 1;
bool ReloadTextFile(TextFileTable& table, TextDb& db, const char* name,
                    const char* lang) {
    int slot = table.FindSlot(name);
    if (slot == -1)
        return false;

    // sprintf((int)v22, "textbin_%s\\%s.res", aGerman, name)
    std::string path = "textbin_";
    path += (lang ? lang : "german");
    path += "\\";
    path += (name ? name : "");
    path += ".res";

    guild::io::VfsHandle* h = guild::io::VfsOpenFile(path.c_str(), "rb");
    if (!h)
        return false;

    guild::io::VfsSeek(h, 0, 0);

    auto rd = [&](void* dst, u32 size) -> bool {
        u32 got = guild::io::VfsReadStream(dst, size, h, 1);
        return got != 0xFFFFFFFFu && got != 0;
    };
    auto rdU32 = [&](u32& out) -> bool {
        u8 b[4];
        if (!rd(b, 4))
            return false;
        out = ReadU32(b);
        return true;
    };

    u32 entryCount = 0, baseIndex = 0, lastIndex = 0;
    bool ok = rdU32(entryCount) && rdU32(baseIndex) && rdU32(lastIndex);

    TextFileSlot& s = table.At(slot);
    s.baseIndex = baseIndex;
    s.lastIndex = lastIndex;

    std::vector<u32> offsets(entryCount);
    for (u32 i = 0; ok && i < entryCount; ++i)
        ok = rdU32(offsets[i]);

    std::vector<std::string> names(entryCount);
    for (u32 i = 0; ok && i < entryCount; ++i) {
        u8 rec[kNameStride];
        ok = rd(rec, kNameStride);
        if (ok) {
            int len = 0;
            while (len < kNameStride && rec[len] != 0)
                ++len;
            names[i].assign(reinterpret_cast<char*>(rec), static_cast<std::size_t>(len));
        }
    }

    std::vector<u8> tags(entryCount, kTagNone);
    for (u32 i = 0; ok && i < entryCount; ++i) {
        u8 t = kTagNone;
        ok = rd(&t, 1);
        tags[i] = t;
    }

    u32 blobSize = 0;
    ok = ok && rdU32(blobSize);
    std::vector<u8> blob(blobSize);
    if (ok && blobSize)
        ok = rd(blob.data(), blobSize);

    guild::io::VfsCloseStream(h);
    if (!ok)
        return false;

    s.blob = blob;

    // Rebuild the TextDb entries at [baseIndex, baseIndex+entryCount). Fill any
    // gap before baseIndex so an entry's index equals baseIndex+i, exactly as the
    // original places dword_8C36B0[baseIndex+i] = blob + offsets[i].
    while (db.Count() < static_cast<int>(baseIndex))
        db.Add("", "", kTagNone);

    for (u32 i = 0; i < entryCount; ++i) {
        u32 so = offsets[i];
        const char* str = (so < blob.size())
                              ? reinterpret_cast<const char*>(s.blob.data() + so)
                              : "";
        db.Add(std::string(str), names[i], tags[i]);
    }
    return true;
}

// gilde.exe 0x44de8c — VIBE_Text_SaveTextFile.
//
// Original flow:
//   sprintf(path, "%s\\german\\textbin_%s\\%s.res", root, "german", name);
//   h = OpenStream(path, "wb"); if (!h) { MessageBox("Could not write..."); return 0; }
//   Seek(0,0);
//   i = lastIndex - baseIndex + 1;  write i;                 // entry count
//   write [+96]=baseIndex;  write [+100]=lastIndex;
//   for i in [base..last]: write (dword_8C36B0[i] - blobPtr);  // offset table
//   for i in [base..last]: write byte_8D36B0[80*i], 80;        // name records
//   for i in [base..last]: write byte_767EB0[i], 1;            // tag bytes
//   write [+108]=blobSize;  write blobPtr, blobSize;           // blob
//   close; return 1;
bool SaveTextFile(const TextFileTable& table, const TextDb& db, int slot,
                  const char* root, const char* lang) {
    if (slot < 0 || slot >= table.Count())
        return false;
    const TextFileSlot& s = table.At(slot);

    std::string path = (root ? root : "");
    path += "\\german\\textbin_";
    path += (lang ? lang : "german");
    path += "\\";
    path += s.name;
    path += ".res";

    guild::io::VfsHandle* h = guild::io::VfsOpenFile(path.c_str(), "wb");
    if (!h)
        return false;  // original: MessageBox("Could not write ResourceFile!")

    guild::io::VfsSeek(h, 0, 0);

    u32 base = s.baseIndex;
    u32 last = s.lastIndex;
    u32 entryCount = last - base + 1;

    // Rebuild the packed blob + offset table from the db entries so the written
    // offsets are self-consistent (the original reuses the slot's owned blob and
    // the live dword_8C36B0 pointers; here we re-pack equivalently).
    std::vector<u8> blob;
    std::vector<u32> offsets;
    offsets.reserve(entryCount);
    for (u32 i = 0; i < entryCount; ++i) {
        offsets.push_back(static_cast<u32>(blob.size()));
        const char* str = db.Text(static_cast<int>(base + i));
        if (!str)
            str = "";
        const u8* p = reinterpret_cast<const u8*>(str);
        std::size_t len = std::strlen(str);
        blob.insert(blob.end(), p, p + len);
        blob.push_back(0);  // NUL terminator (entries are NUL-terminated)
    }

    std::vector<u8> buf;
    PushU32(buf, entryCount);
    PushU32(buf, base);
    PushU32(buf, last);
    for (u32 i = 0; i < entryCount; ++i)
        PushU32(buf, offsets[i]);
    for (u32 i = 0; i < entryCount; ++i) {
        u8 rec[kNameStride];
        std::memset(rec, 0, sizeof(rec));
        const char* nm = db.Name(static_cast<int>(base + i));
        if (nm) {
            std::size_t len = std::strlen(nm);
            if (len > kNameStride)
                len = kNameStride;
            std::memcpy(rec, nm, len);
        }
        buf.insert(buf.end(), rec, rec + kNameStride);
    }
    for (u32 i = 0; i < entryCount; ++i)
        buf.push_back(db.Tag(static_cast<int>(base + i)));
    PushU32(buf, static_cast<u32>(blob.size()));
    buf.insert(buf.end(), blob.begin(), blob.end());

    u32 wrote = guild::io::VfsWriteStream(buf.data(), 1, h, static_cast<u32>(buf.size()));
    guild::io::VfsCloseStream(h);
    return wrote != 0xFFFFFFFFu;
}

} // namespace guild::gui::text
