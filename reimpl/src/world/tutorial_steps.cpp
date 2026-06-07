#include "world/tutorial_steps.h"

#include <cstring>

// Byte-for-byte recovery of the tutorial chapter-node step tables emitted by the
// VIBE_Tutorial_InitChapter*Steps builders (gilde.exe). Each builder allocates a
// 116-byte node (0x74), zero-fills it, writes the fields below, then links it to
// the previous node via *(node+112). The text ids, resource names, voice/coord
// params, arrow-target codes, form-type codes, masks and word54 values are copied
// exactly from the decompiled builders; the engine callback code-pointers are kept
// as TutorialCallback identities (the GUI/sim transition fns are deferred leaves).

namespace guild::world {

namespace {

constexpr i32 kVoiceMain = 1065;     // the common voiceA value

// gilde.exe 0x597f5c — InitChapter1Steps (intro + A,C,D,E,F,G,H,I,J + outro).
// (Note: the builder skips a "B" letter; the recovered order matches the binary.)
const TutorialNodeSpec kChapter1[] = {
  // kind name   mainId mainRes            remId  remRes              auxId  auxRes            doneId doneRes           voiceA      coordB cb0                         cb1                                     cb2                         arrow aux2  form extra19 mask  word54
  {  1, "1rtn", 7362, "CHAPTER_1_INTRO",    0,    nullptr,             0,    nullptr,           0,    nullptr,          -1,         142,   TutorialCallback::kNone,    TutorialCallback::kNone,                TutorialCallback::kSharedMatch, 0,    0,    11,  0,      0,    0 },
  {  1, "As1c", 7364, "CHAPTER_1_A_MAIN", 7366, "CHAPTER_1_A_REMIND",  0,    nullptr,        7367, "CHAPTER_1_A_DONE", kVoiceMain, 355,   TutorialCallback::kNone,    TutorialCallback::kNone,                TutorialCallback::kSharedMatch, 22,   0,    11,  0,      0,    0 },
  {  1, "Cs1c", 7368, "CHAPTER_1_C_MAIN", 7371, "CHAPTER_1_C_REMIND", 7370, "Chapter_1_C_Aux", 7372, "CHAPTER_1_C_DONE", kVoiceMain, 142,   TutorialCallback::kNone,    TutorialCallback::kNone,                TutorialCallback::kSharedMatch, 6,    8520, 9,   7,      1,    0 },
  {  1, "Ds1c", 7373, "CHAPTER_1_D_MAIN", 7376, "CHAPTER_1_D_REMIND", 7375, "Chapter_1_D_Aux", 7377, "CHAPTER_1_D_DONE", kVoiceMain, 142,   TutorialCallback::kNone,    TutorialCallback::kNone,                TutorialCallback::kSharedMatch, 8,    8520, 9,   9,      3,    0 },
  {  1, "Es1c", 7378, "CHAPTER_1_E_MAIN", 7381, "CHAPTER_1_E_REMIND", 7380, "Chapter_1_E_Aux", 7382, "CHAPTER_1_E_DONE", kVoiceMain, 142,   TutorialCallback::kNone,    TutorialCallback::kPickupDropTransition, TutorialCallback::kSharedMatch, 1,    8520, 9,   0,      7,    0 },
  {  1, "Fs1c", 7383, "CHAPTER_1_F_MAIN",   0,    nullptr,             0,    nullptr,           0,    nullptr,          kVoiceMain, 71,    TutorialCallback::kNone,    TutorialCallback::kNone,                TutorialCallback::kSharedMatch, 3,    71,   11,  0,      7,    0 },
  {  1, "Gs1c", 7385, "CHAPTER_1_G_MAIN", 7387, "CHAPTER_1_G_REMIND",  0,    nullptr,        7388, "CHAPTER_1_G_DONE", kVoiceMain, 142,   TutorialCallback::kNone,    TutorialCallback::kNone,                TutorialCallback::kSharedMatch, 12,   8520, 10,  0,      23,   0 },
  {  1, "Hs1c", 7389, "CHAPTER_1_H_MAIN", 7391, "CHAPTER_1_H_REMIND",  0,    nullptr,        7392, "CHAPTER_1_H_DONE", kVoiceMain, 142,   TutorialCallback::kNone,    TutorialCallback::kNone,                TutorialCallback::kSharedMatch, 19,   8520, 10,  0,      2071, 0 },
  {  1, "Is1c", 7393, "CHAPTER_1_I_MAIN", 7395, "CHAPTER_1_I_REMIND",  0,    nullptr,        7396, "CHAPTER_1_I_DONE", kVoiceMain, 142,   TutorialCallback::kNone,    TutorialCallback::kNone,                TutorialCallback::kSharedMatch, 20,   8520, 10,  0,      6167, 0 },
  {  1, "Js1c", 7397, "CHAPTER_1_J_MAIN", 7399, "CHAPTER_1_J_REMIND",  0,    nullptr,        7400, "CHAPTER_1_J_DONE", kVoiceMain, 355,   TutorialCallback::kNone,    TutorialCallback::kEvent13SetState11,    TutorialCallback::kSharedMatch, 13,   8520, 10,  0,      6199, 0 },
  {  7, "1rtu", 7401, "CHAPTER_1_OUTRO",    0,    nullptr,             0,    nullptr,           0,    nullptr,          -1,         355,   TutorialCallback::kNone,    TutorialCallback::kNone,                TutorialCallback::kSharedMatch, 0,    0,    11,  0,      6199, 0 },
};

// gilde.exe 0x598874 — InitChapter2Steps (intro + A,B,C,C1,D,E,F + outro).
const TutorialNodeSpec kChapter2[] = {
  {  2, "2rtn", 7403, "CHAPTER_2_INTRO",    0,    nullptr,             0,    nullptr,           0,    nullptr,          -1,         142,   TutorialCallback::kNone,                  TutorialCallback::kNone,              TutorialCallback::kSharedMatch,            0,  0,    11, 0, 6199,  0 },
  {  2, "As2c", 7405, "CHAPTER_2_A_MAIN", 7407, "CHAPTER_2_A_REMIND",  0,    nullptr,        7408, "CHAPTER_2_A_DONE", kVoiceMain, 142,   TutorialCallback::kNone,                  TutorialCallback::kEvent23Or24SetState, TutorialCallback::kSharedMatch,           1,  8520, 10, 0, 6199,  0 },
  {  2, "Bs2c", 7409, "CHAPTER_2_B_MAIN", 7411, "CHAPTER_2_B_REMIND",  0,    nullptr,        7412, "CHAPTER_2_B_DONE", kVoiceMain, 213,   TutorialCallback::kBeginQuerySetField270, TutorialCallback::kEvent25Code270,    TutorialCallback::kMatchObjectDropTarget,  1,  8520, 11, 0, 22583, 0 },
  {  2, "Cs2c", 7413, "CHAPTER_2_C_MAIN", 7415, "CHAPTER_2_C_REMIND",  0,    nullptr,        7416, "CHAPTER_2_C_DONE", kVoiceMain, 142,   TutorialCallback::kNone,                  TutorialCallback::kEvent25Code272,    TutorialCallback::kMatchPersonState15Cmd272, 1, 8520, 10, 0, 22583, 3584 },
  {  2, "Ds2c", 7417, "CHAPTER_2_C1_MAIN",7419,"CHAPTER_2_C1_REMIND",  0,    nullptr,        7420, "CHAPTER_2_C1_DONE",kVoiceMain, 142,   TutorialCallback::kNone,                  TutorialCallback::kEvent26Code272,    TutorialCallback::kMatchType26OrJump,      1,  8520, 10, 0, 22583, 7680 },
  {  2, "Es2c", 7421, "CHAPTER_2_D_MAIN", 7423, "CHAPTER_2_D_REMIND",  0,    nullptr,        7424, "CHAPTER_2_D_DONE", kVoiceMain, 142,   TutorialCallback::kBeginQueryOpenBuilding, TutorialCallback::kEvent27Or28State13, TutorialCallback::kCheckTargetStateOpen,  1,  8520, 10, 0, 22583, 7680 },
  {  2, "Fs2c", 7425, "CHAPTER_2_E_MAIN", 7427, "CHAPTER_2_E_REMIND",  0,    nullptr,        7428, "CHAPTER_2_E_DONE", kVoiceMain, 142,   TutorialCallback::kBeginQueryOpenBuildingAlt, TutorialCallback::kEvent25Or26Code19, TutorialCallback::kCheckTargetStateOpenOrTwo, 1, 8520, 10, 0, 22583, 7680 },
  {  2, "Gs2c", 7429, "CHAPTER_2_F_MAIN", 7431, "CHAPTER_2_F_REMIND",  0,    nullptr,           0,    nullptr,         kVoiceMain, 142,   TutorialCallback::kNone,                  TutorialCallback::kEvent25Code116,    TutorialCallback::kCheckTargetStateOpenTwoOr18, 1, kVoiceMain, 10, 0, 22583, 7680 },
  {  7, "2rtu", 7432, "CHAPTER_2_OUTRO",    0,    nullptr,             0,    nullptr,           0,    nullptr,          -1,         355,   TutorialCallback::kNone,                  TutorialCallback::kNone,              TutorialCallback::kNone,                   0,  0,    11, 0, 22583, 7680 },
};

// gilde.exe 0x597e80 — InitMainIntro single node.
const TutorialNodeSpec kMainIntro =
  {  0, "RTNI", 7356, "TUTORIAL_MAIN_INTRO", 0,   nullptr,             0,    nullptr,           0,    nullptr,          -1,         142,   TutorialCallback::kNone,    TutorialCallback::kNone,                TutorialCallback::kNone,        0,    0,    11,  0,      0,    0 };

// gilde.exe 0x59b318 — InitMainOutro single node (mask = -9).
const TutorialNodeSpec kMainOutro =
  {  7, "RTUO", 7359, "TUTORIAL_MAIN_OUTRO", 0,   nullptr,             0,    nullptr,           0,    nullptr,          -1,         0,     TutorialCallback::kNone,    TutorialCallback::kNone,                TutorialCallback::kNone,        0,    0,    11,  0,     -9,    0 };

} // namespace

const TutorialNodeSpec* TutorialChapter1Steps(int* count) {
    if (count) *count = static_cast<int>(sizeof(kChapter1) / sizeof(kChapter1[0]));
    return kChapter1;
}

const TutorialNodeSpec* TutorialChapter2Steps(int* count) {
    if (count) *count = static_cast<int>(sizeof(kChapter2) / sizeof(kChapter2[0]));
    return kChapter2;
}

const TutorialNodeSpec& TutorialMainIntro() { return kMainIntro; }
const TutorialNodeSpec& TutorialMainOutro() { return kMainOutro; }

TutorialChapterNode* TutorialBuildChain(const TutorialNodeSpec* specs, int count,
                                        TutorialChapterNode* nodes) {
    if (!specs || !nodes || count <= 0)
        return nullptr;
    for (int i = 0; i < count; ++i) {
        TutorialChapterNode& n = nodes[i];
        std::memset(&n, 0, sizeof(n));            // builders zero-fill the 0x74 block
        const TutorialNodeSpec& s = specs[i];
        n.kind         = s.kind;                  // +0
        std::strncpy(n.name, s.name ? s.name : "", sizeof(n.name) - 1);  // +4
        n.mainTextId   = s.mainTextId;            // +12
        n.mainRes32    = s.mainRes   ? 1u : 0u;   // +16 (presence; ptr is host-wide)
        n.remindTextId = s.remindTextId;          // +20
        n.remindRes32  = s.remindRes ? 1u : 0u;   // +24
        n.auxTextId    = s.auxTextId;             // +28
        n.auxRes32     = s.auxRes    ? 1u : 0u;   // +32
        n.doneTextId   = s.doneTextId;            // +36
        n.doneRes32    = s.doneRes   ? 1u : 0u;   // +40
        n.voiceA       = s.voiceA;                // +44
        n.coordB       = s.coordB;                // +48
        n.cb0          = static_cast<i32>(s.cb0); // +52
        n.cb1          = static_cast<i32>(s.cb1); // +56
        n.cb2          = static_cast<i32>(s.cb2); // +60
        n.arrowTarget  = s.arrowTarget;           // +64
        n.aux2         = s.aux2;                  // +68
        n.formType     = s.formType;              // +72
        n.extra19      = s.extra19;               // +76
        n.mask         = s.mask;                  // +104
        n.word54       = s.word54;                // +108
        n.nextRaw      = 0;                       // +112 (set below)
    }
    // Link node i -> node i+1 (the +112 chain). On the 32-bit original this stored
    // the next node's pointer; here the contiguous array is the chain, so nextRaw
    // is just a non-zero "has next" sentinel preserving the chain-end semantics.
    for (int i = 0; i + 1 < count; ++i)
        nodes[i].nextRaw = 1;
    return &nodes[0];
}

// gilde.exe 0x597e30 — VIBE_Tutorial_FreeStepChain (chain length traversal).
// Walks the contiguous chain while each node's nextRaw sentinel is non-zero.
int TutorialNodeChainLength(const TutorialChapterNode* head) {
    int n = 0;
    const TutorialChapterNode* p = head;
    while (p) {
        ++n;
        if (!p->nextRaw)        // chain end (last node's +112 == 0)
            break;
        ++p;                    // next contiguous node
    }
    return n;
}

const char* TutorialFormTypeName(u8 formType) {
    switch (formType) {
        case 9:  return "tutorial\\aux_form";
        case 10: return "tutorial\\reminder_form";
        case 11: return "tutorial\\main_form";
        default: return nullptr;
    }
}

} // namespace guild::world
