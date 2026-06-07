#pragma once
// Tutorial — the chapter-node STEP TABLES (the data the InitChapter builders emit)
// plus the chain build/free/length traversal, recovered byte-for-byte from the
// VIBE_Tutorial_InitChapter*Steps / InitMainIntro / InitMainOutro builders
// (gilde.exe 0x597e80..0x59b3d8) and VIBE_Tutorial_FreeStepChain (0x597e30).
//
// IMPORTANT: this complements (does not replace) world/tutorial.h. tutorial.h
// recovers the RUNTIME advance state machine over the inner 16-byte step array
// (chapter+88, TutorialStep). The struct built by the InitChapter builders is a
// DIFFERENT 116-byte "chapter node" that is linked into a singly-linked chain via
// its +112 next pointer; each node carries the chapter's main/remind/aux/done text
// ids + resource names, voice/coord params, the highlight-arrow target code and a
// set of engine callback handles. That node layout + the chapter 1/2 step tables
// are recovered here exactly.
//
// The per-node engine callbacks (Interaction/Person/Event transition fns, the
// shared loc_595E70 match handler, the Form/Voice plumbing) are GUI/sim leaves —
// they are kept as opaque named handles (TutorialCallback enum) so the table is
// byte-faithful in structure without pulling the engine in. Chapters 3/4/5 are the
// same mechanical layout bound to dozens more engine callbacks and are DEFERRED
// (listed in the module report); chapters 1 and 2 + intro/outro are fully recovered.
#include <cstddef>

#include "guild/common/types.h"

namespace guild::world {

// ===========================================================================
// Chapter-node kind (the +0 marker the builders write).
// ===========================================================================
enum TutorialNodeKind : i32 {
    kTutNodeIntro   = 0,   // InitMainIntro:  *(_DWORD*)v = 0
    kTutNodeChapter = 1,   // InitChapter1:   = 1   (chapter 2 writes 2, etc.)
    kTutNodeOutro   = 7,   // InitMainOutro:  = 7
};

// Opaque named handles for the per-node engine callbacks. The original stores raw
// code pointers; we keep the identity so two nodes that share a callback compare
// equal and the table stays structurally faithful. kTutCbNone == 0 (a null slot).
enum class TutorialCallback : int {
    kNone = 0,
    kSharedMatch,                       // &loc_595E70 (shared match handler)
    kPickupDropTransition,              // VIBE_Interaction_HandlePickupDropTransition
    kEvent13SetState11,                 // VIBE_Interaction_HandleEvent13SetState11
    kEvent23Or24SetState,               // VIBE_Interaction_HandleEvent23Or24SetState
    kMatchObjectDropTarget,             // VIBE_Interaction_MatchObjectDropTarget
    kBeginQuerySetField270,             // VIBE_Person_BeginQueryThenSetField270
    kEvent25Code270,                    // VIBE_Interaction_HandleEvent25Code270
    kMatchPersonState15Cmd272,          // VIBE_Event_MatchPersonState15Cmd272
    kEvent25Code272,                    // VIBE_Interaction_HandleEvent25Code272
    kMatchType26OrJump,                 // VIBE_Event_MatchType26OrJump
    kEvent26Code272,                    // VIBE_Interaction_HandleEvent26Code272
    kCheckTargetStateOpen,              // VIBE_Interaction_CheckTargetStateOpen
    kBeginQueryOpenBuilding,            // VIBE_Person_BeginQueryThenOpenBuilding
    kEvent27Or28State13,                // VIBE_Interaction_HandleEvent27Or28State13
    kCheckTargetStateOpenOrTwo,         // VIBE_Interaction_CheckTargetStateOpenOrTwo
    kBeginQueryOpenBuildingAlt,         // VIBE_Person_BeginQueryThenOpenBuildingAlt
    kEvent25Or26Code19,                 // VIBE_Interaction_HandleEvent25Or26Code19
    kCheckTargetStateOpenTwoOr18,       // VIBE_Interaction_CheckTargetStateOpenTwoOr18
    kEvent25Code116,                    // VIBE_Interaction_HandleEvent25Code116
};

// ===========================================================================
// Chapter node (116 bytes; allocated 0x74 by the builders, zero-filled first).
// Field offsets recovered from the *(_DWORD*)v + N writes:
// ===========================================================================
// NOTE: the original is a 32-bit record — every pointer slot is 4 bytes. To keep
// the byte layout exact on a 64-bit host, the resource-name and callback pointer
// slots are modelled as u32 raw words (mainRes32 etc.); the decoded const char*
// resource strings live in TutorialNodeSpec. Use BuildChain to materialise nodes.
GUILD_PACKED_BEGIN
struct TutorialChapterNode {
    i32 kind;               // +0   (idx 0)  TutorialNodeKind (chapter # for 1..5)
    char name[8];           // +4   (idx 1..)  ASCII name tag ("1rtn", "2rtn", ...)
    i32 mainTextId;         // +12  (idx 3)  main text id
    u32 mainRes32;          // +16  (idx 4)  main resource ptr (raw 32-bit slot)
    i32 remindTextId;       // +20  (idx 5)  remind text id (0 = none)
    u32 remindRes32;        // +24  (idx 6)  remind resource ptr (raw slot)
    i32 auxTextId;          // +28  (idx 7)  aux text id (0 = none)
    u32 auxRes32;           // +32  (idx 8)  aux resource ptr (raw slot)
    i32 doneTextId;         // +36  (idx 9)  done text id (0 = none)
    u32 doneRes32;          // +40  (idx 10) done resource ptr (raw slot)
    i32 voiceA;             // +44  (idx 11) voice line A (1065, or -1 for intro/outro)
    i32 coordB;             // +48  (idx 12) coord / voice B (142/213/355/71/0)
    i32 cb0;                // +52  (idx 13) callback slot 0
    i32 cb1;                // +56  (idx 14) callback slot 1 (transition fn)
    i32 cb2;                // +60  (idx 15) callback slot 2 (&loc_595E70 / match fn)
    u8  arrowTarget;        // +64  (byte) highlight-arrow target code
    u8  pad65[3];           // +65
    i32 aux2;               // +68  (idx 17) aux value (8520 / 1065 / 71 / 0)
    u8  formType;           // +72  (byte) form-position/type (9/10/11)
    u8  pad73[3];           // +73
    i32 extra19;            // +76  (idx 19) extra (7/9/0)
    u8  pad80[24];          // +80..+103
    i32 mask;               // +104 (idx 26) flags/mask
    u16 word54;             // +108 (idx*2 54) word field (0/3584/7680)
    u16 pad110;             // +110
    u32 nextRaw;            // +112 (idx 28) next-node pointer (chain link)
} GUILD_PACKED;
GUILD_PACKED_END
static_assert(offsetof(TutorialChapterNode, mainTextId) == 12,  "main @+12");
static_assert(offsetof(TutorialChapterNode, voiceA)     == 44,  "voiceA @+44");
static_assert(offsetof(TutorialChapterNode, cb2)        == 60,  "cb2 @+60");
static_assert(offsetof(TutorialChapterNode, arrowTarget)== 64,  "arrow @+64");
static_assert(offsetof(TutorialChapterNode, aux2)       == 68,  "aux2 @+68");
static_assert(offsetof(TutorialChapterNode, formType)   == 72,  "formType @+72");
static_assert(offsetof(TutorialChapterNode, mask)       == 104, "mask @+104");
static_assert(offsetof(TutorialChapterNode, nextRaw)    == 112, "next @+112");
static_assert(sizeof(TutorialChapterNode) == 116, "chapter node is 0x74 bytes");

// Decoded (engine-independent) view of a node's callback slots. The builders write
// raw code pointers into cb0/cb1/cb2; we expose the identity for the recovered
// chapters via this table-of-records.
struct TutorialNodeSpec {
    i32              kind;
    const char*      name;
    i32              mainTextId;
    const char*      mainRes;
    i32              remindTextId;
    const char*      remindRes;
    i32              auxTextId;
    const char*      auxRes;
    i32              doneTextId;
    const char*      doneRes;
    i32              voiceA;
    i32              coordB;
    TutorialCallback cb0;
    TutorialCallback cb1;
    TutorialCallback cb2;
    u8               arrowTarget;
    i32              aux2;
    u8               formType;
    i32              extra19;
    i32              mask;
    u16              word54;
};

// gilde.exe 0x597f5c — VIBE_Tutorial_InitChapter1Steps: the 11-node table
// (intro + steps A,C,D,E,F,G,H,I,J + outro). Returns a pointer to the static node
// array and writes the count into *count.
const TutorialNodeSpec* TutorialChapter1Steps(int* count);
// gilde.exe 0x598874 — VIBE_Tutorial_InitChapter2Steps: the 8-node table
// (intro + steps A,B,C,C1,D,E,F + outro).
const TutorialNodeSpec* TutorialChapter2Steps(int* count);
// gilde.exe 0x597e80 — VIBE_Tutorial_InitMainIntro: the single main-intro node.
const TutorialNodeSpec& TutorialMainIntro();
// gilde.exe 0x59b318 — VIBE_Tutorial_InitMainOutro: the single main-outro node.
const TutorialNodeSpec& TutorialMainOutro();

// ===========================================================================
// Chain build / free / length  (the linked-node chain the chapters form).
// ===========================================================================
// Materialises `count` specs into `nodes` (caller-provided storage of >= count
// TutorialChapterNode), copying every field byte-for-byte. node i's nextRaw is set
// to a non-zero "has next" sentinel (1) for i < count-1 and 0 for the last node,
// mirroring the InitChapter builders' *(node+112) = nextNode chain construction
// (the 32-bit nextRaw field can't hold a 64-bit host pointer, so the contiguous
// `nodes` array IS the chain — walk it with TutorialNodeChainLength). Returns head.
TutorialChapterNode* TutorialBuildChain(const TutorialNodeSpec* specs, int count,
                                        TutorialChapterNode* nodes);

// gilde.exe 0x597e30 — VIBE_Tutorial_FreeStepChain: length of the +112 chain from
// `head` over a contiguous chain (the recursion the freer walks). Walks while each
// node's nextRaw sentinel is non-zero. Pure traversal (the engine owns frees).
int TutorialNodeChainLength(const TutorialChapterNode* head);

// The resource name of a node's form-position/type code (formType). 11=full,
// 10=reminder, 9=aux — matched to the highlight forms. Returns nullptr for
// unknown codes. (Recovered from AdvanceStepForms' position switch + builder use.)
const char* TutorialFormTypeName(u8 formType);

} // namespace guild::world
