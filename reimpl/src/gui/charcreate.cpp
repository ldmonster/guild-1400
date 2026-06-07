#include "gui/charcreate.h"

#include <cstring>

namespace guild::gui {

// ===========================================================================
// Recovered tables (gilde.exe, via get_bytes).
// ===========================================================================
// Male model names — aPatrizierMann2 @0x527258 (32-byte stride; the 9th slot is the
// empty terminator, so 8 valid entries).
const char* const kPreviewMaleNames[kPreviewMaleCount] = {
    "patrizier_MANN2",
    "wirt2_DICKER",
    "ratsherr2_KUTTE",
    "offizier_SOLDAT",
    "abt_KUTTE",
    "buerger_MANN",
    "dieb3_MANN2",
    "handwerker2_MANN",
};
// Female model names — aBauerinFrau @0x52739c (8th slot empty -> 7 valid entries).
const char* const kPreviewFemaleNames[kPreviewFemaleCount] = {
    "bauerin_FRAU",
    "buergerin_FRAU",
    "buergerin3_FRAU",
    "handwerkerin3_FRAU",
    "magd_FRAU",
    "patrizierin_FRAU",
    "zigeunerin_FRAU",
};
// Male command table — dword_527378 @0x527378 (the trailing 0xFFFFFFFF sentinel dropped).
const int kPreviewMaleCommands[kPreviewMaleCount] = {
    0x57, 0x6b, 0x63, 0x55, 0x00, 0x17, 0x29, 0x3b,
};
// Female command table — dword_52749C @0x52749c (sentinel dropped).
const int kPreviewFemaleCommands[kPreviewFemaleCount] = {
    0x10, 0x1f, 0x25, 0x43, 0x4d, 0x5b, 0x74,
};

// ===========================================================================
// RunChooseCharacter — actor classification (0x52bcd4).
// ===========================================================================
// gilde.exe 0x52bcd4 — the `v4` profession-code switch keyed on the clicked actor.
int ChooseCharacter_ActorProfessionCode(ChooseCharActor actor) {
    switch (actor) {
        case ChooseCharActor::kDiebMann:        return 2; // MenuDummyActor
        case ChooseCharActor::kZigeunerinFrau:  return 2; // v42
        case ChooseCharActor::kHandwerkerMann:  return 1; // v36
        case ChooseCharActor::kHandwerkerinFrau:return 1; // v35
        case ChooseCharActor::kOffizierMann:    return 3; // v41
        case ChooseCharActor::kBuergerMann:     return 0; // v38
        case ChooseCharActor::kBuergerinFrau:   return 0; // v37
        case ChooseCharActor::kPriesterMann:    return 4; // v40
        case ChooseCharActor::kPriesterinFrau:  return 4; // v39
        default:                                return -1;
    }
}

// gilde.exe 0x52bcd4 — male actors {Dieb,Handwerker,Offizier,Buerger,Priester} fill the
// even parent slot; the four "Frau" actors fill the odd slot.
bool ChooseCharacter_ActorIsMale(ChooseCharActor actor) {
    switch (actor) {
        case ChooseCharActor::kDiebMann:
        case ChooseCharActor::kHandwerkerMann:
        case ChooseCharActor::kOffizierMann:
        case ChooseCharActor::kBuergerMann:
        case ChooseCharActor::kPriesterMann:
            return true;
        default:
            return false; // the "Frau" actors (and kNone, never clicked) -> female slot
    }
}

// gilde.exe 0x52bcd4 LABEL_33 — apply one actor click into the dynasty table.
//
// The original advances v6 over already-filled slots (dword_122F258[i] != -1) starting from
// the previous fill point, then writes:
//   if (v6 % 2)  // odd target -> female slot; accept only "Frau" actors -> slot 5
//   else         // even target -> male slot;  accept only "Mann"/parent males -> slot 4
// Here we model the same parity-gated write: a male click takes the next free EVEN slot,
// a female click the next free ODD slot; the profession code is stored and the counter
// advanced past the written slot.
int ChooseCharacter_ApplyActorClick(DynastyTable& t, ChooseCharActor actor, int* fillCounter) {
    int code = ChooseCharacter_ActorProfessionCode(actor);
    if (code < 0) return -1;

    // Advance the fill counter past any already-filled slots (mirrors the original's
    // v6 walk over dword_122F258[i] != -1).
    int v6 = fillCounter ? *fillCounter : 0;
    while (v6 < kAncestrySlotCount && t.slot[v6] != -1)
        ++v6;
    if (v6 >= kAncestrySlotCount) {
        if (fillCounter) *fillCounter = v6;
        return -1; // scene already complete
    }

    bool male = ChooseCharacter_ActorIsMale(actor);
    // Parity gate: even slot wants a male, odd slot wants a female (0x52bcd4: the v6 % 2
    // branch selects slot 5 for the "Frau" actors and slot 4 for the males).
    bool slotIsMale = (v6 % 2) == 0;
    if (male != slotIsMale)
        return -1; // actor does not match the slot parity -> click ignored this frame

    t.slot[v6] = code;        // dword_122F258[v6] = v4
    if (fillCounter) *fillCounter = v6 + 1;
    return v6;
}

// ===========================================================================
// ChooseCharacterTalent commit maps (0x52b088).
// ===========================================================================
int Talent_PaternalProfessionByte(int paternalCode, int prev) {
    switch (paternalCode) {
        case 0: return 28;
        case 1: return 49;
        case 2: return 4;
        case 3: return 55;
        case 4: return 16;
        default: return prev; // default: LOBYTE(dword_122F4A0) unchanged
    }
}

int Talent_MaternalProfessionByte(int maternalCode, int prev) {
    switch (maternalCode) {
        case 0: return 28;
        case 1: return 37;
        case 2: return 4;
        case 3: return 0;
        case 4: return 73;
        default: return prev; // default: BYTE1(dword_122F4A0) unchanged
    }
}

// gilde.exe 0x52b088 — the "+" enable test:  v46[i+6] <= dword_122F270[i]  enables it.
bool Talent_CanDecrease(int value, int cap) {
    return value <= cap;
}
// gilde.exe 0x52b088 — the "-" enable test:  v50 > 0 && (double)value >= dbl_622E18.
bool Talent_CanIncrease(int value, int budget, double floorThreshold) {
    return budget > 0 && static_cast<double>(value) >= floorThreshold;
}

void CharCreate_CommitTalents(CharCreateParams& p, int paternalCode, int maternalCode,
                              const std::uint8_t talents[kTalentCount]) {
    // 0x52b088: LOBYTE/BYTE1(dword_122F4A0) from the two parent codes; byte_122F4F0[i].
    p.paternalProfessionByte = Talent_PaternalProfessionByte(paternalCode, p.paternalProfessionByte);
    p.maternalProfessionByte = Talent_MaternalProfessionByte(maternalCode, p.maternalProfessionByte);
    for (int i = 0; i < kTalentCount; ++i)
        p.talents[i] = talents[i];
}

// ===========================================================================
// ChooseProfession click resolution (0x52c50c / 0x52c693).
// ===========================================================================
int ChooseProfession_IndexForClick(const int* buttonIds, int clickedId) {
    if (!buttonIds) return -1;
    // 0x52c693: while ((beruf[i]+1349) != dword_75BF38) { if (++i >= 8) -> no match }
    for (int i = 0; i < kProfessionCount; ++i)
        if (buttonIds[i] == clickedId)
            return i;
    return -1;
}

// ===========================================================================
// BuildCharacterPreviewScene — portrait cycling (0x52b6b8).
// ===========================================================================
int Preview_WrapIndex(int index, int count) {
    if (count <= 0) return 0;
    // 0x52b6b8:  v1 %= count;  if (v1 < 0) v1 = count - 1;
    int v1 = index % count;
    if (v1 < 0) v1 = count - 1;
    return v1;
}

int Preview_StepIndex(int index, int key) {
    if (key == kPreviewKeyNext) return index + 1; // 'N' -> ++v1
    if (key == kPreviewKeyPrev) return index - 1; // 'J' -> --v1
    return index;
}

const char* Preview_ModelName(int gender, int wrappedIndex) {
    int count = Preview_ModelCount(gender);
    if (wrappedIndex < 0 || wrappedIndex >= count) return "";
    return gender ? kPreviewFemaleNames[wrappedIndex] : kPreviewMaleNames[wrappedIndex];
}

int Preview_ModelCommand(int gender, int wrappedIndex) {
    int count = Preview_ModelCount(gender);
    if (wrappedIndex < 0 || wrappedIndex >= count) return 0;
    int cmd = gender ? kPreviewFemaleCommands[wrappedIndex] : kPreviewMaleCommands[wrappedIndex];
    return cmd + kPreviewCommandBase; // dword_122F528 = v36 + 1468
}

void CharCreate_CommitPortrait(CharCreateParams& p, int gender, int wrappedIndex) {
    p.gender = gender;
    p.portraitIndex = wrappedIndex;
    p.portraitModel = Preview_ModelName(gender, wrappedIndex);
    p.startCommand = Preview_ModelCommand(gender, wrappedIndex);
    p.committed = true; // v63 = 1 on Enter/confirm
}

// ===========================================================================
// ChooseCharacterIntroVariant selection (0x52e4e0).
// ===========================================================================
int IntroVariant_ByteForClick(const int* childIds, int clickedId, int prev) {
    if (!childIds) return prev;
    // 0x52e4e0: dword_62D22C compared against ChildObjectId, v2, v3, v11, v12 -> 0..4.
    // The sixth child (childIds[5]) has no mapping; a miss leaves dword_63C744 unchanged.
    for (int i = 0; i < 5; ++i)
        if (childIds[i] == clickedId)
            return i;
    return prev;
}

} // namespace guild::gui
