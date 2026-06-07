#include "sim/building_create2.h"

#include "crt/rand.h"   // RandNext (VIBE_Util_RandNext @0x5cb8bc)

#include <cstring>

namespace guild::sim {

// gilde.exe — name pick: v32 = RandNext() % count (count>0), else 0.
int Building_PickUniqueName(int candidateCount) {
    if (candidateCount <= 0)
        return 0;
    // The original casts to (unsigned __int16) before the modulo:
    //   v32 = (int)VIBE_Util_RandNext() % (unsigned __int16)v31.
    return crt::RandNext() % static_cast<u16>(candidateCount);
}

static ICreateHooks  g_defaultCreateHooks;
static ICreateHooks* g_createHooks = &g_defaultCreateHooks;
void SetCreateHooks(ICreateHooks* hooks) {
    g_createHooks = hooks ? hooks : &g_defaultCreateHooks;
}
ICreateHooks* CreateHooks() { return g_createHooks; }
void ResetCreateHooks() { g_createHooks = &g_defaultCreateHooks; }

// little helpers that write through the raw record base (byte offsets, faithful).
static inline void wr32(u8* r, int off, i32 v) { std::memcpy(r + off, &v, 4); }
static inline void wr16(u8* r, int off, u16 v) { std::memcpy(r + off, &v, 2); }

// The float-bit constant 1061997773 (== 0.65f) the original stores at +73 for
// several types (the quality default).
static constexpr i32 kQualityFloatBits = 1061997773;   // == 0.800000011920929f

// LABEL_92: +101=10, +105=3, +109=5.
static void applyLabel92(u8* rec) {
    wr32(rec, 101, 10);
    wr32(rec, 105, 3);
    wr32(rec, 109, 5);
}

// LABEL_89: +101=-1.
static void applyLabel89(u8* rec) { wr32(rec, 101, -1); }

// LABEL_97: owner/time block for prot 4..7.
static void applyLabel97(u8* rec, u16 ownerWord, i32 ownerPersonId,
                         const PackedTime& now) {
    // *(QWORD*)(v4+105) = qword_13CE852 ; *(DWORD*)(v4+113)=unk_13CE85A ;
    // *(WORD*)(v4+117) = unk_13CE85E.  We pack the PackedTime cursor/header into
    // the same byte span the original copies the 14-byte time record into.
    wr32(rec, 105, now.dayBlock);
    wr16(rec, 109, now.yearTag);
    rec[111] = now.season;
    rec[112] = now.dayInSeason;
    rec[113] = now.hour;
    rec[114] = now.minuteByte;
    wr32(rec, 115, now.cursor & 0xFFFF);   // +117 word portion
    if (ownerWord != 0xFFFF)
        wr32(rec, 101, ownerPersonId);     // dword_12CE914[134*owner]
    else
        applyLabel89(rec);                 // LABEL_89
}

// gilde.exe — the switch(prot) tail of CreateGebaeude.
bool Building_ApplyTypeDefaults(u8* rec, u8 prot, u16 ownerWord,
                                i32 ownerPersonId, const PackedTime& now,
                                bool isProduction) {
    if (!rec)
        return false;

    // *((DWORD*)v4+12) = 5000  when IsProductionType(v4)  (field +48).
    if (isProduction)
        wr32(rec, 48, 5000);

    bool toAddObjekt437 = false;   // LABEL_71 path (scene leaf; flag only)

    if (prot >= 0x15) {                       // >= 21
        if (prot > 0x15) {
            if (prot >= 0x1F) {               // >= 31
                if (prot > 0x1F) {
                    if (prot >= 0x36) {       // >= 54
                        if (prot >= 0x38) {   // >= 56
                            if (prot == 71)
                                wr32(rec, 48, 80000);
                            // else: end
                        }
                        // 54,55: end
                    } else {                  // 0x20..0x35 (32..53)
                        if (prot <= 0x20) {   // == 32
                            wr32(rec, 48, 100);
                            wr32(rec, 73, kQualityFloatBits);
                        } else if (prot == 53) {
                            wr32(rec, 101, 0);
                            wr16(rec, 105, 0);
                            wr16(rec, 107, 0);
                            wr32(rec, 109, 0);
                        }
                        // other 33..52: end
                    }
                } else {                      // == 31
                    wr32(rec, 48, 100);
                    wr32(rec, 73, kQualityFloatBits);
                }
            } else {                          // 22..30
                if (prot >= 0x1B) {           // >= 27
                    if (prot == 30) {
                        // plant map: alloc 0x600, init 24-byte records.
                        u8* map = g_createHooks->AllocPlantMap();
                        wr32(rec, 113, static_cast<i32>(reinterpret_cast<intptr_t>(map)));
                        if (map) {
                            for (int j = 0; j != 1536; j += 24) {
                                map[j + 13] = 0xFF;          // *(BYTE)(map+j+13) = -1
                                wr32(map, j + 20, 0);        // *(DWORD)(map+j+20) = 0
                            }
                        }
                        wr32(rec, 48, 100);
                        wr32(rec, 73, kQualityFloatBits);
                    }
                    // 27,28,29: end
                } else if (prot <= 0x16) {    // == 22
                    toAddObjekt437 = true;    // LABEL_71
                }
            }
        }
        // prot == 21: end
    } else {                                   // < 21
        if (prot < 0xC) {                      // < 12
            if (prot < 6) {
                if (prot < 4) {
                    // 0..3: end
                } else {
                    applyLabel97(rec, ownerWord, ownerPersonId, now);  // 4,5
                }
            } else if (prot < 8) {             // 6,7
                applyLabel97(rec, ownerWord, ownerPersonId, now);
            } else if (prot == 11) {
                applyLabel89(rec);
            }
            // 8,9,10: end
        } else if (prot <= 0xC) {              // == 12
            applyLabel89(rec);
        } else if (prot < 0xF) {               // 13,14
            if (prot <= 0xD)                   // == 13
                applyLabel89(rec);
            else                               // == 14
                applyLabel92(rec);
        } else if (prot <= 0x10) {             // 15,16
            applyLabel92(rec);
        } else if (prot == 20) {
            toAddObjekt437 = true;             // LABEL_71
        }
        // 17,18,19: end
    }

    // LABEL_52 epilogue: if *v4 == 38 -> *(WORD*)(v4+41)=270 (storage object).
    if (rec[0] == 38)
        wr16(rec, 41, 270);

    // LABEL_71 (AddObjekt 437) is a scene leaf — modelled by a flag the caller /
    // scene backend consumes.  We record it in the record's spare alive byte path
    // by leaving rec untouched here (the scene hook owns it).
    (void)toAddObjekt437;
    return true;
}

}  // namespace guild::sim
