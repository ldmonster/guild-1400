// e2e: full scripted sessions asserting the build + dispatch + cleanup call ORDER
// (recorded via the result trace), deterministically. No real assets used.
#include "test.h"

#include "gui/mission_load_run.h"

#include <cstring>
#include <string>

using namespace guild::gui;

namespace {

struct OrderHooks : MissionDialogHooks {
    int slot = 0;
    int checked[64] = {0};
    int frame = 0, totalFrames = 0;
    int confirmFrame = -1;       // the frame to hover v15 with a checked row

    int  FormGetChildObjectId(int) override { return slot++; }
    void SetRowChecked(int s, int v) override { if (s>=0&&s<64) checked[s]=v; }
    int  GetRowChecked(int s) override { return (s>=0&&s<64)?checked[s]:0; }
    int  RunFrameLoop() override { return frame < totalFrames ? (++frame, 1) : 0; }
    // v15 is slot 5 (rows 0..4 -> 0..4). Confirm on the chosen frame.
    int  HoverId(int f) override { return f == confirmFrame ? 5 : -1; }
    int  DialogResult(int f) override { return f == confirmFrame ? 0 : -1; }
};

bool TraceHas(const MissionDialogResult& r, const char* tag, int afterIdx) {
    for (int i = afterIdx; i < r.traceCount; ++i)
        if (std::strcmp(r.trace[i], tag) == 0) return true;
    return false;
}
int TraceIdx(const MissionDialogResult& r, const char* tag) {
    for (int i = 0; i < r.traceCount; ++i)
        if (std::strcmp(r.trace[i], tag) == 0) return i;
    return -1;
}

} // namespace

TEST(MissionDialogE2E, FullSession_BuildDispatchCleanupOrder) {
    OrderHooks h;
    h.totalFrames = 3;
    h.confirmFrame = 1;          // confirm on the 2nd frame (0-based: frame index 1)

    int seed[5] = {0,1,0,0,0};   // row 1 pre-checked
    MissionDialogResult res;
    MissionDialogHooks* prev = Menu_SetMissionDialogHooks(&h);
    int r = Menu_BuildChooseMissionDialog(seed, res, /*maxFrames*/ 8);
    Menu_SetMissionDialogHooks(prev);

    CHECK_EQ(r, 1);
    CHECK_EQ(res.selectedCount, 1);
    CHECK_EQ(res.selection[1], 1);

    // Ordered trace: form.load -> title -> rows.built -> extras.built -> confirm.clicked
    // -> form.destroy.
    int iLoad    = TraceIdx(res, "form.load");
    int iTitle   = TraceIdx(res, "title");
    int iRows    = TraceIdx(res, "rows.built");
    int iExtras  = TraceIdx(res, "extras.built");
    int iConfirm = TraceIdx(res, "confirm.clicked");
    int iDestroy = TraceIdx(res, "form.destroy");

    CHECK(iLoad >= 0);
    CHECK(iLoad < iTitle);
    CHECK(iTitle < iRows);
    CHECK(iRows < iExtras);
    CHECK(iExtras < iConfirm);
    CHECK(iConfirm < iDestroy);
    CHECK_EQ(iDestroy, res.traceCount - 1); // destroy is last

    // Menu-side contract present.
    CHECK(res.impliesMissionFlags);
    CHECK_EQ(res.word_63C740, 137);
    CHECK_EQ(res.byte_63CC1D, 1);
    CHECK_EQ(res.dword_631614, 1);
    (void)TraceHas;
}

TEST(MissionDialogE2E, FullSession_NoConfirm_DestroysCleanlyNoFlags) {
    OrderHooks h;
    h.totalFrames = 2;
    h.confirmFrame = -1;         // never confirm

    int seed[5] = {0,0,0,0,0};
    MissionDialogResult res;
    MissionDialogHooks* prev = Menu_SetMissionDialogHooks(&h);
    int r = Menu_BuildChooseMissionDialog(seed, res, 8);
    Menu_SetMissionDialogHooks(prev);

    CHECK_EQ(r, 0);
    CHECK(!res.impliesMissionFlags);
    CHECK(TraceIdx(res, "confirm.clicked") < 0); // never fired
    CHECK_EQ(TraceIdx(res, "form.destroy"), res.traceCount - 1);
    CHECK_EQ(res.frames, 2);
}

// e2e Map_LoadCityFile: full ordered leaf sequence for a .CTY load.
TEST(MissionDialogE2E, LoadCity_FullCallOrder) {
    struct AllHooks : MapLoadCityHooks {};
    AllHooks h;
    MapLoadCityHooks* prev = Map_SetLoadCityHooks(&h);

    MapLoadCityRecord rec;
    Map_LoadCityFile(0, "Hamburg", &rec);
    Map_SetLoadCityHooks(prev);

    const char* want[] = {
        "universe.reset.pre", "scene.entercity", "fmt.cty", "save.writegamefile",
        "world.reset", "universe.reset.post", "scene.loadfromstream",
    };
    CHECK_EQ(rec.traceCount, (int)(sizeof(want)/sizeof(want[0])));
    for (int i = 0; i < rec.traceCount; ++i)
        CHECK_EQ(std::string(rec.trace[i]), std::string(want[i]));
    CHECK_EQ(std::string(rec.path), std::string("gamedata/cities/Hamburg.CTY"));
}

// Determinism: two identical e2e runs produce identical traces + results.
TEST(MissionDialogE2E, Deterministic) {
    auto run = [](MissionDialogResult& res) {
        OrderHooks h; h.totalFrames = 3; h.confirmFrame = 1;
        int seed[5] = {1,0,0,1,0};
        MissionDialogHooks* prev = Menu_SetMissionDialogHooks(&h);
        int r = Menu_BuildChooseMissionDialog(seed, res, 8);
        Menu_SetMissionDialogHooks(prev);
        return r;
    };
    MissionDialogResult a, b;
    int ra = run(a), rb = run(b);
    CHECK_EQ(ra, rb);
    CHECK_EQ(a.traceCount, b.traceCount);
    for (int i = 0; i < a.traceCount && i < b.traceCount; ++i)
        CHECK_EQ(std::string(a.trace[i]), std::string(b.trace[i]));
    CHECK_EQ(a.selectedCount, b.selectedCount);
}
