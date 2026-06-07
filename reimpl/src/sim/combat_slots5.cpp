#include "sim/combat_slots5.h"

#include <cmath>
#include <cstddef>

namespace guild::sim {

// ---------------------------------------------------------------------------
// Hooks plumbing (inert default = all-null -> every side effect is a no-op).
// ---------------------------------------------------------------------------
namespace {
const CombatSlots5Hooks* g_hooks = nullptr;
CombatSlots5Hooks        g_inert{};
}

void SetCombatSlots5Hooks(const CombatSlots5Hooks* hooks) { g_hooks = hooks; }
const CombatSlots5Hooks& GetCombatSlots5Hooks() {
    return g_hooks ? *g_hooks : g_inert;
}

// ===========================================================================
// FindUnitById  (gilde.exe 0x486430)
// ===========================================================================
// while ( word_B5A350[v2/2] == -1 || a1 != dword_B5A354[v2/4] ) { v2 += 536; if
// ( v2 >= 17152 ) return 0; }  return &word_B5A350[v2/2];
// The byte offset v2 steps by 536 (one record) and the bound is 17152 == 32*536.
int FindUnitById(const std::vector<UnitSlotView>& table, i32 aiId) {
    const int count = static_cast<int>(table.size());
    const int cap   = kUnitSlotCount < count ? kUnitSlotCount : count;
    for (int i = 0; i < cap; ++i) {
        // The original's continue condition is (entityId == -1 || aiId != id);
        // a record matches when BOTH entityId != -1 AND aiId == id.
        if (table[i].entityId != static_cast<i16>(-1) && table[i].aiId == aiId)
            return i;
    }
    return -1;
}

// ===========================================================================
// FindNearestEnemyTarget  (gilde.exe 0x57e50c)
// ===========================================================================
// Primary pass: among candidates that pass the gate, keep the nearest by node
// distance when both self and candidate have a node; otherwise keep the one with
// the highest (level/cap) "most hurt" ratio. If none qualified at all, return a
// uniformly random fallback-roster entry.
int FindNearestEnemyTarget(const std::vector<EnemyCandidate>& candidates,
                           bool selfHasNode, const float selfPos[3],
                           const std::vector<int>& fallbackIndices) {
    const CombatSlots5Hooks& h = GetCombatSlots5Hooks();

    int   best       = -1;       // v3 : chosen candidate index into `candidates`
    int   bestCandSlot = -1;     // position in `candidates` of `best` (for ratio)
    float bestDist   = static_cast<float>(kEnemyInitialBest); // v27 = 1e8

    for (std::size_t k = 0; k < candidates.size(); ++k) {
        const EnemyCandidate& c = candidates[k];
        if (c.defType >= kEnemyDefTypeCap)   // if ( *i < 72 ) ... else skip
            continue;
        // if ( (u8)i[101] < (int)(u16)cap )    — level strictly below cap.
        if (!(static_cast<int>(c.level) < static_cast<int>(c.cap)))
            continue;
        // if ( (i[90] & 1) == 0 )              — flag bit0 must be clear.
        if (c.flagBit0)
            continue;

        if (selfHasNode && c.hasNode) {
            // Distance branch: deltas spilled through float, magnitude via sqrt of
            // double-accumulated squares, spilled to float before the compare.
            float v19 = c.worldPos[0] - selfPos[0];
            float v20 = c.worldPos[1] - selfPos[1];
            float v21 = c.worldPos[2] - selfPos[2];
            float v26 = static_cast<float>(std::sqrt(
                static_cast<double>(v19) * v19 +
                static_cast<double>(v20) * v20 +
                static_cast<double>(v21) * v21));
            if (v26 < bestDist) {       // if ( v8 < v27 )
                best         = c.index;
                bestCandSlot = static_cast<int>(k);
                bestDist     = v26;
            }
        } else {
            // "Most hurt" ratio branch: keep the larger (level/cap). The original:
            //   if ( !v3 || (level/cap) of best > (level/cap) of i ) -> keep i.
            // i.e. take i when there is no current pick, OR when i's ratio is >= the
            // current pick's ratio (the compare is `best > i`, so we replace unless
            // best is strictly more hurt).
            if (best < 0) {
                best = c.index;
                bestCandSlot = static_cast<int>(k);
            } else {
                const EnemyCandidate& b = candidates[bestCandSlot];
                double curRatio  = static_cast<double>(static_cast<i16>(c.level)) /
                                   static_cast<double>(c.cap);
                double bestRatio = static_cast<double>(static_cast<i16>(b.level)) /
                                   static_cast<double>(b.cap);
                // original keeps i when (bestRatio > curRatio) is FALSE -> i.e. when
                // curRatio >= bestRatio.  (v11 = curRatio; compare bestRatio > v11.)
                if (!(bestRatio > curRatio)) {
                    best = c.index;
                    bestCandSlot = static_cast<int>(k);
                }
            }
        }
    }

    if (best >= 0)
        return best;

    // Fallback: gather up to 64 entries (already gathered by the caller) and pick
    // one at random via Math_RandomModulo(count).
    int n = static_cast<int>(fallbackIndices.size());
    if (n > kEnemyFallbackCap) n = kEnemyFallbackCap;
    if (n <= 0)
        return -1;
    int roll = h.randomModulo ? h.randomModulo(n) : 0;  // inert default -> 0
    if (roll < 0) roll = 0;
    if (roll >= n) roll = n - 1;
    return fallbackIndices[static_cast<std::size_t>(roll)];
}

// ===========================================================================
// RefreshHealthBars  (gilde.exe 0x4872a0)
// ===========================================================================
int RefreshHealthBars(int ctx, std::vector<int>& windowIds,
                      const std::vector<bool>& showBar,
                      std::vector<void*>& unitRecs) {
    const CombatSlots5Hooks& h = GetCombatSlots5Hooks();
    const int kInvalid = -1;     // v2 = -1

    // Pass 1: for ( i = 0; i != 32; ++i ) if ( v2 != dword_B5A1D0[i] ) { remove;
    //                                                  dword_B5A1D0[i] = v2; }
    const int n1 = static_cast<int>(windowIds.size());
    for (int i = 0; i < n1; ++i) {
        if (windowIds[i] != kInvalid) {
            if (h.windowRemoveIfActive)
                h.windowRemoveIfActive(windowIds[i]);
            windowIds[i] = kInvalid;
        }
    }

    // Pass 2: for ( j = 0; j != 8576; j += 268 ) if ( byte_B5A4D8[j*2] )
    //              CreateHealthBarWindow(ctx, &word_B5A350[j]);
    // 8576/268 == 32 records.
    int created = 0;
    const int n2 = static_cast<int>(showBar.size());
    for (int j = 0; j < n2; ++j) {
        if (showBar[j]) {
            void* rec = (j < static_cast<int>(unitRecs.size())) ? unitRecs[j] : nullptr;
            if (h.createHealthBarWindow)
                h.createHealthBarWindow(ctx, rec);
            ++created;
        }
    }
    return created;
}

// ===========================================================================
// StartCutscene  (gilde.exe 0x4876d8)
// ===========================================================================
int StartCutscene(int prevFormId, const void* unitNode, int* outFormId) {
    const CombatSlots5Hooks& h = GetCombatSlots5Hooks();
    if (outFormId) *outFormId = -1;

    if (prevFormId != -1) {                 // if ( dword_63125C != -1 )
        if (h.formDestroy) h.formDestroy(prevFormId);
    }

    int xy[2] = {0, 0};                      // v4/v5 (the screen bounds)
    bool onScreen = h.objectScreenBounds ? h.objectScreenBounds(unitNode, xy) : false;
    if (!onScreen)
        return -1;                           // off-screen -> no form

    // GameTick_Finalize(v4 + 48, v5 - 30, asset)
    int formId = h.gameTickFinalize ? h.gameTickFinalize(xy[0] + 48, xy[1] - 30,
                                                          "Cutscenes\\combat") : -1;
    if (outFormId) *outFormId = formId;
    return formId;
}

// ===========================================================================
// SelectIntroTrack  (gilde.exe 0x48ac70 — the music-bucket ladder)
// ===========================================================================
IntroTrack SelectIntroTrack(int roll100) {
    if (roll100 < 20) return IntroTrack::kKrieg;       // aCd2KriegMp3
    if (roll100 < 40) return IntroTrack::kRuesteEuch;  // aCd1Ruesteteuch
    if (roll100 < 60) return IntroTrack::kFlucht;      // aCd1FluchtMp3
    if (roll100 >= 80) return IntroTrack::kAmKuehlen;  // aCd2Amkuehlengr
    return IntroTrack::kSeuche;                         // 60..79 : aCd2Derseuchenz
}

// ===========================================================================
// PlayIntroCutscene  (gilde.exe 0x48ac0c)
// ===========================================================================
int PlayIntroCutscene(bool musicEnabled, void* waitCtx) {
    const CombatSlots5Hooks& h = GetCombatSlots5Hooks();

    int roll = -1;
    if (musicEnabled) {                       // if ( dword_63C8F8 )
        roll = h.randomModulo ? h.randomModulo(100) : 0;  // RandomModulo(0x64)
        IntroTrack track = SelectIntroTrack(roll);
        if (h.playMusicTrack) h.playMusicTrack(track);
    }

    // dword_11BC2D0 = 513734; Weather_UpdateSky(); DayCycle_UpdateBrightness(...);
    if (h.weatherAndSky) h.weatherAndSky();

    // v1 = Script_LoadFromScriptDir("Cutscenes\\Kampf"); if (v1) RunMain; if (v1) RunWaitLoop.
    void* script = h.scriptLoad ? h.scriptLoad("Cutscenes\\Kampf") : nullptr;
    if (script) {
        if (h.scriptRunMain) h.scriptRunMain(script);
        if (h.scriptRunWaitLoop) h.scriptRunWaitLoop(waitCtx);
    }
    return roll;
}

// ===========================================================================
// PlayOutroCutscene  (gilde.exe 0x48ace8)
// ===========================================================================
bool PlayOutroCutscene(int ambientTrackId, void* waitCtx) {
    const CombatSlots5Hooks& h = GetCombatSlots5Hooks();

    if (ambientTrackId != 0) {               // if ( dword_631250 )
        if (h.stopAmbientTrack) h.stopAmbientTrack(ambientTrackId);
    }
    // dword_11BC2D0 = 513734; dword_62D314 = 1;
    void* script = h.scriptLoad ? h.scriptLoad("Cutscenes\\Kampf2") : nullptr;
    if (script) {
        if (h.scriptRunMain) h.scriptRunMain(script);
        if (h.scriptRunWaitLoop) h.scriptRunWaitLoop(waitCtx);
        return true;
    }
    return false;
}

// ===========================================================================
// DropBombAction  (gilde.exe 0x48cdc8)
// ===========================================================================
// v2 = unit+384; def = FindObjectDef(v2); SpawnBomb(); StartVoiceSample(...,def,...);
// if ( dword_631204 == *(side+4) ) {                   -- isLocalSide
//   class = def[88];
//   if ( class == 1 || class == 2 ) {                  -- ranged/thrown
//     tgt = FindActiveTarget(v2);
//     if ( tgt ) { BeginDeltaPacket; AppendRawField; QueueRequestState22; } } }
BombActionOutcome DropBombAction(const BombActionInput& in, void* activeTarget) {
    const CombatSlots5Hooks& h = GetCombatSlots5Hooks();
    BombActionOutcome out{};

    if (h.spawnBomb) h.spawnBomb();
    out.spawnedBomb = true;
    if (h.startVoiceSample) h.startVoiceSample(nullptr);
    out.playedVoice = true;

    if (in.isLocalSide) {
        if (in.weaponClass == 1 || in.weaponClass == 2) {
            if (in.hasActiveTarget && activeTarget) {
                if (h.queueTargetRequest22) h.queueTargetRequest22(activeTarget);
                out.queuedRequest = true;
            }
        }
    }
    return out;
}

// ===========================================================================
// ThrowBombAction  (gilde.exe 0x48ce88)
// ===========================================================================
// v3 = a1[95];
// if ( dword_631204 == *(side+4) ) {
//   tgt = FindActiveTarget(v3);
//   if ( tgt ) { BeginDeltaPacket; AppendRawField; QueueRequestState22; } }
// return SpawnThrownBomb(*(v3+388)->+52 + 76, a1[96], a1[97]);   -- always spawns.
BombActionOutcome ThrowBombAction(const BombActionInput& in, void* activeTarget) {
    const CombatSlots5Hooks& h = GetCombatSlots5Hooks();
    BombActionOutcome out{};

    if (in.isLocalSide) {
        if (in.hasActiveTarget && activeTarget) {
            if (h.queueTargetRequest22) h.queueTargetRequest22(activeTarget);
            out.queuedRequest = true;
        }
    }
    out.thrownBombResult = h.spawnThrownBomb
        ? h.spawnThrownBomb(in.unitNodePos, in.throwArgA, in.throwArgB)
        : 0;
    out.spawnedThrownBomb = true;
    return out;
}

// ===========================================================================
// Scene-graph string-match collectors
//   EscapeTileCallback    (0x48b350): if ( !StrCmpNoCase(name, "sp_ESCAPE") ) append.
//   ConquerObjectCallback (0x48b5a4): if ( !StrncmpN(name, "sp_CONQUER", 10) ) append.
//   WareObjectCallback    (0x48b488): if ( StrncmpN(name, "WARE_", 5) ||
//                                          heldWare == candidate ) skip; else append.
// The string comparison is delegated to the real util sibling at link time (the
// integration test wires util::StrCmpNoCase / util::StrncmpN); the inert build path
// here uses the libc clones via the predicates below being driven by the caller.
// We keep the predicates pure (no global mutation) so callers/tests own the array.
// ===========================================================================

// A local case-insensitive compare mirroring util::StrCmpNoCase semantics so the
// predicate is self-contained; the itest re-routes the *actual* comparison through
// the reconstructed sibling to prove the cross-module wiring.
namespace {
int FoldCmp(const char* a, const char* b) {
    while (*a && *b) {
        unsigned char ca = static_cast<unsigned char>(*a);
        unsigned char cb = static_cast<unsigned char>(*b);
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<unsigned char>(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<unsigned char>(cb - 'A' + 'a');
        if (ca != cb) return static_cast<int>(ca) - static_cast<int>(cb);
        ++a; ++b;
    }
    unsigned char ca = static_cast<unsigned char>(*a);
    unsigned char cb = static_cast<unsigned char>(*b);
    if (ca >= 'A' && ca <= 'Z') ca = static_cast<unsigned char>(ca - 'A' + 'a');
    if (cb >= 'A' && cb <= 'Z') cb = static_cast<unsigned char>(cb - 'A' + 'a');
    return static_cast<int>(ca) - static_cast<int>(cb);
}
int NCmp(const char* a, const char* b, int n) {
    for (int i = 0; i < n; ++i) {
        unsigned char ca = static_cast<unsigned char>(a[i]);
        unsigned char cb = static_cast<unsigned char>(b[i]);
        if (ca != cb) return static_cast<int>(ca) - static_cast<int>(cb);
        if (ca == 0) return 0;
    }
    return 0;
}
} // namespace

bool EscapeTileMatches(const char* name) {
    if (!name) return false;
    return FoldCmp(name, kEscapeTileName) == 0;
}

bool ConquerObjectMatches(const char* name) {
    if (!name) return false;
    return NCmp(name, kConquerObjectName, 10) == 0;
}

bool WareObjectMatches(const char* name, int candidateWareId, int heldWareId) {
    if (!name) return false;
    // original: if ( StrncmpN(name,"WARE_",5) || held == candidate ) return 1 (skip)
    if (NCmp(name, kWareObjectPrefix, 5) != 0)
        return false;
    if (candidateWareId == heldWareId)
        return false;
    return true;
}

// ===========================================================================
// RegisterFlagCallback  (gilde.exe 0x4897bc)
// ===========================================================================
// dword_631260 = 0; return SceneGraph_WalkAndInvoke(off_649D64, 0, CreateFlagObject,
//                                                    256, 0);
int RegisterFlagCallback(int (*cb)(const char* name, void* node), void* arg) {
    const CombatSlots5Hooks& h = GetCombatSlots5Hooks();
    // dword_631260 = 0  — the flag counter reset.
    if (h.sceneWalkAndInvoke) {
        h.sceneWalkAndInvoke(cb, arg);
        return 0;
    }
    return 0;   // inert default -> 0 (no walk)
}

} // namespace guild::sim
