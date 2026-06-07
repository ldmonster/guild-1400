#include "gui/evidence_dialog.h"

namespace guild::gui {

namespace {

// Stable child-object id base for the browse cards.
constexpr int kCardObjBase = 2200;

} // namespace

// gilde.exe 0x548168 (layout half) — VIBE_EvidenceDialog_Browse.
//   v8 = 0;  for each handler: if (FindRecordById(...)) { record card; ++v8; }
//   if (v8) { for i: BuildPersonCard(50, 105*i+10, ...); AddRightAlignedLabel(.., 55+105*i) }
//   else RenderRichString(4948).
EvBrowseLayout EvidenceDialog_BuildBrowse(const std::vector<EvidencePerson>& people) {
    EvBrowseLayout l{};
    l.form = kFormEvBrowse;

    int i = 0;
    for (const auto& p : people) {
        if (p.entity == 0)
            continue; // FindRecordById failed -> skip (no card, no index bump)
        EvBrowseCard c{};
        c.entity   = p.entity;
        c.objectId = kCardObjBase + i;
        c.cardY    = kEvBrowseRowY0 + kEvBrowseRowStride * i;   // 10 + 105*i
        c.labelY   = kEvBrowseLabelY0 + kEvBrowseRowStride * i; // 55 + 105*i
        l.cards.push_back(c);
        ++i;
    }
    l.empty = l.cards.empty();
    return l;
}

// gilde.exe 0x548168 (wiring half).
//   if (dword_75BF38 != -1 && dword_62D22C matches card[v22].obj) -> ShowDetails(person).
int EvidenceDialog_DispatchBrowse(const EvBrowseLayout& l, int clickedObj) {
    for (const auto& c : l.cards) {
        if (c.objectId == clickedObj)
            return c.entity;
    }
    return -1;
}

// gilde.exe 0x547e88 (layout half) — VIBE_EvidenceDialog_ShowDetails.
//   v20=10; for each row i: AddLeftAlignedLabel(name, 10, 315, v20, 68);  v20 += 40.
//   if (Gesetz_GetRecord -> v18 seals) for k in [0,v18): AddToWindow(id 1227) at
//     x = 340 + 16*k, baseline v24 = 40*i + 10.
EvDetailLayout EvidenceDialog_BuildDetails(int targetEntity,
                                           const std::vector<EvidenceRow>& rows) {
    EvDetailLayout l{};
    l.targetEntity = targetEntity;
    if (rows.empty())
        return l; // original returns early when count < 1 (no form built)
    l.form = kFormEvDetails;

    int i = 0;
    for (const auto& r : rows) {
        EvDetailRowLayout row{};
        row.rowIndex = i;
        row.lawId = r.lawId;
        row.nameY = kEvDetailRowY0 + kEvDetailRowStride * i; // 10 + 40*i
        for (int k = 0; k < r.seals; ++k) {
            EvSealIcon s{};
            s.x = kEvDetailSealX0 + kEvDetailSealStep * k;   // 340 + 16*k
            s.y = row.nameY;                                  // shared row baseline
            row.seals.push_back(s);
        }
        l.rows.push_back(row);
        ++i;
    }
    return l;
}

} // namespace guild::gui
