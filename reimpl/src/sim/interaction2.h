#pragma once
// interaction2 — the panel/drag-drop event state-machine slice of the Guild
// interaction system (gilde.exe, VIBE_Interaction_* family, 0x595xxx/0x596xxx
// range). These are the per-event handlers the active context panel runs while a
// player drags an object / drops it on a building (storage, workshop, house,
// town-hall, sermon, multi-stage office drops) and the small predicates that gate
// whether a drop target is allowed. The social-eval (interaction_eval.*) and the
// context-menu executors (interaction_handlers.*, contextaction2.*) are separate
// already-translated slices; this file does NOT redefine their layouts.
//
// Shared global runtime state (recovered, modeled as accessors here):
//   off_5953F0  (0x5953F0) — pointer to the live "active panel" object. The
//                handlers index it as a dword array; field offsets recovered from
//                the bodies (see PanelObject below).
//   dword_649CD0(0x649CD0) — panel-active flag (0 == no panel).
//   dword_62EB4C(0x62EB4C) — input-suppress flag (1 == swallow the event).
//   dword_62EB38(0x62EB38) — the current event timestamp written into panel+8 on
//                every state transition (so the panel knows when it last moved).
//   dword_13CE294(0x13CE294) — base of the 589-byte building-record table. A drop
//                target's first byte is its record index; record[0] is its kind/
//                state byte. Modeled via the BuildingKindOf hook.
//   dword_63174C(0x63174C) — pointer to the "current input event" record; its
//                first word is the event code (the QueryEvent* fns read it).
//   word_63CC5C (0x63CC5C) — the currently-selected building id (drop targets must
//                match this in their +39 word).
//   dword_631744(0x631744) — pointer to the hovered target record (OpenTownHall).
//
// Cross-module leaves (Book page turn, Person query, GameObject query, Dialog
// open, and the panel's own +60 handler slot) are routed through an installable
// Interaction2Hooks struct with inert defaults defined in interaction2.cpp.
//
// Translated functions (absolute addresses, imagebase 0x400000):
//   0x595e54 IsPanelModeTwo                 0x595e74 InvokeHandlerSlot60
//   0x595ed4 TestHandlerFlagDword           0x595f1c TestHandlerFlagWord
//   0x595f70 IsPanelActive                  0x595f90 AllowDropOnShop
//   0x595fd8 MatchObjectDropTarget          0x5960dc CheckTargetStateOpen
//   0x59611c CheckTargetStateOpenOrTwo      0x596160 CheckTargetStateOpenTwoOr18
//   0x5961ac HandlePickupDropTransition     0x596268 HandleEvent13SetState11
//   0x5962c0 HandleEvent23Or24SetState      0x59632c HandleEvent25Code270
//   0x596364 HandleEvent25Code272           0x59639c HandleEvent26Code272
//   0x596540 HandleEvent25Code116           0x5963d4 HandleEvent27Or28State13
//   0x5964a8 HandleEvent25Or26Code19        0x596578 HandleStorageDropStep
//   0x59671c HandleWorkshopDropStep         0x5968b8 HandleHouseDropStep
//   0x5966a0 AllowStorageDropTarget         0x5969e8 AllowHouseDropTarget
//   0x596848 AllowWorkshopDropTarget        0x596aa8 HandleEvent32DropOnShop
//   0x596b84 HandleSermonDropStep           0x596c80 HandleEvent41ShopBusy
//   0x596d28 HandleMultiStageDrop           0x596ea4 HandleConfirmDropStep
//   0x596ccc QueryEventCodeRange146         0x596e1c QueryEventStateRange
//   0x596d1c GetDragSubState                0x596ee8 HandleBookPageTurn
//   0x596f2c OpenTownHallDialog
#include "guild/common/types.h"

namespace guild::sim {

// ===========================================================================
// Active-panel object (off_5953F0). The handlers index it as `(_DWORD*)p + N`
// (offset 4*N) and `(_BYTE*)p + N`. Recovered fields:
//   +0x00 byte  state       — current panel state (written together with subState)
//   +0x01 byte  subState    — drag/transition sub-state (4 idle, 9/10/11 dragging)
//   +0x08 dword lastEventTs — gets dword_62EB38 on every transition (index 2)
//   +0x14 dword handlerSlot — ptr to per-panel handler vtable (index 5); +60 slot
//   +0x34 dword dragSubState— multi-stage drop progress (index 13)
//   +0x38 dword panelMode   — panel kind/mode (index 14): 2 special, 3 town-hall
//   +0x3C dword bookMode     — book reader mode (index 15): 2 fwd, 3 back
//   +0x4C dword bookObj      — ptr to the open book object (index 19)
//   +0x68 dword flagDword    — bitfield AND'd by TestHandlerFlagDword (at slot+104)
//   +0x6C word  flagWord     — bitfield AND'd by TestHandlerFlagWord (at slot+108)
// The flag fields live on the *handlerSlot* object, not the panel; we model both.
// This is a live runtime UI struct (never serialized) so it is NOT byte-packed.
// ===========================================================================
struct PanelHandler {
    int  flagDword = 0;          // handlerSlot + 104
    short flagWord = 0;          // handlerSlot + 108
    // The +60 handler-slot function pointer is modeled via Interaction2Hooks
    // (handlerSlot60), so production can wire the real per-panel handler.
    bool hasSlot60 = false;      // *(handlerSlot + 60) != 0
};

struct PanelObject {
    u8   state = 0;              // +0x00
    u8   subState = 0;           // +0x01
    int  lastEventTs = 0;        // +0x08 (index 2)
    PanelHandler* handlerSlot = nullptr; // +0x14 (index 5)
    int  dragSubState = 0;       // +0x34 (index 13)
    int  panelMode = 0;          // +0x38 (index 14)
    int  bookMode = 0;           // +0x3C (index 15)
    int  bookObj = 0;            // +0x4C (index 19)
};

// ===========================================================================
// Drop-target / event-target record (the pointer at event+4). The handlers read:
//   record[0]  byte  — building-record index into the 589-byte table (the kind/
//                       state byte is BuildingKindOf(record[0]))
//   record+0x27 word  — building id (matched against word_63CC5C)
// plus, for the "code" variants, the FIRST WORD of the record is read directly as
// an event sub-code (e.g. *v1 == 270/272/116/19). In the binary the same pointer
// is reinterpreted both ways; we expose both views.
// ===========================================================================
struct DropTarget {
    u8   recordIndex = 0;        // record[0]
    u16  buildingId  = 0;        // record + 0x27 (offset 39)
    u16  subCode     = 0;        // *(WORD*)record — event sub-code view
    u8   peerKind    = 0;        // record + 2 (HandlePickupDropTransition reads it)
};

// ===========================================================================
// Input event record (the `a1` the handlers receive). Recovered:
//   +0x00 byte  type    — event type discriminator (10,7,13,16,17,23..43,...)
//   +0x04 ptr   target  — DropTarget* (0 == none)
//   +0x08 dword phase   — drag phase / flag (0 begin, 1 commit)
//   +0x0C dword code    — action/param code (270,341,464,121,...)
// ===========================================================================
struct InteractionInputEvent {
    u8   type = 0;               // +0x00
    DropTarget* target = nullptr;// +0x04
    int  phase = 0;              // +0x08
    int  code = 0;               // +0x0C
};

// ===========================================================================
// Global runtime state. In the binary these are individual globals; here they are
// grouped so tests can set them up. Defined ONCE in interaction2.cpp.
// ===========================================================================
struct Interaction2State {
    PanelObject* panel = nullptr;     // off_5953F0
    bool   panelActive = false;       // dword_649CD0
    bool   suppress = false;          // dword_62EB4C
    int    eventTs = 0;               // dword_62EB38
    u16    selectedBuildingId = 0;    // word_63CC5C
    // dword_63174C: ptr to current event code word. Modeled as a value + present.
    bool   curEventPresent = false;
    u16    curEventCode = 0;
    // dword_631744: hovered target record (OpenTownHall). recordIndex only.
    bool   hoverTargetPresent = false;
    u8     hoverTargetIndex = 0;
};
extern Interaction2State g_i2;
void ResetInteraction2State();

// ===========================================================================
// Cross-module leaves (inert defaults defined in interaction2.cpp).
// ===========================================================================
struct Interaction2Hooks {
    // dword_13CE294 record kind/state lookup: BuildingKindOf(index) == the byte at
    // 589*index + tableBase. Default: returns 0.
    u8 (*buildingKindOf)(u8 recordIndex) = nullptr;
    // VIBE_Book_TurnPageForward @0x4be8b8 / Backward @0x4be8c8.
    void (*bookTurnForward)(int bookObj) = nullptr;
    void (*bookTurnBackward)(int bookObj) = nullptr;
    // VIBE_Person_QueryBegin @0x586c20 — returns a person record ptr (as opaque id;
    // 0 == none). OpenTownHall uses it (mode 1,5,18).
    int (*personQueryBegin)(int actorCtx, int a, int b, int c) = nullptr;
    // VIBE_GameObject_QueryFind @0x5857fc — returns matching node (opaque; 0 none).
    int (*gameObjectQueryFind)(int nodeKey, int a, int b, int c) = nullptr;
    // VIBE_Dialog_OpenBuildingForActiveChar @0x4adef4 — returns a verdict byte.
    char (*dialogOpenBuilding)(int nodeId, u8 charIndex) = nullptr;
    // The panel's own +60 handler slot. Returns the slot's result. Default: 1.
    int (*handlerSlot60)(char a, int b, int c, int d) = nullptr;
};
extern Interaction2Hooks g_i2Hooks;
void ResetInteraction2Hooks();

// ===========================================================================
// Translated functions. Signatures mirror the recovered ABI but take typed views.
// "BOOL" results are kept as int (1 allow / 0 reject) to match the binary.
// ===========================================================================

// 0x595e54 — true iff a panel is active and its panelMode (index 14) == 2.
bool IsPanelModeTwo();
// 0x595f70 — true iff no panel OR panelMode != 2 && state != 5 (event allowed).
bool IsPanelActive();

// 0x595e74 — if a panel + handlerSlot exist and not suppressed and the +60 slot is
// present, call it (returns its result); else 1; suppressed → 0.
int InvokeHandlerSlot60(char a, int b, int c, int d);

// 0x595ed4 / 0x595f1c — test the handler flag dword/word against a mask. Returns 1
// if no panel; 0 if panelMode==2 || state==5 || suppressed; else (mask & flag)!=0.
bool TestHandlerFlagDword(int mask);
bool TestHandlerFlagWord(short mask);

// 0x595f90 — reject (0) a drop of a kind-18 target whose subCode==116 onto event
// type 26; else allow (1).
int AllowDropOnShop(const InteractionInputEvent* ev);

// 0x5960dc / 0x59611c / 0x596160 — for an event type 25 with a target, allow only
// if the target's building kind is open(15) [/ or 2 / or 18]; else reject. Other
// event types allow.
int CheckTargetStateOpen(const InteractionInputEvent* ev);
int CheckTargetStateOpenOrTwo(const InteractionInputEvent* ev);
int CheckTargetStateOpenTwoOr18(const InteractionInputEvent* ev);

// 0x595fd8 — multi-branch object-drop match (event 25 kind15&code270, event 26
// not(kind15&&curEventCode270), else type!=27).
int MatchObjectDropTarget(const InteractionInputEvent* ev);

// 0x5961ac — pickup/drop state machine over event types 10 / 7. Returns 1 if a
// transition fired, else 3.
int HandlePickupDropTransition(const InteractionInputEvent* ev);

// 0x596268 — event 13 + target kind 10 → set panel state 11; returns 1 / 3.
int HandleEvent13SetState11(const InteractionInputEvent* ev);
// 0x5962c0 — event 23 → state 10, event 24 → state 11; returns 1 / 3.
int HandleEvent23Or24SetState(const InteractionInputEvent* ev);
// 0x59632c/0x596364/0x596540 — event 25 + target.subCode == 270/272/116 → state 11.
int HandleEvent25Code270(const InteractionInputEvent* ev);
int HandleEvent25Code272(const InteractionInputEvent* ev);
int HandleEvent25Code116(const InteractionInputEvent* ev);
// 0x59639c — event 26 + target.subCode == 272 → state 11.
int HandleEvent26Code272(const InteractionInputEvent* ev);
// 0x5963d4 — event 27 (subState 4) + target kind 13 → state 10; event 28 + target
// kind 13 → state 11; returns 1 / 3.
int HandleEvent27Or28State13(const InteractionInputEvent* ev);
// 0x5964a8 — event 25 (subState 4) + subCode 19 → state 10; event 26 + subCode 19
// → state 11; returns 1 / 3.
int HandleEvent25Or26Code19(const InteractionInputEvent* ev);

// 0x596578 / 0x59671c / 0x5968b8 — storage(kind18,code 12/479) / workshop(kind18,
// code 120/121/122) / house(kind2,code 2/5/8/13) drop step machines (event 43 hover
// then event 16/17). Return 1 if the drop committed, else 3.
int HandleStorageDropStep(const InteractionInputEvent* ev);
int HandleWorkshopDropStep(const InteractionInputEvent* ev);
int HandleHouseDropStep(const InteractionInputEvent* ev);

// 0x5966a0 / 0x596848 / 0x5969e8 — drop-target ALLOW predicates for the three step
// machines above (1 allow / 0 reject).
int AllowStorageDropTarget(const InteractionInputEvent* ev);
int AllowWorkshopDropTarget(const InteractionInputEvent* ev);
int AllowHouseDropTarget(const InteractionInputEvent* ev);

// 0x596aa8 — event 32 + code 341 + panelMode 3 → allow; non-32 falls through to
// AllowDropOnShop; otherwise the binary jumps to IsPanelModeTwo's tail (return 1).
int HandleEvent32DropOnShop(const InteractionInputEvent* ev);
// 0x596b84 — sermon drop (event 37 code341 → sub1, event 36 kind10 → sub2).
int HandleSermonDropStep(const InteractionInputEvent* ev);
// 0x596c80 — event 41 code341 while dragSubState>0 → reset to 1; returns 1 / 0.
int HandleEvent41ShopBusy(const InteractionInputEvent* ev);
// 0x596d28 — 3-stage office drop (event 40 c341 → 1, 39 c464 → 2, 36 kind18 → 3).
int HandleMultiStageDrop(const InteractionInputEvent* ev);
// 0x596ea4 — confirm step (event 38 code464 while dragSubState>=0 → set 1).
int HandleConfirmDropStep(const InteractionInputEvent* ev);

// 0x596ccc / 0x596e1c — query the current-event-code against the 0x92..0x97 range.
int QueryEventCodeRange146();
int QueryEventStateRange();
// 0x596d1c — read panel.dragSubState (index 13).
int GetDragSubState();

// 0x596ee8 — turn the open book's page based on bookMode (2 fwd / 3 back).
int HandleBookPageTurn();

// 0x596f2c — open the town-hall building dialog for the active char. `precheck`
// nonzero short-circuits (returns it). Returns the dialog verdict byte / precheck.
char OpenTownHallDialog(char precheck, int actorCtx);

} // namespace guild::sim
