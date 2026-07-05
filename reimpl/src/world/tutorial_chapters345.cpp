#include "world/tutorial_chapters345.h"

#include <cstring>

// Byte-for-byte recovery of the chapter 3/4/5 tutorial node tables emitted by the
// VIBE_Tutorial_InitChapter3/4/5Steps builders (gilde.exe 0x598fc4 / 0x59a608 /
// 0x59aa20) plus the VIBE_Tutorial_AdvanceChapterOrFree runner (0x4db8ac).
//
// Each builder allocates 116-byte nodes (0x74), zero-fills them, writes the scalar
// fields below, attaches the script / highlight-arrow-anim sub-tables, and links the
// node to the previous via *(node+112). The chapters 3/5 nodes additionally allocate
// a 16-byte-stride script sub-table (idx20 count, idx22 ptr) and a 44-byte-stride
// (11-float) anim coordinate sub-table (idx23 frame count, idx25 ptr) whose three
// branches are selected by the display resolution (dword_69FFBC HIWORD: 800/1024/
// else). All values are copied exactly from the decompiled builders; engine callback
// code-pointers are kept as TutorialCallback345 identities (deferred GUI/sim leaves).

namespace guild::world {

// dword_69FFBC — the screen clip extent dword; its HIWORD is the resolution selector.
// Owned by gui/widget_create.cpp (g_screenClipExt). Reuse it (ODR: extern, no copy).
} // namespace guild::world
namespace guild::gui { extern i32 g_screenClipExt; }
namespace guild::world {

namespace {

constexpr i32 kVoice  = 1065;     // the common voiceA value
constexpr i32 kVoice2 = 1420;     // the alt voiceA value
constexpr i32 kMask   = 22583;    // the common idx26 mask
constexpr i32 kScriptOp = 710;    // every script entry's +0 opcode

using CB = TutorialCallback345;

// ===========================================================================
// Highlight-arrow ANIM coordinate sub-tables. Each frame = 11 floats (44 bytes).
// Three byte-identical-in-structure variants per node keyed by resolution.
// ===========================================================================
// chapter 3, node B (1 frame)
const float kAnim3B_800[]  = { 695, 300, 626, 400, 366, 380, 376, 289, 35, 142, 71 };
const float kAnim3B_1024[] = { 920, 300, 846, 440, 516, 450, 480, 375, 35, 142, 71 };
const float kAnim3B_else[] = { 1026, 320, 986, 440, 656, 600, 526, 440, 35, 142, 71 };
const TutorialAnimTable kAnim3B = { 1, kAnim3B_800, kAnim3B_1024, kAnim3B_else };

// chapter 3, node E (2 frames)
const float kAnim3E_800[]  = { 136, 430, 336, 450, 466, 270, 465, 195, 35, 142, 71,
                               465, 195, 286, 290, 406, 530, 115, 510, 35, 142, 71 };
const float kAnim3E_1024[] = { 250, 515, 466, 540, 566, 380, 575, 275, 35, 142, 71,
                               575, 275, 436, 340, 536, 620, 226, 594, 35, 142, 71 };
const float kAnim3E_else[] = { 306, 580, 456, 600, 616, 520, 626, 350, 35, 142, 71,
                               626, 350, 476, 390, 596, 660, 286, 650, 35, 142, 71 };
const TutorialAnimTable kAnim3E = { 2, kAnim3E_800, kAnim3E_1024, kAnim3E_else };

// chapter 3, node G (5 frames)
const float kAnim3G_800[]  = { 465, 195, 536, 440, 356, 420, 140, 170, 35, 142, 71,
                                75, 170, 206, 300, 426, 260, 465, 195, 35, 142, 71,
                               465, 195, 286, 290, 406, 530, 115, 510, 35, 142, 71,
                               700, 310, 700, 310, 700, 310, 700, 310,  0,   7, 21,
                               700, 280, 700, 280, 700, 280, 700, 280,  0,   7, 21 };
const float kAnim3G_1024[] = { 575, 275, 486, 390, 496, 560, 245, 255, 35, 142, 71,
                               180, 255, 226, 390, 566, 380, 575, 275, 35, 142, 71,
                               575, 275, 436, 340, 536, 620, 226, 594, 35, 142, 71,
                               924, 310, 924, 310, 924, 310, 924, 310,  0,   7, 21,
                               924, 280, 924, 280, 924, 280, 924, 280,  0,   7, 21 };
const float kAnim3G_else[] = { 626, 350, 406, 290, 176, 290, 126, 180, 35, 142, 71,
                               226, 320, 276, 410, 626, 450, 626, 340, 35, 142, 71,
                               626, 350, 476, 390, 596, 660, 286, 650, 35, 142, 71,
                               560, 420, 710, 370, 860, 320, 1020, 310, 0,   7, 21,
                               560, 420, 710, 370, 860, 320, 1020, 280, 0,   7, 21 };
const TutorialAnimTable kAnim3G = { 5, kAnim3G_800, kAnim3G_1024, kAnim3G_else };

// chapter 3, node I (1 frame)
const float kAnim3I_800[]  = { 465, 195, 456, 300, 216, 280, 166, 170, 35, 170, 106 };
const float kAnim3I_1024[] = { 575, 275, 566, 370, 346, 410, 280, 255, 35, 170, 106 };
const float kAnim3I_else[] = { 626, 350, 636, 430, 376, 440, 336, 320, 35, 170, 106 };
const TutorialAnimTable kAnim3I = { 1, kAnim3I_800, kAnim3I_1024, kAnim3I_else };

// chapter 5, node C (2 frames)
const float kAnim5C_800[]  = { 140, 170, 250, 290, 450, 290, 600, 305, 35, 177, 106,
                               700, 310, 700, 310, 700, 310, 700, 310,  0,   7,  21 };
const float kAnim5C_1024[] = { 250, 255, 400, 400, 600, 450, 710, 390, 35, 177, 106,
                               924, 310, 924, 310, 924, 310, 924, 310,  0,   7,  21 };
const float kAnim5C_else[] = { 626, 350, 636, 430, 376, 440, 336, 320, 35, 177, 106,
                               560, 420, 710, 370, 860, 320, 1020, 310, 0,   7,  21 };
const TutorialAnimTable kAnim5C = { 2, kAnim5C_800, kAnim5C_1024, kAnim5C_else };

// ===========================================================================
// Script sub-tables (16-byte entries). Only the chapter-3 step nodes carry them.
// ===========================================================================
const TutorialScriptEntry kScript3A[] = {
    { kScriptOp, 1, 7438, "CHAPTER_3_A_STEP_RIGHT" },
    { kScriptOp, 0, 7439, "CHAPTER_3_A_STEP_LEFT"  },
    { kScriptOp, 2, 7440, "CHAPTER_3_A_STEP_TOP"   },
};
const TutorialScriptEntry kScript3B[] = {
    { kScriptOp, 1, 7445, "CHAPTER_3_B_STEP_RIGHT" },
    { kScriptOp, 0, 7446, "CHAPTER_3_B_STEP_LEFT"  },
};
const TutorialScriptEntry kScript3D[] = {
    { kScriptOp, 1, 7452, "CHAPTER_3_D_STEP_RIGHT"     },
    { kScriptOp, 0, 7453, "CHAPTER_3_D_STEP_LEFT"      },
    { kScriptOp, 0, 7454, "CHAPTER_3_D_STEP_NEXT_LEFT" },
};
const TutorialScriptEntry kScript3E[] = {
    { kScriptOp, 1, 7460, "CHAPTER_3_E_STEP_RIGHT" },
    { kScriptOp, 0, 7461, "CHAPTER_3_E_STEP_LEFT"  },
    { kScriptOp, 0, 7462, "CHAPTER_3_E_STEP_TOP"   },
};

// ===========================================================================
// gilde.exe 0x598fc4 — InitChapter3Steps (intro + A,B,D,E,F,G,H,I + outro).
// ===========================================================================
const TutorialNodeSpecEx kChapter3[] = {
  // kind name   nb8 mainId mainRes            remId remRes              auxId auxRes               doneId doneRes            voiceA  coordB cb0                       cb1                              cb2                       arrow aux2  form ex19 f80 f84                          mask   word54  script    anim       animCb
  { 3, "3rtn", 0, 7434, "CHAPTER_3_INTRO",     0,   nullptr,             0,   nullptr,              0,   nullptr,            -1,     142,  CB::kOpenTownHallDialog,  CB::kNone345,                    CB::kNone345,             0,    0,    11,  0,   0,  0,                           kMask, 7680, nullptr,  nullptr,  CB::kNone345 },
  { 3, "1s3c", 2, 7436, "CHAPTER_3_A_MAIN", 7442, "CHAPTER_3_A_REMIND",  0,   nullptr,           7441, "CHAPTER_3_A_DONE",  kVoice, 213,  CB::kNone345,             CB::kNone345,                    CB::kAllowDropOnShop,     29,   1420, 11,  0,   3,  0,                           kMask, 7696, kScript3A, nullptr,   CB::kNone345 },
  { 3, "2s3c", 2, 7443, "CHAPTER_3_B_MAIN", 7448, "CHAPTER_3_B_REMIND", 7447, "Chapter_3_B_Step_Aux", 7449, "CHAPTER_3_B_DONE", kVoice, 213, CB::kDispatchCarryToEntrance, CB::kNone345,            CB::kHandleEvent32DropOnShop, 31, 1420, 9, 32, 2, 0,                          kMask, 7728, kScript3B, &kAnim3B,  CB::kNone345 },
  { 3, "4s3c", 2, 7450, "CHAPTER_3_D_MAIN", 7456, "CHAPTER_3_D_REMIND", 7455, "Chapter_3_D_Aux",    7457, "CHAPTER_3_D_DONE",  kVoice, 142,  CB::kNone345,             CB::kNone345,                    CB::kAllowDropOnShop,     33,   1420, 10, 34,  3,  (i32)CB::kHandleBookPageTurn, kMask, 7792, kScript3D, nullptr,   CB::kNone345 },
  { 3, "6s3c", 2, 7458, "CHAPTER_3_E_MAIN", 7464, "CHAPTER_3_E_REMIND", 7463, "Chapter_3_E_Step_Aux", 7465, "CHAPTER_3_E_DONE", kVoice2, 213, CB::kNone345,            CB::kNone345,                    CB::kHandleSermonDropStep, 35,  1420, 9,  36,  3,  0,                          kMask, 8048, kScript3E, &kAnim3E,  CB::kGetDragSubState },
  { 3, "7s3c", 1, 7466, "CHAPTER_3_F_MAIN",    0,   nullptr,             0,   nullptr,              0,   nullptr,            kVoice2, 71,  CB::kNone345,             CB::kNone345,                    CB::kAllowDropOnShop,     3,    426,  11,  0,   0,  0,                           kMask, 8048, nullptr,  nullptr,   CB::kNone345 },
  { 3, "8s3c", 2, 7468, "CHAPTER_3_G_MAIN", 7471, "CHAPTER_3_G_REMIND", 7470, "Chapter_3_G_Step_Aux", 7472, "CHAPTER_3_G_DONE", kVoice, 213, CB::kNone345,             CB::kNone345,                    CB::kHandleMultiStageDrop, 23,  1420, 9,  36,  0,  0,                          kMask, 8048, nullptr,  &kAnim3G,  CB::kQueryEventStateRange },
  { 3, "9s3c", 1, 7473, "CHAPTER_3_H_MAIN",    0,   nullptr,             0,   nullptr,              0,   nullptr,            kVoice,  71,  CB::kNone345,             CB::kNone345,                    CB::kSharedMatch,         3,    355,  11,  0,   0,  0,                           kMask, 8048, nullptr,  nullptr,   CB::kNone345 },
  { 3, "As3c", 2, 7475, "CHAPTER_3_I_MAIN",    0,   nullptr,          7477, "Chapter_3_I_Step_Aux", 7479, "CHAPTER_3_I_DONE", kVoice, 213,  CB::kNone345,             CB::kNone345,                    CB::kHandleConfirmDropStep, 18, 1420, 9,  38,  0,  0,                           23607, 7792, nullptr,  &kAnim3I,  CB::kNone345 },
  { 3, "3rtu", 3, 7480, "CHAPTER_3_OUTRO",     0,   nullptr,             0,   nullptr,              0,   nullptr,            -1,     355,  CB::kNone345,             CB::kNone345,                    CB::kNone345,             0,    0,    11,  0,   0,  0,                           kMask, 8048, nullptr,  nullptr,   CB::kNone345 },
};

// ===========================================================================
// gilde.exe 0x59a608 — InitChapter4Steps (intro + D,B,E + outro). No sub-tables.
// NOTE: the intro node's +0 kind byte is 3 (*(_DWORD*)v2 = 3 @0x59a65d — the same
// value InitChapter3's intro writes), NOT 4. The outro writes kind 4.
// ===========================================================================
const TutorialNodeSpecEx kChapter4[] = {
  { 3, "4rtn", 0, 7482, "CHAPTER_4_INTRO",     0,   nullptr,             0,   nullptr,              0,   nullptr,            -1,     142,  CB::kOpenTownHallDialog,        CB::kNone345,                  CB::kNone345,                  0,  0,    11, 0, 0, 0, kMask, 8048, nullptr, nullptr, CB::kNone345 },
  { 1, "4s4c", 2, 7484, "CHAPTER_4_D_MAIN", 7487, "CHAPTER_4_D_REMIND", 7486, "Chapter_4_D_Aux", 7488, "CHAPTER_4_D_DONE", kVoice, 142, CB::kQueueRequestIfNotHandled, CB::kHandleWorkshopDropStep,   CB::kAllowWorkshopDropTarget,  1,  8520, 10, 0, 0, 0, 23095, 8048, nullptr, nullptr, CB::kNone345 },
  { 1, "2s4c", 2, 7489, "CHAPTER_4_B_MAIN", 7492, "CHAPTER_4_B_REMIND", 7491, "Chapter_4_B_Aux", 7493, "CHAPTER_4_B_DONE", kVoice, 142, CB::kQueueRequestIfNotHandled, CB::kHandleStorageDropStep,    CB::kAllowStorageDropTarget,   1,  8520, 10, 0, 0, 0, 23415, 8048, nullptr, nullptr, CB::kNone345 },
  { 1, "5s4c", 2, 7494, "CHAPTER_4_E_MAIN", 7497, "CHAPTER_4_E_REMIND", 7496, "Chapter_4_E_Aux", 7498, "CHAPTER_4_E_DONE", kVoice, 142, CB::kQueueRequestIfNotHandled, CB::kHandleHouseDropStep,      CB::kAllowHouseDropTarget,     1,  8520, 10, 0, 0, 0, 24439, 8048, nullptr, nullptr, CB::kNone345 },
  { 4, "4rtu", 3, 7499, "CHAPTER_4_OUTRO",     0,   nullptr,             0,   nullptr,              0,   nullptr,            -1,     355,  CB::kNone345,                   CB::kNone345,                  CB::kNone345,                  0,  0,    11, 0, 0, 0, 6199,  8048, nullptr, nullptr, CB::kNone345 },
};

// ===========================================================================
// gilde.exe 0x59aa20 — InitChapter5Steps (intro + B,D,C,C1,E,F,G). Final node is
// kind 6 (no separate outro). Node C carries a 2-frame anim sub-table.
// ===========================================================================
const TutorialNodeSpecEx kChapter5[] = {
  { 5, "5rtn", 0, 7501, "CHAPTER_5_INTRO",     0,   nullptr,             0,   nullptr,              0,   nullptr,            -1,     142,  CB::kOpenActiveCharBuilding,  CB::kNone345,             CB::kNone345,                0,  0,    11, 0,  0, 0, 24439, 8048,  nullptr, nullptr, CB::kNone345 },
  { 5, "1s5c", 1, 7503, "CHAPTER_5_B_MAIN", 7505, "CHAPTER_5_B_REMIND",  0,   nullptr,           7506, "CHAPTER_5_B_DONE",  kVoice, 355,  CB::kNone345,                 CB::kNone345,             CB::kNone345,                44, 426,  11, 0,  0, 0, 6263,  8056,  nullptr, nullptr, CB::kNone345 },
  { 5, "3s5c", 1, 7517, "CHAPTER_5_D_MAIN", 7519, "CHAPTER_5_D_REMIND",  0,   nullptr,           7520, "CHAPTER_5_D_DONE",  kVoice, 142,  CB::kNone345,                 CB::kNone345,             CB::kNone345,                15, 8520, 10, 0,  0, 0, 6391,  8056,  nullptr, nullptr, CB::kNone345 },
  { 5, "2s5c", 2, 7507, "CHAPTER_5_C_MAIN", 7510, "CHAPTER_5_C_REMIND", 7509, "Chapter_5_C_Step_Aux", 7511, "CHAPTER_5_C_DONE", kVoice, 213, CB::kStartWorkAtShopSlot, CB::kNone345,           CB::kHandleEvent41ShopBusy,  23, 1420, 9,  41, 0, 0, 6263,  0,     nullptr, &kAnim5C, CB::kQueryEventCodeRange146 },
  { 5, "2s5c", 2, 7512, "CHAPTER_5_C1_MAIN",7515, "CHAPTER_5_C1_REMIND",7514, "Chapter_5_C1_Step_Aux", 7516, "CHAPTER_5_C1_DONE", kVoice, 213, CB::kNone345,           CB::kNone345,             CB::kNone345,                20, 8520, 9,  45, 0, 0, 6263,  0,     nullptr, nullptr, CB::kNone345 },
  { 5, "4s5c", 1, 7521, "CHAPTER_5_E_MAIN", 7523, "CHAPTER_5_E_REMIND",  0,   nullptr,           7524, "CHAPTER_5_E_DONE",  kVoice, 355,  CB::kQueueSellShopObject,     CB::kNone345,             CB::kNone345,                46, 8520, 10, 0,  0, 0, 6391,  16248, nullptr, nullptr, CB::kNone345 },
  { 5, "5s5c", 1, 7525, "CHAPTER_5_F_MAIN", 7527, "CHAPTER_5_F_REMIND",  0,   nullptr,           7528, "CHAPTER_5_F_DONE",  kVoice, 213,  CB::kHandleCandidacyStep,     CB::kNone345,             CB::kNone345,                47, 8520, 10, 0,  0, 0, 6391,  (i16)-16520, nullptr, nullptr, CB::kNone345 },
  { 6, "6s5c", 1, 7529, "CHAPTER_5_G_MAIN",    0,   nullptr,             0,   nullptr,              0,   nullptr,            kVoice, 142,  CB::kSubmitGuildMemberAssignments, CB::kNone345,        CB::kNone345,                48, 0,    11, 0,  0, 0, 6391,  (i16)-16520, nullptr, nullptr, CB::kNone345 },
};

} // namespace

const TutorialNodeSpecEx* TutorialChapter3Steps(int* count) {
    if (count) *count = static_cast<int>(sizeof(kChapter3) / sizeof(kChapter3[0]));
    return kChapter3;
}
const TutorialNodeSpecEx* TutorialChapter4Steps(int* count) {
    if (count) *count = static_cast<int>(sizeof(kChapter4) / sizeof(kChapter4[0]));
    return kChapter4;
}
const TutorialNodeSpecEx* TutorialChapter5Steps(int* count) {
    if (count) *count = static_cast<int>(sizeof(kChapter5) / sizeof(kChapter5[0]));
    return kChapter5;
}

const char* TutorialChapter3VoiceBank() { return "TUTORIAL_CHAPTER_3.sbf"; }
const char* TutorialChapter4VoiceBank() { return "TUTORIAL_CHAPTER_4.sbf"; }
const char* TutorialChapter5VoiceBank() { return "TUTORIAL_CHAPTER_5.sbf"; }

TutorialChapterNode* TutorialBuildChainEx(const TutorialNodeSpecEx* specs, int count,
                                          TutorialChapterNode* nodes) {
    if (!specs || !nodes || count <= 0)
        return nullptr;
    for (int i = 0; i < count; ++i) {
        TutorialChapterNode& n = nodes[i];
        std::memset(&n, 0, sizeof(n));                 // builders zero-fill the 0x74 block
        const TutorialNodeSpecEx& s = specs[i];
        n.kind         = s.kind;                       // +0
        std::strncpy(n.name, s.name ? s.name : "", sizeof(n.name) - 1);  // +4
        n.name[4]      = static_cast<char>(s.nameByte8);// +8 (idx2 low byte: builders set 1/2/3)
        n.mainTextId   = s.mainTextId;                 // +12
        n.mainRes32    = s.mainRes   ? 1u : 0u;        // +16 (presence; ptr is host-wide)
        n.remindTextId = s.remindTextId;               // +20
        n.remindRes32  = s.remindRes ? 1u : 0u;        // +24
        n.auxTextId    = s.auxTextId;                  // +28
        n.auxRes32     = s.auxRes    ? 1u : 0u;        // +32
        n.doneTextId   = s.doneTextId;                 // +36
        n.doneRes32    = s.doneRes   ? 1u : 0u;        // +40
        n.voiceA       = s.voiceA;                     // +44
        n.coordB       = s.coordB;                     // +48
        n.cb0          = static_cast<i32>(s.cb0);      // +52
        n.cb1          = static_cast<i32>(s.cb1);      // +56
        n.cb2          = static_cast<i32>(s.cb2);      // +60
        n.arrowTarget  = s.arrowTarget;                // +64
        n.aux2         = s.aux2;                        // +68
        n.formType     = s.formType;                   // +72
        n.extra19      = s.extra19;                     // +76
        n.mask         = s.mask;                        // +104
        n.word54       = static_cast<u16>(s.word54);    // +108
        n.nextRaw      = 0;                             // +112 (set below)
    }
    // Link node i -> node i+1 (the +112 chain). On the 32-bit original this stored
    // the next node's pointer; here the contiguous array IS the chain, so nextRaw is
    // a non-zero "has next" sentinel preserving the chain-end semantics.
    for (int i = 0; i + 1 < count; ++i)
        nodes[i].nextRaw = 1;
    return &nodes[0];
}

// gilde.exe 0x4db8ac — VIBE_Tutorial_AdvanceChapterOrFree.
bool TutorialAdvanceChapterOrFree(TutorialState& st, TutorialChapter* node,
                                  bool* wasFreed) {
    // result = VIBE_Tutorial_SetActiveChapter(node);  (-4 inactive, 0 armed)
    int result = TutorialSetActiveChapter(st, node);
    if (result) {
        // not armed -> VIBE_He_FreeHandlerEntry(node) (the node is freed).
        if (wasFreed) *wasFreed = true;
        return false;
    }
    // armed -> *(node+112) = 0; (clear the chain link so it doesn't free siblings).
    if (node) node->next = nullptr;
    if (wasFreed) *wasFreed = false;
    return true;
}

// Anchor a reference to the resolution global so the dependency is explicit and the
// extern declaration is exercised (the original builders branch on its HIWORD).
int TutorialActiveResolution() {
    return static_cast<int>(static_cast<u32>(guild::gui::g_screenClipExt) >> 16);
}

// Selects the resolution-appropriate frame block from an anim sub-table, matching the
// builders' `if (HIWORD(dword_69FFBC) == 800) ... else if (== 1024) ... else ...`.
const float* TutorialAnimFramesForResolution(const TutorialAnimTable& t, int hiword) {
    if (hiword == 800)  return t.frames800;
    if (hiword == 1024) return t.frames1024;
    return t.framesElse;
}

} // namespace guild::world
