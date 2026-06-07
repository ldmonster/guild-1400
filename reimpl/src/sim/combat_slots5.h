#pragma once
// gilde.exe — Combat batch-5 LEAVES: the remaining untranslated VIBE_Combat_*
// functions the earlier four batches left as scene / command / audio / cutscene
// glue. Each original here is deterministic CONTROL FLOW over a passed-in record
// or a recovered global table, with its cross-module side effects (command queue,
// audio, script, music, scene-graph, window) routed through CombatSlots5Hooks
// (inert default = no-op). Namespace guild::sim, MODULE combat, prefix
// VIBE_Combat_*.
//
// SCOPE — functions reconstructed 1:1 in this module (confirmed UNTRANSLATED, by
// both address provenance AND bare-name definition search across all combat*.cpp):
//   * FindUnitById          (0x486430) — id -> battle-slot record (table scan).
//   * FindNearestEnemyTarget(0x57e50c) — bone-chain distance min + RNG fallback.
//   * RefreshHealthBars      (0x4872a0) — tear down + rebuild per-unit HP windows.
//   * StartCutscene          (0x4876d8) — destroy old form, screen-bound, finalize.
//   * PlayIntroCutscene      (0x48ac0c) — RandomModulo(100) music-track bucket.
//   * PlayOutroCutscene      (0x48ace8) — stop ambient + run outro script.
//   * DropBombAction         (0x48cdc8) — weapon-class gate -> queue request-22.
//   * ThrowBombAction        (0x48ce88) — active-target gate -> queue request-22.
//   * EscapeTileCallback     (0x48b350) — StrCmpNoCase("sp_ESCAPE") collector.
//   * ConquerObjectCallback  (0x48b5a4) — StrncmpN("sp_CONQUER",10) collector.
//   * WareObjectCallback     (0x48b488) — StrncmpN("WARE_",5) ware-id collector.
//   * RegisterFlagCallback   (0x4897bc) — reset counter + scene-walk register.
//
// x87 NOTE. FindNearestEnemyTarget computes its candidate distance in the x87 FPU:
// the three component deltas are stored back through 32-bit `float` slots
// (`v19/v20/v21`), and the squared-sum is `sqrt`ed at 80-bit then spilled to a
// `float` (`v26`) before the `< bestDist` compare; the "more hurt" tie-break is a
// `double` ratio of two `unsigned __int16`s. We mirror that exactly: deltas truncate
// through `float`, the magnitude is computed as `(float)sqrt((double)...)`, and the
// ratio compare is done in `double`. A host that keeps the intermediate in 80-bit
// register precision (no -ffloat-store) would round identically for the magnitudes
// we test; the explicit `float` round-trips remove the only place double-rounding
// could leak in.
#include "guild/common/types.h"

#include <vector>

namespace guild::sim {

// ===========================================================================
// Recovered table geometry / constants
// ===========================================================================
// The battle-slot table (word_B5A350..): 768 person records, stride 536 bytes
// (== 268 u16 == 134 u32). FindUnitById scans:
//   +0  word_B5A350[268*i] : the slot's entity id (-1 == empty)
//   +4  dword_B5A354[134*i] : the unit's combat-AI id (the search key)
// The original walks a byte offset by +536 up to 17152 (== 32 * 536); only the
// first 32 slots are unit slots (the health-bar / order tables are 32 wide).
constexpr int kUnitSlotStride = 536;   // bytes per record
constexpr int kUnitSlotCount  = 32;    // 17152 / 536 — the unit-slot window
constexpr int kBattleSlotCount = 768;  // full person table (used by FindNearest)

// FindNearestEnemyTarget:
//   * Person_QueryBegin(seed,1,5,1) primary roster; (seed,1,5,22) fallback roster.
//   * a candidate counts only if def-type < 72, its "level" byte (+101) is below
//     the def's cap (word_641DB0[4*type] low word), and its +90 flags bit0 is clear.
//   * Among reachable candidates (both self & cand have a node, +97) it keeps the
//     nearest by bone-chain world distance; with no reachable candidate it keeps the
//     "most hurt relative to cap" one ((level/cap) maximised).
//   * If none matched, it gathers up to 64 fallback-roster entries and returns a
//     uniformly random one (Math_RandomModulo(count)).
constexpr int kEnemyDefTypeCap   = 72;     // *iter < 72
constexpr int kEnemyFallbackCap  = 64;     // v18[16] dword buffer, byte index < 64
constexpr double kEnemyInitialBest = 100000000.0; // v27 = 1e8 sentinel

// PlayIntroCutscene music-track buckets. The original rolls RandomModulo(100) and
// selects a CD track by range. We expose the bucket selector; the actual track
// strings live at the recovered .rdata addresses noted per enum.
enum class IntroTrack {
    kKrieg,        // roll <  20 : aCd2KriegMp3      @0x61b7c8
    kRuesteEuch,   // roll <  40 : aCd1Ruesteteuch   @0x61b7d8
    kFlucht,       // roll <  60 : aCd1FluchtMp3     @0x61b7ec
    kSeuche,       // 60..79     : aCd2Derseuchenz   @0x61b7fc
    kAmKuehlen,    // roll >= 80 : aCd2Amkuehlengr   @0x61b814
};
// gilde.exe 0x48ac70 — the bucket ladder. NB the 60..79 (else) and >=80 cases are
// ordered exactly as the original's if/else chain (`< 20`, `< 40`, `< 60`, `>= 80`,
// else). Pure function of the roll.
IntroTrack SelectIntroTrack(int roll100);

// ===========================================================================
// Leaf hooks — the cross-module side effects these bodies make. Tests install a
// recording mock; the inert default (all null) makes every leaf a no-op.
// ===========================================================================
struct CombatSlots5Hooks {
    // --- FindNearestEnemyTarget -------------------------------------------------
    // Person roster iterator. queryBegin returns the first record (or null); the
    // primary pass uses `mode==1`, the fallback pass `mode==22`. iterNext advances.
    void* (*personQueryBegin)(int seed, int mode) = nullptr;
    void* (*personIterNext)() = nullptr;
    // World position of a unit's node, via the bone-chain transform (writes x/y/z).
    // Returns false if the unit has no node (the +97 pointer is null).
    bool  (*unitWorldPos)(const void* unit, float* outXYZ) = nullptr;
    // Uniform RNG over [0,n) for the fallback pick (Math_RandomModulo).
    int   (*randomModulo)(int n) = nullptr;

    // --- RefreshHealthBars ------------------------------------------------------
    void (*windowRemoveIfActive)(int windowId) = nullptr;       // VIBE_Window_RemoveIfActive
    int  (*createHealthBarWindow)(int ctx, void* unitRec) = nullptr; // VIBE_Combat_CreateHealthBarWindow

    // --- cutscene leaves --------------------------------------------------------
    void (*formDestroy)(int formId) = nullptr;                  // VIBE_Form_Destroy
    bool (*objectScreenBounds)(const void* node, int* outXY) = nullptr; // ComputeScreenBounds
    int  (*gameTickFinalize)(int x, int y, const char* asset) = nullptr; // GameTick_Finalize -> formId
    void (*playMusicTrack)(IntroTrack track) = nullptr;         // VIBE_Music_PlayCutsceneTrack
    void (*stopAmbientTrack)(int trackId) = nullptr;            // VIBE_Audio_StopAmbientTrack
    void* (*scriptLoad)(const char* path) = nullptr;            // VIBE_Script_LoadFromScriptDir
    void (*scriptRunMain)(void* script) = nullptr;              // VIBE_Script_RunMain
    void (*scriptRunWaitLoop)(void* ctx) = nullptr;             // VIBE_Script_RunWaitLoop
    void (*weatherAndSky)() = nullptr;                          // Weather/DayCycle update

    // --- bomb actions -----------------------------------------------------------
    void (*spawnBomb)() = nullptr;                              // VIBE_Object_SpawnBomb
    int  (*spawnThrownBomb)(int nodePos, int a, int b) = nullptr; // VIBE_Object_SpawnThrownBomb
    void (*startVoiceSample)(const void* objDef) = nullptr;     // VIBE_Audio_StartVoiceSample
    // Begin a delta packet against the active target and queue a request-22.
    void (*queueTargetRequest22)(void* target) = nullptr;

    // --- scene-walk callbacks register ------------------------------------------
    void (*sceneWalkAndInvoke)(int (*cb)(const char*, void*), void* arg) = nullptr;
};

void SetCombatSlots5Hooks(const CombatSlots5Hooks* hooks);
const CombatSlots5Hooks& GetCombatSlots5Hooks();

// ===========================================================================
// FindUnitById  (gilde.exe 0x486430)
// ===========================================================================
// Linear scan of the unit-slot table for the record whose id field (+4) equals
// `aiId` and whose entity-id field (+0) is not -1. `table` points at the first
// record; `idAt`/`entAt` read the two fields. Returns the matching record index
// (0..31) or -1 if none. (The original returns &word_B5A350[268*i]; we return the
// index so the caller can address the record in any layout.)
struct UnitSlotView {
    // entity id at +0 (u16, -1 == empty) and the AI id at +4 (i32, the search key).
    i16 entityId = -1;
    i32 aiId = 0;
};
int FindUnitById(const std::vector<UnitSlotView>& table, i32 aiId);

// ===========================================================================
// FindNearestEnemyTarget  (gilde.exe 0x57e50c)
// ===========================================================================
// A candidate enemy record for the primary pass. `defType` (+0) gates the scan
// (must be < 72); `level` (+101) is compared against `cap` (word_641DB0[4*type]);
// `flagBit0` (+90 & 1) excludes the record; `hasNode` says whether the bone-chain
// position is resolvable. `worldPos` is the node position (only meaningful when
// hasNode). `index` is the caller's stable id for tie reporting.
struct EnemyCandidate {
    u8    defType  = 0;
    u8    level    = 0;
    u16   cap      = 0;       // word_641DB0[4*defType] low word
    bool  flagBit0 = false;   // (+90 & 1) != 0  -> skip
    bool  hasNode  = false;   // +97 pointer non-null
    float worldPos[3] = {0,0,0};
    int   index    = -1;
};
// `selfHasNode`/`selfPos` describe the searcher (v28); when either side lacks a node
// the "most hurt" ratio branch is used instead of distance. `fallback` is the
// fallback roster (used only when no primary candidate qualifies); the function
// returns one uniformly at random (via the randomModulo hook). Returns the chosen
// candidate index, or -1 if nothing at all qualified.
int FindNearestEnemyTarget(const std::vector<EnemyCandidate>& candidates,
                           bool selfHasNode, const float selfPos[3],
                           const std::vector<int>& fallbackIndices);

// ===========================================================================
// RefreshHealthBars  (gilde.exe 0x4872a0)
// ===========================================================================
// Two passes over the 32-wide window-id table dword_B5A1D0 and the 32-wide unit
// table: first remove every still-active health-bar window (id != -1) and reset its
// cell to -1; then for each unit slot whose "show health bar" byte (byte_B5A4D8) is
// set, (re)create its health-bar window. `windowIds` is mutated in place (set to -1).
// Returns the number of windows (re)created. Side effects via the hooks.
int RefreshHealthBars(int ctx, std::vector<int>& windowIds,
                      const std::vector<bool>& showBar,
                      std::vector<void*>& unitRecs);

// ===========================================================================
// StartCutscene  (gilde.exe 0x4876d8)
// ===========================================================================
// Destroys the previously-open cutscene form (if `prevFormId != -1`), then projects
// the unit's node to screen; on success it finalizes a cutscene form at (x, y-30)
// and returns its id, else returns -1. `unitNode` is the resolved node pointer.
// `outFormId` receives the new form id (-1 on the off-screen path).
int StartCutscene(int prevFormId, const void* unitNode, int* outFormId);

// ===========================================================================
// PlayIntroCutscene / PlayOutroCutscene  (gilde.exe 0x48ac0c / 0x48ace8)
// ===========================================================================
// Intro: if `musicEnabled`, roll RandomModulo(100) and play the selected track;
// then update sky/daycycle, load + run the kampf intro script (run-main then
// wait-loop). Returns the rolled value (or -1 if music disabled) so the bucket is
// assertable. `waitCtx` is forwarded to the wait-loop hook.
int PlayIntroCutscene(bool musicEnabled, void* waitCtx);

// Outro: stop the ambient track (if `ambientTrackId != 0`), then load + run the
// kampf outro script. Returns true if a script was loaded and run.
bool PlayOutroCutscene(int ambientTrackId, void* waitCtx);

// ===========================================================================
// DropBombAction / ThrowBombAction  (gilde.exe 0x48cdc8 / 0x48ce88)
// ===========================================================================
// The deterministic gate both share: the action queues a target request-22 ONLY
// when (a) the local-player owns the active battle side (isLocalSide) AND (b) — for
// the drop variant — the unit's weapon class (objDef+88) is 1 or 2 (thrown/ranged),
// AND (c) the unit has an active target. The drop variant additionally spawns a bomb
// and plays a voice sample unconditionally; the throw variant always spawns a thrown
// bomb at the unit node. We model the gate + side-effect order.
struct BombActionInput {
    bool isLocalSide   = false;   // dword_631204 == *(side+4)
    bool hasActiveTarget = false; // FindActiveTarget(unit) != null
    i8   weaponClass   = 0;       // objDef+88 (drop variant only: 1/2 -> proceed)
    int  unitNodePos   = 0;       // *(unit+388)->+52 +76 (throw variant spawn arg)
    int  throwArgA = 0, throwArgB = 0; // a1[96]/a1[97]
};
struct BombActionOutcome {
    bool spawnedBomb = false;
    bool playedVoice = false;
    bool queuedRequest = false;
    bool spawnedThrownBomb = false;
    int  thrownBombResult = 0;
};
BombActionOutcome DropBombAction(const BombActionInput& in, void* activeTarget);
BombActionOutcome ThrowBombAction(const BombActionInput& in, void* activeTarget);

// ===========================================================================
// Scene-graph string-match collector callbacks
//   EscapeTileCallback    (0x48b350) — StrCmpNoCase(name, "sp_ESCAPE") == 0
//   ConquerObjectCallback (0x48b5a4) — StrncmpN(name, "sp_CONQUER", 10) == 0
//   WareObjectCallback    (0x48b488) — StrncmpN(name, "WARE_", 5) == 0 AND the
//                                      object's ware id differs from the held ware.
// Each appends the matched object to a shared collector (dword_B59F0C / the caller's
// array) and bumps the count (dword_631268). We model each predicate as a pure bool
// (matched -> append) so a real util sibling can drive it; the original always
// returns 1 (continue the walk). `name` is the scene-object name being tested.
// ===========================================================================
constexpr char kEscapeTileName[]    = "sp_ESCAPE";
constexpr char kConquerObjectName[] = "sp_CONQUER";
constexpr char kWareObjectPrefix[]  = "WARE_";

bool EscapeTileMatches(const char* name);
bool ConquerObjectMatches(const char* name);
// Ware: prefix must match AND the candidate ware id must differ from the held one.
bool WareObjectMatches(const char* name, int candidateWareId, int heldWareId);

// ===========================================================================
// RegisterFlagCallback  (gilde.exe 0x4897bc)
// ===========================================================================
// Resets the flag counter (dword_631260 = 0) then walks the flag scene-graph table
// invoking the flag-create callback. We expose the reset + the walk dispatch; the
// per-node create is the caller's `cb`. Returns the value the walk reports (the
// original forwards SceneGraph_WalkAndInvoke's return; with the inert hook it is 0).
int RegisterFlagCallback(int (*cb)(const char* name, void* node), void* arg);

} // namespace guild::sim
