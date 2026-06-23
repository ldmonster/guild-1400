#pragma once
// ===========================================================================
// cheat_recon.{h,cpp} — Cheat-code dispatch, hotkey handlers, and debug keys
// (gilde.exe; namespaces guild::sim; VIBE_Cheat_*, VIBE_Hotkey_*,
//  VIBE_DebugKey_* families).
// ===========================================================================
//
// Three related input-driven subsystems, reconstructed 1:1 from the Hex-Rays
// decompile:
//
//  * Cheat codes  — typed into the history/commandline ("FEST" prefixed cheat
//    tokens). VIBE_History_ParseCommandlineSecondPass (0x4fd8ac) tokenizes a
//    label string and, for each space-delimited token, scans the 27-entry
//    string table `aFest` (0x633938, 64-byte stride) with memcmp; the matching
//    index selects a handler from the function-pointer table `funcs_4FDAA5`
//    (0x634414, 27 entries) and calls it with the remainder of the token.
//    This cluster owns handler indices 16..26 (0x4fc260..0x4fce2c).
//
//  * Hotkeys      — building hotkeys F1..F10 plus the assign key. Table
//    `byte_122DC10` (0x122dc10), 11 entries x 12 bytes: {u8 key, i32 buildingId
//    (+4), i32 objectId (+8)}. VIBE_Hotkey_HandleKeyPress (0x4ff7a8) dispatches
//    the global last-key byte_67225C (0x67225c) against this table.
//
//  * Debug keys   — developer commands gated behind a 3-mode dispatcher
//    (VIBE_DebugKey_Dispatch 0x4bfa54) keyed on byte_67225C: selection cmds
//    (mode 1), update-flag toggles (mode 0), action cmds (mode 2).
//
// Cross-module EFFECTS (mutating entity arrays, the command queue, building
// state, the form/window system, the global game clock) are routed through an
// inert-default hooks struct, CheatReconHooks, so the dispatch / string-match /
// key-decode logic is reconstructed exactly while the live game state — which
// this cluster cannot reach — is reproduced via injectable callbacks. The
// engine globals (last-key, selection ids, mode) are likewise modeled as a
// CheatReconState the caller owns.
//
// NOTE on scope (rule 8 — no cheap analogues): the large debug-key handlers
// (HandleSelectionCmds, ToggleUpdateFlags, HandleActionCmds) and the windowed
// hotkey-assign UI (OpenAssignWindow) touch dozens of engine subsystems that
// are out of this cluster's reach. We faithfully reconstruct their KEY-DECODE
// DISPATCH (which key code -> which action id / which branch) as an enumerated
// classifier and reproduce the toggle bookkeeping exactly; the per-branch
// engine side effects are delivered as discrete action ids through the hooks.
// Nothing is faked: an unhandled key returns the same "no action" the original
// does (falls through / returns the input unchanged).

#include "guild/common/types.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// Cheat dispatch table — gilde.exe 0x633938 (aFest strings) / 0x634414 (funcs).
// 27 entries; this cluster implements indices 16..26. Indices 0..15 are other
// clusters' handlers (BELAGERUNG_START, BRAND, ... GLAUBENSWECHSEL) and are
// listed here only so the classifier matches the original's table order.
// ---------------------------------------------------------------------------
constexpr int kCheatEntryCount = 27;

// The cheat-string table, exact bytes from 0x633938 (64-byte stride, but stored
// here as C strings — only the leading NUL-terminated token is significant).
extern const char* const kCheatStrings[kCheatEntryCount];

// Action ids dispatched by the cluster-owned cheat handlers. Each corresponds
// to the (eventual) command-queue request the original builds. Returned by the
// handlers via the hooks so callers can route them to the real command system.
enum class CheatAction : int {
    None = 0,
    WinGame,              // 0x4fc260 — AUFRUHR        (op125, slot reset)
    AcquireOffices,       // 0x4fc2d0 — GILDENSITZE_BRACH (offices 30..33 + op0x80)
    SetWeaponsMinus,      // 0x4fc464 — NACHFRAGE / "-MINUS_..." weapon price down
    SetWeaponsPlus,       // 0x4fc464 — NACHFRAGE / "-PLUS_..."  weapon price up
    HealAllChars,         // 0x4fc7f0 — SOELDNER_PLUENDERN (op87 per animal)
    RestoreAllChars,      // 0x4fc8b0 — SOELDNER_MARODIEREN (op88 per animal)
    SpawnChar,            // 0x4fc970 — FERNHANDEL_RAUBRITTER (op18 spawn)
    GiveBuildingsTeam,    // 0x4fca1c — SPENDE_ANSEHEN (op84, type 12, <=8)
    GiveBuildingsAlt,     // 0x4fcae4 — BETEILIGUNG    (op85, type 10, <=8)
    TeleportCharByName,   // 0x4fcbac — KOMMENTARE     (op82, history scan)
    SetGuildLevel,        // 0x4fcd80 — PEST           (op89, level 11)
    RenameChar,           // 0x4fce2c — INVENTAR_PLUS  (op17 rename request)
};

// ---------------------------------------------------------------------------
// Hotkey table — gilde.exe byte_122DC10 @ 0x122dc10 (11 x 12 bytes).
// ---------------------------------------------------------------------------
constexpr int kHotkeyCount = 11;

#pragma pack(push, 1)
struct HotkeyEntry {            // +0x00 stride 12
    u8  key;                    // +0x00 — scancode (byte_122DC10[12*i])
    u8  pad[3];                 // +0x01 — alignment
    i32 buildingId;             // +0x04 — dword_122DC14[3*i]
    i32 objectId;               // +0x08 — dword_122DC18[3*i]
};
#pragma pack(pop)
static_assert(sizeof(HotkeyEntry) == 12, "HotkeyEntry stride must be 12");

// VIBE_Hotkey_ClearEntry @ 0x4fedb0 — record+4 = -1, record+8 = -1.
void Hotkey_ClearEntry(HotkeyEntry* e);

// VIBE_Hotkey_InitTable @ 0x4fedc0 — fills 11 entries: key = 59 + i for i<10,
// entry 10's key overwritten to 87 (unk_122DC88 = 87); ids cleared to -1.
void Hotkey_InitTable(HotkeyEntry table[kHotkeyCount]);

// VIBE_Hotkey_SaveTable @ 0x4ff590 / VIBE_Hotkey_LoadTable @ 0x4ff634 — stream
// out/in: write i32 count(=11) then per entry {1 byte key, 4 byte buildingId,
// 4 byte objectId}. Returns 1 on success, 0 on any short write/read.
// `io` is the inert per-byte stream hook (returns bytes transferred==len).
struct HotkeyStreamHooks {
    // mirrors VIBE_Vfs_WriteStream / VIBE_Vfs_ReadStreamBool: (buf, size, handle,
    // count) -> nonzero on success.
    int (*write)(const void* buf, int size, int handle, int count);
    int (*read)(void* buf, int size, int handle, int count);
};
int Hotkey_SaveTable(const HotkeyEntry table[kHotkeyCount], int handle,
                     const HotkeyStreamHooks& io);
int Hotkey_LoadTable(HotkeyEntry table[kHotkeyCount], int handle,
                     const HotkeyStreamHooks& io);

// ---------------------------------------------------------------------------
// Input/engine state this cluster reads (modeled; the originals are globals).
// ---------------------------------------------------------------------------
struct CheatReconState {
    u8  lastKey       = 0;   // byte_67225C @ 0x67225c — current key code
    u8  assignArmed   = 0;   // byte_671D8A @ 0x671d8a — hotkey-assign mode flag
    i32 frameModeFlag = 0;   // dword_11BC27C @ 0x11bc27c — !=1 gates hotkeys
    i32 debugMode     = 0;   // dword_63C7C4 @ 0x63c7c4 — debug dispatch sub-mode
};

// VIBE_Hotkey_HandleKeyPress @ 0x4ff7a8 — for the current lastKey:
//   if lastKey && frameModeFlag != 1:
//     if lastKey == 88 -> OpenAssignWindow (hook)
//     ActivateBuilding (hook)
//     for i in 0..10: if lastKey == table[i].key && assignArmed
//                        -> AssignFromSelection(i) (hook)
void Hotkey_HandleKeyPress(const CheatReconState& st,
                           const HotkeyEntry table[kHotkeyCount]);

// ---------------------------------------------------------------------------
// Debug-key dispatch — gilde.exe 0x4bfa54.
// Classifies the key into one of three modes and remembers it in debugMode:
//   key == 79 -> mode 1 (selection cmds)
//   key == 80 -> mode 2 (action cmds)
//   key == 82 -> mode 0 (update-flag toggles)
//   otherwise -> re-dispatch to the remembered mode.
// ---------------------------------------------------------------------------
enum class DebugDispatch : int {
    UpdateFlags = 0,   // -> VIBE_DebugKey_ToggleUpdateFlags
    Selection   = 1,   // -> VIBE_DebugKey_HandleSelectionCmds
    Action      = 2,   // -> VIBE_DebugKey_HandleActionCmds
};

// Returns the dispatch class the original would route to, AND updates
// st.debugMode exactly as the original mutates dword_63C7C4.
DebugDispatch DebugKey_Dispatch(CheatReconState& st);

// VIBE_DebugKey_ToggleUpdateFlags @ 0x4bf054 — the developer "update_*" toggles.
// Each toggle flips a global flag and copies an on/off banner string. We model
// the flag set as a struct and return which toggle (if any) fired and its new
// state, plus the banner. Faithful to the exact key codes and toggled flags.
struct DebugUpdateFlags {
    i32 updateD3        = 0;  // dword_631E74  (key 4)
    i32 updatePanel     = 0;  // dword_631E78  (key 25)
    i32 updateScript    = 0;  // dword_631E7C  (key 18)
    i32 updateCharacter = 0;  // dword_631E70  (key 46)
    i32 mainUpdateSim   = 0;  // dword_63C8E4  (key 23)
    i32 mainUpdateAi    = 0;  // dword_63C8E0  (key 30)
    i32 mainUpdateD2    = 0;  // dword_63C8EC  (key 32)
    i32 updateHe        = 0;  // dword_63C8E8  (key 35)
};

enum class DebugToggle : int {
    None = 0,
    UpdateD3,        // key 4
    UpdatePanel,     // key 25
    UpdateScript,    // key 18
    UpdateCharacter, // key 46
    MainUpdateSim,   // key 23
    MainUpdateAi,    // key 30
    MainUpdateD2,    // key 32
    UpdateHe,        // key 35
    ToggleShadow,    // key 21 — VIBE_Command_QueueRequestFlagBlob32(17,..) if word_63C740 & 4
    DuelChallenge,   // key 37 — VIBE_Command_EnqueueDuelChallenge if dword_11BC274
};

struct DebugToggleResult {
    DebugToggle which = DebugToggle::None;
    bool        newState = false;     // for the flip toggles: the flag's new value
    const char* banner   = nullptr;   // the on/off banner string copied to byte_11B6B20
};

// Pure decode of VIBE_DebugKey_ToggleUpdateFlags: mutates `flags` for the flip
// toggles and reports the result. The two non-flip keys (21 shadow, 37 duel)
// report their action so the caller can route to the command queue.
DebugToggleResult DebugKey_ToggleUpdateFlags(u8 key, DebugUpdateFlags& flags);

// VIBE_DebugKey_HandleSelectionCmds @ 0x4bed44 and HandleActionCmds @ 0x4bf2a8
// classify the key into a discrete action id. The per-action engine side
// effects are out of cluster reach, so we expose the EXACT key->action mapping
// (rule 8: faithful dispatch, no fake effects).
enum class DebugSelectionCmd : int {
    None = 0,
    SetAllForSale,        // key 18 (0x12) — op127 over all chars
    Resurrect,            // keys 30..31 (0x1E,0x1F) — op125 reset (resurrect)
    HouseAllVacant,       // keys 33..43 (0x21..0x2B except handled) — sell-house sweep + Amt notices
    SpawnGuard,           // key 35 (0x23) — find idle guard, queue coord/escort
    NpcActionRandom,      // key 44 (0x2C) — VIBE_NpcAction_Dispatch random method
    DrainStock,           // key 20 (0x14) — VIBE_Building_AdjustStockAndNotify(-1000)
};
DebugSelectionCmd DebugKey_ClassifySelection(u8 key);

enum class DebugActionCmd : int {
    None = 0,
    BuildOpDrink,         // key 17 (0x11) — build-op73 drink/sit sequence
    RandomizeStock,       // key 22 (0x16) — dword_12CE934 += rand%128
    SetReload,            // key 23 (0x17) — VIBE_DebugFlag_SetReload
    SitSample,            // key 30 (0x1E) — sit/stand sample toggle
    QueueBuild,           // key 25 (0x19) — VIBE_Command_RequestBuildOp72
    ShadowReset,          // key 31 (0x1F) — flagblob32(17,-1) if word_63C740 & 4
    PartyGather,          // key 32 (0x20) — op (gather up to 8 nearby chars)
    OpFire,               // key 33 (0x21) — op79 fire (if dword_11BC278)
    FreeAttachment,       // key 35 (0x23 > ) — VIBE_Memory_FreeDebug attachment
    SpawnSibling,         // key 35 (0x23 ==) — clone/spawn person
    SetBusy,              // key 36 (0x24 > ) — byte_12CEAC1 = 2
    OpReset114,           // key 37 (0x25) — op114 reset (if dword_11BC270)
    DetachRelease,        // key 38 (0x26) — VIBE_Object_DetachAndRelease
    BuildSequence,        // key 46 (0x2E) — op(VIBE_GameTime_Advance) build seq
    OccupantCategory,     // key 48 (0x30) — VIBE_Building_UpdateOccupantCategory
};
DebugActionCmd DebugKey_ClassifyAction(u8 key);

// ---------------------------------------------------------------------------
// Cheat-token dispatch (gilde.exe 0x4fd8ac match loop + handlers 0x4fc260..).
// ---------------------------------------------------------------------------

// Scan kCheatStrings with memcmp on the token prefix; returns the matched index
// (0..26) or -1 if none matched (original skips the call when index reaches 27).
int Cheat_MatchToken(const char* token);

// Cluster-owned cheat handlers (table indices 16..26). The `arg` is the token
// remainder after the matched cheat string. Each returns the CheatAction it
// would queue, or None when the original's argument gate fails (return 0).
CheatAction Cheat_QueueWinGame();                              // 16 AUFRUHR
CheatAction Cheat_QueueAcquireOffices(const char* arg);        // 17 GILDENSITZE_BRACH
CheatAction Cheat_ParseSetWeapons(const char* arg, int* outCategory = nullptr,
                                  int* outAmount = nullptr);   // 18 NACHFRAGE
CheatAction Cheat_QueueHealAllChars();                         // 19 SOELDNER_PLUENDERN
CheatAction Cheat_QueueRestoreAllChars();                      // 20 SOELDNER_MARODIEREN
CheatAction Cheat_ParseSpawnChar(const char* arg);             // 21 FERNHANDEL_RAUBRITTER
CheatAction Cheat_QueueGiveBuildingsTeam();                    // 22 SPENDE_ANSEHEN
CheatAction Cheat_QueueGiveBuildingsAlt();                     // 23 BETEILIGUNG
CheatAction Cheat_QueueTeleportCharByName();                   // 24 KOMMENTARE
CheatAction Cheat_ParseSetGuildLevel(const char* arg);         // 25 PEST
CheatAction Cheat_ParseRenameChar(const char* arg);            // 26 INVENTAR_PLUS

} // namespace guild::sim
