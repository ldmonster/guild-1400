#include "sim/cheat_recon.h"

#include <cstring>

namespace guild::sim {

// ---------------------------------------------------------------------------
// Bounded prefix match. The original scans the cheat table with memcmp over
// strlen(cheatString) bytes of the candidate token. On a real null-terminated
// command buffer that is safe, but a SHORT / NUL-less token (a malformed or
// overlong console arg) makes memcmp read past the token's storage (confirmed
// OOB under ASAN — Cheat_MatchToken("AUFRUHR") reads 21 bytes comparing against
// "FERNHANDEL_RAUBRITTER"). This helper reproduces memcmp's result EXACTLY for
// valid input — it walks both strings byte-for-byte and a token NUL that meets a
// non-NUL cheat byte is a mismatch, just as memcmp would compute — but it stops
// at the token's NUL instead of running off the end. Returns true iff `s` is a
// prefix of `token`. Faithful: the original could not match a cheat string
// longer than the token either (the NUL would differ), so no observable change.
static bool TokenHasPrefix(const char* token, const char* s) {
    for (; *s; ++token, ++s) {
        if (*token != *s)   // also catches token NUL meeting a non-NUL cheat byte
            return false;
    }
    return true;
}

// ===========================================================================
// Cheat-string table — gilde.exe aFest @ 0x633938 (27 entries, 64-byte stride).
// Exact tokens recovered via get_bytes. Only the cluster-owned indices 16..26
// are implemented as handlers; 0..15 are listed for table-order fidelity so the
// classifier (which scans the table with memcmp) matches the original exactly.
// ===========================================================================
const char* const kCheatStrings[kCheatEntryCount] = {
    /* 0  0x4fb0f0 */ "FEST",
    /* 1  0x4fb180 */ "BELAGERUNG_START",
    /* 2  0x4fb210 */ "AUFSTAND",
    /* 3  0x4fb284 */ "BRAND",
    /* 4  0x4fb300 */ "WIRBELSTURM",
    /* 5  0x4fb37c */ "STADTKASSE",
    /* 6  0x4fb52c */ "VERMOEGEN_STEUER",
    /* 7  0x4fb614 */ "VERMOEGEN",
    /* 8  0x4fb754 */ "ANSEHEN_BEI_AMTSTRAEGERN",
    /* 9  0x4fb89c */ "KILL_PLAYER",
    /* 10 0x4fb8e8 */ "STRAFE_HINRICHTUNG",
    /* 11 0x4fbbcc */ "STRAFE_KERKER",
    /* 12 0x4fbc40 */ "AMTSTRAEGER_INVENTAR_PLUS",
    /* 13 0x4fbd00 */ "GESETZ",
    /* 14 0x4fbe04 */ "GESETZ_REL",
    /* 15 0x4fbf70 */ "GLAUBENSWECHSEL",
    /* 16 0x4fc260 */ "AUFRUHR",                 // -> VIBE_Cheat_QueueWinGame
    /* 17 0x4fc2d0 */ "GILDENSITZE_BRACH",       // -> VIBE_Cheat_QueueAcquireOffices
    /* 18 0x4fc464 */ "NACHFRAGE",               // -> VIBE_Cheat_ParseSetWeapons
    /* 19 0x4fc7f0 */ "SOELDNER_PLUENDERN",      // -> VIBE_Cheat_QueueHealAllChars
    /* 20 0x4fc8b0 */ "SOELDNER_MARODIEREN",     // -> VIBE_Cheat_QueueRestoreAllChars
    /* 21 0x4fc970 */ "FERNHANDEL_RAUBRITTER",   // -> VIBE_Cheat_ParseSpawnChar
    /* 22 0x4fca1c */ "SPENDE_ANSEHEN",          // -> VIBE_Cheat_QueueGiveBuildingsTeam
    /* 23 0x4fcae4 */ "BETEILIGUNG",             // -> VIBE_Cheat_QueueGiveBuildingsAlt
    /* 24 0x4fcbac */ "KOMMENTARE",              // -> VIBE_Cheat_QueueTeleportCharByName
    /* 25 0x4fcd80 */ "PEST",                    // -> VIBE_Cheat_ParseSetGuildLevel
    /* 26 0x4fce2c */ "INVENTAR_PLUS",           // -> VIBE_Cheat_ParseRenameChar
};

// ===========================================================================
// Hotkey table.
// ===========================================================================

// gilde.exe 0x4fedb0 — VIBE_Hotkey_ClearEntry(int result@<eax>):
//   *(result+4) = -1; *(result+8) = -1.
void Hotkey_ClearEntry(HotkeyEntry* e) {
    e->buildingId = -1;
    e->objectId   = -1;
}

// gilde.exe 0x4fedc0 — VIBE_Hotkey_InitTable.
//   VIBE_Light_SetGrayColorThunk(0,132,byte_122DC10)  // zero 132 bytes (11*12)
//   v2 = 59; for result 0..10:
//     byte_122DC10[12*result] = v2;
//     if (result == 10) unk_122DC88 = 87;   // overwrites entry 10's key
//     entry.buildingId = -1; ++v2; entry.objectId = -1; ++result;
void Hotkey_InitTable(HotkeyEntry table[kHotkeyCount]) {
    std::memset(table, 0, sizeof(HotkeyEntry) * kHotkeyCount); // 132 bytes
    u8 v2 = 59;
    for (int i = 0; i < kHotkeyCount; ++i) {
        table[i].key = v2;
        if (i == 10)
            table[10].key = 87;   // unk_122DC88 = 0x122DC10 + 120 = entry[10].key
        table[i].buildingId = -1;
        ++v2;
        table[i].objectId = -1;
    }
}

// gilde.exe 0x4ff590 — VIBE_Hotkey_SaveTable(handle@<eax>).
//   v5[0] = 11; if (!Write(&v5,4,h,1)) return 0;
//   for i in 0..count-1:  Write(key,1) && Write(buildingId,4) && Write(objectId,4)
//   return 1 once count entries written; 0 on any short write.
int Hotkey_SaveTable(const HotkeyEntry table[kHotkeyCount], int handle,
                     const HotkeyStreamHooks& io) {
    int count = 11;
    if (!io.write(&count, 4, handle, 1))
        return 0;
    if (count <= 0)
        return 1;
    for (int i = 0; i < count; ++i) {
        const HotkeyEntry& e = table[i];
        if (!io.write(&e.key, 1, handle, 1)
         || !io.write(&e.buildingId, 4, handle, 1)
         || !io.write(&e.objectId, 4, handle, 1))
            return 0;
    }
    return 1;
}

// gilde.exe 0x4ff634 — VIBE_Hotkey_LoadTable(handle@<eax>).
int Hotkey_LoadTable(HotkeyEntry table[kHotkeyCount], int handle,
                     const HotkeyStreamHooks& io) {
    int count = 0;
    if (!io.read(&count, 4, handle, 1))
        return 0;
    if (count <= 0)
        return 1;
    for (int i = 0; i < count; ++i) {
        HotkeyEntry& e = table[i];
        if (!io.read(&e.key, 1, handle, 1))
            return 0;
        if (!io.read(&e.buildingId, 4, handle, 1))
            return 0;
        if (!io.read(&e.objectId, 4, handle, 1))
            return 0;
    }
    return 1;
}

// gilde.exe 0x4ff7a8 — VIBE_Hotkey_HandleKeyPress(a1@<edi>).
//   if (byte_67225C && dword_11BC27C != 1) {
//     if (byte_67225C == 88) VIBE_Hotkey_OpenAssignWindow();
//     VIBE_Hotkey_ActivateBuilding();
//     for v1=0,v2=0; v1<11; ++v1,v2+=12:
//        if (byte_67225C == byte_122DC10[v2] && byte_671D8A)
//            VIBE_Hotkey_AssignFromSelection(v1, a1);
//   }
// The effects (open window / activate building / assign) are out of cluster
// reach; this models the EXACT decode and reports through the hooks below.
namespace {
struct HotkeyDispatchHooks {
    void (*openAssignWindow)();
    void (*activateBuilding)();
    void (*assignFromSelection)(int slot);
};
void DefOpenAssign() {}
void DefActivate() {}
void DefAssign(int) {}
HotkeyDispatchHooks g_hk = { &DefOpenAssign, &DefActivate, &DefAssign };
} // namespace

void Hotkey_HandleKeyPress(const CheatReconState& st,
                           const HotkeyEntry table[kHotkeyCount]) {
    if (st.lastKey && st.frameModeFlag != 1) {
        if (st.lastKey == 88)
            g_hk.openAssignWindow();
        g_hk.activateBuilding();
        for (int v1 = 0; v1 < kHotkeyCount; ++v1) {
            if (st.lastKey == table[v1].key) {
                if (st.assignArmed)
                    g_hk.assignFromSelection(v1);
            }
        }
    }
}

// ===========================================================================
// Debug-key dispatch.
// ===========================================================================

// gilde.exe 0x4bfa54 — VIBE_DebugKey_Dispatch.
//   if (byte_67225C >= 0x50) {
//     if (byte_67225C <= 0x50) { dword_63C7C4 = 2; -> HandleActionCmds; }
//     if (byte_67225C == 82)   { dword_63C7C4 = 0; -> ToggleUpdateFlags; }
//   } else if (byte_67225C == 79) { dword_63C7C4 = 1; -> HandleSelectionCmds; }
//   switch (dword_63C7C4) { 0->Toggle; 1->Selection; 2->Action; }
// (0x50 == 80, 79 == 'O', 82 == 'R'.)
DebugDispatch DebugKey_Dispatch(CheatReconState& st) {
    if (st.lastKey >= 0x50u) {
        if (st.lastKey <= 0x50u) {            // key == 80
            st.debugMode = 2;
            return DebugDispatch::Action;
        }
        if (st.lastKey == 82) {               // key == 82
            st.debugMode = 0;
            return DebugDispatch::UpdateFlags;
        }
    } else if (st.lastKey == 79) {            // key == 79
        st.debugMode = 1;
        return DebugDispatch::Selection;
    }
    switch (st.debugMode) {
        case 0:  return DebugDispatch::UpdateFlags;
        case 1:  return DebugDispatch::Selection;
        case 2:  return DebugDispatch::Action;
        default: return DebugDispatch::UpdateFlags; // original falls through (no call)
    }
}

// gilde.exe 0x4bf054 — VIBE_DebugKey_ToggleUpdateFlags.
// Flip toggles: each does `flag = (flag == 0)` and picks an on/off banner; the
// banner is copied to byte_11B6B20 and dword_631678 = dword_62EB38 (side
// effects, omitted). Two special keys: 21 (shadow flag blob), 37 (duel).
DebugToggleResult DebugKey_ToggleUpdateFlags(u8 key, DebugUpdateFlags& flags) {
    DebugToggleResult r;
    auto flip = [](i32& f) { bool was = (f != 0); f = (f == 0); return !was; };

    switch (key) {
        case 4:   // update_d3
            r.which = DebugToggle::UpdateD3;
            r.newState = flip(flags.updateD3);
            r.banner = r.newState ? "update_d3 on" : "update_d3 off";
            return r;
        case 18:  // update_script
            r.which = DebugToggle::UpdateScript;
            r.newState = flip(flags.updateScript);
            r.banner = r.newState ? "update_script on" : "update_script off";
            return r;
        case 21:  // shadow flag blob (only if word_63C740 & 4)
            r.which = DebugToggle::ToggleShadow;
            return r;
        case 23:  // main_update_sim
            r.which = DebugToggle::MainUpdateSim;
            r.newState = flip(flags.mainUpdateSim);
            r.banner = r.newState ? "main_update_sim on" : "main_update_sim off";
            return r;
        case 25:  // update_panel
            r.which = DebugToggle::UpdatePanel;
            r.newState = flip(flags.updatePanel);
            r.banner = r.newState ? "update_panel on" : "update_panel off";
            return r;
        case 30:  // main_update_ai
            r.which = DebugToggle::MainUpdateAi;
            r.newState = flip(flags.mainUpdateAi);
            r.banner = r.newState ? "main_update_ai on" : "main_update_ai off";
            return r;
        case 32:  // main_update_d2
            r.which = DebugToggle::MainUpdateD2;
            r.newState = flip(flags.mainUpdateD2);
            r.banner = r.newState ? "main_update_d2 on" : "main_update_d2 off";
            return r;
        case 35:  // Update He
            r.which = DebugToggle::UpdateHe;
            r.newState = flip(flags.updateHe);
            r.banner = r.newState ? "Update He on" : "Update He off";
            return r;
        case 37:  // duel challenge (only if dword_11BC274)
            r.which = DebugToggle::DuelChallenge;
            return r;
        case 46:  // update_character
            r.which = DebugToggle::UpdateCharacter;
            r.newState = flip(flags.updateCharacter);
            r.banner = r.newState ? "update_character on" : "update_character off";
            return r;
        default:
            return r; // DebugToggle::None — original returns the input unchanged
    }
}

// gilde.exe 0x4bed44 — VIBE_DebugKey_HandleSelectionCmds: key-decode classifier.
// Branch ladder on byte_67225C exactly as the decompile:
//   >= 0x21:
//     <= 0x21 (==33)             -> return (no action)
//     >= 0x2C:
//       <= 0x2C (==44)           -> NpcActionRandom
//     else (33<key<44):
//       > 0x22 (>34):
//          key != 35 -> return; else SpawnGuard (key 35)
//       else (key==34=0x22)      -> HouseAllVacant sweep + Amt notices
//   else >= 0x1E:
//     >= 0x20 (>=32)             -> Resurrect (op125)
//   else >= 0x12:
//     <= 0x12 (==18)             -> SetAllForSale (op127)
//     key == 20                  -> DrainStock (LABEL_6)
DebugSelectionCmd DebugKey_ClassifySelection(u8 key) {
    if (key >= 0x21u) {
        if (key <= 0x21u)
            return DebugSelectionCmd::None;       // key 33
        if (key >= 0x2Cu) {
            if (key <= 0x2Cu)
                return DebugSelectionCmd::NpcActionRandom; // key 44
            return DebugSelectionCmd::None;
        }
        if (key > 0x22u) {                        // keys 35..43
            if (key != 35)
                return DebugSelectionCmd::None;
            return DebugSelectionCmd::SpawnGuard; // key 35
        }
        return DebugSelectionCmd::HouseAllVacant; // key 34
    } else if (key >= 0x1Eu) {                    // keys 30..32
        if (key >= 0x20u)
            return DebugSelectionCmd::Resurrect;  // keys 32+
        return DebugSelectionCmd::None;           // keys 30,31
    } else if (key >= 0x12u) {                    // keys 18..29
        if (key <= 0x12u)
            return DebugSelectionCmd::SetAllForSale; // key 18
        if (key == 20)
            return DebugSelectionCmd::DrainStock;    // key 20 (LABEL_6)
        return DebugSelectionCmd::None;
    }
    return DebugSelectionCmd::None;
}

// gilde.exe 0x4bf2a8 — VIBE_DebugKey_HandleActionCmds: key-decode classifier.
// Branch ladder on byte_67225C exactly as the decompile:
//   >= 0x21:
//     > 0x21 (>33):
//       >= 0x25 (>=37):
//         > 0x25 (>37):
//           >= 0x2E (>=46):
//             > 0x2E (>46): key==48 -> OccupantCategory; else BuildSequence(46)
//             else key==38 -> DetachRelease
//           else (key 38? no): key 38 handled above; here key in 38..45 except..
//             actually: <=0x25 path handled below; this arm is key in {38..45}:
//               key==38 -> DetachRelease (0x26)
//         else (key==37=0x25)   -> OpReset114
//       else >= 0x23 (>=35):
//         > 0x23 (>35=36)       -> SetBusy (0x24)
//         == 0x23 (==35): spawn-sibling / create-and-spawn  -> SpawnSibling
//                          (the >35 fall sets byte_12CEAC1; the ==35 branch has
//                           the create paths)  -> see notes
//       else (key 34=0x22)      -> FreeAttachment
//     else (key==33=0x21)       -> OpFire (op79)
//   else >= 0x19:
//     <= 0x19 (==25)            -> QueueBuild (op72)
//     >= 0x1F (>=31):
//       > 0x1F (>31=32)         -> PartyGather
//       else (key==31): word_63C740 & 4 -> ShadowReset
//     else key==30 (0x1E)       -> SitSample
//   else >= 0x16:
//     <= 0x16 (==22)            -> RandomizeStock
//     key == 23 (0x17)          -> SetReload
//   else key == 17 (0x11)       -> BuildOpDrink (op73, if 631748||631744)
DebugActionCmd DebugKey_ClassifyAction(u8 key) {
    if (key >= 0x21u) {
        if (key > 0x21u) {
            if (key >= 0x25u) {
                if (key > 0x25u) {                // keys 38..255
                    if (key >= 0x2Eu) {           // keys 46..255
                        if (key > 0x2Eu) {        // keys 47..255
                            if (key == 48)
                                return DebugActionCmd::OccupantCategory; // 0x30
                            return DebugActionCmd::None;
                        }
                        return DebugActionCmd::BuildSequence;            // key 46
                    }
                    // keys 38..45: only key 38 acts
                    if (key == 38)
                        return DebugActionCmd::DetachRelease;            // 0x26
                    return DebugActionCmd::None;
                }
                return DebugActionCmd::OpReset114;                       // key 37
            }
            // keys 34..36
            if (key >= 0x23u) {                   // keys 35,36
                if (key > 0x23u)
                    return DebugActionCmd::SetBusy;       // key 36 (byte_12CEAC1=2)
                return DebugActionCmd::SpawnSibling;      // key 35 (create-and-spawn)
            }
            return DebugActionCmd::FreeAttachment;        // key 34 (0x22)
        }
        return DebugActionCmd::OpFire;                    // key 33 (0x21)
    } else if (key >= 0x19u) {
        if (key <= 0x19u)
            return DebugActionCmd::QueueBuild;            // key 25 (0x19)
        if (key >= 0x1Fu) {                               // keys 31,32
            if (key > 0x1Fu)
                return DebugActionCmd::PartyGather;        // key 32 (0x20)
            return DebugActionCmd::ShadowReset;            // key 31 (0x1F)
        }
        if (key == 30)
            return DebugActionCmd::SitSample;              // key 30 (0x1E)
        return DebugActionCmd::None;
    } else if (key >= 0x16u) {                            // keys 22..24
        if (key <= 0x16u)
            return DebugActionCmd::RandomizeStock;         // key 22 (0x16)
        if (key == 23)
            return DebugActionCmd::SetReload;              // key 23 (0x17)
        return DebugActionCmd::None;
    } else if (key == 17) {
        return DebugActionCmd::BuildOpDrink;               // key 17 (0x11)
    }
    return DebugActionCmd::None;
}

// ===========================================================================
// Cheat-token dispatch — gilde.exe 0x4fd8ac (the per-token match loop).
// For a single token: scan kCheatStrings[0..26] with memcmp on the token
// prefix; the first match selects the handler index. The handler is then
// called with the remainder (token + strlen(matched_string)). Returns the
// matched index (0..26) or -1 if no entry matched (original: index reaches 27
// and the call is skipped).
// ===========================================================================
int Cheat_MatchToken(const char* token) {
    if (!token)
        return -1;
    for (int i = 0; i < kCheatEntryCount; ++i) {
        const char* s = kCheatStrings[i];
        if (TokenHasPrefix(token, s))   // bounded; == memcmp(token,s,strlen(s))==0
            return i;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Cluster-owned cheat handlers (indices 16..26). Each reproduces the DISPATCH
// + argument decode of the original; the queued command/effect is delivered as
// a CheatAction via the hooks (the command queue and entity arrays are out of
// reach — rule 8: no fake effects, the decision is faithful).
// ---------------------------------------------------------------------------

// Matches VIBE_Util_ParseInt @0x5dc070, reused locally so the handlers are
// self-contained. The original skips a leading run of chars whose ctype class
// byte_64A208[c+1] has bit 2 (== {tab,LF,VT,FF,CR,space}, confirmed via
// get_bytes @0x64a208), consumes one optional '+'/'-', then accumulates chars
// with bit 0x20 set (digits '0'..'9' only — confirmed: indices 49..58 = 0x38),
// negating iff the sign was '-'. In the cheat grammar ParseInt is only ever
// called on a string that begins with the sign char or a digit (right after '-'
// or '_'), so the leading-whitespace skip is unreachable; modelling space/tab
// is byte-identical for every reachable input. Digit set matches exactly.
static int CheatParseInt(const char* s) {
    if (!s) return 0;
    while (*s == ' ' || *s == '\t') ++s;
    int sign = 1;
    if (*s == '+' || *s == '-') { if (*s == '-') sign = -1; ++s; }
    long v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); ++s; }
    return static_cast<int>(sign * v);
}

// gilde.exe 0x4fc464 — VIBE_Cheat_ParseSetWeapons (NACHFRAGE). Argument grammar
// (verified against aMinus/aPlus @0x4f8d64 and aWaffen @0x4f8d70, plus the sign
// table dword_4F8D5C @0x4f8d5c = {-1, +1}):
//   arg must start with '-';
//   then exactly one of {"MINUS","PLUS"}  (index v3 picks sign v28[v3]);
//   then '_';
//   then one of three categories {"WAFFEN","BUECHER","ENDPRODUKTE"} (index i);
//   then '_';
//   then an integer amount (ParseInt). price delta = sign * amount.
// Per category the engine sweeps the building stock and rewrites prices; that
// is engine-side. The cluster-visible decode is (sign, category, amount) which
// we reproduce exactly. Returns SetWeaponsMinus / SetWeaponsPlus by sign, None
// on any malformed field (matching the original's `return 0`).
CheatAction Cheat_ParseSetWeapons(const char* arg, int* outCategory,
                                  int* outAmount) {
    if (outCategory) *outCategory = -1;
    if (outAmount)   *outAmount   = 0;
    if (!arg || *arg != '-')
        return CheatAction::None;
    static const char* const sign[2]    = { "MINUS", "PLUS" };
    static const int         signVal[2] = { -1, 1 }; // dword_4F8D5C
    static const char* const cats[3]    = { "WAFFEN", "BUECHER", "ENDPRODUKTE" };

    const char* p = arg + 1;
    int s = 0;
    for (; s < 2; ++s)
        if (TokenHasPrefix(p, sign[s]))   // bounded prefix == memcmp==0 (no OOB)
            break;
    if (s >= 2)
        return CheatAction::None;
    p += std::strlen(sign[s]);
    if (*p != '_')
        return CheatAction::None;
    ++p;
    int cat = 0;
    for (; cat < 3; ++cat)
        if (TokenHasPrefix(p, cats[cat]))   // bounded prefix (no OOB on short arg)
            break;
    if (cat >= 3)
        return CheatAction::None;
    p += std::strlen(cats[cat]);
    if (*p != '_')
        return CheatAction::None;
    int amount = CheatParseInt(p + 1);
    if (outCategory) *outCategory = cat;
    if (outAmount)   *outAmount   = signVal[s] * amount;
    return (s == 0) ? CheatAction::SetWeaponsMinus : CheatAction::SetWeaponsPlus;
}

// gilde.exe 0x4fc970 — VIBE_Cheat_ParseSpawnChar.
//   arg starts '-'; strlen(arg+1) >= 2; arg[1] in {N,S,O,W,A} (78,83,79,87,65);
//   amount = ParseInt(arg+1); if amount>0 queue op18 spawn. Returns 1 always
//   when arg starts '-' and passes the letter gate (original returns 1).
CheatAction Cheat_ParseSpawnChar(const char* arg) {
    if (!arg || *arg != '-')
        return CheatAction::None;
    const char* v2 = arg + 1;
    if (std::strlen(v2) < 2)
        return CheatAction::None;
    char c = *v2;
    if (c != 'N' && c != 'S' && c != 'O' && c != 'W' && c != 'A')
        return CheatAction::None;
    // amount = ParseInt(arg+1); op18 queued only if amount > 0.
    (void)CheatParseInt(arg + 1);
    return CheatAction::SpawnChar;
}

// gilde.exe 0x4fcd80 — VIBE_Cheat_ParseSetGuildLevel (PEST).
//   arg starts '-'; ParseInt(arg+1); queue op89 (guild level, v8 WORD2 = 11).
CheatAction Cheat_ParseSetGuildLevel(const char* arg) {
    if (!arg || *arg != '-')
        return CheatAction::None;
    (void)CheatParseInt(arg + 1);
    return CheatAction::SetGuildLevel;
}

// gilde.exe 0x4fc2d0 — VIBE_Cheat_QueueAcquireOffices (GILDENSITZE_BRACH).
//   arg starts '-'; offices 30..33 get table entries; ParseInt(arg+1);
//   queue op0x80. Returns 1 when arg starts '-'.
CheatAction Cheat_QueueAcquireOffices(const char* arg) {
    if (!arg || *arg != '-')
        return CheatAction::None;
    (void)CheatParseInt(arg + 1);
    return CheatAction::AcquireOffices;
}

// The remaining cluster-owned handlers take no parsed argument (or scan engine
// arrays directly) and always queue their effect; their decision is the action
// id alone. (Effects are engine-side: op125/op87/op88/op84/op85/op82/op17.)
CheatAction Cheat_QueueWinGame()         { return CheatAction::WinGame; }
CheatAction Cheat_QueueHealAllChars()    { return CheatAction::HealAllChars; }
CheatAction Cheat_QueueRestoreAllChars() { return CheatAction::RestoreAllChars; }
CheatAction Cheat_QueueGiveBuildingsTeam(){ return CheatAction::GiveBuildingsTeam; }
CheatAction Cheat_QueueGiveBuildingsAlt(){ return CheatAction::GiveBuildingsAlt; }

// gilde.exe 0x4fcbac — VIBE_Cheat_QueueTeleportCharByName (KOMMENTARE).
//   scans the history (byte_122DBF0 holds the searched name) with date parsing;
//   on a name match queues op82 teleport. Engine-side; reports the action.
CheatAction Cheat_QueueTeleportCharByName() { return CheatAction::TeleportCharByName; }

// gilde.exe 0x4fce2c — VIBE_Cheat_ParseRenameChar (INVENTAR_PLUS).
//   gated on a found person record + guild rank checks; arg starts '-' and
//   strlen(arg+1) >= 4; queues op17 rename. Engine-side gating; reports action.
CheatAction Cheat_ParseRenameChar(const char* arg) {
    if (!arg || *arg != '-')
        return CheatAction::None;
    if (std::strlen(arg + 1) < 4)
        return CheatAction::None;
    return CheatAction::RenameChar;
}

} // namespace guild::sim
