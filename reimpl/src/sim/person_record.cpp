#include "sim/person_record.h"

#include <cstring>

// Adapter that joins the two distinct arrays the economy demand/supply passes
// read (gilde.exe 0x578438 / 0x578634):
//   v6   = 589 * *j + dword_13CE294;          // type descriptor base
//   need = *(BYTE*)(v6 + 583);
//   unemployed = (*(WORD*)(j + 39) == 0xFFFF);
// Here `*j` is the object record's +0 type byte; `dword_13CE294` is the
// 589-stride type table. See person_record.h for the full evidence trail.

namespace guild::sim {

PersonEcoInputs PersonReadEcoInputs(const ObjectRec* objectRecord,
                                    const BuildingTypeDef* typeTable,
                                    bool typeTableLoaded) {
    PersonEcoInputs out{0, false};

    // *(WORD*)(object + 39): employment / owner word; 0xFFFF == unemployed.
    u16 owner = 0;
    std::memcpy(&owner,
                reinterpret_cast<const u8*>(objectRecord) +
                    person_reconcile::kObjectEmploymentOff,
                sizeof(owner));
    out.unemployed = (owner == person_reconcile::kEmploymentNone);

    // need = typeTable[object.type].security  (typeDef + 583), guarded by the
    // original's null-base bail (dword_13CE294 == 0 => table absent).
    if (typeTableLoaded && typeTable != nullptr) {
        // The type index is the object record's +0 byte (alive/type byte). The
        // 589-stride record's need byte lives at +583 (== BuildingTypeDef::security).
        u8 typeByte = *reinterpret_cast<const u8*>(objectRecord);
        const BuildingTypeDef& def = typeTable[typeByte];
        out.need = def.security;  // +583
    }

    return out;
}

}  // namespace guild::sim
