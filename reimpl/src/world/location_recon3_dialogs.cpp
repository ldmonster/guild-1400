// guild::world — VIBE_Location_* interaction rule-cores (recon batch 3).  See header.
// Reconstructed 1:1 from gilde.exe; addresses on each function.
#include "world/location_recon3_dialogs.h"

namespace guild::world {
namespace loc3 {

// ===========================================================================
// gilde.exe 0x512a6c — VIBE_Location_BriberyMenu  (gate / capacity half)
//
//   if (a1 && *(WORD*)(a1+39) != 0xFFFF) {
//       if (!IsAnimalTargetBusy(a1)) { RenderFormatted(...,5791,...); ShowMessageBox; return; }
//       ... count rivals (v5) and free handlers (v37) ...
//       v12 = v37 + v5;
//       if (v12 >= 2 * capacity_byte[+583]) { RenderFormatted(...,5641,v12,...); return; }
//       ... open menu ...
//   }
// Note the original's early-return ordering: the 5791 message is emitted both when the
// type word is 0xFFFF (the outer `if` simply does nothing — menu never opens) and when the
// target is busy.  We fold the two "menu not opened" reasons into CantBribeHere.
// ===========================================================================
BriberyResult Bribery_Gate(BriberyDeps& deps) {
    if (!deps.TargetValid())
        return BriberyResult::CantBribeHere;            // a1==0 || *(a1+39)==0xFFFF
    if (!deps.AnimalTargetBusy())
        return BriberyResult::CantBribeHere;            // !IsAnimalTargetBusy -> msg 5791
    const int total = deps.FreeHandlerCount() + deps.RivalOfficialCount(); // v37 + v5
    if (Bribery_OverCapacity(total, deps.CapacityByte()))
        return BriberyResult::TooMany;                  // >= 2*capacity -> msg 5641
    return BriberyResult::MenuOpened;
}

// ===========================================================================
// gilde.exe 0x513568 — VIBE_Location_TradeTransport
// ===========================================================================
const double kTransportBarRate = 0.0013333333333333335; // dbl_621918 (== 1/750)
const double kTransportBarCap  = 0.25;                   // dbl_621920

f32 Transport_BarFill(int elapsedTicks) {
    // v20 = (unsigned)(now - start); v21 = v20 * rate; if (v21 >= cap) v22 = 0.25 else v22 = v21.
    // The original casts the tick delta through `unsigned int` before the double multiply.
    const double f = static_cast<double>(static_cast<unsigned int>(elapsedTicks)) * kTransportBarRate;
    const double v = (f >= kTransportBarCap) ? 0.25 : f;
    return static_cast<f32>(v);
}

TransportRebuild Transport_RebuildFor(int dir) {
    // if (dir) { sub 475, text 0x1854 } else { sub 476, text 0x1855 }
    if (dir)
        return TransportRebuild{kTransportSubExport, kTransportTextExport};
    return TransportRebuild{kTransportSubImport, kTransportTextImport};
}

TransportRowEnable Transport_RowEnable(int dir) {
    // *(export_row+56)=dir; *(import_row+56)=(dir==0).
    return TransportRowEnable{dir, dir == 0 ? 1 : 0};
}

// ===========================================================================
// gilde.exe 0x513c60 — VIBE_Location_TradeSearchExport
// gilde.exe 0x5142ec — VIBE_Location_TradeSearchImport
//
// Export row-count gate (v70):
//   v5 = (BYTE*)(589 * *a1 + base);
//   if (*v5 == 8 || v5[583] < 2u) v70 = 2; else v70 = 3;
// ===========================================================================
int Search_ExportRowCount(int buildingTypeByte, int capacityByte) {
    if (buildingTypeByte == 8 || static_cast<unsigned>(capacityByte) < 2u)
        return 2;
    return 3;
}

void Search_Aggregate(const int goodTypes[3], const SearchHandler* handlers, int count,
                      int out[3]) {
    // for (j=0;j!=3;) out[j++]=0;
    out[0] = out[1] = out[2] = 0;
    // for each handler: scan the 3 good-type ids; on the first match accumulate +172 byte.
    //   v34 = goods[i]; v35 = handler.goodType; if (v35 == v34) break;  (else next i, max 3)
    //   v36 = v34 ^ v35;  LOBYTE(v36) = handler.weightByte;  out[i] += v36;
    // Since the loop only reaches the accumulate when v34 == v35, (v34 ^ v35) == 0, so v36
    // is exactly the low byte = weightByte.  Hence out[i] += weightByte.
    for (int h = 0; h < count; ++h) {
        const int gt = handlers[h].goodType;
        for (int i = 0; i < 3; ++i) {
            if (gt == goodTypes[i]) {
                // weightByte is the original's +172 byte (8-bit); mask to match LOBYTE.
                out[i] += (handlers[h].weightByte & 0xFF);
                break;
            }
        }
    }
}

int Search_RowMessageId(int amount, int msgOne, int msgMany) {
    // if (amount == 1) msgOne; else if (amount <= 1) "%s" (0); else msgMany.
    if (amount == 1)
        return msgOne;
    if (amount <= 1)
        return 0;       // the bare "%s" branch — no count shown
    return msgMany;
}

// ===========================================================================
// gilde.exe 0x515524 — VIBE_Location_ResidenceMistress
//   if (v10 < 4) { enable take-mistress } else { disable }
//   header: LOBYTE(dword_12CE919[134*slot]) ? 0x165E : 0x165D
// (both inline in the header)
// ===========================================================================

// ===========================================================================
// gilde.exe 0x517a58 / 0x517724 — Tavern Stammtisch Join / Leave
//
//   w  = (window2.width>>16) - 2*(cardWidth>>16);
//   cw = w / 3;  c = (w % 3) / 2;
//   x  = c + cw*(i%2 + 1) + 10*(i%2 - 1) + cardWidth*(i%2);
//   y  = 130*(i/2) + 100;
// ===========================================================================
StammtischCard Stammtisch_CardGeometry(int i, int windowWidth, int cardWidth) {
    const int w  = windowWidth - 2 * cardWidth;
    const int cw = w / 3;
    const int c  = (w % 3) / 2;
    const int parity = i % 2;
    StammtischCard card;
    card.x = c + cw * (parity + 1) + kStammtischColXOff * (parity - 1) + cardWidth * parity;
    card.y = kStammtischCardYStep * (i / 2) + kStammtischCardYBase;
    return card;
}

StammtischState Stammtisch_JoinState(int memberCount, bool playerAtTable, bool memberElsewhere) {
    // v38 starts CanJoin(1).
    //   if (memberCount == 4 && !playerAtTable)  v38 = Full(2);
    //   else if (memberCount < 4 && !playerAtTable) {
    //        v38 = AlreadyMember(3);  // tentative "join offer"
    //        if (scan finds player seated elsewhere) v38 = MemberElsewhere(4);
    //   }
    // (when playerAtTable is true the state stays CanJoin(1) — the leave path.)
    if (memberCount == kStammtischSeats && !playerAtTable)
        return StammtischState::Full;
    if (memberCount < kStammtischSeats && !playerAtTable) {
        if (memberElsewhere)
            return StammtischState::MemberElsewhere;
        return StammtischState::AlreadyMember; // "join offer" (v38 == 3)
    }
    return StammtischState::CanJoin;
}

// ===========================================================================
// gilde.exe 0x51816c — VIBE_Location_TavernDarkCornerBrowse
//
//   FindFirstHandlerByFilter(1,0,116) keyed to player -> v20 (block handler).
//   if (v20)                                   text 0x14B7  (blocked)
//   else if ((int)qword_13CE852 > *(v43+40))   hire button 0x14B6
//   (else: no offer — too poor.)
// ===========================================================================
DarkCornerOffer DarkCorner_HireOffer(bool blockHandlerMatched, long long playerWealth, int askingPrice) {
    if (blockHandlerMatched)
        return DarkCornerOffer::Blocked;
    // The original compares (int)qword_13CE852 (the low 32 bits, signed) > askingPrice.
    if (static_cast<int>(playerWealth) > askingPrice)
        return DarkCornerOffer::HireButton;
    return DarkCornerOffer::TooPoor;
}

} // namespace loc3
} // namespace guild::world
