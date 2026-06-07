#include "gui/buy_dialog.h"

#include "world/trade_player.h"

namespace guild::gui {

namespace {

OccupantSource  g_defaultOccupants;
OccupantSource* g_occupants = &g_defaultOccupants;

// The child-object id base RunApBuy/PopulateObjectList assigns to the built rows
// (the original fetches each via Form_GetChildObjectId; only the relative ordering
// and the parallel id<->record binding matter for the wiring).
constexpr int kRowObjBase = 3000;

} // namespace

void BuyDialog_SetOccupantSource(OccupantSource* src) {
    g_occupants = src ? src : &g_defaultOccupants;
}

// gilde.exe 0x54d8fc — VIBE_Form_PopulateObjectList.
//   SelectWindow(form, winSlot);
//   Light_SetGrayColorThunk(0, 2048, dword_1232080);        // clear the row id table
//   PopulateOccupantList(building, v14, building);          // gather occupant rows
//   RenderRichString(6869, ...);                            // title line
//   for each occupant row (v15[]) while row != 0 && i < 2048:
//     obj = RenderRichString(6870, 1210, name, ...);        // per-row line (id 1210)
//     dword_1232080[i] = Form_GetChildObjectId(form, .., obj);   // child id
//     dword_1232084[i] = row;                                    // record handle
//     Object_SetValueOrText(dword_1232080[i], row+8, ...);       // bind label
int BuyDialog_PopulateObjectList(int /*formId*/, int /*winSlot*/, int building,
                                 std::vector<ListRow>* outRows) {
    std::vector<int> occupants = g_occupants->Enumerate(building);

    if (outRows)
        outRows->clear();

    int count = 0;
    for (int record : occupants) {
        if (count >= kRowTableScanBound)  // the original's i < 2048 bound
            break;
        if (record == 0)                  // row == 0 terminates the original loop
            break;
        ListRow r{};
        r.objId  = kRowObjBase + count;   // Form_GetChildObjectId result
        r.record = record;                // dword_1232084[i]
        if (outRows)
            outRows->push_back(r);
        ++count;
    }
    return count;
}

// gilde.exe 0x54d9d8 (layout half) — VIBE_Panel_RunApBuy.
//   form = GameTick_Finalize(0, 0, "Panel\Panel_AP");
//   CenterChildWindows(form); SelectWindow(form, ?);
//   RenderRichString(6868);                       // header (text id 6868)
//   PopulateObjectList(form, 0, word_63CC5C);     // build the occupant rows
//   dword_75BF38 = -1;                            // arm the click latch
ApBuyLayout BuyDialog_BuildApBuy(int building) {
    ApBuyLayout l{};
    l.form = kFormApPanel;
    l.textHeader = kTextApHeader;
    l.loopForm = kLoopApPanel;
    l.rowCount = BuyDialog_PopulateObjectList(/*formId*/ 0, /*winSlot*/ 0, building,
                                              &l.rows);
    return l;
}

// gilde.exe 0x54d9d8 (wiring half).
//   do {
//     if (dword_672230) dword_631614 = 1;             // right-click -> cancel
//     if (dword_75BF38 == 1210) {                     // a row was selected
//       v3 = dword_62D22C;                             // the clicked object
//       v4 = 0;
//       while (!dword_1232084[v4] || dword_62D22C != dword_1232080[v4]) {
//         v4 += 2; if (v4 >= 512) goto LABEL_9;        // not one of our rows
//       }
//       DataPtr = Object_GetDataPtr(dword_1232080[v4]);
//       *(dword_1232084[v4] + 8) = DataPtr;            // bind the selection
//       SelectWindow(form, 0); RenderRichString("$C"); PopulateObjectList(..);
//     }
//   } while (RunFrameLoop(415687, ..));
bool BuyDialog_DispatchApBuy(const ApBuyLayout& l, int clickedId, int clickedObj,
                             bool cancelFlag, int* outRow, bool* outCancel) {
    if (outRow)    *outRow = -1;
    if (outCancel) *outCancel = false;

    if (cancelFlag) {                       // dword_672230 -> dword_631614 = 1
        if (outCancel) *outCancel = true;
        return false;
    }

    if (clickedId != kBuyClickSelect)       // dword_75BF38 == 1210
        return false;

    // Scan the parallel id/record table for the row whose child-object id matches the
    // clicked object (the original's stride-2 v4 scan, bound 512).  A null record
    // terminates the scan (the row table is null-terminated).
    const int bound = static_cast<int>(l.rows.size());
    for (int i = 0; i < bound; ++i) {
        if (i >= kRowMatchScanBound)        // v4 >= 512 -> give up
            break;
        if (l.rows[i].record == 0)          // !dword_1232084[v4] -> end of table
            break;
        if (clickedObj == l.rows[i].objId) {  // dword_62D22C == dword_1232080[v4]
            if (outRow) *outRow = i;
            return true;                    // selection bound + list refreshed
        }
    }
    return false;
}

// gilde.exe 0x519b14 (layout half) — VIBE_WineCellar_ShowBuyDialog (display math).
//   playerDisp = ConvertToDisplayCoord(playerHeld, city);
//   cellarDisp = ConvertToDisplayCoord(cellarMoney, city);
//   if (playerDisp + cellarDisp > cellarDisp) shown = playerDisp + cellarDisp;
//   else                                      shown = cellarDisp;
//   Object_SetValueOrText(priceObj, 0, shown, playerDisp, form);
WineBuyLayout BuyDialog_BuildWineCellar(int playerHeld, int cellarMoney, u8 city,
                                        int priceObjId) {
    WineBuyLayout l{};
    l.form = kFormWineCellar;
    l.textTitle = kTextWineTitle;
    l.textPrice = kTextWinePrice;
    l.loopForm = kLoopWineCellar;
    l.priceObjId = priceObjId;

    // Reuse the now-real money/coord model from world/trade_player.
    l.playerCashDisp = world::MoneyConvertToDisplayCoord(playerHeld, city);
    l.cellarCashDisp = world::MoneyConvertToDisplayCoord(cellarMoney, city);

    // The original computes (playerDisp + cellarDisp) and compares it to cellarDisp:
    // when it is greater (playerDisp > 0) the label shows the sum, else just cellarDisp.
    int sum = l.playerCashDisp + l.cellarCashDisp;
    l.shownValue = (sum > l.cellarCashDisp) ? sum : l.cellarCashDisp;
    return l;
}

// gilde.exe 0x519b14 (wiring half).
//   while (RunFrameLoop(423879, ..)) {
//     if (dword_672230 || dword_75BF38 == 1155) dword_631614 = 1;   // cancel
//     if (dword_75BF38 == 1210) {                                   // buy
//       DataPtr = Object_GetDataPtr(priceObj);
//       cost = MultiplyByRate(DataPtr, city);
//       if (playerCash >= cost) EnqueueCmd15(player, seller, playerCash - cost, city);
//       else                    EnqueueCmd15(seller, player, cost - playerCash, city);
//       dword_631614 = 1;
//     }
//   }
// The cost/afford + the chosen Cmd15 leg are REUSED from world::WineCellarBuy.
int BuyDialog_DispatchWineCellar(const WineBuyLayout& /*l*/, int itemValue, u8 city,
                                 int playerCash, int playerAccount, int sellerAccount,
                                 int clickedId, bool cancelFlag, bool commit,
                                 bool* outAfford, bool* outCancel) {
    if (outAfford) *outAfford = false;
    if (outCancel) *outCancel = false;

    if (cancelFlag || clickedId == kBuyClickCancel) {  // dword_672230 || 1155
        if (outCancel) *outCancel = true;
        return 0;
    }

    if (clickedId != kBuyClickSelect)                  // dword_75BF38 == 1210
        return 0;

    world::BuyInput in{};
    in.itemValue     = itemValue;
    in.city          = city;
    in.playerCash    = playerCash;
    in.playerAccount = playerAccount;
    in.sellerAccount = sellerAccount;
    world::BuyResult r = world::WineCellarBuy(in, commit);

    if (outAfford) *outAfford = r.afford;
    return r.cost;
}

} // namespace guild::gui
