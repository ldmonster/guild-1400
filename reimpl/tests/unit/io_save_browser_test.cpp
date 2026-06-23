// Unit tests for guild::io save-browser enumeration/slot-map + VFS tree additions
// (ResolveAndBuildPath, MatchPathPrefix, WalkFileTree, GetFileNameCmp) and the
// save-time person/object relink passes.
#include "test.h"

#include "io/save_browser.h"
#include "io/save_relink.h"
#include "io/vfs_tree.h"
#include "shim/IFileSystem.h"

#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace guild::io;

// ---------------------------------------------------------------------------
// Compact in-memory mock FS (directory listing only — enough to build the tree).
namespace {

struct BrowserMockListing : guild::shim::IDirListing {
    std::vector<std::string> names;
    std::vector<guild::shim::DirEntry> entries;
    std::size_t count() const override { return entries.size(); }
    const guild::shim::DirEntry& at(std::size_t i) const override { return entries[i]; }
};

struct BrowserMockFS : guild::shim::IFileSystem {
    struct Child { std::string name; bool isDir; guild::u32 t; };
    std::map<std::string, std::vector<Child>> dirs;

    void addDir(const std::string& p) { if (!dirs.count(p)) dirs[p] = {}; }
    void addFile(const std::string& dir, const std::string& leaf, guild::u32 t) {
        addDir(dir);
        dirs[dir].push_back({leaf, false, t});
    }
    void addSubdir(const std::string& dir, const std::string& leaf) {
        addDir(dir);
        dirs[dir].push_back({leaf, true, 0});
        addDir(dir + "/" + leaf);
    }

    guild::shim::IFile* open(const char*, const char*) override { return nullptr; }
    void close(guild::shim::IFile* f) override { delete f; }
    bool exists(const char*) override { return false; }
    guild::shim::IDirListing* listDir(const char* path) override {
        auto it = dirs.find(path);
        if (it == dirs.end()) return nullptr;
        BrowserMockListing* l = new BrowserMockListing();
        for (auto& c : it->second) l->names.push_back(c.name);
        for (std::size_t i = 0; i < it->second.size(); ++i) {
            guild::shim::DirEntry e;
            e.name = l->names[i].c_str();
            e.isDir = it->second[i].isDir;
            e.dosTime = it->second[i].t;
            l->entries.push_back(e);
        }
        return l;
    }
    bool makeDir(const char* path) override { addDir(path); return true; }
};

// Build a tree with a SAVES/ dir holding .SAV + non-.SAV files.
BrowserMockFS MakeSaveTree() {
    BrowserMockFS fs;
    fs.addDir("ROOT");
    fs.addSubdir("ROOT", "SAVES");
    fs.addFile("ROOT/SAVES", "GAME1.SAV", 10);
    fs.addFile("ROOT/SAVES", "GAME2.SAV", 20);
    fs.addFile("ROOT/SAVES", "QUICKSAVE.SAV", 30);
    fs.addFile("ROOT/SAVES", "NOTES.TXT", 40);   // filtered out
    return fs;
}

// The SAVES directory node (after VfsTreeInit), which owns the loose save files.
VfsNode* SavesDir() { return NormalizeDirPath("SAVES/", VfsRoot()); }

} // namespace

// ---------------------------------------------------------------------------
// EnumerateSaveFiles: only .SAV files emitted; name+path fields filled.
TEST(IoSaveBrowserEnum, FiltersByExtension) {
    BrowserMockFS fs = MakeSaveTree();
    CHECK(VfsTreeInit(&fs, "ROOT", false));   // case-sensitive (upper-cased) mode

    SaveBrowserRecord recs[16];
    std::memset(recs, 0, sizeof(recs));
    int n = SaveBrowserEnumerateSaveFiles("SAVES/", VfsRoot(), kSaveExt, recs);
    // 3 .SAV files (GAME1, GAME2, QUICKSAVE), NOTES.TXT excluded.
    CHECK_EQ(n, 3);

    // Records are emitted in the sorted order of the file array (GAME1, GAME2, QUICKSAVE).
    // Binary-exact (@0x569530): the NAME field (record+9) is the file name TRUNCATED at
    // its '.' (StrChr @0x5d3ef0 + *v14=0), so the extension is dropped from the name.
    CHECK(std::strcmp(recs[0].name, "GAME1") == 0);
    CHECK(std::strcmp(recs[1].name, "GAME2") == 0);
    CHECK(std::strcmp(recs[2].name, "QUICKSAVE") == 0);
    // The full-path field (record+265) keeps the extension: it is the verbatim
    // Sprintf("%s/%s", basePath, name) and is NOT truncated by the original.
    CHECK(std::strcmp(recs[0].fullPath, "SAVES//GAME1.SAV") == 0);
    CHECK(std::strcmp(recs[2].fullPath, "SAVES//QUICKSAVE.SAV") == 0);

    VfsTreeShutdown();
}

// maxRecords fail-safe: 3 matching files but a cap of 2 -> only 2 emitted, the
// 3rd slot is never written (would be OOB on a tight caller buffer).
TEST(IoSaveBrowserEnum, MaxRecordsBoundsTheEmit) {
    BrowserMockFS fs = MakeSaveTree();
    CHECK(VfsTreeInit(&fs, "ROOT", false));
    SaveBrowserRecord recs[2];
    std::memset(recs, 0, sizeof(recs));
    int n = SaveBrowserEnumerateSaveFiles("SAVES/", VfsRoot(), kSaveExt, recs, 2);
    CHECK_EQ(n, 2);                                   // clamped to the cap
    CHECK(std::strcmp(recs[0].name, "GAME1") == 0);   // NAME truncated at '.'
    CHECK(std::strcmp(recs[1].name, "GAME2") == 0);
    VfsTreeShutdown();
}

TEST(IoSaveBrowserEnum, UnknownPathReturnsZero) {
    BrowserMockFS fs = MakeSaveTree();
    CHECK(VfsTreeInit(&fs, "ROOT", false));
    SaveBrowserRecord recs[4];
    CHECK_EQ(SaveBrowserEnumerateSaveFiles("NOPE/", VfsRoot(), kSaveExt, recs), 0);
    VfsTreeShutdown();
}

// ---------------------------------------------------------------------------
// FindSaveSlot: reserved-name routing + occupancy guard + record copy.
namespace {
// Build an empty slot table: every slot's id (+8) initialised to -1 (free).
struct SlotTable {
    std::vector<guild::u8> bytes;
    explicit SlotTable(int slots) : bytes(static_cast<std::size_t>(slots) * kSlotStride, 0) {
        for (int i = 0; i < slots; ++i) {
            guild::u32 free = 0xFFFFFFFFu;
            std::memcpy(&bytes[i * kSlotStride + kSlotIdOff], &free, 4);
        }
    }
    guild::u8* base() { return bytes.data(); }
    guild::u32 idAt(int slot) {
        guild::u32 v; std::memcpy(&v, &bytes[slot * kSlotStride + kSlotIdOff], 4); return v;
    }
};

SaveBrowserRecord MakeRecord(const char* name) {
    SaveBrowserRecord r;
    std::memset(&r, 0, sizeof(r));
    std::strncpy(r.name, name, sizeof(r.name) - 1);
    return r;
}
} // namespace

TEST(IoSaveBrowserSlot, QuickSaveForcesSlotOne) {
    // The original sets a3 = 1 for QUICKSAVE (slot 1), regardless of preferredSlot.
    SlotTable t(4);
    SaveBrowserRecord r = MakeRecord("QUICKSAVE");
    int s = SaveBrowserFindSaveSlot(t.base(), &r, 2);
    CHECK_EQ(s, 1);
    // record copied to slot 1 + 16, marker stamped at slot 1 + 0.
    CHECK_EQ(t.base()[kSlotStride + kSlotMarkerOff], 1u);
    CHECK(std::strcmp(reinterpret_cast<char*>(t.base() + kSlotStride + kSlotRecordOff + 9),
                      "QUICKSAVE") == 0);
}

TEST(IoSaveBrowserSlot, AutoSaveForcesSlotZero) {
    // The original forces a3 = 0 for AUTOSAVE (slot 0).
    SlotTable t(4);
    SaveBrowserRecord r = MakeRecord("AUTOSAVE");
    int s = SaveBrowserFindSaveSlot(t.base(), &r, 3);
    CHECK_EQ(s, 0);
    CHECK_EQ(t.base()[kSlotMarkerOff], 0u);
}

TEST(IoSaveBrowserSlot, NormalUsesPreferredSlot) {
    SlotTable t(4);
    SaveBrowserRecord r = MakeRecord("GAME1.SAV");
    int s = SaveBrowserFindSaveSlot(t.base(), &r, 2);
    CHECK_EQ(s, 2);
    CHECK_EQ(t.base()[2 * kSlotStride + kSlotMarkerOff], 2u);
    CHECK(std::strcmp(reinterpret_cast<char*>(t.base() + 2 * kSlotStride + kSlotRecordOff + 9),
                      "GAME1.SAV") == 0);
}

TEST(IoSaveBrowserSlot, OccupiedSlotRejected) {
    SlotTable t(4);
    // occupy slot 2 (id != -1)
    guild::u32 occ = 7;
    std::memcpy(&t.bytes[2 * kSlotStride + kSlotIdOff], &occ, 4);
    SaveBrowserRecord r = MakeRecord("GAME1.SAV");
    CHECK_EQ(SaveBrowserFindSaveSlot(t.base(), &r, 2), -1);
}

TEST(IoSaveBrowserSlot, PreferredZeroNonReservedRejected) {
    // preferredSlot 0 for a non-reserved name: original takes the "a3 -> LABEL_3"
    // branch (a3==1 path not taken; falls to the QUICKSAVE re-check) and returns -1
    // because the name is not QUICKSAVE.
    SlotTable t(4);
    SaveBrowserRecord r = MakeRecord("GAME1.SAV");
    CHECK_EQ(SaveBrowserFindSaveSlot(t.base(), &r, 0), -1);
}

// ---------------------------------------------------------------------------
// GetFileNameCmp comparator (VIBE_Vfs_GetFileName_Thunk).
TEST(IoVfsTreeExtras, GetFileNameCmp) {
    VfsFileEntry e;
    char nm[] = "FOO.SAV";
    e.name = nm;
    CHECK_EQ(GetFileNameCmp("FOO.SAV", &e), 0);
    CHECK(GetFileNameCmp("AAA", &e) < 0);
    CHECK(GetFileNameCmp("ZZZ", &e) > 0);
}

// ---------------------------------------------------------------------------
// WalkFileTree: visits every file, filtered + unfiltered, abort on false.
namespace {
struct WalkCtx {
    std::vector<std::string> seen;
    int abortAfter = -1;
};
bool WalkCollect(const VfsFileEntry* e, void* ctx) {
    WalkCtx* w = static_cast<WalkCtx*>(ctx);
    if (e) w->seen.push_back(e->name);
    if (w->abortAfter >= 0 && static_cast<int>(w->seen.size()) >= w->abortAfter)
        return false;
    return true;
}
} // namespace

TEST(IoVfsTreeExtras, WalkFileTreeUnfiltered) {
    BrowserMockFS fs = MakeSaveTree();
    CHECK(VfsTreeInit(&fs, "ROOT", false));
    WalkCtx w;
    bool ok = WalkFileTree(VfsRoot(), true, &WalkCollect, nullptr, &w);
    CHECK(ok);
    // root has no loose files; SAVES has 4 files (3 .SAV + NOTES.TXT).
    CHECK_EQ(static_cast<int>(w.seen.size()), 4);
    VfsTreeShutdown();
}

TEST(IoVfsTreeExtras, WalkFileTreeFiltered) {
    BrowserMockFS fs = MakeSaveTree();
    CHECK(VfsTreeInit(&fs, "ROOT", false));
    WalkCtx w;
    // bare extension "SAV" -> only the three .SAV files.
    bool ok = WalkFileTree(VfsRoot(), true, &WalkCollect, "SAV", &w);
    CHECK(ok);
    CHECK_EQ(static_cast<int>(w.seen.size()), 3);
    VfsTreeShutdown();
}

TEST(IoVfsTreeExtras, WalkFileTreeAbort) {
    BrowserMockFS fs = MakeSaveTree();
    CHECK(VfsTreeInit(&fs, "ROOT", false));
    // Abort must propagate from the TOP frame's own file loop, so start the walk
    // directly at the SAVES dir node (which owns the loose files). When the abort
    // happens inside a *recursive child* walk the original discards that return,
    // so it would NOT propagate — that subtlety is covered below.
    WalkCtx w; w.abortAfter = 2;
    bool ok = WalkFileTree(SavesDir(), false, &WalkCollect, nullptr, &w);
    CHECK(!ok);   // visitor aborted in the top frame -> false
    CHECK_EQ(static_cast<int>(w.seen.size()), 2);
    VfsTreeShutdown();
}

TEST(IoVfsTreeExtras, WalkFileTreeChildAbortNotPropagated) {
    // Faithful subtlety: a child-walk abort is discarded by the parent frame, so
    // the overall walk still reports success. Starting at the root (which owns no
    // loose files), the abort fires inside the SAVES child walk and is swallowed.
    BrowserMockFS fs = MakeSaveTree();
    CHECK(VfsTreeInit(&fs, "ROOT", false));
    WalkCtx w; w.abortAfter = 2;
    bool ok = WalkFileTree(VfsRoot(), true, &WalkCollect, nullptr, &w);
    CHECK(ok);    // child abort NOT propagated -> top frame returns true
    CHECK_EQ(static_cast<int>(w.seen.size()), 2);
    VfsTreeShutdown();
}

TEST(IoVfsTreeExtras, WalkFileTreeNullCallback) {
    CHECK(!WalkFileTree(nullptr, true, nullptr, nullptr, nullptr));
}

// ---------------------------------------------------------------------------
// ResolveAndBuildPath wrapper.
TEST(IoVfsTreeExtras, ResolveAndBuildPath) {
    BrowserMockFS fs = MakeSaveTree();
    CHECK(VfsTreeInit(&fs, "ROOT", false));
    char out[512];
    char* r = ResolveAndBuildPath("SAVES/GAME1.SAV", VfsRoot(), out, true);
    CHECK(r == out);
    // the resolved path ends with the file name we asked for.
    CHECK(std::strstr(out, "GAME1.SAV") != nullptr);
    // unknown path -> null.
    CHECK(ResolveAndBuildPath("SAVES/NOPE.SAV", VfsRoot(), out, true) == nullptr);
    VfsTreeShutdown();
}

// ---------------------------------------------------------------------------
// MatchPathPrefix: when no root bound, copies normalized path; with a bound root
// it peels matching prefix components.
TEST(IoVfsTreeExtras, MatchPathPrefixNoRoot) {
    // No VfsTreeInit -> g_root null -> whole normalized path copied to out.
    char out[256];
    char* r = MatchPathPrefix("a\\b\\c", nullptr, out);
    CHECK(r == out);
    CHECK(std::strcmp(out, "A/B/C") == 0);   // upper-cased + slashes (case-sensitive mode)
}

// ---------------------------------------------------------------------------
// Relink: object pass writes ids into actor+512; table pass maps pointers->ids.
namespace {
struct RelinkHost {
    // object array: 2 records of 169 bytes.
    guild::u8 objects[169 * 2];
    // actor back-store keyed by token.
    std::map<guild::u32, guild::u32> actorBack;
    int activeSlot = 5;
    int switchLog[8]; int switchN = 0;
    int sceneArg = -999;
};

int HostGetSlot(void* c) { return static_cast<RelinkHost*>(c)->activeSlot; }
void HostSwitch(int s, void* c) {
    RelinkHost* h = static_cast<RelinkHost*>(c);
    if (h->switchN < 8) h->switchLog[h->switchN++] = s;
}
void HostWriteBack(guild::u32 actor, guild::u32 val, void* c) {
    static_cast<RelinkHost*>(c)->actorBack[actor] = val;
}
void HostSaveScene(int arg, void* c) { static_cast<RelinkHost*>(c)->sceneArg = arg; }
guild::u32 HostPtrPerson(guild::u32 p, void*)   { return p + 1000; }
guild::u32 HostPtrBuilding(guild::u32 p, void*) { return p + 2000; }
guild::u32 HostPtrObject(guild::u32 p, void*)   { return p + 3000; }
guild::u32 HostPtrCutscene(guild::u32 p, void*) { return p + 9000; }

void SetObj(guild::u8* base, int idx, guild::u8 alive, guild::u32 id, guild::u32 actor) {
    guild::u8* rec = base + idx * kRelinkObjStride;
    rec[kRelinkObjAliveOff] = alive;
    std::memcpy(rec + kRelinkObjIdOff, &id, 4);
    std::memcpy(rec + kRelinkObjActorOff, &actor, 4);
}

RelinkPersonEnv MakeEnv(RelinkHost& h) {
    RelinkPersonEnv env{};
    env.objectBase = h.objects;
    env.objectCount = 2;
    env.getActiveSlot = &HostGetSlot;
    env.switchActiveSlot = &HostSwitch;
    env.writeActorBack = &HostWriteBack;
    env.saveSceneObjects = &HostSaveScene;
    env.ptrToIdPerson = &HostPtrPerson;
    env.ptrToIdBuilding = &HostPtrBuilding;
    env.ptrToIdObject = &HostPtrObject;
    env.ptrToIdCutscene = &HostPtrCutscene;
    env.ctx = &h;
    return env;
}
} // namespace

TEST(IoSaveRelink, PersonObjectsActorAndTable) {
    RelinkHost h;
    std::memset(h.objects, 0, sizeof(h.objects));
    SetObj(h.objects, 0, 1, 0x111, 100);   // alive, id 0x111, actor token 100
    SetObj(h.objects, 1, 0, 0x222, 200);   // dead -> skipped

    // relink table: 32768*10 bytes, set a few tagged entries.
    std::vector<guild::u8> table(kRelinkBytes, 0);
    auto setEntry = [&](guild::u32 n, guild::u8 tag, guild::u32 ptr) {
        table[n + kRelinkTagOffset] = tag;
        std::memcpy(&table[n + kRelinkPtrOffset], &ptr, 4);
    };
    setEntry(0,  kRelinkPerson,   50);
    setEntry(10, kRelinkBuilding, 60);
    setEntry(20, kRelinkObject,   70);
    setEntry(30, kRelinkCutscene, 80);
    setEntry(40, kRelinkPerson,   0);   // empty -> skipped

    RelinkPersonEnv env = MakeEnv(h);
    SaveRelinkPersonObjects(env, table.data());

    // actor 100 received object 0's id; actor 200 (dead) untouched.
    CHECK_EQ(h.actorBack[100], 0x111u);
    CHECK(h.actorBack.find(200) == h.actorBack.end());
    // active slot bracketed: switched to 0 then back to saved (5).
    CHECK_EQ(h.switchN, 2);
    CHECK_EQ(h.switchLog[0], 0);
    CHECK_EQ(h.switchLog[1], 5);
    // table pointers mapped via tag resolvers.
    guild::u32 v;
    std::memcpy(&v, &table[0  + kRelinkPtrOffset], 4); CHECK_EQ(v, 1050u);
    std::memcpy(&v, &table[10 + kRelinkPtrOffset], 4); CHECK_EQ(v, 2060u);
    std::memcpy(&v, &table[20 + kRelinkPtrOffset], 4); CHECK_EQ(v, 3070u);
    std::memcpy(&v, &table[30 + kRelinkPtrOffset], 4); CHECK_EQ(v, 9080u);
    std::memcpy(&v, &table[40 + kRelinkPtrOffset], 4); CHECK_EQ(v, 0u);
}

TEST(IoSaveRelink, PersonExtraDataBrackets) {
    RelinkHost h;
    std::memset(h.objects, 0, sizeof(h.objects));
    SetObj(h.objects, 0, 1, 0x111, 100);

    RelinkPersonEnv env = MakeEnv(h);
    int rc = SaveRelinkPersonExtraData(env, 42);
    CHECK_EQ(rc, 1);
    CHECK_EQ(h.sceneArg, 42);
    // After the two object passes, actor+512 ends holding the record OFFSET (0).
    CHECK_EQ(h.actorBack[100], 0u);
    CHECK_EQ(h.switchN, 2);
    CHECK_EQ(h.switchLog[0], 0);
    CHECK_EQ(h.switchLog[1], 5);
}
