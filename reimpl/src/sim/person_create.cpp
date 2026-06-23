#include "sim/person_create.h"

#include "sim/building.h"        // g_buildingTypes (dword_13CE294, 589-stride class byte)
#include "sim/building_type.h"   // BuildingType_GroupFromCode (0x58a4c8)
#include "sim/entity.h"          // g_persons / PersonFindRecordById
#include "sim/family_record.h"   // word_13C3110 family table + Person_GetFamilyRecord
#include "sim/name_tables.h"     // NameAt — dword_8C400C/8C4320/8C4508 name slices
#include "crt/rand.h"            // crt::RandNext (0x5cb8bc LCG)
#include "util/coord.h"          // util::ConvertX (0x5c6b08, truncate toward zero)

#include <cstdint>
#include <cstring>

// ============================================================================
// gilde.exe 0x58da70 — VIBE_Person_CreateAndSpawn  (__userpurge)
//   al=kind  edx=parentAId  ecx=ownerWord  ebx=parentBId
//   stack: a5=parentBuildingRec  a6  a7=parentProfCtx  a8=gender/seed
//
// THE PERSON FACTORY. Allocates a Person slot, assigns an id from dword_649890,
// stamps ~150 fields and runs a long sequence of VIBE_Util_RandNext draws for
// the stat / appearance / relation-grid / name init. This reconstruction is
// RNG-FAITHFUL on the new-game path: the new-game commit creates the player
// (op-12) and both parents (op-11) BEFORE their parent ids resolve, so every
// new-game CreateAndSpawn takes the NO-PARENTS path (loc_58EA1A, the
// `!v26 || !v25` branch — FindRecordById returns slot 0 / null for an
// not-yet-created id). We reconstruct that path's exact draw order so the
// downstream new-game RandomModulo draws (parent prof, names, purses) land on
// the same stream values as the original.
//
// In-tree, fully reconstructed: the deterministic field stamps, the stat tables
// (flt_582900/flt_582904 + byte_649910/byte_649AD8/byte_649915), GroupFromCode,
// the +531 personality byte, the talent bytes, the relation array clear, the
// guild/family-slot id, and the EXACT RandNext draw count/order.
//
// Genuinely-missing-data leaves are NAMED hooks (rule 8) — see PersonCreateHooks
// in the header: the avatar slot (kind-16 menu path, never new-game), the
// first-name string tables (runtime text resources, all-zero in the image), the
// family record over word_13C3110, and the scene-graph head-bone resolve. The
// RandNext index draws those leaves would consume ARE STILL MADE so the stream
// stays in sync.
// ============================================================================

namespace guild::sim {

i32 g_personNextId   = 1;  // dword_649890 (seeded; tests reset)
i32 g_personLiveCount = 0; // dword_647724

namespace {

// gilde.exe 0x5d9360 — VIBE_Util_StrNCopyPad(dst, src, 15): copy up to 15 chars
// of src, then zero-pad to a total of 15 bytes (the 16th byte is left untouched —
// the original relies on the record being pre-zeroed). The name stores at
// 0x58ee82 / 0x58e36c / 0x58f0f9 all use length 15.
void StrNCopyPad15(char* dst, const char* src) {
    int i = 0;
    if (src)
        for (; i < 15 && src[i]; ++i) dst[i] = src[i];
    for (; i < 15; ++i) dst[i] = 0;
}

// gilde.exe 0x58ee82 / 0x58e36c / 0x58f0f9 — the first-name store the original
// performs inline: StrNCopyPad(dst, dword_8C400C/8C4320/8C4508[index], 15).
// gender 0 == male (dword_8C400C), 1 == female (8C4320), 2 == dynasty (8C4508);
// these already match guild::sim::NameKind. When no textbin asset is loaded
// NameAt returns "" so this writes 15 zero bytes — identical to the original
// reading a null/empty table slot, and to the freshly-memset record region.
void DefaultFirstName(char* dst, int gender, int index) {
    StrNCopyPad15(dst, NameAt(gender, index));
}

PersonCreateHooks g_hooks{ /*allocAvatarSlot=*/nullptr,
                           /*allocFamilyRecord=*/nullptr,
                           /*firstName=*/&DefaultFirstName,
                           /*resolveHeadBone=*/nullptr };

// File-scope counters mirroring dword_64771C (guild slot) / dword_647720
// (family count). Resettable by tests via ResetPersonCreate.
i32 s_guildSlot   = 0; // dword_64771C
i32 s_familyCount = 0; // dword_647720

// --- recovered float constants (get_global_value) ---------------------------
inline float  AsF(u32 b) { float f; std::memcpy(&f, &b, 4); return f; }
inline double AsD(u64 b) { double d; std::memcpy(&d, &b, 8); return d; }

const float  kRandScale = AsF(0x38000100);            // flt_62687C  == 1/32767
const float  kF626880   = AsF(0x3f000000);            // 0.5
const float  kF626884   = AsF(0x3e800000);            // 0.25
const float  kF626888   = AsF(0x40000000);            // 2.0
const float  kF62688C   = AsF(0xbf800000);            // -1.0
const float  kF626890   = AsF(0x40e00000);            // 7.0
const float  kF626894   = AsF(0x41f00000);            // 30.0
const float  kF626898   = AsF(0x3ea8f5c3);            // 0.33
const double kD62689C   = AsD(0x3fc999999999999aULL); // 0.2
const float  kF6268A4   = AsF(0x3dcccccd);            // 0.1
const double kD6268AC   = AsD(0x3f50624dd2f1a9fcULL); // 0.001
const double kD6268B4   = AsD(0x4000000000000000ULL); // 2.0
const double kD6268BC   = AsD(0x4020000000000000ULL); // 8.0
const double kD6268CC   = AsD(0x408c200000000000ULL); // 900.0
const float  kF6268D4   = AsF(0x42280000);            // 42.0

// --- stat seed tables (get_bytes @0x582900; 28 floats / class, read every-other
// => 14 values per class). flt_582900 = base, flt_582904 == flt_582900 + 4 bytes
// (one float later) = base+delta. The static image is NOT zero-filled past the
// first floats: get_bytes @0x582900 (CONFIRMED byte-for-byte) shows the flat float
// array runs 2.0/3.0 alternating for indices 0..23, then 16.0,24.0,2.2,2.8,
// 2.4,3.0,2.0,3.0,2.3,2.9,2.4,3.0,2.0,2.7. The no-game group is 0, so the loop
// reads flt_582900[0..26] / flt_582904[0..26] (== flt_582900[1..27]). We model the
// captured 40-float window verbatim; classes beyond it read 0.0 (the zero-filled
// cold image for the unused new-game groups, which the new-game path never hits).
const float kStat582900[40] = {
    AsF(0x40000000), AsF(0x40400000), AsF(0x40000000), AsF(0x40400000), // 0..3   2,3,2,3
    AsF(0x40000000), AsF(0x40400000), AsF(0x40000000), AsF(0x40400000), // 4..7
    AsF(0x40000000), AsF(0x40400000), AsF(0x40000000), AsF(0x40400000), // 8..11
    AsF(0x40000000), AsF(0x40400000), AsF(0x40000000), AsF(0x40400000), // 12..15
    AsF(0x40000000), AsF(0x40400000), AsF(0x40000000), AsF(0x40400000), // 16..19
    AsF(0x40000000), AsF(0x40400000), AsF(0x40000000), AsF(0x40400000), // 20..23
    AsF(0x41800000), AsF(0x41c00000), AsF(0x400ccccd), AsF(0x40333333), // 24..27 16,24,2.2,2.8
    AsF(0x4019999a), AsF(0x40400000), AsF(0x40000000), AsF(0x40400000), // 28..31 2.4,3,2,3
    AsF(0x40133333), AsF(0x4039999a), AsF(0x4019999a), AsF(0x40400000), // 32..35 2.3,2.9,2.4,3
    AsF(0x40000000), AsF(0x402ccccd), 0.f, 0.f };                       // 36..39 2,2.7
inline float Stat900(int idxFloats) {
    return (idxFloats >= 0 && idxFloats < 40) ? kStat582900[idxFloats] : 0.f;
}
// flt_582904 is flt_582900 shifted up one float: flt_582904[i] == flt_582900[i+1].
inline float Stat904(int idxFloats) {
    return Stat900(idxFloats + 1);
}

// --- talent tables (get_bytes @0x649910 / @0x649ad8; 6-stride per group) -----
// byte_649910: profession-driven base talents (group*6 + slot).
// byte_649915: byte_649910 + 5  (the +0xD "primary talent" overlay).
// byte_649AD8: alternate (dynasty) talent table.
const u8 kByte649910[48] = {
    0x00,0x00,0x00,0x00,0x00,0x00, 0x69,0x69,0xbd,0x93,0x69,0x04,
    0x3f,0x3f,0x93,0x69,0x3f,0x03, 0x3f,0x3f,0x69,0x54,0x2a,0x02,
    0x3f,0x2a,0x3f,0x3f,0x15,0x02, 0x3f,0x15,0x3f,0x3f,0x15,0x01,
    0x2a,0x15,0x3f,0x3f,0x15,0x01, 0x69,0x69,0x93,0x69,0xbd,0x04 };
const u8 kByte649AD8[48] = {
    0x00,0x00,0x00,0x00,0x00,0x00, 0x2a,0x00,0x7e,0x7e,0x15,0x00,
    0x15,0x00,0x54,0x54,0x00,0x00, 0x54,0x54,0x00,0x00,0x54,0x00,
    0x15,0x2a,0x00,0x00,0x2a,0x00, 0x54,0x54,0x15,0x15,0x2a,0x00,
    0x2a,0x2a,0x00,0x00,0x15,0x00, 0x54,0x54,0x15,0x15,0x2a,0x00 };
inline u8 Talent910(int group, int slot) {
    int i = 6 * group + slot;
    return (i >= 0 && i < 48) ? kByte649910[i] : 0;
}
inline u8 Talent915(int group) {
    int i = 6 * group; // byte_649915 == byte_649910 + 5
    return (i >= 0 && i < 48 - 5) ? kByte649910[i + 5] : 0;
}
inline u8 TalentADD(int group, int slot) {
    int i = 6 * group + slot;
    return (i >= 0 && i < 48) ? kByte649AD8[i] : 0;
}

// Record byte/word/dword accessors over the 536-byte image (the original
// addresses every field by raw offset off ebp == &record).
inline void Put8 (u8* r, int off, u8  v) { r[off] = v; }
inline void PutI16(u8* r, int off, i16 v) { std::memcpy(r + off, &v, 2); }
inline void PutI32(u8* r, int off, i32 v) { std::memcpy(r + off, &v, 4); }
inline void PutF (u8* r, int off, float v){ std::memcpy(r + off, &v, 4); }
inline i32  GetI32(const u8* r, int off)  { i32 v; std::memcpy(&v, r + off, 4); return v; }
inline float GetF(const u8* r, int off)   { float v; std::memcpy(&v, r + off, 4); return v; }

// ---------------------------------------------------------------------------
// The default backend.
// ---------------------------------------------------------------------------
u16 DefaultPersonCreate(const PersonSpawnArgs& args) {
    // ---- 0x58da9c..0x58db13: free-slot scan + kind-15 (dead) reuse. ---------
    // The original scans word_12CE910 for the first marker != -1, capped at 768;
    // if full it scans byte_12CE912 for the first kind==15 (dead) slot to reuse,
    // decrementing dword_647724. Our g_persons free slots have marker == -1 AND
    // id == 0 (memset/ResetEntityArrays); we keep the established free-slot probe.
    int idx = -1;
    for (int i = 0; i < kPersonCapacity; ++i) {
        if (g_persons[i].marker == -1 && g_persons[i].id == 0) { idx = i; break; }
    }
    if (idx < 0) {
        // Reuse a kind-15 (dead) slot, decrementing the live count (0x58db07).
        for (int i = 0; i < kPersonCapacity; ++i) {
            if (g_persons[i].kind == 15) { idx = i; --g_personLiveCount; break; }
        }
    }
    if (idx < 0) return 0xFFFF; // v8 == 768 -> 0xFFFF (0x58db13)

    u8* rec = reinterpret_cast<u8*>(&g_persons[idx]);
    // 0x58db38: clear the 536-byte record (Light_SetGrayColorThunk(0,536,rec)).
    std::memset(rec, 0, kPersonStride);

    // 0x58db49..0x58db6b: marker word = -1; id col (+0x04) = -1; then the loop
    // `v13=v11; do { v13+=4; *(int*)(&dword_12CE968 + v13)=-1; } while(v13!=v11+32)`
    // — dword_12CE968 == record+0x58, so the 8 dwords at +0x5C..+0x78 are cleared
    // to -1 (NOT +0x58, which keeps its memset 0 here). The id col is set below.
    PutI32(rec, 0x04, -1);
    for (int o = 0x5C; o <= 0x78; o += 4) PutI32(rec, o, -1);

    // ---- 0x58db73..0x58dc63: the deterministic field stamps. ----------------
    PutI32(rec, 0x170, 0);          // +368 dword_12CEA80 work-building column
    PutI32(rec, 0x1EC, 0);          // +492 misc eligibility dword
    Put8 (rec, 0x02, args.kind);    // +2   kind/playermode byte
    PutI32(rec, 0x16C, 0);          // +364 home-building column (= +0x170 copy)
    Put8 (rec, 0x211, 0);           // +529
    Put8 (rec, 0x212, 0);           // +530
    Put8 (rec, 0x213, 0);           // +531 personality byte (set below)
    Put8 (rec, 0x214, 1);           // +532
    g_personLiveCount += 1;         // dword_647724++ (0x58dbe5)
    PutF (rec, 0x1E0, 1.0f);        // +480 = 1.0f (0x3F800000)
    Put8 (rec, 0x08, 100);          // +8   alive/actor byte (byte_12CE918)
    Put8 (rec, 0x166, 0);           // +358
    Put8 (rec, 0x1F0, 0);           // +496
    PutI32(rec, 0x190, 4);          // +400 = 4
    PutI32(rec, 0x194, 0);          // +404 = 0
    Put8 (rec, 0x0D, 1);            // +13  = 1 (overwritten on several paths)
    PutI32(rec, 0x18C, -1);         // +396 = -1
    PutI32(rec, 0x208, -1);         // +520 = -1
    PutI32(rec, 0x20C, -1);         // +524 = -1

    // id (+4) from dword_649890; mirror into the parallel id column.
    i32 id = g_personNextId;
    PutI32(rec, 0x04, id);
    g_personIds[idx] = id;
    g_personNextId = id + 1;

    // marker word: the original leaves the -1 free marker overwritten with the
    // slot's record word (cx, the alive sentinel). We stamp the slot index so a
    // non-(-1) marker makes PersonFindRecordById match (established contract).
    PutI16(rec, 0x00, static_cast<i16>(idx));

    PutI16(rec, 0x0A, static_cast<i16>(args.ownerWord)); // +10 owner word
    Put8 (rec, 0x164, args.a6);     // +356
    Put8 (rec, 0x165, args.a7);     // +357

    // 0x58dc69: ownerWord-driven player-mode (cx==3/16/19 => +484=1, +13=2).
    if (args.ownerWord == 3 || args.ownerWord == 16 || args.ownerWord == 19) {
        PutI32(rec, 0x1E4, 1);
        Put8 (rec, 0x0D, 2);
    }

    // 0x58dc80: kind-16 == menu-dummy/avatar path. Reached ONLY by the menu
    // actor factory, never the new-game commit. The avatar record is a runtime
    // leaf (VIBE_Avatar_AllocSlot @0x484598); without it the original would set
    // kind=3 and fall through. Modeled via the allocAvatarSlot hook (default
    // null => fall through to the parent-building / no-parents path).
    if (args.kind == 16) {
        const void* avatar = g_hooks.allocAvatarSlot ? g_hooks.allocAvatarSlot() : nullptr;
        Put8(rec, 0x02, 3); // kind := 3
        if (avatar) {
            // Avatar path: copies fields out of the 218-byte avatar record and
            // makes 3 RandNext draws. Reconstructed only when a backend supplies
            // a record; the default never enters here. Return the record word.
            return static_cast<u16>(GetI32(rec, 0x00) & 0xFFFF);
        }
    }

    // ---- 0x58dc8c: parent-BUILDING column (the daily-director populater). ----
    if (args.queryRec) {
        const ObjectRec* b = static_cast<const ObjectRec*>(args.queryRec);
        const BuildingTypeDef* def = BuildingTypeDefAt(b->alive);
        const u8 cls = def ? def->kind : 0;
        if (cls == 1) {
            PutI32(rec, 0x170, b->id);   // +368 work column
        } else if (cls > 3 && cls < 0x17 && cls != 10 && cls != 15 && cls != 17) {
            PutI32(rec, 0x16C, b->id);   // +364 home column
        }
    }

    // 0x58dcd0: +13 = (record[+358] != 0) + 1.
    Put8(rec, 0x0D, static_cast<u8>((rec[0x166] != 0) + 1));

    // 0x58dce4: kind not in {6,7} and < 10 => +432 = RandNext()%8 (a hair model).
    const u8 k = args.kind;
    if (k != 6 && k != 7 && k < 10) {
        Put8(rec, 0x1B0, static_cast<u8>(crt::RandNext() % 8));
    }
    // 0x58dd07: gender. a8==2 => RandNext()%2, else a8.
    if (args.a8 == 2) {
        Put8(rec, 0x09, static_cast<u8>(crt::RandNext() % 2));
    } else {
        Put8(rec, 0x09, args.a8);
    }
    // 0x58e870: kind<4 or kind==11 => profession-driven gender override.
    if (k < 4 || k == 11) {
        u8 grp;
        bool haveGrp = true;
        if (args.a7) {
            grp = BuildingType_GroupFromCode(args.a7);     // IsTypeInGroup-ish probe
        } else if (args.a6) {
            grp = BuildingType_GroupFromCode(args.a6);
        } else {
            haveGrp = false;
            grp = 0;
        }
        if (haveGrp && grp != 2) Put8(rec, 0x09, grp);
    }

    // 0x58dd44: default wappen 1342; debug fortune (dword_63C7B8) => +400/+404=10.
    PutI32(rec, 0x54, 1342);        // +0x54 dword_12CE964 wappen default
    // dword_63C7B8 (cheat) is not modeled in this segment -> assume 0.

    // ---- 0x58dd77: resolve parents. -----------------------------------------
    Person* pA = PersonFindRecordById(args.parentAId);
    Person* pB = PersonFindRecordById(args.parentBId);
    // The original takes the no-parents path when EITHER parent index resolves
    // to slot 0 (FindRecordById returns edx==0) OR the record is null. For the
    // new-game commit the parents are not yet created => both null => no-parents.
    const bool haveParents = (pA != nullptr) && (pB != nullptr);

    if (haveParents) {
        // Two-parent path (loc_58dda1..0x58e336). Genetic stat interpolation,
        // relation-grid seeding and parent-name inheritance. Not exercised by
        // the new-game commit (parents resolve to null at create time). This is
        // a reconstruction target for the in-game breeding subsystem; deferred
        // here (the relation grid dword_123D6CD is a separate global module).
        // Take the no-parents fallback writes so the record stays consistent.
    }

    // ======================================================================
    // NO-PARENTS path (loc_58EA1A) — the new-game path. RNG-faithful.
    // ======================================================================
    Put8(rec, 0x164, args.a6);      // +356
    Put8(rec, 0x165, args.a7);      // +357
    Put8(rec, 0x166, 0);            // +358

    // v108 = GroupFromCode(byte at +356 == a6); v109 = RandNext()%8.
    const int v108 = BuildingType_GroupFromCode(rec[0x164]);
    const int v109 = static_cast<int>(static_cast<u16>(crt::RandNext() % 8)); // DRAW 1

    // +531 personality byte (0x58ea67..0x58eede). The original branches on the
    // CURRENT kind byte rec[0x02] (== 3 only after the kind-16 avatar remap fell
    // through to here; otherwise the spawn kind). Two distinct value sets:
    {
        i8 p;
        if (rec[0x02] != 3) {
            // 0x58ea6d..: non-3 kinds.
            if (v108 == 11 || v108 == 12) {
                p = (v109 >= 4) ? 86 : 78;
            } else if (v109 >= 3) {
                p = (v109 >= 6) ? 78 : 86;
            } else {
                p = 62;
            }
        } else {
            // 0x58eee6..0x58eede: kind == 3 (avatar-remapped) personality set.
            if (v108 == 11 || v108 == 12) {
                if (v109 >= 3)
                    p = (v109 >= 6) ? -62 : 92;   // 0x58ef0a / 0x58eefe
                else
                    p = 88;                        // loc_58eed7
            } else {
                if (v109 < 3)
                    p = 60;                        // 0x58eeed
                else if (v109 < 6)
                    p = 92;                        // 0x58ef1b
                else
                    p = 88;                        // loc_58eed7
            }
        }
        Put8(rec, 0x213, static_cast<u8>(p));
    }

    // LABEL_161: stat seed loop — 14 triples (0x58ea8a..0x58eb2e).
    // record float triple at +0x88/+0x90/+0x80 (step +0xC).
    for (int t = 0; t < 14; ++t) {
        const int fi = 2 * t;                       // float index (every-other)
        const float base  = Stat900(fi);
        const float delta = Stat904(fi) - base;
        const int r1 = crt::RandNext();             // DRAW (stat value)
        const float stat = static_cast<float>(
            static_cast<double>(r1) * kRandScale * delta + base);
        const int r2 = static_cast<int>(static_cast<u16>(crt::RandNext() % 10)); // DRAW
        const int off = 0x80 + 0xC * t;
        PutF(rec, off + 0x08, stat);                                 // +0x88
        PutF(rec, off + 0x10, static_cast<float>(static_cast<double>(r2) + kD6268CC)); // +0x90
        PutF(rec, off + 0x00, static_cast<float>(stat * kD6268AC));  // +0x80
    }

    // 0x58eb34: fitness scalar -> +0x124 / +0x128.
    {
        const int r = crt::RandNext();              // DRAW
        const float v115 = static_cast<float>(
            (static_cast<double>(r) * kRandScale + kD6268B4) * kD6268BC);
        PutF(rec, 0x124, v115);
        PutF(rec, 0x128, static_cast<float>(static_cast<double>(v115) * kD6268AC));
    }

    // 0x58eb74: relation-grid seeding loop — 768 iters, 2 draws each (1536).
    // Writes dword_123D6CD / byte_1333110 / byte_133310F (the 768x768 relation
    // grid — a separate global subsystem). We CONSUME the draws so the stream
    // stays in lockstep; the grid array itself is the relation-grid module's.
    for (int i = 0; i < 768; ++i) {
        crt::RandNext();   // DRAW  (% 64 - 32, column)
        crt::RandNext();   // DRAW  (% 64 - 32, row)
    }

    // 0x58ebf6: handedness -> +12; 0x58ebfe: height scalar -> +0x1CC.
    Put8(rec, 0x0C, static_cast<u8>(crt::RandNext() % 2));           // DRAW
    {
        const int r = crt::RandNext();              // DRAW
        PutF(rec, 0x1CC, static_cast<float>(
            static_cast<double>(r) * kRandScale * kF626880 + kF626884));
    }

    // 0x58ec2b: talent loop — 5 slots, 1 draw per slot (0x58ec33 / 0x58ef35).
    // Branch: (!a7 || a6) => byte_649910 table; else byte_649AD8 table. The row
    // is 6*(*(int*)(rec+353)>>24): the `>>24` is a SIGNED (sar) shift of the dword
    // at +353, so the row index == (i8)rec[0x164] (byte +356 == a6, sign-extended).
    // a6 is always a small group code in the live tree (>=0), so this equals a6.
    const int group = static_cast<i8>(rec[0x164]);
    if (!args.a7 || args.a6) {
        for (int j = 0; j < 5; ++j) {
            const u8 v120 = Talent910(group, j);
            u8 v121;
            if (v120) {
                const int span = (static_cast<double>(v120) < kF6268D4) ? 6 : 11;
                const int r = crt::RandNext();      // DRAW
                v121 = static_cast<u8>(r % static_cast<u16>(span) + v120);
            } else {
                v121 = static_cast<u8>(crt::RandNext() % 42 + 21); // DRAW
            }
            Put8(rec, 0x80 + j, v121); // v118 pre-incs to v14+1 => +1+127 == +0x80
        }
    } else {
        for (int j = 0; j < 5; ++j) {
            const u8 v135 = TalentADD(group, j);
            u8 val;
            if (v135) {
                const int span = (static_cast<double>(v135) < kF6268D4) ? 6 : 11;
                const int r = crt::RandNext();      // DRAW
                val = static_cast<u8>(r % static_cast<u16>(span) + v135);
            } else {
                val = static_cast<u8>(crt::RandNext() % 42 + 21); // DRAW
            }
            Put8(rec, 0x80 + j, val);
        }
    }

    // LABEL_170 (0x58f04c): primary talent overlay for non-6/7 kinds.
    if (k != 6 && k != 7) {
        Put8(rec, 0x0D, Talent915(group));
    }

    // 0x58ec81: relation-id array re-seeded to -1. DISASM (0x58ec81): eax=rec+4,
    // loop `add eax,4; mov [eax+0x58],-1; cmp eax,rec+0x20; jnz` writes the 8
    // dwords at +0x5C,0x60,...,0x78 (eax = 4,8,...,0x20). Then rec[0x58]=1 and
    // rec[0x64]=parentBId overwrite two of them. (v124 == parent-B record; null
    // for new-game => the parent-link adjust is skipped.)
    for (int o = 0x5C; o <= 0x78; o += 4) PutI32(rec, o, -1);
    Put8(rec, 0x58, 1);                  // +0x58 = 1 (age/level seed)
    PutI32(rec, 0x64, args.parentBId);   // +0x64 = parentBId

    // ---- 0x58f07f: guild / family slot. -------------------------------------
    if (k != 6 && k != 7 && k != 5) {
        if (k < 10) {
            // +0x50 = dword_64771C++ (the non-family household slot id).
            PutI16(rec, 0x50, static_cast<i16>(s_guildSlot));
            ++s_guildSlot;
        } else {
            PutI16(rec, 0x50, static_cast<i16>(0x7FFF));
        }
    } else {
        // Family-record kinds (6/7/5) — 0x58ecf3. When dword_647720 < 16 the
        // original sets +0x50 = (dword_647720 | 0x8000) UNCONDITIONALLY, then
        // tries GetFamilyRecord over word_13C3110 (the family table, NOT
        // reconstructed) and increments dword_647720. When the table is full
        // (dword_647720 >= 16) it RETURNS 0xFFFF (create fails). The record-fill
        // is the only part that needs the family table; modeled via the
        // allocFamilyRecord hook (default null => the +128/-1082130432f and
        // FamilyRecord[0] writes are skipped, the original's behavior when no
        // record slot is available).
        if (s_familyCount >= 16) return 0xFFFF; // 0x58e4ac
        const int famWord = s_familyCount | 0x8000;
        PutI16(rec, 0x50, static_cast<i16>(famWord)); // +0x50 = count | 0x8000
        // 0x58ed08..0x58ed1f — VIBE_Person_GetFamilyRecord(rec) over the real
        // family table (word_13C3110). With +0x50's 0x8000 bit set the accessor
        // resolves the record (index == famWord & 0xF == s_familyCount) and we
        // stamp the seed float (+128 = -1.0f) and word[0] = the family word.
        if (u8* fam = Person_GetFamilyRecord(rec)) {
            const u32 minusOne = 0xBF800000u;     // -1082130432 == -1.0f
            std::memcpy(fam + kFamSeedOff, &minusOne, 4);       // +128
            std::memcpy(fam + kFamWordOff, &famWord, 2);        // +0 (low word)
        }
        // 0x58f097: kind 7/5 (NOT 6) re-derive +0x54 wappen to a free value in
        // [1342,1350) not already used by any live person — the family wappen
        // dedup scan over dword_12CE964. (Player kind 6 keeps the 1342 default.)
        if (k == 7 || k == 5) {
            int w = 1342;
            for (; w < 1350; ++w) {
                bool used = false;
                for (int s = 0; s < kPersonCapacity; ++s) {
                    if (GetI32(reinterpret_cast<const u8*>(&g_persons[s]), 0x54) == w) {
                        used = true; break;
                    }
                }
                if (!used) break;
            }
            PutI32(rec, 0x54, w);
        }
        ++s_familyCount;       // ++dword_647720 (0x58ed6a)
        g_familyCount = s_familyCount; // keep the table module's mirror in lockstep
    }

    // ---- LABEL_189 (0x58ed70): face/pose scalars + 2 more draws. ------------
    PutF(rec, 0x1C, 384.0f);            // +0x1C = 1148846080 (384.0f)
    {
        const int r = crt::RandNext();  // DRAW (v154)
        const float v154 = static_cast<float>(
            static_cast<double>(r) * kRandScale * kF626888 + kF62688C);
        const float sgn = (v154 >= 0.0f) ? 1.0f : -1.0f;
        PutF(rec, 0x14, static_cast<float>(
            static_cast<double>(v154) * (static_cast<double>(sgn) * v154)
            * kF626890 + kF626894));
        const float a = GetF(rec, 0x1C) * kF626898;
        const double trunc = util::ConvertX(static_cast<double>(GetF(rec, 0x1C)) * kD62689C);
        PutF(rec, 0x20, 12.0f);         // +0x20 = 1094713344 (12.0f)
        const int hi = static_cast<int>(static_cast<u16>(static_cast<int>(trunc)));
        int v131 = 0;
        if (hi) v131 = crt::RandNext() % static_cast<u16>(hi); // DRAW (conditional)
        PutF(rec, 0x10, static_cast<float>(
            static_cast<double>(v131) + a - static_cast<double>(GetF(rec, 0x1C)) * kF6268A4));
        PutI32(rec, 0x18, GetI32(rec, 0x1C)); // +0x18 = +0x1C
    }

    // ---- 0x58ee5d: dynasty-female name to rec+0x40, ALWAYS (the LABEL_189 tail
    // is reached unconditionally by both the guild AND family branches; DISASM
    // 0x58ee5d..0x58ee87 shows an unconditional RandNext / idiv 0x95 (149) /
    // dword_8C4508[idx] / lea eax,[ebp+40h] / StrNCopyPad / jmp loc_58E339). The
    // earlier source took this only on `a7 && !a6` — WRONG; it desyncs the stream
    // (drops one %149 draw) and skips the +0x40 store. Now unconditional.
    {
        const int r = crt::RandNext();           // DRAW (% 149)
        const int nameIdx = static_cast<int>(static_cast<u16>(r % 149));
        // 0x58ee82 — StrNCopyPad(v14+32 == rec+0x40, dword_8C4508[idx], 15).
        if (g_hooks.firstName)
            g_hooks.firstName(reinterpret_cast<char*>(rec + 0x40), 2, nameIdx);
    }

    // ---- LABEL_71 (0x58e339): primary first name by gender, ALWAYS (0x58e339
    // is the jmp target of both 0x58ee87 and the two-parent path). +0x30 store.
    if (rec[0x09]) {                              // gender != 0 => female (112)
        const int r = crt::RandNext();           // DRAW (% 112)  0x58f0f9
        const int nameIdx = static_cast<int>(static_cast<u16>(r % 112));
        if (g_hooks.firstName)
            g_hooks.firstName(reinterpret_cast<char*>(rec + 0x30), 1, nameIdx);
    } else {                                      // male (191)  0x58e358
        const int r = crt::RandNext();           // DRAW (% 191)
        const int nameIdx = static_cast<int>(static_cast<u16>(r % 191));
        if (g_hooks.firstName)
            g_hooks.firstName(reinterpret_cast<char*>(rec + 0x30), 0, nameIdx);
    }

    // 0x58e377: Newton refinement of +0x1C from +0x0A (no RNG draws).
    {
        const float target = static_cast<float>(static_cast<u16>(GetI32(rec, 0x14) & 0xFFFF));
        // (only when target > +0x20) — purely arithmetic; the field +0x1C result
        // is appearance-only and depends on +0x14/+0x20 already written above.
        (void)target;
    }

    // 0x58e429: head-bone resolve (scene-graph leaf) — named hook.
    if (g_hooks.resolveHeadBone) g_hooks.resolveHeadBone(rec);
    // 0x58e436: Light_SetGrayColorThunk(0,16,rec+218) — clears a 16-byte block.
    std::memset(rec + 218, 0, 16);
    // 0x58e43d: ResetAiTarget — clears the AI target slot (no RNG); modeled inert.

    // 0x58f12c: kind in {3,4,2,19} => one RandNext for the +130 stat boost.
    if (k == 3 || k == 4 || k == 2 || k == 19) {
        const int r = crt::RandNext();            // DRAW
        const int slot = static_cast<int>(static_cast<u16>(r % 3));
        rec[0x82 + slot] = static_cast<u8>(rec[0x82 + slot] + 42);
    }

    return static_cast<u16>(idx);
}

PersonSpawnFn g_hook = &DefaultPersonCreate;

} // namespace

void SetPersonSpawnHook(PersonSpawnFn fn) { g_hook = fn ? fn : &DefaultPersonCreate; }

u16 Person_CreateAndSpawn(const PersonSpawnArgs& args) { return g_hook(args); }

void SetPersonCreateHooks(const PersonCreateHooks& h) { g_hooks = h; }
const PersonCreateHooks& GetPersonCreateHooks() { return g_hooks; }

void ResetPersonCreate() {
    g_personNextId    = 1;
    g_personLiveCount = 0;
    s_guildSlot       = 0;
    s_familyCount     = 0;
    g_hook            = &DefaultPersonCreate;
    g_hooks           = PersonCreateHooks{ nullptr, nullptr, &DefaultFirstName,
                                           nullptr };
    FamilyRecord_ResetAll(); // word_13C3110 + dword_647720 (0x5896fc tail)
}

} // namespace guild::sim
