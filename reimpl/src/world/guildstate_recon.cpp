#include "world/guildstate_recon.h"

// Faithful 1:1 ports — see guildstate_recon.h for provenance and the cursor /
// access-hook model. Control flow, filter ordering, child-push points and the
// do/while continuation condition are translated verbatim from the Hex-Rays
// decompile; raw-pointer record reads become typed access-hook calls and the
// global cursor block becomes IterTreeCursor.

namespace guild::world {

// gilde.exe 0x5831f0 — VIBE_GameTime_Set (as inlined by InitGuardState).
//   v5 = a3;  *(DWORD*)(a1+10) = a3;  LOBYTE(v5) = a2;
//   *(WORD*)(a1+4) = v5;  *(DWORD*)(a1+6) = a4;
void GameTimeSet(GameTimeStamp& s, u8 hour, u8 sec, u8 min) {
    s.secDword = sec;     // *(DWORD*)(a1+10) = a3
    s.hour = hour;        // low byte of *(WORD*)(a1+4)
    s.sec  = sec;         // high byte of *(WORD*)(a1+4)
    s.min  = min;         // *(DWORD*)(a1+6) = a4
}

// gilde.exe 0x4520d0 — VIBE_GameLogic_InitGuardState.
int GameLogicInitGuardState(GuardState& g, bool aiDataFileLoaded) {
    g.counterA = 0;                       // dword_B56450 = 0
    g.stamp = GameTimeStamp{};            // dword_B56454 = 0 (block cleared)
    GameTimeSet(g.stamp, 6, 0, 0);        // VIBE_GameTime_Set(&dword_B56454,6,0,0)
    g.counterB = 0;                       // dword_B56464[0] = 0

    // result = VIBE_AiMethod_LoadDataFile(...);  if ( result ) { table...; return 1; }
    if (aiDataFileLoaded) {
        g.wFAE = 340;   // word_B56FAE = 340
        g.wFAC = 342;   // word_B56FAC = 342
        g.wFB0 = 344;   // word_B56FB0 = 344
        g.wFB2 = 370;   // word_B56FB2 = 370
        g.wFB4 = 366;   // word_B56FB4 = 366
        g.wFB6 = 0;     // word_B56FB6 = 0
        g.wFD0 = 0;     // word_B56FD0 = 0
        g.wFC8 = 372;   // word_B56FC8 = 372
        g.wFCA = 374;   // word_B56FCA = 374
        g.wFCC = 350;   // word_B56FCC = 350
        g.wFCE = 352;   // word_B56FCE = 352
        g.tableValid = true;
        return 1;
    }
    return 0;  // return result; (result == 0)
}

// Shared guard-state instance (mirror of the dword_B56xxx global block). The
// engine-init path writes through this so the reconstructed InitGuardState edge
// has a live target. Process-lifetime, exactly like the original globals.
GuardState& GuardStateGlobal() {
    static GuardState g;
    return g;
}

// gilde.exe 0x585488 — VIBE_GameObject_IterNextTree.
//
// Locals mapped to the decompile:
//   v0  -> cur node id      (edx; "v0" / record pointer)
//   v1  -> index            (ecx; dword_6498D0)
//   v2  -> count            (ebx; dword_6498C0)
//   v3  -> stackTop         (esi; dword_13CE274)
//   v13 -> matched          (the byte flag; 0 == not yet matched)
//   result <- v12           (the returned node id, kNull on exhaustion)
//
// The original threads a few raw reads off `v0` that are pointer-stride based:
//   *v0                         -> acc.protoWord(v0)
//   *(_DWORD*)(v0+20)           -> acc.childHead(v0)
//   *(__int16**)((char*)v0+63)  -> acc.sibling(v0)
//   *(_DWORD*)(v0+1) [word view -> +2 dword] -> acc.locField(v0)
//   *(char*)(typeTable + 65* *v0) -> acc.kindByte(protoWord)
//   (char*)v0 + 67              -> acc.nextLinear(v0)   (linear stride)
//
// NB: the original does raw pointer arithmetic in linear mode, where the node
// pointer is never null mid-scan, so its `while (v1<count && !*v0)` has no null
// test. Because we model nodes as opaque ids, the linear-mode loops here add a
// `v0 != kNull` guard before any record read; in a populated array nextLinear
// never yields kNull until the bound is reached, so the observable iteration is
// identical — the guard only keeps the opaque-id model from dereferencing past
// the end (the original relied on the v1<count bound for that).
i32 GameObjectIterNextTree(IterTreeCursor& c, const IterTreeRecordAccess& acc) {
    using K = IterTreeCursor;

    i32 v0 = c.cur;        // (__int16*)dword_6498E0
    i32 v1 = c.index;      // dword_6498D0
    i32 v2 = c.count;      // dword_6498C0
    i32 v3 = c.stackTop;   // dword_13CE274
    i32 v12 = K::kNull;
    i8  v13 = 0;
    // The original keeps all of these function-scope; hoisted so the gotos that
    // jump over them stay legal C++ (matches the decompile's flat local frame).
    i16 v6 = -1;
    i32 v8 = K::kNull, v11 = K::kNull;

    // if ( !dword_13CE27C ) { v12 = 0; goto LABEL_11; }
    // if ( !dword_6498E0 )  { v12 = 0; goto LABEL_11; }
    if (!c.queryReady || v0 == K::kNull) {
        v12 = K::kNull;
        goto LABEL_11;
    }

    if (c.firstStep) {                       // byte_6498CF
        c.firstStep = 0;                     // byte_6498CF = 0
        if (v0 != K::kNull) {                // dword_6498E0
            if (c.recurse) {                 // byte_6498D5
                i32 v5 = acc.childHead(v0);  // *(_DWORD*)(dword_6498E0+20)
                if (v5 != K::kNull) {
                    v3 = c.stackTop + 1;
                    c.stack[v3] = v5;        // dword_12356CC[++stackTop] = v5
                }
            }
        }
        if (acc.protoWord(v0))               // if ( *v0 )
            v1 = 1;
    } else {
        if (c.linearMode) {                  // byte_6498D4
            v0 = c.cur;                      // (v0 = dword_6498E0+67 view) -> re-fetch
            // v0 = node at +67; the original tested *(WORD*)(cur+67) and the
            // following while; with stride access we advance via nextLinear.
            v0 = acc.nextLinear(c.cur);
            if (v0 != K::kNull && acc.protoWord(v0))
                v1 = c.index + 1;            // dword_6498D0 + 1
            while (v1 < c.count && v0 != K::kNull && !acc.protoWord(v0)) {
                v0 = acc.nextLinear(v0);     // v0 += 67
            }
        }
        if (v1 >= c.count) {                 // if ( v1 >= dword_6498C0 )
            v12 = K::kNull;                  // LABEL_56
            goto LABEL_11;
        }
        if (!c.linearMode) {                 // if ( !byte_6498D4 )
            v0 = acc.sibling(v0);            // *(__int16**)((char*)v0+63)
            if (v0 != K::kNull) {
                if (c.recurse) {             // byte_6498D5
                    v8 = acc.childHead(v0);  // *((_DWORD*)v0+5) == +20
                    if (v8 != K::kNull) {
                        v3 = c.stackTop + 1;
                        c.stack[v3] = v8;
                    }
                }
            }
        }
        if (v0 == K::kNull) {
            // if ( !byte_6498D5 || !v3 ) goto LABEL_57;
            if (!c.recurse || !v3) {
                v12 = K::kNull;              // LABEL_57
                goto LABEL_11;
            }
            v0 = c.stack[v3--];             // dword_12356CC[v3--]
            i32 v9 = acc.childHead(v0);     // *((_DWORD*)v0+5)
            if (v9 != K::kNull)
                c.stack[++v3] = v9;
        }
    }

    // if ( v1 >= v2 ) { v12 = 0; goto LABEL_11; }   (LABEL_57)
    if (v1 >= v2) {
        v12 = K::kNull;
        goto LABEL_11;
    }

    do {
        if (c.matchAll)                      // byte_6498D6
            v13 = 1;
        if (c.filterProto != 0x7FFF) {       // word_6498C4
            // if ( *v0 != (filterProto) ) goto LABEL_58;
            if (acc.protoWord(v0) != c.filterProto)
                goto LABEL_58;
            v13 = 1;
        }
        if (c.filterLoc != -1) {             // dword_6498C8
            if (c.filterLoc != acc.locField(v0))  // *(_DWORD*)(v0+1) -> +2 dword
                goto LABEL_58;
            v13 = 1;
        }
        c.index = v1;                        // dword_6498D0 = v1
        c.count = v2;                        // dword_6498C0 = v2
        c.stackTop = v3;                     // dword_13CE274 = v3
        if (c.filterType != -1) {            // word_6498CC
            c.cur = v0;                      // dword_6498E0 = v0
            v6 = acc.resolveTypeField(v0);   // VIBE_GameObject_ResolveTypeFieldB
            v0 = c.cur;                      // v0 = dword_6498E0 (unchanged)
            if (v6 != c.filterType) {
                v3 = c.stackTop;
                v2 = c.count;
                v1 = c.index;
            LABEL_58:
                v13 = 0;
            LABEL_59:
                if (c.linearMode) {          // byte_6498D4
                    i32 next = acc.nextLinear(v0);  // v0 += 67
                    bool had = (v0 != K::kNull) && acc.protoWord(v0);
                    v0 = next;
                    if (had)
                        ++v1;
                    while (v1 < v2 && v0 != K::kNull && !acc.protoWord(v0))
                        v0 = acc.nextLinear(v0);
                }
                goto LABEL_67;
            }
            v13 = 1;
        }
        v3 = c.stackTop;                     // dword_13CE274
        v2 = c.count;                        // dword_6498C0
        v1 = c.index;                        // dword_6498D0
        if (c.filterKind != -1) {            // byte_6498CE
            // *(char*)(typeTable + 65 * *v0) == (u8)filterKind
            if ((i8)acc.kindByte(acc.protoWord(v0)) == c.filterKind) {
                v13 = 1;
                goto LABEL_34;
            }
            goto LABEL_58;
        }
        if (!v13)
            goto LABEL_59;
    LABEL_67:
        if (!v13 && !c.linearMode) {         // !v13 && !byte_6498D4
            v0 = acc.sibling(v0);            // *(__int16**)((char*)v0+63)
            if (v0 != K::kNull) {
                if (c.recurse) {             // byte_6498D5
                    v11 = acc.childHead(v0);  // *((_DWORD*)v0+5)
                    if (v11 != K::kNull)
                        c.stack[++v3] = v11;
                }
            }
        }
    LABEL_34:
        if (v0 == K::kNull) {
            // if ( !byte_6498D5 || !v3 ) goto LABEL_56;
            if (!c.recurse || !v3) {
                v12 = K::kNull;              // LABEL_56
                goto LABEL_11;
            }
            v0 = c.stack[v3--];             // dword_12356CC[v3--]
            i32 v7 = acc.childHead(v0);     // *((_DWORD*)v0+5)
            if (v7 != K::kNull)
                c.stack[++v3] = v7;
        }
    } while (!v13 && v1 <= v2 &&
             (v0 != K::kNull && acc.inRange(v0)));  // (u32)v0 < nodeBase+548864

    if (!v13)
        v0 = K::kNull;
    v12 = v0;

LABEL_11:
    c.stackTop = v3;   // dword_13CE274 = v3
    c.count = v2;      // dword_6498C0 = v2
    c.index = v1;      // dword_6498D0 = v1
    c.cur = v0;        // dword_6498E0 = v0
    return v12;
}

} // namespace guild::world
