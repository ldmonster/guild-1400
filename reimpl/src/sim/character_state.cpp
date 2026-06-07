// character_state — flag/state accessors + per-turn classification rules.
// Faithful 1:1 port of the gilde.exe Character predicate/flag functions; the two
// render leaves (pivot, visibility re-apply) are routed through CharStateHooks.
#include "sim/character_state.h"

#include "sim/character_query.h"   // LiveActor, g_live, g_activeUniverse

namespace guild::sim {

// Active turn-stride state (byte_63CC1D / dword_764CE0 / dword_764CF4 / dword_63CC20).
// Default: single-player, standalone (every record is local).
TurnState g_turn = { /*stride*/ 0, /*standalone*/ -1, /*myTurnSlot*/ 0, /*localTurnSlot*/ 0 };
void SetTurnState(const TurnState& ts) { g_turn = ts; }

// ---------------------------------------------------------------------------
// State hooks (inert defaults).
// ---------------------------------------------------------------------------
namespace {
const CharStateHooks* g_shooks = nullptr;
void DefApplyPivot(LiveActor*) {}
void DefApplyVisibility(LiveActor*) {}
const CharStateHooks g_sDefault = { DefApplyPivot, DefApplyVisibility };
} // namespace

void SetCharStateHooks(const CharStateHooks* h) { g_shooks = h; }
const CharStateHooks& GetCharStateHooks() { return g_shooks ? *g_shooks : g_sDefault; }

// ---------------------------------------------------------------------------
// The shared network turn-stride gate. The "*ForTurn" rules all end with the
// same idiom: standalone (-1) is always local; otherwise key % stride == my slot,
// where my slot is dword_764CF4 (networked) or dword_63CC20 (standalone-stride).
// With stride 0 (byte_63CC1D == 0) the gate is fully open.
// ---------------------------------------------------------------------------
namespace {
inline bool TypeActive(u8 t) { return t == 2 || t == 5 || t == 4 || t == 3; }
} // namespace

// gilde.exe 0x45263c — VIBE_Character_IsActiveType.
//   v1 = *(BYTE*)(a1+2); return v1==2 || v1==5 || v1==4 || v1==3;
bool IsActiveType(const TurnObject* o) {
    return TypeActive(o->type);
}

// gilde.exe 0x452660 — VIBE_Character_IsActiveTypeForTurn.
//   IsActiveType(a1) && (!stride
//                        || (standalone==-1 || id%stride==dword_764CF4)
//                           && (standalone!=-1 || id%stride==dword_63CC20));
bool IsActiveTypeForTurn(const TurnObject* o) {
    if (!TypeActive(o->type))
        return false;
    if (g_turn.stride == 0)
        return true;
    bool net = (static_cast<unsigned>(o->id) % g_turn.stride == static_cast<unsigned>(g_turn.myTurnSlot));
    bool loc = (static_cast<unsigned>(o->id) % g_turn.stride == static_cast<unsigned>(g_turn.localTurnSlot));
    bool a = (g_turn.standalone == -1) || net;
    bool b = (g_turn.standalone != -1) || loc;
    return a && b;
}

// gilde.exe 0x453228 — VIBE_Character_IsObjectForTurn.
//   return standalone==-1 || *(BYTE*)(a1+2)==6 || id%stride==dword_764CF4;
bool IsObjectForTurn(const TurnObject* o) {
    if (g_turn.standalone == -1)
        return true;
    if (o->type == 6)
        return true;
    if (g_turn.stride == 0)
        return false;  // standalone!=-1 with stride 0 cannot satisfy the modulo
    return static_cast<unsigned>(o->id) % g_turn.stride == static_cast<unsigned>(g_turn.myTurnSlot);
}

// gilde.exe 0x453260 — VIBE_Character_IsOwnerForTurn.
//   if (*(BYTE*)(a1+2) > 1) return 0;
//   key = a1; if (owner && owner+39 != 0xFFFF) key = &persons[268*owner.ownerPlayer];
//   return standalone==-1 || key.id % stride == dword_764CF4;
bool IsOwnerForTurn(const TurnObject* o) {
    if (o->type > 1)
        return false;
    int key = o->id;
    if (o->hasOwner && o->ownerPlayer != 0xFFFF)
        key = o->ownerId;   // owner person's id (resolved by caller)
    if (g_turn.standalone == -1)
        return true;
    if (g_turn.stride == 0)
        return false;
    return static_cast<unsigned>(key) % g_turn.stride == static_cast<unsigned>(g_turn.myTurnSlot);
}

// gilde.exe 0x4532d0 — VIBE_Character_IsAiControllableForTurn.
//   if ((type==1 || type==2) && isMaster && owner && owner+39!=0xFFFF) {
//     kind = persons[owner.ownerPlayer].kind;
//     if (kind != 7 && (standalone==-1 || kind==6 || ownerId%stride==dword_764CF4))
//       return 1;
//   }
//   return 0;
bool IsAiControllableForTurn(const TurnObject* o) {
    if (o->type != 1 && o->type != 2)
        return false;
    if (!o->isMaster)
        return false;
    if (!o->hasOwner || o->ownerPlayer == 0xFFFF)
        return false;
    int kind = o->ownerKindByte;
    if (kind == 7)
        return false;
    if (g_turn.standalone == -1 || kind == 6)
        return true;
    if (g_turn.stride == 0)
        return false;
    return static_cast<unsigned>(o->ownerId) % g_turn.stride == static_cast<unsigned>(g_turn.myTurnSlot);
}

// gilde.exe 0x43d9f4 — VIBE_Character_IsIdle. action head (+296) == 0.
bool IsIdle(const LiveActor* a) {
    return a && a->action == nullptr;
}

// gilde.exe 0x43ddd8 — VIBE_Character_IsSitting. (+140 & 0x10).
bool IsSitting(const LiveActor* a) {
    return a && (a->flagsA & kLaSitting) != 0;
}

// gilde.exe 0x40204c — VIBE_Character_ProcessFlaggedLocal.
//   for (i = 0; i != 512; ++i) {
//     r = live[i];
//     if (r && (*(BYTE*)(r+140) & 1) && off_649D64 == *(char**)(r+136))
//       SetPivotVector(*(DWORD*)(r+52), r+84);
//   }
int ProcessFlaggedLocal() {
    int processed = 0;
    for (int i = 0; i < kLiveCapacity; ++i) {
        LiveActor* a = g_live[i];
        if (a && (a->flagsA & kLaRedraw) != 0 && g_activeUniverse == a->universe) {
            GetCharStateHooks().applyPivot(a);
            ++processed;
        }
    }
    return processed;
}

// flt_6476FC @0x6476FC / flt_64770C @0x64770C — the seasonal work-window edges,
// recovered byte-exact via get_bytes. The tables are adjacent in memory
// (flt_64770C == flt_6476FC + 16), so flt_64770C entries 0..3 == flt_6476FC
// entries 4..7. Only season classes 0..3 carry meaningful window data
// (production / mining / search / training); the rest read as 0 padding.
//   flt_6476FC : 8, 7, 8, 9, 20, 21, 20, 19, 0, ...
//   flt_64770C : 20, 21, 20, 19, 0, 0, 0, 0, ...
const float kWorkSeasonMin[16] = {
    8.0f, 7.0f, 8.0f, 9.0f, 20.0f, 21.0f, 20.0f, 19.0f,
    0, 0, 0, 0, 0, 0, 0, 0,
};
const float kWorkSeasonMax[16] = {
    20.0f, 21.0f, 20.0f, 19.0f, 0.0f, 0.0f, 0.0f, 0.0f,
    0, 0, 0, 0, 0, 0, 0, 0,
};

// gilde.exe 0x4b9a5c (extracted rule) — the seasonal work-window gate.
//   v55 = (double)monthOfYear;
//   if (v55 < flt_64770C[season] && v55 >= flt_6476FC[season]) ... in-window
bool IsInWorkSeason(int seasonClass, float monthOfYear) {
    if (seasonClass < 0 || seasonClass >= 16)
        return false;
    double m = static_cast<double>(monthOfYear);
    return m < static_cast<double>(kWorkSeasonMax[seasonClass])
        && m >= static_cast<double>(kWorkSeasonMin[seasonClass]);
}

// gilde.exe 0x40208c — VIBE_Character_RefreshFlaggedLocal.
//   for (i = 0; i != 512; ++i) {
//     v1 = live[i];
//     if (v1 && (*(BYTE*)(v1+140)&1) && off_649D64 == *(char**)(v1+136)) {
//       v2 = *(DWORD*)(v1+296);
//       if (v2 && *(BYTE*)(v2+9)==45) { clear; continue; }
//       ApplyVisibilityState(v1, ...);   // re-show
//       clear flag;
//     }
//   }
int RefreshFlaggedLocal() {
    int cleared = 0;
    for (int i = 0; i < kLiveCapacity; ++i) {
        LiveActor* a = g_live[i];
        if (!a || (a->flagsA & kLaRedraw) == 0 || g_activeUniverse != a->universe)
            continue;
        bool midTalk = (a->action != nullptr) && (a->actionType == 45);
        if (!midTalk)
            GetCharStateHooks().applyVisibility(a);
        a->flagsA &= static_cast<u8>(~kLaRedraw);
        ++cleared;
    }
    return cleared;
}

} // namespace guild::sim
