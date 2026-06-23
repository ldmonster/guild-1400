#pragma once
// guild::gui — map-view marker collection + city-point marker build (DATA half).
//
//   * VIBE_MapView_CollectOfficeMarkers @0x543870 — collect up to `a1` office records
//       of a category into a caller-supplied pointer array, skipping ids that don't
//       resolve to a person record.  Recovered byte-for-byte (the 24-byte source
//       stride, the 4*a1 output cap).
//   * VIBE_CityMap_CreateCityPointMarker @0x519448 — build a "STADTPUNKT" marker
//       object for a city name: upper-case the name, format "dummy_%s", look up the
//       object, project it through its bone chain, attach to the universe node, and set
//       the marker render flags.  The bone-chain projection + universe attach + the
//       animation load are render/world cluster work; we recover the NAME PREP + the
//       render-flag byte writes and route the 3D edges through a mock sink.

#include "gui/types.h"
#include <string>
#include <vector>

namespace guild::gui {

// ===========================================================================
// Office-marker collection — gilde.exe 0x543870.
// The original calls VIBE_Office_CollectByCategory(category, 6, &unk_1233448) which
// fills a scratch table of 24-byte records (the office id at +4, i.e. dword_123344C
// stepping by 24).  It then resolves each id via VIBE_Person_FindRecordById and stores
// the non-null record pointers into the caller's array `a3`, capped at `a1` entries.
// We model the scratch table as the input ids; `resolve` maps an id to a record handle
// (0 = unresolved), defaulting to identity-nonzero.
// ===========================================================================
inline constexpr int kOfficeRecordStride = 24; // bytes per source record (v6 += 24)

// gilde.exe 0x543870.  `ids` are the office ids from the scratch table (in order);
// `maxOut` is `a1` (the output capacity).  For each id, if `resolved[i]` is non-zero it
// is appended to the output (up to maxOut entries).  Returns the number stored (v5).
// `out` receives the resolved handles in order.
int MapView_CollectOfficeMarkers(const std::vector<int>& resolved, int maxOut,
                                 std::vector<int>& out);

// ===========================================================================
// City-point marker build — gilde.exe 0x519448.
// ===========================================================================
// Render-flag byte writes applied to the attached marker object (offsets into the
// 536-byte scene record):
//   +535 = 4            (marker kind)
//   +536 = 1            (enabled dword)
//   +529 |= 4           (render flag bit)
//   +530  = (orig & 0xF3) | 4   (clear bits 2..3, then set bit 2)
inline constexpr int kCityMarkerKind        = 4; // +535
inline constexpr int kCityMarkerEnabled     = 1; // +536
inline constexpr u8  kCityMarkerFlag529Set  = 0x04;
inline constexpr u8  kCityMarkerFlag530Mask = 0xF3;
inline constexpr u8  kCityMarkerFlag530Set  = 0x04;
inline constexpr const char* kCityMarkerAnim = "sonstiges\\STADTPUNKT_ANM_K.baf";
inline constexpr const char* kCityMarkerFmt  = "dummy_%s";

// Result of the name-prep step: the upper-cased name + the formatted lookup key.
struct CityMarkerName {
    std::string upper; // VIBE_Util_StrToUpper(copy of name)
    std::string key;   // sprintf("dummy_%s", upper)
};

// gilde.exe 0x519448 (the copy/upper/sprintf prologue).  Build the lookup name for a
// city marker from the raw city name.  The original copies the name 2 bytes at a time
// into a stack buffer, upper-cases it, then formats "dummy_%s".
CityMarkerName CityMap_BuildMarkerName(const std::string& cityName);

// The render-flag byte writes (the block after the object resolves).  Applied to a
// 537+-byte scene record blob.  Returns true if applied (record non-null).
struct MarkerRecord {
    u8 raw[544];
    MarkerRecord();
    template <typename T> T& at(int off) { return *reinterpret_cast<T*>(raw + off); }
};
void CityMap_ApplyMarkerFlags(MarkerRecord& rec);

// Mock sink for the 3D edges (bone-chain projection, universe attach, light cache
// rebuild, anim load) — in original call ORDER: ProjectThroughBoneChain (0x5194d4)
// -> AttachToUniverse (0x5194e7) -> [flag writes] -> BuildLightCache (0x51952f) ->
// LoadAnimation (0x51953b).
struct CityMarkerSink {
    virtual ~CityMarkerSink() = default;
    virtual void ProjectThroughBoneChain() {}
    virtual void AttachToUniverse() {}
    virtual void BuildLightCache() {} // VIBE_Light_BuildObjectCache @0x5c8218
    virtual void LoadAnimation(const char* /*baf*/) {}
};
void CityMap_SetMarkerSink(CityMarkerSink* sink);

// gilde.exe 0x519448 (whole flow).  Build a city-point marker for `cityName`: prep the
// name, (mock) resolve+project+attach via the sink, apply the render flags to `rec`,
// and (mock) load the marker animation.  `resolved` indicates the object lookup
// succeeded; when false the original returns early without touching the record.
// Returns true when the marker was built.
bool CityMap_CreateCityPointMarker(const std::string& cityName, bool resolved,
                                   MarkerRecord& rec);

} // namespace guild::gui
