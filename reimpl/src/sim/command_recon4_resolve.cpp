#include "sim/command_recon4_resolve.h"

#include <cstring>

namespace guild::sim {

// ===========================================================================
// Hooks plumbing (inert defaults) — mirrors the command_apply10 pattern.
// ===========================================================================
namespace {

bool  DefReadRecord(int, Recon4Record* out) { if (out) *out = Recon4Record{}; return false; }
void* DefFindRecordById(i32) { return nullptr; }
u8    DefRecordFlag(void*, int) { return 0; }
i32   DefRecordTypeWord(void*) { return 0; }
i32   DefComputeTotalWealth(u16) { return -1; }
int   DefIsNotInSelectionList(i32) { return 1; }
int   DefRandomModulo(u16) { return 0; }
void  DefParseTokens(const char*, int* tc, i32* v) { if (tc) *tc = 0; if (v) *v = 0; }
void  DefRenderMessage(char*, const char*, u16) {}

const Recon4ResolveHooks kDefaults = {
    &DefReadRecord, &DefFindRecordById, &DefRecordFlag, &DefRecordTypeWord,
    &DefComputeTotalWealth, &DefIsNotInSelectionList, &DefRandomModulo,
    &DefParseTokens, &DefRenderMessage,
};

Recon4ResolveHooks g_hooks = kDefaults;

} // namespace

void SetRecon4ResolveHooks(const Recon4ResolveHooks* h) {
    if (!h) { g_hooks = kDefaults; return; }
    g_hooks = *h;
    if (!g_hooks.readRecord)           g_hooks.readRecord = kDefaults.readRecord;
    if (!g_hooks.findRecordById)       g_hooks.findRecordById = kDefaults.findRecordById;
    if (!g_hooks.recordFlag)           g_hooks.recordFlag = kDefaults.recordFlag;
    if (!g_hooks.recordTypeWord)       g_hooks.recordTypeWord = kDefaults.recordTypeWord;
    if (!g_hooks.computeTotalWealth)   g_hooks.computeTotalWealth = kDefaults.computeTotalWealth;
    if (!g_hooks.isNotInSelectionList) g_hooks.isNotInSelectionList = kDefaults.isNotInSelectionList;
    if (!g_hooks.randomModulo)         g_hooks.randomModulo = kDefaults.randomModulo;
    if (!g_hooks.parseTokens)          g_hooks.parseTokens = kDefaults.parseTokens;
    if (!g_hooks.renderMessage)        g_hooks.renderMessage = kDefaults.renderMessage;
}
const Recon4ResolveHooks& GetRecon4ResolveHooks() { return g_hooks; }

// ===========================================================================
// Shared helpers (local; do NOT collide with object_scene_entity's globals).
// ===========================================================================
namespace {

inline Recon4Record rec(int i) {
    Recon4Record r;
    g_hooks.readRecord(i, &r);
    return r;
}

// VIBE_Entity_IsPersonType (0x4f8ee4): typeWord!=-1 && alive && kind<10 && kind!=6 && kind!=7.
inline bool isPerson(int i) {
    Recon4Record r = rec(i);
    if (r.typeWord == -1) return false;
    if (!r.alive) return false;
    return r.kind < 10 && r.kind != 6 && r.kind != 7;
}
// VIBE_Entity_IsCarriedType (0x4f8f2c): typeWord!=-1 && alive && (kind==6||kind==7).
inline bool isCarried(int i) {
    Recon4Record r = rec(i);
    if (r.typeWord == -1) return false;
    if (!r.alive) return false;
    return r.kind == 6 || r.kind == 7;
}
// VIBE_Person_IsValidActiveRecord (0x4f8e60): typeWord!=-1 && alive && kind<10.
inline bool isValidActive(int i) {
    Recon4Record r = rec(i);
    return r.typeWord != -1 && r.alive && r.kind < 10;
}

// param slot: a2 + 8*index + 4 read/written as a 32-bit id.
inline i32 readParamId(u8* params, int index) {
    i32 v; std::memcpy(&v, params + 8 * index + 4, 4); return v;
}
inline void writeParamId(u8* params, int index, i32 v) {
    std::memcpy(params + 8 * index + 4, &v, 4);
}

// The shared parse prologue: returns the clamped mode m in {0,1,2}.
//   if (tokenCount != 1 || (value = ParseInt) > 2) m = 1; else m = value;
inline u32 parseMode(const char* name) {
    int tc = 0; i32 val = 0;
    g_hooks.parseTokens(name, &tc, &val);
    if (tc != 1) return 1;
    if ((u32)val > 2u) return 1;
    return (u32)val;
}

// kind==1 confirm path used by the simple person resolvers: re-resolve the
// previously-chosen id, gate on +8 and a role byte, render its typeWord.
// Returns 1 on success (rendered), 0 otherwise. `roleOff` is the second flag
// offset (+358 / +361 / none).
inline int confirmPath(u8* params, int index, const char* name, char* out,
                       int roleOff, bool requireRole) {
    void* r = g_hooks.findRecordById(readParamId(params, index));
    if (!r) return 0;
    if (!g_hooks.recordFlag(r, 8)) return 0;
    if (requireRole && !g_hooks.recordFlag(r, roleOff)) return 0;
    g_hooks.renderMessage(out, name, (u16)g_hooks.recordTypeWord(r));
    return 1;
}

} // namespace

// ===========================================================================
// gilde.exe 0x4f9238 — VIBE_Command_ResolveTargetClergy
// Clergy resolver: rank byte prof79 in [0x1E,0x21]. mode 0 -> active people
// (alive && kind<10); mode 1 -> IsPersonType; mode 2 -> person-or-carried.
// ===========================================================================
int ResolveTargetClergy(u8 kind, u8* params, const char* name, int index, char* out) {
    u32 m = parseMode(name);

    if (kind == 1) {
        void* r = g_hooks.findRecordById(readParamId(params, index));
        if (r && g_hooks.recordFlag(r, 8) && g_hooks.recordFlag(r, 361)) {
            g_hooks.renderMessage(out, name, (u16)g_hooks.recordTypeWord(r));
            return 1;
        }
        return 0;
    }

    int chosen = -1;     // primary (v11): slot index of chosen record, -1 = none
    int fallback = -1;   // v19: carried-fallback for mode 2

    if (m == 0) {
        int count = 768;
        int i = (u16)g_hooks.randomModulo(0x300);
        while (true) {
            Recon4Record r = rec(i);
            if (r.typeWord != -1 && r.alive && r.kind < 10) {
                u8 rank = r.prof79;
                if (rank >= 0x1E && rank <= 0x21) { chosen = i; break; }
            }
            if (--count == 0) break;
            i = (i + 1) % 768;
        }
    } else if (m == 1) {
        int count = 768;
        int i = (u16)g_hooks.randomModulo(0x300);
        while (true) {
            if (isPerson(i)) {
                u8 rank = rec(i).prof79;
                if (rank >= 0x1E && rank <= 0x21) { chosen = i; break; }
            }
            if (--count == 0) break;
            i = (i + 1) % 768;
        }
    } else { // m == 2
        int count = 768;
        int i = (u16)g_hooks.randomModulo(0x300);
        // First arm: prefer a person; on a hit set fallback(v19) then fall into
        // the carried loop (LABEL_30 advances). Faithful to the goto structure.
        bool seekingPerson = true;
        while (true) {
            if (seekingPerson) {
                if (isPerson(i)) {
                    u8 rank = rec(i).prof79;
                    if (rank >= 0x1E && rank <= 0x21) {
                        fallback = i;            // v19 = &record
                    }
                }
                seekingPerson = false;           // fall through to carried test
            }
            if (isCarried(i)) {
                u8 rank = rec(i).prof79;
                if (rank >= 0x1E && rank <= 0x21) { chosen = i; break; }
            }
            if (--count == 0) break;             // LABEL_30: --v18
            i = (i + 1) % 768;
            if (fallback == -1) seekingPerson = true; // !v19 -> retry person arm
        }
        if (chosen == -1) chosen = fallback;     // LABEL_36
    }

    if (chosen != -1) {
        Recon4Record r = rec(chosen);
        if (kind == 0) writeParamId(params, index, r.entityId);
        g_hooks.renderMessage(out, name, (u16)r.typeWord);
        return 1;
    }
    return 0;
}

// ===========================================================================
// Person resolvers ByName / Alt / Scoped share the role-byte filter:
//   prof76 != 0 && prof76 != 15 && prof76 != 26 && prof76 != 21
// plus IsNotInSelectionList(entityId). ByName gates on flag9==1; Alt on flag9==0.
// mode 0 -> IsValidActiveRecord+flag9; mode 1 -> IsPersonType; mode 2 ->
// person-then-carried fallback. Confirm role offset +358.
// ===========================================================================
namespace {
inline bool roleOk(u8 v) { return v != 0 && v != 15 && v != 26 && v != 21; }
}

// 0x4f9518 — ByName (flag9 == 1)
int ResolveTargetPersonByName(u8 kind, u8* params, const char* name, int index, char* out) {
    u32 m = parseMode(name);
    if (kind == 1) return confirmPath(params, index, name, out, 358, true);

    int chosen = -1, fallback = -1;
    if (m == 0) {
        int count = 768; int i = (u16)g_hooks.randomModulo(0x300);
        while (true) {
            Recon4Record r = rec(i);
            if (r.flag9 == 1 && r.typeWord != -1 && r.alive && r.kind < 10
                && g_hooks.isNotInSelectionList(r.entityId) && roleOk(r.prof76)) {
                chosen = i; break;
            }
            if (--count == 0) break;
            i = (i + 1) % 768;
        }
    } else if (m == 1) {
        int count = 768; int i = (u16)g_hooks.randomModulo(0x300);
        while (true) {
            Recon4Record r = rec(i);
            if (r.flag9 == 1 && isPerson(i)
                && g_hooks.isNotInSelectionList(r.entityId) && roleOk(r.prof76)) {
                chosen = i; break;
            }
            if (--count == 0) break;
            i = (i + 1) % 768;
        }
    } else { // m == 2
        // 0x4f9722: the person arm (set fallback v19) is gated on v19==0 — once a
        // fallback is recorded the original NEVER re-enters it (the carried-arm
        // condition `v19 || ...` is then always true), so the fallback is the
        // FIRST qualifying person, not the last. Gate on fallback==-1, not a
        // sticky "seeking" flag.
        int count = 768; int i = (u16)g_hooks.randomModulo(0x300);
        while (true) {
            Recon4Record r = rec(i);
            if (r.flag9 == 1) {
                if (fallback == -1 && isPerson(i) && roleOk(r.prof76)) {
                    if (g_hooks.isNotInSelectionList(r.entityId)) fallback = i;
                } else if (isCarried(i) && g_hooks.isNotInSelectionList(r.entityId)
                           && roleOk(r.prof76)) {
                    chosen = i; if (chosen == -1) chosen = fallback;
                    break;
                }
            }
            if (--count == 0) { chosen = fallback; break; }
            i = (i + 1) % 768;
        }
    }
    if (chosen != -1) {
        Recon4Record r = rec(chosen);
        if (kind == 0) writeParamId(params, index, r.entityId);
        g_hooks.renderMessage(out, name, (u16)r.typeWord);
        return 1;
    }
    return 0;
}

// 0x4f989c — Alt (flag9 == 0). Same filter; confirm role offset +358.
int ResolveTargetPersonAlt(u8 kind, u8* params, const char* name, int index, char* out) {
    u32 m = parseMode(name);
    if (kind == 1) return confirmPath(params, index, name, out, 358, true);

    int chosen = -1, fallback = -1;
    if (m == 0) {
        int count = 768; int i = (u16)g_hooks.randomModulo(0x300);
        while (true) {
            Recon4Record r = rec(i);
            if (r.flag9 == 0 && r.typeWord != -1 && r.alive && r.kind < 10
                && roleOk(r.prof76) && g_hooks.isNotInSelectionList(r.entityId)) {
                chosen = i; break;
            }
            if (--count == 0) break;
            i = (i + 1) % 768;
        }
    } else if (m == 1) {
        int count = 768; int i = (u16)g_hooks.randomModulo(0x300);
        while (true) {
            Recon4Record r = rec(i);
            if (r.flag9 == 0 && isPerson(i) && roleOk(r.prof76)
                && g_hooks.isNotInSelectionList(r.entityId)) {
                chosen = i; break;
            }
            if (--count == 0) break;
            i = (i + 1) % 768;
        }
    } else { // m == 2
        // 0x4f9ab7: person-arm gate is v19==0 (fallback==-1), see ByName note.
        int count = 768; int i = (u16)g_hooks.randomModulo(0x300);
        while (true) {
            Recon4Record r = rec(i);
            if (r.flag9 == 0) {
                if (fallback == -1 && isPerson(i) && roleOk(r.prof76)) {
                    if (g_hooks.isNotInSelectionList(r.entityId)) fallback = i;
                } else if (isCarried(i) && roleOk(r.prof76)
                           && g_hooks.isNotInSelectionList(r.entityId)) {
                    chosen = i; if (chosen == -1) chosen = fallback;
                    break;
                }
            }
            if (--count == 0) { chosen = fallback; break; }
            i = (i + 1) % 768;
        }
    }
    if (chosen != -1) {
        Recon4Record r = rec(chosen);
        if (kind == 0) writeParamId(params, index, r.entityId);
        g_hooks.renderMessage(out, name, (u16)r.typeWord);
        return 1;
    }
    return 0;
}

// 0x4f9c20 — Scoped: kind==2 rejects; no flag9 filter; role filter only.
// mode 0 -> IsValidActiveRecord; mode 1 -> IsPersonType; mode 2 -> person/carried.
int ResolveTargetPersonScoped(u8 kind, u8* params, const char* name, int index, char* out) {
    u32 m = parseMode(name);
    if (kind == 2) return 0;
    if (kind == 1) return confirmPath(params, index, name, out, 358, true);

    int chosen = -1, fallback = -1;
    if (m == 0) {
        int count = 768; int i = (u16)g_hooks.randomModulo(0x300);
        while (true) {
            Recon4Record r = rec(i);
            if (r.typeWord != -1 && r.alive && r.kind < 10
                && roleOk(r.prof76) && g_hooks.isNotInSelectionList(r.entityId)) {
                chosen = i; break;
            }
            if (--count == 0) break;
            i = (i + 1) % 768;
        }
    } else if (m == 1) {
        int count = 768; int i = (u16)g_hooks.randomModulo(0x300);
        while (true) {
            Recon4Record r = rec(i);
            if (isPerson(i) && roleOk(r.prof76)
                && g_hooks.isNotInSelectionList(r.entityId)) {
                chosen = i; break;
            }
            if (--count == 0) break;
            i = (i + 1) % 768;
        }
    } else { // m == 2
        int count = 768; int i = (u16)g_hooks.randomModulo(0x300);
        bool seekingPerson = true;
        while (true) {
            Recon4Record r = rec(i);
            if (seekingPerson && isPerson(i) && roleOk(r.prof76)) {
                if (g_hooks.isNotInSelectionList(r.entityId)) fallback = i;
                seekingPerson = false;
            } else if (isCarried(i) && roleOk(r.prof76)
                       && g_hooks.isNotInSelectionList(r.entityId)) {
                chosen = i; break;
            }
            if (--count == 0) break;
            i = (i + 1) % 768;
            if (fallback != -1) seekingPerson = false; else seekingPerson = true;
        }
        if (chosen == -1) chosen = fallback;
    }
    if (chosen != -1) {
        Recon4Record r = rec(chosen);
        if (kind == 0) writeParamId(params, index, r.entityId);
        g_hooks.renderMessage(out, name, (u16)r.typeWord);
        return 1;
    }
    return 0;
}

// ===========================================================================
// 0x4f9f74 — BestRated: requires token count == 1 and value <= 2 (else 0).
// kind==2 rejects; kind==1 confirm gates on +8 only. kind==0 scans all 768
// active people, picks the one with max ComputeTotalWealth (init -10,000,000),
// optionally filtered by a "type group" derived from the parsed value.
// ===========================================================================
int ResolveTargetBestRated(u8 kind, u8* params, const char* name, int index, char* out) {
    int tc = 0; i32 val = 0;
    g_hooks.parseTokens(name, &tc, &val);
    if (tc != 1) return 0;
    if ((u32)val > 2u) return 0;
    // v25 / v24 / v14: the optional group filter. value 0 -> no filter (v25=0);
    // value!=0 -> v25=value, v24 = (value==value? 0 : value)==0 -> group key 0.
    // The decompile sets v24 = (v11==v12 ? 0 : v12). v12 is the parsed value, so
    // v11==v12 always true here -> v24==0; v25 carries the "filter active" flag.
    bool filterActive = (val != 0);

    if (kind == 2) return 0;

    if (kind == 1) {
        void* r = g_hooks.findRecordById(readParamId(params, index));
        if (!r) return 0;
        if (!g_hooks.recordFlag(r, 8)) return 0;
        g_hooks.renderMessage(out, name, (u16)g_hooks.recordTypeWord(r));
        return 1;
    }

    i32 best = -10000000;
    int chosen = -1;
    for (int i = 0; i < 768; ++i) {
        Recon4Record r = rec(i);
        if (r.typeWord != -1 && r.alive && r.kind < 10
            && (!filterActive || (r.entityId >> 24) == 0)) {
            i32 w = g_hooks.computeTotalWealth((u16)i);
            if (w > best && g_hooks.isNotInSelectionList(r.entityId)) {
                best = w; chosen = i;
            }
        }
    }
    if (chosen == -1) return 0;
    Recon4Record r = rec(chosen);
    if (kind == 0) writeParamId(params, index, r.entityId);
    g_hooks.renderMessage(out, name, (u16)r.typeWord);
    return 1;
}

// ===========================================================================
// 0x4fa50c — CraftWorker: profession prof74 in [13,18]. Same mode structure as
// the scoped person resolver, but with the profession-range predicate and a
// confirm path gated on +8 only (no role byte).
// ===========================================================================
int ResolveTargetCraftWorker(u8 kind, u8* params, const char* name, int index, char* out) {
    u32 m = parseMode(name);
    if (kind == 1) {
        void* r = g_hooks.findRecordById(readParamId(params, index));
        if (r && g_hooks.recordFlag(r, 8)) {
            g_hooks.renderMessage(out, name, (u16)g_hooks.recordTypeWord(r));
            return 1;
        }
        return 0;
    }

    auto profOk = [](u8 v) { return v >= 13 && v <= 18; };
    int chosen = -1, fallback = -1;
    if (m == 0) {
        int count = 768; int i = (u16)g_hooks.randomModulo(0x300);
        while (true) {
            Recon4Record r = rec(i);
            if (r.typeWord != -1 && r.alive && r.kind < 10
                && profOk(r.prof74) && g_hooks.isNotInSelectionList(r.entityId)) {
                chosen = i; break;
            }
            if (--count == 0) break;
            i = (i + 1) % 768;
        }
    } else if (m == 1) {
        int count = 768; int i = (u16)g_hooks.randomModulo(0x300);
        while (true) {
            Recon4Record r = rec(i);
            if (isPerson(i) && profOk(r.prof74)
                && g_hooks.isNotInSelectionList(r.entityId)) {
                chosen = i; break;
            }
            if (--count == 0) break;
            i = (i + 1) % 768;
        }
    } else { // m == 2
        int count = 768; int i = (u16)g_hooks.randomModulo(0x300);
        bool seekingPerson = true;
        while (true) {
            Recon4Record r = rec(i);
            if (seekingPerson && isPerson(i) && profOk(r.prof74)) {
                if (g_hooks.isNotInSelectionList(r.entityId)) fallback = i;
                seekingPerson = false;
            } else if (isCarried(i) && profOk(r.prof74)
                       && g_hooks.isNotInSelectionList(r.entityId)) {
                chosen = i; break;
            }
            if (--count == 0) break;
            i = (i + 1) % 768;
            if (fallback != -1) seekingPerson = false; else seekingPerson = true;
        }
        if (chosen == -1) chosen = fallback;
    }
    if (chosen != -1) {
        Recon4Record r = rec(chosen);
        if (kind == 0) writeParamId(params, index, r.entityId);
        g_hooks.renderMessage(out, name, (u16)r.typeWord);
        return 1;
    }
    return 0;
}

// ===========================================================================
// Top-5 wealth pool resolvers. Both maintain a parallel:
//   keys[5]  (dword_4F8BF0 / dword_4F8C10 = {0xFF676980 == -10000000} x5)
//   slots[5] (dword_4F8C04 / dword_4F8C24 = {0xFFFF x5, 0,0} -> i16 -1 x5)
// As each qualifying record's wealth exceeds keys[4], it replaces keys[4]/slots[4]
// then bubbles up (descending insertion sort over the 5-wide window). After the
// scan one of the 5 surviving slots is picked at random (RandomModulo(5)).
// kind==2 rejects; kind==1 confirm gates on +8.
// ===========================================================================
namespace {
// Faithful translation of the 0x4fa946..0x4fa9ab bubble: walk pairs from the top
// of the window (index 3) down; whenever the lower entry's key is larger, swap
// both the key array and the parallel i16 slot array. The decompile uses v19/v20
// running 3..0 over keys[] (5 entries: v25=keys[0], v26[0..2]=keys[1..3],
// v27=keys[4]); slots are i16 in v28[0..1]+v29.
void poolInsert(i32 keys[5], i16 slots[5], i32 newKey, i16 newSlot) {
    keys[4] = newKey;
    slots[4] = newSlot;
    for (int j = 3; j >= 0; --j) {
        if (keys[j + 1] > keys[j]) {
            i32 tk = keys[j]; keys[j] = keys[j + 1]; keys[j + 1] = tk;
            i16 ts = slots[j]; slots[j] = slots[j + 1]; slots[j + 1] = ts;
        }
    }
}
} // namespace

// 0x4fa818 — ByStatGroup. mode selects the predicate fn:
//   value 0           -> IsValidActiveRecord
//   value == parsed   -> IsPersonType (the v11==v12 arm AND the !v11 default)
//   else              -> IsCarriedType
int ResolveTargetByStatGroup(u8 kind, u8* params, const char* name, int index, char* out) {
    i32 keys[5]  = { -10000000, -10000000, -10000000, -10000000, -10000000 };
    i16 slots[5] = { -1, -1, -1, -1, -1 };

    if (kind == 2) return 0;

    int tc = 0; i32 val = 0;
    g_hooks.parseTokens(name, &tc, &val);
    int predSel; // 0 valid-active, 1 person, 2 carried
    if (tc != 1) predSel = 1;
    else if ((u32)val > 2u) predSel = 1;
    else if (val == 0) predSel = 0;
    else if (val == val) predSel = 1; // v11 == v12 arm
    else predSel = 2;                  // unreachable in practice; faithful branch

    if (kind == 1) {
        void* r = g_hooks.findRecordById(readParamId(params, index));
        if (!r) return 0;
        if (!g_hooks.recordFlag(r, 8)) return 0;
        g_hooks.renderMessage(out, name, (u16)g_hooks.recordTypeWord(r));
        return 1;
    }

    bool any = false;
    for (int i = 0; i < 768; ++i) {
        bool pass = (predSel == 0) ? isValidActive(i)
                  : (predSel == 1) ? isPerson(i)
                                   : isCarried(i);
        if (!pass) continue;
        i32 w = g_hooks.computeTotalWealth((u16)i);
        if (w > keys[4]) {
            Recon4Record r = rec(i);
            if (g_hooks.isNotInSelectionList(r.entityId)) {
                any = true;
                poolInsert(keys, slots, w, (i16)r.typeWord);
            }
        }
    }
    if (!any) return 0;
    int pick = (u16)g_hooks.randomModulo(5);
    i16 chosenWord = slots[pick];
    // The original recovers the record from word_12CE910[268 * slotWord]; the
    // back-write reads *(record+4). We expose that through the chosen typeWord's
    // record. Re-read its entity id for the writeback.
    // chosenWord here is the resolved typeWord; the engine indexes the table by
    // it to fetch the entity id (record+4). We model that re-read via readRecord
    // at the slot whose typeWord matches; but the table is keyed by record idx,
    // not typeWord. Faithfully, slots[] store the table *typeWord*; the writeback
    // is *(word_12CE910[268*typeWord] + 4). Expose via recordByTypeWord below.
    g_hooks.renderMessage(out, name, (u16)chosenWord);
    if (kind == 0) {
        // The original: *(params+8*idx+4) = *((u32*)&word_12CE910[268*slotWord]+1)
        // i.e. the entity id of the record selected by typeWord. We resolve it by
        // scanning for the record whose typeWord == chosenWord (the table is the
        // person array; typeWord is its identity index). Fallback: chosenWord.
        i32 idForWrite = chosenWord;
        for (int i = 0; i < 768; ++i) {
            Recon4Record r = rec(i);
            if (r.typeWord == chosenWord) { idForWrite = r.entityId; break; }
        }
        writeParamId(params, index, idForWrite);
    }
    return 1;
}

// 0x4fac80 — ByProfessionRange: profession prof74 in [19,69] excluding
// [31,33], [40,45], [52,57]; record valid (typeWord!=-1 && alive && kind<10).
// Same top-5 wealth pool / random pick. kind!=1 only path (no mode predicate fn;
// the original always uses the profession scan). kind==2 rejects.
int ResolveTargetByProfessionRange(u8 kind, u8* params, const char* name, int index, char* out) {
    i32 keys[5]  = { -10000000, -10000000, -10000000, -10000000, -10000000 };
    i16 slots[5] = { -1, -1, -1, -1, -1 };

    if (kind == 2) return 0;

    // StripNameTokens is called for its side effect (format string) but the value
    // is not used for predicate selection here.
    int tc = 0; i32 val = 0; (void)val; (void)tc;
    g_hooks.parseTokens(name, &tc, &val);

    if (kind == 1) {
        void* r = g_hooks.findRecordById(readParamId(params, index));
        if (!r) return 0;
        if (!g_hooks.recordFlag(r, 8)) return 0;
        g_hooks.renderMessage(out, name, (u16)g_hooks.recordTypeWord(r));
        return 1;
    }

    auto profOk = [](u8 v) {
        if (v < 19 || v > 69) return false;
        if (v >= 31 && v <= 33) return false;
        if (v >= 40 && v <= 45) return false;
        if (v >= 52 && v <= 57) return false;
        return true;
    };

    bool any = false;
    for (int i = 0; i < 768; ++i) {
        Recon4Record r = rec(i);
        if (!profOk(r.prof74)) continue;
        if (r.typeWord == -1 || !r.alive || r.kind >= 10) continue;
        i32 w = g_hooks.computeTotalWealth((u16)i);
        if (w > keys[4] && g_hooks.isNotInSelectionList(r.entityId)) {
            any = true;
            poolInsert(keys, slots, w, (i16)r.typeWord);
        }
    }
    if (!any) return 0;
    int pick = (u16)g_hooks.randomModulo(5);
    i16 chosenWord = slots[pick];
    g_hooks.renderMessage(out, name, (u16)chosenWord);
    if (kind == 0) {
        i32 idForWrite = chosenWord;
        for (int i = 0; i < 768; ++i) {
            Recon4Record r = rec(i);
            if (r.typeWord == chosenWord) { idForWrite = r.entityId; break; }
        }
        writeParamId(params, index, idForWrite);
    }
    return 1;
}

// ===========================================================================
// 0x4faab8 — WoundedPerson: scans for a record NOT in the selection list whose
// status byte (kind) == 0 (wounded), alive, typeWord!=-1, optionally filtered by
// the parsed group (entityId>>24 == group). Linear scan (no random start); picks
// the first match. kind==1 confirm gates on +8.
// ===========================================================================
int ResolveTargetWoundedPerson(u8 kind, u8* params, const char* name, int index, char* out) {
    int tc = 0; i32 val = 0;
    g_hooks.parseTokens(name, &tc, &val);
    if (tc != 1) return 0;
    if ((u32)val > 2u) return 0;

    // v13 / a5: group filter. value 0 -> v13=0 (inactive). value!=0 ->
    // v13 = value; a5 = (value==value ? 0 : value) -> 0.
    bool filterActive = (val != 0);
    int groupKey = 0;

    if (kind == 1) {
        void* r = g_hooks.findRecordById(readParamId(params, index));
        if (!r) return 0;
        if (!g_hooks.recordFlag(r, 8)) return 0;
        g_hooks.renderMessage(out, name, (u16)g_hooks.recordTypeWord(r));
        return 1;
    }

    int chosen = -1;
    for (int i = 0; i < 768; ++i) {
        Recon4Record r = rec(i);
        if (!g_hooks.isNotInSelectionList(r.entityId)) continue;
        // candidate established; apply group filter then the wounded predicate.
        if (filterActive && (r.entityId >> 24) != groupKey) continue;
        if (r.typeWord != -1 && r.alive && r.kind == 0) { chosen = i; break; }
    }
    if (chosen == -1) return 0;
    Recon4Record r = rec(chosen);
    if (kind == 0) writeParamId(params, index, r.entityId);
    g_hooks.renderMessage(out, name, (u16)r.typeWord);
    return 1;
}

// ===========================================================================
// 0x4faf54 — RandomCarried: kind==2 rejects. kind==1 confirm gates on +8.
// kind==0: repeatedly pick a random stride from {3,5,7,11} (dword_4F8C30) and a
// random start; walk the table by that stride looking for a carried item NOT in
// the selection list. The outer do/while retries while (i+1 < 768).
// ===========================================================================
int ResolveTargetRandomCarried(u8 kind, u8* params, const char* name, int index, char* out) {
    if (kind == 2) return 0;
    int tc = 0; i32 val = 0; (void)tc; (void)val;
    g_hooks.parseTokens(name, &tc, &val);

    if (kind == 1) {
        void* r = g_hooks.findRecordById(readParamId(params, index));
        if (!r) return 0;
        if (!g_hooks.recordFlag(r, 8)) return 0;
        g_hooks.renderMessage(out, name, (u16)g_hooks.recordTypeWord(r));
        return 1;
    }

    static const int kStride[4] = { 3, 5, 7, 11 }; // dword_4F8C30
    int chosen = -1;
    int i;
    do {
        int count = 768;
        int stride = kStride[(u16)g_hooks.randomModulo(4)];
        i = (u16)g_hooks.randomModulo(0x300);
        while (!isCarried(i) || !g_hooks.isNotInSelectionList(rec(i).entityId)) {
            if (--count == 0) goto next;
            i = (i + stride) % 768;
        }
        chosen = i;
    next:;
    } while (i + 1 < 768);

    if (chosen == -1) return 0;
    Recon4Record r = rec(chosen);
    if (kind == 0) writeParamId(params, index, r.entityId);
    g_hooks.renderMessage(out, name, (u16)r.typeWord);
    return 1;
}

} // namespace guild::sim
