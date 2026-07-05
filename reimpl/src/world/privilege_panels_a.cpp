// privilege_panels_a.cpp — SET-A guild-office privilege action panels, 1:1 from
// gilde.exe. See privilege_panels_a.h for the fidelity model.
#include "world/privilege_panels_a.h"
#include "util/coord.h"   // ConvertX (truncate toward zero) — matches VIBE_Coord_ConvertX

#include <cmath>

namespace guild::world {

namespace {

// Emit a recorded command into the trace (the production hook substitutes the real
// VIBE_Command_* enqueue; tests read the trace to assert sequence + args).
void Emit(PrivilegePanelHooks* h, int op, i32 a = 0, i32 b = 0, i32 c = 0, i32 d = 0) {
    if (!h || !h->trace) return;
    PrivilegePanelTrace* t = h->trace;
    if (t->cmdCount < (int)(sizeof(t->cmds) / sizeof(t->cmds[0]))) {
        PrivCommand& cmd = t->cmds[t->cmdCount++];
        cmd.op = op; cmd.a = a; cmd.b = b; cmd.c = c; cmd.d = d;
    }
}

void SetDone(PrivilegePanelHooks* h, int state) {
    if (h && h->trace) h->trace->doneState = state;
}

int Rng(PrivilegePanelHooks* h, u16 n) {
    return h && h->randomModulo ? h->randomModulo(n, h->ctx) : 0;
}

bool IsOfficeHolderKind(u8 kind) { return kind == 6 || kind == 7; }

} // namespace

// ---------------------------------------------------------------------------
// Cost helpers. The original does:  (double)wealth * factor; ConvertX(); (int)v.
// ConvertX truncates toward zero (verified leaf semantics), so the cast matches.
// ---------------------------------------------------------------------------
int PrivCostFromWealth(i32 wealth, float factor) {
    double v = (double)wealth * (double)factor;
    v = guild::util::ConvertX(v);            // truncate toward zero /*0x5c6b08*/
    return (int)v;
}
int PrivCostFromWealth(i32 wealth, double factor) {
    double v = (double)wealth * factor;
    v = guild::util::ConvertX(v);
    return (int)v;
}

// ===========================================================================
// gilde.exe 0x563000 — VIBE_Privilege_PanelGenerateHatred.
// Drives two persons into mutual hatred: subtracts random+relation deltas from the
// pairwise relation matrix (both directions), charges a wealth-based fee, fires a
// notification to office-holder kinds. Non-office actor (passive) path returns
// 16/32; office actor (kind 6) opens two office-holder pickers then runs the dialog.
// ===========================================================================
char PrivilegePanelGenerateHatred(const PrivPerson* actor, const PrivEvent* ev,
                                  PrivilegePanelHooks* h) {
    if (!actor) return 32;

    if (actor->kind == 6) {
        // --- office-holder GUI path (0x563258) ---
        char result = 0;
        const PrivPerson* p1 = h->pickOfficeHolder ? h->pickOfficeHolder(6607, h->ctx) : nullptr;
        if (!p1) return 0;                                       /*0x5632b4*/
        const PrivPerson* p2 = h->pickOfficeHolder ? h->pickOfficeHolder(6608, h->ctx) : nullptr;
        if (!p2) return 0;

        i32 wealth = h->computeTotalWealth(p1, h->ctx)
                   + h->computeTotalWealth(p2, h->ctx);          /*0x56330a..0x563318*/
        int cost = PrivCostFromWealth(wealth, kPrivHatredWealthFactor); /*0x563326*/
        if (h->trace) h->trace->cost = cost;
        if (h->trace) h->trace->lastFormScene = 0x563348;        // "hass_erzeugen"

        int btn;
        while ((btn = h->nextButton(h->ctx)) != kPrivLoopExit) {
            if (btn == kPrivLoopIdle) continue;
            if (btn == kPrivBtnOk) {                             /*ChildObjectId confirm*/
                if (h->checkSkill(actor, 4, h->ctx)
                    && h->checkResource(cost, actor->byte994, h->ctx)) {
                    // relation deltas — RNG draw ORDER is exact (two draws), then
                    // exactly TWO matrix lookups (0x56347a p1->p2, 0x56348e p2->p1;
                    // each result feeds one delta — the disasm keeps the first in
                    // ecx+0x7F, the second in eax+0x7F):
                    int r0 = Rng(h, 0x1E);                       /*0x563451*/
                    int v50 = Rng(h, 0x1E) + 3;                  /*0x56346b*/
                    int v14 = r0 + 3 -
                              (h->relationEntry(p1->id, p2->id, h->ctx) + 127); /*0x56347a*/
                    v50 -= h->relationEntry(p2->id, p1->id, h->ctx) + 127;      /*0x56348e*/
                    Emit(h, PrivCommand::kCoord27, p1->handle, p2->handle, v14); /*0x5634b3*/
                    Emit(h, PrivCommand::kCoord27, p2->handle, p1->handle, v50); /*0x5634ca*/
                    Emit(h, PrivCommand::kEnqueueCmd15, -1, actor->handle, cost, actor->byte994); /*0x5634e6*/
                    Emit(h, PrivCommand::kBuildOp90, actor->handle, -4);          /*0x5634f3*/
                    if (h->trace) h->trace->lastMessageId = 6610;                  /*0x563511*/
                    if (IsOfficeHolderKind(p1->kind) && h->sendEntityMessage) {    /*0x5635c5*/
                        h->sendEntityMessage(p1->handle, 1418, h->ctx);
                        if (h->trace) h->trace->lastEntityMsgId = 6611;
                    }
                    if (IsOfficeHolderKind(p2->kind) && h->sendEntityMessage) {    /*0x5635d0*/
                        h->sendEntityMessage(p2->handle, 1418, h->ctx);
                        if (h->trace) h->trace->lastEntityMsgId = 6611;
                    }
                    SetDone(h, 1); /*0x5635b1*/
                    result = 1;    /*0x5635b7*/
                }
            } else if (btn == kPrivBtnCancelId) {
                SetDone(h, 1); /*0x5635e1*/
            }
        }
        return result;
    }

    // --- passive (concrete-target) path (0x563020) ---
    if (actor->office404 < 4) return 32;                                 /*0x563020*/
    const PrivPerson* p1 = h->findRecord ? h->findRecord(ev->targetId, h->ctx) : nullptr;
    if (!p1) return 32;                                                  /*0x563048*/
    const PrivPerson* p2 = h->findRecord ? h->findRecord(ev->partnerId, h->ctx) : nullptr;
    if (!p2) return 32;                                                  /*0x563060*/

    i32 wealth = h->computeTotalWealth(p1, h->ctx) + h->computeTotalWealth(p2, h->ctx); /*0x56306c..*/
    int cost = PrivCostFromWealth(wealth, kPrivHatredWealthFactor);      /*0x563088*/
    if (h->trace) h->trace->cost = cost;
    if (h->sumCurrencyHeld(actor, h->ctx) < cost) return 32;             /*0x5630aa*/

    int r0 = Rng(h, 0x1E);                                               /*0x5630bc*/
    int v48 = Rng(h, 0x1E) + 3;                                          /*0x5630d6*/
    int v14 = r0 + 3 - (h->relationEntry(p1->id, p2->id, h->ctx) + 127); /*0x5630f9*/
    v48 -= h->relationEntry(p2->id, p1->id, h->ctx) + 127;              /*0x563102..0x56310f*/
    Emit(h, PrivCommand::kCoord27, p1->handle, p2->handle, v14);         /*0x56311e*/
    Emit(h, PrivCommand::kCoord27, p2->handle, p1->handle, v48);         /*0x563135*/
    Emit(h, PrivCommand::kEnqueueCmd15, -1, actor->handle, cost, actor->byte994); /*0x563151*/
    Emit(h, PrivCommand::kBuildOp90, actor->handle, -4);                 /*0x56315e*/
    if (IsOfficeHolderKind(p1->kind) && h->sendEntityMessage) {         /*0x563213*/
        h->sendEntityMessage(p1->handle, 1418, h->ctx);
        if (h->trace) h->trace->lastEntityMsgId = 6611;
    }
    if (IsOfficeHolderKind(p2->kind) && h->sendEntityMessage) {         /*0x56321e*/
        h->sendEntityMessage(p2->handle, 1418, h->ctx);
        if (h->trace) h->trace->lastEntityMsgId = 6611;
    }
    return 16;                                                           /*0x563222 / 0x563202*/
}

// ===========================================================================
// gilde.exe 0x5643e8 — VIBE_Privilege_PanelConvert.
// Mass religious conversion: sweeps the 768-person array; for each living person of
// kind<10 (and, in the passive path, not 6/7) whose religion differs from the
// actor's, rolls (charm132 > resistance + RandomModulo(0x7E)); on success writes a
// religion-delta packet. Charges a 6%-wealth fee. Office path counts conversions.
// ===========================================================================
char PrivilegePanelConvert(const PrivPerson* actor, const PrivEvent* ev,
                           PrivilegePanelHooks* h,
                           const PrivPerson* people, int peopleCount) {
    if (!actor) return 32;
    (void)ev;

    if (actor->kind == 6) {
        // --- office GUI path (0x5643fb) ---
        char result = 0;
        i32 wealth = h->computeTotalWealth(actor, h->ctx);              /*0x56456c*/
        int cost = PrivCostFromWealth(wealth, kPrivConvertWealthFactor);/*0x56457a*/
        if (h->trace) { h->trace->cost = cost; h->trace->lastFormScene = 0x56459a; }
        u8 charm = actor->charm132;                                      /*0x56465d*/

        int btn;
        while ((btn = h->nextButton(h->ctx)) != kPrivLoopExit) {
            if (btn == kPrivLoopIdle) continue;
            if (btn == kPrivBtnOk) {                                     /*ChildObjectId*/
                if (h->checkSkill(actor, 6, h->ctx)
                    && h->checkResource(cost, actor->byte994, h->ctx)) {
                    Emit(h, PrivCommand::kEnqueueCmd15, -1, actor->handle, cost, actor->byte994); /*0x5646b5*/
                    Emit(h, PrivCommand::kBuildOp90, actor->handle, -6);  /*0x5646c4*/
                    int v19 = 0;
                    for (int i = 0; i < peopleCount; ++i) {              /*0x56475e do/while*/
                        const PrivPerson& pp = people[i];
                        if (pp.id != 0xFFFF
                            && pp.kind < 10
                            && pp.religion != actor->religion) {        /*0x5646f1*/
                            int roll = (int)pp.byte994 + Rng(h, 0x7E);   /*0x564717*/
                            if (charm > roll) {
                                Emit(h, PrivCommand::kDeltaField, pp.handle, 12); /*0x564726*/
                                ++v19;                                    /*0x564747*/
                            }
                        }
                    }
                    if (h->trace) h->trace->lastMessageId = 6626;        /*0x56476f (count v19)*/
                    SetDone(h, 1); /*0x564785*/
                }
            } else if (btn == kPrivBtnCancelId) {
                SetDone(h, 1); /*0x56479f*/
            }
        }
        return result;
    }

    // --- passive path (0x56441a) ---
    u8 charm = actor->charm132;                                          /*0x56441a*/
    if (actor->office404 < 6) return 32;                                 /*0x564432*/
    i32 wealth = h->computeTotalWealth(actor, h->ctx);                   /*0x56444c*/
    int cost = PrivCostFromWealth(wealth, kPrivConvertWealthFactor);     /*0x56445a*/
    if (h->trace) h->trace->cost = cost;
    if (h->sumCurrencyHeld(actor, h->ctx) < cost) return 32;            /*0x56447c*/

    Emit(h, PrivCommand::kEnqueueCmd15, -1, actor->handle, cost, actor->byte994); /*0x564490*/
    Emit(h, PrivCommand::kBuildOp90, actor->handle, -6);                /*0x56449f*/
    for (int i = 0; i < peopleCount; ++i) {                             /*0x56454e do/while*/
        const PrivPerson& pp = people[i];
        if (pp.id != 0xFFFF) {                                          /*0x5644b1*/
            u8 k = pp.kind;
            if (k < 10 && k != 6 && k != 7 && pp.religion != actor->religion) { /*0x5644e5*/
                int roll = (int)pp.byte994 + Rng(h, 0x7E);             /*0x56450b*/
                if (charm > roll)
                    Emit(h, PrivCommand::kDeltaField, pp.handle, 12);   /*0x56451a*/
            }
        }
    }
    return 16;                                                          /*0x564554*/
}

// ===========================================================================
// gilde.exe 0x563f14 — VIBE_Privilege_PanelMakePeace.
// Reconciles two persons: writes positive relation values (15 + RandomModulo(0x28))
// both directions, charges a -3 skill cost, notifies office-holders. Office path
// picks two holders; passive path uses target/partner records.
// ===========================================================================
char PrivilegePanelMakePeace(const PrivPerson* actor, const PrivEvent* ev,
                             PrivilegePanelHooks* h) {
    if (!actor) return 32;

    if (actor->kind == 6) {
        // --- office GUI path (0x5640e4) ---
        char result = 0;
        const PrivPerson* p1 = h->pickOfficeHolder ? h->pickOfficeHolder(6628, h->ctx) : nullptr;
        if (!p1) return 0;
        const PrivPerson* p2 = h->pickOfficeHolder ? h->pickOfficeHolder(6629, h->ctx) : nullptr;
        if (!p2) return 0;                                              /*0x563f32*/
        if (h->trace) h->trace->lastFormScene = 0x564190;              // "frieden_stiften"

        int btn;
        while ((btn = h->nextButton(h->ctx)) != kPrivLoopExit) {
            if (btn == kPrivLoopIdle) continue;
            if (btn == kPrivBtnOk) {
                if (h->checkSkill(actor, 3, h->ctx)) {
                    int d1 = Rng(h, 0x28) + 15;                         /*0x564277*/
                    int d2 = Rng(h, 0x28) + 15;                         /*0x564282*/
                    Emit(h, PrivCommand::kCoord27, p1->handle, p2->handle, d1); /*0x5642a1*/
                    Emit(h, PrivCommand::kCoord27, p2->handle, p1->handle, d2); /*0x5642b8*/
                    Emit(h, PrivCommand::kBuildOp90, actor->handle, -3); /*0x5642c5*/
                    if (h->trace) h->trace->lastMessageId = 6631;       /*0x5642e3*/
                    if (IsOfficeHolderKind(p1->kind) && h->sendEntityMessage) { /*0x56439d*/
                        h->sendEntityMessage(p1->handle, 1418, h->ctx);
                        if (h->trace) h->trace->lastEntityMsgId = 6632;
                    }
                    if (IsOfficeHolderKind(p2->kind) && h->sendEntityMessage) { /*0x5643a8*/
                        h->sendEntityMessage(p2->handle, 1418, h->ctx);
                        if (h->trace) h->trace->lastEntityMsgId = 6632;
                    }
                    SetDone(h, 1); /*0x564389*/
                    result = 1;    /*0x56438f*/
                }
            } else if (btn == kPrivBtnCancelId) {
                SetDone(h, 1); /*0x5643b9*/
            }
        }
        return result;
    }

    // --- passive path (0x563f54). RNG drawn UP-FRONT before the office check. ---
    int v5 = Rng(h, 0x28) + 15;                                         /*0x563f54*/
    int v42 = Rng(h, 0x28) + 15;                                        /*0x563f57 / 0x563f6a*/
    if (actor->office404 < 3) return 32;                                /*0x563f74*/
    const PrivPerson* p1 = h->findRecord ? h->findRecord(ev->targetId, h->ctx) : nullptr;
    if (!p1) return 96;                                                 /*0x563f9c*/
    const PrivPerson* p2 = h->findRecord ? h->findRecord(ev->partnerId, h->ctx) : nullptr;
    if (!p2) return 96;
    Emit(h, PrivCommand::kCoord27, p1->handle, p2->handle, v5);         /*0x563fcd*/
    Emit(h, PrivCommand::kCoord27, p2->handle, p1->handle, v42);        /*0x563fe4*/
    Emit(h, PrivCommand::kBuildOp90, actor->handle, -3);               /*0x563ff1*/
    if (IsOfficeHolderKind(p1->kind) && h->sendEntityMessage) {        /*0x5640b6*/
        h->sendEntityMessage(p1->handle, 1418, h->ctx);
        if (h->trace) h->trace->lastEntityMsgId = 6632;
    }
    if (IsOfficeHolderKind(p2->kind) && h->sendEntityMessage) {        /*0x5640c1*/
        h->sendEntityMessage(p2->handle, 1418, h->ctx);
        if (h->trace) h->trace->lastEntityMsgId = 6632;
    }
    return 16;                                                          /*0x5640a5 / 0x5640c3*/
}

// ===========================================================================
// gilde.exe 0x563614 — VIBE_Privilege_PanelInterrogation.
// Charges actor -2 and target -3, writes a +456 status flag (jails/marks target),
// notifies office-holders. Office path: gate on (457 sign bit) "immune" (-> -127),
// pick a holder, run dialog. Passive path: office>=2 + not immune.
// ===========================================================================
char PrivilegePanelInterrogation(const PrivPerson* actor, const PrivEvent* ev,
                                 PrivilegePanelHooks* h) {
    if (!actor) return 32;

    if (actor->kind == 6) {
        // --- office GUI path (0x563730) ---
        if ((i8)actor->flag457 < 0) {                                   /*0x563748 (457 & 0x80)*/
            if (h->trace) h->trace->lastMessageId = 6368;
            return -127;                                                /*0x5638f9*/
        }
        const PrivPerson* tgt = h->pickOfficeHolder ? h->pickOfficeHolder(6615, h->ctx) : nullptr;
        if (!tgt) return 2;                                             /*0x563907*/
        if (h->trace) h->trace->lastFormScene = 0x5637ba;              // "verhoer"
        char result = 2;
        int btn;
        while ((btn = h->nextButton(h->ctx)) != kPrivLoopExit) {
            if (btn == kPrivLoopIdle) continue;
            if (btn == kPrivBtnOk) {
                if (h->checkSkill(actor, 2, h->ctx)) {
                    Emit(h, PrivCommand::kBuildOp90, actor->handle, -2); /*0x56388e*/
                    Emit(h, PrivCommand::kBuildOp90, tgt->handle, -3);  /*0x5638ac*/
                    // 0x56389a: mov ecx,8000h survives both BuildOp90 wrappers
                    // (EnqueuePacket pushes/pops ecx) -> Args25 a3 == 0x8000.
                    Emit(h, PrivCommand::kArgs25, actor->handle, 456, 0x8000, 4); /*0x5638bd*/
                    if (h->trace) h->trace->lastMessageId = 6617;       /*0x563935*/
                    if (IsOfficeHolderKind(tgt->kind) && h->sendEntityMessage) { /*0x5639b7*/
                        h->sendEntityMessage(tgt->handle, 1418, h->ctx);
                        if (h->trace) h->trace->lastEntityMsgId = 6618;
                    }
                    SetDone(h, 1); /*0x5639a2*/
                    result = -127; /*0x5639a8 (v32 = -127)*/
                }
            } else if (btn == kPrivBtnCancelId) {
                SetDone(h, 1); /*0x5639c3*/
            }
        }
        return result;
    }

    // --- passive path (0x56364e) ---
    if (actor->office404 < 2 || (i8)actor->flag457 < 0) return 32;      /*0x56364e*/
    const PrivPerson* tgt = h->findRecord ? h->findRecord(ev->targetId, h->ctx) : nullptr;
    if (!tgt) return 96;                                                /*0x5636f3*/
    Emit(h, PrivCommand::kBuildOp90, actor->handle, -2);               /*0x563682*/
    Emit(h, PrivCommand::kBuildOp90, tgt->handle, -3);                 /*0x563694*/
    // 0x56367d: mov ecx,8000h survives to the Args25 a3 slot.
    Emit(h, PrivCommand::kArgs25, actor->handle, 456, 0x8000, 4);      /*0x5636a5*/
    if (IsOfficeHolderKind(tgt->kind) && h->sendEntityMessage) {       /*0x563703*/
        h->sendEntityMessage(tgt->handle, 1418, h->ctx);
        if (h->trace) h->trace->lastEntityMsgId = 6618;
    }
    return 16;                                                          /*0x5636e5 / 0x563705*/
}

// ===========================================================================
// gilde.exe 0x5639f4 — VIBE_Privilege_PanelExpelWorker.
// Dismisses a worker. Passive path (mode 3 self / mode 4 drag-source): gate office
// >=5 and target (456 sign bit), set +456 flag, dismiss (Pair33). Office path:
// (456 sign) immune -> -127; pick a person; on confirm set +456, dismiss, pick a
// random replacement person matching the building, notify office-holders.
// ===========================================================================
char PrivilegePanelExpelWorker(const PrivPerson* actor, const PrivEvent* ev,
                               PrivilegePanelHooks* h) {
    if (!actor) return 96;

    if (actor->kind != 6) {
        // --- passive path (0x563a09) ---
        if (actor->office404 < 5) return 32;                            /*0x563a1d*/
        const PrivPerson* subject;
        if (ev->mode == 3 && (i8)(actor->flags456 & 0xFF) >= 0) {       /*0x563a2b (456 sign)*/
            subject = actor;
        } else if (ev->mode == 4 && ev->dragSource
                   && (i8)(ev->dragSource->flags456 & 0xFF) >= 0) {     /*0x563a55/0x563a6b*/
            subject = ev->dragSource;
        } else {
            return 96;
        }
        const PrivPerson* tgt = h->findRecord ? h->findRecord(ev->targetId, h->ctx) : nullptr;
        if (!tgt) return 96;                                            /*0x563a4d*/
        // 0x563a97: BuildOp90's second arg (edx) is a leftover register from the
        // preceding QueryBegin call — indeterminate in the binary; modeled as 0.
        Emit(h, PrivCommand::kBuildOp90, actor->handle, 0);            /*0x563a97*/
        // 0x563a92: mov ecx,80h survives to the Args25 a3 slot (flag byte value).
        Emit(h, PrivCommand::kArgs25, subject->handle, 456, 0x80, 4);  /*0x563aae*/
        if (IsOfficeHolderKind(tgt->kind) && h->sendEntityMessage) {   /*0x563b35*/
            h->sendEntityMessage(tgt->handle, 1418, h->ctx);
            if (h->trace) h->trace->lastEntityMsgId = 6623;
        }
        Emit(h, PrivCommand::kPair33, tgt->handle, 1);                 /*0x563b1f*/
        return 16;                                                      /*0x563b31*/
    }

    // --- office GUI path (0x563b5a) ---
    const PrivPerson* subject;
    if (ev->mode == 3) {                                                /*0x563b64*/
        subject = actor;
    } else if (ev->mode == 4 && ev->dragSource) {                      /*0x563d6b*/
        subject = ev->dragSource;
    } else {
        return 96;                                                      /*0x563d73*/
    }
    if ((i8)(subject->flags456 & 0xFF) < 0) {                           /*0x563b7f (456 sign)*/
        if (h->trace) h->trace->lastMessageId = 6368;
        return -127;                                                    /*0x563da8*/
    }
    // The original opens a worker-picker (MapView_PanelDispatcher). Modeled as the
    // primary target record.
    const PrivPerson* picked = h->findRecord ? h->findRecord(ev->targetId, h->ctx) : nullptr;
    if (!picked) return 0;                                              /*0x563db6*/
    if (h->trace) h->trace->lastFormScene = 0x563c2b;                  // "arbeiter_ausweisen"
    char result = 0;
    int btn;
    while ((btn = h->nextButton(h->ctx)) != kPrivLoopExit) {
        if (btn == kPrivLoopIdle) continue;
        if (btn == kPrivBtnOk) {
            if (h->checkSkill(actor, 5, h->ctx)) {
                Emit(h, PrivCommand::kBuildOp90, actor->handle, -5);    /*0x563d31*/
                // 0x563d24: mov ecx,80h -> Args25 a3 == 0x80.
                Emit(h, PrivCommand::kArgs25, subject->handle, 456, 0x80, 4); /*0x563d49*/
                if (h->trace) h->trace->lastMessageId = 6622;           /*0x563dd3*/
                // random replacement scan: RandomModulo(0x300) start (recorded as a
                // draw); the array scan is a coupled live-array walk (modeled via the
                // picked record as the replacement when present).
                Rng(h, 0x300);                                          /*0x563dfa*/
                if (IsOfficeHolderKind(picked->kind) && h->sendEntityMessage) { /*0x563ede*/
                    h->sendEntityMessage(picked->handle, 1418, h->ctx);
                    if (h->trace) h->trace->lastEntityMsgId = 6623;
                }
                Emit(h, PrivCommand::kPair33, picked->handle, 1);       /*0x563ed1*/
                SetDone(h, 1); /*0x563e47*/
            }
        } else if (btn == kPrivBtnCancelId) {
            SetDone(h, 1); /*0x563eea*/
        }
    }
    return result;
}

// ===========================================================================
// gilde.exe 0x560c1c — VIBE_Privilege_BlackmailConfirm.
// The blackmail success roll dialog (opened from the Blackmail panel). Subject must
// be kind 6/7. Success iff RandomModulo(8) <= matchCount (incriminating-evidence
// count). On success: advance game-time +96, enqueue a jail/slot-reset packet,
// poll for ack; if subject holds office (456 & 0x100) show "now holds office".
// On failure: relation hit (-26), -2 skill, "failed" message. Returns 1/0.
// ===========================================================================
int PrivilegeBlackmailConfirm(const PrivPerson* actor, const PrivPerson* subject,
                              PrivilegePanelHooks* h) {
    if (!actor || !subject) return 0;                                   /*0x560c46*/
    if (!IsOfficeHolderKind(subject->kind)) return 0;

    int matchCount = h->findMatchingEntityIds
        ? h->findMatchingEntityIds(subject->handle, actor->id, h->ctx) : 0; /*0x560c71*/
    if (h->trace) h->trace->lastFormScene = 0x560c81;                  // "erpressung2"

    int v8 = 0;
    int btn;
    while ((btn = h->nextButton(h->ctx)) != kPrivLoopExit) {
        if (btn == kPrivLoopIdle) continue;
        if (btn == kPrivBtnOk) {
            int roll = Rng(h, 8);                                       /*0x560d23*/
            if (roll <= matchCount) {                                   /*0x560d36 success*/
                // VIBE_GameTime_Advance(+96) then jail/slot-reset packet.
                Emit(h, PrivCommand::kSlotReset28, actor->handle, subject->handle, 96); /*0x560e3a*/
                if ((subject->flags456 & 0x100) != 0) {                 /*0x560e89*/
                    if (h->trace) h->trace->lastMessageId = 6490;       /*0x560eba*/
                }
                v8 = 1;        /*0x560e8b*/
                SetDone(h, 1); /*0x560e90*/
            } else {                                                    /*0x560d4f failure*/
                Emit(h, PrivCommand::kCoord27, actor->handle, subject->handle, -26); /*0x560d4f*/
                Emit(h, PrivCommand::kBuildOp90, actor->handle, -2);    /*0x560d5c*/
                if (h->trace) h->trace->lastMessageId = 6489;           /*0x560d78*/
                v8 = 0;        /*0x560d90*/
                SetDone(h, 1); /*0x560d92*/
            }
        } else if (btn == kPrivBtnCancelId) {
            v8 = 0;        /*0x560eed*/
            SetDone(h, 1); /*0x560eef*/
        }
    }
    return v8;                                                          /*0x560d9e*/
}

// ===========================================================================
// gilde.exe 0x560f14 — VIBE_Privilege_PanelBlackmail.
// Office path: builds a scroll-list of the player's office-holder entities (with
// evidence counts), then on a row-click with evidence>=3 runs BlackmailConfirm.
// Passive path: resolve target, roll RandomModulo(8) > matchCount -> relation -26;
// else advance time + jail packet. Returns 16/96 (passive) or dialog byte (office).
// ===========================================================================
char PrivilegePanelBlackmail(const PrivPerson* actor, const PrivEvent* ev,
                             PrivilegePanelHooks* h) {
    if (!actor) return 96;

    if (IsOfficeHolderKind(actor->kind)) {
        // --- office GUI path (0x560f49) ---
        if (h->trace) h->trace->lastFormScene = 0x560f49;             // "erpressung"
        char result = 32;
        int btn;
        while ((btn = h->nextButton(h->ctx)) != kPrivLoopExit) {
            if (btn == kPrivLoopIdle) continue;
            // A row click: nextButton delivers the picked subject id; resolve it.
            const PrivPerson* subject = h->findRecord ? h->findRecord(btn, h->ctx) : nullptr;
            if (!subject) continue;
            if (!h->checkSkill(actor, 2, h->ctx)) continue;            /*0x5611b1*/
            if ((subject->flag457 & 2) != 0) {                         /*0x5611c4 already-immune*/
                if (h->trace) h->trace->lastMessageId = 6482;
                continue;
            }
            // evidence count of the row must be >= 3 (v35[13] >= 3u)
            int matchCount = h->findMatchingEntityIds
                ? h->findMatchingEntityIds(subject->handle, actor->id, h->ctx) : 0;
            if (matchCount >= 3) {                                      /*0x56137c*/
                if (PrivilegeBlackmailConfirm(actor, subject, h))      /*0x56139f*/
                    SetDone(h, 1);                                      /*0x5613b5*/
                result = (char)-110;                                    /*0x5613a8 (0x92)*/
            } else {
                if (h->trace) h->trace->lastMessageId = -1; // dword_8C9BF4 generic /*0x561385*/
            }
        }
        return result;
    }

    // --- passive path (0x561201) ---
    const PrivPerson* subject = h->findRecord ? h->findRecord(ev->targetId, h->ctx) : nullptr;
    if (!subject) return 96;                                            /*0x5612f5*/
    int matchCount = h->findMatchingEntityIds
        ? h->findMatchingEntityIds(subject->handle, actor->id, h->ctx) : 0; /*0x56122a*/
    if (Rng(h, 8) > matchCount) {                                       /*0x561249 failure*/
        Emit(h, PrivCommand::kCoord27, actor->handle, subject->handle, -26); /*0x56131a*/
    } else {                                                            /*0x56126b success*/
        Emit(h, PrivCommand::kSlotReset28, actor->handle, subject->handle, 96); /*0x5612de*/
    }
    return 16;                                                          /*0x5612e5*/
}

// ===========================================================================
// gilde.exe 0x560500 — VIBE_Privilege_PanelMedicus.
// Heals/treats: charges a 2%-wealth fee, with a RandomModulo(2) coin flip choosing
// the success vs failure flavor text; on success transfers the fee + picks a random
// AiNeeds flag. Office path runs the dialog (return 2/129); passive path returns
// 17 (success/charged) / 0.
// ===========================================================================
char PrivilegePanelMedicus(const PrivPerson* actor, PrivilegePanelHooks* h) {
    if (!actor) return 0;

    if (IsOfficeHolderKind(actor->kind)) {
        // --- office GUI path (0x560623) ---
        i32 wealth = h->computeTotalWealth(actor, h->ctx);             /*0x560623*/
        int cost = PrivCostFromWealth(wealth, kPrivMedicusWealthFactor);/*0x560631*/
        if (h->trace) { h->trace->cost = cost; h->trace->lastFormScene = 0x560653; } // "medicus"
        char v40 = 2;
        int btn;
        while ((btn = h->nextButton(h->ctx)) != kPrivLoopExit) {
            if (btn == kPrivLoopIdle) continue;
            if (btn == kPrivBtnOk) {
                if (h->checkSkill(actor, 2, h->ctx)
                    && h->checkResource(cost, actor->byte994, h->ctx)) {
                    Emit(h, PrivCommand::kBuildOp90, actor->handle, -2); /*0x56073e*/
                    if (h->trace) h->trace->lastMessageId = 6460;       /*0x56074d*/
                    if (Rng(h, 2)) {                                    /*0x56075a success*/
                        Emit(h, PrivCommand::kEnqueueCmd15, -1, actor->handle, cost, actor->byte994); /*0x5607c6*/
                        // VIBE_AiNeeds_PickRandomFlagFromEight(a1)        /*0x5607cd*/
                    }
                    Emit(h, PrivCommand::kArgs25, actor->handle, 456, 64, 4); /*0x560800*/
                    SetDone(h, 1); /*0x56086b*/
                    v40 = (char)129; /*0x5607ef (0x81)*/
                }
            } else if (btn == kPrivBtnCancelId) {
                SetDone(h, 1); /*0x560882*/
            }
        }
        return v40;
    }

    // --- passive path (0x560540) ---
    if (actor->office404 < 2) return 0;                                 /*0x560540*/
    i32 wealth = h->computeTotalWealth(actor, h->ctx);                  /*0x56056b*/
    int cost = PrivCostFromWealth(wealth, kPrivMedicusWealthFactor);    /*0x560579*/
    if (h->trace) h->trace->cost = cost;
    // 0x560584: mov edx,-2 before the BuildOp90 call — the passive path also
    // charges the -2 skill cost; 0x560589: mov ecx,40h -> Args25 a3 == 0x40.
    Emit(h, PrivCommand::kBuildOp90, actor->handle, -2);               /*0x56059d*/
    Emit(h, PrivCommand::kArgs25, actor->handle, 456, 0x40, 4);        /*0x5605af*/
    if (Rng(h, 2)) {                                                    /*0x5605b9 success*/
        Emit(h, PrivCommand::kEnqueueCmd15, -1, actor->handle, cost, actor->byte994); /*0x5605e7*/
        // VIBE_Building_AdjustStockAndNotify + AiNeeds_PickRandomFlagFromEight       /*0x5605f4*/
    }
    return 17;                                                          /*0x5605c3*/
}

// ===========================================================================
// gilde.exe 0x5608b0 — VIBE_Privilege_PanelDivorce.
// Splits a married couple. Passive path: office>=8, afford gate (currency/wealth >=
// 12%), write the partner-link delta to both records (offset +0x5C), transfer an
// 8%-wealth fee, return 17/34. Office path: 8%-fee + 4%-fee text variants, on
// confirm write the +0x5C link on both + fee, set done. Returns 17/34 or dialog.
// ===========================================================================
char PrivilegePanelDivorce(const PrivPerson* actor, PrivilegePanelHooks* h) {
    if (!actor) return 34;

    if (IsOfficeHolderKind(actor->kind)) {
        // --- office GUI path (0x5609dd) ---
        i32 wealth = h->computeTotalWealth(actor, h->ctx);             /*0x5609dd*/
        int costA = PrivCostFromWealth(wealth, kPrivDivorceWealthFactorA); /*0x5609e7 8%*/
        int costB = PrivCostFromWealth(wealth, kPrivDivorceWealthFactorB); /*0x5609f6 4%*/
        if (h->trace) { h->trace->cost = costA; h->trace->lastFormScene = 0x560a1f; } // "scheidung"
        const PrivPerson* partner = h->findRecord ? h->findRecord(actor->partnerId, h->ctx) : nullptr;
        (void)costB;
        char result = 0;
        int btn;
        while ((btn = h->nextButton(h->ctx)) != kPrivLoopExit) {
            if (btn == kPrivLoopIdle) continue;
            if (btn == kPrivBtnOk) {                                    /*0x560aff*/
                if (h->checkSkill(actor, 8, h->ctx)
                    && h->checkResource(costA, actor->byte994, h->ctx)) {
                    Emit(h, PrivCommand::kBuildOp90, actor->handle, -8); /*0x560b35*/
                    Emit(h, PrivCommand::kDeltaField, actor->handle, 0x5C);  /*0x560b46/0x560b55*/
                    if (partner)
                        Emit(h, PrivCommand::kDeltaField, partner->handle, 0x5C); /*0x560b6a/0x560b7e*/
                    Emit(h, PrivCommand::kEnqueueCmd15,
                         partner ? partner->handle : -1, actor->handle, costA, actor->byte994); /*0x560b9f*/
                    if (h->trace) h->trace->lastMessageId = 0x1990;     /*0x560bd4*/
                    SetDone(h, 1); /*0x560bdc (dword_631614 = 1)*/
                }
            } else if (btn == kPrivBtnCancelId) {
                SetDone(h, 1); /*0x560bfc*/
            }
        }
        return result;
    }

    // --- passive path (0x5608e4) ---
    if (actor->office404 < 8) return 34;                                /*0x5608e4*/
    i32 wealth = h->computeTotalWealth(actor, h->ctx);                  /*0x5608f6 (discarded)*/
    (void)wealth;
    i64 net = h->sumCurrencyHeld(actor, h->ctx);                       /*0x5608ff*/
    // afford gate 0x56091f: `(double)(int)v4 / (double)SHIDWORD(v4) < dbl_624B74`
    // — the binary DIVIDES currency (lo dword) by wealth (hi dword) and compares
    // the quotient against 0.12. Reproduced as the same division.
    i32 totalWealth = h->computeTotalWealth(actor, h->ctx);
    if ((double)(i32)net / (double)totalWealth < kPrivDivorceAffordRatio) /*0x56091f*/
        return 34;                                                      /*0x5608e6*/
    int fee = PrivCostFromWealth(totalWealth, kPrivDivorceWealthFactorA); /*0x560929 8%*/
    if (h->trace) h->trace->cost = fee;
    const PrivPerson* partner = h->findRecord ? h->findRecord(actor->partnerId, h->ctx) : nullptr;
    Emit(h, PrivCommand::kBuildOp90, actor->handle, 0);               /*0x56094c*/
    Emit(h, PrivCommand::kDeltaField, actor->handle, 0x5C);            /*0x560956/0x560977*/
    if (partner)
        Emit(h, PrivCommand::kDeltaField, partner->handle, 0x5C);      /*0x560986/0x5609a7*/
    Emit(h, PrivCommand::kEnqueueCmd15,
         partner ? partner->handle : -1, actor->handle, fee, actor->byte994); /*0x5609c3*/
    return 17;                                                          /*0x5609c8*/
}

// ===========================================================================
// gilde.exe 0x5647c8 — VIBE_Privilege_PanelApology.
// Pays compensation to a rival to reduce hostility. Non-office (passive) path: gate
// dragField 1..9 and <= office404, request rival-entity-pairs + charge skill cost,
// return 96 on out-of-range. Office path (kind 6): run dialog; on confirm with skill
// requirement, charge -cost, request rival pairs, show result message. Returns
// 96/0 (passive) or the dialog byte (1 on confirm).
// ===========================================================================
char PrivilegePanelApology(const PrivPerson* actor, const PrivEvent* ev,
                           PrivilegePanelHooks* h) {
    if (!actor) return 96;

    if (actor->kind != 6) {                                             /*0x5647da*/
        if (ev->dragField < 1) return 96;                              /*0x5647e0*/
        int v4 = ev->dragField;
        if (v4 > 9 || v4 > actor->office404) return 96;                /*0x5647f0*/
        Emit(h, PrivCommand::kRivalPairs, actor->handle, ev->dragField); /*0x5647f7*/
        Emit(h, PrivCommand::kBuildOp90, actor->handle, -ev->dragField); /*0x564804*/
        return 0;                                                       /*0x56480f*/
    }

    // --- office GUI path (0x564831) ---
    if (h->trace) h->trace->lastFormScene = 0x564831;                 // "abbitte"
    char result = 0;
    int btn;
    while ((btn = h->nextButton(h->ctx)) != kPrivLoopExit) {
        if (btn == kPrivLoopIdle) continue;
        if (btn == kPrivBtnOk) {                                        /*ChildObjectId confirm*/
            // VIBE_Object_GetDataPtr provides the chosen apology amount; modeled as
            // the event dragField (the slider value).
            if (h->checkSkill(actor, ev->dragField, h->ctx)) {         /*0x564912*/
                Emit(h, PrivCommand::kBuildOp90, actor->handle, -ev->dragField); /*0x564922*/
                Emit(h, PrivCommand::kRivalPairs, actor->handle, 0);   /*0x56492c*/
                if (h->trace) h->trace->lastMessageId = 6635;          /*0x56493c*/
                SetDone(h, 1); /*0x564954*/
                result = 1;    /*0x56495a*/
            }
        } else if (btn == kPrivBtnCancelId) {
            SetDone(h, 1); /*0x56496e*/
        }
    }
    return result;
}

// ===========================================================================
// gilde.exe 0x5627cc — VIBE_Privilege_CharmConfirm.
// Charm/persuade. Office path (kind 6/7): open office-overview routed to PanelCharm;
// return 1 on success else 0. Passive path: gate dragField in [1,5] and <= office404
// and a resolvable target; charge -penalty (distance-based) skill cost + write the
// relation value; return 16, else 96.
// ===========================================================================
int PrivilegeCharmConfirm(const PrivPerson* actor, const PrivEvent* ev,
                          PrivilegePanelHooks* h) {
    if (!actor) return 96;

    if (IsOfficeHolderKind(actor->kind)) {                             /*0x5627eb*/
        // VIBE_Amt_RunOfficeOverviewWindow(..., PanelCharm). Modeled via the
        // office-holder picker; success -> 1.
        const PrivPerson* picked = h->pickOfficeHolder
            ? h->pickOfficeHolder(6574, h->ctx) : nullptr;             /*0x5628a1*/
        return picked ? 1 : 0;                                          /*0x5628aa / 0x5628b2*/
    }

    // --- passive path (0x5627f3) ---
    int v4 = ev->dragField;                                             /*0x5627f3*/
    if (actor->office404 >= v4 && v4 >= 1 && v4 <= 5) {                /*0x56281d*/
        const PrivPerson* tgt = h->findRecord ? h->findRecord(ev->targetId, h->ctx) : nullptr;
        if (tgt) {
            // VIBE_AiMethod_ComputeDistancePenalty(level, actor, target) -> v11.
            // The penalty is the relation value written; modeled as the relation
            // lookup actor->target (the matrix entry used as the coord value).
            int penalty = h->relationEntry(actor->id, tgt->id, h->ctx); /*0x56282f*/
            Emit(h, PrivCommand::kBuildOp90, actor->handle, -v4);      /*0x562836*/
            Emit(h, PrivCommand::kCoord27, actor->handle, tgt->handle, penalty); /*0x562846*/
            return 16;                                                  /*0x56284b*/
        }
    }
    return 96;                                                          /*0x562804*/
}

// ===========================================================================
// gilde.exe 0x565304 — VIBE_Privilege_PanelChangeProfession.
// Lets the player change a worker's profession via a 12-cell building-type grid.
// Office-holder (kind 6) only. Builds the grid (categories from
// Building_GetCategoryForObject), then on a grid-cell click that differs from the
// current profession and passes the skill gate, confirms and emits a profession
// change (BuildOp72 newProf + BuildOp90 cost). Returns 16 on confirm, else 0.
// ===========================================================================
char PrivilegePanelChangeProfession(const PrivPerson* actor, PrivilegePanelHooks* h) {
    if (!actor) return 0;
    if (actor->kind != 6) return 0;                                    /*0x565317*/

    if (h->trace) h->trace->lastFormScene = 0x565350;                 // "berufwechseln"
    char result = 0;
    int btn;
    while ((btn = h->nextButton(h->ctx)) != kPrivLoopExit) {
        if (btn == kPrivLoopIdle) continue;
        // A grid cell click delivers the chosen profession index (>=1). The current
        // profession is actor->profession-equivalent (religion-area high byte +353);
        // we model "new != current" via btn != actor->rank as the change gate is on
        // the building category vs the actor's current. The skill cost is 4 (already
        // that category) or 8 (new category); modeled as 8.
        int newProf = btn;
        if (newProf == (int)actor->rank) continue;                     /*0x565772 same-prof skip*/
        if (!h->checkSkill(actor, 8, h->ctx)) continue;                /*0x565772*/
        if (h->confirmBox && !h->confirmBox(6455, h->ctx)) continue;   /*0x565816 confirm dialog*/
        Emit(h, PrivCommand::kBuildOp72, actor->handle, newProf);      /*0x56582f*/
        Emit(h, PrivCommand::kBuildOp90, actor->handle, 0);            /*0x565845*/
        SetDone(h, 1); /*0x56584c*/
        result = 16;   /*0x565852*/
    }
    return result;
}

// ===========================================================================
// Dispatcher — leaf-id (absolute gilde.exe address) -> reconstructed panel.
// ===========================================================================
int PrivilegeDispatchPanelA(int leafId, const PrivPerson* actor,
                            const PrivEvent* ev, PrivilegePanelHooks* h) {
    switch (leafId) {
        case 0x563000: return PrivilegePanelGenerateHatred(actor, ev, h);
        case 0x565304: return PrivilegePanelChangeProfession(actor, h);
        case 0x5639f4: return PrivilegePanelExpelWorker(actor, ev, h);
        case 0x560f14: return PrivilegePanelBlackmail(actor, ev, h);
        case 0x563f14: return PrivilegePanelMakePeace(actor, ev, h);
        case 0x563614: return PrivilegePanelInterrogation(actor, ev, h);
        case 0x560500: return PrivilegePanelMedicus(actor, h);
        case 0x5608b0: return PrivilegePanelDivorce(actor, h);
        case 0x5647c8: return PrivilegePanelApology(actor, ev, h);
        case 0x5627cc: return PrivilegeCharmConfirm(actor, ev, h);
        // Convert (0x5643e8) needs the live person array; dispatched separately.
        default: return 0;
    }
}

} // namespace guild::world
