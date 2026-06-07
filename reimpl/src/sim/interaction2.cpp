// interaction2 — panel/drag-drop event state-machine slice. See interaction2.h.
// 1:1 translation of the VIBE_Interaction_* 0x595xxx/0x596xxx family.
#include "sim/interaction2.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// Global runtime state + hooks (defined ONCE here — the library home).
// ---------------------------------------------------------------------------
Interaction2State g_i2;
Interaction2Hooks g_i2Hooks;

void ResetInteraction2State() { g_i2 = Interaction2State{}; }
void ResetInteraction2Hooks() { g_i2Hooks = Interaction2Hooks{}; }

namespace {

// 589*index + dword_13CE294 record kind/state byte.
inline u8 BuildingKindOf(u8 index) {
    return g_i2Hooks.buildingKindOf ? g_i2Hooks.buildingKindOf(index) : 0;
}

// Stamp a state transition into the panel: state = subState = newState; +8 = ts.
// (Several handlers write state and subState together then store dword_62EB38.)
inline void StampTransition(PanelObject* p, u8 newState) {
    p->state = newState;
    p->subState = newState;
    p->lastEventTs = g_i2.eventTs;
}

// Write only the subState (the "*v1 = v1[1]" idiom copies subState into state and
// stamps the ts; this helper sets subState first then mirrors it).
inline void StampSubStateMirror(PanelObject* p, u8 newSub) {
    p->subState = newSub;
    p->state = p->subState;       // *v1 = v1[1]
    p->lastEventTs = g_i2.eventTs;
}

} // namespace

// 0x595e54
bool IsPanelModeTwo() {
    return g_i2.panelActive && g_i2.panel && g_i2.panel->panelMode == 2;
}

// 0x595f70
bool IsPanelActive() {
    if (!g_i2.panelActive)
        return true;
    return g_i2.panel->panelMode != 2 && g_i2.panel->state != 5;
}

// 0x595e74
int InvokeHandlerSlot60(char a, int b, int c, int d) {
    if (!g_i2.panelActive)
        return 1;
    PanelObject* p = g_i2.panel;
    PanelHandler* slot = p ? p->handlerSlot : nullptr;
    if (!slot)
        return 1;
    if (g_i2.suppress)
        return 0;
    if (!slot->hasSlot60)
        return 1;
    // Original argument order to the +60 slot: (a1=al, a2=edx, a4=ebx, a3=ecx).
    return g_i2Hooks.handlerSlot60 ? g_i2Hooks.handlerSlot60(a, b, d, c) : 1;
}

// 0x595ed4
bool TestHandlerFlagDword(int mask) {
    if (!g_i2.panelActive)
        return true;
    PanelObject* p = g_i2.panel;
    if (p->panelMode == 2 || p->state == 5)
        return false;
    PanelHandler* slot = p->handlerSlot;
    if (!slot)
        return true;
    if (g_i2.suppress)
        return false;
    return (mask & slot->flagDword) != 0;
}

// 0x595f1c
bool TestHandlerFlagWord(short mask) {
    if (!g_i2.panelActive)
        return true;
    PanelObject* p = g_i2.panel;
    if (p->panelMode == 2 || p->state == 5)
        return false;
    PanelHandler* slot = p->handlerSlot;
    if (!slot)
        return true;
    if (g_i2.suppress)
        return false;
    return static_cast<unsigned short>(mask & slot->flagWord) != 0;
}

// 0x595f90
int AllowDropOnShop(const InteractionInputEvent* ev) {
    if (ev->type == 26) {
        DropTarget* t = ev->target;
        if (t) {
            if (BuildingKindOf(t->recordIndex) == 18 &&
                g_i2.curEventCode == 116) {
                return 0;
            }
        }
    }
    return 1;
}

// 0x5960dc
int CheckTargetStateOpen(const InteractionInputEvent* ev) {
    if (ev->type == 25) {
        DropTarget* t = ev->target;
        if (t) {
            if (BuildingKindOf(t->recordIndex) != 15)
                return 0;
        }
    }
    return 1;
}

// 0x59611c
int CheckTargetStateOpenOrTwo(const InteractionInputEvent* ev) {
    if (ev->type == 25) {
        DropTarget* t = ev->target;
        if (t) {
            u8 k = BuildingKindOf(t->recordIndex);
            if (k != 15 && k != 2)
                return 0;
        }
    }
    return 1;
}

// 0x596160
int CheckTargetStateOpenTwoOr18(const InteractionInputEvent* ev) {
    if (ev->type == 25) {
        DropTarget* t = ev->target;
        if (t) {
            u8 k = BuildingKindOf(t->recordIndex);
            if (k != 15 && k != 2 && k != 18)
                return 0;
        }
    }
    return 1;
}

// 0x595fd8
int MatchObjectDropTarget(const InteractionInputEvent* ev) {
    if (ev->type == 25 && ev->target) {
        return (BuildingKindOf(ev->target->recordIndex) == 15 && ev->code == 270)
                   ? 1 : 0;
    } else if (ev->type == 26 && ev->target) {
        return (BuildingKindOf(ev->target->recordIndex) != 15 ||
                g_i2.curEventCode != 270)
                   ? 1 : 0;
    } else {
        return ev->type != 27 ? 1 : 0;
    }
}

// 0x5961ac
int HandlePickupDropTransition(const InteractionInputEvent* ev) {
    PanelObject* p = g_i2.panel;
    if (ev->type == 10) {
        if (ev->phase == 0 && p->subState == 4) {
            StampTransition(p, 9);
            return 1;
        }
        if (ev->phase == 1) {
            u8 v2 = p->subState;
            if (v2 == 9) {
                p->state = 4;
                p->subState = 4;
                p->lastEventTs = g_i2.eventTs;
                return 1;
            }
            if (v2 == 10) {
                p->state = 11;
                p->subState = 11;
                p->lastEventTs = g_i2.eventTs;
                return 1;
            }
        }
    } else if (ev->type == 7 && ev->target && p->subState == 9 &&
               ev->target->peerKind == 5) {
        p->state = 10;
        p->subState = 10;
        p->lastEventTs = g_i2.eventTs;
        return 1;
    }
    return 3;
}

// 0x596268
int HandleEvent13SetState11(const InteractionInputEvent* ev) {
    if (ev->type != 13)
        return 3;
    DropTarget* t = ev->target;
    if (!t || BuildingKindOf(t->recordIndex) != 10)
        return 3;
    StampTransition(g_i2.panel, 11);
    return 1;
}

// 0x5962c0
int HandleEvent23Or24SetState(const InteractionInputEvent* ev) {
    if (ev->type == 23) {
        StampTransition(g_i2.panel, 10);
        return 1;
    } else if (ev->type == 24) {
        StampTransition(g_i2.panel, 11);
        return 1;
    }
    return 3;
}

// 0x59632c / 0x596364 / 0x596540 — event 25 + subCode == N → state 11.
static int HandleEvent25Code(const InteractionInputEvent* ev, u16 code) {
    if (ev->type != 25)
        return 3;
    DropTarget* t = ev->target;
    if (!t || t->subCode != code)
        return 3;
    StampTransition(g_i2.panel, 11);
    return 1;
}
int HandleEvent25Code270(const InteractionInputEvent* ev) { return HandleEvent25Code(ev, 270); }
int HandleEvent25Code272(const InteractionInputEvent* ev) { return HandleEvent25Code(ev, 272); }
int HandleEvent25Code116(const InteractionInputEvent* ev) { return HandleEvent25Code(ev, 116); }

// 0x59639c
int HandleEvent26Code272(const InteractionInputEvent* ev) {
    if (ev->type != 26)
        return 3;
    DropTarget* t = ev->target;
    if (!t || t->subCode != 272)
        return 3;
    StampTransition(g_i2.panel, 11);
    return 1;
}

// 0x5963d4
int HandleEvent27Or28State13(const InteractionInputEvent* ev) {
    PanelObject* p = g_i2.panel;
    if (ev->type == 27 && ev->target && p->subState == 4) {
        if (BuildingKindOf(ev->target->recordIndex) == 13) {
            StampTransition(p, 10);
            return 1;
        }
    } else if (ev->type == 28) {
        DropTarget* t = ev->target;
        if (t) {
            if (BuildingKindOf(t->recordIndex) == 13) {
                StampTransition(p, 11);
                return 1;
            }
        }
    }
    return 3;
}

// 0x5964a8
int HandleEvent25Or26Code19(const InteractionInputEvent* ev) {
    PanelObject* p = g_i2.panel;
    if (ev->type == 25 && ev->target && p->subState == 4) {
        if (ev->target->subCode == 19) {
            StampTransition(p, 10);
            return 1;
        }
    } else if (ev->type == 26) {
        DropTarget* t = ev->target;
        if (t) {
            if (t->subCode == 19) {
                StampTransition(p, 11);
                return 1;
            }
        }
    }
    return 3;
}

// Shared body for the storage/workshop/house drop-step machines. `hoverKind` is
// the building kind required during the event-43 hover; `commitType` is the event
// type that commits the drop (16 storage/house, 17 workshop); `codes`/`nCodes`
// are the accepted hover action codes.
static int HandleDropStep(const InteractionInputEvent* ev, u8 hoverKind,
                          u8 commitType, const int* codes, int nCodes) {
    PanelObject* p = g_i2.panel;
    if (ev->type == 43) {
        DropTarget* t = ev->target;
        if (t) {
            if (BuildingKindOf(t->recordIndex) == hoverKind &&
                t->buildingId == g_i2.selectedBuildingId &&
                p->dragSubState == 0) {
                int code = ev->code;
                bool match = false;
                for (int i = 0; i < nCodes; ++i)
                    if (code == codes[i]) { match = true; break; }
                if (!match)
                    return 3;
                p->dragSubState = 1;
                StampSubStateMirror(p, 10);
            }
            return 3;
        }
    }
    if (ev->type != commitType)
        return 3;
    DropTarget* t = ev->target;
    if (!t || BuildingKindOf(t->recordIndex) != hoverKind ||
        t->buildingId != g_i2.selectedBuildingId) {
        return 3;
    }
    if (ev->phase == 0 && p->dragSubState == 0)
        p->subState = 9;
    if (ev->phase == 1)
        p->subState = (p->dragSubState == 1) ? 11 : 4;
    p->state = p->subState;
    p->lastEventTs = g_i2.eventTs;
    return 1;
}

// 0x596578
int HandleStorageDropStep(const InteractionInputEvent* ev) {
    static const int kCodes[] = {12, 479};
    return HandleDropStep(ev, /*kind*/18, /*commit*/16, kCodes, 2);
}
// 0x59671c
int HandleWorkshopDropStep(const InteractionInputEvent* ev) {
    static const int kCodes[] = {121, 122, 120};
    return HandleDropStep(ev, /*kind*/18, /*commit*/17, kCodes, 3);
}
// 0x5968b8
int HandleHouseDropStep(const InteractionInputEvent* ev) {
    static const int kCodes[] = {5, 2, 13, 8};
    return HandleDropStep(ev, /*kind*/2, /*commit*/16, kCodes, 4);
}

// 0x5966a0
int AllowStorageDropTarget(const InteractionInputEvent* ev) {
    if (ev->type != 43 || !ev->target ||
        BuildingKindOf(ev->target->recordIndex) != 18 ||
        ev->target->buildingId != g_i2.selectedBuildingId ||
        (ev->code != 12 && ev->code != 479)) {
        u8 ty = ev->type;
        if (ty != 26 && ty != 25 && ty != 28)
            return 0;
    }
    return 1;
}

// 0x596848
int AllowWorkshopDropTarget(const InteractionInputEvent* ev) {
    bool a = ev->type == 43 && ev->target &&
             BuildingKindOf(ev->target->recordIndex) == 18 &&
             ev->target->buildingId == g_i2.selectedBuildingId &&
             (ev->code == 121 || ev->code == 122 || ev->code == 120);
    return (a || AllowDropOnShop(ev)) ? 1 : 0;
}

// 0x5969e8
int AllowHouseDropTarget(const InteractionInputEvent* ev) {
    if (ev->type != 43 || !ev->target ||
        BuildingKindOf(ev->target->recordIndex) != 2 ||
        ev->target->buildingId != g_i2.selectedBuildingId ||
        (ev->code != 5 && ev->code != 2 && ev->code != 13 && ev->code != 8)) {
        u8 ty = ev->type;
        if (ty != 26 && ty != 25 && ty != 28)
            return 0;
    }
    return 1;
}

// 0x596aa8
int HandleEvent32DropOnShop(const InteractionInputEvent* ev) {
    if (ev->type != 32)
        return AllowDropOnShop(ev);
    if (ev->code != 341 || g_i2.panel->panelMode != 3)
        return 0;
    return 1;
}

// 0x596b84
int HandleSermonDropStep(const InteractionInputEvent* ev) {
    PanelObject* p = g_i2.panel;
    if (p->dragSubState >= 0 && ev->type == 37 && ev->code == 341) {
        p->dragSubState = 1;
        return 1;
    } else if (p->dragSubState >= 1 && ev->target && ev->type == 36) {
        if (BuildingKindOf(ev->target->recordIndex) == 10) {
            p->dragSubState = 2;
            return 1;
        }
        return 0;
    } else {
        return AllowDropOnShop(ev);
    }
}

// 0x596c80
int HandleEvent41ShopBusy(const InteractionInputEvent* ev) {
    PanelObject* p = g_i2.panel;
    if (p->dragSubState <= 0)
        return 1;
    if (ev->type != 41 || ev->code != 341)
        return 0;
    p->dragSubState = 1;
    return 1;
}

// 0x596d28
int HandleMultiStageDrop(const InteractionInputEvent* ev) {
    PanelObject* p = g_i2.panel;
    if (p->dragSubState >= 0 && ev->type == 40 && ev->code == 341) {
        p->dragSubState = 1;
        return 1;
    } else if (p->dragSubState >= 1 && ev->type == 39 && ev->code == 464) {
        p->dragSubState = 2;
        return 1;
    } else if (p->dragSubState >= 2 && ev->target && ev->type == 36 &&
               BuildingKindOf(ev->target->recordIndex) == 18 &&
               ev->target->buildingId == g_i2.selectedBuildingId) {
        p->dragSubState = 3;
        return 1;
    } else {
        return (ev->type == 26 || ev->type == 28) ? 1 : 0;
    }
}

// 0x596ea4
int HandleConfirmDropStep(const InteractionInputEvent* ev) {
    PanelObject* p = g_i2.panel;
    if (p->dragSubState < 0 || ev->type != 38 || ev->code != 464)
        return (ev->type == 26 || ev->type == 28) ? 1 : 0;
    p->dragSubState = 1;
    return 1;
}

// 0x596ccc
int QueryEventCodeRange146() {
    if (!g_i2.curEventPresent)
        return -1;
    switch (g_i2.curEventCode) {
        case 0x92: case 0x93: case 0x94:
        case 0x95: case 0x96: case 0x97:
            if (g_i2.panel->dragSubState)
                return -1;
            return g_i2.curEventCode != 146 ? 1 : 0;
        default:
            return -1;
    }
}

// 0x596e1c
int QueryEventStateRange() {
    if (!g_i2.curEventPresent)
        return -1;
    switch (g_i2.curEventCode) {
        case 0x92: case 0x93: case 0x94:
        case 0x95: case 0x96: case 0x97: {
            int v0 = g_i2.panel->dragSubState;
            if (v0) {
                if (v0 == 1) {
                    if (g_i2.curEventCode == 149)
                        return g_i2.panel->dragSubState;
                    return 4;
                } else if (v0 == 2) {
                    return 2;
                } else {
                    return -1;
                }
            } else if (g_i2.curEventCode == 146) {
                return 0;
            } else {
                return 3;
            }
        }
        default:
            return -1;
    }
}

// 0x596d1c
int GetDragSubState() {
    return g_i2.panel->dragSubState;
}

// 0x596ee8
int HandleBookPageTurn() {
    PanelObject* p = g_i2.panel;
    if (p->bookMode == 2 && p->bookObj) {
        if (g_i2Hooks.bookTurnForward)
            g_i2Hooks.bookTurnForward(p->bookObj);
        return 0;
    } else if (p->bookMode == 3 && p->bookObj) {
        if (g_i2Hooks.bookTurnBackward)
            g_i2Hooks.bookTurnBackward(p->bookObj);
        return 0;
    }
    return 0;
}

// 0x596f2c
char OpenTownHallDialog(char precheck, int actorCtx) {
    if (precheck)
        return precheck;
    // dword_631744 hovered-target short-circuit: if a kind-18 target is hovered,
    // do nothing (the original returns the table-base low byte, which we model as
    // the precheck pass-through 0).
    if (g_i2.hoverTargetPresent &&
        BuildingKindOf(g_i2.hoverTargetIndex) == 18) {
        return 0;
    }
    int person = g_i2Hooks.personQueryBegin
                     ? g_i2Hooks.personQueryBegin(actorCtx, 1, 5, 18) : 0;
    if (!person)
        return 0;
    int node = g_i2Hooks.gameObjectQueryFind
                   ? g_i2Hooks.gameObjectQueryFind(person, 1, 0, 116) : 0;
    if (!node)
        return 0;
    return g_i2Hooks.dialogOpenBuilding ? g_i2Hooks.dialogOpenBuilding(node, 0) : 0;
}

} // namespace guild::sim
