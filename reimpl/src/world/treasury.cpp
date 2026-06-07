#include "world/treasury.h"

#include "world/amt.h"  // AmtCommitTransfer

namespace guild::world {

namespace {
FieldWriteHook g_fieldWriteHook = nullptr;
} // namespace

void TreasurySetFieldWriteHook(FieldWriteHook hook) { g_fieldWriteHook = hook; }

// Deposit: transfer payer -> treasury. The dialogs gate the OK action on a
// positive entered amount (dword_75BF38 == 1210 with a non-zero edit field).
i32 TreasuryDeposit(Treasury& t, i32 payer, i32 amount) {
    if (amount <= 0)
        return t.balance;
    AmtCommitTransfer(payer, t.accountId, amount, t.currency);
    t.balance += amount;
    return t.balance;
}

// Withdraw: transfer treasury -> recipient, clamped to the available balance
// (the cash dialogs never let you draw past the held amount).
i32 TreasuryWithdraw(Treasury& t, i32 recipient, i32 amount, i32* moved) {
    if (amount <= 0) {
        if (moved) *moved = 0;
        return t.balance;
    }
    i32 take = amount;
    if (take > t.balance)
        take = t.balance;
    if (take <= 0) {
        if (moved) *moved = 0;
        return t.balance;
    }
    AmtCommitTransfer(t.accountId, recipient, take, t.currency);
    t.balance -= take;
    if (moved) *moved = take;
    return t.balance;
}

// Transfer between two treasuries (one command, both mirrors updated).
i32 TreasuryTransfer(Treasury& from, Treasury& to, i32 amount) {
    if (amount <= 0)
        return 0;
    i32 take = amount;
    if (take > from.balance)
        take = from.balance;
    if (take <= 0)
        return 0;
    AmtCommitTransfer(from.accountId, to.accountId, take, from.currency);
    from.balance -= take;
    to.balance += take;
    return take;
}

// gilde.exe 0x51ca40 — VIBE_Exchange_ShowFeesDialog field commit.
//   on OK: BeginDeltaPacket(obj, obj+1);
//          AppendDeltaField(4,1, &rateFee,    105);
//          AppendDeltaField(4,1, &courierFee, 109);
//          QueueRequestState22();
bool ExchangeSetFees(ExchangeFees& fees, i32 newRateFee, i32 newCourierFee,
                     bool commit) {
    fees.rateFee = newRateFee;
    fees.courierFee = newCourierFee;
    if (!commit)
        return true;
    bool a = true, b = true;
    if (g_fieldWriteHook) {
        a = g_fieldWriteHook(fees.objectId, 105, fees.rateFee);
        b = g_fieldWriteHook(fees.objectId, 109, fees.courierFee);
    }
    return a && b;
}

} // namespace guild::world
