#pragma once
// Treasury / Bank / Exchange balance operations — rules core (gilde.exe).
//
// The City/Guild treasuries, the bank (Geldleihe), and the goods exchange/contor
// are UI contact-dispatch modules: every balance change is committed through the
// command lockstep (VIBE_Command_QueueRequest16 for money transfers,
// VIBE_Command_BeginDeltaPacket/AppendDeltaField/QueueRequestState22 for object
// field writes such as the exchange fee fields at object+105 / +109). There is
// no standalone balance arithmetic in the binary beyond "transfer amount from
// payer to recipient" and "set fee field". This module recovers that rules core:
// a tracked balance abstraction whose mutations route through the same command
// hook the Amt passes use, plus the exchange fee-field semantics.
//
// Functions whose *rules core* is modeled here:
//   VIBE_GuildTreasury_ShowCashDialog        0x51f608   (treasury balance read)
//   VIBE_CityTreasury_RunContactLoop         0x51f6a8   (treasury contact)
//   VIBE_Bank_RunContactDispatchLoop         0x51da04   (bank contact)
//   VIBE_Exchange_ShowFeesDialog             0x51ca40   (fee fields +105/+109)
//   VIBE_Exchange_ShowGoodsExchangeDialog    0x51bb4c   (transfer commit) [UI deferred]
// The dialog/window UI itself is deferred (see report).
#include "guild/common/types.h"

namespace guild::world {

// A treasury / account balance the rules code reads & mutates. The live game
// holds these as object fields committed over the network; here we model the
// value directly and route the mutation through the Amt transfer hook so the
// command path is observable.
struct Treasury {
    i32 accountId = -1; // object/account id used as transfer endpoint
    i32 balance = 0;    // current cash (read-only mirror; the command is truth)
    int currency = 0;   // currency context for transfers
};

// Deposit `amount` into `t`: enqueues a transfer (payer -> t.accountId) and, if
// the commit succeeds (or no hook is installed, i.e. single-player), updates the
// mirrored balance. Negative/zero amounts are ignored (no-op), matching the
// dialogs which gate the 1210 "OK" action on a positive entry. Returns the new
// balance.
i32 TreasuryDeposit(Treasury& t, i32 payer, i32 amount);

// Withdraw `amount` from `t`: enqueues a transfer (t.accountId -> recipient).
// The dialogs do not let you withdraw more than the balance; this clamps the
// amount to the available balance (returns the actually-moved amount via *moved
// when non-null). Returns the new balance.
i32 TreasuryWithdraw(Treasury& t, i32 recipient, i32 amount, i32* moved);

// Transfer between two treasuries (city <-> guild etc.): clamps to `from`'s
// balance, enqueues a single transfer command, and mirrors both balances.
// Returns the amount actually moved.
i32 TreasuryTransfer(Treasury& from, Treasury& to, i32 amount);

// gilde.exe 0x51ca40 — VIBE_Exchange_ShowFeesDialog (fee-field rules core).
// The dialog reads the bank object's two fee fields (rate-exchange fee at +105,
// courier fee at +109), lets the player edit them, and on OK commits both via a
// delta packet. This struct mirrors those two fields; ExchangeSetFees applies an
// edit and (when commit) routes the field write through the field-write hook.
struct ExchangeFees {
    i32 objectId = -1;
    i32 rateFee = 0;    // object+105 (Wechselgebuehr)
    i32 courierFee = 0; // object+109 (Kuriergebuehr)
};

// Field-write hook (gilde.exe VIBE_Command_AppendDeltaField path). Args:
// (objectId, fieldOffset, value). Default no-op returns false.
using FieldWriteHook = bool (*)(i32 objectId, int fieldOffset, i32 value);
void TreasurySetFieldWriteHook(FieldWriteHook hook);

// Apply edited fees. When `commit`, writes object+105 and object+109 through the
// field-write hook (mirroring the dialog's two AppendDeltaField calls). Returns
// true if both writes were committed (or no hook installed).
bool ExchangeSetFees(ExchangeFees& fees, i32 newRateFee, i32 newCourierFee,
                     bool commit);

} // namespace guild::world
