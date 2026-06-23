#pragma once
// =============================================================================
// guild::render — TEXTURE SET TABLE (.TXS sidecar) + seasonal set selection.
//
// THE MECHANISM (VERIFIED against the binary, wave-4 verification pass):
//   0x58339c  VIBE_GameTime_GetSeasonFromDay  — season = day % 4 (signed idiv
//                 over the game-clock day dword at 0x13CE852; a new game
//                 starts at day 0 -> season 0).
//   0x505df4  VIBE_Weather_ApplySeasonalMeshes — pins the season SEMANTICS:
//                 season 0 -> "%s_FRUEHLING" floor layers (spring), 1 -> the
//                 suffix-less base (summer), 2 -> "%s_HERBST" (autumn),
//                 3 -> "%s_SNOW" (winter).
//   0x506df4  VIBE_Scene_ActivateAndRefreshCharacters — byte_634484 = season
//                 (0x506f27), then TraverseTree(HideFoliageDecor). Called on
//                 session init/load (0x533a54 / 0x5a7604), the main frame
//                 loop (0x50f0c0) and the turn/day advance (0x52f8d0) — the
//                 LIVE season source is ALWAYS the clock day, never a save
//                 byte (byte_6477A1 is an AI/economy scalar, xrefs are all
//                 MeisterAi_*/CalcBankmeister).
//   0x506388  VIBE_Object_HideFoliageDecor — node names starting "pfl_" /
//                 "vg_" / "!vg_" (VIBE_Util_StrncmpN @0x5e9ee0: plain byte
//                 compare, case-SENSITIVE) get
//                 SelectTextureSet(node, mesh@+460, 1, byte_634484).
//   0x5b3f54  VIBE_Object_SelectTextureSet(name, obj, upload, set) — set ==
//                 *(obj+381) returns 1 (already active); set >= *(mesh+484)
//                 (setCount) returns 0 — NO swap, the current set stays; else
//                 per material slot m: the 64-byte name record at
//                 *(mesh+516) + (materialCount*set + m)*64 is LOADED
//                 (VIBE_Texture_LoadByName @0x5da714) and replaces the slot's
//                 texture on every poly with that material. An EMPTY name
//                 skips the slot (0x5b4099 — the CURRENT texture stays); a
//                 load failure keeps the old texture and only clears the
//                 return flag (0x5b420b, "cannot replace %s with %s").
//
// THE .TXS SIDECAR — ENGINE READER/WRITER (verified):
//   0x5d2240  VIBE_Mesh_LoadTextureSet — reads "<member>.TXS": u32 magic ==
//                 603064750 (0x23F209AE), u32 setCount, u32 namesPerSet, then
//                 setCount*namesPerSet NUL-terminated names, SET-major, into
//                 64-byte records ("d3_io:LoadtextureSet").
//   0x5d20dc  VIBE_Mesh_SaveTextureSet — the matching writer (dev tool, no
//                 live callers) — confirms the byte format field-for-field.
//   0x5d32d4  VIBE_Mesh_LoadAndRegister — loads the sidecar for EVERY mesh,
//                 then VIBE_Model_LoadFastChunk @0x5f87b8 ADOPTS it iff
//                 setCount > 0 && namesPerSet == fast-chunk materialCount
//                 (0x5f8a0e; mesh+480/+484/+516 <- the table). Rejected ->
//                 the engine synthesizes a 1-set table from the material
//                 names (0x5f8d84) and the orphan table is freed (0x5d33c0).
//   LOAD-TIME BINDING (0x5f8c14): when adopted, the initial texture of
//                 material m is LoadByName(SET-0 row m) — the .bgf material
//                 strings only decide IF a load happens (name1 preferred,
//                 |1 flag; else name0; else none) and the flag bits.
//
//   u32 magic        = 0x23F209AE
//   u32 setCount     ; number of texture sets   (== *(mesh+484))
//   u32 namesPerSet  ; names per set            (== fast-chunk materialCount)
//   char names[setCount * namesPerSet][]        ; NUL-terminated, SET-major.
//                                                 EMPTY = "keep the slot's
//                                                 current texture".
//
// CROSS-VALIDATION over the shipped archive (Resources/Objects.BIN, 590 .TXS):
//   * 589 .bgf+.TXS pairs parse; ALL consume the file exactly (0 trailing
//     bytes); magic is 0x23F209AE in all 590.
//   * namesPerSet == materialCount in 588/589 pairs (outlier: Gebaeude/
//     gb_Wohnsitz/gb_PALAZZO_B_0.bgf, 18 vs 17 — NOT adopted: it binds its
//     material names via the synthesized 1-set table; any set>0 application
//     is a no-op through the 0x5b403b gate).
//   * 349 of 3979 adopted set-0 rows differ from the .bgf material name (318
//     non-foliage) — the engine renders the ROW, not the material name.
//     0 set-0 rows are empty; 0 foliage season rows are empty where the
//     set-0 row differs from name0.
//   * Sets are a GENERAL variant mechanism: foliage = seasons (all 40
//     foliage sidecars have setCount 4); buildings = upgrade levels applied
//     CUMULATIVELY 0..level (VIBE_Object_HideFoliageByState @0x506430 — the
//     non-foliage arm loops SelectTextureSet(i) for i=0..level, which is why
//     empty rows mean "keep current"); characters = head/outfit variants
//     (VIBE_Character_ApplyHeadVariant @0x57c548); flags, scaffolds, etc.
//   * SEASONAL ORDER: across every foliage 4-set table the row suffixes are
//     invariant with ZERO contradictions: set 0 -> _F (Frühling), 1 -> _S
//     (Sommer), 2 -> _H (Herbst), 3 -> _W (Winter) — and the engine applies
//     the season INDEX DIRECTLY as the set index (0x5063e4: byte_634484 is
//     passed straight to SelectTextureSet; no mapping table).
// =============================================================================
#include "guild/common/types.h"

#include <cstddef>
#include <string>
#include <vector>

namespace guild::render {

// .TXS header magic — gilde.exe 0x5d2289 (loader compare, immediate 603064750)
// / 0x5d21a1 (writer); all 590 shipped sidecars.
constexpr u32 kTextureSetMagic = 0x23F209AEu;

// ---------------------------------------------------------------------------
// A parsed .TXS texture-set table.
// ---------------------------------------------------------------------------
struct TextureSetTable {
    bool ok = false;
    i32  setCount = 0;                // texture sets (*(mesh+484) of 0x5b3f54)
    i32  namesPerSet = 0;             // == .bgf materialCount (588/589 shipped)
    std::vector<std::string> names;   // SET-major, setCount*namesPerSet entries;
                                      // "" = no swap for that material/set.

    // Replacement texture name for material `material` in set `set`, or null
    // when out of range / empty (= keep the slot's CURRENT texture — the
    // 0x5b4099 empty-name skip).
    const std::string* NameFor(i32 set, i32 material) const {
        if (!ok || set < 0 || set >= setCount || material < 0 ||
            material >= namesPerSet)
            return nullptr;
        const std::string& n =
            names[(std::size_t)set * (std::size_t)namesPerSet + (std::size_t)material];
        return n.empty() ? nullptr : &n;
    }
};

// Parse a raw .TXS byte buffer. Fails (ok=false) on a bad magic, an
// implausible header, or a truncated name list.
bool ParseTextureSetTable(const u8* data, std::size_t size, TextureSetTable& out);

// ---------------------------------------------------------------------------
// gilde.exe 0x506388 — the HideFoliageDecor name gate: strncmp(name,"pfl_",4)
// / strncmp(name,"vg_",3) / strncmp(name,"!vg_",4), case-SENSITIVE exactly as
// the original (VIBE_Util_StrncmpN @0x5e9ee0 == plain byte compare from BYTE 0).
// The original tests the SCENE NODE name pointer at offset 0 — NO basename split.
// The resolve layer applies it to the mesh member NAME (bare archive member
// names, no path separators), so the byte-0 compare is identical for shipped
// content; all 40 foliage .TXS member names match these prefixes byte-for-byte.
// ---------------------------------------------------------------------------
bool IsFoliageMeshName(const char* name);

// ---------------------------------------------------------------------------
// gilde.exe 0x58339c — VIBE_GameTime_GetSeasonFromDay: season = day % 4.
// (Same rule as sim::SeasonFromDay / sim::GetIndexThunk — duplicated here as
// the render layer must not include sim/.) Season index == texture-set index
// (0=_F spring, 1=_S summer, 2=_H autumn, 3=_W winter; see banner). The new
// game starts at day 0 (play/newgame_apply.cpp 0x533b9d GameTime_Set day 0)
// -> season 0 -> set 0 (_F).
// ---------------------------------------------------------------------------
inline i32 SeasonTextureSetFromDay(i32 day) { return day % 4; }

} // namespace guild::render
