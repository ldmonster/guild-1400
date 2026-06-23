#pragma once
// ===========================================================================
// play::SessionInput — the REAL input -> selection frame path, packaged as a
// session-consumable module (wave-2 swaps the city session's renderer Pick for
// this chain).
//
// THE RECOVERED ORIGINAL FRAME SEQUENCE (VIBE_GameLogic_RunFrameLoop @0x4c09a0,
// every step cited — note the ONE-FRAME event latency this produces: the mouse
// device is polled at the END of a frame (events spill into the ring), and the
// edges are consumed from the ring at the HEAD of the NEXT frame):
//
//   1. 0x4c09bf -> 0x40dab8  VIBE_Input_LatchMouseState  (RECONSTRUCTED HERE):
//        when dword_62D0D4: mirror cursor <- current packet (dword_672210 =
//        dword_672174, word_672214 = word_672178); zero the mirror EDGE
//        dwords (67221C/672228/672230/67223C/672240/67224C — the HELD flags
//        672220/672234/672244 persist); pop the FIRST pending event-ring row
//        (dword_671028[0] head, then 76-byte steps < 2432) into the mirror
//        block 0x672210.. and clear its flag; tail-call the keyboard device
//        poll (0x40dbff).  THE MIRROR IS WHAT THE SELECTION GATE READS.
//
//   2. mid-frame, VIBE_Widget_DispatchMouseClick @0x4215b0: MainLoop
//        (ax = word_75BF4A, dx = word_75BF48 — the cursor latched at the END
//        of the PREVIOUS frame, step 7) — see steps 3..5 below; and
//        VIBE_GameLogic_RunFrameLoop 0x4c0e19..0x4c0e96: the selection
//        commit/reset/status latch (step 6).
//
//   7. 0x4c0f21  VIBE_Input_PollMouseDevice — the device step (bound to shim):
//        drains events into the LIVE block 0x6721C4.., spilling one ring row
//        per event burst through VIBE_Input_ProcessMouseClicks @0x40cdd0
//        (RECONSTRUCTED HERE) and latching the current packet 0x672174.
//
//   8. 0x4c0f26..0x4c0f41: when dword_62D0D4 != 0,
//        word_75BF4A = *(u16*)0x672174 (X), word_75BF48 = *(u16*)0x672176 (Y).
//
// THE POLL ORCHESTRATOR (init path, no direct xref):
//   0x40da88  VIBE_Input_PollMouseAndKeyboard  (play/input_recon_select)
//        - device step 0x40d388 VIBE_Input_PollMouseDevice: drains the
//          DirectInput mouse event ring into the LIVE input block 0x6721C4..
//          (cursor packed dword_6721C4 = X lo / Y hi word, per-event deltas
//          word_6721CA/word_6721CC, wheel word_6721C8/word_6721CE +
//          dword_672204/dword_672208, button transition flags
//          dword_6721D0/D4/DC, dword_6721E4/E8/F0, dword_6721F4/F8/672200),
//          calling VIBE_Input_SaveMouseButtonSnapshot @0x40d338 (gui recon) and
//          VIBE_Input_ProcessMouseClicks @0x40cdd0 per event.  The DirectInput
//          ring decode itself is the platform boundary (rule 4); this module
//          BINDS the device step to the per-frame SHIM input, reproducing the
//          exact global writes — including the original's own absolute-cursor
//          path (0x40d74c..0x40d786: the GetCursorPos branch overwrites the
//          accumulated cursor with the clamped OS cursor; SDL's absolute cursor
//          IS that OS cursor).  Mouse-mickey sensitivity scaling
//          ((double)dword_62D0B8 * flt_610C5C * data, 0x40d588) belongs to the
//          relative DirectInput decode and stays on the platform side.
//        - VIBE_Input_ProcessMouseClicks @0x40cdd0 is RECONSTRUCTED 1:1 HERE
//          (Input_ProcessMouseClicks below): the click / double-click / drag
//          window state machine + the event-ring spill (unk_670FE0) + the
//          packet latch  qmemcpy(&dword_672174, &dword_6721C4, 0x4C).
//        - packet mirror copy (the 0x40da88 body): dword_672210 <- dword_672174
//          (0x4C bytes) — the "previous frame" packet the SELECTION GATE reads.
//        - device step 0x40d920 VIBE_Input_PollKeyboardDevice: the keyboard
//          ring drain is the boundary; its PURE auto-repeat tail over the key
//          tables (byte_671D60 down / byte_671F60 released / byte_67225C last
//          key, repeat latch byte_62D100 + dword_672260: +9 tick initial delay,
//          +5 tick repeat, release sentinel 1316134911) is RECONSTRUCTED HERE
//          (Input_PollKeyboardLatch) and fed from shim key events.
//
//   3. 0x414a38  VIBE_GameTick_MainLoop (sim/gametick_entityscan_recon), called
//        from VIBE_Widget_DispatchMouseClick @0x4215b0 as
//        MainLoop(ax = word_75BF4A, dx = word_75BF48): the dual 512-slot
//        entity hit-test scan over dword_62D26C with the REAL screen boxes,
//        writing dword_62D294/dword_62D290/dword_62D240/dword_62D22C and
//        returning the action code (stored to dword_75BF40, gui::g_hoverPrev).
//
//   4. 0x4147cc  VIBE_SelectEntity_ComputeResult (play/input_recon_select) —
//        MainLoop's miss fallback: the 48-actor (dword_676A60) selection-volume
//        hit-test over the child-hotspot lists (dword_67EB80) and rect records
//        (dword_69FFB4).  Its dword_62D22C / dword_62D290 stores are merged
//        back into the scan state (one shared dword in the original).
//
//   5. 0x4215c9 / 0x4218e0 (VIBE_Widget_DispatchMouseClick): dword_75BF08 =
//        (dword_62D290 == -1) ? -1 : dword_62D290 — the "modal widget under
//        the cursor" the selection gate tests.
//
//   6. 0x4b950c  Selection_CommitContact (play/session_select) — THE
//        SET-SELECTION, run every frame (RunFrameLoop 0x4c0e82); its gate reads
//        the REAL packet mirror: dword_67221C (left-button-UP edge mirror —
//        selection commits on RELEASE), cursor (*(i32*)0x67220E)>>16 /
//        (*(i32*)0x672210)>>16, the viewport dword_63CC4C/50/54/58,
//        dword_75BF08 == -1 and dword_62D31C == -1.  An empty hover latch
//        takes the real empty-click branch -> VIBE_Selection_Reset @0x4b9444.
//
//   7. 0x4bc280 tail — Selection_UpdateStatusTextLatch (play/session_select).
//
// HOVER-LATCH BOUNDARY (named gap, same as session_select): the original fills
// the hover latch (dword_631724/63172C/631730/631734/63173C) in
// VIBE_Object_UpdateGateContact @0x4b8ba8 (live scene-graph walk).  Here the
// MainLoop/SelectEntity pick result is resolved into the latch through the
// provider's resolveContact hook; the default materialises shadow records
// carrying exactly the byte fields 0x4b950c dereferences (the session_select
// precedent), from the registered entity descriptors.
//
// ENTITY-RECORD PROVIDER: the hit-test tables are renderer/widget-owned in the
// original (dword_62D26C slots point INTO the 740-byte dword_69FFB4 record
// table — record dword +0 is the record's own table index, see
// VIBE_Widget_DispatchMouseClick 0x421613 `dword_69FFB4 + 740*dword_62D22C`).
// SessionInputProvider exposes them as hooks; the REAL default is an in-tree
// record store that materialises 740-byte records with the EXACT original
// field layout from per-frame screen-space descriptors (wave 2 feeds them from
// the renderer's projections).  Field semantics (byte offsets, all 16.16 reads
// as *(i32*)(rec+off)>>16 == the i16 word at off+2):
//   +0   i32  own table index (the dword_62D22C value on a pick)
//   +8   i32  action code (MainLoop's return)
//   +14/+18 dword>>16 (= words @16/@20): box left / width   (px range)
//   +16/+20 dword>>16 (= words @18/@22): box top  / height  (py range)
//   +24  u8   type byte (64 = hover-able [the dword_69FFB4 +24 gate]; the
//             second scan special-cases 9 -> 1155/1210 and 65 -> pick-enable)
//   +26/+28/+30/+32 dword>>16 (= words @28/@30/@32/@34): the sub-node-less
//             second-scan pick range (x lo / x hi / py lo / py hi)
//   +44  ptr  sub-node (clipped-content pick range; default store: null)
//   +52  i32  busy A (must be 0)        +56 i32 busy B (!=0 -> tentative pick)
//   +60  ptr  child record; *(child+408) must be 0
//   +68/+72/+80 i32 pick-enable dwords (second-scan gate with +24==65)
//   +116 i32  group id (the dword_62D290/62D294 value; scan 1 also needs
//             dword_67EDE4[238*groupId] != 0)
//   +444 u8   bit 0x10 with type 9 -> return 1155 (else 1210)
// The SelectEntity fallback's 48-actor records (0x2AC bytes: live dwords
// +400/+408/+412, anim name slots +428/+492, hotspot-list count +388 / ids
// +4*i+4), the hotspot LIST records (dword_67EB80, decoded count+id-array at
// the hook boundary — see input_recon_select.h on the packed +24/+26 overlap)
// and the rect records (the same 740-byte table: box words @16..@22, +8 action,
// +24 type [64 = store-secondary-and-continue], +44 secondary ptr, +116
// type-64 secondary) are registered the same way.  The selection volume
// (VIBE_Pick_ComputeSelectionVolume @0x5b7134) routes to the REAL kernel
// picksel_recon ComputeSelectionVolumeSolve over provider-supplied corner
// extents; the corner fill (VIBE_Object_ComputeBoneScreenExtents) is
// renderer-owned — a NAMED GAP when absent (the actor sub-slot is skipped,
// the original's exact miss path).
// ===========================================================================
#include "guild/common/types.h"
#include "play/input_recon_select.h"      // poll orchestrator / SelectEntity / Reset
#include "play/picksel_recon.h"           // ComputeSelectionVolumeSolve / ProjectParams
#include "play/session_select.h"          // Selection_CommitContact / gate / latch
#include "sim/gametick_entityscan_recon.h" // GameTickMainLoop / GameTickScanState

#include <cstdint>
#include <functional>
#include <vector>

namespace guild::play {

using guild::i16;
using guild::i32;
using guild::u16;
using guild::u32;
using guild::u8;

// ===========================================================================
// 0x40cdd0 — VIBE_Input_ProcessMouseClicks: module-owned state globals
// (BSS, zero at load; none modelled elsewhere — ODR-grepped).
// ===========================================================================
struct InputClickState {
    i32 g62D0E0 = 0;   // dword_62D0E0 — input disabled (early-out, no latch)
    i32 g62D0DC = 0;   // dword_62D0DC — widget consumed the click (set by
                       //   VIBE_Widget_DispatchMouseClick 0x421657; swallows
                       //   the left-up in ProcessMouseClicks 0x40ce31)
    i32 g62D0E8 = 0;   // dword_62D0E8 — left-press tick (drag/double-click window)
    i32 g62D0EC = 0;   // dword_62D0EC — right-press tick
    i32 g62D0F0 = 0;   // dword_62D0F0 — middle-press tick
    i32 g62D0F4 = 0;   // dword_62D0F4 — press cursor X
    i32 g62D0F8 = 0;   // dword_62D0F8 — press cursor Y
    i32 g62D0FC = 0;   // dword_62D0FC — click-time latch (-1 = none)
    i32 g67195C = 0;   // dword_67195C — event-ring spill disable
};
extern InputClickState g_inputClick;

// dword_62EB44 view — the master tick clock the click windows / key repeat use
// (crt::Clock masterTicks_ is the live counter; the session feeds the value
// per frame through SessionInputFrame::clock).
extern u32 g_inputClock;          // dword_62EB44

// The mouse event ring ProcessMouseClicks spills into (unk_670FE0): 32 rows of
// 19 dwords (76 bytes) — each row a 0x4C packet copy of dword_6721C4..; the
// row-occupied flag is the row's last dword (dword_671028[19*row] == byte
// offset +72 of the row).  Producer side of app/menu_loop's consumer model.
inline constexpr int kMouseRingRows     = 32;
inline constexpr int kMouseRingRowBytes = 76;   // 19 dwords (0x4C)
extern u8 g_mouseEventRing[kMouseRingRows * kMouseRingRowBytes]; // unk_670FE0

// gilde.exe 0x40cdd0 — VIBE_Input_ProcessMouseClicks.  Pure state machine over
// the gui::g_mouseInput block + the globals above; ends with the packet latch
// qmemcpy(&dword_672174, &dword_6721C4, 0x4C).  Returns the original's eax
// (the clock when input is disabled, else 76).
i32 Input_ProcessMouseClicks();

// ===========================================================================
// 0x40d920 — VIBE_Input_PollKeyboardDevice: the pure auto-repeat tail.
// Key tables (none modelled elsewhere; byte_67225C REUSES
// play::g_lastHotkeyChar from input_recon4_hotkey).
// ===========================================================================
extern u8  g_keyDownTable[256];     // byte_671D60 — scancode currently held
extern u8  g_keyReleasedTable[256]; // byte_671F60 — scancode released (edge)
extern u8  g_keyRepeatLatch;        // byte_62D100 — auto-repeat scancode latch
extern u32 g_keyRepeatDueTick;      // dword_672260 — repeat schedule tick

// One drained keyboard event (the DirectInput ring entry's data byte + the
// DIDEVICEOBJECTDATA dwData sign bit: pressed == (data & 0x80) != 0).
struct SessionKeyEvent {
    u8   scancode = 0;
    bool pressed  = false;
};

// gilde.exe 0x40d920 (tail) — drain `events` into the key tables and run the
// auto-repeat latch 1:1: press -> byte_67225C = sc, byte_671D60[sc] = 1,
// byte_62D100 = sc, dword_672260 = clock + 9; held + repeat due
// (dword_672260 + 5 < clock) -> re-latch byte_67225C; latch released ->
// byte_62D100 = 0, dword_672260 = 1316134911, return 0xFF (the original's -1).
// Returns the original's al (the held latch scancode, else byte_67225C).
u8 Input_PollKeyboardLatch(const SessionKeyEvent* events, int count);

// ===========================================================================
// 0x40dab8 — VIBE_Input_LatchMouseState (RunFrameLoop head, 0x4c09bf):
// the per-frame event consumer (see step 1 of the banner).  The keyboard tail
// call (0x40dbff) is bound to the supplied shim key events.
// ===========================================================================
// dword_62D0D4 — "software cursor" mode (the city scenes set it to 1; the ==0
// branch reads the OS cursor instead — platform boundary).
extern i32 g_softCursorMode;       // dword_62D0D4
// word_75BF4A / word_75BF48 — the widget-frame cursor latch MainLoop consumes
// (written at the END of each frame from the current packet, 0x4c0f2f..).
extern i16 g_widgetCursorX;        // word_75BF4A
extern i16 g_widgetCursorY;        // word_75BF48

// gilde.exe 0x40dab8 — VIBE_Input_LatchMouseState.  Returns the keyboard
// device-poll result (the original tail-calls PollKeyboardDevice(0)).
u8 Input_LatchMouseState(const SessionKeyEvent* keys, int keyCount);

// ===========================================================================
// Per-frame shim input (the SDL boundary feed — shim::MouseState + key events).
// ===========================================================================
struct SessionInputFrame {
    i32  mouseX = 0;        // absolute cursor, window pixels (shim::MouseState)
    i32  mouseY = 0;
    bool left   = false;    // button held states this frame
    bool right  = false;
    bool middle = false;
    i32  wheel  = 0;        // wheel data units (DirectInput dwData; 0 = none)
    u32  clock  = 0;        // dword_62EB44 master tick for this frame
    const SessionKeyEvent* keys = nullptr;  // drained key events
    int  keyCount = 0;
};

// ===========================================================================
// Entity / actor descriptors for the default record store (exact original
// record semantics — see the header banner field table).
// ===========================================================================
struct SessionInputEntity {
    i32 id         = 0;     // record +0 (own table index) — the picked id
    i32 actionCode = 0;     // record +8 — MainLoop's return on a pick
    bool slotted   = true;  // present in the 512-slot dword_62D26C scan table
                            // (false = rect/type record only, reachable by id —
                            //  a record is EITHER scanned [+44 = sub-node, kept
                            //  null here] OR a hotspot rect [+44 = secondary
                            //  ptr]; the slot is overloaded in the original)
    // outer 16.16 screen box (hover scans + second-scan outer test):
    i32 left = 0, top = 0, width = 0, height = 0;   // words @16/@18/@20/@22
    i32 yMin = 0, yMax = 0;                          // words @32/@34
    // sub-node-less second-scan pick range (else-branch 0x414cf4):
    i32 innerLeft = 0, innerRight = 0;               // words @28/@30
    // +24 type byte.  TWO REAL PROFILES:
    //   * widget profile (typeByte 64): visible to the hover scans (writes
    //     dword_62D290/62D294 -> dword_75BF08 != -1 -> the commit gate's
    //     "modal widget under cursor" veto — clicking GUI never selects in 3D);
    //   * city-entity profile (typeByte 0, the default): picked by the second
    //     scan only (hover stays -1, the commit gate stays open) — the
    //     configuration the selection chain consumes.
    //   9 -> the 1155/1210 branch; 65 -> the pick-enable alternative.
    u8  typeByte = 0;
    // when the record is a hotspot RECT with type 64, the +116 dword is the
    // store-secondary-and-continue payload instead of a group id.
    u8  flag444  = 0;        // +444 (bit 0x10 + type 9 -> 1155)
    i32 groupId  = 0;        // +116 (dword_62D290/62D294 value)
    bool groupGate = true;   // dword_67EDE4[238*groupId] != 0
    i32 busy52 = 0, busy56 = 0;                      // +52 / +56
    i32 pickEnable68 = 1, pickEnable72 = 0, pickEnable80 = 0; // +68/+72/+80
    bool hasChild = false;   // +60 child record present
    i32  childBusy408 = 0;   // *(child+408)
    // --- contact resolution (the 0x4b8ba8 latch boundary; mirrors
    //     SessionSelectEntry so the commit output matches session_select) ---
    int  kind = 1;           // 1 object, 2 scene, 3 person
    int  typeCode = 0;       // dword_13CE27C type byte (29 = unselectable street)
    const char* handle = ""; // object record handle at +0 ("tp_TUER" == door)
    const char* name   = ""; // person name (+48) / object custom name (+5)
    bool selectable    = true;   // the dword_631730 verdict
    bool highlightable = true;   // record byte +529 bit 0
    u16  selectionFlags = 0x800; // ComputeSelectionFlags value (0x800 = own worker)
    // --- SelectEntity rect-record extras (when the record is referenced from a
    //     hotspot list): +44 secondary ptr / +116 doubles as type-64 secondary ---
    bool hasSecondary = false;   // +44 — pointer to the secondary id value
    i32  secondaryValue = 0;     // *(+44) (and the +116 type-64 store source)
};

struct SessionInputActor {
    int  index = 0;          // 0..47 — dword_676A60 + 0x2AC * index
    bool live  = true;       // +400 / +408 / +412 nonzero
    const char* anim0 = "";  // name slot 0 (record byte +428..)
    const char* anim1 = "";  // name slot 1 (record byte +492..)
    u32  animScale = 1;      // animation record +116
    const char* animName = ""; // animation record +0 (byte-pair name string)
    // selection volume — VIBE_Pick_ComputeSelectionVolume @0x5b7134 inputs (the
    // VIBE_Object_ComputeBoneScreenExtents corner fill is renderer-owned;
    // hasVolume=false leaves the sub-slot on the original miss path):
    bool  hasVolume = false;
    float cornerPos[4][3] = {};
    float cornerUV[4][2]  = {};
    ProjectParams proj{};
    int   halfTileFlag = 0;  // the hook's outHalfTileFlag (==1 adds 256 to Y)
    // child-hotspot lists: count at +388, list ids at +4*i+4 (max 8 here)
    int  listCount = 0;
    i32  listIds[8] = {};
};

// ===========================================================================
// Provider — the renderer-owned data boundary.  Null members fall back to the
// REAL default record store (fed via UpdateEntity / UpdateActor / ...).
// ===========================================================================
struct SessionInputProvider {
    // dword_62D26C / dword_69FFB4 / dword_67EDE4 — the scan tables.
    std::function<void(sim::GameTickScanState& st)> bindScanTables;
    // SelectEntityHooks (the 48-actor fallback tables) — installed before the
    // frame's MainLoop run.
    std::function<void()> bindSelectEntityHooks;
    // VIBE_Object_UpdateGateContact @0x4b8ba8 boundary: resolve the picked id
    // into the hover latch (g_selectionContact).  pickedId == -1 => leave the
    // latch empty (the commit's real empty-click branch).
    std::function<void(i32 pickedId)> resolveContact;
};

// ===========================================================================
// SessionInput — drives the chain; outputs match session_select's commit.
// ===========================================================================
class SessionInput {
public:
    SessionInput();

    // The 3D viewport rect the commit gate tests (dword_63CC4C/50/54/58).
    void SetViewport(i32 left, i32 top, i32 right, i32 bottom);
    // dword_62D4E8 — camera drag-anim active (gate veto). Default 0.
    void SetCameraDragActive(bool active);
    // dword_62D0D4 (g_softCursorMode) — nonzero selects the packet-cursor copy
    // into word_75BF4A/48 and the mirror-cursor latch (0x40dac1 / 0x4c0f26);
    // the ==0 path is the Win32 OS-cursor read.  The ctor sets 1 (city scene).
    void SetSoftwareCursor(bool on);

    // ---- default record store (REAL in-tree data; wave 2 feeds it from the
    //      renderer's per-frame projections) ----
    void ClearEntities();
    void UpdateEntity(const SessionInputEntity& e);   // upsert by id
    void ClearActors();
    void UpdateActor(const SessionInputActor& a);     // upsert by index
    void UpdateHotspotList(int listId, const i32* rectIds, int count);

    // Override the renderer-owned data boundary (null members keep defaults).
    void SetProvider(const SessionInputProvider& p);

    struct Result {
        u8   repeatScancode = 0;  // 0x40da88 return (the keyboard al)
        i32  actionCode = -1;     // dword_75BF40 (MainLoop @0x414a38 return)
        i32  pickedId = -1;       // dword_62D22C
        i32  hoverChildId = -1;   // dword_62D290
        i32  mainGroupId = -1;    // dword_62D294
        i32  secondaryId = -1;    // dword_62D240
        bool leftClickEdge = false;  // dword_6721DC event (mirror dword_672228)
        bool leftRelease = false;    // dword_6721D0 event (mirror dword_67221C)
        bool rightClickEdge = false; // dword_6721F0 event (mirror dword_67223C)
        bool doubleClick = false;    // dword_6721E0 event (mirror dword_67222C)
        bool commitRan = false;      // Selection_CommitContact gate passed
        bool selected = false;       // anchors hold a selection after the frame
        i32  selectedId = 0;         // picked entity id of the live selection
        int  selectedKind = 0;       // descriptor kind (1 obj / 2 scene / 3 person)
    };

    // Run one frame of the REAL chain (steps 1..7 of the header banner).
    Result Frame(const SessionInputFrame& in);

    // Selection state parity helpers (same stores SessionSelect reads).
    bool workerSelected(i32 personId) const;
    const char* selectedName() const;   // latched descriptor name ("" if none)

    // The deselect pair (right-click/ESC): Selection_Reset @0x4b9444 (+ pointer
    // anchor view) + Selection_ClearAll @0x4b94d8 + the status latch.
    void ClearSelection();

private:
    // 48-actor store entry: the 0x2AC actor record (dword_676A60 stride) + the
    // animation blob (name byte-pairs at +0, scale at +116) the hooks hand out.
    struct ActorRecord {
        bool used = false;
        u8   b[0x2AC] = {0};
        u8   anim[160] = {0};
        SessionInputActor desc{};
    };

    void installPollHooks(const SessionInputFrame& in);
    void installSelectEntityHooks();
    void installCommitHooks();
    void bindScanTables(sim::GameTickScanState& st);
    void defaultResolveContact(i32 pickedId);
    void rebuildEntityTable();

    // gate / config
    i32  vpLeft_ = 0, vpTop_ = 0, vpRight_ = 0, vpBottom_ = 0;
    bool cameraDrag_ = false;

    // previous-frame shim button state (the device-step transition source)
    bool prevLeft_ = false, prevRight_ = false, prevMiddle_ = false;

    SessionInputProvider provider_{};

    // ---- default record store (the 740-byte table itself lives in the .cpp
    //      as the module's dword_69FFB4 model, rebuilt per frame) ----
    std::vector<SessionInputEntity> descs_; // registered entity descriptors
    std::vector<const u8*> entityTable_;    // the 512-slot dword_62D26C view
    std::vector<i32> groupGate_;            // dword_67EDE4 (238-dword stride)
    std::vector<ActorRecord> actors_;       // 48-actor store (sparse by index)
    struct HotspotList { int listId; std::vector<i32> ids; };
    std::vector<HotspotList> lists_;        // dword_67EB80 (decoded)

    // latched selection descriptor (for Result parity fields)
    SessionInputEntity sel_{};
    bool selValid_ = false;

    // worker marks (byte_12CEA98 view — same modulo model as SessionSelect)
    static constexpr int kWorkerCount = 768;  // 411648 / 536
    u8 workerMarks_[kWorkerCount] = {0};
    // shadow records the default resolveContact materialises (0x4b950c fields)
    struct Shadow { u8 bytes[540]; };
    Shadow objShadow_{}, contactShadow_{}, personShadow_{};
};

} // namespace guild::play
