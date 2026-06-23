#pragma once
// ===========================================================================
// gilde.exe — Input polling orchestration + entity selection / selection-reset
// (CLUSTER: VIBE_Input / VIBE_SelectEntity / VIBE_Selection).
//
// STRICT 1:1 reconstruction of the PURE input-state / selection LOGIC from the
// Hex-Rays decompile.  The raw device acquisition layer (DirectInput vtable
// calls, Win32 GetCursorPos / SetCursorPos / ClientToScreen, VkKeyScanA /
// MapVirtualKeyA scancode-table build, the wsprintfA error formatter) is the
// PLATFORM BOUNDARY (project rule 4: Win32 -> SDL) and is therefore NOT
// reconstructed here — it is routed through installable hooks whose live
// implementation belongs in src/shim_impl.  The pure per-frame poll
// orchestration and the entity-selection hit-test math ARE reconstructed 1:1.
//
// Provenance (each reconstructed entry carries its gilde.exe address):
//   0x40da88  VIBE_Input_PollMouseAndKeyboard    (poll orchestrator)
//   0x4147cc  VIBE_SelectEntity_ComputeResult     (__usercall; selection volume
//                                                  hit-test over the entity array)
//   0x4b9444  VIBE_Selection_Reset                (__usercall; [hooked iters])
//
// SKIPPED — platform boundary (Win32 / DirectInput); routed via hooks, NOT
// reconstructed (rule 4 + rule 8 "omit + report"):
//   0x40c710  VIBE_Input_BuildScancodeTable       (VkKeyScanA / MapVirtualKeyA)
//   0x40c950  VIBE_Input_FormatErrorMessage       (wsprintfA error string)
//   0x40c9b8  VIBE_Input_AcquireMouseDevice       (DirectInput vtable +28/+32)
//   0x40c9f8  VIBE_Input_AcquireKeyboardDevice    (DirectInput vtable +28/+32)
//   0x40d7c8  VIBE_Input_ReadMouseAxes            (DInput vtable +36, SetCursorPos)
//
// The lower-level pure poll bodies (VIBE_Input_PollMouseDevice @0x40d388,
// VIBE_Input_PollKeyboardDevice @0x40d920, VIBE_Input_ProcessMouseClicks
// @0x40cdd0, VIBE_Input_SaveMouseButtonSnapshot @0x40d338) decode the raw
// DirectInput event ring and therefore straddle the boundary; the snapshot
// helper is already reconstructed in src/gui/input_state.{h,cpp}.  This module
// reconstructs only the THIN orchestrator that sequences them, exposing the two
// device-poll steps as hooks so the pure sequencing is faithful and testable.
// ===========================================================================
#include "guild/common/types.h"

#include <cstdint>
#include <functional>

namespace guild::play {

using guild::i16;
using guild::i32;
using guild::u8;

// ===========================================================================
// 0x40da88 — VIBE_Input_PollMouseAndKeyboard
//
//   char VIBE_Input_PollMouseAndKeyboard()
//   {
//     VIBE_Input_PollMouseDevice();
//     qmemcpy(&dword_672210, &dword_672174, v0);     // copy latched packet
//     return VIBE_Input_PollKeyboardDevice(0);
//   }
//
// The original copies the freshly-latched cursor packet block (dword_672174..)
// into the "previous frame" mirror (dword_672210..); the size `v0` is the
// uninitialised ecx left by the inlined PollMouseDevice tail (the engine relies
// on it being the 0x4C-byte packet size that ProcessMouseClicks last memcpy'd —
// see VIBE_Input_ProcessMouseClicks @0x40cdd0 `qmemcpy(&dword_672174,...,0x4C)`).
// We model that copy explicitly at its real width.
// ===========================================================================

// Width of the latched cursor packet copied current->previous (0x4C bytes; the
// stride VIBE_Input_ProcessMouseClicks last memcpy'd).
inline constexpr int kInputCursorPacketBytes = 0x4C;

// Installable poll steps (platform boundary — the DirectInput event-ring decode).
struct InputPollHooks {
    // VIBE_Input_PollMouseDevice @0x40d388 — drain the mouse event ring, latch
    // axes/buttons into dword_672174.. .  Inert default: no-op.
    std::function<void()> pollMouseDevice;
    // VIBE_Input_PollKeyboardDevice @0x40d920 — drain the keyboard event ring;
    // returns the auto-repeat scancode byte (al).  Inert default: returns 0.
    std::function<u8()> pollKeyboardDevice;
    // Copy the just-latched cursor packet (current dword_672174) into the
    // previous-frame mirror dword_672210.  Inert default: no-op.
    // Signature: (dstPrev, srcCurrent, bytes).
    std::function<void(void* dstPrev, const void* srcCurrent, int bytes)> copyPacket;
};

// Install the poll hooks (null entries restore the inert defaults).
void Input_SetPollHooks(const InputPollHooks& hooks);

// Read back the installed poll hooks (additive exposure only): the frame loop
// VIBE_GameLogic_RunFrameLoop @0x4c0f21 calls the mouse device step DIRECTLY
// (not through the 0x40da88 orchestrator), so a frame driver bound to the same
// platform hooks needs access to the installed set.
const InputPollHooks& Input_GetPollHooks();

// gilde.exe 0x40da88 — VIBE_Input_PollMouseAndKeyboard.
// Returns the keyboard auto-repeat scancode byte (the original's `al`).
u8 Input_PollMouseAndKeyboard();

// ===========================================================================
// 0x4147cc — VIBE_SelectEntity_ComputeResult  (__usercall eax=a1, edx=a2)
//
// Walks the 48-entry actor array (dword_676A60, stride 171 dwords = 0x2AC
// bytes), and for each LIVE actor that carries an animation, computes the
// actor's screen-space selection volume via VIBE_Pick_ComputeSelectionVolume,
// scales it by the animation record's field +116, rounds the two screen
// coordinates through VIBE_Coord_ConvertX (FPU round-to-nearest), then scans
// the actor's hotspot list (array dword_67EB80 stride 238 dwords) for the slot
// whose 16.16 rectangle (fields +14/+16/+18/+20, all >>16) contains the rounded
// point.  On a hit it stores the picked label-id (dword_62D22C) + secondary id
// (dword_62D290) and returns the slot's action code (field +8); on the special
// type byte 64 it stores only the secondary id and keeps scanning.
//
// `a1`/`a2` are the screen X/Y the volume is projected around (a2 is biased by
// flt_610EBC = 5.0f).  Returns the hit action code, or -1 if nothing was hit.
//
// The actor array, the animation/hotspot tables and the projection routine are
// the COUPLED game-state leaves; they are exposed as accessors/hooks so the
// pure walk+hit-test math is faithful and testable in process.
// ===========================================================================

// Result globals the original writes (dword_69FF98 / dword_69FF9C — the rounded
// pick point; byte_6769E0 — the picked actor's name string; dword_62D22C /
// dword_62D290 — the picked label/secondary ids).  Exposed for inspection/tests.
struct SelectEntityResult {
    i32  pointX   = 0;        // dword_69FF98 (v31)
    i32  pointY   = 0;        // dword_69FF9C (v32)
    char name[256] = {0};     // byte_6769E0 (copied from the animation name)
    i32  pickedLabelId = -1;  // dword_62D22C (-1 sentinel on miss)
    i32  pickedSecondary = -1;// dword_62D290 (untouched -> -1 here for tests)
    // dword_62D290 is a LIVE SHARED global in the original (also written by
    // VIBE_GameTick_MainLoop @0x414a38); this flag records whether THIS call
    // actually stored it (the rect +44 secondary or the type-64 +116 store),
    // so a caller holding the shared-global view can merge exactly.  Additive
    // exposure only — the walk behaviour is unchanged.
    bool secondaryWritten = false;
};

// Hooks the selection walk needs from the coupled subsystems.
struct SelectEntityHooks {
    // dword_676A60 + 171*i — base of actor record i (raw bytes).  Must yield a
    // pointer to at least 0x2AC bytes.  Null hook -> no actors (returns -1).
    std::function<u8*(int actorIndex)> actorRecord;

    // VIBE_Animation_GetPtr @0x5d9774 — resolve the animation record for the
    // 76-byte name buffer at `namePtr` (a slot=0/1).  Returns null if absent.
    // The returned record must expose: +116 (u32 scale) and a name byte-pair
    // string at +0.
    std::function<u8*(const u8* namePtr, int slot)> animationGetPtr;

    // VIBE_Pick_ComputeSelectionVolume @0x5b7134 — project the actor's selection
    // volume around (x,y); writes the two volume corners into *outU / *outV and
    // returns its secondary flag (the original's ecx `v13`; ==1 adds 256 to the
    // vertical pick coord).  Returns 0 to abort (skip this actor sub-slot).
    std::function<int(float x, float y, int z, u8* anim, float* outU, float* outV,
                      int* outHalfTileFlag)>
        computeSelectionVolume;

    // dword_67EB80 + 238*listId — the actor child-hotspot LIST record (stride
    // 238 dwords).  In the 32-bit original the entry count is the dword at
    // record byte +26 right-shifted 16 (`*(i32*)(rec+26) >> 16`) and the entry-id
    // array is the 4-byte pointer at byte +24 (`v19[6]`).  Those two fields
    // OVERLAP in the original's packed layout (the >>16 count uses bytes 28-29,
    // just above the 4-byte pointer at 24-27) — a packing that cannot survive a
    // 64-bit pointer.  We therefore decode the list record at the hook boundary:
    // the provider returns the entry count and a pointer to the entry-id array
    // (each id a 4-byte dword), preserving the exact downstream walk math.
    // Returns false (or leaves count 0) if the list is empty/absent.
    std::function<bool(int listId, int* outCount, const i32** outIdArray)>
        hotspotListRecord;

    // dword_69FFB4 + 740*rectId — the hotspot RECT record (stride 740 bytes) for
    // the entry id read from the list's id array.  Null hook -> no rects.
    std::function<u8*(int rectId)> hotspotRect;
};

// Install the selection hooks (null entries restore inert defaults).
void SelectEntity_SetHooks(const SelectEntityHooks& hooks);

// gilde.exe 0x4147cc — VIBE_SelectEntity_ComputeResult.
// `out` (optional) receives the result globals the original wrote.
i32 SelectEntity_ComputeResult(i32 screenX, i32 screenY, SelectEntityResult* out = nullptr);

// gilde.exe 0x5c6b08 — VIBE_Coord_ConvertX.
// FPU round-to-nearest (frndint under the loaded control word) of a double; the
// callers then truncate to int.  Exposed for golden-vector testing.
double Coord_ConvertX(double v);

// ===========================================================================
// 0x4b9444 — VIBE_Selection_Reset  (__usercall eax=ret, esi=a1)  [hooked iters]
//
//   - clears the four selection-anchor globals (dword_11BC274 / 11BC278 /
//     11BC260 / 631740),
//   - resolves the "current selection owner" person record: prefers the latched
//     dword_631748; else, if neither 631744 nor 631748 set, VIBE_Person_QueryBegin
//     (esi=a1, 1 vararg, key 0, value 68); else if 631748 unset returns,
//   - if a person was resolved, reads the person's object id (record +93),
//     clears byte_6317B4, and walks that object's child game-objects
//     (VIBE_GameObject_QueryFind(id, 1, 5) / VIBE_GameObject_IterNext), and for
//     each child whose type byte (dword_13CE27C + 65*objId) == 29 clears bit 2
//     of the child record's flags byte (+32).
//
// The person/game-object query iterators and the global object-type table are
// the coupled leaves (they belong to the entity-table subsystem, not this
// cluster); they are exposed as hooks so the pure control flow is faithful.
// ===========================================================================

// Selection-anchor globals the reset clears (exposed for tests).
struct SelectionAnchors {
    i32 g11BC274 = 0; // dword_11BC274
    i32 g11BC278 = 0; // dword_11BC278
    i32 g11BC260 = 0; // dword_11BC260
    i32 g631740  = 0; // dword_631740
    u8  g6317B4  = 0; // byte_6317B4
};
extern SelectionAnchors g_selectionAnchors;

// Latched-selection globals the reset reads (the "current owner" selection).
// In the 32-bit original both are dwords; 631748 is dereferenced as a person
// record pointer, so it is modelled as a native pointer here (the dword that is
// a pointer per the types.h note); 631744 is only tested for zero.
extern i32  g_selectionOwnerA; // dword_631744
extern u8*  g_selectionOwnerB; // dword_631748 (preferred person record pointer)

struct SelectionResetHooks {
    // VIBE_Person_QueryBegin(esi=a1, argc=1, key=0, val=68) — begin the person
    // query and return the first matching person record (or null).
    std::function<u8*(int a1)> personQueryBegin;

    // VIBE_GameObject_QueryFind(objId, 1, 5) — begin child-object query.
    std::function<u8*(int objId)> gameObjectQueryFind;
    // VIBE_GameObject_IterNext — advance the child-object query.
    std::function<u8*()> gameObjectIterNext;

    // dword_13CE27C + 65*objId — the object-type byte for game-object id.
    std::function<u8(int objId)> objectTypeByte;
};

// Install the selection-reset hooks (null entries restore inert defaults).
void Selection_SetResetHooks(const SelectionResetHooks& hooks);

// gilde.exe 0x4b9444 — VIBE_Selection_Reset.
// `a1` is the esi register-arg (the person-query seed).  Returns the last
// game-object record pointer the iterator yielded (the original's eax), or null.
u8* Selection_Reset(int a1);

} // namespace guild::play
