#pragma once
// Tutorial — the chapter 3/4/5 STEP TABLES (the data the InitChapter3/4/5Steps
// builders emit) plus the small chapter-advance-or-free runner helper. Recovered
// byte-for-byte from gilde.exe:
//   * VIBE_Tutorial_InitChapter3Steps  0x598fc4   (10 nodes)
//   * VIBE_Tutorial_InitChapter4Steps  0x59a608   (5 nodes)
//   * VIBE_Tutorial_InitChapter5Steps  0x59aa20   (8 nodes)
//   * VIBE_Tutorial_AdvanceChapterOrFree 0x4db8ac (runner: arm chapter else free)
//
// This module COMPLEMENTS world/tutorial_steps.{h,cpp} (chapters 1/2 + intro/outro)
// and world/tutorial.{h,cpp} (the runtime advance state machine). It REUSES the
// 116-byte TutorialChapterNode record, the TutorialNodeKind marker, the
// TutorialBuildChain materialiser and TutorialNodeChainLength walker from
// tutorial_steps.h — the only additions here are:
//   * the chapters 3/4/5 callbacks (a separate TutorialCallback345 enum, appended
//     so the chapter 1/2 enum values are untouched),
//   * an EXTENDED node spec (TutorialNodeSpecEx) that also carries the fields the
//     richer chapter 3/5 nodes set: the name's idx2 byte, the +52/+56/+60 callback
//     trio, the +76/+80/+84 extras, the script sub-table (idx20 count, idx22 ptr)
//     and the highlight-arrow ANIM coordinate sub-table (idx23 frame count, idx24
//     callback, idx25 ptr) keyed by screen resolution (dword_69FFBC HIWORD).
//
// Like tutorial_steps, the engine callback BODIES (Interaction/Command/Npc/Office/
// Amt transition fns, the shared loc_595E70 match handler) are GUI/sim leaves kept
// as opaque named handles so the table is byte-faithful in structure without
// pulling the engine in.
#include "guild/common/types.h"
#include "world/tutorial.h"         // TutorialState, TutorialChapter, SetActiveChapter
#include "world/tutorial_steps.h"   // TutorialChapterNode, TutorialNodeKind, chain

namespace guild::world {

// ===========================================================================
// Chapter 3/4/5 engine-callback handles (appended; chapter 1/2 enum is untouched).
// The original stores raw code pointers into the node's +52/+56/+60/+96 slots.
// kNone345 == 0 == a null slot (and aliases TutorialCallback::kNone semantics).
// ===========================================================================
enum class TutorialCallback345 : int {
    kNone345 = 0,
    kSharedMatch,                          // &loc_595E70 (shared match handler)
    // chapter 3
    kOpenTownHallDialog,                   // VIBE_Interaction_OpenTownHallDialog   0x596f2c
    kAllowDropOnShop,                      // VIBE_Interaction_AllowDropOnShop      0x595f90
    kDispatchCarryToEntrance,              // VIBE_NpcAction_DispatchCarryToEntrance 0x596ad8
    kHandleEvent32DropOnShop,              // VIBE_Interaction_HandleEvent32DropOnShop 0x596aa8
    kHandleBookPageTurn,                   // VIBE_Interaction_HandleBookPageTurn   0x596ee8
    kHandleSermonDropStep,                 // VIBE_Interaction_HandleSermonDropStep 0x596b84
    kGetDragSubState,                      // VIBE_Interaction_GetDragSubState      0x596d1c
    kHandleMultiStageDrop,                 // VIBE_Interaction_HandleMultiStageDrop 0x596d28
    kQueryEventStateRange,                 // VIBE_Interaction_QueryEventStateRange 0x596e1c
    kHandleConfirmDropStep,                // VIBE_Interaction_HandleConfirmDropStep 0x596ea4
    // chapter 4
    kQueueRequestIfNotHandled,             // VIBE_Command_QueueRequestIfNotHandled 0x596a6c
    kAllowWorkshopDropTarget,              // VIBE_Interaction_AllowWorkshopDropTarget 0x596848
    kHandleWorkshopDropStep,               // VIBE_Interaction_HandleWorkshopDropStep 0x59671c
    kHandleStorageDropStep,                // VIBE_Interaction_HandleStorageDropStep 0x596578
    kAllowStorageDropTarget,               // VIBE_Interaction_AllowStorageDropTarget 0x5966a0
    kAllowHouseDropTarget,                 // VIBE_Interaction_AllowHouseDropTarget 0x5969e8
    kHandleHouseDropStep,                  // VIBE_Interaction_HandleHouseDropStep  0x5968b8
    // chapter 5
    kOpenActiveCharBuilding,               // VIBE_Tutorial_OpenActiveCharBuilding  0x596fa4
    kStartWorkAtShopSlot,                  // VIBE_Command_StartWorkAtShopSlot      0x596c18
    kHandleEvent41ShopBusy,                // VIBE_Interaction_HandleEvent41ShopBusy 0x596c80
    kQueryEventCodeRange146,               // VIBE_Interaction_QueryEventCodeRange146 0x596ccc
    kQueueSellShopObject,                  // VIBE_Command_QueueSellShopObject      0x597028
    kHandleCandidacyStep,                  // VIBE_Office_HandleCandidacyStep       0x59708c
    kSubmitGuildMemberAssignments,         // VIBE_Amt_SubmitGuildMemberAssignments 0x5970fc
};

// ===========================================================================
// Script sub-table entry (16 bytes; idx22 ptr, idx20 count). Each record:
//   +0  (dword) opcode/id   (always 710 in the recovered tables)
//   +4  (byte)  position    (1=right, 0=left/none, 2=top)
//   +8  (dword) text id
//   +12 (dword) resource ptr (raw 32-bit slot in the original)
// ===========================================================================
struct TutorialScriptEntry {
    i32         opcode;   // +0  (710)
    u8          pos;      // +4
    i32         textId;   // +8
    const char* res;      // +12 (decoded resource string)
};

// ===========================================================================
// Highlight-arrow ANIM coordinate sub-table (idx25 ptr, idx23 frame count). Each
// frame is 11 floats (44 bytes): a screen-coordinate keyframe. The builders emit
// THREE byte-identical-in-structure variants selected by the display resolution
// (dword_69FFBC HIWORD: 800 / 1024 / else==1280). We recover all three.
// ===========================================================================
constexpr int kTutAnimFloatsPerFrame = 11;   // 44-byte stride
struct TutorialAnimTable {
    int          frameCount;       // idx23
    const float* frames800;        // frameCount*11 floats (res 800)
    const float* frames1024;       // res 1024
    const float* framesElse;       // res else (1280)
};

// ===========================================================================
// Extended node spec — the full recoverable scalar layout of a chapter 3/4/5 node
// (a superset of tutorial_steps.h's TutorialNodeSpec). Materialise with
// TutorialBuildChainEx; the script/anim sub-tables are exposed as optional
// pointers (null when the node has none).
// ===========================================================================
struct TutorialNodeSpecEx {
    i32                 kind;          // +0   (idx 0)  3/4/5 for chapter nodes; 6/7 for the final/outro
    const char*         name;          // +4   (idx 1)  4-char tag dword
    u8                  nameByte8;     // +8   (idx 2 low byte) 0/1/2/3
    i32                 mainTextId;    // +12
    const char*         mainRes;       // +16
    i32                 remindTextId;  // +20
    const char*         remindRes;     // +24
    i32                 auxTextId;     // +28
    const char*         auxRes;        // +32
    i32                 doneTextId;    // +36
    const char*         doneRes;       // +40
    i32                 voiceA;        // +44
    i32                 coordB;        // +48
    TutorialCallback345 cb0;           // +52  (idx 13)
    TutorialCallback345 cb1;           // +56  (idx 14)
    TutorialCallback345 cb2;           // +60  (idx 15)
    u8                  arrowTarget;   // +64
    i32                 aux2;          // +68  (idx 17)
    u8                  formType;      // +72
    i32                 extra19;       // +76  (idx 19)
    i32                 field80;       // +80  (idx 20) script entry count (when script present)
    i32                 field84;       // +84  (idx 21) extra callback/flag (HandleBookPageTurn etc.)
    i32                 mask;          // +104 (idx 26)
    i16                 word54;        // +108 (idx*2 54)  (signed: ch5 uses -16520)
    // sub-tables (null when absent)
    const TutorialScriptEntry* script; // idx22; count == field80
    const TutorialAnimTable*   anim;    // idx25; frameCount == idx23
    TutorialCallback345        animCb;  // idx24 (anim query callback; kNone345 if absent)
};

// gilde.exe 0x598fc4 — VIBE_Tutorial_InitChapter3Steps: the 10-node table
// (intro + A,B,D,E,F,G,H,I + outro). Returns the static node array; *count := 10.
const TutorialNodeSpecEx* TutorialChapter3Steps(int* count);
// gilde.exe 0x59a608 — VIBE_Tutorial_InitChapter4Steps: the 5-node table
// (intro + D,B,E + outro).
const TutorialNodeSpecEx* TutorialChapter4Steps(int* count);
// gilde.exe 0x59aa20 — VIBE_Tutorial_InitChapter5Steps: the 8-node table
// (intro + B,D,C,C1,E,F,G). Note: the final node is kind 6 (no separate outro).
const TutorialNodeSpecEx* TutorialChapter5Steps(int* count);

// The voice-bank resource name each builder loads at the end (into dword_649CC0/4/8).
const char* TutorialChapter3VoiceBank();   // "TUTORIAL_CHAPTER_3.sbf"
const char* TutorialChapter4VoiceBank();   // "TUTORIAL_CHAPTER_4.sbf"
const char* TutorialChapter5VoiceBank();   // "TUTORIAL_CHAPTER_5.sbf"

// Materialises `count` extended specs into `nodes`, copying every scalar field
// byte-for-byte (the script/anim sub-tables stay referenced via the spec, exactly
// as the builders store the engine-owned allocations). Chains node i -> i+1 via the
// nextRaw sentinel, mirroring *(node+112) = nextNode. Returns head (REUSES the
// chain semantics of TutorialBuildChain / TutorialNodeChainLength).
TutorialChapterNode* TutorialBuildChainEx(const TutorialNodeSpecEx* specs, int count,
                                          TutorialChapterNode* nodes);

// ===========================================================================
// Runner: gilde.exe 0x4db8ac — VIBE_Tutorial_AdvanceChapterOrFree.
//   result = SetActiveChapter(node);            // -4 inactive, 0 armed
//   if (result) return FreeHandlerEntry(node);  // not armed -> free the node
//   node->nextRaw = 0; return 0;                // armed -> clear chain link
// Modelled WITHOUT the GUI/allocator: returns true when the chapter was armed (the
// node was kept and its +112 cleared), false when it was freed (inactive). On the
// `freed` path the node's `wasFreed` out-flag is set so the caller/test can assert.
// ===========================================================================
bool TutorialAdvanceChapterOrFree(TutorialState& st, TutorialChapter* node,
                                  bool* wasFreed);

// The active display-resolution selector — HIWORD(dword_69FFBC) (g_screenClipExt),
// the value the builders branch on when choosing an anim sub-table block.
int TutorialActiveResolution();

// Selects the resolution-appropriate frame block from an anim sub-table, matching the
// builders' `if (HIWORD(dword_69FFBC)==800) .. else if (==1024) .. else ..` cascade.
const float* TutorialAnimFramesForResolution(const TutorialAnimTable& t, int hiword);

} // namespace guild::world
