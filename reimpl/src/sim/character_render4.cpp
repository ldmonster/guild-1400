// character_render4 — fourth cluster of VIBE_Character_* leaves (gilde.exe). Faithful
// 1:1 ports of the action-queue builder family (sound / play-sample / sample-loop /
// use-gate / take-object / drop-object) and the SitDown / GetUp / SitDownAtOnce /
// CharacterCount script-command handlers.
//
// The single cross-cluster leaf the builders touch — the free-node pool allocator
// (VIBE_CharAction_QueueInsertEntry @0x40c15c) and the unlink-on-failure
// (VIBE_ActionQueue_UnlinkEntry @0x404370) — is routed through CharRender4Hooks; the
// live build forwards both into the GENUINE reconstructed guild::sim sibling
// (see the integration test). The default table is inert (allocator returns null =>
// the builder early-outs exactly as on an exhausted pool), so the field arithmetic,
// the verbatim stride-2 name copy, and the Cmd* control flow are golden-testable in
// isolation. The action node is the REAL reconstructed guild::sim::ActionNode — never
// re-declared here.
#include "sim/character_render4.h"

#include <cstdint>           // intptr_t (step-kind tag <-> fn-ptr round-trip)

#include "sim/character.h"   // Character (reused: flagsA / actions head / universe)

namespace guild::sim {

// ---------------------------------------------------------------------------
// Hook table (inert defaults).
// ---------------------------------------------------------------------------
namespace {
const CharRender4Hooks* g_hooks = nullptr;

ActionNode* DefQueueInsert(Character*) { return nullptr; }
int         DefUnlink(ActionNode*)     { return 0; }

const CharRender4Hooks g_default = { DefQueueInsert, DefUnlink };
} // namespace

void SetCharRender4Hooks(const CharRender4Hooks* h) { g_hooks = h; }
const CharRender4Hooks& GetCharRender4Hooks() { return g_hooks ? *g_hooks : g_default; }

// ---------------------------------------------------------------------------
// Verbatim stride-2, byte-pairwise name copy. Reproduces the original's
//   do { a=*src; *dst=*src; if(!a) break; b=src[1]; src+=2; dst[1]=b; dst+=2; } while(b);
// loop exactly: the EVEN byte of each pair is the loop guard (break on its null,
// after writing it), the ODD byte is copied unconditionally and re-tests the loop.
// For a conventional C string this is a plain strcpy; the pair structure is preserved
// for fidelity (it differs only for strings with an interior null on an odd index).
// ---------------------------------------------------------------------------
int CopyNamePairwise(char* dst, const char* src) {
    char* d0 = dst;
    char a, b;
    do {
        a = *src;
        *dst = *src;
        if (!a) break;        // even byte null -> stop (after copying it)
        b = src[1];
        src += 2;
        dst[1] = b;
        dst += 2;
    } while (b);              // odd byte null -> stop (after copying it)
    // bytes written = (dst - d0) + 1 for the final terminator pair position.
    // Compute precisely by re-walking is unnecessary; the terminator is at the last
    // written slot. Return the count up to and including the first null encountered.
    int n = 0;
    for (const char* p = d0;; ++p) { ++n; if (!*p) break; }
    return n;
}

namespace {
// Bind the recovered step-handler identity onto a node (the original stores the raw
// *ActionUpdate fn address at node+0). We record it as a tagged value so this TU need
// not pull in the separate charaction_misc step bodies; the node's `step` slot is the
// observable "which handler runs" result the dispatcher would later honor.
void BindStep(ActionNode* node, ActionStepKind kind) {
    node->step = reinterpret_cast<ActionStepFn>(static_cast<intptr_t>(kind));
}
} // namespace

ActionStepKind StepKindOf(const ActionNode* node) {
    return static_cast<ActionStepKind>(reinterpret_cast<intptr_t>(node->step));
}

// ===========================================================================
// Builders.
// ===========================================================================

ActionNode* CreateSoundActionEx(Character* ch, const char* name, int param, int speedBits) {
    ActionNode* node = GetCharRender4Hooks().queueInsertEntry(ch);
    if (!node) return nullptr;
    // NB: CreateSoundActionEx does NOT clear node+12 (unlike CreateSoundAction).
    BindStep(node, ActionStepKind::kSound);
    node->type       = kTypeSound;       // node+9 = 46
    node->speedScale = kSpeedOne;        // node+376 = 1.0f (default, then overwritten)
    node->args[0]    = 0;                // node+44 scratch init (orig node+40 scratch dword)
    node->state      = 0;                // node+16 = 0
    node->args[1]    = param;            // node+48
    CopyNamePairwise(node->animBuf, name);
    // node+376 = a4 (the explicit speed override). speedScale aliases node+376.
    node->speedScale = *reinterpret_cast<const float*>(&speedBits);
    return node;
}

namespace {
// take/drop carry a SECOND name (the carried object's model name) at node+304 in the
// original. The reconstruction ActionNode has no +304 field, so we mirror it into the
// node's name buffer tail (animBuf+36) — a faithful, observable "second name" slot.
constexpr int kSecondNameOffset = 36;
char* SecondName(ActionNode* node) { return node->animBuf + kSecondNameOffset; }
} // namespace

int CreateTakeObjectAction(Character* ch, const char* name, const char* srcObjName, int hand) {
    ActionNode* node = GetCharRender4Hooks().queueInsertEntry(ch);
    if (!node) return 0;
    node->callCount = 0;                 // node+12 = 0
    BindStep(node, ActionStepKind::kTakeObject);
    node->type     = kTypeTakeObject;    // node+9 = 49
    node->args[0]  = 0;                  // node+44 scratch init (orig node+40 scratch dword)
    node->state    = 0;                  // node+16 = 0
    node->args[2]  = 0;                  // node+52 = 0
    node->args[3]  = hand;               // node+56 = 2 (normal) / 1 (alt)
    node->owner    = ch;                 // node+20
    CopyNamePairwise(node->animBuf, name);
    if (srcObjName) {                    // src+492 -> +260 present: copy the object name
        CopyNamePairwise(SecondName(node), srcObjName);
        return 1;
    }
    GetCharRender4Hooks().unlinkEntry(node);   // no model -> unlink + fail
    return 0;
}

int CreateDropObjectAction(Character* ch, int param, const char* name,
                           const char* objName, int hand) {
    ActionNode* node = GetCharRender4Hooks().queueInsertEntry(ch);
    if (!node) return 0;
    node->callCount = 0;                 // node+12 = 0
    BindStep(node, ActionStepKind::kDropObject);
    node->type    = kTypeDropObject;     // node+9 = 50
    node->args[0] = 0;                   // node+44 scratch init (orig node+40 scratch dword)
    node->state   = 0;                   // node+16 = 0
    node->args[2] = 0;                   // node+52 = 0
    node->args[3] = hand;                // node+56 = 2 (normal) / 1 (alt)
    node->owner   = ch;                  // node+20
    node->args[1] = param;               // node+48
    if (name) CopyNamePairwise(node->animBuf, name);
    else      node->animBuf[0] = 0;      // node+240 = 0
    if (objName) CopyNamePairwise(SecondName(node), objName);
    else         SecondName(node)[0] = 0; // node+304 = 0
    return 1;
}

// ===========================================================================
// Script-command handlers.
// ===========================================================================
namespace {
// Builder kinds the three sit/stand handlers dispatch to.
enum class SitGetCreate { kPlaySample, kSampleLoop, kPlaySampleAtOnce };

// Shared SitDown/GetUp/SitDownAtOnce entry guard + tail. Returns the original int
// result; *latched is set when the handler latches itself as the chained command;
// *created records whether the create call inserted a node; *seekOut (optional)
// receives the SitDownAtOnce seek-flag application on the created node.
int CmdSitGetTail(const CmdScriptCtx& c, bool* latched, bool* created,
                  SitGetCreate kind, bool* seekOut, const char* errMsg) {
    *latched = false;
    *created = false;
    if (seekOut) *seekOut = false;
    // Re-entry guard: if THIS handler is the active executing command and the actor
    // still has a pending action, re-latch it and return 0 (do not re-create).
    if (c.hasExecCmd && c.execStepIsSelf) {
        if (c.actionHead) *latched = true;   // *(currentCtx+2528) = this handler
        return 0;
    }
    if (!c.actor) {                          // invalid character -> report + return 1
        if (c.reportError) c.reportError(errMsg);
        return 1;
    }
    ActionNode* node = nullptr;
    if (kind == SitGetCreate::kSampleLoop) {
        // CmdGetUp: CreateSampleLoopAction returns 1/0 (no node ptr); model "created".
        *created = CreateSampleLoopAction(c.actor, c.sampleName) != 0;
    } else {
        node = CreatePlaySampleAction(c.actor, c.sampleName);   // returns the node ptr
        *created = (node != nullptr);
        if (kind == SitGetCreate::kPlaySampleAtOnce && node) {
            node->seekFlag |= 1;             // SitDownAtOnce: node+396 |= 1
            if (seekOut) *seekOut = true;
        }
    }
    if (c.chainLatchByte == 1) *latched = true;   // *(currentCtx+2528) = this handler
    return 0;
}
} // namespace

int CmdSitDown(const CmdScriptCtx& c, bool* latched, bool* created) {
    return CmdSitGetTail(c, latched, created, SitGetCreate::kPlaySample, nullptr,
                         "SitDown(): Invalid character");
}

int CmdGetUp(const CmdScriptCtx& c, bool* latched, bool* created) {
    return CmdSitGetTail(c, latched, created, SitGetCreate::kSampleLoop, nullptr,
                         "GetUp(): Invalid character");
}

int CmdSitDownAtOnce(const CmdScriptCtx& c, bool* latched, bool* created, bool* seekFlagSet) {
    return CmdSitGetTail(c, latched, created, SitGetCreate::kPlaySampleAtOnce, seekFlagSet,
                         "SitDown(): Invalid character");
}

int CmdCharacterCount(Character* const* table, int count, const void* activeUniverse) {
    int n = 0;
    for (int i = 0; i < count; ++i) {
        Character* a = table[i];
        if (a && a->universe == activeUniverse) ++n;   // *(slot+136) == off_649D64
    }
    return n;
}

} // namespace guild::sim
