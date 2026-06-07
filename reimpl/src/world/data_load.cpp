#include "world/data_load.h"

#include <cstdio>
#include <cstring>

#include "io/file.h"                    // guild::io::FileOpen/Read/Close (loose backend)
#include "sim/building.h"               // g_buildingTypes / g_buildingTypesLoaded
#include "sim/building_types.h"         // BuildingTypeDef (589B), BuildingTypeField
#include "sim/building_production.h"    // g_sceneTypes (65B) / g_sceneTypeRemap / loaded flag
#include "world/world_setup.h"          // g_world*Base / g_worldLoaded (reused globals)

namespace guild::world {

using guild::sim::g_buildingTypes;
using guild::sim::g_buildingTypesLoaded;
using guild::sim::g_sceneTypes;
using guild::sim::g_sceneTypesLoaded;
using guild::sim::g_sceneTypeRemap;
using guild::sim::kSceneTypeRemap;
using guild::sim::kBuildingTypeStride;   // 589
using guild::sim::kSceneTypeStride;      // 65

// ===========================================================================
// Module-private derived globals (gilde.exe BSS at 0x13CExxx).
//   byte_13CEB3C[25] — kind->worth fallback table (seeded by Init).
//   byte_13CEB3D     — == &byte_13CEB3C[1] (the index used in the room loop /
//                      final fallback is into this +1 view).
//   byte_13CE862[731]— per-object-type remap result. The shared consumer table
//                      sim::g_sceneTypeRemap is only 256 wide (it bounds-checks
//                      prot<256), so we keep the full 731-entry result here and
//                      mirror the low 256 into g_sceneTypeRemap after building it.
// ===========================================================================
namespace {
u8 g_kindWorth[25];                       // byte_13CEB3C
u8 g_typeRemap[kSceneTypeLoadCount];      // byte_13CE862[731]

// byte access into the building-type table by absolute byte offset (the original
// reaches every field off the raw `char*` base dword_13CE294).
inline u8* GebBase() { return reinterpret_cast<u8*>(g_buildingTypes); }
inline u8  GebByte(int byteOff) { return GebBase()[byteOff]; }
inline u16 GebRoom(int recByteOff, int slot) {
    // room list is the u16 array at record +35.
    u16 v;
    std::memcpy(&v, GebBase() + recByteOff + 35 + 2 * slot, 2);
    return v;
}
// byte access into the scene/object-type table.
inline u8 ObjByte(int objType, int field) {
    return reinterpret_cast<const u8*>(g_sceneTypes)[kSceneTypeStride * objType + field];
}
} // namespace

// ===========================================================================
// gilde.exe 0x5833b4 — VIBE_World_InitBuildingTypeTable.
// Builds byte_13CE862[731] (per-object-type remap) by walking the 72 building
// records grouped by kind (record +0 byte) and, for each room in a building's
// room list (u16 array @ +35, active count @ +34), interpolating a record index
// and recording the MINIMUM (the threshold compare keeps the smaller value).
// Records left untouched (still 72) fall back to byte_13CEB3D[objType.subtype].
// ===========================================================================
int WorldInitBuildingTypeTable() {
    // 1. zero kindWorth[1..24] (index 0 left as-is, matching the inc-first loop).
    for (int i = 1; i < 25; ++i)
        g_kindWorth[i] = 0;
    // 2. seed the specific kind->worth entries (byte_13CEB43.. == kindWorth[7..17]).
    g_kindWorth[7]  = 41;  // byte_13CEB43
    g_kindWorth[8]  = 50;  // byte_13CEB44
    g_kindWorth[9]  = 47;  // byte_13CEB45
    g_kindWorth[10] = 20;  // byte_13CEB46
    g_kindWorth[11] = 33;  // byte_13CEB47
    g_kindWorth[12] = 23;  // byte_13CEB48
    g_kindWorth[13] = 53;  // byte_13CEB49
    g_kindWorth[15] = 30;  // byte_13CEB4B
    g_kindWorth[16] = 31;  // byte_13CEB4C
    g_kindWorth[17] = 32;  // byte_13CEB4D
    // byte_13CEB3D[i] == g_kindWorth[i + 1].
    const u8* kindWorthD = g_kindWorth + 1;

    // 3. seed byte_13CE862[0..730] = 72 (the "untouched" sentinel == record count).
    for (int i = 0; i < kSceneTypeLoadCount; ++i)
        g_typeRemap[i] = 72;

    // 4. main propagation loop over the 72 building records.
    u8  curKind   = 72;   // struct[4] (initial v18[4] = 72)
    int groupPos  = 0;    // *(dword)v18 — position within current kind group
    int groupSize = 0;    // v13 — size of current kind group
    int groupStart = 0;   // v17 — running record index
    int byteOff   = 0;    // v12 — record byte offset into the building table

    for (groupStart = 0; groupStart < 72; ++groupStart) {
        const u8 kind = GebByte(byteOff);          // v3 = record kind
        if (kind == curKind) {
            ++groupPos;
        } else {
            curKind  = kind;
            groupPos = 0;
            groupSize = 0;
            for (int v4 = groupStart; v4 < 72; ++v4) {
                if (GebByte(kBuildingTypeStride * v4) != kind)
                    break;
                ++groupSize;
            }
        }

        // room-list propagation: iterate the building's active rooms (+34 count).
        const int activeRooms = GebByte(byteOff + 34);
        for (int s = 0; s < activeRooms; ++s) {
            const u16 room = GebRoom(byteOff, s);
            if (room == 0xFFFF)                    // v7 == -1 -> skip
                continue;
            const int objType = room & 0x7FFF;     // v16 strip present-bit

            // v14 = byte_13CEB3D[objType.subtype]  (a building-record index)
            const int v14 = kindWorthD[ObjByte(objType, 33)];
            // j = run length of same-kind records starting at v14.
            int v10 = v14, j = 0;
            for (; v10 < 72; ++j) {
                if (GebByte(kBuildingTypeStride * v10) != GebByte(kBuildingTypeStride * v14))
                    break;
                ++v10;
            }
            // interpolate a record index and keep the minimum.
            const int v15 = groupPos * j / groupSize + v14;
            if (g_typeRemap[objType] > v15)
                g_typeRemap[objType] = static_cast<u8>(v15);
        }

        byteOff += kBuildingTypeStride;            // v12 += 589
    }

    // 5. fill any object-type still at 72 with its subtype's kind->worth fallback.
    for (int i = 0; i < kSceneTypeLoadCount; ++i) {
        if (g_typeRemap[i] == 72)
            g_typeRemap[i] = kindWorthD[ObjByte(i, 33)];
    }

    // mirror the low part into the shared 256-wide consumer table.
    for (int i = 0; i < kSceneTypeRemap && i < kSceneTypeLoadCount; ++i)
        g_sceneTypeRemap[i] = g_typeRemap[i];

    return kSceneTypeLoadCount;   // result == 731
}

// Read-access for tests: the full 731-entry remap result (byte_13CE862).
u8 WorldTypeRemapAt(int objType) {
    if (objType < 0 || objType >= kSceneTypeLoadCount)
        return 0;
    return g_typeRemap[objType];
}

// ===========================================================================
// gilde.exe 0x5835f8 — VIBE_World_LoadBuildingAndObjectData.
// (The production-slot pre-seed loop and the render allocs in the original are
// owned by other modules / are render leaves; this port does the DATA path:
// raw-read both .dat files into the type tables, run the room-count fixup, then
// InitBuildingTypeTable.)
// ===========================================================================
namespace {
// fread one fixed-stride blob via the loose-file backend, exactly as
// VIBE_File_OpenStream + VIBE_File_Read(stream, dst, recSize, count).
bool ReadDatBlob(guild::shim::IFileSystem* fs, const char* dir, const char* name,
                 void* dst, int recSize, int count) {
    char path[512];
    std::snprintf(path, sizeof(path), "%s%s", dir ? dir : "", name);
    guild::io::LooseFile* lf = guild::io::FileOpen(fs, path, "rb");
    if (!lf)
        return false;
    std::size_t got = guild::io::FileRead(lf, dst, static_cast<std::size_t>(recSize),
                                          static_cast<std::size_t>(count));
    guild::io::FileClose(lf);
    return got == static_cast<std::size_t>(recSize) * static_cast<std::size_t>(count);
}
} // namespace

int WorldLoadBuildingAndObjectData(guild::shim::IFileSystem* fs, const char* dir) {
    // Allocate == use the fixed sim tables; zero the loaded spans first.
    std::memset(g_buildingTypes, 0,
                static_cast<std::size_t>(kBuildingTypeStride) * kBuildingTypeLoadCount);
    std::memset(g_sceneTypes, 0,
                static_cast<std::size_t>(kSceneTypeStride) * kSceneTypeLoadCount);

    // VIBE_File_Read(stream, dword_13CE294, 0x24D, 72)  — A_Geb.dat.
    if (!ReadDatBlob(fs, dir, "A_Geb.dat", g_buildingTypes,
                     kBuildingTypeStride, kBuildingTypeLoadCount))
        return -3;
    // VIBE_File_Read(stream, dword_13CE27C, 0x41, 731) — A_Obj.dat.
    if (!ReadDatBlob(fs, dir, "A_Obj.dat", g_sceneTypes,
                     kSceneTypeStride, kSceneTypeLoadCount))
        return -6;

    g_buildingTypesLoaded = true;
    g_sceneTypesLoaded    = true;

    // VIBE_World_InitBuildingTypeTable();
    WorldInitBuildingTypeTable();

    // Post-read fixup: per building record, count the "qualifying" rooms and ADD
    // that to the building's +33 byte. A room is the u16 at rec+35+2k; the test
    // looks at it both as a value and via its HIGH byte (rec+36+2k):
    //   for v19 in [0,42408) step 589:
    //     for v14 = rec, v14 += 2, while v14 != rec+128:   (64 room words)
    //       v15 = *(WORD*)(v14+35); if v15==-1 skip; HIBYTE(v15)&=~0x80;
    //       if objType[65*v15].kind==2 && v15!=253 && *(char*)(v14+36)<0: ++cnt
    //     rec[33] += cnt;
    // *(char*)(v14+36) is the HIGH byte of the SAME room word -> "present"/bit15.
    u8* geb = GebBase();
    for (int v19 = 0; v19 != kBuildingTypeStride * kBuildingTypeLoadCount;
         v19 += kBuildingTypeStride) {
        u8 cnt = 0;
        for (int k = 0; k < 64; ++k) {
            u16 raw;
            std::memcpy(&raw, geb + v19 + 35 + 2 * k, 2);
            if (raw == 0xFFFF)
                continue;
            const bool present = static_cast<signed char>(raw >> 8) < 0; // bit15
            const u16 objType = static_cast<u16>(raw & 0x7FFF);          // clear bit15
            if (ObjByte(objType, 0) == 2 && objType != 253 && present)
                ++cnt;
        }
        geb[v19 + 33] = static_cast<u8>(geb[v19 + 33] + cnt);
    }

    // mark the world data loaded (the bootstrap globals point at the tables).
    g_worldBuildingBase = g_buildingTypes;   // dword_13CE294
    g_worldTypeBase     = g_sceneTypes;      // dword_13CE27C
    g_worldLoaded       = true;
    return 0;
}

} // namespace guild::world
