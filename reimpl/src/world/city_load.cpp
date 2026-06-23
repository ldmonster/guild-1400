#include "world/city_load.h"

#include <cstring>
#include <cstdio>

#include "world/city.h"     // IniParse / IniGet / IniFree / CityLoadFromIni (reused)
#include "io/vfs.h"

namespace guild::world {

// gilde.exe word_641DB0 — runtime district-coord table (zero until city data loads).
DistrictCoord g_districtCoords[kDistrictCount] = {};

// gilde.exe 0x5783c4 — VIBE_City_GetDistrictCoord.
//   cmp al, 48h ; jl  -> if ( (signed char)a1 >= 72 ) return 0;
//   movsx esi, al ; lea esi, word_641DB0[esi*8]  (8-byte/4-word stride)
//   *a2 = v3[0..1]; a2[1] = v3[2..3];  return 1;
// 1:1 NOTE: `a1` is a SIGNED char and the only bound is the signed `>= 72` test —
// the original has NO lower bound, so a negative id would index word_641DB0 out of
// bounds (UB). District ids are always 0..71 in practice, so we replicate the exact
// signed upper-bound branch and add a low-side guard (the only divergence is the
// unreachable negative-id OOB the original would perform).
int CityGetDistrictCoord(int district, i32* out) {
    signed char id = static_cast<signed char>(district);
    if (id >= static_cast<signed char>(kDistrictCount))   // cmp al,48h ; jl
        return 0;
    if (id < 0)                                            // guard: orig OOBs here
        return 0;
    out[0] = g_districtCoords[id].a;
    out[1] = g_districtCoords[id].b;
    return 1;
}

// ---------------------------------------------------------------------------
// City definition loader (VFS-backed).
//
// The original (0x507144) opens "<projectGfx>/gamedata/cities/<name>.ini" and
// reads each key with GetPrivateProfileStringA, writing fields into
// byte_13CD6A0[756*slot]. We reproduce the path construction and the field
// placement (via CityLoadFromIni), and the slot==0 NachbarStadt recursion.
// ---------------------------------------------------------------------------
constexpr int kMaxNeighbours = 8;   // original caps the NachbarStadt list at 8

int CityLoadDefinitionIni(ICityFileSource& src, const char* name, int slot) {
    if (static_cast<unsigned>(slot) >= static_cast<unsigned>(kCityMaxCount))
        return 0;

    // Path: "%s/%s%s.ini" with prefix "gamedata/cities" (the \project\gfx\ root is
    // a VFS mount detail; the meaningful relative path is gamedata/cities/<name>).
    char path[512];
    std::snprintf(path, sizeof(path), "%s/%s.ini", "gamedata/cities", name);

    char* text = src.Load(path);
    if (!text)
        return 0;

    IniDocument* doc = IniParse(text);
    src.Free(text);

    // Map every field into g_cities[slot] (byte placement owned by city.cpp).
    CityLoadFromIni(doc, slot);

    // slot==0 (the home city): read the NachbarStadt neighbour list and recurse
    // into slots 1..8, exactly like the original's tail recursion.
    if (slot == 0) {
        const char* list = IniGet(doc, "A - ALLGEMEIN", "NachbarStadt", "");
        char names[kMaxNeighbours][64];
        int n = 0;
        const char* p = list;
        while (*p && n < kMaxNeighbours) {
            // skip leading whitespace (the original strtok delimiter set)
            while (*p == ' ' || *p == '\t' || *p == ',' ||
                   *p == '\r' || *p == '\n')
                ++p;
            if (!*p)
                break;
            int k = 0;
            while (*p && *p != ' ' && *p != '\t' && *p != ',' &&
                   *p != '\r' && *p != '\n') {
                if (k < 63)
                    names[n][k++] = *p;
                ++p;
            }
            names[n][k] = '\0';
            ++n;
        }
        IniFree(doc);
        for (int i = 0; i < n; ++i)
            CityLoadDefinitionIni(src, names[i], i + 1);
        return 1;
    }

    IniFree(doc);
    return 1;
}

// ---------------------------------------------------------------------------
// VFS binding of the file source.
// ---------------------------------------------------------------------------
namespace {
struct VfsCityFileSource : ICityFileSource {
    char* Load(const char* path) override {
        guild::io::VfsHandle* h = guild::io::VfsOpenFile(path, "rb");
        if (!h)
            return nullptr;
        // Read to end: the .ini files are small; pull in chunks until EOF.
        long pos = guild::io::VfsSeek(h, 0, 2 /*SEEK_END*/) == 0
                       ? guild::io::VfsTell(h) : -1;
        guild::u32 size;
        if (pos > 0) {
            guild::io::VfsSeek(h, 0, 0 /*SEEK_SET*/);
            size = static_cast<guild::u32>(pos);
        } else {
            size = 0;   // unknown length -> fall back to chunked read below
        }
        char* buf = nullptr;
        if (size > 0) {
            buf = new char[size + 1];
            guild::u32 got = guild::io::VfsReadStream(buf, 1, h, size);
            if (got == 0xFFFFFFFFu)
                got = 0;
            buf[got] = '\0';
        } else {
            // Chunked read for streams without a seekable length.
            guild::u32 cap = 4096, len = 0;
            buf = new char[cap];
            for (;;) {
                if (len + 1024 + 1 > cap) {
                    guild::u32 ncap = cap * 2;
                    char* nb = new char[ncap];
                    std::memcpy(nb, buf, len);
                    delete[] buf;
                    buf = nb;
                    cap = ncap;
                }
                guild::u32 got = guild::io::VfsReadStream(buf + len, 1, h, 1024);
                if (got == 0xFFFFFFFFu || got == 0)
                    break;
                len += got;
                if (got < 1024)
                    break;
            }
            buf[len] = '\0';
        }
        guild::io::VfsCloseStream(h);
        return buf;
    }
    void Free(char* buf) override { delete[] buf; }
};
} // namespace

int CityLoadDefinitionIniFromVfs(const char* name, int slot) {
    VfsCityFileSource src;
    return CityLoadDefinitionIni(src, name, slot);
}

} // namespace guild::world
