// Unit tests — play::SessionPanels (in-game tooltip + selected-entity info
// panel) and the gui/tooltip_content reconstructions:
//   * VIBE_Tooltip_BuildObject   @0x4f7a10 (incl. the @0x6496A9 producer table)
//   * VIBE_Tooltip_BuildUpgrade  @0x4f8154
//   * VIBE_Tooltip_BuildBuilding @0x4f78e4
//   * VIBE_Tooltip_BuildPerson   @0x4f84ac
//   * VIBE_Tooltip_BuildContact  @0x4f83e8 (via Tooltip_ResolveContact)
//   * the screen-edge clamp (0x4f7aa5/0x4f81c2) + the weapon-user mapping
// plus the SessionPanels lifecycle over the REAL VIBE_Tooltip_DispatchByType
// @0x4f7424 and VIBE_InfoPanel_Update @0x4b84c0 reconstructions, composited
// onto a synthetic 16bpp RGB565 framebuffer (pixel-band deltas, determinism,
// register/dedup semantics).  No assets required.
#include "test.h"

#include "gui/text/textdb.h"
#include "gui/tooltip_content.h"
#include "play/session_panels.h"
#include "render/types.h"

#include <cstring>
#include <string>
#include <vector>

using guild::u16;
using guild::u8;
namespace gui = guild::gui;
using guild::play::PanelBuildingHover;
using guild::play::PanelObjectHover;
using guild::play::PanelPersonHover;
using guild::play::SessionPanels;
using guild::play::SessionPanelsInputs;

namespace {

constexpr int kW = 320, kH = 240, kPitch = kW * 2;

std::vector<u16> MakeFb() { return std::vector<u16>((size_t)kW * kH, 0); }

guild::render::Surface Surf(std::vector<u16>& fb) {
    guild::render::Surface s{};
    s.width = kW; s.height = kH; s.pitch = kPitch; s.widthPx = kPitch / 2;
    s.bpp = 16;
    s.pixels = reinterpret_cast<u8*>(fb.data());
    s.clipX0 = 0; s.clipY0 = 0; s.clipX1 = kW; s.clipY1 = kH;
    s.fmt = guild::render::Format565();
    return s;
}

int NonZeroIn(const std::vector<u16>& fb, int x0, int y0, int w, int h) {
    int n = 0;
    for (int y = y0; y < y0 + h && y < kH; ++y)
        for (int x = x0; x < x0 + w && x < kW; ++x)
            if (y >= 0 && x >= 0 && fb[(size_t)y * kW + x]) ++n;
    return n;
}

// Recording gui::TooltipContentHost for the direct builder tests.
struct RecHost : gui::TooltipContentHost {
    struct Call {
        std::string what;
        int a = 0, b = 0, c = 0;
        std::string s;
    };
    std::vector<Call> calls;
    int gx = 0, gy = 0, gw = 0;
    bool haveGeom = false, moved = false;
    int movedX = 0, movedY = 0;

    int LoadForm(const char* n) override {
        calls.push_back({"form", 0, 0, 0, n});
        return 1;
    }
    void CenterChildWindows(int f) override { calls.push_back({"center", f}); }
    bool WindowGeom(int, int* x, int* y, int* w) override {
        *x = gx; *y = gy; *w = gw;
        return haveGeom;
    }
    void MoveWindow(int x, int y) override {
        moved = true; movedX = x; movedY = y;
    }
    void SelectWindow(int f, int sub) override {
        calls.push_back({"win", f, sub});
    }
    void AddIconObject(int objId) override { calls.push_back({"icon", objId}); }
    void AddAnimatedObject(int objId, int x, int y) override {
        calls.push_back({"anim", objId, x, y});
    }
    void Text(int id) override { calls.push_back({"text", id}); }
    void TextArg(int t, int a0) override { calls.push_back({"text1", t, a0}); }
    void TextArg2(int t, int a0, int a1) override {
        calls.push_back({"text2", t, a0, a1});
    }
    void TextFmt(const char* fmt, int a0, int a1) override {
        calls.push_back({"fmt", a0, a1, 0, fmt});
    }
    void TextLabel(int x, int y, int id) override {
        calls.push_back({"label", x, y, id});
    }
    void SkillBar(int cur, int y, int row, int gfx) override {
        calls.push_back({"bar", cur, y, row, std::to_string(gfx)});
    }
    void PersonCard(int x, int y) override { calls.push_back({"card", x, y}); }

    int count(const char* w) const {
        int n = 0;
        for (const Call& c : calls)
            if (c.what == w) ++n;
        return n;
    }
    const Call* nth(const char* w, int idx) const {
        for (const Call& c : calls)
            if (c.what == w && idx-- == 0) return &c;
        return nullptr;
    }
};

} // namespace

// ===========================================================================
// The reconstructed leaves (golden vectors).
// ===========================================================================
TEST(SessionPanels, TooltipClampGoldenVectors) {
    // gilde.exe 0x4f7aa5: x+w > screenW-16 -> x' = screenW-16-w.
    int x = 0;
    CHECK(gui::Tooltip_ClampToScreen(500, 160, 640, &x));
    CHECK_EQ(x, 640 - 16 - 160);                       // 464
    CHECK(!gui::Tooltip_ClampToScreen(100, 160, 640, &x));
    CHECK(!gui::Tooltip_ClampToScreen(464, 160, 640, &x)); // 624 == 624 -> no
    CHECK(gui::Tooltip_ClampToScreen(465, 160, 640, &x));  // 625 > 624 -> yes
    CHECK_EQ(x, 464);
}

TEST(SessionPanels, WeaponUserCodeMapping) {
    // gilde.exe 0x4f7e32..0x4f80df.
    u8 out[2] = {0, 0};
    CHECK_EQ(gui::Tooltip_WeaponUserCodes(449, out), 2);
    CHECK_EQ(out[0], 24); CHECK_EQ(out[1], 25);
    CHECK_EQ(gui::Tooltip_WeaponUserCodes(451, out), 2);
    CHECK_EQ(gui::Tooltip_WeaponUserCodes(452, out), 1);
    CHECK_EQ(out[0], 30);
    CHECK_EQ(gui::Tooltip_WeaponUserCodes(455, out), 0);   // 455 -> none
    CHECK_EQ(gui::Tooltip_WeaponUserCodes(445, out), 1);
    CHECK_EQ(out[0], 32);
    CHECK_EQ(gui::Tooltip_WeaponUserCodes(466, out), 1);
    CHECK_EQ(out[0], 32);
    CHECK_EQ(gui::Tooltip_WeaponUserCodes(439, out), 1);
    CHECK_EQ(out[0], 30);
    CHECK_EQ(gui::Tooltip_WeaponUserCodes(460, out), 1);
    CHECK_EQ(out[0], 30);
    CHECK_EQ(gui::Tooltip_WeaponUserCodes(442, out), 1);
    CHECK_EQ(out[0], 31);
    CHECK_EQ(gui::Tooltip_WeaponUserCodes(463, out), 1);
    CHECK_EQ(out[0], 31);
    CHECK_EQ(gui::Tooltip_WeaponUserCodes(438, out), 0);
    CHECK_EQ(gui::Tooltip_WeaponUserCodes(470, out), 0);
}

TEST(SessionPanels, ObjectMarketRowGate) {
    // gilde.exe 0x4f7ebc: 449..454 suppress; 455 allowed; class 23/37 only.
    CHECK(!gui::Tooltip_ObjectMarketRow(449, 23));
    CHECK(!gui::Tooltip_ObjectMarketRow(454, 37));
    CHECK(gui::Tooltip_ObjectMarketRow(455, 23));
    CHECK(!gui::Tooltip_ObjectMarketRow(455, 5));
    CHECK(gui::Tooltip_ObjectMarketRow(468, 23));
    CHECK(gui::Tooltip_ObjectMarketRow(468, 37));
    CHECK(!gui::Tooltip_ObjectMarketRow(468, 32));
}

TEST(SessionPanels, ObjectValueRatioUsesExactDoubles) {
    // dbl_6206D8 round-trips its recovered bit pattern (~1/252).
    const double k = gui::TooltipObjWorkerWeight();
    CHECK(k > 0.00396 && k < 0.00397);
    // workerByte 0: ratio == baseValue / marketValue, fistp-truncated.
    CHECK_EQ(gui::Tooltip_ObjectValueRatio(1000, 10.0, 0), 100);
    CHECK_EQ(gui::Tooltip_ObjectValueRatio(999, 10.0, 0), 99);
    // A live worker raises the denominator -> strictly smaller ratio.
    CHECK(gui::Tooltip_ObjectValueRatio(100000, 10.0, 200) <
          gui::Tooltip_ObjectValueRatio(100000, 10.0, 0));
}

TEST(SessionPanels, ProducerTableByteExactGolden) {
    // gilde.exe @0x6496A9 — pin the RAW recovered table bytes (size + the
    // documented row layout), independently of the behavioral row-walk test.
    // Provenance: gui/tooltip_content.cpp's kTooltipProductionTable array and
    // its header (23 rows x 21-byte stride + 3 trailing bytes the last
    // unaligned int read touches = 486 bytes total).
    CHECK_EQ((int)gui::kProductionRows, 23);
    CHECK_EQ((int)gui::kProductionRowStride, 21);
    CHECK_EQ((int)sizeof(gui::kTooltipProductionTable),
             gui::kProductionRows * gui::kProductionRowStride + 3); // 486

    const u8* T = gui::kTooltipProductionTable;
    auto word = [&](int byteOff) -> int {
        return (int)(u16)(T[byteOff] | (T[byteOff + 1] << 8));
    };

    // Row 0 (bytes 0..20) byte-exact from get_bytes @0x6496A9:
    //   producer code at +3 == 0x29 (41); item words at +8/+10/+12 == 468/341/342.
    CHECK_EQ((int)T[3], 0x29);          // producer building code 41
    CHECK_EQ(word(8), 468);             // 0x01D4
    CHECK_EQ(word(10), 341);            // 0x0155
    CHECK_EQ(word(12), 342);            // 0x0156
    // Row 1 (+21): code 0x2A (42), the same item 468 at +29, plus 344/343 (+35/+37).
    CHECK_EQ((int)T[21 + 3], 0x2A);
    CHECK_EQ(word(21 + 8), 468);
    CHECK_EQ(word(21 + 14), 344);       // 0x0158
    CHECK_EQ(word(21 + 16), 343);       // 0x0157
    // Row 21 (21*21=441): code 0x1F (31) at +3, item 461 (0x01CD) at +12.
    CHECK_EQ((int)T[441 + 3], 0x1F);
    CHECK_EQ(word(441 + 12), 461);
    // The 3 trailing overflow bytes are zero (the last unaligned int read at +482).
    CHECK_EQ((int)T[483], 0);
    CHECK_EQ((int)T[484], 0);
    CHECK_EQ((int)T[485], 0);
}

TEST(SessionPanels, ObjectTextBiasConstantsGolden) {
    // gilde.exe 0x4f7a10/0x4f8154/0x4f78e4 — pin the recovered text-id / icon
    // arithmetic immediates the builders use (tooltip_content.h provenance).
    CHECK_EQ(gui::kObjNameTextBias, 2151);
    CHECK_EQ(gui::kObjDescTextBias, 2152);
    CHECK_EQ(gui::kObjIconBias, 206);
    CHECK_EQ(gui::kBldIconBias, 1010);
    CHECK_EQ(gui::kBldNameStride, 14);
    CHECK_EQ(gui::kBldNameBias, 1078);
    CHECK_EQ(gui::kUpgEffectBias, 3203);
    CHECK_EQ(gui::kMarketIconObj, 1039);
    CHECK_EQ(gui::kMarketTextId, 1484);
    CHECK_EQ(gui::kTooltipScreenMargin, 16);
    // Row geometry (mov edi,28h / add edi,16h).
    CHECK_EQ(gui::kTipRowBaseY, 40);
    CHECK_EQ(gui::kTipRowPitch, 22);
    // Person tooltip text bases.
    CHECK_EQ(gui::kPersonJobBaseM, 294);
    CHECK_EQ(gui::kPersonJobBaseF, 370);
    CHECK_EQ(gui::kPersonTraitBaseM, 525);
    CHECK_EQ(gui::kPersonTraitBaseF, 560);
    CHECK_EQ(gui::kPersonReligionBaseM, 272);
    CHECK_EQ(gui::kPersonReligionBaseF, 279);
    CHECK_EQ(gui::kPersonClassBase, 1070);
    CHECK_EQ(gui::kPersonSkillTextBase, 4810);
    CHECK_EQ(gui::kPersonSkillRows, 5);
    CHECK_EQ(gui::kPersonSkillPitch, 15);
    CHECK_EQ(gui::kPersonSkillBarGfx, 1162);
    // The exact double bit pattern of dbl_6206D8 (~1/252) round-trips.
    CHECK_EQ(gui::kObjWorkerWeightBits, 0x3F70410441041010ull);
    CHECK_EQ(gui::kObjWorkerScale, 0.25);
    CHECK_EQ(gui::kUpgradePriceScale, 0.25);
    CHECK_EQ(gui::kUpgradePriceBias, 0.5);
}

// ===========================================================================
// VIBE_Tooltip_BuildObject @0x4f7a10 — content emission.
// ===========================================================================
TEST(SessionPanels, BuildObjectEmitsOriginalSequenceAndProducerRows) {
    RecHost host;
    gui::TooltipObjectView rec;
    rec.classByte = 32;            // tradegood class (no weapon/market rows)
    rec.baseValue = 77;
    rec.durability = 5;
    rec.ingredientItem[0] = 341;   // one (count,item) pair
    rec.ingredientCount[0] = 2;
    gui::TooltipObjectEnv env;
    env.isWeapon = false;
    env.hasOwner = false;
    env.screenW = 640;

    const int form = gui::Tooltip_BuildObjectContent(468, rec, env, host);
    CHECK_EQ(form, 1);

    // Form choice + window order (2 -> 1 -> 3 -> 4(form) -> 4 -> 3).
    CHECK(host.nth("form", 0) != nullptr);
    CHECK(host.nth("form", 0)->s == gui::kFormTooltipTradegood);
    CHECK_EQ(host.nth("win", 0)->b, 2);
    CHECK_EQ(host.nth("win", 1)->b, 1);
    CHECK_EQ(host.nth("win", 2)->b, 3);
    CHECK_EQ(host.nth("win", 3)->a, 1);   // SelectWindow(FORM, 4) @0x4f7b91
    CHECK_EQ(host.nth("win", 3)->b, 4);

    // Header: icon code+206, texts 2*code+2152 / +2151.
    CHECK_EQ(host.nth("icon", 0)->a, 468 + 206);
    CHECK_EQ(host.nth("text", 0)->a, 2 * 468 + 2152);
    CHECK_EQ(host.nth("text", 1)->a, 2 * 468 + 2151);

    // Durability branch (0x21, 5) then the no-owner value line (0x20, 77).
    CHECK_EQ(host.nth("text1", 0)->a, 0x21);
    CHECK_EQ(host.nth("text1", 0)->b, 5);
    CHECK_EQ(host.nth("text1", 1)->a, 0x20);
    CHECK_EQ(host.nth("text1", 1)->b, 77);

    // Ingredient row: icon 341+206 at y=40, "%i %s$N"(2, 2*341+2151).
    CHECK_EQ(host.nth("anim", 0)->a, 341 + 206);
    CHECK_EQ(host.nth("anim", 0)->c, 40);
    const RecHost::Call* ing = host.nth("fmt", 0);
    CHECK(ing && ing->s == "%i %s$N");
    CHECK_EQ(ing->a, 2);
    CHECK_EQ(ing->b, 2 * 341 + 2151);

    // Producer rows from the REAL @0x6496A9 table: item 468 is produced by
    // building codes 41/42/43 (rows 0..2) -> icons 1051/1052/1053 at the
    // 22px pitch, names 14*code+1078.
    CHECK_EQ(host.count("anim"), 1 + 3);
    CHECK_EQ(host.nth("anim", 1)->a, 41 + 1010);
    CHECK_EQ(host.nth("anim", 1)->c, 40);
    CHECK_EQ(host.nth("anim", 2)->a, 42 + 1010);
    CHECK_EQ(host.nth("anim", 2)->c, 62);
    CHECK_EQ(host.nth("anim", 3)->a, 43 + 1010);
    CHECK_EQ(host.nth("anim", 3)->c, 84);
    const RecHost::Call* p0 = host.nth("fmt", 1);
    CHECK(p0 && p0->s == "%s$N");
    CHECK_EQ(p0->a, 14 * 41 + 1078);

    // rows != 0 -> no "$C" clear token.
    for (const RecHost::Call& c : host.calls)
        CHECK(!(c.what == "fmt" && c.s == "$C"));
}

TEST(SessionPanels, BuildObjectWeaponFormMarketRowAndClearToken) {
    // A weapon-class object with no producer rows: Waffen form, the weapon
    // "used by" rows, the "+market" row, never "$C".
    RecHost host;
    gui::TooltipObjectView rec;
    rec.classByte = 23;
    rec.durability = 1;
    gui::TooltipObjectEnv env;
    env.isWeapon = true;
    env.screenW = 640;
    gui::Tooltip_BuildObjectContent(900, rec, env, host);   // 900: no producers
    CHECK(host.nth("form", 0)->s == gui::kFormTooltipWeapon);
    // class 23 + id 900: no weapon-user codes, but the market row fires.
    CHECK_EQ(host.count("anim"), 1);
    CHECK_EQ(host.nth("anim", 0)->a, gui::kMarketIconObj);  // 1039
    CHECK_EQ(host.nth("anim", 0)->c, 22 * 2 - 4);           // 22*(0+2)-4
    const RecHost::Call* m = host.nth("fmt", 0);
    CHECK(m && m->s == "%s$N");
    CHECK_EQ(m->a, gui::kMarketTextId);                     // 1484

    // And a tradegood with nothing at all -> "$C" (0x4f8133).
    RecHost host2;
    gui::TooltipObjectView rec2;
    rec2.classByte = 32;
    rec2.durability = 1;
    gui::Tooltip_BuildObjectContent(900, rec2, env, host2);
    bool sawClear = false;
    for (const RecHost::Call& c : host2.calls)
        if (c.what == "fmt" && c.s == "$C") sawClear = true;
    CHECK(sawClear);
}

TEST(SessionPanels, BuildObjectClampFiresAgainstScreenEdge) {
    RecHost host;
    host.haveGeom = true;
    host.gx = 500; host.gy = 30; host.gw = 160;
    gui::TooltipObjectView rec;
    rec.durability = 1;
    gui::TooltipObjectEnv env;
    env.screenW = 640;
    gui::Tooltip_BuildObjectContent(468, rec, env, host);
    CHECK(host.moved);                  // 500+160 > 624
    CHECK_EQ(host.movedX, 464);         // 640-16-160
    CHECK_EQ(host.movedY, 30);          // y kept (0x4f7ae7)
}

// ===========================================================================
// VIBE_Tooltip_BuildUpgrade @0x4f8154.
// ===========================================================================
TEST(SessionPanels, BuildUpgradeGateScanAndEffectLine) {
    gui::TooltipObjectView rec;
    rec.baseValue = 9;
    rec.durability = 0;                       // -> the scaled-price branch
    gui::TooltipUpgradeOwnerView owner;
    owner.present = true;
    owner.objClassGate = 12;                  // not 2/6 -> scan runs
    owner.slotWords[5] = (u16)(468 | 0x8000); // top bit masked by the scan
    owner.kindBytes[5] = 3;                   // kind 3 -> "+%a %s$N"
    owner.valueBytes[5] = 12;
    gui::TooltipUpgradeEnv env;
    env.scaledPrice = 250;
    env.screenW = 640;

    // Class 29 -> -1 (the 0x4f816b early-out / Tooltip_UpgradeApplies).
    RecHost h0;
    CHECK_EQ(gui::Tooltip_BuildUpgradeContent(468, 29, rec, owner, env, h0), -1);
    CHECK_EQ((int)h0.calls.size(), 0);

    RecHost h1;
    CHECK_EQ(gui::Tooltip_BuildUpgradeContent(468, 12, rec, owner, env, h1), 1);
    CHECK(h1.nth("form", 0)->s == gui::kFormTooltipUpgrade);
    CHECK_EQ(h1.nth("text1", 0)->a, 0x23);    // scaled price (no durability)
    CHECK_EQ(h1.nth("text1", 0)->b, 250);
    CHECK_EQ(h1.nth("text1", 1)->a, 0x20);    // base value
    CHECK_EQ(h1.nth("text1", 1)->b, 9);
    CHECK_EQ(h1.nth("text", 2)->a, 0x24);     // the upgrade header
    const RecHost::Call* eff = h1.nth("fmt", 0);
    CHECK(eff && eff->s == "+%a %s$N");       // kind == 3
    CHECK_EQ(eff->a, 12);
    CHECK_EQ(eff->b, 2 * 3 + 3203);           // 3209

    // kind != 3 -> "%i%% %s$N"; kind 0 -> header only; class 2 owner -> none.
    owner.kindBytes[5] = 2;
    RecHost h2;
    gui::Tooltip_BuildUpgradeContent(468, 12, rec, owner, env, h2);
    CHECK(h2.nth("fmt", 0)->s == "%i%% %s$N");
    owner.kindBytes[5] = 0;
    RecHost h3;
    gui::Tooltip_BuildUpgradeContent(468, 12, rec, owner, env, h3);
    CHECK_EQ(h3.count("fmt"), 0);
    CHECK_EQ(h3.nth("text", 2)->a, 0x24);     // header still emitted
    owner.objClassGate = 2;
    RecHost h4;
    gui::Tooltip_BuildUpgradeContent(468, 12, rec, owner, env, h4);
    bool saw24 = false;
    for (const RecHost::Call& c : h4.calls)
        if (c.what == "text" && c.a == 0x24) saw24 = true;
    CHECK(!saw24);                            // object class 2 -> no scan block
}

// ===========================================================================
// VIBE_Tooltip_BuildBuilding @0x4f78e4 / BuildPerson @0x4f84ac / BuildContact.
// ===========================================================================
TEST(SessionPanels, BuildBuildingEmitsTitleIconAndValueLines) {
    RecHost h;
    gui::Tooltip_BuildBuildingContent(30, /*colorSel*/ 2, /*extra*/ 444,
                                      /*salePrice*/ 1200, h);
    CHECK(h.nth("form", 0)->s == gui::kFormTooltipBuilding);
    CHECK_EQ(h.nth("win", 0)->b, 1);
    CHECK_EQ(h.nth("win", 1)->b, 2);
    CHECK_EQ(h.nth("win", 2)->b, 3);
    CHECK_EQ(h.nth("text2", 0)->a, 0x27);                // title template
    CHECK_EQ(h.nth("text2", 0)->b, 14 * 30 + 1078);      // name id
    CHECK_EQ(h.nth("text2", 0)->c, (int)gui::kBuildingColors[2]);
    CHECK_EQ(h.nth("icon", 0)->a, 30 + 1010);
    CHECK_EQ(h.nth("text", 0)->a, 14 * 30 + 1079);       // subtitle
    CHECK_EQ(h.nth("text1", 0)->a, 0x2A);                // colour line
    CHECK_EQ(h.nth("text1", 1)->a, 0x28);                // sale price
    CHECK_EQ(h.nth("text1", 1)->b, 1200);
    CHECK_EQ(h.nth("text1", 2)->a, 0x29);                // record +579
    CHECK_EQ(h.nth("text1", 2)->b, 444);
}

TEST(SessionPanels, BuildPersonEmitsOriginalLineSequence) {
    RecHost h;
    gui::TooltipPersonView p;
    p.id = 7; p.female = 1; p.classCode = 2; p.religion = 1;
    p.jobCode = 4; p.trait1 = 0; p.trait2 = 0;
    gui::TooltipPersonEnv env;
    env.statusCard = true;
    env.cash = 55; env.rank = 3; env.wealth = 99;
    gui::Tooltip_BuildPersonContent(p, env, h);

    CHECK(h.nth("form", 0)->s == gui::kFormTooltipPerson);
    // Window order 1 -> 4 -> 2 -> 3.
    CHECK_EQ(h.nth("win", 0)->b, 1);
    CHECK_EQ(h.nth("win", 1)->b, 4);
    CHECK_EQ(h.nth("win", 2)->b, 2);
    CHECK_EQ(h.nth("win", 3)->b, 3);
    // Header "$Z$[%1N3$]"(id), the person card at (166,5).
    CHECK(h.nth("fmt", 0)->s == "$Z$[%1N3$]");
    CHECK_EQ(h.nth("fmt", 0)->a, 7);
    CHECK_EQ(h.nth("card", 0)->a, 166);
    CHECK_EQ(h.nth("card", 0)->b, 5);
    // Cash / job(+370 female, rank) / religion(+279) / wealth / class(+1070).
    CHECK_EQ(h.nth("text1", 0)->a, 0x2B);
    CHECK_EQ(h.nth("text1", 0)->b, 55);
    CHECK_EQ(h.nth("text2", 0)->a, 0x2C);
    CHECK_EQ(h.nth("text2", 0)->b, 4 + 370);
    CHECK_EQ(h.nth("text2", 0)->c, 3);
    CHECK_EQ(h.nth("text1", 1)->a, 0x37);
    CHECK_EQ(h.nth("text1", 1)->b, 1 + 279);
    CHECK_EQ(h.nth("text1", 2)->a, 0x2E);
    CHECK_EQ(h.nth("text1", 2)->b, 99);
    CHECK_EQ(h.nth("text1", 3)->a, 0x33);
    CHECK_EQ(h.nth("text1", 3)->b, 2 + 1070);
    // No traits -> the single "$3>%s$A"(525); unmarried -> 0x32; no child ->
    // "$3>%s$A"(53).
    CHECK(h.nth("fmt", 1)->s == "$3>%s$A");
    CHECK_EQ(h.nth("fmt", 1)->a, 525);
    bool saw32 = false;
    for (const RecHost::Call& c : h.calls)
        if (c.what == "text" && c.a == 0x32) saw32 = true;
    CHECK(saw32);
    CHECK(h.nth("fmt", 2)->s == "$3>%s$A");
    CHECK_EQ(h.nth("fmt", 2)->a, 53);
    // Five skill rows: labels 4810+i at y=15i, bars at y=2+15i.
    CHECK_EQ(h.count("label"), 5);
    CHECK_EQ(h.count("bar"), 5);
    CHECK_EQ(h.nth("label", 0)->c, 4810);
    CHECK_EQ(h.nth("label", 4)->c, 4814);
    CHECK_EQ(h.nth("label", 4)->b, 60);     // y = 15*4
    CHECK_EQ(h.nth("bar", 4)->b, 62);       // y = 2 + 15*4
}

TEST(SessionPanels, BuildContactEmitsFoundBranch) {
    RecHost h;
    gui::Tooltip_BuildContactContent(120, h);
    CHECK(h.nth("form", 0)->s == gui::kFormTooltipContact);
    CHECK_EQ(h.nth("win", 0)->b, 1);
    CHECK(h.nth("fmt", 0)->s == "$Z$[%s$]");
    CHECK_EQ(h.nth("fmt", 0)->a, 120);
    CHECK_EQ(h.nth("win", 1)->b, 2);
    CHECK_EQ(h.nth("text", 0)->a, 121);     // the following entry
}

// ===========================================================================
// SessionPanels — the live lifecycle over the REAL dispatch @0x4f7424.
// ===========================================================================
TEST(SessionPanels, TooltipLifecycleBuildDedupHide) {
    SessionPanels sp;
    sp.layout.panelY = 150;                  // keep the panel inside 240px

    PanelPersonHover ph;
    ph.view.id = 9;
    ph.env.cash = 10;

    SessionPanelsInputs in;
    in.hoveredTooltipId = 42;                // dword_75BF3C
    in.scenePickActive = 1;
    in.cursorX = 100; in.cursorY = 80;
    in.hoverKind = gui::TooltipKind::kPerson;
    in.hoverPersonId = 9;
    in.person = &ph;

    // Frame 1: built + composited near the cursor.
    auto fb = MakeFb();
    auto s = Surf(fb);
    sp.Frame(s, in);
    CHECK(sp.lastResult().tooltipAction == gui::TooltipAction::kBuilt);
    CHECK_EQ(sp.lastResult().tooltipBuilds, 1);
    CHECK(sp.lastResult().tooltipVisible);
    CHECK(std::string(sp.lastResult().tooltipForm) == gui::kFormTooltipPerson);
    CHECK_EQ(sp.tooltipState().shownForId, 42);   // dword_63390C latched
    CHECK(sp.lastResult().tooltipTextOps > 5);
    CHECK(NonZeroIn(fb, sp.lastResult().tooltipX, sp.lastResult().tooltipY,
                    sp.lastResult().tooltipW, sp.lastResult().tooltipH) > 0);

    // Frame 2 (same hover id): the dispatch DEDUPS — no rebuild, still up.
    auto fb2 = MakeFb();
    auto s2 = Surf(fb2);
    sp.Frame(s2, in);
    CHECK_EQ(sp.lastResult().tooltipBuilds, 0);
    CHECK(sp.lastResult().tooltipVisible);

    // Frame 3 (hover gone): the teardown branch (dword_75BF3C == -1).
    in.hoveredTooltipId = -1;
    auto fb3 = MakeFb();
    auto s3 = Surf(fb3);
    sp.Frame(s3, in);
    CHECK(sp.lastResult().tooltipAction == gui::TooltipAction::kHidden);
    CHECK(!sp.lastResult().tooltipVisible);
    CHECK_EQ(sp.tooltipState().formHandle, -1);

    // Frame 4 (hover back): rebuilds.
    in.hoveredTooltipId = 42;
    auto fb4 = MakeFb();
    auto s4 = Surf(fb4);
    sp.Frame(s4, in);
    CHECK_EQ(sp.lastResult().tooltipBuilds, 1);
    CHECK(sp.lastResult().tooltipVisible);
}

TEST(SessionPanels, ObjectClassByteSelectsObjectVsUpgradeBuilder) {
    // The REAL Tooltip_SelectBuilder reads the class byte at 65*code in the
    // shadow object table: {32,23,37} -> BuildObject, else -> BuildUpgrade.
    PanelObjectHover oh;
    oh.view.durability = 1;
    SessionPanelsInputs in;
    in.hoveredTooltipId = 7;
    in.cursorX = 10; in.cursorY = 10;
    in.hoverKind = gui::TooltipKind::kObject;
    in.hoverObjectCode = 468;
    in.object = &oh;

    {
        SessionPanels sp;
        sp.layout.panelY = 150;
        in.hoverObjectClass = 23;
        auto fb = MakeFb(); auto s = Surf(fb);
        sp.Frame(s, in);
        CHECK(std::string(sp.lastResult().tooltipForm) ==
              gui::kFormTooltipTradegood);
    }
    {
        SessionPanels sp;
        sp.layout.panelY = 150;
        in.hoverObjectClass = 12;                  // not {32,23,37} -> upgrade
        auto fb = MakeFb(); auto s = Surf(fb);
        sp.Frame(s, in);
        CHECK(std::string(sp.lastResult().tooltipForm) ==
              gui::kFormTooltipUpgrade);
    }
}

TEST(SessionPanels, TooltipBoxClampsAtTheRightScreenEdge) {
    // The clamp block lives in the OBJECT/UPGRADE builders (0x4f7aa5/0x4f81c2)
    // — the person/building/contact builders have no clamp in the original.
    SessionPanels sp;
    sp.layout.panelY = 150;
    PanelObjectHover oh;
    oh.view.durability = 1;
    SessionPanelsInputs in;
    in.hoveredTooltipId = 5;
    in.cursorX = 310; in.cursorY = 40;        // pend x = 322 -> overflows 320
    in.hoverKind = gui::TooltipKind::kObject;
    in.hoverObjectCode = 468;
    in.hoverObjectClass = 23;
    in.object = &oh;
    auto fb = MakeFb(); auto s = Surf(fb);
    sp.Frame(s, in);
    CHECK(sp.lastResult().tooltipVisible);
    CHECK(sp.lastResult().tooltipClamped);
    CHECK_EQ(sp.lastResult().tooltipX, kW - 16 - sp.layout.tooltipW); // 144
}

TEST(SessionPanels, ContactTooltipResolvesHilfeKeyThroughTextDb) {
    gui::text::TextDb db;
    // Index of "_HILFE_ANNA+0" + the following entry the builder renders.
    int base = -1;
    for (int i = 0; i < 30; ++i) {
        int idx = db.Add("filler", "X");
        (void)idx;
    }
    base = db.Add("Anna help head", "_HILFE_ANNA+0");
    db.Add("Anna help body", "X2");

    SessionPanels sp;
    sp.layout.panelY = 150;
    SessionPanelsInputs in;
    in.hoveredTooltipId = -1;
    in.scenePickActive = 1;                   // dword_672238 (phase-2 gate)
    in.contactReqId = 5;                      // dword_631724
    in.contactName = "anna";                  // upper-cased by the key build
    in.textDb = &db;
    in.cursorX = 50; in.cursorY = 50;
    auto fb = MakeFb(); auto s = Surf(fb);
    sp.Frame(s, in);
    CHECK(sp.lastResult().tooltipVisible);
    CHECK(std::string(sp.lastResult().tooltipForm) == gui::kFormTooltipContact);
    CHECK_EQ(sp.lastResult().tooltipTextOps, 2);
    (void)base;
}

// ===========================================================================
// SessionPanels — the info panel over the REAL InfoPanel_Update @0x4b84c0.
// ===========================================================================
TEST(SessionPanels, InfoPanelRebuildOnlyOnSelectionChange) {
    SessionPanels sp;
    sp.layout.panelY = 150;

    gui::InfoBuildingRecord bld;
    bld.code = 30;                 // market
    bld.upgradeLevel = 40;
    bld.item = 0xFFFF;

    SessionPanelsInputs in;
    in.hoveredTooltipId = -1;
    in.selection.building = 5;     // dword_631748
    in.selBuilding = &bld;

    // Frame 1: the selection changed (zeroed snapshot) -> BuildBuilding.
    auto fb = MakeFb(); auto s = Surf(fb);
    sp.Frame(s, in);
    CHECK(sp.lastResult().panelRebuilt);
    CHECK(sp.lastResult().panelBuilder == gui::InfoBuilder::kBuilding);
    CHECK(sp.lastResult().panelVisible);
    CHECK(std::string(sp.lastResult().panelForm) == gui::kFormPanelBuilding);
    CHECK(sp.lastResult().panelTextOps >= 1);
    CHECK(sp.lastResult().panelIconOps >= 1);
    CHECK(sp.lastResult().panelSliderFillPx > 0);   // upgrade 40 of 100
    // Pixel band: the panel box region is non-blank.
    CHECK(NonZeroIn(fb, sp.layout.panelX, sp.layout.panelY, sp.layout.panelW,
                    sp.layout.panelH) > 0);

    // Frame 2 (same selection): the @0x4b84c0 change detection DEDUPS.
    auto fb2 = MakeFb(); auto s2 = Surf(fb2);
    sp.Frame(s2, in);
    CHECK(!sp.lastResult().panelRebuilt);
    CHECK(sp.lastResult().panelVisible);            // cached content redrawn
    CHECK(fb2 == fb);                               // same pixels, same panel?

    // Frame 3: person selection -> rebuild with BuildPerson.  The original's
    // person branch requires personMode set (dword_6317B0; personMode<=1 is
    // the single-person detail form).
    gui::InfoPersonRecord person;
    person.nameCode = 11;
    person.outputRatioPct = 50;
    in.selection.building = 0;
    in.selection.person = 9;       // dword_11BC270
    in.selection.personMode = 1;   // dword_6317B0
    in.selBuilding = nullptr;
    in.selPerson = &person;
    auto fb3 = MakeFb(); auto s3 = Surf(fb3);
    sp.Frame(s3, in);
    CHECK(sp.lastResult().panelRebuilt);
    CHECK(sp.lastResult().panelBuilder == gui::InfoBuilder::kPerson);
    CHECK(std::string(sp.lastResult().panelForm) == gui::kFormPanelPerson);
}

TEST(SessionPanels, FrameIsDeterministicAcrossFreshInstances) {
    PanelPersonHover ph;
    ph.view.id = 9;
    gui::InfoBuildingRecord bld;
    bld.code = 30;
    bld.upgradeLevel = 25;

    SessionPanelsInputs in;
    in.hoveredTooltipId = 42;
    in.cursorX = 100; in.cursorY = 60;
    in.hoverKind = gui::TooltipKind::kPerson;
    in.hoverPersonId = 9;
    in.person = &ph;
    in.selection.building = 3;
    in.selBuilding = &bld;

    auto fbA = MakeFb();
    auto fbB = MakeFb();
    {
        SessionPanels sp;
        sp.layout.panelY = 150;
        auto s = Surf(fbA);
        sp.Frame(s, in);
    }
    {
        SessionPanels sp;
        sp.layout.panelY = 150;
        auto s = Surf(fbB);
        sp.Frame(s, in);
    }
    CHECK(fbA == fbB);
    CHECK(NonZeroIn(fbA, 0, 0, kW, kH) > 0);
}

TEST(SessionPanels, DegenerateSurfaceIsANoOp) {
    SessionPanels sp;
    SessionPanelsInputs in;
    in.hoveredTooltipId = 3;
    in.hoverKind = gui::TooltipKind::kPerson;
    in.hoverPersonId = 1;
    guild::render::Surface s{};        // null pixels
    sp.Frame(s, in);
    CHECK(!sp.lastResult().tooltipVisible);
    CHECK(!sp.lastResult().panelVisible);
}
