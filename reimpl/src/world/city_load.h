#pragma once
// city_load — the city-definition load path of the Guild world bootstrap
// (gilde.exe). This is the VFS-backed reconstruction of the city .ini loader plus
// the self-contained district-coordinate accessor.
//
// Translated functions:
//   VIBE_City_LoadDefinitionIni  0x507144  (load gamedata/cities/<name>.ini ->
//                                           756-byte CityRecord at g_cities[slot])
//   VIBE_City_GetDistrictCoord   0x5783c4  (district id -> world coord pair)
//
// The original loader reads keys through the Win32 GetPrivateProfileStringA INI
// API from "<\project\gfx\>/gamedata/cities/<name>.ini". Here we read the whole
// .ini text through guild::io VFS (which transparently handles loose / gzip / zip
// backings) and feed it to the field mapper already recovered in city.cpp
// (IniParse / CityLoadFromIni). The byte placement of every field is owned by
// city.cpp and reused verbatim — this module only adds the VFS file-fetch + the
// NachbarStadt neighbour recursion wrapper and the district-coord accessor.
//
// Deferred (need live world/object/person state, listed in the report):
//   * Waehrung / KONTOR Import / Export object-id resolution
//     (VIBE_World_CountActiveObjects name->type lookup) and Umland person/object
//     counts — left zero exactly as city.cpp does.
#include "guild/common/types.h"
#include "world/types.h"

namespace guild::io { struct VfsHandle; }

namespace guild::world {

// ---------------------------------------------------------------------------
// District coordinate table (gilde.exe word_641DB0 @0x641DB0): 72 districts,
// 8 bytes / district (two dwords = a packed world coord pair). Runtime-populated
// from city data in the original (zero in the cold image); modeled here as a
// settable array the accessor reads.
// ---------------------------------------------------------------------------
constexpr int kDistrictCount = 72;

GUILD_PACKED_BEGIN
struct DistrictCoord {
    i32 a;   // +0x00  word_641DB0[4*id+0..1]
    i32 b;   // +0x04  word_641DB0[4*id+2..3]
} GUILD_PACKED;
GUILD_PACKED_END
static_assert(sizeof(DistrictCoord) == 8, "DistrictCoord must be 8 bytes");

extern DistrictCoord g_districtCoords[kDistrictCount]; // word_641DB0

// gilde.exe 0x5783c4 — VIBE_City_GetDistrictCoord (__usercall, al=id, edx=out).
// Copies the two-dword coord pair for `district` into out[0..1]. Returns 1 on a
// valid district (< 72), 0 (and leaves out untouched) otherwise.
int CityGetDistrictCoord(int district, i32* out);

// ---------------------------------------------------------------------------
// City definition loader.
// ---------------------------------------------------------------------------
// Abstract source the loader pulls the raw .ini text from. The production path
// binds this to the VFS (see CityLoadDefinitionIniFromVfs); tests bind a mock.
// Returns a heap buffer (NUL-terminated) the caller frees, or null on failure.
struct ICityFileSource {
    virtual ~ICityFileSource() = default;
    // Load "<gamedata/cities>/<name>.ini" (the loader supplies the full path).
    virtual char* Load(const char* path) = 0;
    virtual void  Free(char* buf) = 0;
};

// gilde.exe 0x507144 — VIBE_City_LoadDefinitionIni (__usercall, eax=name,
//   edx=slot). Builds the path "%s/%s%s.ini" (project-gfx prefix /
//   "gamedata/cities" / name), pulls the .ini text from `src`, parses it and maps
//   every field into g_cities[slot] (via city.cpp's CityLoadFromIni). When slot==0
//   it also reads the [A - ALLGEMEIN] NachbarStadt token list and recurses to load
//   up to 8 neighbour cities into slots 1..8 (matching the original's recursion).
//   Returns 1 on success, 0 if the file could not be loaded.
int CityLoadDefinitionIni(ICityFileSource& src, const char* name, int slot);

// Production binding: read the .ini through the active guild::io VFS. The path is
// "gamedata/cities/<name>.ini" (the \project\gfx\ prefix of the original is folded
// into the VFS mount root). Returns 1 on success.
int CityLoadDefinitionIniFromVfs(const char* name, int slot);

} // namespace guild::world
