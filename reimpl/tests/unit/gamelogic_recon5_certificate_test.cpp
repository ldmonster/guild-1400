#include "test.h"
#include "sim/gamelogic_recon5_certificate.h"

#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

struct CertFixture {
    u8 categoryByte = 0;
    std::vector<int> windows;
    std::vector<u32> stringIds;
    std::vector<i32> stateOffsets; // a3 arg of renderRichString
    std::vector<i32> setVals;
    int nextObjId = 100;
};
CertFixture g_c;

void CSelect(i32, int w) { g_c.windows.push_back(w); }
i32  CRender(u32 id, i32, i32, i32 a3) {
    g_c.stringIds.push_back(id);
    g_c.stateOffsets.push_back(a3);
    return (i32)id; // text handle == string id (deterministic)
}
i32  CChild(i32, i32 h) { return g_c.nextObjId++ + (h & 0); }
void CSet(i32, i32 v, i32) { g_c.setVals.push_back(v); }
u8   CCat(const void*) { return g_c.categoryByte; }

void InstallCert() {
    Recon5CertHooks h{};
    h.selectWindow = &CSelect;
    h.renderRichString = &CRender;
    h.getChildObjectId = &CChild;
    h.setValueOrText = &CSet;
    h.personCategoryByte = &CCat;
    SetRecon5CertHooks(&h);
}

bool Has(const std::vector<u32>& v, u32 x) {
    for (u32 e : v) if (e == x) return true;
    return false;
}

} // namespace

// ===========================================================================
// Full path (category != 5): all four privilege rows + fee field present.
// ===========================================================================
TEST(GameLogicRecon5, Cert_FullPathStringSequence) {
    g_c = CertFixture{}; g_c.categoryByte = 3; g_c.nextObjId = 100;
    InstallCert();
    int person = 1;
    CertFieldIds out{};
    Meister_PopulateMasterCertificateFields(7, out, &person, 555, 0x0F);
    // window selection order: 1, 2.
    CHECK(g_c.windows.size() == 2);
    CHECK_EQ(g_c.windows[0], 1);
    CHECK_EQ(g_c.windows[1], 2);
    // all field string ids present.
    CHECK(Has(g_c.stringIds, 0x1558u));
    CHECK(Has(g_c.stringIds, 0x155Eu));
    CHECK(Has(g_c.stringIds, 0x1563u));
    CHECK(Has(g_c.stringIds, 0x1566u));
    CHECK(Has(g_c.stringIds, 0x1569u));
    CHECK(Has(g_c.stringIds, 0x1562u));
    // privilege ids back-written (not -1) since category != 5.
    CHECK(out.ids[1] != -1);
    CHECK(out.ids[2] != -1);
    CHECK(out.ids[3] != -1);
}

// ===========================================================================
// Bitmask decode: a6 -> per-row state offset (5472/5476/5479/5482 + bit).
// ===========================================================================
TEST(GameLogicRecon5, Cert_BitmaskStateOffsets) {
    g_c = CertFixture{}; g_c.categoryByte = 3;
    InstallCert();
    int person = 1;
    CertFieldIds out{};
    // a6 = 0b1010 -> bit0=0, bit1=1, bit2=0, bit3=1.
    Meister_PopulateMasterCertificateFields(7, out, &person, 555, 0x0A);
    // Find each rich-string's state offset by matching its string id order.
    // Row name 0x155E -> 5472 + 0 ; 0x1563 -> 5476 + 1 ; 0x1566 -> 5479 + 0 ;
    // 0x1569 -> 5482 + 1.
    i32 off155E = -1, off1563 = -1, off1566 = -1, off1569 = -1;
    for (size_t i = 0; i < g_c.stringIds.size(); ++i) {
        if (g_c.stringIds[i] == 0x155Eu) off155E = g_c.stateOffsets[i];
        if (g_c.stringIds[i] == 0x1563u) off1563 = g_c.stateOffsets[i];
        if (g_c.stringIds[i] == 0x1566u) off1566 = g_c.stateOffsets[i];
        if (g_c.stringIds[i] == 0x1569u) off1569 = g_c.stateOffsets[i];
    }
    CHECK_EQ(off155E, 5472);
    CHECK_EQ(off1563, 5477);
    CHECK_EQ(off1566, 5479);
    CHECK_EQ(off1569, 5483);
}

// ===========================================================================
// Category == 5: privilege rows skipped, ids stay -1, fee still stamped.
// ===========================================================================
TEST(GameLogicRecon5, Cert_Category5SkipsPrivilegeRows) {
    g_c = CertFixture{}; g_c.categoryByte = 5;
    InstallCert();
    int person = 1;
    CertFieldIds out{};
    Meister_PopulateMasterCertificateFields(7, out, &person, 555, 0x0F);
    // privilege string ids absent.
    CHECK(!Has(g_c.stringIds, 0x1563u));
    CHECK(!Has(g_c.stringIds, 0x1566u));
    CHECK(!Has(g_c.stringIds, 0x1569u));
    // but name (0x155E) and fee (0x1562) present.
    CHECK(Has(g_c.stringIds, 0x155Eu));
    CHECK(Has(g_c.stringIds, 0x1562u));
    // privilege ids remain -1.
    CHECK_EQ(out.ids[1], -1);
    CHECK_EQ(out.ids[2], -1);
    CHECK_EQ(out.ids[3], -1);
}

// ===========================================================================
// Fee field SetValueOrText receives the 1000 (0x3E8) value.
// ===========================================================================
TEST(GameLogicRecon5, Cert_FeeValue) {
    g_c = CertFixture{}; g_c.categoryByte = 5;
    InstallCert();
    int person = 1;
    CertFieldIds out{};
    Meister_PopulateMasterCertificateFields(7, out, &person, 555, 0);
    bool sawFee = false;
    for (i32 v : g_c.setVals) if (v == 1000) sawFee = true;
    CHECK(sawFee);
}
