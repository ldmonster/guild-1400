#include "world/history_commandline_pass.h"
#include "world/city.h"   // guild::world::UtilParseInt (VIBE_Util_ParseInt @0x5dc070)

#include <cstring>

// Faithful 1:1 port of the chronicle COMMANDLINE second pass (gilde.exe 0x4fd8ac)
// and its leading GROUP-reference classification (shared with the first pass at
// 0x4fd6ac). The 27 command-keyword strings (aFest @0x633938) are recovered
// byte-for-byte; the 27 dispatch handlers (funcs_4FDAA5 @0x634414) are engine
// leaves surfaced through the HistoryCommandDispatch hook. The label-string table
// (dword_8C36B0) and group table (dword_122DAE0) are caller-supplied.

namespace guild::world {

// gilde.exe aFest @0x633938 (64-byte stride, 27 rows) — recovered byte-for-byte
// via get_bytes. These are the .esc-style chronicle event command keywords.
const char* const kHistoryCommandNames[27] = {
    "FEST",                       //  0
    "BELAGERUNG_START",           //  1
    "AUFSTAND",                   //  2
    "BRAND",                      //  3
    "WIRBELSTURM",                //  4
    "STADTKASSE",                 //  5
    "VERMOEGEN_STEUER",           //  6
    "VERMOEGEN",                  //  7
    "ANSEHEN_BEI_AMTSTRAEGERN",   //  8
    "KILL_PLAYER",                //  9
    "STRAFE_HINRICHTUNG",         // 10
    "STRAFE_KERKER",              // 11
    "AMTSTRAEGER_INVENTAR_PLUS",  // 12
    "GESETZ",                     // 13
    "GESETZ_REL",                 // 14
    "GLAUBENSWECHSEL",            // 15
    "AUFRUHR",                    // 16
    "GILDENSITZE_BRACH",          // 17
    "NACHFRAGE",                  // 18
    "SOELDNER_PLUENDERN",         // 19
    "SOELDNER_MARODIEREN",        // 20
    "FERNHANDEL_RAUBRITTER",      // 21
    "SPENDE_ANSEHEN",             // 22
    "BETEILIGUNG",                // 23
    "KOMMENTARE",                 // 24
    "PEST",                       // 25
    "INVENTAR_PLUS",              // 26
};

// gilde.exe — the command-keyword scan. The original initialises an index slot to
// the table base, then walks rows comparing `memcmp(token, &aFest[64*idx],
// strlen(&aFest[64*idx]))`, stopping when the running index reaches 27 (the
// `if (v36[4] >= 27) goto LABEL_38` / `if (v38[3] >= 27) goto LABEL_21` sentinel).
// A leading-prefix match (memcmp==0) records the index; no match leaves it at 27.
int HistoryCommandIndex(const char* token) {
    if (!token)
        return kHistoryCommandCount;
    for (int i = 0; i < kHistoryCommandCount; ++i) {
        const char* name = kHistoryCommandNames[i];
        std::size_t n = std::strlen(name);
        if (std::memcmp(token, name, n) == 0)
            return i;
    }
    return kHistoryCommandCount;   // == 27, the "not found" sentinel (`>= 27`)
}

// gilde.exe — the leading GROUP-reference classification shared by both passes.
//   if ( v29 == dword_6343C7 || v29 == dword_6343CC ) { v32 = v30;
//        v21 = VIBE_Util_ParseInt(&v32); if ( v21 >= 4 ) ...error... ; }
// v29 is the dword at head+1 (the 4 prefix chars), v30 is head+6 (the slot digit).
HistoryGroupRef HistoryClassifyGroupRef(const char* head) {
    HistoryGroupRef ref;
    if (!head || !head[0])
        return ref;                          // empty -> kNone

    // v29 == *(int*)(head + 1): the 4 prefix bytes after the leading marker.
    const char* prefix4 = head + 1;
    bool isSet = std::memcmp(prefix4, kHistoryGroupSetPrefix, 4) == 0;
    bool isUse = std::memcmp(prefix4, kHistoryGroupUsePrefix, 4) == 0;
    if (!isSet && !isUse)
        return ref;                          // v9 = &v28 (body parsed verbatim)

    ref.kind = isSet ? HistoryGroupKind::kSet : HistoryGroupKind::kUse;

    // v32 = v30 (head+6, the slot digit char); v21 = ParseInt(&v32).
    char digit[2] = { head[6], '\0' };
    int slot = static_cast<int>(UtilParseInt(digit));
    if (static_cast<unsigned>(slot) >= static_cast<unsigned>(kHistoryGroupCount)) {
        // "Invalid GROUP reference in Label %i" -> return 0.
        ref.valid = false;
        ref.slot  = slot;
        return ref;
    }
    ref.slot = slot;
    return ref;
}

// gilde.exe 0x4fd8ac — VIBE_History_ParseCommandlineSecondPass.
//
// The original's flow once the label text is loaded into the 6096-byte buffer
// (here `labelText`):
//   1. classify the leading group reference; on a "_SET"/"_USE" prefix validate
//      the slot (0..3) and require the group-table row to be populated, else 0;
//      the body then starts AFTER the "_SET<n> "/"_USE<n> " head (v9 = &v31).
//   2. walk the body char-by-char, building a token (v13 != 0 == "in a token"):
//        * a space terminates the current token (when v13): match it against the
//          command table and, on a hit (< 27), dispatch the handler; then reset.
//        * a space with no current token is skipped.
//        * any other char appends to the token scratch (v32) and marks v13 = 1.
//   3. a trailing token at end-of-body is matched + dispatched the same way.
HistoryCmdlineResult HistoryParseCommandlineSecondPass(
    const std::string& labelText,
    const HistoryCommandDispatch& dispatch,
    const std::function<bool(int slot)>& groupTableSlotPopulated,
    bool enabled) {
    // if ( !dword_B537B4 ) return 0;
    if (!enabled)
        return HistoryCmdlineResult::kDisabled;

    const char* head = labelText.c_str();

    // --- group-reference head (v29 == dword_6343C7 || == dword_6343CC) ---------
    HistoryGroupRef group = HistoryClassifyGroupRef(head);
    const char* body;
    int groupSlot = -1;
    if (group.kind != HistoryGroupKind::kNone) {
        if (!group.valid)
            return HistoryCmdlineResult::kDisabled;   // "Invalid GROUP reference"
        // v35 = &dword_122DAE0[17 * v21]; if ( *v35 ) { v9 = &v31; ...} return 0;
        bool populated = groupTableSlotPopulated ? groupTableSlotPopulated(group.slot)
                                                 : true;
        if (!populated)
            return HistoryCmdlineResult::kDisabled;   // empty group slot -> 0
        groupSlot = group.slot;
        // v9 = &v31: the body begins after "_<PRE><n> " i.e. at head + 7 (the
        // original points one past the slot digit; the leading space is then
        // consumed by the tokenizer's space-skip).
        body = head + 7;
        // Guard against a head shorter than the prefix (defensive; the original
        // relies on the buffer being NUL-padded to 6080 bytes).
        if (static_cast<std::size_t>(body - head) > labelText.size())
            body = head + labelText.size();
    } else {
        body = head;                                  // v9 = &v28
    }

    // --- tokenize the body on spaces; match + dispatch each token --------------
    std::string tok;                 // the v32 token scratch
    bool inTok = false;              // v13
    for (const char* p = body; *p; ) {
        char c = *p;
        if (c == ' ') {
            if (inTok) {
                int idx = HistoryCommandIndex(tok.c_str());
                if (idx < kHistoryCommandCount && dispatch) {
                    // funcs_4FDAA5[idx](nameLen, &arg[nameLen]).
                    std::size_t nameLen = std::strlen(kHistoryCommandNames[idx]);
                    std::string argText =
                        nameLen <= tok.size() ? tok.substr(nameLen) : std::string();
                    dispatch(idx, kHistoryCommandNames[idx], argText, groupSlot);
                }
                tok.clear();
                inTok = false;
            } else {
                ++p;                 // ++v9 (skip leading/duplicate spaces)
                continue;
            }
        } else {
            if (!inTok)
                inTok = true;        // if ( !v13 ) v13 = 1;
            tok.push_back(c);        // *(v14 - 1) = v27; ++v14
        }
        ++p;
    }

    // --- trailing token at end-of-body (if ( v13 ) { ... }) --------------------
    if (inTok) {
        int idx = HistoryCommandIndex(tok.c_str());
        if (idx < kHistoryCommandCount && dispatch) {
            std::size_t nameLen = std::strlen(kHistoryCommandNames[idx]);
            std::string argText =
                nameLen <= tok.size() ? tok.substr(nameLen) : std::string();
            dispatch(idx, kHistoryCommandNames[idx], argText, groupSlot);
        }
    }

    return HistoryCmdlineResult::kOk;
}

} // namespace guild::world
