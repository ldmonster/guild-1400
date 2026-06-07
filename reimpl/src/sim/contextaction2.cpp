#include "sim/contextaction2.h"

#include <cstdint>

// Faithful 1:1 port of the deferred ContextAction variants. See contextaction2.h
// for per-function provenance. These reuse interaction_handlers' record/event
// layouts and route every privilege/tooltip side effect through the shared
// InvokePrivilegeLeaf / RecordTooltip wrappers so both batches observe one trace.

namespace guild::sim {

// ===========================================================================
// Gesetz leaf hook (FileLawsuit pair). Default: no record.
// ===========================================================================
static bool DefaultGesetzFind(ContextActor*, int, int* out) {
    if (out) *out = 0;
    return false;
}
static GesetzFindRecordFn g_gesetzHook = DefaultGesetzFind;
void SetGesetzFindRecordHook(GesetzFindRecordFn fn) {
    g_gesetzHook = fn ? fn : DefaultGesetzFind;
}

// ===========================================================================
// Rank-train tier variants.
// ---------------------------------------------------------------------------
// Shared body. `exact`: the ==N tiers return 4 when above, 1 when below.
// Otherwise (>=N tiers) return 1 when below. TrainRank4Plus is the odd one: it
// returns 4 only when rank == 6 and rejects (1) when rank < 4, so it is expressed
// with an explicit (overRank, underRank) pair below.
// ===========================================================================
namespace {
char TrainTier(ContextActor* actor, InteractionEventRec* ev,
               u8 overRank, bool hasOver, u8 underRank, int tooltipId) {
    if (ev->mode != 3 && ev->mode != 1)
        return 1;
    u8 r = actor->rank;
    if (hasOver && r > overRank)   // pure >N (TrainRank4B uses overRank=4)
        return 4;
    if (r == overRank && !hasOver) // exact "==6 -> 4" (TrainRank4Plus)
        return 4;
    if (r < underRank)
        return 1;
    if (ev->mode == 3 && actor->kind == 6) {
        InvokePrivilegeLeaf(kPrivShowDialog, actor, ev);
    } else if (ev->mode == 1 && actor->kind == 6) {
        RecordTooltip(ev, tooltipId);
        return 2;
    }
    return 2;
}
} // namespace

// gilde.exe 0x56f514 — VIBE_ContextAction_TrainRank4B (rank == 4 exact).
//   v4 > 4 -> 4 ; v4 < 4 -> 1.
char ContextTrainRank4B(ContextActor* actor, InteractionEventRec* ev) {
    return TrainTier(actor, ev, /*overRank*/4, /*hasOver*/true, /*underRank*/4, 0x1986);
}
// gilde.exe 0x56f5c0 — VIBE_ContextAction_TrainRank4Plus.
//   v4 == 6 -> 4 ; v4 < 4 -> 1 (4 and 5 fall through to the dialog).
char ContextTrainRank4Plus(ContextActor* actor, InteractionEventRec* ev) {
    return TrainTier(actor, ev, /*overRank*/6, /*hasOver*/false, /*underRank*/4, 0x1988);
}
// gilde.exe 0x56f710 — VIBE_ContextAction_TrainRank5B (rank >= 5).
char ContextTrainRank5B(ContextActor* actor, InteractionEventRec* ev) {
    return TrainTier(actor, ev, /*overRank*/0, /*hasOver*/false, /*underRank*/5, 0x198C);
}
// gilde.exe 0x56f85c — VIBE_ContextAction_TrainRank6A (rank >= 6).
char ContextTrainRank6A(ContextActor* actor, InteractionEventRec* ev) {
    return TrainTier(actor, ev, /*overRank*/0, /*hasOver*/false, /*underRank*/6, 0x1992);
}
// gilde.exe 0x56f900 — VIBE_ContextAction_TrainRank6B (rank >= 6).
char ContextTrainRank6B(ContextActor* actor, InteractionEventRec* ev) {
    return TrainTier(actor, ev, /*overRank*/0, /*hasOver*/false, /*underRank*/6, 0x1994);
}

// ===========================================================================
// FileLawsuit pair.
// ---------------------------------------------------------------------------
// `lawsuitType` (1/2) selects the Gesetz record set and the (458 & alreadyMask)
// "already filed" flag; `scratchValue` is written into ev->lawsuitScratch (+532)
// before the panel leaf, matching the original's `*(v9+532) = type`.
// ===========================================================================
namespace {
char FileLawsuit(ContextActor* actor, InteractionEventRec* ev,
                 int lawsuitType, u8 alreadyMask) {
    u8 mode = ev->mode;
    char v14 = 0;
    if (mode == 3 || mode == 1) {
        int actionCode = 0;
        if (actor->profession != 0 &&
            g_gesetzHook(actor, lawsuitType, &actionCode)) {
            if (actor->kind == 6)
                RecordTooltip(ev, /*BuildPenaltyText -> recorded as code*/ actionCode);
            if ((actor->flag457 & 2) != 0)
                return 9;
            if ((actor->flag458 & alreadyMask) == 0) {
                u8 evMode = ev->mode;
                ev->targetId = actionCode;          // ev+4 := record high byte
                if (evMode == 3) {
                    ev->lawsuitScratch = lawsuitType; // ev+532 := type
                    v14 = static_cast<char>(InvokePrivilegeLeaf(kPrivEnactLaw, actor, ev));
                }
                return static_cast<char>(v14 | 2);
            }
        }
        return 1;
    }
    if (mode != 4 && mode != 2)
        return 1;
    if ((actor->flag457 & 1) == 0)
        return 1;
    ContextActor* src = ev->dragSource;
    int actionCode = 0;
    if (!src || src->profession == 0 || (src->flag458 & alreadyMask) != 0 ||
        !g_gesetzHook(src, lawsuitType, &actionCode))
        return 1;
    u8 evMode = ev->mode;
    ev->targetId = actionCode;
    if (evMode == 4) {
        ev->lawsuitScratch = lawsuitType;
        v14 = static_cast<char>(InvokePrivilegeLeaf(kPrivEnactLaw, actor, ev));
        return static_cast<char>(v14 | 0xA);
    }
    if (actor->kind != 6)
        return static_cast<char>(v14 | 0xA);
    RecordTooltip(ev, actionCode);
    return static_cast<char>(v14 | 0xA);
}
} // namespace

// gilde.exe 0x56fbe0 — VIBE_ContextAction_FileLawsuitType1 (458 & 8).
char ContextFileLawsuitType1(ContextActor* actor, InteractionEventRec* ev) {
    return FileLawsuit(actor, ev, 1, 0x08);
}
// gilde.exe 0x56fd3c — VIBE_ContextAction_FileLawsuitType2 (458 & 0x10).
char ContextFileLawsuitType2(ContextActor* actor, InteractionEventRec* ev) {
    return FileLawsuit(actor, ev, 2, 0x10);
}

// ===========================================================================
// Profession-menu variants.
// ---------------------------------------------------------------------------
// `useSubmethod` selects the gate field: submethod (+361) vs profession (+358).
// `accept`/`acceptCount` is the value set. Verdict logic identical to the first
// batch's ContextProfessionMenu (mode 3 -> dialog, mode 1 -> tooltip; else 1/2).
// ===========================================================================
namespace {
char ProfessionMenu(ContextActor* actor, InteractionEventRec* ev,
                    bool useSubmethod, const u8* accept, int acceptCount,
                    int tooltipId) {
    if (ev->mode != 3 && ev->mode != 1)
        return 1;
    u8 gate = useSubmethod ? actor->profession2 /*+361*/ : actor->profession /*+358*/;
    bool ok = false;
    for (int i = 0; i < acceptCount; ++i)
        if (gate == accept[i]) { ok = true; break; }
    if (!ok)
        return 1;
    if (ev->mode == 3 && actor->kind == 6) {
        InvokePrivilegeLeaf(kPrivShowDialog, actor, ev);
    } else if (ev->mode == 1 && actor->kind == 6) {
        RecordTooltip(ev, tooltipId);
        return 2;
    }
    return 2;
}
} // namespace

// Submethod (+361)-gated:
char ContextProfessionMenu28(ContextActor* actor, InteractionEventRec* ev) {        // 0x570028
    static const u8 acc[] = {28};
    return ProfessionMenu(actor, ev, true, acc, 1, 0x19A3);
}
char ContextProfessionMenu29(ContextActor* actor, InteractionEventRec* ev) {        // 0x5700e8
    static const u8 acc[] = {29};
    return ProfessionMenu(actor, ev, true, acc, 1, 0x19A5);
}
char ContextProfessionMenu34(ContextActor* actor, InteractionEventRec* ev) {        // 0x5701a8
    static const u8 acc[] = {34};
    return ProfessionMenu(actor, ev, true, acc, 1, 0x19A7);
}
char ContextProfessionMenuRange30(ContextActor* actor, InteractionEventRec* ev) {   // 0x570268
    static const u8 acc[] = {30, 31, 32, 33};   // submethod 0x1E..0x21
    return ProfessionMenu(actor, ev, true, acc, 4, 0x19A9);
}
// Profession (+358)-gated:
char ContextProfessionMenuOffice(ContextActor* actor, InteractionEventRec* ev) {    // 0x57032c
    static const u8 acc[] = {20, 24, 25};
    return ProfessionMenu(actor, ev, false, acc, 3, 0x19AB);
}
char ContextProfessionMenuType17A(ContextActor* actor, InteractionEventRec* ev) {   // 0x5705bc
    static const u8 acc[] = {17};
    return ProfessionMenu(actor, ev, false, acc, 1, 0x19BB);
}
char ContextProfessionMenuType17B(ContextActor* actor, InteractionEventRec* ev) {   // 0x570684
    static const u8 acc[] = {17};
    return ProfessionMenu(actor, ev, false, acc, 1, 0x19BD);
}
char ContextProfessionMenuClergy(ContextActor* actor, InteractionEventRec* ev) {    // 0x57074c
    static const u8 acc[] = {14, 10};
    return ProfessionMenu(actor, ev, false, acc, 2, 0x19BF);
}
char ContextProfessionMenuType10(ContextActor* actor, InteractionEventRec* ev) {    // 0x5708dc
    static const u8 acc[] = {10};
    return ProfessionMenu(actor, ev, false, acc, 1, 0x19C4);
}
char ContextProfessionMenuType15(ContextActor* actor, InteractionEventRec* ev) {    // 0x57097c
    static const u8 acc[] = {15};
    return ProfessionMenu(actor, ev, false, acc, 1, 0x19C6);
}
char ContextProfessionMenuType23(ContextActor* actor, InteractionEventRec* ev) {    // 0x570bb0
    static const u8 acc[] = {23, 19};
    return ProfessionMenu(actor, ev, false, acc, 2, 0x19D4);
}

// ===========================================================================
// Command-by-profession + drag variants. Identical shape to the first batch's
// ContextCommandByProfession; replicated here against this batch's leaf ids.
//   activate (mode 3/1): if kind==6 render tooltip; if profession in `accept`:
//        busy(457&2)->9; on mode3 invoke leaf; return 2. else reject(1).
//   drag (mode 4/2): (457&1) && dragSource && src.profession in `accept`:
//        on mode4 invoke leaf, return 10; else (kind6) render tooltip; return 10.
//        else reject(1).
// ===========================================================================
namespace {
char CommandByProfession(ContextActor* actor, InteractionEventRec* ev,
                         const u8* accept, int acceptCount, int leafId,
                         int tooltipId) {
    u8 mode = ev->mode;
    if (mode == 3 || mode == 1) {
        if (actor->kind == 6)
            RecordTooltip(ev, tooltipId);
        bool ok = false;
        for (int i = 0; i < acceptCount; ++i)
            if (actor->profession == accept[i]) { ok = true; break; }
        if (!ok)
            return 1;
        if ((actor->flag457 & 2) != 0)
            return 9;
        if (ev->mode == 3)
            InvokePrivilegeLeaf(leafId, actor, ev);
        return 2;
    }
    if (mode != 4 && mode != 2)
        return 1;
    if ((actor->flag457 & 1) == 0)
        return 1;
    ContextActor* src = ev->dragSource;
    if (!src)
        return 1;
    bool ok = false;
    for (int i = 0; i < acceptCount; ++i)
        if (src->profession == accept[i]) { ok = true; break; }
    if (!ok)
        return 1;
    if (ev->mode == 4) {
        InvokePrivilegeLeaf(leafId, actor, ev);
        return 10;
    }
    if (actor->kind != 6)
        return 10;
    RecordTooltip(ev, tooltipId);
    return 10;
}
} // namespace

char ContextCommandType24(ContextActor* actor, InteractionEventRec* ev) {       // 0x570af8
    static const u8 acc[] = {24};
    return CommandByProfession(actor, ev, acc, 1, kPrivGenerateHatred, 0x19CE);
}
char ContextCommandType21Or26(ContextActor* actor, InteractionEventRec* ev) {   // 0x570e2c
    static const u8 acc[] = {21, 26};
    return CommandByProfession(actor, ev, acc, 2, kPrivConvert, 0x19E0);
}
char ContextCommandType24Drag(ContextActor* actor, InteractionEventRec* ev) {   // 0x570fb0
    static const u8 acc[] = {24};
    return CommandByProfession(actor, ev, acc, 1, kPrivApology, 0x19E9);
}
char ContextCommandType18Or22(ContextActor* actor, InteractionEventRec* ev) {   // 0x571068
    static const u8 acc[] = {18, 22};
    return CommandByProfession(actor, ev, acc, 2, kPrivEvidenceReviewAlt, 0x19EC);
}
char ContextCommandType26(ContextActor* actor, InteractionEventRec* ev) {       // 0x571134
    static const u8 acc[] = {26};
    return CommandByProfession(actor, ev, acc, 1, kPrivMiracle, 0x19F5);
}

// ===========================================================================
// Registration.
// ===========================================================================
namespace {
struct Binding { int address; ContextAction2Fn fn; };
const Binding kBindings[] = {
    { 0x56f514, &ContextTrainRank4B },
    { 0x56f5c0, &ContextTrainRank4Plus },
    { 0x56f710, &ContextTrainRank5B },
    { 0x56f85c, &ContextTrainRank6A },
    { 0x56f900, &ContextTrainRank6B },
    { 0x56fbe0, &ContextFileLawsuitType1 },
    { 0x56fd3c, &ContextFileLawsuitType2 },
    { 0x570028, &ContextProfessionMenu28 },
    { 0x5700e8, &ContextProfessionMenu29 },
    { 0x5701a8, &ContextProfessionMenu34 },
    { 0x570268, &ContextProfessionMenuRange30 },
    { 0x57032c, &ContextProfessionMenuOffice },
    { 0x5705bc, &ContextProfessionMenuType17A },
    { 0x570684, &ContextProfessionMenuType17B },
    { 0x57074c, &ContextProfessionMenuClergy },
    { 0x5708dc, &ContextProfessionMenuType10 },
    { 0x57097c, &ContextProfessionMenuType15 },
    { 0x570bb0, &ContextProfessionMenuType23 },
    { 0x570af8, &ContextCommandType24 },
    { 0x570e2c, &ContextCommandType21Or26 },
    { 0x570fb0, &ContextCommandType24Drag },
    { 0x571068, &ContextCommandType18Or22 },
    { 0x571134, &ContextCommandType26 },
};
} // namespace

int RegisterContextActions() {
    return static_cast<int>(sizeof(kBindings) / sizeof(kBindings[0]));
}

ContextAction2Fn ContextAction2_Lookup(int address) {
    for (const auto& b : kBindings)
        if (b.address == address)
            return b.fn;
    return nullptr;
}

} // namespace guild::sim
