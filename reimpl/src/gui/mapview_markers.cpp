#include "gui/mapview_markers.h"

#include <cctype>
#include <cstring>

namespace guild::gui {

// ---------------------------------------------------------------------------
// Office-marker collection — gilde.exe 0x543870.
//   v8 = 24 * count ;  v14 = 4 * a1 (output byte cap) ;  v7 = output byte offset
//   while (v7 < v14): id = FindRecordById(scratch[v6+ +4]); if (id) { out[v7]=id;
//     v7 += 4; ++v5 } ; v6 += 24 ; if (v6 >= v8) return v5
// The output advances only on a resolved id, but the loop ALSO terminates when the
// source is exhausted (v6 >= v8) — so the number stored is min(resolved-count, a1).
// ---------------------------------------------------------------------------
int MapView_CollectOfficeMarkers(const std::vector<int>& resolved, int maxOut,
                                 std::vector<int>& out) {
    out.clear();
    int v5 = 0;                 // stored count
    int outCapBytes = 4 * maxOut; // v14
    int outBytes = 0;           // v7 (only advances on a hit)
    int count = (int)resolved.size();
    int v8 = kOfficeRecordStride * count;
    int v6 = 0;                 // source byte offset

    while (outBytes < outCapBytes) {
        int idx = v6 / kOfficeRecordStride;
        int handle = (idx < count) ? resolved[idx] : 0; // FindRecordById(...)
        if (handle) {
            out.push_back(handle);
            ++v5;
            outBytes += 4;
        }
        v6 += kOfficeRecordStride;
        if (v6 >= v8)
            return v5;
    }
    return v5;
}

// ---------------------------------------------------------------------------
// City-point marker name prep — gilde.exe 0x519448.
//   copy name (2 bytes/iter) into v15 ; StrToUpper(v15) ; sprintf(v16,"dummy_%s",v15)
// ---------------------------------------------------------------------------
CityMarkerName CityMap_BuildMarkerName(const std::string& cityName) {
    CityMarkerName out;
    out.upper = cityName;
    for (char& ch : out.upper)
        ch = (char)std::toupper((unsigned char)ch);
    out.key = std::string(kCityMarkerFmt);
    // Replace "%s" with the upper-cased name (the original sprintf).
    auto pos = out.key.find("%s");
    if (pos != std::string::npos)
        out.key.replace(pos, 2, out.upper);
    return out;
}

MarkerRecord::MarkerRecord() { std::memset(raw, 0, sizeof(raw)); }

// ---------------------------------------------------------------------------
// Render-flag byte writes — gilde.exe 0x519448 (after the object resolves).
//   v11 = rec[+529] ; v12 = rec[+530] ; rec[+535] = 4 ; v12 &= 0xF3 ;
//   rec[+536] = 1 ; rec[+530] = v12 ; rec[+529] = v11 | 4 ; rec[+530] = v12 | 4
// ---------------------------------------------------------------------------
void CityMap_ApplyMarkerFlags(MarkerRecord& rec) {
    u8 v11 = rec.at<u8>(529);
    u8 v12 = rec.at<u8>(530);
    rec.at<u8>(535)  = kCityMarkerKind;             // +535 = 4
    v12 &= kCityMarkerFlag530Mask;                  // &= 0xF3
    rec.at<i32>(536) = kCityMarkerEnabled;          // +536 = 1
    rec.at<u8>(530)  = v12;                          // intermediate write
    rec.at<u8>(529)  = (u8)(v11 | kCityMarkerFlag529Set); // |= 4
    rec.at<u8>(530)  = (u8)(v12 | kCityMarkerFlag530Set); // |= 4
}

// ---------------------------------------------------------------------------
// Whole flow — gilde.exe 0x519448.
// ---------------------------------------------------------------------------
namespace {
CityMarkerSink g_defaultMarkerSink;
CityMarkerSink* g_markerSink = &g_defaultMarkerSink;
} // namespace

void CityMap_SetMarkerSink(CityMarkerSink* sink) {
    g_markerSink = sink ? sink : &g_defaultMarkerSink;
}

bool CityMap_CreateCityPointMarker(const std::string& cityName, bool resolved,
                                   MarkerRecord& rec) {
    (void)CityMap_BuildMarkerName(cityName); // name prep (the lookup key)
    if (!resolved)
        return false; // FindByHandle returned 0 -> early return

    g_markerSink->ProjectThroughBoneChain(); // VIBE_Transform_PointThroughBoneChain @0x5194d4
    g_markerSink->AttachToUniverse();         // VIBE_Object_AttachToUniverseNode @0x5194e7
    CityMap_ApplyMarkerFlags(rec);            // flag writes @0x5194f3..0x519529
    g_markerSink->BuildLightCache();          // VIBE_Light_BuildObjectCache @0x51952f
    g_markerSink->LoadAnimation(kCityMarkerAnim); // VIBE_Character_LoadObjectAnimation @0x51953b
    return true;
}

} // namespace guild::gui
