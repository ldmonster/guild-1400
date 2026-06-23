// ============================================================================
// Die Gilde 1:1 — ground-plan / city-map PURE logic (manifest below).
// See groundplan_recon.h for the design rationale (pure math vs. coupled UI).
//
//   gilde.exe cluster: VIBE_Groundplan_* (0x4ae140..0x4b0992),
//                      VIBE_CityMap_*    (0x51901c..0x519828)
//
// DONE (faithful 1:1, this file):
//   0x4af464  VIBE_Groundplan_GetBuildingState  -> Groundplan_GetBuildingState
//   0x4ae59c  VIBE_Groundplan_GetWappenLabelId  -> Groundplan_GetWappenLabelId
//                                                  + Groundplan_WappenLabelForGroup
//   0x4ae824  VIBE_Groundplan_RetZero           -> Groundplan_RetZero
//
// OMITTED (rule 8 — UI/render/form/frame-loop coupled; not pure layout math;
//          a faithful translation needs the whole widget+surface+object stack,
//          which lives behind shim/ and is out of this cluster's scope):
//   0x4ae140  VIBE_Groundplan_FadeTransition    — slider widget + surface fade frame pump
//   0x4ae678  VIBE_Groundplan_SetWidgetsVisible — form/object visibility toggles
//   0x4ae828  VIBE_Groundplan_DestroyWidgets    — widget teardown
//   0x4ae970  VIBE_Groundplan_DestroyWindow     — window/surface/hotspot teardown
//   0x4aea5c  VIBE_Groundplan_LoadBlueprintBmp  — BMP load + per-pixel scan -> widget spawn
//   0x4af038  VIBE_Groundplan_RenderBlueprint   — blueprint blit + hover pick + tooltip
//   0x4af4a8  VIBE_Groundplan_BuildInfoPanel    — full info-panel layout (widgets/anim/text)
//   0x4b0758  VIBE_Groundplan_FadeInScene       — surface fill + fade register + frame loop
//   0x51901c  VIBE_CityMap_ShowTradeInfoWindow  — trade-info form, person rows, frame pump
//   0x519550  VIBE_CityMap_RunCityPointLoop     — city-point marker loop + pick + frame pump
//   0x5196fc  VIBE_CityMap_RunMapTableLoop      — map-table contact loop + frame pump
//
// REUSED (already present, wired via GroundplanHooks at the call site):
//   0x5878b0  VIBE_Building_MapTypeToCategory  (src/sim/building*.cpp)
//   0x592a5c  VIBE_Building_MapTypeToState     (src/sim/building2.cpp)
//   0x58a4c8  VIBE_BuildingType_GroupFromCode  (src/sim/building_type.cpp)
// ============================================================================
#include "world/groundplan_recon.h"

namespace guild {
namespace world {

// gilde.exe 0x4ae824 — VIBE_Groundplan_RetZero. Verbatim.
int Groundplan_RetZero() {
    return 0;
}

// gilde.exe 0x4af464 — VIBE_Groundplan_GetBuildingState.
//
// Original (Hex-Rays, cleaned):
//   v2 = a1;
//   LOBYTE(a1) = VIBE_Building_MapTypeToCategory(*a1);
//   if (a1 == 3)      byte_6317B5 = 1;
//   else if (a1 == 5) { a1 = VIBE_Building_MapTypeToState(*v2, v4);
//                       if (a1) { byte_6317B5 = v4[0]; LOBYTE(a1) = v4[0]; } }
//   return (char)a1;
//
// Faithful: `cat` carries the running al; on cat==3 set state=1 (return stays
// cat==3); on cat==5 query state, and on a hit overwrite both the global and
// the return value with the resolved state byte. Inert when a hook is null
// (matches the original behavior when the table accessor would early-out / the
// global is unmapped — the category falls straight through).
u8 Groundplan_GetBuildingState(const u8* typeCodePtr, const GroundplanHooks& h) {
    u8 cat = h.MapTypeToCategory ? h.MapTypeToCategory(*typeCodePtr) : 0;
    if (cat == 3) {
        if (h.stateGlobal) *h.stateGlobal = 1;
    } else if (cat == 5) {
        u8 st = 0;
        int hit = h.MapTypeToState ? h.MapTypeToState(*typeCodePtr, &st) : 0;
        if (hit) {
            if (h.stateGlobal) *h.stateGlobal = st;
            cat = st;
        }
    }
    return cat;
}

// Pure group-code -> Wappen label-string-id table (inner switch of 0x4ae59c).
// v0 default == 1241.
int Groundplan_WappenLabelForGroup(u8 group) {
    if (group == 1 || group == 2 || group == 10)
        return 1241;
    if (group == 3 || group == 4)
        return 1245;
    if (group == 11 || group == 12 || group == 6)
        return 1249;
    if (group == 5 || group == 7 || group == 8 || group == 9)
        return 1253;
    return 1241;  // v0
}

// gilde.exe 0x4ae59c — VIBE_Groundplan_GetWappenLabelId.
//
// byte_12335B8 (forcedWappen) overrides; case 0 walks the plot grid.
// Grid walk (byte_12CE912, stride 536 bytes, bound 411648 == 768*536):
//   v1 = 0; v2 = 0;
//   for (i = markers[0]==6; !i; i = markers[v2]==6) {
//       if (markers[v2] == 7) break;     // terminator
//       v2 += 536; ++v1;
//       if (v2 >= 411648) break;          // grid end
//   }
// The selected slot index is v1; its type word is
// *((u32*)&unk_12CEA71 + 134*v1) and HIBYTE(word) is the code -> GroupFromCode.
int Groundplan_GetWappenLabelId(u8 forcedWappen,
                                const u8* markers,
                                int markerCount,
                                const u32* typeWords,
                                const GroundplanHooks& h) {
    const int v0 = 1241;
    switch (forcedWappen) {
        case 1: return v0;
        case 2: return 1245;
        case 3: return 1249;
        case 4: return 1253;
        case 0:
            break;            // fall through to the grid walk below
        default:
            return v0;        // byte_12335B8 != 4 (and != 1..3) -> v0
    }

    // --- case 0: plot/building grid walk (stride 536 bytes) -----------------
    // Reproduces the original byte-offset iteration exactly. `markerCount`
    // bounds the in-process span (defensive; the original relies on a marker
    // hit or the 411648-byte cap). 411648 / 536 == 768 slots.
    int v1 = 0;          // slot index
    long v2 = 0;         // byte offset into markers
    auto markerAt = [&](long off) -> u8 {
        long idx = off / 536;
        if (idx < 0 || idx >= markerCount) return 0;
        return markers[off];
    };
    bool found = (markerAt(0) == 6);
    while (!found) {
        if (markerAt(v2) == 7)        // terminator slot
            break;
        v2 += 536;
        ++v1;
        if (v2 >= 411648)             // grid end (768 slots)
            break;
        found = (markerAt(v2) == 6);  // selected slot
    }

    // HIBYTE(typeWords[134*v1]) -> group code.
    // RECONSTRUCTION GUARD: in the binary the markers (byte_12CE912) and the
    // type-word details (unk_12CEA71) are two columns of ONE 768-slot person
    // array, so a v1 that walked to the end still indexes adjacent in-bounds
    // BSS. Here the caller passes two co-sized spans of `markerCount` slots; if
    // the walk fell through without selecting a slot (v1 == markerCount, or the
    // span is shorter than the 411648-byte cap implies) the typeWords read would
    // be out of bounds. Clamp to the span and treat an unselected fall-through as
    // group code 0 (the v0-default path), matching "no plot selected".
    u32 typeWord = 0;
    if (v1 >= 0 && v1 < markerCount)
        typeWord = typeWords[134 * static_cast<long>(v1)];
    u8 code = static_cast<u8>((typeWord >> 24) & 0xFF);
    u8 group = h.GroupFromCode ? h.GroupFromCode(code) : 0;
    return Groundplan_WappenLabelForGroup(group);
}

}  // namespace world
}  // namespace guild
