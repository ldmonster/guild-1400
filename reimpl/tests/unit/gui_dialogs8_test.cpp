// Unit tests for guild::gui gui_dialogs8 — the VIBE_Tooltip_Build{Person,
// PersonDetailed,Object} builders and their GUI-owned deterministic cores.
//
// The pure helpers (title/spouse/talent/guild text-id arithmetic, stat-bar layout,
// the modal gate) are covered with golden vectors computed independently in python.
// The builders themselves are exercised with installed hooks that record what the
// form/text runtime would have been asked to render.
#include "test.h"

#include "gui/gui_dialogs8.h"

#include <cstdint>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::gui;

namespace {

// Lay out a person record with the fields the builders read at their byte offsets.
struct PersonRec {
    u8 bytes[600];
    PersonRec() { std::memset(bytes, 0, sizeof(bytes)); }
    void setId(u16 v) { std::memcpy(bytes + 0, &v, 2); }
    void setGate(std::int8_t v) { bytes[2] = static_cast<u8>(v); }
    void setGender(u8 v) { bytes[9] = v; }
    void setTalent(u8 v) { bytes[13] = v; }
    void setPacked353(std::int32_t v) { std::memcpy(bytes + 353, &v, 4); }
    void setPacked9(std::int32_t v) { std::memcpy(bytes + 9, &v, 4); }
    void setSpouseA(u8 v) { bytes[358] = v; }
    void setSpouseB(u8 v) { bytes[361] = v; }
    const i16* rec() const { return reinterpret_cast<const i16*>(bytes); }
    const u8*  rb()  const { return bytes; }
};

} // namespace

// ---------------------------------------------------------------------------
// Pure helpers — golden vectors.
// ---------------------------------------------------------------------------
TEST(GuiDialogs8, PersonTitleTextId) {
    PersonRec p;
    p.setPacked353(0x05000000); // high byte 5
    p.setGender(1);
    CHECK_EQ(Person_TitleTextId(p.rb()), 375); // 5 + 370
    p.setGender(0);
    CHECK_EQ(Person_TitleTextId(p.rb()), 299); // 5 + 294
    p.setPacked353(static_cast<std::int32_t>(0x80000000u)); // high byte -128 (signed)
    p.setGender(1);
    CHECK_EQ(Person_TitleTextId(p.rb()), 242); // -128 + 370
}

TEST(GuiDialogs8, PersonTalentTextId) {
    PersonRec p;
    p.setTalent(7);
    p.setGender(1);
    CHECK_EQ(Person_TalentTextId(p.rb()), 286); // 7 + 279
    p.setGender(0);
    CHECK_EQ(Person_TalentTextId(p.rb()), 279); // 7 + 272
}

TEST(GuiDialogs8, PersonGuildTextId) {
    PersonRec p;
    p.setPacked9(0x03000000); // high byte 3
    CHECK_EQ(Person_GuildTextId(p.rb()), 1073); // 3 + 1070
}

TEST(GuiDialogs8, PersonSpouseLayout) {
    PersonRec p;
    p.setGender(1);
    p.setSpouseA(10);
    p.setSpouseB(0);
    PersonSpouseLayout s = Person_SpouseLayout(p.rb());
    CHECK(s.hasAny);
    CHECK(s.showA);
    CHECK(!s.showB);
    CHECK_EQ(s.textIdA, 570); // 10 + 560

    PersonRec q;
    q.setGender(0);
    PersonSpouseLayout n = Person_SpouseLayout(q.rb());
    CHECK(!n.hasAny);
    CHECK_EQ(n.noneTextId, 0x20D);
}

TEST(GuiDialogs8, StatBarRows) {
    int expY[5]  = {2, 17, 32, 47, 62};
    int expId[5] = {4810, 4811, 4812, 4813, 4814};
    for (int i = 0; i < 5; ++i) {
        StatBarRow r = Person_StatBarRow(i);
        CHECK_EQ(r.labelTextId, expId[i]);
        CHECK_EQ(r.barY, expY[i]);
        CHECK_EQ(r.width, 100);
        CHECK_EQ(r.kind, 1162);
    }
}

TEST(GuiDialogs8, DetailGate) {
    PersonRec p;
    p.setGate(9);
    CHECK(Person_DetailGateOpen(p.rb()));
    p.setGate(10);
    CHECK(!Person_DetailGateOpen(p.rb()));
    p.setGate(-1); // signed < 10
    CHECK(Person_DetailGateOpen(p.rb()));
}

TEST(GuiDialogs8, NullSafety) {
    CHECK_EQ(Person_TitleTextId(nullptr), 0);
    CHECK_EQ(Person_TalentTextId(nullptr), 0);
    CHECK_EQ(Person_GuildTextId(nullptr), 0);
    CHECK(!Person_DetailGateOpen(nullptr));
    PersonSpouseLayout s = Person_SpouseLayout(nullptr);
    CHECK(!s.hasAny);
}

// ---------------------------------------------------------------------------
// Builder flows with recording hooks.
// ---------------------------------------------------------------------------
namespace {

struct Recorded {
    std::vector<std::pair<unsigned, unsigned>> rich; // (id, arg)
    int cards = 0;
    int bars = 0;
    int labels = 0;
    int frameLoops = 0;
    int destroys = 0;
    bool statusResolved = false;
    int rankInput = -1;
    int rankReturn = 7;
};

Recorded* g_rec = nullptr;

int HText(unsigned id, unsigned arg) { if (g_rec) g_rec->rich.emplace_back(id, arg); return 0; }
int HResolve(void*) { return g_rec && g_rec->statusResolved ? 1 : 0; }
int HCard(int, int, void*) { if (g_rec) ++g_rec->cards; return 0; }
int HRank(u8 c) { if (g_rec) { g_rec->rankInput = c; return g_rec->rankReturn; } return 0; }
int HBar(int, int, int, int, int) { if (g_rec) ++g_rec->bars; return 0; }
int HLabel(int, i16, int, const char*) { if (g_rec) ++g_rec->labels; return 0; }
int HFmt(char* out, const char*, const char*) { if (out) out[0] = '\0'; return 0; }
int HGameTick(i16, i16, const char*) { return 5; } // a valid in-range form id (g_forms[5])
int HFrame(int, int, const void*) { if (g_rec && g_rec->frameLoops < 3) { ++g_rec->frameLoops; return 1; } return 0; }
int HMouse() { return 0; }
void HDrag(void*) {}

GuiDialogs8Hooks MakeRecordingHooks() {
    GuiDialogs8Hooks h = *GuiDialogs8Hooks_Default();
    h.textRenderRichString = HText;
    h.personResolveStatusFlags = HResolve;
    h.hudBuildPersonCardSimple = HCard;
    h.buildingTypeRankWithinGroup = HRank;
    h.hudBuildScaledTiledBar = HBar;
    h.objectAddTextLabel = HLabel;
    h.textRenderFormatted = HFmt;
    h.gameTickFinalize = HGameTick;
    h.gameLogicRunFrameLoop = HFrame;
    h.readMouseRelease = HMouse;
    h.dragSlotBeginDragText = HDrag;
    return h;
}

bool sawRich(const Recorded& r, unsigned id) {
    for (auto& p : r.rich) if (p.first == id) return true;
    return false;
}

} // namespace

TEST(GuiDialogs8, BuildPersonRendersExpectedSections) {
    ResetGuiDialogs8();
    Recorded rec;
    rec.statusResolved = true;
    rec.rankReturn = 4;
    g_rec = &rec;
    GuiDialogs8Hooks h = MakeRecordingHooks();
    SetGuiDialogs8Hooks(&h);

    PersonRec p;
    p.setId(123);
    p.setGender(1);
    p.setTalent(2);
    p.setPacked353(0x05000000); // group code (high byte) = 5
    int form = Tooltip_BuildPerson(p.rec());
    CHECK_EQ(form, 5);
    // The rank lookup received HIBYTE(*(record+353)) = 5.
    CHECK_EQ(rec.rankInput, 5);
    // The person card fired because status resolved.
    CHECK_EQ(rec.cards, 1);
    // Five stat bars + five labels.
    CHECK_EQ(rec.bars, 5);
    CHECK_EQ(rec.labels, 5);
    // Cash (0x2B), title (0x2C), talent (0x37), wealth (0x2E), guild (0x33) rendered.
    CHECK(sawRich(rec, 0x2B));
    CHECK(sawRich(rec, 0x2C));
    CHECK(sawRich(rec, 0x37));
    CHECK(sawRich(rec, 0x2E));
    CHECK(sawRich(rec, 0x33));

    SetGuiDialogs8Hooks(nullptr);
    g_rec = nullptr;
}

TEST(GuiDialogs8, BuildPersonDetailedGateClosed) {
    ResetGuiDialogs8();
    PersonRec p;
    p.setGate(10); // >= 10 -> gate closed -> builds nothing (returns 0)
    int out = Tooltip_BuildPersonDetailed(p.rec());
    CHECK_EQ(out, 0);
}

TEST(GuiDialogs8, BuildPersonDetailedModalLoopRuns) {
    ResetGuiDialogs8();
    Recorded rec;
    g_rec = &rec;
    GuiDialogs8Hooks h = MakeRecordingHooks();
    SetGuiDialogs8Hooks(&h);

    PersonRec p;
    p.setGate(3); // gate open
    int form = Tooltip_BuildPersonDetailed(p.rec());
    CHECK_EQ(form, 5);
    // The modal idle loop spun until HFrame returned 0 (3 iterations).
    CHECK_EQ(rec.frameLoops, 3);

    SetGuiDialogs8Hooks(nullptr);
    g_rec = nullptr;
}

TEST(GuiDialogs8, BuildObjectPicksFormByAvatar) {
    ResetGuiDialogs8();
    Recorded rec;
    g_rec = &rec;
    GuiDialogs8Hooks h = MakeRecordingHooks();
    SetGuiDialogs8Hooks(&h);

    int form = Tooltip_BuildObject(42);
    CHECK_EQ(form, 5);
    // The price/name spine rendered (id 0x1F price-pair, id 0x25 section).
    CHECK(sawRich(rec, 0x1F));
    CHECK(sawRich(rec, 0x25));

    SetGuiDialogs8Hooks(nullptr);
    g_rec = nullptr;
}
