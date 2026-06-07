#include "sim/building_type.h"

// Building-type classification. The originals are dense switch tables that
// tail-jump into a shared block of `mov al,N; ret` epilogues at 0x589A32.. and
// labels 0x5898E2 / 0x589850 / 0x58A272.. . The return constants were recovered
// from the raw epilogue bytes (`B0 NN C3` == `mov al,NN; ret`):
//   0x5898E2 -> 0    0x589850 -> 2
//   0x58A272 -> 11   0x58A275 -> 12   0x58A278 -> 1    0x58A27B -> 10
//   0x589C74 -> 6    0x589C77 -> 3
//   0x589AA1 -> 5    0x589AA4 -> 8    0x589AA7 -> 9    0x589AAA -> 14
//   0x589AAD -> 16   0x589AB0 -> 19   0x589AB3 -> 20   0x589AB6 -> 21  0x589AB9 -> 22
//   0x589A32 -> 15   0x589A35 -> 4    0x589A38 -> 7    0x589A3B -> 23
//   0x589A3E -> 24   0x589A41 -> 25   0x589A44 -> 26
namespace guild::sim {

// gilde.exe 0x58a4c8 — VIBE_BuildingType_GroupFromCode
u8 BuildingType_GroupFromCode(u8 code) {
    switch (code) {
        case 1: case 2: case 3: case 4: case 5: case 6:        return 11; // ->0x58A272
        case 7: case 8: case 9: case 10: case 11: case 12:     return 9;  // ->0x589AA7
        case 13: case 14: case 15: case 16: case 17: case 18:  return 7;  // ->0x589A38
        case 19: case 20: case 21: case 22: case 23: case 24:  return 6;  // ->0x589C74
        case 25: case 26: case 27: case 28: case 29: case 30:  return 8;  // ->0x589AA4
        case 31: case 32: case 33:                             return 13;
        case 34: case 35: case 36: case 37: case 38: case 39:  return 5;  // ->0x589AA1
        case 40: case 41: case 42: case 43: case 44: case 45:  return 12; // ->0x58A275
        case 46: case 47: case 48: case 49: case 50: case 51:  return 1;  // ->0x58A278
        case 52: case 53: case 54: case 55: case 56: case 57:  return 10; // ->0x58A27B
        case 58: case 59: case 60: case 61: case 62: case 63:  return 2;  // ->0x589850
        case 64: case 65: case 66: case 67: case 68: case 69:  return 3;  // ->0x589C77
        case 70: case 71: case 72: case 73: case 74: case 75:  return 4;  // ->0x589A35
        default:                                               return 0;
    }
}

// gilde.exe 0x58a54c — VIBE_BuildingType_GroupFromPairCode
u8 BuildingType_GroupFromPairCode(u8 code) {
    switch (code) {
        case 1: case 2:    return 11; // ->0x58A272
        case 3: case 4:    return 7;  // ->0x589A38
        case 5: case 6:    return 6;  // ->0x589C74
        case 7: case 8:    return 8;  // ->0x589AA4
        case 13: case 14:  return 5;  // ->0x589AA1
        case 15: case 16:  return 12; // ->0x58A275
        case 17: case 18:  return 1;  // ->0x58A278
        case 19: case 20:  return 10; // ->0x58A27B
        case 21: case 22:  return 2;  // ->0x589850
        case 23: case 24:  return 3;  // ->0x589C77
        case 25: case 26:  return 4;  // ->0x589A35
        default:           return 0;
    }
}

// gilde.exe 0x58a560 — VIBE_BuildingType_ComputeRankWithinGroup
//   highest code in a six-code group is rank 1, lowest is rank 6.
int BuildingType_ComputeRankWithinGroup(u8 code) {
    if (code >= 1  && code <= 6)   return 6  - code + 1;
    if (code >= 7  && code <= 12)  return 12 - code + 1;
    if (code >= 34 && code <= 39)  return 39 - code + 1;
    if (code >= 40 && code <= 45)  return 45 - code + 1;
    if (code >= 46 && code <= 51)  return 51 - code + 1;
    if (code >= 58 && code <= 63)  return 63 - code + 1;
    if (code >= 64 && code <= 69)  return 69 - code + 1;
    if (code >= 70 && code <= 75)  return 75 - code + 1;
    if (code >= 13 && code <= 18)  return 18 - code + 1;
    if (code >= 52 && code <= 57)  return 57 - code + 1;
    if (code >= 25 && code <= 30)  return 30 - code + 1;
    if (code >= 19 && code <= 24)  return 24 - code + 1;
    return 0;
}

// gilde.exe 0x589a7c — VIBE_BuildingType_MapToActionCode
u8 BuildingType_MapToActionCode(u8 code) {
    u8 group = BuildingType_GroupFromCode(code);
    if (!group)
        return group;
    switch (group) {
        case 1:  return 18;
        case 2:  return 20;
        case 3:  return 21;
        case 4:  return 22;
        case 5:  return 14;
        case 6:  return 8;
        case 7:  return 7;   // ->0x589A38
        case 8:  return 9;
        case 9:  return 5;
        case 10: return 19;
        case 11: return 4;   // ->0x589A35
        case 12: return 16;
        default: return 0;   // ->0x5898E2
    }
}

// gilde.exe 0x589af0 — VIBE_BuildingType_MapToCategoryCode  (group -> category)
u8 BuildingType_MapToCategoryCode(u8 group) {
    switch (group) {
        case 1:  return 18;
        case 2:  return 20; // ->0x589AB3
        case 3:  return 21; // ->0x589AB6
        case 4:  return 22; // ->0x589AB9
        case 5:  return 14; // ->0x589AAA
        case 6:  return 8;  // ->0x589AA4
        case 7:  return 7;  // ->0x589A38
        case 8:  return 9;  // ->0x589AA7
        case 9:  return 5;  // ->0x589AA1
        case 10: return 19; // ->0x589AB0
        case 11: return 4;  // ->0x589A35
        case 12: return 16; // ->0x589AAD
        default: return 0;  // ->0x5898E2
    }
}

// gilde.exe 0x589be4 — VIBE_BuildingType_MapToProfessionCode
u8 BuildingType_MapToProfessionCode(u8 code) {
    u8 group = BuildingType_GroupFromCode(code);
    if (!group)
        return group;
    switch (group) {
        case 1:  return 41;
        case 2:  return 47;
        case 3:  return 50;
        case 4:  return 53;
        case 5:  return 33;
        case 6:  return 23; // ->0x589A3B
        case 7:  return 20; // ->0x589AB3
        case 8:  return 26; // ->0x589A44
        case 9:  return 14; // ->0x589AAA
        case 10: return 44;
        case 11: return 11;
        case 12: return 39;
        default: return 0;  // ->0x5898E2
    }
}

// gilde.exe 0x58a25c — VIBE_BuildingType_MapActionToCategory
u8 BuildingType_MapActionToCategory(u8 action) {
    switch (action) {
        case 4:  return 11;
        case 5:  return 9;  // ->0x589AA7
        case 7:  return 7;  // ->0x589A38
        case 8:  return 6;  // ->0x589C74
        case 9:  return 8;  // ->0x589AA4
        case 14: return 5;  // ->0x589AA1
        case 16: return 12;
        case 18: return 1;
        case 19: return 10;
        case 20: return 2;  // ->0x589850
        case 21: return 3;  // ->0x589C77
        case 22: return 4;  // ->0x589A35
        default: return 0;  // ->0x5898E2
    }
}

// gilde.exe 0x589cb0 — VIBE_BuildingType_ComputeVariantIndex
//   v3 = rank - 1; per-group base minus v3.
u8 BuildingType_ComputeVariantIndex(u8 group, u8 rank) {
    if (!group)
        return 0;
    int v3 = static_cast<int>(static_cast<i8>(rank)) - 1;  // a2 - 1 (signed)
    switch (group) {
        case 1:  return static_cast<u8>(51 - v3);
        case 2:  return static_cast<u8>(63 - v3);
        case 3:  return static_cast<u8>(69 - v3);
        case 4:  return static_cast<u8>(75 - v3);
        case 5:  return static_cast<u8>(39 - v3);
        case 6:  return static_cast<u8>(24 - v3);
        case 7:  return static_cast<u8>(18 - v3);
        case 8:  return static_cast<u8>(30 - v3);
        case 9:  return static_cast<u8>(12 - v3);
        case 10: return static_cast<u8>(57 - v3);
        case 11: return static_cast<u8>(6  - v3);
        case 12: return static_cast<u8>(45 - v3);
        default: return 0;
    }
}

// gilde.exe 0x589c20 — VIBE_BuildingType_ClassifyByRange  (signed comparisons)
u8 BuildingType_ClassifyByRange(u8 codeByte) {
    i8 code = static_cast<i8>(codeByte);
    if ((code >= 1 && code <= 6) || (code >= 40 && code <= 45))
        return 7;   // VIBE_BuildingType_ReturnCode7
    if (code >= 35 && code <= 39)
        return 2;   // ->0x589850
    if (code >= 46 && code <= 51)
        return 3;
    if (code < 58 || code > 63) {
        if (code >= 64 && code <= 69)
            return 5;   // ->0x589AA1
        return 0;       // ->0x5898E2
    }
    return 4;   // VIBE_BuildingType_ReturnCode4
}

// gilde.exe 0x589818 — VIBE_Building_ClassifyTypeFlag  (unsigned comparisons)
u8 Building_ClassifyTypeFlag(u8 a1) {
    if (a1 >= 0x2Au) {
        if (a1 <= 0x2Au)
            return 0;
        if (a1 < 0x35u) {
            if (a1 < 0x2Eu)
                return 0;
            return (a1 == 52) ? 0 : 2;
        }
        if (a1 < 0x3Au)
            return 0;
        return 2;
    }
    if (a1 < 4u) {
        if (a1 >= 2u)
            return 0;
        return (a1 == 1) ? 0 : 2;
    }
    if (a1 <= 6u)
        return 0;
    if (a1 < 0x28u)
        return 2;
    return 0;
}

// gilde.exe 0x5898cc — VIBE_Building_IsTypeInGroup
u8 Building_IsTypeInGroup(u8 code) {
    switch (code) {
        case 1: case 2: case 3: case 4:
        case 7: case 8: case 9: case 10: case 11: case 12:
        case 15: case 16: case 17: case 18: case 19: case 20:
        case 21: case 22: case 23: case 24:
            return 0;
        default:
            return 2;   // ->0x589850 (`mov al,2; ret`)
    }
}

// ---------------------------------------------------------------------------
// Table-driven KIND predicates.
// ---------------------------------------------------------------------------

// gilde.exe 0x5878b0 — VIBE_Building_MapTypeToCategory (kind -> UI category)
u8 Building_MapKindToCategory(u8 kind) {
    switch (kind) {
        case 1: case 3: case 6: case 0xF:                      return 3;
        case 2:                                                return 6;
        case 4: case 5: case 9:                                return 8;
        case 7:                                                return 4;
        case 8: case 0xE: case 0x12: case 0x14: case 0x15: case 0x16:
                                                               return 1;
        case 0xB: case 0xC: case 0xD:                          return 2;
        case 0x13:                                             return 7;
        case 0x17: case 0x18: case 0x19: case 0x1A:            return 5;
        default:                                               return 0;
    }
}

// gilde.exe 0x587f50 — VIBE_Building_IsStorageType
bool Building_IsStorageKind(u8 kind) {
    return kind == BuildingTypeKind::kStorage;  // == 10
}

// gilde.exe 0x587f80 — VIBE_Building_IsProductionType
bool Building_IsProductionKind(u8 kind) {
    return kind == 11 || kind == 13 || kind == 12 || kind == 16 || kind == 28;
}

}  // namespace guild::sim
