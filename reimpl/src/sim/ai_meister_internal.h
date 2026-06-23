#pragma once
// ai_meister — INTERNAL shared helpers for the Calc/sub-planner body TUs.
// Not a public API; only the ai_meister_*.cpp files include it. Provides the
// raw little-endian record accessors (matching the decompile's unaligned
// byte-offset addressing) and small helpers over the global tables.
#include <cstring>

#include "guild/common/types.h"
#include "sim/ai_meister.h"
#include "sim/entity.h"

namespace guild::sim {
namespace aimei {

// --- raw little-endian field access at byte offset (records are packed) ------
inline i32 rd32(const void* b, int off) {
    i32 v; std::memcpy(&v, static_cast<const u8*>(b) + off, 4); return v;
}
inline u32 rdu32(const void* b, int off) {
    u32 v; std::memcpy(&v, static_cast<const u8*>(b) + off, 4); return v;
}
inline i16 rd16(const void* b, int off) {
    i16 v; std::memcpy(&v, static_cast<const u8*>(b) + off, 2); return v;
}
inline u16 rdu16(const void* b, int off) {
    u16 v; std::memcpy(&v, static_cast<const u8*>(b) + off, 2); return v;
}
inline u8  rd8(const void* b, int off) { return *(static_cast<const u8*>(b) + off); }
inline void wr32(void* b, int off, i32 v) { std::memcpy(static_cast<u8*>(b) + off, &v, 4); }
inline void wr16(void* b, int off, u16 v) { std::memcpy(static_cast<u8*>(b) + off, &v, 2); }
inline void wr8(void* b, int off, u8 v)  { *(static_cast<u8*>(b) + off) = v; }

// --- person array bases ------------------------------------------------------
// g_persons[i] base (word_12CE910 + 536*i).
inline u8* pr(int i) { return reinterpret_cast<u8*>(&g_persons[i]); }
// dword_12CE914[134*i] — the parallel id column.
inline i32 personId(int i) { return g_personIds[i]; }

// --- record-POINTER columns (64-bit-safe handle model) ----------------------
// Several record columns store a *pointer to another record* in the 32-bit game
// (e.g. the employer building rec ptr at Person+0x16C, the action-object ptr at
// Person+0x184, the Meister's building rec ptr at +0x364/idx91). A native 8-byte
// pointer does NOT fit the 32-bit column (and adjacent columns overlap), so — per
// the index model already used by sim/entity.h — these columns hold a 32-bit
// HANDLE that resolves to the real record base. A handle is:
//     0            == null (matches the original's 0/-1 "no pointer")
//     1 + objIdx   == g_objects[objIdx]   (a building/object record)
//     0x40000000 + personIdx == g_persons[personIdx] (a person record)
// The bridge/tests SET these columns with makeObjHandle()/makePersonHandle(); the
// AI reads them with rdptr() which resolves back to a u8* base. This keeps the
// dereferences (*(ptr+off)) byte-faithful while being width-correct on any host.
constexpr i32 kHandleNull       = 0;
constexpr i32 kHandlePersonBias = 0x40000000;
inline i32 makeObjHandle(int objIdx)       { return objIdx < 0 ? kHandleNull : (1 + objIdx); }
inline i32 makePersonHandle(int personIdx) { return personIdx < 0 ? kHandleNull
                                                    : (kHandlePersonBias + personIdx); }
inline u8* resolveHandle(i32 h) {
    if (h == kHandleNull || h == -1) return nullptr;
    if (h >= kHandlePersonBias) {
        int idx = h - kHandlePersonBias;
        if (idx < 0 || idx >= kPersonCapacity) return nullptr;
        return reinterpret_cast<u8*>(&g_persons[idx]);
    }
    int idx = h - 1;
    if (idx < 0 || idx >= kObjectCapacity) return nullptr;
    return reinterpret_cast<u8*>(&g_objects[idx]);
}
// Read the pointer column at byte offset `off` of record `b` and resolve to a base.
inline u8* rdptr(const void* b, int off) { return resolveHandle(rd32(b, off)); }
// Write a handle into a pointer column.
inline void wrptr(void* b, int off, i32 handle) { wr32(b, off, handle); }

// Person-record column offsets (symbol - 0x12CE910) used by the AI loops:
constexpr int kP_marker     = 0x00;   // word_12CE910  (-1 == free)
constexpr int kP_kind       = 0x02;   // byte_12CE912
constexpr int kP_id         = 0x04;   // dword (record copy of id)
constexpr int kP_isLive     = 0x08;   // byte_12CE918
constexpr int kP_owner39    = 0x27;   // (+39) owner/player word
constexpr int kP_unk162     = 0x162;  // unk_12CEA72 (high byte == prof type)
constexpr int kP_profByte   = 0x165;  // byte_12CEA75 (assigned/profession byte)
constexpr int kP_employer   = 0x16C;  // dword_12CEA7C (employer building rec ptr)
constexpr int kP_field174   = 0x174;  // dword_12CEA84
constexpr int kP_container  = 0x178;  // dword_12CEA88 (scene/container id)
constexpr int kP_busy       = 0x17C;  // dword_12CEA8C (jail/busy; !=0 busy)
constexpr int kP_actionObj  = 0x184;  // dword_12CEA94 (action-object ptr; *(+44)==bldg id)

// --- Meister own-record offsets (the a1/v82 person record) ------------------
constexpr int kM_id         = 0x04;   // (+4) person id (weekly gate uses %3)
constexpr int kM_name       = 0x30;   // (+48) name string
constexpr int kM_bldgRec    = 0x16C;  // (+364) building record ptr  (dword idx 91)
constexpr int kM_action     = 0x17C;  // (+380) current player-action handler (idx 95)
constexpr int kM_budget     = 0x1B8;  // (+440) budget/cash dword     (idx 110)
constexpr int kM_target     = 0x1C0;  // (+448) current attack/target building id (idx112)
constexpr int kM_dayFlags   = 0x1B4;  // (+436) per-tick day-flags byte
constexpr int kM_flags2     = 0x1C8;  // (+456) second flags byte

// building-record offsets the AI reads:
constexpr int kB_typeByte   = 0x00;   // bldgRec[0] (type code; 589/65 strides)
constexpr int kB_id1        = 0x01;   // *(bldgRec+1) building id (unaligned dword)
constexpr int kB_owner39    = 0x27;   // *(bldgRec+39) owner/player word
constexpr int kB_needed57   = 0x39;   // *(bldgRec+57) needed-items id
constexpr int kB_sceneRoot93= 0x5D;   // *(bldgRec+93) scene root id

// --- game-time helpers (qword_13CE852 mirror) -------------------------------
inline u16 gtHour()   { return g_meisterGameTime.hour; }     // WORD2(qword) == +4
inline i32 gtMinute() { return g_meisterGameTime.minute; }   // +6
inline i32 gtDay()    { return g_meisterGameTime.day; }      // +0  ((int)qword)

// --- the type-def / aiplayer table bases (13CE294 / 13CE27C) -----------------
// The AI reads bytes from the building-type def tables by index:
//   *(dword_13CE294 + 589*typeByte + off)  — AiPlayer/building-type record
//   *(dword_13CE27C + 65 *itemId   + off)  — scene type-def record
// These bases come from sim/building.h (g_buildingTypes, 589-stride) and the
// item type table. The body TUs that need them include sim/building.h and use the
// real table; for fields not yet modeled there they read the raw 589/65-stride
// arrays exposed below (zero-init mirrors; the bridge points them at the live
// tables). Provided so the decompile's raw indexing is exact.
extern u8* g_buildingTypeDefBase;   // dword_13CE294 (589-stride)
extern u8* g_itemTypeDefBase;       // dword_13CE27C (65-stride)
extern u8* g_objectArrayBase;       // dword_13CE298 (169-stride, == &g_objects[0])
extern u8* g_sceneIndexBase;        // dword_13CE290 (67-stride)
extern i32 g_aiSelStaffSet;         // dword_B53968 (Diebe stock snapshot count)

// Bound the type-def tables so a garbage/negative index (which the real engine
// never produces, but synthetic test data or an unresolved id can) reads 0 rather
// than dereferencing out of bounds. g_*TypeDefCount default to a generous cap; the
// live bridge may set them to the real table sizes.
extern int g_buildingTypeDefCount;  // entries in dword_13CE294 (589-stride)
extern int g_itemTypeDefCount;      // entries in dword_13CE27C (65-stride)
inline u8 buildingTypeField(u8 typeByte, int off) {
    if (!g_buildingTypeDefBase || (int)typeByte >= g_buildingTypeDefCount) return 0;
    return g_buildingTypeDefBase[589 * typeByte + off];
}
inline u8 itemTypeField(i32 itemId, int off) {
    if (!g_itemTypeDefBase || itemId < 0 || itemId >= g_itemTypeDefCount) return 0;
    return g_itemTypeDefBase[65 * itemId + off];
}
inline u16 itemTypeWord(i32 itemId, int off) {
    if (!g_itemTypeDefBase || itemId < 0 || itemId >= g_itemTypeDefCount) return 0;
    u16 v; std::memcpy(&v, g_itemTypeDefBase + 65 * itemId + off, 2); return v;
}

// --- city-tile danger grid access -------------------------------------------
// danger(row,col) = A + B*0.125 ; A=word_12349A0, B=word_12349A2, valid=byte_12349A4.
inline int tileByteIndex(int row, int col) {
    return kCityTileRowStride * row + kCityTileStride * col;
}
inline u16 tileDangerA(int row, int col) {
    u16 v; std::memcpy(&v, g_cityTileGrid + tileByteIndex(row, col) + 0, 2); return v;
}
inline u16 tileDangerB(int row, int col) {
    u16 v; std::memcpy(&v, g_cityTileGrid + tileByteIndex(row, col) + 2, 2); return v;
}
inline u8 tileValid(int row, int col) {
    return g_cityTileGrid[tileByteIndex(row, col) + 4];
}

// danger constants (byte-exact):
constexpr float  kTileBWeight   = 0.125f;  // flt_619528/619780/6198A8
constexpr double kSecScale05    = 0.5;     // dbl_619530/619788
constexpr double kRandScale001  = 0.01;    // dbl_619790/619538/6198B0
constexpr float  kSecBase8      = 8.0f;    // flt_619540/61979C
constexpr float  kSecBase4      = 4.0f;    // flt_619798 (diebe building sec base)
constexpr double kGate085       = 0.85;    // dbl_619770/6193F8/619898
constexpr double kGate075       = 0.75;    // dbl_619778/6198A0

} // namespace aimei
} // namespace guild::sim
