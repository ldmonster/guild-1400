// privilege_panels_b.cpp — see privilege_panels_b.h. 1:1 reconstruction of the
// SET-B guild-office privilege panels (law / evidence / espionage + dispatch).
// gilde.exe imagebase 0x400000. Coupled GUI/command/person-array leaves deferred.
#include "world/privilege_panels_b.h"

#include <cstddef>
#include <cstring>

#include "world/law.h"        // g_lawTable, GesetzGetRecord (reused)
#include "world/law_types.h"  // LawRecord, kLawCount
#include "util/coord.h"       // ConvertX (truncate toward zero) — VIBE_Coord_ConvertX
#include "world/office_recon_privilege.h" // PrivEvidence* predicates (reused, no ODR)
#include "sim/pathfind_map.h" // ObjectSearchFindOneByPaletteRange (REAL leaf, reused)

namespace guild::world {

// gilde.exe 0x571221..0x57128d
const char* PrivShowDialogBitmap(int size) {
    if (size == 0)
        return "privillegien\\prv_small";   // aPrivillegienPr_0
    if (size == 1)
        return "privillegien\\prv_big";     // aPrivillegienPr
    return "privillegien\\prv_very_big";    // aPrivillegienPr_1
}

// gilde.exe 0x4c244c — the Gesetz table + GesetzGetRecord are reused from
// world/law.cpp (g_lawTable / GesetzGetRecord(u8, LawRecord*)). Here we expose the
// two clamp fields the EnactLaw apply path reads (record ints @+4 and @+8) and the
// clamp/enqueue rules, reading the canonical LawRecord bytes.
static i32 ReadRecI32(const LawRecord& rec, std::size_t off) {
    const u8* b = reinterpret_cast<const u8*>(&rec) + off;
    return static_cast<i32>(static_cast<u32>(b[0]) |
                            (static_cast<u32>(b[1]) << 8) |
                            (static_cast<u32>(b[2]) << 16) |
                            (static_cast<u32>(b[3]) << 24));
}

i32 GesetzRecordMinAmount(const LawRecord& rec) {
    return ReadRecI32(rec, kGesetzMinOffset);   // v5[1] @ +4
}

i32 GesetzRecordMaxAmount(const LawRecord& rec) {
    return ReadRecI32(rec, kGesetzMaxOffset);   // v5[2] @ +8
}

// gilde.exe 0x4c24b5 — clamp amount into [min, max].
i32 GesetzClampAmount(const LawRecord& rec, i32 amount) {
    i32 lo = GesetzRecordMinAmount(rec);
    i32 hi = GesetzRecordMaxAmount(rec);
    if (amount >= lo) {                   /*0x4c24b5*/
        if (amount > hi)                  /*0x4c24ef*/
            amount = hi;
    } else {
        amount = lo;                      /*0x4c24b7*/
    }
    return amount;
}

// gilde.exe 0x4c2488..0x4c24e7 — RequestApply enqueue gate.
bool GesetzApplyWouldEnqueue(int lawIndex, bool hasTarget, bool targetIdIsFFFF) {
    if (lawIndex < 0 || lawIndex >= kLawCount)  /*0x4c2488*/
        return false;
    if (hasTarget && targetIdIsFFFF)            /*0x4c24e2 -> return -1*/
        return false;
    return true;                                /*a1==0: v3=-1, still enqueues op70*/
}

// gilde.exe head shared by the kind-3/4 panels.
int PrivResolveSubjectKind(u8 kindByte, bool linkValid) {
    if (kindByte == 3)
        return kPrivSubjectPrimary;
    if (kindByte == 4)
        return linkValid ? kPrivSubjectLinked : kPrivSubjectNone;
    return kPrivSubjectNone;
}

// gilde.exe 0x561be1
u32 EnactLawClassMask(int lawClass) {
    if (lawClass == 1)
        return kEnactLawMaskClass1;      /*0x561bec*/
    if (lawClass == 2)
        return kEnactLawMaskClass2;      /*0x561c21*/
    return 0;                            /*else return 96*/
}

// gilde.exe 0x561bf8..0x561c64
int EnactLawNonOfficePrecheck(int actorRank, bool recordFound, i32 amount,
                              i32 recMin, i32 recMax, i32 recForbidden) {
    if (actorRank < 2)                   /*0x561c05*/
        return 32;
    if (!recordFound)                    /*0x561c35*/
        return 96;
    if (amount == recForbidden || amount > recMax || amount < recMin) /*0x561c53*/
        return 96;
    return 0;                            /*proceed to RequestApply*/
}

// gilde.exe 0x562373..0x5623ae — ConvertX truncates toward zero.
i32 EmbezzleAmount(int randMod, i32 wage) {
    int r = randMod + kEmbezzleRandBias;                       /*0x562373*/
    double v = static_cast<double>(r) * static_cast<double>(kEmbezzleRate);
    v = v * static_cast<double>(wage);                         /*0x56239c*/
    v = guild::util::ConvertX(v);                              /*0x5c6b08 trunc*/
    return static_cast<i32>(v);
}

// gilde.exe 0x5651d9
int MiracleKindDispatch(u8 actorKindByte) {
    if (actorKindByte == 6 || actorKindByte == 7) {
        return actorKindByte == 6 ? 1 : 0;  /*kind6 dialog; kind7 -> 0*/
    }
    return -1;                              /*non-office direct path*/
}

// gilde.exe 0x562d0b
SwapSeatPair SwapSeatsPairFor(u8 officeByte) {
    switch (officeByte) {
        case 15: return SwapSeatPair{13, 14, true};  /*v25=13,v26=14*/
        case 21: return SwapSeatPair{19, 20, true};  /*v25=19,v26=20*/
        case 27: return SwapSeatPair{25, 26, true};  /*v25=25,v26=26*/
        default: return SwapSeatPair{0, 0, false};   /*return 32*/
    }
}

// gilde.exe 0x562d21..0x562d80 — scan, remaining starts at 2, stop early at 0.
int SwapSeatsCountHolders(const u8* officeBytes, int count,
                          const SwapSeatPair& pair) {
    int remaining = 2;
    if (!officeBytes)
        return 0;
    for (int i = 0; i < count && i < 768 && remaining; ++i) { /*0x562d80*/
        u8 ob = officeBytes[i];
        if (ob == pair.lower) {          /*0x562d60*/
            --remaining;
        } else if (ob == pair.upper) {   /*0x562e2f*/
            --remaining;
        }
    }
    return 2 - remaining;                // located count (0..2)
}

// gilde.exe 0x562e49 / 0x562e66
int SwapSeatsNonOfficeResult(int actorRank, bool bothFound, bool promoteOk) {
    if (actorRank >= 6 && bothFound && promoteOk) /*0x562e49*/
        return 16;
    return 32;
}

// gilde.exe 0x562125..0x5621d6
int RemoveOfficeNonOfficeResult(bool targetFound, bool holderEntryFound,
                                bool categoryMatches) {
    if (!targetFound)                    /*0x56213d*/
        return kRemoveOfficeNoTarget;
    if (holderEntryFound && categoryMatches) /*0x56216b*/
        return kRemoveOfficeDone;
    return kRemoveOfficeNoTarget;        /*0x56217a*/
}

// gilde.exe 0x5620ce..0x562324
int RemoveOfficeSessionResult(bool sessionPersonFound, bool holderEntryFound,
                              bool removable, bool packetConflict) {
    if (!sessionPersonFound)             /*0x5620f1*/
        return kRemoveOfficeSessionNull;
    if (!holderEntryFound)               /*0x5621ec*/
        return kRemoveOfficeNoTarget;
    if (!removable)                      /*0x562289 (byte+16 != 1)*/
        return kRemoveOfficeNotRemovable;
    if (packetConflict)                  /*0x5622e5 (status == 2)*/
        return kRemoveOfficeConflict;
    return kRemoveOfficeCleanRemoved;    /*0x562324 -> -112*/
}

// gilde.exe 0x562913..0x5629f3
int CounterEspNonOfficeResult(int actorRank, bool anyAgentReset) {
    if (actorRank < 4)                   /*0x562913*/
        return kCounterEspRankGate;      /*32*/
    int v45 = kCounterEspBase;           /*2*/
    if (anyAgentReset)                   /*0x5629dc: v45 |= 0x10*/
        v45 |= kCounterEspResetBit;
    return v45;
}

// gilde.exe 0x565fdf..0x566036
int EvidenceReviewConcretePrecheck(u8 actorOfficeByte, bool targetFound,
                                   u8 targetOfficeByte, int matchCount) {
    if (actorOfficeByte == 13)           /*0x565fdf judge*/
        return 96;
    if (!targetFound)                    /*0x566004*/
        return 96;
    if (targetOfficeByte == 13)          /*0x56600d*/
        return 96;
    if (matchCount == 0)                 /*0x566036*/
        return 96;
    return 0;                            /*-> BuildEvidenceEntry*/
}

// ===========================================================================
// PANEL-SHAPED BODIES (full 1:1 control flow; coupled leaves via the B-hooks).
// These reuse the predicate helpers above for the load-bearing decision math.
// ===========================================================================
namespace {

void EmitB(PrivilegePanelBHooks* h, int op, i32 a = 0, i32 b = 0, i32 c = 0, i32 d = 0) {
    if (!h || !h->trace) return;
    PrivBTrace* t = h->trace;
    if (t->cmdCount < static_cast<int>(sizeof(t->cmds) / sizeof(t->cmds[0]))) {
        PrivBCommand& cmd = t->cmds[t->cmdCount++];
        cmd.op = op; cmd.a = a; cmd.b = b; cmd.c = c; cmd.d = d;
    }
}
void SetDoneB(PrivilegePanelBHooks* h, int state) {
    if (h && h->trace) h->trace->doneState = state;
}
bool IsOfficeKind(u8 kind) { return kind == 6 || kind == 7; } // 0x...== 6 || == 7
int NextBtn(PrivilegePanelBHooks* h) {
    return (h && h->nextButton) ? h->nextButton(h->ctx) : kPrivLoopExit;
}
bool CheckSkill(PrivilegePanelBHooks* h, const PrivPerson* a, int level) {
    return (h && h->checkSkill) ? h->checkSkill(a, level, h->ctx) : false;
}
// The subject (a1 for kind 3; the linked drag-source for kind 4) — shared head.
// Returns the subject view, or null when the panel must early-out with 96.
const PrivPerson* PickSubject(const PrivPerson* actor, const PrivEvent* ev,
                              bool* outBad) {
    *outBad = false;
    if (ev && ev->mode == 3) return actor;          /*v4 == 3 -> a1*/
    if (ev && ev->mode == 4 && ev->dragSource)      /*v4 == 4 && +540 != 0*/
        return ev->dragSource;
    *outBad = true;                                  /*else return 96*/
    return nullptr;
}

} // namespace

// gilde.exe 0x561bb4 — VIBE_Privilege_PanelEnactLaw.
char PrivilegePanelEnactLaw(const PrivPerson* actor, const PrivEvent* ev,
                            PrivilegePanelBHooks* h) {
    if (!actor || !ev) return 96;
    // subject-kind gate (3 -> actor; 4 -> linked, null -> 96; else 96)
    bool bad = false;
    PickSubject(actor, ev, &bad);
    if (bad) return 96;                              /*0x561be1*/
    // law-class mask (1 -> 0x80000, 2 -> 0x100000, else 96)
    if (EnactLawClassMask(ev->dragField) == 0)       /*v5 / 0x561bec*/
        return 96;

    if (!IsOfficeKind(actor->kind) && actor->kind != 6) {
        // The original gates strictly on (a1+2)==6 for the GUI arm; the non-office
        // direct arm runs when (a1+2)!=6.
    }
    if (actor->kind != 6) {
        // --- non-office direct path (0x561bf8) ---
        LawRecord rec{};
        bool found = GesetzGetRecord(static_cast<u8>(ev->targetId & 0xFF), &rec);
        i32 amount = ev->partnerId;                  /*event+8 (*(a2+2))*/
        int pre = EnactLawNonOfficePrecheck(
            actor->office404, found, amount,
            found ? GesetzRecordMinAmount(rec) : 0,
            found ? GesetzRecordMaxAmount(rec) : 0,
            found ? GesetzRecordMinAmount(rec) : 0); // v46 forbidden == min slot
        if (pre != 0) return static_cast<char>(pre);
        i32 clamped = GesetzClampAmount(rec, amount);
        EmitB(h, PrivBCommand::kBuildOp90, actor->handle, -2);
        EmitB(h, PrivBCommand::kArgs25, actor->handle, 456, EnactLawClassMask(ev->dragField), 4);
        if (h && h->trace) h->trace->lastAmount = clamped;
        return 0;                                    /*v52 == 0 on the direct path*/
    }

    // --- office (kind 6) GUI frame-loop arm (0x561c64) ---
    LawRecord rec{};
    if (!GesetzGetRecord(static_cast<u8>((ev->targetId >> 8) & 0xFF), &rec))
        return 0;                                    /*0x561cae*/
    u8 mode = static_cast<u8>(GesetzRecordMinAmount(rec) & 0xFF); // v39[4] penalty-mode slot
    (void)mode;
    if (h && h->trace) h->trace->lastFormScene = 0x624bf0; // "privillegien\\gesetz"
    char v52 = 0;
    int btn;
    while ((btn = NextBtn(h)) != kPrivLoopExit) {
        if (btn == kPrivLoopIdle) continue;
        if (btn == kPrivBtnOk) {
            if (CheckSkill(h, actor, 2)) {           /*0x561e95*/
                v52 = static_cast<char>(kEnactLawEnacted); // 128
                i32 amount = ev->partnerId;
                i32 clamped = GesetzClampAmount(rec, amount);
                EmitB(h, PrivBCommand::kBuildOp90, actor->handle, -2);
                EmitB(h, PrivBCommand::kArgs25, actor->handle, 456, 0, 4);
                if (h && h->trace) h->trace->lastAmount = clamped;
                SetDoneB(h, 1);
            }
        } else if (btn == kPrivBtnCancelId) {
            SetDoneB(h, 1);                           /*0x561f1e*/
        }
    }
    return v52;                                       /*0x561f5a*/
}

// gilde.exe 0x561fd0 — VIBE_Privilege_RemoveFromOffice.
char PrivilegeRemoveFromOffice(const PrivPerson* actor, const PrivEvent* ev,
                               PrivilegePanelBHooks* h) {
    if (!actor || !ev) return 96;
    bool bad = false;
    const PrivPerson* subject = PickSubject(actor, ev, &bad);
    if (bad || !subject) return 96;                  /*0x561fe4*/
    u8 subjCat = 0;
    bool defFound = h && h->officeGetDefinition
        ? h->officeGetDefinition(subject->kind /*office byte +358 proxy*/, &subjCat, h->ctx)
        : false;
    if (RemoveOfficeDefinitionGate(defFound) != 0)   /*0x562005*/
        return 96;

    if (actor->kind != 6) {
        // --- non-office direct path (0x56200f) ---
        const PrivPerson* target = (h && h->findRecord)
            ? h->findRecord(ev->targetId, h->ctx) : nullptr; /*+532*/
        u8 tgtCat = 0; bool removable = false;
        bool holderFound = target && h && h->officeHolderEntry
            ? h->officeHolderEntry(target->handle, &tgtCat, &removable, h->ctx)
            : false;
        bool categoryMatches = holderFound && (tgtCat == subjCat); /*>>24 == >>24*/
        int r = RemoveOfficeNonOfficeResult(target != nullptr, holderFound,
                                            categoryMatches);
        if (r == kRemoveOfficeDone) {
            EmitB(h, PrivBCommand::kActionStart);
            EmitB(h, PrivBCommand::kArgs25, subject->handle, 456, 0, 4);
            EmitB(h, PrivBCommand::kCoord27, actor->handle,
                  target ? target->handle : 0, -40);
            EmitB(h, PrivBCommand::kActionEnd);
        }
        return static_cast<char>(r);
    }

    // --- office (kind 6) session path (0x56204a) ---
    const PrivPerson* picked = (h && h->runOfficeSession)
        ? h->runOfficeSession(subjCat, 6552, h->ctx) : nullptr;
    if (h && h->trace) h->trace->lastFormScene = 6552;
    u8 tgtCat = 0; bool removable = false;
    bool holderFound = picked && h && h->officeHolderEntry
        ? h->officeHolderEntry(picked->handle, &tgtCat, &removable, h->ctx)
        : false;
    // packetConflict is decided by the live command queue; the inert path == clean.
    int r = RemoveOfficeSessionResult(picked != nullptr, holderFound, removable,
                                      /*packetConflict*/ false);
    if (r == kRemoveOfficeCleanRemoved) {
        EmitB(h, PrivBCommand::kActionStart);
        EmitB(h, PrivBCommand::kArgs25, subject->handle, 456, 0, 4);
        EmitB(h, PrivBCommand::kCoord27, actor->handle,
              picked ? picked->handle : 0, -20);
        EmitB(h, PrivBCommand::kActionEnd);
    }
    return static_cast<char>(r);
}

// gilde.exe 0x5628c8 — VIBE_Privilege_PanelCounterEspionage.
char PrivilegePanelCounterEspionage(const PrivPerson* actor,
                                    PrivilegePanelBHooks* h) {
    if (!actor) return 32;
    if (IsOfficeKind(actor->kind)) {
        // --- office GUI arm: always returns 2 (0x5628df) ---
        if (h && h->trace) h->trace->lastFormScene = 0x624c48; // "gegenspionage"
        int btn;
        while ((btn = NextBtn(h)) != kPrivLoopExit) {
            if (btn == kPrivLoopIdle) continue;
            if (btn == kPrivBtnOk) {
                if (CheckSkill(h, actor, 4)) {
                    int n = (h && h->counterEspScan)
                        ? h->counterEspScan(actor->handle, true, h->ctx) : 0;
                    if (h && h->trace) h->trace->agentsReset = n;
                    EmitB(h, PrivBCommand::kBuildOp90, actor->handle, -4);
                    SetDoneB(h, 1);
                }
            } else if (btn == kPrivBtnCancelId) {
                SetDoneB(h, 1);
            }
        }
        return static_cast<char>(kCounterEspDialogResult); // 2
    }
    // --- non-office direct arm (0x562913) ---
    int n = (h && h->counterEspScan)
        ? h->counterEspScan(actor->handle, false, h->ctx) : 0;
    if (h && h->trace) h->trace->agentsReset = n;
    if (actor->office404 >= 4)
        EmitB(h, PrivBCommand::kBuildOp90, actor->handle, -4);
    return static_cast<char>(CounterEspNonOfficeResult(actor->office404, n > 0));
}

// gilde.exe 0x562334 — VIBE_Privilege_PanelEmbezzlement.
char PrivilegePanelEmbezzlement(const PrivPerson* actor, const PrivEvent* ev,
                                PrivilegePanelBHooks* h) {
    if (!actor || !ev) return 96;
    bool bad = false;
    const PrivPerson* subject = PickSubject(actor, ev, &bad);
    if (bad || !subject) return 96;                  /*0x56234c*/

    i32 wage = (h && h->computeOfficeWages)
        ? h->computeOfficeWages(subject->id, h->ctx) : 0;
    int draw = (h && h->randomModulo) ? h->randomModulo(kEmbezzleRandRange, h->ctx) : 0;
    i32 amount = EmbezzleAmount(draw, wage);
    if (h && h->trace) h->trace->lastAmount = amount;

    if (actor->kind == 6) {
        // --- office GUI arm (0x56239e) ---
        if (h && h->trace) h->trace->lastFormScene = 0x624c10; // "unterschlagung"
        char ret = 0;
        int btn;
        while ((btn = NextBtn(h)) != kPrivLoopExit) {
            if (btn == kPrivLoopIdle) continue;
            if (btn == kPrivBtnOk) {
                if (CheckSkill(h, actor, 4)) {
                    EmitB(h, PrivBCommand::kRequest16, actor->handle, -1, amount);
                    EmitB(h, PrivBCommand::kBuildOp90, actor->handle, -4);
                    EmitB(h, PrivBCommand::kArgs25, subject->handle, 0, 0, 4);
                    SetDoneB(h, 1);
                }
            } else if (btn == kPrivBtnCancelId) {
                SetDoneB(h, 1);
            }
        }
        return ret;                                  /*v27*/
    }
    // --- non-office direct arm (0x5623cc) ---
    int gate = EmbezzleNonOfficeRankGate(actor->office404); // rank<4 -> 32
    if (gate != 0) return static_cast<char>(gate);
    EmitB(h, PrivBCommand::kRequest16, actor->handle, -1, amount);
    EmitB(h, PrivBCommand::kBuildOp90, actor->handle, -4);
    EmitB(h, PrivBCommand::kArgs25, subject->handle, 0, 0, 4);
    return 0;                                         /*v27*/
}

// gilde.exe 0x562cdc — VIBE_Privilege_PanelSwapSeats.
char PrivilegePanelSwapSeats(const PrivPerson* actor, const PrivEvent* ev,
                             PrivilegePanelBHooks* h) {
    if (!actor || !ev) return 96;
    // subject pick: kind 3 -> actor.officeByte; kind 4 -> linked.officeByte
    u8 officeByte;
    if (ev->mode == 3) {
        officeByte = actor->kind;          // proxy for (a1+358)
    } else if (ev->mode == 4 && ev->dragSource) {
        officeByte = ev->dragSource->kind; // proxy for (link+358)
    } else {
        return 96;                         /*0x562cf2*/
    }
    SwapSeatPair pair = SwapSeatsPairFor(officeByte);
    if (!pair.valid) return 32;            /*0x562d04 default -> 32*/

    int count = 0;
    const u8* bytes = (h && h->officeByteArray)
        ? h->officeByteArray(&count, h->ctx) : nullptr;
    int found = SwapSeatsCountHolders(bytes, count, pair); // 0..2
    bool bothFound = (found >= 2);                          // remaining<=0

    if (!IsOfficeKind(actor->kind)) {
        // --- non-office direct arm (0x562e40) ---
        bool promoteOk = bothFound && h && h->awaitPromote
            ? h->awaitPromote(actor, nullptr, nullptr, h->ctx) : false;
        return static_cast<char>(
            SwapSeatsNonOfficeResult(actor->office404, bothFound, promoteOk));
    }
    // --- office (kind 6/7) arm (0x562e74) ---
    if (!bothFound) {                       /*v3 > 0 -> message + return 0*/
        if (h && h->trace) h->trace->lastMessageId = 0x19CB;
        return 0;
    }
    if (h && h->trace) h->trace->lastFormScene = 0x624c6c; // "sessel_vertauschen"
    int btn;
    while ((btn = NextBtn(h)) != kPrivLoopExit) {
        if (btn == kPrivLoopIdle) continue;
        if (btn == kPrivBtnOk) {
            if (CheckSkill(h, actor, 6)) {
                bool ok = h && h->awaitPromote
                    ? h->awaitPromote(actor, nullptr, nullptr, h->ctx) : false;
                if (h && h->trace) h->trace->lastMessageId = ok ? 6602 : 0x19CB;
                SetDoneB(h, 1);
            }
        } else if (btn == kPrivBtnCancelId) {
            SetDoneB(h, 1);
        }
    }
    return 1;                               /*0x562fcf*/
}

// gilde.exe 0x5651bc — VIBE_Privilege_PanelMiracle.
char PrivilegePanelMiracle(const PrivPerson* actor, PrivilegePanelBHooks* h) {
    if (!actor) return 32;
    int disp = MiracleKindDispatch(actor->kind);
    if (disp == 0) return 0;                 /*kind 7 -> 0*/
    if (disp == 1) {
        // --- kind 6 GUI arm (0x5651e8) ---
        if (h && h->trace) h->trace->lastFormScene = 0x624d70; // "wunder"
        char ret = 0;
        int btn;
        while ((btn = NextBtn(h)) != kPrivLoopExit) {
            if (btn == kPrivLoopIdle) continue;
            if (btn == kPrivBtnOk) {
                if (CheckSkill(h, actor, 4)) {
                    EmitB(h, PrivBCommand::kBuildOp90, actor->handle, -4);
                    int draw = (h && h->randomModulo)
                        ? h->randomModulo(kMiracleRandRange, h->ctx) : 0;
                    EmitB(h, PrivBCommand::kBuildMemberTable, draw);
                    ret = 1;
                    SetDoneB(h, 1);
                }
            } else if (btn == kPrivBtnCancelId) {
                SetDoneB(h, 1);
            }
        }
        return ret;
    }
    // --- non-office direct arm (0x5651e2) ---
    int gate = MiracleNonOfficeRankGate(actor->office404); // rank>=4 ? 16 : 32
    if (gate == 16) {
        EmitB(h, PrivBCommand::kBuildOp90, actor->handle, -4);
        int draw = (h && h->randomModulo)
            ? h->randomModulo(kMiracleRandRange, h->ctx) : 0;
        EmitB(h, PrivBCommand::kBuildMemberTable, draw);
    }
    return static_cast<char>(gate);
}

// gilde.exe 0x565b88 — VIBE_Privilege_PanelEvidenceDetails.
// v30=actor (a1, the inspecting/active person), v31=target (a2, evidence subject).
char PrivilegePanelEvidenceDetails(const PrivPerson* actor, const PrivPerson* target,
                                   bool actorIsTarget, PrivilegePanelBHooks* h) {
    // 0x565bb4 — *a2 == 0xFFFF -> return 0 (no target). We model the target view's
    // null/absent as the 0xFFFF id sentinel.
    if (!target || !PrivEvidenceHasTarget(target->id))
        return 0;                                          /*0x565bc4*/
    // 0x565bed — FindMatchingEntityIndices(a2.handle, &v25, a1.id).
    int matchCount = (h && h->matchingEntityCount && actor)
        ? h->matchingEntityCount(target->handle, actor->id, h->ctx) : 0;
    // 0x565bfe — matchCount < 1 -> return 96.
    int pre = PrivEvidencePrecheck(matchCount);
    if (pre != 0)
        return static_cast<char>(pre);                     /*0x565f4c -> 96*/

    // 0x565c2d..0x565c6d — aggregation loop: each matched row's field37 ORed
    // into v28 as (field37 == 1). Reuse PrivEvidenceAnyActionable's exact rule.
    int v28 = 0;
    for (int i = 0; i < matchCount; ++i) {
        i32 field37 = (h && h->evidenceRowField37 && actor)
            ? h->evidenceRowField37(target->handle, actor->id, i, h->ctx) : 0;
        v28 |= (field37 == 1);                             /*0x565c64*/
    }
    // 0x565c7e — actor cannot use evidence against itself: v30==v31 -> v28 = 0.
    bool anyActionable =
        PrivEvidenceConfirmEnabled(v28 != 0, actorIsTarget); /*0x565c82*/

    if (h && h->trace) h->trace->lastFormScene = 0; // "Beweise_Details" (0x624da0)

    // 0x565ebf..0x565f29 — frame loop. dword_672230 set -> dword_631614 = 1 (close);
    // confirm click (v35 == dword_62D22C, only when the confirm button exists, i.e.
    // anyActionable) -> BuildEvidenceEntry(actor, target, 0, matchCount) + state 3.
    int btn;
    while ((btn = NextBtn(h)) != kPrivLoopExit) {
        if (btn == kPrivLoopIdle) {
            SetDoneB(h, 1);                                /*0x565edd — window close*/
            continue;
        }
        if (btn == kPrivBtnOk && anyActionable) {          /*confirm; 0x565efd*/
            RunBuildEvidenceEntry(actor, target, 0, matchCount, h); /*0x565f16*/
            SetDoneB(h, 3);                                /*0x565f1b*/
        } else if (btn == kPrivBtnCancelId) {
            SetDoneB(h, 1);                                /*window close path*/
        }
    }
    return 0;                                              /*0x565bc6 — always 0*/
}

// ===========================================================================
// gilde.exe 0x56589c — VIBE_Privilege_BuildEvidenceEntry — FULL 1:1 BODY.
//
// Reconstructs the complete function (wave-23): the judge person scan, the
// 248-byte opcode-28 court record build, the judge / accuser sourcing from the
// selection arrays (+ RNG fallback), the two palette-range witness searches, and
// the slot-reset command emission. Return codes reuse the wave-22 predicate
// PrivBuildEvidenceResult (office_recon_privilege.h) verbatim:
//   no judge person at all -> 16 ; a witness search missed -> 64 ; built -> 16.
//
// Stack-var byte offsets of the court record (gilde.exe local frame):
//   +0x04  byte  44     (v17 / var_114 — record kind 44)
//   +0x08  dword actor handle           (v18 / var_110 = a1+4)
//   +0x0C  dword selection id           (v19 / var_10C = dword_12CE914[134*idx])
//   +0x36  byte  2                      (v20 / var_E2)
//   +0x58  dword actor handle           (v21 / var_C0 = a1+4)
//   +0x5C  dword target handle          (v22 / var_BC = a2+4)
//   +0x60  dword judge handle (-1 init) (v23 / var_B8)
//   +0x64  dword witness1 handle (-1)   (v24 / var_B4)
//   +0x68  dword witness2 handle (-1)   (v25 / var_B0)
//   +0x6C  dword accuser handle (-1)    (v26 / var_AC)
// The 4-word search filter blob (&v27, seeded from dword_55272C = {-1,-1}):
//   word[0] target id (v27.lo), word[1] judge id (v27.hi),
//   word[2] accuser id (v28.lo), word[3] witness1 (v28.hi).
// ===========================================================================
namespace {
inline void EvWrU8 (u8* rec, int off, u8 v)  { rec[off] = v; }
inline void EvWrI32(u8* rec, int off, i32 v) { std::memcpy(rec + off, &v, 4); }
} // namespace

char PrivBuildEvidenceEntry(u16 actorId, i32 actorHandle, u8 actorOfficeBit12,
                            u8 actorOfficeByte358, u16 targetId, i32 targetHandle,
                            u8 targetOfficeByte358, int mode, int matchCount,
                            const EvidenceBuildWorld& w, EvidenceBuildSink& sink) {
    (void)actorId;
    // --- 0x5658c7 — seed the 4-word filter blob from dword_55272C = {-1,-1}. ---
    sink.filter[0] = 0xFFFF; sink.filter[1] = 0xFFFF;
    sink.filter[2] = 0xFFFF; sink.filter[3] = 0xFFFF;

    // --- 0x5658d3..0x5658f2 — judge person scan: QueryBegin(op 6) walks every
    //     live person; break on the first whose office byte (aiTable[589*type])
    //     == 15. If none found, return 16 (PrivBuildEvidenceResult judge=false). ---
    bool judgeFound = false;
    if (w.personTypes && w.officeByteForType) {
        for (int i = 0; i < w.personCount; ++i) {
            u8 type = w.personTypes[i];
            if (type < w.officeByteTypeCount &&
                w.officeByteForType[type] == 15) {       /*0x5658ea*/
                judgeFound = true;
                break;
            }
        }
    }
    if (!judgeFound)
        return static_cast<char>(PrivBuildEvidenceResult(false, false)); /*0x5658f2 -> 16*/

    // --- 0x5658f8..0x56595e — build the fixed record fields. ---
    u8* rec = sink.record;
    std::memset(rec, 0, sizeof(sink.record));
    EvWrU8 (rec, 0x04, 44);                                 /*v17*/
    EvWrI32(rec, 0x08, actorHandle);                        /*v18 = a1+4*/
    // var_10C = dword_12CE914[134 * word_63CC5C] (the active selection id).
    i32 selId = 0;
    if (w.personHandleById && w.localPlayerIndex < w.personHandleCount)
        selId = w.personHandleById[134 * w.localPlayerIndex];
    EvWrI32(rec, 0x0C, selId);                             /*v19*/
    EvWrU8 (rec, 0x36, 2);                                  /*v20*/
    EvWrI32(rec, 0x58, actorHandle);                        /*v21 = a1+4*/
    EvWrI32(rec, 0x5C, targetHandle);                       /*v22 = a2+4*/
    EvWrI32(rec, 0x60, -1);                                 /*v23 judge   = -1*/
    EvWrI32(rec, 0x64, -1);                                 /*v24 witness1 = -1*/
    EvWrI32(rec, 0x68, -1);                                 /*v25 witness2 = -1*/
    EvWrI32(rec, 0x6C, -1);                                 /*v26 accuser  = -1*/
    sink.filter[0] = targetId;                              /*v27.lo = *a2*/

    // --- 0x565968 — judge sourcing (mode arg a3 selects the branch). ---
    i32 judgeHandle = -1;
    u16 judgeId = 0xFFFF;
    bool judgeIdValid = false;
    if (mode) {                                            /*0x565b06 — mode != 0*/
        // Judge is one of the two court-party player records, chosen by a1+12.
        if (actorOfficeBit12) {                            /*0x565b06*/
            judgeHandle = w.playerRecBHandle;              /*dword_6498EC[0]*/
            judgeId = w.playerRecBId;                      /*0x565b37*/
        } else {
            judgeHandle = w.playerRecAHandle;              /*dword_6498E8*/
            judgeId = w.playerRecAId;                      /*0x565b23*/
        }
        judgeIdValid = true;
    } else {                                               /*0x565974 — mode == 0*/
        // Scan the selection office array for a kind-13 (judge) holder.
        int found = -1;
        if (w.personOfficeById) {
            for (int v7 = 0; v7 < 0x300; ++v7) {           /*0x56597d*/
                if (v7 < w.personOfficeCount &&
                    w.personOfficeById[536 * v7] == 13) {  /*0x565978 / 0x56598b*/
                    found = v7;
                    break;
                }
            }
        }
        if (found >= 0) {                                  /*LABEL_8 0x565994*/
            if (w.personHandleById && (134 * found) / 134 < w.personHandleCount)
                judgeHandle = w.personHandleById[134 * found];
            if (w.personIdById && 268 * found < 268 * w.personIdCount)
                judgeId = w.personIdById[268 * found];
            judgeIdValid = true;
        }
        if (judgeHandle == -1) {                            /*0x5659ad — no holder*/
            // RNG fallback: RandomModulo(2) picks player A or B.
            if (w.rngJudgePick) {                           /*0x5659be nonzero*/
                judgeHandle = w.playerRecAHandle;           /*dword_6498E8*/
                judgeId = w.playerRecAId;
            } else {                                        /*0x565afb*/
                judgeHandle = w.playerRecBHandle;           /*dword_6498EC[0]*/
                judgeId = w.playerRecBId;
            }
            judgeIdValid = true;
        }
    }
    EvWrI32(rec, 0x60, judgeHandle);                        /*var_B8*/
    if (judgeIdValid)
        sink.filter[1] = judgeId;                          /*v27.hi = v10*/

    // --- 0x5659f6..0x565a4e — accuser sourcing: only when NEITHER the target
    //     (a2+358) NOR the actor (a1+358) office byte is 17. Scan the selection
    //     office array for a kind-17 holder. ---
    if (targetOfficeByte358 != 17 && actorOfficeByte358 != 17) { /*0x5659e6/0x5659ef*/
        int found = -1;
        if (w.personOfficeById) {
            for (int v11 = 0; v11 < 0x300; ++v11) {        /*0x565a07*/
                if (v11 < w.personOfficeCount &&
                    w.personOfficeById[536 * v11] == 17) { /*0x565a02 / 0x565a15*/
                    found = v11;
                    break;
                }
            }
        }
        if (found >= 0) {                                   /*LABEL_19 0x565a1e*/
            if (matchCount > 2) {                            /*v29 > 2 -> 0x565a26*/
                if (w.personHandleById && found < w.personHandleCount)
                    EvWrI32(rec, 0x6C,
                            w.personHandleById[134 * found]); /*var_AC*/
            }
            // v28.lo = word_12CE910[ ((67*v11)*8) words ] == personId at 536*v11.
            if (w.personIdById && 536 * found < 268 * w.personIdCount)
                sink.filter[2] = w.personIdById[536 * found]; /*0x565a3e*/
        }
    }

    // --- 0x565a6b — witness search #1 (the REAL FindOneByPaletteRange). Miss
    //     -> return 64 (PrivBuildEvidenceResult judge=true, witnesses=false). ---
    u16 idx0 = 0;
    int hit0 = sink.findWitness
        ? sink.findWitness(w, sink.filter, 0, &idx0, sink.ctx) : 0;
    if (!hit0)
        return static_cast<char>(PrivBuildEvidenceResult(true, false)); /*0x565a6b -> 64*/
    sink.witnessIndex[0] = idx0;
    ++sink.witnessSearches;
    // 0x565a7d — route the found id into v28.lo (if still 0xFFFF) else v28.hi.
    if (sink.filter[2] == 0xFFFF)
        sink.filter[2] = idx0;                             /*0x565a8a*/
    else
        sink.filter[3] = idx0;                             /*0x565b46*/
    // var_B4 = dword_12CE914[134 * v31[0]].
    if (w.personHandleById && idx0 < w.personHandleCount)
        EvWrI32(rec, 0x64, w.personHandleById[134 * idx0]); /*0x565aaf*/

    // --- 0x565ad0 — witness search #2. Miss -> return 64. ---
    u16 idx1 = 0;
    int hit1 = sink.findWitness
        ? sink.findWitness(w, sink.filter, 1, &idx1, sink.ctx) : 0;
    if (!hit1)
        return static_cast<char>(PrivBuildEvidenceResult(true, false)); /*0x565ae8 -> 64*/
    sink.witnessIndex[1] = idx1;
    ++sink.witnessSearches;
    // var_B0 = dword_12CE914[134 * v31[0]].
    if (w.personHandleById && idx1 < w.personHandleCount)
        EvWrI32(rec, 0x68, w.personHandleById[134 * idx1]); /*0x565b70*/

    // --- 0x565b76 — emit the opcode-28 slot-reset command; return 16. ---
    if (sink.emitReset)
        sink.emitReset(rec, sink.ctx);
    sink.emitted = true;
    return static_cast<char>(PrivBuildEvidenceResult(true, true)); /*0x565adf -> 16*/
}

// gilde.exe 0x565a6b / 0x565ad0 — the witness search routed through the REAL
// reconstructed VIBE_ObjectSearch_FindOneByPaletteRange (pathfind_map.cpp). The
// original calls FindOne(a1, 0xC00, &v27, 0.0, 100.0, &v31): refId = actor id,
// filter blob = the 4-word &v27, favourability range [0,100]. The strideIndex /
// probeStart (the live RandomModulo(16)/RandomModulo(768) draws inside FindOne)
// are supplied by the world view so the search is deterministic.
int EvidenceWitnessSearchViaObjectSearch(const EvidenceBuildWorld& w,
                                         const u16* filter, int which,
                                         u16* outIdx, void* /*ctx*/) {
    u16 refId = w.actorId;
    // a3 (&v27) is the 4-word filter blob; ObjectSearch copies it as the filter.
    return guild::sim::ObjectSearchFindOneByPaletteRange(
        &refId, const_cast<u16*>(filter), 0.0f, 100.0f, outIdx,
        w.findStride[which & 1], w.findProbe[which & 1]);
}

// Bridge the panels' confirm to the evidence-build leaf (see header).
int RunBuildEvidenceEntry(const PrivPerson* actor, const PrivPerson* target,
                          int mode, int matchCount, PrivilegePanelBHooks* h) {
    if (h && h->buildEvidenceEntry && actor)
        return h->buildEvidenceEntry(actor, target, mode, matchCount, h->ctx);
    if (h && h->evidenceWorld && actor) {
        EvidenceBuildWorld world{};
        EvidenceBuildSink  sink{};
        if (h->evidenceWorld(actor, target, &world, &sink, h->ctx)) {
            return PrivBuildEvidenceEntry(
                world.actorId, world.actorHandle, world.actorOfficeBit12,
                world.actorOfficeByte358, world.targetId, world.targetHandle,
                world.targetOfficeByte358, mode, matchCount, world, sink);
        }
    }
    return 0;  // headless: neither wired
}

// Shared EvidenceReview / EvidenceReviewAlt body. The two functions are identical
// except for the BuildEvidenceEntry `mode` arg on the non-office concrete path
// (Review=0 @0x566045, ReviewAlt=1 @0x56684e); the office HUD-list arm both open
// EvidenceDetails(actor, rowTarget) per row click, latching its return into v49/v50.
static char EvidenceReviewImpl(const PrivPerson* actor, const PrivEvent* ev,
                               PrivilegePanelBHooks* h, int mode) {
    if (!actor || !ev) return 32;
    if (IsOfficeKind(actor->kind)) {                       /*0x565fd2 / 0x5667d6*/
        // --- office HUD-list GUI arm (verdict v49/v50 init 32) ---
        if (h && h->trace) h->trace->lastFormScene = 0x624dc0; // "Beweise_Sichten"
        char verdict = 32;                                 /*v49 = 32 / v50 = 32*/
        int btn;
        while ((btn = NextBtn(h)) != kPrivLoopExit) {
            if (btn == kPrivLoopIdle) continue;
            if (btn == kPrivBtnOk) {
                // 0x5663a1 / 0x566b83 — a row click resolves to the row's evidence
                // target; EvidenceDetails latches the verdict (it returns 0).
                const PrivPerson* rowTarget = (h && h->findRecord)
                    ? h->findRecord(ev->targetId, h->ctx) : nullptr;
                verdict = PrivilegePanelEvidenceDetails(
                    actor, rowTarget, rowTarget == actor, h);
            } else if (btn == kPrivBtnCancelId) {
                SetDoneB(h, 1);                            /*dword_631614 = 1*/
            }
        }
        return verdict;                                    /*0x5662d7 / 0x566ae4*/
    }
    // --- non-office concrete path (0x565fdf / 0x5667e3) ---
    const PrivPerson* target = (h && h->findRecord)
        ? h->findRecord(ev->targetId, h->ctx) : nullptr; /*+532*/
    int matchCount = (target && h && h->matchingEntityCount)
        ? h->matchingEntityCount(target->handle, actor->id, h->ctx) : 0;
    int pre = EvidenceReviewConcretePrecheck(
        actor->kind /*office byte +358 proxy*/, target != nullptr,
        target ? target->kind : 0, matchCount);
    if (pre != 0) return static_cast<char>(pre);
    int r = RunBuildEvidenceEntry(actor, target, mode, matchCount, h);
    return static_cast<char>(r);
}

// gilde.exe 0x565f9c — VIBE_Privilege_PanelEvidenceReview (mode 0).
char PrivilegePanelEvidenceReview(const PrivPerson* actor, const PrivEvent* ev,
                                  PrivilegePanelBHooks* h) {
    return EvidenceReviewImpl(actor, ev, h, 0);
}
// gilde.exe 0x5667a0 — VIBE_Privilege_PanelEvidenceReviewAlt (mode 1).
char PrivilegePanelEvidenceReviewAlt(const PrivPerson* actor, const PrivEvent* ev,
                                     PrivilegePanelBHooks* h) {
    return EvidenceReviewImpl(actor, ev, h, 1);
}

// gilde.exe 0x571218 — VIBE_Privilege_ShowDialog (void; records backdrop scene).
void PrivilegeShowDialog(int richStringId, int size, PrivilegePanelBHooks* h) {
    (void)richStringId;
    if (h && h->trace) {
        const char* bmp = PrivShowDialogBitmap(size);
        // Record the chosen backdrop as the form scene (string addr proxy).
        h->trace->lastFormScene = (bmp == nullptr) ? 0 : size; // 0/1/2 selection
    }
    int btn;
    while ((btn = NextBtn(h)) != kPrivLoopExit) {
        if (btn == kPrivLoopIdle) continue;
        // ShowDialog has no confirm side effect; the close ends the loop.
    }
}

// gilde.exe — SET-B leaf dispatcher (Rule 13).
int PrivilegeDispatchPanelB(int leafId, const PrivPerson* actor,
                            const PrivEvent* ev, PrivilegePanelBHooks* h) {
    switch (leafId) {
        case 0x561bb4: return static_cast<signed char>(PrivilegePanelEnactLaw(actor, ev, h));
        case 0x561fd0: return static_cast<signed char>(PrivilegeRemoveFromOffice(actor, ev, h));
        case 0x5628c8: return static_cast<signed char>(PrivilegePanelCounterEspionage(actor, h));
        case 0x562334: return static_cast<signed char>(PrivilegePanelEmbezzlement(actor, ev, h));
        case 0x562cdc: return static_cast<signed char>(PrivilegePanelSwapSeats(actor, ev, h));
        case 0x5651bc: return static_cast<signed char>(PrivilegePanelMiracle(actor, h));
        case 0x565f9c: return static_cast<signed char>(PrivilegePanelEvidenceReview(actor, ev, h));
        case 0x5667a0: return static_cast<signed char>(PrivilegePanelEvidenceReviewAlt(actor, ev, h));
        case 0x565b88: {
            // EvidenceDetails(a1, a2): resolve the evidence target from the event
            // and the self-target (v30==v31) flag, then run the detail dialog.
            const PrivPerson* target = (h && h->findRecord && ev)
                ? h->findRecord(ev->targetId, h->ctx) : nullptr;
            return static_cast<signed char>(PrivilegePanelEvidenceDetails(
                actor, target, target == actor, h));
        }
        case 0x571218: PrivilegeShowDialog(0, 0, h); return 0;
        default: return 0;
    }
}

} // namespace guild::world
