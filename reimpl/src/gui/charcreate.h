#pragma once
// guild::gui — the NEW-CHARACTER creation flow (distinct from gui/newgame_setup, which
// owns the surrounding city/history/player-identity wiring).  This module recovers the
// dynasty/appearance picker reached from RunChooseCharacter:
//
//   VIBE_Menu_RunChooseCharacter        @0x52bcd4  form "Menu\CHOOSECITY_HEADER"
//       A 3D ancestry scene with nine clickable "dummy_*" actors (grandparents + parents,
//       both genders).  Clicking an actor records that ancestor's PROFESSION code and
//       GENDER into the dynasty slot table dword_122F258[6]; once both parent slots (4/5)
//       are filled it chains  Talent -> Profession -> BuildCharacterPreviewScene.
//   VIBE_Menu_ChooseCharacterTalent     @0x52b088  form "menu\choosecharacter_talent"
//       The trait/talent up-down picker; on commit it maps the two parent professions
//       (v46[4]/v46[5]) to the starting-character profession bytes LOBYTE/BYTE1 of
//       dword_122F4A0, copies the five talents into byte_122F4F0[5].
//   VIBE_Menu_ChooseProfession          @0x52c50c  form "Menu\CHOOSEPROFESSION"
//       An 8-button grid (gfx = beruf[i]+1349); on click stores
//       BuildingType_ComputeVariantIndex(beruf,1) into HIBYTE(dword_122F4A0).
//       (The grid GEOMETRY helpers live in gui/newgame_setup; this module recovers the
//       click->profession-byte resolution as the original scans the built id table.)
//   VIBE_Menu_BuildCharacterPreviewScene @0x52b6b8 form (shares the choose-character form)
//       The final PORTRAIT/model picker: cycles a gender-selected model-name table
//       (male aPatrizierMann2 / female aBauerinFrau) with up/down keys (78='N',74='J'),
//       wraps the index, copies the chosen model name to unk_122F4F5 and arms the start
//       command dword_122F528 = modelCommand + 1468.
//   VIBE_Menu_ChooseCharacterIntro       @0x52e3d8  form "menu\choosecharacter_intro"
//   VIBE_Menu_ChooseCharacterIntroVariant@0x52e4e0  (same form)
//       The intro variant radio screens; IntroVariant maps the six clicked child objects
//       to the variant byte (byte_12335BA / dword_63C744).
//
// As with gui/newgame_setup, the HEAVY side effects (the 3D scene, the form loader, the
// rich-text renderer, the radiogroup runner, character spawning and the emitted command)
// are reused/forward-declared and routed through a sink so the deterministic SELECTION,
// CLASSIFICATION, CYCLING and COMMIT logic is testable in isolation.
//
// ODR: the CHOOSEPROFESSION grid geometry (Profession_ButtonX/Y/Gfx, kProfession*) and the
// session/start constants (kStartCommandValue, kSession*) are REUSED from gui/newgame_setup
// and gui/main_menu, not redefined.  The Form/Widget API leaves (Object_AddToWindow,
// RadioGroup_Create, GetChildObjectId, ...) are forward-declared, not redefined.

#include "gui/newgame_setup.h"  // kProfession*, kStartCommandValue, NewGameParams (REUSED)

#include <cstdint>
#include <string>

namespace guild::gui {

// ===========================================================================
// Recovered form names (byte-for-byte from the GameTick_Finalize calls).
// ===========================================================================
inline constexpr const char* kFormChooseCharacterHeader  = "Menu\\CHOOSECITY_HEADER"; // scene form (0x52bcd4)
inline constexpr const char* kFormChooseCharacterTalent  = "menu\\choosecharacter_talent";
inline constexpr const char* kFormChooseCharacterIntro   = "menu\\choosecharacter_intro";
// (kFormChooseProfession "Menu\CHOOSEPROFESSION" is REUSED from gui/newgame_setup.)
// NOTE: VIBE_Menu_ChoosePlayerNew @0x52c7c8 references form "Menu\CHOOSEPLAYER_NEW", but
// that function has zero callers in this build and no such .form ships in forms.BIN; the
// LIVE player-identity screen is RunChoosePlayer ("Menu\CHOOSEPLAYER"), owned by
// gui/newgame_setup.  We do not model the dead ChoosePlayerNew path here.

// ===========================================================================
// RunChooseCharacter — the dynasty/ancestry scene (0x52bcd4).
// ---------------------------------------------------------------------------
// Six dynasty slots dword_122F258[0..5]; the actor click writes slot v6 with a
// PROFESSION code and marks the actor consumed.  The slot order is:
//   0 paternal grandfather, 1 paternal grandmother,
//   2 maternal grandfather, 3 maternal grandmother,
//   4 father,               5 mother.
// 1:1 NOTE (HARDEN gui_00 — divergence confirmed against disasm, kept for follow-up):
// The current ApplyActorClick / IsComplete model fills slots 0..5 (parity-gated) and completes
// after SIX clicks.  The ORIGINAL @0x52bcd4 does NOT.  Disasm ground truth (0x52bf14..0x52bf3e,
// 0x52c084..0x52c0b4) and aliasing:
//   * dword_122F258 is the slot base; slot[i] = 0x122F258 + 4*i, so slot4 == dword_122F268.
//   * Init order @0x52bd1b/0x52bd25: SetGrayColorThunk(0,44,dword_122F258) is a memset of
//     44 BYTES (=11 dwords) to 0 (VIBE_Memory_FillDwordAlignedThunk a3=byte count); THEN
//     `for i=0..5: dword_122F254[i]=-1` writes 0x122F254 and slots 0,1,2,3,4 to -1.
//     => after init: slots 0..4 = -1, slot5 = 0 (left at the gray-fill value).
//   * Free-slot walk: v7=4; edi=dword_122F268(=slot4); if slot4==-1 -> v7 stays 4 (father slot
//     open); else advance and test slot5 (!= -1?) -> since slot5 starts at 0, the walk reaches
//     v7=6 immediately after the father is placed.  Walk only ever inspects slots 4 and 5.
//   * Click writer @0x52c0b4 writes ONLY dword_122F258[v7], v7 in {4,5}, parity-gated
//     (even=4 accepts males, odd=5 accepts females).  Grandparent slots 0..3 are NEVER written.
//   Net observed binary behavior: the scene needs the FATHER (male) click into slot 4; slot 5
//   (mother) is pre-seeded to 0 by the gray-fill so the walk completes (v7>=6) right after the
//   father is placed.  This is a SURPRISING result (mother appears never selectable via the
//   walk) that hinges on the gray-fill-vs-(-1) aliasing; per Rule 8 it is documented with
//   evidence and FLAGGED rather than guessed into both this module + gui/choosecharacter_run.*
//   + their tests.  See progress/harden/gui_00.md.  xrefs confirm 0x52c0b4 is the sole click
//   writer of dword_122F258.
// The clickable "dummy_*" actor object names (table aDummyGrossvate @0x5274e4, 6x48).
// ===========================================================================
inline constexpr int kAncestrySlotCount = 6;
// gilde.exe 0x52bcd4 — the actor-click free-slot walk starts at slot 4 (the father slot).
inline constexpr int kAncestryParentBaseSlot = 4;
inline constexpr const char* kAncestryObjectNames[kAncestrySlotCount] = {
    "dummy_GROSSVATER_VAETERLICH",  // slot 0
    "dummy_GROSSMUTTER_VAETERLICH", // slot 1
    "dummy_GROSSVATER_MUETTERLICH", // slot 2
    "dummy_GROSSMUTTER_MUETTERLICH",// slot 3
    "dummy_VATER",                  // slot 4
    "dummy_MUTTER",                 // slot 5
};

// The nine clickable scene actors (0x52bcd4): each maps to a profession code (v4) and a
// gender (parent slot 4 = male, 5 = female).  Ordering matches the CreateMenuDummyActor
// sequence so a fixture can drive clicks by index.
enum class ChooseCharActor {
    kDiebMann      = 0, // dummy_NACHT_UND_NEBEL_MANN  (dieb_MANN2)        -> code 2, male
    kZigeunerinFrau= 1, // dummy_NACHT_UND_NEBEL_FRAU  (zigeunerin_FRAU)   -> code 2, female
    kHandwerkerMann= 2, // dummy_HANDWERKSKUNST_MANN   (handwerker_MANN)   -> code 1, male
    kHandwerkerinFrau=3, // dummy_HANDWERKSKUNST_FRAU  (handwerkerin2_FRAU)-> code 1, female
    kOffizierMann  = 4, // dummy_KAMPF_MANN            (offizier_SOLDAT)   -> code 3, male
    kBuergerMann   = 5, // dummy_VERHANDELN_MANN       (buerger3_DICKER)   -> code 0, male
    kBuergerinFrau = 6, // dummy_VERHANDELN_FRAU       (buergerin2_FRAU)   -> code 0, female
    kPriesterMann  = 7, // dummy_RHETORIK_MANN         (priester_KUTTE)    -> code 4, male
    kPriesterinFrau= 8, // dummy_RHETORIK_FRAU         (buergerin_FRAU)    -> code 4, female
    kNone          = -1,
};

// gilde.exe 0x52bcd4 — the profession code an actor click records into the dynasty slot
// (the `v4` switch).  Returns -1 for kNone / out of range.
int ChooseCharacter_ActorProfessionCode(ChooseCharActor actor);

// gilde.exe 0x52bcd4 — whether an actor is MALE (placed in even parent slot 4) or female
// (odd parent slot 5).  In the original this is the `v6 % 2` test combined with the actor
// set: males {Dieb,Handwerker,Offizier,Buerger,Priester}, females the rest.
bool ChooseCharacter_ActorIsMale(ChooseCharActor actor);

// The dynasty slot table dword_122F258[6].  -1 = unfilled.  ApplyActorClick fills the
// next free slot for the actor's gender (0x52bcd4 LABEL_33: even slots take males, odd
// females); returns the slot written, or -1 if no slot was available / actor rejected.
// (See the 1:1 NOTE above: the original click only writes parent slots 4/5.)
struct DynastyTable {
    int slot[kAncestrySlotCount];
    DynastyTable() { for (int& s : slot) s = -1; }
};

// gilde.exe 0x52bcd4 — apply one actor click.  `v6` is the running fill counter (the
// original advances it to the next unfilled slot).  On a MALE actor it targets the even slot
// index, on a FEMALE the odd one; writes the profession code and returns the slot, advancing
// `*fillCounter`.  Returns -1 when the actor's parity slot is rejected / past the end.
int ChooseCharacter_ApplyActorClick(DynastyTable& t, ChooseCharActor actor, int* fillCounter);

// gilde.exe 0x52bcd4 LABEL_36 — the scene completes (chains to Talent/Profession/Preview)
// once the fill counter reaches kAncestrySlotCount (v6 >= 6).
inline bool ChooseCharacter_IsComplete(int fillCounter) { return fillCounter >= kAncestrySlotCount; }

// ===========================================================================
// ChooseCharacterTalent commit (0x52b088).
// ---------------------------------------------------------------------------
// On confirm, the two parent profession codes (v46[4] paternal, v46[5] maternal) are
// mapped through fixed switch tables to the starting-character profession bytes written
// into LOBYTE(dword_122F4A0) and BYTE1(dword_122F4A0).  The five chosen talent values
// v46[6..10] are copied into byte_122F4F0[0..4].
// ===========================================================================
inline constexpr int kTalentCount = 5;

// gilde.exe 0x52b088 — LOBYTE(dword_122F4A0) from the paternal code v46[4]:
//   0->28, 1->49, 2->4, 3->55, 4->16; other -> unchanged (returns `prev`).
int Talent_PaternalProfessionByte(int paternalCode, int prev);
// gilde.exe 0x52b088 — BYTE1(dword_122F4A0) from the maternal code v46[5]:
//   0->28, 1->37, 2->4, 3->0, 4->73; other -> unchanged (returns `prev`).
int Talent_MaternalProfessionByte(int maternalCode, int prev);

// gilde.exe 0x52b088 — the two per-talent arrow widgets' DISPLAY-ENABLE predicates (the
// enable loop @0x52b386/@0x52b3b0; ebp=1 enabled, edi=0 disabled):
//   DECREASE ("-") widget: enabled when  value <= cap          (v46[i+6] <= dword_122F270[i])
//   INCREASE ("+") widget: enabled when  budget <= 0 || value >= 126.0  (dbl_622E18)
// Modeled as pure predicates so the enable logic is testable.  floorThreshold defaults to the
// recovered dbl_622E18 = 126.0 (the per-row max / starting budget).
bool Talent_CanDecrease(int value, int cap);                 // v46[i+6] <= dword_122F270[i]
bool Talent_CanIncrease(int value, int budget, double floorThreshold = 126.0);

// ===========================================================================
// ChooseProfession click resolution (0x52c50c).
// ---------------------------------------------------------------------------
// The grid GEOMETRY (Profession_ButtonX/Y/Gfx) is REUSED from gui/newgame_setup.  Here we
// recover the click->profession-byte path: the build loop creates 8 buttons whose +8 id is
// beruf[i] + 1349; the click loop (0x52c693) scans for the id matching dword_75BF38 and
// commits HIBYTE(122F4A0) = ComputeVariantIndex(beruf, 1).
// ===========================================================================
// gilde.exe 0x52c693 — given the built button id table (beruf[i]+1349) and the clicked
// widget id, return the index 0..7 of the matching button, or -1 if none matched.
int ChooseProfession_IndexForClick(const int* buttonIds, int clickedId);

// ===========================================================================
// BuildCharacterPreviewScene — the portrait/model picker (0x52b6b8).
// ---------------------------------------------------------------------------
// Two model-name tables (male aPatrizierMann2, female aBauerinFrau) with parallel command
// tables (dword_527378 / dword_52749C).  byte_122F4A8 (gender) selects the table; up/down
// keys (78='N'=next, 74='J'=prev) step the index with wraparound; the chosen model arms
// dword_122F528 = command[index] + 1468 and copies the name to unk_122F4F5.
// ===========================================================================
inline constexpr int kPreviewKeyNext = 78; // 'N'
inline constexpr int kPreviewKeyPrev = 74; // 'J'
inline constexpr int kPreviewKeyConfirm = 28; // Enter
inline constexpr int kPreviewCommandBase = 1468; // dword_122F528 = command + 1468

inline constexpr int kPreviewMaleCount   = 8;
inline constexpr int kPreviewFemaleCount  = 7;

// The recovered male/female model-name tables (32-byte stride, first-byte-NUL terminated).
extern const char* const kPreviewMaleNames[kPreviewMaleCount];
extern const char* const kPreviewFemaleNames[kPreviewFemaleCount];
// The parallel command tables (dword_527378 / dword_52749C, the -1 sentinel dropped).
extern const int kPreviewMaleCommands[kPreviewMaleCount];
extern const int kPreviewFemaleCommands[kPreviewFemaleCount];

// gilde.exe 0x52b6b8 — number of models for the selected gender (byte_122F4A8: 0=male).
inline int Preview_ModelCount(int gender) {
    return gender ? kPreviewFemaleCount : kPreviewMaleCount;
}

// gilde.exe 0x52b6b8 — wrap a possibly-negative running index into [0,count).  The
// original does  v1 %= count;  if (v1 < 0) v1 = count-1;  (i.e. an underflow snaps to the
// last entry, not a true modulo of a large negative — matched faithfully here).
int Preview_WrapIndex(int index, int count);

// gilde.exe 0x52b6b8 — apply a key to the running index: kPreviewKeyNext => +1,
// kPreviewKeyPrev => -1, else unchanged.  Returns the new (un-wrapped) index.
int Preview_StepIndex(int index, int key);

// gilde.exe 0x52b6b8 — the model name + the armed command for (gender, wrapped index).
const char* Preview_ModelName(int gender, int wrappedIndex);
int         Preview_ModelCommand(int gender, int wrappedIndex); // dword_122F528 value

// ===========================================================================
// ChooseCharacterIntroVariant selection (0x52e4e0).
// ---------------------------------------------------------------------------
// Six radio child objects are created; the clicked object dword_62D22C is matched against
// the first five to set the variant byte (the sixth has no effect).  Modeled as a pure
// lookup over the six child-object ids and the clicked id.
// ===========================================================================
inline constexpr int kIntroVariantChildCount = 6;
// gilde.exe 0x52e4e0 — clicked child -> variant byte (dword_63C744 / byte_12335BA):
// childIds[0]->0, [1]->1, [2]->2, [3]->3, [4]->4; childIds[5] and any miss -> `prev`.
int IntroVariant_ByteForClick(const int* childIds, int clickedId, int prev);

// ===========================================================================
// The character-creation parameter block this flow fills (the 0x122F4A0 region + the
// dynasty/talent/portrait globals).  Layered on NewGameParams' identity block; this
// captures only the pieces THIS module owns.
// ===========================================================================
struct CharCreateParams {
    DynastyTable dynasty;                 // dword_122F258[6]
    int  paternalProfessionByte = 0;      // LOBYTE(dword_122F4A0)
    int  maternalProfessionByte = 0;      // BYTE1(dword_122F4A0)
    int  professionVariant = -1;          // HIBYTE(dword_122F4A0) (ComputeVariantIndex)
    std::uint8_t talents[kTalentCount] = {0,0,0,0,0}; // byte_122F4F0[5]
    int  introVariant = 0;                // byte_12335BA / dword_63C744
    int  gender = 0;                      // byte_122F4A8 (selects the portrait table)
    int  portraitIndex = 0;               // BuildCharacterPreviewScene running index
    std::string portraitModel;            // chosen model name (unk_122F4F5)
    int  startCommand = 0;                // dword_122F528 = command + 1468
    bool committed = false;               // dword_631614 set (v63/v52 == 1)
};

// gilde.exe 0x52b088 commit — fold a paternal/maternal code + five talents into `p`.
void CharCreate_CommitTalents(CharCreateParams& p, int paternalCode, int maternalCode,
                              const std::uint8_t talents[kTalentCount]);

// gilde.exe 0x52b6b8 commit — fold the chosen portrait (gender, wrapped index) into `p`
// (sets portraitModel + startCommand + committed).
void CharCreate_CommitPortrait(CharCreateParams& p, int gender, int wrappedIndex);

} // namespace guild::gui
