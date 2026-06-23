#pragma once
// ===========================================================================
// gilde.exe — city-session SELECTION layer: the set-selection commit, the full
// worker-deselect, and the status-text latch (CLUSTER: VIBE_Object /
// VIBE_Selection / VIBE_Hud selection state).
//
// STRICT 1:1 reconstruction of the selection STATE machine recovered from the
// Hex-Rays decompile.  The recovered original pipeline (per frame, all driven
// from VIBE_GameLogic_RunFrameLoop @0x4c09a0, call sites 0x4c0e19/0x4c0e82/
// 0x4c0e91):
//
//   1. VIBE_Object_UpdateGateContact      @0x4b8ba8   (hover resolution)
//        writes the HOVER LATCH: dword_631724 (hovered object record),
//        dword_63172C (contact game-object sub-record), dword_631730
//        (contact "selectable" flag), dword_631734 (hovered person record),
//        dword_63173C (hovered door record).
//   2. VIBE_Object_ResolveQuickJumpContact @0x4b950c  (THE SET-SELECTION —
//        the missing counterpart of VIBE_Selection_Reset @0x4b9444)
//        commits the hover latch into the selection anchors on click:
//          dword_631740   = selected object record       (anchor Reset clears)
//          dword_11BC274  = selected contact sub-record  (anchor Reset clears)
//          dword_11BC278  = selected contact flag        (anchor Reset clears)
//          dword_11BC270  = selected worker record, dword_6317B0 = worker count,
//          byte_12CEA98[536*id] = worker mark, dword_62D098 = worker mesh,
//          dword_631720/631728/631738/11BC2F0/11BC2F4/11BC2F8/631E50 side state
//        and, when the click lands on NOTHING (empty latch) and no build-mode
//        override (byte_6317B4), calls VIBE_Selection_Reset — the real
//        "click empty ground deselects".
//   3. VIBE_Selection_Reset               @0x4b9444   (already reconstructed in
//        src/play/input_recon_select.{h,cpp}; REUSED here, not redefined)
//   4. VIBE_Selection_ClearAll            @0x4b94d8   (reconstructed HERE — the
//        full worker-mark deselect: dword_11BC270=0, dword_631740=0, the
//        byte_12CE880 536-stride sweep, dword_6317B0=0)
//   5. the status-text latch tail of VIBE_Hud_HandleMouseClick @0x4bc280
//        (0x4bc45a..0x4bc51b): when the selected owner record changes,
//        word_631758 = VIBE_Building_ComputeSelectionFlags(...) and
//        VIBE_StatusText_ResetEntries @0x4bcc4c fires (gui::ResetStatusText).
//
// SELECTION FEEDBACK recovered (the original's "selection visual"):
//   * selected-worker mesh tint — VIBE_Hud_UpdateSelectionAndTargets @0x4ba614
//     drives VIBE_Mesh_ResetVertexColors @0x428898 (reconstructed in
//     src/render/mesh_transform.*) per frame from the byte_12CEA98 marks.
//     The driver 0x4ba614 itself is NOT yet reconstructed (named gap).
//   * name captions — the status-text table dword_11B5220 drawn by
//     VIBE_Hud_DrawObjectNameLabels @0x4bbaec (reconstructed in
//     src/gui/hud_label_draw.*): caption anchored at the projected screen-bounds
//     centre, x = left + (right-left)/2 - 80, width 160, char height 40.
//   * floating click chatter — VIBE_Hud_DrawSelectedUnitInfo @0x4bae88 (text
//     keys 3756..3803 via VIBE_Text_RenderFormattedMessage). NOT reconstructed
//     (named gap; needs the economy/demand snapshot cluster).
//
// NAME RESOLUTION recovered (what the original shows for a selected entity):
//   * building/object info header (VIBE_InfoPanel_BuildStandard @0x4b6db8):
//       "$Z%s$A%s$A%s" = item label (VIBE_Text_FormatItemLabelWithIcon
//       @0x59ccf4, reconstructed in src/play/text_recon3_itemlabel.*),
//       the object-KIND name = text-array id (1078 + 14 * typeByte)  [the
//       gui::infopanel kNameTextBias/kNameTextStride constants], and the
//       record's own custom-name string at record+5.
//   * person info header (VIBE_InfoPanel_BuildPerson @0x4b7104):
//       VIBE_Text_FormatItemLabelWithIcon(*(u16*)record, kind=4) — job title
//       (string-table id) + "$A" + the person record's name field at +48.
//
// 64-BIT FIDELITY NOTE (same precedent as input_recon_select's
// g_selectionOwnerB): the anchor dwords dword_631740 / dword_11BC274 hold
// RECORD POINTERS in the 32-bit original.  input_recon_select models them as
// i32 because VIBE_Selection_Reset only ever zeroes them; this module carries
// the pointer-width view (SelectionAnchorRecords) of the SAME dwords as the
// live store.  SessionSelect::Clear() routes through the real Selection_Reset
// and zeroes the pointer view — the two views are the dual decode of one dword
// and never disagree (both are zero outside a live selection; the i32 view is
// not written during one).
// ===========================================================================
#include "guild/common/types.h"
#include "play/input_recon_select.h"   // Selection_Reset / g_selectionAnchors (REUSED)
#include "play/scene_pick.h"           // ScenePickResult (the session's pick result)

#include <cstdint>
#include <functional>

namespace guild::gui::text { class TextDb; }

namespace guild::play {

using guild::i16;
using guild::i32;
using guild::u16;
using guild::u8;

// ===========================================================================
// State the commit reads/writes (each field cites its gilde.exe dword).
// All exposed for tests, like input_recon_select's g_selectionAnchors.
// ===========================================================================

// The HOVER LATCH (written by VIBE_Object_UpdateGateContact @0x4b8ba8 in the
// original; 0x4b8ba8 needs the live scene-graph walk and is NOT reconstructed —
// named gap.  In this session the pick boundary fills the latch, see
// SessionSelect::OnPick).
struct SelectionContactLatch {
    u8* g631724 = nullptr; // dword_631724 — hovered object (scene mesh) record
    u8* g63172C = nullptr; // dword_63172C — hovered contact game-object sub-record
    i32 g631730 = 0;       // dword_631730 — contact "selectable" flag
    u8* g631734 = nullptr; // dword_631734 — hovered person (worker) record
    u8* g63173C = nullptr; // dword_63173C — hovered door/transport record
};
extern SelectionContactLatch g_selectionContact;

// Pointer-width view of the record-valued anchors (see 64-bit note above).
struct SelectionAnchorRecords {
    u8* a631740  = nullptr; // dword_631740  — selected object record
    u8* a11BC274 = nullptr; // dword_11BC274 — selected contact sub-record
    i32 a11BC278 = 0;       // dword_11BC278 — selected contact flag (int, not ptr)
};
extern SelectionAnchorRecords g_selectionAnchorRecords;

// Committed-selection side state.
struct SelectionCommitState {
    u8* g631720  = nullptr; // dword_631720  — committed object record
    u8* g631728  = nullptr; // dword_631728  — highlight (bone-transform) record
    u8* g631738  = nullptr; // dword_631738  — committed door record
    u8* g11BC2F0 = nullptr; // dword_11BC2F0 — committed contact sub-record
    i32 g11BC2F4 = 0;       // dword_11BC2F4 — committed contact flag
    u8* g11BC2F8 = nullptr; // dword_11BC2F8 — committed person record
    u8* g11BC270 = nullptr; // dword_11BC270 — selected worker record
    i32 g6317B0  = 0;       // dword_6317B0  — selected worker count/flag
    u8* g631E50  = nullptr; // dword_631E50  — last committed record (info-panel seed)
    u8* g62D098  = nullptr; // dword_62D098  — selected worker mesh record
    i32 g631610  = 0;       // dword_631610  — frame counter (input)
    i32 g63161C  = 0;       // dword_63161C  — highlight frame latch (output)
};
extern SelectionCommitState g_selectionCommit;

// Selection-owner records the quickjump block and the status latch read.
// dword_631748 is REUSED from input_recon_select (g_selectionOwnerB, already
// pointer-width there); 631744/63174C get their pointer-width view here.
struct SelectionOwnerRecords {
    u8* g631744 = nullptr; // dword_631744 — selected/entered building record
    u8* g63174C = nullptr; // dword_63174C — selected room/sub-object record
};
extern SelectionOwnerRecords g_selectionOwners;

// Pending QuickJump request (a scripted "jump to contact" the commit resolves).
inline constexpr int kQuickJumpNameBytes = 96; // VIBE_Object_FindByHandle(0, 96, ...)
struct QuickJumpRequest {
    i32 g11BC27C = 0;        // dword_11BC27C — request pending flag
    u8* g11BC280 = nullptr;  // dword_11BC280 — expected owner record
    u8* g11BC284 = nullptr;  // dword_11BC284 — expected room record (or 0 / type 253)
    char name[kQuickJumpNameBytes] = {0}; // byte_11BC290 — contact handle name
};
extern QuickJumpRequest g_quickJump;

// The commit's input gate (cursor inside the 3D viewport, no drag, no modal).
struct SelectGateState {
    i32 g67221C  = 0;  // dword_67221C — input/scene enabled
    i32 g62D4E8  = 0;  // dword_62D4E8 — camera drag-anim active
    i32 cursorX16 = 0; // unk_67220E   — cursor X, 16.16 fixed (>>16 = pixels)
    i32 cursorY16 = 0; // dword_672210 — cursor Y, 16.16 fixed (>>16 = pixels)
    i32 g63CC4C  = 0;  // dword_63CC4C — viewport left
    i32 g63CC50  = 0;  // dword_63CC50 — viewport top
    i32 g63CC54  = 0;  // dword_63CC54 — viewport right
    i32 g63CC58  = 0;  // dword_63CC58 — viewport bottom
    i32 g75BF08  = -1; // dword_75BF08 — modal widget (-1 == none)
    i32 g62D31C  = -1; // dword_62D31C — drag-cursor payload (-1 == none)
};
extern SelectGateState g_selectGate;

// Last QuickJump resolution error ("sv_HandleObjects(): Could not find
// QuickJump.ContactName: %s").  The original formats it into a stack buffer
// (v12/v13 @0x4b95bd/0x4b9609); exposed here for tests.
extern char g_quickJumpError[128];

// ===========================================================================
// Hooks — the coupled leaves the commit calls (same pattern as
// input_recon_select's SelectionResetHooks; inert defaults).
// ===========================================================================
struct SelectCommitHooks {
    // VIBE_Object_FindByHandle(0, 96, name, 0, 0) @0x5b7be4 — resolve a contact
    // handle name to its scene record.  Inert default: nullptr (not found).
    std::function<u8*(const char* name)> objectFindByHandle;

    // VIBE_Building_ComputeSelectionFlags @0x588dec (NOT yet reconstructed —
    // named gap; hooked exactly like sim/building6.h does).  Original signature
    // (activePlayer = word_63CC5C, a2, a3, a4); the commit calls it as
    // (word_63CC5C, 0, person, 0) and reads the HIGH byte (>>8).
    std::function<u16(u16 activePlayer, u8* a2, u8* a3, u8* a4)> computeSelectionFlags;

    // VIBE_Character_ApplyBoneTransform(rec, dword_6316C8) @0x4263fc — the
    // select-highlight bone pulse.  Inert default: no-op.
    std::function<void(u8* rec)> applyBoneTransform;

    // The record's handler block: (*(rec+468)+264)(0, 2) — a vtable-style
    // callback pointer INSIDE the record (cannot be modelled as raw offsets on
    // 64-bit; decoded at the hook boundary).  Inert default: no-op.
    std::function<void(u8* rec)> invokeHighlightHandler;

    // VIBE_Voice_PlayWorkerClickComment(person, id) @0x5823a4.  Default: no-op.
    std::function<void(u8* person, u16 personId)> voiceWorkerClickComment;

    // dword_13CE27C + 65*objId — object-type byte (same table as
    // SelectionResetHooks.objectTypeByte).  Default: 0.
    std::function<u8(int objId)> objectTypeByte;

    // The worker-table mark operations (the 536-byte-stride live person table;
    // it belongs to the sim cluster, so the table ops are the hook boundary —
    // identical reasoning to input_recon_select's hotspotListRecord):
    //   clearWorkerSelectionMarks: the byte_12CE880 sweep
    //       (for (i=0; i!=411648; ) { i+=536; byte_12CE880[i]=0; })
    //   markWorkerSelected(id):    byte_12CEA98[536*id] = 1
    //   workerMeshPtr(id):         dword_12CEA94[134*id]
    std::function<void()>          clearWorkerSelectionMarks;
    std::function<void(u16 id)>    markWorkerSelected;
    std::function<u8*(u16 id)>     workerMeshPtr;

    // VIBE_Selection_Reset @0x4b9444 — the empty-click deselect.  Default:
    // play::Selection_Reset(0) + zero g_selectionAnchorRecords (the pointer-width
    // dual view of the same stores).
    std::function<void()> selectionReset;
};

// Install the commit hooks (null entries restore the inert defaults).
void Selection_SetCommitHooks(const SelectCommitHooks& hooks);

// ===========================================================================
// gilde.exe 0x4b950c — VIBE_Object_ResolveQuickJumpContact.
// THE SET-SELECTION: commits g_selectionContact into the selection anchors
// (control flow 1:1, see .cpp).  Reads g_selectGate / g_quickJump /
// g_selectionOwners / g_selectionAnchors.g6317B4; writes g_selectionCommit /
// g_selectionAnchorRecords (+ the worker marks through the hooks).
// ===========================================================================
void Selection_CommitContact();

// ===========================================================================
// gilde.exe 0x4b94d8 — VIBE_Selection_ClearAll.
// The full worker-deselect: dword_11BC270 = 0; dword_631740 = 0; the
// byte_12CE880 536-stride sweep; dword_6317B0 = 0.  Returns the loop's final
// counter (411648), the original's eax.
// ===========================================================================
i32 Selection_ClearAll();

// ===========================================================================
// gilde.exe 0x4bc280 (tail 0x4bc45a..0x4bc51b) — the status-text latch of
// VIBE_Hud_HandleMouseClick: resolve the selected owner (dword_631744, else
// dword_631748), and when (owner, room, force) differ from the latch
// (dword_631E54 / dword_631E58 / dword_631754):
//   word_631758 = VIBE_Building_ComputeSelectionFlags(word_63CC5C, owner, 0, room)
//   VIBE_StatusText_ResetEntries()       (gui::ResetStatusText, @0x4bcc4c)
//   dword_631E54 = owner; dword_631754 = 0; dword_631E58 = room.
// ===========================================================================
struct SelectionStatusLatch {
    u8* g631E54 = nullptr; // dword_631E54 — latched owner record
    u8* g631E58 = nullptr; // dword_631E58 — latched room record
    i32 g631754 = 0;       // dword_631754 — force-refresh flag
    i16 w631758 = 0;       // word_631758  — latched selection flags
    u16 w63CC5C = 0;       // word_63CC5C  — active player id
};
extern SelectionStatusLatch g_selectionStatus;

struct StatusLatchHooks {
    // VIBE_Building_ComputeSelectionFlags @0x588dec (named gap, hooked).
    std::function<u16(u16 activePlayer, u8* owner, u8* a3, u8* room)> computeSelectionFlags;
    // VIBE_StatusText_ResetEntries @0x4bcc4c. Default: gui::ResetStatusText().
    std::function<void()> statusTextReset;
};
void Selection_SetStatusLatchHooks(const StatusLatchHooks& hooks);

// Returns true when the latch fired (selection changed -> status text reset).
bool Selection_UpdateStatusTextLatch();

// ===========================================================================
// SessionSelect — the city-session selection layer over the reconstructions.
//
// BOUNDARY (documented per the task): the original's hit-test
// (VIBE_Object_UpdateGateContact @0x4b8ba8 hover walk /
// VIBE_SelectEntity_ComputeResult @0x4147cc actor-volume test) needs the live
// scene graph / 48-actor array; the session keeps RealCityRenderer::Pick ->
// play::ScenePickResult as the hit-test provider.  Everything DOWNSTREAM of the
// hit (latch -> commit -> anchors -> deselect -> status latch) runs through the
// 1:1 reconstructions above: OnPick materialises per-entry shadow records
// carrying exactly the byte fields 0x4b950c dereferences (+0 handle string,
// +8 person-active byte, +48 person name, +529 highlight bit, *(u16*)+0 worker
// id, *(i16*)+0 contact object id) and fills the hover latch the way 0x4b8ba8
// would, then calls Selection_CommitContact().
// ===========================================================================

// One roster entry the session supplies with a pick.
struct SessionSelectEntry {
    i32  id   = 0;        // entity id (== ScenePickResult.id on a hit)
    int  kind = 0;        // PickAndResolveSceneEntity kind: 1 object, 2 scene, 3 person
    int  typeCode = 0;    // object/building: record +0 class byte (kind-name key
                          //   14*code+1078); person: ignored for naming
    const char* handle = ""; // object record handle at +0 ("tp_TUER" == door)
    const char* name   = ""; // person: name field (+48); object: custom name (+5)
    bool highlightable = true;  // record byte +529 bit 0
    bool selectable    = true;  // the 0x4b8ba8 dword_631730 verdict (pick boundary)
    u16  selectionFlags = 0x800; // ComputeSelectionFlags(person) value the hook
                                 //   reports (0x800 == "own worker, selectable")
    float screenX = 0.0f; // projected centre (highlight anchor)
    float screenY = 0.0f;
};

// Selection name caption constants — VIBE_Hud_DrawObjectNameLabels @0x4bbaec:
// caption x = left + (right-left)/2 - 80, blit width 160, char height 40.
inline constexpr int kSelectCaptionHalfWidth = 80;
inline constexpr int kSelectCaptionWidth     = 160;
inline constexpr int kSelectCaptionCharH     = 40;
// Object-kind name text key (VIBE_InfoPanel_BuildStandard @0x4b6db8:
// text id = 1078 + 14 * typeByte) — mirrors gui::infopanel kNameTextBias/Stride.
inline constexpr int kSelectKindNameBias   = 1078;
inline constexpr int kSelectKindNameStride = 14;

class SessionSelect {
public:
    // `textDb` resolves the object-kind name (text id 1078+14*code) and the
    // person job-title string table; may be null (names fall back to the
    // entry's own record-name fields, exactly the panel's "$A%s" tail).
    explicit SessionSelect(const gui::text::TextDb* textDb = nullptr);

    // Left-click: route the session pick through the real selection path.
    // A miss (pick.index < 0) leaves the hover latch empty, which makes
    // Selection_CommitContact take its real empty-click branch ->
    // VIBE_Selection_Reset (deselect on empty ground, 1:1).
    void OnPick(const ScenePickResult& pick,
                const SessionSelectEntry* roster, int rosterCount,
                float cursorX, float cursorY, float pickRadius);

    // Right-click / ESC: the real deselect — VIBE_Selection_Reset @0x4b9444
    // (through input_recon_select) + VIBE_Selection_ClearAll @0x4b94d8 (the
    // worker-mark sweep the engine's deselect paths pair it with), then the
    // status latch update (0x4bc280 tail).
    void Clear();

    struct Info {
        bool has  = false;
        int  id   = 0;
        int  kind = 0;
        char name[64] = {0};
    };
    Info current() const;

    // Per-frame highlight descriptor for the renderer/HUD: the selected
    // entity's id + projected screen anchor + pick radius.  The original draws
    // (a) the name caption at (screenX - 80, screenY), width 160 (0x4bbaec,
    // reconstructed in gui/hud_label_draw) and (b) the selected-worker mesh
    // tint via VIBE_Mesh_ResetVertexColors @0x428898 (render/mesh_transform),
    // driven per frame by VIBE_Hud_UpdateSelectionAndTargets @0x4ba614 (gap).
    struct Highlight {
        bool  has = false;
        i32   id  = 0;
        float screenX = 0.0f;
        float screenY = 0.0f;
        float radius  = 0.0f;
    };
    Highlight highlight() const;

    // The worker mark byte_12CEA98[536*id] view (set by the commit's hook).
    bool workerSelected(i32 personId) const;

private:
    // 540-byte shadow records carrying the byte fields 0x4b950c dereferences.
    struct ShadowRecord { u8 bytes[540]; };

    void installHooks();   // live hook impls over this session's tables
    void resetLatch();

    const gui::text::TextDb* textDb_;
    // shadow records for the current selection (object / contact / person)
    ShadowRecord objShadow_{};
    ShadowRecord contactShadow_{};
    ShadowRecord personShadow_{};
    // roster info latched with the selection (for current()/highlight())
    SessionSelectEntry sel_{};
    bool selValid_ = false;
    float selRadius_ = 0.0f;
    // worker marks (byte_12CEA98 view), keyed by person id (mod table size)
    static constexpr int kWorkerCount = 768; // 411648 / 536
    u8 workerMarks_[kWorkerCount] = {0};
};

} // namespace guild::play
