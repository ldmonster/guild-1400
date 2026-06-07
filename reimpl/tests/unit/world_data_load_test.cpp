// Unit tests for the building/object type-table .dat loaders
// (guild::world::WorldLoadBuildingAndObjectData / WorldInitBuildingTypeTable).
// Drives a synthetic in-memory filesystem holding hand-built A_Geb.dat / A_Obj.dat
// blobs and checks the parsed table records + the derived fixups field-by-field.
#include "test.h"

#include "world/data_load.h"
#include "sim/building.h"             // g_buildingTypes, ResetBuildings
#include "sim/building_types.h"       // BuildingTypeDef
#include "sim/building_production.h"  // g_sceneTypes, g_sceneTypeRemap
#include "shim/IFileSystem.h"

#include <cstring>
#include <string>
#include <vector>

using namespace guild;

namespace {

// ---- a trivial in-memory IFileSystem with two named blobs -------------------
class MemFile : public shim::IFile {
public:
    explicit MemFile(const std::vector<u8>* d) : d_(d) {}
    std::size_t read(void* dst, std::size_t n) override {
        std::size_t avail = d_->size() - pos_;
        std::size_t take = n < avail ? n : avail;
        std::memcpy(dst, d_->data() + pos_, take);
        pos_ += take;
        return take;
    }
    std::size_t write(const void*, std::size_t) override { return 0; }
    std::int64_t seek(std::int64_t off, int whence) override {
        std::int64_t base = whence == 1 ? (std::int64_t)pos_
                          : whence == 2 ? (std::int64_t)d_->size() : 0;
        pos_ = (std::size_t)(base + off);
        return 0;
    }
    std::int64_t tell() override { return (std::int64_t)pos_; }
    std::int64_t size() override { return (std::int64_t)d_->size(); }
private:
    const std::vector<u8>* d_;
    std::size_t pos_ = 0;
};

class MemFS : public shim::IFileSystem {
public:
    std::vector<u8> geb, obj;
    shim::IFile* open(const char* path, const char*) override {
        std::string p(path);
        if (p.find("A_Geb.dat") != std::string::npos) return new MemFile(&geb);
        if (p.find("A_Obj.dat") != std::string::npos) return new MemFile(&obj);
        return nullptr;
    }
    void close(shim::IFile* f) override { delete f; }
    bool exists(const char*) override { return true; }
};

constexpr int GEB = 589;
constexpr int OBJ = 65;

void putName(u8* rec, const char* name) {
    // name lives at record +1 (record +0 is the kind byte).
    std::strcpy(reinterpret_cast<char*>(rec + 1), name);
}
void putRoom(u8* rec, int slot, u16 objType) {
    u16 v = objType;
    std::memcpy(rec + 35 + 2 * slot, &v, 2);
}

// Build the synthetic files. We fill the full loaded extents (72 * 589, 731 * 65)
// so the raw fread succeeds; only the first few records carry interesting data.
MemFS makeSynthetic() {
    MemFS fs;
    fs.geb.assign((std::size_t)GEB * world::kBuildingTypeLoadCount, 0);
    fs.obj.assign((std::size_t)OBJ * world::kSceneTypeLoadCount, 0);

    // ---- A_Obj.dat: object/scene type records (65 bytes) ----
    // obj 0: kind 0, "Null"
    putName(fs.obj.data() + 0 * OBJ, "Null");
    // obj 5: kind 2 (qualifies in the building fixup), name + subtype(+33)=7
    {
        u8* o = fs.obj.data() + 5 * OBJ;
        o[0] = 2;                  // kind == 2
        putName(o, "Wand");
        o[33] = 7;                 // subtype -> kindWorth index
    }
    // obj 9: kind 0, subtype(+33)=8
    {
        u8* o = fs.obj.data() + 9 * OBJ;
        o[0] = 0;
        putName(o, "Tuer");
        o[33] = 8;
    }

    // ---- A_Geb.dat: building type records (589 bytes) ----
    // rec 0: kind 0 "Null"
    putName(fs.geb.data() + 0 * GEB, "Null");
    // rec 1: kind 1 "Haus": +33 base=4, +34 active-rooms=2,
    //        roomList[0] = 5 | 0x8000 (obj kind 2 + "present" bit15 -> qualifies),
    //        roomList[1] = 9          (kind 0, no present bit -> no count)
    {
        u8* g = fs.geb.data() + 1 * GEB;
        g[0] = 1;
        putName(g, "Haus");
        g[33] = 4;
        g[34] = 2;
        putRoom(g, 0, (u16)(5 | 0x8000));
        putRoom(g, 1, 9);
    }
    // rec 2: kind 1 "Villa" (same group as rec1): +34=1,
    //        roomList[0] = 5 (obj kind 2 but NO present bit15 -> no fixup count)
    {
        u8* g = fs.geb.data() + 2 * GEB;
        g[0] = 1;
        putName(g, "Villa");
        g[33] = 9;
        g[34] = 1;
        putRoom(g, 0, 5);
    }
    return fs;
}

} // namespace

TEST(WorldDataLoad, ParsesRecordsRawAndPopulatesNamesAndKinds) {
    MemFS fs = makeSynthetic();
    sim::ResetBuildings();
    int rc = world::WorldLoadBuildingAndObjectData(&fs, "data/");
    CHECK_EQ(rc, 0);

    // building-type table: kind byte @+0, name @+1.
    const u8* geb = reinterpret_cast<const u8*>(sim::g_buildingTypes);
    CHECK_EQ((int)geb[0 * GEB + 0], 0);
    CHECK_EQ((int)geb[1 * GEB + 0], 1);
    CHECK_EQ((int)geb[2 * GEB + 0], 1);
    CHECK(std::strcmp((const char*)geb + 1 * GEB + 1, "Haus") == 0);
    CHECK(std::strcmp((const char*)geb + 2 * GEB + 1, "Villa") == 0);

    // scene/object-type table: kind @+0, name @+1.
    const u8* obj = reinterpret_cast<const u8*>(sim::g_sceneTypes);
    CHECK_EQ((int)obj[5 * OBJ + 0], 2);
    CHECK(std::strcmp((const char*)obj + 5 * OBJ + 1, "Wand") == 0);

    CHECK(sim::g_buildingTypesLoaded);
    CHECK(sim::g_sceneTypesLoaded);
}

TEST(WorldDataLoad, RoomCountFixupAddsToPlus33) {
    MemFS fs = makeSynthetic();
    sim::ResetBuildings();
    CHECK_EQ(world::WorldLoadBuildingAndObjectData(&fs, "data/"), 0);

    const u8* geb = reinterpret_cast<const u8*>(sim::g_buildingTypes);
    // rec 1: base +33 = 4; one qualifying room (obj 5, kind==2, id!=253, flag<0)
    //        -> +33 becomes 4 + 1 = 5.
    CHECK_EQ((int)geb[1 * GEB + 33], 5);
    // rec 2: flag(+36) >= 0 so NO room counts -> +33 stays 9.
    CHECK_EQ((int)geb[2 * GEB + 33], 9);
    // rec 0: no rooms -> +33 stays 0.
    CHECK_EQ((int)geb[0 * GEB + 33], 0);
}

TEST(WorldDataLoad, InitBuildingTypeTablePopulatesRemap) {
    MemFS fs = makeSynthetic();
    sim::ResetBuildings();
    CHECK_EQ(world::WorldLoadBuildingAndObjectData(&fs, "data/"), 0);

    // Every loaded object-type must have a remap value (no longer the 72 sentinel
    // unless the fallback resolved to 72). Untouched types fall back to
    // kindWorth[subtype+1]; obj 9 had subtype 8 -> kindWorth[9] == 47.
    CHECK_EQ((int)world::WorldTypeRemapAt(9), 47);
    // obj with subtype 0 (the vast majority) falls back to kindWorth[1] == 0.
    CHECK_EQ((int)world::WorldTypeRemapAt(100), 0);
    // obj 5 is referenced by building rec1's room list (group of kind-1 buildings),
    // so its remap was computed in the propagation loop (a small record index),
    // not the 72 sentinel.
    CHECK(world::WorldTypeRemapAt(5) < 72);
}

TEST(WorldDataLoad, MissingFileReturnsErrorCode) {
    // FS that has no A_Geb.dat -> open fails -> -3.
    class EmptyFS : public shim::IFileSystem {
    public:
        shim::IFile* open(const char*, const char*) override { return nullptr; }
        void close(shim::IFile* f) override { delete f; }
        bool exists(const char*) override { return false; }
    } efs;
    sim::ResetBuildings();
    CHECK_EQ(world::WorldLoadBuildingAndObjectData(&efs, "data/"), -3);
}
